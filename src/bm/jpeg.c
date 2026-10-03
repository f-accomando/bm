#include "jpeg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bits[17];           /* codes of each length */
    uint8_t vals[256];
    int mincode[17], maxcode[18], valptr[17];
} huff_t;

typedef struct {
    int id, h, v, tq, td, ta;
    int bw, bh;                 /* blocks across and down (whole MCUs) */
    uint8_t *pix;               /* bw*8 x bh*8 samples */
    int dc;                     /* the predictor */
} comp_t;

typedef struct {
    const uint8_t *p, *end;
    uint32_t bits;
    int nbits;
    int marker;                 /* a marker met in the data, or 0 */
    uint16_t qt[4][64];
    huff_t hdc[4], hac[4];
    comp_t c[3];
    int nc, w, h, hmax, vmax, restart;
    char *err;
    size_t errlen;
} jd_t;

static const uint8_t ZIGZAG[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
    54, 47, 55, 62, 63
};

static int fail(jd_t *d, const char *what)
{
    if (d->err && d->errlen) {
        strncpy(d->err, what, d->errlen - 1);
        d->err[d->errlen - 1] = 0;
    }
    return -1;
}

int jpeg_is(const uint8_t *data, size_t len)
{
    return len > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

/* ------------------------------------------------------------ the bits */

static int fill(jd_t *d)
{
    while (d->nbits <= 24) {
        int b = 0;
        if (d->marker || d->p >= d->end)
            b = 0;                      /* past the end: zeros */
        else {
            b = *d->p++;
            if (b == 0xFF) {
                int n = d->p < d->end ? *d->p : 0xD9;
                if (n == 0)
                    d->p++;             /* a stuffed FF */
                else {
                    d->marker = n;      /* RSTn, EOI...: the data stops here */
                    d->p--;
                    b = 0;
                }
            }
        }
        d->bits |= (uint32_t)b << (24 - d->nbits);
        d->nbits += 8;
    }
    return 0;
}

static int getbits(jd_t *d, int n)
{
    if (n == 0)
        return 0;
    if (d->nbits < n)
        fill(d);
    int v = (int)(d->bits >> (32 - n));
    d->bits <<= n;
    d->nbits -= n;
    return v;
}

static int decode_huff(jd_t *d, const huff_t *h)
{
    if (d->nbits < 16)
        fill(d);
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | (int)(d->bits >> 31);
        d->bits <<= 1;
        d->nbits--;
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l])
            return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
    return -1;
}

static int extend(int v, int n)
{
    return n == 0 ? 0 : v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

/* ---------------------------------------------------------- the tables */

static void build_huff(huff_t *h)
{
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}

static int segment(jd_t *d, int marker, const uint8_t *s, int n)
{
    if (marker == 0xDB) {                       /* DQT */
        while (n > 0) {
            int pq = s[0] >> 4, tq = s[0] & 15;
            if (tq > 3 || n < 1 + 64 * (pq ? 2 : 1))
                return fail(d, "a bad quantization table");
            for (int i = 0; i < 64; i++)
                d->qt[tq][ZIGZAG[i]] = pq ? (uint16_t)(s[1 + i * 2] << 8 | s[2 + i * 2]) : s[1 + i];
            s += 1 + 64 * (pq ? 2 : 1);
            n -= 1 + 64 * (pq ? 2 : 1);
        }
    } else if (marker == 0xC4) {                /* DHT */
        while (n > 0) {
            int tc = s[0] >> 4, th = s[0] & 15;
            if (th > 3 || tc > 1 || n < 17)
                return fail(d, "a bad Huffman table");
            huff_t *h = tc ? &d->hac[th] : &d->hdc[th];
            int total = 0;
            h->bits[0] = 0;
            for (int i = 1; i <= 16; i++) {
                h->bits[i] = s[i];
                total += s[i];
            }
            if (total > 256 || n < 17 + total)
                return fail(d, "a bad Huffman table");
            memcpy(h->vals, s + 17, (size_t)total);
            build_huff(h);
            s += 17 + total;
            n -= 17 + total;
        }
    } else if (marker == 0xC0 || marker == 0xC1) {      /* SOF0 / SOF1: baseline */
        if (n < 6 || s[0] != 8)
            return fail(d, "not an 8-bit picture");
        d->h = s[1] << 8 | s[2];
        d->w = s[3] << 8 | s[4];
        d->nc = s[5];
        if ((d->nc != 1 && d->nc != 3) || n < 6 + d->nc * 3 || !d->w || !d->h || d->w > 8192 || d->h > 8192)
            return fail(d, "1 or 3 components, up to 8192 pixels a side");
        d->hmax = d->vmax = 1;
        for (int i = 0; i < d->nc; i++) {
            comp_t *c = &d->c[i];
            c->id = s[6 + i * 3];
            c->h = s[7 + i * 3] >> 4;
            c->v = s[7 + i * 3] & 15;
            c->tq = s[8 + i * 3];
            if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 || c->tq > 3)
                return fail(d, "a sampling factor above 2");
            if (c->h > d->hmax)
                d->hmax = c->h;
            if (c->v > d->vmax)
                d->vmax = c->v;
        }
    } else if (marker == 0xC2 || marker == 0xC3 || (marker >= 0xC5 && marker <= 0xCF && marker != 0xC8 && marker != 0xCC)) {
        return fail(d, "a progressive or lossless JPEG (baseline only)");
    } else if (marker == 0xDD) {                /* DRI */
        if (n < 2)
            return fail(d, "a bad restart interval");
        d->restart = s[0] << 8 | s[1];
    }
    return 0;
}

