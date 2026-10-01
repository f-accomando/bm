/*
 * nano8 machine: memory, draw state, drawing on the 4-bit screen, text,
 * numbers, buttons and the conversion to RGB565 (see n8.h).
 */
#include "n8.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The 16 colours and the 16 extra ones (128-143), as the carts expect them. */
static const uint32_t colours[32] = {
    0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
    0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
    0x291814, 0x111D35, 0x422136, 0x125359, 0x742F29, 0x49333B, 0xA28879, 0xF3EF7D,
    0xBE1250, 0xFF6C24, 0xA8E72E, 0x00B543, 0x065AB5, 0x754665, 0xFF6E59, 0xFF9D81,
};

uint32_t n8_rgb(int i)
{
    if (i >= 128 && i < 144)
        return colours[16 + i - 128];
    return colours[i & 15];
}

/* ------------------------------------------------------------ numbers */

int32_t n8_fix(double v)
{
    if (v != v)
        return 0;
    double t = floor(v * 65536.0);
    if (t >= -2147483648.0 && t <= 2147483647.0)
        return (int32_t)t;
    if (isinf(t))
        return t > 0 ? 0x7FFFFFFF : (int32_t)0x80000001;
    t = fmod(t, 4294967296.0);
    if (t < 0)
        t += 4294967296.0;
    return (int32_t)(uint32_t)t;
}

int n8_int(double v)
{
    if (v != v)
        return 0;
    if (v <= -32768.0)
        return -32768;
    if (v >= 32767.0)
        return 32767;
    return (int)floor(v);
}

int32_t n8_shl(int32_t a, int n)
{
    if (n < 0) return n8_shr(a, -n);
    if (n >= 32) return 0;
    return (int32_t)((uint32_t)a << n);
}

int32_t n8_shr(int32_t a, int n)
{
    if (n < 0) return n8_shl(a, -n);
    if (n >= 32) return a < 0 ? -1 : 0;
    return a >> n;
}

int32_t n8_lshr(int32_t a, int n)
{
    if (n < 0) return n8_shl(a, -n);
    if (n >= 32) return 0;
    return (int32_t)((uint32_t)a >> n);
}

int32_t n8_rotl(int32_t a, int n)
{
    n &= 31;
    return n ? (int32_t)((uint32_t)a << n | (uint32_t)a >> (32 - n)) : a;
}

int32_t n8_rotr(int32_t a, int n)
{
    return n8_rotl(a, 32 - (n & 31));
}

int n8_tostr(double v, int flags, char *out, size_t n)
{
    if (flags & 1) {
        uint32_t f = (uint32_t)n8_fix(v);
        if (flags & 2)
            return snprintf(out, n, "0x%08x", (unsigned)f);
        return snprintf(out, n, "0x%04x.%04x", (unsigned)(f >> 16), (unsigned)(f & 0xFFFF));
    }
    if (flags & 2)
        return snprintf(out, n, "%ld", (long)n8_fix(v));
    if (v != v)
        return snprintf(out, n, "nan");
    if (isinf(v))
        return snprintf(out, n, v > 0 ? "32767.9999" : "-32767.9999");
    if (v == floor(v) && fabs(v) < 1e15)
        return snprintf(out, n, "%.0f", v == 0 ? 0.0 : v);
    /* four decimals, the zeros at the end dropped */
    char buf[64];
    snprintf(buf, sizeof buf, "%.4f", v);
    size_t len = strlen(buf);
    while (len && buf[len - 1] == '0')
        buf[--len] = 0;
    if (len && buf[len - 1] == '.')
        buf[--len] = 0;
    if (!strcmp(buf, "-0"))
        strcpy(buf, "0");
    return snprintf(out, n, "%s", buf);
}

static int digit(int c, int base)
{
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
            c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
    return d < base ? d : -1;
}

