#include "monitor.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "drivers/fb.h"
#include "gfx/console.h"
#include "drivers/led.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "lib/printf.h"

static void help(void)
{
    kprintf("commands:\n"
            "  h  help\n"
            "  i  system info\n"
            "  c  clear screen\n"
            "  t  HDMI test pattern (any key returns to the console)\n"
            "  r  reboot (watchdog; the chainloader will ask for a new kernel)\n"
            "  u  test: undefined instruction\n"
            "  a  test: data abort (unaligned access with alignment checking)\n"
            "  b  test: prefetch abort (BKPT)\n"
            "  s  test: software interrupt (SVC)\n");
}

static void __attribute__((noinline)) trigger_undef(void)
{
    __asm__ volatile(".word 0xe7f000f0");       /* permanently undefined */
}

static void __attribute__((noinline)) trigger_dabt(void)
{
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1u << 1;                           /* A: alignment checking */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    volatile uint32_t *p = (volatile uint32_t *)0x8001;
    (void)*p;
}

static void __attribute__((noinline)) trigger_pabt(void)
{
    __asm__ volatile("bkpt #0x33");
}

static void __attribute__((noinline)) trigger_swi(void)
{
    __asm__ volatile("svc #0x42");
}

static void update_uptime(void)
{
    char buf[24];
    uint32_t s = timer_ticks() / 1000000;
    ksnprintf(buf, sizeof buf, "up %02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
    console_set_status(0, buf);
}

/* Waits for a key, blinking the LED at 1 Hz and refreshing the uptime. */
static char wait_key(void)
{
    uint32_t last = timer_ticks();
    int led = 0;
    char c;

    update_uptime();
    while (!uart_getc_timeout(10000, &c)) {
        if (timer_ticks() - last >= 500000) {
            last = timer_ticks();
            led = !led;
            led_set(led);
            if (!led)
                update_uptime();
        }
    }
    return c;
}

static void show_test_pattern(void)
{
    framebuffer_t *fb = console_framebuffer();
    console_suspend(1);
    draw_test_pattern(fb);
    kprintf("test pattern shown, press any key\n");
    wait_key();
    console_suspend(0);
}

void monitor_run(void)
{
    kprintf("type 'h' for help\n");

    for (;;) {
        kprintf("> ");
        char c = wait_key();
        if (c >= ' ' && c < 127)
            kprintf("%c", c);
        kprintf("\n");

        switch (c) {
        case 'h': case '?': help(); break;
        case 'i': sysinfo_print(); break;
        case 'c': console_clear(); break;
        case 't': show_test_pattern(); break;
        case 'r':
            kprintf("rebooting...\n");
            uart_flush();
            watchdog_reboot();
        case 'u': trigger_undef(); break;
        case 'a': trigger_dabt(); break;
        case 'b': trigger_pabt(); break;
        case 's': trigger_swi(); break;
        case '\r': case '\n': break;
        default:
            kprintf("unknown command (0x%02x), 'h' for help\n", (unsigned char)c);
        }
    }
}
