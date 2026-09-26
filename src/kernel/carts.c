/*
 * Cartridge list (built-in + SD card) and the on-screen menu.
 */
#include "carts.h"
#include "input.h"
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

enum { KIND_S32, KIND_B33 };

typedef struct {
    char name[FAT_NAME_MAX];
    char dir[8];                /* "" for built-in, "/" or "/carts" */
    int kind;
    const uint8_t *builtin;     /* NULL: file on SD */
    uint32_t size;
    fat_entry_t fe;
} cart_t;

static cart_t carts[MAX_CARTS];
static int ncarts, sd_ok;
static char last_msg[80];

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

static void add_builtin(const char *name, int kind, const uint8_t *start, const uint8_t *end)
{
    cart_t *c = &carts[ncarts++];
    memset(c, 0, sizeof *c);
    strcpy(c->name, name);
    c->kind = kind;
    c->builtin = start;
    c->size = (uint32_t)(end - start);
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
    }
}

static void rescan(void)
{
    ncarts = 0;
    add_builtin("demo.b33 (built-in)", KIND_B33, b33_demo_cart, b33_demo_cart_end);
    add_builtin("demo.cart (built-in)", KIND_S32, s32_demo_cart, s32_demo_cart_end);
    if (sd_ok) {
        scan_dir("/");
        scan_dir("/carts");
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
        kprintf("sd: %s card, %s; %d cartridges\n", sd_is_hc() ? "SDHC" : "SD",
                fat_describe(), ncarts - 2);
}

int carts_count(void)
{
    return ncarts;
}

void carts_list(void)
{
    for (int i = 0; i < ncarts; i++)
        kprintf("  %2d  %-4s %7lu  %s%s%s\n", i + 1, carts[i].kind == KIND_B33 ? "b33" : "s32",
                carts[i].size, carts[i].dir, carts[i].dir[0] && strcmp(carts[i].dir, "/") ? "/" : "",
                carts[i].name);
}

void carts_play_buffer(framebuffer_t *fb, const uint8_t *data, size_t len)
{
    if (len >= 8 && memcmp(data, "BM33CART", 8) == 0) {
        b33_stats_t st;
        b33_play(fb, data, len, PLAY_SECS, &st);
        b33_print_stats(&st);
    } else if (len >= 8 && memcmp(data, "S32CART1", 8) == 0) {
        s32_play_stats_t st;
        s32_play(fb, data, len, PLAY_SECS, 0, &st);
        s32_play_print(&st);
    } else {
        kprintf("unknown cartridge format\n");
    }
}

static void play(framebuffer_t *fb, const cart_t *c)
{
    kprintf("\nplaying %s\n", c->name);
    if (c->builtin) {
        carts_play_buffer(fb, c->builtin, c->size);
        ksnprintf(last_msg, sizeof last_msg, "last: %s", c->name);
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
    ksnprintf(last_msg, sizeof last_msg, "last: %s", c->name);
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
        char name[57];
        ksnprintf(name, sizeof name, "%s", c->name);
        outf("%s %c %-56s %-4s %5lu KiB \x1b[0m\n", i == sel ? "\x1b[7m" : "",
             i == sel ? '>' : ' ', name, c->kind == KIND_B33 ? "b33" : "s32",
             (c->size + 1023) / 1024);
    }
    for (int i = ncarts - top; i < rows; i++)
        out("\n");
    outf("\n\x1b[90m %s\x1b[0m\n", last_msg[0] ? last_msg : "");
    out("\x1b[93m up/down choose   Enter/A play   Esc or Start+Select: monitor   R rescan SD\x1b[0m");
}

void carts_menu(framebuffer_t *fb)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int list_rows = (int)rows - 6;
    if (list_rows < 3)
        list_rows = 3;
    int sel = 0, top = 0, redraw = 1, esc = 0;
    uint32_t prev_btn = hid_buttons(), repeat_at = 0;

    kprintf("\ncartridge menu: w/s or arrows, Enter plays, q returns to the monitor\n");
    input_flush();
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + list_rows) top = sel - list_rows + 1;
        if (redraw) {
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
        } else if (action == 1) {
            play(fb, &carts[sel]);
            input_flush();
            prev_btn = hid_buttons();
            redraw = 1;
        }
        timer_delay_us(2000);
    }
    console_clear();
    kprintf("back to the monitor\n");
}
