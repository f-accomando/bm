/* Typed native code, test functions (EXPERIMENT, claude/typed-pi-measure): the C that bmt.py would give for the hot
 * functions of Yharnam (fdiv, solid_at, blocked), with the data in C arrays. A test cartridge times them against the
 * Lua of the game on the Pi Zero W. Not part of the API: see docs/design/typed-native.md. */
#include <math.h>
#include <stdint.h>

#include "lauxlib.h"
#include "lua.h"

#define TS 16           /* tile size in pixels, as Yharnam */
#define CS 32           /* tiles a chunk spans */
#define MAXCOLS 64

static uint8_t t_solid[CS * CS];
static struct { double x, y, rx, ry; uint8_t off; } t_col[MAXCOLS];
static int t_ncols;

static int32_t t_floor_div(double a, int32_t b)
{
    const int32_t f = (int32_t)floor(a);
    int32_t q = f / b;
    if ((f % b != 0) && ((f < 0) != (b < 0)))
        q--;
    return q;
}

static int t_solid_at(double x, double y)
{
    const int32_t tx = t_floor_div(x, TS), ty = t_floor_div(y, TS);
    if (tx < 0 || ty < 0 || tx >= CS || ty >= CS)
        return 1;
    return t_solid[ty * CS + tx];
}

static int t_blocked(double x, double y)
{
    if (t_solid_at(x - 6, y - 3) || t_solid_at(x + 6, y - 3) || t_solid_at(x - 6, y + 2) || t_solid_at(x + 6, y + 2))
        return 1;
    for (int i = 0; i < t_ncols; i++) {
        double dx = x - t_col[i].x;
        if (dx < 48 && dx > -48 && !t_col[i].off) {
            dx = dx / (t_col[i].rx + 5);
            const double dy = (y - t_col[i].y) / (t_col[i].ry + 3);
            if (dx * dx + dy * dy < 1)
                return 1;
        }
    }
    return 0;
}

static int l_typed_fdiv(lua_State *L)
{
    lua_pushinteger(L, t_floor_div(luaL_checknumber(L, 1), (int32_t)luaL_checkinteger(L, 2)));
    return 1;
}

/* typed_set(i, solid) and typed_col(i, x, y, rx, ry, off): fill the data in C */
int l_typed_set(lua_State *L)
{
    const int i = (int)luaL_checkinteger(L, 1);
    if (i >= 0 && i < CS * CS)
        t_solid[i] = (uint8_t)lua_toboolean(L, 2);
    return 0;
}

int l_typed_col(lua_State *L)
{
    const int i = (int)luaL_checkinteger(L, 1);
    if (i < 0 || i >= MAXCOLS)
        return 0;
    t_col[i].x = luaL_checknumber(L, 2);
    t_col[i].y = luaL_checknumber(L, 3);
    t_col[i].rx = luaL_checknumber(L, 4);
    t_col[i].ry = luaL_checknumber(L, 5);
    t_col[i].off = (uint8_t)lua_toboolean(L, 6);
    if (i + 1 > t_ncols)
        t_ncols = i + 1;
    return 0;
}

int l_typed_fdiv_c(lua_State *L) { return l_typed_fdiv(L); }

