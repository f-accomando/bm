#ifndef RGB30_MALI_HW_H
#define RGB30_MALI_HW_H

#include "mali.h"
#include "drivers/fb.h"

/* The Mali for r3d (mali3d=1). The hw of the RK3566 (no screen, no triangle); NULL elsewhere (QEMU virt). */
const mali_hw_t *rgb30_mali_hw(void);
/* Probes the Mali with that hw, quietly as mali_probe is (its own lines only); MALI_OK or the step that failed;
 * MALI_E_POWER where there is no Mali. */
int rgb30_mali_probe(void);
/* Probes the Mali with the triangle and variant tests on the screen. */
int rgb30_mali_probe_triangle(const framebuffer_t *fb);
/* The last probe result (MALI_OK or the step that failed; 1: not probed yet) and what it found. */
int rgb30_gpu_result(const mali_info_t **in);

#endif
