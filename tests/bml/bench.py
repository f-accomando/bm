#!/usr/bin/env python3
"""Benchmark & static instruction breakdown for .b ARMv6 backend (bmlc.c) vs GCC -O2 on QEMU arm1176."""
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "bml-bench")
os.makedirs(BUILD, exist_ok=True)
os.environ["QEMU_AUDIO_DRV"] = "none"

F32_KERNEL_SRC = """\
function step_vec4(n: i32, px: f32[], py: f32[], pz: f32[], vx: f32[], vy: f32[], vz: f32[], dt: f32): f32
  local energy: f32 = 0.0
  local damp: f32 = 0.995
  local gx: f32 = 0.01
  local gy: f32 = -0.02
  for i = 1, n do
    local nvx: f32 = (vx[i] + gx * dt) * damp
    local nvy: f32 = (vy[i] + gy * dt) * damp
    local nvz: f32 = vz[i] * damp
    vx[i] = nvx
    vy[i] = nvy
    vz[i] = nvz
    local x: f32 = px[i] + nvx * dt
    local y: f32 = py[i] + nvy * dt
    local z: f32 = pz[i] + nvz * dt
    px[i] = x
    py[i] = y
    pz[i] = z
    energy = energy + nvx * nvx + nvy * nvy + nvz * nvz
  end
  return energy
end
"""

HOST_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "bmlc.h"
#include "bmnative.h"

static void dump_stats(const char *label, const uint8_t *blob, size_t len) {
    uint32_t magic, nfn_p, ncode;
    memcpy(&magic, blob, 4);
    memcpy(&nfn_p, blob + 4, 4);
    memcpy(&ncode, blob + 8, 4);
    uint32_t nfn = nfn_p & 0xFFFFu;
    uint32_t nstruct = (nfn_p >> 16) & 0x7FFFu;
    size_t pos = 12 + nfn * (32 + 4 + 16 + 4) + nstruct * (32 + 4 + 16 + 16 * 16);
    if (magic != BMN_MAGIC || pos + (size_t)ncode * 4u > len) {
        fprintf(stderr, "bad blob for %s\n", label);
        exit(1);
    }
    uint32_t vmov_gp = 0, vldr_str = 0, vpush_pop = 0, vmla = 0, vfp_alu = 0;
    uint32_t ldr_str = 0, push_pop = 0, bl_cnt = 0, mla_cnt = 0;
    for (uint32_t i = 0; i < ncode; i++) {
        uint32_t w;
        memcpy(&w, blob + pos + i * 4u, 4);
        uint32_t top4 = (w >> 24) & 0xFu;
        uint32_t cp = (w >> 8) & 0xFu;
        if (top4 == 0xEu && cp == 10u) {
            if (w & 0x10u) {
                if ((w & 0x0FF00FF0u) != 0x0EF00A10u)
                    vmov_gp++;
            } else {
                vfp_alu++;
                if ((w & 0x0FB00050u) == 0x0E000000u)
                    vmla++;
            }
        } else if ((top4 == 0xCu || top4 == 0xDu) && cp == 10u) {
            if (((w >> 21) & 1u) != 0u && ((w >> 16) & 0xFu) == 13u)
                vpush_pop++;
            else if (((w >> 21) & 1u) == 0u)
                vldr_str++;
        } else if ((w & 0x0FFF0000u) == 0x092D0000u || (w & 0x0FFF0000u) == 0x08BD0000u) {
            push_pop++;
        } else if (((w >> 26) & 3u) == 1u) {
            ldr_str++;
        } else if (((w >> 24) & 0xFu) == 0xBu) {
            bl_cnt++;
        } else if ((w & 0x0FE000F0u) == 0x00200090u) {
            mla_cnt++;
        }
    }
    printf("static %-18s words=%-4u vmov_gp=%-3u vldr_str=%-3u vpush_pop=%-2u vfp_alu=%-3u vmla=%-2u ldr_str=%-3u push_pop=%-2u bl=%-2u mla=%-2u\n",
           label, ncode, vmov_gp, vldr_str, vpush_pop, vfp_alu, vmla, ldr_str, push_pop, bl_cnt, mla_cnt);
}

