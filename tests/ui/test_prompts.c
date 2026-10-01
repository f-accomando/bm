/*
 * The button prompts (src/kernel/prompts.c) on the PC: checks every one of
 * both sets, then draws each set 3x as on a TV into a PNG: the menu's on
 * the menu bar's colour (prompts.png), the apps' chips on the apps' blue
 * (chips.png), in the directory given (default build/prompts).
 */
#include "kernel/prompts.h"
#include "gfx/font.h"
#include "lib/crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_BAR   0x16161C
#define C_LINE  0x3A3A46
#define C_TEXT  0xF0F0F4
#define C_DIM   0x9A9AA8
#define FACE    0xF0F0F4

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t at(const prompt_t *p, int x, int y) { return p->px[y * p->w + x]; }

static const char *const names[PROMPT_COUNT] = {
    "cross", "circle", "square", "triangle", "dpad", "dpad up", "dpad down", "dpad left",
    "dpad right", "dpad up/down", "dpad left/right", "L1", "R1", "L2", "R2", "L3", "R3",
    "left stick", "right stick", "options", "share", "PS", "touchpad",
    "pad A", "pad B", "pad X", "pad Y", "pad start", "pad select",
    "up", "down", "left", "right", "Enter", "Esc", "Space", "Tab", "Backspace", "Shift",
    "Ctrl", "Alt", "Del", "Home", "End", "PgUp", "PgDn",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
};

/* a raised button: transparent corners, a light face, a lip on the last rows */
static void check_shape(const prompt_t *p, const char *name)
{
    CHECK(p, "%s: no prompt", name);
    if (!p)
        return;
    CHECK(p->w >= 16 && p->w <= PROMPT_MAX_W, "%s: width %d", name, p->w);
    CHECK((at(p, 0, 0) >> 24) < 128 && (at(p, p->w - 1, 0) >> 24) < 128, "%s: corners not clear", name);
    int white = 0, lip = 0;
    for (int y = 0; y < PROMPT_H; y++)
        for (int x = 0; x < p->w; x++) {
            uint32_t c = at(p, x, y);
            if ((c >> 24) == 255 && (c >> 16 & 255) >= 0xC0 && (c >> 8 & 255) >= 0xC0 && (c & 255) >= 0xC0)
                white++;
            if (y >= PROMPT_H - 2 && (c >> 24) == 255 && (c & 0xFFFFFF) != FACE)
                lip++;
        }
    CHECK(white >= 12, "%s: %d light pixels", name, white);
    CHECK(lip >= 4, "%s: no lip (%d)", name, lip);
}

static const uint32_t colours[4] = { 0x7EA6FF, 0xFF6B6B, 0xF28AE0, 0x3DDBB0 };

/* the apps' chips: the colour each one is filled with */
static uint32_t chip_colour(int id)
{
    static const uint32_t letters[4] = { 0x5BD47E, 0xFF6B6B, 0x5FA8FF, 0xFFD54A };
    if (id <= PROMPT_TRIANGLE) return colours[id];
    if (id >= PROMPT_DPAD && id <= PROMPT_DPAD_LEFTRIGHT) return 0xE0E4F0;
    if (id >= PROMPT_PAD_A && id <= PROMPT_PAD_Y) return letters[id - PROMPT_PAD_A];
    if (id == PROMPT_LSTICK || id == PROMPT_RSTICK) return 0x949CB4;    /* the hollow top */
    return id >= PROMPT_KEY_UP ? 0xFFC050 : 0xC8CEDE;
}

/* a chip: its size, clear corners, its colour, something cut out of it */
static void check_chip(const prompt_t *p, int small, uint32_t rgb, const char *name)
{
    int h = small ? PROMPT_SMALL_H : PROMPT_H;
    CHECK(p, "%s: no chip", name);
    if (!p)
        return;
    CHECK(p->h == h && p->w >= h && p->w <= PROMPT_MAX_W, "%s: %dx%d", name, p->w, p->h);
    CHECK((at(p, 0, 0) >> 24) < 128 && (at(p, p->w - 1, p->h - 1) >> 24) < 128, "%s: corners not clear", name);
    int fill = 0, cut = 0;
    for (int y = 0; y < p->h; y++)
        for (int x = 0; x < p->w; x++) {
            uint32_t c = at(p, x, y);
            fill += c == (0xFF000000u | rgb);
            /* a hole: clear, with the face to its left and right */
            if ((c >> 24) == 0 && x > 0 && x < p->w - 1 && (at(p, x - 1, y) >> 24) + (at(p, x + 1, y) >> 24) > 0) {
                int l = 0, r = 0;
                for (int i = 0; i < x; i++) l |= (at(p, i, y) >> 24) == 255;
                for (int i = x + 1; i < p->w; i++) r |= (at(p, i, y) >> 24) == 255;
                cut += l && r;
            }
        }
    CHECK(fill >= (small ? 6 : 10), "%s: %d pixels of %06X", name, fill, (unsigned)rgb);
    CHECK(cut >= 1, "%s: nothing cut out", name);
}

