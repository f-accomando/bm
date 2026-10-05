#include "v3d.h"
#include "arch/cache.h"
#include "arch/mmu.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "kernel/pmu.h"
#include "lib/printf.h"

#include <string.h>

#define V3D_BASE        (PERIPHERAL_BASE + 0xC00000)
#define V3D_IDENT0      0x000
#define V3D_IDENT1      0x004
#define V3D_IDENT2      0x008
#define V3D_L2CACTL     0x020
#define V3D_SLCACTL     0x024
#define V3D_INTCTL      0x030
#define V3D_INTENA      0x034
#define V3D_INTDIS      0x038
#define V3D_CT0CS       0x100
#define V3D_CT1CS       0x104
#define V3D_CT0EA       0x108
#define V3D_CT1EA       0x10C
#define V3D_CT0CA       0x110
#define V3D_CT1CA       0x114
#define V3D_PCS         0x130
#define V3D_BFC         0x134
#define V3D_RFC         0x138
#define V3D_BPCA        0x300
#define V3D_BPCS        0x304
#define V3D_BPOA        0x308
#define V3D_BPOS        0x30C
#define V3D_VPMBASE     0x504
#define V3D_DBGE        0xF00
#define V3D_FDBGO       0xF04
#define V3D_FDBGB       0xF08
#define V3D_FDBGR       0xF0C
#define V3D_FDBGS       0xF10
#define V3D_ERRSTAT     0xF20

#define IDENT0_V3D      0x02443356u     /* "V3D" and version 2 */
#define CT_RESET        (1u << 15)
#define CT_ERROR        (1u << 3)
#define L2C_CLEAR       (1u << 2)

#define PROP_ENABLE_QPU 0x00030012u

static int ready;
static const char *status = "not started";
static uint32_t ident[3];
static uint32_t snap[24];               /* registers when a job failed */
static int snapped;
static uint32_t overflow_bus, overflow_size;

static inline uint32_t rd(uint32_t reg)
{
    return mmio_read(V3D_BASE + reg);
}

static inline void wr(uint32_t reg, uint32_t v)
{
    mmio_write(V3D_BASE + reg, v);
}

static const struct { uint16_t reg; const char *name; } dump_regs[] = {
    { V3D_CT0CS, "CT0CS" }, { V3D_CT0CA, "CT0CA" }, { V3D_CT0EA, "CT0EA" },
    { V3D_CT1CS, "CT1CS" }, { V3D_CT1CA, "CT1CA" }, { V3D_CT1EA, "CT1EA" },
    { V3D_PCS, "PCS" }, { V3D_BFC, "BFC" }, { V3D_RFC, "RFC" }, { V3D_INTCTL, "INTCTL" },
    { V3D_BPCA, "BPCA" }, { V3D_BPCS, "BPCS" }, { V3D_BPOA, "BPOA" }, { V3D_BPOS, "BPOS" },
    { V3D_ERRSTAT, "ERRSTAT" }, { V3D_DBGE, "DBGE" }, { V3D_FDBGO, "FDBGO" },
    { V3D_FDBGB, "FDBGB" }, { V3D_FDBGR, "FDBGR" }, { V3D_FDBGS, "FDBGS" },
};
#define NDUMP (int)(sizeof dump_regs / sizeof *dump_regs)

static void snapshot(void)
{
    dmb();
    for (int i = 0; i < NDUMP; i++)
        snap[i] = rd(dump_regs[i].reg);
    dmb();
    snapped = 1;
}

int v3d_init(void)
{
    static int absent;                  /* asked once: QEMU has none */
    if (ready)
        return 0;
    if (absent)
        return -1;
    /* the firmware answers 0 when the QPUs are on (hello_fft); whatever
     * it says, the V3D decides by answering with its identity */
    uint32_t v[1] = { 1 };
    int known = prop_query(PROP_ENABLE_QPU, v, 1) == 0;
    timer_delay_ms(1);
    dmb();
    for (int i = 0; i < 3; i++)
        ident[i] = rd(V3D_IDENT0 + 4u * (uint32_t)i);
    dmb();
    if (ident[0] != IDENT0_V3D) {
        static char why[96];
        ksnprintf(why, sizeof why, "no V3D answers (IDENT0 %08lx, mailbox %s %lx; QEMU has none)",
                  ident[0], known ? "answer" : "error", v[0]);
        status = why;
        absent = 1;
        return -1;
    }
    /* all of the VPM to the vertex pipeline (as Linux: no user programs),
     * both control list threads stopped, counts and interrupts cleared */
    wr(V3D_VPMBASE, 0);
    wr(V3D_INTDIS, 0xF);
    wr(V3D_INTCTL, 0xF);
    wr(V3D_CT0CS, CT_RESET);
    wr(V3D_CT1CS, CT_RESET);
    wr(V3D_BFC, 1);
    wr(V3D_RFC, 1);
    dmb();
    status = "ready";
    ready = 1;
    return 0;
}

const char *v3d_status(void)
{
    return status;
}

uint32_t v3d_ident(int i)
{
    return i >= 0 && i < 3 ? ident[i] : 0;
}

uint32_t v3d_bus(const void *p)
{
    return ARM_TO_BUS(p);
}

/* Waits until the count register changes from 0: 0, or -1 after a control
 * list error or timeout_us. The binner's out-of-memory flag (PCS bit 8) is
 * no error: it is on from reset until the binner is first given overflow
 * memory, also during a job without binning (the Pi showed it while a
 * clear was still storing its tiles). A binner really out of memory
 * stalls, and the timeout catches it with the registers. */
