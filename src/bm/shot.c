#include "bm/shot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ names */

void shot_prefix(char out[5], const char *title, const char *prefix)
{
    const char *src = prefix && *prefix ? prefix : title ? title : "";
    int n = 0;
    for (; *src && n < 4; src++) {
        char c = *src;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out[n++] = c;
    }
    if (n == 0) {
        memcpy(out, "shot", 4);
        n = 4;
    }
    out[n] = 0;
}

int shot_name(char out[13], const char *title, const char *prefix, int n)
{
    if (n < 0 || n > 9999)
        return -1;
    char p[5];
    shot_prefix(p, title, prefix);
    size_t l = strlen(p);
    memcpy(out, p, l);
    out[l + 0] = (char)('0' + n / 1000);
    out[l + 1] = (char)('0' + n / 100 % 10);
    out[l + 2] = (char)('0' + n / 10 % 10);
    out[l + 3] = (char)('0' + n % 10);
    memcpy(out + l + 4, ".png", 5);
    return 0;
}

int shot_number(const char *name, const char prefix[5])
{
    size_t l = strlen(prefix);
    char up[13], pre[5];
    size_t i = 0;
    for (; name[i] && i < sizeof up - 1; i++)           /* any case: old cards have capitals */
        up[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] - 'A' + 'a') : name[i];
    up[i] = 0;
    for (size_t k = 0; k < l && k < 4; k++)
        pre[k] = prefix[k] >= 'A' && prefix[k] <= 'Z' ? (char)(prefix[k] - 'A' + 'a') : prefix[k];
    if (i != l + 8 || memcmp(up, pre, l) != 0 || memcmp(up + l + 4, ".png", 5) != 0)
        return -1;
    int n = 0;
    for (int k = 0; k < 4; k++) {
        if (up[l + k] < '0' || up[l + k] > '9')
            return -1;
        n = n * 10 + (up[l + k] - '0');
    }
    return n;
}

/* ------------------------------------------------------------ PNG */

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    while (n--)
        crc = crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* the deflate bits, least significant first */
typedef struct {
    uint8_t *p;
    size_t n;
    uint32_t acc;
    int bits;
} bw_t;

static void put_bits(bw_t *b, uint32_t v, int n)
{
    b->acc |= v << b->bits;
    b->bits += n;
    while (b->bits >= 8) {
        b->p[b->n++] = (uint8_t)b->acc;
        b->acc >>= 8;
        b->bits -= 8;
    }
}

/* a Huffman code goes out from its most significant bit */
static void put_code(bw_t *b, uint32_t code, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++)
        r |= (code >> i & 1) << (n - 1 - i);
    put_bits(b, r, n);
}

static void put_sym(bw_t *b, int s)             /* the fixed literal/length code */
{
    if (s < 144) put_code(b, 0x30u + (uint32_t)s, 8);
    else if (s < 256) put_code(b, 0x190u + (uint32_t)s - 144, 9);
    else if (s < 280) put_code(b, (uint32_t)s - 256, 7);
    else put_code(b, 0xC0u + (uint32_t)s - 280, 8);
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67,
                                       83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
                                       5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
                                        769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
                                        11, 11, 12, 12, 13, 13 };

static void put_match(bw_t *b, int len, int dist)
{
    int lc = 28;
    while (len_base[lc] > len)
        lc--;
    put_sym(b, 257 + lc);
    put_bits(b, (uint32_t)(len - len_base[lc]), len_extra[lc]);
    int dc = 29;
    while (dist_base[dc] > dist)
        dc--;
    put_code(b, (uint32_t)dc, 5);
    put_bits(b, (uint32_t)(dist - dist_base[dc]), dist_extra[dc]);
}

static int run_len(const uint8_t *raw, size_t i, size_t n, size_t dist)
{
    if (i < dist)
        return 0;
    size_t k = 0;
    while (k < 258 && i + k < n && raw[i + k] == raw[i + k - dist])
        k++;
    return (int)k;
}

static void chunk(uint8_t *out, size_t *o, const char type[4], const uint8_t *data, size_t n)
{
    put32(out + *o, (uint32_t)n);
    memcpy(out + *o + 4, type, 4);
    if (n)
        memcpy(out + *o + 8, data, n);
    put32(out + *o + 8 + n, ~crc_update(0xFFFFFFFFu, out + *o + 4, n + 4));
    *o += n + 12;
}

