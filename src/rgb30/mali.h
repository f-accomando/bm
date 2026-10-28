/*
 * The RGB30's GPU, a Mali-G52 (Bifrost) in the RK3566, without an
 * operating system. First step (bm3d 6.0): the GPU found and powered, its
 * memory mapped, jobs run by its job manager; no drawing yet.
 *
 *   supply     vdd_gpu (the RK817's DCDC2) on, else nothing is touched
 *   power      the clocks of PD_GPU (CRU) and the power domain (PMU), the
 *              bus out of idle, as Linux's rockchip power domains do
 *   identify   GPU_ID and the parts present (cores, L2, tiler, address
 *              spaces, job slots)
 *   reset      a soft reset (a hard one if it does not end)
 *   cores      L2, shader cores and tiler powered on
 *   mmu        address space 0: the GPU's memory mapped 1:1 in 2 MB blocks
 *              (the Mali LPAE format, the one Linux's panfrost uses on the
 *              RK3568)
 *   job        a WRITE_VALUE job on job slot 1, then a chain of two (the
 *              second waits for the first)
 *   fragment   (bm3d 6.1) a FRAGMENT job on job slot 0 with no draw: the
 *              tiles cleared to a colour and written to a surface (no
 *              shader, no tiler), then a square on the screen
 *   triangle   (after bm3d 6.1) a VERTEX job, a TILER job and a FRAGMENT job:
 *              three positions through the shaders of mali_shaders.h into
 *              a 96 x 96 square of the screen (mali_desc.c has the
 *              descriptors)
 *
 * Every wait has a timeout and every step writes a line; the first step
 * that fails ends the probe (nothing after it touches the GPU). The GPU's
 * registers are read only once its power domain is on. Portable: the
 * registers and the clock come through mali_hw_t (tests/rgb30/mali_test.c
 * runs it on a simulated GPU).
 */
#ifndef RGB30_MALI_H
#define RGB30_MALI_H

#include <stdint.h>

#include "bm/r3d.h"
#include "gpu/gpu3d.h"

typedef struct {
    uint32_t (*rd)(uintptr_t addr);
    void (*wr)(uintptr_t addr, uint32_t v);
    uint32_t (*us)(void);               /* a clock in microseconds */
    void (*log)(const char *line);      /* a line of the probe (no newline) */
    int (*supply_mv)(void);             /* vdd_gpu: mV, 0 off, -1 unknown (NULL: not asked) */
    uint8_t *mem;                       /* the GPU's memory as the CPU sees it (uncached) */
    uint64_t mem_pa;                    /* its physical address (2 MB aligned) */
    uint32_t mem_size;                  /* at least 2 MB */
    uint64_t map_pa;                    /* what the GPU sees, 1:1: from map_pa, map_size */
    uint32_t map_size;                  /* bytes (2 MB blocks; must hold mem) */
    uint8_t *screen;                    /* the page shown (XRGB8888), for a square the GPU clears: */
    uint64_t screen_pa;                 /* its physical address (inside the map); 0: none */
    uint32_t screen_w, screen_h, screen_pitch;
    int triangle;                       /* also draw a triangle when the clear has worked: not on a simulated GPU */
} mali_hw_t;

/* the registers' places on the RK3566 */
#define MALI_GPU_BASE   0xfde60000u
#define MALI_CRU_BASE   0xfdd20000u
#define MALI_PMU_BASE   0xfdd90000u

/* the probe's steps; mali_probe() returns MALI_OK or the one that failed */
enum {
    MALI_OK = 0, MALI_E_POWER = -1, MALI_E_ID = -2, MALI_E_RESET = -3, MALI_E_CORES = -4, MALI_E_MMU = -5,
    MALI_E_JOB = -6, MALI_E_CHAIN = -7, MALI_E_MEMORY = -8, MALI_E_SUPPLY = -9, MALI_E_FRAGMENT = -10,
    MALI_E_TRIANGLE = -11,
};

typedef struct {
    uint32_t gpu_id;                    /* GPU_ID: product (bits 31-16), major, minor, status */
    uint64_t shader_present, tiler_present, l2_present;
    uint32_t as_present, js_present, mmu_features, core_features, l2_features, thread_max;
    uint32_t js_status, job_header;     /* of the first job: JS_STATUS, exception status written back */
    uint32_t job_us, chain_us, frag_us; /* how long the jobs took */
    uint32_t frag_pixel;                /* a pixel the fragment job wrote */
    uint32_t tri_us, tri_in, tri_out;   /* the triangle's jobs, a pixel inside it and one outside (RGB) */
    int supply_mv;                      /* vdd_gpu as the PMIC said (-1 unknown) */
    int step;                           /* the last step reached (MALI_OK: all) */
} mali_info_t;

