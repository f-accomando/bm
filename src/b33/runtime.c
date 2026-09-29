/*
 * .b33 runtime: sandboxed Lua 5.4 state + drawing API in C (gfx16) + frame
 * loop. Lua only runs game logic; every pixel is drawn by C.
 */
#include "runtime.h"
#include "b33.h"
#include "gfx16.h"
#include "r3d.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "lib/crc32.h"
#include "kernel/input.h"
#include "usb/hid.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "audio/audio.h"
#include "drivers/dma.h"
#include "arch/cache.h"
#include "kernel/crumbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define FRAME_US        16667
#define HOLD_FRAMES     10          /* a serial key press counts as held this long */
#define HOOK_EVERY      1000        /* instructions between hook calls */
#define FRAME_BUDGET    20000       /* x HOOK_EVERY = 20 M instructions per callback */

enum { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_START, BTN_SELECT,
       BTN_COUNT };

static struct {
    g16_t g;
    g16_sheet_t sheet;
    g16_map_t map;
    uint8_t *cell_dirty;
    int sheet_dirty;
    uint8_t hold[BTN_COUNT];
    int uses_xy;                /* the cart asked for btn(6) or btn(7) */
    uint16_t now, prev;             /* button bits this frame / last frame, any player */
    uint16_t pnow[INPUT_PLAYERS], pprev[INPUT_PLAYERS];     /* the same per player */
    uint32_t praw[INPUT_PLAYERS];   /* HID bits per player (stick() from the cross) */
    int local;                      /* player of the keyboard / USB / serial, or -1 */
    uint32_t start_us, frame;
    uint32_t last_cpu_us, fps;
    uint32_t present_us;        /* last copy of the frame to the framebuffer */
    uint32_t hook_count;
    int esc;
    int quit;
    r3d_t r3d;
    int r3d_ready;
    g16_light_t light;          /* light_begin() .. light_end() */
    int text_mode;              /* keyp() was called: the keyboard types */
    int esc_wait;               /* frames since a serial Esc */
    int esc_num;                /* ESC [ n ~ */
    uint8_t tq[256];            /* typed keys for keyp() */
    uint8_t tq_head, tq_tail;
    char save_name[13];         /* "1A2B3C4D.SAV": CRC-32 of title and author */
} rt;

#define MESH_MT "b33.mesh"

/* ---------------------------------------------------------------- helpers */

static uint16_t col(lua_State *L, int idx, uint32_t def)
{
    return g16_rgb24((uint32_t)luaL_optinteger(L, idx, def));
}

static int ival(lua_State *L, int idx)
{
    return (int)luaL_checknumber(L, idx);
}

static int oval(lua_State *L, int idx, int def)
{
    return lua_isnoneornil(L, idx) ? def : (int)luaL_checknumber(L, idx);
}

static void sheet_commit(void)
{
    if (!rt.sheet_dirty)
        return;
    int per_row = rt.sheet.w / G16_CELL, n = per_row * (rt.sheet.h / G16_CELL);
    for (int i = 0; i < n; i++)
        if (rt.cell_dirty[i]) {
            g16_sheet_update_cell(&rt.sheet, i % per_row, i / per_row);
            rt.cell_dirty[i] = 0;
        }
    rt.sheet_dirty = 0;
}

/* ---------------------------------------------------------------- API */

static int l_cls(lua_State *L)      { g16_cls(&rt.g, col(L, 1, 0)); return 0; }
static int l_pset(lua_State *L)     { g16_pset(&rt.g, ival(L, 1), ival(L, 2), col(L, 3, 0xFFFFFF)); return 0; }
static int l_line(lua_State *L)     { g16_line(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_rect(lua_State *L)     { g16_rect(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_rectfill(lua_State *L) { g16_rectfill(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_circ(lua_State *L)     { g16_circ(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), col(L, 4, 0xFFFFFF)); return 0; }
static int l_circfill(lua_State *L) { g16_circfill(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), col(L, 4, 0xFFFFFF)); return 0; }

static int l_pget(lua_State *L)
{
    int c = g16_pget(&rt.g, ival(L, 1), ival(L, 2));
    if (c < 0) lua_pushnil(L);
    else lua_pushinteger(L, g16_to_rgb24((uint16_t)c));
    return 1;
}

static int l_spr(lua_State *L)
{
    sheet_commit();
    g16_spr(&rt.g, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), oval(L, 4, 1), oval(L, 5, 1),
            lua_toboolean(L, 6), lua_toboolean(L, 7));
    return 0;
}

static int l_sspr(lua_State *L)
{
    sheet_commit();
    g16_sspr(&rt.g, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
             lua_toboolean(L, 7), lua_toboolean(L, 8));
    return 0;
}

static int l_map(lua_State *L)
{
    sheet_commit();
    g16_map(&rt.g, &rt.sheet, &rt.map, ival(L, 1), ival(L, 2), oval(L, 3, 0), oval(L, 4, 0),
            oval(L, 5, rt.map.w), oval(L, 6, rt.map.h));
    return 0;
}

static int l_mget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    lua_pushinteger(L, (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h) ? rt.map.cells[y * rt.map.w + x] : 0);
    return 1;
}

static int l_mset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h)
        rt.map.cells[y * rt.map.w + x] = (uint16_t)ival(L, 3);
    return 0;
}

static int l_sget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h || !rt.sheet.alpha[y * rt.sheet.w + x])
        lua_pushnil(L);
    else
        lua_pushinteger(L, g16_to_rgb24(rt.sheet.px[y * rt.sheet.w + x]));
    return 1;
}

/* sset(x, y, colour) - nil colour = transparent */
static int l_sset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h)
        return 0;
    int opaque = !lua_isnoneornil(L, 3);
    g16_sheet_set(&rt.sheet, x, y, opaque ? col(L, 3, 0) : 0, opaque);
    rt.cell_dirty[(y / G16_CELL) * (rt.sheet.w / G16_CELL) + x / G16_CELL] = 1;
    rt.sheet_dirty = 1;
    return 0;
}

/* print(text, x, y [, colour, scale]) */
static int l_print(lua_State *L)
{
    /* read the numeric arguments first: luaL_tolstring pushes a value */
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    int scale = oval(L, 5, 1);
    if (scale > 8) scale = 8;
    const char *s = luaL_tolstring(L, 1, NULL);
    lua_pushinteger(L, g16_text_scaled(&rt.g, x, y, s, c, scale));
    return 1;
}

static int l_camera(lua_State *L) { g16_camera(&rt.g, oval(L, 1, 0), oval(L, 2, 0)); return 0; }
static int l_clip(lua_State *L)   { g16_clip(&rt.g, oval(L, 1, 0), oval(L, 2, 0), oval(L, 3, 0), oval(L, 4, 0)); return 0; }

static int l_rgb(lua_State *L)
{
    lua_Integer r = luaL_checkinteger(L, 1) & 255, g = luaL_checkinteger(L, 2) & 255, b = luaL_checkinteger(L, 3) & 255;
    lua_pushinteger(L, r << 16 | g << 8 | b);
    return 1;
}

/* A cartridge that never asks for X or Y gets them as A and B (square and
 * triangle keep working in the older games). */
