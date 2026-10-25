/*
 * Cartridge list (built-in + SD card) and the on-screen menu.
 */
#include "sdmap.h"
#include "carts.h"
#include "crumbs.h"
#include "devtool.h"
#include "toolnames.h"
#include "home.h"
#include "syskeys.h"
#include "market.h"
#include "publish.h"
#include "update.h"
#include "menu_ui.h"
#include "pointer.h"
#include "bm/bm.h"
#include "input.h"
#include "upload.h"
#include "bt/bt.h"
#include "net/net.h"
#include "net/netxfer.h"
#include "net/netcon.h"
#include "net/wifi_auto.h"
#include "bm/runtime.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "drivers/prop.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "bm/project.h"
#include "net/catalog.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "usb/hid.h"
#include "notice.h"
#include "bm/loading.h"
#include "ledstate.h"
#include "fiber.h"
#include "reports.h"
#include "config.h"
#include "library.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


extern const uint8_t bm_editor_cart[], bm_editor_cart_end[];
extern const uint8_t bm_sound_cart[], bm_sound_cart_end[];
extern const uint8_t bm_studio_cart[], bm_studio_cart_end[];
extern const uint8_t bm_animator_cart[], bm_animator_cart_end[];
extern const uint8_t bm_code_cart[], bm_code_cart_end[];
extern const uint8_t bm_mesh_cart[], bm_mesh_cart_end[];
extern const uint8_t bm_pixel_cart[], bm_pixel_cart_end[];

static char last_msg[96];
static char perf_msg[80];          /* speed of the last .bm game */

/* the Pi's supply too low now (bit 0 of the firmware's GET_THROTTLED): the
 * LED blinks while it is (ledstate.h); asked every 5 s */
static void power_led(void)
{
    static uint32_t at;
    if (at && timer_ticks() - at < 5000000u)
        return;
    at = timer_ticks() | 1;
    uint32_t thr[1] = { 0 };
    if (prop_query(PROP_GET_THROTTLED, thr, 1) == 0)
        ledstate_set(LED_POWER, thr[0] & 1);
}

/* ---------------------------------------------------------------- covers in the background
 *
 * The menu opens at once with the titles (the Market's placeholders); the
 * covers come in a fiber in the menu's free time (2026-10-04, the user's
 * request: smooth with any number of games), the ones on screen first. Of
 * a file only its first bytes are read: COVER is the first section. */

/* the tabs of the graphical menu (carts_menu) */
enum { TAB_MARKET, TAB_GAMES, TAB_DEV, TAB_SETTINGS };

/* the SD card's reads: the loading screen goes on, a fiber of the menu gives
 * the CPU back when its time is over (and stops when asked) */
static int sd_tick(void)
{
    loading_tick();
    fiber_slice();
    return fiber_cancelled();
}

/* The tests' reports waiting on the SD card go by themselves once the
 * console is on the network (reports_auto_due), in a fiber, only where
 * the network is free: Games and Dev, no panel open (the Market,
 * Settings and the options of a game use it themselves). */
static fiber_job_t report_job = { .name = "reports" };
static int reports_free;                /* the menu is where they may go (carts_menu) */

static void reports_main(void *arg)
{
    (void)arg;
    reports_send_pending();                     /* reports.c logs how it went */
}

static void reports_tick(uint32_t until)
{
    if (!report_job.busy &&
        (!reports_auto_due() || fiber_job_start(&report_job, 128 * 1024, reports_main, NULL) != 0))
        return;
    fiber_job_run(&report_job, until);
}

static void reports_idle(uint32_t until)
{
    if (reports_free)
        reports_tick(until);
}

/* The menu's jobs and their shares of its free time (fiber.h, 2026-10-11):
 * what the user started from a panel and waits for first, then the work of
 * the tab shown (the Market's work only on its tab: on another one it
 * gives its time back at once). Each is sure of its
 * share; what is left goes down the table. Before, the covers took every
 * frame's time first and the Market could get none. */
static const fiber_share_t menu_jobs[] = {
    /*                      game tool menu */
    { update_tick,        {  0,   0,  15 } },   /* Settings > Updates: check, install */
    { publish_tick,       {  0,   0,  15 } },   /* a game to the Market */
    { market_tick,        {  0,   0,  25 } },   /* its tab */
    { lib_covers_tick,    {  0,   0,  10 } },   /* the tab's covers first */
    { reports_idle,       {  0,   0,   5 } },   /* Games, Dev, no panel */
    { wifi_auto_idle,     {  0,   0,   5 } },   /* the saved network again, while the link is down */
    { sdmap_tick,         {  0,   0,  10 } },   /* the SD map scan (Settings > System > SD map), last */
};

/* a file from the PC waits: the report job stops too */
static void idle_write_waits(void)
{
    fiber_job_stop(&report_job);
}

/* before the Market's fiber: one at a time on the network (cancelled: it goes again later) */
static void idle_before_jobs(void)
{
    if (!reports_free)
        fiber_job_stop(&report_job);
}

/* Before the list changes, an application runs or the card is written:
 * the work in the background stops (what it held is freed) and goes on
 * later from where it was. An update or a game being published goes on
 * (they hold nothing of the list, write a file in one go): no application
 * starts until they end (carts_menu). */
static void background_stop(void)
{
    netxfer_write_pause();              /* a file from the PC: again later */
    lib_covers_stop();
    fiber_job_stop(&report_job);
    wifi_auto_stop();
    sdmap_stop();
}

/* Tool id ("sdk", "sound", "studio", "animator", "mesh", "pixel", "code",
 * "assistant") for a built-in or SD-installed app, or NULL for a game. */
static const char *tool_id_of(const lib_game_t *c)
{
    return c->tool;
}

static int has_sd_tool(const char *tid)
{
    for (int i = 0; i < library.nsd; i++) {
        const char *t = tool_id_of(&library.g[i]);
        if (t && strcmp(t, tid) == 0)
            return 1;
    }
    return 0;
}

/* the tools always come last (up from the first cartridge); skipped if
 * already installed on the SD card from Market > Apps or if CLEAN_KERNEL=1 */
static void add_builtins(void)
{
    if (&bm_editor_cart_end[0] > &bm_editor_cart[0] && !has_sd_tool("sdk"))
        lib_add_builtin(&library, "editor (built-in)", "sdk", bm_editor_cart, bm_editor_cart_end);
    if (&bm_sound_cart_end[0] > &bm_sound_cart[0] && !has_sd_tool("sound"))
        lib_add_builtin(&library, "sound (built-in)", "sound", bm_sound_cart, bm_sound_cart_end);
    if (&bm_studio_cart_end[0] > &bm_studio_cart[0] && !has_sd_tool("studio"))
        lib_add_builtin(&library, "studio (built-in)", "studio", bm_studio_cart, bm_studio_cart_end);
    if (&bm_animator_cart_end[0] > &bm_animator_cart[0] && !has_sd_tool("animator"))
        lib_add_builtin(&library, "animator (built-in)", "animator", bm_animator_cart, bm_animator_cart_end);
    if (&bm_mesh_cart_end[0] > &bm_mesh_cart[0] && !has_sd_tool("mesh"))
        lib_add_builtin(&library, "mesh (built-in)", "mesh", bm_mesh_cart, bm_mesh_cart_end);
    if (&bm_pixel_cart_end[0] > &bm_pixel_cart[0] && !has_sd_tool("pixel"))
        lib_add_builtin(&library, "pixel (built-in)", "pixel", bm_pixel_cart, bm_pixel_cart_end);
}

/* the folders of the Pi: the root, /carts, /bm (the games folder of the RGB30: the Pi reads it too) and the
 * projects (.bme, project.h) */
static const char *const pi_dirs[] = { "/", "/carts", "/bm", BM_PROJECTS_DIR, NULL };
static const lib_backend_t pi_backend = {
    .dirs = pi_dirs, .target = BM_TARGET_PI, .add_builtins = add_builtins, .stop_background = background_stop,
    .panel = LIB_CAP_SDK | LIB_CAP_TOOLS | LIB_CAP_COPY | LIB_CAP_PUBLISH | LIB_CAP_SEND,
    .jobs = menu_jobs, .njobs = (int)(sizeof menu_jobs / sizeof *menu_jobs),
    .write_waits = idle_write_waits, .before_jobs = idle_before_jobs,
};

