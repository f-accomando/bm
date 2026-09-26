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
#include "script/luavm.h"
#include "s32/player.h"
#include "b33/runtime.h"
#include "b33/stress.h"
#include "usb/usb.h"
#include "carts.h"

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
#define S32_ATTRACT_SECS 10
#define B33_DEMO_SECS    15

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

extern const char boot_lua[], boot_lua_end[];
extern const uint8_t s32_demo_cart[], s32_demo_cart_end[];
extern const uint8_t b33_demo_cart[], b33_demo_cart_end[];
extern const uint8_t b33_stress_cart[], b33_stress_cart_end[];

#ifdef BM33_BOOT_STRESS
/* Stress-test boot (make BOOT=stress): C and Lua rendering stress tests,
 * results left on the console for a photo. */
static void run_stress(void)
{
    b33_stress_run(&fb);
    kprintf("Lua part (cartridge API):\n");
    b33_stats_t bs;
    b33_play(&fb, b33_stress_cart, (size_t)(b33_stress_cart_end - b33_stress_cart), 600, &bs);
}
#endif

static void run_boot_script(void)
{
    if (!luavm_init()) {
        kprintf("\x1b[91mLua: cannot create the state\x1b[0m\n");
        return;
    }
    luavm_run(boot_lua, (size_t)(boot_lua_end - boot_lua), "@boot.lua");
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
    usb_init();
    usb_print();
    carts_init();

#ifdef BM33_BOOT_STRESS
    run_stress();
    monitor_run();
#endif

    kprintf("s32: playing the built-in demo.cart for %u s ('q' or Esc skips it)...\n",
            S32_ATTRACT_SECS);
    s32_play_stats_t ps;
    s32_play(&fb, s32_demo_cart, (size_t)(s32_demo_cart_end - s32_demo_cart),
             S32_ATTRACT_SECS, 1, &ps);
    s32_play_print(&ps);

    kprintf("b33: C benchmark and native demo cart ('q' or Esc skips)...\n");
    uint32_t bench_us = b33_bench(&fb, 120);
    kprintf("b33 bench: map + 256 sprites, 640x360: %lu.%02lu ms/frame (%lu%% of 16.7 ms)\n",
            bench_us / 1000, bench_us % 1000 / 10, bench_us * 100 / 16667);
    b33_stats_t bs;
    b33_play(&fb, b33_demo_cart, (size_t)(b33_demo_cart_end - b33_demo_cart), B33_DEMO_SECS, &bs);
    b33_print_stats(&bs);

    uint32_t vs[5];
    if (fb_vsync_probe(vs, 5) == 0)
        kprintf("vsync probe: tag ok, waits %lu %lu %lu %lu %lu us\n",
                vs[0], vs[1], vs[2], vs[3], vs[4]);
    else
        kprintf("vsync probe: tag not supported by the firmware\n");

    run_boot_script();

    /* With a USB keyboard or gamepad the console starts on the cartridge
     * menu; Esc (or Start+Select) goes to the monitor. */
    int k = usb_info()->kind;
    if (k == USB_KEYBOARD || k == USB_GAMEPAD || k == USB_XBOX360)
        carts_menu(&fb);

    monitor_run();
}