int main(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *label = argv[i];
        const char *path = argv[i + 1];
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *src = malloc((size_t)sz + 1);
        if (fread(src, 1, (size_t)sz, f) != (size_t)sz) { perror("fread"); return 1; }
        src[sz] = '\0';
        fclose(f);
        uint8_t *blob = NULL;
        size_t blen = 0;
        char err[256];
        if (bml_compile_native(src, (size_t)sz, &blob, &blen, err, sizeof err) != 0) {
            fprintf(stderr, "compile failed (%s): %s\n", path, err);
            return 1;
        }
        dump_stats(label, blob, blen);
        bmnative_mod *m = NULL;
        const char *why = "";
        if (bmnative_load(&m, blob, blen, &why) != BMN_OK) {
            fprintf(stderr, "bmnative_load failed (%s): %s\n", label, why);
            return 1;
        }
        if (strcmp(label, "darknight_enemy.b") == 0) {
            enum { DN_N = 17 };
            uint32_t x[DN_N], z[DN_N], vx[DN_N], vz[DN_N], alive[DN_N];
            int32_t out[DN_N * DN_N], counts[DN_N];
            for (int j = 0; j < DN_N; j++) {
                float fx = (float)((j * 7) % 13) * 0.35f, fz = (float)((j * 11) % 13) * 0.35f;
                float fvx = (j & 1) ? 0.03f : -0.03f, fvz = (j & 2) ? 0.025f : -0.025f;
                memcpy(&x[j], &fx, 4); memcpy(&z[j], &fz, 4);
                memcpy(&vx[j], &fvx, 4); memcpy(&vz[j], &fvz, 4);
                alive[j] = (j == 5) ? 0u : 1u; counts[j] = 0;
            }
            memset(out, 0, sizeof out);
            int fn_move = bmnative_find(m, "move_and_separate");
            bmnative_buf bufs[8]; memset(bufs, 0, sizeof bufs);
            bufs[1].data = x; bufs[1].nwords = DN_N;
            bufs[2].data = z; bufs[2].nwords = DN_N;
            bufs[3].data = vx; bufs[3].nwords = DN_N;
            bufs[4].data = vz; bufs[4].nwords = DN_N;
            bufs[5].data = alive; bufs[5].nwords = DN_N;
            bufs[6].data = (uint32_t *)out; bufs[6].nwords = DN_N * DN_N;
            bufs[7].data = (uint32_t *)counts; bufs[7].nwords = DN_N;
            int32_t args[8] = { DN_N, 0, 0, 0, 0, 0, 0, 0 }, r = 0;
            if (bmnative_call_buf(m, fn_move, args, bufs, 8, &r) != BMN_OK) {
                fprintf(stderr, "sim trap on darknight_enemy.b\n"); return 1;
            }
            printf("dyn    %-18s sim_insns_1call=%-6u ret=%d\n", label, bmnative_last_insns(), r);
        } else if (strcmp(label, "f32_loop.b") == 0) {
            enum { VN = 64 };
            uint32_t px[VN], py[VN], pz[VN], vx[VN], vy[VN], vz[VN];
            for (int j = 0; j < VN; j++) {
                float f0 = (float)j * 0.1f, f1 = (float)(VN - j) * 0.05f;
                float c5 = 0.5f, c2 = 0.2f, cm1 = -0.1f, c05 = 0.05f;
                memcpy(&px[j], &f0, 4); memcpy(&py[j], &f1, 4); memcpy(&pz[j], &c5, 4);
                memcpy(&vx[j], &c2, 4); memcpy(&vy[j], &cm1, 4); memcpy(&vz[j], &c05, 4);
            }
            int fn_vec = bmnative_find(m, "step_vec4");
            bmnative_buf bufs[8]; memset(bufs, 0, sizeof bufs);
            bufs[1].data = px; bufs[1].nwords = VN;
            bufs[2].data = py; bufs[2].nwords = VN;
            bufs[3].data = pz; bufs[3].nwords = VN;
            bufs[4].data = vx; bufs[4].nwords = VN;
            bufs[5].data = vy; bufs[5].nwords = VN;
            bufs[6].data = vz; bufs[6].nwords = VN;
            float dt = 0.016f; int32_t dti = 0, r = 0; memcpy(&dti, &dt, 4);
            int32_t args[8] = { VN, 0, 0, 0, 0, 0, 0, dti };
            if (bmnative_call_buf(m, fn_vec, args, bufs, 8, &r) != BMN_OK) {
                fprintf(stderr, "sim trap on f32_loop.b\n"); return 1;
            }
            float rf = 0.0f; memcpy(&rf, &r, 4);
            printf("dyn    %-18s sim_insns_1call=%-6u energy_x1000=%d\n", label, bmnative_last_insns(), (int)(rf * 1000.0f));
        }
        bmnative_free(m);
        FILE *cf = fopen(strcmp(label, "darknight_enemy.b") == 0 ? "build/bml-bench/dn.code.bin" : "build/bml-bench/f32.code.bin", "wb");
        if (cf) {
            uint32_t magic, nfn_p, ncode;
            memcpy(&magic, blob, 4); memcpy(&nfn_p, blob + 4, 4); memcpy(&ncode, blob + 8, 4);
            uint32_t nfn = nfn_p & 0xFFFFu;
            size_t hdr = 12;
            uint32_t nstruct = (nfn_p >> 16) & 0x7FFFu;
            size_t base_no_ro = 12 + nfn * 56 + ncode * 4 + nstruct * 308;
            if (blen > base_no_ro) hdr = 16;
            fwrite(blob + hdr + nfn * 56, 4, ncode, cf);
            fclose(cf);
        }
        free(blob);
        free(src);
    }
    return 0;
}
"""


def c_str_literal(s: str) -> str:
    b = s.encode("utf-8")
    return ", ".join(str(x) for x in b) + ", 0"


def main() -> int:
    arm_gcc = shutil.which("arm-none-eabi-gcc")
    qemu_arm = shutil.which("qemu-system-arm")
    host_cc = os.environ.get("CC", "gcc")

    f32_path = os.path.join(BUILD, "f32_loop.b")
    with open(f32_path, "w", encoding="utf-8") as f:
        f.write(F32_KERNEL_SRC)

    dn_path = os.path.join(HERE, "darknight_enemy.b")
    with open(dn_path, "r", encoding="utf-8") as f:
        dn_src = f.read()

    # 1. Host static instruction breakdown & bmnative_load check
    host_c = os.path.join(BUILD, "bench_static.c")
    host_bin = os.path.join(BUILD, "bench_static")
    with open(host_c, "w", encoding="utf-8") as f:
        f.write(HOST_C)
    subprocess.run(
        [
            host_cc, "-O2", f"-I{os.path.join(ROOT, 'src', 'bm')}",
            "-o", host_bin, host_c,
            os.path.join(ROOT, "src", "bm", "bmlc.c"),
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            "-lm",
        ],
        check=True,
    )
    stat_out = subprocess.check_output(
        [host_bin, "darknight_enemy.b", dn_path, "f32_loop.b", f32_path],
        text=True,
    )
    print(stat_out, end="")

    if not (arm_gcc and qemu_arm):
        print("bench-bml: arm-none-eabi-gcc or qemu-system-arm missing; QEMU bench skipped")
        return 0

    # 2. Bare-metal ARM1176 QEMU timing benchmark (.b via bml_compile_native vs GCC -O2)
    ld_path = os.path.join(BUILD, "bench.ld")
    with open(ld_path, "w", encoding="utf-8") as f:
        f.write(
            "ENTRY(_start)\nSECTIONS {\n  . = 0x00010000;\n"
            "  .text : { *(.text.boot) *(.text*) }\n"
            "  .rodata : { *(.rodata*) }\n"
            "  .data : { *(.data*) }\n"
            "  .bss : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }\n}\n"
        )

    qemu_c = os.path.join(BUILD, "bench_qemu.c")
    qemu_elf = os.path.join(BUILD, "bench_qemu.elf")
    qemu_code = f"""
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
#include "bmlc.h"
#include "bmnative.h"

