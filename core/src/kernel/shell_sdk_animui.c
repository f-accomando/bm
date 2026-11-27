/*
 * Shell SDK: the animation mode of the Sprite tab (see shell_sdk_animui.h). Original code.
 *
 * The screen (80 x 21 cells): the settings, the state, the frames with the preview on the right, the colours, and
 * the lines of code of the animation. w asks where in the sheet the tiles go, i takes the frames from the sheet.
 */
#include "shell_sdk_animui.h"

#include <stdio.h>
#include <string.h>

#include "drivers/fb.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "shell_sdk_anim.h"
#include "shell_sdk_chr.h"
#include "shell_sdk_code.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_keys.h"
#include "shell_sdk_pal.h"
#include "shell_sdk_pane.h"
#include "shell_sdk_sheet.h"
#include "shell_sdk_tiles.h"
#include "usb/hid.h"

enum { EDIT, SAVE, IMPORT };
enum { F_NAME, F_SIZE, F_FRAMES, F_SPEED, F_PALETTE, F_REUSE, FIELDS };

#define TOP_ROW_ART 3                   /* the first text row of the frames */
#define ART_ROWS 11                     /* and how many they take */
#define SWATCH_ROW 14
#define CODE_TOP 15
#define ART_WIDTH 480                   /* pixels for the frames; the preview is to their right */
#define SHEET_LEFT 6                    /* the text column of the sheet of the picker */

static const uint8_t sizes[][2] = { { 1, 1 }, { 1, 2 }, { 2, 1 }, { 2, 2 }, { 2, 3 }, { 3, 3 }, { 2, 4 }, { 4, 2 },
                                    { 4, 4 } };

static sdk_anims_t all, reading;
static sdk_anim_t work;                 /* the animation being edited */
static sdk_anim_pixels_t pix;
static sdk_anim_plan_t plan;
static int chr, format, readable = 1;   /* the code: SDK_CHR_*, the tiles */
static int synced;
static unsigned code_rev;
static char broken[128];
static int sel;                         /* the animation of the code that is open; all.count: a new one */
static int have;                        /* an animation is open */
static int unsaved;
static int mode, header, field;
static int frame, cx, cy, selecting, ax, ay, colour = 1;
static int reuse = 1;
static int typing, armed;
static char typed[SDK_ANIM_NAME];
static int pick_tile;                   /* the cursor of the picker */
static int clip_w, clip_h;
static uint8_t clip[SDK_ANIM_PIXELS][SDK_ANIM_PIXELS];
static int shown_frame;                 /* of the preview */
static uint32_t next_frame;
static int screen_cols = 80, screen_rows = 21;

void sdk_animui_reset(void)
{
    memset(&all, 0, sizeof all);
    memset(pix, 0, sizeof pix);
    synced = have = unsaved = 0;
    broken[0] = 0;
    sel = 0;
    mode = EDIT;
    header = field = 0;
    frame = cx = cy = selecting = 0;
    colour = 1;
    reuse = 1;
    typing = armed = 0;
    clip_w = clip_h = 0;
    pick_tile = 0;
    shown_frame = 0;
}

int sdk_animui_typing(void) { return typing; }
int sdk_animui_playing(void) { return have && mode == EDIT && !broken[0] && work.frames > 1; }
int sdk_animui_keys(void) { return mode == EDIT ? SDK_KEYS_ANIM : SDK_KEYS_ANIM_PLACE; }

