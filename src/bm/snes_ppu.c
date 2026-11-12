/*
 * snes_ppu.c: the picture of the Super Nintendo, a line at a time from the state of the registers at that
 * line (so a change in the middle of the frame, an HDMA effect, shows). BG modes 0 to 4 and 7, the sprites
 * (all sizes, flips, the 32 per line), windows, colour math with the fixed colour or the sub screen, and the
 * brightness. Not done: modes 5 and 6 (hires), mosaic, offset-per-tile, direct colour, interlace.
 *
 * The cost matters (the Pi Zero W): a layer that is on neither screen is not drawn, a clear row of a tile writes
 * nothing, the screens are made by painting the layers from the back to the front, and a line with no colour
 * math is a palette lookup. A row of 8 pixels of a tile is made 4 pixels at a time in a 32-bit word (one pixel per
 * byte, from a table of the bits of a byte); the layers are merged 4 pixels at a time too; a line with colour
 * math and no window rule uses a table of the mixed colours. tests/snes/ppu_equiv.c checks every line against the
 * reference copy (tests/snes/ppu_ref.c). Original code.
 */
#include "snes_int.h"

#include <string.h>

snes_t S;
#ifdef SNES_TRACE
snes_trace_t snes_trace;
#endif
uint32_t (*snes_now_us)(void);
uint32_t ppu_us[5], ppu_n[5];                     /* the time of the parts of ppu_line (layers, sprites, windows, compose, mix) */
static uint32_t pal_gen = 1;            /* grows with every palette write: the colour tables are made again */
#define T0 uint32_t t_ = snes_now_us ? snes_now_us() : 0
#define TLAP(k) do { if (snes_now_us) { uint32_t n_ = snes_now_us(); ppu_us[k] += n_ - t_; ppu_n[k]++; t_ = n_; } } while (0)

void ppu_reset(void)
{
    ppu_vram_dirty_all();
    memset(S.pal, 0, sizeof S.pal);
    memset(S.picture, 0, sizeof S.picture);
    pal_gen++;
}

void ppu_cgram_write(int i)
{
    pal_gen++;
    S.pal[i & 255] = (uint16_t)((S.cgram[(i & 255) * 2] | (S.cgram[(i & 255) * 2 + 1] << 8)) & 0x7FFF);
}

/* the layer buffers of a line: the colour index (0 = clear) and the priority. Aligned to 4 bytes: they are read and written a word at a time. */
static _Alignas(4) uint8_t bgi[4][256], bgp[4][256], obi[256], obp[256], obmath[256];
static _Alignas(4) uint8_t tbi[4][264], tbp[4][264];   /* a layer as the tiles make it: a row of 8 pixels at a time, 8 pixels of room for the scroll */
static uint8_t bgany[4], obany;         /* the layer has a pixel on this line */
static _Alignas(4) uint8_t winm[6][256];            /* 1 = the layer is hidden at this x (BG1..BG4, OBJ, colour) */
static uint8_t win_on[6];               /* the layer has a window */

/* The bits of a byte as bytes: tA[b] holds pixels 0..3 (bit 7 first), tB[b] pixels 4..7; tAf and tBf have the bits the other way round (a flipped
 * tile). A plane shifted left by its number and OR-ed with the others is one word of 4 pixels. The shifts stay inside a byte, so the order of the
 * bytes in memory (the pixels) does not depend on the byte order of the CPU. */
static uint32_t tA[256], tB[256], tAf[256], tBf[256];
static int tables_ready;
static void make_tables(void)
{
    for (int b = 0; b < 256; b++) {
        uint8_t n[8], f[8];
        for (int i = 0; i < 8; i++) { n[i] = (uint8_t)((b >> (7 - i)) & 1); f[i] = (uint8_t)((b >> i) & 1); }
        memcpy(&tA[b], n, 4); memcpy(&tB[b], n + 4, 4);
        memcpy(&tAf[b], f, 4); memcpy(&tBf[b], f + 4, 4);
    }
    tables_ready = 1;
}

