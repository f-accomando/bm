#ifndef KERNEL_PMU_H
#define KERNEL_PMU_H

#include <stdint.h>

/*
 * The ARM1176's performance monitor (CP15 c15, c12): the cycle counter
 * and two event counters, here the instructions executed (event 0x07) and
 * the data cache misses (0x0B). Off until pmu_start(); QEMU's raspi0 has
 * no such registers (an access is an undefined instruction), so the caller
 * starts it only on a real Pi. The counters are 32 bits: differences of a
 * frame are exact (they wrap after about 4 s at 1 GHz). On the Pi Zero 2
 * W (kernel7.img) the same three from the Cortex-A53's PMU (CP15 c9).
 */
void pmu_start(void);
void pmu_stop(void);

typedef struct { uint32_t cycles, instr, dmiss; } pmu_t;

/* the three counters now (all 0 when off) */
void pmu_read(pmu_t *p);

/* The SNES meter (docs/progress/snes-cpu-c2.md): counter 0 counts the instructions executed, counter 1 one event of a rotation
 * (pmu_meter_event: 0 instruction cache misses, 1 data cache misses, 2 branches mispredicted), and the cycle counter runs.
 * pmu_start_meter: 1 if it runs (0 under an emulator, where the registers are not there). pmu_meter_event also restarts the counters.
 * pmu_meter_read: out = instructions, the event, cycles: differences of a part are exact. */
int pmu_start_meter(void);
void pmu_meter_event(int k);
void pmu_meter_read(uint32_t out[3]);

/* the instructions executed while the ARM waited for the V3D (v3d_run),
 * counted since boot when the monitor is on */
extern volatile uint32_t pmu_wait_instr;

#endif
