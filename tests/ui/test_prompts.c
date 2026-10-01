/*
 * The button prompts (src/kernel/prompts.c) on the PC: checks every one,
 * then draws the whole set on the menu bar's colour, 3x as on a TV, into
 * a PNG (default build/prompts/prompts.png).
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

static void checks(void)
{
    static const uint32_t colours[4] = { 0x7EA6FF, 0xFF6B6B, 0xF28AE0, 0x3DDBB0 };
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
}

/* ---------------------------------------------------------------- the sheet */

#define SW 400                      /* at 1x */
#define SH 380
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
    for (int j = 0; j < PROMPT_H; j++)
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

static void heading(const char *s)
{
    cy += cx > 8 ? 26 : 6;
    text(8, cy, s, C_DIM, &font_console_6x12);
    cy += 16;
    cx = 8;
}

static void item(const prompt_t *p)
{
    if (!p)
        return;
    if (cx + p->w > SW - 8) {
        cx = 8;
        cy += 22;
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
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            sheet[y][x] = C_BAR;
    cx = 8, cy = 2;
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
    draw_sheet();
    const char *out = argc > 1 ? argv[1] : "build/prompts/prompts.png";
    if (write_png(out, SW * K, cy * K, scaled) != 0) {
        printf("FAIL: cannot write %s\n", out);
        return 1;
    }
    if (fails) {
        printf("prompts: %d failures\n", fails);
        return 1;
    }
    printf("prompts: %d prompts, 4 in colour, %d keys ok; sheet %s\n", PROMPT_COUNT, 126 - 33 + 1, out);
    return 0;
}
