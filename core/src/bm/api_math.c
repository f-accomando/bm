/*
 * api_math.c: Procedural noise (Perlin 2002, FBM), Gaussian RNG, sRGB
 * gamma helpers, Bezier evaluation, and polygon convexity/triangulation.
 */
#include "rt_internal.h"
#include <math.h>
#include <stdlib.h>

/* ---- Perlin 2002 Improved Noise ---- */

static const uint8_t perm256[256] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};

static inline uint8_t p_at(int i)
{
    return perm256[(unsigned)i & 255u];
}

static inline float fade5(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerpf_m(float t, float a, float b)
{
    return a + t * (b - a);
}

static inline float grad3(int hash, float x, float y, float z)
{
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static float perlin3(float x, float y, float z)
{
    float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    int X = (int)fx & 255;
    int Y = (int)fy & 255;
    int Z = (int)fz & 255;

    x -= fx; y -= fy; z -= fz;
    float u = fade5(x), v = fade5(y), w = fade5(z);

    int A  = p_at(X) + Y,     AA = p_at(A) + Z,     AB = p_at(A + 1) + Z;
    int B  = p_at(X + 1) + Y, BA = p_at(B) + Z,     BB = p_at(B + 1) + Z;

    float res = lerpf_m(w,
        lerpf_m(v,
            lerpf_m(u, grad3(p_at(AA),     x,        y,        z),
                       grad3(p_at(BA),     x - 1.0f, y,        z)),
            lerpf_m(u, grad3(p_at(AB),     x,        y - 1.0f, z),
                       grad3(p_at(BB),     x - 1.0f, y - 1.0f, z))),
        lerpf_m(v,
            lerpf_m(u, grad3(p_at(AA + 1), x,        y,        z - 1.0f),
                       grad3(p_at(BA + 1), x - 1.0f, y,        z - 1.0f)),
            lerpf_m(u, grad3(p_at(AB + 1), x,        y - 1.0f, z - 1.0f),
                       grad3(p_at(BB + 1), x - 1.0f, y - 1.0f, z - 1.0f))));
    return res;
}

/* noise(x [, y [, z]]) -> float in [-1, 1] */
int l_noise(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_optnumber(L, 2, 0.0);
    float z = (float)luaL_optnumber(L, 3, 0.0);
    lua_pushnumber(L, (lua_Number)perlin3(x, y, z));
    return 1;
}

/* fbm(x, y [, octaves=4, lacunarity=2.0, gain=0.5]) -> float in [-1, 1] */
int l_fbm(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    int octaves = (int)luaL_optinteger(L, 3, 4);
    float lac = (float)luaL_optnumber(L, 4, 2.0);
    float gain = (float)luaL_optnumber(L, 5, 0.5);
    if (octaves < 1) octaves = 1;
    if (octaves > 12) octaves = 12;

    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * perlin3(x * freq, y * freq, 0.0f);
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    lua_pushnumber(L, (lua_Number)(norm > 0.0f ? sum / norm : 0.0f));
    return 1;
}

/* ---- Box-Muller Gaussian RNG ---- */

static uint32_t math_rng_state = 0x12345678u;

static float math_rand01(void)
{
    uint32_t x = math_rng_state;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    math_rng_state = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/* random_normal([mean=0, stddev=1]) -> float */
int l_random_normal(lua_State *L)
{
    float mean = (float)luaL_optnumber(L, 1, 0.0);
    float stddev = (float)luaL_optnumber(L, 2, 1.0);
    float u1 = math_rand01();
    if (u1 < 1e-7f) u1 = 1e-7f;
    float u2 = math_rand01();
    float z = sqrtf(-2.0f * logf(u1)) * cosf(6.28318530718f * u2);
    lua_pushnumber(L, (lua_Number)(mean + z * stddev));
    return 1;
}

/* ---- sRGB Color Conversion ---- */

static float srgb_to_lin(float c)
{
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    if (c <= 0.04045f) return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static float lin_to_srgb(float c)
{
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* gamma_to_linear(r [, g, b]) -> lr [, lg, lb] */
int l_gamma_to_linear(lua_State *L)
{
    int top = lua_gettop(L);
    int n = top >= 3 ? 3 : 1;
    for (int i = 1; i <= n; i++)
        lua_pushnumber(L, (lua_Number)srgb_to_lin((float)luaL_checknumber(L, i)));
    return n;
}

/* linear_to_gamma(r [, g, b]) -> gr [, gg, gb] */
int l_linear_to_gamma(lua_State *L)
{
    int top = lua_gettop(L);
    int n = top >= 3 ? 3 : 1;
    for (int i = 1; i <= n; i++)
        lua_pushnumber(L, (lua_Number)lin_to_srgb((float)luaL_checknumber(L, i)));
    return n;
}

/* ---- Bezier Evaluation ---- */

/* bezier(t, p0x, p0y, p1x, p1y, p2x, p2y [, p3x, p3y]) -> x, y */
int l_bezier(lua_State *L)
{
    float t = (float)luaL_checknumber(L, 1);
    float p0x = (float)luaL_checknumber(L, 2), p0y = (float)luaL_checknumber(L, 3);
    float p1x = (float)luaL_checknumber(L, 4), p1y = (float)luaL_checknumber(L, 5);
    float p2x = (float)luaL_checknumber(L, 6), p2y = (float)luaL_checknumber(L, 7);
    float u = 1.0f - t;
    float rx, ry;
    if (lua_gettop(L) >= 9) {
        float p3x = (float)luaL_checknumber(L, 8), p3y = (float)luaL_checknumber(L, 9);
        float u2 = u * u, t2 = t * t;
        float b0 = u2 * u, b1 = 3.0f * u2 * t, b2 = 3.0f * u * t2, b3 = t2 * t;
        rx = b0 * p0x + b1 * p1x + b2 * p2x + b3 * p3x;
        ry = b0 * p0y + b1 * p1y + b2 * p2y + b3 * p3y;
    } else {
        float b0 = u * u, b1 = 2.0f * u * t, b2 = t * t;
        rx = b0 * p0x + b1 * p1x + b2 * p2x;
        ry = b0 * p0y + b1 * p1y + b2 * p2y;
    }
    lua_pushnumber(L, (lua_Number)rx);
    lua_pushnumber(L, (lua_Number)ry);
    return 2;
}

/* ---- Polygon Helpers (Convexity & Ear-Clipping Triangulation) ---- */

static float *math_read_poly(lua_State *L, int idx, int *out_n)
{
    *out_n = 0;
    if (!lua_istable(L, idx)) return NULL;
    int len = (int)luaL_len(L, idx);
    if (len < 3) return NULL;
    lua_rawgeti(L, idx, 1);
    int is_pairs = lua_istable(L, -1);
    lua_pop(L, 1);

    if (is_pairs) {
        float *pts = malloc((size_t)len * 2 * sizeof(float));
        if (!pts) return NULL;
        for (int i = 0; i < len; i++) {
            lua_rawgeti(L, idx, i + 1);
            lua_rawgeti(L, -1, 1);
            pts[i * 2] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);
            pts[i * 2 + 1] = (float)lua_tonumber(L, -1);
            lua_pop(L, 2);
        }
        *out_n = len;
        return pts;
    }
    int n = len / 2;
    if (n < 3) return NULL;
    float *pts = malloc((size_t)n * 2 * sizeof(float));
    if (!pts) return NULL;
    for (int i = 0; i < n * 2; i++) {
        lua_rawgeti(L, idx, i + 1);
        pts[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    *out_n = n;
    return pts;
}

static inline float cross2(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/* is_convex(points) -> boolean */
int l_is_convex(lua_State *L)
{
    int n = 0;
    float *pts = math_read_poly(L, 1, &n);
    if (!pts || n < 3) {
        free(pts);
        lua_pushboolean(L, 0);
        return 1;
    }
    int sign = 0;
    int convex = 1;
    for (int i = 0; i < n; i++) {
        int i1 = (i + 1) % n;
        int i2 = (i + 2) % n;
        float cr = cross2(pts[i * 2], pts[i * 2 + 1],
                          pts[i1 * 2], pts[i1 * 2 + 1],
                          pts[i2 * 2], pts[i2 * 2 + 1]);
        if (fabsf(cr) > 1e-6f) {
            int s = cr > 0.0f ? 1 : -1;
            if (sign == 0) sign = s;
            else if (s != sign) { convex = 0; break; }
        }
    }
    if (sign == 0) convex = 0;
    free(pts);
    lua_pushboolean(L, convex);
    return 1;
}

static int pt_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy)
{
    float c1 = cross2(ax, ay, bx, by, px, py);
    float c2 = cross2(bx, by, cx, cy, px, py);
    float c3 = cross2(cx, cy, ax, ay, px, py);
    return (c1 >= 0.0f && c2 >= 0.0f && c3 >= 0.0f) ||
           (c1 <= 0.0f && c2 <= 0.0f && c3 <= 0.0f);
}

/* triangulate(points) -> { {x1,y1,x2,y2,x3,y3}, ... } */
int l_triangulate(lua_State *L)
{
    int n = 0;
    float *pts = math_read_poly(L, 1, &n);
    lua_newtable(L);
    if (!pts || n < 3) {
        free(pts);
        return 1;
    }

    int *idx = malloc((size_t)n * sizeof(int));
    if (!idx) {
        free(pts);
        return 1;
    }

    /* Signed area -> normalize to CCW */
    float area = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += pts[i * 2] * pts[j * 2 + 1] - pts[j * 2] * pts[i * 2 + 1];
    }
    if (area >= 0.0f) {
        for (int i = 0; i < n; i++) idx[i] = i;
    } else {
        for (int i = 0; i < n; i++) idx[i] = n - 1 - i;
    }

    int rem = n;
    int tri_idx = 1;
    int guard = n * 2;
    int i = 0;
    while (rem > 2 && guard-- > 0) {
        int prev = idx[(i + rem - 1) % rem];
        int curr = idx[i % rem];
        int next = idx[(i + 1) % rem];

        float ax = pts[prev * 2], ay = pts[prev * 2 + 1];
        float bx = pts[curr * 2], by = pts[curr * 2 + 1];
        float cx = pts[next * 2], cy = pts[next * 2 + 1];

        if (cross2(ax, ay, bx, by, cx, cy) > 1e-6f) {
            int inside = 0;
            for (int k = 0; k < rem; k++) {
                int v = idx[k];
                if (v == prev || v == curr || v == next) continue;
                if (pt_in_tri(pts[v * 2], pts[v * 2 + 1], ax, ay, bx, by, cx, cy)) {
                    inside = 1;
                    break;
                }
            }
            if (!inside) {
                lua_createtable(L, 6, 0);
                float tcoords[6] = { ax, ay, bx, by, cx, cy };
                for (int j = 0; j < 6; j++) {
                    lua_pushnumber(L, (lua_Number)tcoords[j]);
                    lua_rawseti(L, -2, j + 1);
                }
                lua_rawseti(L, -2, tri_idx++);

                int pos = i % rem;
                for (int k = pos; k < rem - 1; k++)
                    idx[k] = idx[k + 1];
                rem--;
                guard = rem * 2;
                if (rem <= 2) break;
                i %= rem;
                continue;
            }
        }
        i = (i + 1) % rem;
    }

    free(idx);
    free(pts);
    return 1;
}
