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

static icon_mask_t masks[ICON_COUNT][5];
static int made[ICON_COUNT][5];

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
    if (icon < 0 || icon >= ICON_COUNT || num < 0 || num > 4)
        return NULL;
    icon_mask_t *m = &masks[icon][num];
    if (made[icon][num])
        return m;
    float (*f)(float, float) = icon == ICON_KEYBOARD ? keyboard : icon == ICON_PAD ? pad
                             : icon == ICON_WIFI ? wifi : ethernet;
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
                m->digit[i] = dx >= 0 && dx < 5 && dy >= 0 && dy < 7 &&
                              (digits[num - 1][dy] >> (4 - dx) & 1);
            }
            m->icon[i] = (uint8_t)(a * 255.0f + 0.5f);
        }
    made[icon][num] = 1;
    return m;
}
