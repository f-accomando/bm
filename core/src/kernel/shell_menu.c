/*
 * The screen of the bm shell: a frame with the files of the SD card, the items as icons in the middle
 * of its bottom border, and one line under it that tells what the selection does. Each item runs a
 * line of commands, as ':' would: the words are rows of the table in monitor.c. The frame uses the
 * code page 437 glyphs of the console (0xDA 0xC4 0xBF 0xB3 0xC0 0xD9 0xC3) and the ANSI subset it
 * understands (console.h); the icons are the CONSOLE_ICON_* codes of the console.
 */
#include "shell_menu.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"

static const smenu_item_t system_sub[] = {
    { "Info",     "sys info",   0, NULL, 0, 0, "Info: the main facts of the console" },
    { "Log",      "sys dmesg",  0, NULL, 0, 0, "Log: everything printed since boot" },
    { "bm3D",     "sys bm3d",   0, NULL, 0, 0, "bm3D: the techniques of the V3D driver, with version and target" },
    { "3D Bench", "bench 3d",   0, NULL, 0, 0, "3D Bench: the 3D benchmark and its report" },
    { "Reboot",   "sys reboot", 0, NULL, 0, 0, "Reboot: start the console again" },
};

static const smenu_item_t network_sub[] = {
    { "WiFi",            "wifi",        1, NULL, 0, 0, "WiFi: start, scan and join a network" },
    { "Pair controller", "bt pair pad", 1, NULL, 0, 0, "Pair controller: pair a Bluetooth pad" },
    { "Send reports",    "report send", 1, NULL, 0, 0, "Send reports: send the reports in bm/reports" },
};

#define NSUB(a) ((int)(sizeof (a) / sizeof (a)[0]))

static const smenu_item_t items[] = {
    { "bm menu gui",  "menu", 0, NULL, 0, CICON_PC, "bm menu gui: the cartridge menu" },
    { "Command line", NULL,   0, NULL, 0, CICON_TERM, "Command line: the bm: prompt" },
    { "Shell Code",   "code", 0, NULL, 0, CICON_CODE, "Shell Code: edit, compile and run .b, .c and .lua" },
    { "Shell SDK",    "sdk",  0, NULL, 0, CICON_CHIP, "Shell SDK: make NES, SNES and bm cartridges (F1-F5: Project, Code, Sprite, Map, Sounds)" },
    { "System",       NULL,   0, system_sub, NSUB(system_sub), CICON_GEAR, "System: Info, Log, bm3D, 3D Bench, Reboot" },
    { "Network",      NULL,   0, network_sub, NSUB(network_sub), CICON_NET, "Network: WiFi, Pair controller, Send reports" },
    { "Help",         "help", 0, NULL, 0, CICON_HELP, "Help: the list of the commands" },
};