static int within(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int width(void) { return work.wide * 8; }
static int height(void) { return work.high * 8; }
static int colours(void) { return sdk_tile_colors(format); }

static void settle(void)
{
    frame = within(frame, 0, work.frames - 1);
    cx = within(cx, 0, width() - 1);
    cy = within(cy, 0, height() - 1);
    ax = within(ax, 0, width() - 1);
    ay = within(ay, 0, height() - 1);
    shown_frame %= work.frames;
}

/* Opens the animation i of the code: its pixels come from the sheet. */
static void open_anim(int i)
{
    sel = within(i, 0, all.count ? all.count - 1 : 0);
    have = all.count > 0;
    unsaved = 0;
    selecting = 0;
    if (have) {
        work = all.anim[sel];
        sdk_anim_load(&work, pix);
        reuse = 1;
        settle();
    }
}

void sdk_animui_sync(int kind, int sheet_readable)
{
    const int c = kind == SDK_CHR_NES_C || kind == SDK_CHR_SNES_C;
    const int was_c = chr == SDK_CHR_NES_C || chr == SDK_CHR_SNES_C;
    const int changed = !synced || kind != chr || sdk_edit_revision() != code_rev;
    chr = kind;
    format = sdk_chr_format(kind);
    readable = sheet_readable;
    colour = within(colour, 1, sdk_tile_colors(format) - 1);
    if (!changed)
        return;
    code_rev = sdk_edit_revision();
    const int first = !synced || c != was_c;
    synced = 1;
    broken[0] = 0;
    if (sdk_anim_parse(sdk_edit_text(), sdk_edit_length(), c, &reading, broken, sizeof broken) != 0)
        return;
    all = reading;
    if (first || !unsaved)                  /* changes that are not saved stay as they are */
        open_anim(first ? 0 : sel);
    else if (sel > all.count)
        sel = all.count;
}

/* The tiles into the sheet and the code, and the animations into the code: one step of the undo. */
static void write_code(int sheet_too)
{
    static int run = 9000;
    char err[96];
    run++;
    const int c = chr == SDK_CHR_NES_C || chr == SDK_CHR_SNES_C;
    if (sheet_too && sdk_chr_commit(chr, run, err, sizeof err) != 0)
        snprintf(sdk_note, sizeof sdk_note, "%.63s", err);
    else if (sdk_anim_commit(&all, c, run, err, sizeof err) != 0)
        snprintf(sdk_note, sizeof sdk_note, "%.63s", err);
}

/* ---- the screen ---- */

static void text(int row, int col, const char *colour_code, const char *s)
{
    sdk_at(row, col);
    console_write("\x1b[0m");
    if (colour_code[0]) {
        char b[24];
        snprintf(b, sizeof b, "\x1b[%sm", colour_code);
        console_write(b);
    }
    console_write(s);
    console_write("\x1b[0m");
}

static int labelled(int row, int col, const char *label, const char *value, int chosen)
{
    char b[48];
    text(row, col, "90", label);
    col += (int)strlen(label) + 1;
    snprintf(b, sizeof b, " %s ", value);
    text(row, col, chosen ? "30;107" : "97", b);
    return col + (int)strlen(b) + 1;
}

/* The lines of code of the open animation. */
static void code_strip(int top, int bottom)
{
    const int c = chr == SDK_CHR_NES_C || chr == SDK_CHR_SNES_C;
    char b[96];
    const int line = have && sel < all.count ? all.anim[sel].line : 0;
    if (line > 0)
        snprintf(b, sizeof b, " %s  anim_%s  line %d", c ? "main.c" : "main.s", all.anim[sel].name, line);
    else
        snprintf(b, sizeof b, " %s  not in the code yet", c ? "main.c" : "main.s");
    sdk_title_bar(top, 1, screen_cols, b, 0);
    for (int i = 0; i < bottom - top; i++) {
        const char *s = NULL;
        int n = 0;
        if (line > 0 && line + i <= sdk_edit_line_count())
            n = sdk_edit_line(line - 1 + i, &s);
        sdk_put_row(top + 1 + i, 2, screen_cols - 2, s ? s : "", n);
    }
}

/* Where the frames are: their pixel size, how many across, and the corner of one. */
static void layout(int *scale, int *across, int *gx, int *gy)
{
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int cols = work.frames <= 4 ? work.frames : (work.frames + 1) / 2;
    const int lines = work.frames <= 4 ? 1 : 2;
    int s = (ART_WIDTH - (cols - 1) * 8) / (cols * width());
    const int high = (ART_ROWS * (int)ch - (lines - 1) * 8 - 4) / (lines * height());
    if (s > high)
        s = high;
    *scale = within(s, 1, 20);
    *across = cols;
    *gx = (int)cw;
    *gy = (int)top + (TOP_ROW_ART - 1) * (int)ch + 2;
}

static uint32_t rgb(framebuffer_t *fb, uint32_t v)
{
    return fb_color(fb, (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

static void frame_rect(framebuffer_t *fb, int x, int y, int w, int h, int t, uint32_t c)
{
    gfx_rect(fb, x, y, w, t, c);
    gfx_rect(fb, x, y + h - t, w, t, c);
    gfx_rect(fb, x, y, t, h, c);
    gfx_rect(fb, x + w - t, y, t, h, c);
}

static void paint_pixels(framebuffer_t *fb, int f, int x0, int y0, int s, int grid)
{
    const int n = colours();
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) {
            const int v = pix[f][y][x];
            const uint32_t c = v ? rgb(fb, sdk_pal_rgb(v, n)) :
                               fb_color(fb, (x + y) & 1 ? 40 : 24, (x + y) & 1 ? 40 : 24, (x + y) & 1 ? 40 : 24);
            gfx_rect(fb, x0 + x * s, y0 + y * s, grid && s > 3 ? s - 1 : s, grid && s > 3 ? s - 1 : s, c);
        }
}

static void paint_preview(void)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb || !have)
        return;
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    int s = 128 / (width() > height() ? width() : height());
    if (s < 1)
        s = 1;
    const int x0 = ART_WIDTH + 24, y0 = (int)top + (TOP_ROW_ART - 1) * (int)ch + 2;
    gfx_rect(fb, x0 - 2, y0 - 2, 132, 132, fb_color(fb, 0, 0, 0));
    paint_pixels(fb, shown_frame, x0, y0, s, 0);
}

static void paint_frames(void)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb || !have)
        return;
    int s, across, gx, gy;
    layout(&s, &across, &gx, &gy);
    for (int f = 0; f < work.frames; f++) {
        const int x = gx + (f % across) * (width() * s + 8), y = gy + (f / across) * (height() * s + 8);
        paint_pixels(fb, f, x, y, s, 1);
        for (int k = 1; k < work.wide; k++)             /* where the tiles meet */
            gfx_rect(fb, x + k * 8 * s - 1, y, 1, height() * s, fb_color(fb, 110, 110, 110));
        for (int k = 1; k < work.high; k++)
            gfx_rect(fb, x, y + k * 8 * s - 1, width() * s, 1, fb_color(fb, 110, 110, 110));
        frame_rect(fb, x - 2, y - 2, width() * s + 4, height() * s + 4, 1,
                   f == frame && !header ? fb_color(fb, 255, 255, 255) : fb_color(fb, 70, 70, 70));
        if (f != frame)
            continue;
        if (selecting) {
            const int x1 = cx < ax ? cx : ax, x2 = cx < ax ? ax : cx, y1 = cy < ay ? cy : ay, y2 = cy < ay ? ay : cy;
            frame_rect(fb, x + x1 * s, y + y1 * s, (x2 - x1 + 1) * s, (y2 - y1 + 1) * s, 1,
                       fb_color(fb, 0, 200, 255));
        }
        if (!header)
            frame_rect(fb, x + cx * s, y + cy * s, s, s, s > 4 ? 2 : 1, fb_color(fb, 255, 220, 0));
    }
    /* the colours: 0 is transparent, the pen has a frame; of 16 at most */
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int n = colours() > 16 ? 16 : colours(), y = (int)top + (SWATCH_ROW - 1) * (int)ch + 1;
    for (int i = 0; i < n; i++) {
        const int x = (int)cw + i * 18;
        gfx_rect(fb, x, y, 16, 13, i ? rgb(fb, sdk_pal_rgb(i, colours())) : fb_color(fb, 24, 24, 24));
        if (i == colour)
            frame_rect(fb, x, y, 16, 13, 2, fb_color(fb, 255, 220, 0));
    }
    paint_preview();
}

