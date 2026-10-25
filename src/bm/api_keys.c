/*
 * api_keys.c: keys (the key, pad and mouse bindings).
 * Moved from runtime.c (docs/NEXT.md, item 0b step 5). It shares the run
 * state through rt_internal.h.
 */
#ifdef BM_RGB30
#include "display.h"            /* fb_init_game */
#endif
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "world3d.h"
#include "map3d.h"
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "platform.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "vid.h"
#include "rec.h"
#include "lib/crc32.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "audio/presets.h"
#include "audio/lua_tone.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "n8lua.h"
#include "shot.h"
#include "webfile.h"
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"
#include "profile.h"
#include "project.h"
#include "rt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* ---------------------------------------------------------------- keys */

/* keyp(): the next key typed, as text ("a", "\n", "\b", "\t"), a name
 * ("up", "down", "left", "right", "home", "end", "pgup", "pgdn", "del",
 * "esc", "f1".."f10") or "^s" for Ctrl+S ("^i", "^m" too: not Tab and
 * Enter), "^S" for Ctrl+Shift+S, "^\n" for Ctrl+Enter, "^." for Ctrl+.
 * (riff's play and stop); nil if
 * none. F11, F12 and Ctrl+Esc are the system's (they never come). The first call
 * turns on typing: the keyboard stops being a gamepad for btn(), Esc no
 * longer leaves the cartridge (Start+Select and PS still do). */
int l_keyp(lua_State *L)
{
    if (!rt.text_mode) {
        rt.text_mode = 1;
        hid_text_mode(1);
    }
    if (rt.tq_tail == rt.tq_head || rt.leave_ask) {
        lua_pushnil(L);
        return 1;
    }
    uint8_t c = rt.tq[rt.tq_tail++];
    static const char *const nav[] = { "up", "down", "left", "right", "home", "end", "pgup", "pgdn",
                                       "del", "f1", "f2", "f3", "f4", "f5" };
    char buf[4];
    if (c == HID_KEY_CTRL_SHIFT || c == HID_KEY_CTRL) {
        /* Ctrl+Shift+S: "^S"; Ctrl+I and Ctrl+M: "^i", "^m" (their codes are
         * Tab's and Enter's); the next code is the Ctrl letter */
        if (rt.tq_tail == rt.tq_head) {
            lua_pushnil(L);
            return 1;
        }
        const int upper = c == HID_KEY_CTRL_SHIFT;
        c = rt.tq[rt.tq_tail++];
        if (c >= 1 && c <= 26) {
            buf[0] = '^';
            buf[1] = (char)((upper ? 'A' : 'a') + c - 1);
            buf[2] = 0;
            lua_pushstring(L, buf);
            return 1;
        }
    }
    if (c >= HID_KEY_UP && c <= HID_KEY_F1 + 4) lua_pushstring(L, nav[c - HID_KEY_UP]);
    else if (c >= HID_KEY_F6 && c <= HID_KEY_F6 + 6) lua_pushfstring(L, "f%d", c - HID_KEY_F6 + 6);
    else if (c == 0x1B) lua_pushstring(L, "esc");
    else if (c == HID_KEY_CTRL_ENTER) lua_pushstring(L, "^\n");
    else if (c == HID_KEY_CTRL_DOT) lua_pushstring(L, "^.");
    else if (c == '\r') lua_pushstring(L, "\n");
    else if (c == 0x7F) lua_pushstring(L, "\b");
    else if (c == '\t') lua_pushstring(L, "\t");
    else if (c >= 1 && c <= 26) { buf[0] = '^'; buf[1] = (char)('a' + c - 1); buf[2] = 0; lua_pushstring(L, buf); }
    else { buf[0] = (char)c; buf[1] = 0; lua_pushstring(L, buf); }
    return 1;
}

/* keyhelp(keys [, title]): the cartridge's own keys, shown under the
 * system's while F12 is held (the system's keys, src/kernel/syskeys.h):
 * a list of { "keys", "what" } ("ctrl d", "x", "shift w", "1 - 5", "a / d")
 * and strings, the headings of its groups. The system's keys are not for
 * the cartridge to give another meaning: one in the list is said in the log
 * and shown in red. Returns how many of them there are. */
