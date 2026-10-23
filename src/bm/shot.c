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