static void draw_edit(void)
{
    char v[32];
    if (!have) {
        text(1, 2, "97", "Animation: none yet");
        text(3, 2, "90", "A new one starts with 4 empty frames of 16x16");
        return;
    }
    snprintf(v, sizeof v, "%s", typing ? typed : work.name);
    int col = labelled(1, 1, "Anim", v, typing || (header && field == F_NAME));
    snprintf(v, sizeof v, "%dx%d", width(), height());
    col = labelled(1, col, "Size", v, header && field == F_SIZE);
    snprintf(v, sizeof v, "%d", work.frames);
    col = labelled(1, col, "Frames", v, header && field == F_FRAMES);
    snprintf(v, sizeof v, "%d", work.speed);
    col = labelled(1, col, "Speed", v, header && field == F_SPEED);
    snprintf(v, sizeof v, "%d", work.cell[0][0][1] & 3);
    col = labelled(1, col, "Pal", v, header && field == F_PALETTE);
    labelled(1, col, "Reuse", reuse ? "mirrors" : "off", header && field == F_REUSE);
    sdk_anim_plan(&work, pix, reuse, &plan);
    int cells = 0;
    for (int f = 0; f < work.frames; f++)
        for (int i = 0; i < work.wide * work.high; i++)
            cells += plan.use[f][i] >= 0;
    char b[96];
    snprintf(b, sizeof b, "frame %d/%d  pixel %d,%d   %d tile%s for %d cells   %s", frame + 1, work.frames, cx, cy,
             plan.count, plan.count == 1 ? "" : "s", cells, unsaved ? "not saved" : "saved");
    text(2, 2, unsaved ? "93" : "90", b);
    text(2, 64, "96", "preview");
}