static void checks(void)
{
    for (int id = 0; id < PROMPT_COUNT; id++) {
        const prompt_t *p = prompt_get(id, 0);
        check_shape(p, names[id]);
        CHECK(prompt_get(id, 0) == p, "%s: made twice", names[id]);
        if (id > PROMPT_TRIANGLE)
            CHECK(prompt_get(id, 1) == p, "%s: has a colour version", names[id]);
    }
    /* the face buttons: the symbol cut out of the white one, painted in
     * its colour on the dark one */
    for (int s = 0; s < 4; s++) {
        const prompt_t *w = prompt_get(PROMPT_CROSS + s, 0), *c = prompt_get(PROMPT_CROSS + s, 1);
        CHECK(c && c != w, "%s: no colour version", names[s]);
        if (!w || !c)
            continue;
        int cut = 0, sym = 0;
        for (int y = 2; y < 13; y++)
            for (int x = 3; x < 13; x++) {
                cut += (at(w, x, y) >> 24) == 0;
                sym += (at(c, x, y) & 0xFFFFFF) == colours[s] && (at(c, x, y) >> 24) == 255;
            }
        CHECK(cut >= 12, "%s: %d pixels cut out", names[s], cut);
        CHECK(sym >= 8, "%s: %d pixels of its colour", names[s], sym);
        CHECK((at(c, 8, 1) >> 24) == 255 && (at(c, 8, 1) & 0xFF) < 0x60, "%s: the colour face is not dark", names[s]);
    }
    /* keys with a character */
    for (int ch = 33; ch < 127; ch++) {
        char name[16];
        snprintf(name, sizeof name, "key %c", ch);
        const prompt_t *p = prompt_key(ch);
        if (ch == '.' || ch == ',' || ch == '\'' || ch == '`' || ch == '-' || ch == '_' || ch == '"' ||
            ch == ':' || ch == ';' || ch == '~' || ch == '^' || ch == '=' || ch == '!' || ch == '|' ||
            ch == '*' || ch == '+' || ch == '<' || ch == '>' || ch == '/' || ch == '\\' || ch == '(' ||
            ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}')
            CHECK(p && p->w == 16, "%s", name);     /* thin glyphs: no count of white pixels */
        else
            check_shape(p, name);
        if (p) {
            int cut = 0;
            for (int y = 1; y < 13; y++)
                for (int x = 2; x < 14; x++)
                    cut += (at(p, x, y) >> 24) == 0;
            CHECK(cut >= 2, "%s: the character is not cut out", name);
        }
    }
    CHECK(prompt_key('x') == prompt_key('X'), "keys: lower case is not the upper case key");
    CHECK(!prompt_key(' ') && !prompt_key(127) && !prompt_key(0), "keys: a key for no character");
    CHECK(!prompt_get(-1, 0) && !prompt_get(PROMPT_COUNT, 0), "prompt_get out of range");

    /* the apps' chips, 16 and 12 px */
    for (int small = 0; small < 2; small++) {
        char name[32];
        for (int id = 0; id < PROMPT_COUNT; id++) {
            snprintf(name, sizeof name, "chip %s%s", names[id], small ? " (12)" : "");
            const prompt_t *p = prompt_chip(id, small);
            check_chip(p, small, id == PROMPT_DPAD_UP || id == PROMPT_DPAD_DOWN || id == PROMPT_DPAD_LEFT ||
                       id == PROMPT_DPAD_RIGHT || id == PROMPT_DPAD_UPDOWN || id == PROMPT_DPAD_LEFTRIGHT
                       ? 0x5A6380 : chip_colour(id), name);
            CHECK(prompt_chip(id, small) == p && p != prompt_get(id, 0), "%s: made twice", name);
        }
        for (int ch = 33; ch < 127; ch++) {
            snprintf(name, sizeof name, "chip key %c%s", ch, small ? " (12)" : "");
            const prompt_t *p = prompt_chip_key(ch, small);
            check_chip(p, small, 0xFFC050, name);
            CHECK(p && p->w == (small ? 12 : 16), "%s: width", name);
        }
        CHECK(prompt_chip_key('q', small) == prompt_chip_key('Q', small), "chip keys: lower case");
        CHECK(!prompt_chip_key(' ', small) && !prompt_chip(PROMPT_COUNT, small), "chips: out of range");
    }
}

