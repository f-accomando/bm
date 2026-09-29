/*
 * Cartridge list (built-in + SD card) and the on-screen menu.
 */
#include "carts.h"
#include "crumbs.h"
#include "menu3d.h"
#include "b33/b33.h"
#include "input.h"
#include "upload.h"
#include "b33/runtime.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "s32/player.h"
#include "usb/hid.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CARTS   64
#define PLAY_SECS   (24u * 3600u)

extern const uint8_t s32_demo_cart[], s32_demo_cart_end[];
extern const uint8_t b33_demo_cart[], b33_demo_cart_end[];
extern const uint8_t b33_editor_cart[], b33_editor_cart_end[];

enum { KIND_S32, KIND_B33 };

typedef struct {
    char title[49];             /* from the header; the file name if none */
    char author[33];
    char name[FAT_NAME_MAX];
    char dir[8];                /* "" for built-in, "/" or "/carts" */
    int kind;
    const uint8_t *builtin;     /* NULL: file on SD */
    uint32_t size;
    fat_entry_t fe;
    g16_sheet_t cover;          /* printed on the card in the menu (px NULL: none) */
    char path[FAT_NAME_MAX + 10];
} cart_t;

static cart_t carts[MAX_CARTS];
static int ncarts, nsd, sd_ok;
static char last_msg[96];
static char perf_msg[80];          /* speed of the last .b33 game */

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

/* Title and author from the first bytes of the image (.b33 or .cart). */
static void read_header(cart_t *c, const uint8_t *h, uint32_t len)
{
    size_t toff = 0, tlen = 0, aoff = 0, alen = 0;
    if (len >= 128 && memcmp(h, "BM33CART", 8) == 0) {
        toff = 24; tlen = 48; aoff = 72; alen = 32;
    } else if (len >= 124 && memcmp(h, "S32CART1", 8) == 0) {
        toff = 28; tlen = 64; aoff = 92; alen = 32;
    }
    if (tlen) {
        size_t n = tlen < sizeof c->title - 1 ? tlen : sizeof c->title - 1;
        memcpy(c->title, h + toff, n);
        c->title[n] = 0;
        memcpy(c->author, h + aoff, alen < sizeof c->author - 1 ? alen : sizeof c->author - 1);
        c->author[sizeof c->author - 1] = 0;
    }
    for (char *p = c->title; *p; p++)           /* the console font is CP437 */
        if ((unsigned char)*p < 32) *p = ' ';
    for (char *p = c->author; *p; p++)
        if ((unsigned char)*p < 32) *p = ' ';
    if (!c->title[0])
        ksnprintf(c->title, sizeof c->title, "%s", c->name);
}