static void note_xy(int b)
{
    if (b == BTN_X || b == BTN_Y)
        rt.uses_xy = 1;
}

/* btn(i [, p]): without p any player; p = 1..4 that player only */
static int buttons(lua_State *L, uint16_t *now, uint16_t *prev)
{
    int b = ival(L, 1);
    note_xy(b);
    if (lua_isnoneornil(L, 2)) {
        *now = rt.now;
        *prev = rt.prev;
    } else {
        int p = ival(L, 2);
        *now = p >= 1 && p <= INPUT_PLAYERS ? rt.pnow[p - 1] : 0;
        *prev = p >= 1 && p <= INPUT_PLAYERS ? rt.pprev[p - 1] : 0;
    }
    return b >= 0 && b < BTN_COUNT ? b : -1;
}

static int l_btn(lua_State *L)
{
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1));
    return 1;
}

static int l_btnp(lua_State *L)
{
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1) && !(prev >> b & 1));
    return 1;
}

/* players() -> how many players have a controller, and which (bit n =
 * player n+1) */
static int l_players(lua_State *L)
{
    unsigned m = input_connected(), n = 0;
    for (int p = 0; p < INPUT_PLAYERS; p++)
        n += m >> p & 1;
    lua_pushinteger(L, n ? n : 1);
    lua_pushinteger(L, m ? m : 1);
    return 2;
}

/* stick([p]) -> x, y in -1..1 (x right, y down): the left stick of player
 * p, or the cross; without p the one pushed furthest */
static int l_stick(lua_State *L)
{
    float x = 0, y = 0;
    if (lua_isnoneornil(L, 1)) {
        float best = -1;
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            float px, py;
            input_stick(p, rt.praw[p], &px, &py);
            if (px * px + py * py > best) {
                best = px * px + py * py;
                x = px;
                y = py;
            }
        }
    } else {
        int p = ival(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            input_stick(p - 1, rt.praw[p - 1], &x, &y);
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int l_time(lua_State *L)
{
    lua_pushnumber(L, (timer_ticks() - rt.start_us) / 1e6);
    return 1;
}

/* stat(n): 0 Lua KiB, 1 last frame CPU ms (update+draw), 2 fps, 3 frame number,
 *         4 3D triangles drawn and 5 3D pixels written since the last zclear() */
static int l_stat(lua_State *L)
{
    switch (ival(L, 1)) {
    case 0: lua_pushinteger(L, (lua_Integer)(luavm_mem() / 1024)); break;
    case 1: lua_pushnumber(L, rt.last_cpu_us / 1000.0); break;
    case 2: lua_pushinteger(L, rt.fps); break;
    case 3: lua_pushinteger(L, rt.frame); break;
    case 4: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.tris_drawn : 0); break;
    case 5: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.pixels : 0); break;
    default: lua_pushnil(L);
    }
    return 1;
}

static int l_tri(lua_State *L)
{
    g16_tri(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
            col(L, 7, 0xFFFFFF));
    return 0;
}

/* ---- 3D (software rasterizer, see r3d.h) */

static r3d_t *r3d(lua_State *L)
{
    if (!rt.r3d_ready) {
        if (r3d_init(&rt.r3d, &rt.g) != 0)
            luaL_error(L, "not enough memory for the z-buffer");
        rt.r3d_ready = 1;
    }
    return &rt.r3d;
}

static r3d_mesh_t *new_mesh(lua_State *L)
{
    r3d_mesh_t *m = lua_newuserdatauv(L, sizeof *m, 0);
    memset(m, 0, sizeof *m);
    luaL_setmetatable(L, MESH_MT);
    return m;
}

static int l_mesh_gc(lua_State *L)
{
    r3d_mesh_free(luaL_checkudata(L, 1, MESH_MT));
    return 0;
}

/* mesh({x,y,z, x,y,z, ...}, {a,b,c,colour, ...} [, {u0,v0,u1,v1,u2,v2, ...}])
 * - 1-based vertex indices, faces counter-clockwise seen from outside. With
 * the third table (6 numbers per face, sprite-sheet pixels), faces whose
 * colour is -1 are textured with the sprite sheet. */
static int l_mesh(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    int nv = (int)(luaL_len(L, 1) / 3), nf = (int)(luaL_len(L, 2) / 4);
    luaL_argcheck(L, nv > 0 && nv <= 4096, 1, "1 to 4096 vertices");
    luaL_argcheck(L, nf > 0 && nf <= 16384, 2, "1 to 16384 faces");
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_alloc(m, nv, nf) != 0)
        return luaL_error(L, "not enough memory for the mesh");
    for (int i = 0; i < nv * 3; i++) {
        lua_rawgeti(L, 1, i + 1);
        ((float *)m->verts)[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    for (int f = 0; f < nf; f++) {
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(L, 2, f * 4 + k + 1);
            lua_Integer idx = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (idx < 1 || idx > nv)
                return luaL_error(L, "face %d: vertex index %d out of range", f + 1, (int)idx);
            m->faces[f * 3 + k] = (uint16_t)(idx - 1);
        }
        lua_rawgeti(L, 2, f * 4 + 4);
        m->colors[f] = (uint32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    if (lua_istable(L, 3)) {
        luaL_argcheck(L, luaL_len(L, 3) >= (lua_Integer)nf * 6, 3, "6 texture coordinates per face");
        if (r3d_mesh_alloc_uv(m) != 0)
            return luaL_error(L, "not enough memory for the mesh");
        for (int i = 0; i < nf * 6; i++) {
            lua_rawgeti(L, 3, i + 1);
            m->uv[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        m->tex = &rt.sheet;             /* live: sset() changes the texture too */
    }
    r3d_mesh_normals(m);
    return 1;
}

static int l_mesh_sphere(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_sphere(m, oval(L, 1, 8), oval(L, 2, 16),
                        (uint32_t)luaL_optinteger(L, 3, 0xFFFFFF), (uint32_t)luaL_optinteger(L, 4, 0xC0C0C0)) != 0)
        return luaL_error(L, "cannot build the sphere");
    return 1;
}

static int l_mesh_cube(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_cube(m, (uint32_t)luaL_optinteger(L, 1, 0xFFFFFF)) != 0)
        return luaL_error(L, "cannot build the cube");
    return 1;
}

static float fnum(lua_State *L, int i, float def)
{
    return (float)luaL_optnumber(L, i, def);
}

/* draw3d(mesh, x, y, z [, rx, ry, rz, scale, flags]) - flags: 1 no z-buffer
 * (floors and backdrops drawn first), 2 unlit (full colour) */
static int l_draw3d(lua_State *L)
{
    r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t p = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) };
    r3d_draw_flags(r3d(L), m, p, fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0), fnum(L, 8, 1),
                   (unsigned)luaL_optinteger(L, 9, 0));
    return 0;
}

/* camera3d(x, y, z [, yaw, pitch, fov, roll]) */
static int l_camera3d(lua_State *L)
{
    r3d_camera(r3d(L), fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, -5), fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 60));
    r3d_camera_roll(r3d(L), fnum(L, 7, 0));
    return 0;
}

