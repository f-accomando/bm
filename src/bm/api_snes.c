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
#include "audio/audio.h"
#include "drivers/timer.h"
#include "snes.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

static uint32_t now_us(void) { return timer_ticks(); }

/* The instruction cache miss counter of the Pi (src/kernel/pmu.c); not in the PC's host program, nor in the RGB30's kernel. */
extern int pmu_start_icache(void) __attribute__((weak));
extern uint32_t pmu_icache_misses(void) __attribute__((weak));

int l_snes_load(lua_State *L)
{
    snes_set_clock(now_us);
    snes_set_counter(pmu_start_icache && pmu_icache_misses && pmu_start_icache() ? pmu_icache_misses : NULL);
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
    snprintf(hex, sizeof hex, "ppu us: bg %lu obj %lu win %lu comp %lu mix %lu", (unsigned long)d.us_bg, (unsigned long)d.us_obj, (unsigned long)d.us_win, (unsigned long)d.us_comp, (unsigned long)d.us_mix);
    if (d.im_cpu | d.im_spc | d.im_ppu) {
        const size_t n = strlen(hex);
        snprintf(hex + n, sizeof hex - n, " | icache miss: cpu %lu spc %lu ppu %lu", (unsigned long)d.im_cpu, (unsigned long)d.im_spc, (unsigned long)d.im_ppu);
    }
    lua_pushstring(L, hex); lua_setfield(L, -2, "us_ppu");
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
