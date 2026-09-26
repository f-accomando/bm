#ifndef FB_H
#define FB_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     /* bytes per row */
    uint32_t is_rgb;    /* 1: R in low byte, 0: B in low byte */
    uint8_t *base;      /* buffer being drawn into */
    uint32_t size;
    uint8_t *mem;       /* start of the whole (virtual) framebuffer */
    uint32_t buffers;   /* 1, or 2 when double buffered */
    uint32_t shown;     /* index of the buffer on screen */
    int vsync;          /* firmware supports "wait for vsync" (-1 = unknown) */
} framebuffer_t;

/* Asks the firmware for a 32bpp framebuffer with `buffers` (1 or 2) pages
 * stacked vertically in a virtual screen. Returns 0 on success. Drawing
 * starts on page 0, which is also the one shown. */
int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers);

/* Shows page `index` (virtual offset) and makes it the drawing target. */
void fb_show(framebuffer_t *fb, uint32_t index);

/* Double buffering: shows the page just drawn, waits for the vertical
 * blank if the firmware supports it, then points `base` at the other page.
 * Returns 1 if it synchronised to vsync, 0 if the caller must pace frames. */
int fb_flip(framebuffer_t *fb);

static inline uint32_t fb_color(const framebuffer_t *fb, uint8_t r, uint8_t g, uint8_t b)
{
    return fb->is_rgb
        ? 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r
        : 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline void fb_putpixel(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color)
{
    *(volatile uint32_t *)(fb->base + y * fb->pitch + x * 4) = color;
}

void fb_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, uint32_t color);

#endif