/* fog3d(colour, near, far): faces fade into the colour with the distance;
 * fog3d() turns it off */
static int l_fog3d(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        r3d_fog(r3d(L), 0, 0, 0);
    else
        r3d_fog(r3d(L), (uint32_t)luaL_checkinteger(L, 1), fnum(L, 2, 10), fnum(L, 3, 100));
    return 0;
}

/* project3d(x, y, z) -> screen x, y and depth, or nil behind the camera */
static int l_project3d(lua_State *L)
{
    float sx, sy, d;
    v3_t p = { fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) };
    if (!r3d_project(r3d(L), p, &sx, &sy, &d)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, sx + rt.g.cam_x);
    lua_pushnumber(L, sy + rt.g.cam_y);
    lua_pushnumber(L, d);
    return 3;
}

/* light3d(x, y, z [, ambient]) - direction towards the light */
static int l_light3d(lua_State *L)
{
    r3d_light(r3d(L), fnum(L, 1, 0), fnum(L, 2, 1), fnum(L, 3, 0), fnum(L, 4, 0.25f));
    return 0;
}

/* lamp3d(i, x, y, z, radius [, k]) - point light i (1-4) that brightens the
 * faces near it by up to k (default 1); lamp3d(i) or lamp3d() turns it (them) off */
static int l_lamp3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    if (lua_isnoneornil(L, 1)) {
        for (int i = 0; i < R3D_LAMPS; i++)
            r3d_lamp(r, i, 0, 0, 0, 0, 0);
        return 0;
    }
    int i = ival(L, 1) - 1;
    luaL_argcheck(L, i >= 0 && i < R3D_LAMPS, 1, "lamp 1 to 4");
    if (lua_isnoneornil(L, 2))
        r3d_lamp(r, i, 0, 0, 0, 0, 0);
    else
        r3d_lamp(r, i, fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0), fnum(L, 5, 3), fnum(L, 6, 1));
    return 0;
}

static int l_zclear(lua_State *L)
{
    r3d_zclear(r3d(L));
    return 0;
}

/* log(...) - text to the kernel log (serial + console), not the screen */
static int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        kprintf("%s%s", i > 1 ? "\t" : "", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    kprintf("\n");
    return 0;
}

/* ---- save() / saved(): one table per cartridge in /bm33/save, as Lua
 * source ("return {...}") read back in an empty environment: data only. */

#define SAVE_DIR   "/bm33/save"
#define SAVE_MAX   (32 * 1024)

static void ser(lua_State *L, luaL_Buffer *b, int idx, int depth);

static void ser_string(luaL_Buffer *b, const char *str, size_t len)
{
    luaL_addchar(b, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"' || c == '\\') {
            luaL_addchar(b, '\\');
            luaL_addchar(b, (char)c);
        } else if (c < 32 || c == 127) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\%03u", c);
            luaL_addstring(b, esc);
        } else {
            luaL_addchar(b, (char)c);
        }
    }
    luaL_addchar(b, '"');
}

static void ser_value(lua_State *L, luaL_Buffer *b, int idx, int depth)
{
    char num[40];
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        luaL_addstring(b, lua_toboolean(L, idx) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
            snprintf(num, sizeof num, "%lld", (long long)lua_tointeger(L, idx));
        } else {
            double d = lua_tonumber(L, idx);
            if (d != d || d - d != 0)
                luaL_error(L, "save: cannot store nan or inf");
            snprintf(num, sizeof num, "%.17g", d);
            if (!strpbrk(num, ".eEn"))
                strcat(num, ".0");              /* stays a float when read back */
        }
        luaL_addstring(b, num);
        break;
    case LUA_TSTRING: {
        size_t len;
        const char *str = lua_tolstring(L, idx, &len);
        ser_string(b, str, len);
        break;
    }
    case LUA_TTABLE:
        ser(L, b, idx, depth + 1);
        break;
    default:
        luaL_error(L, "save: cannot store a %s", luaL_typename(L, idx));
    }
}

static void ser(lua_State *L, luaL_Buffer *b, int idx, int depth)
{
    if (depth > 16)
        luaL_error(L, "save: tables nested too deep (or a cycle)");
    idx = lua_absindex(L, idx);
    luaL_addchar(b, '{');
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        luaL_addchar(b, '[');
        ser_value(L, b, -2, depth);
        luaL_addstring(b, "]=");
        ser_value(L, b, -1, depth);
        luaL_addchar(b, ',');
        lua_pop(L, 1);
        if (luaL_bufflen(b) > SAVE_MAX)
            luaL_error(L, "save: more than %d bytes", SAVE_MAX);
    }
    luaL_addchar(b, '}');
}

