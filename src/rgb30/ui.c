/*
 * The RGB30's menu: 360x360, shown twice as big (the whole 720x720 panel,
 * every pixel a 2x2 square), in three tabs as on the Pi: Games (the .s16
 * files in bm/ on the SD card, whose format is still to be defined, and
 * for testing the Pi's .bm cartridges, which run; show_bm=0 hides them),
 * Dev (the 3D Bench, the render bench, the display modes, the input test,
 * the boot log, Lua) and System (Bluetooth, WiFi, updates from GitHub, the console's state,
 * reboot, power off).
 * L1 / R1 or left / right change tab.
 */
#include "ui.h"
#include "pad.h"
#include "plat.h"
#include "display.h"
#include "drivers/fb.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "kernel/config.h"
#include "kernel/tick.h"
#include "kernel/update.h"
#include "kernel/version.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "bt/bt.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "bm/runtime.h"
#include "b3d_rgb30.h"

#include <stdlib.h>

#include <stdarg.h>
#include <string.h>
#include <strings.h>

#define W 360
#define H 360

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

/* Layout (360x360, 45 x 22 characters): everything on the font's grid
 * (text at multiples of 8 x 16), which is also how the tests read it back.
 * The bar (the tabs, or a page's title) at the top, a list of rows 32
 * pixels high, a line of help, the hints at the bottom. */
#define HEAD_H      40
#define LIST_Y      48              /* the text of the first row */
#define ROW_H       32
#define ROWS        8
#define HELP_Y      304
#define FOOT_Y      328

static void frame_begin(const char *title)
{
    gfx_clear(fb, rgb(C_BG));
    gfx_rect(fb, 0, 0, W, HEAD_H, rgb(C_HEAD));
    text(8, 16, "bm", C_ACCENT, C_HEAD);
    text(40, 16, title, C_TEXT, C_HEAD);
    char v[24];
    ksnprintf(v, sizeof v, "%.12s", bm_version);
    text(W - 8 - 8 * (int)strlen(v), 16, v, C_DIM, C_HEAD);
}

static void footer(const char *hints)
{
    gfx_rect(fb, 0, FOOT_Y, W, H - FOOT_Y, rgb(C_HEAD));
    text(8, FOOT_Y + 8, hints, C_DIM, C_HEAD);
}

/* s in lines of at most 44 characters from (8, y), at most n lines;
 * returns the lines used */
static int text_wrap(int y, const char *s, int n, uint32_t fg)
{
    int used = 0;
    while (*s && used < n) {
        char line[45];
        int len = (int)strlen(s);
        int k = len > 44 ? 44 : len;
        if (len > 44)                               /* at a space, if there is one */
            for (int j = 44; j > 20; j--)
                if (s[j] == ' ') {
                    k = j;
                    break;
                }
        memcpy(line, s, (size_t)k);
        line[k] = 0;
        text(8, y + used * 16, line, fg, C_BG);
        s += k;
        while (*s == ' ')
            s++;
        used++;
    }
    return used;
}

static void frame_end(void)
{
    fb_flip(fb);
}

/* --- games on the SD card --- */

static struct { char name[56]; uint32_t size; int is_bm; } games[MAX_GAMES];
static int n_games, n_hidden;      /* n_hidden: .bm hidden by show_bm=0 */
static const char *sd_state = "not read";

static void scan_games(void)
{
    n_games = n_hidden = 0;
    fat_dir_t d;
    fat_entry_t e;
    const char *show = config_get("show_bm");
    int show_bm = !show || strcmp(show, "0") != 0;     /* for testing: shown unless show_bm=0 */
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
        if (is_bm && !show_bm)
            n_hidden++;
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
    int y = LIST_Y;
    for (int i = 0; i < n && y < HELP_Y; i++)
        y += 16 * (lines[i][0] ? text_wrap(y, lines[i], (HELP_Y - y) / 16, i == 0 ? C_TEXT : C_DIM) : 1);
    footer("A/B back");
    frame_end();
    wait_back();
}

/* A Pi cartridge (listed for testing, show_bm=0 hides them): the runtime of
 * src/bm, its screen as big as the panel (rgb30/display.h), the 3D on the
 * ARM, no sound yet. Start + Select leaves. */
#define PLAY_SECS (24u * 3600u)

