#include "prompts.h"
#include "gfx/font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* colours, 0xRRGGBB */
#define FACE     0xF0F0F4           /* the menu's white */
#define LIP      0x8A8A98
#define OFF      0x6A6A78           /* a d-pad arm that is not meant, */
#define OFF_LIP  0x45454F           /* and its lip */
#define CAP      0x2C2C36           /* coloured face buttons: the dark face, */
#define RIM      0x5A5A6A           /* its rim and lip */
#define STICK    0xC4C4CE           /* the hollow top of a stick */
#define CUT_OUT  0xFF000000u        /* not a colour: cut out of the face */
/* the chips of the apps */
#define AMBER    0xFFC050           /* keyboard keys: the apps' accent */
#define NEUTRAL  0xC8CEDE           /* the pad's other buttons */
#define HOLLOW   0x949CB4           /* a stick's top */
#define D_ON     0xE0E4F0           /* d-pad: the arms meant, */
#define D_OFF    0x5A6380           /* the others, */
#define D_MARK   0x3E4560           /* and their arrows */

/* the DS4's own colours: cross, circle, square, triangle */
static const uint32_t symbol_rgb[4] = { 0x7EA6FF, 0xFF6B6B, 0xF28AE0, 0x3DDBB0 };
/* the lettered pads': A, B, X, Y */
static const uint32_t letter_rgb[4] = { 0x5BD47E, 0xFF6B6B, 0x5FA8FF, 0xFFD54A };
/* the RGB30's letters: A green, B blue, X red, Y yellow */
static const uint32_t rgb30_rgb[4] = { 0x5BD47E, 0x5FA8FF, 0xFF6B6B, 0xFFD54A };

/* ---------------------------------------------------------------- shapes */

/* signed distances, negative inside; a shape is a small tree */
enum { S_DISC, S_RRECT, S_CAPSULE, S_TRI, S_WEDGE, S_UNION, S_CUT, S_BOTH, S_RING, S_GROW, S_MOVE };

typedef struct shape {
    int kind;
    float p[6];
    const struct shape *l, *r;
} shape_t;

#define DISC(cx, cy, r)             (&(shape_t){ S_DISC, { cx, cy, r }, 0, 0 })
#define RRECT(x0, y0, x1, y1, r)    (&(shape_t){ S_RRECT, { x0, y0, x1, y1, r }, 0, 0 })
#define CAPSULE(ax, ay, bx, by, t)  (&(shape_t){ S_CAPSULE, { ax, ay, bx, by, t }, 0, 0 })
#define TRI(ax, ay, bx, by, cx, cy) (&(shape_t){ S_TRI, { ax, ay, bx, by, cx, cy }, 0, 0 })
/* the quarter around the direction (dx, dy) from (cx, cy), between the
 * two diagonals, g in from each */
#define WEDGE(cx, cy, dx, dy, g)    (&(shape_t){ S_WEDGE, { cx, cy, dx, dy, g }, 0, 0 })
#define UNION(a, b)                 (&(shape_t){ S_UNION, { 0 }, a, b })
#define CUT(a, b)                   (&(shape_t){ S_CUT, { 0 }, a, b })
#define BOTH(a, b)                  (&(shape_t){ S_BOTH, { 0 }, a, b })
#define RING(a, t)                  (&(shape_t){ S_RING, { t }, a, 0 })     /* half thickness t */
#define GROW(a, d)                  (&(shape_t){ S_GROW, { d }, a, 0 })
#define MOVE(a, dx, dy)             (&(shape_t){ S_MOVE, { dx, dy }, a, 0 })

static float len2(float x, float y) { return sqrtf(x * x + y * y); }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static float rrect(float px, float py, const float *p)
{
    float cx = (p[0] + p[2]) * 0.5f, cy = (p[1] + p[3]) * 0.5f;
    float hw = (p[2] - p[0]) * 0.5f, hh = (p[3] - p[1]) * 0.5f, r = p[4];
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return len2(ox, oy) + (in < 0 ? in : 0) - r;
}

