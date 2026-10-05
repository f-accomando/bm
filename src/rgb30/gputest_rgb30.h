#ifndef RGB30_GPUTEST_H
#define RGB30_GPUTEST_H

#include "drivers/fb.h"
#include "mali.h"

/* Dev > GPU test (M41): the Mali probed, its lines on the console and in a
 * report */
void rgb30_gpu_test(framebuffer_t *fb);

/* the last test's result (MALI_OK or the step that failed; 1: not run)
 * and what it found */
int rgb30_gpu_result(const mali_info_t **in);

#endif