extern uint8_t __bss_start[], __bss_end[];

#define UART0 ((volatile uint32_t *)0x101F1000u)

static inline uint32_t semi_clock_cs(void) {{
    register uint32_t r0 __asm__("r0") = 0x10u; /* SYS_CLOCK (centiseconds) */
    register uint32_t r1 __asm__("r1") = 0u;
    __asm__ volatile("svc 0x00123456" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}}

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}

static void put_line_kv(const char *k, int32_t v1, int32_t v2, int32_t v3) {{
    char buf[128];
    int p = 0;
    while (k[p] && p < 60) {{ buf[p] = k[p]; p++; }}
    buf[p++] = ' ';
    int32_t vals[3] = {{ v1, v2, v3 }};
    for (int i = 0; i < 3; i++) {{
        int32_t v = vals[i];
        char tmp[16];
        int k2 = 0;
        uint32_t uv = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
        if (v < 0) buf[p++] = '-';
        do {{ tmp[k2++] = (char)('0' + (uv % 10u)); uv /= 10u; }} while (uv);
        while (k2--) buf[p++] = tmp[k2];
        buf[p++] = (i == 2) ? '\\n' : ' ';
    }}
    buf[p] = '\\0';
    put_str(buf);
}}

static uint8_t heap_buf[2u * 1024u * 1024u];
static size_t heap_off = 0;

