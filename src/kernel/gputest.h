#ifndef GPUTEST_H
#define GPUTEST_H

#include "drivers/fb.h"

/* M30: the 3D unit of the VideoCore (V3D) step by step, each step written
 * on the screen before it runs: power on, identity, a clear, a triangle,
 * the depth test, speed, and a picture drawn by the GPU on the console.
 * Monitor 'g', or "GPU test" in the Dev tab. */
void gpu_test(framebuffer_t *fb);

#endif