static float capsule(float px, float py, const float *p)
{
    float pax = px - p[0], pay = py - p[1], bax = p[2] - p[0], bay = p[3] - p[1];
    float h = clamp01((pax * bax + pay * bay) / (bax * bax + bay * bay));
    return len2(pax - bax * h, pay - bay * h) - p[4];
}

static float tri(float px, float py, const float *p)
{
    float e[3][2], v[3][2], dmin = 1e9f, smin = 1e9f;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        e[i][0] = p[2 * j] - p[2 * i];
        e[i][1] = p[2 * j + 1] - p[2 * i + 1];
        v[i][0] = px - p[2 * i];
        v[i][1] = py - p[2 * i + 1];
    }
    float s = e[0][0] * e[2][1] - e[0][1] * e[2][0] > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < 3; i++) {
        float h = clamp01((v[i][0] * e[i][0] + v[i][1] * e[i][1]) /
                          (e[i][0] * e[i][0] + e[i][1] * e[i][1]));
        float qx = v[i][0] - e[i][0] * h, qy = v[i][1] - e[i][1] * h;
        float d = qx * qx + qy * qy, side = s * (v[i][0] * e[i][1] - v[i][1] * e[i][0]);
        if (d < dmin) dmin = d;
        if (side < smin) smin = side;
    }
    return -sqrtf(dmin) * (smin > 0 ? 1.0f : -1.0f);
}

static float sd(const shape_t *s, float x, float y)
{
    const float *p = s->p;
    switch (s->kind) {
    case S_DISC:    return len2(x - p[0], y - p[1]) - p[2];
    case S_RRECT:   return rrect(x, y, p);
    case S_CAPSULE: return capsule(x, y, p);
    case S_TRI:     return tri(x, y, p);
    case S_WEDGE: {
        float along = (x - p[0]) * p[2] + (y - p[1]) * p[3];
        float across = fabsf((x - p[0]) * p[3] - (y - p[1]) * p[2]);
        return (across - along + p[4]) * 0.70710678f;
    }
    case S_UNION: { float a = sd(s->l, x, y), b = sd(s->r, x, y); return a < b ? a : b; }
    case S_CUT:   { float a = sd(s->l, x, y), b = -sd(s->r, x, y); return a > b ? a : b; }
    case S_BOTH:  { float a = sd(s->l, x, y), b = sd(s->r, x, y); return a > b ? a : b; }
    case S_RING:    return fabsf(sd(s->l, x, y)) - p[0];
    case S_GROW:    return sd(s->l, x, y) - p[0];
    case S_MOVE:    return sd(s->l, x - p[0], y - p[1]);
    }
    return 1.0f;
}

/* ---------------------------------------------------------------- canvas */

/* the prompt being made: its look and size, its pixels premultiplied */
static struct {
    int chip;                       /* the look: 0 raised (the menu), 1 chip (the apps) */
    int colour;                     /* raised: the face buttons in their colours */
    int h;                          /* PROMPT_H or PROMPT_SMALL_H */
    float top, fh;                  /* the face: its top row and height */
} st;
static int cw;
static float cr[PROMPT_MAX_W * PROMPT_H], cg[PROMPT_MAX_W * PROMPT_H];
static float cb[PROMPT_MAX_W * PROMPT_H], ca[PROMPT_MAX_W * PROMPT_H];

#define SS 8                        /* 8x8 samples per pixel near the edges */

static float coverage(const shape_t *s, int x, int y)
{
    /* the distances are exact or smaller than the real ones: away from
     * the edge a pixel is all in or all out */
    float d = sd(s, x + 0.5f, y + 0.5f);
    if (d > 0.75f) return 0.0f;
    if (d < -0.75f) return 1.0f;
    int n = 0;
    for (int j = 0; j < SS; j++)
        for (int i = 0; i < SS; i++)
            n += sd(s, x + (i + 0.5f) / SS, y + (j + 0.5f) / SS) <= 0.0f;
    return (float)n / (SS * SS);
}