int n8_tonum(const char *s, size_t len, int flags, double *out)
{
    size_t i = 0;
    int neg = 0, base = 10;
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (i < len && s[i] == '-') { neg = 1; i++; }
    if (flags & 1)
        base = 16;
    else if (i + 1 < len && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { base = 16; i += 2; }
    else if (i + 1 < len && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) { base = 2; i += 2; }
    double v = 0;
    int any = 0;
    for (; i < len && digit(s[i], base) >= 0; i++, any = 1)
        v = v * base + digit(s[i], base);
    if (i < len && s[i] == '.') {
        double k = 1.0 / base;
        for (i++; i < len && digit(s[i], base) >= 0; i++, any = 1, k /= base)
            v += digit(s[i], base) * k;
    }
    if (base == 10 && any && i < len && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        int eneg = 0, e = 0, ed = 0;
        if (j < len && (s[j] == '-' || s[j] == '+')) eneg = s[j++] == '-';
        for (; j < len && s[j] >= '0' && s[j] <= '9'; j++, ed = 1)
            e = e * 10 + (s[j] - '0');
        if (ed) {
            v *= pow(10, eneg ? -e : e);
            i = j;
        }
    }
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (!any || i != len)
        return 0;
    if (neg)
        v = -v;
    if (flags & 2)
        v = n8_fix(v / 65536.0) / 65536.0;  /* the digits are the raw 32 bits */
    else if (base != 10)
        v = n8_fix(v) / 65536.0;            /* 0xffff is -1, as in the carts */
    *out = v;
    return 1;
}

/* ------------------------------------------------------------ random */

static uint32_t rng_next(n8_t *m)
{
    m->rng[1] = m->rng[0] + (m->rng[1] >> 16 | m->rng[1] << 16);
    m->rng[0] += m->rng[1];
    return m->rng[1];
}

int32_t n8_rnd(n8_t *m, int32_t limit)
{
    uint32_t r = rng_next(m);
    return limit > 0 ? (int32_t)(r % (uint32_t)limit) : 0;
}

void n8_srand(n8_t *m, int32_t seed)
{
    uint32_t a = (uint32_t)seed;
    if (!a)
        a = 0xDEADBEEF;
    m->rng[0] = a ^ 0xBEAD29BA;
    m->rng[1] = a;
    for (int i = 0; i < 32; i++)
        rng_next(m);
}

/* ------------------------------------------------------------ memory */

void n8_poke(n8_t *m, uint32_t a, int v)
{
    m->ram[a & 0xFFFF] = (uint8_t)v;
}

void n8_memcpy(n8_t *m, uint32_t dst, uint32_t src, int32_t len)
{
    if (len <= 0)
        return;
    dst &= 0xFFFF;
    src &= 0xFFFF;
    if (dst + (uint32_t)len <= N8_RAM_SIZE && src + (uint32_t)len <= N8_RAM_SIZE) {
        memmove(m->ram + dst, m->ram + src, (size_t)len);
        return;
    }
    /* wrapping around 64 KiB: byte by byte, in the direction that keeps an
     * overlap right */
    if (dst > src)
        for (int32_t i = len - 1; i >= 0; i--)
            m->ram[(dst + (uint32_t)i) & 0xFFFF] = m->ram[(src + (uint32_t)i) & 0xFFFF];
    else
        for (int32_t i = 0; i < len; i++)
            m->ram[(dst + (uint32_t)i) & 0xFFFF] = m->ram[(src + (uint32_t)i) & 0xFFFF];
}

void n8_memset(n8_t *m, uint32_t dst, int v, int32_t len)
{
    if (len <= 0)
        return;
    dst &= 0xFFFF;
    if (dst + (uint32_t)len <= N8_RAM_SIZE) {
        memset(m->ram + dst, v, (size_t)len);
        return;
    }
    for (int32_t i = 0; i < len; i++)
        m->ram[(dst + (uint32_t)i) & 0xFFFF] = (uint8_t)v;
}

/* ------------------------------------------------------------ state */

void n8_reset_pal(n8_t *m)
{
    for (int i = 0; i < 16; i++) {
        m->ram[N8_PAL_DRAW + i] = (uint8_t)i;
        m->ram[N8_PAL_SCREEN + i] = (uint8_t)i;
        m->ram[0x5f60 + i] = (uint8_t)i;
    }
    m->ram[N8_PAL_DRAW] |= 0x10;               /* colour 0 transparent */
}

void n8_reset_draw(n8_t *m)
{
    n8_reset_pal(m);
    m->ram[0x5f20] = 0; m->ram[0x5f21] = 0; m->ram[0x5f22] = 128; m->ram[0x5f23] = 128;
    m->ram[N8_PEN] = 6;
    m->ram[0x5f26] = m->ram[0x5f27] = 0;
    memset(m->ram + N8_CAMERA, 0, 4);
    m->ram[N8_SCREEN_MODE] = 0;
    memset(m->ram + N8_FILLP, 0, 4);
    m->ram[0x5f35] = 0;
    memset(m->ram + 0x5f38, 0, 4);
    m->ram[0x5f54] = 0x00;
    m->ram[0x5f55] = 0x60;
    m->ram[0x5f56] = 0x20;
    m->ram[0x5f57] = 0x80;
    m->ram[0x5f5e] = 0xFF;
    m->ram[0x5f5f] = 0;
    memset(m->ram + 0x5f70, 0, 16);
}

void n8_power(n8_t *m)
{
    memset(m->ram, 0, sizeof m->ram);
    memcpy(m->ram, m->rom, N8_ROM_SIZE);
    n8_reset_draw(m);
    memset(m->btn, 0, sizeof m->btn);
    memset(m->btn_prev, 0, sizeof m->btn_prev);
    memset(m->btn_frames, 0, sizeof m->btn_frames);
    n8_srand(m, 0x1234);
    if (!m->fps)
        m->fps = 30;
}

int n8_color(n8_t *m, int c)
{
    if (c >= 0)
        m->ram[N8_PEN] = (uint8_t)c;
    return m->ram[N8_PEN];
}

/* where the sprite sheet, the screen and the map are (0x5f54-0x5f57) */
static uint32_t region(uint8_t v, uint32_t def)
{
    if (v == 0x00) return 0;
    if (v == 0x60) return 0x6000;
    if (v >= 0x80 && v <= 0xE0) return (uint32_t)v << 8;
    return def;
}

static int cam_x(const n8_t *m) { return (int16_t)(m->ram[0x5f28] | m->ram[0x5f29] << 8); }
static int cam_y(const n8_t *m) { return (int16_t)(m->ram[0x5f2a] | m->ram[0x5f2b] << 8); }

/* A drawing call's view of the state. */
typedef struct {
    uint8_t *scr;
    int x0, y0, x1, y1;             /* clip, x1/y1 exclusive */
    int cx, cy;                     /* camera */
    uint8_t pal[16];
    uint8_t transp[16];
    uint16_t pat;
    uint8_t pat_transp;
    uint8_t wmask, rmask;           /* bitplanes */
    uint8_t c1, c2;                 /* a shape's colours, through the palette */
} dc_t;

static void dc_init(n8_t *m, dc_t *d, int col)
{
    d->scr = m->ram + region(m->ram[0x5f55], 0x6000);
    d->x0 = m->ram[0x5f20]; d->y0 = m->ram[0x5f21];
    d->x1 = m->ram[0x5f22]; d->y1 = m->ram[0x5f23];
    if (d->x1 > 128) d->x1 = 128;
    if (d->y1 > 128) d->y1 = 128;
    d->cx = cam_x(m);
    d->cy = cam_y(m);
    for (int i = 0; i < 16; i++) {
        d->pal[i] = m->ram[N8_PAL_DRAW + i] & 15;
        d->transp[i] = m->ram[N8_PAL_DRAW + i] & 0x10;
    }
    d->pat = (uint16_t)(m->ram[0x5f31] | m->ram[0x5f32] << 8);
    d->pat_transp = m->ram[0x5f33] & 1;
    d->wmask = m->ram[0x5f5e] & 15;
    d->rmask = m->ram[0x5f5e] >> 4;
    d->c1 = d->pal[col & 15];
    d->c2 = d->pal[col >> 4 & 15];
}

static inline void wr(dc_t *d, int x, int y, int c)
{
    uint8_t *p = d->scr + y * 64 + (x >> 1);
    if ((d->wmask & d->rmask) != 15) {
        int old = x & 1 ? *p >> 4 : *p & 15;
        c = (old & ~d->wmask) | (c & d->wmask & d->rmask);
    }
    if (x & 1)
        *p = (uint8_t)((*p & 0x0F) | c << 4);
    else
        *p = (uint8_t)((*p & 0xF0) | c);
}

static inline int rd(const uint8_t *base, int x, int y)
{
    if ((unsigned)x >= 128 || (unsigned)y >= 128)
        return 0;
    uint8_t b = base[y * 64 + (x >> 1)];
    return x & 1 ? b >> 4 : b & 15;
}

/* A pixel of a shape at screen (x, y), inside the clip. */
static inline void put(dc_t *d, int x, int y)
{
    if (d->pat && (d->pat >> (15 - ((y & 3) << 2 | (x & 3))) & 1)) {
        if (!d->pat_transp)
            wr(d, x, y, d->c2);
        return;
    }
    wr(d, x, y, d->c1);
}

static inline void dot(dc_t *d, int x, int y)
{
    if (x >= d->x0 && x < d->x1 && y >= d->y0 && y < d->y1)
        put(d, x, y);
}

/* Horizontal run of a shape, screen coordinates, x0 <= x1 inclusive. */
static void span(dc_t *d, int x0, int x1, int y)
{
    if (y < d->y0 || y >= d->y1)
        return;
    if (x0 < d->x0) x0 = d->x0;
    if (x1 >= d->x1) x1 = d->x1 - 1;
    if (x0 > x1)
        return;
    if (d->pat || (d->wmask & d->rmask) != 15) {
        for (int x = x0; x <= x1; x++)
            put(d, x, y);
        return;
    }
    uint8_t *row = d->scr + y * 64, c = d->c1;
    if (x0 & 1) {
        row[x0 >> 1] = (uint8_t)((row[x0 >> 1] & 0x0F) | c << 4);
        x0++;
    }
    if (!(x1 & 1) && x0 <= x1) {
        row[x1 >> 1] = (uint8_t)((row[x1 >> 1] & 0xF0) | c);
        x1--;
    }
    if (x0 < x1)
        memset(row + (x0 >> 1), c | c << 4, (size_t)((x1 - x0 + 1) >> 1));
}

void n8_cls(n8_t *m, int c)
{
    c &= 15;
    memset(m->ram + region(m->ram[0x5f55], 0x6000), c | c << 4, 0x2000);
    m->ram[0x5f20] = 0; m->ram[0x5f21] = 0; m->ram[0x5f22] = 128; m->ram[0x5f23] = 128;
    m->ram[0x5f26] = m->ram[0x5f27] = 0;
}

void n8_pset(n8_t *m, int x, int y, int c)
{
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    dot(&d, x - d.cx, y - d.cy);
}

int n8_pget(const n8_t *m, int x, int y)
{
    x -= cam_x(m);
    y -= cam_y(m);
    return rd(m->ram + region(m->ram[0x5f55], 0x6000), x, y);
}

int n8_sget(const n8_t *m, int x, int y)
{
    return rd(m->ram + region(m->ram[0x5f54], 0), x, y);
}

void n8_sset(n8_t *m, int x, int y, int c)
{
    if ((unsigned)x >= 128 || (unsigned)y >= 128)
        return;
    uint8_t *p = m->ram + region(m->ram[0x5f54], 0) + y * 64 + (x >> 1);
    c = m->ram[N8_PAL_DRAW + (n8_color(m, c) & 15)] & 15;
    *p = x & 1 ? (uint8_t)((*p & 0x0F) | c << 4) : (uint8_t)((*p & 0xF0) | c);
}

/* Walks a line one pixel per step along its longer axis. */
static void walk(int x0, int y0, int x1, int y1, void (*f)(void *, int, int, int), void *ctx)
{
    int dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx >= ady) {
        int sx = dx < 0 ? -1 : 1;
        int32_t y = y0 * 65536 + 32768, step = adx ? (int32_t)(((int64_t)dy << 16) / adx) : 0;
        for (int i = 0; i <= adx; i++, y += step)
            f(ctx, x0 + i * sx, y >> 16, i);
    } else {
        int sy = dy < 0 ? -1 : 1;
        int32_t x = x0 * 65536 + 32768, step = (int32_t)(((int64_t)dx << 16) / ady);
        for (int i = 0; i <= ady; i++, x += step)
            f(ctx, x >> 16, y0 + i * sy, i);
    }
}

