/* REFERENCE COPY of src/bm/snes_ppu.c before the speed work (2026-11-09). tests/snes/ppu_equiv.c checks that the fast PPU
 * makes the same picture. Do not edit; build with -Dppu_line=ppu_line_ref and the other renames (see the Makefile). */
/*
 * snes_ppu.c: the picture of the Super Nintendo, a line at a time from the state of the registers at that
 * line (so a change in the middle of the frame, an HDMA effect, shows). BG modes 0 to 4 and 7, the sprites
 * (all sizes, flips, the 32 per line), windows, colour math with the fixed colour or the sub screen, and the
 * brightness. Not done: modes 5 and 6 (hires), mosaic, offset-per-tile, direct colour, interlace.
 *
 * The cost matters (the Pi Zero W): a layer that is on neither screen is not drawn, a clear row of a tile writes
 * nothing, the screens are made by painting the layers from the back to the front, and a line with no colour
 * math is a palette lookup. Original code.
 */
#include "snes_int.h"

#include <string.h>

uint32_t ppu_us[5];                     /* the time of the parts of ppu_line (layers, sprites, windows, compose, mix) */
static int pal_dirty = 1;
#define T0 uint32_t t_ = snes_now_us ? snes_now_us() : 0
#define TLAP(k) do { if (snes_now_us) { uint32_t n_ = snes_now_us(); ppu_us[k] += n_ - t_; t_ = n_; } } while (0)

void ppu_reset(void)
{
    memset(S.pal, 0, sizeof S.pal);
    memset(S.picture, 0, sizeof S.picture);
    pal_dirty = 1;
}

void ppu_cgram_write(int i)
{
    pal_dirty = 1;
    S.pal[i & 255] = (uint16_t)((S.cgram[(i & 255) * 2] | (S.cgram[(i & 255) * 2 + 1] << 8)) & 0x7FFF);
}

/* the layer buffers of a line: the colour index (0 = clear) and the priority */
static uint8_t bgi[4][256], bgp[4][256], obi[256], obp[256], obmath[256];
static uint8_t winm[6][256];            /* 1 = the layer is hidden at this x (BG1..BG4, OBJ, colour) */
static uint8_t win_on[6];               /* the layer has a window */