int mali_probe(const mali_hw_t *hw, mali_info_t *info);

/* the name of an exception status (JS_STATUS, AS_FAULTSTATUS & 0xff) */
const char *mali_exception(uint32_t code);

/* "Mali-G52 r1p0" from GPU_ID */
const char *mali_name(uint32_t gpu_id, char *buf, int n);

/* M41 phase 2: a frame of up to MALI_MAX_DRAWS draws, each n triangles of ONE colour (xyz = 3 floats a vertex, in
 * pixels of the surface, z in 0..1, 3n vertices; rgb 0xRRGGBB), at most MALI_BATCH_MAX triangles in all. The surface is
 * w x h XRGB8888 (w, h multiples of 16, at most 1920 x 1080) at target (CPU pointer) / target_pa (what the GPU sees,
 * 64-aligned) with `pitch` bytes a row; it is first cleared to `clear` (0xRRGGBB): what was there is not kept. Draws are
 * drawn in the order given (no depth test). After a good mali_probe with the same hw. 0 drawn, 1 the jobs ended but the
 * first draw's first triangle centre is still `clear`, -1 failed or refused. */
#define MALI_MAX_DRAWS  64
#define MALI_BATCH_MAX  1024
typedef struct {
    const float (*xyz)[3];
    uint32_t n, rgb;
    const float *mvp;   /* NULL: xyz are pixels (phase 2); else a 4x4 matrix by rows, xyz are model-space and the GPU transforms them (phase 3) */
    /* G1, textures: uvk non-NULL makes a textured draw (the fast path's arena only; mvp NULL; rgb unused): xyz are then x and y
     * in pixels and z = 1/depth (> 0), uvk = u and v (0..1 over the texture, the nearest texel, clamped) and the light k (the
     * texel times k) of each vertex; tex_pa is the GPU address of the texture block (mali_backend's pool: Texture at +0x00,
     * Sampler at +0x80). The GPU interpolates u, v and k with perspective. */
    const float (*uvk)[3];
    uint64_t tex_pa;
    /* Q2, lit meshes (the arena only; mvp set): litv non-NULL makes a draw whose vertex shader lights the faces. xyz are model-space,
     * litv[i] is the face normal in model space, an emissive flag (1: no light) and the base r g b in 0..255 of vertex i; light is
     * 20 floats: sun, up (vec3 + 1), then A, B, D per channel (vec3 + 1): light = A + B (n . up) + D max(n . sun, 0). */
    const float (*litv)[12];
    const float *light;     /* 88 floats, see be_set_light */
    int litx;               /* 1: the draw has rim, lamps, gloss or fog: BM_VS_LITX, vertices of 64 bytes (litv[i][8..10]: the face centre);
                             * 2: a skinned mesh, BM_VS_LITB: litv[i][11] is the bone, mvp and light are those of bone 0 and the next
                             * nbones - 1 (16 and 88 floats each) */
    int nbones;
    uint32_t uoff;          /* the draw's uniform words, in bytes from the start of the lit area (the arena only) */
} mali_draw_t;
int mali_frame(const mali_hw_t *hw, uint8_t *target, uint64_t target_pa, uint32_t pitch, uint32_t w, uint32_t h,
               uint32_t clear, const mali_draw_t *draws, uint32_t ndraws);
int mali_probe_vs3(const mali_hw_t *hw);
int mali_probe_async(const mali_hw_t *hw);
/* The kernel gives the cores that build the draws of a frame (docs/progress/multicore.md); without them one core builds. */
void mali_par_set(int (*workers)(void), int (*par)(void (*fn)(void *), void *const *, unsigned));
/* The GPU memory in sets (M41 phase 4): set s (0 or 1) starts at MALI_SET_BASE(s) in hw->mem and holds, from there, its tables
 * and descriptors (+0x0000), the shader code (+0x8000), the vertices (+0x10000), the positions (+0x20000), the pushed words
 * (+0x30000), the per-draw blocks (+0x40000), the tiler heap (+0x80000) and the surface (+0x100000, up to MALI_SET_SURF_MAX
 * bytes). mali_frame uses set 0. hw->mem_size must reach MALI_SET_BASE(2) for two sets. */