static void rescan(void)
{
    library.be = &pi_backend;
    lib_scan(&library);
    market_carts_changed();
}

void carts_init(void)
{
    library.sd_ok = 0;
    if (sd_init() != 0) {
        kprintf("sd: %s\n", sd_error());
    } else if (fat_mount() != 0) {
        kprintf("sd: %s card, %lu MB: %s\n", sd_is_hc() ? "SDHC" : "SD",
                sd_blocks() / 2048, fat_error());
    } else {
        library.sd_ok = 1;
        fat_set_clock(net_time);        /* SNTP time for the file dates, when the net has it */
    }
    ledstate_set(LED_NO_SD, !library.sd_ok);
    fat_load_tick = sd_tick;            /* the loading screen, the menu's fibers */
    bm_parse_tick = sd_tick;
    /* a write in a fiber of the menu (the Market's game, an update) gives
     * the CPU back between its pieces (fat.h; netxfer sets its own) */
    if (!fat_write_tick)
        fat_write_tick = fiber_slice_tick;
    rescan();
    if (library.sd_ok) {
        /* the cluster size (a card's writes depend on it) and, if any, the
         * transfers retried and the waits that ran out so far (sd.h) */
        char size[24], extra[48] = "";
        const uint32_t cb = fat_cluster_bytes();
        if (cb >= 1024)
            ksnprintf(size, sizeof size, "%lu KB", (unsigned long)(cb / 1024));
        else
            ksnprintf(size, sizeof size, "%lu bytes", (unsigned long)cb);
        uint32_t retries, timeouts;
        sd_counters(&retries, &timeouts);
        if (retries || timeouts)
            ksnprintf(extra, sizeof extra, "; %lu retried, %lu timed out", (unsigned long)retries,
                      (unsigned long)timeouts);
        kprintf("sd: %s card (%s), %s; %d cartridges; clusters %s%s\n", sd_is_hc() ? "SDHC" : "SD",
                sd_controller(), fat_describe(), library.nsd, size, extra);
    }
}

int carts_count(void)
{
    return library.n;
}

void carts_list(void)
{
    for (int i = 0; i < library.n; i++)
        kprintf("  %2d  %-4s %7lu  %s%s%s  \"%s\"\n", i + 1, lib_kind(&library.g[i]),
                library.g[i].size, library.g[i].dir, library.g[i].dir[0] && strcmp(library.g[i].dir, "/") ? "/" : "",
                library.g[i].name, library.g[i].title);
}

static void perf_line(const bm_stats_t *st)
{
    perf_msg[0] = 0;
    if (st->frames) {
        uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
        uint32_t avg = st->cpu_us_total / st->frames;
        ksnprintf(perf_msg, sizeof perf_msg,
                  ", %lu.%lu fps, update+draw %lu.%02lu ms (max %lu.%02lu)",
                  fps10 / 10, fps10 % 10, avg / 1000, avg % 1000 / 10,
                  st->cpu_us_max / 1000, st->cpu_us_max % 1000 / 10);
    }
}

void carts_play_buffer(framebuffer_t *fb, const uint8_t *data, size_t len)
{
    if (len >= 8 && bm_is_cart(data)) {
        bm_stats_t st;
        bm_play(fb, data, len, LIB_PLAY_SECS, &st);
        bm_print_stats(&st);
        perf_msg[0] = 0;
        if (st.frames) {
            uint32_t ms = st.elapsed_us / 1000, fps10 = ms ? st.frames * 10000u / ms : 0;
            uint32_t avg = st.cpu_us_total / st.frames;
            ksnprintf(perf_msg, sizeof perf_msg,
                      ", %lu.%lu fps, update+draw %lu.%02lu ms (max %lu.%02lu)",
                      fps10 / 10, fps10 % 10, avg / 1000, avg % 1000 / 10,
                      st.cpu_us_max / 1000, st.cpu_us_max % 1000 / 10);
        }
    } else {
        kprintf("unknown cartridge format\n");
    }
}


/* The tools a tool can open on its file with lib_game_tool(name, path) (bm
 * Studio's "Open in bm Animator" and back). */
static const devtool_t tools[] = {
    { "studio", "bm Studio", bm_studio_cart, bm_studio_cart_end },
    { "animator", "bm Animator", bm_animator_cart, bm_animator_cart_end },
    { "mesh", "bm Mesh", bm_mesh_cart, bm_mesh_cart_end },
    { "pixel", "bm Pixel", bm_pixel_cart, bm_pixel_cart_end },
    { "code", "bm Code", bm_code_cart, bm_code_cart_end },
    { "sdk", "SDK", bm_editor_cart, bm_editor_cart_end },
    { "sound", "Sound editor", bm_sound_cart, bm_sound_cart_end },
};
static const devtools_t suite = { tools, sizeof tools / sizeof tools[0], NULL };

/* A development tool built into the kernel (the SDK, the Sound editor,
 * bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel), and the games it
 * tries (devtool.c, the RGB30's too): when the tool asks to play a file
 * (cart_run), play it, then open the tool again on that file with the
 * error the game stopped with, if any; when it asks for another tool
 * (lib_game_tool), that one opens on the file. `open`: a file to start on (the
 * menu's "Open in ..."), or NULL. Returns the name of the last tool (for
 * the menu's "last: ..."). */
const char *carts_tool_session(framebuffer_t *fb, const uint8_t *cart, size_t cart_len, const char *what,
                               const char *open)
{
    return devtool_session(fb, &suite, cart, cart_len, what, open);
}

/* the SDK or the Sound editor */
static const char *editor_session(framebuffer_t *fb, const char *open, const uint8_t *cart, const uint8_t *end)
{
    return carts_tool_session(fb, cart, (size_t)(end - cart), cart == bm_sound_cart ? "Sound editor" : "SDK",
                              open);
}

const char *carts_code(framebuffer_t *fb, const char *open)
{
    return carts_tool_session(fb, bm_code_cart, (size_t)(bm_code_cart_end - bm_code_cart), "bm Code", open);
}

static const char *studio_session(framebuffer_t *fb, const char *open)
{
    return carts_tool_session(fb, bm_studio_cart, (size_t)(bm_studio_cart_end - bm_studio_cart), "bm Studio",
                              open);
}

static const char *animator_session(framebuffer_t *fb, const char *open)
{
    return carts_tool_session(fb, bm_animator_cart, (size_t)(bm_animator_cart_end - bm_animator_cart),
                              "bm Animator", open);
}

static const char *mesh_session(framebuffer_t *fb, const char *open)
{
    return carts_tool_session(fb, bm_mesh_cart, (size_t)(bm_mesh_cart_end - bm_mesh_cart), "bm Mesh", open);
}

const char *carts_mesh(framebuffer_t *fb)
{
    return mesh_session(fb, NULL);
}

static const char *pixel_session(framebuffer_t *fb, const char *open)
{
    return carts_tool_session(fb, bm_pixel_cart, (size_t)(bm_pixel_cart_end - bm_pixel_cart), "bm Pixel", open);
}

const char *carts_pixel(framebuffer_t *fb)
{
    return pixel_session(fb, NULL);
}

const char *carts_editor(framebuffer_t *fb)
{
    return editor_session(fb, NULL, bm_editor_cart, bm_editor_cart_end);
}

const char *carts_sound_editor(framebuffer_t *fb)
{
    return editor_session(fb, NULL, bm_sound_cart, bm_sound_cart_end);
}

const char *carts_studio(framebuffer_t *fb)
{
    return studio_session(fb, NULL);
}

const char *carts_animator(framebuffer_t *fb)
{
    return animator_session(fb, NULL);
}

