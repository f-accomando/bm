/* The secondary cores of the RGB30 (Cortex-A55 x4): bring-up only (docs/progress/multicore.md, M1). */
#ifndef SMP_H
#define SMP_H
#include <stdint.h>

void mmu_attach(void);                 /* mmu.c: a secondary core joins core 0's tables */

#define SMP_MAX         4
#define SMP_STACK       16384           /* bytes of stack of each secondary core */

/* PSCI CPU_ON for every core 1..3; waits up to 100 ms for each. Returns how many cores came up. */
int smp_start(void);
int smp_cores_up(void);                 /* cores running, core 0 included */
int smp_core_state(int core);           /* 0 off, 1 up, negative: the PSCI error of CPU_ON */

#endif
