#include "monitor.h"
#include "bench.h"
#include "demo.h"
#include "bm/runtime.h"
#include "upload.h"
#include "bm/stress.h"
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
#include "usb/smsc95xx.h"
#include "carts.h"
#include "config.h"
#include "bt/bt.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "net/http.h"
#include "net/netxfer.h"
#include "pager.h"
#include "audio/audio.h"
#include "dmatest.h"
#include "crumbs.h"
#include "drivers/watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char help_text[] =
            "commands (games: arrows/wasd, space = A; q or Esc quits):\n"
            "  M  cartridge menu, or PS on a pad (SD: / and /carts; else built-in demos)\n"
            "  f  list cartridges        F  re-read the SD card\n"
            "  n  built-in native demo.bm\n"
            "  l  Lua REPL (Esc, Ctrl-D or exit() returns here)\n"
            "  U  receive a cartridge over serial and play it\n"
            "  i  system info            m  heap usage          c  clear screen\n"
            "  y  USB: scan port and hub Y  input test: USB, then each player (10 s)\n"
            "  L  keyboard layout Italian / US\n"
            "  a  audio: HDMI sound status and a test tune\n"
            "  e  editor: code, sprites and map of a .bm cartridge\n"
            "  T  Bluetooth: pair a controller as the next player (DS4: Share + PS)\n"
            "  K  Bluetooth: pair a keyboard (LE, e.g. MX Keys: hold an Easy-Switch key)\n"
            "  P  Bluetooth: forget all paired pads and the keyboard (asks first)\n"
            "  W  WiFi: start, list the networks, join one (M18; saved in bm/config.txt)\n"
            "  E  Ethernet (Pi 1 B / B+): link, counters, chip registers\n"
            "     from the PC: tools/bm_net.py IP (console, --send/--play a cart, --kernel)\n"
            "  G  get a web address (http or https): status, size, speed, start\n"
            "  b  boot diagnostics: benchmarks, the bm demo, Lua boot script\n"
            "  k  CPU benchmark          p  rendering benchmark 640x360 RGB565\n"
            "  D  DMA test step by step (CPU against DMA timings)\n"
            "  V  .bm drawing: direct on screen / via RAM (compare with p)\n"
            "  s  rendering stress test (sprites, triangles, 3D; C and Lua)\n"
            "  d  animation demo (60 fps; any key stops it)\n"
            "  t  HDMI test pattern (any key returns)\n"
            "  r  reboot (watchdog; the chainloader will ask for a new kernel)\n"
            "  X  crash tests (then u, a, b, s or f): exception screen, freeze\n"
            "  o  everything printed since boot (scrolls like this help)\n";

