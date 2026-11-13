/* snes_dsp77.h: the uPD77C25 (NEC 7725) core that runs the SNES coprocessors DSP-1, DSP-2, DSP-3 and DSP-4. Private to core/src/bm/snes_*.c.
 * Design, firmware layouts and the facts that are not sure: docs/progress/snes-dsp.md. */
#ifndef SNES_DSP77_H
#define SNES_DSP77_H

#include <stddef.h>
#include <stdint.h>

#define DSP77_PROG_WORDS 2048                   /* program ROM: 2048 words of 24 bits */
#define DSP77_DATA_WORDS 1024                   /* data ROM: 1024 words of 16 bits */
#define DSP77_PROG_BYTES (DSP77_PROG_WORDS * 3)
#define DSP77_DATA_BYTES (DSP77_DATA_WORDS * 2)

/* Firmware: the program (6144 bytes) and the data (2048 bytes), or both in one file of 8192 bytes (program first). Words are 3 and 2 bytes; the byte order
 * (little or big endian) is found from the program. 0, or -1 and *why. A good load resets the chip. */
int dsp77_load(const uint8_t *prog, size_t nprog, const uint8_t *data, size_t ndata, const char **why);
int dsp77_load_combined(const uint8_t *d, size_t n, const char **why);
int dsp77_ready(void);                          /* 1 when a firmware is loaded */
void dsp77_unload(void);
void dsp77_reset(void);                         /* power on: PC 0, registers 0, RQM 0 */
uint8_t dsp77_read_dr(void);                    /* the SNES reads the data port */
uint8_t dsp77_read_sr(void);                    /* the SNES reads the status port (the high byte of SR: bit 7 is RQM) */
void dsp77_write_dr(uint8_t v);                 /* the SNES writes the data port */
void dsp77_run(int master_clocks);              /* run for this time (the SNES clock, 21.48 MHz); a chip that waits for the SNES does not run */
int dsp77_idle(void);                           /* 1 while the chip waits for an access of the SNES in a one-instruction loop on RQM */
uint32_t dsp77_instr_count(void);               /* instructions run since the reset */
typedef struct { uint16_t a, b, tr, trb, dr, si, so, k, l, sr, pc, rp; uint8_t dp, sp, fa, fb; uint16_t ram[256]; } dsp77_dbg_t;   /* fa, fb: flags C 1, Z 2, OV0 4, OV1 8, S0 16, S1 32 */
void dsp77_dbg(dsp77_dbg_t *d);                 /* the state, for the tests */

#endif
