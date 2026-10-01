/*
 * Sprite recipes (see sprite.h). A recipe draws materials (1 = the main
 * colour, 2 = the second, others its own) on a grid with shapes in unit
 * coordinates (0..1, pixel centres), so the same recipe works at 8, 16 and
 * 32 pixels. Then every material gets a ramp of five shades from its
 * colour, chosen by a rough surface normal (light from the top left), and
 * the shape an outline one shade darker than its darkest.
 */
#include "sprite.h"
#include "text.h"

#include <math.h>
#include <string.h>

#define NMAT 16
#define N (SPR_MAX * SPR_MAX)

typedef struct {
    int w, h;
    int wrap;                   /* tiles: neighbours wrap around, no outline */
    uint8_t m[N];               /* material, 0 = empty */
    uint8_t lv[N];              /* forced shade + 1 (0 = from the light) */
    uint32_t rng;
    uint32_t seed;              /* the variant: some recipes make frames of it */
    uint32_t col[NMAT];
    uint8_t flat[NMAT];         /* one shade: eyes, sparks */
    uint8_t soft[NMAT];         /* no outline next to it: glows, glass */
} cv_t;

/* ---------------------------------------------------------------- helpers */

static float rnd(cv_t *c)
{
    uint32_t x = c->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static int rint_(cv_t *c, int a, int b) { return a + (int)(rnd(c) * (float)(b - a + 1)); }
static float rf(cv_t *c, float a, float b) { return a + rnd(c) * (b - a); }
static uint32_t pick(cv_t *c, const uint32_t *list, int n) { return list[rint_(c, 0, n - 1)]; }

static float U(cv_t *c, int x) { return ((float)x + 0.5f) / (float)c->w; }
static float V(cv_t *c, int y) { return ((float)y + 0.5f) / (float)c->h; }

static void set(cv_t *c, int x, int y, int m)
{
    if (c->wrap) {
        x = (x % c->w + c->w) % c->w;
        y = (y % c->h + c->h) % c->h;
    } else if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
        return;
    }
    c->m[y * c->w + x] = (uint8_t)m;
}

static int get(const cv_t *c, int x, int y)
{
    if (c->wrap) {
        x = (x % c->w + c->w) % c->w;
        y = (y % c->h + c->h) % c->h;
    } else if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
        return 0;
    }
    return c->m[y * c->w + x];
}

static void shade(cv_t *c, int x, int y, int level)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h)
        c->lv[y * c->w + x] = (uint8_t)(level + 1);
}

static void ell(cv_t *c, float cx, float cy, float rx, float ry, int m)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float dx = (U(c, x) - cx) / rx, dy = (V(c, y) - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f)
                c->m[y * c->w + x] = (uint8_t)m;
        }
}

static void box(cv_t *c, float x0, float y0, float x1, float y1, int m)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = U(c, x), v = V(c, y);
            if (u >= x0 && u < x1 && v >= y0 && v < y1)
                c->m[y * c->w + x] = (uint8_t)m;
        }
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void tri(cv_t *c, float ax, float ay, float bx, float by, float qx, float qy, int m)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = U(c, x), v = V(c, y);
            float e0 = edge(ax, ay, bx, by, u, v), e1 = edge(bx, by, qx, qy, u, v),
                  e2 = edge(qx, qy, ax, ay, u, v);
            if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
                c->m[y * c->w + x] = (uint8_t)m;
        }
}

/* a thick line: pixels closer than r to the segment */
static void seg(cv_t *c, float x0, float y0, float x1, float y1, float r, int m)
{
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = U(c, x), v = V(c, y);
            float t = l2 > 0 ? ((u - x0) * dx + (v - y0) * dy) / l2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float ex = x0 + t * dx - u, ey = y0 + t * dy - v;
            if (ex * ex + ey * ey <= r * r)
                c->m[y * c->w + x] = (uint8_t)m;
        }
}

/* point in polygon (even-odd), n points x,y */
static void poly(cv_t *c, const float *p, int n, int m)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = U(c, x), v = V(c, y);
            int in = 0;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                float xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
                if ((yi > v) != (yj > v) && u < (xj - xi) * (v - yi) / (yj - yi) + xi)
                    in = !in;
            }
            if (in)
                c->m[y * c->w + x] = (uint8_t)m;
        }
}

/* the right half becomes the mirror of the left half */
static void mirror(cv_t *c)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w / 2; x++) {
            c->m[y * c->w + c->w - 1 - x] = c->m[y * c->w + x];
            c->lv[y * c->w + c->w - 1 - x] = c->lv[y * c->w + x];
        }
}

/* a pair of eyes at unit height v, unit distance d from the centre: one
 * pixel at 8, 1x2 at 16, 2x3 at 32 */
static void eyes(cv_t *c, float v, float d, int m)
{
    int y = (int)(v * (float)c->h);
    int xl = (int)((0.5f - d) * (float)c->w), xr = c->w - 1 - xl;
    int sw = c->w >= 32 ? 2 : 1, sh = c->w >= 32 ? 3 : c->w >= 16 ? 2 : 1;
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            set(c, xl + i, y + j, m);
            set(c, xr - i, y + j, m);
        }
}

/* the share of `m` pixels: a recipe that came out empty at 8x8 tries again */
static int count(const cv_t *c)
{
    int n = 0;
    for (int i = 0; i < c->w * c->h; i++)
        n += c->m[i] != 0;
    return n;
}

/* ---------------------------------------------------------------- colours */

typedef struct { const char *w; uint32_t rgb; } cname_t;

static const cname_t colors[] = {
    { "rosso", 0xD83A3A }, { "rossa", 0xD83A3A }, { "rossi", 0xD83A3A }, { "rosse", 0xD83A3A },
    { "red", 0xD83A3A },
    { "verde", 0x3CB043 }, { "verdi", 0x3CB043 }, { "green", 0x3CB043 },
    { "blu", 0x3A62D8 }, { "blue", 0x3A62D8 },
    { "azzurro", 0x48B8E8 }, { "azzurra", 0x48B8E8 }, { "celeste", 0x48B8E8 }, { "cyan", 0x48B8E8 },
    { "giallo", 0xF0D040 }, { "gialla", 0xF0D040 }, { "gialli", 0xF0D040 }, { "yellow", 0xF0D040 },
    { "arancione", 0xF08A30 }, { "arancio", 0xF08A30 }, { "orange", 0xF08A30 },
    { "viola", 0x8A4AD0 }, { "purple", 0x8A4AD0 }, { "violet", 0x8A4AD0 }, { "lilla", 0xB08AE0 },
    { "rosa", 0xF080B0 }, { "pink", 0xF080B0 }, { "fucsia", 0xE040A0 },
    { "bianco", 0xF0F0F0 }, { "bianca", 0xF0F0F0 }, { "white", 0xF0F0F0 },
    { "nero", 0x383848 }, { "nera", 0x383848 }, { "black", 0x383848 },
    { "grigio", 0x8A8A9A }, { "grigia", 0x8A8A9A }, { "grey", 0x8A8A9A }, { "gray", 0x8A8A9A },
    { "marrone", 0x8A5A30 }, { "brown", 0x8A5A30 },
    { "oro", 0xE8B830 }, { "dorato", 0xE8B830 }, { "dorata", 0xE8B830 }, { "gold", 0xE8B830 },
    { "golden", 0xE8B830 },
    { "argento", 0xC0C8D8 }, { "argentato", 0xC0C8D8 }, { "argentata", 0xC0C8D8 },
    { "silver", 0xC0C8D8 },
};

