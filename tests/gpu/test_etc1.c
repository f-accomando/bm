/*
 * The ETC1 blocks of src/gpu/etc1.c (M39, bm3d 7.3): the decoder against
 * blocks written by hand from the format's bit layout, the encoder's
 * blocks decoded back against their RGB565 texels (PSNR and the worst
 * channel), the same bits every time, and its speed.
 *
 *   test_etc1
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gpu/etc1.h"

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

static uint32_t rgb_of(uint16_t c)
{
    const uint32_t r = c >> 11, g = c >> 5 & 63, b = c & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

/* an image w x h encoded and decoded: its PSNR (dB) and worst channel */
static double round_trip(const uint16_t *img, int w, int h, int swap, int *worst, uint64_t *hash)
{
    double se = 0;
    *worst = 0;
    uint64_t hs = 1469598103934665603ull;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            const uint64_t b = etc1_encode(img + by * w + bx, w, swap);
            hs = (hs ^ b) * 1099511628211ull;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    const uint32_t want = rgb_of(img[(by + y) * w + bx + x]), got = etc1_texel(b, x, y);
                    for (int k = 0; k < 24; k += 8) {
                        const int kk = swap ? (k == 0 ? 16 : k == 16 ? 0 : 8) : k;
                        const int d = (int)(want >> kk & 255) - (int)(got >> k & 255);
                        se += (double)d * d;
                        if (abs(d) > *worst)
                            *worst = abs(d);
                    }
                }
        }
    *hash = hs;
    const double mse = se / ((double)w * h * 3);
    return mse <= 0 ? 99.0 : 10.0 * log10(255.0 * 255.0 / mse);
}

