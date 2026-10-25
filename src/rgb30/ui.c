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
#include "kernel/fiber.h"
#include "kernel/sdmap.h"
#include "kernel/tick.h"
#include "kernel/update.h"
#include "kernel/version.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "bt/bt.h"
#include "usb/hid.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "net/wifi_auto.h"
#include "net/netxfer.h"
#include "bm/bm.h"
#include "bm/project.h"
#include "bm/runtime.h"
#include "kernel/home.h"
#include "kernel/menu_ui.h"
#include "kernel/notice.h"
#include "bm/loading.h"
#include "kernel/ledstate.h"
#include "kernel/reports.h"
#include "kernel/syskeys.h"
#include "kernel/carts.h"
#include "kernel/toolnames.h"
#include "kernel/devtool.h"
#include "audio/audio.h"
#include "kernel/market.h"
#include "kernel/library.h"
#include "net/catalog.h"
#include "b3d_rgb30.h"
#include "battery.h"

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

#define CATALOG_ITEMS (CATALOG_MAX > LIB_MAX ? CATALOG_MAX : LIB_MAX)

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

static lib_game_t *const games = library.g;     /* the library (kernel/library.h) */
/* games[] holds the cartridges of the card (library.n); the tabs list them by lib_tab_items: the games on the Games
 * tab, the apps and utilities in Dev after its fixed items. library.hidden: .bm hidden by show_bm=0 */
static int game_idx[LIB_MAX], n_games;      /* the Games tab: the library indexes of the games */
static int dev_idx[LIB_MAX], n_dev;         /* the Dev tab after its fixed items: apps and utilities */
static const char *sd_state = "not read";

/* --- the covers, in the background (D10, as the Pi's carts.c: a fiber job in the menu's free time, the covers on
 * screen first, stopped before a game or a page starts and before the list changes) --- */

enum { TAB_MARKET, TAB_GAMES, TAB_DEV, TAB_SETTINGS };

/* What the RGB30 lists: .b16 always, .bm unless show_bm=0 (they are for the Pi, listed for testing), no tool
 * cartridge (a tool installed from Market > Apps, the SDK: Dev, not Games), no project. */
static int rgb_want(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    const char *show = config_get("show_bm");
    const int show_bm = !show || strcmp(show, "0") != 0;     /* for testing: shown unless show_bm=0 */
    const int is_b16 = strcasecmp(dot, ".b16") == 0;
    const int is_bm = strcasecmp(dot, ".bm") == 0;
    if (bm_tool_id_of_file(name))
        return 0;                   /* a tool installed from Market > Apps (the SDK): Dev, not Games (as on the Pi) */
    if (is_bm && !show_bm)
        return -1;                  /* .bm: for the Pi, hidden here */
    return is_b16 || is_bm;
}

/* Before the list changes (as the Pi's background_stop): the work that holds a cartridge of it stops. */
static void rgb_stop_background(void)
{
    netxfer_write_pause();                          /* a file from the PC: again later */
    lib_covers_stop();                     /* no cover read left half way */
    wifi_auto_stop();
    sdmap_stop();
}

/* The menu's jobs and their shares of its free time (fiber.h, 2026-10-11),
 * as on the Pi: an update's check or install, the Market's work (on its
 * tab only: market_set_active), the saved WiFi network joined again while
 * the link is down. */
static const fiber_share_t menu_jobs[] = {
    /*                      game tool menu */
    { update_tick,        {  0,   0,  25 } },
    { market_tick,        {  0,   0,  40 } },   /* its tab only: on another one it gives its time back at once */
    { lib_covers_tick,    {  0,   0,  15 } },   /* the tab's covers first (D10) */
    { wifi_auto_idle,     {  0,   0,  10 } },
    { sdmap_tick,         {  0,   0,  10 } },   /* the SD map scan (Settings > System > SD map), last */
};

static void rgb_play_begin(const lib_game_t *g, int resume);

static const char *const rgb_dirs[] = { "/bm", "/carts", NULL };
static const lib_backend_t rgb_backend = {
    .dirs = rgb_dirs, .target = BM_TARGET_RGB30, .want = rgb_want, .stop_background = rgb_stop_background,
    .panel = LIB_CAP_SDK | LIB_CAP_SEND, .play_begin = rgb_play_begin,
    .jobs = menu_jobs, .njobs = (int)(sizeof menu_jobs / sizeof *menu_jobs),       /* no tools but the SDK, no editable copy, no Publish (D11) */
};

static void scan_games(void)
{
    library.be = &rgb_backend;
    library.sd_ok = 1;
    fat_dir_t d;
    sd_state = fat_opendir(&d, "bm") == 0 ? "bm/" : "no bm/ folder on the SD card";
    lib_scan(&library);
    n_games = lib_tab_items(0, game_idx);           /* the games, then the apps and utilities: each keeps the order by title */
    n_dev = lib_tab_items(1, dev_idx);
}

/* what the Market asks of the games on the card (carts.h, as carts.c on the
 * Pi): its paths are /bm/NAME */
static int game_of_path(const char *path)
{
    return lib_find_path(&library, path);
}

/* --- pages --- */

