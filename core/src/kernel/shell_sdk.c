/*
 * Shell SDK: the IDE of the bm shell. Row 0 shows the tab bar (as Shell Code does), the rest of the
 * screen shows the active tab. F1 to F5 select a tab; Esc opens the menu (New, Open, Save, Save as, Exit);
 * Ctrl+N, Ctrl+O, Ctrl+S and Ctrl+Q do the same at once; Open ROM puts a ROM in the Code tab (shell_sdk_code.c). A project is a .bme in /bme (shell_sdk_file.c).
 * The Project tab is done: tiles for the three platforms, the menu of the cartridge, the PCB, the
 * memory facts. The Code tab is an editor, with the ROM beside it. The other tabs have no content yet.
 */
#include "shell_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/console.h"
#include "input.h"
#include "usb/hid.h"
#include "romtabs.h"
#include "shell_sdk_build.h"
#include "shell_sdk_code.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_file.h"
#include "shell_sdk_pcb.h"
#include "shell_sdk_proj.h"
#include "shell_sdk_tpl.h"

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
static int platform_locked(void);        /* defined with the project state, below */
static int proj_sel;                    /* the highlighted row of the menu */

static void at(int row, int col)
{
    char b[40];
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
        snprintf(line, sizeof line, " %-13s %.*s%s", menu[i].label, room > 0 ? room : 0, menu[i].value,
                 menu[i].id == SDK_ROW_PLATFORM && platform_locked() ? "  (fixed)" : "");
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

    if (pcb_col <= cols && proj.target != SDK_BM) {          /* a bm project has no cartridge: no picture */
        sdk_pcb_t pcb;
        sdk_proj_pcb(&proj, &pcb);
        sdk_pcb_draw(proj.target, &pcb, pcb_col, P_MENU, P_PCB_W - 1, P_PCB_H);
    }
    at(1, 1);                                   /* the cursor on a blank cell, not at the end of the help */
}

/* ---- the project: name, unsaved changes, messages ---- */

static char pname[SDK_NAME_MAX];        /* the name of the project ("" until it is saved or opened) */
static int dirty;                       /* a change since the last save */
static char msg[192];                    /* one message for the status bar, until the next key */

static void say(const char *fmt, const char *a)
{
    snprintf(msg, sizeof msg, fmt, a);
}

static void draw(int tab)
{
    char left[96];
    size_t pos = 0, mark = 0, mark_len = 0;
    for (int i = 0; i < SDK_TAB_COUNT; i++) {
        if (i == tab) {
            mark = pos + 1;                 /* the status text starts in column 1 */
            mark_len = strlen(tab_names[i]) + 2;
        }
        pos += (size_t)snprintf(left + pos, sizeof left - pos, " %s  ", tab_names[i]);
    }
    console_set_status(left, NULL);         /* the right half belongs to the input module: it rewrites it */
    console_status_mark((uint32_t)mark, (uint32_t)mark_len);    /* the open tab in the swapped colours */

    uint32_t cols, rows;
    console_size(&cols, &rows);
    console_write("\x1b[0m\x1b[2J");

    if (tab == SDK_TAB_CODE) {
        sdk_code_draw((int)cols, (int)rows, msg);
        return;
    }
    /* the project name at the top right, a message on the free row under the tiles */
    char head[64];
    snprintf(head, sizeof head, "Project: %s%s", pname[0] ? pname : "(new)", dirty ? "*" : "");
    if ((int)cols > 60) {
        at(1, (int)cols - 28);
        console_write("\x1b[96m");
        console_write(head);
        at(2, (int)cols - 28);
        console_write("\x1b[90mEsc menu  F1-F5 tabs\x1b[0m");
    }
    if (msg[0]) {
        at(5, 2);
        console_write("\x1b[93m");
        console_write(msg);
        console_write("\x1b[0m");
    }
    if (tab == SDK_TAB_PROJECT) {
        draw_project((int)cols, (int)rows);
        return;
    }
    at((int)rows / 2, 3);
    console_write(tab_names[tab]);
    console_write(": not built yet");
    at(1, 1);
}

/* ---- popups: a list, a question and a line of text ---- */

static void frame(int r, int c, int h, int w)
{
    at(r, c);
    console_putc((char)0xDA);
    for (int i = 0; i < w - 2; i++)
        console_putc((char)0xC4);
    console_putc((char)0xBF);
    for (int i = 1; i < h - 1; i++) {
        at(r + i, c);
        console_putc((char)0xB3);
        for (int k = 0; k < w - 2; k++)
            console_putc(' ');
        console_putc((char)0xB3);
    }
    at(r + h - 1, c);
    console_putc((char)0xC0);
    for (int i = 0; i < w - 2; i++)
        console_putc((char)0xC4);
    console_putc((char)0xD9);
}