/* save(t): true, or false and a message (no SD card, card full...) */
static int l_save(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, "return ");
    ser(L, &b, 1, 0);
    luaL_pushresult(&b);
    size_t len;
    const char *text = lua_tolstring(L, -1, &len);
    if (fat_mkdirs(SAVE_DIR) != 0 || fat_write_file(SAVE_DIR, rt.save_name, text, len) != 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* saved(): the saved table, or nil. (Not "load": that is Lua's code loader,
 * which the sandbox removes.) */
static int l_saved(lua_State *L)
{
    char path[40];
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    ksnprintf(path, sizeof path, "%s/%s", SAVE_DIR, rt.save_name);
    if (fat_find(path, &e) != 0 || e.size > SAVE_MAX || fat_load(&e, &data, &len) != 0) {
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

/* ---------------------------------------------------------------- sound */

static unsigned voice_arg(lua_State *L)
{
    lua_Integer ch = luaL_checkinteger(L, 1);
    luaL_argcheck(L, ch >= 0 && ch < 8, 1, "voice 0..7");
    return (unsigned)ch;
}

static uint32_t hz_arg(lua_State *L, int i)
{
    lua_Number f = luaL_checknumber(L, i);
    return f <= 0 ? 0 : f >= 65535 ? 65535 : (uint32_t)(f + 0.5);
}

/* note(ch, freq, [ms], [wave], [vol]): restarts the envelope; ms > 0
 * releases the note by itself, else it holds until noteoff(ch). */
static int l_note(lua_State *L)
{
    unsigned ch = voice_arg(L);
    uint32_t hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 0);
    audio_note(ch, hz, ms > 0 ? (uint32_t)ms : 0,
               (int)luaL_optinteger(L, 4, -1), (int)luaL_optinteger(L, 5, -1));
    return 0;
}

static int l_noteoff(lua_State *L)
{
    audio_note_off(voice_arg(L));
    return 0;
}

/* freq(ch, hz): changes the pitch without restarting (slides, vibrato) */
static int l_freq(lua_State *L)
{
    audio_freq(voice_arg(L), hz_arg(L, 2));
    return 0;
}

static int l_envelope(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_envelope(ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                   (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

static int l_duty(lua_State *L)
{
    audio_duty(voice_arg(L), (int)luaL_checkinteger(L, 2));
    return 0;
}

/* playing(ch): true while the voice sounds (release included) */
static int l_playing(lua_State *L)
{
    lua_pushboolean(L, audio_busy(voice_arg(L)));
    return 1;
}

/* apu(ch, reg, [value]): raw register byte, the s32 APU layout (§8) */
static int l_apu(lua_State *L)
{
    unsigned ch = voice_arg(L);
    lua_Integer reg = luaL_checkinteger(L, 2);
    luaL_argcheck(L, reg >= 0 && reg < 16, 2, "register 0..15");
    volatile uint8_t *r = audio_regs() + ch * 16 + reg;
    if (lua_gettop(L) >= 3) {
        *r = (uint8_t)luaL_checkinteger(L, 3);
        return 0;
    }
    lua_pushinteger(L, *r);
    return 1;
}

/* ---------------------------------------------------------------- keys */

/* keyp(): the next key typed, as text ("a", "\n", "\b", "\t"), a name
 * ("up", "down", "left", "right", "home", "end", "pgup", "pgdn", "del",
 * "esc", "f1".."f5") or "^s" for Ctrl+S; nil if none. The first call
 * turns on typing: the keyboard stops being a gamepad for btn(), Esc no
 * longer leaves the cartridge (Start+Select and PS still do). */
static int l_keyp(lua_State *L)
{
    if (!rt.text_mode) {
        rt.text_mode = 1;
        hid_text_mode(1);
    }
    if (rt.tq_tail == rt.tq_head) {
        lua_pushnil(L);
        return 1;
    }
    uint8_t c = rt.tq[rt.tq_tail++];
    static const char *const nav[] = { "up", "down", "left", "right", "home", "end", "pgup", "pgdn",
                                       "del", "f1", "f2", "f3", "f4", "f5" };
    char buf[4];
    if (c >= HID_KEY_UP && c <= HID_KEY_F1 + 4) lua_pushstring(L, nav[c - HID_KEY_UP]);
    else if (c == 0x1B) lua_pushstring(L, "esc");
    else if (c == '\r') lua_pushstring(L, "\n");
    else if (c == 0x7F) lua_pushstring(L, "\b");
    else if (c == '\t') lua_pushstring(L, "\t");
    else if (c >= 1 && c <= 26) { buf[0] = '^'; buf[1] = (char)('a' + c - 1); buf[2] = 0; lua_pushstring(L, buf); }
    else { buf[0] = (char)c; buf[1] = 0; lua_pushstring(L, buf); }
    return 1;
}

/* keyheld(name): true while a key is held on the USB keyboard: "f1".."f12",
 * "tab", "space", "enter", "esc" */
static int l_keyheld(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int u = 0;
    if ((n[0] == 'f' || n[0] == 'F') && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12) u = 0x3A + k - 1;
    } else if (!strcmp(n, "tab")) u = 0x2B;
    else if (!strcmp(n, "space")) u = 0x2C;
    else if (!strcmp(n, "enter")) u = 0x28;
    else if (!strcmp(n, "esc")) u = 0x29;
    lua_pushboolean(L, u && hid_usage_held((uint8_t)u));
    return 1;
}

/* cartridge files, for the editor (defined after the asset loader) */
static int l_ls(lua_State *L);
static int l_cart_load(lua_State *L);
static int l_cart_new(lua_State *L);
static int l_cart_save(lua_State *L);
static int l_cart_run(lua_State *L);
static int l_cart_arg(lua_State *L);

/* ---------------------------------------------------------------- light */

static int video_to_ram(g16_t *g);

/* light_begin([ambient]): starts the lights of this frame; everything drawn
 * so far will be lit by light_end(). The cartridge draws into RAM from now
 * on (lighting reads the picture back). */
static int l_light_begin(lua_State *L)
{
    if (!rt.light.rgb && g16_light_init(&rt.light, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    if (video_to_ram(&rt.g) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_light_clear(&rt.light, (uint32_t)luaL_optinteger(L, 1, 0x000000));
    return 0;
}

/* light(x, y, radius, colour [, intensity]) - world coordinates (camera) */
static int l_light(lua_State *L)
{
    if (!rt.light.rgb)
        return luaL_error(L, "light() before light_begin()");
    g16_light_add(&rt.light, fnum(L, 1, 0) - (float)rt.g.cam_x, fnum(L, 2, 0) - (float)rt.g.cam_y,
                  fnum(L, 3, 32), (uint32_t)luaL_optinteger(L, 4, 0xFFFFFF), fnum(L, 5, 1));
    return 0;
}

static int l_light_end(lua_State *L)
{
    (void)L;
    if (rt.light.rgb)
        g16_light_apply(&rt.g, &rt.light);
    return 0;
}

static int l_quit(lua_State *L)
{
    (void)L;
    rt.quit = 1;
    return 0;
}

static const luaL_Reg api[] = {
    { "cls", l_cls }, { "pset", l_pset }, { "pget", l_pget }, { "line", l_line },
    { "rect", l_rect }, { "rectfill", l_rectfill }, { "circ", l_circ }, { "circfill", l_circfill },
    { "spr", l_spr }, { "sspr", l_sspr }, { "map", l_map }, { "mget", l_mget }, { "mset", l_mset },
    { "sget", l_sget }, { "sset", l_sset }, { "print", l_print }, { "camera", l_camera },
    { "clip", l_clip }, { "rgb", l_rgb }, { "btn", l_btn }, { "btnp", l_btnp },
    { "players", l_players }, { "stick", l_stick },
    { "time", l_time }, { "stat", l_stat }, { "tri", l_tri },
    { "mesh", l_mesh }, { "mesh_sphere", l_mesh_sphere }, { "mesh_cube", l_mesh_cube },
    { "draw3d", l_draw3d }, { "camera3d", l_camera3d }, { "light3d", l_light3d },
    { "fog3d", l_fog3d }, { "project3d", l_project3d }, { "lamp3d", l_lamp3d },
    { "zclear", l_zclear }, { "log", l_log }, { "quit", l_quit },
    { "save", l_save }, { "saved", l_saved },
    { "keyp", l_keyp }, { "keyheld", l_keyheld }, { "ls", l_ls }, { "cart_load", l_cart_load }, { "cart_new", l_cart_new },
    { "cart_save", l_cart_save }, { "cart_run", l_cart_run }, { "cart_arg", l_cart_arg },
    { "light_begin", l_light_begin }, { "light", l_light }, { "light_end", l_light_end },
    { "note", l_note }, { "noteoff", l_noteoff }, { "freq", l_freq },
    { "envelope", l_envelope }, { "duty", l_duty }, { "playing", l_playing }, { "apu", l_apu },
    { NULL, NULL },
};

/* ---------------------------------------------------------------- state */

static void hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (++rt.hook_count > FRAME_BUDGET)
        luaL_error(L, "cart timeout: more than %d million instructions in one frame",
                   FRAME_BUDGET * HOOK_EVERY / 1000000);
}

static lua_State *new_cart_state(const b33_cart_t *c)
{
    lua_State *L = luavm_newstate();
    if (!L)
        return NULL;
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
        { LUA_COLIBNAME, luaopen_coroutine }, { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL },
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    /* sandbox: no file or code loading from outside the cartridge */
    static const char *const removed[] = { "dofile", "loadfile", "load", NULL };
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(L);
        lua_setglobal(L, *r);
    }
    luaL_newmetatable(L, MESH_MT);
    lua_pushcfunction(L, l_mesh_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    lua_pushglobaltable(L);
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);
    static const char *const waves[] = { "SQUARE", "TRIANGLE", "SAW", "NOISE" };
    for (int w = 0; w < 4; w++) {
        lua_pushinteger(L, w);
        lua_setglobal(L, waves[w]);
    }
    lua_pushinteger(L, c->width);
    lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, c->height);
    lua_setglobal(L, "SCREEN_H");
    lua_gc(L, LUA_GCGEN, 0, 0);         /* generational GC: short pauses */
    lua_sethook(L, hook, LUA_MASKCOUNT, HOOK_EVERY);
    return L;
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

/* Calls global `name` if it exists. Returns 0 on success, -1 on error
 * (message left on the stack). */
static int call(lua_State *L, const char *name)
{
    lua_pushcfunction(L, traceback);
    if (lua_getglobal(L, name) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return 0;
    }
    rt.hook_count = 0;
    if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
        lua_remove(L, -2);
        return -1;
    }
    lua_pop(L, 1);
    return 0;
}

/* ---------------------------------------------------------------- input */

/* ---------------------------------------------------------------- typing */

static void text_push(uint8_t c)
{
    if ((uint8_t)(rt.tq_head + 1) != rt.tq_tail)
        rt.tq[rt.tq_head++] = c;
}

/* serial terminal: ESC [ A..D arrows, ESC [ H / F home and end,
 * ESC O P..S F1..F4, ESC [ n ~ (3 delete, 5/6 page up/down, 15 F5) */
static void serial_text(char c)
{
    if (rt.esc == 1) {
        if (c == '[') { rt.esc = 2; rt.esc_num = 0; return; }
        if (c == 'O') { rt.esc = 3; return; }
        text_push(0x1B);
        rt.esc = 0;
    }
    if (rt.esc == 3) {
        rt.esc = 0;
        if (c >= 'P' && c <= 'S') text_push((uint8_t)(HID_KEY_F1 + (c - 'P')));
        return;
    }
    if (rt.esc == 2 && c >= '0' && c <= '9') {
        rt.esc_num = rt.esc_num * 10 + (c - '0');
        return;
    }
    if (rt.esc == 2 && c == '~') {
        rt.esc = 0;
        switch (rt.esc_num) {
        case 3: text_push(HID_KEY_DEL); break;
        case 5: text_push(HID_KEY_PGUP); break;
        case 6: text_push(HID_KEY_PGDN); break;
        case 15: text_push(HID_KEY_F1 + 4); break;
        case 1: text_push(HID_KEY_HOME); break;
        case 4: text_push(HID_KEY_END); break;
        }
        return;
    }
    if (rt.esc == 2) {
        rt.esc = 0;
        switch (c) {
        case 'A': text_push(HID_KEY_UP); return;
        case 'B': text_push(HID_KEY_DOWN); return;
        case 'C': text_push(HID_KEY_RIGHT); return;
        case 'D': text_push(HID_KEY_LEFT); return;
        case 'H': text_push(HID_KEY_HOME); return;
        case 'F': text_push(HID_KEY_END); return;
        }
        return;
    }
    if (c == 0x1B) { rt.esc = 1; return; }
    if (c == '\n') return;                  /* terminals send \r or \r\n */
    if (c == 0x08) c = 0x7F;
    text_push((uint8_t)c);
}

/* HID_* bits -> btn() bits: 0-5 are the same; X and Y are 6 and 7, or A
 * and B for a cartridge that never asked for them */
static uint16_t hid_to_btn(uint32_t pad)
{
    uint16_t b = pad & 0x3F;
    if (pad & HID_START) b |= 1u << BTN_START;
    if (pad & HID_SELECT) b |= 1u << BTN_SELECT;
    if (pad & HID_X) b |= rt.uses_xy ? 1u << BTN_X : 1u << BTN_A;
    if (pad & HID_Y) b |= rt.uses_xy ? 1u << BTN_Y : 1u << BTN_B;
    return b;
}

static int poll_keys(void)
{
    while (uart_rx_ready()) {
        char c = uart_getc();
        int b = -1;
        if (rt.text_mode) {
            serial_text(c);
            continue;
        }
        if (rt.esc == 1) { rt.esc = c == '[' ? 2 : 0; continue; }
        if (rt.esc == 2) {
            rt.esc = 0;
            b = c == 'A' ? BTN_UP : c == 'B' ? BTN_DOWN : c == 'C' ? BTN_RIGHT : c == 'D' ? BTN_LEFT : -1;
        } else {
            switch (c) {
            case 0x1B: rt.esc = 1; continue;
            case 'a': case 'A': b = BTN_LEFT; break;
            case 'd': case 'D': b = BTN_RIGHT; break;
            case 'w': case 'W': b = BTN_UP; break;
            case 's': case 'S': b = BTN_DOWN; break;
            case ' ': case 'j': case 'J': b = BTN_A; break;
            case 'k': case 'K': case 'x': case 'X': b = BTN_B; break;
            case 'c': case 'C': case 'l': case 'L': b = BTN_X; break;
            case 'v': case 'V': case 'i': case 'I': b = BTN_Y; break;
            case '\r': b = BTN_START; break;
            case 'q': case 'Q': return 1;
            }
        }
        if (b >= 0)
            rt.hold[b] = HOLD_FRAMES;
    }
    /* a lone Esc, not the start of a sequence: after 6 frames of nothing */
    if (rt.text_mode && rt.esc == 1 && ++rt.esc_wait >= 6) {
        text_push(0x1B);
        rt.esc = 0;
    }
    if (rt.esc != 1)
        rt.esc_wait = 0;
    int quit = 0;
    uint32_t per[INPUT_PLAYERS];
    uint32_t pad = input_players(per, rt.text_mode, &quit, &rt.local);
    if (rt.text_mode)
        for (int k; (k = hid_getc()) >= 0;)
            text_push((uint8_t)k);
    uint32_t serial = 0;                    /* HID_* bits of the serial keys held */
    for (int b = 0; b < BTN_COUNT; b++)
        if (rt.hold[b]) {
            serial |= b == BTN_X ? HID_X : b == BTN_Y ? HID_Y : b == BTN_START ? HID_START :
                      b == BTN_SELECT ? HID_SELECT : 1u << b;
            rt.hold[b]--;
        }
    rt.prev = rt.now;
    rt.now = hid_to_btn(pad | serial);
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        if (p == rt.local)
            per[p] |= serial;
        rt.praw[p] = per[p];
        rt.pprev[p] = rt.pnow[p];
        rt.pnow[p] = hid_to_btn(per[p]);
    }
    return quit;
}

/* ---------------------------------------------------------------- loading */

static void sheet8_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    (void)ctx;
    g16_sheet_set(&rt.sheet, x, y, g16_rgb(rgba[0], rgba[1], rgba[2]), rgba[3] >= 128);
}

static int load_assets(const b33_cart_t *c)
{
    int has = c->sheet_rgba || c->sheet8;
    int sw = has ? c->sheet_w : 256, sh = has ? c->sheet_h : 256;
    if (g16_sheet_alloc(&rt.sheet, sw, sh) != 0)
        return -1;
    int cells = (rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL);
    rt.cell_dirty = calloc((size_t)cells, 1);
    if (!rt.cell_dirty)
        return -1;
    if (has) {
        if (c->sheet8) {
            if (b33_sheet8_unpack(c, sheet8_set, NULL) != 0)
                return -1;
        } else {
            for (int y = 0; y < c->sheet_h; y++)
                for (int x = 0; x < c->sheet_w; x++) {
                    const uint8_t *p = c->sheet_rgba + ((uint32_t)y * c->sheet_w + x) * 4;
                    g16_sheet_set(&rt.sheet, x, y, g16_rgb(p[0], p[1], p[2]), p[3] >= 128);
                }
        }
        for (int i = 0; i < cells; i++)
            rt.cell_dirty[i] = 1;
        rt.sheet_dirty = 1;
        sheet_commit();
    }

    rt.map.w = c->map_cells ? c->map_w : 256;
    rt.map.h = c->map_cells ? c->map_h : 256;
    rt.map.cells = calloc((size_t)rt.map.w * rt.map.h, 2);
    if (!rt.map.cells)
        return -1;
    if (c->map_cells)
        for (uint32_t i = 0; i < (uint32_t)rt.map.w * rt.map.h; i++)
            rt.map.cells[i] = (uint16_t)(c->map_cells[i * 2] | c->map_cells[i * 2 + 1] << 8);
    return 0;
}

static void free_assets(void)
{
    g16_sheet_free(&rt.sheet);
    free(rt.cell_dirty);
    free(rt.map.cells);
    rt.cell_dirty = NULL;
    rt.map.cells = NULL;
}

/* ---------------------------------------------------------------- editor */

/* The project open in the editor lives in the editor's own sprite sheet and
 * map; its cover is kept here as it came. Across the editor's "run" the
 * kernel keeps a request (which file to play) and an argument (the file,
 * and the error it stopped with). */
static uint8_t *proj_cover;
static uint16_t proj_cover_w, proj_cover_h;
static char run_request[64];
static char arg_path[64], arg_error[512], last_error[512];

void b33_set_arg(const char *path, const char *error)
{
    ksnprintf(arg_path, sizeof arg_path, "%s", path ? path : "");
    ksnprintf(arg_error, sizeof arg_error, "%s", error ? error : "");
}

int b33_take_run(char *path, size_t n)
{
    if (!run_request[0])
        return 0;
    ksnprintf(path, n, "%s", run_request);
    run_request[0] = 0;
    return 1;
}

const char *b33_last_error(void)
{
    return last_error;
}

/* ls([dir]) -> { {name=, size=, dir=}, ... } */
static int l_ls(lua_State *L)
{
    const char *dir = luaL_optstring(L, 1, "/");
    lua_newtable(L);
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, dir) != 0)
        return 1;
    int i = 1;
    while (fat_readdir(&d, &e)) {
        if (e.name[0] == '.')
            continue;
        lua_newtable(L);
        lua_pushstring(L, e.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)e.size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, e.is_dir);
        lua_setfield(L, -2, "dir");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static void push_project(lua_State *L, const char *title, const char *author, int w, const char *lua,
                         size_t lua_len)
{
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, w == 320 ? "320x180" : "640x360");
    lua_setfield(L, -2, "res");
    lua_pushlstring(L, lua, lua_len);
    lua_setfield(L, -2, "lua");
    lua_pushinteger(L, rt.sheet.w);
    lua_setfield(L, -2, "sheet_w");
    lua_pushinteger(L, rt.sheet.h);
    lua_setfield(L, -2, "sheet_h");
    lua_pushinteger(L, rt.map.w);
    lua_setfield(L, -2, "map_w");
    lua_pushinteger(L, rt.map.h);
    lua_setfield(L, -2, "map_h");
}

