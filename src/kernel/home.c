#include "home.h"
#include "carts.h"
#include "bench.h"
#include "b3dpi.h"
#include "config.h"
#include "crumbs.h"
#include "demo.h"
#include "dmatest.h"
#include "gputest.h"
#include "input.h"
#include "monitor.h"
#include "pager.h"
#include "pointer.h"
#include "reports.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "update.h"
#include "version.h"
#include "audio/audio.h"
#include "bm/roombench.h"
#include "bm/runtime.h"
#include "bm/stress.h"
#include "bt/bt.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "gpu/gpu3d.h"
#include "gpu/v3d.h"
#include "gpu/version3d.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "drivers/board.h"
#include "net/net.h"
#include "script/repl.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "wifi/wifi.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void home_wait_back(void)
{
    kprintf("\n\x1b[93mA, Esc or any key: back to the menu\x1b[0m\n");
    input_flush();
    uint32_t prev = hid_buttons();
    for (;;) {
        int quit = 0;
        if (input_key() >= 0)
            break;
        uint32_t b = input_buttons(&quit);
        if (quit || (b & ~prev & (HID_A | HID_B | HID_START)))
            break;
        prev = b;
    }
}

/* a title line on the cleared console, before a tool writes there */
static void heading(const char *what)
{
    console_clear();
    kprintf("\x1b[1;96mbm\x1b[0m \x1b[90m>\x1b[0m %s\n\n", what);
}

/* ---------------------------------------------------------------- tools */

static void t_lua(framebuffer_t *fb)
{
    (void)fb;
    heading("Lua (Esc or B, Ctrl-D or exit() returns to the menu)");
    input_pad_keys(INPUT_PAD_ESC);
    repl_run();
    input_pad_keys(0);
}

static void t_system(framebuffer_t *fb)
{
    (void)fb;
    heading("System");
    kprintf("bm %s\n", bm_version);
    sysinfo_print();
    kprintf("SD card        : %s\n", fat_describe());
    kprintf("audio          : %s\n", audio_status());
    kprintf("network        : %s, time %s\n", net_ip_text(), net_time_text());
}

void home_show_log(framebuffer_t *fb)
{
    (void)fb;
    heading("Log: everything printed since boot (up/down, Esc quits)");
    const char *text = klog_text();
    uint32_t cols, rows, lines = 0;
    console_size(&cols, &rows);
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    input_pad_keys(INPUT_PAD_NAV);
    pager_show(text);
    input_pad_keys(0);
    if (lines + 3 < rows)                       /* it fit on one page: no pager */
        home_wait_back();
}

static void t_input(framebuffer_t *fb)
{
    (void)fb;
    heading("Input test");
    input_live_test(10);
}

static void t_audio(framebuffer_t *fb)
{
    (void)fb;
    heading("Audio test");
    audio_test();
}

static void t_cpu(framebuffer_t *fb)
{
    heading("CPU benchmark");
    bench_t b;
    bench_run(&b, fb, "now");
    bench_print(&b, 1);
}

static void t_render(framebuffer_t *fb)
{
    heading("Rendering benchmark, 640x360 RGB565");
    bm_bench_report(fb, 120);
}

static void t_stress(framebuffer_t *fb)
{
    heading("Stress test: sprites, triangles, 3D");
    extern const uint8_t bm_stress_cart[], bm_stress_cart_end[];
    bm_stress_run(fb);
    kprintf("Lua part (cartridge API):\n");
    bm_stats_t bs;
    bm_play(fb, bm_stress_cart, (size_t)(bm_stress_cart_end - bm_stress_cart), 600, &bs);
}

static void t_dma(framebuffer_t *fb)
{
    heading("DMA test");
    dma_test(fb);
}

static void t_gpu(framebuffer_t *fb)
{
    heading("GPU test");
    gpu_test(fb);
}

static void t_bench3d(framebuffer_t *fb)
{
    heading("3D Bench: every 3D test with every driver");
    bm_bench3d(fb);
}

static void t_room(framebuffer_t *fb)
{
    heading("Texture Room: 3D benchmark");
    bm_room_bench(fb);
}

static void t_demo(framebuffer_t *fb)
{
    heading("Animation demo (10 s)");
    demo_stats_t st;
    demo_run(fb, 10, &st);
    demo_print(&st);
}

static void t_pattern(framebuffer_t *fb)
{
    console_suspend(1);
    draw_test_pattern(fb);
    input_pad_keys(INPUT_PAD_NAV);
    input_flush();
    input_getc();
    input_pad_keys(0);
    console_suspend(0);
    heading("Test pattern");
    kprintf("HDMI test pattern shown\n");
}

/* the development assistant (M30) on its own: ask, see the code and the
 * sprites it would give an editor (also the monitor's I) */
void home_assistant(framebuffer_t *fb)
{
    extern const uint8_t bm_assistant_cart[], bm_assistant_cart_end[];
    bm_stats_t bs;
    bm_play(fb, bm_assistant_cart, (size_t)(bm_assistant_cart_end - bm_assistant_cart), 24u * 3600u, &bs);
}

