#ifndef KERNEL_B3DPI_H
#define KERNEL_B3DPI_H

#include "drivers/fb.h"

/* The 3D Bench (src/bm/b3d.c) on the Pi: Dev > 3D Bench, the monitor's j.
 * The reports go to bm/bench on the SD card. */
void bm_bench3d(framebuffer_t *fb);

#endif