static void line_dot(void *ctx, int x, int y, int i)
{
    (void)i;
    dot(ctx, x, y);
}

void n8_line(n8_t *m, int x0, int y0, int x1, int y1, int c)
{
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    x0 -= d.cx; x1 -= d.cx; y0 -= d.cy; y1 -= d.cy;
    /* both ends far outside: nothing to walk through */
    if ((x0 < -256 && x1 < -256) || (x0 > 384 && x1 > 384) || (y0 < -256 && y1 < -256) || (y0 > 384 && y1 > 384))
        return;
    if (y0 == y1) {
        span(&d, x0 < x1 ? x0 : x1, x0 < x1 ? x1 : x0, y0);
        return;
    }
    walk(x0, y0, x1, y1, line_dot, &d);
}

void n8_rect(n8_t *m, int x0, int y0, int x1, int y1, int c, int fill)
{
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 -= d.cx; x1 -= d.cx; y0 -= d.cy; y1 -= d.cy;
    if (fill) {
        int a = y0 < d.y0 ? d.y0 : y0, b = y1 >= d.y1 ? d.y1 - 1 : y1;
        for (int y = a; y <= b; y++)
            span(&d, x0, x1, y);
        return;
    }
    span(&d, x0, x1, y0);
    if (y1 != y0)
        span(&d, x0, x1, y1);
    for (int y = y0 + 1; y < y1; y++) {
        if (y < d.y0 || y >= d.y1)
            continue;
        dot(&d, x0, y);
        if (x1 != x0)
            dot(&d, x1, y);
    }
}

