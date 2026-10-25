/*
 * The RGB30's Mali probe (src/rgb30/mali.c) on a simulated GPU: the
 * RK3566's CRU and PMU (the GPU's clocks and power domain), the Mali's
 * registers (identity, reset, power of the cores, address space 0, job
 * slot 1) and its job manager running WRITE_VALUE jobs through the Mali
 * LPAE page tables the driver writes. The simulated GPU refuses what the
 * real one would: a register of the GPU read while its power domain is
 * off, a job before the MMU or the cores, an unmapped address, a job
 * header that is not 64-bit.
 *   mali_test            the probe goes through (and the variants below)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb30/mali.h"
#include "rgb30/mali_variants.h"
#include "rgb30/mali_desc.h"
#include "rgb30/mali_shaders.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the kernel's ksnprintf: 32-bit arguments, as here */
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* --- the simulated SoC --- */

#define MEM_PA   0x3fc00000u
#define MEM_SIZE (4u << 20)
#define MAP_PA   0x3c000000u
#define MAP_SIZE (64u << 20)

static uint8_t *mem;                    /* MEM_PA .. MEM_PA + MEM_SIZE */
#define SCREEN_PA 0x3c000000u           /* a screen of 360 x 360 XRGB8888, pitch 1536 */
#define SCREEN_W  360
#define SCREEN_H  360
#define SCREEN_PITCH 1536
static uint8_t *screen;
static struct {
    /* variants */
    int pd_stuck, soft_reset_dead, bad_job_type_bit, cores_dead, frag_no_write;
    uint32_t fail_runs;                 /* tri_mode: bit n: the n-th run of a vertex or tiler job fails (0: base, 1 + n: variant n) */
    int tri_runs, tri_mode;             /* those runs so far; the triangle probe is on */
    int accept_vt;                      /* vertex and tiler jobs (types 5, 7) complete as no-ops; a fragment job may carry a tiler */
    int fault_vt;                       /* the next vertex job faults (once) */
    int hold;                           /* bit n: a start on job slot n is kept back until release() */
    int held[2];
    /* SoC */
    uint32_t gates;                     /* CRU_CLKGATE_CON2 (1: gated) */
    uint32_t pmu_off, pmu_req;          /* PD_GPU off; bus idle requested */
    /* GPU */
    uint32_t int_raw, int_mask, l2_ready, sh_ready, ti_ready, jm_config;
    uint32_t job_raw, js_status[2], js_head[2][2], js_aff[2][2], js_cfg[2];
    uint32_t mmu_raw, as_transtab[2], as_memattr[2], as_lock[2], as_transcfg[2], as_fault, as_faultaddr;
    uint64_t act_transtab;              /* after UPDATE */
    uint32_t act_memattr, mmu_on;
    int gpu_reads_off;                  /* GPU registers touched while off */
    int jobs_run, frags_run;
} S;

static uint32_t now_us;
static uint32_t us(void) { return now_us += 7; }

static int gpu_on(void)
{
    return !S.pmu_off && !(S.gates & 9) && !S.pmu_req;
}

static uint8_t *pa_ptr(uint64_t pa, uint32_t n)
{
    if (pa >= SCREEN_PA && pa + n <= (uint64_t)SCREEN_PA + SCREEN_PITCH * SCREEN_H)
        return screen + (pa - SCREEN_PA);
    if (pa < MEM_PA || pa + n > (uint64_t)MEM_PA + MEM_SIZE)
        return NULL;
    return mem + (pa - MEM_PA);
}

static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* the Mali LPAE walk: 4 levels from TRANSTAB; 0 and *pa, or a fault code */
static uint32_t walk(uint64_t va, uint64_t *pa, int write)
{
    if (!S.mmu_on || (S.act_transtab & 3) != 3)
        return 0xc7;                    /* not mapped through tables */
    uint64_t table = S.act_transtab & ~0xfffull;
    for (int lvl = 0; lvl < 4; lvl++) {
        const unsigned idx = (unsigned)(va >> (39 - 9 * lvl)) & 511;
        const uint8_t *e = pa_ptr(table + 8 * idx, 8);
        if (!e)
            return 0xd0 | (uint32_t)lvl;    /* the table outside memory */
        const uint64_t d = rd64(e);
        if ((d & 3) == 3 && lvl < 3) {
            table = d & 0xfffffffff000ull;
            continue;
        }
        if ((d & 3) != 1 || lvl == 0)
            return 0xc0 | (uint32_t)lvl;    /* translation fault */
        if (!(d & (1u << 6)) || (write && !(d & (2u << 6))))
            return 0xc8 | (uint32_t)lvl;    /* permission */
        if ((d >> 2 & 7) > 2)
            return 0xe8 | (uint32_t)lvl;    /* an attribute index not set */
        const unsigned shift = (unsigned)(39 - 9 * lvl);
        *pa = (d & 0xfffffffff000ull & ~((1ull << shift) - 1)) | (va & ((1ull << shift) - 1));
        return 0;
    }
    return 0xc3;
}

static uint8_t *va_ptr(uint64_t va, uint32_t n, int write)
{
    uint64_t pa;
    const uint32_t f = walk(va, &pa, write);
    if (f) {
        S.as_fault = f | (write ? 3u << 8 : 2u << 8);
        S.as_faultaddr = (uint32_t)va;
        S.mmu_raw |= 1;
        return NULL;
    }
    uint8_t *p = pa_ptr(pa, n);
    if (!p) {
        S.mmu_raw |= 1u << 16;          /* a bus error */
        return NULL;
    }
    return p;
}

static void fault(int js, uint32_t code)
{
    S.js_status[js] = code;
    S.job_raw |= 1u << (16 + js);
}

/* a FRAGMENT job with no draw (tiler 0): the tiles of its bounds cleared
 * and written back through the render target; 0, or a fault */