#define MALI_SET_SIZE         0x190000
#define MALI_SET_BASE(s)      (0x80000u + (uint32_t)(s) * MALI_SET_SIZE)
#define MALI_SET_SURFACE      0x100000
#define MALI_SET_SURF_MAX     0x90000   /* 368 x 368 x 4 with the pitch 64-aligned is 0x84400 */
/* places in hw->mem the test reads (set 0's) */
#define MALI_FRAME_DRAWS      (MALI_SET_BASE(0) + 0x40000)  /* draw d: its Vertex job at +0x800 d, its Tiler job at +0x100 */
#define MALI_FRAME_VERTS      (MALI_SET_BASE(0) + 0x10000)  /* all vertices, 4 floats each (x y z 1), draw after draw */
#define MALI_FRAME_POSITIONS  (MALI_SET_BASE(0) + 0x20000)  /* what the vertex jobs write, same order */
#define MALI_FRAME_PUSH       (MALI_SET_BASE(0) + 0x30000)  /* per draw 64 bytes: the pushed words, colour at +16 */
#define MALI_FRAME_VPUSH      (MALI_SET_BASE(0) + 0x38000)  /* per draw 64 bytes: the vertex shader's 16 pushed words (the matrix, by rows) */

/* M41 phase 4: frames in flight. Two sets: a frame is built in the free one while the other may still be drawing.
 * mali_frame_submit takes the arguments of mali_frame except the surface: it is the set's own (368 x 368 at most), returned in
 * *surface / *pitch for the caller to read after the wait; it stays valid until a submit reuses its set (the second submit
 * after it). 0 submitted, -1 refused (both sets busy, a bad argument, the surface too big: use mali_frame), -2 failed to
 * start (not used yet: a job's fault shows at mali_frame_busy / mali_frame_wait). mali_frame refuses while frames fly. */
int mali_frame_submit(const mali_hw_t *hw, uint32_t w, uint32_t h, uint32_t clear, const mali_draw_t *draws,
                      uint32_t ndraws, uint8_t **surface, uint32_t *pitch);
int mali_frame_busy(void);                      /* frames still drawing (0..2); advances the state machine (polls the slots) */
int mali_frame_wait(uint32_t timeout_us);       /* until none is: 0, -1 timeout or a job fault (the lines are in the log) */

/* M41 phase 2: the r3d backend of the Mali: flat colour triangles only (r3d hands everything else to the ARM: W13b).
 * `hw` must be the one a good mali_probe used. NULL if not probed. */
const r3d_backend_t *mali_backend(const mali_hw_t *hw);
int mali_backend_pending(void);                 /* triangles waiting */
int mali_backend_flush(g16_t *g);               /* draws them, copies the covered pixels into g->px; 0, or -1 (the ARM must draw this frame) */
int mali_backend_submit(g16_t *g);              /* starts the frame on the GPU so the ARM can work meanwhile; mali_backend_sync resolves it */
int mali_backend_sync(void);                    /* waits for a submitted frame and copies its covered pixels into g->px */
int mali_backend_inflight(void);                /* 1 while a submitted frame has not been synced yet */
void mali_backend_drop(void);                   /* forgets what waits */
void mali_backend_set_vshader(int on);          /* 0 off (be_tri), 1 unlit meshes, 2 lit meshes too (default 2) */
int mali_backend_vshader(void);
void mali_backend_set_bilinear(int on);         /* 1: the scene asks for filtered textures, which the Mali does not do: textures go to the ARM */
void mali_backend_set_zbuffer(int on);          /* 1: the Mali uses its depth buffer, not the far-to-near sort (config key mali_z) */
void mali_backend_set_lit(int on, int min_px);  /* 0: lit meshes are lit by the ARM (mali_lit); faces smaller than min_px on average stay on the ARM (mali_lit_min) */
void mali_backend_set_textures(int on);         /* 0: opaque textures go to the ARM, not the Mali (config key mali_tex; default 1) */
void mali_backend_set_queue(int on);            /* 0 synchronous flush, 1..2 async submit/sync (default 1) */
int mali_backend_queue(void);
void mali_backend_stats(gpu3d_stats_t *s, int reset);
uint32_t mali_backend_wait_us(void);
/* test seam: replaces mali_frame (NULL: the real one) */
void mali_backend_set_frame(int (*frame)(const mali_hw_t *, uint8_t *, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                         const mali_draw_t *, uint32_t));

#endif