void *memcpy(void *dst, const void *src, size_t n) {{
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}}
void *memset(void *dst, int c, size_t n) {{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}}
int memcmp(const void *a, const void *b, size_t n) {{
    const uint8_t *p = a, *q = b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    return 0;
}}
int strcmp(const char *a, const char *b) {{
    while (*a && *a == *b) {{ a++; b++; }}
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}}
size_t strlen(const char *s) {{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}}
__attribute__((naked))
int setjmp(jmp_buf env) {{
    __asm__ volatile (
        "stmia r0, {{r4-r11, sp, lr}}\\n"
        "mov r0, #0\\n"
        "bx lr\\n"
    );
}}
__attribute__((naked))
void longjmp(jmp_buf env, int val) {{
    __asm__ volatile (
        "ldmia r0, {{r4-r11, sp, lr}}\\n"
        "movs r0, r1\\n"
        "moveq r0, #1\\n"
        "bx lr\\n"
    );
}}

void *malloc(size_t n) {{
    n = (n + 15u) & ~((size_t)15u);
    if (heap_off + n > sizeof heap_buf) return NULL;
    void *p = heap_buf + heap_off;
    heap_off += n;
    return p;
}}
void *calloc(size_t nm, size_t sz) {{
    size_t tot = nm * sz;
    void *p = malloc(tot);
    if (p) memset(p, 0, tot);
    return p;
}}
void free(void *p) {{ (void)p; }}

static inline uint32_t f2u(float f) {{ uint32_t u; memcpy(&u, &f, 4); return u; }}
static inline float u2f(uint32_t u) {{ float f; memcpy(&f, &u, 4); return f; }}

/* GCC -O2 reference kernels matching Lua/BML semantics */
__attribute__((noinline))
static float gcc_bounce_vel(float p, float v) {{
    if (p < 0.0f || p > 5.0f) return -v;
    return v;
}}

