/*
 * The 3D of the cartridges drawn by the V3D (M33): an r3d backend.
 *
 * r3d transforms, lights, culls and clips on the ARM as before and hands
 * screen triangles to this backend, which collects them into one GPU job:
 * a binning list (batches of triangles sharing a shader, a depth mode and
 * a texture) and the vertices in the NV format. gpu3d_flush() runs the job
 * on the page being drawn: every tile is loaded from the page (what the
 * ARM drew stays under the 3D), gets its triangles with a 24-bit depth
 * buffer cleared at the start (or loaded, when 2D drawing split the 3D of
 * a frame), and is stored back. The runtime flushes before any 2D drawing
 * that follows 3D, before reading pixels, and at the end of the frame;
 * zclear() starts a new job.
 *
 * The default where a V3D answers (verified on the Pi, 2026-10-01); gpu3d=0
 * in bm/config.txt (Settings > 3D of the games: ARM) keeps the ARM's
 * rasterizer, which also takes over when the V3D is absent or fails.
 */
#ifndef GPU3D_H
#define GPU3D_H

#include <stdint.h>

#include "bm/r3d.h"

/* V3D on, memory, shaders, and a probe of the colour byte order (a clear
 * and a texture drawn into a small buffer). 0, or -1 (gpu3d_status says
 * why: no V3D, no memory, the probe failed). */
int gpu3d_init(void);
const char *gpu3d_status(void);
int gpu3d_ready(void);              /* initialized and not failed since */
int gpu3d_failed(void);             /* failed: off until the next restart */

const r3d_backend_t *gpu3d_backend(void);

/* The framebuffer (all its pages) and its bus address as the firmware
 * gave it: pages of the screen are given to the V3D with that address,
 * buffers in ARM memory (drawing via RAM) with the L2-cached alias. */
void gpu3d_set_fb(const void *mem, uint32_t size, uint32_t bus);

int gpu3d_pending(void);            /* triangles waiting for a flush */

/* The page holds nothing but colour c (RGB565: a cls() since the last
 * job): the next job clears its tiles to c instead of loading the page
 * (less memory traffic). gpu3d_page(0, 0) when anything else is drawn on
 * the page or another page is drawn on. */
void gpu3d_page(int uniform, uint16_t c);
void gpu3d_drop(void);              /* forgets them and the depth (the cartridge ended) */

/* Draws the waiting triangles into g's page. keep: the 3D goes on in this
 * frame after some 2D, so the depth is kept for the next job (stored and
 * loaded again unless zclear() comes first); 0 at the end of a frame (the
 * next frame starts from a clear depth). 0, or -1 if the GPU failed (it
 * stays off: gpu3d_ready() is 0 and gpu3d_status() says why). */
int gpu3d_flush(const g16_t *g, int keep);

/* Anti-aliasing (MSAA 4x) for the next jobs, where the probe allows it:
 * gpu3d_msaa() is 0 (no MSAA), 1 (on pages of one colour: cleared, not
 * loaded) or 2 (on any page). Never in a job that keeps the depth. */
void gpu3d_set_msaa(int on);
int gpu3d_msaa(void);
int gpu3d_msaa_on(void);            /* asked for and possible (on any page) */

/* The GPU test: textures in T-format where the probe learned it (1), or in
 * rows (0); the textures made so far are made again. gpu3d_tiles(): the
 * probe learned T-format. */
void gpu3d_tiled_textures(int on);
int gpu3d_tiles(void);

/* M36: whole meshes placed by the GPU's vertex shader instead of a
 * triangle at a time by r3d. gpu3d_vshader(): the probe drew with it (1)
 * or not (0); gpu3d_set_vshader() asks for it (off until verified on the
 * Pi): 1 the meshes unlit or with baked light (a map), 2 also the models
 * lit by the sun, with their skeletons (heroes); gpu3d_vshader_on() what
 * was asked if possible, else 0. */
int gpu3d_vshader(void);
void gpu3d_set_vshader(int on);
int gpu3d_vshader_on(void);

typedef struct {
    uint32_t jobs, tris, bin_us, render_us, max_us;
    uint32_t zjobs;                 /* jobs that loaded or stored the depth */
    uint32_t cleared;               /* jobs that cleared the page instead of loading it */
    uint32_t msjobs;                /* jobs with MSAA 4x */
    uint32_t glmeshes;              /* meshes placed by the vertex shader (M36) */
    uint32_t gltris;                /* their triangles (in tris too) */
} gpu3d_stats_t;

/* totals since the last call, then zeroed */
void gpu3d_take_stats(gpu3d_stats_t *s);

#endif