/* Decoded tiles: every tile of the video RAM as chunky pixels, one byte a pixel, 8 rows of two words (pixels 0 to 3 and 4 to 7), made when the tile is first
 * used and dropped when the video RAM is written there (ppu_vram_dirty, called by the bus). A row of a tile is then two loads; a flipped tile is the
 * two words swapped and byte-reversed. There is one cache for each depth (2, 4 and 8 bits a pixel): a tile address is 16, 32 or 64 bytes apart. */
static uint32_t tc2[4096][16], tc4[2048][16], tc8[1024][16];
static uint8_t tv2[4096], tv4[2048], tv8[1024];
void ppu_vram_dirty(uint16_t a) { tv2[a >> 4] = 0; tv4[a >> 5] = 0; tv8[a >> 6] = 0; }
void ppu_vram_dirty_all(void) { memset(tv2, 0, sizeof tv2); memset(tv4, 0, sizeof tv4); memset(tv8, 0, sizeof tv8); }

static void decode_tile(int bpp, uint32_t a, uint32_t *out)
{
    for (int py = 0; py < 8; py++) {
        const uint32_t ca = (a + (uint32_t)py * 2) & 0xFFFF;
        unsigned b = S.vram[ca];
        uint32_t lo = tA[b], hi = tB[b];
        b = S.vram[(ca + 1) & 0xFFFF]; lo |= tA[b] << 1; hi |= tB[b] << 1;
        if (bpp >= 4) {
            b = S.vram[(ca + 16) & 0xFFFF]; lo |= tA[b] << 2; hi |= tB[b] << 2;
            b = S.vram[(ca + 17) & 0xFFFF]; lo |= tA[b] << 3; hi |= tB[b] << 3;
        }
        if (bpp == 8) {
            b = S.vram[(ca + 32) & 0xFFFF]; lo |= tA[b] << 4; hi |= tB[b] << 4;
            b = S.vram[(ca + 33) & 0xFFFF]; lo |= tA[b] << 5; hi |= tB[b] << 5;
            b = S.vram[(ca + 48) & 0xFFFF]; lo |= tA[b] << 6; hi |= tB[b] << 6;
            b = S.vram[(ca + 49) & 0xFFFF]; lo |= tA[b] << 7; hi |= tB[b] << 7;
        }
        out[py * 2] = lo; out[py * 2 + 1] = hi;
    }
}

