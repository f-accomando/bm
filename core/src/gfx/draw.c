#include "draw.h"

void gfx_clear(framebuffer_t *fb, uint32_t color)
{
    fb_fill_rect(fb, 0, 0, fb->width, fb->height, color);
}

void gfx_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0)
        return;
    fb_fill_rect(fb, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

void gfx_sprite16(framebuffer_t *fb, int x, int y, const uint16_t mask[16], uint32_t color)
{
    for (int r = 0; r < 16; r++) {
        int py = y + r;
        if (py < 0 || py >= (int)fb->height)
            continue;
        uint32_t *row = (uint32_t *)(fb->base + py * fb->pitch);
        uint16_t bits = mask[r];
        for (int c = 0; bits; c++, bits <<= 1) {
            int px = x + c;
            if ((bits & 0x8000) && px >= 0 && px < (int)fb->width)
                row[px] = color;
        }
    }
}

int gfx_text(framebuffer_t *fb, const font_t *font, int x, int y,
             const char *s, uint32_t fg, uint32_t bg)
{
    for (; *s; s++, x += 8) {
        const uint8_t *g = font->glyphs + (uint8_t)*s * font->height;
        for (int r = 0; r < font->height; r++) {
            int py = y + r;
            if (py < 0 || py >= (int)fb->height)
                continue;
            uint32_t *row = (uint32_t *)(fb->base + py * fb->pitch);
            for (int c = 0; c < 8; c++) {
                int px = x + c;
                if (px >= 0 && px < (int)fb->width)
                    row[px] = (g[r] & (0x80 >> c)) ? fg : bg;
            }
        }
    }
    return x;
}
