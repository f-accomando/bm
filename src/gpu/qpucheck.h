/*
 * QPU check (bm3d 7.x, Pi Zero W): runs the particle program and the fade
 * program on a small array, compares the result with the ARM version and
 * times both. Used by the GPU test (step 'QPU programs') and by the 3D Bench
 * (the QPU row). Only called when the config key gpu3d_qpu is 1 (default 0).
 * Every wait has a timeout (QPU_TIMEOUT_US per run), so the check never hangs.
 */
#ifndef QPUCHECK_H
#define QPUCHECK_H

#include <stdint.h>

#define QPUCHECK_PASS  1
#define QPUCHECK_FAIL  0
#define QPUCHECK_SKIP  (-1)

typedef struct {
    int particles;              /* QPUCHECK_* for the particle program */
    int fade;                   /* QPUCHECK_* for the fade program */
    uint32_t p_arm_us, p_qpu_us;    /* particles: the ARM version and the QPU run, in microseconds */
    uint32_t f_arm_us, f_qpu_us;    /* fade: the same */
    uint32_t p_count, f_count;      /* particles and pixels in one run */
    char why[96];               /* the reason of the first SKIP or FAIL, or "" */
} qpucheck_t;

/* Calls qpu_init once. `us` is a microsecond clock. Returns the worst result:
 * FAIL if one program differs from the ARM, else SKIP if none ran on the QPUs
 * (qpu_init failed or a run timed out), else PASS. */
int qpu_check(qpucheck_t *r, uint32_t (*us)(void));

/* "PASS", "FAIL" or "SKIP" */
const char *qpu_check_word(int result);

/* One line for the log and the bench report. */
void qpu_check_line(const qpucheck_t *r, int overall, char *out, unsigned size);

#endif