static uint32_t rgb(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* five shades: 0 darkest .. 2 the colour .. 4 highlight; 5 = outline.
 * Shadows lean to blue, lights to yellow, as in pixel art. */
static uint32_t ramp(uint32_t c, int level)
{
    int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
    switch (level) {
    case 0: return rgb(r * 45 / 100, g * 45 / 100, b * 52 / 100 + 14);
    case 1: return rgb(r * 70 / 100, g * 71 / 100, b * 78 / 100 + 10);
    case 2: return c;
    case 3: return rgb(r + (255 - r) * 30 / 100 + 6, g + (255 - g) * 28 / 100 + 3, b + (255 - b) * 20 / 100);
    case 4: return rgb(r + (255 - r) * 62 / 100, g + (255 - g) * 60 / 100, b + (255 - b) * 52 / 100);
    default: return rgb(r * 22 / 100 + 8, g * 22 / 100 + 8, b * 26 / 100 + 18);
    }
}

void spr_req_init(spr_req_t *r, const char *gen)
{
    memset(r, 0, sizeof *r);
    r->gen = gen;
    r->w = r->h = 16;
    r->seed = 1;
    r->color[0] = r->color[1] = SPR_NO_COLOR;
    r->outline = 1;
}

void spr_parse(const char *text, spr_req_t *r)
{
    static ai_words_t ws;
    ai_words(text, AI_MAX_TOKENS, &ws);
    int nc = 0;
    for (int i = 0; i < ws.n; i++) {
        const char *w = ws.w[i];
        for (unsigned k = 0; k < sizeof colors / sizeof colors[0]; k++)
            if (!strcmp(w, colors[k].w) && nc < 2) {
                /* "azzurro chiaro" and "light blue": one colour, not two */
                if (nc == 1 && r->color[0] == colors[k].rgb)
                    break;
                r->color[nc++] = colors[k].rgb;
                break;
            }
        int size = 0;
        if (!strcmp(w, "8x8") || !strcmp(w, "8")) size = 8;
        else if (!strcmp(w, "16x16") || !strcmp(w, "16")) size = 16;
        else if (!strcmp(w, "32x32") || !strcmp(w, "32")) size = 32;
        else if (!strcmp(w, "piccolo") || !strcmp(w, "piccola") || !strcmp(w, "small") || !strcmp(w, "tiny"))
            size = 8;
        else if (!strcmp(w, "grande") || !strcmp(w, "big") || !strcmp(w, "large"))
            size = 32;
        if (size)
            r->w = r->h = size;
        if ((!strcmp(w, "senza") || !strcmp(w, "no") || !strcmp(w, "without")) && i + 1 < ws.n &&
            (!strcmp(ws.w[i + 1], "contorno") || !strcmp(ws.w[i + 1], "bordo") ||
             !strcmp(ws.w[i + 1], "outline") || !strcmp(ws.w[i + 1], "border")))
            r->outline = 0;
    }
}

/* ---------------------------------------------------------------- palettes */

static const uint32_t P_BRIGHT[] = { 0xD83A3A, 0x3CB043, 0x3A62D8, 0xF0D040, 0xF08A30, 0x8A4AD0,
                                     0xF080B0, 0x48B8E8 };
static const uint32_t P_SKIN[] = { 0xF6C9A0, 0xE8B088, 0xC88A60, 0x9A6440, 0x6E4630 };
static const uint32_t P_HAIR[] = { 0x3A2A20, 0x6A4028, 0xC88A40, 0xE8D070, 0xB03A2A, 0x303040, 0xE0E0E8 };
static const uint32_t P_METAL[] = { 0xB8C0CC, 0x9AA4B4, 0xD0D4DC, 0x8A90A0 };
static const uint32_t P_WOOD[] = { 0x9A6234, 0x8A5A30, 0xA87040, 0x7A4A28 };

/* ---------------------------------------------------------------- recipes */

static void r_ship(cv_t *c)
{
    c->col[1] = pick(c, P_METAL, 4);
    c->col[2] = pick(c, P_BRIGHT, 8);
    c->col[3] = 0x50D8F8;                       /* cockpit */
    c->col[4] = 0x505868;                       /* engines */
    c->col[5] = 0xFFB040; c->flat[5] = 1; c->soft[5] = 1;     /* flame */
    float bw = rf(c, 0.10f, 0.15f), top = rf(c, 0.06f, 0.14f);
    float wy = rf(c, 0.42f, 0.58f), wx = rf(c, 0.38f, 0.47f), sweep = rf(c, 0.05f, 0.22f);
    /* wings, then the body over them */
    tri(c, 0.5f, wy - 0.12f, wx < 0.42f ? 0.5f - wx : 0.06f, wy + sweep + 0.08f, 0.5f, wy + 0.22f, 2);
    if (rnd(c) < 0.6f)          /* cannons at the wing tips */
        box(c, 0.5f - wx - 0.02f, wy - 0.02f + sweep * 0.5f, 0.5f - wx + 0.07f, wy + sweep + 0.12f, 1);
    tri(c, 0.5f, top, 0.5f - bw, 0.42f, 0.5f, 0.42f, 1);
    box(c, 0.5f - bw, 0.36f, 0.5f, 0.86f, 1);
    if (rnd(c) < 0.5f)          /* tail fins */
        tri(c, 0.5f - bw, 0.66f, 0.5f - bw - rf(c, 0.08f, 0.16f), 0.9f, 0.5f - bw, 0.86f, 2);
    box(c, 0.5f - bw * 0.8f, 0.84f, 0.5f - 0.02f, 0.92f, 4);
    if (c->w >= 16)
        box(c, 0.5f - bw * 0.6f, 0.92f, 0.5f - 0.04f, 0.98f, 5);
    mirror(c);
    ell(c, 0.5f, 0.38f, bw * 0.6f, 0.1f, 3);
    shade(c, (int)(0.47f * (float)c->w), (int)(0.33f * (float)c->h), 4);
}

static void r_alien(cv_t *c)
{
    /* space invader: random cells in the left half, mirrored, chunky:
     * a grid of 8 (8x8, 16x16 at 2x) or 12 (32x32 at 2x, centred) */
    c->col[1] = pick(c, P_BRIGHT, 8);
    c->col[6] = 0x181820; c->flat[6] = 1;
    int g = c->w >= 32 ? 12 : 8, sc = c->w >= 32 ? 2 : c->w / 8, off = (c->w - g * sc) / 2;
    uint8_t cell[16][16];
    for (int tries = 0; tries < 8; tries++) {
        memset(cell, 0, sizeof cell);
        int n = 0;
        for (int y = 1; y < g - 1; y++)
            for (int x = 1; x < g / 2; x++) {
                float dx = (float)(g / 2 - x) / (float)(g / 2), dy = (float)(y - g / 2) / (float)(g / 2);
                float p = 0.75f - 0.6f * (dx * dx + dy * dy);
                if (rnd(c) < p) {
                    cell[y][x] = cell[y][g - 1 - x] = 1;
                    n++;
                }
            }
        for (int y = 2; y < g - 2; y++)         /* a solid core */
            cell[y][g / 2 - 1] = cell[y][g / 2] = 1;
        if (n > g * g / 8)
            break;
    }
    /* eyes: holes a third of the way down */
    int ey = g / 3 + 1, ex = g / 2 - 2;
    cell[ey][ex] = cell[ey][g - 1 - ex] = 2;
    for (int y = 0; y < g; y++)
        for (int x = 0; x < g; x++)
            for (int j = 0; j < sc; j++)
                for (int i = 0; i < sc; i++)
                    set(c, off + x * sc + i, off + y * sc + j, cell[y][x] == 1 ? 1 : cell[y][x] == 2 ? 6 : 0);
}

static void r_monster(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);
    c->col[2] = 0xF0E6D0;                       /* teeth, horns */
    c->col[6] = 0x181820; c->flat[6] = 1;
    c->col[7] = 0x7A1E2A; c->flat[7] = 1;       /* mouth */
    float rx = rf(c, 0.34f, 0.44f), ry = rf(c, 0.3f, 0.38f), cy = 0.58f;
    ell(c, 0.5f, cy, rx, ry, 1);
    if (rnd(c) < 0.6f) {        /* horns */
        float hx = rf(c, 0.14f, 0.26f);
        tri(c, 0.5f - hx - 0.06f, cy - ry + 0.08f, 0.5f - hx + 0.06f, cy - ry + 0.06f, 0.5f - hx - 0.08f, 0.08f, 2);
    }
    if (rnd(c) < 0.7f)          /* feet */
        ell(c, 0.5f - rx * 0.55f, cy + ry - 0.02f, 0.1f, 0.07f, 1);
    if (rnd(c) < 0.5f)          /* arms */
        ell(c, 0.5f - rx - 0.02f, cy + 0.04f, 0.07f, 0.12f, 1);
    mirror(c);
    /* mouth with teeth, eyes */
    float mw = rf(c, 0.12f, 0.22f);
    box(c, 0.5f - mw, cy + 0.06f, 0.5f + mw, cy + 0.06f + (c->w >= 16 ? 0.12f : 0.13f), 7);
    if (c->w >= 16)
        for (int x = (int)((0.5f - mw) * (float)c->w) + 1; x < (int)((0.5f + mw) * (float)c->w); x += 2)
            set(c, x, (int)((cy + 0.07f) * (float)c->h), 2);
    if (rnd(c) < 0.25f && c->w >= 16) {        /* one big eye */
        ell(c, 0.5f, cy - ry * 0.4f, 0.1f, 0.1f, 2);
        set(c, c->w / 2, (int)((cy - ry * 0.4f) * (float)c->h), 6);
    } else {
        eyes(c, cy - ry * 0.45f, rf(c, 0.1f, 0.16f), 6);
    }
}