/* the eight points of a circle, or the four spans of a filled one */
static void circ_points(dc_t *d, int cx, int cy, int x, int y, int fill)
{
    if (fill) {
        span(d, cx - x, cx + x, cy + y);
        span(d, cx - x, cx + x, cy - y);
        span(d, cx - y, cx + y, cy + x);
        span(d, cx - y, cx + y, cy - x);
        return;
    }
    dot(d, cx + x, cy + y); dot(d, cx - x, cy + y);
    dot(d, cx + x, cy - y); dot(d, cx - x, cy - y);
    dot(d, cx + y, cy + x); dot(d, cx - y, cy + x);
    dot(d, cx + y, cy - x); dot(d, cx - y, cy - x);
}

void n8_circ(n8_t *m, int cx, int cy, int r, int c, int fill)
{
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    if (r < 0)
        return;
    cx -= d.cx;
    cy -= d.cy;
    if (cx + r < d.x0 || cx - r >= d.x1 || cy + r < d.y0 || cy - r >= d.y1)
        return;
    int x = r, y = 0, err = 1 - r;
    while (y <= x) {
        circ_points(&d, cx, cy, x, y, fill);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void oval_edge(dc_t *d, int cur, int prev, int y)
{
    int a = cur, b = cur;
    if (prev > cur + 1)
        b = prev - 1;
    else if (prev < cur - 1)
        a = prev + 1;
    span(d, a, b, y);
}

void n8_oval(n8_t *m, int x0, int y0, int x1, int y1, int c, int fill)
{
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 -= d.cx; x1 -= d.cx; y0 -= d.cy; y1 -= d.cy;
    if (x1 < d.x0 || x0 >= d.x1 || y1 < d.y0 || y0 >= d.y1 || y1 - y0 > 4096)
        return;
    double cxf = (x0 + x1) / 2.0, cyf = (y0 + y1) / 2.0;
    double rx = (x1 - x0) / 2.0 + 0.5, ry = (y1 - y0) / 2.0 + 0.5;
    int pl = 0, pr = -1;                   /* the row above: left and right ends */
    for (int y = y0; y <= y1; y++) {
        double t = (y - cyf) / ry, k = 1.0 - t * t;
        double hw = k > 0 ? rx * sqrt(k) : 0;
        int l = (int)floor(cxf - hw + 0.5), r = (int)ceil(cxf + hw - 0.5);
        if (l < x0) l = x0;
        if (r > x1) r = x1;
        if (fill || y == y0 || y == y1) {
            span(&d, l, r, y);
        } else {
            /* each end, joined to the row above without gaps */
            oval_edge(&d, l, pl, y);
            oval_edge(&d, r, pr, y);
        }
        pl = l;
        pr = r;
    }
}

void n8_rrect(n8_t *m, int x, int y, int w, int h, int r, int c, int fill)
{
    if (w <= 0 || h <= 0) {
        n8_color(m, c);
        return;
    }
    int lim = (w < h ? w : h) / 2;
    if (r > lim) r = lim;
    if (r <= 0) {
        n8_rect(m, x, y, x + w - 1, y + h - 1, c, fill);
        return;
    }
    dc_t d;
    dc_init(m, &d, n8_color(m, c));
    x -= d.cx;
    y -= d.cy;
    int l = x + r, rr = x + w - 1 - r, t = y + r, b = y + h - 1 - r;
    if (fill) {
        for (int yy = t; yy <= b; yy++)
            span(&d, x, x + w - 1, yy);
    } else {
        span(&d, l, rr, y);
        span(&d, l, rr, y + h - 1);
        for (int yy = t; yy <= b; yy++) {
            dot(&d, x, yy);
            dot(&d, x + w - 1, yy);
        }
    }
    int px = r, py = 0, err = 1 - r;
    while (py <= px) {
        if (fill) {
            span(&d, l - px, rr + px, t - py); span(&d, l - px, rr + px, b + py);
            span(&d, l - py, rr + py, t - px); span(&d, l - py, rr + py, b + px);
        } else {
            dot(&d, l - px, t - py); dot(&d, rr + px, t - py);
            dot(&d, l - px, b + py); dot(&d, rr + px, b + py);
            dot(&d, l - py, t - px); dot(&d, rr + py, t - px);
            dot(&d, l - py, b + px); dot(&d, rr + py, b + px);
        }
        py++;
        if (err < 0) {
            err += 2 * py + 1;
        } else {
            px--;
            err += 2 * (py - px) + 1;
        }
    }
}

/* ------------------------------------------------------------ sprites */

void n8_sspr(n8_t *m, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int fx, int fy)
{
    dc_t d;
    dc_init(m, &d, m->ram[N8_PEN]);
    if (dw < 0) { dw = -dw; dx -= dw - 1; fx = !fx; }
    if (dh < 0) { dh = -dh; dy -= dh - 1; fy = !fy; }
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    const uint8_t *sheet = m->ram + region(m->ram[0x5f54], 0);
    dx -= d.cx;
    dy -= d.cy;
    int ya = dy < d.y0 ? d.y0 : dy, yb = dy + dh > d.y1 ? d.y1 : dy + dh;
    int xa = dx < d.x0 ? d.x0 : dx, xb = dx + dw > d.x1 ? d.x1 : dx + dw;
    if (xa >= xb || ya >= yb)
        return;
    /* source steps in 16.16 */
    int32_t stx = (int32_t)(((int64_t)sw << 16) / dw), sty = (int32_t)(((int64_t)sh << 16) / dh);
    int plain = (d.wmask & d.rmask) == 15;
    for (int y = ya; y < yb; y++) {
        int v = (int)(((int64_t)(y - dy) * sty) >> 16);
        int srcy = fy ? sy + sh - 1 - v : sy + v;
        for (int x = xa; x < xb; x++) {
            int u = (int)(((int64_t)(x - dx) * stx) >> 16);
            int c = rd(sheet, fx ? sx + sw - 1 - u : sx + u, srcy);
            if (d.transp[c])
                continue;
            if (plain) {
                uint8_t *p = d.scr + y * 64 + (x >> 1), pc = d.pal[c];
                *p = x & 1 ? (uint8_t)((*p & 0x0F) | pc << 4) : (uint8_t)((*p & 0xF0) | pc);
            } else {
                wr(&d, x, y, d.pal[c]);
            }
        }
    }
}

void n8_spr(n8_t *m, int n, int x, int y, int w, int h, int fx, int fy)
{
    if (w <= 0 || h <= 0)
        return;
    n &= 255;
    n8_sspr(m, (n & 15) * 8, (n >> 4) * 8, w, h, x, y, w, h, fx, fy);
}

void n8_map_size(const n8_t *m, int *w, int *h)
{
    uint32_t base = m->ram[0x5f56] >= 0x80 ? (uint32_t)m->ram[0x5f56] << 8 : 0x2000;
    int mw = m->ram[0x5f57] ? m->ram[0x5f57] : 256;
    uint32_t size = base == 0x2000 ? 0x2000 : N8_RAM_SIZE - base;
    *w = mw;
    *h = (int)(size / (uint32_t)mw);
}

static int map_addr(const n8_t *m, int x, int y)
{
    int w, h;
    n8_map_size(m, &w, &h);
    if (x < 0 || y < 0 || x >= w || y >= h)
        return -1;
    uint32_t base = m->ram[0x5f56] >= 0x80 ? (uint32_t)m->ram[0x5f56] << 8 : 0x2000;
    uint32_t a = base + (uint32_t)y * (uint32_t)w + (uint32_t)x;
    if (base == 0x2000 && a >= 0x3000)
        a -= 0x2000;                    /* rows 32-63 share the sprite sheet's second half */
    return (int)a;
}

int n8_mget(const n8_t *m, int x, int y)
{
    int a = map_addr(m, x, y);
    return a < 0 ? 0 : m->ram[a];
}

void n8_mset(n8_t *m, int x, int y, int v)
{
    int a = map_addr(m, x, y);
    if (a >= 0)
        m->ram[a] = (uint8_t)v;
}

void n8_map(n8_t *m, int cx, int cy, int sx, int sy, int cw, int ch, int layers)
{
    int cam_dx = cam_x(m), cam_dy = cam_y(m);
    int cx0 = m->ram[0x5f20], cy0 = m->ram[0x5f21], cx1 = m->ram[0x5f22], cy1 = m->ram[0x5f23];
    /* only the cells that can land inside the clip */
    for (int j = 0; j < ch; j++) {
        int y = sy + j * 8 - cam_dy;
        if (y + 8 <= cy0 || y >= cy1)
            continue;
        for (int i = 0; i < cw; i++) {
            int x = sx + i * 8 - cam_dx;
            if (x + 8 <= cx0 || x >= cx1)
                continue;
            int v = n8_mget(m, cx + i, cy + j);
            if (!v)
                continue;
            if (layers && (m->ram[N8_FLAGS + v] & layers) != layers)
                continue;
            n8_spr(m, v, sx + i * 8, sy + j * 8, 8, 8, 0, 0);
        }
    }
}

typedef struct {
    n8_t *m;
    dc_t *d;
    const uint8_t *sheet;
    int32_t mx, my, mdx, mdy;
    int layers;
} tline_t;

static void tline_dot(void *ctx, int x, int y, int i)
{
    tline_t *t = ctx;
    int32_t mx = t->mx + t->mdx * i, my = t->my + t->mdy * i;
    if (x < t->d->x0 || x >= t->d->x1 || y < t->d->y0 || y >= t->d->y1)
        return;
    int tx = mx >> 16, ty = my >> 16;
    int wx = t->m->ram[0x5f38], wy = t->m->ram[0x5f39];
    if (wx) tx = ((tx % wx) + wx) % wx + t->m->ram[0x5f3a];
    if (wy) ty = ((ty % wy) + wy) % wy + t->m->ram[0x5f3b];
    int v = n8_mget(t->m, tx, ty);
    if (!v || (t->layers && (t->m->ram[N8_FLAGS + v] & t->layers) != t->layers))
        return;
    int c = rd(t->sheet, (v & 15) * 8 + (mx >> 13 & 7), (v >> 4) * 8 + (my >> 13 & 7));
    if (!t->d->transp[c])
        wr(t->d, x, y, t->d->pal[c]);
}

void n8_tline(n8_t *m, int x0, int y0, int x1, int y1, int32_t mx, int32_t my, int32_t mdx, int32_t mdy,
              int layers)
{
    dc_t d;
    dc_init(m, &d, m->ram[N8_PEN]);
    tline_t t = { m, &d, m->ram + region(m->ram[0x5f54], 0), mx, my, mdx, mdy, layers };
    x0 -= d.cx; x1 -= d.cx; y0 -= d.cy; y1 -= d.cy;
    if ((x0 < -512 && x1 < -512) || (x0 > 640 && x1 > 640) || (y0 < -512 && y1 < -512) || (y0 > 640 && y1 > 640))
        return;
    walk(x0, y0, x1, y1, tline_dot, &t);
}

/* ------------------------------------------------------------ text */

/* P8SCII "number" characters of the control codes: 0-9, a-z = 10-35 */
static int pnum(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 0;
}

typedef struct {
    n8_t *m;
    dc_t d;
    int fg, bg;                     /* bg < 0: none */
    int wide, tall, stripey, invert, solid, custom;
    int cw, ch;                     /* overrides of the cell size, 0 = default */
    int tab, rhs;
} pen_t;

static void glyph_px(pen_t *p, int x, int y, int c)
{
    int sw = p->wide ? 2 : 1, sh = p->tall ? 2 : 1;
    for (int j = 0; j < sh; j++) {
        if (p->stripey && j)
            continue;
        for (int i = 0; i < sw; i++) {
            int px = x + i, py = y + j;
            if (px >= p->d.x0 && px < p->d.x1 && py >= p->d.y0 && py < p->d.y1)
                wr(&p->d, px, py, c);
        }
    }
}

/* Draws one character at screen (x, y); returns its advance. */
static int draw_char(pen_t *p, int c, int x, int y, const uint8_t *oneoff)
{
    uint8_t rows[8];
    int w, h = 5, adv, cellh = p->ch ? p->ch : 6;
    n8_t *m = p->m;
    if (oneoff) {
        memcpy(rows, oneoff, 8);
        w = 8;
        h = 8;
        adv = 9;
    } else if (p->custom) {
        memcpy(rows, m->ram + N8_FONT + c * 8, 8);
        w = 8;
        h = 8;
        adv = c < 128 ? m->ram[N8_FONT] : m->ram[N8_FONT + 1];
        cellh = m->ram[N8_FONT + 2];
        x += (int8_t)m->ram[N8_FONT + 3];
        y += (int8_t)m->ram[N8_FONT + 4];
    } else {
        uint8_t r5[5];
        w = n8_glyph(c, r5);
        memcpy(rows, r5, 5);
        adv = c >= 128 ? 8 : 4;
        if (!w)
            return 0;
    }
    if (p->cw && !oneoff)
        adv = p->cw;
    int sw = p->wide ? 2 : 1, sh = p->tall ? 2 : 1;
    int fg = p->d.pal[p->fg & 15];
    if (p->bg >= 0 || p->solid) {
        int bg = p->bg >= 0 ? p->d.pal[p->bg & 15] : 0;
        for (int j = -1; j < cellh * sh - 1; j++)
            for (int i = -1; i < adv * sw - 1; i++)
                if (x + i >= p->d.x0 && x + i < p->d.x1 && y + j >= p->d.y0 && y + j < p->d.y1)
                    wr(&p->d, x + i, y + j, bg);
    }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int on = rows[j] >> i & 1;
            if (on != !p->invert)
                continue;
            glyph_px(p, x + i * sw, y + j * sh, fg);
        }
    if (p->invert)                          /* the gap between cells too */
        for (int j = 0; j < h; j++)
            glyph_px(p, x + w * sw, y + j * sh, fg);
    return adv * sw;
}

static void scroll_up(n8_t *m, int rows)
{
    uint8_t *s = m->ram + region(m->ram[0x5f55], 0x6000);
    if (rows >= 128) {
        memset(s, 0, 0x2000);
        return;
    }
    memmove(s, s + rows * 64, (size_t)(128 - rows) * 64);
    memset(s + (128 - rows) * 64, 0, (size_t)rows * 64);
}

int n8_print(n8_t *m, const uint8_t *s, size_t len, int x, int y, int c, int at_cursor)
{
    pen_t p;
    memset(&p, 0, sizeof p);
    p.m = m;
    if (at_cursor) {
        x = m->ram[0x5f26];
        y = m->ram[0x5f27];
    }
    p.fg = n8_color(m, c) & 15;
    dc_init(m, &p.d, p.fg);
    p.d.pat = 0;
    p.bg = -1;
    p.tab = 16;
    uint8_t attr = m->ram[0x5f58];
    if (attr & 1) {
        p.wide = attr >> 2 & 1;
        p.tall = attr >> 3 & 1;
        p.solid = attr >> 4 & 1;
        p.invert = attr >> 5 & 1;
        p.stripey = attr >> 6 & 1;
        p.custom = attr >> 7 & 1;
    }
    int home_x = x, cx = x, cy = y, maxx = x, line_h = 6, ended = 0;
    int camx = p.d.cx, camy = p.d.cy;
    for (size_t i = 0; i < len && !ended; i++) {
        int ch = s[i];
        int nxt = i + 1 < len ? s[i + 1] : 0;
        int lh = (p.ch ? p.ch : p.custom ? m->ram[N8_FONT + 2] : 6) * (p.tall ? 2 : 1);
        if (lh > line_h) line_h = lh;
        switch (ch) {
        case 0:
            ended = 1;
            continue;
        case 1: {                       /* \*n c: repeat */
            if (i + 2 >= len) { i = len; continue; }
            int n = pnum(s[i + 1]);
            int g = s[i + 2];
            i += 2;
            for (int k = 0; k < n; k++)
                cx += draw_char(&p, g, cx - camx, cy - camy, NULL);
            if (cx > maxx) maxx = cx;
            continue;
        }
        case 2:                         /* \#c: background */
            p.bg = pnum(nxt);
            i++;
            continue;
        case 3:                         /* \-n: x offset */
            cx += pnum(nxt) - 16;
            i++;
            continue;
        case 4:                         /* \|n: y offset */
            cy += pnum(nxt) - 16;
            i++;
            continue;
        case 5:                         /* \+xy */
            if (i + 2 < len) {
                cx += pnum(s[i + 1]) - 16;
                cy += pnum(s[i + 2]) - 16;
            }
            i += 2;
            continue;
        case 6: {                       /* \^ commands */
            int cmd = nxt;
            i++;
            if (cmd >= '1' && cmd <= '9')
                continue;               /* waits: not here */
            int arg = i + 1 < len ? s[i + 1] : 0;
            switch (cmd) {
            case 'c': n8_cls(m, pnum(arg)); cx = home_x = 0; cy = 0; i++; break;
            case 'd': i++; break;
            case 'g': cx = home_x; cy = p.m->print_home_y; break;
            case 'h': home_x = cx; p.m->print_home_y = cy; break;
            case 'j':
                if (i + 2 < len) {
                    cx = home_x = pnum(s[i + 1]) * 4;
                    cy = pnum(s[i + 2]) * 4;
                }
                i += 2;
                break;
            case 'r': p.rhs = pnum(arg) * 4; i++; break;
            case 's': p.tab = pnum(arg) * 4; if (!p.tab) p.tab = 16; i++; break;
            case 'x': p.cw = pnum(arg); i++; break;
            case 'y': p.ch = pnum(arg); i++; break;
            case 'w': p.wide = 1; break;
            case 't': p.tall = 1; break;
            case '=': p.stripey = 1; break;
            case 'p': p.wide = p.tall = p.stripey = 1; break;
            case 'i': p.invert = 1; break;
            case 'b': break;
            case '#': p.solid = 1; break;
            case '-':
                switch (arg) {
                case 'w': p.wide = 0; break;
                case 't': p.tall = 0; break;
                case '=': p.stripey = 0; break;
                case 'p': p.wide = p.tall = p.stripey = 0; break;
                case 'i': p.invert = 0; break;
                case '#': p.solid = 0; break;
                }
                i++;
                break;
            case '.':                   /* one-off glyph: 8 raw bytes */
                if (i + 8 < len) {
                    cx += draw_char(&p, 0, cx - camx, cy - camy, s + i + 1);
                    if (cx > maxx) maxx = cx;
                }
                i += 8;
                break;
            case ':': {                 /* one-off glyph: 16 hex digits */
                uint8_t g[8] = { 0 };
                for (int k = 0; k < 8 && i + 2 + 2 * k < len; k++)
                    g[k] = (uint8_t)(digit(s[i + 1 + 2 * k], 16) << 4 | digit(s[i + 2 + 2 * k], 16));
                cx += draw_char(&p, 0, cx - camx, cy - camy, g);
                if (cx > maxx) maxx = cx;
                i += 16;
                break;
            }
            case 'o': i += 3; break;    /* outline: colour and two digits */
            case '@': case '!': i = len; break;     /* pokes: stop here */
            }
            continue;
        }
        case 7:                         /* \a: a sound; skipped */
            while (i + 1 < len && s[i + 1] > ' ')
                i++;
            continue;
        case 8:
            cx -= 4 * (p.wide ? 2 : 1);
            continue;
        case 9:
            cx = home_x + ((cx - home_x) / p.tab + 1) * p.tab;
            continue;
        case 10:
            cx = home_x;
            cy += line_h;
            line_h = 6;
            continue;
        case 11:                        /* \v: decoration: offset and character */
            if (i + 2 < len) {
                int o = pnum(s[i + 1]);
                draw_char(&p, s[i + 2], cx - 4 + (o % 4) - 2 - camx, cy + (o / 4) - 8 - camy, NULL);
            }
            i += 2;
            continue;
        case 12:                        /* \fc: colour */
            p.fg = pnum(nxt);
            i++;
            continue;
        case 13:
            cx = home_x;
            continue;
        case 14:
            p.custom = 1;
            continue;
        case 15:
            p.custom = 0;
            continue;
        }
        if (p.rhs && cx + 4 > p.rhs) {
            cx = home_x;
            cy += line_h;
        }
        if (at_cursor && cy + 6 - camy > 128) {
            int d = cy + 6 - camy - 128;
            scroll_up(m, d);
            cy -= d;
        }
        cx += draw_char(&p, ch, cx - camx, cy - camy, NULL);
        if (cx > maxx)
            maxx = cx;
    }
    if (ended) {
        m->ram[0x5f26] = (uint8_t)cx;
        m->ram[0x5f27] = (uint8_t)cy;
    } else {
        int ny = cy + line_h;
        if (at_cursor && ny + 6 - camy > 128) {
            int d = ny + 6 - camy - 128;
            scroll_up(m, d);
            ny -= d;
        }
        m->ram[0x5f26] = (uint8_t)home_x;
        m->ram[0x5f27] = (uint8_t)(ny < 0 ? 0 : ny > 255 ? 255 : ny);
    }
    return maxx;
}

/* ------------------------------------------------------------ buttons */

void n8_buttons(n8_t *m, const uint8_t bits[8])
{
    for (int p = 0; p < 8; p++) {
        m->btn_prev[p] = m->btn[p];
        m->btn[p] = bits[p];
        for (int b = 0; b < 8; b++)
            m->btn_frames[p][b] = (bits[p] >> b & 1) ? (uint16_t)(m->btn_frames[p][b] + (m->btn_frames[p][b] < 60000)) : 0;
        if (p < 8)
            m->ram[0x5f4c + p] = bits[p];
    }
}

int n8_btn(const n8_t *m, int b, int p)
{
    if (p < 0 || p > 7 || b < 0 || b > 7)
        return 0;
    return m->btn[p] >> b & 1;
}

int n8_btnp(const n8_t *m, int b, int p)
{
    if (p < 0 || p > 7 || b < 0 || b > 7)
        return 0;
    int f = m->btn_frames[p][b];
    if (f == 1)
        return 1;
    int delay = m->ram[0x5f5c] ? m->ram[0x5f5c] : 15, every = m->ram[0x5f5d] ? m->ram[0x5f5d] : 4;
    if (m->ram[0x5f5c] == 255)
        return 0;
    if (m->fps == 60) {
        delay *= 2;
        every *= 2;
    }
    return f > delay && (f - delay - 1) % every == 0;
}

/* ------------------------------------------------------------ display */

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((c >> 19 & 0x1F) << 11 | (c >> 10 & 0x3F) << 5 | (c >> 3 & 0x1F));
}

