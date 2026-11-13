#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

#include "drivers/fb.h"
#include "gfx/font.h"

/* Clipped drawing primitives on fb->base (the page being drawn).
 * Coordinates may be negative or off screen. Colours are fb_color() values. */
void gfx_clear(framebuffer_t *fb, uint32_t color);
void gfx_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color);

/* 16x16 one-bit sprite: mask[row], bit 15 = leftmost pixel. */
void gfx_sprite16(framebuffer_t *fb, int x, int y, const uint16_t mask[16], uint32_t color);

/* Text with the console font; opaque background. Returns the end x. */
int gfx_text(framebuffer_t *fb, const font_t *font, int x, int y,
             const char *s, uint32_t fg, uint32_t bg);

#endif
