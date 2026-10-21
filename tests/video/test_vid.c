/*
 * Host test of the .bmv reader (src/bm/vid.c) and of jpeg_decode565.
 *   test_vid dump DIR     writes DIR/f0..f2.jpg (the Video bench's 320x180 frame) and DIR/a.raw
 *   test_vid check FILE CHANNELS BITS   reads a clip made by scripts/mkvid.py from those files
 * run_vid_test.py runs both with mkvid.py in between.
 */
#include "bm/vid.h"
#include "bm/jpeg.h"
#include "bm/vidbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(c)                                                                        \
    do {                                                                                \
        if (!(c)) {                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                         \
            fails++;                                                                    \
        }                                                                               \
    } while (0)

#define AUDIO_FRAMES 11025                  /* 0.5 s of 22050 Hz mono, 16-bit */

/* frame i: the bench frame with a comment segment "Fi" after the SOI, so that every frame has its own bytes
 * (the decoder skips comments); returns the length, out has room for the bench frame + 6 */
static size_t make_frame(int i, uint8_t *out)
{
    out[0] = vidbench_jpg320[0];
    out[1] = vidbench_jpg320[1];
    out[2] = 0xFF;
    out[3] = 0xFE;
    out[4] = 0;
    out[5] = 4;
    out[6] = 'F';
    out[7] = (uint8_t)('0' + i);
    memcpy(out + 8, vidbench_jpg320 + 2, vidbench_jpg320_len - 2);
    return vidbench_jpg320_len + 6;
}

static int dump(const char *dir)
{
    char path[512];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/f%d.jpg", dir, i);
        FILE *f = fopen(path, "wb");
        if (!f)
            return 1;
        uint8_t *fr = malloc(vidbench_jpg320_len + 6);
        fwrite(fr, 1, make_frame(i, fr), f);
        free(fr);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/a.raw", dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    for (int i = 0; i < AUDIO_FRAMES; i++) {
        const int16_t s = (int16_t)(i & 0x7FFF);
        fputc(s & 0xFF, f);
        fputc(s >> 8 & 0xFF, f);
    }
    fclose(f);
    return 0;
}

static uint16_t to565(const uint8_t *rgb)
{
    return (uint16_t)((rgb[0] >> 3) << 11 | (rgb[1] >> 2) << 5 | (rgb[2] >> 3));
}