static void draw_picker(void)
{
    char b[80];
    sdk_anim_plan(&work, pix, reuse, &plan);
    if (mode == SAVE)
        snprintf(b, sizeof b, " Save anim_%s: %d tile%s from $%02X", work.name, plan.count,
                 plan.count == 1 ? "" : "s", (unsigned)pick_tile);
    else
        snprintf(b, sizeof b, " Import into anim_%s: %d frames of %dx%d from $%02X", work.name, work.frames,
                 width(), height(), (unsigned)pick_tile);
    sdk_title_bar(1, 1, screen_cols, b, 1);
    for (int r = 0; r < 16; r++) {
        snprintf(b, sizeof b, "%02X", r * 16);
        text(3 + r, 2, "90", b);
    }
    const int col = SHEET_LEFT + 34;
    if (mode == SAVE) {
        int where[SDK_ANIM_UNIQUE];
        uint8_t own[SDK_ANIM_TILES];
        sdk_anim_own(&all, sel, own);
        const int fit = sdk_anim_spots(&plan, pick_tile, sel < all.count ? own : NULL, where);
        text(3, col, "93", "Yellow: where they go");
        text(5, col, "37", "Empty tiles are taken, and the");
        text(6, col, "37", "tiles this animation had.");
        if (fit < plan.count) {
            snprintf(b, sizeof b, "Only %d of %d fit from here", fit, plan.count);
            text(8, col, "91", b);
        }
    } else {
        text(3, col, "96", "Blue: the frames, side by side");
        if ((pick_tile % 16) + work.frames * work.wide > 16 || pick_tile / 16 + work.high > 16)
            text(5, col, "91", "They pass the sheet from here");
    }
}

static void paint_picker(void)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb)
        return;
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int side = (int)ch;                           /* a tile a row of text */
    const int x0 = (SHEET_LEFT - 1) * (int)cw, y0 = (int)top + 2 * (int)ch;
    for (int t = 0; t < SDK_ANIM_TILES; t++) {
        const sdk_tile_t *tile = sdk_sheet_tile(t / 16, t % 16);
        const int x = x0 + (t % 16) * side, y = y0 + (t / 16) * side;
        gfx_rect(fb, x, y, side, side, fb_color(fb, 56, 56, 56));
        gfx_rect(fb, x + 1, y + 1, side - 1, side - 1, fb_color(fb, 16, 16, 16));
        if (!tile->used)
            continue;
        const int s = side / 8, n = sdk_tile_colors(tile->fmt);
        for (int i = 0; i < 64; i++)
            if (tile->pix[i])
                gfx_rect(fb, x + (i % 8) * s, y + (i / 8) * s, s, s, rgb(fb, sdk_pal_rgb(tile->pix[i], n)));
    }
    if (mode == SAVE) {
        int where[SDK_ANIM_UNIQUE];
        uint8_t own[SDK_ANIM_TILES];
        sdk_anim_own(&all, sel, own);
        const int fit = sdk_anim_spots(&plan, pick_tile, sel < all.count ? own : NULL, where);
        for (int i = 0; i < fit; i++)
            frame_rect(fb, x0 + (where[i] % 16) * side, y0 + (where[i] / 16) * side, side, side, 2,
                       fb_color(fb, 255, 220, 0));
    } else {
        const int column = pick_tile % 16, row = pick_tile / 16;
        for (int f = 0; f < work.frames; f++) {
            const int c = column + f * work.wide;
            if (c + work.wide > 16 || row + work.high > 16)
                break;
            frame_rect(fb, x0 + c * side, y0 + row * side, work.wide * side, work.high * side, 2,
                       fb_color(fb, 0, 200, 255));
        }
    }
    frame_rect(fb, x0 + (pick_tile % 16) * side, y0 + (pick_tile / 16) * side, side, side, 1,
               fb_color(fb, 255, 255, 255));
}