/* source over, or cut out (destination out) with CUT_OUT */
static void put(int i, float a, uint32_t rgb)
{
    if (rgb == CUT_OUT) {
        cr[i] *= 1 - a; cg[i] *= 1 - a; cb[i] *= 1 - a; ca[i] *= 1 - a;
        return;
    }
    cr[i] = (rgb >> 16 & 255) / 255.0f * a + cr[i] * (1 - a);
    cg[i] = (rgb >> 8 & 255) / 255.0f * a + cg[i] * (1 - a);
    cb[i] = (rgb & 255) / 255.0f * a + cb[i] * (1 - a);
    ca[i] = a + ca[i] * (1 - a);
}

static void paint(const shape_t *s, uint32_t rgb)
{
    for (int y = 0; y < st.h; y++)
        for (int x = 0; x < cw; x++) {
            float a = coverage(s, x, y);
            if (a > 0)
                put(y * cw + x, a, rgb);
        }
}

/* a button standing out: the face on its lip, 2 px lower */
static void raised(const shape_t *face, uint32_t top, uint32_t lip)
{
    paint(MOVE(face, 0, 2), lip);
    paint(face, top);
}

/* the coloured look: a dark face inside a 1 px rim, on a lip of the rim's
 * colour */
static void raised_dark(const shape_t *face)
{
    paint(MOVE(face, 0, 2), RIM);
    paint(face, RIM);
    paint(GROW(face, -1), CAP);
}

/* ---------------------------------------------------------------- labels */

static uint8_t glyph_row(const font_t *f, char ch, int row, int bold)
{
    uint8_t bits = f->glyphs[(uint8_t)ch * f->height + row];
    return bold ? (uint8_t)(bits | bits >> 1) : bits;
}

/* lit columns of a text: [*x0, *x1), from its left edge */
static void ink(const font_t *f, const char *s, int bold, int *x0, int *x1)
{
    *x0 = 1000;
    *x1 = 0;
    for (int k = 0; s[k]; k++)
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = glyph_row(f, s[k], r, bold);
            for (int b = 0; b < 8; b++)
                if (bits >> (7 - b) & 1) {
                    int x = k * f->width + b;
                    if (x < *x0) *x0 = x;
                    if (x + 1 > *x1) *x1 = x + 1;
                }
        }
    if (*x1 < *x0)
        *x0 = *x1 = 0;
}

static int ink_width(const font_t *f, const char *s, int bold)
{
    int x0, x1;
    ink(f, s, bold, &x0, &x1);
    return x1 - x0;
}

/* a text centred on (cx, cy): across by its ink, down by the capitals
 * (rows 2-11 of 8x16, 2-9 of 6x12); whole pixels */
static void label(const font_t *f, const char *s, int bold, float cx, float cy, uint32_t rgb)
{
    int x0, x1;
    ink(f, s, bold, &x0, &x1);
    float caps = f->height == 16 ? 7.0f : 6.0f;
    int ox = (int)floorf(cx - (x0 + x1) * 0.5f + 0.5f), oy = (int)floorf(cy - caps + 0.5f);
    if (s[0] && !s[1]) {
        /* one character low or high in its cell (_ , '): by its own rows */
        int y0 = -1, y1 = 0;
        for (int r = 0; r < f->height; r++)
            if (glyph_row(f, s[0], r, bold)) {
                if (y0 < 0) y0 = r;
                y1 = r + 1;
            }
        if (y0 >= 0 && (oy + y0 < st.top + 1 || oy + y1 > st.top + st.fh - 1))
            oy = (int)floorf(cy - (y0 + y1) * 0.5f + 0.5f);
    }
    for (int k = 0; s[k]; k++)
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = glyph_row(f, s[k], r, bold);
            for (int b = 0; b < 8; b++) {
                int x = ox + k * f->width + b, y = oy + r;
                if ((bits >> (7 - b) & 1) && x >= 0 && x < cw && y >= 0 && y < st.h)
                    put(y * cw + x, 1.0f, rgb);
            }
        }
}