static void play_bm(int i)
{
    char path[80];
    ksnprintf(path, sizeof path, "/bm/%s", games[i].name);
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        const char *lines[] = { path, "", "cannot be read:", fat_error() };
        page_message("Game", lines, 4);
        return;
    }
    kprintf("play: %s (%lu bytes)\n", path, (uint32_t)len);
    bm_stats_t st;
    bm_run(fb, data, len, PLAY_SECS, &st, 0);
    free(data);
    console_suspend(1);                             /* the menu draws itself */
    bm_print_stats(&st);
    while (pad_state())                             /* Start + Select still held */
        timer_delay_ms(10);
    pad_pressed();
    if (!st.ok) {
        const char *lines[] = { games[i].name, "", "stopped with an error:", bm_last_error() };
        page_message("Game", lines, 4);
    }
}

static void page_game(int i)
{
    if (games[i].is_bm) {
        play_bm(i);
        return;
    }
    char l0[64];
    ksnprintf(l0, sizeof l0, "%s (%lu bytes)", games[i].name, games[i].size);
    const char *lines[] = {
        l0,
        "",
        "The .s16 format of the RGB30 is not defined yet: games will start from here once it is.",
    };
    page_message("Game", lines, 3);
}

static void draw_button(int x, int y, const char *name, int on)
{
    gfx_rect(fb, x, y, 56, 24, rgb(on ? C_OK : C_HEAD));
    text(x + 4, y + 4, name, on ? 0x000000 : C_DIM, on ? C_OK : C_HEAD);
}

