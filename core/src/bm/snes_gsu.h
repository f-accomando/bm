/* snes_gsu.h: the Super FX co-processor (GSU-1 and GSU-2) of the SNES core. Private to core/src/bm/snes_*.c. Design and sources: docs/progress/snes-superfx.md. */
#ifndef SNES_GSU_H
#define SNES_GSU_H

#include <stdint.h>

void gsu_reset(uint32_t ram_size);          /* power on; ram_size is the cart RAM in bytes (a power of 2, 32 KB to 128 KB) */
uint8_t gsu_read(uint16_t off);             /* the SNES reads $3000-$34FF */
void gsu_write(uint16_t off, uint8_t v);    /* the SNES writes $3000-$34FF */
void gsu_run(int master_clocks);            /* run for this time (the SNES clock, 21.48 MHz); raises the IRQ at STOP */
int gsu_go(void);                           /* 1 while a program runs */
int gsu_owns_rom(void);                     /* 1 while a program runs and RON is set: the SNES reads the vector pattern */
int gsu_irq_line(void);                     /* 1 while the IRQ line is low (STOP with the mask off, until SFR is read) */
uint32_t gsu_instr_count(void);             /* instructions run since the reset (the tests and the bench) */
uint32_t gsu_ram_mask(void);                /* the cart RAM size minus 1 */
uint16_t gsu_reg(int n);                    /* R0-R15 (the tests) */
typedef struct { uint16_t r[16]; uint8_t pbr, rombr, rambr, cfgr, scbr, clsr, scmr, colr, por, go, irq, alt, b, sfr_lo; uint16_t cbr; } gsu_dbg_t;
void gsu_dbg(gsu_dbg_t *d);                 /* the state, for the tests and the debug line */
uint8_t gsu_vector_byte(uint16_t off);      /* the byte the SNES reads at $FFxx while the GSU owns the ROM */

#endif