static void play(framebuffer_t *fb, lib_game_t *c)
{
    const char *tid = tool_id_of(c);
    if (tid) {
        lib_suspend_close();            /* the menu asked first */
        const char *w;
        if (!strcmp(tid, "sdk"))
            w = carts_editor(fb);
        else if (!strcmp(tid, "sound"))
            w = carts_sound_editor(fb);
        else if (!strcmp(tid, "mesh"))
            w = carts_mesh(fb);
        else if (!strcmp(tid, "pixel"))
            w = carts_pixel(fb);
        else if (!strcmp(tid, "animator"))
            w = carts_animator(fb);
        else if (!strcmp(tid, "code"))
            w = carts_code(fb, NULL);
        else if (!strcmp(tid, "assistant")) {
            home_assistant(fb);
            w = "Assistant";
        } else
            w = carts_studio(fb);
        ksnprintf(last_msg, sizeof last_msg, "last: %s", w);
        crumb("cartridge menu", NULL);
        rescan();                       /* it may have saved new files */
        return;
    }
    bm_stats_t st;
    perf_msg[0] = 0;
    const int r = lib_play(fb, c, &st);
    if (r == LIB_PLAY_UNREADABLE) {
        kprintf("\x1b[91mcannot read %s: %s\x1b[0m\n", c->name, fat_error());
        ksnprintf(last_msg, sizeof last_msg, "cannot read %s: %s", c->name, fat_error());
        notice_fail(last_msg);
        return;
    }
    if (r == LIB_PLAY_BADFORMAT) {
        kprintf("unknown cartridge format\n");
    } else {
        bm_print_stats(&st);
        perf_line(&st);
    }
    ksnprintf(last_msg, sizeof last_msg, "last: %s%s", c->name, perf_msg);
}

static int lower_prefix(const char *s, const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = p[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b || !a)
            return 0;
    }
    return 1;
}

/* The monitor line's play NAME: a game of the SD card or a built-in one by
 * its title or file name (the start of it is enough). 0, or -1 if none. */