void sdk_animui_draw(int cols, int rows, const char *msg)
{
    screen_cols = cols;
    screen_rows = rows;
    console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    if (broken[0]) {
        text(2, 3, "91", "The ;@anim block of the code cannot be read:");
        text(3, 3, "93", broken);
        text(5, 3, "90", "Put it right in the code. Nothing is written until then.");
    } else if (mode == EDIT) {
        draw_edit();
        code_strip(CODE_TOP, rows - 1);
    } else
        draw_picker();
    const char *s = sdk_note[0] ? sdk_note : msg && msg[0] ? msg : NULL;
    if (s) {
        console_write("\x1b[93m");
        sdk_put_row(rows, 1, cols - 1, s, (int)strlen(s));
        console_write("\x1b[0m");
    }
    sdk_at(1, 1);
    if (broken[0])
        return;
    if (mode == EDIT)
        paint_frames();                     /* the pixels after all the text */
    else
        paint_picker();
}

void sdk_animui_tick(void)
{
    if (!sdk_animui_playing())
        return;
    const uint32_t now = timer_ticks();
    if ((int32_t)(now - next_frame) < 0)
        return;
    next_frame = now + (uint32_t)work.speed * 16667u;
    shown_frame = (shown_frame + 1) % work.frames;
    paint_preview();
}

/* ---- keys ---- */

/* The rectangle of the selection, else the whole frame. */
static void area(int *x1, int *y1, int *x2, int *y2)
{
    *x1 = selecting ? (cx < ax ? cx : ax) : 0;
    *x2 = selecting ? (cx < ax ? ax : cx) : width() - 1;
    *y1 = selecting ? (cy < ay ? cy : ay) : 0;
    *y2 = selecting ? (cy < ay ? ay : cy) : height() - 1;
}

/* Paints the selection, or the pixel of the cursor. */
static void paint(int v)
{
    int x1 = cx, x2 = cx, y1 = cy, y2 = cy;
    if (selecting)
        area(&x1, &y1, &x2, &y2);
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            if (pix[frame][y][x] != v) {
                pix[frame][y][x] = (uint8_t)v;
                unsaved = 1;
            }
}

static void copy(int cut)
{
    int x1, y1, x2, y2;
    area(&x1, &y1, &x2, &y2);
    clip_w = x2 - x1 + 1;
    clip_h = y2 - y1 + 1;
    for (int y = 0; y < clip_h; y++)
        memcpy(clip[y], &pix[frame][y1 + y][x1], (size_t)clip_w);
    if (!selecting) {                           /* a whole frame also pastes as tiles in the sheet */
        sdk_clip_start(work.wide, work.high);
        for (int r = 0; r < work.high; r++)
            for (int c = 0; c < work.wide; c++) {
                sdk_tile_t *t = sdk_clip_cell(r, c);
                t->fmt = (uint8_t)format;
                t->used = 0;
                for (int i = 0; i < 64; i++) {
                    t->pix[i] = pix[frame][r * 8 + i / 8][c * 8 + i % 8];
                    t->used |= t->pix[i] != 0;
                }
            }
    }
    if (cut) {
        for (int y = y1; y <= y2; y++)
            memset(&pix[frame][y][x1], 0, (size_t)clip_w);
        unsaved = 1;
    }
    snprintf(sdk_note, sizeof sdk_note, "%s %s %dx%d", cut ? "Cut" : "Copied", selecting ? "pixels" : "the frame",
             clip_w, clip_h);
}

