/*
 * Host test of the .bmv reader (src/bm/vid.c) and of jpeg_decode565.
 *   test_vid dump DIR     writes DIR/f0..f2.jpg (the Video bench's 320x180 frame) and DIR/a.raw
 *   test_vid rle          the RLE565 codec of the recorder: round trips, hostile streams
 *   test_vid raw          the raw codecs of the recorder, built in memory
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

/* the recorder's raw codecs: 16x4, 2 frames, built here */
static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static int raw(void)
{
    char err[80];
    for (int codec = BMV_CODEC_RAW565; codec <= BMV_CODEC_RGB332; codec++) {
        const uint32_t bpp = codec == BMV_CODEC_RAW565 ? 2 : 1, fb = 16 * 4 * bpp;
        uint8_t buf[BMV_HEADER + 2 * 128 + 16];
        memset(buf, 0, sizeof buf);
        memcpy(buf, "BMV1", 4);
        buf[4] = 1;
        buf[5] = (uint8_t)codec;
        buf[8] = 16;
        buf[10] = 4;
        buf[12] = 15;
        w32(buf + 16, 2);
        w32(buf + 20, BMV_HEADER + 2 * fb);
        for (int k = 0; k < 2; k++) {
            w32(buf + BMV_HEADER + 2 * fb + k * 8, BMV_HEADER + (uint32_t)k * fb);
            w32(buf + BMV_HEADER + 2 * fb + k * 8 + 4, fb);
        }
        const size_t len = BMV_HEADER + 2 * fb + 16;
        if (codec == BMV_CODEC_RAW565)
            for (int k = 0; k < 64; k++) {              /* frame 0: 0x1234 + k; frame 1: all 0xF800 */
                buf[BMV_HEADER + 2 * k] = (uint8_t)(0x1234 + k);
                buf[BMV_HEADER + 2 * k + 1] = (uint8_t)((0x1234 + k) >> 8);
                buf[BMV_HEADER + fb + 2 * k] = 0x00;
                buf[BMV_HEADER + fb + 2 * k + 1] = 0xF8;
            }
        else
            for (int k = 0; k < 64; k++) {              /* frame 0: k * 4; frame 1: 0xE0 (red) */
                buf[BMV_HEADER + k] = (uint8_t)(k * 4);
                buf[BMV_HEADER + fb + k] = 0xE0;
            }
        bmv_t v;
        CHECK(bmv_open(&v, buf, len, err, sizeof err) == 0);
        if (fails)
            return 1;
        uint16_t px[64];
        CHECK(bmv_decode(&v, 0, px, err, sizeof err) == 0);
        if (codec == BMV_CODEC_RAW565) {
            CHECK(px[0] == 0x1234 && px[63] == 0x1234 + 63);
        } else {                                        /* 0xE0 -> r 7 g 0 b 0 -> 0xF800; k*4 = 0: black */
            CHECK(px[0] == 0 && px[1] == 0x0120 && px[63] == 0xFFE0);
        }
        CHECK(bmv_decode(&v, 1, px, err, sizeof err) == 0 && px[0] == 0xF800 && px[63] == 0xF800);
        /* a raw frame of the wrong size is refused */
        w32(buf + BMV_HEADER + 2 * fb + 4, fb - 1);
        CHECK(bmv_open(&v, buf, len, err, sizeof err) != 0);
    }
    if (!fails)
        printf("test_vid raw: all passed\n");
    return fails != 0;
}

