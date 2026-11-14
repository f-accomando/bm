#include "icons.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- shapes */

/* signed distances: negative inside */

static float len2(float x, float y) { return sqrtf(x * x + y * y); }

static float rrect(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return len2(ox, oy) + (in < 0 ? in : 0) - r;
}

static float circle(float px, float py, float cx, float cy, float r)
{
    return len2(px - cx, py - cy) - r;
}

static float capsule(float px, float py, float ax, float ay, float bx, float by, float r)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = (pax * bax + pay * bay) / (bax * bax + bay * bay);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return len2(pax - bax * h, pay - bay * h) - r;
}

static float un(float a, float b) { return a < b ? a : b; }
static float cut(float a, float b) { return a > -b ? a : -b; }
static float both(float a, float b) { return a > b ? a : b; }

#define CX 13.5f                    /* the middle of the box */

/* whole-pixel edges wherever possible: the shapes stay crisp at 1x */

static float keyboard(float x, float y)
{
    float d = rrect(x, y, CX, 9.0f, 11.5f, 7.0f, 2.5f);
    for (int row = 0; row < 2; row++)               /* two rows of six keys */
        for (int k = 0; k < 6; k++)
            d = cut(d, rrect(x, y, 6.0f + k * 3.0f, 5.0f + row * 3.0f, 1.0f, 1.0f, 0.25f));
    d = cut(d, rrect(x, y, 6.0f, 11.0f, 1.0f, 1.0f, 0.25f));
    d = cut(d, rrect(x, y, 21.0f, 11.0f, 1.0f, 1.0f, 0.25f));
    d = cut(d, rrect(x, y, CX, 11.0f, 5.5f, 1.0f, 0.25f)); /* the space bar */
    return d;
}

static float pad(float x, float y)
{
    float d = rrect(x, y, CX, 6.5f, 11.5f, 4.5f, 4.0f);       /* body */
    d = un(d, capsule(x, y, 7.0f, 8.0f, 4.6f, 14.2f, 3.3f));   /* grips, a little outwards */
    d = un(d, capsule(x, y, 20.0f, 8.0f, 22.4f, 14.2f, 3.3f));
    d = cut(d, rrect(x, y, CX, 4.5f, 3.5f, 1.5f, 0.5f));       /* touchpad */
    d = cut(d, rrect(x, y, 6.5f, 6.5f, 2.5f, 0.5f, 0.0f));     /* cross */
    d = cut(d, rrect(x, y, 6.5f, 6.5f, 0.5f, 2.5f, 0.0f));
    d = cut(d, rrect(x, y, 20.5f, 4.5f, 0.5f, 0.5f, 0.0f));    /* four buttons */
    d = cut(d, rrect(x, y, 20.5f, 8.5f, 0.5f, 0.5f, 0.0f));
    d = cut(d, rrect(x, y, 18.5f, 6.5f, 0.5f, 0.5f, 0.0f));
    d = cut(d, rrect(x, y, 22.5f, 6.5f, 0.5f, 0.5f, 0.0f));
    return d;
}

static float wifi(float x, float y)
{
    const float cy = 16.0f, t = 1.1f;               /* arcs: half thickness */
    float r = len2(x - CX, y - cy);
    /* a wedge of 90 degrees opening upwards from (CX, cy) */
    float wedge = both((x - CX + y - cy) * 0.7071f, (CX - x + y - cy) * 0.7071f);
    float d = circle(x, y, CX, cy - 0.5f, 1.9f);
    static const float radii[] = { 6.0f, 10.3f, 14.6f };
    for (int i = 0; i < 3; i++)
        d = un(d, both(fabsf(r - radii[i]) - t, wedge));
    return d;
}

static float ethernet(float x, float y)
{
    float d = rrect(x, y, CX, 9.0f, 9.5f, 8.0f, 2.0f);
    float hole = rrect(x, y, CX, 8.5f, 5.5f, 3.5f, 0.3f);          /* the socket */
    hole = un(hole, rrect(x, y, CX, 13.5f, 2.5f, 1.5f, 0.3f));      /* its latch */
    d = cut(d, hole);
    for (int k = 0; k < 4; k++)                                      /* contacts */
        d = un(d, rrect(x, y, 10.5f + k * 2.0f, 6.0f, 0.5f, 1.0f, 0.0f));
    return d;
}

