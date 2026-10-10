/*
 * The RGB30's menu: the Pi's (src/kernel/menu_ui.c, the same drawing and
 * bar) at 360x360, shown twice as big (the whole 720x720 panel, every pixel
 * a 2x2 square), two covers a row. Tabs as on the Pi: Market (the catalog's
 * .b16 games, src/kernel/market.c in fibers as on the Pi; they go to bm/),
 * Games (the .b16 files in bm/ on the SD card, the handhelds' cartridges:
 * the .bm container, docs/B16.md §0, they play; and for testing the Pi's
 * .bm cartridges, which run; show_bm=0 hides them), Dev
 * (the 3D Bench, the render bench, the display modes, the input test, the
 * boot log, Lua) and Settings, whose panel opens when it is the tab
 * (Bluetooth, WiFi, updates from GitHub, the console's state, reboot, power
 * off). The controller drives it: L1 / R1 the tabs, the D-pad the covers,
 * B confirms and A goes back (confirm=a swaps them). The pages behind it
 * still draw on the text console.
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
#include "net/wifi_auto.h"
#include "net/netxfer.h"
#include "bm/bm.h"
#include "bm/runtime.h"
#include "kernel/home.h"
#include "kernel/menu_ui.h"
#include "kernel/notice.h"
#include "bm/loading.h"
#include "kernel/ledstate.h"
#include "kernel/reports.h"
#include "kernel/syskeys.h"
#include "kernel/carts.h"
#include "audio/audio.h"
#include "kernel/market.h"
#include "net/catalog.h"
#include "b3d_rgb30.h"
#include "battery.h"
#include "gputest_rgb30.h"

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
#define C_OK        0x4ccf6a
#define C_WARN      0xffb13a
#define C_BAD       0xff5a5a

#define MAX_GAMES   64
#define CATALOG_ITEMS (CATALOG_MAX > MAX_GAMES ? CATALOG_MAX : MAX_GAMES)

static framebuffer_t *fb;

static uint32_t rgb(uint32_t c)
{
    return fb_color(fb, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

/* the battery (battery.c): the icon of the bar, the LED (low: under 3.45 V
 * off the charger); the bolt as soon as the cable goes in */

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

/* The pages (360x360, 45 x 22 characters): everything on the font's grid
 * (text at multiples of 8 x 16), which is also how the tests read it back.
 * The page's title at the top, its lines, a line of help, the hints at the
 * bottom. */
#define HEAD_H      40
#define LIST_Y      48              /* the text of the first line */
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

static struct {
    char name[56], title[48], author[33];
    uint32_t size;
    int is_bm;
    g16_sheet_t cover;              /* 128x80: the .bm's COVER, or its title on a label */
} games[MAX_GAMES];
static int n_games, n_hidden;      /* n_hidden: .bm hidden by show_bm=0 */
static const char *sd_state = "not read";

/* a .bm's title and cover (its COVER section) as on the Pi's menu; a
 * label with the file's name for the rest */
static void load_cover(int i)
{
    char path[80];
    ksnprintf(path, sizeof path, "/bm/%s", games[i].name);
    ksnprintf(games[i].title, sizeof games[i].title, "%s", games[i].name);
    games[i].author[0] = 0;
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    /* only the first bytes: the header, then COVER, the first section */
    uint32_t need = 48 * 1024;
    const uint8_t *rgba = NULL;
    int w = 0, h = 0, r = -1;
    for (int tries = 0; tries < 3 && fat_find(path, &e) == 0; tries++) {
        free(data);
        data = NULL;
        if (fat_load_part(&e, need, &data, &len) != 0)
            break;
        r = bm_cover_peek(data, len, &need, &rgba, &w, &h);
        if (r || len >= e.size)
            break;
    }
    if (data && len >= BM_HEADER_SIZE && bm_is_cart(data) && data[24]) {
        char t[49];
        memcpy(t, data + 24, 48);
        t[48] = 0;
        ksnprintf(games[i].title, sizeof games[i].title, "%s", t);
        memcpy(t, data + 72, 32);
        t[32] = 0;
        ksnprintf(games[i].author, sizeof games[i].author, "%s", t);
    }
    if (r == 1)
        menu_load_cover(&games[i].cover, rgba, w, h);
    free(data);
    if (!games[i].cover.px)
        menu_make_cover(&games[i].cover, games[i].title, games[i].is_bm ? "bm" : "b16");
}

