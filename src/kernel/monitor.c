#include "monitor.h"
#include "bench.h"
#include "demo.h"
#include "s32/player.h"
#include "b33/runtime.h"
#include "upload.h"
#include "b33/stress.h"
#include "input.h"
#include "script/repl.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "drivers/fb.h"
#include "gfx/console.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "lib/printf.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "carts.h"

static void help(void)
{
    kprintf("commands (games: arrows/wasd, space = A; q or Esc quits):\n"
            "  M  cartridge menu (built-in + SD card: / and /carts)\n"
            "  f  list cartridges        F  re-read the SD card\n"
            "  g  built-in s32 demo.cart n  built-in native demo.b33\n"
            "  l  Lua REPL (Ctrl-D or exit() returns here)\n"
            "  U  receive a cartridge over serial and play it\n"
            "  i  system info            m  heap usage          c  clear screen\n"
            "  y  USB: scan the port     L  keyboard layout Italian / US\n"
            "  k  CPU benchmark          p  rendering benchmark 640x360 RGB565\n"
            "  S  rendering stress test (sprites, triangles, 3D; C and Lua)\n"
            "  d  animation demo (60 fps; any key stops it)\n"
            "  t  HDMI test pattern (any key returns)\n"
            "  r  reboot (watchdog; the chainloader will ask for a new kernel)\n"
            "  u a b s  tests: undefined insn, data abort, prefetch abort, SVC\n");
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

static void show_test_pattern(void)
{
    framebuffer_t *fb = console_framebuffer();
    console_suspend(1);
    draw_test_pattern(fb);
    kprintf("test pattern shown, press any key\n");
    input_getc();
    console_suspend(0);
}

void monitor_run(void)
{
    kprintf("\ntype 'h' for help\n");

    for (;;) {
        kprintf("> ");
        char c = input_getc();
        if (c >= ' ' && c < 127)
            kprintf("%c", c);
        kprintf("\n");

        switch (c) {
        case 'h': case '?': help(); break;
        case 'i': sysinfo_print(); break;
        case 'l': repl_run(); break;
        case 'c': console_clear(); break;
        case 'm': sysinfo_print_heap(); break;
        case 'g': {
            extern const uint8_t s32_demo_cart[], s32_demo_cart_end[];
            s32_play_stats_t ps;
            s32_play(console_framebuffer(), s32_demo_cart,
                     (size_t)(s32_demo_cart_end - s32_demo_cart), 3600, 0, &ps);
            s32_play_print(&ps);
            break;
        }
        case 'n': {
            extern const uint8_t b33_demo_cart[], b33_demo_cart_end[];
            b33_stats_t bs;
            b33_play(console_framebuffer(), b33_demo_cart,
                     (size_t)(b33_demo_cart_end - b33_demo_cart), 3600, &bs);
            b33_print_stats(&bs);
            break;
        }
        case 'U': upload_and_play(console_framebuffer()); break;
        case 'S': {
            extern const uint8_t b33_stress_cart[], b33_stress_cart_end[];
            b33_stress_run(console_framebuffer());
            kprintf("Lua part (cartridge API):\n");
            b33_stats_t bs;
            b33_play(console_framebuffer(), b33_stress_cart,
                     (size_t)(b33_stress_cart_end - b33_stress_cart), 600, &bs);
            break;
        }
        case 'p': {
            uint32_t us = b33_bench(console_framebuffer(), 120);
            kprintf("b33 bench: %lu.%02lu ms/frame (%lu%% of 16.7 ms)\n",
                    us / 1000, us % 1000 / 10, us * 100 / 16667);
            break;
        }
        case 'd': {
            demo_stats_t st;
            demo_run(console_framebuffer(), 60, &st);
            demo_print(&st);
            break;
        }
        case 'k': {
            bench_t b;
            bench_run(&b, console_framebuffer(), "now");
            bench_print(&b, 1);
            break;
        }
        case 't': show_test_pattern(); break;
        case 'M': carts_menu(console_framebuffer()); break;
        case 'f': carts_list(); break;
        case 'F': carts_init(); carts_list(); break;
        case 'y': usb_init(); usb_print(); break;
        case 'L':
            hid_set_layout(hid_layout()[0] == 'i' ? "us" : "it");
            kprintf("keyboard layout: %s\n", hid_layout());
            break;
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