/* the outline of a shape, `t` thick, inside its edge */
static float outline(float d, float t) { return both(d, -d - t); }

static float mouse(float x, float y)
{
    /* filled like the other icons: an upright capsule; cut into it, the
     * line between the buttons from the top to under the wheel and the
     * wheel's outline (odd widths around CX: edges on whole pixels) */
    float wheel = rrect(x, y, CX, 5.5f, 1.5f, 2.5f, 1.5f);
    float d = rrect(x, y, CX, 9.0f, 6.5f, 9.0f, 6.5f);
    d = cut(d, both(rrect(x, y, CX, 5.0f, 0.5f, 5.0f, 0.0f), -wheel));
    return cut(d, outline(wheel, 1.0f));
}

/* a polygon's signed distance (its edges; inside by the crossings) */
static float poly(float x, float y, const float *p, int n)
{
    float d = 1e9f;
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float ax = p[2 * j], ay = p[2 * j + 1], bx = p[2 * i], by = p[2 * i + 1];
        float e = capsule(x, y, ax, ay, bx, by, 0.0f);
        if (e < d)
            d = e;
        if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax)
            in = !in;
    }
    return in ? -d : d;
}

/* the battery (the RGB30): an outline with its terminal at the right, up
 * to four bars of charge inside; on the charger a bolt instead of the
 * bars, the outline cut around it */
static int batt_bars, batt_bolt;

static float battery(float x, float y)
{
    static const float bolt[] = { 16.0f, 0.0f, 7.5f, 10.5f, 12.0f, 10.5f,
                                  10.0f, 18.0f, 18.5f, 7.0f, 14.0f, 7.0f };
    float d = outline(rrect(x, y, 12.5f, 9.0f, 10.5f, 6.0f, 2.5f), 2.0f);     /* x 2..23, y 3..15 */
    d = un(d, rrect(x, y, 24.0f, 9.0f, 1.0f, 3.0f, 0.5f));
    if (batt_bolt) {
        float b = poly(x, y, bolt, 6);
        return un(cut(d, b - 1.5f), b);
    }
    for (int k = 0; k < batt_bars; k++)
        d = un(d, rrect(x, y, 6.5f + 4.0f * k, 9.0f, 1.5f, 3.0f, 0.0f));
    return d;
}

/* ---------------------------------------------------------------- masks */