static uint32_t fragment(const uint8_t *h)
{
    const uint32_t b0 = rd32(h + 32), b1 = rd32(h + 36);
    const uint64_t fp = rd64(h + 40);
    if (!(fp & 1) || (fp >> 1 & 1) || (fp >> 2 & 15))
        return 0x40;                    /* only a multi-target framebuffer of 1 target here */
    const uint8_t *f = va_ptr(fp & ~63ull, 192, 0);
    if (!f)
        return 0x42;
    const uint32_t w11 = rd32(f + 44);
    if (rd32(f) & 0x1ff || !rd64(f + 16) || !va_ptr(rd64(f + 16), 256, 0) || (w11 >> 9 & 15) > 8 ||
        (w11 >> 9 & 15) < 4 || ((w11 >> 24) & 255) * 1024u < 4u << (w11 >> 9 & 15) || (w11 >> 19 & 15) ||
        (rd64(f + 56) && !S.accept_vt))
        return 0x40;                    /* frame shaders, no sample locations, tile size, colour room, tiler */
    const uint32_t fw = (rd32(f + 32) & 0xffff) + 1, fh = (rd32(f + 32) >> 16) + 1;
    const uint8_t *rt = f + 128;
    const uint32_t r0 = rd32(rt), r1 = rd32(rt + 4);
    if ((r0 >> 26) != 1 || !(r1 & 1) || (r1 >> 3 & 31) != 19 || (r1 >> 8 & 15) != 2)
        return 0x58;                    /* only R8G8B8A8 into linear R8G8B8A8 here */
    if (!(r1 >> 31) || S.frag_no_write)
        return 0;                       /* clean tiles not written: nothing changes */
    const uint64_t base = rd64(rt + 32);
    const uint32_t stride = rd32(rt + 40), swz = r1 >> 16 & 0xfff, clear = rd32(rt + 48);
    const unsigned tx0 = b0 & 0xfff, ty0 = b0 >> 16 & 0xfff, tx1 = b1 & 0xfff, ty1 = b1 >> 16 & 0xfff;
    uint8_t px[4];
    for (int i = 0; i < 4; i++) {
        const unsigned sel = swz >> (3 * i) & 7;
        px[i] = sel < 4 ? (uint8_t)(clear >> (8 * sel)) : sel == 4 ? 0 : 255;
    }
    for (unsigned y = ty0 * 16; y < (ty1 + 1) * 16 && y < fh; y++)
        for (unsigned x = tx0 * 16; x < (tx1 + 1) * 16 && x < fw; x++) {
            uint8_t *d = va_ptr(base + (uint64_t)y * stride + x * 4, 4, 1);
            if (!d)
                return 0x43;
            memcpy(d, px, 4);
        }
    if (S.tri_mode && rd64(f + 56)) {   /* the tiled triangle: one pixel inside it (the sim has no rasterizer) */
        static const uint8_t orange[4] = { 0x19, 0x80, 0xff, 0 };
        uint8_t *d = va_ptr(base + 61 * (uint64_t)stride + 48 * 4, 4, 1);
        if (!d)
            return 0x43;
        memcpy(d, orange, 4);
    }
    S.frags_run++;
    return 0;
}

static void run_chain(int js)
{
    S.js_status[js] = 0x08;
    if ((S.js_cfg[js] & 15) != 0 || !S.mmu_on) {
        fault(js, 0x40);                /* JOB_CONFIG_FAULT: no address space */
        return;
    }
    if (!S.js_aff[js][0] || (S.js_aff[js][0] & ~S.sh_ready)) {
        fault(js, 0x44);                /* JOB_AFFINITY_FAULT */
        return;
    }
    uint64_t job = S.js_head[js][0] | (uint64_t)S.js_head[js][1] << 32;
    {
        const uint8_t *h0 = va_ptr(job, 64, 0);
        const int t0 = h0 ? (int)(rd32(h0 + 16) >> 1 & 0x7f) : 0;
        if (S.tri_mode && js == 1 && (t0 == 5 || t0 == 7)) {  /* a run of the triangle's jobs */
            const int run = S.tri_runs++;
            if (S.fail_runs >> run & 1) {
                fault(js, 0x58);        /* DATA_INVALID_FAULT */
                return;
            }
        }
    }
    uint32_t done[8] = { 0 };
    for (int n = 0; job && n < 64; n++) {
        uint8_t *h = va_ptr(job, 64, 0);
        if (!h) {
            fault(js, 0x42);            /* JOB_READ_FAULT */
            return;
        }
        const uint32_t w4 = rd32(h + 16), w5 = rd32(h + 20);
        const int is64 = w4 & 1, type = S.bad_job_type_bit ? (int)(w4 >> 2 & 0x3f) : (int)(w4 >> 1 & 0x7f);
        const unsigned index = w4 >> 16, dep1 = w5 & 0xffff, dep2 = w5 >> 16;
        if (!is64 || !index || index >= 256 || (dep1 && !(done[dep1 / 32] >> (dep1 % 32) & 1)) ||
            (dep2 && !(done[dep2 / 32] >> (dep2 % 32) & 1)) || ((type == 9) != (js == 0))) {
            fault(js, 0x40);            /* fragment jobs on slot 0 only, the others not there */
            return;
        }
        if (type == 2) {                /* WRITE_VALUE */
            const uint64_t addr = rd64(h + 32);
            const uint32_t t = rd32(h + 40);
            const uint64_t imm = rd64(h + 48);
            static const int size[8] = { 0, 8, 8, 4, 1, 2, 4, 8 };
            if (t < 3 || t > 7) {
                fault(js, 0x58);        /* DATA_INVALID_FAULT */
                return;
            }
            uint8_t *d = va_ptr(addr, (uint32_t)size[t], 1);
            if (!d) {
                fault(js, 0x43);        /* JOB_WRITE_FAULT */
                return;
            }
            const uint64_t v = t == 3 ? 0 : imm;
            memcpy(d, &v, (size_t)size[t]);
        } else if (type == 9) {
            const uint32_t f = fragment(h);
            if (f) {
                fault(js, f);
                return;
            }
        } else if (S.accept_vt && (type == 5 || type == 7)) {
            if (S.fault_vt && type == 5) {
                S.fault_vt = 0;
                fault(js, 0x58);        /* DATA_INVALID_FAULT */
                return;
            }
        } else if (type != 1 && type != 3) {
            fault(js, 0x40);            /* only NULL, WRITE_VALUE, CACHE_FLUSH, FRAGMENT here */
            return;
        }
        if (!(S.js_cfg[js] & (1u << 15))) { /* the exception status written back */
            const uint32_t st = 1;
            memcpy(h, &st, 4);
        }
        done[index / 32] |= 1u << (index % 32);
        S.jobs_run++;
        job = rd64(h + 24);
    }
    S.js_status[js] = 0x01;
    S.job_raw |= 1u << js;
}

/* a start on a slot: at once, or kept back while S.hold has the slot's bit */
static void start_chain(int js)
{
    if (S.hold >> js & 1) {
        S.js_status[js] = 0x08;         /* running */
        S.held[js] = 1;
        return;
    }
    run_chain(js);
}

/* lets the held slots in mask go: the chains they were asked to run run now */
static void release(int mask)
{
    S.hold &= ~mask;
    for (int js = 0; js < 2; js++)
        if (mask >> js & 1 && S.held[js]) {
            S.held[js] = 0;
            run_chain(js);
        }
}

