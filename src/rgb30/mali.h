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
} mali_hw_t;

/* the registers' places on the RK3566 */
#define MALI_GPU_BASE   0xfde60000u
#define MALI_CRU_BASE   0xfdd20000u
#define MALI_PMU_BASE   0xfdd90000u

/* the probe's steps; mali_probe() returns MALI_OK or the one that failed */
enum {
    MALI_OK = 0, MALI_E_POWER = -1, MALI_E_ID = -2, MALI_E_RESET = -3, MALI_E_CORES = -4, MALI_E_MMU = -5,
    MALI_E_JOB = -6, MALI_E_CHAIN = -7, MALI_E_MEMORY = -8, MALI_E_SUPPLY = -9,
};

typedef struct {
    uint32_t gpu_id;                    /* GPU_ID: product (bits 31-16), major, minor, status */
    uint64_t shader_present, tiler_present, l2_present;
    uint32_t as_present, js_present, mmu_features, core_features, l2_features, thread_max;
    uint32_t js_status, job_header;     /* of the first job: JS_STATUS, exception status written back */
    uint32_t job_us, chain_us;          /* how long the jobs took */
    int supply_mv;                      /* vdd_gpu as the PMIC said (-1 unknown) */
    int step;                           /* the last step reached (MALI_OK: all) */
} mali_info_t;

int mali_probe(const mali_hw_t *hw, mali_info_t *info);

/* the name of an exception status (JS_STATUS, AS_FAULTSTATUS & 0xff) */
const char *mali_exception(uint32_t code);

/* "Mali-G52 r1p0" from GPU_ID */
const char *mali_name(uint32_t gpu_id, char *buf, int n);

#endif
