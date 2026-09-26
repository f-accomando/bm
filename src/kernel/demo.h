#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

#include "drivers/fb.h"

typedef struct {
    uint32_t frames;
    uint32_t elapsed_us;
    uint32_t min_frame_us, max_frame_us;
    uint32_t dropped;       /* frame intervals longer than 1.5 periods */
    uint32_t draw_us_avg;   /* time spent drawing a frame */
    int vsync;              /* paced by the firmware vsync (else by the timer) */
} demo_stats_t;

/* M4 animation demo on the double-buffered framebuffer: bouncing sprites,
 * a moving rectangle and a fast vertical bar (tearing shows up as a broken
 * bar). Runs for `seconds` or until a byte arrives on the UART. The console
 * is suspended meanwhile and redrawn afterwards. */
void demo_run(framebuffer_t *fb, uint32_t seconds, demo_stats_t *st);
void demo_print(const demo_stats_t *st);

#endif