/* cart_load(path) -> project table, or nil and a message. The cartridge's
 * sprite sheet and map replace the running cartridge's own. */
static int l_cart_load(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    b33_cart_t c;
    char err[64];
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    if (b33_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    free_assets();
    if (load_assets(&c) != 0) {
        free(data);
        return luaL_error(L, "not enough memory for the cartridge");
    }
    free(proj_cover);
    proj_cover = NULL;
    if (c.cover_rgba && (proj_cover = malloc((size_t)c.cover_w * c.cover_h * 4)) != NULL) {
        memcpy(proj_cover, c.cover_rgba, (size_t)c.cover_w * c.cover_h * 4);
        proj_cover_w = c.cover_w;
        proj_cover_h = c.cover_h;
    }
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    push_project(L, title, author, c.width, c.lua, c.lua_size);
    free(data);
    return 1;
}

/* cart_new(): an empty 256x256 sprite sheet and map, no cover */
static int l_cart_new(lua_State *L)
{
    b33_cart_t c;
    memset(&c, 0, sizeof c);
    free_assets();
    if (load_assets(&c) != 0)
        return luaL_error(L, "not enough memory for the cartridge");
    free(proj_cover);
    proj_cover = NULL;
    return 0;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static const char *field(lua_State *L, int t, const char *k, const char *def)
{
    lua_getfield(L, t, k);
    const char *s = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return s;
}

/* cart_save(path, {title=, author=, res=, lua=}) -> true, or false and a
 * message. Sprite sheet and map are the running cartridge's; the file
 * name must be 8.3 (e.g. "/carts/MYGAME.B33"). */
static int l_cart_save(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const char *title = field(L, 2, "title", ""), *author = field(L, 2, "author", "");
    const char *res = field(L, 2, "res", "640x360");
    lua_getfield(L, 2, "lua");
    size_t lua_len;
    const char *lua = luaL_checklstring(L, -1, &lua_len);
    int w = strcmp(res, "320x180") == 0 ? 320 : 640, h = w == 320 ? 180 : 360;

    char dir[64], name[16];
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n >= sizeof dir) n = sizeof dir - 1;
        memcpy(dir, path, n);
        dir[n] = 0;
        if (!dir[0]) strcpy(dir, "/");
        ksnprintf(name, sizeof name, "%s", slash + 1);
    } else {
        strcpy(dir, "/carts");
        ksnprintf(name, sizeof name, "%s", path);
    }

    const uint32_t sw = (uint32_t)rt.sheet.w, sh = (uint32_t)rt.sheet.h;
    const uint32_t mw = (uint32_t)rt.map.w, mh = (uint32_t)rt.map.h;
    uint32_t sizes[4] = { proj_cover ? 4u + (uint32_t)proj_cover_w * proj_cover_h * 4 : 0, (uint32_t)lua_len,
                          4 + sw * sh * 4, 4 + mw * mh * 2 };
    static const uint32_t types[4] = { B33_SEC_COVER, B33_SEC_LUA, B33_SEC_SHEET, B33_SEC_MAP };
    uint32_t count = 0, total = B33_HEADER_SIZE;
    for (int i = 0; i < 4; i++)
        if (sizes[i]) { count++; total += 16 + ((sizes[i] + 3) & ~3u); }
    uint8_t *buf = calloc(total, 1);
    if (!buf)
        return luaL_error(L, "not enough memory to save");
    uint8_t *tab = buf + B33_HEADER_SIZE, *p = tab + count * 16;
    for (int i = 0; i < 4; i++) {
        if (!sizes[i]) continue;
        put32(tab, types[i]);
        put32(tab + 4, (uint32_t)(p - buf));
        put32(tab + 8, sizes[i]);
        tab += 16;
        if (i == 0) {
            put16(p, proj_cover_w); put16(p + 2, proj_cover_h);
            memcpy(p + 4, proj_cover, sizes[i] - 4);
        } else if (i == 1) {
            memcpy(p, lua, lua_len);
        } else if (i == 2) {
            sheet_commit();
            put16(p, sw); put16(p + 2, sh);
            for (uint32_t k = 0; k < sw * sh; k++) {
                uint32_t rgb = g16_to_rgb24(rt.sheet.px[k]);
                uint8_t *q = p + 4 + k * 4;
                q[0] = (uint8_t)(rgb >> 16); q[1] = (uint8_t)(rgb >> 8); q[2] = (uint8_t)rgb;
                q[3] = rt.sheet.alpha[k] ? 255 : 0;
            }
        } else {
            put16(p, mw); put16(p + 2, mh);
            for (uint32_t k = 0; k < mw * mh; k++)
                put16(p + 4 + k * 2, rt.map.cells[k]);
        }
        p += (sizes[i] + 3) & ~3u;
    }
    memcpy(buf, "BM33CART", 8);
    put16(buf + 8, 1);
    put16(buf + 10, B33_HEADER_SIZE);
    put16(buf + 12, (uint32_t)w);
    put16(buf + 14, (uint32_t)h);
    buf[16] = B33_FMT_RGB565;
    buf[17] = (uint8_t)count;
    strncpy((char *)buf + 24, title, 47);
    strncpy((char *)buf + 72, author, 31);
    put32(buf + 20, crc32(buf + B33_HEADER_SIZE, total - B33_HEADER_SIZE));
    int ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
    free(buf);
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_run(path): leaves the editor, plays the cartridge, then comes back
 * to the editor with cart_arg() = { path =, error = } */
static int l_cart_run(lua_State *L)
{
    ksnprintf(run_request, sizeof run_request, "%s", luaL_checkstring(L, 1));
    rt.quit = 1;
    return 0;
}

static int l_cart_arg(lua_State *L)
{
    if (!arg_path[0]) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, arg_path);
    lua_setfield(L, -2, "path");
    if (arg_error[0]) {
        lua_pushstring(L, arg_error);
        lua_setfield(L, -2, "error");
    }
    return 1;
}

