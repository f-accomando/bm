/*
 * api_snes.c: the Lua side of the SNES core (snes.h): what the cartridge carts/snes calls. The cartridge keeps the
 * list of ROMs, the menu and the battery RAM files; this runs the machine.
 *
 *   snes_load(bytes)            -> true, or nil and why        loads and resets
 *   snes_reset()                                               a reset of the machine
 *   snes_frame(pad1, pad2, [render])                           one frame; a pad is bits (SNES_PAD_* in snes.h);
 *                                                              render false: the picture is not drawn
 *   snes_draw(target)           -> lines                       copies the picture (256 x lines) to the top left of a
 *                                                              render target of 256 x 240 or bigger
 *   snes_info()                 -> {title, hirom, size, sram}
 *   snes_sram([bytes])          -> the battery RAM as a string if it changed since the last call, else nil;
 *                                  with bytes: puts them in it
 *   snes_debug()                -> {pc, wai, stp, io = {..}, spc = "boot"/"run"/"halt", spc_pc, inidisp, tm, nmi, frames}
 */
#include "rt_internal.h"
#include "snes.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

int l_snes_load(lua_State *L)
{
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    const char *why = "";
    if (snes_load((const uint8_t *)data, len, &why) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_snes_reset(lua_State *L)
{
    (void)L;
    snes_reset();
    return 0;
}

int l_snes_frame(lua_State *L)
{
    snes_frame((uint16_t)luaL_optinteger(L, 1, 0), (uint16_t)luaL_optinteger(L, 2, 0), lua_isnone(L, 3) || lua_toboolean(L, 3));
    return 0;
}

int l_snes_draw(lua_State *L)
{
    int w, h, lines;
    uint16_t *dst = target_pixels(L, 1, &w, &h);
    if (!dst)
        return luaL_error(L, "snes_draw: a render target is needed");
    const uint16_t *src = snes_picture(&lines);
    if (w < SNES_W || h < lines)
        return luaL_error(L, "snes_draw: the target is smaller than %dx%d", SNES_W, lines);
    for (int y = 0; y < lines; y++)
        memcpy(dst + (size_t)y * (size_t)w, src + (size_t)y * SNES_W, SNES_W * sizeof(uint16_t));
    lua_pushinteger(L, lines);
    return 1;
}

int l_snes_info(lua_State *L)
{
    const snes_info_t *i = snes_info();
    lua_createtable(L, 0, 5);
    lua_pushstring(L, i->title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, i->hirom ? "HiROM" : "LoROM");
    lua_setfield(L, -2, "mapping");
    lua_pushinteger(L, (lua_Integer)i->rom_size);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, (lua_Integer)i->sram_size);
    lua_setfield(L, -2, "sram");
    return 1;
}

int l_snes_sram(lua_State *L)
{
    uint32_t size;
    uint8_t *ram = snes_sram(&size);
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        size_t len;
        const char *s = lua_tolstring(L, 1, &len);
        memcpy(ram, s, len < size ? len : size);
        snes_sram_clean();
        return 0;
    }
    if (!size || !snes_sram_dirty())
        return 0;
    lua_pushlstring(L, (const char *)ram, size);
    snes_sram_clean();
    return 1;
}

int l_snes_debug(lua_State *L)
{
    snes_debug_t d;
    snes_debug(&d);
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, d.pc); lua_setfield(L, -2, "pc");
    lua_pushboolean(L, d.flags & 1); lua_setfield(L, -2, "wai");
    lua_pushboolean(L, d.flags & 2); lua_setfield(L, -2, "stp");
    lua_createtable(L, 6, 0);
    for (int i = 0; i < 6; i++) { lua_pushinteger(L, d.io[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "io");
    lua_pushstring(L, d.spc_running ? (d.spc_halted ? "halt" : "run") : "boot"); lua_setfield(L, -2, "spc");
    lua_pushinteger(L, d.spc_pc); lua_setfield(L, -2, "spc_pc");
    lua_pushinteger(L, d.inidisp); lua_setfield(L, -2, "inidisp");
    lua_pushinteger(L, d.tm); lua_setfield(L, -2, "tm");
    lua_pushinteger(L, d.nmitimen); lua_setfield(L, -2, "nmi");
    lua_pushinteger(L, d.frames); lua_setfield(L, -2, "frames");
    char hex[96];
    int n = 0;
    for (int i = 0; i < 12; i++) n += snprintf(hex + n, sizeof hex - (size_t)n, "%02X", d.cpu_code[i]);
    lua_pushstring(L, hex); lua_setfield(L, -2, "cpu_code");
    n = 0;
    for (int i = 0; i < 12; i++) n += snprintf(hex + n, sizeof hex - (size_t)n, "%02X", d.spc_code[i]);
    lua_pushstring(L, hex); lua_setfield(L, -2, "spc_code");
    snprintf(hex, sizeof hex, "in %02X%02X%02X%02X out %02X%02X%02X%02X t %02X%02X%02X F1 %02X tg %02X%02X%02X en %X wr %lu",
             d.port_in[0], d.port_in[1], d.port_in[2], d.port_in[3], d.port_out[0], d.port_out[1], d.port_out[2],
             d.port_out[3], d.timer_out[0], d.timer_out[1], d.timer_out[2], d.spc_ctl, d.spc_tgt[0], d.spc_tgt[1],
             d.spc_tgt[2], d.spc_en, (unsigned long)d.spc_writes);
    lua_pushstring(L, hex); lua_setfield(L, -2, "ports");
    char tb[160];
    n = 0;
    for (int i = 0; i < 12; i++)
        n += snprintf(tb + n, sizeof tb - (size_t)n, "%c%d:%02X ", (d.trace[i] >> 12) == 1 ? 'c' : 's', (d.trace[i] >> 8) & 15, d.trace[i] & 255);
    lua_pushstring(L, tb); lua_setfield(L, -2, "trace");
    n = 0;
    for (int i = 0; i < 12; i++)
        n += snprintf(tb + n, sizeof tb - (size_t)n, "%d:%02X ", (d.trace_s[i] >> 8) & 15, d.trace_s[i] & 255);
    lua_pushstring(L, tb); lua_setfield(L, -2, "trace_s");
    return 1;
}
