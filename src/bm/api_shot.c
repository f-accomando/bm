/*
 * api_shot.c: screenshots (docs/SCREENSHOTS.md), config(), saved() and the other save readers.
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

/* ---------------------------------------------------------------- screenshots (docs/SCREENSHOTS.md) */


/* Plans a screenshot: the next free number of the prefix in /bm/shots (one more than the
 * highest), and the request, done when the frame is drawn (shot_grab). 0, or -1 if the
 * numbers are over. A second request in the same frame is the first one. */
int shot_plan(const char *prefix)
{
    if (shot_req)
        return 0;
    char p[5];
    shot_prefix(p, perm_title, prefix);
    int top = 0;
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, SHOT_DIR) == 0)
        while (fat_readdir(&d, &e)) {
            const int n = shot_number(e.name, p);
            if (n > top)
                top = n;
        }
    if (shot_name(shot_file, perm_title, prefix, top + 1) != 0)
        return -1;
    shot_req = 1;
    return 0;
}

/* shot([prefix]) -> "bm/shots/PONG0007.png": the frame being drawn is saved as a PNG when it
 * is done (the dev kit's overlays and the pointer are not in it). The name starts with the
 * first letters of the game's title, or of `prefix` (up to 4 letters or digits). nil and a
 * message if the numbers are over; a card that cannot be written to shows when it is saved. */
int l_shot(lua_State *L)
{
    const char *prefix = luaL_optstring(L, 1, NULL);
    if (shot_plan(prefix) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "no free number (9999 screenshots)");
        return 2;
    }
    lua_pushfstring(L, "bm/shots/%s", shot_file);
    return 1;
}

/* The PC's request (netxfer op 'T'): the next frame drawn is saved, as with the keys. */
void bm_shot_ask(void)
{
    shot_net = 1;
    shot_res = 2;
    shot_net_file[0] = 0;
}

/* The 3D Bench has no game loop: it calls this after each frame it shows. If the PC asked (bm_shot_ask), the page is saved as
 * bm/shots/B3Dnnnn.png and the answer is ready for bm_shot_done. 1 if it saved one. */
int bm_shot_page(const uint16_t *px, int stride, int w, int h)
{
    if (shot_net != 1)
        return 0;
    shot_net = 0;
    if (shot_plan("b3d") != 0) {
        shot_res = -1;
        return 0;
    }
    shot_req = 0;
    size_t n = 0;
    uint8_t *png = shot_png(px, stride, w, h, &n);
    const int ok = png && fat_mkdirs(SHOT_DIR) == 0 && fat_write_file(SHOT_DIR, shot_file, png, n) == 0;
    free(png);
    shot_res = ok ? 1 : -1;
    memcpy(shot_net_file, shot_file, sizeof shot_net_file);
    return ok;
}

/* The PC gave up (3 s with no frame): a later game does not answer it. */
void bm_shot_cancel(void)
{
    shot_net = 0;
    shot_res = 0;
}

/* 0 while the frame is not drawn yet, 1 when saved (name in `name`, "pong0007.png"), -1 if it could not be written. */
int bm_shot_done(char *name, size_t n)
{
    if (shot_res == 1 && name && n)
        ksnprintf(name, n, "%s", shot_net_file);
    return shot_res == 2 ? 0 : shot_res;
}

/* cart_config(key [, value]) -> the text of the key in bm/config.txt, or nil.
 * Only keys that start with the game's first title word in lower case and "_" (Overbit:
 * overbit_bench): the flags a game takes from the monitor line
 * ("set overbit_bench=1080 ; play overbit") or from easy_install. With a
 * value the key is changed and saved ("" clears it). */
int l_cart_config(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    char pre[56];
    size_t o = 0;
    for (const char *t = perm_title; *t && *t != ' ' && o < 40; t++)     /* the first word: "Overbit kaiju" is overbit_ */
        if ((*t >= 'a' && *t <= 'z') || (*t >= '0' && *t <= '9'))
            pre[o++] = *t;
        else if (*t >= 'A' && *t <= 'Z')
            pre[o++] = (char)(*t + 32);
    pre[o++] = '_';
    pre[o] = 0;
    if (strncmp(key, pre, o) != 0)
        return luaL_error(L, "cart_config: the keys of this game start with '%s'", pre);
    if (lua_gettop(L) >= 2) {
        config_set(key, luaL_checkstring(L, 2));
        config_save();
    }
    const char *v = config_get(key);
    if (v && *v)
        lua_pushstring(L, v);
    else
        lua_pushnil(L);
    return 1;
}

int l_saved(lua_State *L)
{
    int slot = save_slot(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    if (slot_find(slot, &e) != 0 || e.size > SAVE_MAX || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        return 1;
    }
    int ok = luaL_loadbufferx(L, (const char *)data, len, "=save", "t") == LUA_OK;
    free(data);
    if (!ok) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);                            /* empty environment: data only */
    lua_setupvalue(L, -2, 1);
    if (lua_pcall(L, 0, 1, 0) != LUA_OK || !lua_istable(L, -1)) {
        lua_pushnil(L);
        return 1;
    }
    return 1;
}

/* saves(): {[slot] = bytes} of the slots in use, and how many slots there
 * are (8): for a "load game" page */
int l_saves(lua_State *L)
{
    lua_newtable(L);
    for (int slot = 1; slot <= SAVE_SLOTS; slot++) {
        fat_entry_t e;
        if (slot_find(slot, &e) == 0 && !e.is_dir) {
            lua_pushinteger(L, (lua_Integer)e.size);
            lua_rawseti(L, -2, slot);
        }
    }
    lua_pushinteger(L, SAVE_SLOTS);
    return 2;
}

/* delsave([slot]): true when the slot was emptied, or false and why */
int l_delsave(lua_State *L)
{
    int slot = save_slot(L, 1);
    char name[16], path[48];
    slot_name(slot, name);
    ksnprintf(path, sizeof path, "%s/%s", SAVE_DIR, name);
    fat_entry_t e;
    if (fat_find(path, &e) != 0) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "nothing saved in slot %d", slot);
        return 2;
    }
    if (fat_delete(path) != 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
