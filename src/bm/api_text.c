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
    else
        lua_pushinteger(L, g16_to_rgb24(rt.sheet.px[y * rt.sheet.w + x]));
    return 1;
}

/* sset(x, y, colour) - nil colour = transparent */
int l_sset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h)
        return 0;
    int opaque = !lua_isnoneornil(L, 3);
    later_paint();                      /* the frame held's sprites as they were (bm3d 6.9) */
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    g16_sheet_set(&rt.sheet, x, y, opaque ? col(L, 3, 0) : 0, opaque);
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
        else return luaL_argerror(L, 1, "\"6x12\", \"8x14\" or \"8x16\"");
    }
    lua_pushinteger(L, rt.g.font->width);
    lua_pushinteger(L, rt.g.font->height);
    return 2;
}