/* The window of a popup: w x h cells, in the middle of the screen. */
static void window(int h, int w, int *r, int *c)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    *r = ((int)rows - h) / 2 + 1;
    *c = ((int)cols - w) / 2 + 1;
    if (*r < 1)
        *r = 1;
    if (*c < 1)
        *c = 1;
    frame(*r, *c, h, w);
}

static int is_up(int k) { return k == 'w' || k == HID_KEY_UP; }
static int is_down(int k) { return k == 's' || k == HID_KEY_DOWN; }
static int is_enter(int k) { return k == '\r' || k == '\n'; }

/* A list. Returns the number of the item chosen, or -1 on Esc. The screen behind is drawn by redraw(). */
static int popup(const char *title, const char *const *items, int n, int sel, void (*redraw)(void))
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int w = (int)strlen(title) + 4, vis = n;
    for (int i = 0; i < n; i++)
        if ((int)strlen(items[i]) + 4 > w)
            w = (int)strlen(items[i]) + 4;
    if (vis > (int)rows - 6)
        vis = (int)rows - 6;
    if (vis < 1)
        vis = 1;
    int top = 0;
    for (;;) {
        if (sel < top)
            top = sel;
        if (sel >= top + vis)
            top = sel - vis + 1;
        redraw();
        int r, c;
        window(vis + 2, w, &r, &c);
        at(r, c + 2);
        console_write(title);
        for (int i = 0; i < vis; i++) {
            char row[80];
            snprintf(row, sizeof row, " %-*.*s ", w - 4, w - 4, items[top + i]);
            at(r + 1 + i, c + 1);
            console_write(top + i == sel ? "\x1b[7m" : "\x1b[0m");
            console_write(row);
            console_write("\x1b[0m");
        }
        at(1, 1);
        const int k = (unsigned char)input_getc();
        if (is_up(k))
            sel = (sel + n - 1) % n;
        else if (is_down(k))
            sel = (sel + 1) % n;
        else if (is_enter(k))
            return sel;
        else if (k == 0x1B)
            return -1;
    }
}

/* One line of text, at most max characters of an 8.3 name. Returns 1 on Enter, 0 on Esc. */
static int prompt(const char *title, char *buf, int max, void (*redraw)(void))
{
    int len = (int)strlen(buf);
    for (;;) {
        redraw();
        const int w = (int)strlen(title) + 6 > max + 8 ? (int)strlen(title) + 6 : max + 8;
        int r, c;
        window(4, w, &r, &c);
        at(r, c + 2);
        console_write(title);
        at(r + 1, c + 2);
        console_write("\x1b[7m");
        char line[40];
        snprintf(line, sizeof line, "%-*s", max + 1, buf);
        console_write(line);
        console_write("\x1b[0m");
        at(r + 2, c + 2);
        console_write("Enter saves, Esc cancels");
        at(1, 1);
        const int k = (unsigned char)input_getc();
        if (k == 0x1B)
            return 0;
        if (is_enter(k))
            return 1;
        if ((k == 0x08 || k == 0x7F) && len > 0)
            buf[--len] = 0;
        else if (len < max && ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') ||
                               k == '_' || k == '-' || k == '.' || k == ' ')) {
            buf[len++] = (char)k;
            buf[len] = 0;
        }
    }
}

static int cur_tab;

static void redraw_screen(void) { draw(cur_tab); }

/* Asks before a change loses work. Returns 1 to go on. */
static int discard_ok(void)
{
    static const char *const yn[2] = { "No, keep working", "Yes, discard the changes" };
    return !dirty || popup("Unsaved changes", yn, 2, 0, redraw_screen) == 1;
}

/* ---- Esc menu: New, Open, Save, Save as, Build ---- */

static unsigned seen_rev;               /* the revision of the text when the project was saved or opened */

/* The start file of the platform in the editor. */
static void load_template(int target)
{
    size_t n;
    const char *t = sdk_template(target, &n);
    sdk_edit_set(t, n);
    seen_rev = sdk_edit_revision();
}

/* 1 when the text in the editor is the start file of the platform, as it was given (nothing written yet). */
static int text_is_template(int target)
{
    size_t n;
    const char *t = sdk_template(target, &n);
    return sdk_edit_length() == n && !memcmp(sdk_edit_text(), t, n);
}

/* The platform of a project is fixed once it has work: a name (it is saved or opened) or code of its own.
 * A new project that has none of that may still change platform, and the start file follows it. */
static int platform_locked(void)
{
    return pname[0] || !text_is_template(proj.target);
}

