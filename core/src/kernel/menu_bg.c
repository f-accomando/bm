#include "menu_bg.h"

enum { LW = 16, LH = 10 };

static const uint8_t bayer[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static inline uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

void menu_bg_begin(menu_bg_t *b, const uint16_t *cover, int w, int h, int sw, int sh)
{
    if (sw > MENU_BG_MAX_W)
        sw = MENU_BG_MAX_W;
    b->sw = sw;
    b->sh = sh;
    b->row = 0;
    for (int j = 0; j < LH; j++)
        for (int i = 0; i < LW; i++) {
            uint32_t r = 0, g = 0, bl = 0, n = 0;
            if (cover && w >= LW && h >= LH) {
                const int x0 = i * w / LW, x1 = (i + 1) * w / LW, y0 = j * h / LH, y1 = (j + 1) * h / LH;
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++, n++) {
                        const uint32_t c = cover[y * w + x], cr = c >> 11, cg = c >> 5 & 0x3F, cb = c & 0x1F;
                        r += cr << 3 | cr >> 2;
                        g += cg << 2 | cg >> 4;
                        bl += cb << 3 | cb >> 2;
                    }
            }
            if (n) {
                b->lo[j][i][0] = (int32_t)(r * 256 / n);
                b->lo[j][i][1] = (int32_t)(g * 256 / n);
                b->lo[j][i][2] = (int32_t)(bl * 256 / n);
            } else {
                b->lo[j][i][0] = 40 * 256;
                b->lo[j][i][1] = 44 * 256;
                b->lo[j][i][2] = 70 * 256;
            }
        }
    for (int x = 0; x < sw; x++) {
        /* u = (x + 0.5) * LW / sw - 0.5, in 1/256 */
        const int u = ((2 * x + 1) * LW * 256 / sw - 256) / 2;
        const int i0 = u < 0 ? -1 : u >> 8;
        const int ia = i0 < 0 ? 0 : i0, ib = i0 + 1 >= LW ? LW - 1 : i0 + 1;
        b->ia[x] = (uint8_t)ia;
        b->ib[x] = (uint8_t)ib;
        b->fx[x] = (uint16_t)(u - i0 * 256);
        /* edge = 1 - 0.25 * |2x - sw| / sw: the sides are darker */
        const int d = (2 * x - sw) < 0 ? sw - 2 * x : 2 * x - sw;
        b->edge[x] = (uint16_t)(256 - 64 * d / sw);
    }
}

int menu_bg_step(menu_bg_t *b, uint16_t *out, int rows)
{
    const int sw = b->sw, sh = b->sh;
    int end = b->row + rows;
    if (end > sh)
        end = sh;
    for (int y = b->row; y < end; y++) {
        /* v = (y + 0.5) * LH / sh - 0.5, in 1/256 */
        const int v = ((2 * y + 1) * LH * 256 / sh - 256) / 2;
        const int j0 = v < 0 ? -1 : v >> 8;
        const int fy = v - j0 * 256;
        const int ja = j0 < 0 ? 0 : j0, jb = j0 + 1 >= LH ? LH - 1 : j0 + 1;
        /* darker at the bottom, where the grid is: 0.50 - 0.22 * y / sh */
        const int dark = 128 - 56 * y / sh;
        int col[LW][3];                         /* this row's 16 blocks, 8.8 */
        for (int i = 0; i < LW; i++)
            for (int k = 0; k < 3; k++) {
                const int m = (b->lo[ja][i][k] * (256 - fy) + b->lo[jb][i][k] * fy) >> 8;
                col[i][k] = m * dark >> 8;
            }
        uint16_t *o = out + (uint32_t)y * (uint32_t)sw;
        const uint8_t *bay = bayer[y & 3];
        for (int x = 0; x < sw; x++) {
            const int ia = b->ia[x], ib = b->ib[x], fx = b->fx[x], e = b->edge[x], d = bay[x & 3];
            int c[3];
            for (int k = 0; k < 3; k++) {
                const int h = (col[ia][k] * (256 - fx) + col[ib][k] * fx) >> 8;
                const int q = (h * e >> 16) + 10 + (k == 1 ? d >> 2 : d >> 1);
                c[k] = q > 255 ? 255 : q;
            }
            o[x] = rgb565(c[0], c[1], c[2]);
        }
    }
    b->row = end;
    return end >= sh;
}
