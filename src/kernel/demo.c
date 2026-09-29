#include "demo.h"
#include "input.h"
#include "tick.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "lib/printf.h"

#include <string.h>

#define NUM_BALLS       64
#define FRAME_US        16667       /* 60 Hz when there is no vsync */

typedef struct {
    float x, y, vx, vy;
    uint32_t color;
} ball_t;

static uint16_t ball_mask[16];
static ball_t balls[NUM_BALLS];

static uint32_t rng = 0x1234567;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static float rndf(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() & 0xFFFF) / 65535.0f;
}

static void make_ball_mask(void)
{
    for (int y = 0; y < 16; y++) {
        uint16_t m = 0;
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x - 15, dy = 2 * y - 15;
            if (dx * dx + dy * dy <= 15 * 15)
                m |= 0x8000 >> x;
        }
        ball_mask[y] = m;
    }
}

static void init_balls(framebuffer_t *fb)
{
    static const uint8_t pal[8][3] = {
        {255, 85, 85}, {85, 255, 85}, {85, 85, 255}, {255, 255, 85},
        {255, 85, 255}, {85, 255, 255}, {255, 170, 0}, {255, 255, 255},
    };
    for (int i = 0; i < NUM_BALLS; i++) {
        ball_t *b = &balls[i];
        b->x = rndf(0, fb->width - 16);
        b->y = rndf(16, fb->height - 16);
        b->vx = rndf(-3.0f, 3.0f);
        b->vy = rndf(-3.0f, 3.0f);
        const uint8_t *c = pal[i % 8];
        b->color = fb_color(fb, c[0], c[1], c[2]);
    }
}

static void move(float *p, float *v, float lo, float hi)
{
    *p += *v;
    if (*p < lo) { *p = lo; *v = -*v; }
    if (*p > hi) { *p = hi; *v = -*v; }
}

void demo_run(framebuffer_t *fb, uint32_t seconds, demo_stats_t *st)
{
    const uint32_t w = fb->width, h = fb->height;
    const uint32_t bg = fb_color(fb, 0, 0, 40);
    const uint32_t bar_bg = fb_color(fb, 0, 170, 170);
    const uint32_t black = fb_color(fb, 0, 0, 0);
    const uint32_t white = fb_color(fb, 255, 255, 255);
    const uint32_t orange = fb_color(fb, 255, 170, 0);
    char text[96];

    memset(st, 0, sizeof *st);
    st->min_frame_us = ~0u;
    make_ball_mask();
    init_balls(fb);

    float rx = 40, ry = 60, rvx = 2.0f, rvy = 1.5f;
    int bar = 0;
    uint32_t fps_frames = 0, fps_t0, fps = 0, draw_total = 0, last_draw = 0;

    console_suspend(1);

    uint32_t start = timer_ticks(), prev = start;
    fps_t0 = start;
    uint32_t deadline = start + FRAME_US;

    while (timer_ticks() - start < seconds * 1000000u && !input_remote_ready()) {
        uint32_t t_draw = timer_ticks();

        gfx_clear(fb, bg);
        for (int i = 0; i < NUM_BALLS; i++) {
            ball_t *b = &balls[i];
            move(&b->x, &b->vx, 0, w - 16);
            move(&b->y, &b->vy, 16, h - 16);
            gfx_sprite16(fb, (int)b->x, (int)b->y, ball_mask, b->color);
        }
        move(&rx, &rvx, 0, w - 48);
        move(&ry, &rvy, 16, h - 48);
        gfx_rect(fb, (int)rx, (int)ry, 48, 48, orange);

        /* Tearing detector: a full-height bar that moves 8 px per frame. */
        gfx_rect(fb, bar, 16, 4, h - 16, white);
        bar = (bar + 8) % w;

        gfx_rect(fb, 0, 0, w, 16, bar_bg);
        ksnprintf(text, sizeof text,
                  " bm33 M4 demo  %2lu fps  draw %lu.%lu ms  %s  %lus  (key: exit)",
                  fps, last_draw / 1000, last_draw / 100 % 10,
                  st->vsync ? "vsync" : "timer", (timer_ticks() - start) / 1000000);
        gfx_text(fb, &font_console_8x16, 0, 0, text, black, bar_bg);

        last_draw = timer_ticks() - t_draw;
        draw_total += last_draw;

        st->vsync = fb_flip(fb);
        if (!st->vsync) {
            while ((int32_t)(timer_ticks() - deadline) < 0)
                ;
            deadline += FRAME_US;
            if ((int32_t)(timer_ticks() - deadline) > 0)
                deadline = timer_ticks() + FRAME_US;   /* fell behind */
        }

        uint32_t now = timer_ticks(), dt = now - prev;
        prev = now;
        st->frames++;
        if (st->frames > 1) {       /* the first interval includes setup */
            if (dt < st->min_frame_us) st->min_frame_us = dt;
            if (dt > st->max_frame_us) st->max_frame_us = dt;
            if (dt > FRAME_US * 3 / 2) st->dropped++;
        }
        fps_frames++;
        if (now - fps_t0 >= 1000000) {
            fps = fps_frames * 1000000u / (now - fps_t0);
            fps_frames = 0;
            fps_t0 = now;
        }
    }

    st->elapsed_us = timer_ticks() - start;
    st->draw_us_avg = st->frames ? draw_total / st->frames : 0;
    if (st->min_frame_us == ~0u)
        st->min_frame_us = 0;
    while (input_remote_getc() >= 0)
        ;

    fb_show(fb, 0);
    console_suspend(0);
}

void demo_print(const demo_stats_t *st)
{
    uint32_t ms = st->elapsed_us / 1000;
    uint32_t fps10 = ms ? st->frames * 10000u / ms : 0;
    kprintf("demo: %lu frames in %lu.%02lu s = %lu.%lu fps (%s)\n"
            "      frame %lu-%lu us, %lu dropped, draw avg %lu us\n",
            st->frames, ms / 1000, ms % 1000 / 10, fps10 / 10, fps10 % 10,
            st->vsync ? "vsync" : "timer", st->min_frame_us, st->max_frame_us,
            st->dropped, st->draw_us_avg);
}
