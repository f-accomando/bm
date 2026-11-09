/*
 * psx_gte.c: the Geometry Transformation Engine of the PlayStation (COP2): the vector and matrix registers and the
 * commands RTPS, RTPT, NCLIP, OP, DPCS, INTPL, MVMVA, NCDS, CDP, NCDT, NCCS, CC, NCS, NCT, SQR, DCPL, DPCT, AVSZ3,
 * AVSZ4, GPF, GPL and NCCT, with the saturation flags. Original code, from the public hardware description.
 *
 * Data registers d[0..31], control registers c[0..31] (numbers as the MFC2/CFC2 instructions use them).
 */
#include "psx_int.h"

gte_t G;

#define VXY(n) (G.d[(n) * 2])
#define VZ(n)  (G.d[(n) * 2 + 1])
#define RGBC   (G.d[6])
#define OTZ    (G.d[7])
#define IR0    (G.d[8])
#define IR1    (G.d[9])
#define IR2    (G.d[10])
#define IR3    (G.d[11])
#define SXY0   (G.d[12])
#define SXY1   (G.d[13])
#define SXY2   (G.d[14])
#define SXYP   (G.d[15])
#define SZ0    (G.d[16])
#define SZ1    (G.d[17])
#define SZ2    (G.d[18])
#define SZ3    (G.d[19])
#define RGB0   (G.d[20])
#define RGB1   (G.d[21])
#define RGB2   (G.d[22])
#define MAC0   (G.d[24])
#define MAC1   (G.d[25])
#define MAC2   (G.d[26])
#define MAC3   (G.d[27])
#define FLAG   ((uint32_t)G.c[31])

/* control register numbers */
#define C_TR   5
#define C_LLM  8
#define C_BK   13
#define C_LCM  16
#define C_FC   21
#define C_OFX  24
#define C_OFY  25
#define C_H    26
#define C_DQA  27
#define C_DQB  28
#define C_ZSF3 29
#define C_ZSF4 30

static void flag(uint32_t f) { G.c[31] |= (int32_t)f; }

void psx_gte_reset(void) { memset(&G, 0, sizeof G); }

/* ------------------------------------------------------------------ register access */
static inline int16_t lo16(int32_t v) { return (int16_t)(v & 0xFFFF); }
static inline int16_t hi16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }

uint32_t psx_gte_get_data(int n)
{
    switch (n) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return (uint32_t)(int32_t)(int16_t)G.d[n];
    case 7: case 16: case 17: case 18: case 19:
        return (uint32_t)(uint16_t)G.d[n];
    case 15: return (uint32_t)SXY2;                        /* SXYP reads SXY2 */
    case 28: case 29: {                                    /* IRGB, ORGB */
        int r = IR1 >> 7, g = IR2 >> 7, b = IR3 >> 7;
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        g = g < 0 ? 0 : g > 31 ? 31 : g;
        b = b < 0 ? 0 : b > 31 ? 31 : b;
        return (uint32_t)(r | g << 5 | b << 10);
    }
    default: return (uint32_t)G.d[n];
    }
}

void psx_gte_set_data(int n, uint32_t v)
{
    switch (n) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        G.d[n] = (int32_t)(int16_t)v;
        break;
    case 7: case 16: case 17: case 18: case 19:
        G.d[n] = (int32_t)(v & 0xFFFF);
        break;
    case 15:                                               /* SXYP: pushes the FIFO */
        SXY0 = SXY1; SXY1 = SXY2; SXY2 = (int32_t)v;
        break;
    case 28:                                               /* IRGB */
        IR1 = (int32_t)(v & 31) * 0x80;
        IR2 = (int32_t)((v >> 5) & 31) * 0x80;
        IR3 = (int32_t)((v >> 10) & 31) * 0x80;
        break;
    case 29: break;                                        /* ORGB: read only */
    case 30: {                                             /* LZCS: sets LZCR */
        G.d[30] = (int32_t)v;
        uint32_t x = (v & 0x80000000u) ? ~v : v;
        int n0 = 0;
        if (x == 0) n0 = 32; else while (!(x & 0x80000000u)) { x <<= 1; n0++; }
        G.d[31] = n0;
        break;
    }
    case 31: break;                                        /* LZCR: read only */
    default: G.d[n] = (int32_t)v; break;
    }
}

uint32_t psx_gte_get_ctl(int n)
{
    switch (n) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        return (uint32_t)(int32_t)(int16_t)G.c[n];         /* 16-bit values read with the sign extended */
    case 31: {
        uint32_t f = FLAG & 0x7FFFF000u;
        if (f & 0x7F87E000u) f |= 0x80000000u;
        return f;
    }
    default: return (uint32_t)G.c[n];
    }
}