int carts_play_title(framebuffer_t *fb, const char *name)
{
    size_t n = strlen(name);
    for (int i = 0; n && i < library.nsd; i++) {
        lib_game_t *c = &library.g[i];
        if (!lib_is_dev(c) && (lower_prefix(c->title, name, n) || lower_prefix(c->name, name, n))) {
            play(fb, c);
            return 0;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- menu */

static void out(const char *s)
{
    console_write(s);
}

static void outf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void outf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out(buf);
}

static void draw(int sel, int top, int rows)
{
    out("\x1b[2J\x1b[H");
    out("\x1b[1;96m bm - cartridges\x1b[0m");
    if (library.sd_ok)
        outf("\x1b[90m   SD: %s\x1b[0m", fat_describe());
    else
        outf("\x1b[90m   SD: %s\x1b[0m", sd_error());
    out("\n\n");
    for (int i = top; i < library.n && i < top + rows; i++) {
        const lib_game_t *c = &library.g[i];
        char title[39], author[19];
        ksnprintf(title, sizeof title, "%s", c->title);
        ksnprintf(author, sizeof author, "%s", c->author);
        /* SGR 7 swaps the colours: the selected row gets it once, no
         * other attribute inside */
        outf("%s %c %-38s %s%-18s%s %-3s %5lu KB \x1b[0m\n",
             i == sel ? "\x1b[7m" : "", i == sel ? '>' : ' ', title,
             i == sel ? "" : "\x1b[90m", author, i == sel ? "" : "\x1b[0m",
             "bm", (c->size + 1023) / 1024);
    }
    for (int i = library.n - top; i < rows; i++)
        out("\n");
    if (sel < library.n) {
        const lib_game_t *c = &library.g[sel];
        if (c->builtin)
            outf("\x1b[90m %s\x1b[0m\n", c->name);
        else
            outf("\x1b[90m %s%s%s\x1b[0m\n", c->dir, strcmp(c->dir, "/") ? "/" : "", c->name);
    }
    outf("\x1b[90m %s\x1b[0m\n", last_msg[0] ? last_msg : "");
    out("\x1b[93m up/down choose   Enter/A play   Ctrl+Esc/Start+Select: monitor   R rescan SD\x1b[0m");
}

/* ---------------------------------------------------------------- BareMetal UI */

/* The tabs of the graphical menu: the Market (market.c), games, the
 * development tools (the SDK, then the tools of home.c), and Settings. For
 * Games and Dev `idx` gets what the tab shows: a cartridge index, or -1 - n
 * for tool n. */
static int tab_items(int tab, int *idx)
{
    int n = lib_tab_items(tab == TAB_DEV, idx);
    if (tab == TAB_DEV) {               /* the projects are not listed: the editors open them (Open) */
        for (int t = 0; t < home_tools() && n < LIB_MAX + 16; t++)
            idx[n++] = -1 - t;
    }
    return n;
}

/* The options of a cartridge (X on its cover): a panel like the settings. */
static int opt_cart;            /* the cartridge of the HOME_CART panel */
static int opt_market;          /* the game of the HOME_MARKET panel */

static void cart_panel(home_panel_t *p)
{
    lib_panel(&library.g[opt_cart], p);
}

/* The rows of the options that stay in the menu (play and the SDK are
 * started by the menu itself). */
static void cart_act(int row, int how, home_do_t *d)
{
    lib_game_t *c = &library.g[opt_cart];
    if (lib_act(c, row, how, d))
        return;
    if (row == LIB_ROW_PUBLISH) {
        publish_setup(c->path, c->title, c->author);
        d->what = HOME_OPEN;
        d->panel = HOME_PUBLISH;
    }
}

enum { ASK_NONE, ASK_SWITCH, ASK_PANEL, ASK_MARKET, ASK_OFFER, ASK_COMPAT, ASK_UPDATE_SUSPENDED };
enum { GO_NONE, GO_PLAY, GO_SDK, GO_SOUND, GO_CODE, GO_STUDIO, GO_ANIMATOR, GO_MESH, GO_PIXEL, GO_TEXT, GO_UPLOAD,
       GO_NETPLAY };

#define DEPTH_MAX 4

/* what the network console is told takes its keys (netcon_focus) */
static const char FOCUS_MENU[] = "the menu (w a s d, Enter, Esc, 1-4 the tabs; q the monitor, "
                                 "or a line for it: :b3d; send)";
static const char FOCUS_APP[] = "the application (Ctrl-\\ back to the menu)";
static const char FOCUS_PAGE[] = "a page of the menu (Esc back)";

/* The state of the cartridge menu (carts_menu): what stays from a frame to the next, and what one frame fills in. */
typedef struct {
    framebuffer_t *fb;
    int gfx;                            /* the graphic menu (else the text list) */
    int list_rows;                      /* rows of the text list */
    int tab, on_gear, tsel[3];          /* the tab, Settings open as the tab, the selection of each tab */
    int sel, top, redraw, esc;          /* the text list; esc: the serial line is inside an escape sequence */
    uint32_t prev_btn, repeat_at;       /* the buttons of the last frame, the auto repeat */
    struct { int id, sel, top; } stack[DEPTH_MAX];      /* the panels open, one over the other */
    int depth, built, frame;
    int ask, ask_cart, ask_go, ask_row; /* the question over the menu, and what it is about */
    int compat_ok;                      /* the greyed game the user has said yes to (ASK_COMPAT) */
    char ask_q[64], ask_d[64], ask_y[16];
    /* one frame */
    int n;                              /* what the tab lists */
    menu_panel_t mp;                    /* the panel on top */
    int rebuilt;
    int dx, dy, action, quit, back, opts, ybtn, tabto, cur;     /* the input of the frame */
    int home_key;                       /* PS or Ctrl+Esc: home */
    int go, go_cart, go_wait, leave;    /* what to start, with what, and the exit to the monitor */
    void (*go_text)(framebuffer_t *);
    home_do_t d;                        /* what a row asked for */
    uint8_t *net_buf;                   /* a cartridge sent to play */
    size_t net_len;
} cmenu_t;

static menu_item_t items[LIB_MAX + 16 + CATALOG_MAX];
static int idx[LIB_MAX + 16];
static home_panel_t pb;

/* The menu opens: the keys on the console, the graphic menu if the screen allows it. */
static void cm_open(cmenu_t *m, framebuffer_t *fb)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    memset(m, 0, sizeof *m);
    m->fb = fb;
    m->list_rows = (int)rows - 7;
    if (m->list_rows < 3)
        m->list_rows = 3;
    /* the tabs: Market, Games, Dev and Settings,
     * whose panel opens when it is the tab (on_gear); L1 / R1 move between
     * them. Games comes first. */
    m->tab = TAB_GAMES;
    m->redraw = 1;
    m->prev_btn = hid_buttons();

    kprintf("\ncartridge menu: arrows or wasd, Enter plays, x options, [ ] or Tab or 1-4 change tab, q returns to the monitor\n"
            "(keyboard: Enter, Esc back, C options, Q E or 1-4 tabs, Ctrl+Esc home, Ctrl+Shift+Esc the monitor, hold F12: the keys)\n");
    input_flush();
    m->built = -1;
    m->ask = ASK_NONE;
    m->ask_cart = -1;
    m->ask_go = GO_NONE;
    m->compat_ok = -1;
    m->gfx = menu_ui_open(fb) == 0;         /* else the text menu */
    if (m->gfx)
        home_init();
}

/* The start of a frame: the Market works on its tab only, a game a nearby console offers is a question, the
 * tab lists what it has, and the panel on top is rebuilt when it changes. */
static void cm_begin_frame(cmenu_t *m)
{
    netcon_focus(FOCUS_MENU, NULL);
    /* the Market works only while its tab is shown */
    int on_market = m->gfx && m->tab == TAB_MARKET && !m->on_gear;
    market_set_active(on_market);
    /* nearby consoles: with the Market, or while choosing one to send to;
     * a game they offer is a question */
    market_lan(on_market || (m->gfx && m->depth && m->stack[m->depth - 1].id == HOME_SEND));
    if (m->ask == ASK_NONE && market_offer(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d)) {
        ksnprintf(m->ask_y, sizeof m->ask_y, "Accept");
        m->ask = ASK_OFFER;
    } else if (m->ask == ASK_OFFER) {
        char q_[64], d_[64];
        if (!market_offer(q_, sizeof q_, d_, sizeof d_))
            m->ask = ASK_NONE;                 /* the sender gave up */
    }
    m->n = m->tab == TAB_MARKET ? (m->gfx ? market_items(items, (int)(sizeof items / sizeof *items)) : 0)
                              : tab_items(m->tab, idx);
    if (m->tsel[m->tab] >= m->n) m->tsel[m->tab] = m->n ? m->n - 1 : 0;
    if (m->tab == TAB_MARKET)
        market_select(m->tsel[m->tab]);
    m->frame++;

    /* the panel on top: rebuilt when it changes, and twice a second
     * for the values that move (pads, uptime) */
    m->rebuilt = 0;
    if (m->depth) {
        int id = m->stack[m->depth - 1].id;
        if (m->built != id || m->frame % 30 == 0) {
            m->rebuilt = 1;
            if (id == HOME_CART)
                cart_panel(&pb);
            else if (id == HOME_MARKET)
                market_panel(opt_market, &pb);
            else if (id == HOME_PUBLISH)
                publish_panel(&pb);
            else if (id == HOME_SEND)
                market_send_panel(&pb);
            else
                home_panel(id, &pb);
            m->built = id;
        }
        int *ps = &m->stack[m->depth - 1].sel, *pt = &m->stack[m->depth - 1].top;
        if (*ps >= pb.n) *ps = pb.n ? pb.n - 1 : 0;
        if (*ps < *pt) *pt = *ps;
        if (*ps >= *pt + MENU_PANEL_ROWS) *pt = *ps - MENU_PANEL_ROWS + 1;
        m->mp = (menu_panel_t){ pb.title, pb.rows, pb.n, *ps, *pt, pb.n ? pb.help[*ps] : NULL };
    }
}

/* The frame: the graphic menu, or the text list. */
static void cm_draw(cmenu_t *m)
{
    static const char *const tabs[] = { "Market", "Games", "Dev" };
    char susp_title[49];
    const int on_market = m->gfx && m->tab == TAB_MARKET && !m->on_gear;
    if (m->gfx) {
        for (int i = 0; i < m->n && m->tab != TAB_MARKET; i++) {
            if (idx[i] < 0) {
                int t = -1 - idx[i];
                items[i] = (menu_item_t){ .title = home_tool_title(t), .path = "",
                                          .kind = "tool", .cover = home_tool_cover(t) };
                continue;
            }
            const lib_game_t *c = &library.g[idx[i]];
            items[i] = (menu_item_t){ .title = c->title, .path = c->path, .kind = lib_kind(c),
                                      .cover = c->cover.px ? &c->cover : NULL,
                                      .loading = !c->cover_read, .running = lib_is_suspended(c),
                                      .badge = NULL, .grey = c->grey,
                                      .update = market_has_update(c->path) };
            /* a file from the PC on its way (bm_net.py --send) */
            const int up = netxfer_updating(c->path);
            if (up) {
                items[i].badge = up == NETXFER_QUEUED ? "Queued" : "Updating";
                items[i].running = 0;
            }
        }
        /* the covers still to read: the tab's, from the selection */
        lib_cover_focus(m->on_gear ? -1 : m->tab == TAB_GAMES ? 0 : m->tab == TAB_DEV ? 1 : -1,
                        (m->tab == TAB_GAMES || m->tab == TAB_DEV) && m->n && idx[m->tsel[m->tab]] >= 0 ? idx[m->tsel[m->tab]] : -1);
        reports_free = !m->on_gear && !m->depth && m->ask == ASK_NONE && m->tab != TAB_MARKET;
        const char *a_label = NULL;
        if (m->tab == TAB_MARKET) {
            a_label = market_action_label(m->tsel[m->tab]);
            if (!a_label)
                a_label = "";
        }
        static const char *const market_pages[] = { "Games", "Apps" };
        menu_view_t v = {
            .tabs = tabs, .ntabs = 3, .tab = m->tab, .on_gear = m->on_gear, .peek_first = 1,
            .subtabs = on_market ? market_pages : NULL, .nsubtabs = on_market ? 2 : 0, .subtab = market_page(),
            .refresh_label = on_market ? "Refresh" : NULL,
            .refresh_enabled = on_market && market_refresh_available(),
            .items = items, .n = m->n, .sel = m->tsel[m->tab],
            .panel = m->depth ? &m->mp : NULL,
            .banner = on_market ? market_banner() : NULL,
            .a_label = a_label,
            .y_label = on_market ? (market_page() == MARKET_PAGE_APPS ? "Games" : "Apps") : NULL,
            .idle = lib_menu_idle,
        };
        /* Settings is a page: the sections on the left, the rows of the
         * one chosen (or under the selection, as a preview) on the right */
        static home_panel_t pside;
        static int side_id;
        menu_page_t page;
        menu_panel_t sp, rp;
        if (m->on_gear && m->depth && m->stack[0].id == HOME_SETTINGS) {
            int want = m->depth == 1 ? (pb.n ? home_sub_panel(pb.ids[m->stack[0].sel]) : 0) : HOME_SETTINGS;
            if (want && (m->rebuilt || side_id != want)) {
                home_panel(want, &pside);
                side_id = want;
            }
            if (m->depth == 1) {
                sp = m->mp;
                rp = (menu_panel_t){ pside.title, pside.rows, pside.n, -1, 0, NULL };
                page = (menu_page_t){ &sp, want ? &rp : NULL, 0 };
            } else {
                sp = (menu_panel_t){ pside.title, pside.rows, pside.n, m->stack[0].sel, m->stack[0].top, NULL };
                rp = m->mp;
                page = (menu_page_t){ &sp, &rp, 1 };
            }
            v.page = &page;
            v.panel = NULL;
        }
        for (int p = 0; p < 4; p++) {
            int d_ = input_device(p), kind = d_ & INPUT_DEV_KIND;     /* (not the pad's model) */
            v.dev[p] = kind == INPUT_DEV_KEYBOARD ? MENU_DEV_KEYBOARD
                     : kind == INPUT_DEV_PAD ? MENU_DEV_PAD : MENU_DEV_NONE;
            if (d_ & INPUT_DEV_BLUETOOTH)
                v.bt |= 1u << p;
        }
        v.mice = pointer_devices();
        /* the hints show the buttons of what was pressed last; before
         * that the keyboard's: on the Pi keyboard and mouse come first
         * (decision of 2026-10-04), the controllers after */
        int src = hid_last_source();
        v.prompts = src == HID_SOURCE_PAD ? MENU_PROMPTS_PAD
                  : src == HID_SOURCE_NONE || src == HID_SOURCE_KEYBOARD ? MENU_PROMPTS_KEYBOARD
                  : MENU_PROMPTS_DS4;
        v.prompts_colour = home_prompts_colour();
        v.keys_help = hid_usage_held(0x45);     /* F12 held (the system's keys) */
        static char nt[NOTICE_LEN], nd[NOTICE_LEN];
        if (notice_now(nt, nd, &v.notice_progress)) {
            v.notice = nt;                      /* a kernel arriving, the restart */
            v.notice_detail = nd;
        }
        power_led();
        int link = net_link_kind();
        v.net = link == NET_LINK_ETHERNET ? MENU_NET_ETHERNET
              : link == NET_LINK_WIFI ? MENU_NET_WIFI : MENU_NET_NONE;
        v.net_wait = !net_ip();
        if (m->ask == ASK_SWITCH && bm_suspended(NULL, 0)) {
            lib_switch_question(m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y);
            v.ask = m->ask_q;
            v.ask_detail = m->ask_d;
            v.ask_yes = m->ask_y;
        } else if (m->ask == ASK_UPDATE_SUSPENDED && bm_suspended(susp_title, sizeof susp_title)) {
            ksnprintf(m->ask_q, sizeof m->ask_q, "Update %s?", susp_title);
            v.ask = m->ask_q;
            v.ask_detail = "Game is frozen in memory: close and update? Unsaved progress will be lost.";
            v.ask_yes = "Close & Update";
        } else if (m->ask == ASK_PANEL || m->ask == ASK_MARKET || m->ask == ASK_OFFER || m->ask == ASK_COMPAT) {
            v.ask = m->ask_q;
            v.ask_detail = m->ask_d;
            v.ask_yes = m->ask_y;
        }
        menu_ui_frame(m->fb, &v);
    } else {
        if (m->sel < m->top) m->top = m->sel;
        if (m->sel >= m->top + m->list_rows) m->top = m->sel - m->list_rows + 1;
        if (m->redraw) {
            draw(m->sel, m->top, m->list_rows);
            m->redraw = 0;
        }
    }
}

/* Nothing pressed yet in this frame. */
static void cm_reset_input(cmenu_t *m)
{
    m->dx = m->dy = m->action = m->quit = m->back = m->opts = m->ybtn = 0;
    m->cur = m->on_gear ? TAB_SETTINGS : m->tab;
    m->tabto = -1;    /* the tab to go to */
}

/* The serial line (and the network console): 1 if a monitor line starts (":b3d"), the menu leaves for it. */
static int cm_input_serial(cmenu_t *m)
{
    /* serial */
    int to_line = 0;
    for (int k; !to_line && (k = input_remote_getc()) >= 0; ) {
        char c = (char)k;
        if (m->esc == 1) { m->esc = c == '[' ? 2 : 0; continue; }
        if (m->esc == 2) {
            m->esc = 0;
            if (c == 'A') m->dy--;
            if (c == 'B') m->dy++;
            if (c == 'C') m->dx++;
            if (c == 'D') m->dx--;
            continue;
        }
        switch (c) {
        case 0x1B:                          /* an arrow key, or Esc alone: back */
            if (input_remote_follows()) m->esc = 1; else m->back = 1;
            break;
        case 'w': case 'W': case 'k': m->dy--; break;
        case 's': case 'S': case 'j': m->dy++; break;
        case 'a': case 'A': case 'h': m->dx--; break;
        case 'd': case 'D': case 'l': m->dx++; break;
        case '\t': m->tabto = (m->cur + 1) % (TAB_SETTINGS + 1); break;
        case '1': m->tabto = TAB_MARKET; break;
        case '2': m->tabto = TAB_GAMES; break;
        case '3': m->tabto = TAB_DEV; break;
        case '4': m->tabto = TAB_SETTINGS; break;
        case '[': m->tabto = m->cur > 0 ? m->cur - 1 : -1; break;       /* L1 */
        case ']': m->tabto = m->cur < TAB_SETTINGS ? m->cur + 1 : -1; break;   /* R1 */
        case 'v': case 'V': m->ybtn = 1; break;                  /* Y */
        case 'x': case 'X': m->opts = 1; break;
        case 0x7F: case 0x08: m->back = 1; break;
        case '\r': case '\n': case ' ': m->action = 1; break;
        case 'q': case 'Q': m->quit = HID_QUIT_MONITOR; break;
        case 'r': case 'R': m->action = 2; break;
        case 'U': m->action = 3; break;         /* bm_load.py --cart */
        case ':':                           /* a monitor line (":b3d; send"), from */
            input_unget(':');               /* wherever the menu is: the monitor reads */
            to_line = 1;                    /* the rest of it */
            break;
        }
    }
    return to_line;
}

/* The USB keyboard and the gamepads: edges plus auto repeat. */
static void cm_input_pad(cmenu_t *m)
{
    /* USB keyboard / gamepads: edges plus auto repeat */
    const uint32_t DIRS = HID_UP | HID_DOWN | HID_LEFT | HID_RIGHT;
    uint32_t b = input_buttons(&m->quit);
    uint32_t pressed = b & ~m->prev_btn;
    uint32_t now = timer_ticks();
    uint32_t dirs = 0;
    if (pressed & DIRS) {
        dirs = pressed & DIRS;
        m->repeat_at = now + 400000;
    } else if ((b & DIRS) && (int32_t)(now - m->repeat_at) >= 0) {
        dirs = b & DIRS;
        m->repeat_at = now + 110000;
    }
    if (dirs & HID_UP) m->dy--;
    if (dirs & HID_DOWN) m->dy++;
    if (dirs & HID_LEFT) m->dx--;
    if (dirs & HID_RIGHT) m->dx++;
    if ((pressed & (HID_A | HID_START)) && !(b & HID_SELECT))
        m->action = 1;
    if (pressed & HID_B)
        m->back = 1;
    if (pressed & HID_X)
        m->opts = 1;
    if (pressed & HID_Y)
        m->ybtn = 1;
    if ((pressed & HID_SELECT) && !(b & HID_START) && m->tab == TAB_MARKET && !m->on_gear)
        market_refresh();
    if ((pressed & HID_L1) && m->cur > 0)
        m->tabto = m->cur - 1;
    if ((pressed & HID_R1) && m->cur < TAB_SETTINGS)
        m->tabto = m->cur + 1;
    m->prev_btn = b;
    for (int k; (k = hid_getc()) >= 0;) {  /* text keys from the USB keyboard */
        if (k == 'r' || k == 'R')
            m->action = 2;
        if (k == '\t')
            m->tabto = (m->cur + 1) % (TAB_SETTINGS + 1);
        if (k >= '1' && k <= '0' + TAB_SETTINGS + 1)    /* the tabs by number, as on the serial line */
            m->tabto = k - '1';
    }
    /* PS (and Ctrl+Esc, the system's keys) is home: Games, with every
     * panel and question closed (never the monitor). Esc alone goes back
     * like B; Start+Select, Ctrl+Shift+Esc and q from the serial port go
     * back a level too, and from the grid to the monitor. */
    if (m->dx || m->dy)
        pointer_hide();                     /* the keys move the selection: no arrow */
}

/* The pointer: moving over a cover or a row selects it, the left button does what A does there, the right one
 * goes back or opens the options, the wheel moves by rows. */
static void cm_input_pointer(cmenu_t *m)
{
    /* the pointer (M32): moving over a cover or a row selects it, the
     * left button does what A does there (or what is clicked: a tab, a
     * button of the hints), the right one goes back, or opens the
     * options of a cover; outside a panel or a question it closes them;
     * the wheel moves by rows */
    const pointer_t *pt = pointer_update();
    if (m->gfx && pt->shown) {
        menu_hit_t h = menu_ui_hit(pt->x, pt->y);
        int click = pt->pressed & 1, right = pt->pressed & 2;
        m->dy -= pt->wheel;
        if (m->ask != ASK_NONE) {
            if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
                m->action = 1;
            else if (click && h.kind == MENU_HIT_BUTTON)
                m->back = 1;
            else if ((click && h.kind != MENU_HIT_ASK) || right)
                m->back = 1;
        } else if (m->depth) {
            int *ps_ = &m->stack[m->depth - 1].sel;
            if (h.kind == MENU_HIT_ROW && h.index < pb.n && (pt->moved || click)) {
                *ps_ = h.index;
                m->action = click ? 1 : m->action;
            } else if (click && h.kind == MENU_HIT_SECTION && m->on_gear) {
                m->depth = 1;                  /* another section of the Settings page */
                m->built = -1;
                m->stack[0].sel = h.index;
                m->action = 1;
            } else if (click && h.kind == MENU_HIT_BUTTON) {
                m->action = h.index == 'A' ? 1 : m->action;
                m->back = h.index == 'B';
            } else if (click && h.kind == MENU_HIT_TAB) {
                m->tabto = h.index;
            } else if (click && h.kind == MENU_HIT_SETTINGS) {
                m->tabto = TAB_SETTINGS;
            } else if ((click && h.kind != MENU_HIT_PANEL && h.kind != MENU_HIT_ROW) || right) {
                m->back = 1;
            }
        } else {
            if (h.kind == MENU_HIT_COVER && h.index < m->n && ((pt->moved && h.full) || click || right))
                m->tsel[m->tab] = h.index;
            if (click && h.kind == MENU_HIT_COVER && h.full)
                m->action = 1;
            else if (click && h.kind == MENU_HIT_GROUP && m->tab == TAB_MARKET && !m->on_gear) {
                if (h.index != market_page()) {
                    market_set_page(h.index);
                    m->tsel[TAB_MARKET] = 0;
                }
            } else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'Y')
                m->ybtn = 1;
            else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'R') {
                if (m->tab == TAB_MARKET && !m->on_gear)
                    market_refresh();
            } else if (click && h.kind == MENU_HIT_TAB)
                m->tabto = h.index;
            else if (click && h.kind == MENU_HIT_SETTINGS)
                m->tabto = TAB_SETTINGS;
            else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
                m->action = 1;
            else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'X')
                m->opts = 1;
            else if (right && h.kind == MENU_HIT_COVER)
                m->opts = 1;
        }
    }
}

