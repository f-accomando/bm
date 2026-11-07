/*
 * api_text.c: sget, sset, print and font (the sheet's pixels and the text of print()).
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

int l_sget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h || !rt.sheet.alpha[y * rt.sheet.w + x])
        lua_pushnil(L);
    else {
        lua_pushinteger(L, g16_to_rgb24(rt.sheet.px[y * rt.sheet.w + x]));
        uint8_t a = rt.sheet.alpha[y * rt.sheet.w + x];
        lua_pushinteger(L, a == 1 ? 255 : a);
        return 2;
    }
    return 1;
}

/* sset(x, y, colour [, alpha]) - nil colour = transparent */
int l_sset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h)
        return 0;
    int opaque = !lua_isnoneornil(L, 3);
    uint8_t alpha = 0;
    if (opaque) {
        if (!lua_isnoneornil(L, 4)) {
            int a = (int)luaL_checkinteger(L, 4);
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            alpha = (uint8_t)a;
        } else {
            alpha = 1;
        }
    }
    later_paint();                      /* the frame held's sprites as they were (bm3d 6.9) */
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    g16_sheet_set_alpha(&rt.sheet, x, y, alpha > 0 ? col(L, 3, 0) : 0, alpha);
    rt.cell_dirty[(y / G16_CELL) * (rt.sheet.w / G16_CELL) + x / G16_CELL] = 1;
    rt.sheet_dirty = 1;
    proj_sheet_drawn = 1;
    return 0;
}

/* print(text, x, y [, colour, scale]) */
int l_print(lua_State *L)
{
    /* read the numeric arguments first: luaL_tolstring pushes a value */
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    int scale = oval(L, 5, 1);
    if (scale > 8) scale = 8;
    size_t len;
    const char *s = luaL_tolstring(L, 1, &len);
    int32_t *a;
    if (gpu2d_open() && gpu3d_text2d(&rt.g, x, y, s, c, scale)) {
        g16_t m = rt.g;                 /* where it ends, as drawn (bm3d 4.8: the GPU drew it) */
        m.cx0 = m.cy0 = 0x40000000;
        m.cx1 = m.cy1 = -0x40000000;
        lua_pushinteger(L, g16_text_scaled(&m, x, y, s, c, scale));
        return 1;
    }
    if (draw2d(D2_TEXT, 4 + (uint32_t)(len + 4) / 4, &a)) {
        a[0] = x; a[1] = y; a[2] = c; a[3] = scale;
        memcpy(a + 4, s, len + 1);
        /* where it ends, as drawn (nothing drawn: no clip rectangle) */
        g16_t m = rt.g;
        m.cx0 = m.cy0 = 0x40000000;
        m.cx1 = m.cy1 = -0x40000000;
        lua_pushinteger(L, g16_text_scaled(&m, x, y, s, c, scale));
        return 1;
    }
    lua_pushinteger(L, g16_text_scaled(&rt.g, x, y, s, c, scale));
    return 1;
}

/* font([name]): the font of print() from now on, "8x16" (the default),
 * "8x14" or "6x12" (also "large", "medium", "small"); returns the width
 * and height of a character of the current font */
int l_font(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        const char *n = luaL_checkstring(L, 1);
        if (!strcmp(n, "6x12") || !strcmp(n, "small")) rt.g.font = &font_console_6x12;
        else if (!strcmp(n, "8x14") || !strcmp(n, "medium")) rt.g.font = &font_console_8x14;
        else if (!strcmp(n, "8x16") || !strcmp(n, "large")) rt.g.font = &font_console_8x16;
        else {
            const font_t *f = font_find(n);
            if (f) rt.g.font = f;
            else return luaL_argerror(L, 1, "\"6x12\", \"8x14\", \"8x16\" or .bmf font name");
        }
    }
    lua_pushinteger(L, rt.g.font->width);
    lua_pushinteger(L, rt.g.font->height);
    return 2;
}

