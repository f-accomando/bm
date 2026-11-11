/* ppu_equiv_arm.c: ppu_equiv.c for the QEMU ARM harness (make test-snes-ppu-arm): the fast PPU with the assembly mix loop (snes_ppu_arm.S) against the reference copy.
 * It runs the equivalence only (a few cases: QEMU is slow). Original code. */
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
int clock_gettime(int, struct timespec *);      /* a stub in arm_mmu.c: the bench of ppu_equiv is not used here */
#endif
#define main ppu_equiv_main
#include "ppu_equiv.c"
#undef main
int main(void)
{
    ppu_reset(); ppu_reset_ref();
    return rotation() || equiv(CASES_ARM);
}