/* Esc, the keys of the system and PS: back or home. */
static void cm_input_finish(cmenu_t *m)
{
    m->home_key = m->quit & HID_QUIT_PS;
    if (((m->quit & HID_QUIT_KEY) && !(m->quit & HID_QUIT_MONITOR)) || (m->quit & HID_QUIT_ESC))
        m->back = 1;
    m->quit = (m->quit & HID_QUIT_MONITOR) != 0;
    if (m->home_key)
        m->tabto = TAB_GAMES;
}

/* Nothing to start yet in this frame. */
static void cm_reset_go(cmenu_t *m)
{
    m->go = GO_NONE; m->go_cart = -1; m->go_wait = 0; m->leave = 0;
    m->go_text = NULL;
    m->d.what = -1;
}

/* Another tab: whatever panel is open closes (a question waits for its answer, except for PS); Settings opens its
 * panel. */
static void cm_tab_switch(cmenu_t *m)
{
    /* another tab: whatever panel is open closes (a question waits for
     * its answer, except for PS); Settings opens its panel */
    if (m->gfx && m->tabto >= 0 && (m->ask == ASK_NONE || m->home_key)) {
        m->ask = ASK_NONE;
        m->depth = 0;
        m->built = -1;
        m->on_gear = m->tabto == TAB_SETTINGS;
        if (m->on_gear) {
            m->stack[0].id = HOME_SETTINGS;
            m->stack[0].sel = m->stack[0].top = 0;
            m->depth = 1;
        } else {
            m->tab = m->tabto;
        }
        m->dx = m->dy = m->back = m->opts = m->ybtn = 0;
        if (m->action == 1)
            m->action = 0;
    }
}

