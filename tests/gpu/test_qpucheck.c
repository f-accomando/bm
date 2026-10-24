/*
 * The QPU check (src/gpu/qpucheck.c: 3D Bench row, config key
 * gpu3d_qpu=1) on the V3D emulator.
 *
 *   test_qpucheck 0     the QPUs work: PASS for both programs
 *   test_qpucheck 1     the QPUs never answer: SKIP, no hang
 *   test_qpucheck 2     the QPUs store wrong data: the probe turns them off: SKIP
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "gpu/qpu.h"
#include "gpu/qpucheck.h"
#include "v3d_emu.h"

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static uint32_t us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000u + t.tv_nsec / 1000u);
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    const int mode = argc > 1 ? atoi(argv[1]) : 0;
    emu_qpu = mode;
    qpucheck_t r;
    char line[256];
    const uint32_t t0 = us();
    const int all = qpu_check(&r, us);
    qpu_check_line(&r, all, line, sizeof line);
    printf("%s\n", line);
    CHECK(us() - t0 < 20000000u, "the check took too long");
    if (mode == 0) {
        CHECK(all == QPUCHECK_PASS && r.particles == QPUCHECK_PASS && r.fade == QPUCHECK_PASS, "wanted PASS: %s", line);
        CHECK(r.p_count == 1024 && r.f_count == 1024, "counts %u %u", r.p_count, r.f_count);
        CHECK(strstr(line, "QPU PASS"), "line: %s", line);
    } else {
        CHECK(all == QPUCHECK_SKIP && r.particles == QPUCHECK_SKIP && r.fade == QPUCHECK_SKIP, "wanted SKIP: %s", line);
        CHECK(r.why[0], "SKIP without a reason");
    }
    /* the second call does not start the V3D again and gives the same word */
    qpucheck_t r2;
    CHECK(qpu_check(&r2, us) == all, "second call differs");
    printf(failures ? "test_qpucheck %d: %d FAILED\n" : "test_qpucheck %d: ok\n", mode, failures ? failures : 0);
    return failures != 0;
}