/* ---------------------------------------------------------------- prompts */

#define BIG (st.h == PROMPT_H)

static float face_cy(void) { return st.top + st.fh * 0.5f; }

/* a button's face: raised (white on its lip) or a flat chip of colour rgb */
static void face(const shape_t *shape, uint32_t rgb)
{
    if (st.chip)
        paint(shape, rgb);
    else
        raised(shape, FACE, LIP);
}

/* the four symbols of the DS4 at (cx, cy), s = 1 at 16 px */
static void symbol(int which, float cx, float cy, float s, uint32_t rgb)
{
    float t = BIG ? 0.95f : 0.8f, k = 2.9f * s;
    switch (which) {
    case 0:
        paint(UNION(CAPSULE(cx - k, cy - k, cx + k, cy + k, t),
                    CAPSULE(cx + k, cy - k, cx - k, cy + k, t)), rgb);
        break;
    case 1:
        paint(RING(DISC(cx, cy, 3.2f * s), t), rgb);
        break;
    case 2:
        t = BIG ? 0.9f : 0.75f;
        paint(RING(RRECT(cx - 3.1f * s, cy - 3.1f * s, cx + 3.1f * s, cy + 3.1f * s, 0.6f * s), t), rgb);
        break;
    case 3: {
        t = BIG ? 0.9f : 0.75f;
        float ax = cx, ay = cy - 3.7f * s, bx = cx - 3.9f * s, by = cy + 2.5f * s;
        float ex = cx + 3.9f * s, ey = cy + 2.5f * s;
        paint(UNION(UNION(CAPSULE(ax, ay, bx, by, t), CAPSULE(bx, by, ex, ey, t)),
                    CAPSULE(ex, ey, ax, ay, t)), rgb);
        break;
    }
    }
}

static void face_button(int which)
{
    cw = BIG ? 16 : 12;
    float cx = cw * 0.5f, cy = face_cy(), s = BIG ? 1.0f : 0.72f;
    const shape_t *disc = DISC(cx, cy, BIG ? 7 : 5);
    if (!st.chip && st.colour) {
        raised_dark(disc);
        symbol(which, cx, cy, s, symbol_rgb[which]);
    } else {
        face(disc, symbol_rgb[which]);
        symbol(which, cx, cy, s, CUT_OUT);
    }
}

/* the d-pad: a cross with an arrow on each arm, on whole pixels; the
 * arms in `on` (bit 0 up, 1 down, 2 left, 3 right) light, the others grey */