/* ---------------------------------------------------------------- the sheet */

#define SW 400                      /* at 1x */
#define SH 560
#define K  3

static uint32_t sheet[SH][SW];

static uint32_t blend(uint32_t b, uint32_t c)
{
    uint32_t a = c >> 24, out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int)(b >> sh & 255), y = (int)(c >> sh & 255);
        out |= (uint32_t)(x + (y - x) * (int)a / 255) << sh;
    }
    return out;
}

static int put(const prompt_t *p, int x, int y)
{
    if (!p)
        return x;
    for (int j = 0; j < p->h; j++)
        for (int i = 0; i < p->w; i++)
            if (x + i < SW && y + j < SH)
                sheet[y + j][x + i] = blend(sheet[y + j][x + i], at(p, i, j));
    return x + p->w;
}

static int text(int x, int y, const char *s, uint32_t c, const font_t *f)
{
    for (; *s; s++, x += f->width)
        for (int r = 0; r < f->height; r++)
            for (int b = 0; b < f->width; b++)
                if ((f->glyphs[(uint8_t)*s * f->height + r] >> (7 - b) & 1) && x + b < SW && y + r < SH)
                    sheet[y + r][x + b] = c;
    return x;
}

/* a row of prompts that wraps */
static int cx, cy;

static uint32_t bg = C_BAR, dim = C_DIM;
static int row_h = 22;

static void clear(uint32_t c)
{
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            sheet[y][x] = c;
    bg = c;
    cx = 8, cy = 2;
}

static void heading(const char *s)
{
    cy += cx > 8 ? row_h + 4 : 6;
    text(8, cy, s, dim, &font_console_6x12);
    cy += 16;
    cx = 8;
}

static void item(const prompt_t *p)
{
    if (!p)
        return;
    if (cx + p->w > SW - 8) {
        cx = 8;
        cy += row_h;
    }
    cx = put(p, cx, cy) + 6;
}

static void gap(void) { cx += 10; }

/* a hint of the menu: the prompt on the 8x16 text row, the label after it */
static int hint(int x, int y, const prompt_t *p, const char *label)
{
    x = put(p, x, y) + 4;
    return text(x, y, label, C_TEXT, &font_console_8x16) + 16;
}

