/*
 * api_snes16.c: Lua interface for the native ARM SNES-16 PPU + S-DSP engine (snes16.h).
 * Setup, VRAM/CGRAM/OAM/HDMA/DSP control from Lua; hot per-frame updates run either
 * here or directly in C from BMN1 native ARMv6 code via s16_host_call().
 */
#include "rt_internal.h"
#include "audio/audio.h"
#include "bmnative.h"
#include "drivers/timer.h"
#include "snes16.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

static uint32_t now_us(void) { return timer_ticks(); }

static void read_px64(lua_State *L, int idx, uint8_t out[64], const char *who)
{
    if (lua_isstring(L, idx)) {
        size_t n = 0;
        const char *s = lua_tolstring(L, idx, &n);
        if (n < 64)
            luaL_error(L, "%s: expected 64 pixel bytes", who);
        memcpy(out, s, 64);
        return;
    }
    luaL_checktype(L, idx, LUA_TTABLE);
    for (int i = 0; i < 64; i++) {
        lua_rawgeti(L, idx, i + 1);
        out[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
}

int l_s16_init(lua_State *L)
{
    const int lines = (int)luaL_optinteger(L, 1, 224);
    s16_set_clock(now_us);
    s16_reset(lines);
    api_vm_set_fast_host(s16_host_call);
    audio_set_ext(snes_audio_mix);
    return 0;
}

int l_s16_mode(lua_State *L)
{
    const int bgmode = (int)luaL_checkinteger(L, 1);
    const int tm = (int)luaL_checkinteger(L, 2);
    const int ts = (int)luaL_optinteger(L, 3, 0);
    const int bright = (int)luaL_optinteger(L, 4, 15);
    s16_mode(bgmode, tm, ts, bright);
    return 0;
}

int l_s16_bg(lua_State *L)
{
    const int layer = (int)luaL_checkinteger(L, 1);
    const uint16_t map_a = (uint16_t)luaL_checkinteger(L, 2);
    const uint16_t chr_a = (uint16_t)luaL_checkinteger(L, 3);
    const int sz = (int)luaL_optinteger(L, 4, 0);
    s16_bg(layer, map_a, chr_a, sz);
    return 0;
}

int l_s16_scroll(lua_State *L)
{
    s16_scroll((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

int l_s16_obj_cfg(lua_State *L)
{
    const int sz = (int)luaL_checkinteger(L, 1);
    const uint16_t base0 = (uint16_t)luaL_optinteger(L, 2, 0x8000);
    const uint16_t gap = (uint16_t)luaL_optinteger(L, 3, 0x2000);
    s16_obj_cfg(sz, base0, gap);
    return 0;
}

int l_s16_math(lua_State *L)
{
    const uint8_t cgwsel = (uint8_t)luaL_checkinteger(L, 1);
    const uint8_t cgadsub = (uint8_t)luaL_checkinteger(L, 2);
    const uint16_t fix = (uint16_t)luaL_optinteger(L, 3, 0);
    s16_colormath(cgwsel, cgadsub, fix);
    return 0;
}

int l_s16_m7(lua_State *L)
{
    const int16_t a = (int16_t)luaL_checkinteger(L, 1);
    const int16_t b = (int16_t)luaL_checkinteger(L, 2);
    const int16_t c = (int16_t)luaL_checkinteger(L, 3);
    const int16_t d = (int16_t)luaL_checkinteger(L, 4);
    const int16_t cx = (int16_t)luaL_optinteger(L, 5, 128);
    const int16_t cy = (int16_t)luaL_optinteger(L, 6, 112);
    const int16_t hx = (int16_t)luaL_optinteger(L, 7, 0);
    const int16_t hy = (int16_t)luaL_optinteger(L, 8, 0);
    const uint8_t sel = (uint8_t)luaL_optinteger(L, 9, 0);
    s16_m7(a, b, c, d, cx, cy, hx, hy, sel);
    return 0;
}

int l_s16_tile(lua_State *L)
{
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    const int bpp = (int)luaL_checkinteger(L, 2);
    uint8_t px[64];
    read_px64(L, 3, px, "s16_tile");
    s16_tile_write(addr, bpp, px);
    return 0;
}

int l_s16_map(lua_State *L)
{
    const int layer = (int)luaL_checkinteger(L, 1);
    const int tx = (int)luaL_checkinteger(L, 2);
    const int ty = (int)luaL_checkinteger(L, 3);
    const int tile = (int)luaL_checkinteger(L, 4);
    const int pal = (int)luaL_optinteger(L, 5, 0);
    const int prio = (int)luaL_optinteger(L, 6, 0);
    const int hf = lua_toboolean(L, 7);
    const int vf = lua_toboolean(L, 8);
    const uint16_t e = (uint16_t)((tile & 0x3FF) | ((pal & 7) << 10) | ((prio & 1) << 13) | (hf ? 0x4000 : 0) | (vf ? 0x8000 : 0));
    s16_map_set(layer, tx, ty, e);
    return 0;
}

int l_s16_m7_tile(lua_State *L)
{
    const uint8_t tile = (uint8_t)luaL_checkinteger(L, 1);
    uint8_t px[64];
    read_px64(L, 2, px, "s16_m7_tile");
    s16_m7_tile(tile, px);
    return 0;
}

int l_s16_m7_map(lua_State *L)
{
    s16_m7_map((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (uint8_t)luaL_checkinteger(L, 3));
    return 0;
}

int l_s16_cgram(lua_State *L)
{
    const uint8_t idx = (uint8_t)luaL_checkinteger(L, 1);
    uint16_t rgb555;
    if (lua_gettop(L) >= 4) {
        const unsigned r = (unsigned)luaL_checkinteger(L, 2) & 31u;
        const unsigned g = (unsigned)luaL_checkinteger(L, 3) & 31u;
        const unsigned b = (unsigned)luaL_checkinteger(L, 4) & 31u;
        rgb555 = (uint16_t)(r | (g << 5) | (b << 10));
    } else {
        rgb555 = (uint16_t)luaL_checkinteger(L, 2);
    }
    s16_cgram_set(idx, rgb555);
    return 0;
}

int l_s16_oam(lua_State *L)
{
    const int id = (int)luaL_checkinteger(L, 1);
    const int x = (int)luaL_checkinteger(L, 2);
    const int y = (int)luaL_checkinteger(L, 3);
    const int tile = (int)luaL_checkinteger(L, 4);
    const int pal = (int)luaL_optinteger(L, 5, 0);
    const int prio = (int)luaL_optinteger(L, 6, 2);
    const int hf = lua_toboolean(L, 7);
    const int vf = lua_toboolean(L, 8);
    const int big = lua_toboolean(L, 9);
    const int tab = (int)luaL_optinteger(L, 10, 0);
    const int attr = (tab & 1) | ((pal & 7) << 1) | ((prio & 3) << 4) | (hf ? 0x40 : 0) | (vf ? 0x80 : 0) | (big ? 0x100 : 0);
    s16_oam_set(id, x, y, tile, attr);
    return 0;
}

int l_s16_oam_clear(lua_State *L)
{
    s16_oam_clear((int)luaL_optinteger(L, 1, 0));
    return 0;
}

int l_s16_oam_rot(lua_State *L)
{
    s16_oam_rot(lua_toboolean(L, 1), (int)luaL_optinteger(L, 2, 0));
    return 0;
}

int l_s16_hdma_clear(lua_State *L)
{
    s16_hdma_clear((uint32_t)luaL_optinteger(L, 1, 0x0F));
    return 0;
}

int l_s16_hdma_grad(lua_State *L)
{
    s16_hdma_grad((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                  (uint16_t)luaL_checkinteger(L, 3), (uint16_t)luaL_checkinteger(L, 4));
    return 0;
}

int l_s16_hdma_band(lua_State *L)
{
    s16_hdma_band((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                  (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

int l_s16_hdma_m7(lua_State *L)
{
    s16_hdma_m7_persp((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                      (int32_t)luaL_checkinteger(L, 3), (int32_t)luaL_checkinteger(L, 4),
                      (int32_t)luaL_checkinteger(L, 5), (int32_t)luaL_checkinteger(L, 6),
                      (int32_t)luaL_checkinteger(L, 7));
    return 0;
}

int l_s16_hdma_mode(lua_State *L)
{
    s16_hdma_mode_band((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                       (uint8_t)luaL_checkinteger(L, 3), (uint8_t)luaL_checkinteger(L, 4),
                       (uint8_t)luaL_optinteger(L, 5, 0));
    return 0;
}

int l_s16_render(lua_State *L)
{
    (void)L;
    s16_render();
    return 0;
}

int l_s16_draw(lua_State *L)
{
    int lines;
    const uint16_t *src = s16_picture(&lines);
    return snes_blit_target(L, src, lines, "s16_draw");
}

int l_s16_to_sheet(lua_State *L)
{
    int lines;
    const uint16_t *src = s16_picture(&lines);
    return snes_blit_sheet(L, src, lines, "s16_to_sheet");
}

int l_s16_dsp_pcm(lua_State *L)
{
    const int slot = (int)luaL_checkinteger(L, 1);
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 3);
    const int loop_s = (int)luaL_optinteger(L, 4, -1);
    if (n <= 0 || n > 16384)
        return luaL_error(L, "s16_dsp_pcm: 1..16384 samples expected");
    int16_t *buf = malloc((size_t)n * sizeof(int16_t));
    if (!buf)
        return luaL_error(L, "s16_dsp_pcm: out of memory");
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 3, i + 1);
        buf[i] = (int16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    const int bytes = s16_dsp_upload_pcm16(slot, addr, buf, n, loop_s);
    free(buf);
    lua_pushinteger(L, bytes);
    return 1;
}

int l_s16_dsp_play(lua_State *L)
{
    const int voice = (int)luaL_checkinteger(L, 1);
    const int slot = (int)luaL_checkinteger(L, 2);
    const int pitch = (int)luaL_optinteger(L, 3, 0x1000);
    const int adsr1 = (int)luaL_optinteger(L, 4, 0xFF);
    const int adsr2 = (int)luaL_optinteger(L, 5, 0xE0);
    const int vl = (int)luaL_optinteger(L, 6, 96);
    const int vr = (int)luaL_optinteger(L, 7, vl);
    s16_dsp_play(voice, slot, pitch, adsr1, adsr2, vl, vr);
    return 0;
}

int l_s16_dsp_stop(lua_State *L)
{
    s16_dsp_stop((int)luaL_checkinteger(L, 1));
    return 0;
}

int l_s16_dsp_step(lua_State *L)
{
    s16_dsp_step((int)luaL_optinteger(L, 1, 534));
    return 0;
}

int l_s16_stats(lua_State *L)
{
    if (lua_isinteger(L, 1))
        s16_record_arm_us((uint32_t)lua_tointeger(L, 1));
    snes16_stats_t st;
    s16_get_stats(&st);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, (lua_Integer)st.us_ppu); lua_setfield(L, -2, "ppu_us");
    lua_pushinteger(L, (lua_Integer)st.us_dsp); lua_setfield(L, -2, "dsp_us");
    lua_pushinteger(L, (lua_Integer)st.us_arm); lua_setfield(L, -2, "arm_us");
    return 1;
}