/* ------------------------------------------------------------------ backgrounds */
static void draw_bg(int n, int y, int bpp, int pal_base_mode0)
{
    uint8_t *oi = bgi[n], *op = bgp[n];
    memset(oi, 0, 256); memset(op, 0, 256);
    if (!((S.tm | S.ts) & (1 << n))) return;            /* the layer is on neither screen: nothing to draw */
    const int big = (S.bgmode >> (4 + n)) & 1, tsz = big ? 16 : 8;
    const int sc = S.sc[n], wide = sc & 1, tall = sc & 2;
    const uint32_t map_base = (uint32_t)(sc & 0xFC) << 9;       /* bytes */
    const uint32_t chr_base = S.nba[n];
    const int bytes_tile = bpp * 8;
    const int sy = (y + S.vofs[n]) & 0x3FF;
    const int ty = sy / tsz;
    const unsigned pal_off = (bpp == 2 && pal_base_mode0) ? (unsigned)n * 32 : 0;
    int x = 0, sx = S.hofs[n] & 0x3FF;
    while (x < 256) {                                   /* one column of 8 pixels of a tile per turn */
        const int px0 = sx & 7;
        int cnt = 8 - px0;
        if (x + cnt > 256) cnt = 256 - x;
        const int tcol = sx / tsz;
        int screen = 0;
        if (wide) screen += (tcol >> 5) & 1;
        if (tall) screen += ((ty >> 5) & 1) * (wide ? 2 : 1);
        const uint32_t ea = (map_base + (uint32_t)screen * 0x800 + (uint32_t)(((ty & 31) * 32 + (tcol & 31)) * 2)) & 0xFFFF;
        const unsigned e = S.vram[ea] | (S.vram[ea + 1] << 8);
        unsigned tile = e & 0x3FF;
        const int pal = (e >> 10) & 7, pri = (e >> 13) & 1, hflip = (e >> 14) & 1, vflip = (e >> 15) & 1;
        int py = sy & (tsz - 1);
        if (big) {
            int sxm = sx & 15;
            if (hflip) sxm = 15 - sxm;
            int row = py >> 3;
            if (vflip) row = 1 - row;
            tile = (tile + (sxm >> 3) + row * 16) & 0x3FF;
            py &= 7;
        }
        if (vflip) py = 7 - (py & 7);
        const uint32_t ca = (chr_base + tile * (uint32_t)bytes_tile + (uint32_t)((py & 7) * 2)) & 0xFFFF;
        const unsigned b0 = S.vram[ca], b1 = S.vram[(ca + 1) & 0xFFFF];
        unsigned b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
        if (bpp >= 4) { b2 = S.vram[(ca + 16) & 0xFFFF]; b3 = S.vram[(ca + 17) & 0xFFFF]; }
        if (bpp == 8) {
            b4 = S.vram[(ca + 32) & 0xFFFF]; b5 = S.vram[(ca + 33) & 0xFFFF];
            b6 = S.vram[(ca + 48) & 0xFFFF]; b7 = S.vram[(ca + 49) & 0xFFFF];
        }
        if (b0 | b1 | b2 | b3 | b4 | b5 | b6 | b7) {    /* a clear row of a tile writes nothing */
            for (int i = 0; i < cnt; i++) {
                const int sh = hflip ? px0 + i : 7 - (px0 + i);
                unsigned v = ((b0 >> sh) & 1) | (((b1 >> sh) & 1) << 1);
                if (bpp >= 4) v |= (((b2 >> sh) & 1) << 2) | (((b3 >> sh) & 1) << 3);
                if (bpp == 8) v |= (((b4 >> sh) & 1) << 4) | (((b5 >> sh) & 1) << 5) | (((b6 >> sh) & 1) << 6) | (((b7 >> sh) & 1) << 7);
                if (!v) continue;
                oi[x + i] = (uint8_t)(bpp == 2 ? pal_off + (unsigned)pal * 4 + v : bpp == 4 ? (unsigned)pal * 16 + v : v);
                op[x + i] = (uint8_t)pri;
            }
        }
        x += cnt;
        sx = (sx + cnt) & 0x3FF;
    }
}

static void draw_mode7(int y)
{
    uint8_t *oi = bgi[0], *op = bgp[0];
    memset(oi, 0, 256); memset(op, 0, 256);
    const int yy = (S.m7sel & 2) ? 255 - y : y;
    const int vx = S.m7hofs - S.m7x, vy = S.m7vofs - S.m7y;
    const int cx = (vx & 0x2000) ? (vx | ~0x3FF) : (vx & 0x3FF);
    const int cy = (vy & 0x2000) ? (vy | ~0x3FF) : (vy & 0x3FF);
    int32_t X = ((S.m7a * cx) & ~63) + ((S.m7b * cy) & ~63) + ((S.m7b * yy) & ~63) + (S.m7x << 8);
    int32_t Y = ((S.m7c * cx) & ~63) + ((S.m7d * cy) & ~63) + ((S.m7d * yy) & ~63) + (S.m7y << 8);
    const int rep = S.m7sel >> 6;
    for (int x = 0; x < 256; x++) {
        const int xx = (S.m7sel & 1) ? 255 - x : x;
        int px = (X + S.m7a * xx) >> 8, py = (Y + S.m7c * xx) >> 8;
        int outside = (px | py) & ~1023;
        unsigned c;
        if (outside && rep == 2) continue;
        if (outside && rep == 3) c = S.vram[(((py & 7) * 8 + (px & 7)) << 1) + 1];
        else {
            const unsigned t = S.vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) << 1];
            c = S.vram[((t * 64 + (unsigned)(py & 7) * 8 + (unsigned)(px & 7)) << 1) + 1];
        }
        oi[x] = (uint8_t)c;
    }
}

