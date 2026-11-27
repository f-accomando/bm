/*
 * Shell SDK: the palettes of the Sprite tab (see shell_sdk_pal.h). Original code.
 */
#include "shell_sdk_pal.h"

#include <stdio.h>
#include <string.h>

#include "shell_sdk_sprite.h"

/* the colours of the NES as 0xRRGGBB, the same table as bm NES (core/src/bm/nes.c) */
static const uint32_t nes_rgb[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22, 0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5, 0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000 };

static const uint32_t set16[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };

enum { PAL_GREYS, PAL_CODE, PAL_SET16 };

static uint32_t code_rgb[SDK_PAL_CODE_MAX][16];
static char code_name[SDK_PAL_CODE_MAX][20];
static int code_count;
static int has_set16 = 1;
static int choice;                      /* 0 greys, 1 to code_count the code, then the 16 */

void sdk_pal_reset(void)
{
    code_count = 0;
    has_set16 = 1;
    choice = 0;
}

int sdk_pal_count(void) { return 1 + code_count + has_set16; }

static int kind_of(int i) { return i == 0 ? PAL_GREYS : i <= code_count ? PAL_CODE : PAL_SET16; }

/* The numbers of the data lines after the label (the label's own line included): at most max, each at most
 * `top`. Comments and empty lines are skipped; any other line ends the data. */
static int data_after(const char *s, size_t len, const char *label, long top, long *out, int max)
{
    static const char *const directives[] = { ".byte", ".db", ".word", ".dw" };
    const size_t ln = strlen(label);
    size_t p = 0;
    int found = 0, n = 0;
    while (p < len && n < max) {
        size_t end = p;
        while (end < len && s[end] != '\n')
            end++;
        size_t q = p, stop = p;
        while (stop < end && s[stop] != ';')
            stop++;
        while (q < stop && (s[q] == ' ' || s[q] == '\t'))
            q++;
        while (stop > q && (s[stop - 1] == ' ' || s[stop - 1] == '\t' || s[stop - 1] == '\r'))
            stop--;
        p = end + 1;
        if (!found) {
            if (stop - q <= ln || memcmp(s + q, label, ln) || s[q + ln] != ':')
                continue;
            found = 1;
            q += ln + 1;
            while (q < stop && (s[q] == ' ' || s[q] == '\t'))
                q++;
        }
        if (q == stop)
            continue;
        size_t d = 0;
        for (int i = 0; i < 4; i++) {
            const size_t m = strlen(directives[i]);
            if (stop - q > m && !memcmp(s + q, directives[i], m) && (s[q + m] == ' ' || s[q + m] == '\t'))
                d = m;
        }
        if (!d)
            break;
        for (q += d; q < stop && n < max;) {
            while (q < stop && (s[q] == ' ' || s[q] == '\t' || s[q] == ','))
                q++;
            int base = 10;
            if (q < stop && (s[q] == '$' || s[q] == '%'))
                base = s[q++] == '$' ? 16 : 2;
            long v = 0;
            int digits = 0;
            for (; q < stop; q++, digits++) {
                const char c = s[q];
                const int k = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                              c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
                if (k >= base)
                    break;
                v = v * base + k;
                if (v > top)
                    return n;
            }
            if (!digits || (q < stop && s[q] != ',' && s[q] != ' ' && s[q] != '\t'))
                return n;                       /* not a number: the data ends here */
            out[n++] = v;
        }
    }
    return n;
}

/* The next argument of a call of bm C: a number, a name of a colour or, on the SNES, rgb(r, g, b). -1: none of
 * them. A colour of the NES is its number (0 to 63), of the SNES its BGR555. */