static void add_builtin(const char *name, int kind, const uint8_t *start, const uint8_t *end)
{
    cart_t *c = &carts[ncarts++];
    memset(c, 0, sizeof *c);
    strcpy(c->name, name);
    c->kind = kind;
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
        int kind = ends_with(e.name, ".b33") ? KIND_B33 : ends_with(e.name, ".cart") ? KIND_S32 : -1;
        if (kind < 0)
            continue;
        cart_t *c = &carts[ncarts++];
        memset(c, 0, sizeof *c);
        memcpy(c->name, e.name, sizeof c->name);
        strcpy(c->dir, path);
        c->kind = kind;
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

/* The cover from the .b33 COVER section, or a label with the title. */
static void load_cover(cart_t *c)
{
    b33_cart_t bc;
    char err[8];
    if (c->kind == KIND_B33) {
        uint8_t *data = NULL;
        size_t len = c->size;
        const uint8_t *d = c->builtin;
        if (!d && fat_load(&c->fe, &data, &len) == 0)
            d = data;
        if (d && b33_parse(d, len, &bc, err, sizeof err) == 0 && bc.cover_rgba)
            menu3d_load_cover(&c->cover, bc.cover_rgba, bc.cover_w, bc.cover_h);
        free(data);
    }
    if (!c->cover.px)
        menu3d_make_cover(&c->cover, c->title, c->kind == KIND_B33 ? "b33" : "s32");
}

static void rescan(void)
{
    /* SD cartridges by title; the built-in ones only when the SD has none */
    for (int i = 0; i < ncarts; i++)
        g16_sheet_free(&carts[i].cover);
    ncarts = 0;
    if (sd_ok) {
        scan_dir("/");
        scan_dir("/carts");
        qsort(carts, (size_t)ncarts, sizeof *carts, title_cmp);
    }
    nsd = ncarts;
    if (!nsd) {
        add_builtin("demo.b33 (built-in)", KIND_B33, b33_demo_cart, b33_demo_cart_end);
        add_builtin("demo.cart (built-in)", KIND_S32, s32_demo_cart, s32_demo_cart_end);
    }
    /* the editor always comes last (up from the first cartridge) */
    if (ncarts < MAX_CARTS) {
        cart_t *c = &carts[ncarts++];
        memset(c, 0, sizeof *c);
        strcpy(c->name, "editor (built-in)");
        c->kind = KIND_B33;
        c->builtin = b33_editor_cart;
        c->size = (uint32_t)(b33_editor_cart_end - b33_editor_cart);
        read_header(c, b33_editor_cart, c->size);
    }
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
        kprintf("  %2d  %-4s %7lu  %s%s%s  \"%s\"\n", i + 1, carts[i].kind == KIND_B33 ? "b33" : "s32",
                carts[i].size, carts[i].dir, carts[i].dir[0] && strcmp(carts[i].dir, "/") ? "/" : "",
                carts[i].name, carts[i].title);
}

void carts_play_buffer(framebuffer_t *fb, const uint8_t *data, size_t len)
{
    if (len >= 8 && memcmp(data, "BM33CART", 8) == 0) {
        b33_stats_t st;
        b33_play(fb, data, len, PLAY_SECS, &st);
        b33_print_stats(&st);
        perf_msg[0] = 0;
        if (st.frames) {
            uint32_t ms = st.elapsed_us / 1000, fps10 = ms ? st.frames * 10000u / ms : 0;
            uint32_t avg = st.cpu_us_total / st.frames;
            ksnprintf(perf_msg, sizeof perf_msg,
                      ", %lu.%lu fps, update+draw %lu.%02lu ms (max %lu.%02lu)",
                      fps10 / 10, fps10 % 10, avg / 1000, avg % 1000 / 10,
                      st.cpu_us_max / 1000, st.cpu_us_max % 1000 / 10);
        }
    } else if (len >= 8 && memcmp(data, "S32CART1", 8) == 0) {
        s32_play_stats_t st;
        s32_play(fb, data, len, PLAY_SECS, 0, &st);
        s32_play_print(&st);
    } else {
        kprintf("unknown cartridge format\n");
    }
}

/* The editor, and the games it tries: when the editor asks to play a
 * file (cart_run), play it, then open the editor again on that file with
 * the error the game stopped with, if any. */
void carts_editor(framebuffer_t *fb)
{
    char path[64] = "", err[512] = "";
    for (;;) {
        crumb("editor", NULL);
        b33_set_arg(path[0] ? path : NULL, err[0] ? err : NULL);
        b33_stats_t st;
        b33_play(fb, b33_editor_cart, (size_t)(b33_editor_cart_end - b33_editor_cart), PLAY_SECS, &st);
        b33_set_arg(NULL, NULL);
        if (!b33_take_run(path, sizeof path))
            break;
        err[0] = 0;
        fat_entry_t e;
        uint8_t *data;
        size_t len;
        if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
            ksnprintf(err, sizeof err, "cannot read %s: %s", path, fat_error());
            continue;
        }
        crumb("editor: trying", path);
        b33_play(fb, data, len, PLAY_SECS, &st);
        b33_print_stats(&st);
        free(data);
        ksnprintf(err, sizeof err, "%s", b33_last_error());
    }
}

static void play(framebuffer_t *fb, const cart_t *c)
{
    if (c->builtin == b33_editor_cart) {
        carts_editor(fb);
        ksnprintf(last_msg, sizeof last_msg, "last: editor");
        crumb("cartridge menu", NULL);
        return;
    }
    kprintf("\nplaying %s\n", c->name);
    perf_msg[0] = 0;
    crumb("playing", c->title[0] ? c->title : c->name);
    if (c->builtin) {
        carts_play_buffer(fb, c->builtin, c->size);
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
    carts_play_buffer(fb, data, len);
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
    out("\x1b[1;96m bm33 - cartridges\x1b[0m");
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
             c->kind == KIND_B33 ? "b33" : "s32", (c->size + 1023) / 1024);
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
    out("\x1b[93m up/down choose   Enter/A play   Esc or Start+Select: monitor   R rescan SD\x1b[0m");
}

