#ifndef FB_H
#define FB_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     /* bytes per row */
    uint32_t is_rgb;    /* 1: R in low byte, 0: B in low byte */
    uint8_t *base;
    uint32_t size;
} framebuffer_t;

/* Asks the firmware for a 32bpp framebuffer. Returns 0 on success. */
int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height);

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