static void help(void)
{
    pager_show(help_text);
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

/* 'G': a GET, to try the network from the monitor (M19) */
static void net_get_test(void)
{
    static char url[256] = "https://example.com/";
    kprintf("address (Enter: %s): ", url);
    char line[240];
    int n = input_read_line(line, sizeof line, 0);
    if (n < 0)
        return;
    if (n > 0)
        snprintf(url, sizeof url, "%s%s", strstr(line, "://") ? "" : "https://", line);
    uint32_t t0 = timer_ticks();
    uint8_t *data;
    size_t len;
    http_info_t info;
    int st = http_get_buffer(url, NULL, 8u << 20, &data, &len, &info);
    uint32_t ms = (timer_ticks() - t0) / 1000;
    if (st < 0) {
        kprintf("\x1b[91mget: %s\x1b[0m\n", info.error);
    } else {
        kprintf("get: %d, %lu bytes in %lu ms (%lu KiB/s), %s\n", st, (unsigned long)len, ms,
                ms ? (unsigned long)(len * 1000 / 1024 / ms) : 0, info.type[0] ? info.type : "no type");
        if (strcmp(info.url, url) != 0)
            kprintf("     from %s\n", info.url);
        if (info.error[0])
            kprintf("     %s\n", info.error);
        size_t show = len < 300 ? len : 300;
        for (size_t i = 0; i < show; i++) {
            char c = (char)data[i];
            kprintf("%c", (c == '\n' || (c >= 32 && c < 127)) ? c : '.');
        }
        if (show)
            kprintf("%s\n", len > show ? "..." : "");
    }
    kprintf("net: time %s\n", net_time_text());
    free(data);
}

void monitor_run(void)
{
    kprintf("\ntype 'h' for help\n");

    for (;;) {
        kprintf("> ");
        crumb("monitor, waiting for a key", NULL);
        int c = input_getc_home();              /* PS on a controller: INPUT_HOME */
        if (c == 0x1B) {                /* a terminal's arrow key: not a command */
            if (input_skip_sequence()) {
                kprintf("\r");
                continue;
            }
        }
        if (c >= ' ' && c < 127)
            kprintf("%c", c);
        kprintf("\n");
        char cmd[2] = { c >= ' ' && c < 127 ? c : '?', 0 };
        crumb("monitor command", cmd);

        switch (c) {
        case 'h': case '?': help(); break;
        case 'i': sysinfo_print(); break;
        case 'l': repl_run(); break;
        case 'c': console_clear(); break;
        case 'm': sysinfo_print_heap(); break;
        case 'n': {
            extern const uint8_t bm_demo_cart[], bm_demo_cart_end[];
            bm_stats_t bs;
            bm_play(console_framebuffer(), bm_demo_cart,
                     (size_t)(bm_demo_cart_end - bm_demo_cart), 3600, &bs);
            bm_print_stats(&bs);
            break;
        }
        case 'U': upload_and_play(console_framebuffer()); break;
        case 'S': case 's': {
            extern const uint8_t bm_stress_cart[], bm_stress_cart_end[];
            bm_stress_run(console_framebuffer());
            kprintf("Lua part (cartridge API):\n");
            bm_stats_t bs;
            bm_play(console_framebuffer(), bm_stress_cart,
                     (size_t)(bm_stress_cart_end - bm_stress_cart), 600, &bs);
            break;
        }
        case 'p': bm_bench_report(console_framebuffer(), 120); break;
        case 'V':
            bm_set_via_ram(!bm_via_ram());
            kprintf(".bm carts draw %s\n", bm_via_ram() ? "via a RAM buffer" : "directly on screen");
            config_save();
            break;
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
        case 'B': case 'b': diagnostics_run(); break;
        case 'M': case INPUT_HOME: carts_menu(console_framebuffer()); break;
        case 'f': carts_list(); break;
        case 'F': carts_init(); carts_list(); break;
        case 'y': usb_init(); usb_print(); break;
        case 'Y': usb_live_test(5); input_live_test(10); break;
        case 'T': bt_scan(8); break;
        case 'K': bt_pair_keyboard(15); break;
        case 'W':
            if (wifi_start() == 0 && wifi_scan() > 0 && wifi_connect() == 0 &&
                net_start(&net_wifi) == 0)
                net_wait_ip(15000);
            break;
        case 'E':
            eth_diag();
            if (eth_present())
                kprintf("net: IP %s, time %s\n", net_ip_text(), net_time_text());
            break;
        case 'o': pager_show(klog_text()); break;
        case 'P': {
            kprintf("forget all Bluetooth pads and the keyboard (keys removed from bm/config.txt)? y = yes\n");
            input_flush();                      /* only a key pressed after the question */
            char k = input_getc();
            if (k == 'y' || k == 'Y') {
                int n = bt_forget_all();
                kprintf("bt: %d device%s forgotten; pair again with T (DS4: Share + PS)\n"
                        "    or K (keyboard)\n", n, n == 1 ? "" : "s");
            }
            else
                kprintf("cancelled\n");
            break;
        }
        case 'a': audio_test(); break;
        case 'e': carts_editor(console_framebuffer()); break;
        case 'D': dma_test(console_framebuffer()); break;
        case 'L':
            hid_set_layout(hid_layout()[0] == 'i' ? "us" : "it");
            kprintf("keyboard layout: %s\n", hid_layout());
            config_save();
            break;
        case 'r':
            kprintf("rebooting...\n");
            crumbs_clean_exit();
            uart_flush();
            watchdog_reboot();
        case 'X': {
            /* two keys, so that a stray key never halts the console */
            kprintf("crash test: u undefined insn, a data abort, b prefetch abort, "
                    "s SVC,\n  f freeze (the watchdog restarts the Pi in 3 s); other keys cancel\n");
            char t = input_getc();
            if (t == 'u') trigger_undef();
            else if (t == 'a') trigger_dabt();
            else if (t == 'b') trigger_pabt();
            else if (t == 's') trigger_swi();
            else if (t == 'f') {
                crumb("freeze test (X f)", NULL);
                __asm__ volatile("cpsid i" ::: "memory");
                for (;;)
                    ;
            }
            else kprintf("cancelled\n");
            break;
        }
        case '\r': case '\n': break;
        case 0x1B: break;               /* Esc alone: already at the monitor */
        case 'G': net_get_test(); break;
        case INPUT_NET_PLAY: {                  /* bm_net.py --play */
            uint8_t *buf;
            size_t len;
            if (netxfer_take_play(&buf, &len)) {
                carts_play_buffer(console_framebuffer(), buf, len);
                free(buf);
            }
            break;
        }
        default:
            kprintf("unknown command (0x%02x), 'h' for help\n", (unsigned char)c);
        }
    }
}