const smenu_item_t *shell_menu_items(int *n)
{
    *n = NSUB(items);
    return items;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int ends(const char *s, const char *ext)
{
    const size_t a = strlen(s), b = strlen(ext);
    if (a <= b)
        return 0;
    for (size_t i = 0; i < b; i++)
        if (lower((unsigned char)s[a - b + i]) != ext[i])
            return 0;
    return 1;
}

int shell_file_kind(const shell_file_t *f)
{
    if (!strcmp(f->name, ".."))
        return SHELL_KIND_UP;
    if (f->is_dir)
        return SHELL_KIND_DIR;
    if (ends(f->name, ".bm") || ends(f->name, ".b16"))
        return SHELL_KIND_CART;
    if (ends(f->name, ".nes"))
        return SHELL_KIND_NES;
    if (ends(f->name, ".sfc") || ends(f->name, ".smc"))
        return SHELL_KIND_SNES;
    return SHELL_KIND_FILE;
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

static void box(int r, int c, int h, int w)
{
    at(r, c);
    console_putc((char)0xDA);
    rep((char)0xC4, w - 2);
    console_putc((char)0xBF);
    for (int i = 1; i < h - 1; i++) {
        at(r + i, c);
        console_putc((char)0xB3);
        at(r + i, c + w - 1);
        console_putc((char)0xB3);
    }
    at(r + h - 1, c);
    console_putc((char)0xC0);
    rep((char)0xC4, w - 2);
    console_putc((char)0xD9);
}

/* The longest label of a list, plus the room for the border and a space each side. */
static int box_width(const smenu_item_t *it, int n, int least)
{
    int w = least;
    for (int i = 0; i < n; i++) {
        const int l = (int)strlen(it[i].label) + 4;
        if (l > w)
            w = l;
    }
    return w;
}

/* One row inside a box: " label" padded. style: the SGR to start it. */
static void item_row(int r, int c, int w, const smenu_item_t *it, const char *style)
{
    char row[80];
    const int iw = w - 2;
    if (iw >= (int)sizeof row)
        return;
    memset(row, ' ', (size_t)iw);
    row[iw] = 0;
    const int l = (int)strlen(it->label);
    memcpy(row + 1, it->label, (size_t)(l < iw - 2 ? l : iw - 2));
    at(r, c + 1);
    console_write(style);
    console_write(row);
    console_write("\x1b[0m");
}

/* The folder on the first row inside the frame, then its entries as a tree, from row r to row last. */
static void file_tree(int r, int last, int cols, const smenu_t *m, const char *dir, const shell_file_t *files)
{
    char line[100];
    at(r, 3);
    console_write("\x1b[93m");
    console_putc(CONSOLE_ICON_FOLDER);
    console_write("\x1b[0m ");
    snprintf(line, sizeof line, "SD:%.80s", dir);
    console_write(line);
    r++;
    const int vis = last - r + 1;
    if (vis < 1 || m->nfiles <= 0)
        return;
    const int top = m->filesel >= vis ? m->filesel - vis + 1 : 0;
    int width = cols - 10;                      /* the border, the branch, the icon, the spaces */
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    for (int i = top; i < m->nfiles && i < top + vis; i++, r++) {
        const shell_file_t *f = &files[i];
        const int sel = m->in_files && i == m->filesel;
        at(r, 3);
        console_write("\x1b[90m");
        console_putc(i == m->nfiles - 1 ? (char)0xC0 : (char)0xC3);   /* the last entry: a corner */
        console_putc((char)0xC4);
        console_write("\x1b[0m");
        console_write(sel ? "\x1b[7m" : f->is_dir ? "\x1b[93m" : "\x1b[96m");
        console_putc(f->is_dir ? CONSOLE_ICON_FOLDER : CONSOLE_ICON_FILE);
        console_write(sel ? " " : "\x1b[0m ");
        snprintf(line, sizeof line, "%.*s", width, f->name);
        console_write(line);
        if (sel)
            console_putc(' ');
        console_write("\x1b[0m");
    }
}

/* A size as bytes, KB or MB (powers of 1024). */
static void size_text(char *out, size_t n, unsigned long size)
{
    if (size < 1024)
        snprintf(out, n, "%lu bytes", size);
    else if (size < 1024ul * 1024)
        snprintf(out, n, "%lu KB", (size + 1023) / 1024);
    else
        snprintf(out, n, "%lu.%lu MB", size / (1024ul * 1024), size % (1024ul * 1024) * 10 / (1024ul * 1024));
}

/* The line under the bar: what Enter does on the selection. */
static void help_text(char *out, size_t n, const smenu_t *m, const shell_file_t *files)
{
    if (m->in_files && files && m->filesel < m->nfiles) {
        const shell_file_t *f = &files[m->filesel];
        char sz[24];
        switch (shell_file_kind(f)) {
        case SHELL_KIND_UP: snprintf(out, n, "..: the folder above (Enter or Left)"); return;
        case SHELL_KIND_DIR: snprintf(out, n, "%.40s: a folder (Enter opens it)", f->name); return;
        case SHELL_KIND_CART: snprintf(out, n, "%.40s: a cartridge (Enter plays it)", f->name); return;
        case SHELL_KIND_NES: snprintf(out, n, "%.40s: a NES ROM (Enter starts bm NES)", f->name); return;
        case SHELL_KIND_SNES: snprintf(out, n, "%.40s: a SNES ROM (Enter starts bm SNES)", f->name); return;
        default:
            size_text(sz, sizeof sz, f->size);
            snprintf(out, n, "%.40s: %s (Enter opens a text file in Shell Code)", f->name, sz);
            return;
        }
    }
    const smenu_item_t *it = smenu_current(m);
    snprintf(out, n, "%s", it->help ? it->help : it->label);
}

void shell_menu_draw(const smenu_t *m, const char *dir, const shell_file_t *files)
{
    static const char title[] = " bm shell ";
    uint32_t ucols, urows;
    console_size(&ucols, &urows);
    const int cols = (int)ucols, rows = (int)urows;
    const int top = 2;                          /* the frame's top border */
    const int bar = rows - 3;                   /* its bottom border: the icons */
    const int help = rows - 2;                  /* the line under it; rows - 1 is the serial marker */

    console_write("\x1b[0m\x1b[2J\x1b[H");
    if (bar - top < 3 || cols < 30)
        return;
    box(top, 1, bar - top + 1, cols);
    at(top, 3);
    console_write(title);
    if (dir && files)
        file_tree(top + 1, bar - 1, cols, m, dir, files);

    /* the icons, in the middle of the bottom border: two cells each, a space between */
    const int width = m->n * 3 + 1;
    const int c0 = (cols - width) / 2 + 1;
    int icon_col = c0 + 1;
    at(bar, c0);
    console_putc(' ');
    for (int i = 0; i < m->n; i++) {
        const int sel = i == m->sel;
        if (sel)
            icon_col = c0 + 1 + 3 * i;
        console_write(!sel ? "\x1b[97m" : m->in_files ? "\x1b[97m" : m->in_sub ? "\x1b[93m" : "\x1b[7m");
        console_putc(CONSOLE_ICON_BIG(m->items[i].icon));
        console_putc((char)(CONSOLE_ICON_BIG(m->items[i].icon) + 1));
        console_write("\x1b[0m ");
    }

    /* an open group: its box above its icon, on the bottom of the frame */
    const smenu_item_t *g = &m->items[m->sel];
    if (m->n > 0 && g->sub && m->in_sub) {
        const int sw = box_width(g->sub, g->nsub, (int)strlen(g->label) + 6);
        const int sh = g->nsub + 2;
        int sc = icon_col - 1;
        if (sc + sw > cols - 1)
            sc = cols - 1 - sw;
        if (sc < 2)
            sc = 2;
        const int sr = bar - sh;
        if (sr > top) {
            box(sr, sc, sh, sw);
            char name[40];
            snprintf(name, sizeof name, " %.30s ", g->label);
            at(sr, sc + 2);
            console_write(name);
            for (int i = 0; i < g->nsub; i++)
                item_row(sr + 1 + i, sc, sw, &g->sub[i], i == m->subsel ? "\x1b[7m" : "\x1b[0m");
        }
    }

    char line[160];
    help_text(line, sizeof line, m, files);
    int l = (int)strlen(line);
    if (l > cols - 2)
        line[l = cols - 2] = 0;
    at(help, (cols - l) / 2 + 1);
    console_write(line);
    at(rows - 1, 1);                            /* the serial marker is printed here and wiped from the screen */
}

/* The system info box: label and value rows in a frame, drawn over a clear screen. */
void shell_info_draw(const char *title, const char (*label)[14], const char (*value)[40], int n)
{
    int w = (int)strlen(title) + 6;
    for (int i = 0; i < n; i++) {
        const int l = 2 + 13 + (int)strlen(value[i]) + 2;
        if (l > w)
            w = l;
    }
    console_write("\x1b[0m\x1b[2J\x1b[H");
    box(2, 1, n + 2, w);
    char t[40];
    snprintf(t, sizeof t, " %.30s ", title);
    at(2, 3);
    console_write(t);
    for (int i = 0; i < n; i++) {
        at(3 + i, 3);
        console_write("\x1b[90m");
        console_write(label[i]);
        console_write("\x1b[0m");
        at(3 + i, 3 + 13);
        console_write(value[i]);
    }
    at(n + 5, 1);
}
