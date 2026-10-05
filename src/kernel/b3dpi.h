#ifndef KERNEL_B3DPI_H
#define KERNEL_B3DPI_H

#include "drivers/fb.h"

/* The 3D Bench (src/bm/b3d.c) on the Pi: Dev > 3D Bench, the monitor's j.
 * The reports go to bm/bench on the SD card. */
void bm_bench3d(framebuffer_t *fb);

/* a part of it (the monitor's ":b3d"): only these tests and profiles
 * (comma lists, NULL all), without waiting on the pages if no_wait */
void bm_bench3d_part(framebuffer_t *fb, const char *tests, const char *profiles, int no_wait);

#endif