int l_keyhelp(lua_State *L)
{
    if (rt.keyhelp) {
        luaL_unref(L, LUA_REGISTRYINDEX, rt.keyhelp);
        rt.keyhelp = 0;
    }
    if (lua_isnoneornil(L, 1)) {
        lua_pushinteger(L, 0);
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    int clashes = 0;
    const lua_Integer n = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= n; i++) {
        if (lua_rawgeti(L, 1, i) == LUA_TTABLE) {
            lua_rawgeti(L, -1, 1);
            const char *k = lua_tostring(L, -1);
            if (k && syskeys_reserved(k)) {
                kprintf("keyhelp: \"%s\" is a system key: not for the cartridge's own use\n", k);
                clashes++;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_newtable(L);                        /* { list, title } */
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    lua_pushstring(L, luaL_optstring(L, 2, "this cartridge"));
    lua_rawseti(L, -2, 2);
    rt.keyhelp = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushinteger(L, clashes);
    return 1;
}

/* showkeys(): the same keys as F12 held, but they stay open, for a click on
 * a chip "F12 keys" (a mouse has no way to hold a key): a click, F12 or Esc
 * closes them; the arrows turn the pages. Nothing else reaches the cartridge
 * meanwhile. Returns true when the keys are open. */
int l_showkeys(lua_State *L)
{
    if (!rt.help_open) {
        rt.help_open = 1;
        rt.help_page = 0;
    }
    lua_pushboolean(L, rt.help_open == 1);
    return 1;
}

/* keyheld(name): true while a key is held on the USB keyboard: "f1".."f12",
 * "tab", "space", "enter", "esc" */
int l_keyheld(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int u = 0;
    if ((n[0] == 'f' || n[0] == 'F') && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12) u = 0x3A + k - 1;
    } else if (!strcmp(n, "tab")) u = 0x2B;
    else if (!strcmp(n, "space")) u = 0x2C;
    else if (!strcmp(n, "enter")) u = 0x28;
    else if (!strcmp(n, "esc")) u = 0x29;
    lua_pushboolean(L, u && hid_usage_held((uint8_t)u));
    return 1;
}

/* rawkeys(on): the keyboards stop being controllers for btn() and pad()
 * (the cartridge reads them with keydown(), to map them as it likes); Esc
 * still leaves the cartridge */
int l_rawkeys(lua_State *L)
{
    rt.raw_keys = lua_toboolean(L, 1);
    return 0;
}

/* keydown(usage): true while the key with this USB HID usage is held on a
 * keyboard (0x04 = A ... 0x1D = Z, 0x28 Enter, 0x4F-0x52 arrows, 0xE0-0xE7
 * Ctrl, Shift, Alt, GUI left then right) */
int l_keydown(lua_State *L)
{
    lua_Integer u = luaL_checkinteger(L, 1);
    lua_pushboolean(L, u > 0 && u < 256 && !rt.leave_ask && hid_usage_held((uint8_t)u));
    return 1;
}

/* keys() -> { usage, ... }: the keys held now */
int l_keys(lua_State *L)
{
    uint8_t u[16];
    int n = rt.leave_ask ? 0 : hid_keys_held(u, 16);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, u[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* pad([p]) -> the controller buttons player p (1-4) holds, as bits: 1 left,
 * 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y,
 * 1024 L1, 2048 R1; without p, any player */
/* mouse(on [, arrow]): the cartridge wants the pointer (M32; without it
 * there is none in a cartridge), with the system's arrow drawn over the
 * frame unless arrow is false; returns false when the console has it off
 * (mouse=off in bm/config.txt). mouse() -> x, y, buttons (1 left, 2 right,
 * 4 middle), wheel steps this frame (up positive), shown; nil when the
 * cartridge did not ask or nothing can move it (no mouse: a controller never
 * moves it). */
int l_mouse(lua_State *L)
{
    if (lua_gettop(L) >= 1) {
        rt.mouse = lua_toboolean(L, 1);
        rt.mouse_arrow = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
        pointer_env(rt.mouse, rt.g.w, rt.g.h);
        rt.ptr = *pointer_get();
        lua_pushboolean(L, pointer_enabled());
        return 1;
    }
    if (!rt.mouse || !pointer_enabled() || !rt.ptr.available) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, rt.ptr.x);
    lua_pushinteger(L, rt.ptr.y);
    lua_pushinteger(L, rt.leave_ask || rt.help_open ? 0 : rt.ptr.buttons);
    lua_pushinteger(L, rt.leave_ask || rt.help_open ? 0 : rt.ptr.wheel);
    lua_pushboolean(L, rt.ptr.shown);
    return 5;
}

/* mousep([i]): the button i (0 left, 1 right, 2 middle; default 0) was
 * pressed this frame, with the pointer shown */
int l_mousep(lua_State *L)
{
    lua_Integer i = luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, rt.mouse && !rt.leave_ask && !rt.help_open && i >= 0 && i < 3 && (rt.ptr.pressed >> i & 1));
    return 1;
}

int l_pad(lua_State *L)
{
    uint32_t b = 0;
    if (lua_isnoneornil(L, 1)) {
        for (int p = 0; p < INPUT_PLAYERS; p++)
            b |= rt.praw[p];
    } else {
        lua_Integer p = luaL_checkinteger(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            b = rt.praw[p - 1];
    }
    lua_pushinteger(L, b);
    return 1;
}

/* timeslice(co, [k]): coroutine co yields (resume returns true and
 * nothing) after about k thousand Lua instructions in a frame (default
 * 400), so a long computation goes on over several frames instead of
 * stopping the cartridge; timeslice(nil) turns it off */
int l_timeslice(lua_State *L)
{
    rt.slice_thread = lua_isthread(L, 1) ? lua_tothread(L, 1) : NULL;
    lua_Integer k = luaL_optinteger(L, 2, 400);
    rt.slice_len = (uint32_t)(k < 10 ? 10 : k > FRAME_BUDGET / 2 ? FRAME_BUDGET / 2 : k);
    rt.slice_at = rt.slice_len;
    return 0;
}