int main(void)
{
    /* the decoder, from the bit layout: individual mode, halves left and
     * right; left grey 8 (136) table 0 index 0 (+2), right 0 table 7 index 3
     * (-183, clamped to 0) */
    {
        uint64_t b = (uint64_t)8 << 60 | (uint64_t)8 << 52 | (uint64_t)8 << 44 | (uint64_t)7 << 34;
        for (int x = 2; x < 4; x++)
            for (int y = 0; y < 4; y++)
                b |= (uint64_t)1 << (16 + x * 4 + y) | (uint64_t)1 << (x * 4 + y);
        CHECK(etc1_texel(b, 0, 0) == 0x8A8A8A && etc1_texel(b, 1, 3) == 0x8A8A8A, "individual, left: %06x",
              etc1_texel(b, 0, 0));
        CHECK(etc1_texel(b, 2, 0) == 0 && etc1_texel(b, 3, 3) == 0, "individual, right: %06x", etc1_texel(b, 3, 3));
        /* index 1 (+large) on texel (1, 2) only: bit 1 * 4 + 2 of the low plane */
        b |= (uint64_t)1 << 6;
        CHECK(etc1_texel(b, 1, 2) == 0x909090 && etc1_texel(b, 1, 1) == 0x8A8A8A, "individual, +8: %06x",
              etc1_texel(b, 1, 2));
    }
    /* differential, halves top and bottom: red 16 (132) and 16 - 1 (123),
     * green 31 (255) + 0, blue 0 + 3 (3 -> 24); table 1 top (+5), 2
     * bottom, index 2 (-9) */
    {
        uint64_t b = (uint64_t)16 << 59 | (uint64_t)7 << 56 | (uint64_t)31 << 51 | (uint64_t)0 << 48 |
                     (uint64_t)0 << 43 | (uint64_t)3 << 40 | (uint64_t)1 << 37 | (uint64_t)2 << 34 |
                     (uint64_t)1 << 33 | (uint64_t)1 << 32;
        for (int x = 0; x < 4; x++)
            for (int y = 2; y < 4; y++)
                b |= (uint64_t)1 << (16 + x * 4 + y);
        CHECK(etc1_texel(b, 3, 1) == (uint32_t)(137 << 16 | 255 << 8 | 5), "differential, top: %06x",
              etc1_texel(b, 3, 1));
        CHECK(etc1_texel(b, 0, 2) == (uint32_t)(114 << 16 | 246 << 8 | 15), "differential, bottom: %06x",
              etc1_texel(b, 0, 2));
    }

    enum { W = 256, H = 256 };
    static uint16_t img[W * H];
    int worst;
    uint64_t hash, hash2;
    /* flat blocks: every 4x4 one colour (a sheet's large areas) */
    for (int i = 0; i < W * H; i++) {
        const int bl = (i / W / 4) * (W / 4) + (i % W) / 4;
        img[i] = (uint16_t)(bl * 2654435761u >> 16);
    }
    double p = round_trip(img, W, H, 0, &worst, &hash);
    printf("  flat blocks: PSNR %.1f dB, worst channel %d\n", p, worst);
    CHECK(p >= 40 && worst <= 12, "flat blocks: PSNR %.1f dB, worst %d", p, worst);
    /* smooth gradients */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = rgb565(x, y, (x + y) / 2);
    p = round_trip(img, W, H, 0, &worst, &hash);
    printf("  gradients: PSNR %.1f dB, worst channel %d\n", p, worst);
    CHECK(p >= 36 && worst <= 16, "gradients: PSNR %.1f dB, worst %d", p, worst);
    /* 8x8 checkers of two colours over a gradient (the 3D Bench's sheets) */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = ((x >> 3) ^ (y >> 3)) & 1 ? rgb565(0x20 + x / 4, 0x60, 0xD0) : rgb565(0xF0, 0xD0, 0x40 + y / 4);
    p = round_trip(img, W, H, 0, &worst, &hash);
    double p2 = round_trip(img, W, H, 1, &worst, &hash2);
    printf("  checkers: PSNR %.1f dB (red and blue swapped %.1f), worst channel %d\n", p, p2, worst);
    CHECK(p >= 34 && p2 >= 34, "checkers: PSNR %.1f / %.1f dB", p, p2);
    /* noise: the worst case (no two texels alike), only a floor */
    uint32_t seed = 12345;
    for (int i = 0; i < W * H; i++) {
        seed = seed * 1664525u + 1013904223u;
        img[i] = (uint16_t)(seed >> 16);
    }
    p = round_trip(img, W, H, 0, &worst, &hash);
    round_trip(img, W, H, 0, &worst, &hash2);
    printf("  noise: PSNR %.1f dB, worst channel %d\n", p, worst);
    CHECK(p >= 10, "noise: PSNR %.1f dB", p);
    CHECK(hash == hash2, "the same texels gave other bits");
    /* both modes and both splits come out */
    int modes[2] = { 0, 0 }, flips[2] = { 0, 0 };
    for (int by = 0; by < H; by += 4)
        for (int bx = 0; bx < W; bx += 4) {
            const uint64_t b = etc1_encode(img + by * W + bx, W, 0);
            modes[b >> 33 & 1]++;
            flips[b >> 32 & 1]++;
        }
    CHECK(modes[0] && modes[1] && flips[0] && flips[1], "modes %d/%d, splits %d/%d", modes[0], modes[1], flips[0],
          flips[1]);
    /* speed: a 1024x1024 sheet (65536 blocks) on the PC, for the record:
     * noise (no block alike), then 8x8 checkers (flat blocks) */
    for (int kind = 0; kind < 2; kind++) {
        static uint16_t big[1024 * 1024];
        for (int i = 0; i < 1024 * 1024; i++)
            big[i] = kind ? (((i % 1024) >> 3 ^ (i / 1024) >> 3) & 1 ? 0x1234 : 0xF7A0) : img[i % (W * H)];
        const clock_t t0 = clock();
        uint64_t acc = 0;
        for (int by = 0; by < 1024; by += 4)
            for (int bx = 0; bx < 1024; bx += 4)
                acc += etc1_encode(big + by * 1024 + bx, 1024, 0);
        printf("  1024x1024 %s: %.1f ms on this PC (%llx)\n", kind ? "checkers" : "noise",
               (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC, (unsigned long long)(acc & 0xFFFF));
    }
    printf("etc1: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
