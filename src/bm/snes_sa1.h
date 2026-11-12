/* snes_sa1.h: the SA-1 co-processor of the SNES core (a second 65C816 at 10.74 MHz, 2 KB I-RAM, BW-RAM, DMA, math, bit processor). Private to src/bm/snes_*.c.
 * Design and rules: docs/progress/snes-sa1.md. */
#ifndef SNES_SA1_H
#define SNES_SA1_H

#include <stdint.h>

void sa1_reset(void);                               /* power on: the SA-1 CPU is held in reset until the S-CPU releases it */
int sa1_cpu_read(uint32_t a, uint8_t *v);           /* the S-CPU reads: 1 when the SA-1 cartridge owns the address (*v is the byte), 0 for the rest of the machine */
int sa1_cpu_write(uint32_t a, uint8_t v);           /* the S-CPU writes: 1 when handled */
int sa1_cpu_map(uint32_t a, const uint8_t **p);     /* the S-CPU page tables: 1 when the cartridge owns the address; *p is plain ROM or NULL (slow path) */
void sa1_run(int master_clocks);                    /* run the SA-1 CPU for this time (master clocks); raises the S-CPU IRQ through S.irq_pending */
void sa1_sync(int force);                           /* bring the SA-1 to the position of the S-CPU (before the S-CPU touches shared state) */
void sa1_frame_end(void);                           /* the frame wrapped: the clocks count from 0 again */
int sa1_irq_line(void);                             /* 1 while the SA-1 holds the S-CPU IRQ line */
int sa1_running(void);                              /* 1 when the SA-1 CPU runs (not in reset, not held) */
uint32_t sa1_instr_count(void);                     /* SA-1 instructions run (the debug line) */
void sa1_line_end(int line);                        /* the S-CPU finished a line: the SA-1 catches up */

#endif
