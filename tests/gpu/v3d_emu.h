#ifndef V3D_EMU_H
#define V3D_EMU_H

#include <stddef.h>
#include <stdint.h>

/* see v3d_emu.c */
extern int emu_red_a, emu_tex_swap, emu_tformat, emu_ms_load_one, emu_skip;
typedef struct { uint32_t jobs, prims, pixels, batches, zstores, loads, msframes; } emu_stats_t;
extern emu_stats_t emu_stats;
extern char emu_error[256];

void *test_aligned_alloc(size_t align, size_t size);
void test_free(void *p);

#endif