static uint32_t rd(uintptr_t a)
{
    if (a >= MALI_CRU_BASE && a < MALI_CRU_BASE + 0x1000) {
        if (a == MALI_CRU_BASE + 0x308) return S.gates;
        if (a == MALI_CRU_BASE + 0x118) return 0x0101;     /* gpll / 2 */
        return 0;
    }
    if (a >= MALI_PMU_BASE && a < MALI_PMU_BASE + 0x1000) {
        switch (a - MALI_PMU_BASE) {
        case 0x98: return S.pmu_off;
        case 0x60: case 0x68: return S.pmu_req ? 2 : 0;
        default: return 0;
        }
    }
    if (a < MALI_GPU_BASE || a >= MALI_GPU_BASE + 0x4000) {
        CHECK(0, "a read of %08lx", (unsigned long)a);
        return 0;
    }
    if (!gpu_on()) {
        S.gpu_reads_off++;
        return 0;
    }
    const uint32_t r = (uint32_t)(a - MALI_GPU_BASE);
    switch (r) {
    case 0x000: return 0x74021000u;
    case 0x004: return 0x07120206u;
    case 0x014: return 0x00002830u;     /* 48-bit VA, 40-bit PA */
    case 0x018: return 0xff;
    case 0x01c: return 0x7;
    case 0x020: return S.int_raw;
    case 0x0a0: return 384;
    case 0x100: return 0x1;
    case 0x110: return 0x1;
    case 0x120: return 0x1;
    case 0x140: return S.sh_ready;
    case 0x150: return S.ti_ready;
    case 0x160: return S.l2_ready;
    case 0x1000: return S.job_raw;
    case 0x2000: return S.mmu_raw;
    case 0x1800 + 0x24: return S.js_status[0];
    case 0x1800 + 0x80 + 0x24: return S.js_status[1];
    case 0x2400 + 0x1c: return S.as_fault;
    case 0x2400 + 0x20: return S.as_faultaddr;
    case 0x2400 + 0x28: return 0;       /* never busy */
    default: return 0;
    }
}

static void wr(uintptr_t a, uint32_t v)
{
    if (a == MALI_CRU_BASE + 0x308) {
        const uint32_t m = v >> 16;
        S.gates = (S.gates & ~m) | (v & m);
        return;
    }
    if (a == MALI_PMU_BASE + 0xa0) {
        if (v >> 16 & 1 && !S.pd_stuck)
            S.pmu_off = v & 1;
        return;
    }
    if (a == MALI_PMU_BASE + 0x50) {
        if (v >> 16 & 2)
            S.pmu_req = (v & 2) ? 1 : 0;
        return;
    }
    if (a < MALI_GPU_BASE || a >= MALI_GPU_BASE + 0x4000) {
        CHECK(0, "a write of %08lx", (unsigned long)a);
        return;
    }
    if (!gpu_on()) {
        S.gpu_reads_off++;
        return;
    }
    const uint32_t r = (uint32_t)(a - MALI_GPU_BASE);
    switch (r) {
    case 0x024: S.int_raw &= ~v; break;
    case 0x028: S.int_mask = v; break;
    case 0x030:
        if (v == 1 && !S.soft_reset_dead)
            S.int_raw |= 1u << 8;
        if (v == 2)
            S.int_raw |= 1u << 8;
        if (v == 1 || v == 2) {
            S.l2_ready = S.sh_ready = S.ti_ready = 0;
            S.mmu_on = 0;
        }
        break;
    case 0x180: if (S.l2_ready && !S.cores_dead) S.sh_ready = v & 1; break;
    case 0x190: if (S.l2_ready) S.ti_ready = v & 1; break;
    case 0x1a0: S.l2_ready = v & 1; break;
    case 0xf00: S.jm_config = v; break;
    case 0x1004: S.job_raw &= ~v; break;
    case 0x2004: S.mmu_raw &= ~v; break;
    case 0x1800 + 0x40: case 0x1800 + 0x80 + 0x40: S.js_head[r >= 0x1880][0] = v; break;
    case 0x1800 + 0x44: case 0x1800 + 0x80 + 0x44: S.js_head[r >= 0x1880][1] = v; break;
    case 0x1800 + 0x50: case 0x1800 + 0x80 + 0x50: S.js_aff[r >= 0x1880][0] = v; break;
    case 0x1800 + 0x54: case 0x1800 + 0x80 + 0x54: S.js_aff[r >= 0x1880][1] = v; break;
    case 0x1800 + 0x58: case 0x1800 + 0x80 + 0x58: S.js_cfg[r >= 0x1880] = v; break;
    case 0x1800 + 0x60: case 0x1800 + 0x80 + 0x60: if (v == 1) start_chain(r >= 0x1880); break;
    case 0x2400 + 0x00: S.as_transtab[0] = v; break;
    case 0x2400 + 0x04: S.as_transtab[1] = v; break;
    case 0x2400 + 0x08: S.as_memattr[0] = v; break;
    case 0x2400 + 0x0c: S.as_memattr[1] = v; break;
    case 0x2400 + 0x10: S.as_lock[0] = v; break;
    case 0x2400 + 0x14: S.as_lock[1] = v; break;
    case 0x2400 + 0x30: S.as_transcfg[0] = v; break;
    case 0x2400 + 0x34: S.as_transcfg[1] = v; break;
    case 0x2400 + 0x18:
        if (v == 1) {                   /* UPDATE */
            S.act_transtab = S.as_transtab[0] | (uint64_t)S.as_transtab[1] << 32;
            S.act_memattr = S.as_memattr[0];
            S.mmu_on = (S.as_transcfg[0] & 15) == 0;    /* legacy mode: the tables */
        }
        break;
    default: break;
    }
}

static int supply = 900;
static int supply_mv(void) { return supply; }

static char lines[16384];
static void log_line(const char *s)
{
    printf("  %s\n", s);
    strncat(lines, s, sizeof lines - strlen(lines) - 2);
    strcat(lines, "\n");
}

static mali_hw_t G;                     /* the hw of the last probe (mali_frame wants the same one) */

static int tri_on;                      /* the probe also draws the triangle */

static int probe(mali_info_t *in)
{
    memset(mem, 0xee, MEM_SIZE);
    lines[0] = 0;
    S.gates = 9;                        /* gated */
    S.pmu_off = 1;
    S.pmu_req = 1;
    memset(screen, 0x10, SCREEN_PITCH * SCREEN_H);
    G = (mali_hw_t){ .rd = rd, .wr = wr, .us = us, .log = log_line, .supply_mv = supply_mv, .mem = mem,
                           .mem_pa = MEM_PA, .mem_size = MEM_SIZE, .map_pa = MAP_PA, .map_size = MAP_SIZE,
                           .screen = screen, .screen_pa = SCREEN_PA, .screen_w = SCREEN_W, .screen_h = SCREEN_H,
                           .screen_pitch = SCREEN_PITCH, .triangle = tri_on };
    return mali_probe(&G, in);
}

/* --- the triangle's variants (src/rgb30/mali_variants.h) --- */

/* a pointer field: 0 (if zero_ok) or inside the GPU's memory, aligned */
static int ptr_ok(uint64_t v, unsigned align, int zero_ok)
{
    if (!v)
        return zero_ok;
    return v >= MEM_PA && v < (uint64_t)MEM_PA + MEM_SIZE && !(v % align);
}

