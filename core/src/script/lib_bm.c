/* The `bm` Lua module: access to the machine from scripts. */
#include "luavm.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "kernel/crumbs.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "fs/fat.h"
#ifndef BM_RGB30
#include "kernel/input.h"
#endif
#include "kernel/tick.h"
#include "lib/printf.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

#include "kernel/version.h"

/* bm.micros() -> free-running microsecond counter (wraps every ~71 min) */
static int l_micros(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)timer_ticks());
    return 1;
}

/* bm.millis() -> milliseconds since the tick started */
static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)tick_ms());
    return 1;
}

/* bm.sleep(ms) */
static int l_sleep(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms > 0)
        timer_delay_ms((uint32_t)ms);
    return 0;
}

/* bm.mem() -> bytes used by Lua, peak, bytes in use in the C heap */
static int l_mem(lua_State *L)
{
    struct mallinfo mi = mallinfo();
    lua_pushinteger(L, (lua_Integer)luavm_mem());
    lua_pushinteger(L, (lua_Integer)luavm_mem_peak());
    lua_pushinteger(L, (lua_Integer)mi.uordblks);
    return 3;
}

/* bm.color(fg [, bg]) -> console colours 0-15 (ANSI order) */
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

/* bm.cls() */
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
    crumbs_restart(0);
}

/* ---- the shell's Lua (SYS-19): a monitor line, files of the SD card, drawing on the screen ---- */

static int (*sh_hook)(const char *line, char *out, size_t cap);

void bm_set_sh(int (*fn)(const char *line, char *out, size_t cap))
{
    sh_hook = fn;
}

/* bm.sh("wifi list") -> ok, output: runs a line of the monitor and returns what it printed (the
 * colours cut); the output is also on the screen. */
static int l_sh(lua_State *L)
{
    const char *line = luaL_checkstring(L, 1);
    if (!sh_hook)
        return luaL_error(L, "sh: this console has no monitor");
    static char out[8192];
    int r = sh_hook(line, out, sizeof out);
    lua_pushboolean(L, r == 0);
    lua_pushstring(L, out);
    return 2;
}

/* bm.read(path [, max]) -> the file as a string, or nil and why */
static int l_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    lua_Integer max = luaL_optinteger(L, 2, 1 << 20);
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_find(path, &e) != 0 || e.is_dir) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: not a file", path);
        return 2;
    }
    if (fat_load_part(&e, (size_t)(max > 0 ? max : 1), &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushlstring(L, (const char *)data, len);
    free(data);
    return 1;
}

/* bm.write(path, data) -> true, or false and why (a new name is 8.3) */
static int l_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    fat_entry_t e;
    int r;
    if (fat_find(path, &e) == 0) {
        r = e.is_dir ? -1 : fat_replace(path, data, len);
    } else {
        char dir[128];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (!slash)
            return luaL_error(L, "write: a path starts with /");
        const char *name = path + (slash - dir) + 1;
        if (slash == dir)
            slash[1] = 0;
        else
            *slash = 0;
        r = fat_write_file(dir, name, data, len);
    }
    lua_pushboolean(L, r == 0);
    if (r != 0) {
        lua_pushstring(L, fat_error());
        return 2;
    }
    return 1;
}

/* bm.ls(path) -> a list of { name = , size = , dir = }, or nil and why */
static int l_ls(lua_State *L)
{
    const char *path = luaL_optstring(L, 1, "/");
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, path) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_newtable(L);
    int i = 1;
    while (fat_readdir(&d, &e)) {
        lua_newtable(L);
        lua_pushstring(L, e.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)e.size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, e.is_dir);
        lua_setfield(L, -2, "dir");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

/* the screen: drawn on the console's page at once (no flip). A colour is 0xRRGGBB. */
static uint32_t scr_color(lua_State *L, int arg)
{
    lua_Integer c = luaL_checkinteger(L, arg);
    return fb_color(console_framebuffer(), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

/* bm.size() -> width, height in pixels */
static int l_size(lua_State *L)
{
    const framebuffer_t *fb = console_framebuffer();
    lua_pushinteger(L, (lua_Integer)fb->width);
    lua_pushinteger(L, (lua_Integer)fb->height);
    return 2;
}

/* bm.fill(rgb) */
static int l_fill(lua_State *L)
{
    gfx_clear(console_framebuffer(), scr_color(L, 1));
    return 0;
}

/* bm.rect(x, y, w, h, rgb) */
static int l_rect(lua_State *L)
{
    gfx_rect(console_framebuffer(), (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
             (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4), scr_color(L, 5));
    return 0;
}

/* bm.text(x, y, string, rgb [, background rgb]) -> the x after the text (8x16 letters) */
static int l_text(lua_State *L)
{
    framebuffer_t *fb = console_framebuffer();
    const uint32_t bg = lua_gettop(L) >= 5 ? scr_color(L, 5) : fb_color(fb, 0, 0, 0);
    lua_pushinteger(L, gfx_text(fb, &font_console_8x16, (int)luaL_checkinteger(L, 1),
                                (int)luaL_checkinteger(L, 2), luaL_checkstring(L, 3), scr_color(L, 4), bg));
    return 1;
}

/* bm.key() -> the key pressed (a character code), or nil; it does not wait */
static int l_key(lua_State *L)
{
#ifdef BM_RGB30
    int k = -1;                                 /* no keyboard reader here yet */
#else
    int k = input_key();
#endif
    if (k < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, k);
    return 1;
}

static const luaL_Reg funcs[] = {
    { "sh",     l_sh },
    { "read",   l_read },
    { "write",  l_write },
    { "ls",     l_ls },
    { "size",   l_size },
    { "fill",   l_fill },
    { "rect",   l_rect },
    { "text",   l_text },
    { "key",    l_key },
    { "micros", l_micros },
    { "millis", l_millis },
    { "sleep",  l_sleep },
    { "mem",    l_mem },
    { "color",  l_color },
    { "cls",    l_cls },
    { "reboot", l_reboot },
    { NULL, NULL },
};

int luaopen_bm(lua_State *L)
{
    luaL_newlib(L, funcs);
    lua_pushstring(L, bm_version);
    lua_setfield(L, -2, "version");
    return 1;
}
