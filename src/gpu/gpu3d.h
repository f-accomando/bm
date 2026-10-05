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
/* the screen's size (the guard band of the 12.4 corners: up to 1920x1080) */
void gpu3d_set_size(int w, int h);

int gpu3d_pending(void);            /* triangles waiting for a flush */

/* The page holds nothing but colour c (RGB565: a cls() since the last
 * job): the next job clears its tiles to c instead of loading the page
 * (less memory traffic). gpu3d_page(0, 0) when anything else is drawn on
 * the page or another page is drawn on. */
void gpu3d_page(int uniform, uint16_t c);
/* 1 if a job has cleared the page to the colour of gpu3d_page(1, c) since
 * the last call (a cls() the ARM did not have to draw, M39); then 0 */
int gpu3d_cleared(void);
void gpu3d_drop(void);              /* forgets them and the depth (the cartridge ended) */

/* Draws the waiting triangles into g's page. keep: the 3D goes on in this
 * frame after some 2D, so the depth is kept for the next job (stored and
 * loaded again unless zclear() comes first); 0 at the end of a frame (the
 * next frame starts from a clear depth). 0, or -1 if the GPU failed (it
 * stays off: gpu3d_ready() is 0 and gpu3d_status() says why). */
int gpu3d_flush(const g16_t *g, int keep);

/* M35, the frame in the queue: the 3D waiting (as gpu3d_flush(g, keep))
 * started on the V3D and not waited for, when the probe saw it work and
 * gpu3d_set_queue(1) asked for it; else gpu3d_flush(g, keep). Before the
 * ARM touches the page (2D, reading it, showing it) gpu3d_sync() waits
 * for it; the driver waits by itself before its next job or before
 * changing what the job reads (meshes, textures). With nothing waiting, a
 * job started before is not waited for. gpu3d_inflight(): a job started
 * and not waited for yet. */
int gpu3d_submit(const g16_t *g, int keep);
int gpu3d_sync(void);
/* M39: gpu3d_sync() if the started job draws on the page at px (the ARM is
 * about to touch it), else nothing: the job of another page goes on (its
 * memory, the meshes and textures it reads are waited for by the driver) */
int gpu3d_sync_page(const void *px);
int gpu3d_inflight(void);
void gpu3d_set_queue(int on);
int gpu3d_queue(void);                  /* on: asked for and possible */
int gpu3d_queue_ok(void);               /* the probe saw a started job end right */
int gpu3d_zclear_ok(void);              /* the probe saw zclear() inside a job work (with the queue) */
/* M39: gpu3d_set_queue(2): the queue and two jobs in flight, each in its
 * own memory: the ARM fills the next frame's job while the GPU draws this
 * one (bm3d 5.1). gpu3d_queue2(): asked for and possible. */
int gpu3d_queue2(void);

/* M35, step 2: the memory of the jobs (lists, records, uniforms, vertices)
 * uncached, the ARM's writes merged by the write buffer, no cache line read
 * for them (gpu3d_wc=1, off until the Pi shows it pays). gpu3d_wc(): it is
 * (the MMU changed it). */
void gpu3d_set_wc(int on);
int gpu3d_wc(void);

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

/* M37: the GPU's textures filtered (bilinear) instead of the nearest texel
 * as the ARM draws them (gpu3d_filter=1: it changes how they look);
 * gpu3d_bilinear() what was asked. */
void gpu3d_set_bilinear(int on);
int gpu3d_bilinear(void);

/* M39: opaque sheets as RGB565 textures in T-format (gpu3d_tex16=1), where
 * the probe learned the layout: half the memory, half the TMU's reads, the
 * same colours. gpu3d_tex16(): asked for and possible. */
void gpu3d_set_tex16(int on);
int gpu3d_tex16(void);

/* M39: the opaque textured faces shaded with two threads a QPU
 * (gpu3d_fs2=1), where the probe saw them draw the same pixels: one thread
 * runs while the other waits for its texel. gpu3d_fs2(): asked for and
 * possible. */
void gpu3d_set_fs2(int on);
int gpu3d_fs2(void);

/* M39: the draws of opaque meshes by the vertex shader written into the
 * job nearest first (gpu3d_sort=1): the GPU's early z throws away the
 * pixels they hide before shading them. The same picture but where two
 * faces have the same depth. */
void gpu3d_set_sort(int on);
int gpu3d_sort(void);
int gpu3d_tiles(void);

/* M36: whole meshes placed by the GPU's vertex shader instead of a
 * triangle at a time by r3d. gpu3d_vshader(): the probes drew with it (1),
 * also clipped and with lit models (2), or not (0); gpu3d_set_vshader() asks for it (off until verified on the
 * Pi): 1 the meshes unlit or with baked light (a map), 2 also the models
 * lit by the sun, with their skeletons (heroes); gpu3d_vshader_on() what
 * was asked if possible, else 0. */
int gpu3d_vshader(void);
void gpu3d_set_vshader(int on);
int gpu3d_vshader_on(void);

/* What the start-up probes of the optional things saw (the vertex
 * shader's, the clipping's, the queue's, zclear() in a job), "" when all
 * went as expected; the GPU test puts it in its report. */
const char *gpu3d_probe_log(void);

/* M37: 2D over the 3D in the same job, drawn by the GPU pixel for pixel as
 * the ARM draws it (no depth test or write): a rectangle [x0, x1) x [y0,
 * y1) of the page (after the camera, cut to g's clip rectangle); the
 * rectangle (sx, sy, sw, sh) of a sheet at (dx, dy), zoom times bigger,
 * transparent texels left out (g16_sspr); text as g16_text (scale 1) or
 * g16_text_scaled, in g's font. 1 if the GPU took it, 0 if it cannot (the
 * ARM draws it, after the job). */
int gpu3d_rect2d(const g16_t *g, int x0, int y0, int x1, int y1, uint16_t c);
int gpu3d_blit2d(const g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh, int dx, int dy, int zoom,
                 int flip_x, int flip_y);
int gpu3d_text2d(const g16_t *g, int x, int y, const char *str, uint16_t c, int scale);

/* M37: the w x h RGB565 frame src scale times bigger on page (as many
 * pixels as w * scale x h * scale; the menu at 1920x1080 from its
 * 640x360 layout), the nearest pixel, by the GPU: 0, or -1 (then the ARM
 * enlarges it) */
int gpu3d_enlarge(const uint16_t *src, int w, int h, int scale, const g16_t *page);

typedef struct {
    uint32_t jobs, tris, bin_us, render_us, max_us;
    uint32_t zjobs;                 /* jobs that loaded or stored the depth */
    uint32_t cleared;               /* jobs that cleared the page instead of loading it */
    uint32_t msjobs;                /* jobs with MSAA 4x */
    uint32_t glmeshes;              /* meshes placed by the vertex shader (M36) */
    uint32_t gltris;                /* their triangles (in tris too) */
    uint32_t queued;                /* jobs started and waited for later (M35) */
    uint32_t zinjob;                /* zclear() inside a job (M35) */
    uint32_t quads2d;               /* 2D drawn by the GPU over the 3D (M37) */
    uint32_t glverts;               /* corners the vertex shader is given (M39: indexed, once each) */
    uint32_t overlapped;            /* jobs filled while the one before was drawn (M39, two blocks) */
    uint32_t sorted;                /* mesh draws moved by the nearest-first order (M39) */
} gpu3d_stats_t;

/* totals since the last call, then zeroed */
void gpu3d_take_stats(gpu3d_stats_t *s);
/* the same totals, left as they are (the dev kit's overlay) */
void gpu3d_peek_stats(gpu3d_stats_t *s);

#endif