static void dpad(int on)
{
    static const float dirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    /* 16 px: each arrow a row (column) 2 px across at the tip, then one 4
     * px across; 12 px: only the tip */
    static const float arrows[4][2][4] = {
        { { 7, 2, 9, 3 }, { 6, 3, 10, 4 } },
        { { 7, 11, 9, 12 }, { 6, 10, 10, 11 } },
        { { 2, 6, 3, 8 }, { 3, 5, 4, 9 } },
        { { 13, 6, 14, 8 }, { 12, 5, 13, 9 } },
    };
    static const float small_arrows[4][4] = {
        { 5, 2, 7, 3 }, { 5, 9, 7, 10 }, { 2, 5, 3, 7 }, { 9, 5, 10, 7 },
    };
    float c = BIG ? 8.0f : 6.0f;
    const shape_t *plus = BIG ? UNION(RRECT(5, 0, 11, 14, 1), RRECT(1, 4, 15, 10, 1))
                              : UNION(RRECT(4, 1, 8, 11, 0.75f), RRECT(1, 4, 11, 8, 0.75f));
    shape_t w[4];                                   /* the quarters meant, or nothing */
    for (int d = 0; d < 4; d++)
        w[d] = on >> d & 1 ? (shape_t){ S_WEDGE, { c, BIG ? 7 : 6, dirs[d][0], dirs[d][1], 0 }, 0, 0 }
                           : (shape_t){ S_DISC, { -99, -99, 0 }, 0, 0 };
    /* the arms meant, as one shape: no seam where two meet */
    const shape_t *lit = BOTH(plus, UNION(UNION(&w[0], &w[1]), UNION(&w[2], &w[3])));
    cw = BIG ? 16 : 12;
    if (st.chip) {
        paint(plus, on == 15 ? D_ON : D_OFF);
        if (on != 15)
            paint(lit, D_ON);
    } else if (on != 15) {
        paint(MOVE(plus, 0, 2), OFF_LIP);
        paint(MOVE(lit, 0, 2), LIP);
        paint(plus, OFF);
        paint(lit, FACE);
    } else {
        raised(plus, FACE, LIP);
    }
    for (int d = 0; d < 4; d++) {
        uint32_t mark = on >> d & 1 ? CUT_OUT : st.chip ? D_MARK : OFF_LIP;
        if (!BIG) {
            const float *r = small_arrows[d];
            paint(RRECT(r[0], r[1], r[2], r[3], 0), mark);
            continue;
        }
        for (int k = 0; k < 2; k++) {
            const float *r = arrows[d][k];
            paint(RRECT(r[0], r[1], r[2], r[3], 0), mark);
        }
    }
}

/* the font of a label: one character 8x16 bold at 16 px, else 6x12 */
static const font_t *label_font(int one)
{
    return one && BIG ? &font_console_8x16 : &font_console_6x12;
}

/* a key-like face w px wide, corner radius r, with a text cut out (bold:
 * one character, in label_font) */
static void plate(int w, float r, const char *text, int bold, uint32_t rgb)
{
    cw = w;
    face(RRECT(0, st.top, w, st.top + st.fh, r), rgb);
    if (text)
        label(label_font(bold), text, bold, w * 0.5f, face_cy(), CUT_OUT);
}

static int cw_min(void) { return BIG ? 16 : 12; }

/* the width of a key for a label in 6x12: 5 px each side (4 at 12 px), even */
static int key_w(const char *text, int bold)
{
    int w = ink_width(&font_console_6x12, text, bold) + (BIG ? 10 : 8);
    w = w < cw_min() ? cw_min() : w;
    return (w + 1) & ~1;
}

static float key_r(void) { return BIG ? 3 : 2.5f; }

static void key_label(const char *text, int w)
{
    plate(w ? w : key_w(text, 0), key_r(), text, 0, AMBER);
}

/* a round button with a label: one character (8x16 bold, 6x12 bold at 12
 * px) or two in 6x12; at 12 px two characters need a pill */
static void round_button(const char *text, uint32_t rgb)
{
    int one = text[1] == 0;
    if (!one && !BIG) {
        plate(key_w(text, 0), st.fh * 0.5f, text, 0, rgb);
        return;
    }
    cw = BIG ? 16 : 12;
    face(DISC(cw * 0.5f, face_cy(), BIG ? 7 : 5), rgb);
    label(label_font(one), text, one, cw * 0.5f, face_cy(), CUT_OUT);
}

/* the RGB30's face button: dark, a grey rim, the letter in its colour;
 * raised on the rim's lip in the menu, flat in the apps */
static void rgb30_button(int which)
{
    static const char *const letter[4] = { "A", "B", "X", "Y" };
    cw = BIG ? 16 : 12;
    float cx = cw * 0.5f, cy = face_cy();
    const shape_t *disc = DISC(cx, cy, BIG ? 7 : 5);
    if (st.chip) {
        paint(disc, RIM);
        paint(GROW(disc, -1), CAP);
    } else {
        raised_dark(disc);
    }
    label(label_font(1), letter[which], 1, cx, cy, rgb30_rgb[which]);
}