/* the pointers of the vertex job, the tiler job, the heap and the tables in a built triangle */
static int pointers_ok(const uint8_t *b)
{
    int ok = ptr_ok(rd64(b + MV_VJOB + 24), 8, 1) && ptr_ok(rd64(b + MV_TJOB + 24), 8, 1);
    static const int words[] = { 4, 6, 12, 14, 16, 18, 20, 22, 24, 28 };   /* a Draw's addresses */
    for (int d = 0; d < 2; d++)
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
            const uint64_t v = rd64(b + (d ? MV_TJOB + 128 : MV_VJOB + 64) + 4 * words[i]);
            ok = ok && ptr_ok(v, 8, 1) && (words[i] != 14 || ptr_ok(v, 64, 1));    /* a Renderer State: 64-aligned */
        }
    const uint64_t hbase = rd64(b + MV_HEAPD + 8), hbot = rd64(b + MV_HEAPD + 16), htop = rd64(b + MV_HEAPD + 24);
    ok = ok && hbase == (uint64_t)MEM_PA + MV_HEAP && hbot >= hbase && hbot < htop && !(hbot % 64) &&
         htop == hbase + HEAP_SIZE && ptr_ok(rd64(b + MV_TCTX + 24), 64, 0);
    for (int e = 0; e < 3; e++) {                                   /* varying buffers: the pointer is shifted by 6 */
        const uint64_t v = (rd64(b + MV_VBUFS + 16 * e) >> 6 & ((1ull << 50) - 1)) << 6;
        ok = ok && ptr_ok(v, 64, 1);
    }
    return ok;
}

static void variant_checks(void)
{
    uint8_t *base = calloc(1, MEM_SIZE), *v = calloc(1, MEM_SIZE), *w = calloc(1, MEM_SIZE);
    mali_hw_t hw = { .mem = base, .mem_pa = MEM_PA, .mem_size = MEM_SIZE, .map_pa = MAP_PA, .map_size = MAP_SIZE,
                     .screen = screen, .screen_pa = SCREEN_PA, .screen_w = SCREEN_W, .screen_h = SCREEN_H,
                     .screen_pitch = SCREEN_PITCH, .triangle = 1 };
    mali_tri_build(&hw);
    CHECK(pointers_ok(base), "the base triangle's pointers");
    for (int a = 0; a < MV_COUNT; a++) {
        CHECK(mv_table[a].name && mv_table[a].name[0], "variant %d has a name", a);
        for (int b = a + 1; b < MV_COUNT; b++)
            CHECK(strcmp(mv_table[a].name, mv_table[b].name), "variants %d and %d have the name %s", a, b,
                  mv_table[a].name);
    }
    for (int n = 0; n < MV_COUNT; n++) {
        const struct mv_patch *t = &mv_table[n];
        memcpy(v, base, MEM_SIZE);
        mv_apply(v, MEM_PA, n);
        const unsigned lo = t->block * 8u + t->bit, hi = lo + t->nbits;
        int diff = 0, outside = 0;
        for (size_t i = 0; i < MEM_SIZE; i++)
            if (v[i] != base[i])
                for (unsigned k = 0; k < 8; k++)
                    if ((v[i] ^ base[i]) >> k & 1) {
                        const unsigned ab = (unsigned)i * 8 + k;
                        diff++;
                        outside += ab < lo || ab >= hi;
                    }
        CHECK(diff > 0, "variant %d %s changes nothing", n, t->name);
        CHECK(!outside, "variant %d %s changes %d bits outside [%u, %u)", n, t->name, outside, lo, hi);
        CHECK(pointers_ok(v), "variant %d %s: a pointer is outside the memory or misaligned", n, t->name);
        memcpy(w, v, MEM_SIZE);
        mv_apply(v, MEM_PA, n);
        CHECK(!memcmp(v, w, MEM_SIZE), "variant %d %s: a second mv_apply changes the bytes", n, t->name);
    }
    free(base);
    free(v);
    free(w);
}

/* the probe with the triangle on, the runs of the vertex and tiler jobs in `fail` failing; the log has the variants' lines */
static int tri_probe(uint32_t fail, mali_info_t *in)
{
    memset(&S, 0, sizeof S);
    S.fail_runs = fail;
    S.tri_mode = S.accept_vt = 1;
    tri_on = 1;
    const int r = probe(in);
    tri_on = 0;
    return r;
}

static int has_variant(int n, const char *what)
{
    char key[96];
    snprintf(key, sizeof key, "mali: variant %d %s: %s", n, mv_table[n].name, what);
    return strstr(lines, key) != NULL;
}

static uint32_t word(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint64_t word64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

/* mali_frame: the memory image the jobs are built from (the simulated GPU has no tiler, so what it draws is not checked) */
static void test_frame_image(void)
{
    /* the layout of mali.c: draw d at MEM_DRAWS + 0x800 d, its Vertex job at +0, its Tiler job at +0x100 */
    const uint32_t DRAWS = MALI_FRAME_DRAWS, DSIZE = 0x800, TJ = 0x100;
    static const float t0[3][3] = { { 4, 4, 0.25f }, { 40, 4, 0.25f }, { 4, 40, 0.25f } };
    static const float t1[6][3] = { { 8, 8, 0.5f }, { 50, 8, 0.5f }, { 8, 50, 0.5f },
                                    { 10, 10, 0.75f }, { 20, 10, 0.75f }, { 10, 20, 0.75f } };
    static const float t2[3][3] = { { 30, 30, 0.125f }, { 60, 30, 0.125f }, { 30, 60, 0.125f } };
    const mali_draw_t d[3] = { { t0, 1, 0xff0000, NULL }, { t1, 2, 0x00ff00, NULL }, { t2, 1, 0x0000ff, NULL } };

    printf("mali: mali_frame, the memory image of three draws\n");
    memset(&S, 0, sizeof S);
    mali_info_t in;
    CHECK(probe(&in) == MALI_OK, "probe before the frame");
    const uint32_t surf_pa_off = 0x200000;              /* inside mem, 64-aligned, after the heap */
    uint8_t *surf = mem + surf_pa_off;
    const int r = mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0x102050, d, 3);
    (void)r;                                            /* the simulator has no tiler: the result is not asked */

    int bad = 0;                                        /* (a) the vertices as (x y z 1) */
    uint32_t first = 0;
    for (int k = 0; k < 3; k++) {
        for (uint32_t i = 0; i < 3 * d[k].n; i++) {
            const float want[4] = { d[k].xyz[i][0], d[k].xyz[i][1], d[k].xyz[i][2], 1.0f };
            bad += memcmp(mem + MALI_FRAME_VERTS + 16 * (first + i), want, 16) != 0;
        }
        first += (3 * d[k].n + 3) & ~3u;
    }
    CHECK(bad == 0, "frame: %d of 12 vertices differ from (x y z 1)", bad);

    static const float col[3][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 } };      /* (b) the pushed colours */
    bad = 0;
    for (int k = 0; k < 3; k++)
        bad += memcmp(mem + MALI_FRAME_PUSH + 64 * k + 16, col[k], 16) != 0;
    CHECK(bad == 0, "frame: %d of 3 pushed colours differ", bad);

    uint8_t tmp[64];                                    /* (c) draw 1's vertex job carries 6 vertices; the job chain */
    memset(tmp, 0, sizeof tmp);
    md_invocation(tmp, 6);
    CHECK(memcmp(mem + DRAWS + DSIZE + 32, tmp, 8) == 0, "frame: draw 1's vertex job invocation is not md_invocation(6)");
    bad = 0;
    for (uint32_t k = 0; k < 3; k++) {
        const uint8_t *v = mem + DRAWS + DSIZE * k, *t = v + TJ;
        const uint32_t vidx = word(v + 16) >> 16, tidx = word(t + 16) >> 16;
        bad += vidx != 2 * k + 1 || word(v + 20) != (k ? 2 * k : 0);                    /* Vertex 2k+1, after Tiler 2k */
        bad += tidx != 2 * k + 2 || word(t + 20) != 2 * k + 1;                          /* Tiler 2k+2, after its Vertex */
        bad += word64(v + 24) != MEM_PA + DRAWS + DSIZE * k + TJ;                       /* Vertex -> its Tiler */
        bad += word64(t + 24) != (k < 2 ? MEM_PA + DRAWS + DSIZE * (k + 1) : 0);        /* Tiler -> next Vertex, or end */
    }
    CHECK(bad == 0, "frame: %d job-chain fields wrong (index, dependency, next)", bad);

    printf("mali: mali_frame refuses\n");
    const mali_draw_t none = { t0, 0, 0, NULL };
    mali_draw_t big[2] = { { t0, 1000, 0, NULL }, { t0, 25, 0, NULL } };       /* 1025 triangles (the vertices are not read: refused first) */
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, d, 0) == -1, "frame: 0 draws refused");
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, &none, 1) == -1, "frame: a draw of 0 refused");
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 60, 64, 0, d, 3) == -1, "frame: w = 60 refused");
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, big, 2) == -1, "frame: 1025 triangles refused");
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off + 4, 256, 64, 64, 0, d, 3) == -1, "frame: unaligned target_pa refused");
    memset(&S, 0, sizeof S);
    S.pd_stuck = 1;                                                 /* a probe that fails: no frame after it */
    CHECK(probe(&in) == MALI_E_POWER, "probe fails (power)");
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, d, 3) == -1, "frame: refused before a good probe");
    memset(&S, 0, sizeof S);
    CHECK(probe(&in) == MALI_OK, "probe again");
    mali_hw_t other = G;                                            /* another hw than the probed one */
    CHECK(mali_frame(&other, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, d, 3) == -1, "frame: another hw refused");
}