/* A question is open: A (Enter) says yes, B / q / Esc cancel. */
static void cm_answer(cmenu_t *m)
{
    /* A (Enter) says yes, B / q / Esc cancel */
    int yes = m->action == 1, no = m->quit || m->back;
    m->quit = m->back = m->opts = 0;
    m->action = 0;
    m->dx = m->dy = 0;
    if (no) {
        if (m->ask == ASK_OFFER)
            market_offer_answer(0);
        m->ask = ASK_NONE;
        m->compat_ok = -1;
    } else if (yes && m->ask == ASK_COMPAT) {
        m->compat_ok = m->ask_cart;           /* the user knows: the other questions go on */
        if (lib_need_switch(&library.g[m->ask_cart])) {
            m->ask = ASK_SWITCH;
        } else {
            m->ask = ASK_NONE;
            m->go = m->ask_go;
            m->go_cart = m->ask_cart;
        }
    } else if (yes && m->ask == ASK_OFFER) {
        m->ask = ASK_NONE;
        market_offer_answer(1);
    } else if (yes && m->ask == ASK_SWITCH && m->ask_cart >= 0 && netxfer_updating(library.g[m->ask_cart].path)) {
        m->ask = ASK_NONE;                 /* the game stays frozen: nothing to start yet */
        ksnprintf(last_msg, sizeof last_msg, "Updating, wait for the end of the download");
        notice_fail(last_msg);
    } else if (yes && m->ask == ASK_UPDATE_SUSPENDED) {
        m->ask = ASK_NONE;
        lib_suspend_close();
        m->go = m->ask_go;
        m->go_cart = m->ask_cart;
    } else if (yes && m->ask == ASK_SWITCH) {
        m->ask = ASK_NONE;
        lib_suspend_close();
        m->go = m->ask_go;
        m->go_cart = m->ask_cart;
    } else if (yes && m->ask == ASK_MARKET) {
        m->ask = ASK_NONE;
        market_get(m->ask_cart);
    } else if (yes && m->ask == ASK_PANEL) {
        m->ask = ASK_NONE;
        int id = m->stack[m->depth - 1].id;
        if (id == HOME_CART) {
            cart_act(m->ask_row, HOME_YES, &m->d);
        } else if (id == HOME_MARKET) {
            int c = lib_find_path(&library, market_path(opt_market));
            if (m->ask_row == M_DELETE && c >= 0 && lib_is_suspended(&library.g[c])) {
                lib_suspend_close();
            }
            market_act(opt_market, m->ask_row, HOME_YES, &m->d);
        } else if (id == HOME_PUBLISH) {
            publish_act(m->ask_row, HOME_YES, &m->d);
        } else if (id == HOME_SEND) {
            market_send_act(m->ask_row, HOME_YES, &m->d);
        } else {
            home_act(id, m->ask_row, HOME_YES, &m->d);
        }
    }
}

/* A panel is open: up/down choose, A does (in Settings A changes a choice, left goes back), left/right change a
 * value elsewhere, B / Esc / q go back one level. */
static void cm_panel_input(cmenu_t *m)
{
    /* a panel: up/down choose, A does (in Settings A changes a choice,
     * left goes back), left/right change a value elsewhere,
     * B / Esc / q go back one level */
    int *ps = &m->stack[m->depth - 1].sel, id = m->stack[m->depth - 1].id;
    if (m->dy && pb.n)
        *ps = (*ps + m->dy + pb.n) % pb.n;
    const menu_row_t *r = pb.n ? &pb.rows[*ps] : NULL;
    int row = pb.n ? pb.ids[*ps] : 0;
    /* the Settings page: right opens the section, left goes back to
     * the sections */
    if (m->on_gear && m->depth == 1 && m->dx > 0 && r && r->kind == MENU_ROW_SUB)
        m->action = 1;
    else if (m->on_gear && m->depth == 2 && m->dx < 0)
        m->back = 1;
    if (m->quit || m->back) {
        m->depth--;
        m->built = -1;
    } else if (r && id == HOME_CART && m->action == 1 &&
               (row == LIB_ROW_PLAY || row == LIB_ROW_SDK || row == LIB_ROW_SOUND || row == LIB_ROW_CODE || row == LIB_ROW_STUDIO ||
                row == LIB_ROW_ANIMATOR || row == LIB_ROW_MESH || row == LIB_ROW_PIXEL)) {
        const lib_game_t *c = &library.g[opt_cart];
        int g = row == LIB_ROW_PLAY ? GO_PLAY : row == LIB_ROW_SDK ? GO_SDK : row == LIB_ROW_SOUND ? GO_SOUND :
                row == LIB_ROW_CODE ? GO_CODE : row == LIB_ROW_MESH ? GO_MESH : row == LIB_ROW_PIXEL ? GO_PIXEL :
                row == LIB_ROW_ANIMATOR ? GO_ANIMATOR : GO_STUDIO;
        if (bm_suspended(NULL, 0) && !(g == GO_PLAY && lib_is_suspended(c))) {
            m->ask = ASK_SWITCH;           /* another game is frozen: ask first */
            m->ask_go = g;
            m->ask_cart = opt_cart;
        } else {
            m->go = g;
            m->go_cart = opt_cart;
        }
    } else if (r && id == HOME_MARKET && m->action == 1 && row == M_PLAY) {
        int c = lib_find_path(&library, market_path(opt_market));
        if (c < 0) {
            ksnprintf(last_msg, sizeof last_msg, "%s is not on the SD card", market_path(opt_market));
            notice_fail(last_msg);
        } else if (lib_need_switch(&library.g[c])) {
            m->ask = ASK_SWITCH;
            m->ask_go = GO_PLAY;
            m->ask_cart = c;
        } else {
            m->go = GO_PLAY;
            m->go_cart = c;
        }
    } else if (r && ((m->action == 1 && r->kind != MENU_ROW_INFO) ||
                     (m->dx && r->kind == MENU_ROW_CHOICE && !m->on_gear))) {
        if (id == HOME_CART)
            cart_act(row, m->action == 1 ? 0 : m->dx, &m->d);
        else if (id == HOME_MARKET)
            market_act(opt_market, row, m->action == 1 ? 0 : m->dx, &m->d);
        else if (id == HOME_PUBLISH)
            publish_act(row, m->action == 1 ? 0 : m->dx, &m->d);
        else if (id == HOME_SEND)
            market_send_act(row, m->action == 1 ? 0 : m->dx, &m->d);
        else
            home_act(id, row, m->action == 1 ? 0 : m->dx, &m->d);
        m->ask_row = row;
    }
    m->quit = 0;
    m->action = m->action >= 3 ? m->action : 0;
    m->dx = m->dy = m->opts = 0;
}

