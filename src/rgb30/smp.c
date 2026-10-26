/* The secondary cores: PSCI CPU_ON, then each core attaches to the page tables of core 0 and waits. */
#include "smp.h"
#include "plat.h"
#include "arch/mmu.h"
#include "drivers/timer.h"
#include "lib/printf.h"

extern void smp_entry(void);

uint8_t smp_stacks[SMP_MAX][SMP_STACK] __attribute__((aligned(16)));
static volatile int core_state[SMP_MAX];

void smp_secondary_main(uint64_t core)
{
    mmu_attach();
    core_state[core] = 1;
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

int smp_start(void)
{
    core_state[0] = 1;
    for (int i = 1; i < SMP_MAX; i++) {
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
