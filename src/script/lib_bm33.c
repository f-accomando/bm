/* The `bm33` Lua module: access to the machine from scripts. */
#include "luavm.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "gfx/console.h"
#include "kernel/tick.h"
#include "lib/printf.h"

#include <malloc.h>
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"

#ifndef BM33_VERSION
#define BM33_VERSION "dev"
#endif

/* bm33.micros() -> free-running microsecond counter (wraps every ~71 min) */
static int l_micros(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)timer_ticks());
    return 1;
}

/* bm33.millis() -> milliseconds since the tick started */
static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)tick_ms());
    return 1;
}

/* bm33.sleep(ms) */
static int l_sleep(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms > 0)
        timer_delay_ms((uint32_t)ms);
    return 0;
}

/* bm33.mem() -> bytes used by Lua, peak, bytes in use in the C heap */
static int l_mem(lua_State *L)
{
    struct mallinfo mi = mallinfo();
    lua_pushinteger(L, (lua_Integer)luavm_mem());
    lua_pushinteger(L, (lua_Integer)luavm_mem_peak());
    lua_pushinteger(L, (lua_Integer)mi.uordblks);
    return 3;
}

/* bm33.color(fg [, bg]) -> console colours 0-15 (ANSI order) */
static int l_color(lua_State *L)
{
    int fg = (int)luaL_checkinteger(L, 1);
    int bg = (int)luaL_optinteger(L, 2, 0);
    luaL_argcheck(L, fg >= 0 && fg < 16, 1, "colour must be 0-15");
    luaL_argcheck(L, bg >= 0 && bg < 16, 2, "colour must be 0-15");
    fflush(stdout);
    console_set_color((uint8_t)fg, (uint8_t)bg);
    return 0;
}

/* bm33.cls() */
static int l_cls(lua_State *L)
{
    (void)L;
    fflush(stdout);
    console_clear();
    return 0;
}

static int l_reboot(lua_State *L)
{
    (void)L;
    fflush(stdout);
    kprintf("rebooting...\n");
    watchdog_reboot();
}

static const luaL_Reg funcs[] = {
    { "micros", l_micros },
    { "millis", l_millis },
    { "sleep",  l_sleep },
    { "mem",    l_mem },
    { "color",  l_color },
    { "cls",    l_cls },
    { "reboot", l_reboot },
    { NULL, NULL },
};

int luaopen_bm33(lua_State *L)
{
    luaL_newlib(L, funcs);
    lua_pushstring(L, BM33_VERSION);
    lua_setfield(L, -2, "version");
    return 1;
}
