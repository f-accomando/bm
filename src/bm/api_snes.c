/*
 * api_snes.c: the Lua side of the SNES core (snes.h): what the cartridge carts/snes calls. The cartridge keeps the
 * list of ROMs, the menu and the battery RAM files; this runs the machine.
 *
 *   snes_load(bytes)            -> true, or nil and why        loads and resets
 *   snes_dsp_firmware(bytes[, data]) -> true, or nil and why   the firmware of a DSP-1/2/3/4 cartridge (docs/progress/snes-dsp.md)
 *   snes_reset()                                               a reset of the machine
 *   snes_frame(pad1, pad2, [render])                           one frame; a pad is bits (SNES_PAD_* in snes.h);
 *                                                              render false: the picture is not drawn
 *   snes_draw(target)           -> lines                       copies the picture (256 x lines) to the top left of a
 *                                                              render target of 256 x 240 or bigger
 *   snes_info()                 -> {title, hirom, size, sram}
 *   snes_sram([bytes])          -> the battery RAM as a string if it changed since the last call, else nil;
 *                                  with bytes: puts them in it
 *   snes_debug()                -> {pc, wai, stp, io = {..}, spc = "boot"/"run"/"halt", spc_pc, inidisp, tm, nmi, frames}
 *
 * The debug API (docs/api/snes.md, "Debug API"): snes_peek, snes_poke, snes_regs, snes_step, snes_break, snes_run. Only a breakpoint and a step enter the
 * slow debug loop of the CPU; with none armed the machine runs the same loop as without the API.
 */
#include "rt_internal.h"
#include "audio/audio.h"
#include "drivers/timer.h"
#include "snes.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

static uint32_t now_us(void) { return timer_ticks(); }

/* The instruction cache miss counter of the Pi (src/kernel/pmu.c); not in the PC's host program, nor in the RGB30's kernel. */
extern int pmu_start_meter(void) __attribute__((weak));
extern void pmu_meter_read(uint32_t out[3]) __attribute__((weak));
extern void pmu_meter_event(int k) __attribute__((weak));

