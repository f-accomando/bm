#ifndef V3D_EMU_H
#define V3D_EMU_H

#include <stddef.h>
#include <stdint.h>

/* see v3d_emu.c */
extern int emu_red_a, emu_tex_swap, emu_tformat, emu_ms_load_one, emu_skip, emu_cw_flip, emu_clip;
extern int emu_need_clip;               /* GL without the clipper's flag draws nothing (as the Pi) */
extern int emu_vpm_words;               /* GL records: VPM offsets and sizes in words (else bytes) */
extern int emu_uncached;                /* v3d_uncached() asked for the jobs' memory uncached */
extern int emu_no_threads;              /* jobs with two-thread fragment shaders do not end */
/* bm3d 7.2: where the TMU finds alpha in RGBA5551 / RGBA4444 texels:
 * 0 the low bits (as the VideoCore IV and Mesa's vc4), 1 the top bits, 2
 * neither (the types read as 0: the probe must turn them off) */
extern int emu_t16_alpha;
/* 1: the TMU widens RGBA5551 / RGBA4444's colour fields to 8 bits with half
 * a step below them, not their top bits again (as the Pi, some pixels come
 * back a step off: the probe must still learn the right order) */
extern int emu_t16_lossy;
/* bm3d 7.3: the TMU's ETC1 (type 8) in T-format: 0 utiles of 2 x 4
 * blocks, each block's bytes big-endian (as the format; the probe's first
 * candidate); 1 utiles of 4 x 2 blocks, the bytes little-endian (a later
 * candidate the probe must learn); 2 none (texels read as 0: off); 3 as 0
 * with each block's rows reversed (as the Pi: the probe learns the map) */
extern int emu_etc1;
extern int emu_qpu;                     /* bm3d 7.x QPU programs: 0 run, 1 the QPUs do not answer,
                                         * 2 they run and store wrong data (the probe turns them off) */
extern int emu_hang_zclear;             /* a job with fs_zclear does not end (a GPU without it) */
typedef struct { uint32_t jobs, prims, pixels, batches, zstores, loads, msframes, glverts, async, glindexed, threaded, vmiss,
                  ms_early, attr_words,
                  qpu_jobs, qpu_steps, qpu_dma; } emu_stats_t;   /* ms_early: primitives with early z in MSAA tiles;
                           * attr_words: words the VCD loaded for vertex shaders;
                           * qpu_*: QPU programs run, their instructions, DMA stores */
extern emu_stats_t emu_stats;
extern char emu_error[256];

void *test_aligned_alloc(size_t align, size_t size);
void test_free(void *p);
int test_arena_overruns(void);          /* blocks written past their end (their guard changed) */

#endif