void psx_gte_set_ctl(int n, uint32_t v)
{
    if (n == 31) { G.c[31] = (int32_t)(v & 0x7FFFF000u); return; }
    if (n == 4 || n == 12 || n == 20 || n == 26 || n == 27 || n == 29 || n == 30) { G.c[n] = (int32_t)(int16_t)v; return; }
    G.c[n] = (int32_t)v;
}

/* ------------------------------------------------------------------ the arithmetic with its flags */
/* A1..A3: a 44-bit result; i is 1..3 */
static int64_t A(int i, int64_t v)
{
    if (v > 0x7FFFFFFFFFFLL) flag(0x40000000u >> (i - 1));
    else if (v < -0x80000000000LL) flag(0x08000000u >> (i - 1));
    return (v << 20) >> 20;
}

static int32_t sat_ir(int i, int64_t v, int lm)             /* i 1..3 */
{
    const int64_t lo = lm ? 0 : -0x8000, hi = 0x7FFF;
    if (v < lo) { flag(0x01000000u >> (i - 1)); return (int32_t)lo; }
    if (v > hi) { flag(0x01000000u >> (i - 1)); return (int32_t)hi; }
    return (int32_t)v;
}

static int32_t F(int64_t v)                                 /* MAC0 */
{
    if (v > 0x7FFFFFFFLL) flag(0x80010000u);
    else if (v < -0x80000000LL) flag(0x80008000u);
    return (int32_t)v;
}

static uint32_t sat_c(int i, int32_t v)                     /* the colour FIFO: i 0..2 for R G B */
{
    if (v < 0) { flag(0x00200000u >> i); return 0; }
    if (v > 255) { flag(0x00200000u >> i); return 255; }
    return (uint32_t)v;
}

static void push_color(void)
{
    RGB0 = RGB1; RGB1 = RGB2;
    const uint32_t r = sat_c(0, MAC1 >> 4), g = sat_c(1, MAC2 >> 4), b = sat_c(2, MAC3 >> 4);
    RGB2 = (int32_t)((uint32_t)RGBC & 0xFF000000u) | (int32_t)(b << 16 | g << 8 | r);
}

static void push_sz(int64_t v)
{
    SZ0 = SZ1; SZ1 = SZ2; SZ2 = SZ3;
    if (v < 0) { flag(0x80040000u); v = 0; }
    else if (v > 0xFFFF) { flag(0x80040000u); v = 0xFFFF; }
    SZ3 = (int32_t)v;
}

