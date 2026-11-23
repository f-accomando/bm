/*
 * Shell SDK: the IDE of the bm shell. Row 0 shows the tab bar (as Shell Code does), the rest of the
 * screen shows the active tab. F1 to F5 select a tab; Esc or Ctrl+Q leaves.
 * The Project tab is done: tiles for the three platforms, the menu of the cartridge, the PCB, the
 * memory facts. The other tabs have no content yet.
 */
#include "shell_sdk.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"
#include "input.h"
#include "usb/hid.h"
#include "shell_sdk_pcb.h"
#include "shell_sdk_proj.h"

static const char *const tab_names[SDK_TAB_COUNT] = { "Project", "Code", "Sprite", "Map", "Sounds" };

const char *shell_sdk_tab_name(int tab)
{
    return tab >= 0 && tab < SDK_TAB_COUNT ? tab_names[tab] : "";
}

int shell_sdk_tab_for_key(int key)
{
    return key >= HID_KEY_F1 && key < HID_KEY_F1 + SDK_TAB_COUNT ? key - HID_KEY_F1 : -1;
}

/* The Project tab. Rows of the screen (ANSI, 1 is under the status bar): the tiles 1-4, the menu from 6,
 * the PCB from 6 (8 rows), the facts 14-18, the help on 19-20 (two rows). */
#define P_TILES   1
#define P_MENU    6
#define P_INFO    14
#define P_NOTE    19                    /* two rows */
#define P_TILE_W  13
#define P_PCB_W   34
#define P_PCB_H   8

static sdk_proj_t proj;
static int proj_sel;                    /* the highlighted row of the menu */

static void at(int row, int col)
{
    char b[24];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

static void draw_project(int cols, int rows)
{
    sdk_row_t menu[12];
    const int n = sdk_proj_rows(&proj, menu, 12);
    if (proj_sel >= n)
        proj_sel = n - 1;

    for (int t = 0; t < SDK_TARGETS; t++)
        sdk_tile_draw(t, t == proj.target, 2 + t * (P_TILE_W + 2), P_TILES, P_TILE_W, 4);

    const int pcb_col = cols > P_PCB_W + 42 ? cols - P_PCB_W : cols + 1;      /* no room: no picture */
    const int room = (pcb_col > cols ? cols : pcb_col) - 2 - 15;              /* the width of a value */
    for (int i = 0; i < n && P_MENU + i < rows; i++) {
        char line[96];
        snprintf(line, sizeof line, " %-13s %.*s", menu[i].label, room > 0 ? room : 0, menu[i].value);
        at(P_MENU + i, 2);
        console_write(i == proj_sel ? "\x1b[7m" : "\x1b[0m");
        console_write(line);
        console_write("\x1b[0m");
    }

    char facts[8][72];
    const int nf = sdk_proj_info(&proj, facts, 8);
    for (int i = 0; i < nf && P_INFO + i < rows; i++) {
        at(P_INFO + i, 2);
        console_write("\x1b[96m");
        console_write(facts[i]);
        console_write("\x1b[0m");
    }

    /* the help text on two rows, cut at a space */
    const char *text = sdk_proj_note(&proj, menu[proj_sel].id);
    const int width = cols - 3;
    for (int r = 0; r < 2 && *text && P_NOTE + r < rows; r++) {
        int len = (int)strlen(text);
        if (len > width) {
            len = width;
            if (r == 0)
                while (len > 0 && text[len] != ' ')
                    len--;
            if (len == 0)
                len = width;
        }
        char note[128];
        snprintf(note, sizeof note, "%.*s", len, text);
        at(P_NOTE + r, 2);
        console_write("[93m");
        console_write(note);
        console_write("[0m");
        text += len;
        while (*text == ' ')
            text++;
    }

    if (pcb_col <= cols) {
        sdk_pcb_t pcb;
        sdk_proj_pcb(&proj, &pcb);
        sdk_pcb_draw(proj.target, &pcb, pcb_col, P_MENU, P_PCB_W - 1, P_PCB_H);
    }
}

static void draw(int tab)
{
    char left[96];
    size_t pos = 0;
    for (int i = 0; i < SDK_TAB_COUNT; i++)
        pos += (size_t)snprintf(left + pos, sizeof left - pos, i == tab ? "[%s] " : "%s ", tab_names[i]);
    console_set_status(left, "F1-F5 tab  Esc exit");

    uint32_t cols, rows;
    console_size(&cols, &rows);
    console_write("\x1b[0m\x1b[2J");
    if (tab == SDK_TAB_PROJECT) {
        draw_project((int)cols, (int)rows);
        return;
    }
    at((int)rows / 2, 3);
    console_write(tab_names[tab]);
    console_write(": not built yet");
}

/* A key in the Project tab: w s Up Down move, a d Left Right Enter change the value. */
static void project_key(int c)
{
    sdk_row_t menu[12];
    const int n = sdk_proj_rows(&proj, menu, 12);
    if (c == 'w' || c == HID_KEY_UP)
        proj_sel = (proj_sel + n - 1) % n;
    else if (c == 's' || c == HID_KEY_DOWN)
        proj_sel = (proj_sel + 1) % n;
    else if (c == 'a' || c == HID_KEY_LEFT)
        sdk_proj_step(&proj, menu[proj_sel].id, -1);
    else if (c == 'd' || c == HID_KEY_RIGHT || c == '\r' || c == '\n')
        sdk_proj_step(&proj, menu[proj_sel].id, 1);
}

int shell_sdk_open(void)
{
    int tab = SDK_TAB_PROJECT;
    sdk_proj_default(&proj, SDK_NES);
    proj_sel = 0;
    hid_text_mode(1);
    console_clear();
    for (;;) {
        draw(tab);
        const int c = (unsigned char)input_getc();
        const int t = shell_sdk_tab_for_key(c);
        if (t >= 0)
            tab = t;
        else if (c == 0x1B || c == 0x11)
            break;
        else if (tab == SDK_TAB_PROJECT)
            project_key(c);
    }
    hid_text_mode(0);
    console_clear();
    return 0;
}