/* phase 3: a draw with a matrix runs BM_VS3 with the matrix pushed; a draw without is the phase-2 draw */
static void test_matrix_image(void)
{
    const uint32_t SB = MALI_SET_BASE(0), DRAWS = MALI_FRAME_DRAWS, DSIZE = 0x800, VRSD = 0xa00, DV_VRSD = 0x500, DV_VUBO = 0x480,
                   VS = 0x8000, VS3 = 0x8400;                     /* the layout of mali.c (SO_*, D_*) */
    static const float t0[3][3] = { { 4, 4, 0.25f }, { 40, 4, 0.25f }, { 4, 40, 0.25f } };
    static const float t1[3][3] = { { 8, 8, 0.5f }, { 50, 8, 0.5f }, { 8, 50, 0.5f } };
    static const float m[16] = { 2, 0, 0, 10, 0, 3, 0, 20, 0, 0, 1, 0, 0, 0, 0, 1 };
    const mali_draw_t both[2] = { { t0, 1, 0xff0000, m }, { t1, 1, 0x00ff00, NULL } };
    const mali_draw_t plain[2] = { { t0, 1, 0xff0000, NULL }, { t1, 1, 0x00ff00, NULL } };

    printf("mali: mali_frame, a draw with a matrix\n");
    memset(&S, 0, sizeof S);
    mali_info_t in;
    CHECK(probe(&in) == MALI_OK, "probe before the matrix frame");
    const uint32_t surf_pa_off = 0x200000;
    uint8_t *surf = mem + surf_pa_off;
    memset(mem + SB, 0, 0x90000);
    (void)mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0x102050, both, 2);

    CHECK(memcmp(mem + MALI_FRAME_VPUSH, m, sizeof m) == 0, "matrix: the 16 floats at MALI_FRAME_VPUSH differ from the matrix");

    uint8_t tmp[128];                                               /* (b) the Renderer States and the vertex Draws */
    md_renderer_state_t v3 = { .shader = MEM_PA + SB + VS3, .attribute_count = BM_VS3_ATTRIBUTE_COUNT,
                               .varying_count = BM_VS3_VARYING_COUNT, .ubo_count = BM_VS3_UBO_COUNT,
                               .work_regs = BM_VS3_WORK_REGS, .preload_regs = BM_VS3_PRELOAD, .push_count = BM_VS3_PUSH_COUNT };
    memset(tmp, 0, sizeof tmp);
    md_renderer_state(tmp, &v3);
    CHECK(memcmp(mem + DRAWS + DV_VRSD, tmp, MD_RENDERER_STATE_SIZE) == 0, "matrix: draw 0's Renderer State is not BM_VS3's");
    CHECK(memcmp(mem + SB + VS3, BM_VS3_bin, sizeof BM_VS3_bin) == 0, "matrix: BM_VS3 is not at its place");
    md_renderer_state_t v1 = { .shader = MEM_PA + SB + VS, .attribute_count = BM_VS_ATTRIBUTE_COUNT,
                               .varying_count = BM_VS_VARYING_COUNT, .ubo_count = BM_VS_UBO_COUNT,
                               .work_regs = BM_VS_WORK_REGS, .preload_regs = BM_VS_PRELOAD, .push_count = BM_VS_PUSH_COUNT };
    memset(tmp, 0, sizeof tmp);
    md_renderer_state(tmp, &v1);
    CHECK(memcmp(mem + SB + VRSD, tmp, MD_RENDERER_STATE_SIZE) == 0, "matrix: the shared Renderer State is not BM_VS's");

    const uint64_t d0 = MEM_PA + DRAWS, d1 = d0 + DSIZE;
    md_draw_t vd0 = { .state = d0 + DV_VRSD, .attribute_buffers = d0 + 0x380, .attributes = MEM_PA + SB + 0xbc0,
                      .varying_buffers = d0 + 0x3c0, .varyings = MEM_PA + SB + 0xc40, .thread_storage = MEM_PA + SB + 0xb00,
                      .uniform_buffers = d0 + DV_VUBO, .push_uniforms = MEM_PA + MALI_FRAME_VPUSH };
    memset(tmp, 0, sizeof tmp);
    md_draw(tmp, &vd0);
    CHECK(memcmp(mem + DRAWS + MD_VJ_DRAW, tmp, MD_DRAW_SIZE) == 0, "matrix: draw 0's vertex Draw (state, uniform buffer, push) differs");
    md_draw_t vd1 = { .state = MEM_PA + SB + VRSD, .attribute_buffers = d1 + 0x380, .attributes = MEM_PA + SB + 0xbc0,
                      .varying_buffers = d1 + 0x3c0, .varyings = MEM_PA + SB + 0xc40, .thread_storage = MEM_PA + SB + 0xb00 };
    memset(tmp, 0, sizeof tmp);
    md_draw(tmp, &vd1);
    CHECK(memcmp(mem + DRAWS + DSIZE + MD_VJ_DRAW, tmp, MD_DRAW_SIZE) == 0, "matrix: draw 1's vertex Draw is not the phase-2 one");
    uint8_t ub[16] = { 0 };
    md_uniform_buffer(ub, MEM_PA + MALI_FRAME_VPUSH, 4);
    CHECK(memcmp(mem + DRAWS + DV_VUBO, ub, 8) == 0, "matrix: draw 0's uniform buffer descriptor differs");

    memset(mem + SB, 0, 0x90000);                                   /* (c) no matrix: BM_VS3 stays out, the phase-2 state */
    (void)mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0x102050, plain, 2);
    int nz = 0;
    for (uint32_t i = 0; i < sizeof BM_VS3_bin; i++)
        nz += mem[SB + VS3 + i] != 0;
    CHECK(nz == 0, "matrix: a frame without a matrix wrote %d bytes of BM_VS3", nz);
    memset(tmp, 0, sizeof tmp);
    md_renderer_state(tmp, &v1);
    CHECK(memcmp(mem + SB + VRSD, tmp, MD_RENDERER_STATE_SIZE) == 0, "matrix: the plain frame's Renderer State changed");

    mali_draw_t bad[1] = { { t0, 1025, 0, m } };                    /* (d) the limit holds with a matrix */
    CHECK(mali_frame(&G, surf, MEM_PA + surf_pa_off, 256, 64, 64, 0, bad, 1) == -1, "matrix: 1025 triangles with a matrix refused");
}

