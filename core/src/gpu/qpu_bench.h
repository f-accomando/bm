#ifndef GPU_QPU_BENCH_H
#define GPU_QPU_BENCH_H

#include <stddef.h>
#include <stdint.h>

/* The VPM micro-benchmark (qpu_bench.c): one line of numbers into line. 0, or -1 if the QPUs did not run. */
int qpu_vpm_bench(uint32_t (*us)(void), char *line, size_t n);

#endif
