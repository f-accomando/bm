/*
 * nano8 cartridges: PNG (with its own inflate), the hidden bytes, the code
 * compressions and the .p8 text sections (see n8cart.h).
 */
#include "n8cart.h"

#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t n, const char *msg)
{
    if (err && n) {
        strncpy(err, msg, n - 1);
        err[n - 1] = 0;
    }
    return -1;
}

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

long n8_inflate(const uint8_t *src, size_t len, uint8_t *out, size_t cap)
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

int n8_png_rgba(const uint8_t *png, size_t len, uint8_t **rgba, int *pw, int *ph)
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
    if (!raw || !out || n8_inflate(z, idat, raw, raw_len) != (long)raw_len) {
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

/* ------------------------------------------------------------ code */

static char *old_unpack(const uint8_t *a, size_t len, size_t *out_len)
{
    static const char lut[] = "\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";
    size_t n = (size_t)(a[4] << 8 | a[5]), o = 0;
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    for (size_t i = 8; i < len && o < n;) {
        int b = a[i++];
        if (b == 0) {
            if (i < len)
                out[o++] = (char)a[i++];
        } else if (b <= 0x3B) {
            out[o++] = lut[b - 1];
        } else {
            if (i >= len)
                break;
            int b2 = a[i++];
            size_t off = (size_t)(b - 0x3C) * 16 + (b2 & 15), cnt = (size_t)(b2 >> 4) + 2;
            if (off > o || !off)
                break;
            for (size_t k = 0; k < cnt && o < n; k++, o++)
                out[o] = out[o - off];
        }
    }
    out[o] = 0;
    *out_len = o;
    return out;
}

typedef struct {
    const uint8_t *p;
    size_t len, bit;
} bitrd_t;

static int getbit(bitrd_t *r)
{
    size_t byte = r->bit >> 3;
    int b = byte < r->len ? r->p[byte] >> (r->bit & 7) & 1 : 0;
    r->bit++;
    return b;
}

static int getbits(bitrd_t *r, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++)
        v |= getbit(r) << i;
    return v;
}

static char *pxa_unpack(const uint8_t *a, size_t len, size_t *out_len)
{
    size_t n = (size_t)(a[4] << 8 | a[5]), o = 0;
    size_t clen = (size_t)(a[6] << 8 | a[7]);
    if (clen > len)
        clen = len;
    uint8_t mtf[256];
    for (int i = 0; i < 256; i++)
        mtf[i] = (uint8_t)i;
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    bitrd_t r = { a + 8, clen > 8 ? clen - 8 : 0, 0 };
    while (o < n && (r.bit >> 3) <= r.len) {
        if (getbit(&r)) {
            int nb = 4;
            while (getbit(&r) && nb < 16)
                nb++;
            int idx = getbits(&r, nb) + (1 << nb) - 16;
            if (idx > 255)
                break;
            uint8_t c = mtf[idx];
            memmove(mtf + 1, mtf, (size_t)idx);
            mtf[0] = c;
            out[o++] = (char)c;
        } else {
            int nb = getbit(&r) ? (getbit(&r) ? 5 : 10) : 15;
            size_t off = (size_t)getbits(&r, nb) + 1;
            if (nb == 10 && off == 1) {
                for (int c = getbits(&r, 8); c && o < n; c = getbits(&r, 8))
                    out[o++] = (char)c;
                continue;
            }
            size_t cnt = 3;
            for (int part = 7; part == 7; cnt += (size_t)part)
                part = getbits(&r, 3);
            if (off > o)
                break;
            for (size_t k = 0; k < cnt && o < n; k++, o++)
                out[o] = out[o - off];
        }
    }
    out[o] = 0;
    *out_len = o;
    return out;
}

char *n8_code_unpack(const uint8_t *a, size_t len, size_t *out_len)
{
    if (len >= 8 && !memcmp(a, ":c:\0", 4))
        return old_unpack(a, len, out_len);
    if (len >= 8 && !memcmp(a, "\0pxa", 4))
        return pxa_unpack(a, len, out_len);
    size_t n = 0;
    while (n < len && a[n])
        n++;
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, a, n);
    out[n] = 0;
    *out_len = n;
    return out;
}

/* ------------------------------------------------------------ P8SCII */

