/*
 * api_nes.c: the Lua side of the NES core (nes.h): what the cartridge bm/nes calls. The cartridge keeps the
 * list of ROMs, the menu and the battery RAM files; this runs the machine.
 *
 *   nes_load(bytes)            -> true, or nil and why        loads a .nes file and resets
 *   nes_reset()                                               a reset of the machine
 *   nes_frame(pad1, pad2, [render])                           one frame; a pad is bits (NES_PAD_* in nes.h);
 *                                                             render false: the picture is not drawn
 *   nes_draw(target, [w, h])   -> lines                       copies the picture (256 x 240) to the top left of a
 *                                                             render target, or scales it to w x h
 *   nes_to_sheet([x, y])       -> lines                       the picture into the cartridge's sprite sheet (for sspr)
 *   nes_info()                 -> {mapper, prg, chr, battery, mirroring, sram}
 *   nes_sram([bytes])          -> the battery RAM as a string if it changed since the last call, else nil;
 *                                 with bytes: puts them in it
 *   nes_debug()                -> {pc, a, x, y, s, p, ctrl, mask, status, frames, cycles, jammed, under, over, us, audio}
 */
#include "rt_internal.h"
#include "audio/audio.h"
#include "drivers/timer.h"
#include "nes.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

static uint32_t now_us(void) { return timer_ticks(); }

int l_nes_load(lua_State *L)
{
    nes_set_clock(now_us);
    audio_set_ext(nes_audio_mix);                  /* the sound of the machine goes out through the console's output */
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    const char *why = "";
    if (nes_load((const uint8_t *)data, len, &why) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_nes_reset(lua_State *L)
{
    (void)L;
    nes_reset();
    return 0;
}

int l_nes_frame(lua_State *L)
{
    nes_frame((uint8_t)luaL_optinteger(L, 1, 0), (uint8_t)luaL_optinteger(L, 2, 0), lua_isnone(L, 3) || lua_toboolean(L, 3));
    return 0;
}

int l_nes_draw(lua_State *L)
{
    return snes_blit_target(L, nes_picture(), NES_H_PIX, "nes_draw");
}

int l_nes_to_sheet(lua_State *L)
{
    return snes_blit_sheet(L, nes_picture(), NES_H_PIX, "nes_to_sheet");
}

int l_nes_info(lua_State *L)
{
    static const char *const mir[] = { "horizontal", "vertical", "one screen", "one screen", "four screens" };
    const nes_info_t *i = nes_info();
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, i->mapper); lua_setfield(L, -2, "mapper");
    lua_pushinteger(L, (lua_Integer)i->prg_size); lua_setfield(L, -2, "prg");
    lua_pushinteger(L, (lua_Integer)i->chr_size); lua_setfield(L, -2, "chr");
    lua_pushboolean(L, i->battery); lua_setfield(L, -2, "battery");
    lua_pushstring(L, mir[i->mirroring]); lua_setfield(L, -2, "mirroring");
    lua_pushinteger(L, (lua_Integer)i->sram_size); lua_setfield(L, -2, "sram");
    return 1;
}

int l_nes_sram(lua_State *L)
{
    uint32_t size;
    uint8_t *ram = nes_sram(&size);
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        size_t len;
        const char *s = lua_tolstring(L, 1, &len);
        memcpy(ram, s, len < size ? len : size);
        nes_sram_clean();
        return 0;
    }
    if (!size || !nes_sram_dirty())
        return 0;
    lua_pushlstring(L, (const char *)ram, size);
    nes_sram_clean();
    return 1;
}

int l_nes_debug(lua_State *L)
{
    nes_debug_t d;
    nes_debug(&d);
    lua_createtable(L, 0, 16);
    lua_pushinteger(L, d.pc); lua_setfield(L, -2, "pc");
    lua_pushinteger(L, d.a); lua_setfield(L, -2, "a");
    lua_pushinteger(L, d.x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, d.y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, d.s); lua_setfield(L, -2, "s");
    lua_pushinteger(L, d.p); lua_setfield(L, -2, "p");
    lua_pushinteger(L, d.ctrl); lua_setfield(L, -2, "ctrl");
    lua_pushinteger(L, d.mask); lua_setfield(L, -2, "mask");
    lua_pushinteger(L, d.status); lua_setfield(L, -2, "status");
    lua_pushinteger(L, d.frames); lua_setfield(L, -2, "frames");
    lua_pushinteger(L, d.cycles); lua_setfield(L, -2, "cycles");
    lua_pushboolean(L, d.jammed); lua_setfield(L, -2, "jammed");
    lua_pushinteger(L, d.audio_under); lua_setfield(L, -2, "under");
    lua_pushinteger(L, d.audio_over); lua_setfield(L, -2, "over");
    char buf[96];
    snprintf(buf, sizeof buf, "us frame %lu ppu %lu cpu %lu",
             (unsigned long)d.us_frame, (unsigned long)d.us_ppu, (unsigned long)d.us_cpu);
    lua_pushstring(L, buf); lua_setfield(L, -2, "us");
    snprintf(buf, sizeof buf, "audio: %s  under %lu over %lu",
             audio_ready() ? audio_status() : "no output",
             (unsigned long)d.audio_under, (unsigned long)d.audio_over);
    lua_pushstring(L, buf); lua_setfield(L, -2, "audio");
    return 1;
}
