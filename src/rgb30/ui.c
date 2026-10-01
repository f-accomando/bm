/*
 * The RGB30's menu: 512x512, centred on the 720x720 panel. Games are the
 * .s16 files in bm/ on the SD card (the format is still to be defined: they
 * are listed, not run); .bm cartridges are for the Pi and stay hidden
 * unless bm/config.txt says show_bm=1. Then the tools: input test, system
 * information, Bluetooth, WiFi, boot log, Lua, reboot, power off.
 */
#include "ui.h"
#include "pad.h"
#include "plat.h"
#include "drivers/fb.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "kernel/config.h"
#include "kernel/tick.h"
#include "kernel/version.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "bt/bt.h"

#include <stdarg.h>
#include <string.h>
#include <strings.h>

#define W 512
#define H 512

#define C_BG        0x10141c
#define C_HEAD      0x1c2433
#define C_TEXT      0xe8ecf2
#define C_DIM       0x8a94a6
#define C_ACCENT    0x3aa0ff
#define C_SEL       0x24406a
#define C_OK        0x4ccf6a
#define C_WARN      0xffb13a
#define C_BAD       0xff5a5a

#define MAX_GAMES   64

static framebuffer_t *fb;

static uint32_t rgb(uint32_t c)
{
    return fb_color(fb, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

static void text(int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    gfx_text(fb, &font_console_8x16, x, y, s, rgb(fg), rgb(bg));
}

/* the console font twice as big, for titles */
static void text2x(int x, int y, const char *s, uint32_t fg)
{
    const font_t *f = &font_console_8x16;
    uint32_t c = rgb(fg);
    for (; *s; s++, x += f->width * 2) {
        const uint8_t *g = f->glyphs + (uint8_t)*s * f->height;
        for (int row = 0; row < f->height; row++)
            for (int col = 0; col < f->width; col++)
                if (g[row] & (0x80 >> col))
                    gfx_rect(fb, x + col * 2, y + row * 2, 2, 2, c);
    }
}

static void textf(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

struct sbuf { char *p; size_t left; };

static void sbuf_putc(char c, void *ctx)
{
    struct sbuf *b = ctx;
    if (b->left > 1) {
        *b->p++ = c;
        b->left--;
    }
}

static void textf(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
{
    char buf[80];
    struct sbuf b = { buf, sizeof buf };
    va_list ap;
    va_start(ap, fmt);
    kvprintf(sbuf_putc, &b, fmt, ap);
    va_end(ap);
    *b.p = 0;
    text(x, y, buf, fg, bg);
}

/* Layout: everything on the font's grid (1x text at multiples of 8 x 16,
 * 2x text at multiples of 16), which is also how the tests read it back. */
#define HEAD_H      64
#define FOOT_Y      (H - 32)
#define LIST_Y      80
#define ROW_H       48

static void frame_begin(const char *title)
{
    gfx_clear(fb, rgb(C_BG));
    gfx_rect(fb, 0, 0, W, HEAD_H, rgb(C_HEAD));
    text2x(16, 16, "bm", C_ACCENT);
    text2x(80, 16, title, C_TEXT);
    char v[32];
    ksnprintf(v, sizeof v, "%s", bm_version);
    text(W - 8 - 8 * (int)strlen(v), 32, v, C_DIM, C_HEAD);
}

static void footer(const char *hints)
{
    gfx_rect(fb, 0, FOOT_Y, W, H - FOOT_Y, rgb(C_HEAD));
    text(16, FOOT_Y + 16, hints, C_DIM, C_HEAD);
}

static void frame_end(void)
{
    fb_flip(fb);
}

/* --- games on the SD card --- */

static struct { char name[56]; uint32_t size; int is_bm; } games[MAX_GAMES];
static int n_games;
static const char *sd_state = "not read";

static void scan_games(void)
{
    n_games = 0;
    fat_dir_t d;
    fat_entry_t e;
    const char *show = config_get("show_bm");
    int show_bm = show && strcmp(show, "1") == 0;
    if (fat_opendir(&d, "bm") != 0) {
        sd_state = "no bm/ folder on the SD card";
        return;
    }
    sd_state = "bm/";
    while (fat_readdir(&d, &e) == 1 && n_games < MAX_GAMES) {
        if (e.is_dir)
            continue;
        const char *dot = strrchr(e.name, '.');
        if (!dot)
            continue;
        int is_s16 = strcasecmp(dot, ".s16") == 0;
        int is_bm = strcasecmp(dot, ".bm") == 0;
        if (!is_s16 && !(is_bm && show_bm))
            continue;               /* .bm: for the Pi, hidden here */
        ksnprintf(games[n_games].name, sizeof games[0].name, "%s", e.name);
        games[n_games].size = e.size;
        games[n_games].is_bm = is_bm;
        n_games++;
    }
}

/* --- pages --- */

static void wait_back(void)
{
    while (!(pad_pressed() & (PAD_B | PAD_A)))
        timer_delay_ms(10);
}

static void page_message(const char *title, const char *lines[], int n)
{
    frame_begin(title);
    for (int i = 0; i < n; i++)
        text(16, LIST_Y + i * 16, lines[i], i == 0 ? C_TEXT : C_DIM, C_BG);
    footer("A/B back");
    frame_end();
    wait_back();
}

static void page_game(int i)
{
    char l0[64];
    ksnprintf(l0, sizeof l0, "%s (%lu bytes)", games[i].name, games[i].size);
    const char *lines[] = {
        l0,
        "",
        games[i].is_bm ? "A .bm cartridge for the Pi: the RGB30 does not run"
                       : "The .s16 format of the RGB30 is not defined yet:",
        games[i].is_bm ? "them (show_bm=1 only lists them)."
                       : "games will start from here once it is.",
    };
    page_message("Game", lines, 4);
}

static void draw_button(int x, int y, const char *name, int on)
{
    gfx_rect(fb, x, y, 72, 32, rgb(on ? C_OK : C_HEAD));
    text(x + 8, y + 16 - 8, name, on ? 0x000000 : C_DIM, on ? C_OK : C_HEAD);
}

static void draw_stick(int cx, int cy, int16_t ax, int16_t ay, const char *name)
{
    gfx_rect(fb, cx - 50, cy - 50, 100, 100, rgb(C_HEAD));
    gfx_rect(fb, cx - 1, cy - 50, 2, 100, rgb(C_BG));
    gfx_rect(fb, cx - 50, cy - 1, 100, 2, rgb(C_BG));
    int x = cx + ax * 46 / 32768, y = cy + ay * 46 / 32768;
    gfx_rect(fb, x - 4, y - 4, 8, 8, rgb(C_ACCENT));
    textf(cx - 48, cy + 64, C_DIM, C_BG, "%s %6d %6d", name, ax, ay);
}

/* every button and both sticks, live; Start + Select together to leave */
static void page_input(void)
{
    uint32_t idle_since = timer_ticks();
    for (;;) {
        uint32_t s = pad_state();
        int16_t ax[4];
        plat_sticks(ax);
        frame_begin("Input test");
        for (int i = 0; i < PAD_COUNT; i++)
            draw_button(16 + (i % 6) * 80, LIST_Y + (i / 6) * 48, pad_names[i], (s >> i) & 1);
        draw_stick(128, 304, ax[0], ax[1], "L");
        draw_stick(384, 304, ax[2], ax[3], "R");
        textf(16, 432, C_DIM, C_BG, "held: %08lx", s);
        footer("Start + Select: back (or 20 s without input)");
        frame_end();
        if ((s & (PAD_START | PAD_SELECT)) == (PAD_START | PAD_SELECT))
            break;
        if (s)
            idle_since = timer_ticks();
        else if (timer_ticks() - idle_since > 20000000u)
            break;
        timer_delay_ms(16);
    }
    while (pad_state())
        timer_delay_ms(10);
    pad_pressed();
}

static void page_system(void)
{
    char l[10][72];
    uint64_t midr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    ksnprintf(l[0], sizeof l[0], "bm %s on %s", bm_version, PLAT_NAME);
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    ksnprintf(l[1], sizeof l[1], "CPU %s r%lup%lu, counter %lu Hz",
              ((midr >> 4) & 0xfff) == 0xd05 ? "Cortex-A55" : "ARMv8",
              (uint32_t)(midr >> 20) & 0xf, (uint32_t)midr & 0xf, (uint32_t)f);
    ksnprintf(l[2], sizeof l[2], "heap %lu MiB free of %lu MiB",
              (uint32_t)((heap_end() - heap_brk()) >> 20),
              (uint32_t)((heap_end() - heap_start()) >> 20));
    ksnprintf(l[3], sizeof l[3], "Lua %lu KiB (peak %lu KiB)",
              (uint32_t)(luavm_mem() >> 10), (uint32_t)(luavm_mem_peak() >> 10));
    ksnprintf(l[4], sizeof l[4], "uptime %lu s", tick_ms() / 1000);
    ksnprintf(l[5], sizeof l[5], "SD (%s): %lu MiB, %s", sd_controller(),
              sd_blocks() / 2048, sd_blocks() ? fat_describe() : sd_error());
    ksnprintf(l[6], sizeof l[6], "games: %d in %s", n_games, sd_state);
    int mv, charge;
    if (plat_battery(&mv, &charge) == 0)
        ksnprintf(l[7], sizeof l[7], "battery %d.%02d V%s", mv / 1000, (mv % 1000) / 10,
                  charge == 2 ? ", full" : charge == 1 ? ", charging" : "");
    else
        ksnprintf(l[7], sizeof l[7], "battery: unknown");
    ksnprintf(l[8], sizeof l[8], "%s", plat_display_info());
    frame_begin("System");
    for (int i = 0; i < 9; i++)
        text(16, LIST_Y + i * 32, l[i], i == 0 ? C_TEXT : C_DIM, C_BG);
    footer("A/B back");
    frame_end();
    wait_back();
}

/* the console with everything printed since boot */
static void page_log(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    wait_back();
    console_suspend(1);
}

static void serial_lua(void)
{
    const char *lines[] = {
        "Lua prompt on the serial port (1500000 8N1).",
        "Type exit() or Ctrl-D there to come back.",
    };
    frame_begin("Lua");
    for (int i = 0; i < 2; i++)
        text(16, LIST_Y + i * 16, lines[i], i == 0 ? C_TEXT : C_DIM, C_BG);
    frame_end();
    ui_serial_repl();
}

/* Bluetooth, in the console (the stack reports what it does with kprintf):
 * started on the first visit; then pairing a pad or a keyboard */
static int bt_on;

static void page_bt(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    if (!bt_on) {
        kprintf("\n\x1b[1mBluetooth\x1b[0m: starting the RTL8821CS...\n");
        bt_on = bt_start() == 0;
    }
    for (;;) {
        if (bt_on)
            kprintf("\n\x1b[96mA\x1b[0m pair a controller (10 s)  \x1b[96mX\x1b[0m pair a keyboard (20 s)\n"
                    "\x1b[96mY\x1b[0m forget all  \x1b[96mB\x1b[0m back   pads: %u, %s\n",
                    bt_pads(), bt_keyboard() ? "keyboard connected" : "no keyboard");
        else
            kprintf("\nBluetooth is off (see above). \x1b[96mA\x1b[0m try again  \x1b[96mB\x1b[0m back\n");
        uint32_t p;
        while (!(p = pad_pressed()))
            timer_delay_ms(10);
        if (p & PAD_B)
            break;
        if (!bt_on) {
            if (p & PAD_A)
                bt_on = bt_start() == 0;
            continue;
        }
        if (p & PAD_A) {
            kprintf("put the controller in pairing mode (DS4: Share + PS)\n");
            bt_scan(10);
        } else if (p & PAD_X) {
            bt_pair_keyboard(20);
        } else if (p & PAD_Y) {
            bt_forget_all();
        }
    }
    console_suspend(1);
}

enum { T_INPUT, T_SYSTEM, T_BT, T_WIFI, T_LOG, T_LUA, T_REBOOT, T_OFF, T_COUNT };
static const char *const tool_names[T_COUNT] = {
    "Input test", "System", "Bluetooth", "WiFi", "Boot log", "Lua (serial)", "Reboot", "Power off",
};
static const char *const tool_help[T_COUNT] = {
    "every button and both sticks, live",
    "board, memory, SD card, display",
    "controllers and keyboards",
    "network (bm/config.txt: wifi_ssid, wifi_psk)",
    "everything printed since boot",
    "a Lua prompt on the serial port",
    "restart the console",
    "turn the console off",
};

static void run_tool(int t)
{
    switch (t) {
    case T_INPUT: page_input(); break;
    case T_SYSTEM: page_system(); break;
    case T_BT: page_bt(); break;
    case T_WIFI: {
        const char *l[] = { "WiFi: coming next (RTL8821CS on SDIO).", "" };
        page_message("WiFi", l, 1);
        break;
    }
    case T_LOG: page_log(); break;
    case T_LUA: serial_lua(); break;
    case T_REBOOT: kprintf("rebooting...\n"); plat_reset();
    case T_OFF: kprintf("power off\n"); plat_poweroff();
    }
}

void ui_home(framebuffer_t *f)
{
    fb = f;
    console_suspend(1);
    scan_games();
    int sel = 0, top = 0;
    kprintf("cartridge menu: %d games\n", n_games);
    for (;;) {
        int n = n_games + T_COUNT;
        int rows = n_games ? 7 : 6;
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;

        frame_begin("home");
        int y = LIST_Y;
        if (!n_games) {
            text(16, y, "No games yet:", C_DIM, C_BG);
            text(16, y + 16, strcmp(sd_state, "bm/") == 0
                 ? "put .s16 games in bm/ on the SD card" : sd_state, C_DIM, C_BG);
            y += ROW_H;
        }
        for (int i = top; i < n && i < top + rows; i++) {
            int is_game = i < n_games;
            const char *name = is_game ? games[i].name : tool_names[i - n_games];
            if (i == sel)
                gfx_rect(fb, 8, y - 8, W - 16, ROW_H - 4, rgb(C_SEL));
            gfx_rect(fb, 16, y - 4, 6, 36, rgb(is_game ? C_OK : C_ACCENT));
            text2x(48, y, name, C_TEXT);
            y += ROW_H;
        }
        const char *help = sel < n_games ? (games[sel].is_bm ? "Pi cartridge (show_bm=1)" : ".s16 game")
                                         : tool_help[sel - n_games];
        text(16, FOOT_Y - 16, help, C_DIM, C_BG);
        footer("Up/Down: choose   A: open");
        frame_end();

        uint32_t p;
        for (;;) {
            p = pad_pressed();
            int c = pad_serial_char();
            if (c == '`') {
                serial_lua();
                p = 0;
                break;
            }
            if (p)
                break;
            timer_delay_ms(10);
        }
        if (p & PAD_UP) sel--;
        if (p & PAD_DOWN) sel++;
        if (p & PAD_A) {
            if (sel < n_games)
                page_game(sel);
            else
                run_tool(sel - n_games);
        }
    }
}
