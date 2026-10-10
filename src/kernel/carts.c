/*
 * Cartridge list (built-in + SD card) and the on-screen menu.
 */
#include "carts.h"
#include "crumbs.h"
#include "home.h"
#include "market.h"
#include "publish.h"
#include "lib.h"
#include "menu_ui.h"
#include "pointer.h"
#include "bm/bm.h"
#include "input.h"
#include "upload.h"
#include "bt/bt.h"
#include "net/net.h"
#include "net/netxfer.h"
#include "net/netcon.h"
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CARTS   64
#define PLAY_SECS   (24u * 3600u)

extern const uint8_t bm_editor_cart[], bm_editor_cart_end[];
extern const uint8_t bm_sound_cart[], bm_sound_cart_end[];
extern const uint8_t bm_studio_cart[], bm_studio_cart_end[];
extern const uint8_t bm_animator_cart[], bm_animator_cart_end[];
extern const uint8_t bm_code_cart[], bm_code_cart_end[];
extern const uint8_t bm_mesh_cart[], bm_mesh_cart_end[];
extern const uint8_t bm_pixel_cart[], bm_pixel_cart_end[];

typedef struct {
    char title[49];             /* from the header; the file name if none */
    char author[33];
    char name[FAT_NAME_MAX];
    char dir[8];                /* "" for built-in, "/" or "/carts" */
    const uint8_t *builtin;     /* NULL: file on SD */
    uint32_t size;
    fat_entry_t fe;
    g16_sheet_t cover;          /* printed on the card in the menu (px NULL: none) */
    uint8_t cover_read;         /* the covers' fiber has been at it (else a placeholder) */
    char path[FAT_NAME_MAX + 10];
    char save[32];              /* its save file (.bm), "" if none */
} cart_t;

static cart_t carts[MAX_CARTS];
static int ncarts, nsd, sd_ok;
static char last_msg[96];
static char perf_msg[80];          /* speed of the last .bm game */
static char susp_path[FAT_NAME_MAX + 10];   /* the cartridge frozen in memory, "" if none */

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

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n <= m)
        return 0;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i])
            return 0;
    }
    return 1;
}

/* "bm", "b16" or "bme" (a project): the menu's label of a file */
static const char *kind_of(const cart_t *c)
{
    return ends_with(c->name, ".b16") ? "b16" : ends_with(c->name, ".bme") ? "bme" : "bm";
}

/* Title and author from the first bytes of the .bm image. */
static void read_header(cart_t *c, const uint8_t *h, uint32_t len)
{
    size_t toff = 0, tlen = 0, aoff = 0, alen = 0;
    if (len >= 128 && bm_is_cart(h)) {
        toff = 24; tlen = 48; aoff = 72; alen = 32;
    }
    if (tlen) {
        size_t n = tlen < sizeof c->title - 1 ? tlen : sizeof c->title - 1;
        memcpy(c->title, h + toff, n);
        c->title[n] = 0;
        memcpy(c->author, h + aoff, alen < sizeof c->author - 1 ? alen : sizeof c->author - 1);
        c->author[sizeof c->author - 1] = 0;
        bm_save_path(c->title, c->author, c->save, sizeof c->save);   /* as the runtime names it */
    }
    for (char *p = c->title; *p; p++)           /* the console font is CP437 */
        if ((unsigned char)*p < 32) *p = ' ';
    for (char *p = c->author; *p; p++)
        if ((unsigned char)*p < 32) *p = ' ';
    if (!c->title[0])
        ksnprintf(c->title, sizeof c->title, "%s", c->name);
}

static void add_builtin(const char *name, const uint8_t *start, const uint8_t *end)
{
    cart_t *c = &carts[ncarts++];
    memset(c, 0, sizeof *c);
    strcpy(c->name, name);
    c->builtin = start;
    c->size = (uint32_t)(end - start);
    read_header(c, start, c->size);
}

static void scan_dir(const char *path)
{
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, path) != 0)
        return;
    while (ncarts < MAX_CARTS && fat_readdir(&d, &e)) {
        if (e.is_dir || e.name[0] == '.')
            continue;
        /* .b16, the handhelds' cartridge, is the same container on the Pi
         * and runs the same way (the user, 2026-10-05); .bme a project,
         * the editable file of a game (project.h, 2026-10-06) */
        if (!ends_with(e.name, ".bm") && !ends_with(e.name, ".b16") && !ends_with(e.name, ".bme"))
            continue;
        cart_t *c = &carts[ncarts++];
        memset(c, 0, sizeof *c);
        memcpy(c->name, e.name, sizeof c->name);
        strcpy(c->dir, path);
        c->size = e.size;
        c->fe = e;
        static uint8_t head[512] __attribute__((aligned(4)));
        if (fat_read_head(&e, head) == 0)
            read_header(c, head, e.size < 512 ? e.size : 512);
        else
            read_header(c, head, 0);
    }
}

static int title_cmp(const void *a, const void *b)
{
    const char *x = ((const cart_t *)a)->title, *y = ((const cart_t *)b)->title;
    for (;; x++, y++) {
        int cx = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x;
        int cy = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;
        if (cx != cy || !cx)
            return cx - cy;
    }
}

/* ---------------------------------------------------------------- covers in the background
 *
 * The menu opens at once with the titles (the Market's placeholders); the
 * covers come in a fiber in the menu's free time (2026-10-04, the user's
 * request: smooth with any number of games), the ones on screen first. Of
 * a file only its first bytes are read: COVER is the first section. */

/* the tabs of the graphical menu (carts_menu) */
enum { TAB_MARKET, TAB_GAMES, TAB_DEV, TAB_LIB, TAB_SETTINGS };

static fiber_job_t cover_job;
static int cover_tab = TAB_GAMES, cover_sel = -1;      /* what the menu shows: its covers first */

static int is_dev(const cart_t *c);

/* The cover from the .bm COVER section, or a label with the title. */
static void read_cover(cart_t *c)
{
    const uint8_t *rgba = NULL;
    int w = 0, h = 0, r = -1;
    uint32_t need = 48 * 1024;          /* header, sections, an 88x88 or 128x80 cover: one read */
    uint8_t *data = NULL;
    if (c->builtin) {
        r = bm_cover_peek(c->builtin, c->size, &need, &rgba, &w, &h);
    } else {
        for (int tries = 0; tries < 3; tries++) {
            size_t len;
            free(data);
            data = NULL;
            if (fat_load_part(&c->fe, need, &data, &len) != 0)
                break;
            r = bm_cover_peek(data, len, &need, &rgba, &w, &h);
            if (r || len >= c->size)
                break;
        }
    }
    if (fiber_cancelled()) {            /* stopped half way: read it next time */
        free(data);
        return;
    }
    g16_sheet_t s = { 0 };
    if (r == 1)
        menu_load_cover(&s, rgba, w, h);
    free(data);
    if (!s.px)
        menu_make_cover(&s, c->title, kind_of(c));
    c->cover = s;
    c->cover_read = 1;
}

/* the next cover to read: the ones of the tab shown, nearest to the
 * selection first, then the others */
static int next_cover(void)
{
    int best = -1, best_d = 1 << 30;
    if (cover_tab == TAB_GAMES || cover_tab == TAB_DEV) {
        const int dev = cover_tab == TAB_DEV;
        int sel_pos = 0;
        for (int i = 0, pos = 0; i < ncarts; i++)
            if (is_dev(&carts[i]) == dev) {
                if (i == cover_sel)
                    sel_pos = pos;
                pos++;
            }
        for (int i = 0, pos = 0; i < ncarts; i++) {
            if (is_dev(&carts[i]) != dev)
                continue;
            const int d = pos > sel_pos ? pos - sel_pos : sel_pos - pos;
            pos++;
            if (!carts[i].cover_read && d < best_d) {
                best = i;
                best_d = d;
            }
        }
    }
    for (int i = 0; best < 0 && i < ncarts; i++)
        if (!carts[i].cover_read)
            best = i;
    return best;
}

static void covers_main(void *arg)
{
    (void)arg;
    int i;
    while (!fiber_cancelled() && (i = next_cover()) >= 0) {
        read_cover(&carts[i]);
        fiber_slice();
    }
}

static void covers_tick(uint32_t until)
{
    if (!cover_job.busy) {
        if (next_cover() < 0 || fiber_job_start(&cover_job, 16 * 1024, covers_main, NULL) != 0)
            return;
    }
    fiber_job_run(&cover_job, until);
}

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
 * the network is free: Games, Dev and Lib, no panel open (the Market,
 * Settings and the options of a game use it themselves). */
static fiber_job_t report_job;
static int reports_free;                /* the menu is where they may go (carts_menu) */
static int reports_note;                /* the job ended: its word for the menu's note */

static void reports_main(void *arg)
{
    (void)arg;
    reports_send_pending();
    if (!fiber_cancelled())
        reports_note = 1;
}