/* Unicode of the P8SCII characters that are not ASCII, as .p8 files write them */
static const uint16_t uni_low[32] = {
    0, 0x00B9, 0x00B2, 0x00B3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0, 0, 0x1D47, 0x1D9C, 0,
    0x1D49, 0x1DA0, 0x25AE, 0x25A0, 0x25A1, 0x2059, 0x2058, 0x2016, 0x25C0, 0x25B6, 0x300C,
    0x300D, 0x00A5, 0x2022, 0x3001, 0x3002, 0x309B, 0x309C,
};
static const uint32_t uni_high[128] = {
    0x2588, 0x2592, 0x1F431, 0x2B07, 0x2591, 0x273D, 0x25CF, 0x2665, 0x2609, 0xC6C3, 0x2302, 0x2B05,
    0x1F610, 0x266A, 0x1F17E, 0x25C6, 0x2026, 0x27A1, 0x2605, 0x29D7, 0x2B06, 0x02C7, 0x2227, 0x274E,
    0x25A4, 0x25A5,
    /* hiragana */
    0x3042, 0x3044, 0x3046, 0x3048, 0x304A, 0x304B, 0x304D, 0x304F, 0x3051, 0x3053, 0x3055, 0x3057,
    0x3059, 0x305B, 0x305D, 0x305F, 0x3061, 0x3064, 0x3066, 0x3068, 0x306A, 0x306B, 0x306C, 0x306D,
    0x306E, 0x306F, 0x3072, 0x3075, 0x3078, 0x307B, 0x307E, 0x307F, 0x3080, 0x3081, 0x3082, 0x3084,
    0x3086, 0x3088, 0x3089, 0x308A, 0x308B, 0x308C, 0x308D, 0x308F, 0x3092, 0x3093, 0x3063, 0x3083,
    0x3085, 0x3087,
    /* katakana */
    0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7,
    0x30B9, 0x30BB, 0x30BD, 0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD,
    0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4,
    0x30E6, 0x30E8, 0x30E9, 0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F2, 0x30F3, 0x30C3, 0x30E3,
    0x30E5, 0x30E7,
    0x25DC, 0x25DD,
};

static int p8_of(uint32_t cp)
{
    if (cp == 0x25CB)
        return 127;
    for (int i = 1; i < 32; i++)
        if (uni_low[i] == cp)
            return i;
    for (int i = 0; i < 128; i++)
        if (uni_high[i] == cp)
            return 128 + i;
    return -1;
}

size_t n8_utf8_to_p8(char *s, size_t len)
{
    size_t o = 0;
    const uint8_t *u = (const uint8_t *)s;
    for (size_t i = 0; i < len;) {
        uint32_t cp = u[i];
        int n = 1;
        if (cp >= 0xF0 && i + 3 < len) {
            cp = (uint32_t)(u[i] & 7) << 18 | (uint32_t)(u[i + 1] & 63) << 12 | (uint32_t)(u[i + 2] & 63) << 6 | (u[i + 3] & 63);
            n = 4;
        } else if (cp >= 0xE0 && i + 2 < len) {
            cp = (uint32_t)(u[i] & 15) << 12 | (uint32_t)(u[i + 1] & 63) << 6 | (u[i + 2] & 63);
            n = 3;
        } else if (cp >= 0xC0 && i + 1 < len) {
            cp = (uint32_t)(u[i] & 31) << 6 | (u[i + 1] & 63);
            n = 2;
        }
        if (i + (size_t)n > len)
            n = 1;
        i += (size_t)n;
        if (cp == 0xFE0F || cp == 0xFEFF)       /* emoji presentation, byte order mark */
            continue;
        if (cp < 128 && n == 1) {
            s[o++] = (char)cp;
            continue;
        }
        int b = p8_of(cp);
        s[o++] = (char)(b >= 0 ? b : '?');
    }
    if (o < len)
        s[o] = 0;
    return o;
}

/* ------------------------------------------------------------ labels */

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((c >> 19 & 0x1F) << 11 | (c >> 10 & 0x3F) << 5 | (c >> 3 & 0x1F));
}

/* "-- title" and "-- by author" on the first two lines of the code */
static void comment_line(const char *code, size_t len, int line, char *out, size_t n)
{
    size_t i = 0;
    for (int l = 0; l < line && i < len; i++)
        if (code[i] == '\n')
            l++;
    out[0] = 0;
    while (i < len && (code[i] == ' ' || code[i] == '\t'))
        i++;
    if (i + 2 > len || code[i] != '-' || code[i + 1] != '-')
        return;
    i += 2;
    while (i < len && (code[i] == ' ' || code[i] == '-'))
        i++;
    size_t o = 0;
    for (; i < len && code[i] != '\n' && code[i] != '\r' && o + 1 < n; i++)
        out[o++] = code[i];
    while (o && out[o - 1] == ' ')
        o--;
    out[o] = 0;
}

