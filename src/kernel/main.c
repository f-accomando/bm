/*
 * bm33 kernel entry.
 *
 * ACT LED protocol:
 *   solid on               -> init in progress (stuck here = early hang)
 *   1 Hz heartbeat         -> running (driven by the 1 kHz timer IRQ)
 *   N blinks + pause       -> fatal exception number N (9 = panic)
 */
#include <stdint.h>

#include "bench.h"
#include "demo.h"
#include "irq.h"
#include "tick.h"
#include "exceptions.h"
#include "monitor.h"
#include "selftest.h"
#include "sysinfo.h"
#include "arch/mmu.h"
#include "drivers/fb.h"
#include "drivers/led.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/heap.h"
#include "lib/printf.h"

#ifndef BM33_VERSION
#define BM33_VERSION "dev"
#endif

/* Low resolution, 16:9: the firmware scales it to the HDMI mode in hardware
 * (x2 on 720p, x3 on 1080p), so drawing stays cheap. 80x22 text cells. */
#define SCREEN_W 640
#define SCREEN_H 360

static framebuffer_t fb;

static void print_palette(void)
{
    kprintf("colours ");
    for (int i = 0; i < 16; i++)
        kprintf("\x1b[%dm\xdb\xdb", i < 8 ? 30 + i : 90 + i - 8);
    kprintf("\x1b[0m\n");
}

#define TICK_HZ     1000
#define DEMO_SECS   10

static void heartbeat(uint32_t tick)
{
    if (tick % (TICK_HZ / 2) == 0)
        led_set((tick / (TICK_HZ / 2)) & 1);
}

/* Counts timer IRQs against the free-running counter for 200 ms. */
static void report_irq(void)
{
    uint32_t t0 = timer_ticks(), n0 = tick_count();
    timer_delay_us(200000);
    uint32_t n = tick_count() - n0, us = timer_ticks() - t0;
    kprintf("IRQ on: timer %lu Hz (measured %lu Hz), double buffer %s\n",
            tick_hz(), (uint32_t)((uint64_t)n * 1000000u / us),
            fb.buffers == 2 ? "on" : "OFF");
}

/* End of the ARM's share of SDRAM (the GPU owns the rest). */
static uint32_t arm_memory_end(void)
{
    uint32_t v[2] = { 0, 0 };
    if (prop_query(PROP_GET_ARM_MEMORY, v, 2) != 0 || v[1] == 0)
        return 0x10000000u;             /* 256 MiB: safe for gpu_mem <= 256 */
    return v[0] + v[1];
}

void kernel_main(uint32_t atags)
{
    bench_t bench[3];

    led_init();
    led_set(1);
    uart_init();

    int err = fb_init(&fb, SCREEN_W, SCREEN_H, 2);
    if (err == 0) {
        exceptions_set_panic_fb(&fb);
        console_init(&fb, &font_console_8x16);
        console_set_status("bm33 " BM33_VERSION, 0);
        kprintf_set_sink(console_putc);
    }

    kprintf("\n\x1b[1;36mbm33\x1b[0m kernel %s - Raspberry Pi Zero (BCM2835)\n", BM33_VERSION);
    (void)atags;
    if (err)
        panic("framebuffer init failed (%d)", err);

    uint32_t mem_end = arm_memory_end();
    heap_init(mem_end);

    /* Same work three times: as the firmware left us, at full clock, and
     * with MMU + caches on. */
    bench_run(&bench[0], &fb, "700MHz nocache");
    prop_clock_set_max(CLOCK_ARM);
    bench_run(&bench[1], &fb, "max nocache");
    mmu_init(mem_end);
    bench_run(&bench[2], &fb, "max + cache");

    irq_init();
    tick_init(TICK_HZ);
    tick_set_hook(heartbeat);
    irq_cpu_enable();

    sysinfo_print_short();
    uint32_t cols, rows;
    console_size(&cols, &rows);
    kprintf("screen %lux%lu %s, console %lux%lu; serial 115200 8N1 on GPIO14/15\n",
            fb.width, fb.height, fb.is_rgb ? "RGB" : "BGR", cols, rows);
    print_palette();
    bench_print(bench, 3);
    libc_selftest();
    report_irq();

    kprintf("running the %u s animation demo (any key on serial skips it)...\n", DEMO_SECS);
    demo_stats_t st;
    demo_run(&fb, DEMO_SECS, &st);
    demo_print(&st);

    monitor_run();
}