void n8_blit(const n8_t *m, uint16_t *dst, uint32_t stride, int dw, int dh)
{
    static uint16_t disp[128 * 128];
    static uint16_t line[1024];
    static uint8_t xmap[1024];
    uint16_t pal[2][16];
    for (int i = 0; i < 16; i++) {
        pal[0][i] = rgb565(n8_rgb(m->ram[N8_PAL_SCREEN + i]));
        pal[1][i] = rgb565(n8_rgb(m->ram[0x5f60 + i]));
    }
    const uint8_t *scr = m->ram + N8_SCREEN;
    int scan = m->ram[0x5f5f] == 0x10;
    int mode = m->ram[N8_SCREEN_MODE];
    for (int y = 0; y < 128; y++) {
        const uint16_t *pl = pal[scan && (m->ram[0x5f70 + (y >> 3)] >> (y & 7) & 1)];
        uint16_t *o = disp + y * 128;
        if (!mode) {
            const uint8_t *r = scr + y * 64;
            for (int x = 0; x < 64; x++) {
                o[2 * x] = pl[r[x] & 15];
                o[2 * x + 1] = pl[r[x] >> 4];
            }
            continue;
        }
        for (int x = 0; x < 128; x++) {
            int sx = x, sy = y;
            switch (mode) {
            case 1: sx = x / 2; break;
            case 2: sy = y / 2; break;
            case 3: sx = x / 2; sy = y / 2; break;
            case 5: sx = x < 64 ? x : 127 - x; break;
            case 6: sy = y < 64 ? y : 127 - y; break;
            case 7: sx = x < 64 ? x : 127 - x; sy = y < 64 ? y : 127 - y; break;
            case 129: sx = 127 - x; break;
            case 130: sy = 127 - y; break;
            case 131: sx = 127 - x; sy = 127 - y; break;
            case 133: sx = y; sy = 127 - x; break;
            case 134: sx = 127 - x; sy = 127 - y; break;
            case 135: sx = 127 - y; sy = x; break;
            }
            o[x] = pl[rd(scr, sx, sy)];
        }
    }
    if (dw > 1024) dw = 1024;
    for (int x = 0; x < dw; x++)
        xmap[x] = (uint8_t)(x * 128 / dw);
    int prev = -1;
    for (int y = 0; y < dh; y++) {
        int sy = y * 128 / dh;
        if (sy != prev) {
            const uint16_t *src = disp + sy * 128;
            for (int x = 0; x < dw; x++)
                line[x] = src[xmap[x]];
            prev = sy;
        }
        uint16_t *o = dst + (uint32_t)y * stride;
        /* writes only, 32 bits at a time where aligned: the framebuffer is
         * uncached */
        int x = 0;
        if (((uintptr_t)o & 2) && dw) {
            o[0] = line[0];
            x = 1;
        }
        for (; x + 1 < dw; x += 2)
            *(uint32_t *)(o + x) = (uint32_t)line[x] | (uint32_t)line[x + 1] << 16;
        if (x < dw)
            o[x] = line[x];
    }
}
