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

enum { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B, BTN_COUNT };

static struct {
    g16_t g;
    g16_sheet_t sheet;
    g16_map_t map;
    uint8_t *cell_dirty;
    int sheet_dirty;
    uint8_t hold[BTN_COUNT];
    uint8_t now, prev;              /* button bits this frame / last frame */
    uint32_t start_us, frame;
    uint32_t last_cpu_us, fps;
    uint32_t present_us;        /* last copy of the frame to the framebuffer */
    uint32_t hook_count;
    int esc;
    int quit;
    r3d_t r3d;
    int r3d_ready;
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

static int l_print(lua_State *L)
{
    /* read the numeric arguments first: luaL_tolstring pushes a value */
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    const char *s = luaL_tolstring(L, 1, NULL);
    lua_pushinteger(L, g16_text(&rt.g, x, y, s, c));
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

static int l_btn(lua_State *L)
{
    int b = ival(L, 1);
    lua_pushboolean(L, b >= 0 && b < BTN_COUNT && (rt.now >> b & 1));
    return 1;
}

static int l_btnp(lua_State *L)
{
    int b = ival(L, 1);
    lua_pushboolean(L, b >= 0 && b < BTN_COUNT && (rt.now >> b & 1) && !(rt.prev >> b & 1));
    return 1;
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

/* mesh({x,y,z, x,y,z, ...}, {a,b,c,colour, ...}) - 1-based vertex indices,
 * faces counter-clockwise seen from outside. */
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

/* draw3d(mesh, x, y, z [, rx, ry, rz, scale]) */
static int l_draw3d(lua_State *L)
{
    r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t p = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) };
    r3d_draw(r3d(L), m, p, fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0), fnum(L, 8, 1));
    return 0;
}

/* camera3d(x, y, z [, yaw, pitch, fov]) */
static int l_camera3d(lua_State *L)
{
    r3d_camera(r3d(L), fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, -5), fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 60));
    return 0;
}

/* light3d(x, y, z [, ambient]) - direction towards the light */
static int l_light3d(lua_State *L)
{
    r3d_light(r3d(L), fnum(L, 1, 0), fnum(L, 2, 1), fnum(L, 3, 0), fnum(L, 4, 0.25f));
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
    { "time", l_time }, { "stat", l_stat }, { "tri", l_tri },
    { "mesh", l_mesh }, { "mesh_sphere", l_mesh_sphere }, { "mesh_cube", l_mesh_cube },
    { "draw3d", l_draw3d }, { "camera3d", l_camera3d }, { "light3d", l_light3d },
    { "zclear", l_zclear }, { "log", l_log }, { "quit", l_quit },
    { "save", l_save }, { "saved", l_saved },
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

static int poll_keys(void)
{
    while (uart_rx_ready()) {
        char c = uart_getc();
        int b = -1;
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
            case 'q': case 'Q': return 1;
            }
        }
        if (b >= 0)
            rt.hold[b] = HOLD_FRAMES;
    }
    int quit = 0;
    uint32_t pad = input_buttons(&quit);
    rt.prev = rt.now;
    rt.now = pad & ((1u << BTN_COUNT) - 1);   /* HID_* bits match btn() numbers */
    for (int b = 0; b < BTN_COUNT; b++)
        if (rt.hold[b]) { rt.now |= 1u << b; rt.hold[b]--; }
    return quit;
}

/* ---------------------------------------------------------------- loading */

static int load_assets(const b33_cart_t *c)
{
    int sw = c->sheet_rgba ? c->sheet_w : 256, sh = c->sheet_rgba ? c->sheet_h : 256;
    if (g16_sheet_alloc(&rt.sheet, sw, sh) != 0)
        return -1;
    int cells = (rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL);
    rt.cell_dirty = calloc((size_t)cells, 1);
    if (!rt.cell_dirty)
        return -1;
    if (c->sheet_rgba) {
        for (int y = 0; y < c->sheet_h; y++)
            for (int x = 0; x < c->sheet_w; x++) {
                const uint8_t *p = c->sheet_rgba + ((uint32_t)y * c->sheet_w + x) * 4;
                g16_sheet_set(&rt.sheet, x, y, g16_rgb(p[0], p[1], p[2]), p[3] >= 128);
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

/* ---------------------------------------------------------------- player */

/* Two ways to draw a frame:
 * - direct: into the framebuffer's back page (uncached GPU memory; writes
 *   only, pget() is slow);
 * - via RAM: into a cached buffer ("shadow") copied to the back page once
 *   per frame. On the ARM1176 the copy has to read the buffer back from
 *   SDRAM (16 KB data cache, reads ~4x slower than writes), so it is not
 *   obviously a win: b33_bench() measures both. Default: direct. */
static int via_ram;
static uint16_t *shadow;

void b33_set_via_ram(int on) { via_ram = on; }
int b33_via_ram(void) { return via_ram; }

int b33_video_enter(framebuffer_t *fb, int w, int h, g16_t *g)
{
    console_suspend(1);
    free(shadow);
    shadow = NULL;
    if (fb_init_depth(fb, (uint32_t)w, (uint32_t)h, 2, 16) != 0)
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
    if (fb->pitch == row) {
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
    if (error)
        kprintf("\x1b[91mb33: \"%s\" stopped with an error:\n%s\x1b[0m\n", cart.title, error);
    st->ok = error == NULL;
    lua_close(L);               /* frees meshes (__gc) before the z-buffer */
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
void b33_bench_report(framebuffer_t *fb, uint32_t frames)
{
    int saved = via_ram;
    via_ram = 0;
    uint32_t direct = b33_bench(fb, frames);
    via_ram = 1;
    uint32_t ram = b33_bench(fb, frames);
    via_ram = saved;
    kprintf("b33 bench (map + 256 sprites): direct %lu.%02lu ms, via RAM %lu.%02lu ms\n", direct / 1000, direct % 1000 / 10, ram / 1000, ram % 1000 / 10);
}