static int wait_count(uint32_t reg, uint32_t cs_reg, uint32_t timeout_us, uint32_t *us)
{
    uint32_t t0 = timer_ticks();
    pmu_t p0;
    pmu_read(&p0);                      /* the 3D Bench: the instructions spent waiting */
    for (;;) {
        dmb();
        uint32_t n = rd(reg), cs = rd(cs_reg);
        dmb();
        if (n & 0xFF) {
            if (us)
                *us = timer_ticks() - t0;
            pmu_t p1;
            pmu_read(&p1);
            pmu_wait_instr += p1.instr - p0.instr;
            wr(reg, 1);                 /* write 1: back to 0 */
            dmb();
            return 0;
        }
        if ((cs & CT_ERROR) || timer_ticks() - t0 > timeout_us) {
            snapshot();
            return -1;
        }
    }
}

void v3d_set_overflow(uint32_t bus, uint32_t size)
{
    overflow_bus = bus;
    overflow_size = size;
}

int v3d_run(uint32_t bin, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end, uint32_t timeout_us,
            uint32_t *bin_us, uint32_t *rnd_us)
{
    if (!ready)
        return -1;
    if (bin_us) *bin_us = 0;
    if (rnd_us) *rnd_us = 0;
    /* lists, vertices and shaders written by the ARM reach the L2, and no
     * line of a buffer the V3D writes stays in the data cache */
    dcache_clean_invalidate_all();
    dmb();
    /* the V3D's own caches (L2, and per slice: instructions, uniforms,
     * textures) may hold the data of the previous job */
    wr(V3D_L2CACTL, L2C_CLEAR);
    wr(V3D_SLCACTL, 0x0F0F0F0Fu);
    wr(V3D_BFC, 1);
    wr(V3D_RFC, 1);
    dmb();
    int err = 0;
    if (bin_end != bin) {
        wr(V3D_BPOA, overflow_bus);
        wr(V3D_BPOS, overflow_size);
        wr(V3D_CT0CA, bin);
        wr(V3D_CT0EA, bin_end);         /* the end address starts the thread */
        dmb();
        err = wait_count(V3D_BFC, V3D_CT0CS, timeout_us, bin_us);
    }
    if (!err) {
        wr(V3D_CT1CA, rnd);
        wr(V3D_CT1EA, rnd_end);
        dmb();
        err = wait_count(V3D_RFC, V3D_CT1CS, timeout_us, rnd_us);
    }
    if (err) {
        wr(V3D_CT0CS, CT_RESET);
        wr(V3D_CT1CS, CT_RESET);
        wr(V3D_BFC, 1);
        wr(V3D_RFC, 1);
        dmb();
    }
    /* what the V3D wrote is read from memory, not from stale lines */
    dcache_clean_invalidate_all();
    return err;
}

/* M35: a job started and not waited for. The binning list ends with
 * INCREMENT_SEMAPHORE and the rendering list waits on it before its first
 * tile, as Linux's vc4 does, so the two threads start together and the ARM
 * goes on. */
static struct {
    int busy, bin;
    uint32_t t0;
} job;

int v3d_start(uint32_t bin, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end)
{
    if (!ready || job.busy)
        return -1;
    dcache_clean_invalidate_all();
    dmb();
    wr(V3D_L2CACTL, L2C_CLEAR);
    wr(V3D_SLCACTL, 0x0F0F0F0Fu);
    wr(V3D_BFC, 1);
    wr(V3D_RFC, 1);
    dmb();
    job.bin = bin_end != bin;
    if (job.bin) {
        wr(V3D_BPOA, overflow_bus);
        wr(V3D_BPOS, overflow_size);
        wr(V3D_CT0CA, bin);
        wr(V3D_CT0EA, bin_end);
    }
    wr(V3D_CT1CA, rnd);
    wr(V3D_CT1EA, rnd_end);             /* waits on the semaphore if there is binning */
    dmb();
    job.busy = 1;
    job.t0 = timer_ticks();
    return 0;
}

int v3d_busy(void)
{
    if (!job.busy)
        return 0;
    dmb();
    const int done = (rd(V3D_RFC) & 0xFF) != 0;
    dmb();
    return !done;
}

int v3d_wait(uint32_t timeout_us, uint32_t *bin_us, uint32_t *rnd_us)
{
    if (bin_us) *bin_us = 0;
    if (rnd_us) *rnd_us = 0;
    if (!job.busy)
        return 0;
    job.busy = 0;
    int err = 0;
    uint32_t b = 0, r = 0;
    if (job.bin)
        err = wait_count(V3D_BFC, V3D_CT0CS, timeout_us, &b);
    if (!err)
        err = wait_count(V3D_RFC, V3D_CT1CS, timeout_us, &r);
    /* the time from the start to the end the ARM saw (at most the job's:
     * it may have ended while the ARM was busy) */
    (void)b;
    (void)r;
    if (rnd_us) *rnd_us = timer_ticks() - job.t0;
    if (err) {
        wr(V3D_CT0CS, CT_RESET);
        wr(V3D_CT1CS, CT_RESET);
        wr(V3D_BFC, 1);
        wr(V3D_RFC, 1);
        dmb();
    }
    dcache_clean_invalidate_all();
    return err;
}

int v3d_uncached(void *p, uint32_t size, int on)
{
    return mmu_set_cached(p, size, !on);
}

void v3d_dump(char *buf, size_t n)
{
    if (!snapped)
        snapshot();
    size_t k = 0;
    buf[0] = 0;
    for (int i = 0; i < NDUMP && k + 24 < n; i++)
        k += (size_t)ksnprintf(buf + k, n - k, "%s%-7s %08lx", i % 4 ? "  " : (i ? "\n" : ""),
                               dump_regs[i].name, snap[i]);
    if (k + 2 < n) {
        buf[k++] = '\n';
        buf[k] = 0;
    }
}