/* ---------------------------------------------------------------- player */

/* Two ways to draw a frame:
 * - direct: into the framebuffer's back page (uncached GPU memory; writes
 *   only, pget() is slow);
 * - via RAM: into a cached buffer ("shadow") copied to the back page once
 *   per frame. On the ARM1176 the copy has to read the buffer back from
 *   SDRAM (16 KB data cache, reads ~4x slower than writes), so it is not
 *   obviously a win: b33_bench() measures both. Default: direct. */
static int via_ram;
static int dma_frames;          /* copy by DMA: off until the DMA test passes on the Pi */
static uint16_t *shadow;

void b33_set_via_ram(int on) { via_ram = on; }

/* Switches a running cartridge to the RAM buffer (keeps clip and camera). */
static int video_to_ram(g16_t *g)
{
    if (shadow)
        return 0;
    shadow = malloc((size_t)g->w * (size_t)g->h * 2);
    if (!shadow)
        return -1;
    memset(shadow, 0, (size_t)g->w * (size_t)g->h * 2);
    g16_t keep = *g;
    g16_target(g, shadow, (uint32_t)g->w, g->w, g->h, keep.font);
    g->cx0 = keep.cx0; g->cy0 = keep.cy0; g->cx1 = keep.cx1; g->cy1 = keep.cy1;
    g->cam_x = keep.cam_x; g->cam_y = keep.cam_y;
    return 0;
}
int b33_via_ram(void) { return via_ram; }

