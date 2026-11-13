/* made by core/scripts/bmt.py from a .bmt source: do not edit */
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

int32_t bmt_cm_solid(double x, double y, int32_t *solid, int32_t *ready, int32_t tile, int32_t ctiles, int32_t mw, int32_t mh)
{
    int32_t tx = bmt_idiv(bmt_floor(x), tile);
    int32_t ty = bmt_idiv(bmt_floor(y), tile);
    int32_t cx = bmt_idiv(tx, ctiles);
    int32_t cy = bmt_idiv(ty, ctiles);
    if (((((cx < 0) || (cx >= mw)) || (cy < 0)) || (cy >= mh))) {
        return 1;
    }
    int32_t ci = (((cy * mw) + cx) + 1);
    if ((ready[(ci) - 1] == 0)) {
        return (-1);
    }
    return solid[(((((((ci - 1) * ctiles) * ctiles) + ((ty - (cy * ctiles)) * ctiles)) + (tx - (cx * ctiles))) + 1)) - 1];
}

int32_t bmt_cm_blocked(double x, double y, int32_t *solid, int32_t *ready, double *colx, double *coly, double *colrx, double *colry, int32_t *coloff, int32_t *ccount, int32_t tile, int32_t ctiles, int32_t mw, int32_t mh, int32_t maxcols, double fx0, double fy0, double fx1, double fy1, double margin, double padx, double pady)
{
    int32_t s = bmt_cm_solid((x + fx0), (y + fy0), solid, ready, tile, ctiles, mw, mh);
    if ((s != 0)) {
        return s;
    }
    s = bmt_cm_solid((x + fx1), (y + fy0), solid, ready, tile, ctiles, mw, mh);
    if ((s != 0)) {
        return s;
    }
    s = bmt_cm_solid((x + fx0), (y + fy1), solid, ready, tile, ctiles, mw, mh);
    if ((s != 0)) {
        return s;
    }
    s = bmt_cm_solid((x + fx1), (y + fy1), solid, ready, tile, ctiles, mw, mh);
    if ((s != 0)) {
        return s;
    }
    int32_t cpx = (ctiles * tile);
    int32_t pcx = bmt_idiv(bmt_floor(x), cpx);
    int32_t pcy = bmt_idiv(bmt_floor(y), cpx);
    double fx = (x - (pcx * cpx));
    double fy = (y - (pcy * cpx));
    int32_t cy0 = pcy;
    if ((fy < margin)) {
        cy0 = (pcy - 1);
    }
    int32_t cy1 = pcy;
    if ((fy > (cpx - margin))) {
        cy1 = (pcy + 1);
    }
    int32_t cx0 = pcx;
    if ((fx < margin)) {
        cx0 = (pcx - 1);
    }
    int32_t cx1 = pcx;
    if ((fx > (cpx - margin))) {
        cx1 = (pcx + 1);
    }
    {
        int32_t cy__end = cy1;
        for (int32_t cy = cy0; cy <= cy__end; cy++) {
            {
                int32_t cx__end = cx1;
                for (int32_t cx = cx0; cx <= cx__end; cx++) {
                    if (((((cx >= 0) && (cx < mw)) && (cy >= 0)) && (cy < mh))) {
                        int32_t ci = (((cy * mw) + cx) + 1);
                        if ((ready[(ci) - 1] == 0)) {
                            return (-1);
                        }
                        {
                            int32_t j__end = ccount[(ci) - 1];
                            for (int32_t j = 1; j <= j__end; j++) {
                                int32_t k = (((ci - 1) * maxcols) + j);
                                double dx = (x - colx[(k) - 1]);
                                if ((((dx < margin) && (dx > (-margin))) && (coloff[(k) - 1] == 0))) {
                                    dx = ((double)(dx) / (double)((colrx[(k) - 1] + padx)));
                                    double dy = ((double)((y - coly[(k) - 1])) / (double)((colry[(k) - 1] + pady)));
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
