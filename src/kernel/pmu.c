#include "pmu.h"

static int on;
volatile uint32_t pmu_wait_instr;

#if __ARM_ARCH >= 7
/* The Cortex-A53 (Pi Zero 2 W, AArch32): the architected PMU (CP15 c9).
 * PMCR: E, P and C; counter 0 counts the instructions architecturally
 * executed (0x08), counter 1 the L1 data cache refills (0x03); bit 31 of
 * the enable and overflow registers is the cycle counter. */
#define PMU_COUNTERS 0x80000003u

static void pmu_select(uint32_t n, uint32_t event)
{
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 5" ::"r"(n));       /* PMSELR */
    __asm__ volatile("isb");
    __asm__ volatile("mcr p15, 0, %0, c9, c13, 1" ::"r"(event));   /* PMXEVTYPER */
}

static uint32_t pmu_event(uint32_t n)
{
    uint32_t v;
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 5" ::"r"(n));
    __asm__ volatile("isb");
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 2" : "=r"(v));      /* PMXEVCNTR */
    return v;
}

void pmu_start(void)
{
    pmu_select(0, 0x08);
    pmu_select(1, 0x03);
    uint32_t v = PMU_COUNTERS;
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 3" ::"r"(v));       /* PMOVSR: cleared */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" ::"r"(v));       /* PMCNTENSET */
    v = 1u << 0 | 1u << 1 | 1u << 2;
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(v));       /* PMCR */
    on = 1;
}

void pmu_stop(void)
{
    if (!on)
        return;
    uint32_t v = PMU_COUNTERS;
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 2" ::"r"(v));       /* PMCNTENCLR */
    v = 0;
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(v));
    on = 0;
}

int pmu_on(void) { return on; }

void pmu_read(pmu_t *p)
{
    if (!on) {
        p->cycles = p->instr = p->dmiss = 0;
        return;
    }
    uint32_t c;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(c));      /* PMCCNTR */
    p->cycles = c;
    p->instr = pmu_event(0);
    p->dmiss = pmu_event(1);
}
#else
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
#endif