static void r_robot(cv_t *c)
{
    c->col[1] = pick(c, P_METAL, 4);
    c->col[2] = pick(c, P_BRIGHT, 8);
    c->col[3] = 0x40E0FF; c->flat[3] = 1;       /* visor lights */
    c->col[4] = 0x505868;
    float hw = rf(c, 0.2f, 0.28f);
    seg(c, 0.5f, 0.04f, 0.5f, 0.16f, 0.6f / (float)c->w, 4);             /* antenna */
    set(c, c->w / 2, 0, 2);
    box(c, 0.5f - hw, 0.14f, 0.5f + hw, 0.42f, 1);                        /* head */
    box(c, 0.5f - 0.24f, 0.46f, 0.5f + 0.24f, 0.78f, 2);                  /* body */
    box(c, 0.5f - 0.38f, 0.48f, 0.5f - 0.26f, 0.72f, 1);                  /* arms */
    box(c, 0.5f + 0.26f, 0.48f, 0.5f + 0.38f, 0.72f, 1);
    box(c, 0.5f - 0.2f, 0.8f, 0.5f - 0.06f, 0.96f, 4);                    /* legs */
    box(c, 0.5f + 0.06f, 0.8f, 0.5f + 0.2f, 0.96f, 4);
    box(c, 0.5f - 0.04f, 0.42f, 0.5f + 0.04f, 0.46f, 4);                  /* neck */
    if (rnd(c) < 0.5f)
        box(c, 0.5f - hw + 0.06f, 0.24f, 0.5f + hw - 0.06f, 0.32f, 3);    /* visor */
    else
        eyes(c, 0.26f, hw * 0.5f, 3);
    if (c->w >= 16)
        box(c, 0.5f - 0.08f, 0.56f, 0.5f + 0.08f, 0.66f, 1);              /* chest plate */
}

static void r_hero(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);           /* shirt */
    c->col[2] = 0x3A4A7A;                       /* trousers */
    c->col[3] = pick(c, P_SKIN, 5);
    c->col[4] = pick(c, P_HAIR, 7);
    c->col[6] = 0x181820; c->flat[6] = 1;
    c->col[7] = 0x40302A;                       /* shoes */
    box(c, 0.5f - 0.18f, 0.84f, 0.5f - 0.02f, 0.97f, 7);
    box(c, 0.5f - 0.17f, 0.68f, 0.5f - 0.03f, 0.86f, 2);
    box(c, 0.5f - 0.21f, 0.46f, 0.5f, 0.7f, 1);
    box(c, 0.5f - 0.3f, 0.47f, 0.5f - 0.2f, 0.66f, 1);                   /* sleeves */
    box(c, 0.5f - 0.3f, 0.64f, 0.5f - 0.2f, 0.72f, 3);                   /* hands */
    ell(c, 0.5f, 0.27f, 0.21f, 0.21f, 3);                                /* head */
    int style = rint_(c, 0, 2);
    box(c, 0.28f, 0.03f, 0.5f, 0.2f, 4);                                 /* hair */
    if (style == 1)
        box(c, 0.28f, 0.1f, 0.34f, 0.44f, 4);                            /* long */
    else if (style == 2)
        tri(c, 0.26f, 0.2f, 0.4f, 0.02f, 0.44f, 0.18f, 4);              /* spiky */
    mirror(c);
    eyes(c, 0.3f, 0.08f, 6);
}

/* side view, facing right; the seed picks the step: 1 standing, 2 and 3
 * walking (three variants make a walk cycle) */
static void r_hero_side(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);           /* shirt */
    c->col[2] = 0x3A4A7A;                       /* trousers */
    c->col[3] = pick(c, P_SKIN, 5);
    c->col[4] = pick(c, P_HAIR, 7);
    c->col[6] = 0x181820; c->flat[6] = 1;
    c->col[7] = 0x40302A;                       /* shoes */
    int step = (int)((c->seed - 1) % 3);
    if (step == 0) {
        box(c, 0.38f, 0.7f, 0.48f, 0.9f, 2);
        box(c, 0.5f, 0.7f, 0.6f, 0.9f, 2);
        box(c, 0.36f, 0.88f, 0.5f, 0.97f, 7);
        box(c, 0.5f, 0.88f, 0.66f, 0.97f, 7);
    } else {
        float d = step == 1 ? 1.0f : -1.0f;
        seg(c, 0.49f, 0.72f, 0.49f + 0.13f * d, 0.88f, 0.06f, 2);
        seg(c, 0.49f, 0.72f, 0.49f - 0.13f * d, 0.88f, 0.06f, 2);
        box(c, 0.43f + 0.13f * d, 0.88f, 0.59f + 0.13f * d, 0.97f, 7);
        box(c, 0.4f - 0.13f * d, 0.88f, 0.56f - 0.13f * d, 0.97f, 7);
    }
    box(c, 0.34f, 0.46f, 0.64f, 0.72f, 1);                              /* body */
    ell(c, 0.5f, 0.27f, 0.19f, 0.2f, 3);                                /* head */
    box(c, 0.3f, 0.06f, 0.66f, 0.17f, 4);                               /* hair */
    box(c, 0.3f, 0.06f, 0.4f, 0.36f, 4);
    if (rnd(c) < 0.5f)
        box(c, 0.6f, 0.08f, 0.7f, 0.14f, 4);                            /* fringe */
    float arm = step == 0 ? 0.0f : step == 1 ? -0.08f : 0.08f;
    seg(c, 0.5f, 0.5f, 0.5f + arm, 0.66f, 0.05f, 1);                    /* arm */
    ell(c, 0.5f + arm, 0.69f, 0.05f, 0.04f, 3);                         /* hand */
    set(c, (int)(0.62f * (float)c->w), (int)(0.26f * (float)c->h), 6);  /* eye */
    if (c->w >= 32)
        set(c, (int)(0.62f * (float)c->w), (int)(0.26f * (float)c->h) + 1, 6);
}

