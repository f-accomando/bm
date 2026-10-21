/*
 * The RGB30's Mali probe (src/rgb30/mali.c, M41) on a simulated GPU: the
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
    uint32_t fail_runs;                 /* bit n: the n-th run of a vertex or tiler job fails (0: base, 1 + n: variant n) */
    int tri_runs, tiled;                /* those runs so far; a tiler job has run since the last reset */
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
        (rd64(f + 56) && !S.tiled))
        return 0x40;                    /* frame shaders, no sample locations, tile size, colour room, a tiler without a tiler job */
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
    if (rd64(f + 56)) {                 /* the tiled triangle: one pixel inside it (the sim has no rasterizer) */
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
        if (js == 1 && (t0 == 5 || t0 == 7)) {  /* a run of the triangle's jobs */
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
        } else if (type == 7) {
            S.tiled = 1;                /* a TILER job: the tiler's lists (not simulated) */
        } else if (type != 1 && type != 3 && type != 5) {
            fault(js, 0x40);            /* only NULL, WRITE_VALUE, CACHE_FLUSH, VERTEX, TILER, FRAGMENT here */
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
            S.tiled = 0;
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
    case 0x1800 + 0x60: case 0x1800 + 0x80 + 0x60: if (v == 1) run_chain(r >= 0x1880); break;
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

static int tri_on;                      /* the probe also draws the triangle */

static int probe(mali_info_t *in)
{
    memset(mem, 0xee, MEM_SIZE);
    lines[0] = 0;
    S.gates = 9;                        /* gated */
    S.pmu_off = 1;
    S.pmu_req = 1;
    memset(screen, 0x10, SCREEN_PITCH * SCREEN_H);
    const mali_hw_t hw = { .rd = rd, .wr = wr, .us = us, .log = log_line, .supply_mv = supply_mv, .mem = mem,
                           .mem_pa = MEM_PA, .mem_size = MEM_SIZE, .map_pa = MAP_PA, .map_size = MAP_SIZE,
                           .screen = screen, .screen_pa = SCREEN_PA, .screen_w = SCREEN_W, .screen_h = SCREEN_H,
                           .screen_pitch = SCREEN_PITCH, .triangle = tri_on };
    return mali_probe(&hw, in);
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
    int ok = ptr_ok(rd64(b + MEM_VJOB + 24), 8, 1) && ptr_ok(rd64(b + MEM_TJOB + 24), 8, 1);
    static const int words[] = { 4, 6, 12, 14, 16, 18, 20, 22, 24, 28 };   /* a Draw's addresses */
    for (int d = 0; d < 2; d++)
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
            const uint64_t v = rd64(b + (d ? MEM_TJOB + 128 : MEM_VJOB + 64) + 4 * words[i]);
            ok = ok && ptr_ok(v, 8, 1) && (words[i] != 14 || ptr_ok(v, 64, 1));    /* a Renderer State: 64-aligned */
        }
    const uint64_t hbase = rd64(b + MEM_HEAPD + 8), hbot = rd64(b + MEM_HEAPD + 16), htop = rd64(b + MEM_HEAPD + 24);
    ok = ok && hbase == (uint64_t)MEM_PA + MEM_HEAP && hbot >= hbase && hbot < htop && !(hbot % 64) &&
         htop == hbase + HEAP_SIZE && ptr_ok(rd64(b + MEM_TCTX + 24), 64, 0);
    for (int e = 0; e < 3; e++) {                                   /* varying buffers: the pointer is shifted by 6 */
        const uint64_t v = (rd64(b + MEM_VBUFS + 16 * e) >> 6 & ((1ull << 50) - 1)) << 6;
        ok = ok && ptr_ok(v, 64, 1);
    }
    return ok;
}

static void variant_checks(void)
{
    const mali_hw_t hw = { .mem = mem, .mem_pa = MEM_PA, .mem_size = MEM_SIZE, .map_pa = MAP_PA, .map_size = MAP_SIZE,
                           .screen = screen, .screen_pa = SCREEN_PA, .screen_w = SCREEN_W, .screen_h = SCREEN_H,
                           .screen_pitch = SCREEN_PITCH, .triangle = 1 };
    uint8_t *base = calloc(1, MEM_SIZE), *v = calloc(1, MEM_SIZE), *w = calloc(1, MEM_SIZE);
    mali_tri_build(&hw, base, MEM_PA);
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
