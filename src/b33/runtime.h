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
    uint32_t copy_us_total;     /* frame copies to the framebuffer */
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
/* The same; with `suspendable`, leaving with Esc / PS / Start+Select / 'q'
 * keeps the cartridge frozen in memory (B33_SUSPENDED) instead of closing
 * it (quit(), an error or the time limit still close it). Starting a
 * cartridge closes the suspended one first. */
enum { B33_ENDED = 0, B33_SUSPENDED = 1 };
int  b33_run(framebuffer_t *fb, const uint8_t *data, size_t len,
             uint32_t seconds, b33_stats_t *st, int suspendable);
/* Continues the suspended cartridge from the frame it stopped at. */
int  b33_resume(framebuffer_t *fb, uint32_t seconds, b33_stats_t *st);
/* 1 if a cartridge is suspended (its title in `title`). */
int  b33_suspended(char *title, size_t n);
/* Closes the suspended cartridge and frees its memory. */
void b33_close_suspended(void);
void b33_print_stats(const b33_stats_t *st);

/* Pure C worst case: full-screen map + 256 16x16 sprites + text, at
 * 640x360 RGB565. Shows it on screen for `frames` frames; returns the
 * average drawing time in microseconds. */
uint32_t b33_bench(framebuffer_t *fb, uint32_t frames);
/* b33_bench drawing directly and via RAM; prints one line. */
void b33_bench_report(framebuffer_t *fb, uint32_t frames);

/* Switches the screen to w x h RGB565 double buffered and points g at the
 * back page, or at a cached RAM buffer when "via RAM" is on (console
 * suspended); present shows the frame; leave restores the w x h console. */
int  b33_video_enter(framebuffer_t *fb, int w, int h, g16_t *g);
void b33_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h);
/* Shows the frame drawn in g (copying it first when drawing via RAM) and
 * points g at the next one. Returns the copy time in microseconds (0 when
 * drawing directly). */
uint32_t b33_video_present(framebuffer_t *fb, g16_t *g);
/* Draw target: 0 = framebuffer back page (default), 1 = RAM buffer + copy. */
void b33_set_via_ram(int on);
int  b33_via_ram(void);
/* 1 while the running cartridge draws into a RAM buffer (lights). */
int  b33_video_uses_ram(void);

/* Copy "via RAM" frames by DMA (after the DMA test passed). */
void b33_set_dma_frames(int on);

/* Editor support: what cart_arg() returns in the next cartridge, the
 * file a cartridge asked to play with cart_run() (0 if none), and the
 * error the last cartridge stopped with ("" if none). */
void b33_set_arg(const char *path, const char *error);
int  b33_take_run(char *path, size_t n);
const char *b33_last_error(void);
/* cart_arg().back: the editor comes back from trying a game (1), or opens
 * the file it was given from the menu (0). */
void b33_set_arg_back(int back);

/* The save file of a cartridge ("/bm33/save/1A2B3C4D.SAV", CRC-32 of its
 * title and author as in the header). */
void b33_save_path(const char *title, const char *author, char *out, size_t n);

#endif
