/*
 * Milestone 0: boot and draw an HDMI colour test pattern.
 *
 * ACT LED protocol:
 *   solid on during init
 *   slow heartbeat (1 Hz)   -> framebuffer ok, pattern drawn
 *   fast blink (5 Hz)       -> mailbox/framebuffer init failed
 */
#include <stdint.h>

#include "../drivers/fb.h"
#include "../drivers/led.h"

#define SCREEN_W 1280
#define SCREEN_H 720

static framebuffer_t fb;

static void draw_test_pattern(framebuffer_t *f)
{
    const uint32_t w = f->width, h = f->height;
    const uint32_t h_bars = h * 2 / 3;
    const uint32_t h_castle = h / 12;
    const uint32_t y_ramp = h_bars + h_castle;
    const uint32_t h_ramp = (h - y_ramp) / 4;

    /* 75% SMPTE colour bars: white, yellow, cyan, green, magenta, red, blue */
    static const uint8_t bars[7][3] = {
        {191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
        {191, 0, 191},   {191, 0, 0},   {0, 0, 191},
    };
    /* Reverse castellations: blue, black, magenta, black, cyan, black, white */
    static const uint8_t castle[7][3] = {
        {0, 0, 191}, {19, 19, 19}, {191, 0, 191}, {19, 19, 19},
        {0, 191, 191}, {19, 19, 19}, {191, 191, 191},
    };

    for (uint32_t i = 0; i < 7; i++) {
        uint32_t x0 = w * i / 7, x1 = w * (i + 1) / 7;
        fb_fill_rect(f, x0, 0, x1 - x0, h_bars,
                     fb_color(f, bars[i][0], bars[i][1], bars[i][2]));
        fb_fill_rect(f, x0, h_bars, x1 - x0, h_castle,
                     fb_color(f, castle[i][0], castle[i][1], castle[i][2]));
    }

    /* Four horizontal ramps: grey, red, green, blue */
    for (uint32_t x = 0; x < w; x++) {
        uint8_t v = (uint8_t)(x * 255 / (w - 1));
        const uint32_t cols[4] = {
            fb_color(f, v, v, v), fb_color(f, v, 0, 0),
            fb_color(f, 0, v, 0), fb_color(f, 0, 0, v),
        };
        for (uint32_t r = 0; r < 4; r++) {
            uint32_t y1 = (r == 3) ? h : y_ramp + (r + 1) * h_ramp;
            for (uint32_t y = y_ramp + r * h_ramp; y < y1; y++)
                fb_putpixel(f, x, y, cols[r]);
        }
    }

    /* 1px white border to check overscan / geometry, plus centre cross */
    const uint32_t white = fb_color(f, 255, 255, 255);
    fb_fill_rect(f, 0, 0, w, 1, white);
    fb_fill_rect(f, 0, h - 1, w, 1, white);
    fb_fill_rect(f, 0, 0, 1, h, white);
    fb_fill_rect(f, w - 1, 0, 1, h, white);
    fb_fill_rect(f, w / 2 - 20, h / 2, 41, 1, white);
    fb_fill_rect(f, w / 2, h / 2 - 20, 1, 41, white);
}

void kernel_main(uint32_t atags)
{
    (void)atags;

    led_init();
    led_set(1);

    if (fb_init(&fb, SCREEN_W, SCREEN_H) != 0)
        led_blink_forever(100, 100);

    draw_test_pattern(&fb);

    led_blink_forever(500, 500);
}
