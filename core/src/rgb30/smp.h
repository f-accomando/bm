/* The secondary cores of the RGB30 (Cortex-A55 x4): docs/progress/multicore.md.
 * Core 0 owns the heap, the FAT, lwIP and Lua. A worker runs pure functions only (no malloc, no libc state, no device). */
#ifndef SMP_H
#define SMP_H
#include <stdint.h>

void mmu_attach(void);                  /* mmu.c: a secondary core joins core 0's tables */

#define SMP_MAX         4
#define SMP_FAULTED     (-100)          /* core_state: a fault on the worker (the core stays parked) */
#define SMP_HUNG        (-101)          /* a job over its time: the core is not used again */
#define SMP_STACK       16384           /* bytes of stack of each secondary core */

typedef volatile uint32_t spinlock_t;

static inline int smp_core_id(void)     /* Aff0 on QEMU virt, Aff1 on the RK3568 */
{
    uint64_t m;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    return (int)((m & 0xff) | ((m >> 8) & 0xff));
}

static inline void spin_lock(spinlock_t *l)
{
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(l, __ATOMIC_RELAXED))
            __asm__ volatile("yield");
}

static inline void spin_unlock(spinlock_t *l)
{
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

/* PSCI CPU_ON for the cores 1 to cores-1 (cores is 1 to 4); waits up to 100 ms for each. Returns how many secondary cores came up. */
int smp_start(int cores);
int smp_cores_up(void);                 /* cores running, core 0 included */
int smp_core_state(int core);           /* 0 off, 1 up, negative: the PSCI error of CPU_ON */

/* Run fn(arg) on a worker core (1..3). The function must be pure (see the top of this file).
 * smp_post returns 0, or -1 when the core is not up or busy. smp_poll returns 1 when the job is done
 * (the core is free again), 0 while it runs, -1 when the job faulted. smp_wait polls up to us microseconds; returns 1 done, 0 timeout. */
typedef void (*smp_fn)(void *arg);
int smp_post(int core, smp_fn fn, void *arg);
int smp_poll(int core);
int smp_wait(int core, uint32_t us);

/* the check at boot: every core adds to one counter, with atomics and under a lock; returns 0 when exact */
int smp_offload_count(void);            /* jobs that kernel/offload.h ran on a worker */
int smp_selftest(void);
int smp_selftest_faults(void);          /* QEMU: a faulting job and a looping job, then the cores are lost, as designed */
void smp_worker_fault(uint64_t type, uint64_t esr, uint64_t elr, uint64_t far_);

#endif
