#ifndef BM_RUNTIME_H
#define BM_RUNTIME_H

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
    uint32_t tris3d;            /* 3D triangles drawn in the last frame */
    int gpu3d;                  /* the GPU drew the 3D at the end */
    uint32_t d2_ops;            /* 2D drawn after the GPU's 3D, recorded meanwhile (M35) */
    int ok;                     /* 0 = error (message printed), 1 = ran */
    /* the dev kit (the SDK's report of a test run: cart_arg().run) */
    uint32_t lua_peak_kb;       /* the most Lua memory seen at the end of a frame */
    uint32_t assets_kb;         /* sprite sheet, map, models, sound bank, z-buffer */
    uint32_t instr_k_max;       /* Lua instructions (thousands) of the busiest frame */
    uint32_t slow;              /* frames over 16.7 ms of _update + _draw */
    uint32_t tokens;            /* of the code (src/bm/tokens.h) */
    int left;                   /* the player left it (Esc's menu, PS, Start+Select, 'q'), not
                                 * its time nor quit() nor an error (the kernel's tests: stopped) */
} bm_stats_t;

/*
 * Runs a native cartridge: screen at the cartridge resolution in RGB565,
 * double buffered, 60 frames per second; _init() once, then _update() and
 * _draw() every frame. Serial keys: arrows or w/a/s/d, space/j = A,
 * k/x = B, q = quit. Returns after `seconds`, on 'q' or on an error.
 */
void bm_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, bm_stats_t *st);
/* For the next bm_run / bm_play only (kernel benchmarks): the screen size
 * instead of the cartridge's (0, 0: its own), who draws the 3D (-1: the
 * setting, 0: the ARM, 1: the GPU), and a number the cartridge reads as
 * the global BENCH (0: none). */
void bm_next_run(int w, int h, int gpu3d, int bench);
/* The same; with `suspendable`, leaving with Esc / PS / Start+Select / 'q'
 * keeps the cartridge frozen in memory (BM_SUSPENDED) instead of closing
 * it (quit(), an error or the time limit still close it). Starting a
 * cartridge closes the suspended one first. */
enum { BM_ENDED = 0, BM_SUSPENDED = 1 };
/* The system's notice over a game (src/kernel/notice.c: a kernel arriving
 * over the network, the restart counted down): 1 and its title, line (64
 * bytes each) and progress (0..1000, -1) while there is one. The kernel
 * gives it at boot; without one (bmhost) there is none. */
void bm_set_notice(int (*fn)(char *title, char *detail, int *progress));
/* The console's battery (the RGB30: src/rgb30/battery.c; none on the Pi):
 * 1 and its charge (0..100), on the charger, low (20% or less off the
 * charger); 0 if unknown. Called every frame: it must be cheap. It gives
 * battery() / battery_low() to the games, and while it is low a small red
 * battery over the frame (bm/config.txt battery_icon=0: not). */
void bm_set_battery(int (*fn)(int *pct, int *charging, int *low));
/* The first time a game uses the network or report(), the player is asked
 * (the answer kept in bm/config.txt): the kernel turns it on; off, every
 * game may (bmhost, the tests on the PC). */
void bm_permissions(int on);

int  bm_run(framebuffer_t *fb, const uint8_t *data, size_t len,
             uint32_t seconds, bm_stats_t *st, int suspendable);
/* Continues the suspended cartridge from the frame it stopped at. */
int  bm_resume(framebuffer_t *fb, uint32_t seconds, bm_stats_t *st);
/* 1 if a cartridge is suspended (its title in `title`). */
int  bm_suspended(char *title, size_t n);
/* Closes the suspended cartridge and frees its memory. */
void bm_close_suspended(void);
void bm_print_stats(const bm_stats_t *st);

/* Pure C worst case: full-screen map + 256 16x16 sprites + text, at
 * 640x360 RGB565. Shows it on screen for `frames` frames; returns the
 * average drawing time in microseconds. */
uint32_t bm_bench(framebuffer_t *fb, uint32_t frames);
/* bm_bench drawing directly and via RAM; prints one line. */
void bm_bench_report(framebuffer_t *fb, uint32_t frames);

/* Switches the screen to w x h RGB565 double buffered and points g at the
 * back page, or at a cached RAM buffer when "via RAM" is on (console
 * suspended); present shows the frame; leave restores the w x h console. */
int  bm_video_enter(framebuffer_t *fb, int w, int h, g16_t *g);
void bm_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h);
/* Shows the frame drawn in g (copying it first when drawing via RAM) and
 * points g at the next one. Returns the copy time in microseconds (0 when
 * drawing directly). */
uint32_t bm_video_present(framebuffer_t *fb, g16_t *g);
/* Draw target: 0 = framebuffer back page (default), 1 = RAM buffer + copy. */
void bm_set_via_ram(int on);
int  bm_via_ram(void);
/* The dev kit: the performance overlay over the games (fps, ms, Lua
 * instructions, the time of the last frames): 0 off, 1 simple, 2 detailed
 * (each phase of the frame, the GPU); F11 or 'p' goes round them too. */
void bm_set_perf(int level);
int  bm_perf(void);
/* 1 while the running cartridge draws into a RAM buffer (lights). */
int  bm_video_uses_ram(void);

/* Copy "via RAM" frames by DMA (after the DMA test passed). */
void bm_set_dma_frames(int on);

/* Editor support: what cart_arg() returns in the next cartridge, the
 * file a cartridge asked to play with cart_run() (0 if none), and the
 * error the last cartridge stopped with ("" if none). */
void bm_set_arg(const char *path, const char *error);
int  bm_take_run(char *path, size_t n);
/* the tool a tool asked for with cart_tool(name, path) (the path: bm_take_run) */
int  bm_take_tool(char *name, size_t n);
const char *bm_last_error(void);
/* cart_arg().back: the editor comes back from trying a game (1), or opens
 * the file it was given from the menu (0). */
void bm_set_arg_back(int back);
/* cart_arg().run: the dev kit's numbers of the game just tried (frames,
 * fps, ms, memory, tokens), or NULL for none */
void bm_set_arg_run(const bm_stats_t *st);
/* cart_arg().from: the tool that opened this one with cart_tool() ("sdk",
 * "studio", ...: the menus offer the way back), or NULL */
void bm_set_arg_from(const char *tool);
/* The next cartridges are development tools built into the kernel (the SDK,
 * bm Code, the Sound editor, the 3D studio): cart_save, cart_write and
 * cart_put_audio write where they are told. Off (the default), as for every
 * cartridge from the SD card or the Market, they write only .bm files in
 * /carts. */
void bm_set_tool(int on);

/* The console's screen is square (the RGB30's panel; on by itself there):
 * screen() offers 360x360 and 720x720 instead of the 16:9 modes, and a
 * square page is shown whole, not in the middle of a 16:9 one. bmhost
 * --square sets it on the PC. */
void bm_set_square_panel(int on);

/* The save file of a cartridge ("/bm/save/1A2B3C4D.SAV", CRC-32 of its
 * title and author as in the header): its slot 1. */
void bm_save_path(const char *title, const char *author, char *out, size_t n);

/* The file of save slot 1..BM_SAVE_SLOTS from that of slot 1: the same
 * name with ".S02" ... ".S08" (save(t, slot) of the cartridges). */
#define BM_SAVE_SLOTS 8
void bm_save_slot(const char *path, int slot, char *out, size_t n);

#endif