/* a car seen from above, the front up */
static void r_car(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);
    c->col[2] = 0x80C8E8; c->soft[2] = 1;       /* windows */
    c->col[4] = 0x282830;                       /* tyres */
    c->col[5] = 0xFFE890; c->flat[5] = 1;       /* lights */
    c->col[3] = 0xF0F0F0;                       /* racing stripes */
    box(c, 0.22f, 0.16f, 0.32f, 0.32f, 4);
    box(c, 0.22f, 0.66f, 0.32f, 0.82f, 4);
    box(c, 0.28f, 0.08f, 0.5f, 0.92f, 1);
    ell(c, 0.36f, 0.14f, 0.08f, 0.08f, 1);
    ell(c, 0.36f, 0.88f, 0.08f, 0.06f, 1);
    box(c, 0.32f, 0.3f, 0.5f, 0.42f, 2);                                /* windscreen */
    box(c, 0.34f, 0.68f, 0.5f, 0.76f, 2);                               /* rear window */
    set(c, (int)(0.34f * (float)c->w), (int)(0.08f * (float)c->h), 5);
    if (rnd(c) < 0.5f && c->w >= 16) {
        box(c, 0.42f, 0.08f, 0.46f, 0.3f, 3);                           /* stripes */
        box(c, 0.42f, 0.42f, 0.46f, 0.68f, 3);
        box(c, 0.42f, 0.76f, 0.46f, 0.92f, 3);
    }
    mirror(c);
    shade(c, c->w / 2 - 1, (int)(0.55f * (float)c->h), 3);
}

static void r_slime(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);
    c->col[6] = 0x181820; c->flat[6] = 1;
    float ry = rf(c, 0.38f, 0.48f);
    ell(c, 0.5f, 0.94f, rf(c, 0.4f, 0.46f), ry * 1.6f, 1);
    box(c, 0, 0.94f, 1, 1, 0);
    if (rnd(c) < 0.4f)          /* a drip on top */
        ell(c, 0.5f, 0.94f - ry * 1.6f + 0.02f, 0.06f, 0.08f, 1);
    eyes(c, 0.94f - ry * 0.85f, rf(c, 0.1f, 0.16f), 6);
    int hx = (int)(0.3f * (float)c->w), hy = (int)((0.94f - ry * 1.3f) * (float)c->h);
    shade(c, hx, hy + 1, 4);
    if (c->w >= 16)
        shade(c, hx + 1, hy + 1, 4);
}

static void r_ghost(cv_t *c)
{
    c->col[1] = rnd(c) < 0.7f ? 0xE8ECF8 : 0xB8E0F0;
    c->col[6] = 0x30304A; c->flat[6] = 1;
    ell(c, 0.5f, 0.38f, 0.36f, 0.32f, 1);
    box(c, 0.14f, 0.38f, 0.86f, 0.86f, 1);
    /* a wavy hem */
    float ph = rf(c, 0, 6.28f);
    for (int x = 0; x < c->w; x++) {
        float u = U(c, x);
        float hem = 0.86f + 0.07f * sinf(u * 18.0f + ph);
        for (int y = 0; y < c->h; y++)
            if (V(c, y) > hem && V(c, y) < 0.96f && u > 0.14f && u < 0.86f)
                c->m[y * c->w + x] = 1;
    }
    if (rnd(c) < 0.5f) {        /* arms up */
        ell(c, 0.12f, 0.5f, 0.07f, 0.1f, 1);
        ell(c, 0.88f, 0.5f, 0.07f, 0.1f, 1);
    }
    eyes(c, 0.36f, 0.12f, 6);
    if (c->w >= 16 && rnd(c) < 0.6f)
        ell(c, 0.5f, 0.56f, 0.06f, 0.07f, 6);        /* "oh" mouth */
}

static void r_bat(cv_t *c)
{
    c->col[1] = rnd(c) < 0.5f ? 0x5A4A7A : 0x6A5050;
    c->col[2] = 0xF0F0F0; c->flat[2] = 1;
    c->col[6] = 0xFF4040; c->flat[6] = 1;
    float lift = rf(c, -0.06f, 0.08f);         /* wings up or down: frames */
    ell(c, 0.5f, 0.52f, 0.12f, 0.17f, 1);
    tri(c, 0.4f, 0.4f, 0.42f, 0.22f, 0.49f, 0.38f, 1);                   /* ears */
    float p[] = { 0.48f, 0.42f, 0.3f, 0.3f - lift, 0.05f, 0.26f - lift * 1.5f, 0.02f, 0.52f - lift,
                  0.12f, 0.47f - lift * 0.8f, 0.18f, 0.62f - lift * 0.6f, 0.27f, 0.53f - lift * 0.4f,
                  0.35f, 0.64f - lift * 0.2f, 0.48f, 0.58f };
    poly(c, p, 9, 1);
    mirror(c);
    eyes(c, 0.46f, c->w >= 32 ? 0.08f : 0.05f, 6);
    if (c->w >= 16) {           /* fangs */
        set(c, c->w / 2 - 1, (int)(0.62f * (float)c->h), 2);
        set(c, c->w / 2, (int)(0.62f * (float)c->h), 2);
    }
}

static void r_skull(cv_t *c)
{
    c->col[1] = 0xE8E4D4;
    c->col[6] = 0x202028; c->flat[6] = 1;
    ell(c, 0.5f, 0.42f, 0.38f, 0.34f, 1);
    box(c, 0.28f, 0.6f, 0.72f, 0.88f, 1);
    ell(c, 0.36f, 0.46f, 0.1f, 0.1f, 6);
    ell(c, 0.64f, 0.46f, 0.1f, 0.1f, 6);
    tri(c, 0.5f, 0.56f, 0.45f, 0.66f, 0.55f, 0.66f, 6);
    if (c->w >= 16)
        for (int x = (int)(0.34f * (float)c->w); x < (int)(0.68f * (float)c->w); x += 2)
            set(c, x, (int)(0.8f * (float)c->h), 6);
}

