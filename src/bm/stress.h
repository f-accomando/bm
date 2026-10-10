#ifndef BM_STRESS_H
#define BM_STRESS_H

#include "drivers/fb.h"

/*
 * Rendering stress test at 640x360 RGB565. For each test the load is
 * raised step by step; the drawing time per frame is measured (no 60 Hz
 * pacing) and the loads that still fit in 16.7 ms (60 fps) and 33.3 ms
 * (30 fps) are interpolated. Summary on the console, per-step details on
 * the serial port. The Lua part runs the embedded stress.bm cartridge.
 * Start+Select, Ctrl+Esc or PS stop it after the frame on screen
 * (syskeys_test_stopped() then says 1: no table, no report).
 */
void bm_stress_run(framebuffer_t *fb);

/* Waits until the kernel has run for ms milliseconds, with a countdown:
 * right after boot the WiFi joins its network and a paired pad comes back,
 * and the first tests would pay for it. The keys that stop a test end the
 * wait too (syskeys_test_stop). */
void bm_stress_settle(uint32_t ms);

#endif
