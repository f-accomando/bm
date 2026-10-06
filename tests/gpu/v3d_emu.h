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
extern int emu_hang_zclear;             /* a job with fs_zclear does not end (a GPU without it) */
typedef struct { uint32_t jobs, prims, pixels, batches, zstores, loads, msframes, glverts, async, glindexed, threaded, vmiss,
                  ms_early; } emu_stats_t;   /* ms_early: primitives with early z in MSAA tiles */
extern emu_stats_t emu_stats;
extern char emu_error[256];

void *test_aligned_alloc(size_t align, size_t size);
void test_free(void *p);
int test_arena_overruns(void);          /* blocks written past their end (their guard changed) */

#endif
