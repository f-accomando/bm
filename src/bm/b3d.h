#ifndef BM_B3D_H
#define BM_B3D_H

/*
 * The 3D Bench (docs/BENCH3D.md): every 3D capacity of bm's drivers, each
 * test with each renderer (the ARM, the GPU, its anti-aliasing, its vertex
 * shader), the load raised until a frame takes more than 40 ms; the loads
 * that still fit 60 fps and 30 fps, with the work behind them (triangles,
 * vertices, pixels, ARM instructions and cache misses, GPU time, jobs).
 * At the end a bar chart a test, against the numbers measured on the Pi
 * with the drivers before (docs/DRIVERS.md), the last saved report and
 * the limits of the hardware; the report is saved for the next time.
 *
 * Portable: the Pi (src/kernel/b3dpi.c) and the PC (tests/bm/b3d_host.c,
 * the V3D emulated) give it what is theirs.
 */

#include <stddef.h>
#include <stdint.h>

#include "bm/gfx16.h"

enum { B3D_KEY_NONE = -1, B3D_KEY_LEFT = 1, B3D_KEY_RIGHT, B3D_KEY_BACK };

typedef struct {
    uint32_t cycles, instr, dmiss;      /* the ARM's counters (0 without them) */
    uint32_t wait_instr;                /* instructions spent waiting for the V3D */
} b3d_count_t;

typedef struct {
    g16_t *g;                           /* the page the scenes draw into (640x360) */
    uint32_t (*us)(void);               /* microseconds */
    uint32_t (*present)(void);          /* shows the page; the microseconds it took */
    void (*count)(b3d_count_t *c);      /* the counters now */
    int counting;                       /* they count */
    const char *pmu;                    /* whose counters (NULL: the ARM1176's) */
    int (*key)(void);                   /* B3D_KEY_*: what was pressed since the last call */
    const char *back;                   /* the button that leaves (NULL: "B") */
    void (*log)(const char *line);      /* a line for the log (serial, console) */
    /* the report: saved as a new file (its name back), the last one saved */
    int (*save)(const char *text, size_t len, char *name, size_t n);
    int (*load_last)(char **text, char *name, size_t n);
    const char *kernel;                 /* git describe */
    const char *machine;                /* clocks, temperature, throttling */
    const char *date;                   /* the network's time, or "" */
    int quick;                          /* a few steps, one frame each (the PC's test) */
    void (*page_shown)(int page);       /* a page of results is on the screen (the PC's test) */
} b3d_platform_t;

/* runs the bench, then shows the pages until B3D_KEY_BACK (in quick mode,
 * each page once); 0, or -1 if the report could not be saved */
int b3d_run(const b3d_platform_t *p);

#endif