/* frames in flight: two sets, submit / busy / wait, on the simulator with vertex and tiler jobs accepted as no-ops */
static void test_async(void)
{
    static const float t0[3][3] = { { 4, 4, 0.25f }, { 40, 4, 0.25f }, { 4, 40, 0.25f } };
    const mali_draw_t d = { t0, 1, 0xff0000, NULL };
    mali_info_t in;
    uint8_t *s0 = NULL, *s1 = NULL, *s2 = NULL;
    uint32_t p0 = 0, p1 = 0, p2 = 0;

    printf("mali: frames in flight (two sets)\n");
    memset(&S, 0, sizeof S);
    CHECK(probe(&in) == MALI_OK, "async: probe");
    S.accept_vt = 1;
    S.hold = 3;                                         /* both slots keep their start back */
    const int base_frags = S.frags_run;
    CHECK(mali_frame_busy() == 0, "async: nothing in flight at first");
    CHECK(mali_frame_submit(&G, 64, 64, 0x102050, &d, 1, &s0, &p0) == 0, "async: first submit");
    CHECK(mali_frame_submit(&G, 64, 64, 0x304050, &d, 1, &s1, &p1) == 0, "async: second submit");
    CHECK(mali_frame_busy() == 2, "async: two frames in flight (%d)", mali_frame_busy());
    CHECK(mali_frame_submit(&G, 64, 64, 0, &d, 1, &s2, &p2) == -1, "async: a third submit while both sets are busy");      /* (b) */
    CHECK(mali_frame_submit(&G, 400, 400, 0, &d, 1, &s2, &p2) == -1, "async: a 400 x 400 surface is too big for a set");  /* (c) */
    CHECK(s0 != s1 && s0 == mem + MALI_SET_BASE(0) + 0x100000 && s1 == mem + MALI_SET_BASE(1) + 0x100000,
          "async: the surfaces sit at the sets' +0x100000 (%p %p)", (void *)s0, (void *)s1);                                  /* (d) */
    CHECK(p0 == 256 && p1 == 256, "async: pitch %u %u", p0, p1);

    /* only set 0's vertex job is on slot 1; set 1 waits for the slot */
    CHECK(S.held[1] == 1 && S.held[0] == 0, "async: one chain on slot 1, none on slot 0 yet");
    /* let slot 1 go, keep slot 0 back: set 0's fragment job starts and stays; set 1's vertex work ends behind it */
    release(2);
    CHECK(mali_frame_busy() == 2 && S.held[0] == 1 && S.held[1] == 0, "async: set 0 on slot 0, set 1's vertex jobs ran");
    CHECK(mali_frame_busy() == 2 && S.frags_run == base_frags, "async: set 1 waits for slot 0 (fragment jobs run: %d)",
          S.frags_run - base_frags);
    release(1);                                         /* slot 0 free: set 0 ends, set 1's fragment job starts and ends */
    CHECK(mali_frame_wait(1000000) == 0, "async: wait ends");                                                                  /* (a) */
    CHECK(S.frags_run == base_frags + 2 && mali_frame_busy() == 0, "async: two fragment jobs ran (%d)",
          S.frags_run - base_frags);
    CHECK((word(s0) & 0xffffff) == 0x102050 && (word(s1) & 0xffffff) == 0x304050,
          "async: each surface holds its own clear (%06x %06x)", word(s0) & 0xffffff, word(s1) & 0xffffff);

    /* the synchronous frame is refused while a frame flies, and works after */
    S.hold = 3;
    CHECK(mali_frame_submit(&G, 64, 64, 0, &d, 1, &s2, &p2) == 0, "async: submit again after the wait");
    CHECK(mali_frame(&G, mem + 0x200000, MEM_PA + 0x200000, 256, 64, 64, 0, &d, 1) == -1,
          "async: mali_frame refused while a frame flies");
    release(3);
    CHECK(mali_frame_wait(1000000) == 0, "async: that frame ends");

    /* (e) a job that faults */
    S.fault_vt = 1;
    CHECK(mali_frame_submit(&G, 64, 64, 0, &d, 1, &s2, &p2) == 0, "async: submit that will fault");
    CHECK(mali_frame_wait(1000000) == -1 && strstr(lines, "the vertex and tiler jobs of a queued frame: failed"),
          "async: a fault: wait says -1");
    CHECK(mali_frame_busy() == 0, "async: nothing flies after the fault");
    CHECK(mali_frame_submit(&G, 64, 64, 0x405060, &d, 1, &s2, &p2) == 0 && mali_frame_wait(1000000) == 0 &&
          (word(s2) & 0xffffff) == 0x405060, "async: the next frame works and is drawn");

    /* a frame that never ends: wait times out */
    S.hold = 3;
    CHECK(mali_frame_submit(&G, 64, 64, 0, &d, 1, &s2, &p2) == 0 && mali_frame_wait(1000) == -1,
          "async: wait times out while the GPU holds the frame");
    release(3);
    CHECK(mali_frame_wait(1000000) == 0, "async: and the frame ends after the release");
    CHECK(mali_frame(&G, mem + 0x200000, MEM_PA + 0x200000, 256, 64, 64, 0, &d, 1) >= 0,
          "async: mali_frame works again when none flies");
}

/* the r3d backend against a mock of mali_frame: which draws it makes, in which order, and what it copies into the page */
static int m_calls, m_nd, m_draws[8], m_rgb[8], m_total, m_firsts[8], m_nfirst;