static long c_value(const char **p, const char *end, int snes)
{
    static const struct { const char *name; int nes, snes; } names[] = {
        { "BLACK", 0x0F, 0x0000 }, { "DARK_GRAY", 0x00, 0x294A }, { "GRAY", 0x10, 0x56B5 }, { "WHITE", 0x30, 0x7FFF },
        { "RED", 0x16, 0x109F }, { "ORANGE", 0x27, 0x025F }, { "YELLOW", 0x28, 0x13BF }, { "GREEN", 0x1A, 0x12C4 },
        { "LIME", 0x2A, 0x1BEC }, { "CYAN", 0x2C, 0x7386 }, { "SKY", 0x21, 0x7ECC }, { "BLUE", 0x12, 0x7144 },
        { "NAVY", 0x02, 0x3840 }, { "PURPLE", 0x14, 0x60D2 }, { "PINK", 0x25, 0x5A1F }, { "BROWN", 0x07, 0x0930 } };
    const char *s = *p;
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == ',' || *s == '('))
        s++;
    const char *w = s;
    while (s < end && (*s == '_' || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')))
        s++;
    *p = s;
    const size_t n = (size_t)(s - w);
    if (!n)
        return -1;
    if (*w >= '0' && *w <= '9') {
        const int hex = n > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X');
        long v = 0;
        for (size_t i = hex ? 2 : 0; i < n; i++) {
            const char c = w[i];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                          c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (d >= (hex ? 16 : 10) || v > 0xFFFF)
                return -1;
            v = v * (hex ? 16 : 10) + d;
        }
        return v;
    }
    if (snes && n == 3 && !memcmp(w, "rgb", 3)) {
        long c[3];
        for (int i = 0; i < 3; i++)
            c[i] = c_value(p, end, snes);
        while (*p < end && (**p == ' ' || **p == ')'))
            (*p)++;
        return c[0] >= 0 && c[1] >= 0 && c[2] >= 0 && c[0] < 32 && c[1] < 32 && c[2] < 32 ?
                   c[0] | c[1] << 5 | c[2] << 10 : -1;
    }
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strlen(names[i].name) == n && !memcmp(names[i].name, w, n))
            return snes ? names[i].snes : names[i].nes;
    return -1;
}

static uint32_t rgb_of(long v, int snes)
{
    if (!snes)
        return nes_rgb[v & 63];
    const uint32_t r = (uint32_t)(v & 31) * 255 / 31, g = (uint32_t)(v >> 5 & 31) * 255 / 31;
    return r << 16 | g << 8 | (uint32_t)(v >> 10 & 31) * 255 / 31;
}

/* The palettes that the calls of a bm C source set: sprite_palette(n, a, b, c), the background_palette of the
 * NES, the palette(index, colour) of the sprites of the SNES, with the colour behind from background(colour).
 * Calls whose arguments are not plain values are left out. The sprite palettes come first. */
static void c_palettes(const char *src, size_t len, int snes)
{
    static const char *const calls[4] = { "sprite_palette(", "background_palette(", "background(", "palette(" };
    long colour[SDK_PAL_CODE_MAX * 2][16];
    int have[SDK_PAL_CODE_MAX * 2] = { 0 };
    long behind = snes ? 0 : 0x0F;
    memset(colour, 0, sizeof colour);
    const char *end = src + len;
    for (const char *p = src; p < end; p++) {
        if (p[0] == '/' && p + 1 < end && p[1] == '/') {
            while (p + 1 < end && p[1] != '\n')
                p++;
            continue;
        }
        if (p > src && (p[-1] == '_' || (p[-1] >= 'a' && p[-1] <= 'z')))
            continue;
        for (int k = 0; k < 4; k++) {
            const size_t n = strlen(calls[k]);
            if ((size_t)(end - p) < n || memcmp(p, calls[k], n))
                continue;
            const char *q = p + n;
            if (k == 2) {
                const long v = c_value(&q, end, snes);
                if (v >= 0)
                    behind = v;
            } else if (k == 3) {                    /* one colour of the sprites: 128 to 255 */
                const long i = c_value(&q, end, snes), v = c_value(&q, end, snes);
                if (snes && i >= 128 && i < 256 && v >= 0) {
                    colour[(i - 128) / 16][(i - 128) % 16] = v;
                    have[(i - 128) / 16] = 1;
                }
            } else if (!(snes && k == 1)) {
                long v[4];
                for (int i = 0; i < 4; i++)
                    v[i] = c_value(&q, end, snes);
                if (v[0] >= 0 && v[1] >= 0 && v[2] >= 0 && v[3] >= 0) {
                    const int at = snes ? (int)(v[0] & 7) : (int)(v[0] & 3) + (k == 0 ? 0 : 4);
                    memcpy(&colour[at][1], &v[1], 3 * sizeof v[0]);
                    have[at] = 1;
                }
            }
            break;
        }
    }
    for (int at = 0; at < SDK_PAL_CODE_MAX && code_count < SDK_PAL_CODE_MAX; at++) {
        if (!have[at])
            continue;
        snprintf(code_name[code_count], sizeof code_name[0], "%s%d of the code", !snes && at >= 4 ? "BG" : "SPR",
                 snes ? at : at % 4);
        colour[at][0] = behind;
        for (int c = 0; c < 16; c++)
            code_rgb[code_count][c] = rgb_of(colour[at][snes ? c : c % 4], snes);
        code_count++;
    }
}