/* ------------------------------------------------------------------ sprites */
static const uint8_t obj_w[8][2] = { {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {16,32}, {16,32} };
static const uint8_t obj_h[8][2] = { {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {32,64}, {32,32} };

static void draw_obj(int y)
{
    memset(obi, 0, 256); memset(obp, 0, 256); memset(obmath, 0, 256);
    if (!((S.tm | S.ts) & 16)) return;
    const int sz = S.obsel >> 5;
    const uint32_t base0 = (uint32_t)(S.obsel & 7) << 14;
    const uint32_t base1 = base0 + ((uint32_t)(((S.obsel >> 3) & 3) + 1) << 13);
    int count = 0;
    const int first = S.oamrot ? (S.oamaddr_reload >> 2) & 0x7F : 0;       /* $2103 bit 7: the sprite at the OAM address is the first in priority, the others follow in order */
    for (int n = 0; n < 128 && count < 32; n++) {
        const int i = (first + n) & 127;
        const uint8_t *o = &S.oam[i * 4];
        const unsigned hi = (S.oam[512 + (i >> 2)] >> ((i & 3) * 2)) & 3;
        const int big = (hi >> 1) & 1;
        const int w = obj_w[sz][big], h = obj_h[sz][big];
        const int dy = (y - (o[1] + 1)) & 0xFF;
        if (dy >= h) continue;
        count++;
        int x0 = o[0] | ((hi & 1) << 8);
        if (x0 >= 256) x0 -= 512;
        if (x0 + w <= 0) continue;
        const unsigned attr = o[3], tile0 = o[2];
        const int hflip = (attr >> 6) & 1, vflip = (attr >> 7) & 1, pri = (attr >> 4) & 3, pal = (attr >> 1) & 7;
        const uint32_t base = (attr & 1) ? base1 : base0;
        int ry = vflip ? h - 1 - dy : dy;
        const int trow = ry >> 3, py = ry & 7;
        for (int cx = 0; cx < w; cx++) {
            const int x = x0 + cx;
            if (x < 0 || x >= 256 || obi[x]) continue;
            int sxp = hflip ? w - 1 - cx : cx;
            const int tcol = sxp >> 3, px = sxp & 7;
            const unsigned tile = (tile0 & 0xF0) + (unsigned)(trow << 4) + (((tile0 & 0x0F) + (unsigned)tcol) & 0x0F);
            const uint32_t ca = (base + ((tile & 0xFF) * 32u) + (uint32_t)(py * 2)) & 0xFFFF;
            const int sh = 7 - px;
            unsigned v = ((S.vram[ca] >> sh) & 1) | (((S.vram[(ca + 1) & 0xFFFF] >> sh) & 1) << 1)
                       | (((S.vram[(ca + 16) & 0xFFFF] >> sh) & 1) << 2) | (((S.vram[(ca + 17) & 0xFFFF] >> sh) & 1) << 3);
            if (!v) continue;
            obi[x] = (uint8_t)(128 + pal * 16 + v);
            obp[x] = (uint8_t)pri;
            obmath[x] = pal >= 4;
        }
    }
}

/* ------------------------------------------------------------------ windows */
static void window_mask(int layer, int en1, int inv1, int en2, int inv2, int log)
{
    uint8_t *m = winm[layer];
    win_on[layer] = (uint8_t)(en1 || en2);
    if (!en1 && !en2) return;
    for (int x = 0; x < 256; x++) {
        int w1 = (x >= S.wh0 && x <= S.wh1) ^ inv1;
        int w2 = (x >= S.wh2 && x <= S.wh3) ^ inv2;
        int r;
        if (en1 && !en2) r = w1;
        else if (!en1 && en2) r = w2;
        else switch (log) { case 0: r = w1 | w2; break; case 1: r = w1 & w2; break; case 2: r = w1 ^ w2; break; default: r = !(w1 ^ w2); break; }
        m[x] = (uint8_t)r;
    }
}

static void build_windows(void)
{
    const uint8_t s12 = S.w12sel, s34 = S.w34sel, so = S.wobjsel;
    window_mask(0, s12 >> 1 & 1, s12 & 1, s12 >> 3 & 1, s12 >> 2 & 1, S.wbglog & 3);
    window_mask(1, s12 >> 5 & 1, s12 >> 4 & 1, s12 >> 7 & 1, s12 >> 6 & 1, S.wbglog >> 2 & 3);
    window_mask(2, s34 >> 1 & 1, s34 & 1, s34 >> 3 & 1, s34 >> 2 & 1, S.wbglog >> 4 & 3);
    window_mask(3, s34 >> 5 & 1, s34 >> 4 & 1, s34 >> 7 & 1, s34 >> 6 & 1, S.wbglog >> 6 & 3);
    window_mask(4, so >> 1 & 1, so & 1, so >> 3 & 1, so >> 2 & 1, S.wobjlog & 3);
    window_mask(5, so >> 5 & 1, so >> 4 & 1, so >> 7 & 1, so >> 6 & 1, S.wobjlog >> 2 & 3);
}

/* ------------------------------------------------------------------ the order of the layers, front to back */
enum { B1 = 0, B2, B3, B4, OB };
typedef struct { uint8_t layer, prio; } slot_t;
static const slot_t ord0[] = { {OB,3}, {B1,1}, {B2,1}, {OB,2}, {B1,0}, {B2,0}, {OB,1}, {B3,1}, {B4,1}, {OB,0}, {B3,0}, {B4,0} };
static const slot_t ord1[] = { {OB,3}, {B1,1}, {B2,1}, {OB,2}, {B1,0}, {B2,0}, {OB,1}, {B3,1}, {OB,0}, {B3,0} };
static const slot_t ord1b[] = { {B3,1}, {OB,3}, {B1,1}, {B2,1}, {OB,2}, {B1,0}, {B2,0}, {OB,1}, {OB,0}, {B3,0} };
static const slot_t ord2[] = { {OB,3}, {B1,1}, {OB,2}, {B2,1}, {OB,1}, {B1,0}, {OB,0}, {B2,0} };
static const slot_t ord6[] = { {OB,3}, {B1,1}, {OB,2}, {OB,1}, {B1,0}, {OB,0} };
static const slot_t ord7[] = { {OB,3}, {OB,2}, {OB,1}, {B1,0}, {OB,0} };

/* the layers of a screen that are on, in order */
typedef struct { uint8_t layer, prio, win; } fslot_t;
static int build_list(const slot_t *o, int n, int en, int wm, fslot_t *out)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        const int l = o[i].layer;
        if (!(en & (1 << l))) continue;
        out[k].layer = o[i].layer; out[k].prio = o[i].prio;
        out[k].win = (uint8_t)(((wm >> l) & 1) && win_on[l]);
        k++;
    }
    return k;
}

