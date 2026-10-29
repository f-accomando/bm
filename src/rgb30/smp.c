/* The secondary cores: PSCI CPU_ON, then each core attaches to the page tables of core 0 and waits for a job. */
#include "smp.h"
#include "plat.h"
#include "drivers/timer.h"
#include "lib/printf.h"
#include "kernel/offload.h"
#include <string.h>

extern void smp_entry(void);

uint8_t smp_stacks[SMP_MAX][SMP_STACK] __attribute__((aligned(16)));
static volatile int core_state[SMP_MAX];

/* one job slot for each worker: fn is written last (release), cleared by the worker when done */
typedef struct {
    smp_fn volatile fn;
    void *volatile arg;
    volatile uint32_t done;
    volatile uint64_t fault[4];             /* type, esr, elr, far of a fault on the worker */
    volatile uint32_t faulted;
} slot_t;
static slot_t slot[SMP_MAX] __attribute__((aligned(64)));

void smp_secondary_main(uint64_t core)
{
    mmu_attach();
    __atomic_store_n(&core_state[core], 1, __ATOMIC_RELEASE);
    __asm__ volatile("dsb sy\n sev" ::: "memory");
    for (;;) {
        smp_fn fn = __atomic_load_n(&slot[core].fn, __ATOMIC_ACQUIRE);
        if (!fn) {
            __asm__ volatile("wfe");
            continue;
        }
        fn(slot[core].arg);
        __atomic_store_n(&slot[core].fn, (smp_fn)0, __ATOMIC_RELEASE);
        __atomic_store_n(&slot[core].done, 1, __ATOMIC_RELEASE);
        __asm__ volatile("dsb sy\n sev" ::: "memory");
    }
}

/* exc.c calls this on a core other than 0: record, park the core; core 0 reports it (smp_wait) */
void smp_worker_fault(uint64_t type, uint64_t esr, uint64_t elr, uint64_t far_)
{
    int c = smp_core_id();
    if (c < 1 || c >= SMP_MAX)
        for (;;)
            __asm__ volatile("wfe");
    slot[c].fault[0] = type; slot[c].fault[1] = esr; slot[c].fault[2] = elr; slot[c].fault[3] = far_;
    __atomic_store_n(&slot[c].faulted, 1, __ATOMIC_RELEASE);
    __asm__ volatile("dsb sy\n sev" ::: "memory");
    for (;;)
        __asm__ volatile("wfe");
}

int smp_core_state(int core)
{
    return core >= 0 && core < SMP_MAX ? core_state[core] : 0;
}

int smp_cores_up(void)
{
    int n = 1;
    for (int i = 1; i < SMP_MAX; i++)
        n += core_state[i] == 1;
    return n;
}

int smp_post(int core, smp_fn fn, void *arg)
{
    if (core < 1 || core >= SMP_MAX || core_state[core] != 1 || slot[core].fn)
        return -1;
    slot[core].arg = arg;
    slot[core].done = 0;
    __atomic_store_n(&slot[core].fn, fn, __ATOMIC_RELEASE);
    __asm__ volatile("dsb sy\n sev" ::: "memory");
    return 0;
}

int smp_poll(int core)
{
    if (core < 1 || core >= SMP_MAX)
        return 0;
    if (__atomic_load_n(&slot[core].faulted, __ATOMIC_ACQUIRE)) {
        if (core_state[core] == 1) {
            core_state[core] = SMP_FAULTED;
            kprintf("smp: core %d FAULT type %d esr %08lx elr %08lx far %08lx\n", core,
                    (int)slot[core].fault[0], (uint32_t)slot[core].fault[1],
                    (uint32_t)slot[core].fault[2], (uint32_t)slot[core].fault[3]);
        }
        return -1;
    }
    return __atomic_load_n(&slot[core].done, __ATOMIC_ACQUIRE);
}

int smp_wait(int core, uint32_t us)
{
    int r = 0;
    for (uint32_t t = 0; t <= us && !(r = smp_poll(core)); t += 10)
        timer_delay_us(10);
    if (r == 0 && core >= 1 && core < SMP_MAX && core_state[core] == 1) {
        core_state[core] = SMP_HUNG;                    /* the job did not end: the core is not used again */
        kprintf("smp: core %d HUNG (job over %lu us)\n", core, us);
    }
    return r;
}

int smp_start(int cores)
{
    core_state[0] = 1;
    for (int i = 1; i < SMP_MAX && i < cores; i++) {
        kprintf("smp: core %d: CPU_ON\n", i);
        long r = plat_cpu_on(PLAT_CORE_MPIDR(i), (uint64_t)(uintptr_t)smp_entry, (uint64_t)i);
        if (r != 0) {
            core_state[i] = (int)r;
            kprintf("smp: core %d: CPU_ON answered %d\n", i, (int)r);
            continue;
        }
        for (int t = 0; t < 100 && core_state[i] == 0; t++)
            timer_delay_us(1000);
        kprintf("smp: core %d %s\n", i, core_state[i] == 1 ? "up" : "did not start");
    }
    return smp_cores_up() - 1;
}

#define ST_ROUNDS 100000
static struct { volatile uint32_t atomic; spinlock_t lock; uint32_t plain; } st __attribute__((aligned(64)));

static void st_work(void *arg)
{
    (void)arg;
    for (int i = 0; i < ST_ROUNDS; i++) {
        __atomic_fetch_add(&st.atomic, 1, __ATOMIC_RELAXED);
        spin_lock(&st.lock);
        st.plain++;
        spin_unlock(&st.lock);
    }
}

