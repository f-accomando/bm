/*
 * Cartridge list (built-in + SD card) and the on-screen menu.
 */
#include "carts.h"
#include "crumbs.h"
#include "home.h"
#include "menu_ui.h"
#include "pointer.h"
#include "bm/bm.h"
#include "input.h"
#include "upload.h"
#include "bt/bt.h"
#include "net/net.h"
#include "net/netxfer.h"
#include "bm/runtime.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "usb/hid.h"

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
    char path[FAT_NAME_MAX + 10];
    char save[32];              /* its save file (.bm), "" if none */
} cart_t;

static cart_t carts[MAX_CARTS];
static int ncarts, nsd, sd_ok;
static char last_msg[96];
static char perf_msg[80];          /* speed of the last .bm game */
static char susp_path[FAT_NAME_MAX + 10];   /* the cartridge frozen in memory, "" if none */

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
        if (!ends_with(e.name, ".bm"))
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

/* The cover from the .bm COVER section, or a label with the title. */
static void load_cover(cart_t *c)
{
    bm_cart_t bc;
    char err[8];
    uint8_t *data = NULL;
    size_t len = c->size;
    const uint8_t *d = c->builtin;
    if (!d && fat_load(&c->fe, &data, &len) == 0)
        d = data;
    if (d && bm_parse(d, len, &bc, err, sizeof err) == 0 && bc.cover_rgba)
        menu_load_cover(&c->cover, bc.cover_rgba, bc.cover_w, bc.cover_h);
    free(data);
    if (!c->cover.px)
        menu_make_cover(&c->cover, c->title, "bm");
}

static void rescan(void)
{
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
        load_cover(c);
    }
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
    rescan();
    if (sd_ok)
        kprintf("sd: %s card (%s), %s; %d cartridges\n", sd_is_hc() ? "SDHC" : "SD",
                sd_controller(), fat_describe(), nsd);
}

int carts_count(void)
{
    return ncarts;
}

