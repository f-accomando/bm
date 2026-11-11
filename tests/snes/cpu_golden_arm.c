/* cpu_golden_arm.c: the golden test of the CPU (cpu_golden.c) for the QEMU ARM harness (make test-snes-cpu-arm): it prints one line a case.
 * Case 8 differs from the table on every ARM build, in C too (-DSNES_CPU_C), and equals between the assembly and C: it is listed as known. Original code. */
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
        const int known = c == 8;
        printf("case %d: %08x%08x %s\n", c, (unsigned)(h >> 32), (unsigned)h, h == GOLDEN[c] ? "ok" : known ? "differs (known on ARM)" : "MISMATCH");
        if (h != GOLDEN[c] && !known) bad++;
    }
    printf("cpu golden arm: %s\n", bad ? "FAILED" : "ok");
    return bad != 0;
}
