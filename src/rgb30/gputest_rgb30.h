#ifndef RGB30_GPUTEST_H
#define RGB30_GPUTEST_H

#include "drivers/fb.h"
#include "mali.h"

/* Dev > GPU test (M41): the Mali probed, its lines on the console and in a
 * report, then the line saying how to go back (back: the button's name)
 * and, if every step went through, the GPU's square over the text */
void rgb30_gpu_test(framebuffer_t *fb, const char *back);

/* the last test's result (MALI_OK or the step that failed; 1: not run)
 * and what it found */
int rgb30_gpu_result(const mali_info_t **in);

/* M41 phase 2: the Mali for r3d (mali3d=1). The hw of the RK3566 (no screen, no triangle); NULL elsewhere (QEMU virt) */
const mali_hw_t *rgb30_mali_hw(void);
/* probes the Mali with that hw, quietly as mali_probe is (its own lines only); MALI_OK or the step that failed;
 * MALI_E_POWER where there is no Mali */
int rgb30_mali_probe(void);

#endif
