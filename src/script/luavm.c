#include "luavm.h"
#include "lib/printf.h"

#include <stdio.h>
#include <stdlib.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* Upper bound for Lua's memory, so a runaway script cannot eat the heap
 * the kernel needs. */
#define LUA_MEM_LIMIT   (64u << 20)

static lua_State *L;
static size_t mem_used, mem_peak;

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (!ptr)
        osize = 0;
    if (nsize == 0) {
        free(ptr);
        mem_used -= osize;
        return NULL;
    }
    if (nsize > osize && mem_used - osize + nsize > LUA_MEM_LIMIT)
        return NULL;                    /* Lua raises "not enough memory" */
    void *p = realloc(ptr, nsize);
    if (p) {
        mem_used = mem_used - osize + nsize;
        if (mem_used > mem_peak)
            mem_peak = mem_used;
    }
    return p;
}

static int panic_handler(lua_State *l)
{
    const char *msg = lua_tostring(l, -1);
    kprintf("\x1b[91mLua PANIC: %s\x1b[0m\n", msg ? msg : "(error object is not a string)");
    return 0;   /* Lua then calls abort() -> kernel panic */
}

static int traceback(lua_State *l)
{
    const char *msg = lua_tostring(l, 1);
    if (!msg) {
        if (luaL_callmeta(l, 1, "__tostring") && lua_type(l, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(l, "(error object is a %s value)", luaL_typename(l, 1));
    }
    luaL_traceback(l, l, msg, 1);
    return 1;
}

lua_State *luavm_init(void)
{
    if (L)
        return L;
    L = lua_newstate(lua_alloc, NULL);
    if (!L)
        return NULL;
    lua_atpanic(L, panic_handler);
    luaL_openlibs(L);
    luaL_requiref(L, "bm33", luaopen_bm33, 1);
    lua_pop(L, 1);
    return L;
}

lua_State *luavm_newstate(void)
{
    lua_State *l = lua_newstate(lua_alloc, NULL);
    if (l)
        lua_atpanic(l, panic_handler);
    return l;
}

lua_State *luavm_state(void)
{
    return L;
}

int luavm_run(const char *code, size_t len, const char *chunkname)
{
    int base = lua_gettop(L);
    lua_pushcfunction(L, traceback);
    int status = luaL_loadbuffer(L, code, len, chunkname);
    if (status == LUA_OK)
        status = lua_pcall(L, 0, 0, base + 1);
    if (status != LUA_OK) {
        fflush(stdout);
        kprintf("\x1b[91m%s\x1b[0m\n", lua_tostring(L, -1));
    }
    lua_settop(L, base);
    fflush(stdout);
    return status;
}

size_t luavm_mem(void)
{
    return mem_used;
}

size_t luavm_mem_peak(void)
{
    return mem_peak;
}
