/* cpu_golden_arm.c: the golden test of the CPU (cpu_golden.c) for the QEMU ARM harness (make test-snes-cpu-arm): it prints one line a case.
 * Every case equals the table on ARM (case 8 differed until the two pads were drawn in separate statements). Original code. */
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
int clock_gettime(int, struct timespec *);      /* a stub in arm_mmu.c: the bench of cpu_golden is not used here */
#endif
#define main golden_main
#include "cpu_golden.c"
#undef main
int main(void)
{
    int bad = 0;
    for (int c = 0; c < CASES; c++) {
        const uint64_t h = run_case(c);
        printf("case %d: %08x%08x %s\n", c, (unsigned)(h >> 32), (unsigned)h, h == GOLDEN[c] ? "ok" : "MISMATCH");
        if (h != GOLDEN[c]) bad++;
    }
    printf("cpu golden arm: %s\n", bad ? "FAILED" : "ok");
    return bad != 0;
}