static void do_save(const char *name)
{
    char err[64];
    if (sdk_file_save(&proj, name, sdk_edit_text(), sdk_edit_length(), err, sizeof err) != 0) {
        say("save failed: %s", err);
        return;
    }
    sdk_file_stem(name, pname);
    dirty = 0;
    seen_rev = sdk_edit_revision();
    char path[32];
    sdk_file_path(pname, proj.target, path, sizeof path);
    say("saved %s", path);
}

static void do_save_as(void)
{
    char name[SDK_NAME_MAX];
    snprintf(name, sizeof name, "%s", pname);
    if (!prompt("Project name (8 letters)", name, 8, redraw_screen) || name[0] == 0)
        return;
    char stem[SDK_NAME_MAX];
    sdk_file_stem(name, stem);
    if (strcmp(stem, pname) && sdk_file_exists(stem, proj.target)) {
        static const char *const yn[2] = { "No, choose another name", "Yes, replace it" };
        if (popup("That project exists", yn, 2, 0, redraw_screen) != 1)
            return;
    }
    do_save(stem);
}

static void do_new(void)
{
    if (!discard_ok())
        return;
    sdk_proj_default(&proj, SDK_NES);
    proj_sel = 0;
    pname[0] = 0;
    sdk_code_reset();
    load_template(proj.target);
    dirty = 0;
    say("%s", "a new project: Esc > Save as gives it a name");
}

static void do_open(void)
{
    if (!discard_ok())
        return;
    static char names[32][SDK_FILE_MAX];
    static const char *list[32];
    const int n = sdk_file_list(names, 32);
    if (n == 0) {
        say("%s", "no projects yet: Save as makes one (/bmr for NES and SNES, /bme for bm)");
        return;
    }
    for (int i = 0; i < n; i++)
        list[i] = names[i];
    const int pick = popup("Open a project", list, n, 0, redraw_screen);
    if (pick < 0)
        return;
    char err[64];
    sdk_proj_t loaded;
    char *source = NULL;
    size_t source_len = 0;
    if (sdk_file_load(names[pick], &loaded, &source, &source_len, err, sizeof err) != 0) {
        say("%s", err);
        return;
    }
    proj = loaded;
    proj_sel = 0;
    sdk_file_stem(names[pick], pname);
    sdk_edit_set(source ? source : "", source_len);
    free(source);
    seen_rev = sdk_edit_revision();
    dirty = 0;
    say("opened %s", names[pick]);
}

/* Open ROM: a list of the ROMs of /nes and /snes. The Code tab opens with the ROM on its right side. */
static void do_open_rom(void)
{
    static char label[2 * ROMTABS_MAX][64];
    static const char *list[2 * ROMTABS_MAX];
    static const rom_entry_t *entry[2 * ROMTABS_MAX];
    romtabs_scan();
    int n = 0;
    for (int plat = 0; plat < ROM_PLATFORMS; plat++)
        for (int i = 0; i < romtabs_count(plat) && n < 2 * ROMTABS_MAX; i++) {
            const rom_entry_t *e = romtabs_get(plat, i);
            snprintf(label[n], sizeof label[n], "%-5s %s", plat == ROM_NES ? "NES" : "SNES", e->title);
            list[n] = label[n];
            entry[n] = e;
            n++;
        }
    if (n == 0) {
        say("%s", "no ROMs: put .nes files in /nes and .sfc or .smc files in /snes");
        return;
    }
    const int pick = popup("Open a ROM", list, n, 0, redraw_screen);
    if (pick < 0)
        return;
    char err[64];
    if (sdk_code_open_rom(entry[pick]->path, err, sizeof err) != 0) {
        say("%s", err);
        return;
    }
    cur_tab = SDK_TAB_CODE;
    say("opened %s", sdk_code_rom_name());
}

/* Build ROM: assembles the source of the Code tab and writes /nes/NAME.nes or /snes/NAME.sfc, then shows the
 * ROM beside the code. An error goes to the message line and the cursor goes to its line. */
