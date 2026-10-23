/*
 * The menu's background (src/kernel/menu_bg.c) on the PC.
 *
 * The bug (2026-10-23, the user): in the menu the mouse pointer jumped when
 * it passed over the covers. Each new selection made the whole 640x360
 * background in the frame (230,000 pixels of float math: tens of ms on the
 * Pi Zero W, 3 frames or more), so the motion of those frames came in one
 * step. The background is now made a band of rows per frame.
 *
 * Checks: one band never writes more rows than asked (the rows after it stay
 * untouched), the bands make the same picture as one call, a new cover in the
 * middle restarts the picture, and the picture is close to the old float
 * routine (kept here as the reference). The speed is printed, not checked.
 */
#include "kernel/menu_bg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SW 640
#define SH 360
#define BAND 24                     /* rows per frame in menu_ui.c: keep equal */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const uint8_t bayer[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static uint32_t to24(uint16_t c)
{
    uint32_t r = c >> 11, g = c >> 5 & 0x3F, b = c & 0x1F;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

/* the routine of menu_ui.c before the fix, as it was */
static void reference(uint16_t *out, const uint16_t *cover, int w, int h)
{
    enum { LW = 16, LH = 10 };
    float lo[LH][LW][3];
    for (int j = 0; j < LH; j++)
        for (int i = 0; i < LW; i++) {
            uint32_t r = 0, gg = 0, b = 0;
            int x0 = i * w / LW, x1 = (i + 1) * w / LW, y0 = j * h / LH, y1 = (j + 1) * h / LH;
            uint32_t n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++, n++) {
                    uint32_t c = to24(cover[y * w + x]);
                    r += c >> 16; gg += c >> 8 & 255; b += c & 255;
                }
            r /= n; gg /= n; b /= n;
            lo[j][i][0] = (float)r; lo[j][i][1] = (float)gg; lo[j][i][2] = (float)b;
        }
    for (int y = 0; y < SH; y++) {
        float v = (y + 0.5f) * LH / SH - 0.5f;
        int j0 = (int)floorf(v);
        float fy = v - j0;
        int ja = j0 < 0 ? 0 : j0, jb = j0 + 1 >= LH ? LH - 1 : j0 + 1;
        float dark = 0.50f - 0.22f * (float)y / SH;
        for (int x = 0; x < SW; x++) {
            float u = (x + 0.5f) * LW / SW - 0.5f;
            int i0 = (int)floorf(u);
            float fx = u - i0;
            int ia = i0 < 0 ? 0 : i0, ib = i0 + 1 >= LW ? LW - 1 : i0 + 1;
            float edge = 1.0f - 0.25f * fabsf((float)x / SW - 0.5f) * 2.0f;
            int d = bayer[y & 3][x & 3];
            uint32_t c[3];
            for (int k = 0; k < 3; k++) {
                float top = lo[ja][ia][k] + (lo[ja][ib][k] - lo[ja][ia][k]) * fx;
                float bot = lo[jb][ia][k] + (lo[jb][ib][k] - lo[jb][ia][k]) * fx;
                float val = (top + (bot - top) * fy) * dark * edge + 10.0f;
                int q = (int)val + (k == 1 ? d >> 2 : d >> 1);
                c[k] = q < 0 ? 0 : q > 255 ? 255 : (uint32_t)q;
            }
            out[y * SW + x] = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3));
        }
    }
}