int b33_video_enter(framebuffer_t *fb, int w, int h, g16_t *g)
{
    console_suspend(1);
    free(shadow);
    shadow = NULL;
    if (fb_init_depth(fb, (uint32_t)w, (uint32_t)h, 3, 16) != 0)
        return -1;
    if (via_ram) {
        shadow = malloc((size_t)w * (size_t)h * 2);
        if (!shadow)
            return -1;
        memset(shadow, 0, (size_t)w * (size_t)h * 2);
        g16_target(g, shadow, (uint32_t)w, w, h, &font_console_8x16);
    } else {
        g16_target(g, (uint16_t *)fb->base, fb->pitch / 2, w, h, &font_console_8x16);
    }
    return 0;
}

uint32_t b33_video_present(framebuffer_t *fb, g16_t *g)
{
    if (!shadow) {
        fb_flip(fb);
        g->px = (uint16_t *)fb->base;
        return 0;
    }
    uint32_t t0 = timer_ticks();
    const uint32_t row = (uint32_t)g->w * 2;
    if (fb->pitch == row && dma_ready() && dma_frames) {
        /* the DMA reads the SDRAM much faster than the ARM1176 */
        dcache_clean_all();
        dma_copy(fb->base, g->px, row * (uint32_t)g->h);
        dma_wait();
    } else if (fb->pitch == row) {
        memcpy(fb->base, g->px, row * (uint32_t)g->h);
    } else {
        for (int y = 0; y < g->h; y++)
            memcpy(fb->base + (uint32_t)y * fb->pitch, g->px + (uint32_t)y * g->stride, row);
    }
    uint32_t us = timer_ticks() - t0;
    fb_flip(fb);
    return us;
}

void b33_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    free(shadow);
    shadow = NULL;
    fb_init(fb, w, h, 2);
    console_suspend(0);
}

static int enter_mode(framebuffer_t *fb, int w, int h)
{
    return b33_video_enter(fb, w, h, &rt.g);
}

static void leave_mode(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    b33_video_leave(fb, w, h);
    input_flush();              /* keys typed in the game stay in the game */
}

static void present(framebuffer_t *fb, uint32_t *deadline, uint32_t *prev, uint32_t *dropped)
{
    rt.present_us = b33_video_present(fb, &rt.g);
    while ((int32_t)(timer_ticks() - *deadline) < 0)
        ;
    uint32_t now = timer_ticks();
    if (dropped && now - *prev > FRAME_US * 3 / 2)
        (*dropped)++;
    *prev = now;
    *deadline += FRAME_US;
    if ((int32_t)(now - *deadline) > 0)
        *deadline = now + FRAME_US;
}