static int check(const char *file, int channels, int bits)
{
    FILE *f = fopen(file, "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)len);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len)
        return 1;
    fclose(f);

    char err[80];
    bmv_t v;
    CHECK(bmv_open(&v, buf, (size_t)len, err, sizeof err) == 0);
    if (fails)
        return 1;
    CHECK(v.frames == 3 && v.width == 320 && v.height == 180 && v.fps == 15);
    CHECK(v.audio_channels == channels && v.audio_bits == bits && v.audio_rate == 22050);
    const int sf = channels * bits / 8;         /* a.raw is 22050 bytes: 11025 frames of 16-bit mono or 8-bit stereo */
    CHECK(v.audio_len == AUDIO_FRAMES * 2 && AUDIO_FRAMES * 2 / sf == 11025);
    CHECK(bmv_duration_ms(&v) == 200);
    CHECK(bmv_frame_at(&v, 0) == 0 && bmv_frame_at(&v, 67) == 1 && bmv_frame_at(&v, 133) == 1 &&
          bmv_frame_at(&v, 134) == 2 && bmv_frame_at(&v, 99999) == 2);

    /* the frames are what was packed, and decode to what jpeg_decode gives */
    uint16_t *px = malloc(320 * 180 * 2);
    uint8_t *rgb;
    int w, h;
    CHECK(jpeg_decode(vidbench_jpg320, vidbench_jpg320_len, &rgb, &w, &h, err, sizeof err) == 0);
    for (uint32_t i = 0; i < 3; i++) {
        const uint8_t *p;
        size_t n;
        uint8_t *want = malloc(vidbench_jpg320_len + 6);
        const size_t wn = make_frame((int)i, want);
        CHECK(bmv_frame(&v, i, &p, &n) == 0 && n == wn && !memcmp(p, want, n));
        free(want);
        memset(px, 0xAA, 320 * 180 * 2);
        CHECK(bmv_decode(&v, i, px, err, sizeof err) == 0);
        int diff = 0;
        for (int k = 0; k < 320 * 180; k++)
            diff += px[k] != to565(rgb + (size_t)k * 3);
        CHECK(diff == 0);
    }
    {
        const uint8_t *p;
        size_t n;
        CHECK(bmv_frame(&v, 3, &p, &n) != 0);
    }
    CHECK(bmv_decode(&v, 3, px, err, sizeof err) != 0);

    /* audio: the span of the second frame, then the edges */
    const uint8_t *a = NULL;
    CHECK(bmv_audio_span(&v, 0, 1000, &a) == AUDIO_FRAMES * 2 && a == v.audio);
    CHECK(bmv_audio_span(&v, 100, 200, &a) == (size_t)(2205 * sf) && a == v.audio + 2205 * sf);
    CHECK(bmv_audio_span(&v, 200, 100, &a) == 0 && bmv_audio_span(&v, 5000, 6000, &a) == 0);

    /* a smaller buffer refuses the picture */
    CHECK(jpeg_decode565(vidbench_jpg320, vidbench_jpg320_len, px, 319, 180, &w, &h, err, sizeof err) != 0);

    /* every cut of the file, and a flipped byte in every field of the header and the first index entry,
     * ends in a clean refusal or a clean open, never a crash */
    for (long n = 0; n < len; n += (n < 200 ? 1 : 37)) {
        bmv_t t;
        CHECK(bmv_open(&t, buf, (size_t)n, NULL, 0) != 0);   /* the index and the audio are at the end */
    }
    {
        bmv_t t;
        CHECK(bmv_open(&t, buf, (size_t)len - 1, NULL, 0) != 0);
    }
    uint8_t *copy = malloc((size_t)len);
    for (int pos = 0; pos < BMV_HEADER; pos++)
        for (int bit = 0; bit < 8; bit++) {
            bmv_t t;
            memcpy(copy, buf, (size_t)len);
            copy[pos] ^= (uint8_t)(1 << bit);
            if (bmv_open(&t, copy, (size_t)len, NULL, 0) == 0) {
                /* accepted: then every frame it names must lie inside the file */
                for (uint32_t i = 0; i < t.frames && i < 4; i++) {
                    const uint8_t *p;
                    size_t n;
                    CHECK(bmv_frame(&t, i, &p, &n) == 0 && p >= copy && p + n <= copy + len);
                }
            }
        }
    const long idx = (long)(v.index - buf);
    for (int field = 0; field < 2; field++) {
        bmv_t t;
        memcpy(copy, buf, (size_t)len);
        memset(copy + idx + field * 4, 0xFF, 4);            /* offset or size 0xFFFFFFFF: no overflow */
        CHECK(bmv_open(&t, copy, (size_t)len, err, sizeof err) != 0);
    }
    /* hostile audio fields: a size that is not a whole number of samples, a start or an end outside the file */
    struct { int pos; uint32_t val; } hostile[] = {
        { 28, (uint32_t)v.audio_len + 1 },          /* not a whole number of sample frames (sf is 2) */
        { 24, (uint32_t)len },                      /* audio_off at the end of the file */
        { 24, 0xFFFFFFFFu },
        { 28, 0xFFFFFFFFu },
        { 24, 4 },                                  /* inside the header */
    };
    for (size_t k = 0; k < sizeof hostile / sizeof hostile[0]; k++) {
        bmv_t t;
        memcpy(copy, buf, (size_t)len);
        memcpy(copy + hostile[k].pos, &hostile[k].val, 4);   /* little-endian host */
        CHECK(bmv_open(&t, copy, (size_t)len, err, sizeof err) != 0);
    }
    free(copy);
    free(px);
    free(rgb);
    free(buf);
    if (!fails)
        printf("test_vid: all passed\n");
    return fails != 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "dump"))
        return dump(argv[2]);
    if (argc == 5 && !strcmp(argv[1], "check"))
        return check(argv[2], atoi(argv[3]), atoi(argv[4]));
    fprintf(stderr, "usage: test_vid dump DIR | check FILE CHANNELS BITS\n");
    return 2;
}