static void t_diag(framebuffer_t *fb)
{
    (void)fb;
    heading("Boot diagnostics: benchmarks, the bm demo, Lua boot script");
    diagnostics_run();
}

typedef struct {
    const char *title, *about;
    int icon;
    uint32_t rgb;
    void (*run)(framebuffer_t *fb);     /* NULL: the monitor */
    int wait;
    g16_sheet_t cover;
    const char *report;                 /* what it prints is a report of this kind (reports.h), or NULL */
} tool_t;

/* bm Code, the code editor */
static void t_code(framebuffer_t *fb)
{
    carts_code(fb, NULL);
}

static tool_t tools[] = {
    { "Code", "code editor: tabs, two pages, small font", MENU_ICON_CODE, 0x3A4A8A, t_code, 0, { 0 }, NULL },
    { "Assistant", "help with code and sprites; F6 in the tools", MENU_ICON_ASSIST, 0x2A6A9A, home_assistant, 0, { 0 }, NULL },
    { "Monitor", "the text console with every command (h: help)", MENU_ICON_TERMINAL, 0x2A3A4A, NULL, 0, { 0 }, NULL },
    { "Lua", "Lua 5.4 prompt (USB keyboard); Ctrl-D or exit() returns", MENU_ICON_LUA, 0x2A3A9A, t_lua, 0, { 0 }, NULL },
    { "System", "board, clocks, memory, SD card, network", MENU_ICON_CHIP, 0x2A7A5A, t_system, 1, { 0 }, "system" },
    { "Log", "everything printed since boot", MENU_ICON_LOG, 0x6A6A7A, home_show_log, 0, { 0 }, NULL },
    { "Input test", "the buttons each player holds, for 10 s", MENU_ICON_PAD, 0x8A3A8A, t_input, 1, { 0 }, NULL },
    { "Audio test", "HDMI sound status and a test tune", MENU_ICON_SOUND, 0xB05A2A, t_audio, 1, { 0 }, "audio" },
    { "CPU bench", "CPU and memory benchmark", MENU_ICON_GAUGE, 0x3A5A8A, t_cpu, 1, { 0 }, "cpu" },
    { "Render bench", "drawing benchmark, 640x360 RGB565", MENU_ICON_TRIANGLES, 0x5A3AA0, t_render, 1, { 0 }, "render" },
    { "Stress test", "sprites, triangles and 3D, in C and in Lua", MENU_ICON_FLAME, 0xA03A3A, t_stress, 1, { 0 }, "stress" },
    { "DMA test", "copies by the CPU against the DMA, step by step", MENU_ICON_ARROWS, 0x2A7A8A, t_dma, 1, { 0 }, "dma" },
    { "GPU test", "the 3D unit (V3D) step by step; ARM against GPU", MENU_ICON_TRIANGLES, 0x8A5A2A, t_gpu, 1, { 0 }, "gpu" },
    { "3D Bench", "every 3D test, every driver: bars, report on the SD", MENU_ICON_GAUGE, 0x2A6A8A, t_bench3d, 1,
      { 0 }, NULL },
    { "Texture Room", "3D bench: crates doubled to 30 fps, ARM and GPU", MENU_ICON_GAUGE, 0x9A6A2A, t_room, 1, { 0 }, "room" },
    { "Demo", "the 60 fps animation demo, 10 s", MENU_ICON_PLAY, 0x3A8A3A, t_demo, 1, { 0 }, "demo" },
    { "Test pattern", "HDMI colour bars; any button returns", MENU_ICON_BARS, 0x404050, t_pattern, 0, { 0 }, NULL },
    { "Diagnostics", "the old boot sequence: benchmarks and demos", MENU_ICON_CHECK, 0x7A6A2A, t_diag, 1, { 0 }, "diag" },
};

#define NTOOLS ((int)(sizeof tools / sizeof tools[0]))

void home_init(void)
{
    for (int i = 0; i < NTOOLS; i++)
        if (!tools[i].cover.px)
            menu_make_tool_cover(&tools[i].cover, tools[i].title, tools[i].icon, tools[i].rgb);
}

int home_tools(void) { return NTOOLS; }
const char *home_tool_title(int i) { return tools[i].title; }
const char *home_tool_about(int i) { return tools[i].about; }
const g16_sheet_t *home_tool_cover(int i) { return tools[i].cover.px ? &tools[i].cover : NULL; }

/* a tool whose printout is a report: run between reports_begin and _end */
static int reported;

static void run_reported(framebuffer_t *fb)
{
    reports_begin(tools[reported].report);
    tools[reported].run(fb);
    reports_end();
}

void home_tool_start(int i, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    crumb("dev tool", tools[i].title);
    if (!tools[i].run) {
        d->what = HOME_MONITOR;
        return;
    }
    d->what = HOME_TEXT;
    d->text = tools[i].report ? run_reported : tools[i].run;
    reported = i;
    d->wait = tools[i].wait;
}