void carts_list(void)
{
    for (int i = 0; i < ncarts; i++)
        kprintf("  %2d  %-4s %7lu  %s%s%s  \"%s\"\n", i + 1, "bm",
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
    char path[64] = "", err[512] = "", tool[16];
    int back = 0;
    if (open)
        ksnprintf(path, sizeof path, "%s", open);
    for (;;) {
        crumb(what, NULL);
        bm_set_arg(path[0] ? path : NULL, err[0] ? err : NULL);
        bm_set_arg_back(back);
        bm_stats_t st;
        bm_play(fb, cart, cart_len, PLAY_SECS, &st);
        bm_set_arg(NULL, NULL);
        if (bm_take_tool(tool, sizeof tool)) {
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

/* The two tabs of the graphical menu: games, and the development tools
 * (the SDK, then the tools of home.c). `idx` gets what tab `tab` shows: a
 * cartridge index, or -1 - n for tool n. */
static int tab_items(int tab, int *idx)
{
    int n = 0;
    for (int i = 0; i < ncarts; i++)
        if (is_dev(&carts[i]) == (tab == 1))
            idx[n++] = i;
    if (tab == 1)
        for (int t = 0; t < home_tools() && n < MAX_CARTS + 16; t++)
            idx[n++] = -1 - t;
    return n;
}

static int is_suspended(const cart_t *c)
{
    return susp_path[0] && strcmp(susp_path, c->path) == 0 && bm_suspended(NULL, 0);
}

/* The options of a cartridge (X on its cover): a panel like the settings. */
enum { C_PLAY = 100, C_CLOSE, C_SDK, C_SOUND, C_STUDIO, C_AUTHOR, C_FILE, C_SIZE, C_TYPE, C_SAVE,
       C_DEL_SAVE, C_DELETE, C_CODE, C_MESH, C_PIXEL, C_ANIMATOR };

static int opt_cart;            /* the cartridge of the HOME_CART panel */
static long opt_save;           /* its save file: bytes, -1 if none */

static long save_size(const cart_t *c)
{
    fat_entry_t e;
    if (!sd_ok || !c->save[0] || fat_find(c->save, &e) != 0)
        return -1;
    return (long)e.size;
}

static void cart_panel(home_panel_t *p)
{
    const cart_t *c = &carts[opt_cart];
    memset(p, 0, sizeof *p);
    ksnprintf(p->title, sizeof p->title, "%s", c->title);
    int susp = is_suspended(c);
    home_row(p, MENU_ROW_ACTION, C_PLAY, susp ? "Resume" : "Play",
             susp ? "Back to the point where the game was left" : "Start the game", NULL);
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
    }
    home_row(p, MENU_ROW_INFO, C_AUTHOR, "Author", "From the cartridge header",
             "%s", c->author[0] ? c->author : "-");
    home_row(p, MENU_ROW_INFO, C_FILE, "File", c->builtin ? "Built into the kernel" : "On the SD card",
             "%s", c->path);
    home_row(p, MENU_ROW_INFO, C_SIZE, "Size", "The whole cartridge",
             "%lu KiB", (c->size + 1023) / 1024);
    home_row(p, MENU_ROW_INFO, C_TYPE, "Type", "Lua 5.4 on bm", "%s", "bm (native)");
    char v[24];
    if (opt_save >= 0)
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
    case C_DEL_SAVE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete the save data?");
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "Records and progress start again.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES) {
            if (fat_delete(c->save) == 0)
                ksnprintf(d->note, sizeof d->note, "save data deleted");
            else
                ksnprintf(d->note, sizeof d->note, "cannot delete %s: %s", c->save, fat_error());
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
        } else if (how == HOME_YES) {
            if (is_suspended(c)) {
                bm_close_suspended();
                susp_path[0] = 0;
            }
            char path[sizeof c->path];
            ksnprintf(path, sizeof path, "%s", c->path);
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

enum { ASK_NONE, ASK_SWITCH, ASK_PANEL };
enum { GO_NONE, GO_PLAY, GO_SDK, GO_SOUND, GO_CODE, GO_STUDIO, GO_ANIMATOR, GO_MESH, GO_PIXEL, GO_TEXT, GO_UPLOAD,
       GO_NETPLAY };

#define DEPTH_MAX 4

void carts_menu(framebuffer_t *fb)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int list_rows = (int)rows - 7;
    if (list_rows < 3)
        list_rows = 3;
    static const char *const tabs[] = { "Games", "Dev" };
    /* the tabs: Games (0), Dev (1) and Settings (2), whose panel opens
     * when it is the tab (on_gear); L1 / R1 move between them */
    int tab = 0, on_gear = 0, tsel[2] = { 0, 0 };
    int sel = 0, top = 0, redraw = 1, esc = 0;
    uint32_t prev_btn = hid_buttons(), repeat_at = 0;

    kprintf("\ncartridge menu: arrows or wasd, Enter plays, x options, [ ] or Tab or 1 2 3 change tab, q returns to the monitor\n");
    input_flush();
    static menu_item_t items[MAX_CARTS + 16];
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
        int n = tab_items(tab, idx);
        if (tsel[tab] >= n) tsel[tab] = n ? n - 1 : 0;
        frame++;

        /* the panel on top: rebuilt when it changes, and twice a second
         * for the values that move (pads, uptime) */
        menu_panel_t mp;
        if (depth) {
            int id = stack[depth - 1].id;
            if (built != id || frame % 30 == 0) {
                if (id == HOME_CART)
                    cart_panel(&pb);
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
            for (int i = 0; i < n; i++) {
                if (idx[i] < 0) {
                    int t = -1 - idx[i];
                    items[i] = (menu_item_t){ home_tool_title(t), "", "", "tool", 0,
                                              home_tool_cover(t), 0 };
                    continue;
                }
                const cart_t *c = &carts[idx[i]];
                items[i] = (menu_item_t){ c->title, c->author, c->path, "bm",
                                          c->size, c->cover.px ? &c->cover : NULL, is_suspended(c) };
            }
            details[0] = 0;
            if (n && idx[tsel[tab]] < 0) {
                ksnprintf(details, sizeof details, "%s", home_tool_about(-1 - idx[tsel[tab]]));
            } else if (n) {
                const cart_t *c = &carts[idx[tsel[tab]]];
                ksnprintf(details, sizeof details, "%s   %s   %lu KiB   %s",
                          c->author[0] ? c->author : "-", "bm",
                          (c->size + 1023) / 1024, c->path);
            }
            menu_view_t v = {
                .tabs = tabs, .ntabs = 2, .tab = tab, .on_gear = on_gear,
                .items = items, .n = n, .sel = tsel[tab],
                .details = details, .note = last_msg,
                .panel = depth ? &mp : NULL,
            };
            for (int p = 0; p < 4; p++) {
                int d_ = input_device(p), kind = d_ & ~INPUT_DEV_BLUETOOTH;
                v.dev[p] = kind == INPUT_DEV_KEYBOARD ? MENU_DEV_KEYBOARD
                         : kind == INPUT_DEV_PAD ? MENU_DEV_PAD : MENU_DEV_NONE;
                if (d_ & INPUT_DEV_BLUETOOTH)
                    v.bt |= 1u << p;
            }
            v.mice = pointer_devices();
            /* the hints show the buttons of what was pressed last; before
             * that, player 1's keyboard or the DS4 */
            int src = hid_last_source();
            v.prompts = src == HID_SOURCE_KEYBOARD ? MENU_PROMPTS_KEYBOARD
                      : src == HID_SOURCE_PAD ? MENU_PROMPTS_PAD
                      : src == HID_SOURCE_NONE && v.dev[0] == MENU_DEV_KEYBOARD ? MENU_PROMPTS_KEYBOARD
                      : MENU_PROMPTS_DS4;
            v.prompts_colour = home_prompts_colour();
            int link = net_link_kind();
            v.net = link == NET_LINK_ETHERNET ? MENU_NET_ETHERNET
                  : link == NET_LINK_WIFI ? MENU_NET_WIFI : MENU_NET_NONE;
            v.net_wait = !net_ip();
            if (ask == ASK_SWITCH && bm_suspended(susp_title, sizeof susp_title)) {
                ksnprintf(ask_q, sizeof ask_q, "Close %s?", susp_title);
                v.ask = ask_q;
                v.ask_detail = "It is suspended: what was not saved is lost.";
                v.ask_yes = "Close it";
            } else if (ask == ASK_PANEL) {
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

        int dx = 0, dy = 0, action = 0, quit = 0, back = 0, opts = 0;
        int cur = on_gear ? 2 : tab, tabto = -1;    /* the tab to go to */

        /* serial */
        for (int k; (k = input_remote_getc()) >= 0; ) {
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
            case '\t': tabto = (cur + 1) % 3; break;
            case '1': tabto = 0; break;
            case '2': tabto = 1; break;
            case '3': tabto = 2; break;
            case '[': tabto = cur > 0 ? cur - 1 : -1; break;       /* L1 */
            case ']': tabto = cur < 2 ? cur + 1 : -1; break;       /* R1 */
            case 'x': case 'X': opts = 1; break;
            case 0x7F: case 0x08: back = 1; break;
            case '\r': case '\n': case ' ': action = 1; break;
            case 'q': case 'Q': quit = HID_QUIT_MONITOR; break;
            case 'r': case 'R': action = 2; break;
            case 'U': action = 3; break;         /* bm_load.py --cart */
            }
        }

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
        if ((pressed & HID_L1) && cur > 0)
            tabto = cur - 1;
        if ((pressed & HID_R1) && cur < 2)
            tabto = cur + 1;
        prev_btn = b;
        for (int k; (k = hid_getc()) >= 0;) {  /* text keys from the USB keyboard */
            if (k == 'r' || k == 'R')
                action = 2;
            if (k == '\t')
                tabto = (cur + 1) % 3;
        }
        /* PS is home: Games, with every panel and question closed (never
         * the monitor). Esc alone goes back like B; Ctrl+Esc, Start+Select
         * and q from the serial port go back a level too, and from the
         * grid to the monitor (Esc alone did that until 2026-10-01). */
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
                } else if (click && h.kind == MENU_HIT_BUTTON) {
                    action = h.index == 'A' ? 1 : action;
                    back = h.index == 'B';
                } else if (click && h.kind == MENU_HIT_TAB) {
                    tabto = h.index;
                } else if (click && h.kind == MENU_HIT_SETTINGS) {
                    tabto = 2;
                } else if ((click && h.kind != MENU_HIT_PANEL && h.kind != MENU_HIT_ROW) || right) {
                    back = 1;
                }
            } else {
                if (h.kind == MENU_HIT_COVER && h.index < n && ((pt->moved && h.full) || click || right))
                    tsel[tab] = h.index;
                if (click && h.kind == MENU_HIT_COVER && h.full)
                    action = 1;
                else if (click && h.kind == MENU_HIT_TAB)
                    tabto = h.index;
                else if (click && h.kind == MENU_HIT_SETTINGS)
                    tabto = 2;
                else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
                    action = 1;
                else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'X')
                    opts = 1;
                else if (right && h.kind == MENU_HIT_COVER)
                    opts = 1;
            }
        }

        int ps = quit & HID_QUIT_PS;
        if ((quit & HID_QUIT_KEY) && !(quit & HID_QUIT_MONITOR))
            back = 1;
        quit = (quit & HID_QUIT_MONITOR) != 0;
        if (ps)
            tabto = 0;

        int go = GO_NONE, go_cart = -1, go_wait = 0, leave = 0;
        void (*go_text)(framebuffer_t *) = NULL;
        home_do_t d;
        d.what = -1;

        /* another tab: whatever panel is open closes (a question waits for
         * its answer, except for PS); Settings opens its panel */
        if (gfx && tabto >= 0 && (ask == ASK_NONE || ps)) {
            ask = ASK_NONE;
            depth = 0;
            built = -1;
            on_gear = tabto == 2;
            if (on_gear) {
                stack[0].id = HOME_SETTINGS;
                stack[0].sel = stack[0].top = 0;
                depth = 1;
            } else {
                tab = tabto;
            }
            dx = dy = back = opts = 0;
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
                ask = ASK_NONE;
            } else if (yes && ask == ASK_SWITCH) {
                ask = ASK_NONE;
                bm_close_suspended();
                susp_path[0] = 0;
                go = ask_go;
                go_cart = ask_cart;
            } else if (yes && ask == ASK_PANEL) {
                ask = ASK_NONE;
                int id = stack[depth - 1].id;
                if (id == HOME_CART)
                    cart_act(ask_row, HOME_YES, &d);
                else
                    home_act(id, ask_row, HOME_YES, &d);
            }
        } else if (depth && gfx) {
            /* a panel: up/down choose, A does, left/right change a value,
             * B / Esc / q go back one level */
            int *ps = &stack[depth - 1].sel, id = stack[depth - 1].id;
            if (dy && pb.n)
                *ps = (*ps + dy + pb.n) % pb.n;
            const menu_row_t *r = pb.n ? &pb.rows[*ps] : NULL;
            int row = pb.n ? pb.ids[*ps] : 0;
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
            } else if (r && ((action == 1 && r->kind != MENU_ROW_INFO) ||
                             (dx && r->kind == MENU_ROW_CHOICE))) {
                if (id == HOME_CART)
                    cart_act(row, action == 1 ? 0 : dx, &d);
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
        if (gfx && !depth && ask == ASK_NONE) {
            if (n) {
                /* up and down along the rows (not into the tab bar: L1 / R1
                 * change the tab), left/right along the covers, wrapping */
                int s_ = tsel[tab];
                if (dy) {
                    int to = s_ + dy * MENU_COLS;
                    if (to >= n && dy > 0)      /* the last row may be shorter */
                        to = (to / MENU_COLS) * MENU_COLS < n ? n - 1 : s_;
                    if (to >= 0 && to < n) s_ = to;
                }
                if (dx && s_ + dx >= 0 && s_ + dx < n)
                    s_ += dx;
                tsel[tab] = s_;
            }
            /* X: the options of the highlighted cartridge */
            if (opts && !on_gear && n && idx[tsel[tab]] >= 0) {
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
        if (netxfer_saves() != seen_saves) {
            seen_saves = netxfer_saves();
            if (!action)
                action = 2;
        }
        if (!action && netxfer_take_play(&net_buf, &net_len))
            action = 4;

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
                ask_go = GO_PLAY;
                ask_cart = i;
            } else {
                go = GO_PLAY;
                go_cart = i;
            }
        }

        if (go != GO_NONE) {
            ask = ASK_NONE;
            if (gfx)
                menu_ui_close(fb);
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
                          editor_session(fb, carts[go_cart].path, go == GO_SDK ? bm_editor_cart : bm_sound_cart,
                                         go == GO_SDK ? bm_editor_cart_end : bm_sound_cart_end),
                          carts[go_cart].name);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();                       /* it may have saved new files */
                break;
            case GO_CODE:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", carts_code(fb, carts[go_cart].path),
                          carts[go_cart].name);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_STUDIO:
            case GO_ANIMATOR:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s",
                          go == GO_STUDIO ? studio_session(fb, carts[go_cart].path)
                                          : animator_session(fb, carts[go_cart].path),
                          carts[go_cart].name);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_MESH:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", mesh_session(fb, carts[go_cart].path),
                          carts[go_cart].name);
                crumb("cartridge menu", NULL);
                depth = 0;
                rescan();
                break;
            case GO_PIXEL:
                bm_close_suspended();
                susp_path[0] = 0;
                ksnprintf(last_msg, sizeof last_msg, "last: %s on %s", pixel_session(fb, carts[go_cart].path),
                          carts[go_cart].name);
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
    if (gfx)
        menu_ui_close(fb);
    console_clear();
    kprintf("back to the monitor\n");
}