/* Four 8-bit lanes in a 32-bit word. On the ARMv6 core of the Pi (the SIMD32 instructions: UADD8, SEL, UQADD8, UQSUB8, UHADD8) one instruction does what
 * the C code below does in several; on any other CPU (the host tests) the C code runs. Both give the same bits (tests/snes/ppu_equiv.c on the PC, the
 * picture on the console). */
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32)
#include <arm_acle.h>
static inline uint32_t lane_uqadd(uint32_t a, uint32_t b) { return __uqadd8(a, b); }
static inline uint32_t lane_uqsub(uint32_t a, uint32_t b) { return __uqsub8(a, b); }
static inline uint32_t lane_uhadd(uint32_t a, uint32_t b) { return __uhadd8(a, b); }
/* the bytes of a word that are not zero, as 0xFF: v + 0xFF carries out of a byte exactly when the byte is not 0; SEL picks 0xFF for those bytes */
static inline uint32_t nz_mask(uint32_t v)
{
    uint32_t r;
    __asm__("uadd8 %0, %1, %2\n\tsel %0, %2, %3" : "=&r"(r) : "r"(v), "r"(0xFFFFFFFFu), "r"(0u) : "cc");
    return r;
}
#else
static inline uint32_t lane_uqadd(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int i = 0; i < 32; i += 8) { uint32_t t = ((a >> i) & 255) + ((b >> i) & 255); r |= (t > 255 ? 255u : t) << i; }
    return r;
}
static inline uint32_t lane_uqsub(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int i = 0; i < 32; i += 8) { uint32_t x = (a >> i) & 255, y = (b >> i) & 255; r |= (x > y ? x - y : 0u) << i; }
    return r;
}
static inline uint32_t lane_uhadd(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int i = 0; i < 32; i += 8) r |= ((((a >> i) & 255) + ((b >> i) & 255)) >> 1) << i;
    return r;
}
static inline uint32_t nz_mask(uint32_t v) { return (((((v & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | v) & 0x80808080u) >> 7) * 0xFFu; }
#endif
/* the bytes of a word that equal the bytes of another, as 0xFF */
static inline uint32_t eq_mask(uint32_t a, uint32_t b) { return ~nz_mask(a ^ b); }
static inline uint32_t ld(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void st(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* ------------------------------------------------------------------ backgrounds */
static void draw_bg(int n, int y, int bpp, int pal_base_mode0)
{
    uint8_t *oi = bgi[n], *op = bgp[n];
    bgany[n] = 0;
    memset(oi, 0, 256); memset(op, 0, 256);
    if (!((S.tm | S.ts) & (1 << n))) return;            /* the layer is on neither screen: nothing to draw */
    if (!tables_ready) make_tables();
    const int big = (S.bgmode >> (4 + n)) & 1, tsh = big ? 4 : 3;        /* tsh: log2 of the tile size (the ARM1176 has no divide) */
    const int sc = S.sc[n], wide = sc & 1, tall = sc & 2;
    const uint32_t chr_base = S.nba[n];
    const int sy = (y + S.vofs[n]) & 0x3FF;
    const int ty = sy >> tsh;
    /* the tile map: the address of the entry of tile column tcol is base_a + (tcol & 31) * 2 + (tcol & 32) * wsh, all modulo 64 KB */
    const uint32_t map_base = (uint32_t)(sc & 0xFC) << 9;
    const uint32_t base_a = map_base + (uint32_t)(tall ? ((ty >> 5) & 1) * (wide ? 2 : 1) * 0x800 : 0) + (uint32_t)((ty & 31) * 64);
    const uint32_t wsh = wide ? 64u : 0u;
    /* the tile data: the depth gives the size of a tile and the cache */
    const int tshift = bpp == 2 ? 4 : bpp == 4 ? 5 : 6;                  /* log2 of the bytes of a tile */
    uint32_t (*tc)[16] = bpp == 2 ? tc2 : bpp == 4 ? tc4 : tc8;
    uint8_t *tv = bpp == 2 ? tv2 : bpp == 4 ? tv4 : tv8;
    const uint32_t pbase = (bpp == 2 && pal_base_mode0) ? (uint32_t)n * 32 : 0;
    const int py0 = sy & (big ? 15 : 7);
    const int hofs = S.hofs[n] & 0x3FF, px0 = hofs & 7;
    int sx = hofs & ~7, any = 0;
    uint8_t *ti = tbi[n], *tp = tbp[n];
    memset(ti, 0, 264); memset(tp, 0, 264);
    for (int k = 0; k < 33; k++, sx = (sx + 8) & 0x3FF) {      /* one tile column of 8 pixels per turn, on the grid of the tiles; the scroll is a shift at the end */
        const int tcol = sx >> tsh;
        const uint32_t ea = (base_a + (uint32_t)(tcol & 31) * 2 + (uint32_t)(tcol & 32) * wsh) & 0xFFFF;
        const unsigned e = S.vram[ea] | (S.vram[ea + 1] << 8);
        unsigned tile = e & 0x3FF;
        const int pal = (e >> 10) & 7, pri = (e >> 13) & 1, hflip = (e >> 14) & 1, vflip = (e >> 15) & 1;
        int py = py0;
        if (big) {
            int sxm = sx & 15;
            if (hflip) sxm = 15 - sxm;
            int row = py >> 3;
            if (vflip) row = 1 - row;
            tile = (tile + (sxm >> 3) + row * 16) & 0x3FF;
            py &= 7;
        }
        if (vflip) py = 7 - (py & 7);
        const uint32_t chunk = ((chr_base + tile * (1u << tshift)) & 0xFFFF) >> tshift;
        if (!tv[chunk]) { decode_tile(bpp, chunk << tshift, tc[chunk]); tv[chunk] = 1; }
        uint32_t lo = tc[chunk][(py & 7) * 2], hi = tc[chunk][(py & 7) * 2 + 1];
        if (!(lo | hi)) continue;                       /* a clear row of a tile writes nothing */
        if (hflip) { const uint32_t t = __builtin_bswap32(hi); hi = __builtin_bswap32(lo); lo = t; }
        any = 1;
        const uint32_t pb = (pbase + (uint32_t)(bpp == 2 ? pal * 4 : bpp == 4 ? pal * 16 : 0)) * 0x01010101u;
        const uint32_t pr = (uint32_t)pri * 0x01010101u;
        const uint32_t ml = nz_mask(lo), mh = nz_mask(hi);
        st(ti + k * 8, (lo + pb) & ml);     st(ti + k * 8 + 4, (hi + pb) & mh);
        st(tp + k * 8, pr & ml);            st(tp + k * 8 + 4, pr & mh);
    }
    if (any) {
        bgany[n] = 1;
        memcpy(oi, ti + px0, 256); memcpy(op, tp + px0, 256);
    }
}

static void draw_mode7(int y)
{
    uint8_t *oi = bgi[0], *op = bgp[0];
    memset(oi, 0, 256); memset(op, 0, 256);
    bgany[0] = 1;
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
    obany = 0;
    if (!((S.tm | S.ts) & 16)) return;
    if (!tables_ready) make_tables();
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
        const uint32_t *ta = hflip ? tAf : tA, *tb = hflip ? tBf : tB;
        const int ntc = w >> 3;
        for (int c8 = 0; c8 < ntc; c8++) {                  /* a row of 8 pixels of the sprite per turn */
            const int xs = x0 + c8 * 8;
            if (xs >= 256 || xs + 8 <= 0) continue;
            const int tcol = hflip ? ntc - 1 - c8 : c8;
            const unsigned tile = (tile0 & 0xF0) + (unsigned)(trow << 4) + (((tile0 & 0x0F) + (unsigned)tcol) & 0x0F);
            const uint32_t ca = (base + ((tile & 0xFF) * 32u) + (uint32_t)(py * 2)) & 0xFFFF;
            unsigned b = S.vram[ca];
            uint32_t lo = ta[b], hv = tb[b];
            b = S.vram[(ca + 1) & 0xFFFF]; lo |= ta[b] << 1; hv |= tb[b] << 1;
            b = S.vram[(ca + 16) & 0xFFFF]; lo |= ta[b] << 2; hv |= tb[b] << 2;
            b = S.vram[(ca + 17) & 0xFFFF]; lo |= ta[b] << 3; hv |= tb[b] << 3;
            if (!(lo | hv)) continue;
            uint8_t v[8];
            memcpy(v, &lo, 4); memcpy(v + 4, &hv, 4);
            for (int k = 0; k < 8; k++) {
                const int x = xs + k;
                if (x < 0 || x >= 256 || obi[x] || !v[k]) continue;
                obi[x] = (uint8_t)(128 + pal * 16 + v[k]);
                obp[x] = (uint8_t)pri;
                obmath[x] = pal >= 4;
                obany = 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ windows */
/* one window as a row of 0 and 1: 1 inside [lo, hi] (nothing when lo > hi), the other way round with inv; made with two or three runs, not a test per pixel */
static void win_row(uint8_t *a, int lo, int hi, int inv)
{
    memset(a, inv, 256);
    if (lo <= hi) memset(a + lo, !inv, (size_t)(hi - lo + 1));
}
static void window_mask(int layer, int en1, int inv1, int en2, int inv2, int log)
{
    uint8_t *m = winm[layer];
    win_on[layer] = (uint8_t)(en1 || en2);
    if (!en1 && !en2) return;
    if (!en2) { win_row(m, S.wh0, S.wh1, inv1); return; }
    if (!en1) { win_row(m, S.wh2, S.wh3, inv2); return; }
    static _Alignas(4) uint8_t w2[256];
    win_row(m, S.wh0, S.wh1, inv1); win_row(w2, S.wh2, S.wh3, inv2);
    const uint32_t flip = log == 3 ? 0x01010101u : 0;                  /* 0 or, 1 and, 2 xor, 3 not xor */
    for (int x = 0; x < 256; x += 4) {
        const uint32_t a = ld(m + x), b = ld(w2 + x);
        st(m + x, (log == 0 ? a | b : log == 1 ? a & b : a ^ b) ^ flip);
    }
}

/* the masks are made again only when a window register changes (an HDMA window effect changes them in every line) */
static void build_windows(void)
{
    static uint8_t key[9];
    static int key_ok;
    const uint8_t k[9] = { S.w12sel, S.w34sel, S.wobjsel, S.wbglog, S.wobjlog, S.wh0, S.wh1, S.wh2, S.wh3 };
    if (key_ok && !memcmp(key, k, sizeof k)) return;
    memcpy(key, k, sizeof k); key_ok = 1;
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
/* the main screen (idx, lay, om) and the sub screen (sidx) of a line, in one block: the ARM loop of the mix (snes_ppu_arm.S) takes one base register.
 * The sub screen has no layer array: sidx 0 is the backdrop (a layer never paints a clear pixel). In the mix with no clip rule, lay holds the maths flag of
 * the pixel (0 or 1: its layer takes maths, the sprite allows it, no prevent rule) in place of the layer number, and om is not used. */
typedef struct { uint8_t idx[256], lay[256], om[256], sidx[256]; } mixbuf_t;
static _Alignas(4) mixbuf_t mbuf;
#define m_idx mbuf.idx
#define m_lay mbuf.lay
#define m_om mbuf.om
#define s_idx mbuf.sidx
/* lmap NULL: lay gets the layer number (5 = backdrop) and om the sprite flag; lmap: lay gets lmap[layer] and a sprite pixel that forbids maths gets 0; lay NULL: no layer array */
static void compose(const fslot_t *o, int n, uint8_t *idx, uint8_t *lay, uint8_t *om, const uint8_t *lmap)
{
    memset(idx, 0, 256);
    if (lay) memset(lay, lmap ? lmap[5] : 5, 256);
    if (om) memset(om, 1, 256);
    for (int i = n - 1; i >= 0; i--) {
        const int l = o[i].layer, pr = o[i].prio;
        if (l == OB ? !obany : !bgany[l]) continue;                    /* the layer has no pixel on this line */
        const uint8_t *win = o[i].win ? winm[l] : NULL;
        const uint8_t *ci = l == OB ? obi : bgi[l], *cp = l == OB ? obp : bgp[l];
        const uint32_t prl = (uint32_t)pr * 0x01010101u, lal = (uint32_t)(lmap ? lmap[l] : l) * 0x01010101u;
        for (int x = 0; x < 256; x += 4) {                              /* 4 pixels a turn */
            const uint32_t c = ld(ci + x);
            if (!c) continue;
            uint32_t m = nz_mask(c) & eq_mask(ld(cp + x), prl);
            if (win) m &= ~nz_mask(ld(win + x));
            if (!m) continue;
            st(idx + x, (ld(idx + x) & ~m) | (c & m));
            if (lay) st(lay + x, (ld(lay + x) & ~m) | ((lmap && l == OB ? lal & ld(obmath + x) : lal) & m));
            if (om) st(om + x, (ld(om + x) & ~m) | ((l == OB ? ld(obmath + x) : 0x01010101u) & m));
        }
    }
}

static inline unsigned rgb565(unsigned r, unsigned g, unsigned b) { return (r << 11) | (((g << 1) | (g >> 4)) << 5) | b; }

/* what the mix loop needs, in the order that the ARM loop loads it (snes_ppu_arm.S) */
typedef struct { const uint32_t *pal8; const uint16_t *lut; const uint32_t *rt, *gt, *bt; uint32_t fix8, mode; } mixctx_t;
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32)
void ppu_mix_sub_arm(uint16_t *dst, const mixbuf_t *b, const mixctx_t *c);
#endif

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
    const unsigned mb = S.cgadsub & 0x3F;
    const int fastmix = math_on && !clipmode;           /* colour maths with no clip rule: the maths flag of a pixel goes in m_lay */
    uint8_t lmap[6];
    for (int l = 0; l < 6; l++) lmap[l] = (uint8_t)((mb >> l) & 1);
    compose(mlist, nm, m_idx, m_lay, m_om, fastmix ? lmap : NULL);
    if (fastmix && premode) {                           /* the colour window prevents the maths: inside, outside, everywhere */
        const uint8_t *cw = winm[5];
        for (int x = 0; x < 256; x += 4) {
            const uint32_t c = win_on[5] ? nz_mask(ld(cw + x)) : 0;
            st(m_lay + x, ld(m_lay + x) & ~(premode == 3 ? 0x01010101u : premode == 1 ? ~c & 0x01010101u : c & 0x01010101u));
        }
    }
    TLAP(3);

    /* the palette as RGB565 with the brightness, and the brightness tables of the channels, kept until the palette or the brightness changes */
    static uint16_t lut[256], rt[32], gt[32], bt[32];
    static uint32_t rt32[32], gt32[32], bt32[32];     /* the same tables as words: the ARM loop (snes_ppu_arm.S); unused on the host */
    (void)rt32; (void)gt32; (void)bt32;
    static uint32_t pal8[256];                          /* the palette as three lanes (red, green, blue) of the colour times 8: the lane maths below */
    static int lut_bright = -1;
    static uint32_t lut_gen;
    if (lut_gen != pal_gen || lut_bright != bright) {
        for (int k = 0; k < 256; k++) { const unsigned c = S.pal[k]; pal8[k] = (c & 31) * 8u | ((c >> 5) & 31) * 8u << 8 | ((c >> 10) & 31) * 8u << 16; }
        for (int k = 0; k < 256; k++) {
            unsigned c = S.pal[k], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            if (bright != 15) { r = r * (unsigned)bright / 15; g = g * (unsigned)bright / 15; b = b * (unsigned)bright / 15; }
            lut[k] = (uint16_t)rgb565(r, g, b);
        }
        for (unsigned c = 0; c < 32; c++) {
            const unsigned v = bright != 15 ? c * (unsigned)bright / 15 : c;
            rt[c] = (uint16_t)rgb565(v, 0, 0); gt[c] = (uint16_t)rgb565(0, v, 0); bt[c] = (uint16_t)rgb565(0, 0, v);
            rt32[c] = rt[c]; gt32[c] = gt[c]; bt32[c] = bt[c];
        }
        lut_bright = bright; lut_gen = pal_gen;
    }
    if (!math_on && !clipmode) {                        /* the common line: a palette lookup */
        for (int x = 0; x < SNES_W; x++) dst[x] = lut[m_idx[x]];
        TLAP(4);
        return;
    }
    if (fastmix) {                                      /* colour maths with no clip rule: the pixels with the flag are mixed, the others are a lookup */
        if (use_sub) compose(slist, ns, s_idx, NULL, NULL, NULL);
        else {                                          /* the fixed colour: the mixed colour of every palette entry, made once */
            static uint16_t mlut[256];
            static uint32_t m_gen, m_key = 0xFFFFFFFFu;
            const uint32_t key = (uint32_t)bright | (uint32_t)S.fixr << 4 | (uint32_t)S.fixg << 9 | (uint32_t)S.fixb << 14 | (uint32_t)(S.cgadsub & 0xC0) << 16;
            if (m_gen != pal_gen || m_key != key) {
                for (int k = 0; k < 256; k++) {
                    const unsigned c = S.pal[k];
                    int rr = (int)(c & 31), gg = (int)((c >> 5) & 31), bb = (int)((c >> 10) & 31);
                    if (add) { rr += S.fixr; gg += S.fixg; bb += S.fixb; } else { rr -= S.fixr; gg -= S.fixg; bb -= S.fixb; }
                    if (half) { rr >>= 1; gg >>= 1; bb >>= 1; }
                    rr = rr < 0 ? 0 : rr > 31 ? 31 : rr; gg = gg < 0 ? 0 : gg > 31 ? 31 : gg; bb = bb < 0 ? 0 : bb > 31 ? 31 : bb;
                    mlut[k] = (uint16_t)(rt[rr] | gt[gg] | bt[bb]);
                }
                m_gen = pal_gen; m_key = key;
            }
            const uint16_t *tab[2] = { lut, mlut };
            for (int x = 0; x < SNES_W; x++) dst[x] = tab[m_lay[x]][m_idx[x]];
            TLAP(4);
            return;
        }
        const uint32_t fix8 = (uint32_t)S.fixr * 8u | (uint32_t)S.fixg * 8u << 8 | (uint32_t)S.fixb * 8u << 16;
        const int mode_k = (add ? 2 : 0) | (half ? 1 : 0);
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32)
        {
            const mixctx_t cx = { pal8, lut, rt32, gt32, bt32, fix8, (uint32_t)mode_k };
            ppu_mix_sub_arm(dst, &mbuf, &cx);
            TLAP(4);
            return;
        }
#endif
        for (int x = 0; x < SNES_W; x++) {              /* the sub screen: the colour behind the pixel; the three channels are one lane operation */
            const int idx = m_idx[x];
            if (!m_lay[x]) { dst[x] = lut[idx]; continue; }
            const uint32_t c = pal8[idx], sc = s_idx[x] ? pal8[s_idx[x]] : fix8;
            uint32_t v;      /* each lane is 8 times the 5-bit value: a sum above 31 saturates at 255 (31 after the shift), a difference below 0 stops at 0, a half is (a + b) >> 1 */
            switch (mode_k) {
            case 3: v = lane_uhadd(c, sc); break;
            case 2: v = lane_uqadd(c, sc); break;
            case 1: v = lane_uhadd(lane_uqsub(c, sc), 0); break;
            default: v = lane_uqsub(c, sc); break;
            }
            dst[x] = (uint16_t)(rt[(v >> 3) & 31] | gt[(v >> 11) & 31] | bt[(v >> 19) & 31]);
        }
        TLAP(4);
        return;
    }
    if (use_sub) compose(slist, ns, s_idx, NULL, NULL, NULL);
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
            if (use_sub && s_idx[x]) { unsigned sc = S.pal[s_idx[x]]; sr = sc & 31; sg = (sc >> 5) & 31; sb = (sc >> 10) & 31; }
            int rr, gg, bb;
            if (add) { rr = (int)(r + sr); gg = (int)(g + sg); bb = (int)(b + sb); }
            else { rr = (int)r - (int)sr; gg = (int)g - (int)sg; bb = (int)b - (int)sb; }
            if (half && !clip) { rr >>= 1; gg >>= 1; bb >>= 1; }
            r = (unsigned)(rr < 0 ? 0 : rr > 31 ? 31 : rr);
            g = (unsigned)(gg < 0 ? 0 : gg > 31 ? 31 : gg);
            b = (unsigned)(bb < 0 ? 0 : bb > 31 ? 31 : bb);
        }
        dst[x] = (uint16_t)(rt[r] | gt[g] | bt[b]);      /* the brightness tables: the same as the formula, with no division */
    }
    TLAP(4);
}