static void arrow_key(int d)
{
    cw = BIG ? 16 : 12;
    face(RRECT(0, st.top, cw, st.top + st.fh, key_r()), AMBER);
    /* a triangle pointing up, turned */
    float s = BIG ? 1.0f : 0.7f;
    float pts[3][2] = { { 0, -2.7f * s }, { -3.6f * s, 1.9f * s }, { 3.6f * s, 1.9f * s } };
    float q[3][2];
    for (int i = 0; i < 3; i++) {
        float x = pts[i][0], y = pts[i][1];
        switch (d) {
        case 0: q[i][0] = x;  q[i][1] = y;  break;  /* up */
        case 1: q[i][0] = x;  q[i][1] = -y; break;  /* down */
        case 2: q[i][0] = y;  q[i][1] = x;  break;  /* left */
        default: q[i][0] = -y; q[i][1] = x; break;  /* right */
        }
    }
    float cx = cw * 0.5f, cy = face_cy();
    float oy = (d == 0 ? 0.3f : d == 1 ? -0.3f : 0) * s;  /* the middle of the triangle's box */
    paint(TRI(cx + q[0][0], cy + q[0][1] + oy, cx + q[1][0], cy + q[1][1] + oy,
              cx + q[2][0], cy + q[2][1] + oy), CUT_OUT);
}

static uint32_t chip_rgb(int id)
{
    if (id <= PROMPT_TRIANGLE)
        return symbol_rgb[id - PROMPT_CROSS];
    if (id >= PROMPT_PAD_A && id <= PROMPT_PAD_Y)
        return letter_rgb[id - PROMPT_PAD_A];
    if (id >= PROMPT_RGB30_A)
        return rgb30_rgb[id - PROMPT_RGB30_A];
    return id >= PROMPT_KEY_UP ? AMBER : NEUTRAL;
}

