/* ref_diff_ours.h: the small C interface between tests/snes/ref_diff.cpp (C++) and our core (tests/snes/ref_diff_ours.c). */
#ifndef REF_DIFF_OURS_H
#define REF_DIFF_OURS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RD_MAXW 64
typedef struct { uint32_t addr; uint8_t val, old; } rd_write_t;
typedef struct { uint16_t a, x, y, s, d, pc; uint8_t p; int e; uint8_t pb, db; } rd_regs_t;   /* the same layout as cpu_regs_t of snes_int.h */

extern rd_write_t ours_w[RD_MAXW];
extern int ours_nw;

void ours_init(uint8_t *mem16mb);
/* one instruction (after an interrupt entry when nmi or irq is set); returns the master clocks */
int ours_step(const rd_regs_t *in, rd_regs_t *out, int nmi, int irq, int *waiting, int *stopped);

/* the Super FX of our core on a made-up LoROM cartridge (a ROM image of 128 KB, a cart RAM of 64 KB) */
typedef struct { uint16_t r[16]; uint8_t pbr, rombr, rambr, scbr, scmr, colr, por, clsr, sfr_lo; uint16_t cbr; } rd_gsu_t;
void ours_gsu_load(const uint8_t *rom128k);
uint8_t *ours_gsu_ram(void);                    /* the cart RAM (64 KB) */
unsigned ours_gsu_ram_size(void);
void ours_wr(uint32_t addr, uint8_t v);         /* a write of the SNES CPU: registers $3000-$34FF */
int ours_gsu_run(int max_lines);                /* runs until STOP; returns the instruction count, -1 when it does not stop */
void ours_gsu_state(rd_gsu_t *o);

#ifdef __cplusplus
}
#endif
#endif
