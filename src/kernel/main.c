/*
 * bm33 kernel entry.
 *
 * ACT LED protocol:
 *   solid on               -> init in progress (stuck here = early hang)
 *   1 Hz heartbeat         -> running, monitor waiting for commands
 *   N blinks + pause       -> fatal exception number N (9 = panic)
 */
#include <stdint.h>

#include "exceptions.h"
#include "monitor.h"
#include "sysinfo.h"
#include "drivers/fb.h"
#include "drivers/led.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "gfx/font.h"
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
    kprintf("colours        : ");
    for (int i = 0; i < 16; i++)
        kprintf("\x1b[%dm\xdb\xdb", i < 8 ? 30 + i : 90 + i - 8);
    kprintf("\x1b[0m\n");
}

void kernel_main(uint32_t atags)
{
    led_init();
    led_set(1);
    uart_init();

    int err = fb_init(&fb, SCREEN_W, SCREEN_H);
    if (err == 0) {
        exceptions_set_panic_fb(&fb);
        console_init(&fb, &font_console_8x16);
        console_set_status("bm33 " BM33_VERSION, 0);
        kprintf_set_sink(console_putc);
    }

    kprintf("\n\x1b[1;36mbm33\x1b[0m kernel %s - Raspberry Pi Zero (BCM2835)\n", BM33_VERSION);
    kprintf("atags/dtb at %p\n", (void *)atags);
    if (err)
        panic("framebuffer init failed (%d)", err);

    sysinfo_print();
    kprintf("framebuffer    : %lux%lu, pitch %lu, %s, at %p\n",
            fb.width, fb.height, fb.pitch, fb.is_rgb ? "RGB" : "BGR", fb.base);
    uint32_t cols, rows;
    console_size(&cols, &rows);
    kprintf("console        : %lux%lu, 8x16 font\n", cols, rows);
    print_palette();
    kprintf("serial         : 115200 8N1 on GPIO14 (TX, pin 8) / GPIO15 (RX, pin 10)\n\n");

    monitor_run();
}