/* ------------------------------------------------------------ the IDCT */

static float COS[8][8];                         /* c(u) cos((2x+1)u pi/16) / 2 */

static void idct_init(void)
{
    if (COS[0][0] != 0)
        return;
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            COS[x][u] = (float)((u == 0 ? sqrt(0.5) : 1.0) * cos((2 * x + 1) * u * 3.14159265358979 / 16) / 2);
}

static void idct(const int *in, uint8_t *out, int stride)
{
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) {
                int v = in[y * 8 + u];
                if (v)
                    s += (float)v * COS[x][u];
            }
            tmp[y * 8 + x] = s;
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++)
                s += tmp[v * 8 + x] * COS[y][v];
            int p = (int)(s + 128.5f);
            out[y * stride + x] = (uint8_t)(p < 0 ? 0 : p > 255 ? 255 : p);
        }
}

/* ------------------------------------------------------------ the scan */

static int block(jd_t *d, comp_t *c, uint8_t *out, int stride)
{
    int coef[64];
    memset(coef, 0, sizeof coef);
    int t = decode_huff(d, &d->hdc[c->td]);
    if (t < 0)
        return -1;
    c->dc += extend(getbits(d, t), t);
    coef[0] = c->dc * d->qt[c->tq][0];
    for (int k = 1; k < 64;) {
        int rs = decode_huff(d, &d->hac[c->ta]);
        if (rs < 0)
            return -1;
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15)
                break;                          /* EOB */
            k += 16;
            continue;
        }
        k += r;
        if (k > 63)
            return -1;
        coef[ZIGZAG[k]] = extend(getbits(d, s), s) * d->qt[c->tq][ZIGZAG[k]];
        k++;
    }
    idct(coef, out, stride);
    return 0;
}

