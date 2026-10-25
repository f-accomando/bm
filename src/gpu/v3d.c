#include "v3d.h"
#include "arch/cache.h"
#include "arch/mmu.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "kernel/pmu.h"
#include "audio/audio.h"
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
#define V3D_SRQPC       0x430           /* user program queue: program counter (the write queues it) */
#define V3D_SRQUA       0x434           /* uniforms address */
#define V3D_SRQUL       0x438           /* uniforms length */
#define V3D_SRQCS       0x43C           /* queue length 5:0, error 7, programs sent 15:8, done 23:16 */
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
#define SRQ_ERROR       (1u << 7)
#define SRQ_RESET       (1u << 7 | 1u << 8 | 1u << 16)  /* clear the error, the sent and the done counts */
#define SRQ_QLEN(cs)    ((cs) & 0x3Fu)
#define SRQ_DONE(cs)    ((cs) >> 16 & 0xFFu)

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

/* The V3D's registers as the vertex pipeline wants them: all of the VPM to
 * it (as Linux: no user programs), both control list threads stopped, counts
 * and interrupts cleared. v3d_init runs it, and v3d_qpu_run runs it again
 * after a QPU reset, as the reset may power-cycle the V3D. */
static void regs_init(void)
{
    wr(V3D_VPMBASE, 0);
    wr(V3D_INTDIS, 0xF);
    wr(V3D_INTCTL, 0xF);
    wr(V3D_CT0CS, CT_RESET);
    wr(V3D_CT1CS, CT_RESET);
    wr(V3D_BFC, 1);
    wr(V3D_RFC, 1);
    dmb();
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
    regs_init();
    status = "ready";
    ready = 1;
    return 0;
}

