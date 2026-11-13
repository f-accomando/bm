/*
 * A Lua 5.4 host for the prototype of the typed subset (make test-typed): it gives Lua the two hot
 * functions of hot.bmt as native functions, through the Lua C API, as the console would. The
 * tables of the script are the game's tables: each call copies the numbers out of them, and the
 * result goes back. This is the cost of a call that bmt.py's C alone does not show.
 *
 *   bindhost script.lua [args...]
 */
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "hot.c"        /* build/typed/hot.c, made by core/scripts/bmt.py */
#include "parts.c"      /* build/typed/parts.c: update_parts of Yharnam */

#define MAXN 64

static void get_numbers(lua_State *L, int idx, int n, double *out)
{
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, idx, i);
        out[i - 1] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

/* cam_blocked_c(x, z, n, ox, oz, r) -> bool */
static int l_cam_blocked(lua_State *L)
{
    double x = luaL_checknumber(L, 1), z = luaL_checknumber(L, 2);
    int n = (int)luaL_checkinteger(L, 3);
    double ox[MAXN], oz[MAXN], r[MAXN];
    if (n > MAXN)
        return luaL_error(L, "too many");
    get_numbers(L, 4, n, ox);
    get_numbers(L, 5, n, oz);
    get_numbers(L, 6, n, r);
    lua_pushboolean(L, bmt_cam_blocked(x, z, n, ox, oz, r));
    return 1;
}

/* near_pairs_c(self, n, x, z, alive, out) -> count; the numbers go into out */
static int l_near_pairs(lua_State *L)
{
    int self = (int)luaL_checkinteger(L, 1), n = (int)luaL_checkinteger(L, 2);
    double x[MAXN], z[MAXN];
    bool alive[MAXN];
    int32_t out[MAXN];
    if (n > MAXN)
        return luaL_error(L, "too many");
    get_numbers(L, 3, n, x);
    get_numbers(L, 4, n, z);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 5, i);
        alive[i - 1] = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    int m = bmt_near_pairs(self, n, x, z, alive, out);
    for (int i = 0; i < m; i++) {
        lua_pushinteger(L, out[i]);
        lua_rawseti(L, 6, i + 1);
    }
    lua_pushinteger(L, m);
    return 1;
}

/* The enemies kept in C: one array for each field, owned by the host. Lua moves them with get and set (two numbers
 * a call), and reads the hits with short calls. No table is copied. */
static double EX[MAXN], EZ[MAXN];
static bool EA[MAXN];
static int32_t EOUT[MAXN * MAXN], ECNT[MAXN];
static int EN;

static int l_e_init(lua_State *L)
{
    EN = (int)luaL_checkinteger(L, 1);
    if (EN > MAXN)
        return luaL_error(L, "too many");
    return 0;
}

static int l_e_set(lua_State *L)       /* E.set(i, x, z, alive) */
{
    int i = (int)luaL_checkinteger(L, 1) - 1;
    EX[i] = luaL_checknumber(L, 2);
    EZ[i] = luaL_checknumber(L, 3);
    if (lua_gettop(L) >= 4)
        EA[i] = lua_toboolean(L, 4);
    return 0;
}

static int l_e_get(lua_State *L)       /* E.get(i) -> x, z */
{
    int i = (int)luaL_checkinteger(L, 1) - 1;
    lua_pushnumber(L, EX[i]);
    lua_pushnumber(L, EZ[i]);
    return 2;
}

static int l_e_near(lua_State *L)      /* E.near(i) -> count; the hits are E.hit(1..count) */
{
    lua_pushinteger(L, bmt_near_pairs((int)luaL_checkinteger(L, 1), EN, EX, EZ, EA, EOUT));
    return 1;
}

static int l_e_hit(lua_State *L)       /* E.hit(k) of the last E.near */
{
    lua_pushinteger(L, EOUT[luaL_checkinteger(L, 1) - 1]);
    return 1;
}