static int scan(jd_t *d, const uint8_t *s, int n)
{
    int ns = s[0];
    if (ns != d->nc || n < 1 + ns * 2 + 3)
        return fail(d, "a scan with the components apart (baseline only)");
    for (int i = 0; i < ns; i++) {
        int id = s[1 + i * 2], tables = s[2 + i * 2];
        comp_t *c = NULL;
        for (int k = 0; k < d->nc; k++)
            if (d->c[k].id == id)
                c = &d->c[k];
        if (!c || (tables >> 4) > 3 || (tables & 15) > 3)
            return fail(d, "a scan of an unknown component");
        c->td = tables >> 4;
        c->ta = tables & 15;
    }
    int mcux = (d->w + 8 * d->hmax - 1) / (8 * d->hmax), mcuy = (d->h + 8 * d->vmax - 1) / (8 * d->vmax);
    for (int i = 0; i < d->nc; i++) {
        comp_t *c = &d->c[i];
        c->bw = mcux * c->h;
        c->bh = mcuy * c->v;
        c->pix = malloc((size_t)c->bw * 8 * c->bh * 8);
        if (!c->pix)
            return fail(d, "no memory for the picture");
        c->dc = 0;
    }
    d->p = s + 1 + ns * 2 + 3;
    d->bits = 0;
    d->nbits = 0;
    d->marker = 0;
    int todo = d->restart;
    for (int my = 0; my < mcuy; my++)
        for (int mx = 0; mx < mcux; mx++) {
            if (d->restart && todo == 0) {
                /* an RSTn marker: the bits start again, the predictors too */
                d->bits = 0;
                d->nbits = 0;
                if (d->marker >= 0xD0 && d->marker <= 0xD7) {
                    d->p += 2;
                    d->marker = 0;
                } else {
                    while (d->p + 1 < d->end && !(d->p[0] == 0xFF && d->p[1] >= 0xD0 && d->p[1] <= 0xD7))
                        d->p++;
                    if (d->p + 1 < d->end)
                        d->p += 2;
                }
                for (int i = 0; i < d->nc; i++)
                    d->c[i].dc = 0;
                todo = d->restart;
            }
            for (int i = 0; i < d->nc; i++) {
                comp_t *c = &d->c[i];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        int stride = c->bw * 8;
                        uint8_t *out = c->pix + ((my * c->v + by) * 8) * stride + (mx * c->h + bx) * 8;
                        if (block(d, c, out, stride) < 0)
                            return fail(d, "broken picture data");
                    }
            }
            if (d->restart)
                todo--;
        }
    return 0;
}

static uint8_t clamp(float v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
}

int jpeg_decode(const uint8_t *data, size_t len, uint8_t **rgb, int *pw, int *ph, char *err, size_t errlen)
{
    jd_t *d = calloc(1, sizeof *d);
    if (!d)
        return -1;
    d->err = err;
    d->errlen = errlen;
    if (err && errlen)
        err[0] = 0;
    int ret = -1;
    if (!jpeg_is(data, len)) {
        fail(d, "not a JPEG file");
        goto out;
    }
    idct_init();
    const uint8_t *p = data + 2;
    d->end = data + len;
    int scanned = 0;
    while (p + 4 <= d->end && !scanned) {
        if (*p != 0xFF) {
            p++;
            continue;
        }
        int marker = p[1];
        if (marker == 0xFF || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            p += marker == 0xFF ? 1 : 2;
            continue;
        }
        if (marker == 0xD9)
            break;
        int n = p[2] << 8 | p[3];
        if (n < 2 || p + 2 + n > d->end) {
            fail(d, "a broken segment");
            goto out;
        }
        if (marker == 0xDA) {
            if (!d->w) {
                fail(d, "a scan before the frame");
                goto out;
            }
            if (scan(d, p + 4, n - 2) < 0)
                goto out;
            scanned = 1;
        } else if (segment(d, marker, p + 4, n - 2) < 0)
            goto out;
        p += 2 + n;
    }
    if (!scanned) {
        fail(d, "no picture data");
        goto out;
    }
    uint8_t *out = malloc((size_t)d->w * d->h * 3);
    if (!out) {
        fail(d, "no memory for the picture");
        goto out;
    }
    for (int y = 0; y < d->h; y++)
        for (int x = 0; x < d->w; x++) {
            float yy, cb = 128, cr = 128;
            const comp_t *c = &d->c[0];
            yy = c->pix[(y * c->v / d->vmax) * c->bw * 8 + x * c->h / d->hmax];
            if (d->nc == 3) {
                c = &d->c[1];
                cb = c->pix[(y * c->v / d->vmax) * c->bw * 8 + x * c->h / d->hmax];
                c = &d->c[2];
                cr = c->pix[(y * c->v / d->vmax) * c->bw * 8 + x * c->h / d->hmax];
            }
            uint8_t *o = out + ((size_t)y * d->w + x) * 3;
            o[0] = clamp(yy + 1.402f * (cr - 128));
            o[1] = clamp(yy - 0.344136f * (cb - 128) - 0.714136f * (cr - 128));
            o[2] = clamp(yy + 1.772f * (cb - 128));
        }
    *rgb = out;
    *pw = d->w;
    *ph = d->h;
    ret = 0;
out:
    for (int i = 0; i < 3; i++)
        free(d->c[i].pix);
    free(d);
    return ret;
}
