/* The WAV files the PC's tools write (tests/audio/render.c: bmrender, make
 * wav; tests/host/bmhost.c --wav): 48 kHz stereo, in one of four depths.
 *
 *   16    PCM, 16-bit integers (the default: the files of the tests)
 *   24    PCM, 24-bit integers, three bytes a sample (little endian)
 *   32    PCM, 32-bit integers
 *   f32   IEEE floats, +-1.0 full scale (format 3)
 *
 * A plain 16-byte fmt chunk (format 1 or 3), as most readers expect for
 * every depth (ffmpeg, sox, Audacity, Python's wave for the integers). */
#ifndef BM_WAVOUT_H
#define BM_WAVOUT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WAV_F32 33                      /* wav_bits(): the code of "f32" */

/* "16", "24", "32", "f32" (or "float") -> 16, 24, 32, WAV_F32; 0 if unknown */
static inline int wav_bits(const char *s)
{
    if (!strcmp(s, "16")) return 16;
    if (!strcmp(s, "24")) return 24;
    if (!strcmp(s, "32")) return 32;
    if (!strcmp(s, "f32") || !strcmp(s, "float")) return WAV_F32;
    return 0;
}

/* the bytes of one sample of the file */
static inline unsigned wav_sample_bytes(int bits)
{
    return bits == WAV_F32 ? 4 : (unsigned)bits / 8;
}

static inline void wav_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> 8 * i);
}

/* the 44-byte header for `frames` stereo frames at `rate`, at the start of f */
static inline void wav_header(FILE *f, uint32_t rate, int bits, uint32_t frames)
{
    const unsigned sb = wav_sample_bytes(bits), align = 2 * sb;
    const uint32_t bytes = frames * align;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    wav_le(h + 4, 36 + bytes, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    wav_le(h + 16, 16, 4);
    wav_le(h + 20, bits == WAV_F32 ? 3 : 1, 2);     /* PCM, or IEEE float */
    wav_le(h + 22, 2, 2);                           /* stereo */
    wav_le(h + 24, rate, 4);
    wav_le(h + 28, rate * align, 4);
    wav_le(h + 32, align, 2);
    wav_le(h + 34, 8 * sb, 2);
    memcpy(h + 36, "data", 4);
    wav_le(h + 40, bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

/* n stereo frames of words aligned to the left (synth_render32's: bit 31 the
 * sign) as 16, 24 or 32-bit samples */
static inline void wav_put_words(FILE *f, const int32_t *w, unsigned n, int bits)
{
    const unsigned sb = wav_sample_bytes(bits);
    uint8_t buf[2 * 64 * 4];
    while (n) {
        unsigned m = n < 64 ? n : 64, k = 0;
        for (unsigned i = 0; i < 2 * m; i++) {
            uint32_t v = (uint32_t)w[i];
            for (unsigned b = 0; b < sb; b++)
                buf[k++] = (uint8_t)(v >> (32 - 8 * sb + 8 * b));
        }
        fwrite(buf, 1, k, f);
        w += 2 * m;
        n -= m;
    }
}

/* n stereo frames of floats as IEEE floats, clamped to +-1.0 */
static inline void wav_put_floats(FILE *f, const float *x, unsigned n)
{
    float buf[2 * 64];
    while (n) {
        unsigned m = n < 64 ? n : 64;
        for (unsigned i = 0; i < 2 * m; i++) {
            float v = x[i];
            buf[i] = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v == v ? v : 0.0f;
        }
        fwrite(buf, 4, 2 * m, f);       /* the PC is little endian */
        x += 2 * m;
        n -= m;
    }
}

#endif
