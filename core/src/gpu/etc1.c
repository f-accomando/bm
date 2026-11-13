/*
 * ETC1 blocks (bm3d 7.3): a fast encoder for the opaque sheets of
 * gpu3d_etc1 and the decoder of the V3D emulator. The format is Khronos'
 * OES_compressed_ETC1_RGB8_texture: two halves of 2x4 (flip 0) or 4x2
 * (flip 1) texels, each a base colour (4 bits a channel, or 5 bits and the
 * second half as a difference of -4..3) and a table of light modifiers;
 * each texel a 2-bit index into its half's table.
 */
#include "gpu/etc1.h"

/* the modifiers' two sizes: an index gives +small, +large, -small, -large */
static const int mods[8][2] = { { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 },
                                { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 } };

static int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static int modifier(int t, int idx)
{
    const int m = mods[t][idx & 1];
    return idx & 2 ? -m : m;
}

uint32_t etc1_texel(uint64_t b, int x, int y)
{
    const int diff = (int)(b >> 33 & 1), flip = (int)(b >> 32 & 1);
    const int half = flip ? y >= 2 : x >= 2;
    const int t = (int)(b >> (half ? 34 : 37) & 7), i = x * 4 + y;
    const int idx = (int)(b >> (16 + i) & 1) << 1 | (int)(b >> i & 1);
    const int m = modifier(t, idx);
    uint32_t out = 0;
    for (int c = 0; c < 3; c++) {
        int v;
        if (diff) {
            v = (int)(b >> (59 - 8 * c) & 31);
            if (half) {
                int d = (int)(b >> (56 - 8 * c) & 7);
                v += d >= 4 ? d - 8 : d;
            }
            v = v << 3 | v >> 2;
        } else {
            v = (int)(b >> ((half ? 56 : 60) - 8 * c) & 15);
            v = v << 4 | v;
        }
        out |= (uint32_t)clamp255(v + m) << (16 - 8 * c);
    }
    return out;
}

uint64_t etc1_encode(const uint16_t *px, int stride, int swap_rb)
{
    /* a block of one colour (a sheet's flat areas): as the last such one,
     * if the same colour */
    static uint32_t flat_key = ~0u;
    static uint64_t flat_b;
    int flat = 1;
    for (int y = 0; y < 4 && flat; y++)
        for (int x = 0; x < 4; x++)
            flat &= px[y * stride + x] == px[0];
    const uint32_t key = (uint32_t)px[0] | (uint32_t)!!swap_rb << 16;
    if (flat && key == flat_key)
        return flat_b;
    int c[16][3];                       /* [y * 4 + x][r g b], 8 bits */
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            const uint32_t v = px[y * stride + x];
            const int hi = (int)(v >> 11), g = (int)(v >> 5 & 63), lo = (int)(v & 31);
            int *o = c[y * 4 + x];
            o[swap_rb ? 2 : 0] = hi << 3 | hi >> 2;
            o[1] = g << 2 | g >> 4;
            o[swap_rb ? 0 : 2] = lo << 3 | lo >> 2;
        }
    /* the split whose halves' means hold more of the block (the sum of the
     * squares of the halves' sums: the larger, the smaller the spread) */
    int s[2][2][3] = { { { 0 } } };
    for (int i = 0; i < 16; i++) {
        const int x = i & 3, y = i >> 2;
        for (int k = 0; k < 3; k++) {
            s[0][x >= 2][k] += c[i][k];
            s[1][y >= 2][k] += c[i][k];
        }
    }
    int64_t score[2] = { 0, 0 };
    for (int f = 0; f < 2; f++)
        for (int h = 0; h < 2; h++)
            for (int k = 0; k < 3; k++)
                score[f] += (int64_t)s[f][h][k] * s[f][h][k];
    const int flip = score[1] > score[0];
    /* the halves' base colours: 5 bits and a difference if it fits, else
     * 4 bits each */
    int q[2][3], base[2][3], diff = 1;
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 3; k++) {
            const int avg = (s[flip][h][k] + 4) / 8;
            q[h][k] = (avg * 31 + 127) / 255;
        }
    for (int k = 0; k < 3; k++) {
        const int d = q[1][k] - q[0][k];
        if (d < -4 || d > 3)
            diff = 0;
    }
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 3; k++) {
            if (diff) {
                base[h][k] = q[h][k] << 3 | q[h][k] >> 2;
            } else {
                const int avg = (s[flip][h][k] + 4) / 8;
                q[h][k] = (avg * 15 + 127) / 255;
                base[h][k] = q[h][k] * 17;
            }
        }
    uint64_t b = 0;
    for (int k = 0; k < 3; k++) {
        if (diff)
            b |= (uint64_t)q[0][k] << (59 - 8 * k) | (uint64_t)((q[1][k] - q[0][k]) & 7) << (56 - 8 * k);
        else
            b |= (uint64_t)q[0][k] << (60 - 8 * k) | (uint64_t)q[1][k] << (56 - 8 * k);
    }
    b |= (uint64_t)diff << 33 | (uint64_t)flip << 32;
    /* each half: the table nearest the texels' light against the base (the
     * sum of the three channels' differences), then each texel's index the
     * nearest in colour (clamped as the decoder does) */
    for (int h = 0; h < 2; h++) {
        int texel[8], dev[8], neg[8], n = 0;
        for (int i = 0; i < 16; i++) {
            const int x = i & 3, y = i >> 2;
            if ((flip ? y >= 2 : x >= 2) != h)
                continue;
            texel[n] = i;
            dev[n] = c[i][0] + c[i][1] + c[i][2] - base[h][0] - base[h][1] - base[h][2];
            neg[n] = dev[n] < 0;
            if (dev[n] < 0)
                dev[n] = -dev[n];
            n++;
        }
        int best_t = 0;
        int64_t best_e = -1;
        for (int t = 0; t < 8; t++) {
            const int a = 3 * mods[t][0], l = 3 * mods[t][1];
            int64_t e = 0;
            for (int j = 0; j < 8; j++) {
                const int ea = dev[j] - a, el = dev[j] - l;
                e += ea * ea < el * el ? ea * ea : el * el;
            }
            if (best_e < 0 || e < best_e) {
                best_e = e;
                best_t = t;
            }
        }
        b |= (uint64_t)best_t << (h ? 34 : 37);
        const int split2 = 3 * (mods[best_t][0] + mods[best_t][1]);    /* twice the middle */
        for (int j = 0; j < 8; j++) {
            const int i = texel[j], x = i & 3, y = i >> 2;
            /* the sign of the light, small or large (the nearest of the
             * four in light) */
            const int best = (neg[j] ? 2 : 0) | (dev[j] * 2 > split2);
            const int bit = x * 4 + y;
            b |= (uint64_t)(best >> 1) << (16 + bit) | (uint64_t)(best & 1) << bit;
        }
    }
    if (flat) {
        flat_key = key;
        flat_b = b;
    }
    return b;
}
