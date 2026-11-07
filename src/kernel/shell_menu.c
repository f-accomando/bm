/*
 * The menu of the bm shell. Each item runs a line of commands, as ':' would: the words are rows of
 * the table in monitor.c. The box uses the code page 437 glyphs of the console (0xDA 0xC4 0xBF
 * 0xB3 0xC0 0xD9) and the ANSI subset it understands (console.h).
 */
#include "shell_menu.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"

static const smenu_item_t items[] = {
    { "Command line",    NULL,          0 },
    { "Cartridge menu",  "menu",        0 },
    { "Lua",             "lua",         0 },
    { "Info",            "sys info",    1 },
    { "Log",             "sys dmesg",   0 },
    { "WiFi",            "wifi",        1 },
    { "Pair controller", "bt pair pad", 1 },
    { "Send reports",    "report send", 1 },
    { "3D Bench",        "bench 3d",    0 },
    { "Reboot",          "sys reboot",  0 },
    { "Help",            "help",        0 },
};

const smenu_item_t *shell_menu_items(int *n)
{
    *n = (int)(sizeof items / sizeof items[0]);
    return items;
}

static void at(int row, int col)
{
    char b[32];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

static void rep(char c, int n)
{
    while (n-- > 0)
        console_putc(c);
}

void shell_menu_draw(const smenu_t *m)
{
    static const char title[] = " bm shell ";
    static const char hint[] = "w/s or arrows: move   Enter: run   ':' a line   Esc: cartridge menu";
    uint32_t cols, rows;
    console_size(&cols, &rows);

    int w = (int)strlen(title) + 4;
    for (int i = 0; i < m->n; i++) {
        const int l = (int)strlen(m->items[i].label) + 6;
        if (l > w)
            w = l;
    }
    const int h = m->n + 2;
    int c0 = ((int)cols - w) / 2 + 1;
    if (c0 < 1)
        c0 = 1;
    const int r0 = 2;

    console_write("\x1b[0m\x1b[2J\x1b[H");
    at(r0, c0);
    console_putc((char)0xDA);
    rep((char)0xC4, w - 2);
    console_putc((char)0xBF);
    at(r0, c0 + 2);
    console_write(title);
    for (int i = 0; i < m->n; i++) {
        at(r0 + 1 + i, c0);
        console_putc((char)0xB3);
        if (i == m->sel)
            console_write("\x1b[7m");
        char row[80];
        snprintf(row, sizeof row, " %-*s ", w - 4, m->items[i].label);
        console_write(row);
        console_write("\x1b[0m");
        console_putc((char)0xB3);
    }
    at(r0 + h - 1, c0);
    console_putc((char)0xC0);
    rep((char)0xC4, w - 2);
    console_putc((char)0xD9);
    at(r0 + h + 1, 1);
    console_write(hint);
    at(r0 + h + 3, 1);                          /* the marker of the serial line goes below the box */
}