static void r_coin(cv_t *c)
{
    c->col[1] = rnd(c) < 0.8f ? 0xE8B830 : 0xC0C8D8;
    float rx = rnd(c) < 0.7f ? 0.4f : rf(c, 0.15f, 0.3f);      /* turning: for animations */
    ell(c, 0.5f, 0.5f, rx, 0.42f, 1);
    if (c->w >= 16 && rx > 0.3f) {
        /* the rim: a darker ring, and a mark in the middle */
        for (int y = 0; y < c->h; y++)
            for (int x = 0; x < c->w; x++) {
                float dx = (U(c, x) - 0.5f) / (rx - 0.09f), dy = (V(c, y) - 0.5f) / 0.33f;
                float d = dx * dx + dy * dy;
                if (d > 0.82f && d <= 1.0f)
                    c->lv[y * c->w + x] = 2;        /* shade 1 */
            }
        for (int y = (int)(0.32f * (float)c->h); y < (int)(0.68f * (float)c->h); y++)
            shade(c, c->w / 2, y, 1);
    }
    shade(c, (int)(0.32f * (float)c->w), (int)(0.3f * (float)c->h), 4);
}

static void r_gem(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);
    float p[] = { 0.3f, 0.2f, 0.7f, 0.2f, 0.9f, 0.4f, 0.5f, 0.9f, 0.1f, 0.4f };
    poly(c, p, 5, 1);
    /* facets: the crown lighter, the left lighter, the right darker */
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            if (!c->m[y * c->w + x]) continue;
            float u = U(c, x), v = V(c, y);
            int l = v < 0.4f ? 3 : u < 0.5f - (v - 0.4f) * 0.3f ? 2 : 1;
            if (v < 0.4f && u > 0.62f) l = 2;
            c->lv[y * c->w + x] = (uint8_t)(l + 1);
        }
    shade(c, (int)(0.36f * (float)c->w), (int)(0.28f * (float)c->h), 4);
}

static void r_heart(cv_t *c)
{
    c->col[1] = rnd(c) < 0.8f ? 0xE83A4A : 0xF080B0;
    ell(c, 0.32f, 0.36f, 0.2f, 0.2f, 1);
    ell(c, 0.68f, 0.36f, 0.2f, 0.2f, 1);
    tri(c, 0.11f, 0.42f, 0.89f, 0.42f, 0.5f, 0.9f, 1);
    shade(c, (int)(0.26f * (float)c->w), (int)(0.3f * (float)c->h), 4);
}

static void r_star(cv_t *c)
{
    c->col[1] = rnd(c) < 0.8f ? 0xF8D030 : pick(c, P_BRIGHT, 8);
    float p[20];
    for (int i = 0; i < 10; i++) {
        float a = -1.5708f + (float)i * 0.6283f, r = i & 1 ? 0.2f : 0.47f;
        p[2 * i] = 0.5f + r * cosf(a);
        p[2 * i + 1] = 0.54f + r * sinf(a);
    }
    poly(c, p, 10, 1);
}

static void r_key(cv_t *c)
{
    c->col[1] = rnd(c) < 0.7f ? 0xE8B830 : 0xC0C8D8;
    ell(c, 0.3f, 0.3f, 0.2f, 0.2f, 1);
    ell(c, 0.3f, 0.3f, 0.09f, 0.09f, 0);
    seg(c, 0.4f, 0.4f, 0.86f, 0.86f, c->w >= 16 ? 0.07f : 0.09f, 1);
    seg(c, 0.66f, 0.66f, 0.56f, 0.78f, 0.06f, 1);
    seg(c, 0.78f, 0.78f, 0.68f, 0.9f, 0.06f, 1);
}

static void r_sword(cv_t *c)
{
    c->col[1] = 0xD0D8E8;                       /* blade */
    c->col[2] = rnd(c) < 0.5f ? 0xE8B830 : 0x8A5A30;   /* guard */
    c->col[3] = 0x6A4028;                       /* grip */
    float r = c->w >= 16 ? 0.06f : 0.07f;
    seg(c, 0.36f, 0.64f, 0.86f, 0.14f, r, 1);
    seg(c, 0.2f, 0.52f, 0.48f, 0.8f, 0.06f, 2);
    seg(c, 0.32f, 0.68f, 0.16f, 0.84f, 0.05f, 3);
    ell(c, 0.13f, 0.87f, 0.07f, 0.07f, 2);
}

static void r_potion(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);           /* the liquid */
    c->col[2] = 0xC8E4F0; c->soft[2] = 1;       /* glass */
    c->col[3] = 0x9A6234;                       /* cork */
    ell(c, 0.5f, 0.66f, 0.32f, 0.3f, 2);
    box(c, 0.38f, 0.2f, 0.62f, 0.44f, 2);
    box(c, 0.36f, 0.06f, 0.64f, 0.2f, 3);
    float lvl = rf(c, 0.5f, 0.66f);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            if (c->m[y * c->w + x] == 2 && V(c, y) > lvl)
                c->m[y * c->w + x] = 1;
    shade(c, (int)(0.32f * (float)c->w), (int)(0.6f * (float)c->h), 4);
    if (c->w >= 16) {
        shade(c, (int)(0.6f * (float)c->w), (int)(0.78f * (float)c->h), 4);
        shade(c, (int)(0.5f * (float)c->w), (int)(0.7f * (float)c->h), 3);
    }
}

static void r_chest(cv_t *c)
{
    c->col[1] = pick(c, P_WOOD, 4);
    c->col[2] = rnd(c) < 0.7f ? 0xE8B830 : 0xA0A8B8;
    c->col[6] = 0x202028; c->flat[6] = 1;
    ell(c, 0.5f, 0.4f, 0.42f, 0.2f, 1);
    box(c, 0.08f, 0.4f, 0.92f, 0.88f, 1);
    box(c, 0.08f, 0.44f, 0.92f, 0.52f, 2);                               /* band */
    box(c, 0.16f, 0.22f, 0.26f, 0.88f, 2);
    box(c, 0.74f, 0.22f, 0.84f, 0.88f, 2);
    box(c, 0.42f, 0.44f, 0.58f, 0.64f, 2);                               /* lock */
    set(c, c->w / 2, (int)(0.56f * (float)c->h), 6);
}

static void r_crate(cv_t *c)
{
    c->col[1] = pick(c, P_WOOD, 4);
    box(c, 0.06f, 0.06f, 0.94f, 0.94f, 1);
    int b = c->w >= 16 ? 2 : 1;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            if (!c->m[y * c->w + x]) continue;
            int in = x > b && y > b && x < c->w - 1 - b && y < c->h - 1 - b;
            if (!in) c->lv[y * c->w + x] = 4;                            /* frame: light */
            else if (x == y || x == y + 1) c->lv[y * c->w + x] = 3;     /* brace */
            else if (c->w >= 16 && y % 4 == 0) c->lv[y * c->w + x] = 2; /* planks */
        }
}

static void r_tree(cv_t *c)
{
    c->col[1] = rnd(c) < 0.8f ? 0x3CA048 : 0xD08A30;   /* green or autumn */
    c->col[2] = 0x7A4A28;
    c->col[6] = 0xE83A3A; c->flat[6] = 1;
    box(c, 0.42f, 0.6f, 0.58f, 0.96f, 2);
    if (rnd(c) < 0.3f) {        /* a pine */
        tri(c, 0.5f, 0.04f, 0.2f, 0.4f, 0.8f, 0.4f, 1);
        tri(c, 0.5f, 0.2f, 0.1f, 0.62f, 0.9f, 0.62f, 1);
        tri(c, 0.5f, 0.38f, 0.04f, 0.82f, 0.96f, 0.82f, 1);
        return;
    }
    int n = rint_(c, 3, 5);
    for (int i = 0; i < n; i++)
        ell(c, rf(c, 0.3f, 0.7f), rf(c, 0.25f, 0.45f), rf(c, 0.18f, 0.28f), rf(c, 0.16f, 0.24f), 1);
    ell(c, 0.5f, 0.36f, 0.3f, 0.26f, 1);
    if (rnd(c) < 0.4f)          /* fruit */
        for (int i = 0; i < 4; i++) {
            int x = rint_(c, 3, c->w - 4), y = rint_(c, 2, c->h / 2);
            if (get(c, x, y) == 1) set(c, x, y, 6);
        }
}