/* a screen as three arrays: the colour index (0 = backdrop), the layer (5 = backdrop: the bit of $2131 that stands for it) and whether a sprite allows
 * maths. The layers are painted from the back to the front, so a later one covers an earlier one. */
static uint8_t m_idx[256], m_lay[256], m_om[256], s_idx[256], s_lay[256];
static void compose(const fslot_t *o, int n, uint8_t *idx, uint8_t *lay, uint8_t *om)
{
    memset(idx, 0, 256); memset(lay, 5, 256);
    if (om) memset(om, 1, 256);
    for (int i = n - 1; i >= 0; i--) {
        const int l = o[i].layer, pr = o[i].prio;
        const uint8_t *win = o[i].win ? winm[l] : NULL;
        if (l == OB) {
            for (int x = 0; x < 256; x++) {
                const uint8_t c = obi[x];
                if (c && obp[x] == pr && !(win && win[x])) { idx[x] = c; lay[x] = OB; if (om) om[x] = obmath[x]; }
            }
        } else {
            const uint8_t *ci = bgi[l], *cp = bgp[l];
            for (int x = 0; x < 256; x++) {
                const uint8_t c = ci[x];
                if (c && cp[x] == pr && !(win && win[x])) { idx[x] = c; lay[x] = (uint8_t)l; if (om) om[x] = 1; }
            }
        }
    }
}

static inline unsigned rgb565(unsigned r, unsigned g, unsigned b) { return (r << 11) | (((g << 1) | (g >> 4)) << 5) | b; }

