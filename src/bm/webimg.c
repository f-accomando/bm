/* Pictures from the web to RGB565: see webimg.h. */
#include "webimg.h"

#include "jpeg.h"
#include "png.h"
#include "webp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t en, const char *why)
{
    if (err && en)
        snprintf(err, en, "%s", why);
    return -1;
}

const char *webimg_kind(const uint8_t *d, size_t n)
{
    if (webp_is(d, n))
        return "webp";
    if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
        return "png";
    if (jpeg_is(d, n))
        return "jpeg";
    return NULL;
}

/* The size of a JPEG from its first frame header. 0, or -1; *prog = 1 for a progressive or
 * arithmetic file (jpeg.c refuses them). */
static int jpeg_size(const uint8_t *d, size_t n, int *w, int *h, int *prog)
{
    size_t i = 2;
    *prog = 0;
    while (i + 4 <= n) {
        if (d[i] != 0xFF) {
            i++;
            continue;
        }
        const uint8_t m = d[i + 1];
        if (m == 0xFF) {
            i++;
            continue;
        }
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) {
            i += 2;
            continue;
        }
        if (m == 0xD9 || m == 0xDA)
            return -1;
        const size_t seg = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (seg < 8 || i + 9 > n)
                return -1;
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            *prog = m != 0xC0 && m != 0xC1;
            return 0;
        }
        i += 2 + seg;
    }
    return -1;
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

void webimg_free(webimg_t *im)
{
    free(im->px);
    free(im->alpha);
    memset(im, 0, sizeof *im);
}

int webimg_decode(const uint8_t *d, size_t n, int maxw, int maxh, webimg_t *out, char *err, size_t en)
{
    memset(out, 0, sizeof *out);
    const char *kind = webimg_kind(d, n);
    if (!kind)
        return fail(err, en, "not a JPEG, PNG or WebP picture");
    int w = 0, h = 0;
    if (kind[0] == 'w') {
        if (webp_info(d, n, &w, &h) != 0)
            return fail(err, en, "a broken or unsupported WebP");
    } else if (kind[0] == 'p') {
        if (n < 24 || memcmp(d + 12, "IHDR", 4) != 0)
            return fail(err, en, "a broken PNG");
        const uint32_t pw = (uint32_t)d[16] << 24 | d[17] << 16 | d[18] << 8 | d[19];
        const uint32_t ph = (uint32_t)d[20] << 24 | d[21] << 16 | d[22] << 8 | d[23];
        if (pw > 65535 || ph > 65535)
            return fail(err, en, "the picture is too big");
        w = (int)pw;
        h = (int)ph;
    } else {
        int prog;
        if (jpeg_size(d, n, &w, &h, &prog) != 0)
            return fail(err, en, "a broken JPEG");
        if (prog)
            return fail(err, en, "a progressive JPEG is not supported");
    }
    if (w <= 0 || h <= 0)
        return fail(err, en, "an empty picture");
    if ((uint64_t)w * (uint64_t)h > WEBIMG_MAX_PIXELS)
        return fail(err, en, "the picture is over 4 megapixels");
    const int cw = maxw > 0 && maxw < w ? maxw : w, ch = maxh > 0 && maxh < h ? maxh : h;
    uint16_t *px = malloc((size_t)cw * (size_t)ch * 2);
    uint8_t *alpha = malloc((size_t)cw * (size_t)ch);
    if (!px || !alpha) {
        free(px);
        free(alpha);
        return fail(err, en, "not enough memory for the picture");
    }
    memset(alpha, 1, (size_t)cw * (size_t)ch);
    if (kind[0] == 'w') {
        if (webp_decode565(d, n, px, cw, cw, ch) != 0) {
            free(px);
            free(alpha);
            return fail(err, en, "a broken WebP");
        }
    } else if (kind[0] == 'p') {
        uint8_t *rgba = NULL;
        int pw, ph;
        if (png_rgba(d, n, &rgba, &pw, &ph) != 0 || pw != w || ph != h) {
            free(rgba);
            free(px);
            free(alpha);
            return fail(err, en, "a broken or unsupported PNG");
        }
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
                px[(size_t)y * (size_t)cw + (size_t)x] = rgb565(p[0], p[1], p[2]);
                alpha[(size_t)y * (size_t)cw + (size_t)x] = p[3] >= 128;
            }
        free(rgba);
    } else {
        uint8_t *rgb = NULL;
        int jw, jh;
        char why[64];
        if (jpeg_decode(d, n, &rgb, &jw, &jh, why, sizeof why) != 0 || jw != w || jh != h) {
            free(rgb);
            free(px);
            free(alpha);
            return fail(err, en, "a broken or unsupported JPEG");
        }
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                const uint8_t *p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
                px[(size_t)y * (size_t)cw + (size_t)x] = rgb565(p[0], p[1], p[2]);
            }
        free(rgb);
    }
    out->w = cw;
    out->h = ch;
    out->px = px;
    out->alpha = alpha;
    return 0;
}