void b33_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, b33_stats_t *st)
{
    b33_cart_t cart;
    char err[64];
    const char *error = NULL;
    lua_State *L = NULL;

    memset(st, 0, sizeof *st);
    memset(&rt, 0, sizeof rt);
    if (b33_parse(data, len, &cart, err, sizeof err) != 0) {
        kprintf("\x1b[91mb33: %s\x1b[0m\n", err);
        return;
    }
    memcpy(st->title, cart.title, sizeof st->title);
    if (load_assets(&cart) != 0 || !(L = new_cart_state(&cart))) {
        free_assets();
        kprintf("\x1b[91mb33: out of memory\x1b[0m\n");
        return;
    }

    const uint32_t con_w = fb->width, con_h = fb->height;
    if (enter_mode(fb, cart.width, cart.height) != 0) {
        leave_mode(fb, con_w, con_h);
        lua_close(L);
        free_assets();
        kprintf("\x1b[91mb33: cannot set %ux%u RGB565\x1b[0m\n", cart.width, cart.height);
        return;
    }

    {
        char id[96];
        int n = ksnprintf(id, sizeof id, "%s\n%s", cart.title, cart.author);
        ksnprintf(rt.save_name, sizeof rt.save_name, "%08lX.SAV", crc32(id, (uint32_t)n));
    }
    audio_reset();
    rt.start_us = timer_ticks();
    rt.hook_count = 0;
    if (luaL_loadbuffer(L, cart.lua, cart.lua_size, "=main.lua") != LUA_OK ||
        (lua_pushcfunction(L, traceback), lua_insert(L, -2), lua_pcall(L, 0, 0, -2)) != LUA_OK ||
        call(L, "_init") != 0)
        error = lua_tostring(L, -1);

    uint32_t start = timer_ticks(), deadline = start + FRAME_US, prev = start;
    uint32_t fps_t0 = start, fps_frames = 0;
    while (!error) {
        if (rt.quit || poll_keys() || timer_ticks() - start >= seconds * 1000000u)
            break;
        uint32_t t0 = timer_ticks();
        if (call(L, "_update") != 0 || call(L, "_draw") != 0) {
            error = lua_tostring(L, -1);
            break;
        }
        rt.last_cpu_us = timer_ticks() - t0;
        st->cpu_us_total += rt.last_cpu_us;
        if (rt.last_cpu_us > st->cpu_us_max)
            st->cpu_us_max = rt.last_cpu_us;
        rt.frame++;
        crumb_frame(rt.frame);
        present(fb, &deadline, &prev, &st->dropped);
        st->copy_us_total += rt.present_us;
        if (++fps_frames, timer_ticks() - fps_t0 >= 1000000) {
            rt.fps = fps_frames;
            fps_frames = 0;
            fps_t0 = timer_ticks();
        }
    }

    audio_reset();
    st->frames = rt.frame;
    st->elapsed_us = timer_ticks() - start;
    st->lua_kb = (uint32_t)(luavm_mem() / 1024);
    leave_mode(fb, con_w, con_h);
    ksnprintf(last_error, sizeof last_error, "%s", error ? error : "");
    hid_text_mode(0);
    if (error)
        kprintf("\x1b[91mb33: \"%s\" stopped with an error:\n%s\x1b[0m\n", cart.title, error);
    st->ok = error == NULL;
    lua_close(L);               /* frees meshes (__gc) before the z-buffer */
    g16_light_free(&rt.light);
    if (rt.r3d_ready)
        r3d_free(&rt.r3d);
    free_assets();
}

void b33_print_stats(const b33_stats_t *st)
{
    if (!st->frames)
        return;
    uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
    uint32_t avg = st->cpu_us_total / st->frames;
    kprintf("b33: \"%s\" %lu frames, %lu.%lu fps, %lu dropped\n",
            st->title, st->frames, fps10 / 10, fps10 % 10, st->dropped);
    uint32_t copy = st->copy_us_total / st->frames;
    kprintf("     update+draw avg %lu.%02lu ms (%lu%% of frame), max %lu.%02lu ms, Lua %lu KiB\n",
            avg / 1000, avg % 1000 / 10, avg * 100 / FRAME_US,
            st->cpu_us_max / 1000, st->cpu_us_max % 1000 / 10, st->lua_kb);
    if (copy)
        kprintf("     copy to screen %lu.%02lu ms per frame\n", copy / 1000, copy % 1000 / 10);
}

/* ---------------------------------------------------------------- C bench */

uint32_t b33_bench(framebuffer_t *fb, uint32_t frames)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    memset(&rt, 0, sizeof rt);
    if (g16_sheet_alloc(&rt.sheet, 128, 128) != 0)
        return 0;
    /* half of the cells opaque, half with holes */
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int cell = (y / 8) * 16 + x / 8;
            int hole = (cell & 1) && ((x + y) % 5 == 0);
            g16_sheet_set(&rt.sheet, x, y, g16_rgb((uint32_t)x * 2, (uint32_t)y * 2, (uint32_t)cell), !hole);
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(&rt.sheet, cx, cy);
    rt.map.w = 80;
    rt.map.h = 45;
    rt.map.cells = calloc(80 * 45, 2);
    if (!rt.map.cells || enter_mode(fb, 640, 360) != 0) {
        g16_sheet_free(&rt.sheet);
        free(rt.map.cells);
        leave_mode(fb, con_w, con_h);
        return 0;
    }
    for (int i = 0; i < 80 * 45; i++)
        rt.map.cells[i] = (uint16_t)(1 + (i * 7) % 255);

    uint32_t total = 0, deadline = timer_ticks() + FRAME_US, prev = 0;
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t t0 = timer_ticks();
        g16_cls(&rt.g, 0);
        g16_map(&rt.g, &rt.sheet, &rt.map, 0, 0, -(int)(f % 8), 0, 81, 45);
        for (int i = 0; i < 256; i++) {
            int x = (i * 37 + (int)f * 3) % 660 - 10, y = (i * 53 + (int)f * 2) % 380 - 10;
            g16_spr(&rt.g, &rt.sheet, (i * 2) % 224, x, y, 2, 2, i & 1, i & 2);
        }
        g16_rectfill(&rt.g, 0, 0, 640, 16, 0);
        g16_text(&rt.g, 0, 0, "b33 C benchmark: full map + 256 sprites 16x16", 0xFFFF);
        total += timer_ticks() - t0;
        present(fb, &deadline, &prev, NULL);
        total += rt.present_us;             /* the frame is on screen only after the copy */
    }
    leave_mode(fb, con_w, con_h);
    g16_sheet_free(&rt.sheet);
    free(rt.map.cells);
    rt.map.cells = NULL;
    return frames ? total / frames : 0;
}

/* The benchmark both ways (direct and via RAM), one line; the faster one
 * is not chosen automatically: see b33_set_via_ram. */
static void ms2(const char *label, uint32_t us)
{
    kprintf("%s %lu.%02lu ms", label, us / 1000, us % 1000 / 10);
}

void b33_set_dma_frames(int on) { dma_frames = on; }

void b33_bench_report(framebuffer_t *fb, uint32_t frames)
{
    int saved = via_ram;
    via_ram = 0;
    uint32_t direct = b33_bench(fb, frames);
    via_ram = 1;
    uint32_t ram = b33_bench(fb, frames);
    via_ram = saved;
    kprintf("b33 bench (map + 256 sprites):");
    ms2(" direct", direct);
    ms2(", via RAM", ram);
    kprintf("\n");
}