static void draw_sheet(void)
{
    clear(C_BAR);
    dim = C_DIM;
    row_h = 22;
    heading("DualShock 4");
    for (int i = PROMPT_CROSS; i <= PROMPT_TRIANGLE; i++) item(prompt_get(i, 0));
    gap();
    for (int i = PROMPT_CROSS; i <= PROMPT_TRIANGLE; i++) item(prompt_get(i, 1));
    gap();
    for (int i = PROMPT_DPAD; i <= PROMPT_DPAD_LEFTRIGHT; i++) item(prompt_get(i, 0));
    cx = SW;
    for (int i = PROMPT_L1; i <= PROMPT_R2; i++) item(prompt_get(i, 0));
    gap();
    for (int i = PROMPT_L3; i <= PROMPT_RSTICK; i++) item(prompt_get(i, 0));
    gap();
    for (int i = PROMPT_OPTIONS; i <= PROMPT_TOUCHPAD; i++) item(prompt_get(i, 0));
    heading("Other pads");
    for (int i = PROMPT_PAD_A; i <= PROMPT_PAD_SELECT; i++) item(prompt_get(i, 0));
    heading("Keyboard");
    for (int i = PROMPT_KEY_UP; i <= PROMPT_KEY_RIGHT; i++) item(prompt_get(i, 0));
    gap();
    for (int i = PROMPT_KEY_ENTER; i <= PROMPT_KEY_PGDN; i++) item(prompt_get(i, 0));
    cx = SW;
    for (int i = 0; i < 12; i++) item(prompt_get(PROMPT_KEY_F1 + i, 0));
    cx = SW;
    for (int c = 'A'; c <= 'Z'; c++) item(prompt_key(c));
    cx = SW;
    for (int c = '0'; c <= '9'; c++) item(prompt_key(c));
    gap();
    for (const char *s = "+-*/=.,;:!?#$%&@<>()[]"; *s; s++) item(prompt_key(*s));

    /* the menu's bottom bar with each device */
    heading("In the menu");
    int y = cy;
    for (int r = 0; r < 3; r++, y += 24) {
        for (int x = 0; x < SW; x++)
            for (int j = -4; j < 20; j++)
                sheet[y + j][x] = C_BAR;
        int x = 8;
        if (r == 0) {
            x = hint(x, y, prompt_get(PROMPT_CROSS, 0), "Play");
            x = hint(x, y, prompt_get(PROMPT_SQUARE, 0), "Options");
            x = put(prompt_get(PROMPT_SHARE, 0), x, y) + 2;
            x = text(x, y, "+", C_TEXT, &font_console_8x16) + 2;
            hint(x, y, prompt_get(PROMPT_OPTIONS, 0), "Monitor");
        } else if (r == 1) {
            x = hint(x, y, prompt_get(PROMPT_KEY_ENTER, 0), "Play");
            x = hint(x, y, prompt_key('C'), "Options");
            hint(x, y, prompt_get(PROMPT_KEY_ESC, 0), "Monitor");
        } else {
            x = hint(x, y, prompt_get(PROMPT_CROSS, 1), "Select");
            x = hint(x, y, prompt_get(PROMPT_DPAD_LEFTRIGHT, 0), "Change");
            hint(x, y, prompt_get(PROMPT_CIRCLE, 1), "Back");
        }
    }
    cy = y;
}

/* the apps' colours */
#define A_BG    0x14161E
#define A_PANEL 0x1C2030
#define A_BAR   0x2A3048
#define A_TEXT  0xE0E4F0
#define A_DIM   0x8088A0
#define A_SEL   0x3050A0
#define S_DARK  0x0B0C0F            /* the Sound editor's bars */

static void band(int y, int h, uint32_t c)
{
    for (int j = y; j < y + h && j < SH; j++)
        for (int x = 0; x < SW; x++)
            sheet[j][x] = c;
}

/* a hint of the apps: chips, the label after them */
static int chip_hint(int x, int y, const prompt_t *a, const prompt_t *b, const char *label, int small, uint32_t c)
{
    x = put(a, x, y) + (b ? 1 : 0);
    if (b)
        x = put(b, x, y);
    x += small ? 3 : 4;
    return text(x, y, label, c, small ? &font_console_6x12 : &font_console_8x16) + (small ? 10 : 14);
}

