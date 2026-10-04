#include "splash.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"

#include <string.h>


void splash_show(framebuffer_t *fb, const char *version)
{
    console_suspend(1);                 /* the boot goes on printing there, unseen */
    const uint8_t br = (uint8_t)(bm_logo_bg >> 16), bgc = (uint8_t)(bm_logo_bg >> 8), bb = (uint8_t)bm_logo_bg;
    const uint32_t bg = fb_color(fb, br, bgc, bb);
    gfx_clear(fb, bg);
    const int w = (int)fb->width, h = (int)fb->height;
    const int x0 = (w - bm_logo_w) / 2, y0 = (h - bm_logo_h) / 2 - 16;
    for (int y = 0; y < bm_logo_h; y++) {
        if (y0 + y < 0 || y0 + y >= h)
            continue;
        uint32_t *row = (uint32_t *)(fb->base + (uint32_t)(y0 + y) * fb->pitch);
        const uint16_t *src = bm_logo + y * bm_logo_w;
        for (int x = 0; x < bm_logo_w; x++) {
            if (x0 + x < 0 || x0 + x >= w)
                continue;
            const uint16_t c = src[x];
            const uint8_t r = (uint8_t)((c >> 11) << 3 | (c >> 13)), g = (uint8_t)((c >> 5 & 63) << 2 | (c >> 9 & 3)),
                          b = (uint8_t)((c & 31) << 3 | (c >> 2 & 7));
            row[x0 + x] = fb_color(fb, r, g, b);
        }
    }
    if (version && version[0]) {
        const int tx = (w - (int)strlen(version) * 8) / 2 / 8 * 8;
        gfx_text(fb, &font_console_8x16, tx, (y0 + bm_logo_h + 24) / 16 * 16, version,
                 fb_color(fb, 110, 120, 150), bg);
    }
}
