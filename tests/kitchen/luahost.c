/*
 * A plain Lua 5.4 interpreter for the PC, built from third_party/lua: runs
 * the host tests of the Lua cartridges (tests/kitchen/sim.lua) with the
 * same Lua version the console has.
 *
 *   luahost script.lua [args...]      (the arguments are in the table `arg`)
 */
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

static int traceback(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
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
    lua_createtable(L, argc, 0);
    for (int i = 1; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);
    }
    lua_setglobal(L, "arg");
    lua_pushcfunction(L, traceback);
    if (luaL_loadfile(L, argv[1]) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