int l_typed_solid_at(lua_State *L)
{
    lua_pushboolean(L, t_solid_at(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

int l_typed_blocked(lua_State *L)
{
    lua_pushboolean(L, t_blocked(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/* typed_blocked_many(n, x0, y0, dx, dy): the n points (x0 + i*dx, y0 + i*dy); the number of blocked ones. One call
 * for a whole loop: the crossing between Lua and C is paid once. */
int l_typed_blocked_many(lua_State *L)
{
    const int n = (int)luaL_checkinteger(L, 1);
    const double x0 = luaL_checknumber(L, 2), y0 = luaL_checknumber(L, 3), dx = luaL_checknumber(L, 4),
                 dy = luaL_checknumber(L, 5);
    int count = 0;
    for (int i = 0; i < n; i++)
        count += t_blocked(x0 + i * dx, y0 + i * dy);
    lua_pushinteger(L, count);
    return 1;
}

/* ---- Yharnam: all the chunks of the map (EXPERIMENT). The game keeps the truth in Lua tables and copies a chunk here
 * when it is ready and each time its solid tiles or its columns change (typed_ysync). typed_yblocked is the game's
 * `blocked`; it answers nil (the game then runs its Lua) when a chunk it needs is not here. */
#define YCS 16                  /* tiles in a chunk side */
#define YTS 16                  /* tile pixels */
#define YCPX (YCS * YTS)
#define YMAP 8                  /* chunks in a map side (MAP.size) */
#define YMAXCOLS 96

typedef struct {
    uint8_t ready;
    uint8_t solid[YCS * YCS];
    int ncols;
    struct { float x, y, rx, ry; uint8_t off; } col[YMAXCOLS];
} ychunk_t;

static ychunk_t y_ch[YMAP * YMAP];

/* typed_ysync(cx, cy, solid, cols): solid is a string of YCS*YCS bytes (0 or 1), cols a flat table x, y, rx, ry, off
 * for each column (off 0 or 1). Gives false if the chunk does not fit (the game uses its Lua for it). */
int l_typed_ysync(lua_State *L)
{
    const int cx = (int)luaL_checkinteger(L, 1), cy = (int)luaL_checkinteger(L, 2);
    size_t n;
    const char *s = luaL_checklstring(L, 3, &n);
    if (cx < 0 || cy < 0 || cx >= YMAP || cy >= YMAP || n != YCS * YCS) {
        lua_pushboolean(L, 0);
        return 1;
    }
    ychunk_t *c = &y_ch[cy * YMAP + cx];
    const int len = (int)lua_rawlen(L, 4), nc = len / 5;
    if (nc > YMAXCOLS) {
        c->ready = 0;
        lua_pushboolean(L, 0);
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        c->solid[i] = (uint8_t)(s[i] != 0);
    for (int i = 0; i < nc; i++) {
        double v[5];
        for (int k = 0; k < 5; k++) {
            lua_rawgeti(L, 4, i * 5 + k + 1);
            v[k] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        c->col[i].x = (float)v[0]; c->col[i].y = (float)v[1]; c->col[i].rx = (float)v[2]; c->col[i].ry = (float)v[3];
        c->col[i].off = v[4] != 0;
    }
    c->ncols = nc;
    c->ready = 1;
    lua_pushboolean(L, 1);
    return 1;
}

/* the chunk of tile (tx, ty) is in C, or NULL */
static const ychunk_t *y_chunk(int32_t cx, int32_t cy)
{
    if (cx < 0 || cy < 0 || cx >= YMAP || cy >= YMAP)
        return NULL;
    const ychunk_t *c = &y_ch[cy * YMAP + cx];
    return c->ready ? c : NULL;
}

/* -1: a chunk is missing in C (use the Lua); 0, 1: the game's solid_at */
static int y_solid_at(double x, double y)
{
    const int32_t tx = t_floor_div(x, YTS), ty = t_floor_div(y, YTS);
    const int32_t cx = t_floor_div(tx, YCS), cy = t_floor_div(ty, YCS);
    if (cx < 0 || cy < 0 || cx >= YMAP || cy >= YMAP)
        return 1;                               /* the edge of the hunt */
    const ychunk_t *c = y_chunk(cx, cy);
    if (!c)
        return -1;
    return c->solid[(ty - cy * YCS) * YCS + (tx - cx * YCS)];
}

/* the game's blocked(x, y): 1 or 0, or -1 when a chunk is not in C */
static int y_blocked(double x, double y)
{
    const double px[4] = { x - 6, x + 6, x - 6, x + 6 }, py[4] = { y - 3, y - 3, y + 2, y + 2 };
    for (int i = 0; i < 4; i++) {
        const int s = y_solid_at(px[i], py[i]);
        if (s != 0)
            return s;                           /* 1 blocked, -1 the Lua decides */
    }
    const int32_t pcx = t_floor_div(x, YCPX), pcy = t_floor_div(y, YCPX);
    const double fx = x - pcx * YCPX, fy = y - pcy * YCPX;
    for (int32_t cy = fy < 48 ? pcy - 1 : pcy; cy <= (fy > YCPX - 48 ? pcy + 1 : pcy); cy++) {
        for (int32_t cx = fx < 48 ? pcx - 1 : pcx; cx <= (fx > YCPX - 48 ? pcx + 1 : pcx); cx++) {
            if (cx < 0 || cy < 0 || cx >= YMAP || cy >= YMAP)
                continue;                       /* no chunk there in the game either (chunk() gives nil) */
            const ychunk_t *c = &y_ch[cy * YMAP + cx];
            if (!c->ready) {
                /* a chunk the game has not made is nil in Lua too, but one that is made and not copied is not:
                 * only the game knows. */
                return -1;
            }
            for (int i = 0; i < c->ncols; i++) {
                double dx = x - c->col[i].x;
                if (dx < 48 && dx > -48 && !c->col[i].off) {
                    dx = dx / (c->col[i].rx + 5);
                    const double dy = (y - c->col[i].y) / (c->col[i].ry + 3);
                    if (dx * dx + dy * dy < 1)
                        return 1;
                }
            }
        }
    }
    return 0;
}

/* typed_yblocked(x, y) -> true, false, or nil when a chunk is missing in C */
int l_typed_yblocked(lua_State *L)
{
    const int r = y_blocked(luaL_checknumber(L, 1), luaL_checknumber(L, 2));
    if (r < 0)
        lua_pushnil(L);
    else
        lua_pushboolean(L, r);
    return 1;
}