static const uint8_t digits[4][7] = {           /* 5x7, bit 4 = left column */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
    { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
};

#define DISC_Y   16.5f              /* the number disc, centred at (CX, DISC_Y) */
#define DISC_R   5.6f
#define GAP_R    7.3f               /* cut around it */

static icon_mask_t masks[ICON_COUNT][ICON_DOT + 1];
static int made[ICON_COUNT][ICON_DOT + 1];

static float coverage(float (*f)(float, float), int x, int y)
{
    int n = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            n += f(x + (i + 0.5f) / 4.0f, y + (j + 0.5f) / 4.0f) <= 0.0f;
    return (float)n / 16.0f;
}

static float disc_r;
static float disc(float x, float y) { return circle(x, y, CX, DISC_Y, disc_r); }

const icon_mask_t *icon_mask(int icon, int num)
{
    if (icon < 0 || icon >= ICON_COUNT || num < 0 || num > ICON_DOT)
        return NULL;
    icon_mask_t *m = &masks[icon][num];
    if (made[icon][num])
        return m;
    float (*f)(float, float) = icon == ICON_KEYBOARD ? keyboard : icon == ICON_PAD ? pad
                             : icon == ICON_WIFI ? wifi : icon == ICON_MOUSE ? mouse : ethernet;
    int dot = num == ICON_DOT;                  /* the disc, no number */
    memset(m, 0, sizeof *m);
    for (int y = 0; y < ICON_BH; y++)
        for (int x = 0; x < ICON_W; x++) {
            int i = y * ICON_W + x;
            float a = y < ICON_H ? coverage(f, x, y) : 0.0f;
            if (num) {
                disc_r = GAP_R;
                a *= 1.0f - coverage(disc, x, y);
                disc_r = DISC_R;
                m->disc[i] = (uint8_t)(coverage(disc, x, y) * 255.0f + 0.5f);
                int dx = x - 11, dy = y - 13;             /* the digit, on whole pixels */
                m->digit[i] = !dot && dx >= 0 && dx < 5 && dy >= 0 && dy < 7 &&
                              (digits[num - 1][dy] >> (4 - dx) & 1);
            }
            m->icon[i] = (uint8_t)(a * 255.0f + 0.5f);
        }
    made[icon][num] = 1;
    return m;
}

const icon_mask_t *icon_battery(int bars, int charging)
{
    static icon_mask_t bm[5][2];
    static int done[5][2];
    bars = bars < 0 ? 0 : bars > 4 ? 4 : bars;
    charging = charging != 0;
    icon_mask_t *m = &bm[bars][charging];
    if (done[bars][charging])
        return m;
    batt_bars = bars;
    batt_bolt = charging;
    memset(m, 0, sizeof *m);
    for (int y = 0; y < ICON_H; y++)
        for (int x = 0; x < ICON_W; x++)
            m->icon[y * ICON_W + x] = (uint8_t)(coverage(battery, x, y) * 255.0f + 0.5f);
    done[bars][charging] = 1;
    return m;
}

/* ----------------------------------------------------------- Switch tab icons */

static float tab_disc(float x, float y)
{
    return circle(x, y, 16.0f, 16.0f, 14.5f);
}

static float tab_ring_t;
static float tab_ring(float x, float y)
{
    return outline(circle(x, y, 16.0f, 16.0f, 14.5f), tab_ring_t);
}

static float tab_market(float x, float y)
{
    float body = rrect(x, y, 16.0f, 19.0f, 6.2f, 4.2f, 1.2f);
    float h1 = capsule(x, y, 13.2f, 16.0f, 13.2f, 11.2f, 0.9f);
    float h2 = capsule(x, y, 13.2f, 11.2f, 18.8f, 11.2f, 0.9f);
    float h3 = capsule(x, y, 18.8f, 11.2f, 18.8f, 16.0f, 0.9f);
    return un(body, un(h1, un(h2, h3)));
}

static float tab_games(float x, float y)
{
    float body = rrect(x, y, 16.0f, 16.0f, 6.2f, 7.8f, 1.2f);
    /* Top-right corner chamfer cut */
    float cut_box = rrect((x - 22.2f) * 0.7071f + (y - 8.2f) * 0.7071f,
                          -(x - 22.2f) * 0.7071f + (y - 8.2f) * 0.7071f,
                          0.0f, 0.0f, 2.5f, 2.5f, 0.3f);
    body = cut(body, cut_box);

    /* Left lock notch */
    float notch = rrect(x, y, 9.6f, 12.5f, 1.0f, 1.6f, 0.4f);
    body = cut(body, notch);

    /* Label window cutout in lower half */
    float label = rrect(x, y, 16.0f, 18.2f, 4.0f, 3.4f, 0.8f);

    /* Pin grooves on top edge */
    float g1 = rrect(x, y, 13.0f, 8.8f, 0.5f, 1.2f, 0.2f);
    float g2 = rrect(x, y, 15.0f, 8.8f, 0.5f, 1.2f, 0.2f);
    float g3 = rrect(x, y, 17.0f, 8.8f, 0.5f, 1.2f, 0.2f);
    float grooves = un(un(g1, g2), g3);

    return cut(cut(body, label), grooves);
}

/* a flat pad with a cross and two round buttons (the NES tab; original drawing) */
static float tab_nes(float x, float y)
{
    float body = rrect(x, y, 16.0f, 16.0f, 9.5f, 5.0f, 1.0f);
    float ch = rrect(x, y, 11.0f, 16.0f, 2.6f, 0.9f, 0.2f), cv = rrect(x, y, 11.0f, 16.0f, 0.9f, 2.6f, 0.2f);
    float b1 = circle(x, y, 19.5f, 17.0f, 1.3f), b2 = circle(x, y, 22.5f, 17.0f, 1.3f);
    return cut(body, un(un(ch, cv), un(b1, b2)));
}

/* a pad with round ends and four buttons in a diamond (the SNES tab; original drawing) */
static float tab_snes(float x, float y)
{
    float body = un(un(circle(x, y, 10.0f, 16.0f, 5.6f), circle(x, y, 22.0f, 16.0f, 5.6f)), rrect(x, y, 16.0f, 16.0f, 6.0f, 4.2f, 0.5f));
    float ch = rrect(x, y, 10.0f, 16.0f, 2.4f, 0.8f, 0.2f), cv = rrect(x, y, 10.0f, 16.0f, 0.8f, 2.4f, 0.2f);
    float b = un(un(circle(x, y, 22.0f, 13.6f, 1.0f), circle(x, y, 22.0f, 18.4f, 1.0f)),
                 un(circle(x, y, 19.6f, 16.0f, 1.0f), circle(x, y, 24.4f, 16.0f, 1.0f)));
    return cut(body, un(un(ch, cv), b));
}

static float tab_dev(float x, float y)
{
    float l1 = capsule(x, y, 11.8f, 11.2f, 8.8f, 16.0f, 0.95f);
    float l2 = capsule(x, y, 8.8f, 16.0f, 11.8f, 20.8f, 0.95f);
    float slash = capsule(x, y, 17.5f, 10.2f, 14.5f, 21.8f, 0.95f);
    float r1 = capsule(x, y, 20.2f, 11.2f, 23.2f, 16.0f, 0.95f);
    float r2 = capsule(x, y, 23.2f, 16.0f, 20.2f, 20.8f, 0.95f);
    return un(un(l1, l2), un(slash, un(r1, r2)));
}

static float tab_settings(float x, float y)
{
    float body = circle(x, y, 16.0f, 16.0f, 6.0f);
    float teeth = 1e9f;
    for (int i = 0; i < 6; i++) {
        float ang = (float)i * 1.04719755f; /* pi / 3 */
        float tx = 16.0f + 6.8f * cosf(ang);
        float ty = 16.0f + 6.8f * sinf(ang);
        float ca = cosf(-ang), sa = sinf(-ang);
        float rx = (x - tx) * ca - (y - ty) * sa + tx;
        float ry = (x - tx) * sa + (y - ty) * ca + ty;
        float tooth = rrect(rx, ry, tx, ty, 1.5f, 1.8f, 0.5f);
        teeth = un(teeth, tooth);
    }
    float gear = un(body, teeth);
    float hole = circle(x, y, 16.0f, 16.0f, 2.6f);
    return cut(gear, hole);
}

const tab_icon_mask_t *tab_icon_mask(int icon, int active)
{
    static tab_icon_mask_t masks[TAB_ICON_COUNT][2];
    static int made[TAB_ICON_COUNT][2];
    if (icon < 0 || icon >= TAB_ICON_COUNT)
        return NULL;
    active = active != 0;
    tab_icon_mask_t *m = &masks[icon][active];
    if (made[icon][active])
        return m;

    float (*glyph_fn)(float, float) =
        icon == TAB_ICON_MARKET   ? tab_market :
        icon == TAB_ICON_NES      ? tab_nes :
        icon == TAB_ICON_SNES     ? tab_snes :
        icon == TAB_ICON_GAMES    ? tab_games :
        icon == TAB_ICON_DEV      ? tab_dev :
                                    tab_settings;

    tab_ring_t = active ? 2.0f : 1.2f;
    memset(m, 0, sizeof *m);

    for (int y = 0; y < TAB_ICON_H; y++) {
        for (int x = 0; x < TAB_ICON_W; x++) {
            int i = y * TAB_ICON_W + x;
            m->disc[i]  = (uint8_t)(coverage(tab_disc, x, y) * 255.0f + 0.5f);
            m->ring[i]  = (uint8_t)(coverage(tab_ring, x, y) * 255.0f + 0.5f);
            m->glyph[i] = (uint8_t)(coverage(glyph_fn, x, y) * 255.0f + 0.5f);
        }
    }
    made[icon][active] = 1;
    return m;
}
