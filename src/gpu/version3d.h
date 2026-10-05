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
 * scenery 3.0, for every model 3.4, with the frame in the queue 4.1, with
 * the jobs' memory uncached 4.5 (gpu3d_wc: the 3D Bench's GPU+WC), with
 * the 2D over the 3D in the job 4.8 (gpu3d_2d: GPU+2D), with two jobs in
 * flight 5.1 (gpu3d_queue=2: GPU+VS+Q2), with 16-bit textures 5.2
 * (gpu3d_tex16: GPU+T16), with two-thread pixel shaders 5.3 (gpu3d_fs2:
 * GPU+FS2), with the meshes nearest first 5.4 and their
 * triangles in the vertex cache's order 5.6 (gpu3d_sort: GPU+VS+S). 0.1
 * and 1.0 no longer run: the 3D Bench shows the numbers measured on the Pi
 * with them.
 */
#define BM3D_VERSION "6.0"
#define BM3D_BLOCK   "M41"

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
        { "4.1", "M35", "2026-10-03", "2D after 3D recorded while the GPU draws, zclear() inside the job" },
        { "4.2", "M38", "2026-10-04", "screens up to 1920x1080 (screen()), cls() cleared by the GPU's job" },
        { "4.3", "M36", "2026-10-05", "the vertex shader's VPM offsets in bytes, as the V3D wants; the probe learns it" },
        { "4.4", "M35", "2026-10-05", "no early z after a zclear() in the job nor with MSAA; the queue without VS" },
        { "4.5", "M35", "2026-10-05", "the jobs' memory uncached, the ARM's writes merged (option)" },
        { "4.6", "M35", "2026-10-05", "up to 8 textures in a job (2 before: a third ended it)" },
        { "4.7", "M34", "2026-10-05", "textured screen-door faces on the GPU too (the last case left to the ARM)" },
        { "4.8", "M37", "2026-10-05", "2D over the 3D in the GPU's job, pixel for pixel; textures filtered (options)" },
        { "4.9", "M37", "2026-10-05", "the ARM's 3D: one matrix a model, lit in its own axes (option r3d_fast)" },
        { "5.0", "M39", "2026-10-05", "meshes up to 65535 vertices; indexed: a shared corner shaded once" },
        { "5.1", "M39", "2026-10-05", "two jobs in flight: a job filled while the one before is drawn (option)" },
        { "5.2", "M39", "2026-10-05", "opaque textures as RGB565 in tiles, layout learned by the probe (option)" },
        { "5.3", "M39", "2026-10-05", "textured faces shaded with two threads a QPU, checked by the probe (option)" },
        { "5.4", "M39", "2026-10-05", "the meshes' draws nearest first: early z skips the hidden pixels (option)" },
        { "5.5", "M39", "2026-10-05", "visible3d and pvs3d: characters hidden by the map skipped (Overbit)" },
        { "5.6", "M39", "2026-10-05", "each mesh's triangles in the vertex cache's order (with the draw order option)" },
        { "6.0", "M41", "2026-10-05", "RGB30: the Mali-G52 powered, mapped, running jobs (GPU test); no drawing yet" },
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
        return "4.1";
    return vs >= 2 ? "3.4" : vs == 1 ? "3.0" : "2.1";
}

static inline const char *bm3d_mode(int gpu, int vs)
{
    return bm3d_mode_q(gpu, vs, 0);
}

#endif
