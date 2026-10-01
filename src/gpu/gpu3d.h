/*
 * The 3D of the cartridges drawn by the V3D (M30): an r3d backend.
 *
 * r3d transforms, lights, culls and clips on the ARM as before and hands
 * screen triangles to this backend, which collects them into one GPU job:
 * a binning list (batches of triangles sharing a shader, a depth mode and
 * a texture) and the vertices in the NV format. gpu3d_flush() runs the job
 * on the page being drawn: every tile is loaded from the page (what the
 * ARM drew stays under the 3D), gets its triangles with a 24-bit depth
 * buffer cleared at the start, and is stored back. The runtime flushes
 * before any 2D drawing that follows 3D, before reading pixels, and at the
 * end of the frame; zclear() starts a new job.
 *
 * Off by default: gpu3d=1 in bm/config.txt, or Settings > 3D drawing.
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
void gpu3d_drop(void);              /* forgets them (the cartridge ended) */

/* Draws the waiting triangles into g's page. 0, or -1 if the GPU failed
 * (it stays off: gpu3d_ready() is 0 and gpu3d_status() says why). */
int gpu3d_flush(const g16_t *g);

typedef struct {
    uint32_t jobs, tris, bin_us, render_us, max_us;
} gpu3d_stats_t;

/* totals since the last call, then zeroed */
void gpu3d_take_stats(gpu3d_stats_t *s);

#endif
