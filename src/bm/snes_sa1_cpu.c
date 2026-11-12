/*
 * snes_sa1_cpu.c: the second 65C816 of the SA-1 cartridge. It is the same source as the S-CPU (snes_cpu.c), compiled a second time in this file with the
 * names of its globals changed, so the two CPUs have their own registers, page tables, clock and interrupt lines. Only the C loop is used (SNES_CPU_C):
 * the ARM assembly loop works on the S-CPU tables. Keep the list of names below in step with snes_cpu.c (the link fails when one is missing).
 * The clock of this CPU counts in units of 6 for a bus cycle, as the S-CPU core does; one unit is 1/3 of a master clock (the SA-1 runs at 10.74 MHz,
 * a bus cycle is 2 master clocks), see snes_sa1.c. Original code.
 */
#include "snes_int.h"

typedef struct { int irq_pending, nmi_pending, line_clk; } sa1_isr_t;
sa1_isr_t sa1_s;                                    /* the interrupt lines and the position of the SA-1 CPU: the names the core reads as S.irq_pending ... */
const uint8_t *sa1_rtab[4096];                      /* the page tables of the SA-1 (snes_sa1.c builds them) */
uint8_t *sa1_wtab[4096];
uint8_t sa1_ctab[4096];
int sa1_clk;                                        /* the clock of the SA-1 CPU, in units (6 a bus cycle) */
uint8_t sa1_bus_read(uint32_t a);                   /* the slow bus of the SA-1 (snes_sa1.c) */
void sa1_bus_write(uint32_t a, uint8_t v);

#undef SNES_TRACE
#define SNES_CPU_C 1
#define S sa1_s
#define snes_rtab sa1_rtab
#define snes_wtab sa1_wtab
#define snes_ctab sa1_ctab
#define snes_clk sa1_clk
#define snes_read sa1_bus_read
#define snes_write sa1_bus_write
#define cpu_run sa1c_run
#define cpu_step sa1c_step
#define cpu_reset sa1c_reset
#define cpu_get sa1c_get
#define cpu_set sa1c_set
#define cpu_info sa1c_info
#define cpu_waiting sa1c_waiting
#define cpu_stopped sa1c_stopped
#include "snes_cpu.c"