/* What a row asked for (HOME_OPEN, HOME_BACK, HOME_ASK, HOME_TEXT, HOME_MONITOR). */
static void cm_row_result(cmenu_t *m)
{
    /* what a row asked for */
    if (m->d.what >= 0) {
        if (m->d.note[0]) {
            ksnprintf(last_msg, sizeof last_msg, "%s", m->d.note);      /* the text menu's line */
            home_report(&m->d);
        }
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
            if (m->depth && m->stack[m->depth - 1].id == HOME_CART) {
                m->depth = 0;                  /* the cartridge is gone */
                rescan();
            } else if (m->depth) {
                m->depth--;
            }
            break;
        case HOME_ASK:
            m->ask = ASK_PANEL;
            ksnprintf(m->ask_q, sizeof m->ask_q, "%s", m->d.ask);
            ksnprintf(m->ask_d, sizeof m->ask_d, "%s", m->d.ask_detail);
            ksnprintf(m->ask_y, sizeof m->ask_y, "%s", m->d.ask_yes);
            break;
        case HOME_TEXT:
            m->go = GO_TEXT;
            m->go_text = m->d.text;
            m->go_wait = m->d.wait;
            break;
        case HOME_MONITOR:
            m->leave = 1;
            break;
        }
    }

    /* Settings is the tab only while its panel is open: B out of it
     * goes back to the tab before */
    if (m->on_gear && (!m->depth || m->stack[0].id != HOME_SETTINGS))
        m->on_gear = 0;
}

/* Up, down, left and right along the covers (or the text list), and X: the options of the cartridge or of the
 * Market's game. */
static void cm_navigate(cmenu_t *m)
{
    if (m->gfx && !m->depth && m->ask == ASK_NONE) {
        if (m->n) {
            /* up and down along the rows (not into the tab bar: L1 / R1
             * change the tab), left/right along the covers, wrapping */
            int s_ = m->tsel[m->tab];
            if (m->dy) {
                int cols = menu_ui_cols();
                int to = s_ + m->dy * cols;
                if (to >= m->n && m->dy > 0)      /* the last row may be shorter */
                    to = (to / cols) * cols < m->n ? m->n - 1 : s_;
                if (to >= 0 && to < m->n) s_ = to;
            }
            if (m->dx && s_ + m->dx >= 0 && s_ + m->dx < m->n)
                s_ += m->dx;
            m->tsel[m->tab] = s_;
        }
        if (m->ybtn && !m->on_gear && m->tab == TAB_MARKET) {
            market_set_page(market_page() == MARKET_PAGE_APPS ? MARKET_PAGE_GAMES : MARKET_PAGE_APPS);
            m->tsel[TAB_MARKET] = 0;
        }
        /* X: the options of the highlighted cartridge, or of the
         * Market's game */
        if (m->opts && !m->on_gear && m->tab == TAB_MARKET && market_action(m->tsel[m->tab]) != MARKET_NONE) {
            opt_market = m->tsel[m->tab];
            m->stack[0].id = HOME_MARKET;
            m->stack[0].sel = m->stack[0].top = 0;
            m->depth = 1;
            m->built = -1;
        } else if (m->opts && !m->on_gear && m->tab != TAB_MARKET && m->n && idx[m->tsel[m->tab]] >= 0) {
            opt_cart = idx[m->tsel[m->tab]];
            lib_panel_open(&library.g[opt_cart]);
            m->stack[0].id = HOME_CART;
            m->stack[0].sel = m->stack[0].top = 0;
            m->depth = 1;
            m->built = -1;
        }
    } else if (!m->gfx && (m->dx || m->dy)) {
        m->sel = ((m->sel + m->dx + m->dy) % library.n + library.n) % library.n;
        m->redraw = 1;
    }
}

/* From the network (bm_net.py): a file saved on the SD card shows up in the list, a cartridge sent to play starts;
 * a game of the Market installed or deleted: the SD card again. */
static void cm_events(cmenu_t *m)
{
    /* from the network (bm_net.py): a file saved on the SD card
     * shows up in the list, a cartridge sent to play starts */
    static unsigned seen_saves;
    m->net_buf = NULL;
    m->net_len = 0;
    /* a new copy of the game frozen in memory arrived: keep it suspended until
     * the user chooses to close and update (ASK_UPDATE_SUSPENDED) */
    if (library.susp_path[0] && netxfer_updating(library.susp_path) >= NETXFER_QUEUED && bm_suspended(NULL, 0)) {
        /* do not kill suspended game: prompt on launch */
    }
    if (netxfer_saves() != seen_saves) {
        seen_saves = netxfer_saves();
        if (!m->action)
            m->action = 2;
    }
    if (!m->action && netxfer_take_play(&m->net_buf, &m->net_len))
        m->action = 4;
    /* a game of the Market installed or deleted: the SD card again */
    if (market_take_changed()) {
        rescan();
        if (m->depth && m->stack[0].id == HOME_CART)
            m->depth = 0;                      /* indices changed */
    }
}

/* The action of the frame: rescan, upload, play a cartridge sent, A on a game of the Market (get it after a
 * question, or play it) or on a cover. 1 if the menu leaves for the monitor. */
static int cm_activate(cmenu_t *m)
{
    if (m->action == 2) {
        if (m->tab == TAB_MARKET && !m->on_gear) {
            market_refresh();
        } else {
            carts_init();
            if (m->sel >= library.n) m->sel = 0;
            m->depth = 0;                          /* indices changed */
            m->ask = ASK_NONE;
            m->redraw = 1;
        }
    } else if (m->action == 3) {
        m->go = GO_UPLOAD;
    } else if (m->action == 4) {
        m->go = GO_NETPLAY;
    } else if (m->action == 1 && !m->on_gear && m->gfx && m->tab == TAB_MARKET) {
        /* A on a game of the Market: get it (after a question), or play it */
        int i = m->tsel[m->tab], a = market_action(i);
        if (a == MARKET_GET || a == MARKET_UPDATE) {
            home_do_t q;
            market_ask(i, &q);
            ksnprintf(m->ask_q, sizeof m->ask_q, "%s", q.ask);
            ksnprintf(m->ask_d, sizeof m->ask_d, "%s", q.ask_detail);
            ksnprintf(m->ask_y, sizeof m->ask_y, "%s", q.ask_yes);
            m->ask = ASK_MARKET;
            m->ask_cart = i;
        } else if (a == MARKET_PLAY) {
            int c = lib_find_path(&library, market_path(i));
            if (c < 0) {
                ksnprintf(last_msg, sizeof last_msg, "%s is not on the SD card", market_path(i));
                notice_fail(last_msg);
            } else if (lib_need_switch(&library.g[c])) {
                m->ask = ASK_SWITCH;
                m->ask_go = GO_PLAY;
                m->ask_cart = c;
            } else {
                m->go = GO_PLAY;
                m->go_cart = c;
            }
        }
    } else if (m->action == 1 && !m->on_gear && (!m->gfx || m->n > 0)) {
        /* A plays the highlighted cover, also from the tab bar; if
         * another game is suspended, ask first */
        int i = m->gfx ? idx[m->tsel[m->tab]] : m->sel;
        if (i < 0) {
            home_tool_start(-1 - i, &m->d);
            if (m->d.what == HOME_MONITOR)
                return 1;
            m->go = GO_TEXT;
            m->go_text = m->d.text;
            m->go_wait = m->d.wait;
        } else if (m->gfx && lib_is_suspended(&library.g[i]) && netxfer_updating(library.g[i].path)) {
            m->ask = ASK_UPDATE_SUSPENDED;
            m->ask_go = lib_is_project(&library.g[i]) ? GO_SDK : GO_PLAY;
            m->ask_cart = i;
        } else if (m->gfx && lib_need_switch(&library.g[i])) {
            m->ask = ASK_SWITCH;
            m->ask_go = lib_is_project(&library.g[i]) ? GO_SDK : GO_PLAY;
            m->ask_cart = i;
        } else {
            m->go = lib_is_project(&library.g[i]) ? GO_SDK : GO_PLAY;     /* a project opens in the SDK */
            m->go_cart = i;
        }
    }
    return 0;
}