__attribute__((noinline))
static int32_t gcc_move_and_separate(int32_t n, float *x, float *z, float *vx, float *vz,
                                     const uint32_t *alive, int32_t *out, int32_t *counts) {{
    for (int32_t i = 0; i < n; i++) {{
        if (alive[i]) {{
            float nvx = gcc_bounce_vel(x[i], vx[i]);
            float nvz = gcc_bounce_vel(z[i], vz[i]);
            vx[i] = nvx;
            vz[i] = nvz;
            x[i] += nvx;
            z[i] += nvz;
        }}
    }}
    int32_t total = 0;
    for (int32_t self = 0; self < n; self++) {{
        int32_t count = 0;
        counts[self] = 0;
        if (alive[self]) {{
            float ex = x[self];
            float ez = z[self];
            for (int32_t i = 0; i < n; i++) {{
                if (i != self && alive[i]) {{
                    float dx = ex - x[i];
                    if (dx < 0.8f && dx > -0.8f) {{
                        float dz = ez - z[i];
                        float d2 = dx * dx + dz * dz;
                        if (d2 < 0.64f && d2 > 0.00001f) {{
                            out[self * n + count] = i + 1;
                            count++;
                        }}
                    }}
                }}
            }}
            counts[self] = count;
            total += count;
        }}
    }}
    return total;
}}

__attribute__((noinline))
static float gcc_step_vec4(int32_t n, float *px, float *py, float *pz,
                           float *vx, float *vy, float *vz, float dt) {{
    float energy = 0.0f;
    float damp = 0.995f, gx = 0.01f, gy = -0.02f;
    for (int32_t i = 0; i < n; i++) {{
        float nvx = (vx[i] + gx * dt) * damp;
        float nvy = (vy[i] + gy * dt) * damp;
        float nvz = vz[i] * damp;
        vx[i] = nvx; vy[i] = nvy; vz[i] = nvz;
        px[i] += nvx * dt;
        py[i] += nvy * dt;
        pz[i] += nvz * dt;
        energy += nvx * nvx + nvy * nvy + nvz * nvz;
    }}
    return energy;
}}

static const char src_dn[] = {{ {c_str_literal(dn_src)} }};
static const char src_f32[] = {{ {c_str_literal(F32_KERNEL_SRC)} }};

static bmnative_mod *compile_mod(const char *s, size_t n) {{
    uint8_t *blob = NULL; size_t blen = 0; char err[128];
    if (bml_compile_native(s, n, &blob, &blen, err, sizeof err) != 0) {{
        put_str("ERR compile\\n");
        return NULL;
    }}
    bmnative_mod *m = NULL; const char *why = "";
    if (bmnative_load(&m, blob, blen, &why) != BMN_OK) {{
        put_str("ERR load\\n");
        return NULL;
    }}
    return m;
}}