static void scan_games(void)
{
    for (int i = 0; i < n_games; i++)
        g16_sheet_free(&games[i].cover);
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
        int is_b16 = strcasecmp(dot, ".b16") == 0;
        int is_bm = strcasecmp(dot, ".bm") == 0;
        if (is_bm && !show_bm)
            n_hidden++;
        if (!is_b16 && !(is_bm && show_bm))
            continue;               /* .bm: for the Pi, hidden here */
        ksnprintf(games[n_games].name, sizeof games[0].name, "%s", e.name);
        games[n_games].size = e.size;
        games[n_games].is_bm = is_bm;
        n_games++;
    }
    for (int i = 0; i < n_games; i++)
        load_cover(i);
}

/* what the Market asks of the games on the card (carts.h, as carts.c on the
 * Pi): its paths are /bm/NAME */
static int same_ci(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

const char *carts_find_title(const char *title, const char *author)
{
    static char path[80];
    for (int i = 0; i < n_games; i++)
        if (same_ci(games[i].title, title) && same_ci(games[i].author, author)) {
            ksnprintf(path, sizeof path, "/bm/%s", games[i].name);
            return path;
        }
    return NULL;
}

static int game_of_path(const char *path)
{
    if (strncasecmp(path, "/bm/", 4) != 0)
        return -1;
    for (int i = 0; i < n_games; i++)
        if (same_ci(games[i].name, path + 4))
            return i;
    return -1;
}

int carts_has_path(const char *path)
{
    return path[0] && game_of_path(path) >= 0;
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

/* the reads and the CRC of a big game: the loading screen goes on */
static int load_tick(void)
{
    loading_tick();
    return 0;
}

static void play_bm(int i)
{
    char path[80];
    ksnprintf(path, sizeof path, "/bm/%s", games[i].name);
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    wifi_auto_stop();                              /* a WiFi try of the menu's: not under a game */
    loading_begin(fb);                             /* the retro intro while it loads */
    fat_load_tick = load_tick;
    bm_parse_tick = load_tick;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        loading_stop();
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

/* a .b16 is the .bm container (docs/B16.md §0, 2026-10-05): it plays the
 * same way; its profile's limits are not applied yet */
static void page_game(int i)
{
    play_bm(i);
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

/* every button and both sticks, live; Start + Select together to leave
 * (Dev > Input test, Settings > Controllers > Test the buttons) */
void ui_input_test(void)
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

/* the console with everything printed since boot (Dev > Boot log,
 * Settings > System > Log since boot) */
void ui_show_log(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    wait_back();
    console_suspend(1);
}

/* the Lua prompt on the serial port: ` typed there (not in the Dev tab: the Pi's only) */
static void serial_lua(void)
{
    frame_begin("Lua");
    int y = LIST_Y + 16 * text_wrap(LIST_Y, "Lua prompt on the serial port (1500000 8N1).", 2, C_TEXT);
    text_wrap(y, "Type exit() or Ctrl-D there to come back.", 2, C_DIM);
    frame_end();
    ui_serial_repl();
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

/* Dev > Display, Settings > Screen and sound > Screen modes */
void ui_screen_modes(void)
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

/* Dev > GPU test (M41): the Mali step by step */
static void page_gputest(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    rgb30_gpu_test(fb, pad_back_name());
    wait_back();
    console_suspend(1);
}

static void page_render(void)
{
    fb_show(fb, 0);
    console_suspend(0);
    reports_begin("render");
    kprintf("\n\x1b[1mRender bench\x1b[0m: map, 256 sprites and text at 640x360 RGB565\n");
    bm_bench_report(fb, 120);               /* Start+Select stops it: no report (syskeys.h) */
    if (syskeys_test_stopped()) {
        reports_drop();
        while (pad_state())                 /* straight back to the menu, the buttons let go */
            timer_delay_ms(10);
        pad_pressed();
    } else {
        reports_end();
        kprintf("\n\x1b[96m%s\x1b[0m back\n", pad_back_name());
        wait_back();
    }
    console_suspend(1);
}

/* --- the menu --- */

typedef struct {
    const char *name, *help;
    void (*run)(void);
    int icon;                       /* the cover, as the Pi's tools (MENU_ICON_*, colour) */
    uint32_t rgb;
} item_t;

/* the Dev tab, with the covers of the same tools on the Pi (home.c) */
static const item_t dev_items[] = {
    { "3D Bench", "every 3D test: bars, report in bm/bench", page_bench3d, MENU_ICON_GAUGE, 0x2A6A8A },
    { "Render bench", "map, sprites and text, 640x360", page_render, MENU_ICON_TRIANGLES, 0x5A3AA0 },
    { "Display", "the screen modes for games and the GPU", ui_screen_modes, MENU_ICON_BARS, 0x404050 },
    { "Input test", "every button and both sticks, live", ui_input_test, MENU_ICON_PAD, 0x8A3A8A },
    { "Boot log", "everything printed since boot", ui_show_log, MENU_ICON_LOG, 0x6A6A7A },
    { "GPU test", "the Mali-G52 step by step: power, MMU, jobs", page_gputest, MENU_ICON_CHIP, 0x8A5A2A },
};
#define N_DEV ((int)(sizeof dev_items / sizeof dev_items[0]))
static g16_sheet_t dev_covers[N_DEV];

/* Market first, off the screen at the left until it is the tab (as on the
 * Pi); Settings the last, its panel */
enum { TAB_MARKET, TAB_GAMES, TAB_DEV, TAB_SETTINGS };
static const char *const tab_names[] = { "Market", "Games", "Dev" };

/* the questions over the menu: a row of Settings, a download of the
 * Market, a row of a Market game's details, a game a nearby console sends */
enum { ASK_NONE, ASK_ROW, ASK_MARKET, ASK_MOPT, ASK_OFFER };

/* the menu's screen again after a page; if it cannot be had, the console
 * says why and a button tries again */
static void menu_reopen(void)
{
    while (menu_ui_open(fb) != 0) {
        kprintf("menu: the screen cannot be opened; %s tries again\n", pad_ok_name());
        while (!(pad_pressed() & pad_ok))
            timer_delay_ms(10);
    }
}

/* a page or a game: the console's screen, then the menu again */
static void run_page(void (*run)(void))
{
    market_set_active(0);                           /* its work stops first, as on the Pi */
    netxfer_write_pause();                          /* a file from the PC: written later */
    menu_ui_close_quiet(fb);                        /* (not the console flashing: the pages draw themselves) */
    console_suspend(1);
    run();
    while (pad_state())                             /* the button that left, released */
        timer_delay_ms(10);
    pad_pressed();
    menu_reopen();
}

static int play_index;
static void play_selected(void) { page_game(play_index); }

/* a game of the Market, installed in bm/: plays from the card */
static void play_market(int i, char *note, size_t n)
{
    const int g = game_of_path(market_path(i));
    if (g < 0) {
        ksnprintf(note, n, "%s is not on the SD card", market_path(i));
        return;
    }
    if (netxfer_updating(market_path(i))) {
        ksnprintf(note, n, "Updating, wait for the end of the download");
        return;
    }
    play_index = g;
    run_page(play_selected);
}

/* a row of Settings that runs on the console (settings.c: home_do_t) */
static void (*text_fn)(framebuffer_t *);
static int text_wait, text_own;

static void text_page(void)
{
    if (!text_own) {
        fb_show(fb, 0);
        console_suspend(0);
    }
    text_fn(fb);
    if (text_wait) {
        kprintf("\n\x1b[96m%s\x1b[0m or \x1b[96m%s\x1b[0m: back to the menu\n", pad_ok_name(), pad_back_name());
        while (pad_state())                         /* the button that started it, released */
            timer_delay_ms(10);
        pad_pressed();
        wait_back();
    }
    console_suspend(1);
}

#define DEPTH_MAX 4

/* the menu's free time in a frame: a file from the PC (bm_net.py --send)
 * written first, alone on the card (2026-10-10); then the Market's work */
static int idle_market;

static void menu_idle(uint32_t until)
{
    if (netxfer_write_tick(until))
        return;
    if (idle_market) {
        market_tick(until);
        return;
    }
    /* elsewhere the saved WiFi network is joined again while the link is down */
    wifi_auto_idle(until);
}

void ui_home(framebuffer_t *f)
{
    fb = f;
    console_suspend(1);
    scan_games();
    for (int i = 0; i < N_DEV; i++)
        menu_make_tool_cover(&dev_covers[i], dev_items[i].name, dev_items[i].icon, dev_items[i].rgb);
    kprintf("cartridge menu: %d games", n_games);
    if (n_hidden)
        kprintf(" (%d .bm hidden by show_bm=0 in bm/config.txt)", n_hidden);
    kprintf("\n");
    menu_reopen();

    int tab = TAB_GAMES, sel[3] = { 0, 0, 0 };
    static menu_item_t items[CATALOG_ITEMS];
    /* a Market game's details (X): its panel over the covers */
    static home_panel_t mpb;
    int mopt = -1, mopt_sel = 0, ask_game = 0;
    /* Settings: its panels, as on the Pi (settings.c), one over the other */
    static home_panel_t pb;
    struct { int id, sel, top; } stack[DEPTH_MAX];
    int depth = 0, built = -1, frame = 0, ask_row = 0, asking = ASK_NONE;
    char ask_q[64] = "", ask_d[64] = "", ask_y[16] = "";
    char details[64] = "", note[96] = "";
    for (;;) {
        /* the view: the tab's covers, or the Settings panels over the last tab's */
        int on_gear = tab == TAB_SETTINGS, shown = on_gear ? TAB_DEV : tab;
        if (on_gear && !depth) {
            stack[0].id = HOME_SETTINGS;
            stack[0].sel = stack[0].top = 0;
            depth = 1;
            built = -1;
        } else if (!on_gear && depth) {
            depth = 0;
            asking = ASK_NONE;
        }
        /* the Market works only while its tab is shown; the nearby consoles
         * hear this one only then (M24) */
        const int on_market = tab == TAB_MARKET && !on_gear;
        market_set_active(on_market);
        market_lan(on_market);
        if (!on_market)
            mopt = -1;
        if (asking == ASK_NONE && market_offer(ask_q, sizeof ask_q, ask_d, sizeof ask_d)) {
            ksnprintf(ask_y, sizeof ask_y, "Accept");
            asking = ASK_OFFER;
        } else if (asking == ASK_OFFER) {
            char q_[64], d_[64];
            if (!market_offer(q_, sizeof q_, d_, sizeof d_))
                asking = ASK_NONE;          /* the sender gave up */
        }
        /* a game of the Market installed or deleted: the card again */
        if (market_take_changed()) {
            scan_games();
            market_carts_changed();
        }
        /* a game written from the network: its title, cover and size again */
        static unsigned seen_saves;
        if (netxfer_saves() != seen_saves) {
            seen_saves = netxfer_saves();
            scan_games();
            market_carts_changed();
        }
        /* the tests' reports waiting on the SD card go once the console is
         * on the network (reports_auto_due; no fibers here: a moment's wait,
         * from the covers only, not in Settings) */
        if (!on_gear && !asking && !on_market && !netxfer_write_pending() && reports_auto_due()) {
            reports_send_pending();
            ksnprintf(note, sizeof note, "report %s", reports_last());
        }
        int n = shown == TAB_MARKET ? market_items(items, CATALOG_ITEMS) : shown == TAB_GAMES ? n_games : N_DEV;
        int *s = &sel[shown];
        if (*s >= n) *s = n - 1;
        if (*s < 0) *s = 0;
        if (shown == TAB_MARKET)
            market_select(*s);
        for (int i = 0; i < n && shown != TAB_MARKET; i++) {
            if (shown == TAB_GAMES) {
                char gp[80];
                ksnprintf(gp, sizeof gp, "/bm/%s", games[i].name);
                const int up = netxfer_updating(gp);        /* a file from the PC on its way */
                items[i] = (menu_item_t){ .title = games[i].title, .kind = games[i].is_bm ? "bm" : "b16",
                                          .size = games[i].size, .cover = &games[i].cover,
                                          .badge = !up ? NULL : up == NETXFER_QUEUED ? "Queued" : "Updating" };
            } else {
                items[i] = (menu_item_t){ .title = dev_items[i].name, .kind = "tool", .cover = &dev_covers[i] };
            }
        }
        idle_market = on_market;
        menu_view_t v = {
            .tabs = tab_names, .ntabs = 3, .tab = shown, .on_gear = on_gear, .peek_first = 1,
            .items = items, .n = n, .sel = *s,
            .prompts = MENU_PROMPTS_RGB30, .confirm_b = pad_ok == PAD_B, .no_monitor = 1,
            .idle = menu_idle,
        };
        /* the bar: the WiFi and the battery only; no icons of the
         * controllers, mice and keyboards (the user, 2026-10-05) */
        v.net = wifi_linked() ? MENU_NET_WIFI : MENU_NET_NONE;
        v.net_wait = !net_ip();
        details[0] = 0;
        if (shown == TAB_MARKET) {
            market_details(*s, details, sizeof details);
            v.banner = market_banner();
            v.a_label = market_action_label(*s);
            if (!v.a_label)
                v.a_label = "";
        } else if (shown == TAB_GAMES && n == 0) {
            v.banner = strcmp(sd_state, "bm/") == 0 ? "No games yet: put .b16 games in bm/" : "No games yet";
            v.a_label = "";
            if (n_hidden)
                ksnprintf(details, sizeof details, "%d Pi cartridge%s (.bm) hidden by show_bm=0", n_hidden,
                          n_hidden > 1 ? "s" : "");
            else if (strcmp(sd_state, "bm/") != 0)
                ksnprintf(details, sizeof details, "%s", sd_state);
        } else if (shown == TAB_GAMES) {
            v.a_label = "Play";
            ksnprintf(details, sizeof details, "bm/%s: Start+Select leaves", games[*s].name);
        } else {
            ksnprintf(details, sizeof details, "%s", dev_items[*s].help);
        }
        v.details = details[0] ? details : NULL;
        v.note = on_market ? market_status() : note[0] ? note : NULL;
        menu_panel_t panel, sections, mpanel;
        if (mopt >= 0) {
            market_panel(mopt, &mpb);
            if (mopt_sel >= mpb.n) mopt_sel = mpb.n ? mpb.n - 1 : 0;
            mpanel = (menu_panel_t){ mpb.title, mpb.rows, mpb.n, mopt_sel,
                                     mopt_sel >= MENU_PANEL_ROWS ? mopt_sel - MENU_PANEL_ROWS + 1 : 0,
                                     mpb.n ? mpb.help[mopt_sel] : NULL };
            v.panel = &mpanel;
        }
        menu_page_t page;
        frame++;
        if (depth) {
            /* rebuilt when it changes, and twice a second for the values
             * that move (pads, uptime, battery) */
            int id = stack[depth - 1].id;
            if (built != id || frame % 30 == 0) {
                home_panel(id, &pb);
                built = id;
            }
            int *ps = &stack[depth - 1].sel, *pt = &stack[depth - 1].top;
            if (*ps >= pb.n) *ps = pb.n ? pb.n - 1 : 0;
            if (*ps < *pt) *pt = *ps;
            if (*ps >= *pt + MENU_PANEL_ROWS) *pt = *ps - MENU_PANEL_ROWS + 1;
            panel = (menu_panel_t){ pb.title, pb.rows, pb.n, *ps, *pt, pb.n ? pb.help[*ps] : NULL };
            /* Settings is a page (the narrow screen: one list at a time) */
            sections = panel;
            page = (menu_page_t){ &sections, depth > 1 ? &panel : NULL, depth > 1 };
            v.page = &page;
        }
        if (asking != ASK_NONE) {
            v.ask = ask_q;
            v.ask_detail = ask_d[0] ? ask_d : NULL;
            v.ask_yes = ask_y;
        }
        static char nt[NOTICE_LEN], nd[NOTICE_LEN];
        if (notice_now(nt, nd, &v.notice_progress)) {
            v.notice = nt;                      /* a kernel arriving, the restart */
            v.notice_detail = nd;
        }
        audio_idle();                           /* the output's news (QEMU's sink: what it heard) */
        v.battery = battery_state(&v.battery_pct, &v.charging, NULL);
        menu_ui_frame(fb, &v);
        /* a file from the PC goes on even when the frame left no time for
         * it (menu_idle): a piece a frame */
        if (netxfer_write_pending())
            netxfer_write_tick(timer_ticks() + 2000);

        uint32_t p = pad_pressed();
        if (pad_serial_char() == '`') {
            run_page(serial_lua);
            continue;
        }
        home_do_t d;
        d.what = -1;
        if (asking) {
            /* a question: confirm says yes, back cancels */
            const int kind = asking;
            if (p & (pad_ok | pad_back))
                asking = ASK_NONE;
            if ((p & pad_ok) && kind == ASK_ROW) {
                home_act(stack[depth - 1].id, ask_row, HOME_YES, &d);
            } else if ((p & pad_ok) && kind == ASK_MARKET) {
                market_get(ask_game);
            } else if ((p & pad_ok) && kind == ASK_MOPT) {
                home_do_t md;
                market_act(mopt, ask_row, HOME_YES, &md);
                if (md.note[0])
                    ksnprintf(note, sizeof note, "%s", md.note);
                if (md.what == HOME_BACK)
                    mopt = -1;
            } else if (kind == ASK_OFFER && (p & (pad_ok | pad_back))) {
                market_offer_answer((p & pad_ok) != 0);
            }
        } else if ((p & PAD_L1) && tab > TAB_MARKET) {
            /* L1 / R1: Market, Games, Dev, Settings, no going round (as on the Pi) */
            tab--;
            mopt = -1;
        } else if ((p & PAD_R1) && tab < TAB_SETTINGS) {
            tab++;
            mopt = -1;
        } else if (mopt >= 0) {
            /* a Market game's details: up/down choose, confirm does, back closes */
            int dy = (p & PAD_UP) ? -1 : (p & PAD_DOWN) ? 1 : 0;
            if (dy && mpb.n)
                mopt_sel = (mopt_sel + dy + mpb.n) % mpb.n;
            if (p & pad_back) {
                mopt = -1;
            } else if ((p & pad_ok) && mpb.n && mpb.rows[mopt_sel].kind != MENU_ROW_INFO) {
                const int row = mpb.ids[mopt_sel], game = mopt;
                if (row == M_PLAY) {
                    mopt = -1;
                    play_market(game, note, sizeof note);
                } else {
                    home_do_t md;
                    market_act(game, row, 0, &md);
                    if (md.note[0])
                        ksnprintf(note, sizeof note, "%s", md.note);
                    if (md.what == HOME_ASK) {
                        asking = ASK_MOPT;
                        ask_row = row;
                        ksnprintf(ask_q, sizeof ask_q, "%s", md.ask);
                        ksnprintf(ask_d, sizeof ask_d, "%s", md.ask_detail);
                        ksnprintf(ask_y, sizeof ask_y, "%s", md.ask_yes);
                    } else if (md.what == HOME_BACK) {
                        mopt = -1;
                    }
                }
            }
        } else if (depth) {
            /* a panel: up/down choose, confirm does, left/right change a
             * value, back goes back one level (out of Settings: to Dev) */
            int *ps = &stack[depth - 1].sel, id = stack[depth - 1].id;
            int dy = (p & PAD_UP) ? -1 : (p & PAD_DOWN) ? 1 : 0;
            int dx = (p & PAD_LEFT) ? -1 : (p & PAD_RIGHT) ? 1 : 0;
            if (dy && pb.n)
                *ps = (*ps + dy + pb.n) % pb.n;
            const menu_row_t *r = pb.n ? &pb.rows[*ps] : NULL;
            int row = pb.n ? pb.ids[*ps] : 0;
            /* right opens a section, left goes back to the sections (but
             * changes a value on a choice), as on the Pi's page */
            if (depth == 1 && dx > 0 && r && r->kind == MENU_ROW_SUB)
                p |= pad_ok;
            else if (depth > 1 && dx < 0 && (!r || r->kind != MENU_ROW_CHOICE))
                p |= pad_back;
            if (p & pad_back) {
                if (--depth == 0)
                    tab = TAB_DEV;
                built = -1;
            } else if (r && (((p & pad_ok) && r->kind != MENU_ROW_INFO) || (dx && r->kind == MENU_ROW_CHOICE))) {
                home_act(id, row, (p & pad_ok) ? 0 : dx, &d);
                ask_row = row;
            }
        } else if (n > 0) {
            /* the covers: left / right along the row, up / down a row (the
             * last row may be shorter) */
            int cols = menu_ui_cols(), to = *s;
            if (p & PAD_LEFT) to = *s - 1;
            if (p & PAD_RIGHT) to = *s + 1;
            if (p & PAD_UP) to = *s - cols;
            if (p & PAD_DOWN)
                to = *s + cols < n ? *s + cols : (*s / cols + 1) * cols < n ? n - 1 : *s;
            if (to >= 0 && to < n)
                *s = to;
            if (shown == TAB_MARKET) {
                /* X: a game's details; confirm: get it (after a question) or play it */
                const int a = market_action(*s);
                if ((p & PAD_X) && a != MARKET_NONE) {
                    mopt = *s;
                    mopt_sel = 0;
                } else if ((p & pad_ok) && (a == MARKET_GET || a == MARKET_UPDATE)) {
                    home_do_t q;
                    market_ask(*s, &q);
                    ksnprintf(ask_q, sizeof ask_q, "%s", q.ask);
                    ksnprintf(ask_d, sizeof ask_d, "%s", q.ask_detail);
                    ksnprintf(ask_y, sizeof ask_y, "%s", q.ask_yes);
                    asking = ASK_MARKET;
                    ask_game = *s;
                } else if ((p & pad_ok) && a == MARKET_PLAY) {
                    play_market(*s, note, sizeof note);
                }
            } else if (p & pad_ok) {
                char gp[80] = "";
                if (shown == TAB_GAMES)
                    ksnprintf(gp, sizeof gp, "/bm/%s", games[*s].name);
                if (shown == TAB_GAMES && netxfer_updating(gp)) {
                    ksnprintf(note, sizeof note, "Updating, wait for the end of the download");
                } else if (shown == TAB_GAMES) {
                    play_index = *s;
                    run_page(play_selected);
                } else {
                    run_page(dev_items[*s].run);
                }
            }
        }

        /* what a row of Settings asked for */
        if (d.what >= 0) {
            if (d.note[0])
                ksnprintf(note, sizeof note, "%s", d.note);
            built = -1;
            switch (d.what) {
            case HOME_OPEN:
                if (depth < DEPTH_MAX) {
                    stack[depth].id = d.panel;
                    stack[depth].sel = stack[depth].top = 0;
                    depth++;
                }
                break;
            case HOME_BACK:
                if (depth > 1)
                    depth--;
                break;
            case HOME_ASK:
                asking = ASK_ROW;
                ksnprintf(ask_q, sizeof ask_q, "%s", d.ask);
                ksnprintf(ask_d, sizeof ask_d, "%s", d.ask_detail);
                ksnprintf(ask_y, sizeof ask_y, "%s", d.ask_yes);
                break;
            case HOME_TEXT:
                text_fn = d.text;
                text_wait = d.wait;
                text_own = d.own;
                run_page(text_page);
                break;
            }
        }
    }
}
