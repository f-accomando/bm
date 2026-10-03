#ifndef KERNEL_PMU_H
#define KERNEL_PMU_H

/* test_v3d: the ARM's performance monitor is not there (src/kernel/pmu.h) */
#include <stdint.h>

typedef struct { uint32_t cycles, instr, dmiss; } pmu_t;
static inline void pmu_read(pmu_t *p) { p->cycles = p->instr = p->dmiss = 0; }
static volatile uint32_t pmu_wait_instr;

#endif
