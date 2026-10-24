#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

#include "drivers/fb.h"

typedef struct {
    const char *label;
    uint32_t fill_us;       /* full-screen framebuffer fill */
    uint32_t memset_us;     /* 1 MB memset in RAM */
    uint32_t memcpy_us;     /* 1 MB memcpy in RAM */
    uint32_t crc_us;        /* CRC-32 of 64 KB (integer ALU + loads) */
    uint32_t float_us;      /* 100k VFP multiply-adds */
} bench_t;

/* Needs the heap (2 MB scratch). Blanks the screen while it runs. */
void bench_run(bench_t *b, framebuffer_t *fb, const char *label);
void bench_print(const bench_t *b, int n);

#endif
