#ifndef BM_ROOMBENCH_H
#define BM_ROOMBENCH_H

#include "drivers/fb.h"

/*
 * Texture Room benchmark (M30): the Texture Room cartridge, built into the
 * kernel, with its crates doubled from 8 while it keeps 30 fps, at 320x180
 * and then at 640x360, drawn by the ARM and then by the GPU (where there
 * is one). Every step runs 2 s; its line goes to the console, and at the
 * end a summary: the most crates at 60 and at 30 fps for each case.
 * Esc / PS / Start+Select during a step stops the benchmark.
 */
void bm_room_bench(framebuffer_t *fb);

#endif