void c_main(void) {{
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    bmnative_mod *m_dn = compile_mod(src_dn, sizeof(src_dn) - 1u);
    bmnative_mod *m_f32 = compile_mod(src_f32, sizeof(src_f32) - 1u);
    if (!m_dn || !m_f32) return;

    enum {{ DN_N = 17, DN_FRAMES = 5000 }};
    uint32_t x[DN_N], z[DN_N], vx[DN_N], vz[DN_N], alive[DN_N];
    int32_t out[DN_N * DN_N], counts[DN_N];
    float gx[DN_N], gz[DN_N], gvx[DN_N], gvz[DN_N];

    for (int iter = 0; iter < 2; iter++) {{
        for (int i = 0; i < DN_N; i++) {{
            float fx = (float)((i * 7) % 13) * 0.35f;
            float fz = (float)((i * 11) % 13) * 0.35f;
            float fvx = ((i & 1) ? 0.03f : -0.03f);
            float fvz = ((i & 2) ? 0.025f : -0.025f);
            x[i] = f2u(fx); z[i] = f2u(fz); vx[i] = f2u(fvx); vz[i] = f2u(fvz);
            gx[i] = fx; gz[i] = fz; gvx[i] = fvx; gvz[i] = fvz;
            alive[i] = (i == 5) ? 0u : 1u;
            counts[i] = 0;
        }}
        int fn_move = bmnative_find(m_dn, "move_and_separate");
        int32_t a[8] = {{
            DN_N, (int32_t)(uintptr_t)x, (int32_t)(uintptr_t)z,
            (int32_t)(uintptr_t)vx, (int32_t)(uintptr_t)vz,
            (int32_t)(uintptr_t)alive, (int32_t)(uintptr_t)out, (int32_t)(uintptr_t)counts
        }};
        if (iter == 0) {{
            int32_t sum_bml = 0;
            uint32_t t0 = semi_clock_cs();
            for (int f = 0; f < DN_FRAMES; f++) {{
                int32_t r = 0;
                bmnative_call(m_dn, fn_move, a, 8, &r);
                sum_bml += r;
            }}
            uint32_t t1 = semi_clock_cs();
            put_line_kv("dn_5000_bml:", (int32_t)((t1 - t0) * 10u), sum_bml, (int32_t)(u2f(x[0]) * 1000.0f));
        }} else {{
            int32_t sum_gcc = 0;
            uint32_t t0 = semi_clock_cs();
            for (int f = 0; f < DN_FRAMES; f++) {{
                sum_gcc += gcc_move_and_separate(DN_N, gx, gz, gvx, gvz, alive, out, counts);
            }}
            uint32_t t1 = semi_clock_cs();
            put_line_kv("dn_5000_gcc:", (int32_t)((t1 - t0) * 10u), sum_gcc, (int32_t)(gx[0] * 1000.0f));
        }}
    }}

    enum {{ VN = 64, VITERS = 25000 }};
    uint32_t b_px[VN], b_py[VN], b_pz[VN], b_vx[VN], b_vy[VN], b_vz[VN];
    float    g_px[VN], g_py[VN], g_pz[VN], g_vx[VN], g_vy[VN], g_vz[VN];
    for (int i = 0; i < VN; i++) {{
        float f0 = (float)i * 0.1f, f1 = (float)(VN - i) * 0.05f;
        b_px[i] = f2u(f0); b_py[i] = f2u(f1); b_pz[i] = f2u(0.5f);
        b_vx[i] = f2u(0.2f); b_vy[i] = f2u(-0.1f); b_vz[i] = f2u(0.05f);
        g_px[i] = f0; g_py[i] = f1; g_pz[i] = 0.5f;
        g_vx[i] = 0.2f; g_vy[i] = -0.1f; g_vz[i] = 0.05f;
    }}
    int fn_vec = bmnative_find(m_f32, "step_vec4");
    int32_t va[8] = {{
        VN, (int32_t)(uintptr_t)b_px, (int32_t)(uintptr_t)b_py, (int32_t)(uintptr_t)b_pz,
        (int32_t)(uintptr_t)b_vx, (int32_t)(uintptr_t)b_vy, (int32_t)(uintptr_t)b_vz,
        (int32_t)f2u(0.016f)
    }};
    int32_t rb = 0;
    uint32_t t2 = semi_clock_cs();
    for (int it = 0; it < VITERS; it++)
        bmnative_call(m_f32, fn_vec, va, 8, &rb);
    uint32_t t3 = semi_clock_cs();
    put_line_kv("vec4_25k_bml:", (int32_t)((t3 - t2) * 10u), (int32_t)(u2f((uint32_t)rb) * 1000.0f), (int32_t)(u2f(b_px[7]) * 1000.0f));

    float rg = 0.0f;
    uint32_t t4 = semi_clock_cs();
    for (int it = 0; it < VITERS; it++)
        rg = gcc_step_vec4(VN, g_px, g_py, g_pz, g_vx, g_vy, g_vz, 0.016f);
    uint32_t t5 = semi_clock_cs();
    put_line_kv("vec4_25k_gcc:", (int32_t)((t5 - t4) * 10u), (int32_t)(rg * 1000.0f), (int32_t)(g_px[7] * 1000.0f));
}}

__attribute__((naked, section(".text.boot")))
void _start(void) {{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    with open(qemu_c, "w", encoding="utf-8") as f:
        f.write(qemu_code)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-mfpu=vfp", "-mfloat-abi=softfp",
            "-marm", "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'bm')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", ld_path, "-o", qemu_elf,
            qemu_c,
            os.path.join(ROOT, "src", "bm", "bmlc.c"),
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    q_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=60,
    )
    print(q_out, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
