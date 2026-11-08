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