static void make(int id)
{
    static const char *const fkeys[12] = {
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    };
    static const char *const names[] = {
        [PROMPT_L1] = "L1", [PROMPT_R1] = "R1", [PROMPT_L2] = "L2", [PROMPT_R2] = "R2",
        [PROMPT_L3] = "L3", [PROMPT_R3] = "R3", [PROMPT_LSTICK] = "L", [PROMPT_RSTICK] = "R",
        [PROMPT_OPTIONS] = "OPTIONS", [PROMPT_SHARE] = "SHARE", [PROMPT_PS] = "PS",
        [PROMPT_PAD_A] = "A", [PROMPT_PAD_B] = "B", [PROMPT_PAD_X] = "X", [PROMPT_PAD_Y] = "Y",
        [PROMPT_PAD_START] = "START", [PROMPT_PAD_SELECT] = "SELECT",
    };
    uint32_t rgb = chip_rgb(id);
    switch (id) {
    case PROMPT_CROSS: case PROMPT_CIRCLE: case PROMPT_SQUARE: case PROMPT_TRIANGLE:
        face_button(id - PROMPT_CROSS);
        break;
    case PROMPT_DPAD:           dpad(15); break;
    case PROMPT_DPAD_UP:        dpad(1); break;
    case PROMPT_DPAD_DOWN:      dpad(2); break;
    case PROMPT_DPAD_LEFT:      dpad(4); break;
    case PROMPT_DPAD_RIGHT:     dpad(8); break;
    case PROMPT_DPAD_UPDOWN:    dpad(3); break;
    case PROMPT_DPAD_LEFTRIGHT: dpad(12); break;
    case PROMPT_L1: case PROMPT_R1:                 /* bumpers: a flat bar */
        plate(BIG ? 24 : key_w(names[id], 1), key_r(), names[id], 1, rgb);
        break;
    case PROMPT_L2: case PROMPT_R2: {               /* triggers: rounder on top */
        cw = BIG ? 24 : key_w(names[id], 1);
        float t = st.top, b = st.top + st.fh, big = BIG ? 6 : 4.5f, small = BIG ? 2 : 1.5f;
        face(BOTH(RRECT(0, t, cw, b + big, big), RRECT(0, t - big, cw, b, small)), rgb);
        label(label_font(1), names[id], 1, cw * 0.5f, face_cy(), CUT_OUT);
        break;
    }
    case PROMPT_L3: case PROMPT_R3: case PROMPT_PS:
        round_button(names[id], rgb);
        break;
    case PROMPT_LSTICK: case PROMPT_RSTICK: {       /* the stick: a hollow top, its letter */
        cw = BIG ? 16 : 12;
        float cx = cw * 0.5f, cy = face_cy();
        face(DISC(cx, cy, BIG ? 7 : 5), rgb);
        paint(DISC(cx, cy, BIG ? 5.5f : 4), st.chip ? HOLLOW : STICK);
        label(label_font(1), names[id], 1, cx, cy, CUT_OUT);
        break;
    }
    case PROMPT_OPTIONS: case PROMPT_SHARE: case PROMPT_PAD_START: case PROMPT_PAD_SELECT:
        plate(key_w(names[id], 0) - 2, BIG ? 4 : 3, names[id], 0, rgb);
        break;
    case PROMPT_TOUCHPAD:                           /* a wide pad, its surface inset */
        if (BIG) {
            plate(30, 3, NULL, 0, rgb);
            paint(RING(RRECT(3.5f, 2.5f, 26.5f, 11.5f, 1.5f), 0.5f), CUT_OUT);
        } else {
            plate(22, 2.5f, NULL, 0, rgb);
            paint(RING(RRECT(2.5f, st.top + 1.5f, 19.5f, st.top + st.fh - 1.5f, 1), 0.5f), CUT_OUT);
        }
        break;
    case PROMPT_PAD_A: case PROMPT_PAD_B: case PROMPT_PAD_X: case PROMPT_PAD_Y:
        round_button(names[id], rgb);
        break;
    case PROMPT_KEY_UP:    arrow_key(0); break;
    case PROMPT_KEY_DOWN:  arrow_key(1); break;
    case PROMPT_KEY_LEFT:  arrow_key(2); break;
    case PROMPT_KEY_RIGHT: arrow_key(3); break;
    case PROMPT_KEY_ENTER: key_label("Enter", 0); break;
    case PROMPT_KEY_ESC:   key_label("Esc", 0); break;
    case PROMPT_KEY_SPACE: key_label("Space", BIG ? 48 : 36); break;
    case PROMPT_KEY_TAB:   key_label("Tab", 0); break;
    case PROMPT_KEY_BACKSPACE: {                    /* a long arrow to the left */
        cw = BIG ? 28 : 22;
        float cy = face_cy();
        face(RRECT(0, st.top, cw, st.top + st.fh, key_r()), rgb);
        if (BIG)
            paint(UNION(TRI(7.5f, cy, 12, cy - 3.5f, 12, cy + 3.5f),
                        RRECT(11, cy - 1, 21, cy + 1, 0.5f)), CUT_OUT);
        else
            paint(UNION(TRI(5, cy, 9, cy - 3, 9, cy + 3), RRECT(8.5f, cy - 1, 16.5f, cy + 1, 0.4f)), CUT_OUT);
        break;
    }
    case PROMPT_KEY_SHIFT: key_label("Shift", 0); break;
    case PROMPT_KEY_CTRL:  key_label("Ctrl", 0); break;
    case PROMPT_KEY_ALT:   key_label("Alt", 0); break;
    case PROMPT_KEY_DEL:   key_label("Del", 0); break;
    case PROMPT_KEY_HOME:  key_label("Home", 0); break;
    case PROMPT_KEY_END:   key_label("End", 0); break;
    case PROMPT_KEY_PGUP:  key_label("PgUp", 0); break;
    case PROMPT_KEY_PGDN:  key_label("PgDn", 0); break;
    case PROMPT_RGB30_A: case PROMPT_RGB30_B: case PROMPT_RGB30_X: case PROMPT_RGB30_Y:
        rgb30_button(id - PROMPT_RGB30_A);
        break;
    default:              key_label(fkeys[id - PROMPT_KEY_F1], 0); break;
    }
}

