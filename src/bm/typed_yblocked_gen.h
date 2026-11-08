/* made by scripts/bmt.py from a .bmt source: do not edit */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Lua semantics: // and % round toward minus infinity; a zero divisor gives 0 */
#ifndef BMT_HELPERS
#define BMT_HELPERS
static inline int32_t bmt_idiv(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static inline int32_t bmt_imod(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

static inline double bmt_fdiv(double a, double b) { return floor(a / b); }
static inline double bmt_fmod(double a, double b) { return a - floor(a / b) * b; }
static inline int32_t bmt_floor(double a) { return (int32_t)floor(a); }
static inline int32_t bmt_min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t bmt_max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline float bmt_min_f32(float a, float b) { return a < b ? a : b; }
static inline float bmt_max_f32(float a, float b) { return a > b ? a : b; }
static inline double bmt_min_f64(double a, double b) { return a < b ? a : b; }
static inline double bmt_max_f64(double a, double b) { return a > b ? a : b; }
#endif

int32_t bmt_tile_solid(double x, double y, int32_t *solid, int32_t *ready)
{
    int32_t tx = bmt_idiv(bmt_floor(x), 16);
    int32_t ty = bmt_idiv(bmt_floor(y), 16);
    int32_t cx = bmt_idiv(tx, 16);
    int32_t cy = bmt_idiv(ty, 16);
    if (((((cx < 0) || (cx >= 8)) || (cy < 0)) || (cy >= 8))) {
        return 1;
    }
    int32_t ci = (((cy * 8) + cx) + 1);
    if ((ready[(ci) - 1] == 0)) {
        return (-1);
    }
    return solid[((((((ci - 1) * 256) + ((ty - (cy * 16)) * 16)) + (tx - (cx * 16))) + 1)) - 1];
}

int32_t bmt_blocked(double x, double y, int32_t *solid, int32_t *ready, double *colx, double *coly, double *colrx, double *colry, int32_t *coloff, int32_t *ccount)
{
    int32_t s = bmt_tile_solid((x - 6), (y - 3), solid, ready);
    if ((s != 0)) {
        return s;
    }
    s = bmt_tile_solid((x + 6), (y - 3), solid, ready);
    if ((s != 0)) {
        return s;
    }
    s = bmt_tile_solid((x - 6), (y + 2), solid, ready);
    if ((s != 0)) {
        return s;
    }
    s = bmt_tile_solid((x + 6), (y + 2), solid, ready);
    if ((s != 0)) {
        return s;
    }
    int32_t pcx = bmt_idiv(bmt_floor(x), 256);
    int32_t pcy = bmt_idiv(bmt_floor(y), 256);
    double fx = (x - (pcx * 256));
    double fy = (y - (pcy * 256));
    int32_t cy0 = pcy;
    if ((fy < 48)) {
        cy0 = (pcy - 1);
    }
    int32_t cy1 = pcy;
    if ((fy > 208)) {
        cy1 = (pcy + 1);
    }
    int32_t cx0 = pcx;
    if ((fx < 48)) {
        cx0 = (pcx - 1);
    }
    int32_t cx1 = pcx;
    if ((fx > 208)) {
        cx1 = (pcx + 1);
    }
    {
        int32_t cy__end = cy1;
        for (int32_t cy = cy0; cy <= cy__end; cy++) {
            {
                int32_t cx__end = cx1;
                for (int32_t cx = cx0; cx <= cx__end; cx++) {
                    if (((((cx >= 0) && (cx < 8)) && (cy >= 0)) && (cy < 8))) {
                        int32_t ci = (((cy * 8) + cx) + 1);
                        if ((ready[(ci) - 1] == 0)) {
                            return (-1);
                        }
                        {
                            int32_t j__end = ccount[(ci) - 1];
                            for (int32_t j = 1; j <= j__end; j++) {
                                int32_t k = (((ci - 1) * 96) + j);
                                double dx = (x - colx[(k) - 1]);
                                if ((((dx < 48) && (dx > (-48))) && (coloff[(k) - 1] == 0))) {
                                    dx = ((double)(dx) / (double)((colrx[(k) - 1] + 5)));
                                    double dy = ((double)((y - coly[(k) - 1])) / (double)((colry[(k) - 1] + 3)));
                                    if ((((dx * dx) + (dy * dy)) < 1)) {
                                        return 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
