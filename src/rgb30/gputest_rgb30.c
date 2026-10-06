/*
 * Dev > GPU test on the RGB30 (M41): the Mali-G52 probed step by step
 * (mali.c), a line on the screen for each step, a report ("gpu") to send.
 * Only when asked: the boot never touches the GPU.
 */
#include "gputest_rgb30.h"

#include "mali.h"
#include "plat.h"
#include "rk_pmic.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "kernel/reports.h"
#include "kernel/version.h"
#include "lib/printf.h"
#include "gpu/version3d.h"

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
#endif

static mali_info_t info;
static int probed, result = 1;

int rgb30_gpu_result(const mali_info_t **in)
{
    if (in)
        *in = probed ? &info : NULL;
    return probed ? result : 1;
}

void rgb30_gpu_test(framebuffer_t *fb, const char *back)
{
    reports_begin("gpu");
    kprintf("\n\x1b[1mGPU test\x1b[0m: the Mali-G52, step by step (bm3d " BM3D_VERSION ", M41)\n");
#ifdef PLAT_RK3566
    /* the page shown, for the square the GPU clears (XRGB8888 only) */
    const uint32_t page = fb->buffers ? fb->size / fb->buffers : 0, off = fb->shown * page;
    static mali_hw_t hw;
    hw = (mali_hw_t){
        .rd = rd, .wr = wr, .us = us, .log = log_line, .supply_mv = supply_mv,
        .mem = (uint8_t *)(uintptr_t)PLAT_GPU_START, .mem_pa = PLAT_GPU_START, .mem_size = 4u << 20,
        .map_pa = PLAT_FB_START, .map_size = (uint32_t)(PLAT_FB_END - PLAT_FB_START),
        .screen = fb->depth == 32 ? fb->mem + off : NULL, .screen_pa = fb->depth == 32 ? fb->bus + off : 0,
        .screen_w = fb->width, .screen_h = fb->height, .screen_pitch = fb->pitch,
    };
    result = mali_probe(&hw, &info);
    probed = 1;
    if (result == MALI_OK)
        kprintf("\x1b[92mmali: every step done\x1b[0m: the GPU runs jobs and clears tiles (no triangles yet)\n");
    else
        kprintf("\x1b[91mmali: stopped at step %d\x1b[0m (the lines above say why)\n", -result);
    reports_end();
    if (back)
        kprintf("\n\x1b[96m%s\x1b[0m back\n", back);
    if (result == MALI_OK)
        mali_square(&hw, 0x40d060);     /* drawn again over the text: the last thing on the screen */
#else
    (void)fb;
    kprintf("mali: no Mali on " PLAT_NAME ": nothing to test\n");
    reports_end();
    if (back)
        kprintf("\n\x1b[96m%s\x1b[0m back\n", back);
#endif
}
