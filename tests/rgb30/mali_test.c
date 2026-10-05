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
static struct {
    /* variants */
    int pd_stuck, soft_reset_dead, bad_job_type_bit, cores_dead;
    /* SoC */
    uint32_t gates;                     /* CRU_CLKGATE_CON2 (1: gated) */
    uint32_t pmu_off, pmu_req;          /* PD_GPU off; bus idle requested */
    /* GPU */
    uint32_t int_raw, int_mask, l2_ready, sh_ready, ti_ready, jm_config;
    uint32_t job_raw, js_status, js_head[2], js_aff[2], js_cfg;
    uint32_t mmu_raw, as_transtab[2], as_memattr[2], as_lock[2], as_transcfg[2], as_fault, as_faultaddr;
    uint64_t act_transtab;              /* after UPDATE */
    uint32_t act_memattr, mmu_on;
    int gpu_reads_off;                  /* GPU registers touched while off */
    int jobs_run;
} S;

static uint32_t now_us;
static uint32_t us(void) { return now_us += 7; }

static int gpu_on(void)
{
    return !S.pmu_off && !(S.gates & 9) && !S.pmu_req;
}

static uint8_t *pa_ptr(uint64_t pa, uint32_t n)
{
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

static void run_chain(void)
{
    S.js_status = 0x08;
    if ((S.js_cfg & 15) != 0 || !S.mmu_on) {
        S.js_status = 0x40;             /* JOB_CONFIG_FAULT: no address space */
        S.job_raw |= 1u << 17;
        return;
    }
    if (!S.js_aff[0] || (S.js_aff[0] & ~S.sh_ready)) {
        S.js_status = 0x44;             /* JOB_AFFINITY_FAULT */
        S.job_raw |= 1u << 17;
        return;
    }
    uint64_t job = S.js_head[0] | (uint64_t)S.js_head[1] << 32;
    uint32_t done[8] = { 0 };
    for (int n = 0; job && n < 64; n++) {
        uint8_t *h = va_ptr(job, 64, 0);
        if (!h) {
            S.js_status = 0x42;         /* JOB_READ_FAULT */
            S.job_raw |= 1u << 17;
            return;
        }
        const uint32_t w4 = rd32(h + 16), w5 = rd32(h + 20);
        const int is64 = w4 & 1, type = S.bad_job_type_bit ? (int)(w4 >> 2 & 0x3f) : (int)(w4 >> 1 & 0x7f);
        const unsigned index = w4 >> 16, dep1 = w5 & 0xffff, dep2 = w5 >> 16;
        if (!is64 || !index || index >= 256 || (dep1 && !(done[dep1 / 32] >> (dep1 % 32) & 1)) ||
            (dep2 && !(done[dep2 / 32] >> (dep2 % 32) & 1))) {
            S.js_status = 0x40;
            S.job_raw |= 1u << 17;
            return;
        }
        if (type == 2) {                /* WRITE_VALUE */
            const uint64_t addr = rd64(h + 32);
            const uint32_t t = rd32(h + 40);
            const uint64_t imm = rd64(h + 48);
            static const int size[8] = { 0, 8, 8, 4, 1, 2, 4, 8 };
            if (t < 3 || t > 7) {
                S.js_status = 0x58;     /* DATA_INVALID_FAULT */
                S.job_raw |= 1u << 17;
                return;
            }
            uint8_t *d = va_ptr(addr, (uint32_t)size[t], 1);
            if (!d) {
                S.js_status = 0x43;     /* JOB_WRITE_FAULT */
                S.job_raw |= 1u << 17;
                return;
            }
            const uint64_t v = t == 3 ? 0 : imm;
            memcpy(d, &v, (size_t)size[t]);
        } else if (type != 1 && type != 3) {
            S.js_status = 0x40;         /* only NULL, WRITE_VALUE, CACHE_FLUSH here */
            S.job_raw |= 1u << 17;
            return;
        }
        if (!(S.js_cfg & (1u << 15))) {     /* the exception status written back */
            const uint32_t st = 1;
            memcpy(h, &st, 4);
        }
        done[index / 32] |= 1u << (index % 32);
        S.jobs_run++;
        job = rd64(h + 24);
    }
    S.js_status = 0x01;
    S.job_raw |= 1u << 1;
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
    case 0x1800 + 0x80 + 0x24: return S.js_status;
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
    case 0x1800 + 0x80 + 0x40: S.js_head[0] = v; break;
    case 0x1800 + 0x80 + 0x44: S.js_head[1] = v; break;
    case 0x1800 + 0x80 + 0x50: S.js_aff[0] = v; break;
    case 0x1800 + 0x80 + 0x54: S.js_aff[1] = v; break;
    case 0x1800 + 0x80 + 0x58: S.js_cfg = v; break;
    case 0x1800 + 0x80 + 0x60: if (v == 1) run_chain(); break;
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

static char lines[4096];
static void log_line(const char *s)
{
    printf("  %s\n", s);
    strncat(lines, s, sizeof lines - strlen(lines) - 2);
    strcat(lines, "\n");
}

static int probe(mali_info_t *in)
{
    memset(mem, 0xee, MEM_SIZE);
    lines[0] = 0;
    S.gates = 9;                        /* gated */
    S.pmu_off = 1;
    S.pmu_req = 1;
    const mali_hw_t hw = { .rd = rd, .wr = wr, .us = us, .log = log_line, .supply_mv = supply_mv, .mem = mem, .mem_pa = MEM_PA,
                           .mem_size = MEM_SIZE, .map_pa = MAP_PA, .map_size = MAP_SIZE };
    return mali_probe(&hw, in);
}

int main(void)
{
    mem = malloc(MEM_SIZE);
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
    CHECK(S.jobs_run == 3 && in.js_status == 1 && in.job_header == 1, "jobs: %d, status %x, header %x",
          S.jobs_run, in.js_status, in.job_header);
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

    CHECK(!strcmp(mali_exception(0xc1), "TRANSLATION_FAULT") && !strcmp(mali_exception(0x43), "JOB_WRITE_FAULT"),
          "exception names");
    printf("mali: %d/%d checks passed\n", checks - failures, checks);
    free(mem);
    return failures ? 1 : 0;
}
