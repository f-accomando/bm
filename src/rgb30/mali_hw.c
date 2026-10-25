/*
 * The Mali-G52 of the RGB30 for r3d (mali3d=1): the hw of the RK3566 and the probe (mali.c).
 * Only when asked: the boot never touches the GPU.
 */
#include "mali_hw.h"

#include "mali.h"
#include "plat.h"
#include "rk_pmic.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#ifdef PLAT_RK3566
/* device writes after the job memory's (normal, uncached) writes; reads
 * before what the CPU reads next of that memory */
static uint32_t rd(uintptr_t a)
{
    uint32_t v = *(volatile uint32_t *)a;
    __asm__ volatile("dsb sy" ::: "memory");
    return v;
}

static void wr(uintptr_t a, uint32_t v)
{
    __asm__ volatile("dsb st" ::: "memory");
    *(volatile uint32_t *)a = v;
}

static uint32_t us(void)
{
    return timer_ticks();
}

static void log_line(const char *s)
{
    kprintf("%s\n", s);
}

/* vdd_gpu: the RK817's DCDC2 (enable: POWER_EN0 bit 1; ON_VSEL 0xbe:
 * 0.5 V + 12.5 mV a step to 1.5 V, then 0.1 V a step) */
static int supply_mv(void)
{
    const int en = rk817_read(0xb1), vs = rk817_read(0xbe);
    if (en < 0 || vs < 0)
        return -1;
    if (!(en & 2))
        return 0;
    const int v = vs & 0x7f;
    return v <= 80 ? 500 + v * 25 / 2 : 1600 + (v - 81) * 100;
}

static mali_hw_t hw;            /* the one hw every probe uses (mali_frame wants the probed one) */

/* the fields that never change: the registers, the GPU's memory, what the GPU sees; no screen */
static void hw_fixed(void)
{
    hw = (mali_hw_t){
        .rd = rd, .wr = wr, .us = us, .log = log_line, .supply_mv = supply_mv,
        .mem = (uint8_t *)(uintptr_t)PLAT_GPU_START, .mem_pa = PLAT_GPU_START, .mem_size = 4u << 20,
        .map_pa = PLAT_FB_START, .map_size = (uint32_t)(PLAT_FB_END - PLAT_FB_START),
    };
}
#endif

static mali_info_t info;
static int probed, result = 1;

const mali_hw_t *rgb30_mali_hw(void)
{
#ifdef PLAT_RK3566
    hw_fixed();
    return &hw;
#else
    return NULL;
#endif
}

int rgb30_mali_probe(void)
{
#ifdef PLAT_RK3566
    hw_fixed();
    result = mali_probe(&hw, &info);
    probed = 1;
    return result;
#else
    return MALI_E_POWER;
#endif
}

int rgb30_mali_probe_triangle(const framebuffer_t *fb)
{
#ifdef PLAT_RK3566
    hw_fixed();
    if (fb && fb->base && fb->mem) {
        hw.screen = fb->base;
        hw.screen_pa = fb->bus + (uint32_t)(fb->base - fb->mem);
        hw.screen_w = fb->width;
        hw.screen_h = fb->height;
        hw.screen_pitch = fb->pitch;
    }
    hw.triangle = 1;
    result = mali_probe(&hw, &info);
    if (result == MALI_OK)
        mali_probe_vs3(&hw);
    probed = 1;
    return result;
#else
    (void)fb;
    return MALI_E_POWER;
#endif
}

int rgb30_gpu_result(const mali_info_t **in)
{
    if (in)
        *in = probed ? &info : NULL;
    return probed ? result : 1;
}
