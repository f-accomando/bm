/*
 * Host tool for the WebP decoder (src/bm/webp.c).
 *   test_webp info FILE          prints "W H"
 *   test_webp dec FILE OUT       writes the picture as raw RGB565 (little endian)
 *   test_webp crop FILE          decodes a cut of the picture into a padded buffer and checks it
 *   test_webp fuzz FILE [N]      truncations and bit flips of the file: no crash, no read past the end
 * Build with -fsanitize=address,undefined so the fuzz mode finds the bad reads.
 */
#include "webp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static uint32_t rng_state = 12345;

static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* decodes a copy of exactly len bytes, so the address sanitizer sees a read past the end */
static int try_decode(const uint8_t *src, size_t len, uint16_t *out, int w, int h)
{
    uint8_t *tight = (uint8_t *)malloc(len ? len : 1);
    int r, iw, ih;

    memcpy(tight, src, len);
    if (webp_info(tight, len, &iw, &ih) == 0 && (iw > w || ih > h))
        r = -1;                                 /* a flipped size: the output is only w x h */
    else
        r = webp_decode565(tight, len, out, w, w, h);
    free(tight);
    return r;
}

static int do_dec(const uint8_t *buf, size_t len, int w, int h, const char *path)
{
    uint16_t *px = (uint16_t *)calloc((size_t)w * (size_t)h, 2);
    FILE *f;
    int i, rc = 0;

    if (webp_decode565(buf, len, px, w, w, h) < 0) {
        puts("decode failed");
        rc = 1;
    } else if (!(f = fopen(path, "wb"))) {
        rc = 2;
    } else {
        for (i = 0; i < w * h; i++) {
            fputc(px[i] & 255, f);
            fputc(px[i] >> 8, f);
        }
        fclose(f);
        printf("ok %d %d\n", w, h);
    }
    free(px);
    return rc;
}

static int do_crop(const uint8_t *buf, size_t len, int w, int h)
{
    /* the cut is smaller than the picture; the stride is wider than the cut and the margin keeps a mark */
    int cw = w > 5 ? w / 2 + 1 : w, ch = h > 5 ? h / 3 + 1 : h, stride = cw + 7, bad = 0, x, y;
    uint16_t *full = (uint16_t *)calloc((size_t)w * (size_t)h, 2);
    uint16_t *cut = (uint16_t *)malloc((size_t)stride * (size_t)ch * 2);

    for (x = 0; x < stride * ch; x++)
        cut[x] = 0xABCD;
    if (webp_decode565(buf, len, full, w, w, h) < 0 || webp_decode565(buf, len, cut, stride, cw, ch) < 0) {
        puts("decode failed");
        free(full);
        free(cut);
        return 1;
    }
    for (y = 0; y < ch; y++)
        for (x = 0; x < stride; x++) {
            uint16_t v = cut[y * stride + x];
            if (x < cw ? v != full[y * w + x] : v != 0xABCD)
                bad++;
        }
    free(full);
    free(cut);
    printf("%s %d %d\n", bad ? "crop-bad" : "crop-ok", cw, ch);
    return bad != 0;
}

static int do_fuzz(const uint8_t *buf, size_t len, int w, int h, int n)
{
    int ok = 0, rejected = 0, i;
    size_t cut;
    uint16_t *px = (uint16_t *)calloc((size_t)w * (size_t)h + 16, 2);
    uint8_t *tmp = (uint8_t *)malloc(len);

    for (cut = 0; cut < len; cut += (len / 64) + 1) {                  /* truncations */
        if (try_decode(buf, cut, px, w, h) < 0)
            rejected++;
        else
            ok++;
    }
    for (i = 0; i < n; i++) {                                           /* flipped bits */
        int flips = 1 + (int)(rnd() % 4);
        memcpy(tmp, buf, len);
        while (flips--)
            tmp[rnd() % len] ^= (uint8_t)(1u << (rnd() & 7));
        if (try_decode(tmp, len, px, w, h) < 0)
            rejected++;
        else
            ok++;
    }
    for (i = 0; i < n / 4; i++) {                                       /* a damaged start */
        int k;
        memcpy(tmp, buf, len);
        for (k = 0; k < 4 && len > 40; k++)
            tmp[12 + rnd() % 30] = (uint8_t)rnd();
        if (try_decode(tmp, len, px, w, h) < 0)
            rejected++;
        else
            ok++;
    }
    free(px);
    free(tmp);
    printf("fuzz-ok decoded=%d rejected=%d\n", ok, rejected);
    return 0;
}

static int tool(int argc, char **argv, const uint8_t *buf, size_t len)
{
    int w, h;

    if (webp_info(buf, len, &w, &h) < 0) {
        puts("bad");
        return 1;
    }
    if (!strcmp(argv[1], "info")) {
        printf("%d %d\n", w, h);
        return 0;
    }
    if (!strcmp(argv[1], "dec") && argc >= 4)
        return do_dec(buf, len, w, h, argv[3]);
    if (!strcmp(argv[1], "crop"))
        return do_crop(buf, len, w, h);
    if (!strcmp(argv[1], "fuzz"))
        return do_fuzz(buf, len, w, h, argc >= 4 ? atoi(argv[3]) : 300);
    return 2;
}

int main(int argc, char **argv)
{
    size_t len;
    uint8_t *buf;
    int rc;

    if (argc < 3)
        return 2;
    buf = slurp(argv[2], &len);
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    rc = tool(argc, argv, buf, len);
    free(buf);
    return rc;
}