static void title_author(n8_cart_t *c)
{
    if (!c->code)
        return;
    comment_line(c->code, c->code_len, 0, c->title, sizeof c->title);
    comment_line(c->code, c->code_len, 1, c->author, sizeof c->author);
    if (c->title[0] == '[' || !strncmp(c->title, "#include", 8))
        c->title[0] = 0;
}

/* ------------------------------------------------------------ .p8.png */

static int load_png(const uint8_t *data, size_t len, int flags, n8_cart_t *c, char *err, size_t errlen)
{
    uint8_t *px;
    int w, h;
    if (n8_png_rgba(data, len, &px, &w, &h) != 0)
        return fail(err, errlen, "not a PNG this loader reads");
    if (w * h < 0x8000 + 1) {
        free(px);
        return fail(err, errlen, "the picture is too small for a cartridge");
    }
    if (w >= 144 && h >= 152 && (c->label = malloc(128 * 128 * 2)) != NULL)
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                const uint8_t *p = px + ((size_t)(y + 24) * (size_t)w + (size_t)x + 16) * 4;
                c->label[y * 128 + x] = rgb565((uint32_t)p[0] << 16 | p[1] << 8 | p[2]);
            }
    uint8_t *bytes = malloc(0x8020);
    if (!bytes) {
        free(px);
        return fail(err, errlen, "out of memory");
    }
    for (int i = 0; i < 0x8020 && i < w * h; i++) {
        const uint8_t *p = px + (size_t)i * 4;
        bytes[i] = (uint8_t)((p[3] & 3) << 6 | (p[0] & 3) << 4 | (p[1] & 3) << 2 | (p[2] & 3));
    }
    free(px);
    memcpy(c->rom, bytes, N8_ROM_SIZE);
    c->version = bytes[0x8000];
    c->code = n8_code_unpack(bytes + 0x4300, 0x8000 - 0x4300, &c->code_len);
    free(bytes);
    if (!c->code)
        return fail(err, errlen, "cannot unpack the code");
    title_author(c);
    if (flags & N8_LOAD_LABEL) {
        free(c->code);
        c->code = NULL;
        c->code_len = 0;
    }
    return 0;
}

/* ------------------------------------------------------------ .p8 */

static int hexv(int ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
           ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}

enum { S_NONE, S_LUA, S_GFX, S_GFF, S_LABEL, S_MAP, S_SFX, S_MUSIC, S_OTHER };

static int section(const char *l, size_t n)
{
    if (n < 5 || l[0] != '_' || l[1] != '_' || l[n - 1] != '_' || l[n - 2] != '_')
        return -1;
    static const struct { const char *name; int s; } names[] = {
        { "__lua__", S_LUA }, { "__gfx__", S_GFX }, { "__gff__", S_GFF }, { "__label__", S_LABEL },
        { "__map__", S_MAP }, { "__sfx__", S_SFX }, { "__music__", S_MUSIC },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strlen(names[i].name) == n && !memcmp(l, names[i].name, n))
            return names[i].s;
    for (size_t i = 2; i < n - 2; i++)
        if (!((l[i] >= 'a' && l[i] <= 'z') || (l[i] >= '0' && l[i] <= '9') || l[i] == '_' || l[i] == ':'))
            return -1;
    return S_OTHER;
}

static void sfx_line(uint8_t *rom, int idx, const char *l, size_t n)
{
    if (idx >= 64 || n < 8)
        return;
    uint8_t *s = rom + N8_SFX + idx * 68;
    for (int k = 0; k < 4; k++) {
        int a = hexv(l[2 * k]), b = hexv(l[2 * k + 1]);
        if (a >= 0 && b >= 0)
            s[64 + k] = (uint8_t)(a << 4 | b);
    }
    for (int note = 0; note < 32 && 8 + note * 5 + 5 <= (int)n; note++) {
        const char *q = l + 8 + note * 5;
        int p1 = hexv(q[0]), p2 = hexv(q[1]), wave = hexv(q[2]), vol = hexv(q[3]), fx = hexv(q[4]);
        if (p1 < 0 || p2 < 0 || wave < 0 || vol < 0 || fx < 0)
            continue;
        int pitch = (p1 << 4 | p2) & 63;
        unsigned v = (unsigned)pitch | (unsigned)(wave & 7) << 6 | (unsigned)(vol & 7) << 9 |
                     (unsigned)(fx & 7) << 12 | (unsigned)(wave >> 3 & 1) << 15;
        s[note * 2] = (uint8_t)v;
        s[note * 2 + 1] = (uint8_t)(v >> 8);
    }
}