static void reports_tick(uint32_t until)
{
    if (!report_job.busy &&
        (!reports_auto_due() || fiber_job_start(&report_job, 128 * 1024, reports_main, NULL) != 0))
        return;
    /* the network's waits give the CPU back at once: on until the frame's time is over */
    while (report_job.busy && (int32_t)(until - timer_ticks()) > 0)
        fiber_job_run(&report_job, until);
}

/* the menu's free time in a frame (menu_view_t.idle): the work of the tab
 * shown first */
static void menu_idle(uint32_t until)
{
    /* a file from the PC (bm_net.py --send) first, alone on the SD card:
     * the rest waits for it (2026-10-10) */
    if (netxfer_write_pending()) {
        fiber_job_stop(&cover_job);
        lib_job_stop();
        fiber_job_stop(&report_job);
        netxfer_write_tick(until);
        return;
    }
    if (!reports_free)                  /* before the Market's fiber: one at a time on the network */
        fiber_job_stop(&report_job);    /* (cancelled: it goes again later) */
    if (cover_tab == TAB_LIB) {
        lib_tick(until);
        covers_tick(until);
    } else {
        covers_tick(until);
        market_tick(until);
    }
    if (reports_free)
        reports_tick(until);
}

/* Before the list changes, an application runs or the card is written:
 * the work in the background stops (what it held is freed) and goes on
 * later from where it was. */
static void background_stop(void)
{
    netxfer_write_pause();              /* a file from the PC: again later */
    fiber_job_stop(&cover_job);
    lib_job_stop();
    fiber_job_stop(&report_job);
}

static void rescan(void)
{
    background_stop();
    lib_invalidate();                   /* the Lib tab reads the files again */
    /* SD cartridges by title, then the SDK. The native demo built into
     * the kernel is not a game of the menu (monitor `n`). */
    for (int i = 0; i < ncarts; i++)
        g16_sheet_free(&carts[i].cover);
    ncarts = 0;
    if (sd_ok) {
        scan_dir("/");
        scan_dir("/carts");
        qsort(carts, (size_t)ncarts, sizeof *carts, title_cmp);
    }
    nsd = ncarts;
    /* the tools always come last (up from the first cartridge) */
    if (ncarts < MAX_CARTS)
        add_builtin("editor (built-in)", bm_editor_cart, bm_editor_cart_end);
    if (ncarts < MAX_CARTS)
        add_builtin("sound (built-in)", bm_sound_cart, bm_sound_cart_end);
    if (ncarts < MAX_CARTS)
        add_builtin("studio (built-in)", bm_studio_cart, bm_studio_cart_end);
    if (ncarts < MAX_CARTS)
        add_builtin("animator (built-in)", bm_animator_cart, bm_animator_cart_end);
    if (ncarts < MAX_CARTS)
        add_builtin("mesh (built-in)", bm_mesh_cart, bm_mesh_cart_end);
    if (ncarts < MAX_CARTS)
        add_builtin("pixel (built-in)", bm_pixel_cart, bm_pixel_cart_end);
    for (int i = 0; i < ncarts; i++) {
        cart_t *c = &carts[i];
        if (c->builtin)
            ksnprintf(c->path, sizeof c->path, "%s", c->name);
        else
            ksnprintf(c->path, sizeof c->path, "%s%s%s", c->dir, strcmp(c->dir, "/") ? "/" : "", c->name);
    }                                   /* the covers: covers_tick, in the menu's free time */
    market_carts_changed();
}

void carts_init(void)
{
    sd_ok = 0;
    if (sd_init() != 0) {
        kprintf("sd: %s\n", sd_error());
    } else if (fat_mount() != 0) {
        kprintf("sd: %s card, %lu MiB: %s\n", sd_is_hc() ? "SDHC" : "SD",
                sd_blocks() / 2048, fat_error());
    } else {
        sd_ok = 1;
    }
    ledstate_set(LED_NO_SD, !sd_ok);
    fat_load_tick = sd_tick;            /* the loading screen, the menu's fibers */
    bm_parse_tick = sd_tick;
    rescan();
    if (sd_ok)
        kprintf("sd: %s card (%s), %s; %d cartridges\n", sd_is_hc() ? "SDHC" : "SD",
                sd_controller(), fat_describe(), nsd);
}

int carts_count(void)
{
    return ncarts;
}

