#include "pmu.h"

static int on;
volatile uint32_t pmu_wait_instr;

/* PMNC: E (count), P and C (reset the counters), the overflow flags
 * written 1 (cleared), events: PMN0 instructions, PMN1 D-cache misses */
#define PMNC_START (1u << 0 | 1u << 1 | 1u << 2 | 7u << 8 | 0x07u << 20 | 0x0Bu << 12)

void pmu_start(void)
{
    uint32_t v = PMNC_START;
    __asm__ volatile("mcr p15, 0, %0, c15, c12, 0" ::"r"(v));
    on = 1;
}

void pmu_stop(void)
{
    if (!on)
        return;
    uint32_t v = 7u << 8;
    __asm__ volatile("mcr p15, 0, %0, c15, c12, 0" ::"r"(v));
    on = 0;
}

int pmu_on(void) { return on; }

void pmu_read(pmu_t *p)
{
    if (!on) {
        p->cycles = p->instr = p->dmiss = 0;
        return;
    }
    uint32_t c, i, d;
    __asm__ volatile("mrc p15, 0, %0, c15, c12, 1" : "=r"(c));
    __asm__ volatile("mrc p15, 0, %0, c15, c12, 2" : "=r"(i));
    __asm__ volatile("mrc p15, 0, %0, c15, c12, 3" : "=r"(d));
    p->cycles = c;
    p->instr = i;
    p->dmiss = d;
}
