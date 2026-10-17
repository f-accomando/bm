/*
 * M41: the RGB30's GPU, a Mali-G52 (Bifrost) in the RK3566, without an
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
 *   mmu        address space 0: the GPU's memory mapped 1:1 in 2 MiB blocks
 *              (the Mali LPAE format, the one Linux's panfrost uses on the
 *              RK3568)
 *   job        a WRITE_VALUE job on job slot 1, then a chain of two (the
 *              second waits for the first)
 *   fragment   (bm3d 6.1) a FRAGMENT job on job slot 0 with no draw: the
 *              tiles cleared to a colour and written to a surface (no
 *              shader, no tiler), then a square on the screen
 *   triangle   (M41, after 6.1) a VERTEX job, a TILER job and a FRAGMENT job:
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

typedef struct {
    uint32_t (*rd)(uintptr_t addr);
    void (*wr)(uintptr_t addr, uint32_t v);
    uint32_t (*us)(void);               /* a clock in microseconds */
    void (*log)(const char *line);      /* a line of the probe (no newline) */
    int (*supply_mv)(void);             /* vdd_gpu: mV, 0 off, -1 unknown (NULL: not asked) */
    uint8_t *mem;                       /* the GPU's memory as the CPU sees it (uncached) */
    uint64_t mem_pa;                    /* its physical address (2 MiB aligned) */
    uint32_t mem_size;                  /* at least 2 MiB */
    uint64_t map_pa;                    /* what the GPU sees, 1:1: from map_pa, map_size */
    uint32_t map_size;                  /* bytes (2 MiB blocks; must hold mem) */
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

/* after a probe that went through (same hw): the GPU clears a 64 x 64
 * square of rgb at the top right of the screen; 0, 1 if its pixels are not
 * there, -1 */
int mali_square(const mali_hw_t *hw, uint32_t rgb);

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
typedef struct { const float (*xyz)[3]; uint32_t n, rgb; } mali_draw_t;
int mali_frame(const mali_hw_t *hw, uint8_t *target, uint64_t target_pa, uint32_t pitch, uint32_t w, uint32_t h,
               uint32_t clear, const mali_draw_t *draws, uint32_t ndraws);
/* places in hw->mem the test reads */
#define MALI_FRAME_VERTS      0x10000   /* all vertices, 4 floats each (x y z 1), draw after draw */
#define MALI_FRAME_POSITIONS  0x20000   /* what the vertex jobs write, same order */
#define MALI_FRAME_PUSH       0x30000   /* per draw 64 bytes: the pushed words, colour at +16 */

#endif
