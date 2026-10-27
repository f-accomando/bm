/*
 * api_light.c: the nano8 light (the light bindings).
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

/* ---------------------------------------------------------------- light */


/* light_begin([ambient]): starts the lights of this frame; everything drawn
 * so far will be lit by light_end(). The cartridge draws into RAM from now
 * on (lighting reads the picture back). */
int l_light_begin(lua_State *L)
{
    sync3d();
    if (!rt.light.rgb && g16_light_init(&rt.light, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    if (video_to_ram(&rt.g) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_light_clear(&rt.light, (uint32_t)luaL_optinteger(L, 1, 0x000000));
    return 0;
}

/* light(x, y, radius, colour [, intensity]) - world coordinates (camera) */
int l_light(lua_State *L)
{
    if (!rt.light.rgb)
        return luaL_error(L, "light() before light_begin()");
    g16_light_add(&rt.light, fnum(L, 1, 0) - (float)rt.g.cam_x, fnum(L, 2, 0) - (float)rt.g.cam_y,
                  fnum(L, 3, 32), (uint32_t)luaL_optinteger(L, 4, 0xFFFFFF), fnum(L, 5, 1));
    return 0;
}

int l_light_end(lua_State *L)
{
    (void)L;
    sync3d();
    if (rt.light.rgb)
        g16_light_apply(&rt.g, &rt.light);
    return 0;
}

/* Lighting by levels, as in Dank Tomb: fades() gives each colour what it
 * becomes at every light level, dark_begin() fills the screen with the
 * ambient level, glow() adds lamps (rings of levels), dark_end() turns
 * every pixel into its colour at its level. */
static int fade_ready(lua_State *L)
{
    if (!rt.fade.lv && g16_fade_init(&rt.fade, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    return 0;
}

/* fades({ {from, l0, l1, ...}, ... }) -> levels: every row has the same
 * length, 3..17 (2..16 levels, l0 the darkest) */
int l_fades(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    fade_ready(L);
    const lua_Integer n = luaL_len(L, 1);
    int levels = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_geti(L, 1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        const lua_Integer len = luaL_len(L, -1);
        if (i == 1) {
            if (len < 3 || len > G16_FADE_LEVELS + 1)
                return luaL_error(L, "fades: a row is a colour and 2 to %d levels", G16_FADE_LEVELS);
            levels = (int)len - 1;
            g16_fade_reset(&rt.fade, levels);
        } else if (len != levels + 1) {
            return luaL_error(L, "fades: row %d has %d levels, not %d", (int)i, (int)len - 1, levels);
        }
        uint16_t to[G16_FADE_LEVELS];
        lua_geti(L, -1, 1);
        const uint16_t from = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
        lua_pop(L, 1);
        for (int k = 0; k < levels; k++) {
            lua_geti(L, -1, k + 2);
            to[k] = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (g16_fade_colour(&rt.fade, from, to) != 0)
            return luaL_error(L, "fades: more than %d colours", G16_FADE_COLOURS);
    }
    g16_fade_done(&rt.fade);
    lua_pushinteger(L, rt.fade.levels);
    return 1;
}

/* dark_begin([ambient level [, step]]): everything drawn so far will be lit by
 * dark_end() (the cartridge draws into RAM from now on); step 2: the lamps
 * work on a quarter of the pixels, a level for each 2x2 */
int l_dark_begin(lua_State *L)
{
    const uint32_t t0 = timer_ticks();
    fade_ready(L);
    if (!bm_video_uses_ram())
        sync3d();                       /* the 2D the GPU holds goes on the page it was drawn for, before the RAM page takes over */
    if (video_to_ram(&rt.g) != 0 || g16_fade_set_step(&rt.fade, (int)luaL_optinteger(L, 2, 1)) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_fade_clear(&rt.fade, (int)luaL_optinteger(L, 1, 0));
    if (rt.gpu2d_only && rt.r3d.backend)
        gpu2d_submit();                 /* the GPU draws what the frame has so far while the ARM makes the lights */
    rt.light_us += timer_ticks() - t0;
    return 0;
}

/* online([on [, note]]): the game is played over the network. PS, Ctrl+Esc
 * and Start+Select do not suspend it (the others play on) but ask the
 * player leaving, on this console only, "Leave the match?" over the game
 * (which sees none of the buttons while it is open): yes calls the
 * cartridge's _leave() (it tells the server) and ends the game, back
 * stays. note: a line under the question ("You are the host: the match
 * ends for everyone."). Returns whether it was online and whether the
 * question is open (the player may be away). */
int l_online(lua_State *L)
{
    const int was = rt.online;
    if (!lua_isnone(L, 1)) {
        rt.online = lua_toboolean(L, 1);
        ksnprintf(rt.online_note, sizeof rt.online_note, "%s", rt.online ? luaL_optstring(L, 2, "") : "");
        if (!rt.online)
            rt.leave_ask = 0;                   /* (the buttons it took come back released) */
    }
    lua_pushboolean(L, was);
    lua_pushboolean(L, rt.leave_ask);
    return 2;
}

/* glow(x, y, radius, level [, dither 0..1]) - world coordinates (camera) */
int l_glow(lua_State *L)
{
    if (!rt.fade.lv)
        return luaL_error(L, "glow() before dark_begin()");
    const uint32_t t0 = timer_ticks();
    const float d = fnum(L, 5, 0.5f);
    g16_fade_glow(&rt.fade, (int)floorf(fnum(L, 1, 0)) - rt.g.cam_x, (int)floorf(fnum(L, 2, 0)) - rt.g.cam_y,
                  (int)fnum(L, 3, 32), (int)luaL_checkinteger(L, 4), (int)(d * 256));
    rt.light_us += timer_ticks() - t0;
    return 0;
}

int (*bm_fade_accel)(g16_t *g, const g16_fade_t *f) = NULL;

int l_dark_end(lua_State *L)
{
    (void)L;
    const uint32_t t0 = timer_ticks();
    if (rt.fade.lv) {
        sync3d();                       /* it reads the page: the 3D (and a cls) on it first */
        const uint32_t tf = timer_ticks();
        if (bm_fade_accel) {
            g16_fade_flush(&rt.fade);       /* the QPU reads the full-size map */
            bm_fade_accel(&rt.g, &rt.fade);
        } else
            g16_fade_apply(&rt.g, &rt.fade);
        rt.fade_us += timer_ticks() - tf;
    }
    rt.light_us += timer_ticks() - t0;
    return 0;
}

int l_quit(lua_State *L)
{
    (void)L;
    rt.quit = 1;
    return 0;
}
