/*
 * The RGB30's display modes (drivers/fb.h on the AArch64 build). Every
 * framebuffer can be drawn by the CPU or rendered by a GPU, laid out as
 * the Mali-G52 wants its render targets: rows aligned to 64 bytes, the
 * height rounded up to 16-pixel tiles, each page on a 64 KiB boundary in
 * the display and GPU memory (plat.h: physically contiguous, uncached),
 * fb->bus its physical address. The display controller scales the image
 * to the panel: a game can render at 720x720, or at 360x360 shown twice
 * as big (a quarter of the pixels for the GPU).
 */
#ifndef RGB30_DISPLAY_H
#define RGB30_DISPLAY_H

#include "drivers/fb.h"
#include "plat.h"

#define FB_ROW_ALIGN    64u
#define FB_TILE         16u
#define FB_PAGE_ALIGN   0x10000u

/* w x h pixels, depth 32 or 16, 1 to 3 pages, shown scale times bigger
 * (1..8; 0: as big as fits the panel, keeping the shape), nearest
 * neighbour or smooth. 0, or -1 (no room), -2 (bad size), -3 (the display
 * refused it). */
#define FB_FILL 0u
int fb_init_mode(framebuffer_t *fb, uint32_t w, uint32_t h, uint32_t buffers, uint32_t depth,
                 uint32_t scale, int smooth);
/* the mode on screen now */
const plat_mode_t *fb_mode(void);

/* A cartridge's screen (src/bm): w x h RGB565, three pages, as big as the
 * panel allows (bm_scale=int in bm/config.txt: whole multiples only, the
 * pixels all the same size; bm_smooth=1: smooth instead of sharp). */
int fb_init_game(framebuffer_t *fb, int w, int h);

#endif
