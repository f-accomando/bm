#ifndef KERNEL_PMU_H
#define KERNEL_PMU_H

#include <stdint.h>

/*
 * The ARM1176's performance monitor (CP15 c15, c12): the cycle counter
 * and two event counters, here the instructions executed (event 0x07) and
 * the data cache misses (0x0B). Off until pmu_start(); QEMU's raspi0 has
 * no such registers (an access is an undefined instruction), so the caller
 * starts it only on a real Pi. The counters are 32 bits: differences of a
 * frame are exact (they wrap after about 4 s at 1 GHz).
 */
void pmu_start(void);
void pmu_stop(void);
int pmu_on(void);

typedef struct { uint32_t cycles, instr, dmiss; } pmu_t;

/* the three counters now (all 0 when off) */
void pmu_read(pmu_t *p);

/* the instructions executed while the ARM waited for the V3D (v3d_run),
 * counted since boot when the monitor is on */
extern volatile uint32_t pmu_wait_instr;

#endif