static void push_sxy(int64_t x, int64_t y)
{
    if (x < -0x400) { flag(0x80004000u); x = -0x400; } else if (x > 0x3FF) { flag(0x80004000u); x = 0x3FF; }
    if (y < -0x400) { flag(0x80002000u); y = -0x400; } else if (y > 0x3FF) { flag(0x80002000u); y = 0x3FF; }
    SXY0 = SXY1; SXY1 = SXY2;
    SXY2 = (int32_t)(((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF));
}

static const uint8_t *unr(void)
{
    static uint8_t t[257];
    static int ok;
    if (!ok) {
        for (int i = 0; i <= 256; i++) {
            int v = ((0x40000 / (i + 0x100)) + 1) / 2 - 0x101;
            t[i] = (uint8_t)(v < 0 ? 0 : v);
        }
        ok = 1;
    }
    return t;
}

static uint32_t divide(uint32_t h, uint32_t sz3)
{
    if (h >= sz3 * 2) { flag(0x80020000u); return 0x1FFFFu; }
    int z = 0;
    for (uint32_t m = 0x8000; m && !(sz3 & m); m >>= 1) z++;
    if (sz3 == 0) z = 16;
    const uint32_t n = h << z;
    uint32_t d = sz3 << z;
    const uint32_t u = (uint32_t)unr()[(d - 0x7FC0u) >> 7] + 0x101u;
    d = (uint32_t)((0x2000080LL - (int64_t)d * u) >> 8);
    d = (uint32_t)((0x0000080LL + (int64_t)d * u) >> 8);
    const uint64_t q = (((uint64_t)n * d) + 0x8000u) >> 16;
    return q > 0x1FFFFu ? 0x1FFFFu : (uint32_t)q;
}

/* ------------------------------------------------------------------ matrices and vectors */
static void get_m(const int32_t *c, int16_t m[9])           /* five control registers hold a 3x3 matrix */
{
    m[0] = lo16(c[0]); m[1] = hi16(c[0]); m[2] = lo16(c[1]);
    m[3] = hi16(c[1]); m[4] = lo16(c[2]); m[5] = hi16(c[2]);
    m[6] = lo16(c[3]); m[7] = hi16(c[3]); m[8] = (int16_t)c[4];
}

static void vec(int v, int32_t out[3])
{
    switch (v) {
    case 0: case 1: case 2: out[0] = lo16(VXY(v)); out[1] = hi16(VXY(v)); out[2] = (int16_t)VZ(v); break;
    default: out[0] = IR1; out[1] = IR2; out[2] = IR3; break;
    }
}

/* [MAC1..3] = (tr << 12 + m * v) >> shift, flags checked after each addition; lm for the IR; tr_mode: 0 TR, 1 BK, 2 FC (the hardware bug), 3 none */
static void mvmva_core(const int16_t m[9], const int32_t v[3], int tr_mode, int sf, int lm, int setir)
{
    int64_t tr[3] = { 0, 0, 0 };
    if (tr_mode < 3)
        for (int k = 0; k < 3; k++) tr[k] = (int64_t)G.c[(tr_mode == 0 ? C_TR : tr_mode == 1 ? C_BK : C_FC) + k] * 4096;
    int64_t out[3];
    for (int k = 0; k < 3; k++) {
        int64_t acc = tr[k];
        acc = A(k + 1, acc);
        if (tr_mode == 2) {                                 /* far colour: only the flags of the first product and the rest are kept */
            A(k + 1, acc + (int64_t)m[k * 3] * v[0]);
            acc = 0;
        } else {
            acc = A(k + 1, acc + (int64_t)m[k * 3] * v[0]);
        }
        acc = A(k + 1, acc + (int64_t)m[k * 3 + 1] * v[1]);
        acc = A(k + 1, acc + (int64_t)m[k * 3 + 2] * v[2]);
        out[k] = acc;
    }
    MAC1 = (int32_t)(out[0] >> (sf * 12));
    MAC2 = (int32_t)(out[1] >> (sf * 12));
    MAC3 = (int32_t)(out[2] >> (sf * 12));
    if (setir) {
        IR1 = sat_ir(1, MAC1, lm);
        IR2 = sat_ir(2, MAC2, lm);
        IR3 = sat_ir(3, MAC3, lm);
    }
}

static void do_mvmva(uint32_t op)
{
    const int sf = (op >> 19) & 1, mx = (op >> 17) & 3, v = (op >> 15) & 3, cv = (op >> 13) & 3, lm = (op >> 10) & 1;
    int16_t m[9];
    int32_t vv[3];
    if (mx == 3) {                                          /* the garbage matrix */
        const int16_t rt13 = lo16(G.c[1]);
        m[0] = (int16_t)(-(RGBC & 0xFF) << 4); m[1] = (int16_t)((RGBC & 0xFF) << 4); m[2] = (int16_t)IR0;
        m[3] = rt13; m[4] = rt13; m[5] = rt13; m[6] = lo16(G.c[2]); m[7] = lo16(G.c[2]); m[8] = lo16(G.c[2]);
    } else {
        get_m(G.c + (mx == 0 ? 0 : mx == 1 ? C_LLM : C_LCM), m);
    }
    vec(v, vv);
    mvmva_core(m, vv, cv, sf, lm, 1);
}

/* the interpolation towards the far colour: base is the colour in the MAC scale (colour << 16), as three values */
static void interp(const int64_t base[3], int sf, int lm)
{
    for (int k = 0; k < 3; k++) {
        const int64_t t = A(k + 1, ((int64_t)G.c[C_FC + k] << 12) - base[k]) >> (sf * 12);
        const int32_t ir = sat_ir(k + 1, t, 0);
        const int64_t m = A(k + 1, (int64_t)ir * IR0 + base[k]) >> (sf * 12);
        G.d[25 + k] = (int32_t)m;
    }
    IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
}

static void rtp(int v, int sf, int lm, int last)
{
    int16_t m[9];
    int32_t vv[3];
    get_m(G.c, m);
    vec(v, vv);
    int64_t out[3];
    for (int k = 0; k < 3; k++) {
        int64_t acc = A(k + 1, (int64_t)G.c[C_TR + k] * 4096);
        acc = A(k + 1, acc + (int64_t)m[k * 3] * vv[0]);
        acc = A(k + 1, acc + (int64_t)m[k * 3 + 1] * vv[1]);
        acc = A(k + 1, acc + (int64_t)m[k * 3 + 2] * vv[2]);
        out[k] = acc;
    }
    MAC1 = (int32_t)(out[0] >> (sf * 12));
    MAC2 = (int32_t)(out[1] >> (sf * 12));
    MAC3 = (int32_t)(out[2] >> (sf * 12));
    IR1 = sat_ir(1, MAC1, lm);
    IR2 = sat_ir(2, MAC2, lm);
    {   /* IR3: the flag follows MAC3 >> 12 whatever sf is, the value follows MAC3 */
        const int64_t chk = out[2] >> 12;
        if (chk < -0x8000 || chk > 0x7FFF) flag(0x00400000u);
        const int64_t lo = lm ? 0 : -0x8000;
        IR3 = (int32_t)(MAC3 < lo ? lo : MAC3 > 0x7FFF ? 0x7FFF : MAC3);
    }
    push_sz(out[2] >> 12);                                  /* SZ3 = the sum >> 12 whatever sf is */
    const uint32_t n = divide((uint32_t)(uint16_t)G.c[C_H], (uint32_t)(uint16_t)SZ3);
    const int32_t x = F((int64_t)n * IR1 + G.c[C_OFX]);
    const int32_t y = F((int64_t)n * IR2 + G.c[C_OFY]);
    push_sxy(x >> 16, y >> 16);
    if (last) {
        const int32_t m0 = F((int64_t)n * (int16_t)G.c[C_DQA] + G.c[C_DQB]);
        MAC0 = m0;
        const int32_t t = m0 >> 12;
        if (t < 0) { flag(0x80001000u); IR0 = 0; } else if (t > 0x1000) { flag(0x80001000u); IR0 = 0x1000; } else IR0 = t;
    }
}

/* the light: [IR] = LLM * V, then [IR] = BK * 1000h + LCM * IR */
static void light(int v, int sf, int lm)
{
    int16_t m[9];
    int32_t vv[3];
    get_m(G.c + C_LLM, m);
    vec(v, vv);
    mvmva_core(m, vv, 3, sf, lm, 1);
    get_m(G.c + C_LCM, m);
    vv[0] = IR1; vv[1] = IR2; vv[2] = IR3;
    mvmva_core(m, vv, 1, sf, lm, 1);
}

static void light_color(int sf, int lm)                     /* [IR] = BK * 1000h + LCM * IR (the light colour only) */
{
    int16_t m[9];
    int32_t vv[3] = { IR1, IR2, IR3 };
    get_m(G.c + C_LCM, m);
    mvmva_core(m, vv, 1, sf, lm, 1);
}

static void color_times_ir(int64_t base[3])
{
    const uint32_t c = (uint32_t)RGBC;
    base[0] = ((int64_t)(c & 0xFF) * IR1) << 4;
    base[1] = ((int64_t)((c >> 8) & 0xFF) * IR2) << 4;
    base[2] = ((int64_t)((c >> 16) & 0xFF) * IR3) << 4;
}

static void mult_color(int sf, int lm)                      /* NCCS tail: [MAC] = (colour * IR) << 4 */
{
    int64_t b[3];
    color_times_ir(b);
    for (int k = 0; k < 3; k++) G.d[25 + k] = (int32_t)(A(k + 1, b[k]) >> (sf * 12));
    IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
}

static void base_color(int64_t b[3])                        /* the colour << 16 */
{
    const uint32_t c = (uint32_t)RGBC;
    b[0] = (int64_t)(c & 0xFF) << 16; b[1] = (int64_t)((c >> 8) & 0xFF) << 16; b[2] = (int64_t)((c >> 16) & 0xFF) << 16;
}

void psx_gte_command(uint32_t op)
{
    const int sf = (op >> 19) & 1, lm = (op >> 10) & 1;
    G.c[31] = 0;
    switch (op & 63) {
    case 0x01: rtp(0, sf, lm, 1); break;                                                 /* RTPS */
    case 0x30: rtp(0, sf, lm, 0); rtp(1, sf, lm, 0); rtp(2, sf, lm, 1); break;           /* RTPT */
    case 0x06: {                                                                         /* NCLIP */
        const int32_t x0 = lo16(SXY0), y0 = hi16(SXY0), x1 = lo16(SXY1), y1 = hi16(SXY1), x2 = lo16(SXY2), y2 = hi16(SXY2);
        MAC0 = F((int64_t)x0 * (y1 - y2) + (int64_t)x1 * (y2 - y0) + (int64_t)x2 * (y0 - y1));
        break;
    }
    case 0x0C: {                                                                         /* OP */
        const int32_t d1 = lo16(G.c[0]), d2 = lo16(G.c[2]), d3 = (int16_t)G.c[4];
        MAC1 = (int32_t)(A(1, (int64_t)IR3 * d2 - (int64_t)IR2 * d3) >> (sf * 12));
        MAC2 = (int32_t)(A(2, (int64_t)IR1 * d3 - (int64_t)IR3 * d1) >> (sf * 12));
        MAC3 = (int32_t)(A(3, (int64_t)IR2 * d1 - (int64_t)IR1 * d2) >> (sf * 12));
        IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
        break;
    }
    case 0x10: { int64_t b[3]; base_color(b); interp(b, sf, lm); push_color(); break; }               /* DPCS */
    case 0x11: { int64_t b[3] = { (int64_t)IR1 << 12, (int64_t)IR2 << 12, (int64_t)IR3 << 12 }; interp(b, sf, lm); push_color(); break; }   /* INTPL */
    case 0x12: do_mvmva(op); break;                                                      /* MVMVA */
    case 0x13: { light(0, sf, lm); int64_t b[3]; color_times_ir(b); interp(b, sf, lm); push_color(); break; }   /* NCDS */
    case 0x16: for (int v = 0; v < 3; v++) { light(v, sf, lm); int64_t b[3]; color_times_ir(b); interp(b, sf, lm); push_color(); } break;   /* NCDT */
    case 0x14: { light_color(sf, lm); int64_t b[3]; color_times_ir(b); interp(b, sf, lm); push_color(); break; }       /* CDP */
    case 0x1B: light(0, sf, lm); mult_color(sf, lm); push_color(); break;                /* NCCS */
    case 0x3F: for (int v = 0; v < 3; v++) { light(v, sf, lm); mult_color(sf, lm); push_color(); } break;    /* NCCT */
    case 0x1C: light_color(sf, lm); mult_color(sf, lm); push_color(); break;             /* CC */
    case 0x1E: light(0, sf, lm); push_color(); break;                                    /* NCS */
    case 0x20: for (int v = 0; v < 3; v++) { light(v, sf, lm); push_color(); } break;    /* NCT */
    case 0x28:                                                                           /* SQR */
        MAC1 = (int32_t)(((int64_t)IR1 * IR1) >> (sf * 12));
        MAC2 = (int32_t)(((int64_t)IR2 * IR2) >> (sf * 12));
        MAC3 = (int32_t)(((int64_t)IR3 * IR3) >> (sf * 12));
        IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
        break;
    case 0x29: { int64_t b[3]; color_times_ir(b); interp(b, sf, lm); push_color(); break; }     /* DCPL */
    case 0x2A:                                                                           /* DPCT */
        for (int k = 0; k < 3; k++) {
            const uint32_t c = (uint32_t)RGB0;
            int64_t b[3] = { (int64_t)(c & 0xFF) << 16, (int64_t)((c >> 8) & 0xFF) << 16, (int64_t)((c >> 16) & 0xFF) << 16 };
            interp(b, sf, lm);
            push_color();
        }
        break;
    case 0x2D: {                                                                         /* AVSZ3 */
        MAC0 = F((int64_t)(int16_t)G.c[C_ZSF3] * (SZ1 + SZ2 + SZ3));
        const int64_t t = MAC0 >> 12;
        OTZ = (int32_t)(t < 0 ? (flag(0x80040000u), 0) : t > 0xFFFF ? (flag(0x80040000u), 0xFFFF) : t);
        break;
    }
    case 0x2E: {                                                                         /* AVSZ4 */
        MAC0 = F((int64_t)(int16_t)G.c[C_ZSF4] * (SZ0 + SZ1 + SZ2 + SZ3));
        const int64_t t = MAC0 >> 12;
        OTZ = (int32_t)(t < 0 ? (flag(0x80040000u), 0) : t > 0xFFFF ? (flag(0x80040000u), 0xFFFF) : t);
        break;
    }
    case 0x3D:                                                                           /* GPF */
        for (int k = 0; k < 3; k++) G.d[25 + k] = (int32_t)(A(k + 1, (int64_t)G.d[9 + k] * IR0) >> (sf * 12));
        IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
        push_color();
        break;
    case 0x3E:                                                                           /* GPL */
        for (int k = 0; k < 3; k++)
            G.d[25 + k] = (int32_t)(A(k + 1, ((int64_t)G.d[25 + k] << (sf * 12)) + (int64_t)G.d[9 + k] * IR0) >> (sf * 12));
        IR1 = sat_ir(1, MAC1, lm); IR2 = sat_ir(2, MAC2, lm); IR3 = sat_ir(3, MAC3, lm);
        push_color();
        break;
    default: break;
    }
    if (G.c[31] & 0x7F87E000) G.c[31] |= (int32_t)0x80000000u;
}