static int l_e_near_all(lua_State *L)  /* E.near_all() -> pairs; E.count(i), E.hit_all(i, k) */
{
    lua_pushinteger(L, bmt_near_all(EN, EX, EZ, EA, EOUT, ECNT));
    return 1;
}

static int l_e_count(lua_State *L)
{
    lua_pushinteger(L, ECNT[luaL_checkinteger(L, 1) - 1]);
    return 1;
}

static int l_e_hit_all(lua_State *L)
{
    lua_pushinteger(L, EOUT[(luaL_checkinteger(L, 1) - 1) * EN + luaL_checkinteger(L, 2) - 1]);
    return 1;
}

/* The particles of Yharnam kept in C (tests/typed/parts.lua, mode U): one array for each field. */
#define PMAX 160
static int32_t PT[PMAX], PL[PMAX], PK[PMAX];
static double PX[PMAX], PY[PMAX], PVX[PMAX], PVY[PMAX], PGY[PMAX], JV[64];
static int PN;

static int l_p_reset(lua_State *L)
{
    (void)L;
    PN = 0;
    return 0;
}

static int l_p_jit(lua_State *L)       /* Parts.jit(i, v), i from 1 to 64 */
{
    JV[luaL_checkinteger(L, 1) - 1] = luaL_checknumber(L, 2);
    return 0;
}

static int l_p_spawn(lua_State *L)     /* Parts.spawn(x, y, vx, vy, life, kind, gy): the same limit as the game's spawn() */
{
    int kind = (int)luaL_checkinteger(L, 6);
    if (PN >= (kind == 1 ? PMAX - 40 : PMAX))
        return 0;
    PX[PN] = luaL_checknumber(L, 1);
    PY[PN] = luaL_checknumber(L, 2);
    PVX[PN] = luaL_checknumber(L, 3);
    PVY[PN] = luaL_checknumber(L, 4);
    PL[PN] = (int32_t)luaL_checkinteger(L, 5);
    PK[PN] = kind;
    PGY[PN] = luaL_checknumber(L, 7);
    PT[PN] = 0;
    PN++;
    return 0;
}

static int l_p_update(lua_State *L)    /* Parts.update(k0) */
{
    PN = bmt_update_parts(PN, PT, PL, PK, PX, PY, PVX, PVY, PGY, JV, (int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int l_p_checksum(lua_State *L)  /* Parts.checksum() -> n, sum: the order of the sum is the one of the Lua loop */
{
    double sum = 0.0;
    for (int i = 0; i < PN; i++) {
        sum = sum + PX[i];
        sum = sum + PY[i];
        sum = sum + PVX[i];
        sum = sum + PVY[i];
        sum = sum + (double)PT[i];
    }
    lua_pushinteger(L, PN);
    lua_pushnumber(L, sum);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s script.lua [args...]\n", argv[0]);
        return 2;
    }
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "cam_blocked_c", l_cam_blocked);
    lua_register(L, "near_pairs_c", l_near_pairs);
    static const luaL_Reg enemies[] = {
        { "init", l_e_init }, { "set", l_e_set }, { "get", l_e_get }, { "near", l_e_near }, { "hit", l_e_hit },
        { "near_all", l_e_near_all }, { "count", l_e_count }, { "hit_all", l_e_hit_all }, { NULL, NULL } };
    luaL_newlib(L, enemies);
    lua_setglobal(L, "E");
    static const luaL_Reg particles[] = {
        { "reset", l_p_reset }, { "jit", l_p_jit }, { "spawn", l_p_spawn }, { "update", l_p_update },
        { "checksum", l_p_checksum }, { NULL, NULL } };
    luaL_newlib(L, particles);
    lua_setglobal(L, "Parts");
    lua_createtable(L, argc, 0);
    for (int i = 1; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);       /* arg[0] is the script, as in luahost */
    }
    lua_setglobal(L, "arg");
    int rc = luaL_dofile(L, argv[1]);
    if (rc)
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return rc ? 1 : 0;
}