static void do_build(void)
{
    if (proj.target == SDK_BM) {
        say("%s", "a bm project has no ROM: Build is for NES and SNES");
        return;
    }
    if (!pname[0])
        do_save_as();                           /* the ROM takes the name of the project */
    if (!pname[0])
        return;
    if (dirty)
        do_save(pname);
    char err[96];
    uint8_t *rom = NULL;
    size_t rom_len = 0;
    if (sdk_build(&proj, pname, sdk_edit_text(), sdk_edit_length(), &rom, &rom_len, err, sizeof err) != 0) {
        const int line = sdk_proj_error_line(err);
        cur_tab = SDK_TAB_CODE;
        if (line > 0)
            sdk_edit_goto_line(line);
        say("%s", err);
        return;
    }
    if (sdk_build_write(&proj, pname, rom, rom_len, err, sizeof err) != 0) {
        free(rom);
        say("write failed: %s", err);
        return;
    }
    free(rom);
    char path[24];
    sdk_proj_rom_path(&proj, pname, path, sizeof path);
    if (sdk_code_open_rom(path, err, sizeof err) != 0) {
        say("%s", err);
        return;
    }
    cur_tab = SDK_TAB_CODE;
    snprintf(msg, sizeof msg, "built %s (%u KB)", path, (unsigned)((rom_len + 1023) / 1024));
}

/* Returns 1 to leave the SDK. */
static int esc_menu(void)
{
    static const char *const items[] = { "Resume", "New", "Open", "Open ROM...", "Save", "Save as...", "Build ROM", "Exit" };
    const int pick = popup("Shell SDK", items, 8, 0, redraw_screen);
    switch (pick) {
    case 1: do_new(); break;
    case 2: do_open(); break;
    case 3: do_open_rom(); break;
    case 4:
        if (pname[0])
            do_save(pname);
        else
            do_save_as();
        break;
    case 5: do_save_as(); break;
    case 6: do_build(); break;
    case 7: return discard_ok();
    default: break;
    }
    return 0;
}

/* A key in the Project tab: w s Up Down move, a d Left Right Enter change the value. */
static void project_key(int c)
{
    sdk_row_t menu[12];
    const int n = sdk_proj_rows(&proj, menu, 12);
    const sdk_proj_t before = proj;
    const int on_platform = menu[proj_sel].id == SDK_ROW_PLATFORM;
    const int change = c == 'a' || c == HID_KEY_LEFT || c == 'd' || c == HID_KEY_RIGHT || is_enter(c);
    if (on_platform && change && platform_locked()) {
        say("%s", "the platform is fixed: this project has a name or code. Esc > New starts one for another platform");
        return;
    }
    if (is_up(c))
        proj_sel = (proj_sel + n - 1) % n;
    else if (is_down(c))
        proj_sel = (proj_sel + 1) % n;
    else if (c == 'a' || c == HID_KEY_LEFT)
        sdk_proj_step(&proj, menu[proj_sel].id, -1);
    else if (c == 'd' || c == HID_KEY_RIGHT || is_enter(c))
        sdk_proj_step(&proj, menu[proj_sel].id, 1);
    if (on_platform && proj.target != before.target)
        load_template(proj.target);                 /* the project was untouched: the start file follows */
    if (memcmp(&before, &proj, sizeof proj))
        dirty = 1;
}

int shell_sdk_open(void)
{
    cur_tab = SDK_TAB_PROJECT;
    sdk_proj_default(&proj, SDK_NES);
    proj_sel = 0;
    pname[0] = 0;
    dirty = 0;
    msg[0] = 0;
    sdk_code_reset();
    load_template(proj.target);
    hid_text_mode(1);
    console_clear();
    for (;;) {
        draw(cur_tab);
        const int c = (unsigned char)input_getc();
        msg[0] = 0;
        const int t = shell_sdk_tab_for_key(c);
        if (t >= 0)
            cur_tab = t;
        else if (c == 0x1B) {
            if (esc_menu())
                break;
        } else if (c == 0x11) {                 /* Ctrl+Q */
            if (discard_ok())
                break;
        } else if (c == 0x13) {                 /* Ctrl+S */
            if (pname[0])
                do_save(pname);
            else
                do_save_as();
        } else if (c == 0x0F)                   /* Ctrl+O */
            do_open();
        else if (c == 0x0E)                     /* Ctrl+N */
            do_new();
        else if (c == 0x02)                     /* Ctrl+B */
            do_build();
        else if (cur_tab == SDK_TAB_PROJECT)
            project_key(c);
        else if (cur_tab == SDK_TAB_CODE)
            sdk_code_key(c, (hid_usage_held(0xE0) || hid_usage_held(0xE4) ? SDK_MOD_CTRL : 0) |
                            (hid_usage_held(0xE1) || hid_usage_held(0xE5) ? SDK_MOD_SHIFT : 0) |
                            (hid_usage_held(0xE2) || hid_usage_held(0xE6) ? SDK_MOD_ALT : 0));
        if (sdk_edit_revision() != seen_rev) {  /* the text changed: the project is not saved */
            seen_rev = sdk_edit_revision();
            dirty = 1;
        }
    }
    sdk_code_reset();
    hid_text_mode(0);
    console_clear();
    return 0;
}
