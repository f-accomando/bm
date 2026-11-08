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
 * `blocked`, made by scripts/bmt.py from tests/typed/yblocked.bmt (src/bm/typed_yblocked_gen.h: do not edit); it answers
 * nil (the game then runs its Lua) when a chunk it needs is not here. */
#include "typed_yblocked_gen.h"

#define YCS 16                  /* tiles in a chunk side */
#define YMAP 8                  /* chunks in a map side (MAP.size) */
#define YMAXCOLS 96             /* column slots for each chunk */

static int32_t y_solid[YMAP * YMAP * YCS * YCS];
static int32_t y_ready[YMAP * YMAP];
static int32_t y_ccount[YMAP * YMAP];
static int32_t y_coloff[YMAP * YMAP * YMAXCOLS];
static double y_colx[YMAP * YMAP * YMAXCOLS], y_coly[YMAP * YMAP * YMAXCOLS], y_colrx[YMAP * YMAP * YMAXCOLS],
    y_colry[YMAP * YMAP * YMAXCOLS];

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
    const int ci = cy * YMAP + cx;
    const int nc = (int)lua_rawlen(L, 4) / 5;
    if (nc > YMAXCOLS) {
        y_ready[ci] = 0;
        lua_pushboolean(L, 0);
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        y_solid[ci * YCS * YCS + i] = s[i] != 0;
    for (int i = 0; i < nc; i++) {
        double v[5];
        for (int k = 0; k < 5; k++) {
            lua_rawgeti(L, 4, i * 5 + k + 1);
            v[k] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        const int slot = ci * YMAXCOLS + i;
        y_colx[slot] = v[0]; y_coly[slot] = v[1]; y_colrx[slot] = v[2]; y_colry[slot] = v[3];
        y_coloff[slot] = v[4] != 0;
    }
    y_ccount[ci] = nc;
    y_ready[ci] = 1;
    lua_pushboolean(L, 1);
    return 1;
}

/* typed_yblocked(x, y) -> true, false, or nil when a chunk is missing in C */
int l_typed_yblocked(lua_State *L)
{
    const int r = bmt_blocked(luaL_checknumber(L, 1), luaL_checknumber(L, 2), y_solid, y_ready, y_colx, y_coly, y_colrx,
                              y_colry, y_coloff, y_ccount);
    if (r < 0)
        lua_pushnil(L);
    else
        lua_pushboolean(L, r);
    return 1;
}