static void ensure_vfont(void)
{
    if (!rt.vfont_loaded) {
        size_t len = 0;
        const uint8_t *data = g16_vfont_default_data(&len);
        if (g16_vfont_init(&rt.vfont, data, len, 0, 16.0f) == 0) {
            rt.vfont_loaded = 1;
        }
    }
}

/* vfont([data_or_path, [size]]): sets or scales the active vector font.
 * Returns ascent, descent, line_gap, size. */
int l_vfont(lua_State *L)
{
    if (lua_isnumber(L, 1)) {
        ensure_vfont();
        float sz = (float)lua_tonumber(L, 1);
        if (sz > 0) g16_vfont_size(&rt.vfont, sz);
    } else if (lua_isstring(L, 1)) {
        size_t slen = 0;
        const char *s = lua_tolstring(L, 1, &slen);
        float sz = (float)luaL_optnumber(L, 2, 16.0);
        uint8_t *data = NULL;
        size_t dlen = 0;
        int owns = 0;

        if (slen >= 12 && (memcmp(s, "\0\1\0\0", 4) == 0 ||
                           memcmp(s, "OTTO", 4) == 0 ||
                           memcmp(s, "true", 4) == 0)) {
            data = malloc(slen);
            if (data) {
                memcpy(data, s, slen);
                dlen = slen;
                owns = 1;
            }
        } else {
            /* Try FAT first */
            fat_entry_t e;
            memset(&e, 0, sizeof e);
            if (fat_find(s, &e) == 0 && !e.is_dir && fat_load(&e, &data, &dlen) == 0) {
                owns = 1;
            } else {
                /* Host fallback fopen */
                FILE *f = fopen(s, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsz = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (fsz > 0) {
                        data = malloc(fsz);
                        if (data && fread(data, 1, fsz, f) == (size_t)fsz) {
                            dlen = (size_t)fsz;
                            owns = 1;
                        } else {
                            free(data);
                            data = NULL;
                        }
                    }
                    fclose(f);
                }
            }
        }

        if (data && dlen > 0) {
            if (g16_vfont_init(&rt.vfont, data, dlen, owns, sz) == 0) {
                rt.vfont_loaded = 1;
            } else {
                if (owns) free(data);
                return luaL_error(L, "vfont: invalid TrueType font");
            }
        } else {
            return luaL_error(L, "vfont: font file not found '%s'", s);
        }
    } else {
        ensure_vfont();
        if (!lua_isnoneornil(L, 2)) {
            float sz = (float)luaL_checknumber(L, 2);
            if (sz > 0) g16_vfont_size(&rt.vfont, sz);
        }
    }

    lua_pushinteger(L, rt.vfont.ascent);
    lua_pushinteger(L, rt.vfont.descent);
    lua_pushinteger(L, rt.vfont.line_gap);
    lua_pushnumber(L, rt.vfont.size);
    return 4;
}

/* vprint(text, x, y [, colour, size]) */
int l_vprint(lua_State *L)
{
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    if (!lua_isnoneornil(L, 5)) {
        float sz = (float)luaL_checknumber(L, 5);
        ensure_vfont();
        if (sz > 0 && sz != rt.vfont.size) g16_vfont_size(&rt.vfont, sz);
    } else {
        ensure_vfont();
    }
    size_t len;
    const char *s = luaL_tolstring(L, 1, &len);
    int next_x = g16_vprint(&rt.g, &rt.vfont, s, x, y, c);
    lua_pushinteger(L, next_x);
    return 1;
}

/* vmeasure(text [, size]) -> w, h */
int l_vmeasure(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    if (!lua_isnoneornil(L, 2)) {
        float sz = (float)luaL_checknumber(L, 2);
        ensure_vfont();
        if (sz > 0 && sz != rt.vfont.size) g16_vfont_size(&rt.vfont, sz);
    } else {
        ensure_vfont();
    }
    int w = 0, h = 0;
    g16_vmeasure(&rt.vfont, s, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}
