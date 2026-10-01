/*
 * Lua 5.4 for the PC with the assistant's `ai` table (src/ai/lua_ai.c), for
 * the host tests of the panel (tests/ai/panel_test.lua).
 *
 *   luaai assist.bin script.lua [args...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ai/lua_ai.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

extern const uint8_t *ai_host_blob;
extern uint32_t ai_host_len;

uint32_t timer_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000u + ts.tv_nsec / 1000);
}

static int traceback(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s assist.bin script.lua [args...]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *blob = aligned_alloc(4, (size_t)(len + 4) & ~(size_t)3);
    if (fread(blob, 1, (size_t)len, f) != (size_t)len)
        return 2;
    fclose(f);
    ai_host_blob = blob;
    ai_host_len = (uint32_t)len;

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    ai_lua_open(L);
    lua_createtable(L, argc, 0);
    for (int i = 2; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 2);
    }
    lua_setglobal(L, "arg");
    lua_pushcfunction(L, traceback);
    int r = luaL_loadfile(L, argv[2]) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK;
    if (r)
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return r;
}