static void make_cover(uint16_t *px, int w, int h, unsigned seed)
{
    srand(seed);
    int r0 = rand() & 255, g0 = rand() & 255, b0 = rand() & 255;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int r = (r0 + x * 2 + (rand() & 15)) & 255, g = (g0 + y * 3) & 255, b = (b0 + x + y) & 255;
            px[y * w + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void)
{
    static uint16_t cover[88 * 88], cover2[88 * 88], whole[SW * SH], banded[SW * SH], ref[SW * SH];
    static menu_bg_t b;
    make_cover(cover, 88, 88, 1);
    make_cover(cover2, 88, 88, 2);

    /* the whole picture in one call, then in bands */
    menu_bg_begin(&b, cover, 88, 88, SW, SH);
    CHECK(menu_bg_step(&b, whole, SH) == 1, "one call for all rows does not finish");

    memset(banded, 0xA5, sizeof banded);
    menu_bg_begin(&b, cover, 88, 88, SW, SH);
    int calls = 0, done = 0;
    while (!done) {
        const int before = b.row;
        done = menu_bg_step(&b, banded, BAND);
        calls++;
        CHECK(b.row - before <= BAND, "a band made %d rows (limit %d)", b.row - before, BAND);
        /* the rows after the band are untouched */
        for (int y = b.row; y < SH && y < b.row + 4; y++)
            CHECK(banded[y * SW] == 0xA5A5, "row %d written before its band", y);
        if (calls > SH)
            break;
    }
    CHECK(calls == (SH + BAND - 1) / BAND, "%d bands for %d rows of %d", calls, SH, BAND);
    CHECK(memcmp(whole, banded, sizeof whole) == 0, "the bands do not make the same picture as one call");

    /* a new cover half way: the picture starts again and is the new cover's */
    memset(banded, 0xA5, sizeof banded);
    menu_bg_begin(&b, cover, 88, 88, SW, SH);
    menu_bg_step(&b, banded, 2 * BAND);
    menu_bg_begin(&b, cover2, 88, 88, SW, SH);
    CHECK(b.row == 0, "begin does not restart the rows");
    while (!menu_bg_step(&b, banded, BAND))
        ;
    menu_bg_begin(&b, cover2, 88, 88, SW, SH);
    menu_bg_step(&b, whole, SH);
    CHECK(memcmp(whole, banded, sizeof whole) == 0, "a restart leaves the old cover in the picture");

    /* close to the old float routine (a 565 step is 8 levels of red and blue, 4 of green) */
    reference(ref, cover, 88, 88);
    menu_bg_begin(&b, cover, 88, 88, SW, SH);
    menu_bg_step(&b, whole, SH);
    int worst = 0;
    double sum = 0;
    for (int i = 0; i < SW * SH; i++) {
        uint32_t a = to24(whole[i]), r = to24(ref[i]);
        for (int s = 0; s < 24; s += 8) {
            int d = abs((int)(a >> s & 255) - (int)(r >> s & 255));
            if (d > worst) worst = d;
            sum += d;
        }
    }
    printf("against the float routine: worst %d, mean %.2f (of 255)\n", worst, sum / (SW * SH * 3));
    CHECK(worst <= 16, "worst difference %d", worst);
    CHECK(sum / (SW * SH * 3) < 2.0, "mean difference %.2f", sum / (SW * SH * 3));

    /* no cover: the dark blue */
    menu_bg_begin(&b, NULL, 0, 0, SW, SH);
    menu_bg_step(&b, whole, SH);
    uint32_t mid = to24(whole[100 * SW + 320]);
    CHECK((mid & 255) > (mid >> 16 & 255) && (mid & 255) > 20, "an empty cover is not dark blue (%06x)", mid);

    /* the narrow layout (RGB30, 360 wide) */
    menu_bg_begin(&b, cover, 88, 88, 360, SH);
    CHECK(menu_bg_step(&b, whole, SH) == 1, "360 wide does not finish");

    /* the speed: printed */
    double t0 = now_ms();
    for (int i = 0; i < 20; i++) reference(ref, cover, 88, 88);
    double t1 = now_ms();
    for (int i = 0; i < 20; i++) { menu_bg_begin(&b, cover, 88, 88, SW, SH); menu_bg_step(&b, whole, SH); }
    double t2 = now_ms();
    for (int i = 0; i < 20; i++) { menu_bg_begin(&b, cover, 88, 88, SW, SH); menu_bg_step(&b, whole, BAND); }
    double t3 = now_ms();
    printf("on this PC: old float routine %.2f ms, new whole %.2f ms, one band (%d rows) %.2f ms\n",
           (t1 - t0) / 20, (t2 - t1) / 20, BAND, (t3 - t2) / 20);

    printf(fails ? "FAILED: %d\n" : "menu background: all checks passed\n", fails);
    return fails ? 1 : 0;
}
