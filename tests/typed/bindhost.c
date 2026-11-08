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

#include "hot.c"        /* build/typed/hot.c, made by scripts/bmt.py */

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
