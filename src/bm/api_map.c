/*
 * api_map.c: the map layers, the cell flags and the named zones of the sheet.
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

/* ---- the map (R11, 2026-10-04): up to 8 layers of the same size, drawn
 * in their order (layer 1, the MAP section, at the back), and 8 flags for
 * each 8x8 cell of the sheet (fget/fset), which map() can choose by and
 * mflags() reads under a rectangle (collisions with the map) */

int sheet_cells(void)
{
    return (rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL);
}

/* the layer of argument idx: a number (1..) or a name; absent: layer 1.
 * Returns the index (0-based); an unknown layer is an error. */
static int layer_arg(lua_State *L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return 0;
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *n = lua_tostring(L, idx);
        for (int i = 0; i < rt.nlayers; i++)
            if (!strcmp(rt.layer_name[i], n))
                return i;
        return luaL_error(L, "the map has no layer \"%s\"", n);
    }
    lua_Integer i = luaL_checkinteger(L, idx);
    if (i < 1 || i > rt.nlayers)
        return luaL_error(L, "the map has %d layer%s, not %d", rt.nlayers, rt.nlayers == 1 ? "" : "s", (int)i);
    return (int)i - 1;
}

/* map(mx, my, [x, y, mw, mh, layer, mask]): mask, only the cells whose
 * tile has one of those flags */
int l_map(lua_State *L)
{
    const int32_t v[8] = { ival(L, 1), ival(L, 2), oval(L, 3, 0), oval(L, 4, 0), oval(L, 5, rt.map.w),
                           oval(L, 6, rt.map.h), layer_arg(L, 7), oval(L, 8, 0) & 255 };
    int32_t *a;
    sheet_commit();
    if (gpu2d(D2_MAP, v))
        return 0;
    if (draw2d(D2_MAP, 8, &a)) {
        memcpy(a, v, sizeof v);
    } else {
        g16_map_t m = { rt.map.w, rt.map.h, rt.layer[v[6]] };
        g16_map_mask(&rt.g, &rt.sheet, &m, v[0], v[1], v[2], v[3], v[4], v[5], rt.flags, (uint8_t)v[7]);
    }
    return 0;
}

int l_mget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2), l = layer_arg(L, 3);
    lua_pushinteger(L, (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h) ? rt.layer[l][y * rt.map.w + x] : 0);
    return 1;
}

int l_mset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2), n = ival(L, 3), l = layer_arg(L, 4);
    if (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h) {
        settle_reads();                 /* a map() recorded draws the map as it was */
        rt.layer[l][y * rt.map.w + x] = (uint16_t)n;
    }
    return 0;
}

/* fget(n, [f]): the flags of sprite (cell) n, a byte; with f (0-7), that
 * flag as a boolean */
int l_fget(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int v = rt.flags && n >= 0 && n < sheet_cells() ? rt.flags[n] : 0;
    if (lua_isnoneornil(L, 2)) {
        lua_pushinteger(L, v);
        return 1;
    }
    lua_Integer f = luaL_checkinteger(L, 2);
    luaL_argcheck(L, f >= 0 && f < 8, 2, "a flag is 0 to 7");
    lua_pushboolean(L, v >> f & 1);
    return 1;
}

/* fset(n, f, on) sets or clears flag f (0-7) of sprite n; fset(n, byte)
 * all its flags at once */
int l_fset(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int v;
    if (lua_gettop(L) >= 3) {
        lua_Integer f = luaL_checkinteger(L, 2);
        luaL_argcheck(L, f >= 0 && f < 8, 2, "a flag is 0 to 7");
        v = -1 - (int)f;                /* below: that bit */
    } else {
        v = (int)(luaL_checkinteger(L, 2) & 255);
    }
    if (!rt.flags || n < 0 || n >= sheet_cells())
        return 0;
    settle_reads();                     /* a map() recorded with a mask draws them as they were */
    if (v < 0) {
        uint8_t bit = (uint8_t)(1u << (-1 - v));
        if (lua_toboolean(L, 3)) rt.flags[n] |= bit;
        else rt.flags[n] &= (uint8_t)~bit;
    } else {
        rt.flags[n] = (uint8_t)v;
    }
    rt.flags_dirty = 1;
    return 0;
}