/* the RLE565 codec of the recorder: encode, decode, bit exact; hostile streams */
static uint32_t rng_state = 7;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void round_trip(const char *what, const uint16_t *px, int w, int h)
{
    uint8_t *enc = malloc(BMV_RLE565_MAX(w, h));
    uint16_t *back = malloc((size_t)w * (size_t)h * 2);
    const size_t n = bmv_rle565_encode(enc, px, w, h);
    int ok = n >= 1 && n <= BMV_RLE565_MAX(w, h) && bmv_rle565_decode(enc, n, back, w, h) == 0 &&
             !memcmp(back, px, (size_t)w * (size_t)h * 2);
    if (!ok)
        printf("round trip of %s (%dx%d) failed\n", what, w, h);
    CHECK(ok);
    /* a cut stream and a stream with a byte too many are refused */
    CHECK(bmv_rle565_decode(enc, n - 1, back, w, h) != 0);
    enc[n] = 0;
    CHECK(bmv_rle565_decode(enc, n + 1, back, w, h) != 0);
    free(enc);
    free(back);
}

static int rle(void)
{
    char err[80];
    static const int sizes[][2] = { { 1, 1 }, { 1, 5 }, { 3, 2 }, { 63, 3 }, { 64, 3 }, { 65, 3 }, { 129, 4 },
                                    { 320, 180 }, { 16, 16 } };
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const int w = sizes[k][0], h = sizes[k][1];
        uint16_t *px = malloc((size_t)w * (size_t)h * 2);
        const size_t cnt = (size_t)w * (size_t)h;
        for (size_t i = 0; i < cnt; i++)                    /* flat */
            px[i] = 0x1234;
        round_trip("flat", px, w, h);
        for (size_t i = 0; i < cnt; i++)                    /* noise: the worst case, nothing repeats by chance */
            px[i] = (uint16_t)rnd();
        round_trip("noise", px, w, h);
        for (size_t i = 0; i < cnt; i++)                    /* a few colours: runs and copies of every length */
            px[i] = (uint16_t)(rnd() % 3 ? 0xF800 : rnd() % 4);
        round_trip("few colours", px, w, h);
        for (size_t i = cnt > (size_t)w ? (size_t)w : 0; i < cnt; i++)  /* the row above, with changes */
            px[i] = rnd() % 5 ? px[i - (cnt > (size_t)w ? (size_t)w : 0)] : (uint16_t)rnd();
        round_trip("rows like the row above", px, w, h);
        if (w >= 4 && h >= 2) {
            vidbench_synth_frame(px, w, h);
            round_trip("test frame", px, w, h);
        }
        free(px);
    }
    {   /* sizes: noise is at most the bound (and the bound is reached by literals); flat is tiny */
        uint16_t *px = malloc(320 * 180 * 2);
        uint8_t *enc = malloc(BMV_RLE565_MAX(320, 180));
        for (int i = 0; i < 320 * 180; i++)
            px[i] = (uint16_t)(i * 7 + (rnd() | 1));
        CHECK(bmv_rle565_encode(enc, px, 320, 180) <= BMV_RLE565_MAX(320, 180));
        CHECK(BMV_RLE565_MAX(320, 180) == 180u * (640u + 5u));
        memset(px, 0, 320 * 180 * 2);
        const size_t flat = bmv_rle565_encode(enc, px, 320, 180);
        CHECK(flat == 3 * 5 + 179 * 5);     /* row 0: five repeats of 64 pixels; the others: five copies */
        vidbench_synth_frame(px, 320, 180);
        const size_t art = bmv_rle565_encode(enc, px, 320, 180);
        printf("test frame 320x180: %zu of 115200 bytes\n", art);
        CHECK(art < 115200 / 2);
        free(px);
        free(enc);
    }
    /* hostile streams: type 3, copy above in row 0, a run past the end, a literal cut short */
    {
        uint16_t out[16];
        const uint8_t t3[] = { 0xC0 }, up0[] = { 0x80 }, over[] = { 0x40 | 31, 1, 2 }, cut[] = { 0x03, 1, 2, 3 },
                      ok[] = { 0x40 | 15, 1, 2 };
        CHECK(bmv_rle565_decode(t3, sizeof t3, out, 4, 4) != 0);
        CHECK(bmv_rle565_decode(up0, sizeof up0, out, 4, 4) != 0);
        CHECK(bmv_rle565_decode(over, sizeof over, out, 4, 4) != 0);
        CHECK(bmv_rle565_decode(cut, sizeof cut, out, 4, 4) != 0);
        CHECK(bmv_rle565_decode(ok, sizeof ok, out, 4, 4) == 0 && out[0] == 0x0201 && out[15] == 0x0201);
        CHECK(bmv_rle565_decode(ok, 0, out, 4, 4) != 0);
        /* random bytes never crash, and a stream that decodes uses every byte */
        for (int k = 0; k < 20000; k++) {
            uint8_t junk[40];
            const size_t n = rnd() % 40;
            for (size_t i = 0; i < n; i++)
                junk[i] = (uint8_t)rnd();
            (void)bmv_rle565_decode(junk, n, out, 4, 4);
        }
    }
    /* a clip of codec 4 in memory: 8x4, 3 frames of different sizes; open, decode, refusals */
    {
        uint16_t f[3][32];
        uint8_t buf[BMV_HEADER + 3 * 80 + 3 * 8], *w = buf + BMV_HEADER;
        uint32_t off[3], sz[3];
        for (int i = 0; i < 32; i++) {
            f[0][i] = 0xAAAA;                               /* flat */
            f[1][i] = (uint16_t)(i * 1111);                 /* all different */
            f[2][i] = i < 8 ? 0x07E0 : f[1][i];             /* one row, then copies of nothing */
        }
        memset(buf, 0, sizeof buf);
        for (int i = 0; i < 3; i++) {
            off[i] = (uint32_t)(w - buf);
            sz[i] = (uint32_t)bmv_rle565_encode(w, f[i], 8, 4);
            w += sz[i];
        }
        memcpy(buf, "BMV1", 4);
        buf[4] = 1;
        buf[5] = BMV_CODEC_RLE565;
        buf[8] = 8;
        buf[10] = 4;
        buf[12] = 30;
        w32(buf + 16, 3);
        w32(buf + 20, (uint32_t)(w - buf));
        for (int i = 0; i < 3; i++) {
            w32(w + i * 8, off[i]);
            w32(w + i * 8 + 4, sz[i]);
        }
        const size_t len = (size_t)(w - buf) + 24;
        bmv_t v;
        CHECK(bmv_open(&v, buf, len, err, sizeof err) == 0);
        if (fails)
            return 1;
        uint16_t px[32];
        for (uint32_t i = 0; i < 3; i++) {
            memset(px, 0, sizeof px);
            CHECK(bmv_decode(&v, i, px, err, sizeof err) == 0 && !memcmp(px, f[i], sizeof px));
        }
        w32(w + 4, 2);                                      /* 2 bytes cannot be a frame */
        CHECK(bmv_open(&v, buf, len, err, sizeof err) != 0);
        w32(w + 4, (uint32_t)BMV_RLE565_MAX(8, 4) + 1);     /* larger than the worst case */
        CHECK(bmv_open(&v, buf, len, err, sizeof err) != 0);
        w32(w + 4, sz[0]);
        buf[BMV_HEADER] = 0xC0;                             /* opens (the tokens are checked when it is decoded) */
        CHECK(bmv_open(&v, buf, len, err, sizeof err) == 0 && bmv_decode(&v, 0, px, err, sizeof err) != 0);
    }
    if (!fails)
        printf("test_vid rle: all passed\n");
    return fails != 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "rle"))
        return rle();
    if (argc == 2 && !strcmp(argv[1], "raw"))
        return raw();
    if (argc == 3 && !strcmp(argv[1], "dump"))
        return dump(argv[2]);
    if (argc == 5 && !strcmp(argv[1], "check"))
        return check(argv[2], atoi(argv[3]), atoi(argv[4]));
    fprintf(stderr, "usage: test_vid dump DIR | raw | rle | check FILE CHANNELS BITS\n");
    return 2;
}