const char *v3d_status(void)
{
    return status;
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
volatile uint32_t v3d_wait_us;

/* the time of a wait from t0 to the dev kit's count, less the audio
 * interrupt's (a0: audio_busy_us at its start) */
static void wait_add(uint32_t t0, uint32_t a0)
{
    const uint32_t us = timer_ticks() - t0, au = audio_busy_us - a0;
    v3d_wait_us += us > au ? us - au : 0;
}

static int wait_count(uint32_t reg, uint32_t cs_reg, uint32_t timeout_us, uint32_t *us)
{
    uint32_t t0 = timer_ticks(), a0 = audio_busy_us;
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
            wait_add(t0, a0);
            wr(reg, 1);                /* write 1: back to 0 */
            dmb();
            return 0;
        }
        if ((cs & CT_ERROR) || timer_ticks() - t0 > timeout_us) {
            wait_add(t0, a0);
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

/* measurement only (v3d_last_cost): nothing below changes what a job does */
static uint32_t pool_bus, pool_size;
static v3d_cost_t cost;

void v3d_set_pool(uint32_t bus, uint32_t size)
{
    pool_bus = bus;
    pool_size = size;
}

void v3d_last_cost(v3d_cost_t *c)
{
    *c = cost;
}

/* dcache_clean_invalidate_all(), its cycles added to *cyc */
static void cache_all(uint32_t *cyc)
{
    pmu_t p0, p1;
    pmu_read(&p0);
    dcache_clean_invalidate_all();
    pmu_read(&p1);
    *cyc += p1.cycles - p0.cycles;
}

/* after a binning job: where the binner's pool pointer ended (BPCA). The
 * pool first, then the overflow (on the Pi they are contiguous) */
static void measure_bin(void)
{
    dmb();
    const uint32_t a = rd(V3D_BPCA);
    dmb();
    cost.pool_used = cost.ovf_used = cost.ovf = 0;
    if (pool_size && a >= pool_bus && a < pool_bus + pool_size) {
        cost.pool_used = a - pool_bus;
    } else if (overflow_size && a >= overflow_bus && a <= overflow_bus + overflow_size) {
        cost.pool_used = pool_size;
        cost.ovf_used = a - overflow_bus;
        cost.ovf = 1;
    }
}

int v3d_run(uint32_t bin, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end, uint32_t timeout_us,
            uint32_t *bin_us, uint32_t *rnd_us)
{
    if (!ready)
        return -1;
    if (bin_us) *bin_us = 0;
    if (rnd_us) *rnd_us = 0;
    memset(&cost, 0, sizeof cost);
    /* lists, vertices and shaders written by the ARM reach the L2, and no
     * line of a buffer the V3D writes stays in the data cache */
    cache_all(&cost.clean_cyc);
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
    if (bin_end != bin)
        measure_bin();
    if (err) {
        wr(V3D_CT0CS, CT_RESET);
        wr(V3D_CT1CS, CT_RESET);
        wr(V3D_BFC, 1);
        wr(V3D_RFC, 1);
        dmb();
    }
    /* what the V3D wrote is read from memory, not from stale lines */
    cache_all(&cost.inval_cyc);
    return err;
}

/* bm3d 4.1: a job started and not waited for. The binning list ends with
 * INCREMENT_SEMAPHORE and the rendering list waits on it before its first
 * tile, as Linux's vc4 does, so the two threads start together and the ARM
 * goes on. */
static struct {
    int busy, bin;
    uint32_t t0;
    int seen;                           /* its end seen by v3d_busy, at t_done */
    uint32_t t_done;
} job;

int v3d_start(uint32_t bin, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end)
{
    if (!ready || job.busy)
        return -1;
    memset(&cost, 0, sizeof cost);
    cache_all(&cost.clean_cyc);
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
    job.seen = 0;
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
    if (done && !job.seen) {
        job.seen = 1;
        job.t_done = timer_ticks();
    }
    return !done;
}

int v3d_wait(uint32_t timeout_us, uint32_t *bin_us, uint32_t *rnd_us)
{
    if (bin_us) *bin_us = 0;
    if (rnd_us) *rnd_us = 0;
    if (!job.busy)
        return 0;
    /* the job's time: from its start to the end the ARM saw, polling (here
     * or v3d_busy). One that had ended before anyone looked is not timed:
     * *rnd_us 0 and *bin_us the time from its start to this wait, at most
     * (it may hold a pause of the ARM's: a report, a menu) */
    dmb();
    const int unseen = !job.seen && (rd(V3D_RFC) & 0xFF) != 0;
    dmb();
    const uint32_t at = timer_ticks();
    job.busy = 0;
    int err = 0;
    uint32_t b = 0, r = 0;
    if (job.bin)
        err = wait_count(V3D_BFC, V3D_CT0CS, timeout_us, &b);
    if (!err)
        err = wait_count(V3D_RFC, V3D_CT1CS, timeout_us, &r);
    (void)b;
    (void)r;
    if (unseen) {
        if (bin_us) *bin_us = at - job.t0;
    } else if (rnd_us) {
        *rnd_us = (job.seen ? job.t_done : timer_ticks()) - job.t0;
    }
    if (job.bin)
        measure_bin();
    if (err) {
        wr(V3D_CT0CS, CT_RESET);
        wr(V3D_CT1CS, CT_RESET);
        wr(V3D_BFC, 1);
        wr(V3D_RFC, 1);
        dmb();
    }
    cache_all(&cost.inval_cyc);
    return err;
}

/* ------------------------------------------------------------ QPU programs */

static const char *qpu_why = "not run";
static volatile int qpu_lock;

const char *v3d_qpu_status(void)
{
    return qpu_why;
}

/* The V3D's QPUs: a queue of programs (code, uniforms address and length,
 * written in this order: the PC write queues the program). The counts of
 * sent and done programs are reset first; the VPM share is given for the
 * run only, as the vertex pipeline wants all of it. */
static int qpu_run(const v3d_qpu_job_t *jobs, int n, uint32_t timeout_us);

/* One run at a time. The kernel's fibers are cooperative and this function
 * never yields, so a second caller can only come from another core or an
 * interrupt: it gets -2 (nothing started) and tries again later. */
int v3d_qpu_run(const v3d_qpu_job_t *jobs, int n, uint32_t timeout_us)
{
    if (__atomic_exchange_n(&qpu_lock, 1, __ATOMIC_ACQUIRE)) {
        qpu_why = "another QPU run";
        return -2;
    }
    const int r = qpu_run(jobs, n, timeout_us);
    __atomic_store_n(&qpu_lock, 0, __ATOMIC_RELEASE);
    return r;
}

static int qpu_run(const v3d_qpu_job_t *jobs, int n, uint32_t timeout_us)
{
    if (!ready) {
        qpu_why = "no V3D";
        return -1;
    }
    if (job.busy) {
        qpu_why = "a 3D job is running";
        return -2;
    }
    if (n <= 0 || n > V3D_QPU_MAX) {
        qpu_why = "bad job count";
        return -1;
    }
    cache_all(&cost.clean_cyc);
    dmb();
    wr(V3D_L2CACTL, L2C_CLEAR);
    wr(V3D_SLCACTL, 0x0F0F0F0Fu);       /* the instruction and uniform caches may hold the last program */
    wr(V3D_SRQCS, SRQ_RESET);
    wr(V3D_VPMBASE, V3D_QPU_VPM);
    dmb();
    for (int i = 0; i < n; i++) {
        wr(V3D_SRQUL, jobs[i].nunif);
        wr(V3D_SRQUA, jobs[i].unif);
        wr(V3D_SRQPC, jobs[i].code);    /* the write queues the program */
    }
    dmb();
    const uint32_t t0 = timer_ticks(), a0 = audio_busy_us;
    int r = 0;
    for (;;) {
        dmb();
        const uint32_t cs = rd(V3D_SRQCS);
        dmb();
        if (cs & SRQ_ERROR) {
            qpu_why = "queue error";
            r = -1;
            break;
        }
        if (SRQ_DONE(cs) >= (uint32_t)n) {
            qpu_why = "ok";
            break;
        }
        if (timer_ticks() - t0 > timeout_us) {
            qpu_why = "timeout";
            r = -1;
            break;
        }
    }
    wait_add(t0, a0);
    if (r) {
        snapshot();
        uint32_t off[1] = { 0 }, on[1] = { 1 };
        prop_query(PROP_ENABLE_QPU, off, 1);    /* the firmware's reset of QPUs that do not end */
        prop_query(PROP_ENABLE_QPU, on, 1);
        timer_delay_ms(1);
        regs_init();                            /* the reset may have power-cycled the V3D */
    }
    wr(V3D_VPMBASE, 0);
    wr(V3D_SRQCS, SRQ_RESET);
    dmb();
    cache_all(&cost.inval_cyc);
    return r;
}

int v3d_uncached(void *p, uint32_t size, int on)
{
    return mmu_set_cached(p, size, !on);
}

void v3d_errors(char *buf, size_t n)
{
    dmb();
    ksnprintf(buf, n, "ERRSTAT %08lx DBGE %08lx FDBGO %08lx FDBGB %08lx FDBGR %08lx FDBGS %08lx", rd(V3D_ERRSTAT),
              rd(V3D_DBGE), rd(V3D_FDBGO), rd(V3D_FDBGB), rd(V3D_FDBGR), rd(V3D_FDBGS));
    dmb();
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