static void r_flower(cv_t *c)
{
    c->col[1] = pick(c, P_BRIGHT, 8);           /* petals */
    c->col[2] = 0x3CA048;                       /* stem, leaves */
    c->col[3] = 0xF8D840;                       /* centre */
    seg(c, 0.5f, 0.5f, 0.5f, 0.96f, 0.05f, 2);
    ell(c, 0.36f, 0.76f, 0.12f, 0.06f, 2);
    ell(c, 0.64f, 0.7f, 0.12f, 0.06f, 2);
    int np = rint_(c, 5, 6);
    float a0 = rf(c, 0, 1);
    for (int i = 0; i < np; i++) {
        float a = a0 + (float)i * 6.2832f / (float)np;
        ell(c, 0.5f + 0.17f * cosf(a), 0.34f + 0.17f * sinf(a), 0.12f, 0.12f, 1);
    }
    ell(c, 0.5f, 0.34f, 0.1f, 0.1f, 3);
}

static void r_mushroom(cv_t *c)
{
    c->col[1] = rnd(c) < 0.7f ? 0xD83A3A : pick(c, P_BRIGHT, 8);
    c->col[2] = 0xF0E6D0;
    ell(c, 0.5f, 0.5f, 0.44f, 0.38f, 1);
    box(c, 0, 0.5f, 1, 1, 0);
    box(c, 0.36f, 0.5f, 0.64f, 0.92f, 2);
    /* white spots */
    ell(c, 0.34f, 0.32f, 0.07f, 0.06f, 2);
    ell(c, 0.6f, 0.24f, 0.08f, 0.07f, 2);
    ell(c, 0.74f, 0.4f, 0.05f, 0.05f, 2);
}

static void r_rock(cv_t *c)
{
    c->col[1] = rnd(c) < 0.7f ? 0x8A8A9A : 0x9A8A70;
    float ph = rf(c, 0, 6.28f), k = rf(c, 0.04f, 0.08f);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float dx = U(c, x) - 0.5f, dy = V(c, y) - 0.6f;
            float a = atan2f(dy, dx);
            float r = 0.4f + k * sinf(a * 3 + ph) + k * 0.5f * sinf(a * 5 + ph * 2);
            if (dx * dx / (r * r) + dy * dy / (r * r * 0.6f) <= 1.0f)
                c->m[y * c->w + x] = 1;
        }
    if (c->w >= 16)             /* a crack */
        for (int i = 0; i < c->w / 4; i++)
            shade(c, c->w / 2 + i / 2, c->h / 2 + i, 0);
}

static void r_bomb(cv_t *c)
{
    c->col[1] = 0x384050;
    c->col[2] = 0x9A6234;
    c->col[5] = 0xFFD040; c->flat[5] = 1; c->soft[5] = 1;
    ell(c, 0.46f, 0.6f, 0.36f, 0.36f, 1);
    box(c, 0.52f, 0.2f, 0.7f, 0.32f, 1);
    seg(c, 0.66f, 0.2f, 0.8f, 0.1f, 0.04f, 2);
    set(c, (int)(0.84f * (float)c->w), (int)(0.06f * (float)c->h), 5);
    if (c->w >= 16) {
        set(c, (int)(0.9f * (float)c->w), (int)(0.06f * (float)c->h), 5);
        set(c, (int)(0.84f * (float)c->w), 0, 5);
    }
    shade(c, (int)(0.32f * (float)c->w), (int)(0.44f * (float)c->h), 4);
}

/* a flame: a drop with tongues at the top, in nested layers anchored at
 * the bottom (red, orange, yellow, white); u centred, v from the top */
static int flame_in(float u, float v, float lean, float ph)
{
    if (v > 0.97f || v < 0.03f)
        return 0;
    float a = u - lean * (0.64f - v);
    if (v >= 0.64f)
        return a * a + (v - 0.64f) * (v - 0.64f) <= 0.33f * 0.33f;
    float t = (v - 0.03f) / 0.61f;             /* 0 at the tip, 1 at the widest */
    float half = 0.33f * powf(t, 0.75f) * (1.0f + 0.4f * sinf(a * 21.0f + ph) * (1.0f - t));
    return fabsf(a) <= half;
}

static void r_fire(cv_t *c)
{
    static const uint32_t cols[4] = { 0xD8401C, 0xF88A2C, 0xFFD050, 0xFFF8D8 };
    static const float scale[4] = { 1.0f, 0.76f, 0.52f, 0.3f };
    for (int k = 0; k < 4; k++) {
        c->col[k + 1] = cols[k];
        c->flat[k + 1] = c->soft[k + 1] = 1;
    }
    float ph = rf(c, 0, 6.28f), lean = rf(c, -0.12f, 0.12f);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            for (int k = 0; k < 4; k++) {
                float u = (U(c, x) - 0.5f) / scale[k], v = 0.97f - (0.97f - V(c, y)) / scale[k];
                if (flame_in(u, v, lean, ph + 1.3f * (float)k))
                    c->m[y * c->w + x] = (uint8_t)(k + 1);
            }
}

static void r_bullet(cv_t *c)
{
    c->col[1] = rnd(c) < 0.5f ? 0xFFE060 : 0x60F0FF; c->soft[1] = 1;
    c->col[3] = 0xFFFFFF; c->flat[3] = 1; c->soft[3] = 1;
    if (rnd(c) < 0.5f) {        /* a round shot */
        ell(c, 0.5f, 0.5f, 0.22f, 0.22f, 1);
        ell(c, 0.47f, 0.47f, 0.1f, 0.1f, 3);
    } else {                    /* a laser bolt */
        box(c, 0.4f, 0.1f, 0.6f, 0.9f, 1);
        box(c, 0.46f, 0.16f, 0.54f, 0.84f, 3);
    }
}

static void r_explosion(cv_t *c)
{
    c->col[1] = 0xE04020; c->soft[1] = 1;
    c->col[2] = 0xF8A030; c->soft[2] = 1;
    c->col[3] = 0xFFF0A0; c->flat[3] = 1; c->soft[3] = 1;
    float ph = rf(c, 0, 6.28f);
    int spikes = rint_(c, 6, 9);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float dx = U(c, x) - 0.5f, dy = V(c, y) - 0.5f;
            float a = atan2f(dy, dx), d = sqrtf(dx * dx + dy * dy);
            float r = 0.32f + 0.12f * sinf(a * (float)spikes + ph);
            if (d < r) c->m[y * c->w + x] = 1;
            if (d < r * 0.7f) c->m[y * c->w + x] = 2;
            if (d < r * 0.35f) c->m[y * c->w + x] = 3;
        }
    for (int i = 0; i < 4; i++)  /* debris */
        set(c, rint_(c, 0, c->w - 1), rint_(c, 0, c->h - 1), 2);
}

