#ifndef B33_RUNTIME_H
#define B33_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/fb.h"
#include "gfx16.h"

typedef struct {
    char title[49];
    uint32_t frames;
    uint32_t elapsed_us;
    uint32_t cpu_us_total;      /* _update + _draw */
    uint32_t cpu_us_max;
    uint32_t dropped;
    uint32_t lua_kb;
    int ok;                     /* 0 = error (message printed), 1 = ran */
} b33_stats_t;

/*
 * Runs a native cartridge: screen at the cartridge resolution in RGB565,
 * double buffered, 60 frames per second; _init() once, then _update() and
 * _draw() every frame. Serial keys: arrows or w/a/s/d, space/j = A,
 * k/x = B, q = quit. Returns after `seconds`, on 'q' or on an error.
 */
void b33_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, b33_stats_t *st);
void b33_print_stats(const b33_stats_t *st);

/* Pure C worst case: full-screen map + 256 16x16 sprites + text, at
 * 640x360 RGB565. Shows it on screen for `frames` frames; returns the
 * average drawing time in microseconds. */
uint32_t b33_bench(framebuffer_t *fb, uint32_t frames);

/* Switches the screen to w x h RGB565 double buffered and points g at the
 * back page (console suspended); leave restores the w x h console. */
int  b33_video_enter(framebuffer_t *fb, int w, int h, g16_t *g);
void b33_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h);

#endif