void ppu_line(int y)
{
    uint16_t *dst = &S.picture[y * SNES_W];
    if (S.inidisp & 0x80) { memset(dst, 0, SNES_W * 2); return; }
    T0;
    const int mode = S.bgmode & 7;
    const slot_t *ord;
    int nord, nbg_mask = 0;
    switch (mode) {
    case 0: ord = ord0; nord = 12; draw_bg(0, y, 2, 1); draw_bg(1, y, 2, 1); draw_bg(2, y, 2, 1); draw_bg(3, y, 2, 1); nbg_mask = 15; break;
    case 1: ord = (S.bgmode & 8) ? ord1b : ord1; nord = 10; draw_bg(0, y, 4, 0); draw_bg(1, y, 4, 0); draw_bg(2, y, 2, 0); nbg_mask = 7; break;
    case 2: ord = ord2; nord = 8; draw_bg(0, y, 4, 0); draw_bg(1, y, 4, 0); nbg_mask = 3; break;
    case 3: ord = ord2; nord = 8; draw_bg(0, y, 8, 0); draw_bg(1, y, 4, 0); nbg_mask = 3; break;
    case 4: ord = ord2; nord = 8; draw_bg(0, y, 8, 0); draw_bg(1, y, 2, 0); nbg_mask = 3; break;
    case 7: ord = ord7; nord = 5; draw_mode7(y); nbg_mask = 1; break;
    default: ord = ord6; nord = 6; draw_bg(0, y, 4, 0); nbg_mask = 1; break;
    }
    TLAP(0);
    draw_obj(y);
    TLAP(1);
    build_windows();
    TLAP(2);
    const int main_en = (S.tm & (nbg_mask | 16)), sub_en = (S.ts & (nbg_mask | 16));
    fslot_t mlist[12], slist[12];
    const int nm = build_list(ord, nord, main_en, S.tmw, mlist);
    const int ns = build_list(ord, nord, sub_en, S.tsw, slist);
    const int math_on = (S.cgadsub & 0x3F) != 0;
    const int bright = S.inidisp & 15;
    const int clipmode = S.cgwsel >> 6, premode = (S.cgwsel >> 4) & 3, use_sub = S.cgwsel & 2;
    const int add = !(S.cgadsub & 0x80), half = (S.cgadsub & 0x40) != 0;
    compose(mlist, nm, m_idx, m_lay, m_om);
    TLAP(3);

    /* the palette as RGB565 with the brightness, kept until the palette or the brightness changes */
    static uint16_t lut[256];
    static int lut_bright = -1;
    if (pal_dirty || lut_bright != bright) {
        for (int k = 0; k < 256; k++) {
            unsigned c = S.pal[k], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            if (bright != 15) { r = r * (unsigned)bright / 15; g = g * (unsigned)bright / 15; b = b * (unsigned)bright / 15; }
            lut[k] = (uint16_t)rgb565(r, g, b);
        }
        lut_bright = bright; pal_dirty = 0;
    }
    if (!math_on && !clipmode) {                        /* the common line: a palette lookup */
        for (int x = 0; x < SNES_W; x++) dst[x] = lut[m_idx[x]];
        TLAP(4);
        return;
    }
    if (use_sub) compose(slist, ns, s_idx, s_lay, NULL);
    for (int x = 0; x < SNES_W; x++) {
        const int idx = m_idx[x], lay = m_lay[x], om = m_om[x];
        unsigned c = S.pal[idx];                         /* index 0: the backdrop, colour 0 */
        unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        const int cw = win_on[5] ? winm[5][x] : 0;
        int clip = clipmode == 3 || (clipmode == 1 && !cw) || (clipmode == 2 && cw);
        if (clip) r = g = b = 0;
        int prevent = premode == 3 || (premode == 1 && !cw) || (premode == 2 && cw);
        if (math_on && !prevent && (S.cgadsub & (1 << lay)) && om) {
            unsigned sr = S.fixr, sg = S.fixg, sb = S.fixb;
            if (use_sub && s_lay[x] != 5) { unsigned sc = S.pal[s_idx[x]]; sr = sc & 31; sg = (sc >> 5) & 31; sb = (sc >> 10) & 31; }
            int rr, gg, bb;
            if (add) { rr = (int)(r + sr); gg = (int)(g + sg); bb = (int)(b + sb); }
            else { rr = (int)r - (int)sr; gg = (int)g - (int)sg; bb = (int)b - (int)sb; }
            if (half && !clip) { rr >>= 1; gg >>= 1; bb >>= 1; }
            r = (unsigned)(rr < 0 ? 0 : rr > 31 ? 31 : rr);
            g = (unsigned)(gg < 0 ? 0 : gg > 31 ? 31 : gg);
            b = (unsigned)(bb < 0 ? 0 : bb > 31 ? 31 : bb);
        }
        if (bright != 15) { r = r * (unsigned)bright / 15; g = g * (unsigned)bright / 15; b = b * (unsigned)bright / 15; }
        dst[x] = (uint16_t)rgb565(r, g, b);
    }
    TLAP(4);
}