static void r_apple(cv_t *c)
{
    c->col[1] = rnd(c) < 0.75f ? 0xD8303A : 0x7AC040;
    c->col[2] = 0x3CA048;
    c->col[3] = 0x6A4028;
    ell(c, 0.36f, 0.58f, 0.24f, 0.3f, 1);
    ell(c, 0.64f, 0.58f, 0.24f, 0.3f, 1);
    seg(c, 0.5f, 0.3f, 0.54f, 0.1f, 0.035f, 3);
    ell(c, 0.66f, 0.18f, 0.12f, 0.06f, 2);
    shade(c, (int)(0.3f * (float)c->w), (int)(0.44f * (float)c->h), 4);
}

static void r_cloud(cv_t *c)
{
    c->col[1] = 0xF4F6FC;
    ell(c, 0.3f, 0.62f, 0.2f, 0.16f, 1);
    ell(c, 0.52f, 0.48f, 0.24f, 0.22f, 1);
    ell(c, 0.74f, 0.6f, 0.18f, 0.16f, 1);
    box(c, 0.2f, 0.6f, 0.82f, 0.76f, 1);
}

/* ------------------------------------------------- tiles (seamless) */

static void tile_noise(cv_t *c, int m, int levels, float density)
{
    for (int i = 0; i < c->w * c->h; i++) {
        c->m[i] = (uint8_t)m;
        if (rnd(c) < density)
            c->lv[i] = (uint8_t)(1 + rint_(c, 1, levels));
    }
}

static void r_grass(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = rnd(c) < 0.8f ? 0x4CA840 : 0x8AB040;
    c->col[6] = pick(c, P_BRIGHT, 8); c->flat[6] = 1;
    tile_noise(c, 1, 0, 0);
    /* blades: a light pixel on a dark one */
    for (int i = 0; i < c->w * c->h / 6; i++) {
        int x = rint_(c, 0, c->w - 1), y = rint_(c, 0, c->h - 1);
        c->lv[((y + 1) % c->h) * c->w + x] = 2;
        c->lv[y * c->w + x] = 4;
    }
    if (rnd(c) < 0.35f)         /* little flowers */
        for (int i = 0; i < c->w / 8; i++)
            set(c, rint_(c, 0, c->w - 1), rint_(c, 0, c->h - 1), 6);
    for (int i = 0; i < c->w * c->h; i++)
        if (!c->lv[i]) c->lv[i] = 3;
}

static void r_brick(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = rnd(c) < 0.6f ? 0xB0503A : 0x8A8A9A;
    c->col[2] = rnd(c) < 0.5f ? 0xC8BCA8 : 0x50505A;    /* mortar */
    int bh = c->h >= 16 ? c->h / 4 : c->h / 2, bw = c->w >= 16 ? c->w / 2 : c->w;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            int row = y / bh, off = (row & 1) * bw / 2;
            int mortar = y % bh == bh - 1 || (x + off) % bw == bw - 1;
            set(c, x, y, mortar ? 2 : 1);
            if (!mortar) {
                int top = y % bh == 0, left = (x + off) % bw == 0;
                c->lv[y * c->w + x] = (uint8_t)(top || left ? 4 : y % bh == bh - 2 ? 2 : 3);
            } else {
                c->lv[y * c->w + x] = 2;
            }
        }
    for (int i = 0; i < c->w * c->h / 16; i++) {        /* wear */
        int k = rint_(c, 0, c->w * c->h - 1);
        if (c->m[k] == 1 && c->lv[k] == 3) c->lv[k] = 2;
    }
}

static void r_stone(cv_t *c)
{
    /* irregular stones: cells around random points, wrapping */
    c->wrap = 1;
    c->col[1] = rnd(c) < 0.6f ? 0x8A8A9A : 0x9A8A70;
    c->col[2] = 0x3A3A46;
    int np = c->w >= 32 ? 6 : c->w >= 16 ? 4 : 2;
    float px[8], py[8];
    for (int i = 0; i < np; i++) {
        px[i] = rnd(c);
        py[i] = rnd(c);
    }
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = U(c, x), v = V(c, y), d1 = 9, d2 = 9;
            int near = 0;
            for (int i = 0; i < np; i++)
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++) {
                        float dx = u - px[i] - (float)ox, dy = v - py[i] - (float)oy;
                        float d = dx * dx + dy * dy;
                        if (d < d1) { d2 = d1; d1 = d; near = i; }
                        else if (d < d2) d2 = d;
                    }
            float gap = sqrtf(d2) - sqrtf(d1);
            int mortar = gap < 0.9f / (float)c->w;
            set(c, x, y, mortar ? 2 : 1);
            c->lv[y * c->w + x] = (uint8_t)(mortar ? 2 : near % 2 ? 3 : 4);
        }
    /* each stone lit from the top left */
    static uint8_t lv[N];
    memcpy(lv, c->lv, sizeof lv);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            if (get(c, x, y) != 1) continue;
            if (get(c, x - 1, y) == 2 || get(c, x, y - 1) == 2) lv[y * c->w + x] = 5;
            else if (get(c, x + 1, y) == 2 || get(c, x, y + 1) == 2) lv[y * c->w + x] = 2;
        }
    memcpy(c->lv, lv, sizeof lv);
}

static void r_water(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = rnd(c) < 0.8f ? 0x3A78D8 : 0x30A8B0;
    float ph = rf(c, 0, 6.28f);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            set(c, x, y, 1);
            float s = sinf(((float)x / (float)c->w) * 6.2832f * 2 + ph + (float)(y / 4) * 1.7f);
            int crest = (y % 4 == 0 && s > 0.6f), deep = (y % 4 == 2 && s < -0.5f);
            c->lv[y * c->w + x] = (uint8_t)(crest ? 5 : deep ? 2 : 3);
        }
}

static void r_wood(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = pick(c, P_WOOD, 4);
    int ph = c->h >= 16 ? c->h / 4 : c->h / 2;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            set(c, x, y, 1);
            int row = y / ph;
            int seam = y % ph == ph - 1 || (x + row * 5) % c->w == 0;
            int grain = (x * 3 + row * 7 + (y % ph) * 11) % 9 == 0;
            c->lv[y * c->w + x] = (uint8_t)(seam ? 1 : y % ph == 0 ? 4 : grain ? 2 : 3);
        }
}

static void r_sand(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = rnd(c) < 0.7f ? 0xE0C080 : 0x9A6A40;    /* sand or dirt */
    tile_noise(c, 1, 0, 0);
    for (int i = 0; i < c->w * c->h; i++) {
        float r = rnd(c);
        c->lv[i] = (uint8_t)(r < 0.12f ? 2 : r < 0.2f ? 4 : 3);
    }
}

static void r_lava(cv_t *c)
{
    c->wrap = 1;
    c->col[1] = 0xD04018;
    c->col[2] = 0xFFC040; c->flat[2] = 1;
    float ph = rf(c, 0, 6.28f);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float u = (float)x / (float)c->w * 6.2832f, v = (float)y / (float)c->h * 6.2832f;
            float s = sinf(u * 2 + ph) + sinf(v * 2 + u + ph * 2) + sinf(v - u * 2);
            set(c, x, y, s > 1.3f ? 2 : 1);
            c->lv[y * c->w + x] = (uint8_t)(s > 1.3f ? 3 : s > 0.4f ? 4 : s < -1.2f ? 2 : 3);
        }
}

