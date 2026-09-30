/*
 * bm kernel entry.
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
#include "bm/runtime.h"
#include "bm/stress.h"
#include "usb/usb.h"
#include "carts.h"
#include "config.h"
#include "bt/bt.h"
#include "audio/audio.h"
#include "crumbs.h"
#include "drivers/watchdog.h"
#include "drivers/dma.h"
#include "version.h"
#include "net/net.h"
#include "wifi/wifi.h"

#include <string.h>


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
#define BM_DEMO_SECS    15

static void heartbeat(uint32_t tick)
{
    if (tick % (TICK_HZ / 2) == 0)
        led_set((tick / (TICK_HZ / 2)) & 1);
    if (tick % (TICK_HZ / 10) == 0) {       /* 10 Hz: freeze guard */
        watchdog_pet();
        crumb_tick(tick * (1000 / TICK_HZ));
    }
}

/* Counts timer IRQs against the free-running counter for 200 ms. */
static void report_irq(void)
{
    uint32_t t0 = timer_ticks(), n0 = tick_count();
    timer_delay_us(200000);
    uint32_t n = tick_count() - n0, us = timer_ticks() - t0;
    kprintf("IRQ on: timer %lu Hz (measured %lu Hz), double buffer %s\n",
            tick_hz(), (uint32_t)((uint64_t)n * 1000000u / us),
            fb.buffers >= 2 ? "on" : "OFF");
}

extern const char boot_lua[], boot_lua_end[];
extern const uint8_t s32_demo_cart[], s32_demo_cart_end[];
extern const uint8_t bm_demo_cart[], bm_demo_cart_end[];
extern const uint8_t bm_stress_cart[], bm_stress_cart_end[];

#ifdef BM_BOOT_STRESS
/* Stress-test boot (make BOOT=stress): C and Lua rendering stress tests,
 * results left on the console for a photo. */
static void run_stress(void)
{
    bm_stress_run(&fb);
    kprintf("Lua part (cartridge API):\n");
    bm_stats_t bs;
    bm_play(&fb, bm_stress_cart, (size_t)(bm_stress_cart_end - bm_stress_cart), 600, &bs);
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

/* Everything the boot used to show before M9: palette, CPU benchmark,
 * libc self-test, the s32 attract demo, the bm benchmark and demo, the
 * vsync probe and the Lua boot script. Monitor command 'B'. */
void diagnostics_run(void)
{
    bench_t bench;
    uint32_t cols, rows;
    console_size(&cols, &rows);
    kprintf("screen %lux%lu %s, console %lux%lu; serial 115200 8N1 on GPIO14/15\n",
            fb.width, fb.height, fb.is_rgb ? "RGB" : "BGR", cols, rows);
    print_palette();
    bench_run(&bench, &fb, "max + cache");
    bench_print(&bench, 1);
    libc_selftest();

    kprintf("s32: playing the built-in demo.cart for %u s ('q' or Esc skips it)...\n",
            S32_ATTRACT_SECS);
    s32_play_stats_t ps;
    s32_play(&fb, s32_demo_cart, (size_t)(s32_demo_cart_end - s32_demo_cart),
             S32_ATTRACT_SECS, 1, &ps);
    s32_play_print(&ps);

    kprintf("bm: C benchmark and native demo cart ('q' or Esc skips)...\n");
    bm_bench_report(&fb, 120);
    bm_stats_t bs;
    bm_play(&fb, bm_demo_cart, (size_t)(bm_demo_cart_end - bm_demo_cart), BM_DEMO_SECS, &bs);
    bm_print_stats(&bs);

    uint32_t vs[5];
    if (fb_vsync_probe(vs, 5) == 0)
        kprintf("vsync probe: tag ok, waits %lu %lu %lu %lu %lu us\n",
                vs[0], vs[1], vs[2], vs[3], vs[4]);
    else
        kprintf("vsync probe: tag not supported by the firmware\n");

    run_boot_script();
}


/* The saved WiFi network is joined at boot (wifi_boot=0 in
 * bm/config.txt turns it off); the address comes later, in the
 * background, and shows in the status line. */
static void wifi_boot(void)
{
    const char *ssid = config_get("wifi_ssid"), *on = config_get("wifi_boot");
    if (!ssid || !ssid[0] || (on && strcmp(on, "0") == 0))
        return;
    kprintf("wifi: joining the saved network (wifi_boot=0 in bm/config.txt: off)\n");
    if (wifi_start() == 0 && wifi_connect_saved() == 0)
        net_start();
}

void kernel_main(uint32_t atags)
{
    led_init();
    led_set(1);
    uart_init();

    int err = fb_init(&fb, SCREEN_W, SCREEN_H, 2);
    if (err == 0) {
        exceptions_set_panic_fb(&fb);
        console_init(&fb, &font_console_8x16);
        char title[40];
        ksnprintf(title, sizeof title, "bm %s", bm_version);
        console_set_status(title, 0);
        kprintf_set_sink(console_putc);
    }

    kprintf("\n\x1b[1;36mbm\x1b[0m kernel %s - Raspberry Pi Zero (BCM2835)\n", bm_version);
    (void)atags;
    if (err)
        panic("framebuffer init failed (%d)", err);

    uint32_t mem_end = arm_memory_end();
    heap_init(mem_end);
    prop_clock_set_max(CLOCK_ARM);
    mmu_init(mem_end);

    irq_init();
    tick_init(TICK_HZ);
    tick_set_hook(heartbeat);
    irq_cpu_enable();

    sysinfo_print_short();
    crumbs_boot();
    if (watchdog_arm(3000) != 0) {  /* a frozen Pi restarts and says what it was doing */
        uint32_t ld, lf;
        watchdog_probe_values(&ld, &lf);
        kprintf("watchdog: not available, no freeze guard (loaded %lu, after 20 ms %lu)\n", ld, lf);
    }
    report_irq();
    if (audio_init() == 0) {
        kprintf("audio: %s\n", audio_status());
        audio_note(0, 523, 90, 1, 110);         /* short chime: sound works */
        audio_note(1, 784, 160, 1, 90);
    } else {
        kprintf("audio: off - %s\n", audio_status());
    }
    if (dma_init() != 0)
        kprintf("dma: no free channel, copies by the CPU\n");
    usb_init();
    usb_print();
    carts_init();
    config_load();
    if (bt_paired())
        bt_start();             /* a paired pad can come back with its PS button */
    wifi_boot();

#ifdef BM_BOOT_STRESS
    run_stress();
    monitor_run();
#endif

    /* The console starts on the cartridge menu; Esc, Start+Select or 'q'
     * on the serial port go to the monitor ('B' there runs the old boot
     * diagnostics). */
    carts_menu(&fb);
    monitor_run();
}