static void paste(void)
{
    if (!clip_w) {
        snprintf(sdk_note, sizeof sdk_note, "Nothing to paste: copy first");
        return;
    }
    const int whole = clip_w == width() && clip_h == height();
    const int x0 = whole ? 0 : cx, y0 = whole ? 0 : cy;
    for (int y = 0; y < clip_h && y0 + y < height(); y++)
        for (int x = 0; x < clip_w && x0 + x < width(); x++)
            pix[frame][y0 + y][x0 + x] = clip[y][x];
    selecting = 0;
    unsaved = 1;
    snprintf(sdk_note, sizeof sdk_note, "Pasted %dx%d", clip_w, clip_h);
}

/* 1 when the changes of the open animation may be dropped: asked once, the second time yes. */
static int may_leave(void)
{
    if (!unsaved || armed) {
        armed = 0;
        return 1;
    }
    armed = 1;
    snprintf(sdk_note, sizeof sdk_note, "Not saved (w saves). The same key again drops the changes");
    return 0;
}

static int taken(const char *name)
{
    for (int i = 0; i < all.count; i++)
        if (i != sel && !strcmp(all.anim[i].name, name))
            return 1;
    return 0;
}

static void new_anim(void)
{
    if (all.count == SDK_ANIM_MAX) {
        snprintf(sdk_note, sizeof sdk_note, "%d animations at most", SDK_ANIM_MAX);
        return;
    }
    char name[SDK_ANIM_NAME] = "spin";
    sel = all.count;
    for (int n = 2; taken(name); n++)
        snprintf(name, sizeof name, "spin%d", n % 100);
    sdk_anim_new(&work, name);
    memset(pix, 0, sizeof pix);
    have = 1;
    unsaved = 1;
    header = 1;
    field = F_NAME;
    frame = cx = cy = selecting = 0;
    snprintf(sdk_note, sizeof sdk_note, "New animation: set it up, paint, then w saves it");
}

static void set_header(int d)
{
    if (field == F_NAME) {
        const int n = all.count + (sel == all.count);
        if (n > 1 && may_leave())
            open_anim((sel + n + d) % n >= all.count ? (d > 0 ? 0 : all.count - 1) : (sel + n + d) % n);
        return;
    }
    if (field == F_SIZE) {
        const int count = (int)(sizeof sizes / sizeof sizes[0]);
        int i = 0;
        while (i < count - 1 && (sizes[i][0] != work.wide || sizes[i][1] != work.high))
            i++;
        i = within(i + d, 0, count - 1);
        work.wide = sizes[i][0];
        work.high = sizes[i][1];
    } else if (field == F_FRAMES)
        work.frames = (uint8_t)within(work.frames + d, 1, SDK_ANIM_FRAMES);
    else if (field == F_SPEED)
        work.speed = (uint8_t)within(work.speed + d, 1, 60);
    else if (field == F_PALETTE) {
        const int p = ((work.cell[0][0][1] & 3) + 4 + d) % 4;
        for (int f = 0; f < SDK_ANIM_FRAMES; f++)
            for (int i = 0; i < SDK_ANIM_CELLS; i++)
                work.cell[f][i][1] = (uint8_t)((work.cell[f][i][1] & ~3) | p);
    } else
        reuse = !reuse;
    unsaved = 1;
    selecting = 0;
    settle();
}

static void save(void)
{
    if (!readable) {
        snprintf(sdk_note, sizeof sdk_note, "Fix the tiles block in Code first");
        return;
    }
    int where[SDK_ANIM_UNIQUE];
    uint8_t own[SDK_ANIM_TILES];
    sdk_anim_plan(&work, pix, reuse, &plan);
    sdk_anim_own(&all, sel, own);
    const int fit = sdk_anim_spots(&plan, pick_tile, sel < all.count ? own : NULL, where);
    if (fit < plan.count) {
        snprintf(sdk_note, sizeof sdk_note, "Only %d of %d tiles fit from $%02X", fit, plan.count,
                 (unsigned)pick_tile);
        return;
    }
    const int palette = work.cell[0][0][1] & 3;
    sdk_anim_place(&work, &plan, where, format, palette);
    if (sel >= all.count)
        sel = all.count++;
    all.anim[sel] = work;
    unsaved = 0;
    mode = EDIT;
    write_code(1);
    if (!sdk_note[0])
        snprintf(sdk_note, sizeof sdk_note, "Saved anim_%s: %d tiles in the sheet", work.name, plan.count);
}