/* ---------------------------------------------------------------- cache */

/* the menu's set; the apps' chips, 16 and 12 px */
static prompt_t made[PROMPT_COUNT][2], keys[94];
static prompt_t chips[2][PROMPT_COUNT], chip_keys[2][94];

static const prompt_t *keep(prompt_t *p)
{
    uint32_t *px = malloc((size_t)cw * st.h * 4);
    if (!px)
        return NULL;
    for (int i = 0; i < cw * st.h; i++) {
        float a = ca[i];
        uint32_t A = (uint32_t)(clamp01(a) * 255.0f + 0.5f), r = 0, g = 0, b = 0;
        if (a > 0.0f) {                             /* back from premultiplied */
            r = (uint32_t)(clamp01(cr[i] / a) * 255.0f + 0.5f);
            g = (uint32_t)(clamp01(cg[i] / a) * 255.0f + 0.5f);
            b = (uint32_t)(clamp01(cb[i] / a) * 255.0f + 0.5f);
        }
        px[i] = A << 24 | r << 16 | g << 8 | b;
    }
    p->w = cw;
    p->h = st.h;
    p->px = px;
    return p;
}

/* a new prompt: the look, the size, an empty canvas */
static void begin(int chip, int colour, int small)
{
    st.chip = chip;
    st.colour = colour;
    st.h = small ? PROMPT_SMALL_H : PROMPT_H;
    st.top = small ? 1 : 0;
    st.fh = small ? 10 : 14;
    memset(cr, 0, sizeof cr);
    memset(cg, 0, sizeof cg);
    memset(cb, 0, sizeof cb);
    memset(ca, 0, sizeof ca);
}

/* a key with a character: 33..126, letters in upper case; -1 if none */
static int key_index(int c)
{
    if (c >= 'a' && c <= 'z')
        c -= 32;
    return c < 33 || c > 126 ? -1 : c - 33;
}

static const prompt_t *char_key(prompt_t *p, int c)
{
    if (p->px)
        return p;
    char s[2] = { (char)c, 0 };
    plate(BIG ? 16 : 12, key_r(), s, 1, AMBER);
    return keep(p);
}

const prompt_t *prompt_get(int id, int colour)
{
    if (id < 0 || id >= PROMPT_COUNT)
        return NULL;
    colour = colour && id <= PROMPT_TRIANGLE;
    prompt_t *p = &made[id][colour];
    if (p->px)
        return p;
    begin(0, colour, 0);
    make(id);
    return keep(p);
}

const prompt_t *prompt_key(int c)
{
    int i = key_index(c);
    if (i < 0)
        return NULL;
    if (!keys[i].px)
        begin(0, 0, 0);
    return char_key(&keys[i], i + 33);
}

const prompt_t *prompt_chip(int id, int small)
{
    if (id < 0 || id >= PROMPT_COUNT)
        return NULL;
    small = small != 0;
    prompt_t *p = &chips[small][id];
    if (p->px)
        return p;
    begin(1, 0, small);
    make(id);
    return keep(p);
}

const prompt_t *prompt_chip_key(int c, int small)
{
    int i = key_index(c);
    if (i < 0)
        return NULL;
    small = small != 0;
    if (!chip_keys[small][i].px)
        begin(1, 0, small);
    return char_key(&chip_keys[small][i], i + 33);
}