int smp_selftest(void)
{
    int n = smp_cores_up();
    st.atomic = st.plain = 0;
    st.lock = 0;
    for (int c = 1; c < SMP_MAX; c++)
        if (core_state[c] == 1 && smp_post(c, st_work, 0) != 0)
            return -1;
    st_work(0);
    for (int c = 1; c < SMP_MAX; c++)
        if (core_state[c] == 1 && smp_wait(c, 2000000) != 1)
            return -2;
    uint32_t want = (uint32_t)n * ST_ROUNDS;
    int bad = st.atomic != want || st.plain != want;
    kprintf("smp: selftest %s (%d cores, %lu)\n", bad ? "FAILED" : "ok", n, st.atomic);
    return bad ? -3 : 0;
}

/* the fault tests: a division is not a fault on ARM, so the job reads an unmapped address; the other loops */
static void job_fault(void *arg) { (void)arg; *(volatile uint32_t *)0xf0000000u; }
static void job_loop(void *arg) { (void)arg; for (;;) __asm__ volatile("yield"); }

int smp_selftest_faults(void)
{
    if (smp_core_state(2) != 1 || smp_core_state(3) != 1)
        return -1;
    smp_post(2, job_fault, 0);
    int a = smp_wait(2, 200000);
    smp_post(3, job_loop, 0);
    int b = smp_wait(3, 50000);
    kprintf("smp: fault test %s (core 2 %d, core 3 %d)\n",
            a == -1 && b == 0 && smp_core_state(2) == SMP_FAULTED && smp_core_state(3) == SMP_HUNG ? "ok" : "FAILED", a, b);
    return 0;
}

/* kernel/offload.h: the block of each worker is a static copy, so a stopped caller leaves no pointer to its stack */
static volatile uint32_t offload_count;
int smp_offload_count(void) { return (int)offload_count; }

static uint8_t offload_blk[SMP_MAX][OFFLOAD_BLK] __attribute__((aligned(64)));

int offload_total(void)
{
    int n = 0;
    for (int c = 1; c < SMP_MAX; c++)
        n += core_state[c] == 1;
    return n;
}

int offload_core_ok(int core)
{
    return core >= 1 && core < SMP_MAX && core_state[core] == 1 && !slot[core].fn;
}

int offload_post_core(int core, void (*fn)(void *arg), void *arg)
{
    return smp_post(core, fn, arg);
}

int offload_poll_core(int core)
{
    return smp_poll(core);
}

void offload_abandon_core(int core)
{
    if (core >= 1 && core < SMP_MAX && core_state[core] == 1) {
        core_state[core] = SMP_HUNG;
        kprintf("smp: core %d HUNG (job)\n", core);
    }
}

int offload_workers(void)
{
    int n = 0;
    for (int c = 1; c < SMP_MAX; c++)
        if (core_state[c] == 1 && !slot[c].fn)
            n++;
    return n;
}

int offload_par(void (*fn)(void *blk), void *const *blks, unsigned n)
{
    int core[SMP_MAX] = { 0 };          /* the worker of block i, 0 for the caller */
    int c = 1, ran = 0;
    for (unsigned i = 1; i < n && i < SMP_MAX; i++) {
        while (c < SMP_MAX && (core_state[c] != 1 || slot[c].fn))
            c++;
        if (c >= SMP_MAX)
            break;
        if (smp_post(c, fn, blks[i]) == 0)
            core[i] = c;
        c++;
    }
    for (unsigned i = 0; i < n; i++)    /* block 0, and the blocks no worker took */
        if (i >= SMP_MAX || !core[i])
            fn(blks[i]);
    for (unsigned i = 1; i < n && i < SMP_MAX; i++) {
        if (!core[i])
            continue;
        int r;
        const uint32_t t0 = timer_ticks();      /* a tight poll: smp_wait sleeps 10 us a step, a band lasts about 20 us */
        while (!(r = smp_poll(core[i])) && timer_ticks() - t0 < 200000)
            __asm__ volatile("yield");
        if (r == 1) {
            ran++;
            continue;
        }
        if (smp_poll(core[i]) == 0 && core_state[core[i]] == 1) {
            core_state[core[i]] = SMP_HUNG;
            kprintf("smp: core %d HUNG (offload_par)\n", core[i]);
        }
        fn(blks[i]);                    /* the worker faulted or hung: the same block, again, here */
    }
    offload_count += ran;
    return ran;
}

int offload_run(void (*fn)(void *blk), void *blk, unsigned size, void (*idle)(void))
{
    if (size > OFFLOAD_BLK)
        return -1;
    for (int c = 1; c < SMP_MAX; c++) {
        if (core_state[c] != 1 || slot[c].fn)
            continue;
        memcpy(offload_blk[c], blk, size);
        if (smp_post(c, fn, offload_blk[c]) != 0)
            continue;
        int r = 0;
        for (uint32_t t = 0; t < 2000000 && !(r = smp_poll(c)); t += 100) {
            if (idle)
                idle();
            timer_delay_us(100);
        }
        if (r != 1) {                       /* faulted, or over 2 s: the core is lost, the caller runs fn itself */
            if (r == 0 && core_state[c] == 1) {
                core_state[c] = SMP_HUNG;
                kprintf("smp: core %d HUNG (offload)\n", c);
            }
            return -1;
        }
        memcpy(blk, offload_blk[c], size);
        offload_count++;
        return 0;
    }
    return -1;
}