static int mock_frame(const mali_hw_t *hw, uint8_t *target, uint64_t target_pa, uint32_t pitch, uint32_t w, uint32_t h,
                      uint32_t clear, const mali_draw_t *d, uint32_t nd)
{
    (void)hw; (void)target_pa;
    m_calls++;
    m_nd = (int)nd;
    m_total = 0;
    m_nfirst = 0;
    for (uint32_t k = 0; k < nd; k++) {
        if (k < 8) {
            m_draws[k] = (int)d[k].n;
            m_rgb[k] = (int)d[k].rgb;
        }
        for (uint32_t j = 0; j < d[k].n; j++) {
            if (m_nfirst < 8)
                m_firsts[m_nfirst++] = (int)d[k].xyz[3 * j][0];
            m_total++;
        }
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t v = (x >= 8 && x < 12 && y >= 8 && y < 12) ? 0x336699u :
                               (x >= 20 && x < 24 && y >= 8 && y < 12) ? 0xFFEE00u : clear;   /* above the key too */
            memcpy(target + (size_t)y * pitch + 4 * x, &v, 4);
        }
    return 0;
}

static void feed(const r3d_backend_t *be, const g16_t *g, float x, float r, float gg, float b, float z, int kind, int depth)
{
    r3d_corner_t c[3];
    memset(c, 0, sizeof c);
    for (int i = 0; i < 3; i++) {
        c[i].x = x + (float)i;
        c[i].y = 4;
        c[i].z = z;
        c[i].a = r;
        c[i].b = gg;
        c[i].c = b;
    }
    be->tri(be->ctx, g, c, kind, NULL, depth);
}

static void test_backend(void)
{
    static uint16_t page[64 * 64];
    printf("mali: the r3d backend (a mock of mali_frame)\n");
    memset(&S, 0, sizeof S);
    mali_info_t in;
    CHECK(probe(&in) == MALI_OK, "backend: probe");
    mali_hw_t other = G;
    CHECK(mali_backend(&other) == NULL, "backend: NULL for a hw that was not probed");
    const r3d_backend_t *be = mali_backend(&G);
    CHECK(be != NULL && be->colour_only == 1 && be->tri && be->zclear, "backend: after a probe, colour_only");
    if (!be)
        return;
    g16_t g = { .px = page, .stride = 64, .w = 64, .h = 64 };
    for (int i = 0; i < 64 * 64; i++)
        page[i] = 0xFFFF;
    mali_backend_set_frame(mock_frame);
    mali_backend_drop();
    m_calls = 0;

    feed(be, &g, 10, 1, 0, 0, 0.5f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);       /* A: red, z 0.5 */
    feed(be, &g, 20, 0, 1, 0, 0.1f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);       /* B: green, far */
    feed(be, &g, 30, 1, 0, 0, 0.3f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);       /* C: red, z 0.3 */
    feed(be, &g, 40, 0, 0, 1, 0.9f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);       /* D: blue, near */
    CHECK(mali_backend_pending() == 4, "backend: 4 waiting (%d)", mali_backend_pending());
    feed(be, &g, 50, 1, 1, 1, 0.5f, R3D_KIND_TEXTURE, R3D_DEPTH_WRITE);      /* not flat: ignored */
    feed(be, &g, 50, 1, 1, 1, 0.5f, R3D_KIND_COLOUR, R3D_DEPTH_TEST);        /* not depth-written: ignored */
    CHECK(mali_backend_pending() == 4, "backend: a texture and a tested triangle add nothing (%d)", mali_backend_pending());

    CHECK(mali_backend_flush(&g) == 0 && mali_backend_pending() == 0, "backend: flush");
    CHECK(m_calls == 1 && m_nd == 3 && m_draws[0] == 1 && m_draws[1] == 2 && m_draws[2] == 1,
          "backend: 3 draws of 1, 2, 1 triangles (%d: %d %d %d)", m_nd, m_draws[0], m_draws[1], m_draws[2]);
    CHECK(m_rgb[0] == 0x00FF00 && m_rgb[1] == 0xFF0000 && m_rgb[2] == 0x0000FF,
          "backend: green, red, blue far to near (%06x %06x %06x)", m_rgb[0], m_rgb[1], m_rgb[2]);
    CHECK(m_firsts[0] == 20 && m_firsts[1] == 30 && m_firsts[2] == 10 && m_firsts[3] == 40,
          "backend: B, C, A, D (%d %d %d %d)", m_firsts[0], m_firsts[1], m_firsts[2], m_firsts[3]);
    const uint16_t want = (uint16_t)((0x33 >> 3) << 11 | (0x66 >> 2) << 5 | (0x99 >> 3));
    const uint16_t want2 = (uint16_t)((0xFF >> 3) << 11 | (0xEE >> 2) << 5 | (0x00 >> 3));
    CHECK(page[8 * 64 + 20] == want2, "backend: a covered pixel above the key colour is %04x (want %04x)", page[8 * 64 + 20], want2);
    CHECK(page[8 * 64 + 8] == want && page[0] == 0xFFFF && page[63 * 64 + 63] == 0xFFFF,
          "backend: the covered pixel is %04x (want %04x), the others stay (%04x)", page[8 * 64 + 8], want, page[0]);

    feed(be, &g, 10, 1, 0, 1, 0.5f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);       /* exactly the key colour ff00ff */
    CHECK(mali_backend_flush(&g) == 0 && m_rgb[0] == 0xFF01FF, "backend: the key colour is nudged (%06x)", m_rgb[0]);

    m_calls = 0;
    for (int i = 0; i < 1025; i++)
        feed(be, &g, 4, 0, 1, 0, 0.5f, R3D_KIND_COLOUR, R3D_DEPTH_WRITE);
    CHECK(m_calls == 1 && m_total == 1024 && mali_backend_pending() == 1,
          "backend: the 1025th flushes 1024 (%d calls, %d drawn, %d waiting)", m_calls, m_total, mali_backend_pending());
    be->zclear(be->ctx, &g);
    CHECK(mali_backend_pending() == 0, "backend: zclear forgets what waits");
    mali_backend_set_frame(NULL);
}

