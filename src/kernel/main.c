/*
 * bm33 kernel entry.
 *
 * ACT LED protocol:
 *   solid on               -> init in progress (stuck here = early hang)
 *   1 Hz heartbeat         -> running, serial monitor waiting for commands
 *   N blinks + pause       -> fatal exception number N (9 = panic)
 */
#include <stdint.h>

#include "exceptions.h"
#include "monitor.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "drivers/fb.h"
#include "drivers/led.h"
#include "drivers/uart.h"
#include "lib/printf.h"

#ifndef BM33_VERSION
#define BM33_VERSION "dev"
#endif

#define SCREEN_W 1280
#define SCREEN_H 720

static framebuffer_t fb;

void kernel_main(uint32_t atags)
{
    led_init();
    led_set(1);
    uart_init();

    kprintf("\n\nbm33 kernel %s - Raspberry Pi Zero (BCM2835)\n", BM33_VERSION);
    kprintf("atags/dtb at %p\n", (void *)atags);
    sysinfo_print();

    int err = fb_init(&fb, SCREEN_W, SCREEN_H);
    if (err)
        panic("framebuffer init failed (%d)", err);
    kprintf("framebuffer    : %lux%lu, pitch %lu, %s, at %p\n",
            fb.width, fb.height, fb.pitch, fb.is_rgb ? "RGB" : "BGR", fb.base);

    draw_test_pattern(&fb);
    exceptions_set_panic_fb(&fb);

    monitor_run();
}