static void picker_key(int key)
{
    if (key == HID_KEY_LEFT)
        pick_tile = within(pick_tile - 1, 0, 255);
    else if (key == HID_KEY_RIGHT)
        pick_tile = within(pick_tile + 1, 0, 255);
    else if (key == HID_KEY_UP)
        pick_tile = pick_tile >= 16 ? pick_tile - 16 : pick_tile;
    else if (key == HID_KEY_DOWN)
        pick_tile = pick_tile < 240 ? pick_tile + 16 : pick_tile;
    else if (key == 0x08 || key == 0x7F)
        mode = EDIT;
    else if (key == '\r' || key == '\n') {
        if (mode == SAVE)
            save();
        else if (sdk_anim_import(&work, pick_tile, pix) != 0)
            snprintf(sdk_note, sizeof sdk_note, "The frames pass the sheet from $%02X", (unsigned)pick_tile);
        else {
            mode = EDIT;
            unsaved = 1;
            snprintf(sdk_note, sizeof sdk_note, "Imported %d frames from $%02X", work.frames, (unsigned)pick_tile);
        }
    }
}

static void name_key(int key)
{
    const size_t n = strlen(typed);
    if (key == 0x1B)
        typing = 0;
    else if (key == '\r' || key == '\n') {
        if (!sdk_anim_name_ok(typed) || taken(typed))
            snprintf(sdk_note, sizeof sdk_note, taken(typed) ? "'%s' is taken" : "'%s' cannot be a name", typed);
        else {
            typing = 0;
            snprintf(work.name, sizeof work.name, "%s", typed);
            if (sel < all.count) {                      /* the name goes to the code at once */
                snprintf(all.anim[sel].name, sizeof all.anim[sel].name, "%s", typed);
                write_code(0);
            }
            snprintf(sdk_note, sizeof sdk_note, "In the program: anim_%s", work.name);
        }
    } else if ((key == 0x08 || key == 0x7F) && n)
        typed[n - 1] = 0;
    else if (n + 1 < sizeof typed && key > 0 && key < 127 && (key == '_' || (key >= '0' && key <= '9') ||
             (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z'))) {
        typed[n] = (char)key;
        typed[n + 1] = 0;
    }
}

/* An arrow over the pixels: past the edge of a frame, into its neighbour; past the top, to the settings. */
static void move(int dx, int dy, int shift, int mods)
{
    if (shift) {
        if (!selecting) {
            selecting = 1;
            ax = cx;
            ay = cy;
        }
        cx = within(cx + dx, 0, width() - 1);
        cy = within(cy + dy, 0, height() - 1);
        return;
    }
    selecting = 0;
    const int across = work.frames <= 4 ? work.frames : (work.frames + 1) / 2;
    cx += dx;
    cy += dy;
    if (cx < 0) {
        if (frame > 0) {
            frame--;
            cx = width() - 1;
        } else
            cx = 0;
    } else if (cx >= width()) {
        if (frame + 1 < work.frames) {
            frame++;
            cx = 0;
        } else
            cx = width() - 1;
    }
    if (cy < 0) {
        if (frame >= across) {
            frame -= across;
            cy = height() - 1;
        } else {
            cy = 0;
            header = 1;
            return;
        }
    } else if (cy >= height()) {
        if (frame + across < work.frames) {
            frame += across;
            cy = 0;
        } else
            cy = height() - 1;
    }
    if (mods & SDK_MOD_PAINT)
        paint(colour);
    else if (mods & SDK_MOD_ERASE)
        paint(0);
}

int sdk_animui_key(int key, int mods)
{
    sdk_note[0] = 0;
    if (typing) {
        name_key(key);
        return 1;
    }
    if (key == 'a') {                           /* back: from the picker to the frames, from them to the sprites */
        if (mode != EDIT) {
            mode = EDIT;
            return 1;
        }
        return 0;                               /* the changes stay for the next time */
    }
    if (broken[0])
        return 1;
    if (mode != EDIT) {
        picker_key(key);
        return 1;
    }
    if (key == 'n') {
        if (may_leave())
            new_anim();
        return 1;
    }
    if (!have)
        return 1;
    const int shift = (mods & SDK_MOD_SHIFT) != 0;
    const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
    if (!d && key != 'n' && !(header && field == F_NAME))
        armed = 0;
    if (key == 'w' || key == 'i') {
        mode = key == 'w' ? SAVE : IMPORT;
        if (key == 'w' && sel < all.count && !(work.cell[0][0][1] & SDK_ANIM_EMPTY))
            pick_tile = work.cell[0][0][0];     /* where it was */
        selecting = 0;
        return 1;
    }
    if (header) {
        if (key == HID_KEY_LEFT)
            field = (field + FIELDS - 1) % FIELDS;
        else if (key == HID_KEY_RIGHT)
            field = (field + 1) % FIELDS;
        else if (key == HID_KEY_DOWN)
            header = 0;
        else if (d)
            set_header(d);
        else if ((key == '\r' || key == '\n') && field == F_NAME) {
            typing = 1;
            snprintf(typed, sizeof typed, "%s", work.name);
        } else if (key == HID_KEY_DEL && field == F_NAME) {
            if (!armed) {
                armed = 1;
                snprintf(sdk_note, sizeof sdk_note, "Del again deletes the animation (its tiles stay)");
                return 1;
            }
            armed = 0;
            if (sel < all.count) {
                memmove(&all.anim[sel], &all.anim[sel + 1], (size_t)(all.count - sel - 1) * sizeof all.anim[0]);
                all.count--;
                write_code(0);
            }
            open_anim(sel ? sel - 1 : 0);
        }
        return 1;
    }
    switch (key) {
    case HID_KEY_UP: move(0, -1, shift, mods); break;
    case HID_KEY_DOWN: move(0, 1, shift, mods); break;
    case HID_KEY_LEFT: move(-1, 0, shift, mods); break;
    case HID_KEY_RIGHT: move(1, 0, shift, mods); break;
    case HID_KEY_PGUP: frame = within(frame - 1, 0, work.frames - 1); selecting = 0; break;
    case HID_KEY_PGDN: frame = within(frame + 1, 0, work.frames - 1); selecting = 0; break;
    case ' ': paint(colour); break;
    case 0x08:
    case 0x7F:
    case HID_KEY_DEL: paint(0); break;
    case '[':
    case ']':
        colour = within(colour + (key == ']' ? 1 : -1), 1, colours() - 1);
        break;
    case 'p':
        sdk_pal_next();
        snprintf(sdk_note, sizeof sdk_note, "Colours: %s", sdk_pal_name());
        break;
    case 'm':
    case 'f': {
        int x1, y1, x2, y2;
        area(&x1, &y1, &x2, &y2);
        sdk_anim_flip(pix, frame, x1, y1, x2, y2, key == 'm');
        unsaved = 1;
        snprintf(sdk_note, sizeof sdk_note, "%s %s", key == 'm' ? "Mirrored" : "Flipped",
                 selecting ? "the selection" : "the frame");
        break;
    }
    case 'd':                                   /* the next frame starts as this one */
        if (frame + 1 >= work.frames)
            snprintf(sdk_note, sizeof sdk_note, "This is the last frame");
        else {
            memcpy(pix[frame + 1], pix[frame], sizeof pix[0]);
            frame++;
            selecting = 0;
            unsaved = 1;
            snprintf(sdk_note, sizeof sdk_note, "Frame %d is now a copy of frame %d", frame + 1, frame);
        }
        break;
    case 0x01:                                  /* Ctrl+A */
        selecting = 1;
        ax = ay = 0;
        cx = width() - 1;
        cy = height() - 1;
        break;
    case 0x03: copy(0); break;
    case 0x18: copy(1); break;
    case 0x16: paste(); break;
    default: break;
    }
    return 1;
}