static void draw_chips(void)
{
    clear(A_PANEL);
    dim = A_DIM;
    for (int small = 0; small < 2; small++) {
        row_h = small ? 16 : 22;
        const char *size = small ? " (12 px, 6x12)" : " (16 px, 8x16)";
        char h[48];
        snprintf(h, sizeof h, "DualShock 4%s", size);
        heading(h);
        for (int i = PROMPT_CROSS; i <= PROMPT_DPAD_LEFTRIGHT; i++) item(prompt_chip(i, small));
        gap();
        for (int i = PROMPT_L1; i <= PROMPT_TOUCHPAD; i++) item(prompt_chip(i, small));
        snprintf(h, sizeof h, "Other pads, keyboard%s", size);
        heading(h);
        for (int i = PROMPT_PAD_A; i <= PROMPT_PAD_SELECT; i++) item(prompt_chip(i, small));
        gap();
        for (int i = PROMPT_KEY_UP; i <= PROMPT_KEY_PGDN; i++) item(prompt_chip(i, small));
        gap();
        for (int i = 0; i < 12; i++) item(prompt_chip(PROMPT_KEY_F1 + i, small));
        gap();
        for (int c = 'A'; c <= 'Z'; c++) item(prompt_chip_key(c, small));
        gap();
        for (int c = '0'; c <= '9'; c++) item(prompt_chip_key(c, small));
        gap();
        for (const char *s = "+-*/=.,;:!?#$%&@<>()[]"; *s; s++) item(prompt_chip_key(*s, small));
    }

    /* in the apps: the SDK's tabs, the Sound editor's bottom line, a
     * footer of bm Code */
    heading("In the apps");
    int y = cy + 2;
    band(y - 2, 20, A_BAR);
    int x = 8;
    static const char *const tabs[] = { "code", "sprites", "map" };
    for (int i = 0; i < 3; i++) {
        if (i == 0)
            for (int j = y - 2; j < y + 18; j++)
                for (int k = x - 4; k < x + 16 + 4 + 32 + 4; k++)
                    sheet[j][k] = A_SEL;
        x = chip_hint(x, y, prompt_chip(PROMPT_KEY_F1 + i, 0), NULL, tabs[i], 0, i ? A_DIM : A_TEXT);
    }
    chip_hint(x, y, prompt_chip(PROMPT_KEY_ESC, 0), NULL, "menu", 0, A_DIM);
    y += 26;
    band(y - 2, 20, S_DARK);
    x = 8;
    x = chip_hint(x, y, prompt_chip(PROMPT_CROSS, 0), prompt_chip(PROMPT_DPAD_UPDOWN, 0), "+-1", 0, A_DIM);
    x = chip_hint(x, y, prompt_chip(PROMPT_TRIANGLE, 0), NULL, "play C4", 0, A_DIM);
    x = chip_hint(x, y, prompt_chip(PROMPT_OPTIONS, 0), NULL, "chord", 0, A_DIM);
    y += 26;
    band(y - 1, 14, A_BAR);
    x = 8;
    x = chip_hint(x, y, prompt_chip(PROMPT_KEY_ENTER, 1), NULL, "insert", 1, A_DIM);
    x = chip_hint(x, y, prompt_chip(PROMPT_KEY_UP, 1), prompt_chip(PROMPT_KEY_DOWN, 1), "choose", 1, A_DIM);
    x = chip_hint(x, y, prompt_chip(PROMPT_KEY_TAB, 1), NULL, "mode", 1, A_DIM);
    x = chip_hint(x, y, prompt_chip(PROMPT_KEY_CTRL, 1), prompt_chip_key('S', 1), "save", 1, A_DIM);
    chip_hint(x, y, prompt_chip(PROMPT_KEY_ESC, 1), NULL, "close", 1, A_DIM);
    cy = y + 18;
}

/* ---------------------------------------------------------------- PNG */

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t *buf = malloc(len + 4), hdr[4], crc[4];
    memcpy(buf, type, 4);
    if (len)
        memcpy(buf + 4, data, len);
    be32(hdr, len);
    be32(crc, crc32(buf, len + 4));
    fwrite(hdr, 1, 4, f);
    fwrite(buf, 1, len + 4, f);
    fwrite(crc, 1, 4, f);
    free(buf);
}

/* RGB, zlib with stored blocks: no compression, any viewer opens it */
static int write_png(const char *path, int w, int h, uint32_t (*pixel)(int x, int y))
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    uint32_t row = (uint32_t)w * 3 + 1, raw_len = row * (uint32_t)h;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = pixel(x, y);
            uint8_t *p = raw + y * row + 1 + x * 3;
            p[0] = c >> 16; p[1] = c >> 8; p[2] = (uint8_t)c;
        }
    }
    uint32_t blocks = (raw_len + 65534) / 65535, z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (uint32_t off = 0; off < raw_len; off += 65535) {
        uint32_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        q += n;
    }
    be32(q, b << 16 | a);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, z_len);
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    return fclose(f);
}

static uint32_t scaled(int x, int y) { return sheet[y / K][x / K]; }

int main(int argc, char **argv)
{
    checks();
    const char *dir = argc > 1 ? argv[1] : "build/prompts";
    char path[256];
    draw_sheet();
    snprintf(path, sizeof path, "%s/prompts.png", dir);
    if (write_png(path, SW * K, cy * K, scaled) != 0) {
        printf("FAIL: cannot write %s\n", path);
        return 1;
    }
    draw_chips();
    snprintf(path, sizeof path, "%s/chips.png", dir);
    if (write_png(path, SW * K, cy * K, scaled) != 0) {
        printf("FAIL: cannot write %s\n", path);
        return 1;
    }
    if (fails) {
        printf("prompts: %d failures\n", fails);
        return 1;
    }
    printf("prompts: %d prompts, 4 in colour, %d keys; the apps' chips, 16 and 12 px: ok; sheets in %s\n",
           PROMPT_COUNT, 126 - 33 + 1, dir);
    return 0;
}