static int same_text(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* the SD card cartridge with this path, or -1 */
static int find_path(const char *path)
{
    for (int i = 0; i < nsd; i++)
        if (same_text(carts[i].path, path))
            return i;
    return -1;
}

const char *carts_find_title(const char *title, const char *author)
{
    for (int i = 0; i < nsd; i++)
        if (same_text(carts[i].title, title) && same_text(carts[i].author, author))
            return carts[i].path;
    return NULL;
}

int carts_has_path(const char *path)
{
    return path[0] && find_path(path) >= 0;
}

void carts_list(void)
{
    for (int i = 0; i < ncarts; i++)
        kprintf("  %2d  %-4s %7lu  %s%s%s  \"%s\"\n", i + 1, kind_of(&carts[i]),
                carts[i].size, carts[i].dir, carts[i].dir[0] && strcmp(carts[i].dir, "/") ? "/" : "",
                carts[i].name, carts[i].title);
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

/* A cartridge from the menu: .bm ones can be left suspended. Returns
 * BM_SUSPENDED if it was. */
static int run_buffer(framebuffer_t *fb, const uint8_t *data, size_t len, int suspendable)
{
    if (len >= 8 && bm_is_cart(data)) {
        bm_stats_t st;
        int r = bm_run(fb, data, len, PLAY_SECS, &st, suspendable);
        bm_print_stats(&st);
        perf_line(&st);
        return r;
    }
    bm_close_suspended();
    carts_play_buffer(fb, data, len);
    return BM_ENDED;
}

void carts_play_buffer(framebuffer_t *fb, const uint8_t *data, size_t len)
{
    if (len >= 8 && bm_is_cart(data)) {
        bm_stats_t st;
        bm_play(fb, data, len, PLAY_SECS, &st);
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

/* a project (.bme): in the Dev tab, A opens it in the SDK */
static int is_project(const cart_t *c)
{
    return !c->builtin && bm_is_project(c->name);
}

/* a development tool built into the kernel (the Dev tab) */
static int is_dev(const cart_t *c)
{
    return c->builtin == bm_editor_cart || c->builtin == bm_sound_cart || c->builtin == bm_studio_cart ||
           c->builtin == bm_animator_cart || c->builtin == bm_mesh_cart || c->builtin == bm_pixel_cart;
}

/* The tools a tool can open on its file with cart_tool(name, path) (bm
 * Studio's "Open in bm Animator" and back). */
static const struct {
    const char *name, *what;
    const uint8_t *start, *end;
} tools[] = {
    { "studio", "bm Studio", bm_studio_cart, bm_studio_cart_end },
    { "animator", "bm Animator", bm_animator_cart, bm_animator_cart_end },
    { "mesh", "bm Mesh", bm_mesh_cart, bm_mesh_cart_end },
    { "pixel", "bm Pixel", bm_pixel_cart, bm_pixel_cart_end },
    { "code", "bm Code", bm_code_cart, bm_code_cart_end },
    { "sdk", "SDK", bm_editor_cart, bm_editor_cart_end },
    { "sound", "Sound editor", bm_sound_cart, bm_sound_cart_end },
};

/* A development tool built into the kernel (the SDK, the Sound editor,
 * bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel), and the games it
 * tries: when the tool asks to play a file (cart_run), play it, then open
 * the tool again on that file with the error the game stopped with, if
 * any; when it asks for another tool (cart_tool), that one opens on the
 * file. `open`: a file to start on (the menu's "Open in ..."), or NULL.
 * Returns the name of the last tool (for the menu's "last: ..."). */
const char *carts_tool_session(framebuffer_t *fb, const uint8_t *cart, size_t cart_len, const char *what,
                               const char *open)
{
    char path[64] = "", err[512] = "", tool[16], from[16] = "";
    int back = 0, tried = 0;
    bm_stats_t st;
    if (open)
        ksnprintf(path, sizeof path, "%s", open);
    for (;;) {
        crumb(what, NULL);
        bm_set_arg(path[0] ? path : NULL, err[0] ? err : NULL);
        bm_set_arg_back(back);
        bm_set_arg_run(tried ? &st : NULL);     /* the dev kit's numbers of the game tried */
        bm_set_arg_from(from[0] ? from : NULL);
        tried = 0;
        bm_set_tool(1);                 /* the tool saves where it is told */
        bm_play(fb, cart, cart_len, PLAY_SECS, &st);
        bm_set_tool(0);                 /* the games it tries do not */
        bm_set_arg(NULL, NULL);
        bm_set_arg_run(NULL);
        bm_set_arg_from(NULL);
        if (bm_take_tool(tool, sizeof tool)) {
            /* the tool that asks: the next one finds it in cart_arg().from */
            from[0] = 0;
            for (unsigned k = 0; k < sizeof tools / sizeof tools[0]; k++)
                if (!strcmp(tools[k].what, what))
                    ksnprintf(from, sizeof from, "%s", tools[k].name);
            unsigned i = 0;
            while (i < sizeof tools / sizeof tools[0] && strcmp(tools[i].name, tool))
                i++;
            path[0] = 0;
            bm_take_run(path, sizeof path);
            if (i == sizeof tools / sizeof tools[0])
                break;
            cart = tools[i].start;
            cart_len = (size_t)(tools[i].end - tools[i].start);
            what = tools[i].what;
            back = 0;
            err[0] = 0;
            continue;
        }
        if (!bm_take_run(path, sizeof path))
            break;
        back = 1;
        err[0] = 0;
        fat_entry_t e;
        uint8_t *data;
        size_t len;
        if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
            ksnprintf(err, sizeof err, "cannot read %s: %s", path, fat_error());
            continue;
        }
        crumb("trying", path);
        bm_play(fb, data, len, PLAY_SECS, &st);
        bm_print_stats(&st);
        tried = st.frames > 0;
        free(data);
        ksnprintf(err, sizeof err, "%s", bm_last_error());
    }
    return what;
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

static void play(framebuffer_t *fb, const cart_t *c)
{
    if (susp_path[0] && strcmp(susp_path, c->path) == 0 && bm_suspended(NULL, 0)) {
        kprintf("\nresuming %s\n", c->name);
        crumb("playing", c->title[0] ? c->title : c->name);
        bm_stats_t st;
        int r = bm_resume(fb, PLAY_SECS, &st);
        bm_print_stats(&st);
        perf_line(&st);
        if (r != BM_SUSPENDED)
            susp_path[0] = 0;
        ksnprintf(last_msg, sizeof last_msg, "last: %s%s", c->name, perf_msg);
        crumb("cartridge menu", NULL);
        return;
    }
    bm_close_suspended();              /* the menu asked first */
    susp_path[0] = 0;
    if (is_dev(c)) {
        const char *w;
        if (c->builtin == bm_editor_cart)
            w = carts_editor(fb);
        else if (c->builtin == bm_sound_cart)
            w = carts_sound_editor(fb);
        else if (c->builtin == bm_mesh_cart)
            w = carts_mesh(fb);
        else if (c->builtin == bm_pixel_cart)
            w = carts_pixel(fb);
        else if (c->builtin == bm_animator_cart)
            w = carts_animator(fb);
        else
            w = carts_studio(fb);
        ksnprintf(last_msg, sizeof last_msg, "last: %s", w);
        crumb("cartridge menu", NULL);
        rescan();                       /* it may have saved new files */
        return;
    }
    kprintf("\nplaying %s\n", c->name);
    perf_msg[0] = 0;
    crumb("playing", c->title[0] ? c->title : c->name);
    if (c->builtin) {
        if (run_buffer(fb, c->builtin, c->size, 1) == BM_SUSPENDED)
            ksnprintf(susp_path, sizeof susp_path, "%s", c->path);
        ksnprintf(last_msg, sizeof last_msg, "last: %s%s", c->name, perf_msg);
        crumb("cartridge menu", NULL);
        return;
    }
    uint8_t *data;
    size_t len;
    if (fat_load(&c->fe, &data, &len) != 0) {
        loading_stop();
        kprintf("\x1b[91mcannot read %s: %s\x1b[0m\n", c->name, fat_error());
        ksnprintf(last_msg, sizeof last_msg, "cannot read %s: %s", c->name, fat_error());
        return;
    }
    if (run_buffer(fb, data, len, 1) == BM_SUSPENDED)
        ksnprintf(susp_path, sizeof susp_path, "%s", c->path);
    free(data);
    ksnprintf(last_msg, sizeof last_msg, "last: %s%s", c->name, perf_msg);
    crumb("cartridge menu", NULL);
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
    for (int i = 0; n && i < nsd; i++) {
        const cart_t *c = &carts[i];
        if (!is_dev(c) && (lower_prefix(c->title, name, n) || lower_prefix(c->name, name, n))) {
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
    if (sd_ok)
        outf("\x1b[90m   SD: %s\x1b[0m", fat_describe());
    else
        outf("\x1b[90m   SD: %s\x1b[0m", sd_error());
    out("\n\n");
    for (int i = top; i < ncarts && i < top + rows; i++) {
        const cart_t *c = &carts[i];
        char title[39], author[19];
        ksnprintf(title, sizeof title, "%s", c->title);
        ksnprintf(author, sizeof author, "%s", c->author);
        /* SGR 7 swaps the colours: the selected row gets it once, no
         * other attribute inside */
        outf("%s %c %-38s %s%-18s%s %-3s %5lu KiB \x1b[0m\n",
             i == sel ? "\x1b[7m" : "", i == sel ? '>' : ' ', title,
             i == sel ? "" : "\x1b[90m", author, i == sel ? "" : "\x1b[0m",
             "bm", (c->size + 1023) / 1024);
    }
    for (int i = ncarts - top; i < rows; i++)
        out("\n");
    if (sel < ncarts) {
        const cart_t *c = &carts[sel];
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
    int n = 0;
    if (tab == TAB_LIB)                 /* Lib: a list of its own */
        return 0;
    for (int i = 0; i < ncarts; i++)
        if (is_dev(&carts[i]) == (tab == TAB_DEV) && !is_project(&carts[i]))
            idx[n++] = i;
    if (tab == TAB_DEV) {
        for (int t = 0; t < home_tools() && n < MAX_CARTS + 16; t++)
            idx[n++] = -1 - t;
        for (int i = 0; i < ncarts && n < MAX_CARTS + 16; i++)     /* the projects, after the tools */
            if (is_project(&carts[i]))
                idx[n++] = i;
    }
    return n;
}

static int is_suspended(const cart_t *c)
{
    return susp_path[0] && strcmp(susp_path, c->path) == 0 && bm_suspended(NULL, 0);
}

/* The options of a cartridge (X on its cover): a panel like the settings. */
enum { C_PLAY = 100, C_CLOSE, C_SDK, C_SOUND, C_STUDIO, C_AUTHOR, C_FILE, C_SIZE, C_TYPE, C_SAVE,
       C_DEL_SAVE, C_DELETE, C_CODE, C_MESH, C_PIXEL, C_ANIMATOR, C_PUBLISH, C_SEND, C_COPY, C_BUILD };

static int opt_cart;            /* the cartridge of the HOME_CART panel */
static int opt_market;          /* the game of the HOME_MARKET panel */
static long opt_save;           /* its save slots: bytes in all, -1 if none */
static int opt_slots;           /* how many slots it uses */

static long save_size(const cart_t *c)
{
    long bytes = -1;
    opt_slots = 0;
    for (int slot = 1; sd_ok && c->save[0] && slot <= BM_SAVE_SLOTS; slot++) {
        char path[sizeof c->save];
        fat_entry_t e;
        bm_save_slot(c->save, slot, path, sizeof path);
        if (fat_find(path, &e) == 0) {
            bytes = (bytes < 0 ? 0 : bytes) + (long)e.size;
            opt_slots++;
        }
    }
    return bytes;
}

/* every slot of the cartridge: 0, or -1 and the first error in note */
static int save_delete(const cart_t *c, char *note, size_t n)
{
    int err = 0;
    for (int slot = 1; c->save[0] && slot <= BM_SAVE_SLOTS; slot++) {
        char path[sizeof c->save];
        fat_entry_t e;
        bm_save_slot(c->save, slot, path, sizeof path);
        if (fat_find(path, &e) == 0 && fat_delete(path) != 0 && !err) {
            ksnprintf(note, n, "cannot delete %s: %s", path, fat_error());
            err = -1;
        }
    }
    return err;
}

static void cart_panel(home_panel_t *p)
{
    const cart_t *c = &carts[opt_cart];
    memset(p, 0, sizeof *p);
    ksnprintf(p->title, sizeof p->title, "%s", c->title);
    int susp = is_suspended(c), proj = is_project(c);
    home_row(p, MENU_ROW_ACTION, C_PLAY, susp ? "Resume" : proj ? "Try it" : "Play",
             susp ? "Back to the point where the game was left" : proj ? "Plays the project as it is saved"
                                                                     : "Start the game", NULL);
    if (susp)
        home_row(p, MENU_ROW_ACTION, C_CLOSE, "Close the game",
                 "Ends the suspended game: what was not saved is lost", NULL);
    if (!c->builtin) {
        home_row(p, MENU_ROW_ACTION, C_SDK, "Open in the SDK",
                 "Code, sprites and map of this cartridge", NULL);
        home_row(p, MENU_ROW_ACTION, C_CODE, "Open in bm Code",
                 "Its code in tabs, small font; sprites, map and sounds stay as they are", NULL);
        home_row(p, MENU_ROW_ACTION, C_SOUND, "Open in the Sound editor",
                 "Sounds, sound effects and music of this cartridge", NULL);
        home_row(p, MENU_ROW_ACTION, C_STUDIO, "Open in bm Studio",
                 "Its 3D models: build them with tiles and blocks, choose, move, paint", NULL);
        home_row(p, MENU_ROW_ACTION, C_ANIMATOR, "Open in bm Animator",
                 "Its models' skeletons and animations: play, rig, animate, sprites", NULL);
        home_row(p, MENU_ROW_ACTION, C_MESH, "Open in bm Mesh",
                 "Its meshes, also those its code builds: vertices, faces; to models or to code", NULL);
        home_row(p, MENU_ROW_ACTION, C_PIXEL, "Open in bm Pixel",
                 "Its sprite sheet: pixel art, palette, animation; the rest stays as it is", NULL);
        if (proj) {
            home_row(p, MENU_ROW_ACTION, C_BUILD, "Build the game (.bm)",
                     "Its game, in Games: it replaces the last build", NULL);
        } else {
            home_row(p, MENU_ROW_ACTION, C_COPY, "Make an editable copy",
                     "Games are read only: the copy (.bme) goes to Dev", NULL);
            home_row(p, MENU_ROW_ACTION, C_PUBLISH, "Publish to the Market",
                     "A pull request with your GitHub token: everyone can get it", NULL);
        }
        home_row(p, MENU_ROW_ACTION, C_SEND, "Send to a nearby console",
                 "To a console on this network with the Market tab open", NULL);
    }
    home_row(p, MENU_ROW_INFO, C_AUTHOR, "Author", "From the cartridge header",
             "%s", c->author[0] ? c->author : "-");
    home_row(p, MENU_ROW_INFO, C_FILE, "File", c->builtin ? "Built into the kernel" : "On the SD card",
             "%s", c->path);
    home_row(p, MENU_ROW_INFO, C_SIZE, "Size", "The whole cartridge",
             "%lu KiB", (c->size + 1023) / 1024);
    home_row(p, MENU_ROW_INFO, C_TYPE, "Type", proj ? "A project: the tools change it" : "Lua 5.4 on bm", "%s",
             proj ? "bme (project)" : !strcmp(kind_of(c), "b16") ? "b16 (handheld)" : "bm (native)");
    char v[40];
    if (opt_save >= 0 && opt_slots > 1)
        ksnprintf(v, sizeof v, "%ld bytes in %d slots", opt_save, opt_slots);
    else if (opt_save >= 0)
        ksnprintf(v, sizeof v, "%ld bytes", opt_save);
    else
        ksnprintf(v, sizeof v, "none");
    home_row(p, MENU_ROW_INFO, C_SAVE, "Save data", c->save[0] ? c->save : "-", "%s", v);
    if (opt_save >= 0)
        home_row(p, MENU_ROW_ACTION, C_DEL_SAVE, "Delete the save data",
                 "Records and progress start again", NULL);
    if (!c->builtin)
        home_row(p, MENU_ROW_ACTION, C_DELETE, "Delete from the SD card",
                 "Removes the file: it cannot be undone", NULL);
}

/* The rows of the options that stay in the menu (play and the SDK are
 * started by the menu itself). */
static void cart_act(int row, int how, home_do_t *d)
{
    cart_t *c = &carts[opt_cart];
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    switch (row) {
    case C_CLOSE:
        bm_close_suspended();
        susp_path[0] = 0;
        ksnprintf(d->note, sizeof d->note, "closed %s", c->name);
        break;
    case C_COPY:
    case C_BUILD: {
        char to[96], err[64];
        background_stop();
        int r = row == C_COPY ? bm_copy_name(c->path, to, sizeof to) : 0;
        if (r != 0)
            ksnprintf(err, sizeof err, "no free name");
        else if (row == C_COPY)
            r = bm_make_copy(c->path, to, err, sizeof err);
        else
            r = bm_build(c->path, to, sizeof to, err, sizeof err);
        if (r == 0) {
            const char *slash = strrchr(to, '/');
            ksnprintf(d->note, sizeof d->note, row == C_COPY ? "%s is in Dev" : "%s is in Games",
                      slash ? slash + 1 : to);
            kprintf("menu: %s %s to %s\n", row == C_COPY ? "copied" : "built", c->path, to);
            d->what = HOME_BACK;            /* the menu reads the SD card again */
        } else {
            ksnprintf(d->note, sizeof d->note, "%s: %s", row == C_COPY ? "cannot copy" : "cannot build", err);
            kprintf("menu: %s\n", d->note);
        }
        break;
    }
    case C_PUBLISH:
        publish_setup(c->path, c->title, c->author);
        d->what = HOME_OPEN;
        d->panel = HOME_PUBLISH;
        break;
    case C_SEND:
        market_send_setup(c->path, c->title, c->author);
        d->what = HOME_OPEN;
        d->panel = HOME_SEND;
        break;
    case C_DEL_SAVE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete the save data?");
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "Records and progress start again.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES) {
            if (save_delete(c, d->note, sizeof d->note) == 0)
                ksnprintf(d->note, sizeof d->note, "save data deleted");
            kprintf("menu: %s\n", d->note);
            opt_save = save_size(c);
        }
        break;
    case C_DELETE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete %s?", c->title);
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "The file leaves the SD card for good.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES && netxfer_updating(c->path)) {
            ksnprintf(d->note, sizeof d->note, "Updating, wait for the end of the download");
        } else if (how == HOME_YES) {
            if (is_suspended(c)) {
                bm_close_suspended();
                susp_path[0] = 0;
            }
            char path[sizeof c->path];
            ksnprintf(path, sizeof path, "%s", c->path);
            background_stop();          /* no read of it left half way */
            if (fat_delete(path) == 0) {
                ksnprintf(d->note, sizeof d->note, "deleted %s", path);
                d->what = HOME_BACK;
            } else {
                ksnprintf(d->note, sizeof d->note, "cannot delete %s: %s", path, fat_error());
            }
            kprintf("menu: %s\n", d->note);
        }
        break;
    }
}

/* ---------------------------------------------------------------- the Lib tab */

#define LIB_ROWS_MAX 2400
#define LIB_SOURCES_SHOWN 160

static int lib_group;                   /* the group shown */
static int lib_sel[LIB_GROUPS], lib_top[LIB_GROUPS];
static menu_lib_row_t lib_rows[LIB_ROWS_MAX];
static int lib_row_item[LIB_ROWS_MAX];  /* the item of a row, -1 for a file's row */
static char lib_counts[LIB_SOURCES_SHOWN][8];
static int lib_nrows;

/* the rows of the group: a row for each file that has some, then its items
 * (lib.c adds the items file after file: one pass) */
static void lib_build(void)
{
    lib_nrows = 0;
    int nfiles = 0, head = -1, count = 0, src = -1;
    for (int i = 0; i < lib_items() && lib_nrows < LIB_ROWS_MAX; i++) {
        const lib_item_t *it = lib_item(i);
        if (it->group != lib_group)
            continue;
        if (it->source != src) {
            if (head >= 0)
                ksnprintf((char *)lib_rows[head].value, 8, "%d", count);
            head = -1;
            count = 0;
            src = it->source;
            if (nfiles >= LIB_SOURCES_SHOWN || lib_nrows + 1 >= LIB_ROWS_MAX)
                break;
            head = lib_nrows++;
            lib_rows[head] = (menu_lib_row_t){ lib_source(src)->file, lib_counts[nfiles], 1 };
            lib_row_item[head] = -1;
            nfiles++;
        }
        lib_rows[lib_nrows] = (menu_lib_row_t){ it->name, it->tag, 0 };
        lib_row_item[lib_nrows++] = i;
        count++;
    }
    if (head >= 0)
        ksnprintf((char *)lib_rows[head].value, 8, "%d", count);
}

/* the selected row moved by dy items (never onto a file's row) */
static void lib_move(int dy)
{
    int *sel = &lib_sel[lib_group], *top = &lib_top[lib_group];
    if (*sel >= lib_nrows) *sel = lib_nrows - 1;
    if (*sel < 0) *sel = 0;
    int step = dy < 0 ? -1 : 1;
    for (int k = dy ? (dy < 0 ? -dy : dy) : 0; k > 0; k--) {
        int to = *sel + step;
        while (to >= 0 && to < lib_nrows && lib_row_item[to] < 0)
            to += step;
        if (to < 0 || to >= lib_nrows)
            break;
        *sel = to;
    }
    while (*sel < lib_nrows && lib_row_item[*sel] < 0)     /* never on a file's row */
        (*sel)++;
    if (*sel >= lib_nrows) *sel = lib_nrows - 1;
    /* the file's row above the first item shows too */
    int want = *sel > 0 && lib_row_item[*sel - 1] < 0 ? *sel - 1 : *sel;
    if (want < *top) *top = want;
    if (*sel >= *top + MENU_LIB_ROWS) *top = *sel - MENU_LIB_ROWS + 1;
    if (*top < 0) *top = 0;
}

static const lib_item_t *lib_current(void)
{
    int r = lib_sel[lib_group];
    return r >= 0 && r < lib_nrows && lib_row_item[r] >= 0 ? lib_item(lib_row_item[r]) : NULL;
}

enum { ASK_NONE, ASK_SWITCH, ASK_PANEL, ASK_MARKET, ASK_OFFER };
enum { GO_NONE, GO_PLAY, GO_SDK, GO_SOUND, GO_CODE, GO_STUDIO, GO_ANIMATOR, GO_MESH, GO_PIXEL, GO_TEXT, GO_UPLOAD,
       GO_NETPLAY };

#define DEPTH_MAX 4

/* what the network console is told takes its keys (netcon_focus) */
static const char FOCUS_MENU[] = "the menu (w a s d, Enter, Esc, 1-5 the tabs; q the monitor, "
                                 "or a line for it: :gpu; b3d; send)";
static const char FOCUS_APP[] = "the application (Ctrl-\\ back to the menu)";
static const char FOCUS_PAGE[] = "a page of the menu (Esc back)";

void carts_menu(framebuffer_t *fb)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int list_rows = (int)rows - 7;
    if (list_rows < 3)
        list_rows = 3;
    static const char *const tabs[] = { "Market", "Games", "Dev", "Lib" };
    /* the Lib tab is off for now (the user's decision, 2026-10-06):
     * lib_tab=1 in bm/config.txt shows it again; L1 / R1 and Tab go past it */
    const char *lib_key = config_get("lib_tab");
    const int lib_on = lib_key && lib_key[0] == '1';
    /* the tabs: Market, Games, Dev, Lib (docs/RISORSE.md) and Settings,
     * whose panel opens when it is the tab (on_gear); L1 / R1 move between
     * them. Games comes first. */
    int tab = TAB_GAMES, on_gear = 0, tsel[4] = { 0, 0, 0, 0 };
    int lib_still = 0, lib_last = -1, lib_info = 0;     /* the Lib tab: the selection at rest, its INFO shown */
    char lib_lines[5][48], lib_path[64] = "", lib_name[16] = "";
    int sel = 0, top = 0, redraw = 1, esc = 0;
    uint32_t prev_btn = hid_buttons(), repeat_at = 0;

    kprintf("\ncartridge menu: arrows or wasd, Enter plays, x options, [ ] or Tab or 1-5 change tab, q returns to the monitor\n"
            "(keyboard: Enter, Esc back, C options, Q E or 1-5 tabs, Ctrl+Esc home, Ctrl+Shift+Esc the monitor, hold F12: the keys)\n");
    input_flush();
    static menu_item_t items[MAX_CARTS + 16 + CATALOG_MAX];
    static int idx[MAX_CARTS + 16];
    static home_panel_t pb;
    struct { int id, sel, top; } stack[DEPTH_MAX];
    int depth = 0, built = -1, frame = 0;
    char details[128], susp_title[49];
    int ask = ASK_NONE, ask_cart = -1, ask_go = GO_NONE, ask_row = 0;
    char ask_q[64] = "", ask_d[64] = "", ask_y[16] = "";
    int gfx = menu_ui_open(fb) == 0;         /* else the text menu */
    if (gfx)
        home_init();
    for (;;) {
        netcon_focus(FOCUS_MENU, NULL);
        /* the Market works only while its tab is shown */
        int on_market = gfx && tab == TAB_MARKET && !on_gear;
        market_set_active(on_market);
        /* nearby consoles: with the Market, or while choosing one to send to;
         * a game they offer is a question */
        market_lan(on_market || (gfx && depth && stack[depth - 1].id == HOME_SEND));
        if (ask == ASK_NONE && market_offer(ask_q, sizeof ask_q, ask_d, sizeof ask_d)) {
            ksnprintf(ask_y, sizeof ask_y, "Accept");
            ask = ASK_OFFER;
        } else if (ask == ASK_OFFER) {
            char q_[64], d_[64];
            if (!market_offer(q_, sizeof q_, d_, sizeof d_))
                ask = ASK_NONE;                 /* the sender gave up */
        }
        int n = tab == TAB_MARKET ? (gfx ? market_items(items, (int)(sizeof items / sizeof *items)) : 0)
                                  : tab_items(tab, idx);
        if (tsel[tab] >= n) tsel[tab] = n ? n - 1 : 0;
        if (tab == TAB_MARKET)
            market_select(tsel[tab]);
        frame++;

        /* the panel on top: rebuilt when it changes, and twice a second
         * for the values that move (pads, uptime) */
        menu_panel_t mp;
        int rebuilt = 0;
        if (depth) {
            int id = stack[depth - 1].id;
            if (built != id || frame % 30 == 0) {
                rebuilt = 1;
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
                built = id;
            }
            int *ps = &stack[depth - 1].sel, *pt = &stack[depth - 1].top;
            if (*ps >= pb.n) *ps = pb.n ? pb.n - 1 : 0;
            if (*ps < *pt) *pt = *ps;
            if (*ps >= *pt + MENU_PANEL_ROWS) *pt = *ps - MENU_PANEL_ROWS + 1;
            mp = (menu_panel_t){ pb.title, pb.rows, pb.n, *ps, *pt, pb.n ? pb.help[*ps] : NULL };
        }

        if (gfx) {
            for (int i = 0; i < n && tab != TAB_MARKET; i++) {
                if (idx[i] < 0) {
                    int t = -1 - idx[i];
                    items[i] = (menu_item_t){ .title = home_tool_title(t), .author = "", .path = "",
                                              .kind = "tool", .cover = home_tool_cover(t) };
                    continue;
                }
                const cart_t *c = &carts[idx[i]];
                items[i] = (menu_item_t){ .title = c->title, .author = c->author, .path = c->path, .kind = kind_of(c),
                                          .size = c->size, .cover = c->cover.px ? &c->cover : NULL,
                                          .loading = !c->cover_read, .running = is_suspended(c),
                                          .badge = is_project(c) ? "Project" : NULL };
                /* a file from the PC on its way (bm_net.py --send) */
                const int up = netxfer_updating(c->path);
                if (up) {
                    items[i].badge = up == NETXFER_QUEUED ? "Queued" : "Updating";
                    items[i].running = 0;
                }
            }
            /* the covers still to read: the tab's, from the selection */
            cover_tab = on_gear ? TAB_SETTINGS : tab;
            reports_free = !on_gear && !depth && ask == ASK_NONE && tab != TAB_MARKET;
            if (reports_note) {                 /* the reports sent in the background */
                reports_note = 0;
                ksnprintf(last_msg, sizeof last_msg, "report %s", reports_last());
            }
            cover_sel = (tab == TAB_GAMES || tab == TAB_DEV) && n && idx[tsel[tab]] >= 0 ? idx[tsel[tab]] : -1;
            details[0] = 0;
            if (tab == TAB_MARKET) {
                market_details(tsel[tab], details, sizeof details);
            } else if (n && idx[tsel[tab]] < 0) {
                ksnprintf(details, sizeof details, "%s", home_tool_about(-1 - idx[tsel[tab]]));
            } else if (n) {
                const cart_t *c = &carts[idx[tsel[tab]]];
                ksnprintf(details, sizeof details, "%s   %s   %lu KiB   %s",
                          c->author[0] ? c->author : "-", kind_of(c),
                          (c->size + 1023) / 1024, c->path);
            }
            const char *a_label = NULL;
            if (tab == TAB_MARKET) {
                a_label = market_action_label(tsel[tab]);
                if (!a_label)
                    a_label = "";
            }
            menu_view_t v = {
                .tabs = tabs, .ntabs = lib_on ? 4 : 3, .tab = tab, .on_gear = on_gear, .peek_first = 1,
                .items = items, .n = n, .sel = tsel[tab],
                .details = details, .note = on_market ? market_status() : last_msg,
                .panel = depth ? &mp : NULL,
                .banner = on_market ? market_banner() : NULL,
                .a_label = a_label, .idle = menu_idle,
            };
            /* Settings is a page: the sections on the left, the rows of the
             * one chosen (or under the selection, as a preview) on the right */
            static home_panel_t pside;
            static int side_id;
            menu_page_t page;
            menu_panel_t sp, rp;
            if (on_gear && depth && stack[0].id == HOME_SETTINGS) {
                int want = depth == 1 ? (pb.n ? home_sub_panel(pb.ids[stack[0].sel]) : 0) : HOME_SETTINGS;
                if (want && (rebuilt || side_id != want)) {
                    home_panel(want, &pside);
                    side_id = want;
                }
                if (depth == 1) {
                    sp = mp;
                    rp = (menu_panel_t){ pside.title, pside.rows, pside.n, -1, 0, NULL };
                    page = (menu_page_t){ &sp, want ? &rp : NULL, 0 };
                } else {
                    sp = (menu_panel_t){ pside.title, pside.rows, pside.n, stack[0].sel, stack[0].top, NULL };
                    rp = mp;
                    page = (menu_page_t){ &sp, &rp, 1 };
                }
                v.page = &page;
                v.panel = NULL;
            }
            menu_lib_t lv;
            if (tab == TAB_LIB && !on_gear) {
                /* the Lib tab: the files are read in its fiber (lib_tick,
                 * the menu's free time), the menu goes on meanwhile */
                if (!lib_ready()) {
                    lib_last = -2;
                } else if (lib_last == -2) {
                    lib_build();
                    lib_move(0);
                    lib_last = -1;
                }
                const lib_item_t *it = lib_ready() ? lib_current() : NULL;
                int key = it ? (int)(it - lib_item(0)) : -1;
                lib_still = key == lib_last ? lib_still + 1 : 0;
                if (lib_last != -2)
                    lib_last = key;
                if (lib_still == 0) {
                    lib_details(it, lib_lines, 0);
                    lib_info = 0;
                } else if (lib_still >= 8 && !lib_info && lib_preview_ready(it)) {
                    lib_details(it, lib_lines, 1);  /* the INFO lines once its file is open */
                    lib_info = 1;
                }
                static char reading[48];
                int done_, total_;
                lib_progress(&done_, &total_);
                if (total_)
                    ksnprintf(reading, sizeof reading, "reading the SD card: %d/%d", done_, total_);
                else
                    ksnprintf(reading, sizeof reading, "reading the SD card...");
                const lib_source_t *src = it ? lib_source(it->source) : NULL;
                static const char *const open_in[LIB_GROUPS] = {
                    "Open in bm Studio", "Open in bm Pixel", "Open in Sound", "Open in SDK", "Open in bm Pixel", NULL };
                lv = (menu_lib_t){
                    .groups = lib_groups, .ngroups = LIB_GROUPS, .group = lib_group,
                    .rows = lib_rows, .n = lib_ready() ? lib_nrows : 0,
                    .sel = lib_sel[lib_group], .top = lib_top[lib_group],
                    .empty = !lib_ready() ? reading : sd_ok ? "nothing of this kind yet" : "no SD card",
                    .open = src && src->kind == BM_RES_CART ? open_in[lib_group] : NULL,
                    .play = lib_play_label(it),
                    /* the preview once the selection rests (its file is read) */
                    .preview = it && lib_still >= 8 ? lib_preview : NULL,
                    .ctx = (void *)it,
                };
                for (int i = 0; i < 5; i++)
                    lv.lines[i] = it ? lib_lines[i] : NULL;
                v.lib = &lv;
                v.details = NULL;
            } else {
                lib_stop();                     /* a sound of the Lib tab ends with it */
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
            if (ask == ASK_SWITCH && bm_suspended(susp_title, sizeof susp_title)) {
                ksnprintf(ask_q, sizeof ask_q, "Close %s?", susp_title);
                v.ask = ask_q;
                v.ask_detail = "It is suspended: what was not saved is lost.";
                v.ask_yes = "Close it";
            } else if (ask == ASK_PANEL || ask == ASK_MARKET || ask == ASK_OFFER) {
                v.ask = ask_q;
                v.ask_detail = ask_d;
                v.ask_yes = ask_y;
            }
            menu_ui_frame(fb, &v);
        } else {
            if (sel < top) top = sel;
            if (sel >= top + list_rows) top = sel - list_rows + 1;
            if (redraw) {
                draw(sel, top, list_rows);
                redraw = 0;
            }
        }

        /* a file from the PC goes on even when the frame left no time for
         * it (menu_idle), and in the text list too: a piece a frame */
        if (netxfer_write_pending())
            menu_idle(timer_ticks() + 2000);

        int dx = 0, dy = 0, action = 0, quit = 0, back = 0, opts = 0, ybtn = 0;
        int cur = on_gear ? TAB_SETTINGS : tab, tabto = -1;    /* the tab to go to */

        /* serial */
        int to_line = 0;
        for (int k; !to_line && (k = input_remote_getc()) >= 0; ) {
            char c = (char)k;
            if (esc == 1) { esc = c == '[' ? 2 : 0; continue; }
            if (esc == 2) {
                esc = 0;
                if (c == 'A') dy--;
                if (c == 'B') dy++;
                if (c == 'C') dx++;
                if (c == 'D') dx--;
                continue;
            }
            switch (c) {
            case 0x1B:                          /* an arrow key, or Esc alone: back */
                if (input_remote_follows()) esc = 1; else back = 1;
                break;
            case 'w': case 'W': case 'k': dy--; break;
            case 's': case 'S': case 'j': dy++; break;
            case 'a': case 'A': case 'h': dx--; break;
            case 'd': case 'D': case 'l': dx++; break;
            case '\t': tabto = (cur + 1) % (TAB_SETTINGS + 1); break;
            case '1': tabto = TAB_MARKET; break;
            case '2': tabto = TAB_GAMES; break;
            case '3': tabto = TAB_DEV; break;
            case '4': tabto = lib_on ? TAB_LIB : -1; break;
            case '5': tabto = TAB_SETTINGS; break;
            case '[': tabto = cur > 0 ? cur - 1 : -1; break;       /* L1 */
            case ']': tabto = cur < TAB_SETTINGS ? cur + 1 : -1; break;   /* R1 */
            case 'v': case 'V': ybtn = 1; break;                  /* Y */
            case 'x': case 'X': opts = 1; break;
            case 0x7F: case 0x08: back = 1; break;
            case '\r': case '\n': case ' ': action = 1; break;
            case 'q': case 'Q': quit = HID_QUIT_MONITOR; break;
            case 'r': case 'R': action = 2; break;
            case 'U': action = 3; break;         /* bm_load.py --cart */
            case ':':                           /* a monitor line (":gpu; b3d; send"), from */
                input_unget(':');               /* wherever the menu is: the monitor reads */
                to_line = 1;                    /* the rest of it */
                break;
            }
        }
        if (to_line)
            break;

        /* USB keyboard / gamepads: edges plus auto repeat */
        const uint32_t DIRS = HID_UP | HID_DOWN | HID_LEFT | HID_RIGHT;
        uint32_t b = input_buttons(&quit);
        uint32_t pressed = b & ~prev_btn;
        uint32_t now = timer_ticks();
        uint32_t dirs = 0;
        if (pressed & DIRS) {
            dirs = pressed & DIRS;
            repeat_at = now + 400000;
        } else if ((b & DIRS) && (int32_t)(now - repeat_at) >= 0) {
            dirs = b & DIRS;
            repeat_at = now + 110000;
        }
        if (dirs & HID_UP) dy--;
        if (dirs & HID_DOWN) dy++;
        if (dirs & HID_LEFT) dx--;
        if (dirs & HID_RIGHT) dx++;
        if ((pressed & (HID_A | HID_START)) && !(b & HID_SELECT))
            action = 1;
        if (pressed & HID_B)
            back = 1;
        if (pressed & HID_X)
            opts = 1;
        if (pressed & HID_Y)
            ybtn = 1;
        if ((pressed & HID_L1) && cur > 0)
            tabto = cur - 1;
        if ((pressed & HID_R1) && cur < TAB_SETTINGS)
            tabto = cur + 1;
        prev_btn = b;
        for (int k; (k = hid_getc()) >= 0;) {  /* text keys from the USB keyboard */
            if (k == 'r' || k == 'R')
                action = 2;
            if (k == '\t')
                tabto = (cur + 1) % (TAB_SETTINGS + 1);
            if (k >= '1' && k <= '0' + TAB_SETTINGS + 1)    /* the tabs by number, as on the serial line */
                tabto = k - '1' == TAB_LIB && !lib_on ? -1 : k - '1';
        }
        /* PS (and Ctrl+Esc, the system's keys) is home: Games, with every
         * panel and question closed (never the monitor). Esc alone goes back
         * like B; Start+Select, Ctrl+Shift+Esc and q from the serial port go
         * back a level too, and from the grid to the monitor. */
        if (dx || dy)
            pointer_hide();                     /* the keys move the selection: no arrow */

        /* the pointer (M32): moving over a cover or a row selects it, the
         * left button does what A does there (or what is clicked: a tab, a
         * button of the hints), the right one goes back, or opens the
         * options of a cover; outside a panel or a question it closes them;
         * the wheel moves by rows */
        const pointer_t *pt = pointer_update();
        if (gfx && pt->shown) {
            menu_hit_t h = menu_ui_hit(pt->x, pt->y);
            int click = pt->pressed & 1, right = pt->pressed & 2;
            dy -= pt->wheel;
            if (ask != ASK_NONE) {
                if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
                    action = 1;
                else if (click && h.kind == MENU_HIT_BUTTON)
                    back = 1;
                else if ((click && h.kind != MENU_HIT_ASK) || right)
                    back = 1;
            } else if (depth) {
                int *ps_ = &stack[depth - 1].sel;
                if (h.kind == MENU_HIT_ROW && h.index < pb.n && (pt->moved || click)) {
                    *ps_ = h.index;
                    action = click ? 1 : action;
                } else if (click && h.kind == MENU_HIT_SECTION && on_gear) {
                    depth = 1;                  /* another section of the Settings page */
                    built = -1;
                    stack[0].sel = h.index;
                    action = 1;
                } else if (click && h.kind == MENU_HIT_BUTTON) {
                    action = h.index == 'A' ? 1 : action;
                    back = h.index == 'B';
                } else if (click && h.kind == MENU_HIT_TAB) {
                    tabto = h.index;
                } else if (click && h.kind == MENU_HIT_SETTINGS) {
                    tabto = TAB_SETTINGS;
                } else if ((click && h.kind != MENU_HIT_PANEL && h.kind != MENU_HIT_ROW) || right) {
                    back = 1;
                }
            } else {
                if (h.kind == MENU_HIT_COVER && h.index < n && ((pt->moved && h.full) || click || right))
                    tsel[tab] = h.index;
                if (h.kind == MENU_HIT_LIB && h.index < lib_nrows && (pt->moved || click))
                    lib_sel[lib_group] = h.index;
                if (click && h.kind == MENU_HIT_COVER && h.full)
                    action = 1;
                else if (click && h.kind == MENU_HIT_LIB)
                    action = 1;
                else if (click && h.kind == MENU_HIT_GROUP)
                    dx = h.index - lib_group;
                else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'Y')
                    ybtn = 1;
                else if (click && h.kind == MENU_HIT_TAB)
                    tabto = h.index;
                else if (click && h.kind == MENU_HIT_SETTINGS)
                    tabto = TAB_SETTINGS;
                else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
                    action = 1;
                else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'X')
                    opts = 1;
                else if (right && h.kind == MENU_HIT_COVER)
                    opts = 1;
            }
        }

        int ps = quit & HID_QUIT_PS;
        if (((quit & HID_QUIT_KEY) && !(quit & HID_QUIT_MONITOR)) || (quit & HID_QUIT_ESC))
            back = 1;
        quit = (quit & HID_QUIT_MONITOR) != 0;
        if (ps)
            tabto = TAB_GAMES;

        int go = GO_NONE, go_cart = -1, go_wait = 0, leave = 0;
        void (*go_text)(framebuffer_t *) = NULL;
        home_do_t d;
        d.what = -1;

        if (tabto == TAB_LIB && !lib_on)    /* past the Lib, off for now */
            tabto = cur == TAB_DEV ? TAB_SETTINGS : cur == TAB_SETTINGS ? TAB_DEV : -1;
        /* another tab: whatever panel is open closes (a question waits for
         * its answer, except for PS); Settings opens its panel */
        if (gfx && tabto >= 0 && (ask == ASK_NONE || ps)) {
            ask = ASK_NONE;
            depth = 0;
            built = -1;
            on_gear = tabto == TAB_SETTINGS;
            if (on_gear) {
                stack[0].id = HOME_SETTINGS;
                stack[0].sel = stack[0].top = 0;
                depth = 1;
            } else {
                tab = tabto;
            }
            dx = dy = back = opts = ybtn = 0;
            if (action == 1)
                action = 0;
        }

        if (ask != ASK_NONE) {
            /* A (Enter) says yes, B / q / Esc cancel */
            int yes = action == 1, no = quit || back;
            quit = back = opts = 0;
            action = 0;
            dx = dy = 0;
            if (no) {
                if (ask == ASK_OFFER)
                    market_offer_answer(0);
                ask = ASK_NONE;
            } else if (yes && ask == ASK_OFFER) {
                ask = ASK_NONE;
                market_offer_answer(1);
            } else if (yes && ask == ASK_SWITCH && ask_cart >= 0 && netxfer_updating(carts[ask_cart].path)) {
                ask = ASK_NONE;                 /* the game stays frozen: nothing to start yet */
                ksnprintf(last_msg, sizeof last_msg, "Updating, wait for the end of the download");
            } else if (yes && ask == ASK_SWITCH) {
                ask = ASK_NONE;
                bm_close_suspended();
                susp_path[0] = 0;
                go = ask_go;
                go_cart = ask_cart;
            } else if (yes && ask == ASK_MARKET) {
                ask = ASK_NONE;
                market_get(ask_cart);
            } else if (yes && ask == ASK_PANEL) {
                ask = ASK_NONE;
                int id = stack[depth - 1].id;
                if (id == HOME_CART) {
                    cart_act(ask_row, HOME_YES, &d);
                } else if (id == HOME_MARKET) {
                    int c = find_path(market_path(opt_market));
                    if (ask_row == M_DELETE && c >= 0 && is_suspended(&carts[c])) {
                        bm_close_suspended();
                        susp_path[0] = 0;
                    }
                    market_act(opt_market, ask_row, HOME_YES, &d);
                } else if (id == HOME_PUBLISH) {
                    publish_act(ask_row, HOME_YES, &d);
                } else if (id == HOME_SEND) {
                    market_send_act(ask_row, HOME_YES, &d);
                } else {
                    home_act(id, ask_row, HOME_YES, &d);
                }
            }
        } else if (depth && gfx) {
            /* a panel: up/down choose, A does, left/right change a value,
             * B / Esc / q go back one level */
            int *ps = &stack[depth - 1].sel, id = stack[depth - 1].id;
            if (dy && pb.n)
                *ps = (*ps + dy + pb.n) % pb.n;
            const menu_row_t *r = pb.n ? &pb.rows[*ps] : NULL;
            int row = pb.n ? pb.ids[*ps] : 0;
            /* the Settings page: right opens the section, left goes back to
             * the sections (but changes a value on a choice) */
            if (on_gear && depth == 1 && dx > 0 && r && r->kind == MENU_ROW_SUB)
                action = 1;
            else if (on_gear && depth == 2 && dx < 0 && (!r || r->kind != MENU_ROW_CHOICE))
                back = 1;
            if (quit || back) {
                depth--;
                built = -1;
            } else if (r && id == HOME_CART && action == 1 &&
                       (row == C_PLAY || row == C_SDK || row == C_SOUND || row == C_CODE || row == C_STUDIO ||
                        row == C_ANIMATOR || row == C_MESH || row == C_PIXEL)) {
                const cart_t *c = &carts[opt_cart];
                int g = row == C_PLAY ? GO_PLAY : row == C_SDK ? GO_SDK : row == C_SOUND ? GO_SOUND :
                        row == C_CODE ? GO_CODE : row == C_MESH ? GO_MESH : row == C_PIXEL ? GO_PIXEL :
                        row == C_ANIMATOR ? GO_ANIMATOR : GO_STUDIO;
                if (bm_suspended(NULL, 0) && !(g == GO_PLAY && is_suspended(c))) {
                    ask = ASK_SWITCH;           /* another game is frozen: ask first */
                    ask_go = g;
                    ask_cart = opt_cart;
                } else {
                    go = g;
                    go_cart = opt_cart;
                }
            } else if (r && id == HOME_MARKET && action == 1 && row == M_PLAY) {
                int c = find_path(market_path(opt_market));
                if (c < 0) {
                    ksnprintf(last_msg, sizeof last_msg, "%s is not on the SD card", market_path(opt_market));
                } else if (bm_suspended(NULL, 0) && !is_suspended(&carts[c])) {
                    ask = ASK_SWITCH;
                    ask_go = GO_PLAY;
                    ask_cart = c;
                } else {
                    go = GO_PLAY;
                    go_cart = c;
                }
            } else if (r && ((action == 1 && r->kind != MENU_ROW_INFO) ||
                             (dx && r->kind == MENU_ROW_CHOICE))) {
                if (id == HOME_CART)
                    cart_act(row, action == 1 ? 0 : dx, &d);
                else if (id == HOME_MARKET)
                    market_act(opt_market, row, action == 1 ? 0 : dx, &d);
                else if (id == HOME_PUBLISH)
                    publish_act(row, action == 1 ? 0 : dx, &d);
                else if (id == HOME_SEND)
                    market_send_act(row, action == 1 ? 0 : dx, &d);
                else
                    home_act(id, row, action == 1 ? 0 : dx, &d);
                ask_row = row;
            }
            quit = 0;
            action = action >= 3 ? action : 0;
            dx = dy = opts = 0;
        }

        /* what a row asked for */
        if (d.what >= 0) {
            if (d.note[0])
                ksnprintf(last_msg, sizeof last_msg, "%s", d.note);
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
                if (depth && stack[depth - 1].id == HOME_CART) {
                    depth = 0;                  /* the cartridge is gone */
                    rescan();
                } else if (depth) {
                    depth--;
                }
                break;
            case HOME_ASK:
                ask = ASK_PANEL;
                ksnprintf(ask_q, sizeof ask_q, "%s", d.ask);
                ksnprintf(ask_d, sizeof ask_d, "%s", d.ask_detail);
                ksnprintf(ask_y, sizeof ask_y, "%s", d.ask_yes);
                break;
            case HOME_TEXT:
                go = GO_TEXT;
                go_text = d.text;
                go_wait = d.wait;
                break;
            case HOME_MONITOR:
                leave = 1;
                break;
            }
        }

        /* Settings is the tab only while its panel is open: B out of it
         * goes back to the tab before */
        if (on_gear && (!depth || stack[0].id != HOME_SETTINGS))
            on_gear = 0;
        if (quit || leave)
            break;
        if (gfx && !depth && ask == ASK_NONE && tab == TAB_LIB && !on_gear) {
            /* Lib: left/right the group, up/down the list, A opens the
             * file of the resource in the app of its group */
            if (dx && lib_ready()) {
                lib_group = ((lib_group + dx) % LIB_GROUPS + LIB_GROUPS) % LIB_GROUPS;
                lib_build();
                lib_move(0);
            } else if (lib_ready()) {
                lib_move(dy);
            }
            const lib_item_t *it = lib_ready() ? lib_current() : NULL;
            const lib_source_t *src = it ? lib_source(it->source) : NULL;
            if (action == 1 && src && src->kind == BM_RES_CART && lib_group != LIB_KITS) {
                static const int go_of[LIB_GROUPS] = { GO_STUDIO, GO_PIXEL, GO_SOUND, GO_SDK, GO_PIXEL, GO_NONE };
                int g = go_of[lib_group];
                if (bm_suspended(NULL, 0)) {
                    ksnprintf(last_msg, sizeof last_msg, "close the game that is playing first");
                } else {
                    ksnprintf(lib_path, sizeof lib_path, "%s", src->path);
                    ksnprintf(lib_name, sizeof lib_name, "%s", src->file);
                    go = g;
                    go_cart = -1;
                }
            } else if (action == 1 && src) {
                ksnprintf(last_msg, sizeof last_msg, "a resource file: copy it into a game");
            }
            if (ybtn && it)
                lib_play(it);
            action = (action == 2 || action >= 3) ? action : 0;
            dx = dy = opts = ybtn = 0;
        }
        if (gfx && !depth && ask == ASK_NONE) {
            if (n) {
                /* up and down along the rows (not into the tab bar: L1 / R1
                 * change the tab), left/right along the covers, wrapping */
                int s_ = tsel[tab];
                if (dy) {
                    int cols = menu_ui_cols();
                    int to = s_ + dy * cols;
                    if (to >= n && dy > 0)      /* the last row may be shorter */
                        to = (to / cols) * cols < n ? n - 1 : s_;
                    if (to >= 0 && to < n) s_ = to;
                }
                if (dx && s_ + dx >= 0 && s_ + dx < n)
                    s_ += dx;
                tsel[tab] = s_;
            }
            /* X: the options of the highlighted cartridge, or of the
             * Market's game */
            if (opts && !on_gear && tab == TAB_MARKET && market_action(tsel[tab]) != MARKET_NONE) {
                opt_market = tsel[tab];
                stack[0].id = HOME_MARKET;
                stack[0].sel = stack[0].top = 0;
                depth = 1;
                built = -1;
            } else if (opts && !on_gear && tab != TAB_MARKET && n && idx[tsel[tab]] >= 0) {
                opt_cart = idx[tsel[tab]];
                opt_save = save_size(&carts[opt_cart]);
                stack[0].id = HOME_CART;
                stack[0].sel = stack[0].top = 0;
                depth = 1;
                built = -1;
            }
        } else if (!gfx && (dx || dy)) {
            sel = ((sel + dx + dy) % ncarts + ncarts) % ncarts;
            redraw = 1;
        }
        /* from the network (bm_net.py): a file saved on the SD card
         * shows up in the list, a cartridge sent to play starts */
        static unsigned seen_saves;
        uint8_t *net_buf = NULL;
        size_t net_len = 0;
        /* a new copy of the game frozen in memory arrived: the old one
         * never comes back (the new one is written next) */
        if (susp_path[0] && netxfer_updating(susp_path) >= NETXFER_QUEUED && bm_suspended(NULL, 0)) {
            kprintf("menu: %s closed, its new copy from the network replaces it\n", susp_path);
            bm_close_suspended();
            susp_path[0] = 0;
        }
        if (netxfer_saves() != seen_saves) {
            seen_saves = netxfer_saves();
            if (!action)
                action = 2;
        }
        if (!action && netxfer_take_play(&net_buf, &net_len))
            action = 4;
        /* a game of the Market installed or deleted: the SD card again */
        if (market_take_changed()) {
            rescan();
            if (depth && stack[0].id == HOME_CART)
                depth = 0;                      /* indices changed */
        }

        if (action == 2) {
            carts_init();
            if (sel >= ncarts) sel = 0;
            depth = 0;                          /* indices changed */
            ask = ASK_NONE;
            redraw = 1;
        } else if (action == 3) {
            go = GO_UPLOAD;
        } else if (action == 4) {
            go = GO_NETPLAY;
        } else if (action == 1 && !on_gear && gfx && tab == TAB_MARKET) {
            /* A on a game of the Market: get it (after a question), or play it */
            int i = tsel[tab], a = market_action(i);
            if (a == MARKET_GET || a == MARKET_UPDATE) {
                home_do_t q;
                market_ask(i, &q);
                ksnprintf(ask_q, sizeof ask_q, "%s", q.ask);
                ksnprintf(ask_d, sizeof ask_d, "%s", q.ask_detail);
                ksnprintf(ask_y, sizeof ask_y, "%s", q.ask_yes);
                ask = ASK_MARKET;
                ask_cart = i;
            } else if (a == MARKET_PLAY) {
                int c = find_path(market_path(i));
                if (c < 0) {
                    ksnprintf(last_msg, sizeof last_msg, "%s is not on the SD card", market_path(i));
                } else if (bm_suspended(NULL, 0) && !is_suspended(&carts[c])) {
                    ask = ASK_SWITCH;
                    ask_go = GO_PLAY;
                    ask_cart = c;
                } else {
                    go = GO_PLAY;
                    go_cart = c;
                }
            }
        } else if (action == 1 && !on_gear && (!gfx || n > 0)) {
            /* A plays the highlighted cover, also from the tab bar; if
             * another game is suspended, ask first */
            int i = gfx ? idx[tsel[tab]] : sel;
            if (i < 0) {
                home_tool_start(-1 - i, &d);
                if (d.what == HOME_MONITOR)
                    break;
                go = GO_TEXT;
                go_text = d.text;
                go_wait = d.wait;
            } else if (gfx && bm_suspended(NULL, 0) && !is_suspended(&carts[i])) {
                ask = ASK_SWITCH;
                ask_go = is_project(&carts[i]) ? GO_SDK : GO_PLAY;
                ask_cart = i;
            } else {
                go = is_project(&carts[i]) ? GO_SDK : GO_PLAY;     /* a project opens in the SDK */
                go_cart = i;
            }
        }

        if (go != GO_NONE && go_cart >= 0 && netxfer_updating(carts[go_cart].path)) {
            ksnprintf(last_msg, sizeof last_msg, "Updating, wait for the end of the download");
            go = GO_NONE;
        }
        if (go != GO_NONE) {
            ask = ASK_NONE;
            market_set_active(0);               /* nothing loads behind a game */
            market_lan(0);
            background_stop();                  /* the covers, the Lib tab: later */
            lib_stop();
            /* an application (a game, a tool): the system's splash while it
             * loads, never the log (2026-10-04); a suspended game comes back
             * at once */
            const int app = go >= GO_PLAY && go <= GO_PIXEL;
            const int resume = go == GO_PLAY && go_cart >= 0 && susp_path[0] &&
                               strcmp(susp_path, carts[go_cart].path) == 0 && bm_suspended(NULL, 0);
            if (gfx && app)
                menu_ui_close_quiet(fb);
            else if (gfx)
                menu_ui_close(fb);
            if (app && !resume)
                loading_begin(fb);
            /* the file of a tool: a cartridge of the menu, or one of the Lib tab */
            const char *gpath = go_cart >= 0 ? carts[go_cart].path : lib_path;
            const char *gname = go_cart >= 0 ? carts[go_cart].name : lib_name;
            netcon_focus(go == GO_TEXT ? FOCUS_PAGE : FOCUS_APP, NULL);
            switch (go) {
            case GO_PLAY:
                play(fb, &carts[go_cart]);
                depth = 0;
                break;
            case GO_SDK:
            case GO_SOUND:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s",
                          editor_session(fb, gpath, go == GO_SDK ? bm_editor_cart : bm_sound_cart,
                                         go == GO_SDK ? bm_editor_cart_end : bm_sound_cart_end),
                          gname);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();                       /* it may have saved new files */
                break;
            case GO_CODE:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", carts_code(fb, gpath),
                          gname);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_STUDIO:
            case GO_ANIMATOR:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s",
                          go == GO_STUDIO ? studio_session(fb, gpath)
                                          : animator_session(fb, gpath),
                          gname);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_MESH:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", mesh_session(fb, gpath),
                          gname);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_PIXEL:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", pixel_session(fb, gpath),
                          gname);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_TEXT:
                go_text(fb);
                if (go_wait)
                    home_wait_back();
                crumb("cartridge menu", NULL);
                break;
            case GO_UPLOAD:
                upload_and_play(fb);
                depth = 0;
                break;
            case GO_NETPLAY:
                carts_play_buffer(fb, net_buf, net_len);
                free(net_buf);
                depth = 0;
                break;
            }
            loading_stop();                     /* an application that never got to its first frame */
            input_flush();
            prev_btn = hid_buttons();
            redraw = 1;
            built = -1;
            if (gfx)
                gfx = menu_ui_open(fb) == 0;
        }
        if (!gfx)
            timer_delay_us(2000);
    }
    market_set_active(0);
    market_lan(0);
    if (gfx)
        menu_ui_close(fb);
    console_clear();
    kprintf("back to the monitor\n");
}
