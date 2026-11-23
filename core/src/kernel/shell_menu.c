/*
 * The menu of the bm shell. Each item runs a line of commands, as ':' would: the words are rows of
 * the table in monitor.c. The box uses the code page 437 glyphs of the console (0xDA 0xC4 0xBF
 * 0xB3 0xC0 0xD9) and the ANSI subset it understands (console.h).
 */
#include "shell_menu.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"

static const smenu_item_t system_sub[] = {
    { "Info",     "sys info",   0, NULL, 0 },
    { "Log",      "sys dmesg",  0, NULL, 0 },
    { "3D Bench", "bench 3d",   0, NULL, 0 },
    { "Reboot",   "sys reboot", 0, NULL, 0 },
};

static const smenu_item_t network_sub[] = {
    { "WiFi",            "wifi",        1, NULL, 0 },
    { "Pair controller", "bt pair pad", 1, NULL, 0 },
    { "Send reports",    "report send", 1, NULL, 0 },
};

#define NSUB(a) ((int)(sizeof (a) / sizeof (a)[0]))

static const smenu_item_t items[] = {
    { "bm menu gui", "menu", 0, NULL, 0 },
    { "Command line", NULL,  0, NULL, 0 },
    { "Shell Code",   "code", 0, NULL, 0 },
    { "System",         NULL,   0, system_sub, NSUB(system_sub) },
    { "Network",        NULL,   0, network_sub, NSUB(network_sub) },
    { "Help",           "help", 0, NULL, 0 },
};

const smenu_item_t *shell_menu_items(int *n)
{
    *n = NSUB(items);
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

/* The longest label of a list, plus the room for the border, a space each side and the arrow. */
static int box_width(const smenu_item_t *it, int n, int least)
{
    int w = least;
    for (int i = 0; i < n; i++) {
        const int l = (int)strlen(it[i].label) + 6;
        if (l > w)
            w = l;
    }
    return w;
}

/* One row inside a box: " label" padded, a ">" on a group (the console draws no glyph for code 0x10). style: the SGR to start it. */
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
    if (it->sub)
        row[iw - 1] = '>';
    at(r, c + 1);
    console_write(style);
    console_write(row);
    console_write("\x1b[0m");
}

/* The file list from row r: the folder in grey, then one row per entry; the last row of the screen
 * stays free (the serial marker). Returns the row after the list. */
static int file_list(int r, int cols, int rows, const smenu_t *m, const char *dir, const shell_file_t *files)
{
    char line[96];
    at(r, 2);
    snprintf(line, sizeof line, "SD:%.80s", dir);
    console_write("\x1b[90m");
    console_write(line);
    console_write("\x1b[0m");
    r++;
    const int vis = rows - 1 - r;               /* ESC[r;cH row rows - 1 is the last: the marker's; the list stops before it */
    if (vis < 1 || m->nfiles <= 0)
        return r;
    const int top = m->filesel >= vis ? m->filesel - vis + 1 : 0;
    int width = cols - 6;                       /* the icon, a space, the name, a margin */
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    for (int i = top; i < m->nfiles && i < top + vis; i++, r++) {
        const shell_file_t *f = &files[i];
        const int sel = m->in_files && i == m->filesel;
        at(r, 2);
        console_write(sel ? "\x1b[7m" : f->is_dir ? "\x1b[93m" : "\x1b[96m");
        console_putc(f->is_dir ? CONSOLE_ICON_FOLDER : CONSOLE_ICON_FILE);
        console_write(sel ? " " : "\x1b[0m ");
        snprintf(line, sizeof line, "%-*.*s", width, width, f->name);
        if (!sel) {
            int l = (int)strlen(line);           /* the unselected rows: no trailing spaces */
            while (l > 0 && line[l - 1] == ' ')
                line[--l] = 0;
        }
        console_write(line);
        console_write("\x1b[0m");
    }
    return r;
}

void shell_menu_draw(const smenu_t *m, const char *dir, const shell_file_t *files)
{
    static const char title[] = " bm shell ";
    uint32_t cols, rows;
    console_size(&cols, &rows);

    const int c0 = 1, r0 = 2;                   /* at the left, not centred */
    const int w = box_width(m->items, m->n, (int)strlen(title) + 4);
    const int h = m->n + 2;
    int bottom = r0 + h - 1;

    console_write("\x1b[0m\x1b[2J\x1b[H");
    box(r0, c0, h, w);
    at(r0, c0 + 2);
    console_write(title);
    for (int i = 0; i < m->n; i++) {
        const char *style = i != m->sel ? "\x1b[0m" : m->in_sub || m->in_files ? "\x1b[1m" : "\x1b[7m";
        item_row(r0 + 1 + i, c0, w, &m->items[i], style);
    }

    const smenu_item_t *g = &m->items[m->sel];
    if (m->n > 0 && g->sub) {                   /* its submenu, to the right of the main one, from the same top row */
        const int sw = box_width(g->sub, g->nsub, (int)strlen(g->label) + 4);
        const int sh = g->nsub + 2;
        const int sr = r0;
        const int sc = c0 + w + 1;
        box(sr, sc, sh, sw);
        char name[40];
        snprintf(name, sizeof name, " %.30s ", g->label);      /* its name on the top border, as " bm shell " */
        at(sr, sc + 2);
        console_write(name);
        for (int i = 0; i < g->nsub; i++) {
            const char *style = !m->in_sub ? "\x1b[90m" : i == m->subsel ? "\x1b[7m" : "\x1b[0m";
            item_row(sr + 1 + i, sc, sw, &g->sub[i], style);
        }
        if (sr + sh - 1 > bottom)
            bottom = sr + sh - 1;
    }
    if (dir && files)                           /* the files of the SD card, under the menu */
        bottom = file_list(bottom + 2, (int)cols, (int)rows, m, dir, files) - 1;
    at(bottom + 2 < (int)rows - 1 ? bottom + 2 : (int)rows - 1, 1);   /* the serial marker is printed here and wiped from the screen */
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
