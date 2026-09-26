#ifndef B33_STRESS_H
#define B33_STRESS_H

#include "drivers/fb.h"

/*
 * Rendering stress test at 640x360 RGB565. For each test the load is
 * raised step by step; the drawing time per frame is measured (no 60 Hz
 * pacing) and the loads that still fit in 16.7 ms (60 fps) and 33.3 ms
 * (30 fps) are interpolated. Summary on the console, per-step details on
 * the serial port. The Lua part runs the embedded stress.b33 cartridge.
 */
void b33_stress_run(framebuffer_t *fb);

#endif