int main(void)
{
    mem = malloc(MEM_SIZE);
    screen = malloc(SCREEN_PITCH * SCREEN_H);
    mali_info_t in;

    printf("mali: the probe on the simulated G52\n");
    memset(&S, 0, sizeof S);
    int r = probe(&in);
    CHECK(r == MALI_OK, "probe: %d", r);
    CHECK(S.gpu_reads_off == 0, "%d GPU registers touched while off", S.gpu_reads_off);
    CHECK(!(S.gates & 9) && !S.pmu_off && !S.pmu_req, "clocks, power domain, bus idle: %x %u %u", S.gates,
          S.pmu_off, S.pmu_req);
    CHECK(in.gpu_id == 0x74021000u && in.shader_present == 1 && strstr(lines, "Mali-G52 r1p0"), "identity");
    CHECK(S.jm_config == 0xf0000u, "JM_CONFIG %x", S.jm_config);
    CHECK(S.act_memattr == 0x888d88u && (S.act_transtab & 0xfff) == 7 && (S.act_transtab & ~0xfffull) == MEM_PA,
          "MMU: transtab %llx, memattr %x", (unsigned long long)S.act_transtab, S.act_memattr);
    CHECK(S.as_lock[0] == 63, "the whole space locked before the flush");
    CHECK(S.jobs_run == 5 && S.frags_run == 2 && in.js_status == 1 && in.job_header == 1,
          "jobs: %d (fragment %d), status %x, header %x", S.jobs_run, S.frags_run, in.js_status, in.job_header);
    CHECK(in.frag_pixel == 0x0020c060u, "the cleared surface: %08x (B G R in bytes 0-2)", in.frag_pixel);
    {
        /* the square at the top right of the screen, and nothing around it */
        const unsigned x0 = (SCREEN_W - 80) & ~15u, y0 = 16;
        int in_sq = 0, out_sq = 0;
        for (unsigned y = 0; y < SCREEN_H; y++)
            for (unsigned x = 0; x < SCREEN_W; x++) {
                uint32_t p;
                memcpy(&p, screen + y * SCREEN_PITCH + x * 4, 4);
                const int inside = x >= x0 && x < x0 + 64 && y >= y0 && y < y0 + 64;
                if (inside)
                    in_sq += p == 0x0040d060u;
                else
                    out_sq += p != 0x10101010u;
            }
        CHECK(in_sq == 64 * 64 && out_sq == 0, "the square: %d pixels in it, %d changed around it", in_sq, out_sq);
    }
    CHECK(strstr(lines, "a chain of two jobs: done"), "the chain");
    /* the framebuffer's memory mapped too (for the drawing to come) */
    uint64_t pa = 0;
    CHECK(walk(0x3c123456u, &pa, 1) == 0 && pa == 0x3c123456u, "the framebuffer mapped 1:1 (%llx)",
          (unsigned long long)pa);
    CHECK(walk(0x40000000u, &pa, 0) != 0 && walk(0x3bffffffu, &pa, 0) != 0, "nothing else mapped");

    CHECK(strstr(lines, "vdd_gpu 900 mV") != NULL, "the supply said");

    printf("mali: vdd_gpu off\n");
    memset(&S, 0, sizeof S);
    supply = 0;
    r = probe(&in);
    supply = 900;
    CHECK(r == MALI_E_SUPPLY && S.gpu_reads_off == 0 && S.gates == 9, "stopped before anything (%d)", r);

    printf("mali: the power domain does not turn on\n");
    memset(&S, 0, sizeof S);
    S.pd_stuck = 1;
    r = probe(&in);
    CHECK(r == MALI_E_POWER && S.gpu_reads_off == 0 && strstr(lines, "did not turn on"),
          "stopped at the power, nothing of the GPU touched (%d, %d)", r, S.gpu_reads_off);

    printf("mali: the soft reset does not end\n");
    memset(&S, 0, sizeof S);
    S.soft_reset_dead = 1;
    r = probe(&in);
    CHECK(r == MALI_OK && strstr(lines, "reset: hard"), "the hard reset instead (%d)", r);

    printf("mali: the shader cores do not power on\n");
    memset(&S, 0, sizeof S);
    S.cores_dead = 1;
    r = probe(&in);
    CHECK(r == MALI_E_CORES && strstr(lines, "the shader cores did not power on"), "cores (%d)", r);

    printf("mali: a GPU reading the job's type elsewhere (it sees a NULL job)\n");
    memset(&S, 0, sizeof S);
    S.bad_job_type_bit = 1;
    r = probe(&in);
    CHECK(r == MALI_E_MEMORY && strstr(lines, "but the word is 00000000"), "the word not written, said (%d)", r);

    printf("mali: clean tiles not written (the clear lost)\n");
    memset(&S, 0, sizeof S);
    S.frag_no_write = 1;
    r = probe(&in);
    CHECK(r == MALI_E_FRAGMENT && strstr(lines, "of 4096 pixels are not"), "the clear lost, said (%d)", r);

    test_frame_image();
    test_backend();
    test_matrix_image();
    test_async();

    printf("mali: the triangle's variants (the patches)\n");
    variant_checks();

    printf("mali: the triangle's base jobs work: no variant runs\n");
    r = tri_probe(0, &in);
    CHECK(r == MALI_OK && !strstr(lines, "variant") && S.tri_runs == 1 && in.tri_in == 0xff8019u &&
          in.tri_out == 0x102050u, "base path (%d, %d runs, %06x, %06x)", r, S.tri_runs, in.tri_in, in.tri_out);

    printf("mali: the triangle's base jobs fail, every variant works\n");
    r = tri_probe(1u << 0, &in);
    CHECK(r == MALI_OK && strstr(lines, "mali: variant base: JS_STATUS 0x58"), "base failed, said (%d)", r);
    for (int n = 0; n < MV_COUNT; n++)
        CHECK(has_variant(n, "ok"), "variant %d %s: ok said", n, mv_table[n].name);
    CHECK(strstr(lines, "mali: variant 2 heap-bottom64 goes on to the fragment job") && S.tri_runs == 10 &&
          in.tri_in == 0xff8019u, "the first non-isolating variant goes on (%d runs)", S.tri_runs);

    printf("mali: the triangle's base jobs and variants 2 and 3 fail\n");
    r = tri_probe(1u << 0 | 1u << 3 | 1u << 4, &in);
    CHECK(r == MALI_OK && has_variant(2, "JS_STATUS 0x58") && has_variant(3, "JS_STATUS 0x58") &&
          has_variant(0, "ok") && has_variant(1, "ok") && has_variant(4, "ok") && has_variant(7, "ok") &&
          strstr(lines, "mali: variant 4 mask-ff goes on to the fragment job"), "variant 4 is the one (%d)", r);

    printf("mali: every run of the triangle's jobs fails\n");
    r = tri_probe(0x1ffu, &in);
    CHECK(r == MALI_E_TRIANGLE && strstr(lines, "mali: variant base: JS_STATUS 0x58") &&
          strstr(lines, "mali: a triangle (vertex, tiler and fragment jobs): failed") && !strstr(lines, "goes on"),
          "all failed, said (%d)", r);
    for (int n = 0; n < MV_COUNT; n++)
        CHECK(has_variant(n, "JS_STATUS 0x58"), "variant %d %s: the status said", n, mv_table[n].name);

    printf("mali: only the two isolation runs work (variants 0 and 1)\n");
    r = tri_probe(0x1ffu & ~(1u << 1 | 1u << 2), &in);
    CHECK(r == MALI_E_TRIANGLE && has_variant(0, "ok") && has_variant(1, "ok") && !strstr(lines, "goes on"),
          "they never count as the one that works (%d)", r);

    CHECK(!strcmp(mali_exception(0xc1), "TRANSLATION_FAULT") && !strcmp(mali_exception(0x43), "JOB_WRITE_FAULT"),
          "exception names");
    printf("mali: %d/%d checks passed\n", checks - failures, checks);
    free(mem);
    free(screen);
    return failures ? 1 : 0;
}
