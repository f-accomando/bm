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

#ifdef __cplusplus
}
#endif
#endif