static void wait_back(void)
{
    while (!(pad_pressed() & (pad_ok | pad_back)))
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

/* A game: the runtime of src/bm, its screen as big as the panel
 * (rgb30/display.h). Start + Select (or Esc, PS) does as on the Pi: the game
 * stays in memory, frozen, and the menu comes back; A on its cover resumes it
 * from the same frame, another game closes it (bm_close_suspended). */

/* the reads and the CRC of a big game: the loading screen goes on */
static int load_tick(void)
{
    loading_tick();
    return 0;
}

/* Just before a game starts or resumes: a WiFi try of the menu's stops (not under a game), the reads show the
 * loading screen, and a game gets the retro intro while it loads (an app or a utility opens with none). */
static void rgb_play_begin(const lib_game_t *g, int resume)
{
    wifi_auto_stop();
    fat_load_tick = load_tick;
    bm_parse_tick = load_tick;
    if (!resume && !lib_is_dev(g))
        loading_begin(fb);
}

static void play_bm(int i)
{
    bm_stats_t st;
    const int r = lib_play(fb, &games[i], &st);
    if (r != LIB_PLAY_OK) {
        const char *lines[] = { games[i].path, "", r == LIB_PLAY_UNREADABLE ? "cannot be read:" : "unknown cartridge format",
                                r == LIB_PLAY_UNREADABLE ? fat_error() : "" };
        page_message("Game", lines, 4);
        return;
    }
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

static uint8_t *net_play_buf;
static size_t net_play_len;

static void play_net(void)
{
    lib_suspend_close();                           /* another game: the frozen one's memory is freed */
    wifi_auto_stop();
    if (net_play_len < 8 || !bm_is_cart(net_play_buf)) {
        kprintf("unknown cartridge format\n");
        const char *lines[] = { "From the network", "", "unknown cartridge format", "" };
        page_message("Game", lines, 4);
        return;
    }
    loading_begin(fb);
    kprintf("play: from the network (%lu bytes)\n", (uint32_t)net_play_len);
    bm_stats_t st;
    bm_play(fb, net_play_buf, net_play_len, LIB_PLAY_SECS, &st);
    console_suspend(1);                             /* the menu draws itself */
    bm_print_stats(&st);
    while (pad_state())                             /* Start + Select still held */
        timer_delay_ms(10);
    pad_pressed();
    if (!st.ok) {
        const char *lines[] = { "From the network", "", "stopped with an error:", bm_last_error() };
        page_message("Game", lines, 4);
    }
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
 * (Settings > Controllers > Test the buttons) */
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

/* the console with everything printed since boot (Settings > System > Log since boot) */
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

/* Settings > Screen > Screen modes */
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

/* --- bm SDK (RGB30): the Dev tab's tool --- */

/* the cartridge built into this kernel (rgb30.mk, bm_embed.S: carts/sdk_b16) */
extern const uint8_t bm_sdk_cart[], bm_sdk_cart_end[];

/* the tools of this console for its tool session (src/kernel/devtool.c, the
 * Pi's loop): the SDK alone. The Pi's others (bm Code, Pixel, Studio...)
 * are not in this kernel: a cart_tool() to one would end the session, and
 * the SDK does not offer them. */
static const devtool_t rgb30_tools[] = {
    { "sdk", "bm SDK", bm_sdk_cart, bm_sdk_cart_end },
};

/* after the tool and after each game it tries: the buttons that left
 * (Start + Select) or started it let go, so the next one does not see them */
static void tool_released(void)
{
    while (pad_state())
        timer_delay_ms(10);
    pad_pressed();
}

static const devtools_t rgb30_devtools = { rgb30_tools, sizeof rgb30_tools / sizeof rgb30_tools[0], tool_released };

static const char *sdk_open;        /* the file it opens on (a game's options), or NULL */

/* The SDK as a tool, as on the Pi: it saves where it is told, builds the
 * game of a project (cart_build) and tries it (cart_run: the game plays,
 * Start + Select leaves it, the SDK comes back on the file with cart_arg()
 * = {path, error, back, run}). Then the games again: it may have built or
 * copied one in bm/. */
static void page_sdk(void)
{
    wifi_auto_stop();                               /* a WiFi try of the menu's: not under the tool */
    const char *open = sdk_open;
    sdk_open = NULL;
    kprintf("bm SDK%s%s\n", open ? " on " : "", open ? open : "");
    devtool_session(fb, &rgb30_devtools, bm_sdk_cart, (size_t)(bm_sdk_cart_end - bm_sdk_cart), "bm SDK", open);
    console_suspend(1);                             /* the menu draws itself */
    scan_games();
    market_carts_changed();
    /* the session ends with the tool's own run: its error, if it stopped
     * with one (a game's goes back to the SDK), as play_bm says a game's */
    if (bm_last_error()[0]) {
        const char *lines[] = { "bm SDK", "", "stopped with an error:", bm_last_error() };
        page_message("bm SDK", lines, 4);
    }
}

/* --- the menu --- */

typedef struct {
    const char *name;
    void (*run)(void);
    int icon;                       /* the cover, as the Pi's tools (MENU_ICON_*, colour) */
    uint32_t rgb;
    const uint8_t *cart, *cart_end; /* a tool built in: its cartridge's COVER instead */
} item_t;

/* The Dev tab, as on the Pi: the bm SDK first (the tests find it by its place), then the installed tools: the
 * cartridges of type app or utility on the card (dev_idx[]). The benchmarks and tests are not here: they are
 * Settings rows (the utilities below), as the Pi's DMA test and 3D Bench. */
static const item_t dev_items[] = {
    { "bm SDK", page_sdk, MENU_ICON_CODE, 0x1E8A5A,
      bm_sdk_cart, bm_sdk_cart_end },
};
#define N_DEV ((int)(sizeof dev_items / sizeof dev_items[0]))
static g16_sheet_t dev_covers[N_DEV];

/* The utilities of this console, listed as the Pi's (home.h): group 1 is in Settings > Screen, group 0 in
 * Settings > System > Utilities. Display, Input test and Boot log are not here: Settings has the rows
 * "Screen modes", "Test the buttons" and "Log since boot". */
typedef struct { const char *name, *help; void (*run)(framebuffer_t *); int group; } util_t;
static void util_bench3d(framebuffer_t *f) { (void)f; page_bench3d(); }
static void util_render(framebuffer_t *f) { (void)f; page_render(); }
static const util_t utils[] = {
    { "3D Bench", "every 3D test: bars, report in bm/bench", util_bench3d, 1 },
    { "Render bench", "map, sprites and text, 640x360", util_render, 0 },
};
#define N_UTILS ((int)(sizeof utils / sizeof utils[0]))

int home_utils(void) { return N_UTILS; }
const char *home_util_title(int i) { return utils[i].name; }
const char *home_util_about(int i) { return utils[i].help; }

/* ---- what differs between the consoles in Settings (home.h): the RGB30's ---- */

int home_wireless(void)
{
    return 1;
}

void home_restart(void)
{
    wifi_leave();                               /* the AP told: it does not hold the old association */
    plat_reset();
}

void home_poweroff(void)
{
    plat_poweroff();
}

const char *home_poweroff_help(void)
{
    return "Turns the console off";
}

const char *home_poweroff_detail(void)
{
    return "A game left suspended is closed.";
}

const char *home_test_help(void)
{
    return "Every button and both sticks, live (Start + Select: back)";
}

const char *home_connect_help(void)
{
    return "Lists the networks, joins wifi_ssid of bm/config.txt";
}

int home_has_battery(void)
{
    return 1;
}

void home_controls_changed(void)
{
    pad_config();
}
int home_util_group(int i) { return utils[i].group; }

void home_util_start(int i, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_TEXT;
    d->text = utils[i].run;
    d->own = 1;                     /* the page shows its own screen and waits for its own end */
}

/* Market first, off the screen at the left until it is the tab (as on the
 * Pi); Settings the last, its panel */

/* The tab a key of a keyboard goes to, -1 if none (as on the Pi, carts.c): Tab is the next tab and wraps
 * after the last; 1 2 3 4 are Market, Games, Dev and Settings */
static int tab_of_key(int k, int tab)
{
    switch (k) {
    case '\t': return (tab + 1) % (TAB_SETTINGS + 1);
    case '1': return TAB_MARKET;
    case '2': return TAB_GAMES;
    case '3': return TAB_DEV;
    case '4': return TAB_SETTINGS;
    default: return -1;
    }
}
static const char *const tab_names[] = { "Market", "Games", "Dev" };

/* the questions over the menu: a row of Settings, a download of the
 * Market, a row of a Market game's details, a game a nearby console sends */
enum { ASK_NONE, ASK_ROW, ASK_MARKET, ASK_MOPT, ASK_GOPT, ASK_OFFER, ASK_SWITCH, ASK_SEND, ASK_COMPAT };

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

/* a page or a game: the console's screen, then the menu again; not while
 * an update is checked or installed in the menu's background (the install
 * restarts the console), as on the Pi */
static void run_page(void (*run)(void))
{
    if (update_busy()) {
        notice_flash(update_busy(), "Wait for it to end", -1, 3000);
        return;
    }
    market_set_active(0);                           /* its work stops first, as on the Pi */
    lib_covers_stop();                     /* the covers: later (a game or a page owns the card now) */
    sdmap_stop();
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
static void play_market(int i)
{
    const char *mp = market_path(i);
    if (strcasecmp(mp, "/bm/SDK.B16") == 0 || strcasecmp(mp, "/bm/SDK_B16.B16") == 0 ||
        strcasecmp(mp, "/bm/SDK.BM") == 0) {
        run_page(page_sdk);
        return;
    }
    const int g = game_of_path(mp);
    if (g < 0) {
        char why[80];
        ksnprintf(why, sizeof why, "%s is not on the SD card", mp);
        notice_fail(why);
        return;
    }
    if (netxfer_updating(mp)) {
        notice_fail("Updating, wait for the end of the download");
        return;
    }
    play_index = g;
    run_page(play_selected);
}

/* --- a game's options (X on its cover; the Pi's C): its panel over the covers --- */

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

/* a cartridge of the card on the Games tab or in Dev */
static menu_item_t game_item(int g)
{
    char gp[80];
    ksnprintf(gp, sizeof gp, "%s/%s", games[g].dir, games[g].name);
    const int up = netxfer_updating(gp);                /* a file from the PC on its way */
    return (menu_item_t){ .title = games[g].title, .kind = lib_kind(&games[g]),
                          .cover = games[g].cover.px ? &games[g].cover : NULL, .loading = !games[g].cover_read, .path = games[g].name, .grey = games[g].grey,
                          .badge = !up ? NULL : up == NETXFER_QUEUED ? "Queued" : "Updating",
                          .running = lib_is_suspended(&games[g]) };
}

/* the cartridge under the cursor: a game on the Games tab, an app or a utility in Dev after the fixed items; -1 if
 * none (the fixed tools) */
static int cart_at(int shown, int sel)
{
    if (shown == TAB_GAMES)
        return sel < n_games ? game_idx[sel] : -1;
    if (shown == TAB_DEV && sel >= N_DEV)
        return sel - N_DEV < n_dev ? dev_idx[sel - N_DEV] : -1;
    return -1;
}

/* The state of the RGB30 menu (ui_home): what stays from a frame to the next, and what one frame fills in. */
typedef struct {
    int tab, sel[3];                    /* the tab, and the selection of each */
    int mopt, mopt_sel, ask_game;       /* a Market game's details (X): its panel over the covers */
    int gopt, gopt_sel;                 /* a game's options (X on the Games tab) */
    int gsend;                          /* 1: the options show the consoles to send the game to */
    struct { int id, sel, top; } stack[DEPTH_MAX];      /* the panels of Settings, one over the other */
    int depth, built, frame, ask_row, asking, ask_switch, ask_compat;
    char ask_q[64], ask_d[64], ask_y[16];
    /* one frame */
    int on_gear, shown, on_market;      /* Settings is the tab; the list shown; the Market is the tab */
    int n, *s;                          /* what the list has, and its selection */
    uint32_t p;                         /* the buttons pressed */
    int tabto;
    home_do_t d;                        /* what a row of Settings asked for */
} uhome_t;

static menu_item_t items[CATALOG_ITEMS];
static home_panel_t mpb;                /* a Market game's details */
static home_panel_t gpb;                /* a game's options */
static home_panel_t pb;                 /* Settings */

/* The menu opens: the cartridges, the covers of the tools, the screen. */
static void uh_open(uhome_t *m, framebuffer_t *f)
{
    fb = f;
    console_suspend(1);
    scan_games();
    for (int i = 0; i < N_DEV; i++) {
        const uint8_t *rgba = NULL;
        uint32_t need = 0;
        int w = 0, h = 0;
        if (dev_items[i].cart &&
            bm_cover_peek(dev_items[i].cart, (size_t)(dev_items[i].cart_end - dev_items[i].cart), &need, &rgba,
                          &w, &h) == 1)
            menu_load_cover(&dev_covers[i], rgba, w, h);         /* the SDK's icon, as on the Pi */
        if (!dev_covers[i].px)
            menu_make_tool_cover(&dev_covers[i], dev_items[i].name, dev_items[i].icon, dev_items[i].rgb);
    }
    kprintf("cartridge menu: %d games", n_games);
    if (library.hidden)
        kprintf(" (%d .bm hidden by show_bm=0 in bm/config.txt)", library.hidden);
    kprintf("\n");
    menu_reopen();

    memset(m, 0, sizeof *m);
    m->tab = TAB_GAMES;
    m->mopt = -1;
    m->gopt = -1;
    m->built = -1;
    m->asking = ASK_NONE;
    m->ask_switch = -1;
    m->ask_compat = -1;
}

/* The view: the tab's covers, or the Settings panels over the last tab's. */
static void uh_view_state(uhome_t *m)
{
    /* the view: the tab's covers, or the Settings panels over the last tab's */
    m->on_gear = m->tab == TAB_SETTINGS;
    m->shown = m->on_gear ? TAB_DEV : m->tab;
    if (m->on_gear && !m->depth) {
        m->stack[0].id = HOME_SETTINGS;
        m->stack[0].sel = m->stack[0].top = 0;
        m->depth = 1;
        m->built = -1;
    } else if (!m->on_gear && m->depth) {
        m->depth = 0;
        m->asking = ASK_NONE;
    }
}

/* The work behind the menu: the Market on its tab only, a game a nearby console offers, the card read again
 * when a game came or went, a cartridge sent to play, the tests' reports. */
static void uh_background(uhome_t *m)
{
    /* the Market works only while its tab is shown; the nearby consoles
     * hear this one only then (M24) */
    m->on_market = m->tab == TAB_MARKET && !m->on_gear;
    market_set_active(m->on_market);
    market_lan(m->on_market || (m->gopt >= 0 && m->gsend));
    if (!m->on_market)
        m->mopt = -1;
    if (m->tab != TAB_GAMES || m->on_gear)
        m->gopt = -1;
    if (m->asking == ASK_NONE && market_offer(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d)) {
        ksnprintf(m->ask_y, sizeof m->ask_y, "Accept");
        m->asking = ASK_OFFER;
    } else if (m->asking == ASK_OFFER) {
        char q_[64], d_[64];
        if (!market_offer(q_, sizeof q_, d_, sizeof d_))
            m->asking = ASK_NONE;          /* the sender gave up */
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
    /* a cartridge sent with bm_net.py --play: it plays at once, not saved (D21, as GO_NETPLAY on the Pi) */
    {
        uint8_t *nb;
        size_t nl;
        if (netxfer_take_play(&nb, &nl)) {
            net_play_buf = nb;
            net_play_len = nl;
            run_page(play_net);
            free(nb);
            net_play_buf = NULL;
            net_play_len = 0;
            m->asking = ASK_NONE;
            m->ask_switch = -1;
            m->mopt = -1;
            m->gopt = -1;
            m->built = -1;
        }
    }
    /* the tests' reports waiting on the SD card go once the console is
     * on the network (reports_auto_due; no fibers here: a moment's wait,
     * from the covers only, not in Settings) */
    if (!m->on_gear && !m->asking && !m->on_market && !netxfer_write_pending() && reports_auto_due()) {
        reports_send_pending();
        kprintf("report %s\n", reports_last());
    }
}

/* What the tab lists, and the selection in it; the covers to read first. */
static void uh_selection(uhome_t *m)
{
    m->n = m->shown == TAB_MARKET ? market_items(items, CATALOG_ITEMS) : m->shown == TAB_GAMES ? n_games : N_DEV + n_dev;
    m->s = &m->sel[m->shown];
    if (*m->s >= m->n) *m->s = m->n - 1;
    if (*m->s < 0) *m->s = 0;
    if (m->shown == TAB_MARKET)
        market_select(*m->s);
    lib_cover_focus(m->on_gear ? -1 : m->tab == TAB_GAMES ? 0 : m->tab == TAB_DEV ? 1 : -1,   /* the covers to read first */
                    cart_at(m->shown, *m->s));
}

/* The covers of the tab. */
static void uh_items(uhome_t *m)
{
    for (int i = 0; i < m->n && m->shown != TAB_MARKET; i++) {
        if (m->shown == TAB_GAMES) {
            items[i] = game_item(game_idx[i]);
        } else if (i >= N_DEV) {
            items[i] = game_item(cart_at(m->shown, i));
        } else {
            items[i] = (menu_item_t){ .title = dev_items[i].name, .kind = "tool", .cover = &dev_covers[i] };
        }
    }
}

/* The frame: the view with the panels over the covers, the notice, the question. */
static void uh_draw(uhome_t *m)
{
    static const char *const market_pages[] = { "Games", "Apps" };
    menu_view_t v = {
        .tabs = tab_names, .ntabs = 3, .tab = m->shown, .on_gear = m->on_gear, .peek_first = 1,
        .subtabs = m->on_market ? market_pages : NULL, .nsubtabs = m->on_market ? 2 : 0, .subtab = market_page(),
        .refresh_label = m->on_market ? "Refresh" : NULL,
        .refresh_enabled = m->on_market && market_refresh_available(),
        .items = items, .n = m->n, .sel = *m->s,
        .prompts = MENU_PROMPTS_RGB30, .confirm_b = pad_ok == PAD_B,
        .ok_btn = pad_ok_name(), .back_btn = pad_back_name(), .no_monitor = 1,
        .y_label = m->on_market ? (market_page() == MARKET_PAGE_APPS ? "Games" : "Apps") : NULL,
        .idle = lib_menu_idle,
    };
    /* the bar: the WiFi and the battery only; no icons of the
     * controllers, mice and keyboards (the user, 2026-10-05) */
    v.net = wifi_linked() ? MENU_NET_WIFI : MENU_NET_NONE;
    v.net_wait = !net_ip();
    if (m->shown == TAB_MARKET) {
        v.banner = market_banner();
        v.a_label = market_action_label(*m->s);
        if (!v.a_label)
            v.a_label = "";
    } else if (m->shown == TAB_GAMES && m->n == 0) {
        /* nothing to list: the title pill says why (the bottom bar has the key chips only) */
        static char empty[72];
        if (library.hidden)
            ksnprintf(empty, sizeof empty, "No games yet: %d .bm hidden by show_bm=0", library.hidden);
        else if (strcmp(sd_state, "bm/") != 0)
            ksnprintf(empty, sizeof empty, "No games yet: no bm/ folder");
        else
            ksnprintf(empty, sizeof empty, "No games yet: put .b16 games in bm/");
        v.banner = empty;
        v.a_label = "";
    } else if (cart_at(m->shown, *m->s) >= 0) {
        v.a_label = lib_is_suspended(&games[cart_at(m->shown, *m->s)]) ? "Resume" : "Play";
    }
    menu_panel_t panel, sections, mpanel;
    if (m->mopt >= 0) {
        market_panel(m->mopt, &mpb);
        if (m->mopt_sel >= mpb.n) m->mopt_sel = mpb.n ? mpb.n - 1 : 0;
        mpanel = (menu_panel_t){ mpb.title, mpb.rows, mpb.n, m->mopt_sel,
                                 m->mopt_sel >= MENU_PANEL_ROWS ? m->mopt_sel - MENU_PANEL_ROWS + 1 : 0,
                                 mpb.n ? mpb.help[m->mopt_sel] : NULL };
        v.panel = &mpanel;
    } else if (m->gopt >= 0 && m->gopt < library.n) {
        if (m->gsend)
            market_send_panel(&gpb);
        else
            lib_panel(&games[m->gopt], &gpb);
        if (m->gopt_sel >= gpb.n) m->gopt_sel = gpb.n ? gpb.n - 1 : 0;
        mpanel = (menu_panel_t){ gpb.title, gpb.rows, gpb.n, m->gopt_sel,
                                 m->gopt_sel >= MENU_PANEL_ROWS ? m->gopt_sel - MENU_PANEL_ROWS + 1 : 0,
                                 gpb.n ? gpb.help[m->gopt_sel] : NULL };
        v.panel = &mpanel;
    } else {
        m->gopt = -1;
    }
    if (m->gopt < 0)
        m->gsend = 0;
    menu_page_t page;
    m->frame++;
    if (m->depth) {
        /* rebuilt when it changes, and twice a second for the values
         * that move (pads, uptime, battery) */
        int id = m->stack[m->depth - 1].id;
        if (m->built != id || m->frame % 30 == 0) {
            home_panel(id, &pb);
            m->built = id;
        }
        int *ps = &m->stack[m->depth - 1].sel, *pt = &m->stack[m->depth - 1].top;
        if (*ps >= pb.n) *ps = pb.n ? pb.n - 1 : 0;
        if (*ps < *pt) *pt = *ps;
        if (*ps >= *pt + MENU_PANEL_ROWS) *pt = *ps - MENU_PANEL_ROWS + 1;
        panel = (menu_panel_t){ pb.title, pb.rows, pb.n, *ps, *pt, pb.n ? pb.help[*ps] : NULL };
        /* Settings is a page (the narrow screen: one list at a time) */
        sections = panel;
        page = (menu_page_t){ &sections, m->depth > 1 ? &panel : NULL, m->depth > 1 };
        v.page = &page;
    }
    if (m->asking != ASK_NONE) {
        v.ask = m->ask_q;
        v.ask_detail = m->ask_d[0] ? m->ask_d : NULL;
        v.ask_yes = m->ask_y;
    }
    static char nt[NOTICE_LEN], nd[NOTICE_LEN];
    if (notice_now(nt, nd, &v.notice_progress)) {
        v.notice = nt;                      /* a kernel arriving, the restart */
        v.notice_detail = nd;
    }
    audio_idle();                           /* the output's news (QEMU's sink: what it heard) */
    v.battery = battery_state(&v.battery_pct, &v.charging, NULL);
    menu_ui_frame(fb, &v);
    static int first_frame = 1;
    if (first_frame) {
        first_frame = 0;
        kprintf("cartridge menu: first frame, %d of %d covers read\n", lib_covers_read(&library), library.n);
    }
    /* a file from the PC goes on even when the frame left no time for
     * it (lib_menu_idle): a piece a frame */
    if (netxfer_write_pending())
        netxfer_write_tick(timer_ticks() + 2000);
}

/* The first look at the input: PS is home, the serial line's backtick is a Lua prompt, a keyboard's Tab and 1 2 3 4
 * jump tabs. 1 if the frame is over. */
static int uh_input(uhome_t *m)
{
    m->p = pad_pressed();
    /* Start confirms unless Select is held (the Pi, carts.c:1474) */
    if ((m->p & PAD_START) && !(pad_state() & PAD_SELECT))
        m->p |= pad_ok;
    if (hid_quit_pressed() & HID_QUIT_PS) {
        /* the PS button or Ctrl+Esc of a Bluetooth pad or keyboard: home, the Games tab (as on the Pi) */
        m->tab = TAB_GAMES;
        m->mopt = -1;
        m->gopt = -1;
        m->asking = ASK_NONE;
        return 1;
    }
    const int serial_c = pad_serial_char();
    if (serial_c == '`') {
        run_page(serial_lua);
        return 1;
    }
    m->tabto = -1;                             /* a keyboard: Tab and 1 2 3 4 jump tabs (carts.c) */
    if (serial_c >= 0 && tab_of_key(serial_c, m->tab) >= 0)    /* the serial line's digits too, as on the Pi (its Tab is Select here) */
        m->tabto = tab_of_key(serial_c, m->tab);
    for (int k; (k = hid_getc()) >= 0;)
        if (tab_of_key(k, m->tab) >= 0)
            m->tabto = tab_of_key(k, m->tab);
    if (m->tabto >= 0 && m->asking == ASK_NONE) {
        m->tab = m->tabto;
        m->mopt = -1;
        m->gopt = -1;
        return 1;
    }
    return 0;
}

/* A question is open: confirm says yes, back cancels. */
static void uh_answer(uhome_t *m)
{
    /* a question: confirm says yes, back cancels */
    const int kind = m->asking;
    if (m->p & (pad_ok | pad_back))
        m->asking = ASK_NONE;
    if ((m->p & pad_ok) && kind == ASK_ROW) {
        home_act(m->stack[m->depth - 1].id, m->ask_row, HOME_YES, &m->d);
    } else if ((m->p & pad_ok) && kind == ASK_MARKET) {
        market_get(m->ask_game);
    } else if ((m->p & pad_ok) && kind == ASK_MOPT) {
        home_do_t md;
        market_act(m->mopt, m->ask_row, HOME_YES, &md);
        home_report(&md);
        if (md.what == HOME_BACK)
            m->mopt = -1;
    } else if ((m->p & pad_ok) && kind == ASK_GOPT && m->gopt >= 0) {
        home_do_t gd;
        lib_act(&games[m->gopt], m->ask_row, HOME_YES, &gd);
        home_report(&gd);
        if (gd.what == HOME_BACK) {
            m->gopt = -1;
            scan_games();
            market_carts_changed();
        }
    } else if ((m->p & pad_ok) && kind == ASK_SEND && m->gopt >= 0 && m->gsend) {
        market_send_act(m->ask_row, HOME_YES, &m->d);     /* HOME_TEXT: the sending, below */
    } else if ((m->p & pad_ok) && kind == ASK_COMPAT && m->ask_compat >= 0) {
        const int cg = m->ask_compat;          /* yes: the other questions go on */
        m->ask_compat = -1;
        if (lib_need_switch(&games[cg])) {
            m->ask_switch = cg;
            m->asking = ASK_SWITCH;
            lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
        } else {
            play_index = cg;
            run_page(play_selected);
        }
    } else if ((m->p & pad_ok) && kind == ASK_SWITCH && m->ask_switch >= 0) {
        play_index = m->ask_switch;            /* the frozen game is closed by starting this one */
        m->ask_switch = -1;
        run_page(play_selected);
    } else if (kind == ASK_OFFER && (m->p & (pad_ok | pad_back))) {
        market_offer_answer((m->p & pad_ok) != 0);
    }
}

/* The consoles nearby: up/down choose, confirm asks, back returns to the options of the game. */
static void uh_send_panel(uhome_t *m)
{
    /* the consoles nearby: up/down choose, confirm asks, back returns to the options */
    int dy = (m->p & PAD_UP) ? -1 : (m->p & PAD_DOWN) ? 1 : 0;
    if (dy && gpb.n)
        m->gopt_sel = (m->gopt_sel + dy + gpb.n) % gpb.n;
    if ((m->p & pad_back) || ((m->p & PAD_X) && pad_ok != PAD_X)) {
        m->gsend = 0;
        m->gopt_sel = 0;
    } else if ((m->p & pad_ok) && gpb.n && gpb.rows[m->gopt_sel].kind != MENU_ROW_INFO) {
        home_do_t sd;
        m->ask_row = gpb.ids[m->gopt_sel];
        market_send_act(m->ask_row, 0, &sd);
        if (sd.what == HOME_ASK) {
            m->asking = ASK_SEND;
            ksnprintf(m->ask_q, sizeof m->ask_q, "%s", sd.ask);
            ksnprintf(m->ask_d, sizeof m->ask_d, "%s", sd.ask_detail);
            ksnprintf(m->ask_y, sizeof m->ask_y, "%s", sd.ask_yes);
        }
    }
}

/* A game's options: up/down choose, confirm does, back closes. */
static void uh_game_panel(uhome_t *m)
{
    /* a game's options: up/down choose, confirm does, back closes */
    int dy = (m->p & PAD_UP) ? -1 : (m->p & PAD_DOWN) ? 1 : 0;
    if (dy && gpb.n)
        m->gopt_sel = (m->gopt_sel + dy + gpb.n) % gpb.n;
    if ((m->p & pad_back) || ((m->p & PAD_X) && pad_ok != PAD_X)) {
        m->gopt = -1;
    } else if ((m->p & pad_ok) && gpb.n && gpb.rows[m->gopt_sel].kind != MENU_ROW_INFO) {
        const int row = gpb.ids[m->gopt_sel], g = m->gopt;
        if (row == LIB_ROW_PLAY || row == LIB_ROW_SDK) {
            m->gopt = -1;
            static char gp[80];
            ksnprintf(gp, sizeof gp, "%s/%s", games[g].dir, games[g].name);
            if (netxfer_updating(gp)) {
                notice_fail("Updating, wait for the end of the download");
            } else if (row == LIB_ROW_SDK) {
                sdk_open = gp;              /* the SDK on the game: its project next to it */
                run_page(page_sdk);
            } else if (row == LIB_ROW_PLAY && lib_compat_question(&games[g], m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y,
                                                         sizeof m->ask_y)) {
                m->ask_compat = g;
                m->asking = ASK_COMPAT;
            } else if (row == LIB_ROW_PLAY && lib_need_switch(&games[g])) {
                m->ask_switch = g;
                m->asking = ASK_SWITCH;
                lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
            } else {
                play_index = g;
                run_page(play_selected);
            }
        } else if (row == LIB_ROW_SEND) {
            char gp[80];
            ksnprintf(gp, sizeof gp, "%s/%s", games[g].dir, games[g].name);
            market_send_setup(gp, games[g].title, games[g].author);
            m->gsend = 1;
            m->gopt_sel = 0;
        } else {
            home_do_t gd;
            lib_act(&games[g], row, 0, &gd);
            home_report(&gd);
            if (gd.what == HOME_ASK) {
                m->asking = ASK_GOPT;
                m->ask_row = row;
                ksnprintf(m->ask_q, sizeof m->ask_q, "%s", gd.ask);
                ksnprintf(m->ask_d, sizeof m->ask_d, "%s", gd.ask_detail);
                ksnprintf(m->ask_y, sizeof m->ask_y, "%s", gd.ask_yes);
            } else if (gd.what == HOME_BACK) {
                m->gopt = -1;
                scan_games();
                market_carts_changed();
            }
        }
    }
}

/* A Market game's details: up/down choose, confirm does, back closes. */
static void uh_market_panel(uhome_t *m)
{
    /* a Market game's details: up/down choose, confirm does, back closes */
    int dy = (m->p & PAD_UP) ? -1 : (m->p & PAD_DOWN) ? 1 : 0;
    if (dy && mpb.n)
        m->mopt_sel = (m->mopt_sel + dy + mpb.n) % mpb.n;
    if (m->p & pad_back) {
        m->mopt = -1;
    } else if ((m->p & pad_ok) && mpb.n && mpb.rows[m->mopt_sel].kind != MENU_ROW_INFO) {
        const int row = mpb.ids[m->mopt_sel], game = m->mopt;
        if (row == M_PLAY) {
            m->mopt = -1;
            const int mg = game_of_path(market_path(game));
            if (mg >= 0 && lib_compat_question(&games[mg], m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y)) {
                m->ask_compat = mg;
                m->asking = ASK_COMPAT;
            } else if (mg >= 0 && lib_need_switch(&games[mg])) {
                m->ask_switch = mg;
                m->asking = ASK_SWITCH;
                lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
            } else {
                play_market(game);
            }
        } else {
            home_do_t md;
            market_act(game, row, 0, &md);
            home_report(&md);
            if (md.what == HOME_ASK) {
                m->asking = ASK_MOPT;
                m->ask_row = row;
                ksnprintf(m->ask_q, sizeof m->ask_q, "%s", md.ask);
                ksnprintf(m->ask_d, sizeof m->ask_d, "%s", md.ask_detail);
                ksnprintf(m->ask_y, sizeof m->ask_y, "%s", md.ask_yes);
            } else if (md.what == HOME_BACK) {
                m->mopt = -1;
            }
        }
    }
}

/* A panel of Settings: up/down choose, confirm does, left/right change a value, back goes back one level (out of
 * Settings: to Dev). */
static void uh_settings_panel(uhome_t *m)
{
    /* a panel: up/down choose, confirm does, left/right change a
     * value, back goes back one level (out of Settings: to Dev) */
    int *ps = &m->stack[m->depth - 1].sel, id = m->stack[m->depth - 1].id;
    int dy = (m->p & PAD_UP) ? -1 : (m->p & PAD_DOWN) ? 1 : 0;
    int dx = (m->p & PAD_LEFT) ? -1 : (m->p & PAD_RIGHT) ? 1 : 0;
    if (dy && pb.n)
        *ps = (*ps + dy + pb.n) % pb.n;
    const menu_row_t *r = pb.n ? &pb.rows[*ps] : NULL;
    int row = pb.n ? pb.ids[*ps] : 0;
    /* right opens a section, left goes back to the sections (but
     * changes a value on a choice), as on the Pi's page */
    if (m->depth == 1 && dx > 0 && r && r->kind == MENU_ROW_SUB)
        m->p |= pad_ok;
    else if (m->depth > 1 && dx < 0 && (!r || r->kind != MENU_ROW_CHOICE))
        m->p |= pad_back;
    if (m->p & pad_back) {
        if (--m->depth == 0)
            m->tab = TAB_DEV;
        m->built = -1;
    } else if (r && (((m->p & pad_ok) && r->kind != MENU_ROW_INFO) || (dx && r->kind == MENU_ROW_CHOICE))) {
        home_act(id, row, (m->p & pad_ok) ? 0 : dx, &m->d);
        m->ask_row = row;
    }
}

/* The covers: left / right along the row, up / down a row; X the options, confirm gets or plays. */
static void uh_covers(uhome_t *m)
{
    /* the covers: left / right along the row, up / down a row (the
     * last row may be shorter) */
    int cols = menu_ui_cols(), to = *m->s;
    if (m->p & PAD_LEFT) to = *m->s - 1;
    if (m->p & PAD_RIGHT) to = *m->s + 1;
    if (m->p & PAD_UP) to = *m->s - cols;
    if (m->p & PAD_DOWN)
        to = *m->s + cols < m->n ? *m->s + cols : (*m->s / cols + 1) * cols < m->n ? m->n - 1 : *m->s;
    if (to >= 0 && to < m->n)
        *m->s = to;
    if (m->shown == TAB_MARKET) {
        /* X: a game's details; confirm: get it (after a question) or play it */
        const int a = market_action(*m->s);
        if ((m->p & PAD_X) && a != MARKET_NONE) {
            m->mopt = *m->s;
            m->mopt_sel = 0;
        } else if ((m->p & pad_ok) && (a == MARKET_GET || a == MARKET_UPDATE)) {
            home_do_t q;
            market_ask(*m->s, &q);
            ksnprintf(m->ask_q, sizeof m->ask_q, "%s", q.ask);
            ksnprintf(m->ask_d, sizeof m->ask_d, "%s", q.ask_detail);
            ksnprintf(m->ask_y, sizeof m->ask_y, "%s", q.ask_yes);
            m->asking = ASK_MARKET;
            m->ask_game = *m->s;
        } else if ((m->p & pad_ok) && a == MARKET_PLAY) {
            const int mg = game_of_path(market_path(*m->s));
            if (mg >= 0 && lib_compat_question(&games[mg], m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y)) {
                m->ask_compat = mg;
                m->asking = ASK_COMPAT;
            } else if (mg >= 0 && lib_need_switch(&games[mg])) {
                m->ask_switch = mg;
                m->asking = ASK_SWITCH;
                lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
            } else {
                play_market(*m->s);
            }
        }
    } else if ((m->p & PAD_X) && m->shown == TAB_GAMES && m->n > 0) {
        m->gopt = game_idx[*m->s];            /* X: the game's options (the Pi's C) */
        lib_panel_open(&games[m->gopt]);
        m->gopt_sel = 0;
    } else if (m->p & pad_ok) {
        char gp[80] = "";
        const int cg = cart_at(m->shown, *m->s);      /* a game, or an app or utility in Dev */
        if (cg >= 0)
            ksnprintf(gp, sizeof gp, "%s/%s", games[cg].dir, games[cg].name);
        if (cg >= 0 && netxfer_updating(gp)) {
            notice_fail("Updating, wait for the end of the download");
        } else if (cg >= 0 && lib_compat_question(&games[cg], m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y,
                                              sizeof m->ask_y)) {
            m->ask_compat = cg;
            m->asking = ASK_COMPAT;
        } else if (cg >= 0 && lib_need_switch(&games[cg])) {
            m->ask_switch = cg;
            m->asking = ASK_SWITCH;
            lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
        } else if (cg >= 0) {
            play_index = cg;
            run_page(play_selected);
        } else {
            run_page(dev_items[*m->s].run);
        }
    }
}

/* What a row of Settings asked for. */
static void uh_row_result(uhome_t *m)
{
    /* what a row of Settings asked for */
    if (m->d.what >= 0) {
        home_report(&m->d);
        m->built = -1;
        switch (m->d.what) {
        case HOME_OPEN:
            if (m->depth < DEPTH_MAX) {
                m->stack[m->depth].id = m->d.panel;
                m->stack[m->depth].sel = m->stack[m->depth].top = 0;
                m->depth++;
            }
            break;
        case HOME_BACK:
            if (m->depth > 1)
                m->depth--;
            break;
        case HOME_ASK:
            m->asking = ASK_ROW;
            ksnprintf(m->ask_q, sizeof m->ask_q, "%s", m->d.ask);
            ksnprintf(m->ask_d, sizeof m->ask_d, "%s", m->d.ask_detail);
            ksnprintf(m->ask_y, sizeof m->ask_y, "%s", m->d.ask_yes);
            break;
        case HOME_TEXT:
            text_fn = m->d.text;
            text_wait = m->d.wait;
            text_own = m->d.own;
            run_page(text_page);
            break;
        }
    }
}

void ui_home(framebuffer_t *f)
{
    uhome_t m;
    uh_open(&m, f);
    for (;;) {
        uh_view_state(&m);
        uh_background(&m);
        uh_selection(&m);
        uh_items(&m);
        uh_draw(&m);
        if (uh_input(&m))
            continue;
        m.d.what = -1;
        if (m.asking) {
            uh_answer(&m);
        } else if ((m.p & PAD_L1) && m.tab > TAB_MARKET) {
            /* L1 / R1: Market, Games, Dev, Settings, no going round (as on the Pi) */
            m.tab--;
            m.mopt = -1;
        } else if ((m.p & PAD_R1) && m.tab < TAB_SETTINGS) {
            m.tab++;
            m.mopt = -1;
        } else if (m.gopt >= 0 && m.gsend) {
            uh_send_panel(&m);
        } else if (m.gopt >= 0) {
            uh_game_panel(&m);
        } else if (m.mopt >= 0) {
            uh_market_panel(&m);
        } else if (m.depth) {
            uh_settings_panel(&m);
        } else if ((m.p & PAD_Y) && m.shown == TAB_MARKET) {
            market_set_page(market_page() == MARKET_PAGE_APPS ? MARKET_PAGE_GAMES : MARKET_PAGE_APPS);
            *m.s = 0;
        } else if ((m.p & PAD_SELECT) && !(pad_state() & PAD_START) && m.shown == TAB_MARKET) {
            market_refresh();
        } else if (m.n > 0) {
            uh_covers(&m);
        }
        uh_row_result(&m);
    }
}
