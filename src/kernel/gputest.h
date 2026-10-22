#ifndef GPUTEST_H
#define GPUTEST_H

#include "drivers/fb.h"

#include <stddef.h>

/* M33: the 3D unit of the VideoCore (V3D) step by step, each step written
 * on the screen before it runs: power on, identity, a clear, a triangle,
 * the depth test, speed, a texture, and a picture drawn by the GPU on the
 * console.
 * Monitor 'g', or "GPU test" in the Dev tab. */
void gpu_test(framebuffer_t *fb);

/* 1 if bm/config.txt has gpu3d_qpu=1 (default 0). Only then the GPU test and
 * the 3D Bench run the QPU programs (src/gpu/qpucheck.h). */
int gpu_qpu_key(void);

/* the QPU check for the 3D Bench (b3d_platform_t.qpu_row): one line */
void gpu_qpu_row(char *line, size_t n);

#endif