int l_snes_load(lua_State *L)
{
    snes_set_clock(now_us);
    if (pmu_start_meter && pmu_meter_read && pmu_meter_event && pmu_start_meter()) snes_set_meter(pmu_meter_read, pmu_meter_event);
    else snes_set_meter(NULL, NULL);
    audio_set_ext(snes_audio_mix);                 /* the sound of the machine goes out through the console's output */
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

/* snes_dsp_firmware(bytes [, data]) -> true, or nil and why: the firmware of the DSP chip (snes_info().dsp); one file of 8192 bytes, or program and data */
int l_snes_dsp_firmware(lua_State *L)
{
    size_t na, nb = 0;
    const char *a = luaL_checklstring(L, 1, &na), *b = lua_isnoneornil(L, 2) ? NULL : luaL_checklstring(L, 2, &nb);
    const char *why = "";
    if (snes_dsp_firmware((const uint8_t *)a, na, (const uint8_t *)b, nb, &why) != 0) {
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

int snes_blit_target(lua_State *L, const uint16_t *src, int lines, const char *who)
{
    int w, h;
    uint16_t *dst = target_pixels(L, 1, &w, &h);
    if (!dst)
        return luaL_error(L, "%s: a render target is needed", who);
    const int dw = (int)luaL_optinteger(L, 2, SNES_W), dh = (int)luaL_optinteger(L, 3, lines);
    if (dw < 1 || dh < 1 || dw > w || dh > h)
        return luaL_error(L, "%s: the size %dx%d does not fit the target (%dx%d)", who, dw, dh, w, h);
    if (dw == SNES_W && dh == lines) {
        for (int y = 0; y < lines; y++)
            memcpy(dst + (size_t)y * (size_t)w, src + (size_t)y * SNES_W, SNES_W * sizeof(uint16_t));
    } else {
        /* nearest neighbour to dw x dh: a table of the source column of each column, and a row that repeats is a copy */
        static uint16_t xmap[1280];
        if (dw > 1280)
            return luaL_error(L, "%s: at most 1280 pixels wide", who);
        for (int x = 0; x < dw; x++)
            xmap[x] = (uint16_t)(x * SNES_W / dw);
        int prev = -1;
        for (int y = 0; y < dh; y++) {
            const int sy = y * lines / dh;
            uint16_t *row = dst + (size_t)y * (size_t)w;
            if (sy == prev) {
                memcpy(row, row - w, (size_t)dw * sizeof(uint16_t));
                continue;
            }
            const uint16_t *s = src + (size_t)sy * SNES_W;
            for (int x = 0; x < dw; x++)
                row[x] = s[xmap[x]];
            prev = sy;
        }
    }
    lua_pushinteger(L, lines);
    return 1;
}

int l_snes_draw(lua_State *L)
{
    int lines;
    const uint16_t *src = snes_picture(&lines);
    return snes_blit_target(L, src, lines, "snes_draw");
}

/* snes_to_sheet([x, y]) -> lines: the picture (256 pixels wide, 224 or 239 lines) into the cartridge's sprite sheet at (x, y), opaque, so that `sspr`
 * draws it with a zoom (the GPU does it where it draws the 2D: no pixel of the big picture is written by the ARM). x and y are multiples of 8. */
int snes_blit_sheet(lua_State *L, const uint16_t *src, int lines, const char *who)
{
    const int dx = (int)luaL_optinteger(L, 1, 0), dy = (int)luaL_optinteger(L, 2, 0);
    g16_sheet_t *s = &rt.sheet;
    if (!s->px || dx < 0 || dy < 0 || (dx & 7) || (dy & 7) || dx + SNES_W > s->w || dy + lines > s->h)
        return luaL_error(L, "%s: the sprite sheet must hold 256 x %d pixels at (%d, %d), x and y multiples of 8", who, lines, dx, dy);
    const int per_row = s->w / G16_CELL;
    for (int y = 0; y < lines; y++) {
        memcpy(s->px + (size_t)(dy + y) * (size_t)s->w + (size_t)dx, src + (size_t)y * SNES_W, SNES_W * sizeof(uint16_t));
        memset(s->alpha + (size_t)(dy + y) * (size_t)s->w + (size_t)dx, 1, SNES_W);
    }
    for (int cy = dy / G16_CELL; cy < (dy + lines + G16_CELL - 1) / G16_CELL; cy++)
        for (int cx = dx / G16_CELL; cx < (dx + SNES_W) / G16_CELL; cx++)
            s->cell_opaque[cy * per_row + cx] = (dy + lines >= (cy + 1) * G16_CELL) ? 1 : s->cell_opaque[cy * per_row + cx];
    s->version++;
    lua_pushinteger(L, lines);
    return 1;
}

int l_snes_to_sheet(lua_State *L)
{
    int lines;
    const uint16_t *src = snes_picture(&lines);
    return snes_blit_sheet(L, src, lines, "snes_to_sheet");
}

/* snes_tiles(addr, bpp, tiles, cols, [sx, sy, cgbase, pal]) -> w, h: the tiles of the video RAM as pictures in the cartridge's sprite sheet, to be
 * exported (sheet_png) and drawn on in bm Pixel. addr: the byte address of the first tile; bpp 2, 4 or 8 (8, 32 or 64 bytes a tile); tiles: how many (up to 4096);
 * cols: tiles in a row. The colours come from the colour RAM: entry cgbase + pal * 2^bpp + pixel (pal is ignored with 8 bpp; cgbase 128 for the sprites).
 * Colour 0 of a tile is transparent. The rectangle at (sx, sy) must lie in the sheet. */
int l_snes_tiles(lua_State *L)
{
    const unsigned addr = (unsigned)luaL_checkinteger(L, 1) & 0xFFFF;
    const int bpp = (int)luaL_checkinteger(L, 2), tiles = (int)luaL_checkinteger(L, 3), cols = (int)luaL_checkinteger(L, 4);
    const int sx = (int)luaL_optinteger(L, 5, 0), sy = (int)luaL_optinteger(L, 6, 0);
    const int cgbase = (int)luaL_optinteger(L, 7, 0), pal = (int)luaL_optinteger(L, 8, 0);
    if (bpp != 2 && bpp != 4 && bpp != 8)
        return luaL_error(L, "snes_tiles: bpp is 2, 4 or 8");
    if (tiles < 1 || tiles > 4096 || cols < 1 || cols > 64)
        return luaL_error(L, "snes_tiles: 1 to 4096 tiles, 1 to 64 in a row");
    const int rows = (tiles + cols - 1) / cols, w = cols * 8, h = rows * 8;
    g16_sheet_t *s = &rt.sheet;
    if (!s->px || sx < 0 || sy < 0 || sx + w > s->w || sy + h > s->h)
        return luaL_error(L, "snes_tiles: the sprite sheet must hold %d x %d pixels at (%d, %d)", w, h, sx, sy);
    const int tb = bpp * 8;                              /* bytes a tile */
    uint16_t colour[256];
    for (int i = 0; i < (1 << bpp); i++) {
        uint8_t c[2];
        const int idx = (cgbase + (bpp == 8 ? 0 : pal << bpp) + i) & 255;
        snes_cgram_peek((uint16_t)(idx * 2), c, 2);
        const unsigned v = (unsigned)c[0] | (unsigned)c[1] << 8;
        const unsigned r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
        colour[i] = (uint16_t)((r << 11) | (g << 6) | (g >> 4) << 5 | b);
    }
    for (int t = 0; t < tiles; t++) {
        uint8_t d[64];
        snes_vram_peek((uint16_t)(addr + (unsigned)t * (unsigned)tb), d, tb);
        const int ox = sx + (t % cols) * 8, oy = sy + (t / cols) * 8;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int p = 0;
                for (int pl = 0; pl < bpp; pl++)       /* planes in pairs: bytes 16 * (pl / 2) + 2 * y + (pl & 1) */
                    p |= ((d[(pl >> 1) * 16 + y * 2 + (pl & 1)] >> (7 - x)) & 1) << pl;
                g16_sheet_set(s, ox + x, oy + y, colour[p], p != 0);
            }
    }
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

int l_snes_info(lua_State *L)
{
    const snes_info_t *i = snes_info();
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, i->dsp);
    lua_setfield(L, -2, "dsp");
    lua_pushboolean(L, i->dsp_fw);
    lua_setfield(L, -2, "dsp_fw");
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
    if (lua_isinteger(L, 1)) {                          /* snes_debug(addr): 32 bytes of the video RAM at addr, and the 32 bytes of colours of sprite palette 3 */
        uint8_t b[32];
        snes_vram_peek((uint16_t)lua_tointeger(L, 1), b, 32);
        lua_pushlstring(L, (const char *)b, 32); lua_setfield(L, -2, "vram");
        snes_cgram_peek(352, b, 32);
        lua_pushlstring(L, (const char *)b, 32); lua_setfield(L, -2, "cgram");
        char map[256];                                  /* one character for each 256 bytes of the video RAM: . all zero, F all FF, x anything else */
        for (int pg = 0; pg < 256; pg++) {
            uint8_t row[256];
            snes_vram_peek((uint16_t)(pg << 8), row, 256);
            int z = 1, f = 1;
            for (int i = 0; i < 256; i++) { z &= row[i] == 0; f &= row[i] == 0xFF; }
            map[pg] = z ? '.' : f ? 'F' : 'x';
        }
        lua_pushlstring(L, map, 256); lua_setfield(L, -2, "vmap");
    }
    lua_pushlstring(L, (const char *)d.oam, sizeof d.oam); lua_setfield(L, -2, "oam");
    char hex[192];
    snprintf(hex, sizeof hex, "rot %d@%d mode %d obsel %02X tm %02X ts %02X sc %02X %02X %02X %02X hofs %d %d %d %d vofs %d %d %d %d", d.oamrot, d.oam_reload >> 2, d.bgmode & 7, d.obsel, d.tm, d.ts,
             d.sc[0], d.sc[1], d.sc[2], d.sc[3], d.hofs[0], d.hofs[1], d.hofs[2], d.hofs[3], d.vofs[0], d.vofs[1], d.vofs[2], d.vofs[3]);
    lua_pushstring(L, hex); lua_setfield(L, -2, "regs");
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
    snprintf(hex, sizeof hex, "us frame %lu ppu %lu spc %lu cpu %lu dma %lu hdma %lu ins %lu", (unsigned long)d.us_frame, (unsigned long)d.us_ppu, (unsigned long)d.us_spc,
             (unsigned long)d.us_cpu, (unsigned long)d.us_dma, (unsigned long)d.us_hdma, (unsigned long)d.instr);
    if (d.gsu_instr) {                                  /* a Super FX cartridge: the GSU time (timed frames) and its instructions in a frame */
        const size_t l = strlen(hex);
        snprintf(hex + l, sizeof hex - l, " gsu %lu gins %lu", (unsigned long)d.us_gsu, (unsigned long)d.gsu_instr);
    }
    lua_pushstring(L, hex); lua_setfield(L, -2, "us");
    {
        char ml[720];                                  /* the PPU parts, then the ARM's counters by part (cpu, spc, ppu) when the meter runs */
        int n = snprintf(ml, sizeof ml, "ppu us: bg %lu obj %lu win %lu comp %lu mix %lu", (unsigned long)d.us_bg, (unsigned long)d.us_obj, (unsigned long)d.us_win, (unsigned long)d.us_comp, (unsigned long)d.us_mix);
        if (d.m_cycles[0] | d.m_cycles[1] | d.m_cycles[2]) {
            static const char *const names[3] = { "icache miss", "dcache miss", "mispredict" };
            n += snprintf(ml + n, sizeof ml - (size_t)n, " | arm instr cpu %lu spc %lu ppu %lu | cycles cpu %lu spc %lu ppu %lu",
                          (unsigned long)d.m_instr[0], (unsigned long)d.m_instr[1], (unsigned long)d.m_instr[2],
                          (unsigned long)d.m_cycles[0], (unsigned long)d.m_cycles[1], (unsigned long)d.m_cycles[2]);
            for (int k = 0; k < 3 && n < (int)sizeof ml - 60; k++)
                n += snprintf(ml + n, sizeof ml - (size_t)n, " | %s: cpu %lu spc %lu ppu %lu", names[k], (unsigned long)d.m_ev[k][0], (unsigned long)d.m_ev[k][1], (unsigned long)d.m_ev[k][2]);
        }
        n += snprintf(ml + n, sizeof ml - (size_t)n, " | bus ppuw %lu ppur %lu apu %lu wram %lu cpu %lu dmar %lu other %lu | dma runs %lu bytes %lu",
                      (unsigned long)d.bus_n[0], (unsigned long)d.bus_n[1], (unsigned long)d.bus_n[2], (unsigned long)d.bus_n[3], (unsigned long)d.bus_n[4],
                      (unsigned long)d.bus_n[5], (unsigned long)d.bus_n[6], (unsigned long)d.dma_runs, (unsigned long)d.dma_bytes);
        lua_pushstring(L, ml); lua_setfield(L, -2, "us_ppu");
    }
    {
        uint32_t under, over, kons;
        snes_audio_stats(&under, &over, &kons);
        snprintf(hex, sizeof hex, "audio: %s  voices started %lu  under %lu over %lu", audio_ready() ? audio_status() : "no output",
                 (unsigned long)kons, (unsigned long)under, (unsigned long)over);
        lua_pushstring(L, hex); lua_setfield(L, -2, "audio");
    }
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

/* ------------------------------------------------------------------ the debug API */
static uint32_t dbg_addr(lua_State *L, int i)
{
    const lua_Integer a = luaL_checkinteger(L, i);
    if (a < 0 || a > 0xFFFFFF) luaL_argerror(L, i, "the address is 0 to 0xFFFFFF");
    return (uint32_t)a;
}

/* snes_peek(addr24 [, len]) -> a byte, or with len a string of len bytes (up to 65536); no register side effect */
int l_snes_peek(lua_State *L)
{
    const uint32_t a = dbg_addr(L, 1);
    if (lua_isnoneornil(L, 2)) { lua_pushinteger(L, snes_peek8(a)); return 1; }
    const lua_Integer n = luaL_checkinteger(L, 2);
    if (n < 0 || n > 65536) return luaL_argerror(L, 2, "the length is 0 to 65536");
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (lua_Integer i = 0; i < n; i++) luaL_addchar(&b, (char)snes_peek8((a + (uint32_t)i) & 0xFFFFFF));
    luaL_pushresult(&b);
    return 1;
}

/* snes_mem(space, addr, len) -> a string of the video memory: "vram" (65536 bytes), "cgram" (512), "oam" (544). It has no side effect and costs no clock. */
int l_snes_mem(lua_State *L)
{
    const char *sp = luaL_checkstring(L, 1);
    const lua_Integer a = luaL_checkinteger(L, 2), n = luaL_checkinteger(L, 3);
    int size;
    if (!strcmp(sp, "vram")) size = 65536;
    else if (!strcmp(sp, "cgram")) size = 512;
    else if (!strcmp(sp, "oam")) size = 544;
    else return luaL_argerror(L, 1, "the space is vram, cgram or oam");
    if (a < 0 || a >= size) return luaL_argerror(L, 2, "the address is outside the memory");
    if (n < 0 || n > 65536) return luaL_argerror(L, 3, "the length is 0 to 65536");
    const int cnt = (a + n > size) ? (int)(size - a) : (int)n;      /* it stops at the end of the memory */
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    if (!strcmp(sp, "oam")) {
        static snes_debug_t d;
        snes_debug(&d);
        luaL_addlstring(&b, (const char *)d.oam + a, (size_t)cnt);
    } else {
        uint8_t t[256];
        for (int done = 0; done < cnt;) {
            const int c = cnt - done > 256 ? 256 : cnt - done;
            if (size == 512) snes_cgram_peek((uint16_t)(a + done), t, c); else snes_vram_peek((uint16_t)(a + done), t, c);
            luaL_addlstring(&b, (const char *)t, (size_t)c);
            done += c;
        }
    }
    luaL_pushresult(&b);
    return 1;
}

/* snes_poke(addr24, byte) */
int l_snes_poke(lua_State *L)
{
    const uint32_t a = dbg_addr(L, 1);
    snes_poke8(a, (uint8_t)luaL_checkinteger(L, 2));
    return 0;
}

/* snes_regs() -> {a, x, y, s, d, db, pb, pc, p, e, pc24, cycle, frames, wai, stp} */
int l_snes_regs(lua_State *L)
{
    snes_dbg_regs_t r;
    snes_dbg_regs(&r);
    lua_createtable(L, 0, 15);
#define F(name, v) lua_pushinteger(L, (lua_Integer)(v)); lua_setfield(L, -2, name)
    F("a", r.a); F("x", r.x); F("y", r.y); F("s", r.s); F("d", r.d); F("db", r.db); F("pb", r.pb); F("pc", r.pc); F("p", r.p);
    F("pc24", ((uint32_t)r.pb << 16) | r.pc); F("cycle", r.cycle); F("frames", r.frames);
#undef F
    lua_pushboolean(L, r.e); lua_setfield(L, -2, "e");
    lua_pushboolean(L, r.waiting); lua_setfield(L, -2, "wai");
    lua_pushboolean(L, r.stopped); lua_setfield(L, -2, "stp");
    return 1;
}

/* snes_step() -> pc24 after the instruction */
int l_snes_step(lua_State *L)
{
    snes_dbg_step();
    snes_dbg_regs_t r;
    snes_dbg_regs(&r);
    lua_pushinteger(L, ((lua_Integer)r.pb << 16) | r.pc);
    return 1;
}

/* snes_break(addr24 | nil) -> the count of breakpoints; an address adds one (up to 8), nil clears all; nil and a text when the table is full */
int l_snes_break(lua_State *L)
{
    int n;
    if (lua_isnoneornil(L, 1)) n = snes_dbg_break(-1);
    else n = snes_dbg_break((int32_t)dbg_addr(L, 1));
    if (n < 0) { lua_pushnil(L); lua_pushstring(L, "8 breakpoints are armed"); return 2; }
    lua_pushinteger(L, n);
    return 1;
}

/* snes_run([pad1, pad2, render]) -> nil when the frame is done, or the pc24 of the breakpoint that stopped it (call it again to go on with the same frame) */
int l_snes_run(lua_State *L)
{
    const int r = snes_frame_break((uint16_t)luaL_optinteger(L, 1, 0), (uint16_t)luaL_optinteger(L, 2, 0), lua_isnone(L, 3) || lua_toboolean(L, 3), 1);
    if (r < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, r);
    return 1;
}