static void draw_stick(int cx, int cy, int16_t ax, int16_t ay, const char *name)
{
    gfx_rect(fb, cx - 40, cy - 40, 80, 80, rgb(C_HEAD));
    gfx_rect(fb, cx - 1, cy - 40, 2, 80, rgb(C_BG));
    gfx_rect(fb, cx - 40, cy - 1, 80, 2, rgb(C_BG));
    int x = cx + ax * 36 / 32768, y = cy + ay * 36 / 32768;
    gfx_rect(fb, x - 3, y - 3, 6, 6, rgb(C_ACCENT));
    textf(cx - 64, 256, C_DIM, C_BG, "%s %6d %6d", name, ax, ay);
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
            draw_button(8 + (i % 6) * 58, 48 + (i / 6) * 32, pad_names[i], (s >> i) & 1);
        draw_stick(88, 200, ax[0], ax[1], "L");
        draw_stick(272, 200, ax[2], ax[3], "R");
        textf(8, 288, C_DIM, C_BG, "held: %08lx", s);
        footer("Start+Select: back (or 20 s idle)");
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
    char l[10][200];
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
    int y = LIST_Y;
    for (int i = 0; i < 9 && y < FOOT_Y - 8; i++)
        y += 16 * text_wrap(y, l[i], (FOOT_Y - 8 - y) / 16, i == 0 ? C_TEXT : C_DIM);
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
    frame_begin("Lua");
    int y = LIST_Y + 16 * text_wrap(LIST_Y, "Lua prompt on the serial port (1500000 8N1).", 2, C_TEXT);
    text_wrap(y, "Type exit() or Ctrl-D there to come back.", 2, C_DIM);
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
            kprintf("\n\x1b[96m%s\x1b[0m pair a controller (10 s)  \x1b[96mX\x1b[0m pair a keyboard (20 s)\n"
                    "\x1b[96mY\x1b[0m forget all  \x1b[96m%s\x1b[0m back   pads: %u, %s\n",
                    pad_ok_name(), pad_back_name(),
                    bt_pads(), bt_keyboard() ? "keyboard connected" : "no keyboard");
        else
            kprintf("\nBluetooth is off (see above). \x1b[96m%s\x1b[0m try again  \x1b[96m%s\x1b[0m back\n",
                    pad_ok_name(), pad_back_name());
        uint32_t p;
        while (!(p = pad_pressed()))
            timer_delay_ms(10);
        if (p & pad_back)
            break;
        if (!bt_on) {
            if (p & pad_ok)
                bt_on = bt_start() == 0;
            continue;
        }
        if (p & pad_ok) {
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

/* WiFi, in the console too: the chip started on the first visit */
static int wifi_on;

static void page_wifi(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    if (!wifi_on) {
        kprintf("\n\x1b[1mWiFi\x1b[0m: starting the RTL8821CS...\n");
        wifi_on = wifi_start() == 0;
    }
    for (;;) {
        const char *ssid = config_get("wifi_ssid");
        if (wifi_on && wifi_linked())
            kprintf("\nconnected to \"%s\", IP %s\n", ssid ? ssid : "?", net_ip_text());
        if (wifi_on)
            kprintf("\n\x1b[96m%s\x1b[0m scan  \x1b[96mX\x1b[0m join %s%s%s  \x1b[96m%s\x1b[0m back\n",
                    pad_ok_name(), ssid && ssid[0] ? "\"" : "",
                    ssid && ssid[0] ? ssid : "(none: bm/config.txt)", ssid && ssid[0] ? "\"" : "",
                    pad_back_name());
        else
            kprintf("\nWiFi is off (see above). \x1b[96m%s\x1b[0m try again  \x1b[96m%s\x1b[0m back\n",
                    pad_ok_name(), pad_back_name());
        uint32_t p;
        while (!(p = pad_pressed()))
            timer_delay_ms(10);
        if (p & pad_back)
            break;
        if (!wifi_on) {
            if (p & pad_ok)
                wifi_on = wifi_start() == 0;
        } else if (p & pad_ok) {
            wifi_scan();
        } else if (p & PAD_X) {
            /* the address comes from DHCP, then the network console */
            if (wifi_connect() == 0 && net_start(&net_wifi) == 0)
                net_wait_ip(15000);
        }
    }
    console_suspend(1);
}

/* Updates from GitHub, as on the Pi (src/kernel/update.c): the latest
 * release's manifest for the RGB30 (manifest-rgb30.txt, signed), what would
 * change on the card; then A installs it (kernel8.img last) and restarts.
 * The WiFi first (System > WiFi); update_url=sd:/folder/ for a release
 * copied on the card. */
static void page_update(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    update_check(fb);
    for (;;) {
        const char *v = update_ready();
        if (v)
            kprintf("\n\x1b[96m%s\x1b[0m install %s and restart  \x1b[96m%s\x1b[0m back\n",
                    pad_ok_name(), v, pad_back_name());
        else
            kprintf("\n\x1b[96m%s\x1b[0m check again  \x1b[96m%s\x1b[0m back\n", pad_ok_name(),
                    pad_back_name());
        uint32_t p;
        while (!(p = pad_pressed()))
            timer_delay_ms(10);
        if (p & pad_back)
            break;
        if (p & pad_ok) {
            if (v)
                update_install(fb);     /* restarts; back here only if it stopped */
            else
                update_check(fb);
        }
    }
    console_suspend(1);
}

/* --- the display modes a game (and the GPU) can use, with a test image --- */

static const struct { uint32_t w, h, scale; int smooth; } modes[] = {
    { 720, 720, 1, 0 },             /* the panel, 1:1 */
    { 360, 360, 2, 0 },             /* a quarter of the pixels, sharp */
    { 360, 360, 2, 1 },             /* the same, smooth */
    { 240, 240, 3, 0 },
    { 512, 512, 1, 0 },             /* 1:1 in the middle */
};

static void test_image(int i)
{
    const plat_mode_t *m = fb_mode();
    uint32_t w = fb->width, h = fb->height;
    gfx_clear(fb, rgb(C_BG));
    for (uint32_t x = 0; x < w; x += FB_TILE)     /* the GPU's 16-pixel tiles */
        gfx_rect(fb, (int)x, 0, 1, (int)h, rgb(C_HEAD));
    for (uint32_t y = 0; y < h; y += FB_TILE)
        gfx_rect(fb, 0, (int)y, (int)w, 1, rgb(C_HEAD));
    static const uint32_t bars[] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000,
                                     0x0000ff, 0x000000 };
    uint32_t bw = w / 8;
    for (int b = 0; b < 8; b++)
        gfx_rect(fb, (int)(b * bw), (int)(h - 64), (int)bw, 48, rgb(bars[b]));
    for (uint32_t k = 0; k < w && k < h; k++) {   /* diagonals: the scaling's edges */
        gfx_rect(fb, (int)k, (int)k, 1, 1, rgb(C_ACCENT));
        gfx_rect(fb, (int)(w - 1 - k), (int)k, 1, 1, rgb(C_ACCENT));
    }
    gfx_rect(fb, 0, 0, (int)w, 1, rgb(C_OK));     /* the edges: all of it visible */
    gfx_rect(fb, 0, (int)h - 1, (int)w, 1, rgb(C_OK));
    gfx_rect(fb, 0, 0, 1, (int)h, rgb(C_OK));
    gfx_rect(fb, (int)w - 1, 0, 1, (int)h, rgb(C_OK));
    textf(16, 16, C_TEXT, C_BG, "%lux%lu x%lu %s", w, h, m->scale, m->smooth ? "smooth" : "sharp");
    textf(16, 64, C_DIM, C_BG, "on the panel %lux%lu", m->out_w, m->out_h);
    textf(16, 32, C_DIM, C_BG, "pitch %lu, %lu page%s", fb->pitch, fb->buffers,
          fb->buffers == 1 ? "" : "s");
    textf(16, 48, C_DIM, C_BG, "at %08lx", fb->bus);
    textf(16, 80, C_ACCENT, C_BG, "%s: %s mode", pad_ok_name(),
          i + 1 < (int)(sizeof modes / sizeof modes[0]) ? "next" : "first");
    textf(16, 96, C_ACCENT, C_BG, "%s: back", pad_back_name());
    fb_show(fb, 0);
}

static void page_display(void)
{
    int n = (int)(sizeof modes / sizeof modes[0]);
    kprintf("display: test of the modes\n");
    for (int i = 0;;) {
        int r = fb_init_mode(fb, modes[i].w, modes[i].h, 2, 32, modes[i].scale, modes[i].smooth);
        kprintf("display: %lux%lu x%lu %s: %s\n", modes[i].w, modes[i].h, modes[i].scale,
                modes[i].smooth ? "smooth" : "sharp", r ? "refused" : "on");
        if (r == 0)
            test_image(i);
        uint32_t p;
        while (!(p = pad_pressed()))
            timer_delay_ms(10);
        if (p & pad_back)
            break;
        if (p & (pad_ok | PAD_RIGHT | PAD_DOWN))
            i = (i + 1) % n;
        else if (p & (PAD_LEFT | PAD_UP))
            i = (i + n - 1) % n;
    }
    fb_init(fb, W, H, 2);                           /* the menu's mode again */
}

/* the 3D Bench (src/bm/b3d.c) and the render bench, in the console */
static void page_bench3d(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    rgb30_bench3d(fb);
    console_suspend(1);
    while (pad_state())
        timer_delay_ms(10);
    pad_pressed();
}

static void page_render(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    kprintf("\n\x1b[1mRender bench\x1b[0m: map, 256 sprites and text at 640x360 RGB565\n");
    bm_bench_report(fb, 120);
    kprintf("\n\x1b[96m%s\x1b[0m back\n", pad_back_name());
    wait_back();
    console_suspend(1);
}

/* --- the tabs --- */

typedef struct {
    const char *name, *help;
    void (*run)(void);
} item_t;

static void do_reboot(void)   { kprintf("rebooting...\n"); plat_reset(); }
static void do_poweroff(void) { kprintf("power off\n"); plat_poweroff(); }

static const item_t dev_items[] = {
    { "3D Bench", "every 3D test: bars, report in bm/bench", page_bench3d },
    { "Render bench", "map, sprites and text, 640x360", page_render },
    { "Display", "the screen modes for games and the GPU", page_display },
    { "Input test", "every button and both sticks, live", page_input },
    { "Boot log", "everything printed since boot", page_log },
    { "Lua (serial)", "a Lua prompt on the serial port", serial_lua },
};
static const item_t system_items[] = {
    { "Bluetooth", "controllers and keyboards", page_bt },
    { "WiFi", "network (bm/config.txt: wifi_ssid, wifi_psk)", page_wifi },
    { "Updates", "the latest bm from GitHub (WiFi first)", page_update },
    { "System", "board, memory, SD card, battery, display", page_system },
    { "Reboot", "restart the console", do_reboot },
    { "Power off", "turn the console off", do_poweroff },
};

enum { TAB_GAMES, TAB_DEV, TAB_SYSTEM, TAB_COUNT };
static const char *const tab_names[TAB_COUNT] = { "Games", "Dev", "System" };

static int tab_size(int t)
{
    return t == TAB_GAMES ? n_games
         : t == TAB_DEV ? (int)(sizeof dev_items / sizeof dev_items[0])
         : (int)(sizeof system_items / sizeof system_items[0]);
}

static const item_t *tab_item(int t, int i)
{
    return t == TAB_DEV ? &dev_items[i] : &system_items[i];
}

/* the bar: "bm", then the tabs, the selected one as a pill */
static void tab_bar(int tab)
{
    gfx_clear(fb, rgb(C_BG));
    gfx_rect(fb, 0, 0, W, HEAD_H, rgb(C_HEAD));
    text(8, 16, "bm", C_ACCENT, C_HEAD);
    int x = 40;
    for (int t = 0; t < TAB_COUNT; t++) {
        int w = 8 * (int)strlen(tab_names[t]);
        if (t == tab) {
            gfx_rect(fb, x - 8, 10, w + 16, 28, rgb(C_ACCENT));
            text(x, 16, tab_names[t], 0x000000, C_ACCENT);
        } else {
            text(x, 16, tab_names[t], C_DIM, C_HEAD);
        }
        x += w + 24;
    }
}

void ui_home(framebuffer_t *f)
{
    fb = f;
    console_suspend(1);
    scan_games();
    int tab = TAB_GAMES, sel[TAB_COUNT] = { 0 }, top[TAB_COUNT] = { 0 };
    kprintf("cartridge menu: %d games", n_games);
    if (n_hidden)
        kprintf(" (%d .bm hidden by show_bm=0 in bm/config.txt)", n_hidden);
    kprintf("\n");
    for (;;) {
        int n = tab_size(tab);
        int *s = &sel[tab], *t = &top[tab];
        if (*s >= n) *s = n - 1;
        if (*s < 0) *s = 0;
        if (*s < *t) *t = *s;
        if (*s >= *t + ROWS) *t = *s - ROWS + 1;

        tab_bar(tab);
        const char *help = "";
        if (tab == TAB_GAMES && !n_games) {
            text(8, LIST_Y, "No games yet:", C_DIM, C_BG);
            text_wrap(LIST_Y + 16, strcmp(sd_state, "bm/") == 0 ? "put .s16 games in bm/ on the SD card"
                                                                : sd_state, 2, C_DIM);
            if (n_hidden) {
                char note[96];
                ksnprintf(note, sizeof note, "%d Pi cartridge%s (.bm) hidden by show_bm=0 in "
                          "bm/config.txt", n_hidden, n_hidden > 1 ? "s" : "");
                text_wrap(LIST_Y + 48, note, 3, C_TEXT);
            }
        }
        for (int i = *t; i < n && i < *t + ROWS; i++) {
            int y = LIST_Y + (i - *t) * ROW_H;
            const char *name = tab == TAB_GAMES ? games[i].name : tab_item(tab, i)->name;
            if (i == *s)
                gfx_rect(fb, 0, y - 8, W, ROW_H, rgb(C_SEL));
            gfx_rect(fb, 8, y - 4, 4, 24, rgb(tab == TAB_GAMES ? C_OK : C_ACCENT));
            text(24, y, name, C_TEXT, i == *s ? C_SEL : C_BG);
            if (i == *s)
                help = tab == TAB_GAMES ? (games[i].is_bm ? "Pi cartridge: Start+Select leaves" : ".s16 game")
                                        : tab_item(tab, i)->help;
        }
        text_wrap(HELP_Y, help, 1, C_DIM);
        char hint[48];
        ksnprintf(hint, sizeof hint, "L1/R1: tab   Up/Down: choose   %s: open", pad_ok_name());
        footer(hint);
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
        if (p & (PAD_L1 | PAD_LEFT)) tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
        if (p & (PAD_R1 | PAD_RIGHT)) tab = (tab + 1) % TAB_COUNT;
        if (p & PAD_UP) (*s)--;
        if (p & PAD_DOWN) (*s)++;
        if ((p & pad_ok) && n > 0 && !(p & (PAD_L1 | PAD_R1 | PAD_LEFT | PAD_RIGHT))) {
            if (tab == TAB_GAMES)
                page_game(*s);
            else
                tab_item(tab, *s)->run();
        }
    }
}