static void r_ground(cv_t *c)
{
    /* a platform: grass on top, earth below; repeats left to right */
    c->col[1] = 0x9A6A40;
    c->col[2] = rnd(c) < 0.8f ? 0x4CA840 : 0xE8ECF8;     /* grass or snow */
    int g = c->h >= 16 ? c->h / 4 : 2;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            int top = y < g + ((x * 7 + 3) % 5 == 0 ? 1 : 0);
            set(c, x, y, top ? 2 : 1);
            c->lv[y * c->w + x] = (uint8_t)(top ? (y == 0 ? 5 : 4) : ((x * 5 + y * 3) % 7 == 0 ? 2 : 3));
        }
    for (int i = 0; i < c->w / 4; i++) {        /* pebbles */
        int x = rint_(c, 0, c->w - 1), y = rint_(c, g + 1, c->h - 1);
        c->lv[y * c->w + x] = 5;
    }
}

/* ---------------------------------------------------------------- table */

typedef struct {
    const char *id, *name;
    void (*draw)(cv_t *c);
} recipe_t;

static const recipe_t recipes[] = {
    { "ship", "spaceship", r_ship },        { "alien", "space invader", r_alien },
    { "monster", "monster", r_monster },    { "robot", "robot", r_robot },
    { "hero", "character", r_hero },        { "hero_side", "character, side view", r_hero_side },
    { "car", "car", r_car },                { "slime", "slime", r_slime },
    { "ghost", "ghost", r_ghost },          { "bat", "bat", r_bat },
    { "skull", "skull", r_skull },          { "coin", "coin", r_coin },
    { "gem", "gem", r_gem },                { "heart", "heart", r_heart },
    { "star", "star", r_star },             { "key", "key", r_key },
    { "sword", "sword", r_sword },          { "potion", "potion", r_potion },
    { "chest", "treasure chest", r_chest }, { "crate", "crate", r_crate },
    { "tree", "tree", r_tree },             { "flower", "flower", r_flower },
    { "mushroom", "mushroom", r_mushroom }, { "rock", "rock", r_rock },
    { "bomb", "bomb", r_bomb },             { "fire", "flame", r_fire },
    { "bullet", "bullet", r_bullet },       { "explosion", "explosion", r_explosion },
    { "apple", "apple", r_apple },          { "cloud", "cloud", r_cloud },
    { "grass", "grass tile", r_grass },     { "brick", "brick tile", r_brick },
    { "stone", "stone tile", r_stone },     { "water", "water tile", r_water },
    { "wood", "wood tile", r_wood },        { "sand", "sand tile", r_sand },
    { "lava", "lava tile", r_lava },        { "ground", "ground tile", r_ground },
};

#define NRECIPES ((int)(sizeof recipes / sizeof recipes[0]))

int spr_recipes(void) { return NRECIPES; }
const char *spr_recipe_id(int i) { return i >= 0 && i < NRECIPES ? recipes[i].id : NULL; }
const char *spr_recipe_name(int i) { return i >= 0 && i < NRECIPES ? recipes[i].name : NULL; }

int spr_find(const char *gen)
{
    for (int i = 0; i < NRECIPES; i++)
        if (gen && !strcmp(gen, recipes[i].id))
            return i;
    return -1;
}

/* ---------------------------------------------------------------- render */

static uint32_t nearest(uint32_t c, const uint32_t *pal, int n)
{
    int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
    uint32_t best = c;
    long bd = -1;
    for (int i = 0; i < n; i++) {
        int pr = (int)(pal[i] >> 16 & 255), pg = (int)(pal[i] >> 8 & 255), pb = (int)(pal[i] & 255);
        int rm = (r + pr) / 2, dr = r - pr, dg = g - pg, db = b - pb;
        long d = (long)((512 + rm) * dr * dr >> 8) + 4L * dg * dg + (long)((767 - rm) * db * db >> 8);
        if (bd < 0 || d < bd) {
            bd = d;
            best = pal[i];
        }
    }
    return best;
}

int spr_make(const spr_req_t *r, spr_img_t *out)
{
    int k = spr_find(r->gen);
    if (k < 0)
        return -1;
    static cv_t c;
    memset(&c, 0, sizeof c);
    c.w = r->w == 8 || r->w == 32 ? r->w : 16;
    c.h = c.w;
    uint32_t h = ai_fnv1a(r->gen, (int)strlen(r->gen), 2166136261u);
    c.rng = (h ^ (r->seed * 2654435761u)) | 1;
    for (int i = 0; i < 4; i++)
        rnd(&c);
    c.seed = r->seed ? r->seed : 1;
    recipes[k].draw(&c);
    if (r->color[0] != SPR_NO_COLOR) c.col[1] = r->color[0];
    if (r->color[1] != SPR_NO_COLOR) c.col[2] = r->color[1];

    out->w = c.w;
    out->h = c.h;
    int rad = c.w >= 32 ? 3 : c.w >= 16 ? 2 : 1;
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            int i = y * c.w + x, m = c.m[i];
            if (!m) {
                out->px[i] = SPR_CLEAR;
                continue;
            }
            int level;
            if (c.lv[i]) {
                level = c.lv[i] - 1;
            } else if (c.flat[m]) {
                level = 2;
            } else {
                /* the outward direction: where the other materials are */
                float gx = 0, gy = 0;
                int other = 0, total = 0;
                for (int dy = -rad; dy <= rad; dy++)
                    for (int dx = -rad; dx <= rad; dx++) {
                        if (dx * dx + dy * dy > rad * rad || (!dx && !dy)) continue;
                        total++;
                        if (get(&c, x + dx, y + dy) != m) {
                            gx += (float)dx;
                            gy += (float)dy;
                            other++;
                        }
                    }
                float s = 2.0f;
                if (other) {
                    float len = sqrtf(gx * gx + gy * gy);
                    float lit = len > 0 ? -(gx + gy) / (len * 1.4142f) : 0;
                    float near = (float)other / (float)total * 2.5f;
                    s += lit * (near > 1 ? 1 : near) * 1.4f;
                }
                if (!c.wrap) {
                    float v = ((float)y + 0.5f) / (float)c.h;
                    s += (0.5f - v) * 0.6f;
                }
                level = (int)floorf(s + 0.5f);
                if (level < 1) level = 1;
                if (level > 3) level = 3;
            }
            out->px[i] = ramp(c.col[m], level);
        }
    /* outline: empty pixels next to the shape */
    if (r->outline && !c.wrap) {
        static uint8_t line[N];
        memset(line, 0, sizeof line);
        for (int y = 0; y < c.h; y++)
            for (int x = 0; x < c.w; x++) {
                if (c.m[y * c.w + x]) continue;
                static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int k2 = 0; k2 < 4; k2++) {
                    int m = get(&c, x + d[k2][0], y + d[k2][1]);
                    if (m && !c.soft[m]) {
                        line[y * c.w + x] = (uint8_t)m;
                        break;
                    }
                }
            }
        for (int i = 0; i < c.w * c.h; i++)
            if (line[i])
                out->px[i] = ramp(c.col[line[i]], 5);
    }
    if (r->palette && r->npalette > 0)
        for (int i = 0; i < c.w * c.h; i++)
            if (out->px[i] != SPR_CLEAR)
                out->px[i] = nearest(out->px[i], r->palette, r->npalette);
    (void)count;
    return 0;
}
