/* gsu_arm.c: the Super FX tests (gsu_test.c) on an ARM1176 in QEMU (make test-snes-gsu-arm), and a count of the ARM instructions a GSU instruction costs.
 * QEMU runs with -icount shift=0: one ARM instruction is 1 ns of virtual time, and the SP804 timer of the board ticks each microsecond.
 * `gsu cost` lines: ARM instructions per GSU instruction for one program, by class. Original code. */
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
int clock_gettime(int, struct timespec *);
#endif
#define main gsu_test_main
#include "gsu_test.c"
#undef main
#define GSU_DIFF_NO_MAIN
#include "gsu_diff.c"

static inline uint32_t tick(void) { return 0u - *(volatile uint32_t *)(0x101E2004); }   /* the timer counts down */

#define LOOP_OPEN(n) 0x02, IWT(12, n), WITH(15), TO(13)
#define LOOP_CLOSE 0x3C, 0x01, STOP

/* runs a program 4 times, a loop of n turns each; prints the cost of one GSU instruction */
static void cost(const char *name, const uint8_t *code, size_t len, uint8_t scmr)
{
    printf("run %s\n", name); fflush(stdout);
    load_rom(code, len);
    uint32_t ticks = 0, ins = 0;
    for (int rep = 0; rep < 4; rep++) {
        wr(0x303A, scmr); wr(0x3038, 0); wr(0x3039, 1); wr(0x303C, 0); wreg(15, 0x8000);
        const uint32_t i0 = gsu_instr_count();
        const uint32_t t0 = tick();
        { int guard = 0; while (gsu_go() && guard++ < 400) gsu_run(1364); if (gsu_go()) { printf("HANG %s:", name); for (int i = 0; i < 16; i++) printf(" R%d=%04x", i, gsu_reg(i)); printf("\n"); return; } }
        ticks += tick() - t0; ins += gsu_instr_count() - i0;
    }
    printf("gsu cost %-10s %7u GSU instr, %8u ARM instr (+-%u): %5.1f per GSU instr\n", name, ins, ticks * 1000, 4000u, ticks * 1000.0 / ins);
}
#define COST(name, scmr, ...) do { static const uint8_t c_[] = { __VA_ARGS__ }; cost(name, c_, sizeof c_, scmr); } while (0)

int main(void)
{
    *(volatile uint32_t *)(0x101E2000) = 0xFFFFFFFFu;       /* load */
    *(volatile uint32_t *)(0x101E2008) = 0xC2;              /* enable, periodic, 32 bits, 1 MHz */
    { const uint32_t t0 = tick(); register uint32_t n = 1000000; __asm__ volatile("1: subs %0, %0, #1; bne 1b" : "+r"(n)); printf("calibration: 2000000 ARM instr = %u ns\n", (tick() - t0) * 1000); }
    int r = gsu_test_main(1, (char *[]){"gsu", 0});
    if (r) return r;
    r = gsu_diff_main(60);
    if (r) return r;
    #define N 2000
    COST("alu", 0, LOOP_OPEN(N), FROM(1), TO(2), 0x52, FROM(2), TO(3), 0x62, FROM(3), TO(4), 0x71 + 1, FROM(4), TO(5), 0xC2, LOOP_CLOSE);
    COST("alu-pfx", 0, LOOP_OPEN(N), FROM(1), ALT2, 0x57, ALT2, 0x57, ALT2, 0x77, ALT2, 0xC7, ALT1, 0x52, LOOP_CLOSE);
    COST("move/ibt", 0, LOOP_OPEN(N), IBT(1, 5), IBT(2, 7), IWT(3, 0x1234), IWT(4, 0x4321), TO(5), 0xB1 /*FROM R1 w/o b: no move*/, LOOP_CLOSE);
    COST("ldw/stw", 0, LOOP_OPEN(N), IWT(1, 0x100), IWT(2, 0x200), FROM(3), 0x31, 0x42, 0x41, FROM(4), 0x32, 0x42, LOOP_CLOSE);
    COST("lms/sms", 0, LOOP_OPEN(N), ALT1, 0xA1, 0x20, ALT2, 0xA2, 0x30, ALT1, 0xA3, 0x40, LOOP_CLOSE);
    COST("plot4", 1, LOOP_OPEN(N), IWT(1, 0), IBT(2, 0), IBT(3, 5), FROM(3), 0x4E, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, LOOP_CLOSE);
    COST("plot2", 0, LOOP_OPEN(N), IWT(1, 0), IBT(2, 0), IBT(3, 1), FROM(3), 0x4E, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, 0x4C, LOOP_CLOSE);
    COST("mult", 0, LOOP_OPEN(N), IBT(1, 7), IBT(2, 9), FROM(1), TO(3), 0x82, FROM(1), TO(3), ALT1, 0x82, FROM(2), TO(4), 0x9F, LOOP_CLOSE);
    COST("getb", 0, LOOP_OPEN(N), IWT(14, 0x8100), TO(1), 0xEF, 0xDE, 0xD7, TO(2), 0xEF, 0xDE, LOOP_CLOSE);
    COST("branch", 0, LOOP_OPEN(N), IBT(1, 0), 0x08, 0x00, 0x01, 0x05, 0x00, 0x01, 0x09, 0x00, 0x01, LOOP_CLOSE);
    COST("inc/dec", 0, LOOP_OPEN(N), 0xD1, 0xD2, 0xD3, 0xE1, 0xE2, 0xE3, 0xD4, 0xE4, LOOP_CLOSE);
    COST("fill", 0, LOOP_OPEN(N), 0xD1, 0x3C, 0x31, STOP);
    COST("bench", 0x19, LOOP_OPEN(N), IBT(1, 0), IBT(2, 0), IWT(3, 1), IWT(5, 0), IWT(14, 0x8100), FROM(3), 0x4E, 0x4C, 0xD2, 0xD3, FROM(3), TO(4), 0x83, FROM(4), 0x35, 0xD5, 0xD5, TO(6), 0xEF, 0xDE, 0xD7, 0x56, FROM(6), TO(7), 0x57, LOOP_CLOSE);
    return 0;
}