void carts_menu(framebuffer_t *fb)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int list_rows = (int)rows - 7;
    if (list_rows < 3)
        list_rows = 3;
    int sel = 0, top = 0, redraw = 1, esc = 0;
    uint32_t prev_btn = hid_buttons(), repeat_at = 0;

    kprintf("\ncartridge menu: w/s or arrows, Enter plays, q returns to the monitor\n");
    input_flush();
    static menu_item_t items[MAX_CARTS];
    char header[96], pads[24];
    int gfx = menu3d_open(fb) == 0;         /* else the text menu */
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + list_rows) top = sel - list_rows + 1;
        if (gfx) {
            for (int i = 0; i < ncarts; i++)
                items[i] = (menu_item_t){ carts[i].title, carts[i].author, carts[i].path,
                                          carts[i].kind == KIND_B33 ? "b33" : "s32", carts[i].size,
                                          carts[i].cover.px ? &carts[i].cover : NULL };
            input_status(pads, sizeof pads);
            ksnprintf(header, sizeof header, "   SD: %s   %s", sd_ok ? fat_describe() : sd_error(), pads);
            menu3d_frame(fb, items, ncarts, sel, header, last_msg);
        } else if (redraw) {
            draw(sel, top, list_rows);
            redraw = 0;
        }

        int move = 0, action = 0, quit = 0;

        /* serial */
        while (uart_rx_ready()) {
            char c = uart_getc();
            if (esc == 1) { esc = c == '[' ? 2 : 0; continue; }
            if (esc == 2) {
                esc = 0;
                if (c == 'A') move--;
                if (c == 'B') move++;
                continue;
            }
            switch (c) {
            case 0x1B: esc = 1; break;
            case 'w': case 'W': case 'k': move--; break;
            case 's': case 'S': case 'j': move++; break;
            case '\r': case '\n': case ' ': action = 1; break;
            case 'q': case 'Q': quit = 1; break;
            case 'r': case 'R': action = 2; break;
            case 'U': action = 3; break;         /* bm33_load.py --cart */
            }
        }

        /* USB keyboard / gamepad: edges plus auto repeat on up/down */
        uint32_t b = input_buttons(&quit);
        uint32_t pressed = b & ~prev_btn;
        uint32_t now = timer_ticks();
        if (pressed & HID_UP) { move = -1; repeat_at = now + 400000; }
        if (pressed & HID_DOWN) { move = 1; repeat_at = now + 400000; }
        if ((b & (HID_UP | HID_DOWN)) && !(pressed & (HID_UP | HID_DOWN)) &&
            (int32_t)(now - repeat_at) >= 0) {
            move = (b & HID_UP) ? -1 : 1;
            repeat_at = now + 90000;
        }
        if ((pressed & (HID_A | HID_START)) && !(b & HID_SELECT))
            action = 1;
        prev_btn = b;
        for (int k; (k = hid_getc()) >= 0;)     /* text keys from the USB keyboard */
            if (k == 'r' || k == 'R')
                action = 2;

        if (quit)
            break;
        if (move) {
            sel = ((sel + move) % ncarts + ncarts) % ncarts;
            redraw = 1;
        }
        if (action == 2) {
            carts_init();
            if (sel >= ncarts) sel = 0;
            redraw = 1;
        } else if (action == 3 || action == 1) {
            if (gfx)
                menu3d_close(fb);
            if (action == 3)
                upload_and_play(fb);
            else
                play(fb, &carts[sel]);
            input_flush();
            prev_btn = hid_buttons();
            redraw = 1;
            if (gfx)
                gfx = menu3d_open(fb) == 0;
        }
        if (!gfx)
            timer_delay_us(2000);
    }
    if (gfx)
        menu3d_close(fb);
    console_clear();
    kprintf("back to the monitor\n");
}
