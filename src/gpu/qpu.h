/*
 * QPU programs outside the drawing cycle (bm3d 7.x, Pi Zero W).
 *
 * The 12 QPUs of the V3D run small programs (src/gpu/qpuprog.h, made by
 * tools/qpuprogs.py) on large arrays: 16 floats at a time. A request is
 * queued with qpu_*_queue and run by qpu_flush; qpu_* does both. Every request
 * has an ARM version that gives the same result, and it runs when the QPU
 * path is off (no V3D, QEMU, probe failed, job timed out). The QPU path reads
 * a source array and writes another, so a run that fails is done again by the
 * ARM from the start.
 *
 * Memory given to a request must be reachable by the V3D (qpu_alloc, or the
 * frame buffer pages): the caches are cleaned around a run (v3d_qpu_run).
 */
#ifndef QPU_H
#define QPU_H

#include <stddef.h>
#include <stdint.h>

#include "bm/gfx16.h"

/* Starts the V3D, checks every program against its ARM version on a small
 * array (probe), and decides which programs may run on the QPUs. 0 if at
 * least one does; -1 else (the ARM versions still work). Once. */
int qpu_init(void);
const char *qpu_status(void);           /* e.g. "particles yes, fade yes" or why not */
int qpu_particles_ok(void);
int qpu_fade_ok(void);
/* Turns the QPU path off for good (the ARM versions go on). */
void qpu_disable(void);

/* Memory for the arrays of requests: aligned to 64 bytes, bus-reachable. */
void *qpu_alloc(size_t bytes);
void qpu_free(void *p);

typedef struct {
    uint32_t runs;              /* requests done */
    uint32_t on_qpu;            /* ... by the QPUs */
    uint32_t on_arm;            /* ... by the ARM (QPU path off, or after a failed run) */
    uint32_t failures;          /* v3d_qpu_run calls that failed */
    uint32_t jobs;              /* QPU jobs launched */
    uint32_t probe_bad;         /* programs the probe turned off */
    uint32_t busy_skips;        /* runs skipped because a 3D job was running (the ARM did them; not a failure) */
    uint32_t fade_arm_align;    /* qpu_fade_apply calls done by the ARM: a picture or table not on a 64-byte line */
    uint32_t fade_arm_shape;    /* ... by the ARM: another shape (stride, size, levels) */
} qpu_stats_t;
extern qpu_stats_t qpu_stats;

#define QPU_TIMEOUT_US  50000u  /* a run that takes longer fails (a frame is 16666 us) */

/* ---------------------------------------------------------------- particles */
/* Blocks of 16 particles; a block is 5 rows of 16 floats: x, y, vx, vy, life
 * (80 floats). One step of dt seconds with gravity g (added to vy):
 *   x += vx * dt; y += vy * dt; vy += g * dt; life = max(life - dt, 0)
 * The step reads src and writes dst (not the same array). */
#define QPU_PBLOCK 80

void qpu_particles_arm(const float *src, float *dst, uint32_t blocks, float dt, float gdt);
int  qpu_particles_queue(const float *src, float *dst, uint32_t blocks, float dt, float g);
int  qpu_particles(const float *src, float *dst, uint32_t blocks, float dt, float g);

/* ------------------------------------------------------------------- fade */
/* The levels of g16_fade_apply (src/bm/gfx16.h) for `pixels` RGB565 pixels
 * (a multiple of 32) from src to dst: each colour becomes its table's colour
 * at the pixel's level, or is scaled when it has no table. Tables as in
 * g16_fade_t: lv a level byte per pixel; index[65536] (255 = none);
 * tab[levels * 256]; mul[levels][3] scales (up to 512). */
typedef struct {
    const uint16_t *src;
    uint16_t *dst;
    uint32_t pixels;
    const uint8_t *lv, *index;
    const uint16_t *tab;
    const uint16_t (*mul)[3];
    int levels;
} qpu_fade_t;

void qpu_fade_arm(const qpu_fade_t *f);
int  qpu_fade_queue(const qpu_fade_t *f);
int  qpu_fade(const qpu_fade_t *f);

/* g16_fade_apply (src/bm/gfx16.h) done by the QPUs when they can: the same
 * picture on g. A picture whose rows are the screen's width (stride == w ==
 * f->w) goes to the QPUs in blocks of 32 pixels (the rest, and everything
 * else, to the ARM version). The QPUs write a second picture, then copied
 * back: a run that fails leaves g as it was for the ARM. 1 if the QPUs did
 * it, 0 if the ARM did. */
int qpu_fade_apply(g16_t *g, const g16_fade_t *f);

/* The hook of the runtime (bm_fade_accel, src/bm/runtime.h) for the config key
 * gpu3d_qpu=1: qpu_init on the first call, then qpu_fade_apply. If qpu_init fails
 * or one run fails (a timeout too), the picture is done by the ARM and every
 * later call goes to the ARM at once (the QPU path is off for good). A run
 * skipped because a 3D job is running is not a failure: the ARM does that call
 * and the next call tries the QPUs again. Returns 1
 * if the QPUs did it, 0 if the ARM did. Never waits more than QPU_TIMEOUT_US. */
int qpu_fade_hook(g16_t *g, const g16_fade_t *f);

/* Runs the queued requests; 0, or the number of requests the ARM had to do. */
int qpu_flush(void);

#endif