/* Before something starts: a game that is not made for this console asks first, a file on its way or an update
 * in the background stops it. */
static void cm_checks(cmenu_t *m)
{
    /* a game that is not made for this kernel (docs/spec/compat-policy.toml): a question first, before the
     * one about a suspended game. It is not a lock: yes starts it. */
    {
        const int gi = m->ask == ASK_SWITCH && m->ask_go == GO_PLAY ? m->ask_cart : m->go == GO_PLAY ? m->go_cart : -1;
        if (m->gfx && gi >= 0 && m->compat_ok != gi &&
            lib_compat_question(&library.g[gi], m->ask_q, sizeof m->ask_q, m->ask_d, sizeof m->ask_d, m->ask_y, sizeof m->ask_y)) {
            m->ask = ASK_COMPAT;
            m->ask_go = GO_PLAY;
            m->ask_cart = gi;
            m->go = GO_NONE;
        }
    }
    if (m->go != GO_NONE && m->go_cart >= 0 && netxfer_updating(library.g[m->go_cart].path)) {
        ksnprintf(last_msg, sizeof last_msg, "Updating, wait for the end of the download");
        notice_fail(last_msg);
        m->go = GO_NONE;
    }
    /* an update or a game being published in the background: nothing
     * starts before it ends (the install restarts the console) */
    const char *busy = update_busy() ? update_busy() : publish_busy() ? "Publishing to the Market" : NULL;
    if (m->go != GO_NONE && busy) {
        ksnprintf(last_msg, sizeof last_msg, "%s: wait for it to end", busy);
        notice_fail(last_msg);
        if (m->go == GO_NETPLAY)
            free(m->net_buf);
        m->go = GO_NONE;
    }
}

/* Starts what the frame chose (m->go): a game, a tool, a page on the text console, an upload; then the menu
 * opens again. */
static void cm_launch(cmenu_t *m)
{
    m->ask = ASK_NONE;
    m->compat_ok = -1;
    market_set_active(0);               /* nothing loads behind a game */
    market_lan(0);
    background_stop();                  /* the covers: later */
    /* an application (a game, a tool): the system's splash while it
     * loads, never the log (2026-10-04); a suspended game comes back
     * at once */
    const int app = m->go >= GO_PLAY && m->go <= GO_PIXEL;
    const int resume = m->go == GO_PLAY && m->go_cart >= 0 && lib_is_suspended(&library.g[m->go_cart]);
    if (m->gfx && app)
        menu_ui_close_quiet(m->fb);
    else if (m->gfx)
        menu_ui_close(m->fb);
    /* the bm suite (editor, Studio, Animator, Mesh, Pixel, Sound, a project) opens
     * with no splash; the splash is for games */
    const int suite = (m->go >= GO_SDK && m->go <= GO_PIXEL) ||
                      (m->go == GO_PLAY && m->go_cart >= 0 && lib_is_dev(&library.g[m->go_cart]));
    if (app && !resume && !suite)
        loading_begin(m->fb);
    /* the file of a tool: a cartridge of the menu, or none */
    const char *gpath = m->go_cart >= 0 ? library.g[m->go_cart].path : "";
    const char *gname = m->go_cart >= 0 ? library.g[m->go_cart].name : "";
    netcon_focus(m->go == GO_TEXT ? FOCUS_PAGE : FOCUS_APP, NULL);
    switch (m->go) {
    case GO_PLAY:
        play(m->fb, &library.g[m->go_cart]);
        m->depth = 0;
        break;
    case GO_SDK:
    case GO_SOUND:
        lib_suspend_close();
        ksnprintf(last_msg, sizeof last_msg, "last: %s on %s",
                  editor_session(m->fb, gpath, m->go == GO_SDK ? bm_editor_cart : bm_sound_cart,
                                 m->go == GO_SDK ? bm_editor_cart_end : bm_sound_cart_end),
                  gname);
        crumb("cartridge menu", NULL);
        m->depth = 0;
        rescan();                       /* it may have saved new files */
        break;
    case GO_CODE:
        lib_suspend_close();
        ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", carts_code(m->fb, gpath),
                  gname);
        crumb("cartridge menu", NULL);
        m->depth = 0;
        rescan();
        break;
    case GO_STUDIO:
    case GO_ANIMATOR:
        lib_suspend_close();
        ksnprintf(last_msg, sizeof last_msg, "last: %s on %s",
                  m->go == GO_STUDIO ? studio_session(m->fb, gpath)
                                  : animator_session(m->fb, gpath),
                  gname);
        crumb("cartridge menu", NULL);
        m->depth = 0;
        rescan();
        break;
    case GO_MESH:
        lib_suspend_close();
        ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", mesh_session(m->fb, gpath),
                  gname);
        crumb("cartridge menu", NULL);
        m->depth = 0;
        rescan();
        break;
    case GO_PIXEL:
        lib_suspend_close();
        ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", pixel_session(m->fb, gpath),
                  gname);
        crumb("cartridge menu", NULL);
        m->depth = 0;
        rescan();
        break;
    case GO_TEXT:
        syskeys_test_begin();
        m->go_text(m->fb);
        if (m->go_wait && !syskeys_test_stopped())  /* a test stopped (PS...): the menu at once */
            home_wait_back();
        if (!m->gfx)
            console_suspend(0);         /* the text menu: a stopped test left it hidden */
        crumb("cartridge menu", NULL);
        break;
    case GO_UPLOAD:
        upload_and_play(m->fb);
        m->depth = 0;
        break;
    case GO_NETPLAY:
        carts_play_buffer(m->fb, m->net_buf, m->net_len);
        free(m->net_buf);
        m->depth = 0;
        break;
    }
    loading_stop();                     /* an application that never got to its first frame */
    input_flush();
    m->prev_btn = hid_buttons();
    m->redraw = 1;
    m->built = -1;
    if (m->gfx)
        m->gfx = menu_ui_open(m->fb) == 0;
}

/* The menu closes: back to the monitor. */
static void cm_close(cmenu_t *m)
{
    market_set_active(0);
    market_lan(0);
    if (m->gfx)
        menu_ui_close(m->fb);
    console_clear();
    kprintf("back to the monitor\n");
}

void carts_menu(framebuffer_t *fb)
{
    cmenu_t m;
    cm_open(&m, fb);
    for (;;) {
        cm_begin_frame(&m);
        cm_draw(&m);

        /* a file from the PC goes on even when the frame left no time for
         * it (lib_menu_idle), and in the text list too: a piece a frame */
        if (netxfer_write_pending())
            lib_menu_idle(timer_ticks() + 2000);

        cm_reset_input(&m);
        if (cm_input_serial(&m))
            break;
        cm_input_pad(&m);
        cm_input_pointer(&m);
        cm_input_finish(&m);
        cm_reset_go(&m);
        cm_tab_switch(&m);
        if (m.ask != ASK_NONE)
            cm_answer(&m);
        else if (m.depth && m.gfx)
            cm_panel_input(&m);
        cm_row_result(&m);
        if (m.quit || m.leave)
            break;
        cm_navigate(&m);
        cm_events(&m);
        if (cm_activate(&m))
            break;
        cm_checks(&m);
        if (m.go != GO_NONE)
            cm_launch(&m);
        if (!m.gfx)
            timer_delay_us(2000);
    }
    cm_close(&m);
}