/* mflags(x, y, [w, h, layer]): the flags of the tiles the rectangle
 * touches (pixels of the map drawn at 0, 0; a point without w and h), all
 * together (or); 0 = nothing there. The empty cell (0) and the outside of
 * the map have none. For collisions: mflags(x, y + h, w, 1) & SOLID. */
int l_mflags(lua_State *L)
{
    lua_Number x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2);
    lua_Number w = luaL_optnumber(L, 3, 0), h = luaL_optnumber(L, 4, 0);
    int l = layer_arg(L, 5);
    lua_Integer c0 = (lua_Integer)floor(x / G16_CELL), r0 = (lua_Integer)floor(y / G16_CELL);
    lua_Integer c1 = w > 0 ? (lua_Integer)ceil((x + w) / G16_CELL) - 1 : c0;
    lua_Integer r1 = h > 0 ? (lua_Integer)ceil((y + h) / G16_CELL) - 1 : r0;
    if (c1 < c0) c1 = c0;
    if (r1 < r0) r1 = r0;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= rt.map.w) c1 = rt.map.w - 1;
    if (r1 >= rt.map.h) r1 = rt.map.h - 1;
    int out = 0;
    const int nc = sheet_cells();
    for (lua_Integer r = r0; rt.flags && r <= r1; r++)
        for (lua_Integer c = c0; c <= c1; c++) {
            int n = rt.layer[l][r * rt.map.w + c];
            if (n > 0 && n < nc)
                out |= rt.flags[n];
        }
    lua_pushinteger(L, out);
    return 1;
}

/* msize([w, h]) -> w, h, layers: the map's size in cells and its layers;
 * with w and h every layer gets that size, the cells that fit staying where
 * they are */
int l_msize(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        lua_Integer w = luaL_checkinteger(L, 1), h = luaL_checkinteger(L, 2);
        luaL_argcheck(L, w >= 1 && w <= 65535, 1, "1 to 65535 cells");
        luaL_argcheck(L, h >= 1 && h <= 65535, 2, "1 to 65535 cells");
        luaL_argcheck(L, w * h <= (1 << 22), 2, "at most 4194304 cells");
        if (w != rt.map.w || h != rt.map.h) {
            uint16_t *nl[BM_LAYERS_MAX] = { 0 };
            for (int i = 0; i < rt.nlayers; i++)
                if (!(nl[i] = calloc((size_t)(w * h), 2))) {
                    for (int k = 0; k < i; k++)
                        free(nl[k]);
                    return luaL_error(L, "not enough memory for a %dx%d map", (int)w, (int)h);
                }
            settle_reads();
            int cw = w < rt.map.w ? (int)w : rt.map.w, ch = h < rt.map.h ? (int)h : rt.map.h;
            for (int i = 0; i < rt.nlayers; i++) {
                for (int y = 0; y < ch; y++)
                    memcpy(nl[i] + (size_t)y * w, rt.layer[i] + (size_t)y * rt.map.w, (size_t)cw * 2);
                free(rt.layer[i]);
                rt.layer[i] = nl[i];
            }
            rt.map.w = (int)w;
            rt.map.h = (int)h;
            rt.map.cells = rt.layer[0];
        }
    }
    lua_pushinteger(L, rt.map.w);
    lua_pushinteger(L, rt.map.h);
    lua_pushinteger(L, rt.nlayers);
    return 3;
}

/* mlayers([list]) -> the names of the layers, in their order. With a list
 * (1 to 8 entries) the map gets those layers: an entry is a name (the layer
 * of that name, or a new empty one) or {name, from} (a copy of layer `from`,
 * a number or a name; false: empty), so they can be renamed, moved, added
 * and taken away. */
