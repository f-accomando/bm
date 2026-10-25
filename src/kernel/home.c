#include "home.h"
#include "carts.h"
#include "bench.h"
#include "b3dpi.h"
#include "config.h"
#include "crumbs.h"
#include "demo.h"
#include "dmatest.h"
#include "input.h"
#include "monitor.h"
#include "pager.h"
#include "pointer.h"
#include "reports.h"
#include "sysinfo.h"
#include "syskeys.h"
#include "testpattern.h"
#include "update.h"
#include "version.h"
#include "audio/audio.h"
#include "bm/bm.h"
#include "bm/runtime.h"
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
#include "net/wifi_auto.h"
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

static void t_dma(framebuffer_t *fb)
{
    heading("DMA test");
    dma_test(fb);
}

static void t_bench3d(framebuffer_t *fb)
{
    heading("3D Bench: every 3D test with every driver");
    bm_bench3d(fb);
}

/* the development assistant (M30) on its own: ask, see the code and the
 * sprites it would give an editor (also the monitor's I) */
void home_assistant(framebuffer_t *fb)
{
    extern const uint8_t bm_assistant_cart[], bm_assistant_cart_end[];
    bm_stats_t bs;
    fat_entry_t e;
    uint8_t *sd_data = NULL;
    size_t sd_len = 0;
    if ((fat_find("/carts/assistant.bm", &e) == 0 || fat_find("/carts/assistan.bm", &e) == 0) &&
        fat_load(&e, &sd_data, &sd_len) == 0) {
        bm_play(fb, sd_data, sd_len, 24u * 3600u, &bs);
        free(sd_data);
        return;
    }
    size_t len = (size_t)(bm_assistant_cart_end - bm_assistant_cart);
    if (len == 0) {
        kprintf("assistant: not installed (get it from Market > Apps)\n");
        return;
    }
    bm_play(fb, bm_assistant_cart, len, 24u * 3600u, &bs);
}

typedef struct {
    const char *title, *about;
    int icon;
    uint32_t rgb;
    void (*run)(framebuffer_t *fb);     /* NULL: the monitor */
    int wait;
    g16_sheet_t cover;
    const char *report;                 /* what it prints is a report of this kind (reports.h), or NULL */
    int group;                          /* a utility: 1 = Settings > Screen, 0 = Settings > System */
} tool_t;

/* bm Code, the code editor */
static void t_code(framebuffer_t *fb)
{
    carts_code(fb, NULL);
}

static tool_t tools[] = {
    /* the Dev tab, after the SDK */
    { "bm Code", "code editor: tabs, two pages, small font", MENU_ICON_CODE, 0x1E8A5A, t_code, 0, { 0 }, NULL, 0 },
    { "Assistant", "help with code and sprites; F6 in the tools", MENU_ICON_ASSIST, 0x2A6A9A, home_assistant, 0, { 0 }, NULL, 0 },
    { "Monitor", "the text console with every command (h: help)", MENU_ICON_TERMINAL, 0x2A3A4A, NULL, 0, { 0 }, NULL, 0 },
    { "Lua", "Lua 5.4 prompt (USB keyboard); Ctrl-D or exit() returns", MENU_ICON_LUA, 0x2A3A9A, t_lua, 0, { 0 }, NULL, 0 },
#define NDEV 4
    /* the utilities: Settings > System > Utilities, and group 1 in Settings > Screen */
    { "DMA test", "copies by the CPU against the DMA, step by step", MENU_ICON_ARROWS, 0x2A7A8A, t_dma, 1, { 0 }, "dma", 0 },
    { "3D Bench", "every 3D test, every driver: bars, report on the SD", MENU_ICON_GAUGE, 0x2A6A8A, t_bench3d, 1,
      { 0 }, NULL, 1 },
};

#define NTOOLS ((int)(sizeof tools / sizeof tools[0]))

void home_init(void)
{
    /* bm Code: the bm Suite's icon, the COVER of its cartridge */
    extern const uint8_t bm_code_cart[], bm_code_cart_end[];
    bm_cart_t bc;
    char err[8];
    size_t code_len = (size_t)(bm_code_cart_end - bm_code_cart);
    if (!tools[0].cover.px && code_len > 0 &&
        bm_parse(bm_code_cart, code_len, &bc, err, sizeof err) == 0 &&
        bc.cover_rgba)
        menu_load_cover(&tools[0].cover, bc.cover_rgba, bc.cover_w, bc.cover_h);
    for (int i = 0; i < NDEV; i++)
        if (!tools[i].cover.px)
            menu_make_tool_cover(&tools[i].cover, tools[i].title, tools[i].icon, tools[i].rgb);
}

int home_tools(void) { return NDEV; }
int home_tool_is_utility(int i) { return i >= NDEV; }
int home_utils(void) { return NTOOLS - NDEV; }
const char *home_util_title(int i) { return tools[NDEV + i].title; }
const char *home_util_about(int i) { return tools[NDEV + i].about; }
int home_util_group(int i) { return tools[NDEV + i].group; }
const char *home_tool_title(int i) { return tools[i].title; }
const g16_sheet_t *home_tool_cover(int i) { return tools[i].cover.px ? &tools[i].cover : NULL; }

/* a tool whose printout is a report: run between reports_begin and _end */
static int reported;

static void run_reported(framebuffer_t *fb)
{
    home_report_begin(tools[reported].report);
    tools[reported].run(fb);
    home_report_end();
}

void home_report_begin(const char *kind)
{
    syskeys_test_begin();               /* not stopped yet: this tool's own */
    reports_begin(kind);
}

void home_report_end(void)
{
    /* a rendering test stopped by the user (syskeys.h): its report, made
     * so far, is thrown away, never on the SD card nor sent */
    if (syskeys_test_stopped())
        reports_drop();
    else
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

void home_util_start(int i, home_do_t *d)
{
    home_tool_start(NDEV + i, d);
}

/* ---- what differs between the consoles in Settings (home.h): the Pi's ---- */

int home_wireless(void)
{
    return board()->wireless;
}

void home_restart(void)
{
    crumbs_restart(0);
}

void home_poweroff(void)
{
    wifi_auto_stop();
    crumbs_restart(1);
}

const char *home_poweroff_help(void)
{
    return "Stops the Pi: it starts again when its power is plugged back";
}

const char *home_poweroff_detail(void)
{
    return "To start it again, plug its power back in.";
}

const char *home_test_help(void)
{
    return "The buttons each player holds, for 10 s";
}

const char *home_connect_help(void)
{
    return "Lists the networks; the USB keyboard types the password";
}

int home_has_battery(void)
{
    return 0;
}

void home_controls_changed(void)
{
}