/* The PNG chunks around a zlib stream of `raw` (n bytes, `row` bytes a row, filter byte 0 each);
 * frees `raw`. ctype: 2 RGB, 6 RGBA. */
static uint8_t *png_encode(uint8_t *raw, size_t n, size_t row, int w, int h, int ctype, size_t *len)
{
    crc_init();
    const size_t cap = n + n / 8 + 128;             /* 9 bits at the worst for a byte */
    uint8_t *out = malloc(64);
    if (!out) {
        free(raw);
        return NULL;
    }
    uint8_t *z = malloc(cap);                       /* the zlib stream */
    if (!z) {
        free(raw);
        free(out);
        return NULL;
    }
    bw_t b = { z, 0, 0, 0 };
    b.p[b.n++] = 0x78;
    b.p[b.n++] = 0x01;
    put_bits(&b, 1, 1);                             /* the only block, and the last */
    put_bits(&b, 1, 2);                             /* with the fixed codes */
    uint32_t s1 = 1, s2 = 0;                        /* Adler-32 of the raw data */
    for (size_t i = 0; i < n; i++) {
        s1 = (s1 + raw[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    for (size_t i = 0; i < n;) {
        int a = run_len(raw, i, n, 3), c = run_len(raw, i, n, row);
        if (a >= c && a >= 3) {
            put_match(&b, a, 3);
            i += (size_t)a;
        } else if (c >= 3) {
            put_match(&b, c, (int)row);
            i += (size_t)c;
        } else {
            put_sym(&b, raw[i++]);
        }
        if (b.n + 8 > cap)                          /* (cannot happen: see cap) */
            break;
    }
    put_sym(&b, 256);
    put_bits(&b, 0, 7);                             /* to a byte */
    b.p[b.n++] = (uint8_t)(s2 >> 8); b.p[b.n++] = (uint8_t)s2;
    b.p[b.n++] = (uint8_t)(s1 >> 8); b.p[b.n++] = (uint8_t)s1;
    free(raw);

    size_t o = 0;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(out, sig, 8);
    o = 8;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = (uint8_t)ctype; ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* 8 bits */
    chunk(out, &o, "IHDR", ihdr, 13);
    out = realloc(out, o + b.n + 24);
    if (!out) {
        free(z);
        return NULL;
    }
    chunk(out, &o, "IDAT", z, b.n);
    chunk(out, &o, "IEND", NULL, 0);
    free(z);
    *len = o;
    return out;
}

uint8_t *shot_png(const uint16_t *px, int stride, int w, int h, size_t *len)
{
    if (w < 1 || h < 1 || w > 4096 || h > 4096 || stride < w)
        return NULL;
    const size_t row = 3 * (size_t)w + 1;           /* a filter byte (0), then R G B */
    const size_t n = row * (size_t)h;
    uint8_t *raw = malloc(n);
    if (!raw)
        return NULL;
    for (int y = 0; y < h; y++) {
        uint8_t *d = raw + (size_t)y * row;
        const uint16_t *s = px + (size_t)y * (size_t)stride;
        *d++ = 0;
        for (int x = 0; x < w; x++) {
            uint16_t c = s[x];
            uint8_t r = (uint8_t)(c >> 11 << 3), g = (uint8_t)(c >> 5 << 2), b = (uint8_t)(c << 3);
            *d++ = (uint8_t)(r | r >> 5);
            *d++ = (uint8_t)(g | g >> 6);
            *d++ = (uint8_t)(b | b >> 5);
        }
    }
    return png_encode(raw, n, row, w, h, 2, len);
}

uint8_t *shot_png_rgba(const uint16_t *px, const uint8_t *alpha, int stride, int w, int h, size_t *len)
{
    if (w < 1 || h < 1 || w > 4096 || h > 4096 || stride < w)
        return NULL;
    const size_t row = 4 * (size_t)w + 1;           /* a filter byte (0), then R G B A */
    const size_t n = row * (size_t)h;
    uint8_t *raw = malloc(n);
    if (!raw)
        return NULL;
    for (int y = 0; y < h; y++) {
        uint8_t *d = raw + (size_t)y * row;
        const uint16_t *s = px + (size_t)y * (size_t)stride;
        const uint8_t *al = alpha ? alpha + (size_t)y * (size_t)stride : NULL;
        *d++ = 0;
        for (int x = 0; x < w; x++) {
            uint16_t c = s[x];
            uint8_t r = (uint8_t)(c >> 11 << 3), g = (uint8_t)(c >> 5 << 2), b = (uint8_t)(c << 3);
            *d++ = (uint8_t)(r | r >> 5);
            *d++ = (uint8_t)(g | g >> 6);
            *d++ = (uint8_t)(b | b >> 5);
            *d++ = (uint8_t)(!al || al[x] ? 255 : 0);
        }
    }
    return png_encode(raw, n, row, w, h, 6, len);
}

/* ---------------------------------------------------------------------------------------------------
 * Animated GIF: the work buffers are allocated for the call and freed before it returns. */

typedef struct { uint8_t *p; size_t n, cap; int bad; } gbuf_t;

static void gput(gbuf_t *b, const void *d, size_t n)
{
    if (b->bad)
        return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + n)
            c *= 2;
        uint8_t *q = realloc(b->p, c);
        if (!q) {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void gbyte(gbuf_t *b, int v)
{
    uint8_t c = (uint8_t)v;
    gput(b, &c, 1);
}

static void gword(gbuf_t *b, int v)
{
    gbyte(b, v & 255);
    gbyte(b, v >> 8 & 255);
}

/* LZW codes, packed low bits first, in sub-blocks of up to 255 bytes */
typedef struct { gbuf_t *o; uint32_t acc; int nacc; uint8_t blk[255]; int nblk; } lzw_out_t;

static void lzw_flush_block(lzw_out_t *w)
{
    if (w->nblk) {
        gbyte(w->o, w->nblk);
        gput(w->o, w->blk, (size_t)w->nblk);
        w->nblk = 0;
    }
}

static void lzw_code(lzw_out_t *w, int code, int nbits)
{
    w->acc |= (uint32_t)code << w->nacc;
    w->nacc += nbits;
    while (w->nacc >= 8) {
        w->blk[w->nblk++] = (uint8_t)w->acc;
        w->acc >>= 8;
        w->nacc -= 8;
        if (w->nblk == 255)
            lzw_flush_block(w);
    }
}

#define LZW_HASH 8192                               /* a power of two, more than 4096 entries */

/* The image data of one frame: the minimum code size, the sub-blocks, the 0 end. `key` and `val` are
 * the work table (LZW_HASH entries each). */
static void lzw_frame(gbuf_t *o, const uint8_t *idx, size_t n, int mcs, uint32_t *key, uint16_t *val)
{
    lzw_out_t w = { o, 0, 0, { 0 }, 0 };
    const int clear = 1 << mcs, eoi = clear + 1;
    int next = eoi, nbits = mcs + 1;
    gbyte(o, mcs);
    memset(key, 0xFF, LZW_HASH * sizeof *key);
    lzw_code(&w, clear, nbits);
    int cur = idx[0];
    for (size_t i = 1; i < n; i++) {
        const int c = idx[i];
        const uint32_t k = (uint32_t)cur << 8 | (uint32_t)c;
        uint32_t h = (k * 2654435761u) >> 19 & (LZW_HASH - 1);
        while (key[h] != 0xFFFFFFFFu && key[h] != k)
            h = (h + 1) & (LZW_HASH - 1);
        if (key[h] == k) {
            cur = val[h];
            continue;
        }
        lzw_code(&w, cur, nbits);
        key[h] = k;
        val[h] = (uint16_t)++next;
        if (next >= (1 << nbits))
            nbits++;
        if (next == 4095) {
            lzw_code(&w, clear, nbits);
            memset(key, 0xFF, LZW_HASH * sizeof *key);
            next = eoi;
            nbits = mcs + 1;
        }
        cur = c;
    }
    lzw_code(&w, cur, nbits);
    lzw_code(&w, eoi, nbits);
    if (w.nacc)
        w.blk[w.nblk++] = (uint8_t)w.acc;
    lzw_flush_block(&w);
    gbyte(o, 0);
}

/* RGB565 widened to 8 bits a channel, as shot_png does */
static void rgb565_to_888(uint16_t c, uint8_t *rgb)
{
    uint8_t r = (uint8_t)(c >> 11 << 3), g = (uint8_t)(c >> 5 << 2), b = (uint8_t)(c << 3);
    rgb[0] = (uint8_t)(r | r >> 5);
    rgb[1] = (uint8_t)(g | g >> 6);
    rgb[2] = (uint8_t)(b | b >> 5);
}

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Median cut of the distinct colours: cols[i] = RGB565 (reordered), cnt[c] = how many pixels have colour
 * c. Fills map[c] (0-based colour number) for each colour in use and pal[] (RGB565, the weighted mean of
 * the box); returns the number of colours (at most max). `work` has one uint32 a distinct colour. */
static int median_cut(uint16_t *cols, int ncol, const uint32_t *cnt, int max, uint16_t *map,
                      uint16_t *pal, uint32_t *work)
{
    struct { int lo, hi; } box[255];
    int nb = 1;
    box[0].lo = 0;
    box[0].hi = ncol;
    while (nb < max) {
        int best = -1;
        uint64_t bestw = 0;
        for (int b = 0; b < nb; b++) {                  /* the box with most pixels that can be split */
            if (box[b].hi - box[b].lo < 2)
                continue;
            uint64_t t = 0;
            for (int i = box[b].lo; i < box[b].hi; i++)
                t += cnt[cols[i]];
            if (best < 0 || t > bestw) {
                best = b;
                bestw = t;
            }
        }
        if (best < 0)
            break;
        const int lo = box[best].lo, hi = box[best].hi;
        int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 };
        for (int i = lo; i < hi; i++) {
            const int ch[3] = { cols[i] >> 11, cols[i] >> 6 & 31, cols[i] & 31 };   /* green to 5 bits */
            for (int k = 0; k < 3; k++) {
                if (ch[k] < mn[k]) mn[k] = ch[k];
                if (ch[k] > mx[k]) mx[k] = ch[k];
            }
        }
        int axis = 0;
        for (int k = 1; k < 3; k++)
            if (mx[k] - mn[k] > mx[axis] - mn[axis])
                axis = k;
        for (int i = lo; i < hi; i++) {
            const uint16_t c = cols[i];
            const uint32_t v = axis == 0 ? (uint32_t)(c >> 11) : axis == 1 ? (uint32_t)(c >> 5 & 63) : (uint32_t)(c & 31);
            work[i] = v << 16 | c;
        }
        qsort(work + lo, (size_t)(hi - lo), sizeof *work, cmp_u32);
        for (int i = lo; i < hi; i++)
            cols[i] = (uint16_t)work[i];
        uint64_t acc = 0;
        int cut = lo + 1;
        for (int i = lo; i < hi - 1; i++) {             /* the median pixel; both halves are not empty */
            acc += cnt[cols[i]];
            cut = i + 1;
            if (acc * 2 >= bestw)
                break;
        }
        box[nb].lo = cut;
        box[nb].hi = hi;
        box[best].hi = cut;
        nb++;
    }
    for (int b = 0; b < nb; b++) {
        uint64_t r = 0, g = 0, bl = 0, t = 0;
        for (int i = box[b].lo; i < box[b].hi; i++) {
            const uint16_t c = cols[i];
            const uint64_t n = cnt[c];
            r += n * (uint64_t)(c >> 11);
            g += n * (uint64_t)(c >> 5 & 63);
            bl += n * (uint64_t)(c & 31);
            t += n;
            map[c] = (uint16_t)b;
        }
        pal[b] = (uint16_t)(((r + t / 2) / t) << 11 | ((g + t / 2) / t) << 5 | ((bl + t / 2) / t));
    }
    return nb;
}

uint8_t *shot_gif(const uint16_t *px, const uint8_t *alpha, int w, int h, int nframes, int delay_cs,
                  size_t *len, const char **why)
{
    const char *dummy;
    if (!why)
        why = &dummy;
    if (w < 1 || h < 1 || w > 4096 || h > 4096) {
        *why = "the frames are not 1 to 4096 pixels a side";
        return NULL;
    }
    if (nframes < 1 || nframes > 64) {
        *why = "a GIF has 1 to 64 frames";
        return NULL;
    }
    if (delay_cs < 1 || delay_cs > 65535) {
        *why = "the delay is 1 to 65535 hundredths of a second";
        return NULL;
    }
    const size_t fpix = (size_t)w * (size_t)h, total = fpix * (size_t)nframes;
    if (total > (size_t)4096 * 64) {
        *why = "more than 262144 pixels in all the frames";
        return NULL;
    }
    uint32_t *cnt = calloc(65536, sizeof *cnt);        /* pixels a colour has */
    uint16_t *map = malloc(65536 * sizeof *map);       /* colour -> palette number */
    uint16_t *cols = malloc(65536 * sizeof *cols);     /* the distinct colours */
    uint32_t *work = malloc(65536 * sizeof *work);     /* the median cut's sort; the LZW table later */
    uint8_t *idx = malloc(fpix);
    uint16_t *val = malloc(LZW_HASH * sizeof *val);
    gbuf_t o = { 0 };
    uint8_t *res = NULL;
    uint16_t pal[255];
    int ncol = 0, npal;
    if (!cnt || !map || !cols || !work || !idx || !val) {
        *why = "no memory";
        goto done;
    }
    for (size_t i = 0; i < total; i++) {
        if (alpha && !alpha[i])
            continue;
        if (cnt[px[i]]++ == 0)
            cols[ncol++] = px[i];
    }
    if (ncol <= 255) {                                  /* exact */
        npal = ncol;
        for (int i = 0; i < ncol; i++) {
            map[cols[i]] = (uint16_t)i;
            pal[i] = cols[i];
        }
    } else {
        npal = median_cut(cols, ncol, cnt, 255, map, pal, work);
    }
    /* palette entry 0 is the transparent one; colour i is entry i + 1 */
    const int entries = npal + 1;
    int bits = 1;
    while ((1 << bits) < entries)
        bits++;
    const int mcs = bits < 2 ? 2 : bits;                /* minimum LZW code size */
    gput(&o, "GIF89a", 6);
    gword(&o, w);
    gword(&o, h);
    gbyte(&o, 0x80 | 7 << 4 | (bits - 1));              /* a global table of 2^bits entries */
    gbyte(&o, 0);                                       /* background: entry 0 */
    gbyte(&o, 0);
    for (int i = 0; i < (1 << bits); i++) {
        uint8_t rgb[3] = { 0, 0, 0 };
        if (i >= 1 && i < entries)
            rgb565_to_888(pal[i - 1], rgb);
        gput(&o, rgb, 3);
    }
    gput(&o, "\x21\xFF\x0B" "NETSCAPE2.0" "\x03\x01\x00\x00\x00", 19);   /* loop for ever */
    for (int f = 0; f < nframes; f++) {
        const uint16_t *fp = px + (size_t)f * fpix;
        const uint8_t *fa = alpha ? alpha + (size_t)f * fpix : NULL;
        int tr = 0;
        for (size_t i = 0; i < fpix; i++) {
            if (fa && !fa[i]) {
                idx[i] = 0;
                tr = 1;
            } else {
                idx[i] = (uint8_t)(map[fp[i]] + 1);
            }
        }
        gput(&o, "\x21\xF9\x04", 3);                    /* graphic control: disposal 2, transparency */
        gbyte(&o, 2 << 2 | (tr ? 1 : 0));
        gword(&o, delay_cs);
        gbyte(&o, 0);                                   /* transparent entry */
        gbyte(&o, 0);
        gbyte(&o, 0x2C);                                /* image descriptor: the whole screen */
        gword(&o, 0);
        gword(&o, 0);
        gword(&o, w);
        gword(&o, h);
        gbyte(&o, 0);
        lzw_frame(&o, idx, fpix, mcs, work, val);
    }
    gbyte(&o, 0x3B);
    if (o.bad) {
        *why = "no memory";
        goto done;
    }
    res = o.p;
    *len = o.n;
    o.p = NULL;
done:
    free(o.p);
    free(cnt);
    free(map);
    free(cols);
    free(work);
    free(idx);
    free(val);
    return res;
}