int l_mlayers(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_Integer n = luaL_len(L, 1);
        luaL_argcheck(L, n >= 1 && n <= BM_LAYERS_MAX, 1, "1 to 8 layers");
        char names[BM_LAYERS_MAX][BM_LAYER_NAME + 1];
        int from[BM_LAYERS_MAX];
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, 1, i + 1);
            int t = lua_gettop(L);
            const char *name;
            from[i] = -1;
            if (lua_istable(L, t)) {
                lua_rawgeti(L, t, 1);
                name = luaL_checkstring(L, -1);
                lua_rawgeti(L, t, 2);
                if (lua_type(L, -1) == LUA_TSTRING || lua_isinteger(L, -1))
                    from[i] = layer_arg(L, lua_gettop(L));
                lua_pop(L, 2);
            } else {
                name = luaL_checkstring(L, t);
                for (int k = 0; k < rt.nlayers; k++)
                    if (!strcmp(rt.layer_name[k], name))
                        from[i] = k;
            }
            if (!name[0] || strlen(name) > BM_LAYER_NAME)
                return luaL_error(L, "mlayers: a layer's name is 1 to %d bytes", BM_LAYER_NAME);
            ksnprintf(names[i], sizeof names[i], "%s", name);
            for (int k = 0; k < i; k++)
                if (!strcmp(names[k], names[i]))
                    return luaL_error(L, "mlayers: two layers called \"%s\"", name);
            lua_settop(L, t - 1);
        }
        uint16_t *nl[BM_LAYERS_MAX] = { 0 };
        int taken[BM_LAYERS_MAX] = { 0 };
        const size_t cells = (size_t)rt.map.w * rt.map.h;
        for (int i = 0; i < n; i++) {
            if (from[i] >= 0 && !taken[from[i]]) {
                nl[i] = rt.layer[from[i]];      /* the layer itself */
                taken[from[i]] = 1;
                continue;
            }
            if (!(nl[i] = calloc(cells, 2))) {
                for (int k = 0; k < i; k++)
                    if (!(from[k] >= 0 && nl[k] == rt.layer[from[k]]))
                        free(nl[k]);
                return luaL_error(L, "not enough memory for the map's layers");
            }
            if (from[i] >= 0)
                memcpy(nl[i], rt.layer[from[i]], cells * 2);
        }
        settle_reads();
        for (int k = 0; k < rt.nlayers; k++)
            if (!taken[k])
                free(rt.layer[k]);
        for (int i = 0; i < BM_LAYERS_MAX; i++) {
            rt.layer[i] = i < n ? nl[i] : NULL;
            ksnprintf(rt.layer_name[i], sizeof rt.layer_name[i], "%s", i < n ? names[i] : "");
        }
        rt.nlayers = (int)n;
        rt.map.cells = rt.layer[0];
    }
    lua_createtable(L, rt.nlayers, 0);
    for (int i = 0; i < rt.nlayers; i++) {
        lua_pushstring(L, rt.layer_name[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ---- named zones of the sheet (the SPRITES section: bm Pixel, bmres.py,
 * mkbm.py --sprites): sprites and animations by their name */

/* the zone called `name`, or -1 */
static int zone_find(const char *name)
{
    if (strlen(name) > BM_MODEL_NAME)
        return -1;
    for (int i = 0; i < rt.nzones; i++)
        if (!strncmp((const char *)rt.zones + 4 + i * BM_SPRITE_SIZE, name, BM_MODEL_NAME))
            return i;
    return -1;
}

static int zone_arg(lua_State *L, int idx, bm_zone_t *z)
{
    const char *name = luaL_checkstring(L, idx);
    int i = zone_find(name);
    if (i < 0)
        return luaL_error(L, "the sheet has no sprite zone \"%s\"", name);
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    c.sprites = rt.zones;
    c.zones = (uint16_t)rt.nzones;
    bm_zone(&c, i, z);
    return i;
}

/* zone(name) -> x, y, w, h, frames, fps, slices: where the zone is in the sheet (its
 * first frame), or nil; slices is 0 for a plain zone */
int l_zone(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (zone_find(name) < 0) {
        lua_pushnil(L);
        return 1;
    }
    bm_zone_t z;
    zone_arg(L, 1, &z);
    lua_pushinteger(L, z.x);
    lua_pushinteger(L, z.y);
    lua_pushinteger(L, z.w);
    lua_pushinteger(L, z.h);
    lua_pushinteger(L, z.frames);
    lua_pushinteger(L, z.fps);
    lua_pushinteger(L, z.slices);
    return 7;
}

/* strnlen (POSIX, not C11: bmhost builds this file as C11) */
size_t name_len(const char *s, size_t max)
{
    const char *end = memchr(s, 0, max);
    return end ? (size_t)(end - s) : max;
}

/* zones() -> the names of the zones, in their order */
int l_zones(lua_State *L)
{
    lua_createtable(L, rt.nzones, 0);
    for (int i = 0; i < rt.nzones; i++) {
        lua_pushlstring(L, (const char *)rt.zones + 4 + i * BM_SPRITE_SIZE,
                        name_len((const char *)rt.zones + 4 + i * BM_SPRITE_SIZE, BM_MODEL_NAME));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static const char *const box_kinds[] = { "hurt", "hit", "body" };

/* zboxes(name, [frame, kind]) -> the boxes of a zone's frame (hitboxes and
 * hurtboxes, the BOXES section): a list of {x, y, w, h, kind, frame}, x and
 * y from the frame's top-left corner, kind "hurt", "hit", "body" or the
 * game's number (3..255). With frame (1..frames) its boxes and those of
 * every frame (frame 0); without, all the zone's. kind (a name or a
 * number) keeps those of one kind. An empty list if there are none. */
int l_zboxes(lua_State *L)
{
    bm_zone_t z;
    zone_arg(L, 1, &z);
    int frame = 0, kind = -1;
    if (!lua_isnoneornil(L, 2))
        frame = (int)((luaL_checkinteger(L, 2) - 1) % z.frames + z.frames) % z.frames + 1;
    if (lua_type(L, 3) == LUA_TSTRING) {
        const char *k = lua_tostring(L, 3);
        for (int i = 0; i < 3; i++)
            if (!strcmp(k, box_kinds[i]))
                kind = i;
        if (kind < 0)
            return luaL_error(L, "zboxes: kind \"%s\" (hurt, hit, body or a number)", k);
    } else if (!lua_isnoneornil(L, 3)) {
        kind = (int)luaL_checkinteger(L, 3);
    }
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    c.boxes = rt.boxes;
    c.nboxes = (uint16_t)rt.nboxes;
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < rt.nboxes; i++) {
        bm_box_t b;
        bm_box(&c, i, &b);
        if (strcmp(b.zone, z.name) || (frame && b.frame && b.frame != frame) || (kind >= 0 && b.kind != kind))
            continue;
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, b.x); lua_setfield(L, -2, "x");
        lua_pushinteger(L, b.y); lua_setfield(L, -2, "y");
        lua_pushinteger(L, b.w); lua_setfield(L, -2, "w");
        lua_pushinteger(L, b.h); lua_setfield(L, -2, "h");
        if (b.kind < 3)
            lua_pushstring(L, box_kinds[b.kind]);
        else
            lua_pushinteger(L, b.kind);
        lua_setfield(L, -2, "kind");
        lua_pushinteger(L, b.frame); lua_setfield(L, -2, "frame");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}


/* zspr(name, x, y, [frame, flip_x, flip_y, zoom]) draws a zone: frame 1..
 * frames, or (nil) the one its fps gives at time(); returns the frame */
int l_zspr(lua_State *L)
{
    bm_zone_t z;
    zone_arg(L, 1, &z);
    int f;
    if (lua_isnoneornil(L, 4))
        f = z.fps ? (int)((uint64_t)(uint32_t)(timer_ticks() - rt.start_us) * z.fps / 1000000u % z.frames) : 0;
    else
        f = (int)((luaL_checkinteger(L, 4) - 1) % z.frames + z.frames) % z.frames;
    lua_Number x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3);
    int fx = lua_toboolean(L, 5), fy = lua_toboolean(L, 6);
    lua_Number zoom = luaL_optnumber(L, 7, 1);
    lua_settop(L, 0);
    lua_pushinteger(L, z.x + f * z.w);
    lua_pushinteger(L, z.y);
    lua_pushinteger(L, z.w);
    lua_pushinteger(L, z.h);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushboolean(L, fx);
    lua_pushboolean(L, fy);
    lua_pushnumber(L, zoom);
    l_sspr(L);
    lua_pushinteger(L, f + 1);
    return 1;
}

/* zstack(name, x, y, [rz=0, scale=1]): draws a stack zone (a zone with slices) as stack() does
 * (stackv() for a zone whose slices go down), with the step 1; a zone without slices is an error */
int l_zstack(lua_State *L)
{
    bm_zone_t z;
    zone_arg(L, 1, &z);
    if (z.slices < 2)
        return luaL_error(L, "zstack: the zone \"%s\" has no slices (a stack zone is \"stack name x y w h n\")",
                          z.name);
    const int32_t v[7] = { z.x, z.y, z.w, z.h, z.slices, ival(L, 2), ival(L, 3) };
    const float f[3] = { (float)luaL_optnumber(L, 4, 0), (float)luaL_optnumber(L, 5, 1), 1.0f };
    stack_draw(v, f, z.vertical);
    return 0;
}
