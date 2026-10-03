#ifndef GPU_VERSION3D_H
#define GPU_VERSION3D_H

/*
 * The version of bm's 3D drivers, "bm3d X.Y": X the block of work (a
 * milestone), Y its step (docs/DRIVERS.md). The drivers are r3d (scene,
 * transform, light, the ARM's rasterizer) and gpu3d (the V3D's backend and
 * shaders); one version for both. A new block or step raises it and adds a
 * line to the table below.
 *
 * The settings reproduce older versions on today's code (the "mode"): the
 * ARM alone is 0.2, the GPU without its vertex shader 2.1, with it for the
 * scenery 3.0, for every model 3.4, with the frame in the queue 4.0. 0.1
 * and 1.0 no longer run: the 3D Bench shows the numbers measured on the Pi
 * with them.
 */
#define BM3D_VERSION "4.0"
#define BM3D_BLOCK   "M35"

typedef struct {
    const char *version, *block, *date, *what;
} bm3d_version_t;

/* every version, oldest first; *n of them */
static inline const bm3d_version_t *bm3d_versions(int *n)
{
    static const bm3d_version_t v[] = {
        { "0.1", "M7-M32", "2026-09", "ARM only: r3d's first rasterizer (the September stress test)" },
        { "0.2", "M33", "2026-10-01", "ARM only: edges in fixed point, specialized texture loops, DMA z clear" },
        { "1.0", "M33", "2026-10-01", "the GPU draws the triangles, the ARM places and lights them" },
        { "2.0", "M34", "2026-10-01", "cleared pages, textures in T-format, MSAA 4x, less ARM a triangle" },
        { "2.1", "M34", "2026-10-03", "Overbit on the GPU: screen-door, baked RGB light, shadows, effects" },
        { "3.0", "M36", "2026-10-03", "vertex shader for the scenery, clipping on the GPU" },
        { "3.1", "M36", "2026-10-03", "vertex shader for models lit by the sun, with bones (heroes)" },
        { "3.2", "M36", "2026-10-03", "vertex shader for shadows and the first-person layer" },
        { "3.3", "M36", "2026-10-03", "textured models lit by the sun: Gouraud, on the vertex shader too" },
        { "3.4", "M36", "2026-10-03", "skins on the vertex shader: faces on two bones (the Meshy heroes)" },
        { "4.0", "M35", "2026-10-03", "the frame in the queue: the GPU draws while the game's next update runs" },
    };
    *n = (int)(sizeof v / sizeof v[0]);
    return v;
}

/* the version a renderer reproduces: gpu (the V3D draws), vs (its vertex
 * shader: 0, 1 the scenery, 2 every model), queue (the frame in the queue,
 * M35) */
static inline const char *bm3d_mode_q(int gpu, int vs, int queue)
{
    if (!gpu)
        return "0.2";
    if (queue)
        return "4.0";
    return vs >= 2 ? "3.4" : vs == 1 ? "3.0" : "2.1";
}

static inline const char *bm3d_mode(int gpu, int vs)
{
    return bm3d_mode_q(gpu, vs, 0);
}

#endif
