/*
 * PNG pictures (the .p8.png cartridges of nano8, the textures of .glb
 * files, the pictures on the SD card): inflate (RFC 1951) and the PNG
 * filters, 8 bits a sample, colour types 0, 2, 3, 4 and 6, no interlace.
 * Plain C, kernel and PC.
 */
#include "png.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ inflate */

typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t cap, n;
    int bad;
} inf_t;

typedef struct {
    uint16_t count[16];
    uint16_t symbol[320];
} huff_t;

static int bits(inf_t *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->len) {
            s->bad = 1;
            return 0;
        }
        v |= (uint32_t)s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

static int decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        if (s->bad)
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int build(huff_t *h, const uint8_t *lengths, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    return left;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(inf_t *s, const huff_t *lc, const huff_t *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->n >= s->cap)
                return -1;
            s->out[s->n++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int len = len_base[sym] + bits(s, len_extra[sym]);
            int ds = decode(s, dc);
            if (ds < 0 || ds >= 30)
                return -1;
            size_t dist = dist_base[ds] + (size_t)bits(s, dist_extra[ds]);
            if (s->bad || dist > s->n || s->n + (size_t)len > s->cap)
                return -1;
            for (int i = 0; i < len; i++, s->n++)
                s->out[s->n] = s->out[s->n - dist];
        }
    }
}

static int fixed_block(inf_t *s)
{
    static huff_t lc, dc;
    static int ready;
    if (!ready) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dc, l, 30);
        ready = 1;
    }
    return codes(s, &lc, &dc);
}

static int dynamic_block(inf_t *s)
{
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t lengths[320];
    huff_t lc, dc;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30)
        return -1;
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < ncode; i++)
        lengths[order[i]] = (uint8_t)bits(s, 3);
    if (build(&lc, lengths, 19) != 0)
        return -1;
    int i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, &lc);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        int rep, val = 0;
        if (sym == 16) {
            if (!i)
                return -1;
            val = lengths[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (i + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[i++] = (uint8_t)val;
    }
    if (build(&lc, lengths, nlen) < 0 || build(&dc, lengths + nlen, ndist) < 0)
        return -1;
    return codes(s, &lc, &dc);
}

long png_inflate(const uint8_t *src, size_t len, uint8_t *out, size_t cap)
{
    if (len < 2 || (src[0] & 15) != 8 || ((src[0] << 8) | src[1]) % 31)
        return -1;
    inf_t s = { src, len, 2, 0, 0, out, cap, 0, 0 };
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2), r;
        if (s.bad)
            return -1;
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.pos + 4 > s.len)
                return -1;
            unsigned n = s.src[s.pos] | s.src[s.pos + 1] << 8;
            s.pos += 4;
            if (s.pos + n > s.len || s.n + n > s.cap)
                return -1;
            memcpy(s.out + s.n, s.src + s.pos, n);
            s.pos += n;
            s.n += n;
            r = 0;
        } else if (type == 1) {
            r = fixed_block(&s);
        } else if (type == 2) {
            r = dynamic_block(&s);
        } else {
            return -1;
        }
        if (r != 0 || s.bad)
            return -1;
    } while (!last);
    return (long)s.n;
}

/* ------------------------------------------------------------ PNG */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

int png_rgba(const uint8_t *png, size_t len, uint8_t **rgba, int *pw, int *ph)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (len < 33 || memcmp(png, sig, 8) != 0)
        return -1;
    uint32_t w = 0, h = 0;
    int depth = 0, type = -1, inter = 0;
    uint8_t pal[256][4];
    int npal = 0;
    size_t idat = 0;
    uint8_t *z = NULL;
    for (size_t p = 8; p + 12 <= len;) {
        uint32_t n = be32(png + p);
        const uint8_t *t = png + p + 4, *d = png + p + 8;
        if (n > len - p - 12)
            break;
        if (!memcmp(t, "IHDR", 4) && n >= 13) {
            w = be32(d);
            h = be32(d + 4);
            depth = d[8];
            type = d[9];
            inter = d[12];
        } else if (!memcmp(t, "PLTE", 4)) {
            npal = (int)(n / 3 > 256 ? 256 : n / 3);
            for (int i = 0; i < npal; i++) {
                pal[i][0] = d[i * 3];
                pal[i][1] = d[i * 3 + 1];
                pal[i][2] = d[i * 3 + 2];
                pal[i][3] = 255;
            }
        } else if (!memcmp(t, "tRNS", 4) && type == 3) {
            for (uint32_t i = 0; i < n && i < 256; i++)
                pal[i][3] = d[i];
        } else if (!memcmp(t, "IDAT", 4)) {
            uint8_t *nz = realloc(z, idat + n);
            if (!nz) {
                free(z);
                return -1;
            }
            z = nz;
            memcpy(z + idat, d, n);
            idat += n;
        } else if (!memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + n;
    }
    int bpp = type == 6 ? 4 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 0 ? 1 : 0;
    if (!z || !w || !h || w > 4096 || h > 4096 || depth != 8 || !bpp || inter) {
        free(z);
        return -1;
    }
    size_t row = (size_t)w * (size_t)bpp, raw_len = (row + 1) * h;
    uint8_t *raw = malloc(raw_len), *out = malloc((size_t)w * h * 4);
    if (!raw || !out || png_inflate(z, idat, raw, raw_len) != (long)raw_len) {
        free(z);
        free(raw);
        free(out);
        return -1;
    }
    free(z);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *r = raw + y * (row + 1), f = r[0], *cur = r + 1;
        const uint8_t *up = y ? raw + (y - 1) * (row + 1) + 1 : NULL;
        for (size_t i = 0; i < row; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = up ? up[i] : 0,
                c = up && i >= (size_t)bpp ? up[i - bpp] : 0;
            switch (f) {
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + b); break;
            case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
            case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
            }
        }
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *o = out + ((size_t)y * w + x) * 4;
            const uint8_t *s = cur + x * (uint32_t)bpp;
            switch (type) {
            case 6: memcpy(o, s, 4); break;
            case 2: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
            case 3: memcpy(o, s[0] < npal ? pal[s[0]] : pal[0], 4); break;
            case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
            default: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
            }
        }
    }
    free(raw);
    *rgba = out;
    *pw = (int)w;
    *ph = (int)h;
    return 0;
}