static void music_line(uint8_t *rom, int idx, const char *l, size_t n)
{
    if (idx >= 64 || n < 11)
        return;
    int f1 = hexv(l[0]), f2 = hexv(l[1]);
    int flags = f1 < 0 || f2 < 0 ? 0 : f1 << 4 | f2;
    uint8_t *p = rom + N8_MUSIC + idx * 4;
    for (int k = 0; k < 4; k++) {
        int a = hexv(l[3 + 2 * k]), b = hexv(l[4 + 2 * k]);
        if (a < 0 || b < 0)
            continue;
        p[k] = (uint8_t)((a << 4 | b) & 0x7F) | (uint8_t)((flags >> k & 1) << 7);
    }
}

static int load_text(const uint8_t *data, size_t len, int flags, n8_cart_t *c, char *err, size_t errlen)
{
    const char *t = (const char *)data;
    int sec = S_NONE, row = 0;
    size_t code_start = 0, code_end = 0;
    int have_code = 0;
    uint16_t *label = NULL;
    for (size_t i = 0; i < len;) {
        size_t e = i;
        while (e < len && t[e] != '\n')
            e++;
        size_t n = e - i;
        const char *l = t + i;
        size_t next = e < len ? e + 1 : e;
        if (n && l[n - 1] == '\r')
            n--;
        int s = section(l, n);
        if (s >= 0) {
            if (sec == S_LUA) {
                code_end = i;
                have_code = 1;
            }
            sec = s;
            row = 0;
            if (s == S_LUA)
                code_start = next;
            if (s == S_LABEL && !label)
                label = calloc(128 * 128, 2);
            i = next;
            continue;
        }
        if (!strncmp(l, "version ", 8) && sec == S_NONE)
            c->version = atoi(l + 8);
        switch (sec) {
        case S_GFX:
            if (row < 128)
                for (size_t x = 0; x + 1 < n && x < 128; x += 2) {
                    int a = hexv(l[x]), b = hexv(l[x + 1]);
                    if (a >= 0 && b >= 0)
                        c->rom[N8_GFX + row * 64 + x / 2] = (uint8_t)(b << 4 | a);
                }
            break;
        case S_GFF:
            for (size_t x = 0; x + 1 < n && x < 256 && row < 2; x += 2) {
                int a = hexv(l[x]), b = hexv(l[x + 1]);
                if (a >= 0 && b >= 0)
                    c->rom[N8_FLAGS + row * 128 + x / 2] = (uint8_t)(a << 4 | b);
            }
            break;
        case S_MAP:
            for (size_t x = 0; x + 1 < n && x < 256 && row < 32; x += 2) {
                int a = hexv(l[x]), b = hexv(l[x + 1]);
                if (a >= 0 && b >= 0)
                    c->rom[N8_MAP + row * 128 + x / 2] = (uint8_t)(a << 4 | b);
            }
            break;
        case S_LABEL:
            if (label && row < 128)
                for (size_t x = 0; x < n && x < 128; x++) {
                    int ch = l[x], v = hexv(ch);
                    if (v < 0 && ch >= 'g' && ch <= 'v')
                        v = 16 + ch - 'g';
                    if (v < 0)
                        v = 0;
                    label[row * 128 + x] = rgb565(n8_rgb(v < 16 ? v : 128 + v - 16));
                }
            break;
        case S_SFX:
            sfx_line(c->rom, row, l, n);
            break;
        case S_MUSIC:
            music_line(c->rom, row, l, n);
            break;
        }
        if (sec != S_NONE && sec != S_LUA)
            row++;
        i = next;
    }
    if (sec == S_LUA) {
        code_end = len;
        have_code = 1;
    }
    if (!have_code) {
        free(label);
        return fail(err, errlen, "no __lua__ section");
    }
    c->label = label;
    size_t n = code_end - code_start;
    c->code = malloc(n + 1);
    if (!c->code)
        return fail(err, errlen, "out of memory");
    memcpy(c->code, t + code_start, n);
    c->code[n] = 0;
    c->code_len = n8_utf8_to_p8(c->code, n);
    c->code[c->code_len] = 0;
    title_author(c);
    if (flags & N8_LOAD_LABEL) {
        free(c->code);
        c->code = NULL;
        c->code_len = 0;
    }
    return 0;
}

int n8_cart_load(const uint8_t *data, size_t len, int flags, n8_cart_t *c, char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P')
        return load_png(data, len, flags, c, err, errlen);
    size_t skip = len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
    if (len - skip >= 16 && !memcmp(data + skip, "pico-8 cartridge", 16))
        return load_text(data + skip, len - skip, flags, c, err, errlen);
    return fail(err, errlen, "not a .p8 or .p8.png cartridge");
}

void n8_cart_free(n8_cart_t *c)
{
    free(c->code);
    free(c->label);
    c->code = NULL;
    c->label = NULL;
}