void sdk_pal_load(int kind, const char *src, size_t len)
{
    char before[20];
    snprintf(before, sizeof before, "%s", sdk_pal_name());
    code_count = 0;
    has_set16 = kind != SDK_SPRITE_NES && kind != SDK_SPRITE_NES_C;
    long v[SDK_PAL_CODE_MAX * 16];
    if (kind == SDK_SPRITE_NES_C || kind == SDK_SPRITE_SNES_C)
        c_palettes(src, len, kind == SDK_SPRITE_SNES_C);
    else if (kind == SDK_SPRITE_NES) {
        const int n = data_after(src, len, "palette", 0x3F, v, 32);
        const int groups = n / 4;
        for (int i = 0; i < groups; i++) {          /* the sprite palettes first: they are the last four */
            const int g = (i + (groups > 4 ? 4 : 0)) % groups;
            snprintf(code_name[i], sizeof code_name[i], "%s%d of the code", g < 4 ? "BG" : "SPR", g % 4);
            for (int c = 0; c < 16; c++)
                code_rgb[i][c] = nes_rgb[v[g * 4 + c % 4]];
        }
        code_count = groups;
    } else if (kind == SDK_SPRITE_SNES) {
        const int n = data_after(src, len, "sprites_pal", 0x7FFF, v, SDK_PAL_CODE_MAX * 16);
        code_count = (n + 15) / 16;
        for (int i = 0; i < code_count; i++) {
            snprintf(code_name[i], sizeof code_name[i], "%d of the code", i);
            for (int c = 0; c < 16; c++) {
                const long w = i * 16 + c < n ? v[i * 16 + c] : 0;
                const uint32_t r = (uint32_t)(w & 31) * 255 / 31, g = (uint32_t)(w >> 5 & 31) * 255 / 31;
                code_rgb[i][c] = r << 16 | g << 8 | (uint32_t)(w >> 10 & 31) * 255 / 31;
            }
        }
    }
    const int count = sdk_pal_count();
    if (choice >= count)
        choice = 0;
    if (strcmp(before, sdk_pal_name()))             /* the same palette in its new place, else greys */
        for (choice = count - 1; choice > 0 && strcmp(before, sdk_pal_name()); choice--)
            ;
}

void sdk_pal_next(void) { choice = (choice + 1) % sdk_pal_count(); }

const char *sdk_pal_name(void)
{
    switch (kind_of(choice)) {
    case PAL_CODE: return code_name[choice - 1];
    case PAL_SET16: return "16 colours";
    default: return "greys";
    }
}

uint32_t sdk_pal_rgb(int number, int colours)
{
    const int kind = kind_of(choice);
    if (kind == PAL_CODE && number < 16)
        return code_rgb[choice - 1][number];
    if (kind == PAL_SET16 && number < 16)
        return set16[number];
    if (colours < 2)
        colours = 2;
    const uint32_t g = (uint32_t)(40 + 215 * number / (colours - 1));
    return g << 16 | g << 8 | g;
}
