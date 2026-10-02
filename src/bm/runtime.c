/*
 * .bm runtime: sandboxed Lua 5.4 state + drawing API in C (gfx16) + frame
 * loop. Lua only runs game logic; every pixel is drawn by C.
 */
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/crc32.h"
#include "kernel/input.h"
#include "usb/hid.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "drivers/dma.h"
#include "arch/cache.h"
#include "kernel/crumbs.h"
#include "kernel/prompts.h"
#include "n8lua.h"
#include "ai/lua_ai.h"
#include "require.h"
#include "meshcap.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
/* the shoulder buttons from the serial keys: pad() only, not btn() */
enum { SER_L1 = BTN_COUNT, SER_R1, SER_COUNT };

static struct {
    g16_t g;
    g16_sheet_t sheet;
    g16_map_t map;
    uint8_t *cell_dirty;
    int sheet_dirty;
    uint8_t hold[SER_COUNT];
    int uses_xy;                /* the cart asked for btn(6) or btn(7) */
    uint16_t now, prev;             /* button bits this frame / last frame, any player */
    uint16_t pnow[INPUT_PLAYERS], pprev[INPUT_PLAYERS];     /* the same per player */
    uint32_t praw[INPUT_PLAYERS];   /* HID bits per player (stick() from the cross) */
    int local;                      /* player of the keyboard / USB / serial, or -1 */
    uint32_t start_us, frame;
    uint32_t last_cpu_us, fps;
    uint32_t present_us;        /* last copy of the frame to the framebuffer */
    uint32_t hook_count;
    uint32_t frame_instr_k, last_instr_k;   /* Lua instructions (thousands): this frame, the last */
    int perf_key;               /* 'p' on the serial line: the dev kit's overlay on or off */
    int f3_held;
    int esc;
    int quit;
    r3d_t r3d;
    int r3d_ready;
    g16_light_t light;          /* light_begin() .. light_end() */
    g16_fade_t fade;            /* fades(), dark_begin() .. dark_end() */
    int text_mode;              /* keyp() was called: the keyboard types */
    int raw_keys;               /* rawkeys(true): the keyboard is read with keydown() */
    lua_State *slice_thread;    /* timeslice(): this coroutine yields after slice_at */
    uint32_t slice_at, slice_len;
    int esc_wait;               /* frames since a serial Esc */
    int esc_num;                /* ESC [ n ~ */
    uint8_t tq[256];            /* typed keys for keyp() */
    uint8_t tq_head, tq_tail;
    char save_name[13];         /* "1A2B3C4D.SAV": CRC-32 of title and author */
    uint8_t *mesh;              /* copy of the cartridge's MESH section, or NULL */
    uint32_t mesh_size;
    uint8_t *anim;              /* copy of its ANIM section (skeletons), or NULL */
    uint32_t anim_size;
} rt;

#define MESH_MT "bm.mesh"

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

/* sspr(sx, sy, sw, sh, dx, dy, [flip_x, flip_y, zoom]): zoom (default 1)
 * draws it that many times bigger, or smaller below 1 (nearest pixel) */
static int l_sspr(lua_State *L)
{
    sheet_commit();
    lua_Number zoom = luaL_optnumber(L, 9, 1);
    if (zoom == 1)
        g16_sspr(&rt.g, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
                 lua_toboolean(L, 7), lua_toboolean(L, 8));
    else
        g16_sspr_zoom(&rt.g, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
                      lua_toboolean(L, 7), lua_toboolean(L, 8), (float)zoom);
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

/* font([name]): the font of print() from now on, "8x16" (the default),
 * "8x14" or "6x12" (also "large", "medium", "small"); returns the width
 * and height of a character of the current font */
static int l_font(lua_State *L)
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

/* ---------------------------------------------------------------- prompts */

/* the pad's buttons, upper case: as on a DS4, or on a lettered pad */
static const struct { const char *name; uint8_t ds4, pad; } pad_prompts[] = {
    { "A", PROMPT_CROSS, PROMPT_PAD_A }, { "B", PROMPT_CIRCLE, PROMPT_PAD_B },
    { "X", PROMPT_SQUARE, PROMPT_PAD_X }, { "Y", PROMPT_TRIANGLE, PROMPT_PAD_Y },
    { "START", PROMPT_OPTIONS, PROMPT_PAD_START }, { "SELECT", PROMPT_SHARE, PROMPT_PAD_SELECT },
    { "L1", PROMPT_L1, PROMPT_L1 }, { "R1", PROMPT_R1, PROMPT_R1 },
    { "L2", PROMPT_L2, PROMPT_L2 }, { "R2", PROMPT_R2, PROMPT_R2 },
    { "L3", PROMPT_L3, PROMPT_L3 }, { "R3", PROMPT_R3, PROMPT_R3 },
    { "LSTICK", PROMPT_LSTICK, PROMPT_LSTICK }, { "RSTICK", PROMPT_RSTICK, PROMPT_RSTICK },
    { "DPAD", PROMPT_DPAD, PROMPT_DPAD }, { "UP", PROMPT_DPAD_UP, PROMPT_DPAD_UP },
    { "DOWN", PROMPT_DPAD_DOWN, PROMPT_DPAD_DOWN }, { "LEFT", PROMPT_DPAD_LEFT, PROMPT_DPAD_LEFT },
    { "RIGHT", PROMPT_DPAD_RIGHT, PROMPT_DPAD_RIGHT }, { "UPDOWN", PROMPT_DPAD_UPDOWN, PROMPT_DPAD_UPDOWN },
    { "LEFTRIGHT", PROMPT_DPAD_LEFTRIGHT, PROMPT_DPAD_LEFTRIGHT },
    { "PS", PROMPT_PS, PROMPT_PS }, { "TOUCHPAD", PROMPT_TOUCHPAD, PROMPT_TOUCHPAD },
    { "CROSS", PROMPT_CROSS, PROMPT_CROSS }, { "CIRCLE", PROMPT_CIRCLE, PROMPT_CIRCLE },
    { "SQUARE", PROMPT_SQUARE, PROMPT_SQUARE }, { "TRIANGLE", PROMPT_TRIANGLE, PROMPT_TRIANGLE },
    { "OPTIONS", PROMPT_OPTIONS, PROMPT_OPTIONS }, { "SHARE", PROMPT_SHARE, PROMPT_SHARE },
};

/* the keyboard's keys with a name, lower case (the names of keyp()) */
static const struct { const char *name; uint8_t id; } key_prompts[] = {
    { "up", PROMPT_KEY_UP }, { "down", PROMPT_KEY_DOWN }, { "left", PROMPT_KEY_LEFT },
    { "right", PROMPT_KEY_RIGHT }, { "enter", PROMPT_KEY_ENTER }, { "esc", PROMPT_KEY_ESC },
    { "space", PROMPT_KEY_SPACE }, { "tab", PROMPT_KEY_TAB }, { "backspace", PROMPT_KEY_BACKSPACE },
    { "shift", PROMPT_KEY_SHIFT }, { "ctrl", PROMPT_KEY_CTRL }, { "alt", PROMPT_KEY_ALT },
    { "del", PROMPT_KEY_DEL }, { "home", PROMPT_KEY_HOME }, { "end", PROMPT_KEY_END },
    { "pgup", PROMPT_KEY_PGUP }, { "pgdn", PROMPT_KEY_PGDN },
};

/* the pad the prompts show: the one pressed last, a DS4 until then */
static int prompt_lettered;

static const prompt_t *find_prompt(const char *n, int small)
{
    int src = hid_last_source();
    if (src == HID_SOURCE_DS4 || src == HID_SOURCE_PAD)
        prompt_lettered = src == HID_SOURCE_PAD;
    for (size_t i = 0; i < sizeof pad_prompts / sizeof *pad_prompts; i++)
        if (!strcmp(n, pad_prompts[i].name))
            return prompt_chip(prompt_lettered ? pad_prompts[i].pad : pad_prompts[i].ds4, small);
    for (size_t i = 0; i < sizeof key_prompts / sizeof *key_prompts; i++)
        if (!strcmp(n, key_prompts[i].name))
            return prompt_chip(key_prompts[i].id, small);
    if (n[0] == 'f' && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12 && (n[2] == 0 || (k >= 10 && n[3] == 0)))
            return prompt_chip(PROMPT_KEY_F1 + k - 1, small);
    }
    if (n[0] && !n[1] && !(n[0] >= 'A' && n[0] <= 'Z'))
        return prompt_chip_key((unsigned char)n[0], small);
    return NULL;
}

/* prompt(name, x, y [, small]): a button or a key as a chip of the apps'
 * set (prompts.c), its top left at (x, y), 16 px high for 8x16 text, 12
 * with small (by default when the font is 6x12); returns the x after it.
 * prompt(name [, small]) only measures: the width and height.
 * Upper case the pad's buttons ("A", "B", "X", "Y", "START", "L1",
 * "UPDOWN"...), shown as on the pad pressed last: a DS4 (cross, circle...)
 * until another pad is used. Lower case the keyboard's keys, with the
 * names of keyp() ("enter", "esc", "f1", "up") or one character ("s"). */
static int l_prompt(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int measure = !lua_isnumber(L, 2), at = measure ? 2 : 4;
    int small = lua_isnoneornil(L, at) ? rt.g.font->height <= 12 : lua_toboolean(L, at);
    const prompt_t *p = find_prompt(n, small);
    if (!p)
        return luaL_argerror(L, 1, "not a button or a key");
    if (measure) {
        lua_pushinteger(L, p->w);
        lua_pushinteger(L, p->h);
        return 2;
    }
    int x = ival(L, 2), y = ival(L, 3);
    for (int j = 0; j < p->h; j++)
        for (int i = 0; i < p->w; i++) {
            uint32_t c = p->px[j * p->w + i], a = c >> 24;
            if (!a)
                continue;
            if (a < 255) {                      /* the edges: over what is there */
                int b = g16_pget(&rt.g, x + i, y + j);
                if (b < 0)
                    continue;
                uint32_t under = g16_to_rgb24((uint16_t)b), out = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    int u = (int)(under >> sh & 255), v = (int)(c >> sh & 255);
                    out |= (uint32_t)(u + (v - u) * (int)a / 255) << sh;
                }
                c = out;
            }
            g16_pset(&rt.g, x + i, y + j, g16_rgb24(c & 0xFFFFFF));
        }
    lua_pushinteger(L, x + p->w);
    return 1;
}

/* lastinput(): what pressed something last, "keyboard", "ds4" or "pad"
 * (another controller); nil before anything is pressed */
static int l_lastinput(lua_State *L)
{
    int s = hid_last_source();
    if (s == HID_SOURCE_NONE)
        lua_pushnil(L);
    else
        lua_pushstring(L, s == HID_SOURCE_KEYBOARD ? "keyboard" : s == HID_SOURCE_DS4 ? "ds4" : "pad");
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
 *         4 3D triangles drawn and 5 3D pixels written since the last zclear(),
 *         6 Lua instructions of the last frame (update+draw, to the thousand) */
static int l_stat(lua_State *L)
{
    switch (ival(L, 1)) {
    case 0: lua_pushinteger(L, (lua_Integer)(luavm_mem() / 1024)); break;
    case 1: lua_pushnumber(L, rt.last_cpu_us / 1000.0); break;
    case 2: lua_pushinteger(L, rt.fps); break;
    case 3: lua_pushinteger(L, rt.frame); break;
    case 4: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.tris_drawn : 0); break;
    case 5: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.pixels : 0); break;
    case 6: lua_pushinteger(L, (lua_Integer)rt.last_instr_k * 1000); break;
    default: lua_pushnil(L);
    }
    return 1;
}

/* tri(x0, y0, x1, y1, x2, y2, c [, c1, c2]): with three colours, one per
 * corner, blended across the triangle (Gouraud, dithered) */
static int l_tri(lua_State *L)
{
    if (!lua_isnoneornil(L, 8)) {
        uint32_t c0 = (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF);
        g16_tri_gouraud(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
                        c0, (uint32_t)luaL_checkinteger(L, 8), (uint32_t)luaL_optinteger(L, 9, (lua_Integer)c0));
        return 0;
    }
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

/* What animate() needs for a model with a skeleton (bm Animator): its own
 * copy of the rig, the vertices at rest, the bone matrices of the pose. */
typedef struct {
    uint8_t *data;
    bm_rig_t r;
    v3_t *rest;
    float (*mat)[12];
} skel_t;

/* A mesh of Lua: r3d_mesh_t first, so every function can see just that. */
typedef struct {
    r3d_mesh_t m;
    skel_t *skel;
} lmesh_t;

static void skel_free(skel_t *s)
{
    if (!s)
        return;
    free(s->data);
    free(s->rest);
    free(s->mat);
    free(s);
}

static r3d_mesh_t *new_mesh(lua_State *L)
{
    lmesh_t *lm = lua_newuserdatauv(L, sizeof *lm, 0);
    memset(lm, 0, sizeof *lm);
    luaL_setmetatable(L, MESH_MT);
    return &lm->m;
}

static int l_mesh_gc(lua_State *L)
{
    lmesh_t *lm = luaL_checkudata(L, 1, MESH_MT);
    r3d_mesh_free(&lm->m);
    skel_free(lm->skel);
    lm->skel = NULL;
    return 0;
}

/* mesh({x,y,z, x,y,z, ...}, {a,b,c,colour, ...} [, {u0,v0,u1,v1,u2,v2, ...}])
 * - 1-based vertex indices, faces clockwise seen from outside (on screen). With
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

/* models() -> { "name", ... }: the 3D models of the cartridge (MESH
 * section, made with bm Studio), in order */
static int l_models(lua_State *L)
{
    lua_newtable(L);
    bm_model_t m;
    for (int i = 0; rt.mesh && bm_mesh_model(rt.mesh, rt.mesh_size, i, &m) == 0; i++) {
        lua_pushstring(L, m.name);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* Moves the texture corners of a face `inset` pixels towards its middle, on
 * each axis (not past it), so the next tile of the sheet never shows. */
static void uv_inset(float *uv, float inset)
{
    for (int axis = 0; axis < 2; axis++) {
        float lo = uv[axis], hi = uv[axis];
        for (int k = 1; k < 3; k++) {
            float t = uv[k * 2 + axis];
            if (t < lo) lo = t;
            if (t > hi) hi = t;
        }
        if (hi - lo <= 2 * inset)
            continue;
        float mid = (lo + hi) * 0.5f;
        for (int k = 0; k < 3; k++) {
            float *t = &uv[k * 2 + axis];
            if (*t < mid - inset) *t += inset;
            else if (*t > mid + inset) *t -= inset;
        }
    }
}

/* model(name or number) -> a mesh built from the cartridge's MESH section
 * (textured faces use the sprite sheet), or nil if there is no such model */
static int l_model(lua_State *L)
{
    bm_model_t md;
    int found = -1;
    if (rt.mesh) {
        if (lua_type(L, 1) == LUA_TNUMBER) {
            int i = (int)luaL_checkinteger(L, 1) - 1;
            if (bm_mesh_model(rt.mesh, rt.mesh_size, i, &md) == 0)
                found = i;
        } else {
            const char *name = luaL_checkstring(L, 1);
            for (int i = 0; bm_mesh_model(rt.mesh, rt.mesh_size, i, &md) == 0; i++)
                if (strcmp(md.name, name) == 0) {
                    found = i;
                    break;
                }
        }
    } else if (lua_type(L, 1) != LUA_TNUMBER) {
        luaL_checkstring(L, 1);
    }
    if (found < 0) {
        lua_pushnil(L);
        return 1;
    }
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_alloc(m, md.nverts, md.nfaces) != 0)
        return luaL_error(L, "not enough memory for the model");
    for (int i = 0; i < md.nverts; i++) {
        float xyz[3];
        bm_model_vertex(&md, i, xyz);
        m->verts[i] = (v3_t){ xyz[0], xyz[1], xyz[2] };
    }
    const float inset = bm_mesh_inset(rt.mesh);
    for (int f = 0; f < md.nfaces; f++) {
        uint32_t colour;
        float uv[6];
        bm_model_face(&md, f, m->faces + f * 3, &colour, uv);
        if (colour & R3D_TEXTURED) {
            colour = R3D_TEXTURED;
            if (!m->uv) {
                if (r3d_mesh_alloc_uv(m) != 0)
                    return luaL_error(L, "not enough memory for the model");
                m->tex = &rt.sheet;     /* live, as for mesh() */
            }
            uv_inset(uv, inset);
            memcpy(m->uv + f * 6, uv, sizeof uv);
        }
        m->colors[f] = colour;
    }
    r3d_mesh_normals(m);
    /* a skeleton made for this model (the same vertices) comes with it */
    bm_rig_t r;
    if (rt.anim && bm_anim_rig(rt.anim, rt.anim_size, md.name, &r) == 0 && r.nverts == md.nverts) {
        skel_t *sk = calloc(1, sizeof *sk);
        const uint8_t *start = r.bones - (BM_MODEL_NAME + 8);
        if (!sk || !(sk->data = malloc(r.size)) || !(sk->rest = malloc(md.nverts * sizeof(v3_t))) ||
            !(sk->mat = malloc(r.nbones * sizeof *sk->mat))) {
            skel_free(sk);
            return luaL_error(L, "not enough memory for the model");
        }
        memcpy(sk->data, start, r.size);
        bm_rig_read(sk->data, r.size, &sk->r);
        memcpy(sk->rest, m->verts, md.nverts * sizeof(v3_t));
        for (int i = 0; i < r.nbones; i++) {
            static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            memcpy(sk->mat[i], id, sizeof id);
        }
        ((lmesh_t *)m)->skel = sk;
    }
    return 1;
}

/* ---- skeletal animation (bm Animator): the same arithmetic as
 * sdk/studio/js/rig.js */

static void quat_norm(float q[4])
{
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-12f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int k = 0; k < 4; k++) q[k] /= l;
}

/* the shorter way from a to b */
static void quat_slerp(float out[4], const float a[4], const float b[4], float u)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = 1, k0, k1;
    if (d < 0) { d = -d; s = -1; }
    if (d > 0.9995f) {
        k0 = 1 - u;
        k1 = u;
    } else {
        float th = acosf(d), sn = sinf(th);
        k0 = sinf((1 - u) * th) / sn;
        k1 = sinf(u * th) / sn;
    }
    for (int k = 0; k < 4; k++) out[k] = a[k] * k0 + s * b[k] * k1;
    quat_norm(out);
}

static float pose_q[BM_BONES_MAX][4], pose_t[BM_BONES_MAX][3];
static float pose_q2[BM_BONES_MAX][4], pose_t2[BM_BONES_MAX][3];
static float key_q[BM_BONES_MAX][4], key_t[BM_BONES_MAX][3];

/* the pose of clip c at time t into q, tr */
static void pose_at(int nb, const bm_clip_t *c, float t, float (*q)[4], float (*tr)[3])
{
    const float L = c->length > 0 ? c->length : 1;
    if (c->loop) {
        t = fmodf(t, L);
        if (t < 0) t += L;
    } else {
        t = t < 0 ? 0 : t > L ? L : t;
    }
    int n = c->nkeys, ka, kb;
    float first = bm_clip_key(c, nb, 0, NULL, NULL), last = bm_clip_key(c, nb, n - 1, NULL, NULL), ta, tb;
    if (n == 1 || (!c->loop && t < first)) {
        bm_clip_key(c, nb, 0, q, tr);
        return;
    }
    if (t < first || t >= last) {
        if (!c->loop) {
            bm_clip_key(c, nb, n - 1, q, tr);
            return;
        }
        ka = n - 1; kb = 0;
        ta = t < first ? last - L : last;
        tb = t < first ? first : first + L;
    } else {
        ka = 0;
        while (ka + 1 < n && bm_clip_key(c, nb, ka + 1, NULL, NULL) <= t)
            ka++;
        kb = ka + 1;
        ta = bm_clip_key(c, nb, ka, NULL, NULL);
        tb = bm_clip_key(c, nb, kb, NULL, NULL);
    }
    float u = tb > ta ? (t - ta) / (tb - ta) : 0;
    if (c->mode == 2) u = 0;                         /* step */
    else if (c->mode == 1) u = u * u * (3 - 2 * u);  /* smooth */
    bm_clip_key(c, nb, ka, q, tr);
    bm_clip_key(c, nb, kb, key_q, key_t);
    for (int i = 0; i < nb; i++) {
        float a[4] = { q[i][0], q[i][1], q[i][2], q[i][3] };
        quat_slerp(q[i], a, key_q[i], u);
        for (int k = 0; k < 3; k++) tr[i][k] += (key_t[i][k] - tr[i][k]) * u;
    }
}

/* M[i] = M[parent] * T(head + t) * R(q) * T(-head); every vertex follows its bone */
static void skin(lmesh_t *lm, float (*q)[4], float (*tr)[3])
{
    skel_t *s = lm->skel;
    const int nb = s->r.nbones;
    for (int i = 0; i < nb; i++) {
        int parent;
        float h[3], tail[3], r[9];
        bm_rig_bone(&s->r, i, NULL, &parent, h, tail);
        quat_norm(q[i]);
        float x = q[i][0], y = q[i][1], z = q[i][2], w = q[i][3];
        r[0] = 1 - 2 * (y * y + z * z); r[1] = 2 * (x * y - z * w); r[2] = 2 * (x * z + y * w);
        r[3] = 2 * (x * y + z * w); r[4] = 1 - 2 * (x * x + z * z); r[5] = 2 * (y * z - x * w);
        r[6] = 2 * (x * z - y * w); r[7] = 2 * (y * z + x * w); r[8] = 1 - 2 * (x * x + y * y);
        float local[12];
        for (int row = 0; row < 3; row++) {
            local[row * 4] = r[row * 3];
            local[row * 4 + 1] = r[row * 3 + 1];
            local[row * 4 + 2] = r[row * 3 + 2];
            local[row * 4 + 3] = h[row] + tr[i][row] - (r[row * 3] * h[0] + r[row * 3 + 1] * h[1] + r[row * 3 + 2] * h[2]);
        }
        float *m = s->mat[i];
        if (parent < 0) {
            memcpy(m, local, sizeof local);
            continue;
        }
        const float *a = s->mat[parent];
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++)
                m[row * 4 + col] = a[row * 4] * local[col] + a[row * 4 + 1] * local[4 + col] + a[row * 4 + 2] * local[8 + col];
            m[row * 4 + 3] = a[row * 4] * local[3] + a[row * 4 + 1] * local[7] + a[row * 4 + 2] * local[11] + a[row * 4 + 3];
        }
    }
    r3d_mesh_t *mesh = &lm->m;
    for (int v = 0; v < mesh->nverts; v++) {
        const float *m = s->mat[s->r.vbones[v]];
        v3_t p = s->rest[v];
        mesh->verts[v] = (v3_t){ m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                                 m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11] };
    }
    r3d_mesh_normals(mesh);
}

static lmesh_t *skel_mesh(lua_State *L, int idx)
{
    lmesh_t *lm = luaL_checkudata(L, idx, MESH_MT);
    if (!lm->skel)
        luaL_error(L, "this mesh has no skeleton (make one with bm Animator)");
    return lm;
}

/* the clip named (or numbered, from 1) by argument idx */
static void find_clip(lua_State *L, const skel_t *s, int idx, bm_clip_t *c)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        if (bm_rig_clip(&s->r, (int)luaL_checkinteger(L, idx) - 1, c) == 0)
            return;
        luaL_error(L, "no animation number %d", (int)lua_tointeger(L, idx));
    }
    const char *name = luaL_checkstring(L, idx);
    for (int i = 0; bm_rig_clip(&s->r, i, c) == 0; i++)
        if (strcmp(c->name, name) == 0)
            return;
    luaL_error(L, "no animation \"%s\"", name);
}

/* animate(mesh, [clip, time, [clip2, time2, k]]) -> the clip's length: the
 * mesh (from model()) takes the pose of the clip at that time, in seconds
 * (a looping clip goes round); with a second clip, a mix of the two (k = 0
 * the first, 1 the second); with no clip, the rest pose */
static int l_animate(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const int nb = lm->skel->r.nbones;
    float len = 0;
    if (lua_isnoneornil(L, 2)) {
        for (int i = 0; i < nb; i++) {
            pose_q[i][0] = pose_q[i][1] = pose_q[i][2] = 0;
            pose_q[i][3] = 1;
            pose_t[i][0] = pose_t[i][1] = pose_t[i][2] = 0;
        }
    } else {
        bm_clip_t c;
        find_clip(L, lm->skel, 2, &c);
        pose_at(nb, &c, (float)luaL_optnumber(L, 3, 0), pose_q, pose_t);
        len = c.length;
        if (!lua_isnoneornil(L, 4)) {
            bm_clip_t c2;
            find_clip(L, lm->skel, 4, &c2);
            pose_at(nb, &c2, (float)luaL_optnumber(L, 5, 0), pose_q2, pose_t2);
            float k = (float)luaL_optnumber(L, 6, 0.5);
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            for (int i = 0; i < nb; i++) {
                float a[4] = { pose_q[i][0], pose_q[i][1], pose_q[i][2], pose_q[i][3] };
                quat_slerp(pose_q[i], a, pose_q2[i], k);
                for (int j = 0; j < 3; j++) pose_t[i][j] += (pose_t2[i][j] - pose_t[i][j]) * k;
            }
        }
    }
    skin(lm, pose_q, pose_t);
    lua_pushnumber(L, len);
    return 1;
}

/* clips(mesh) -> { {name =, length =, loop =}, ... }: its animations */
static int l_clips(lua_State *L)
{
    lmesh_t *lm = luaL_checkudata(L, 1, MESH_MT);
    lua_newtable(L);
    bm_clip_t c;
    for (int i = 0; lm->skel && bm_rig_clip(&lm->skel->r, i, &c) == 0; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, c.name);
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, c.length);
        lua_setfield(L, -2, "length");
        lua_pushboolean(L, c.loop);
        lua_setfield(L, -2, "loop");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* bone3d(mesh, name or number) -> x, y, z, tx, ty, tz: where the head and
 * the tail of the bone are in the mesh's last pose (its own coordinates, as
 * bounds3d), or nil */
static int l_bone3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const bm_rig_t *r = &lm->skel->r;
    int found = -1;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int i = (int)luaL_checkinteger(L, 2) - 1;
        if (i >= 0 && i < r->nbones) found = i;
    } else {
        const char *name = luaL_checkstring(L, 2);
        char bn[BM_MODEL_NAME + 1];
        for (int i = 0; i < r->nbones && found < 0; i++) {
            bm_rig_bone(r, i, bn, NULL, NULL, NULL);
            if (strcmp(bn, name) == 0) found = i;
        }
    }
    if (found < 0) {
        lua_pushnil(L);
        return 1;
    }
    float h[3], t[3];
    bm_rig_bone(r, found, NULL, NULL, h, t);
    const float *m = lm->skel->mat[found];
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * h[0] + m[k * 4 + 1] * h[1] + m[k * 4 + 2] * h[2] + m[k * 4 + 3]);
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * t[0] + m[k * 4 + 1] * t[1] + m[k * 4 + 2] * t[2] + m[k * 4 + 3]);
    return 6;
}

/* bounds3d(mesh) -> x0, y0, z0, x1, y1, z1: the box around its vertices, in
 * its own coordinates (before draw3d moves, turns and scales it) */
static int l_bounds3d(lua_State *L)
{
    const r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t lo = m->verts[0], hi = m->verts[0];
    for (int i = 1; i < m->nverts; i++) {
        v3_t p = m->verts[i];
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z > hi.z) hi.z = p.z;
    }
    const float v[6] = { lo.x, lo.y, lo.z, hi.x, hi.y, hi.z };
    for (int i = 0; i < 6; i++)
        lua_pushnumber(L, v[i]);
    return 6;
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
 * (floors and backdrops drawn first), 2 unlit (full colour), 4 smooth
 * (Gouraud) */
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

/* ---- save() / saved(): one table per cartridge in /bm/save, as Lua
 * source ("return {...}") read back in an empty environment: data only. */

#define SAVE_DIR   "/bm/save"
#define SAVE_MAX   (32 * 1024)

void bm_save_path(const char *title, const char *author, char *out, size_t n)
{
    char id[96];
    int len = ksnprintf(id, sizeof id, "%s\n%s", title, author);
    ksnprintf(out, n, "%s/%08lX.SAV", SAVE_DIR, crc32(id, (uint32_t)len));
}

/* The text is built in a fixed buffer: a luaL_Buffer cannot be used here,
 * it may sit on the Lua stack while the serializer walks the tables with
 * lua_next (a save of more than about 1 KiB then broke the stack). */
static struct {
    char p[SAVE_MAX + 64];
    size_t n;
} sb;

static void sb_add(lua_State *L, const char *s, size_t len)
{
    if (sb.n + len > SAVE_MAX)
        luaL_error(L, "save: more than %d bytes", SAVE_MAX);
    memcpy(sb.p + sb.n, s, len);
    sb.n += len;
}

static void sb_str(lua_State *L, const char *s) { sb_add(L, s, strlen(s)); }
static void sb_chr(lua_State *L, char c) { sb_add(L, &c, 1); }

static void ser(lua_State *L, int idx, int depth);

static void ser_string(lua_State *L, const char *str, size_t len)
{
    sb_chr(L, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"' || c == '\\') {
            sb_chr(L, '\\');
            sb_chr(L, (char)c);
        } else if (c < 32 || c == 127) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\%03u", c);
            sb_str(L, esc);
        } else {
            sb_chr(L, (char)c);
        }
    }
    sb_chr(L, '"');
}

static void ser_value(lua_State *L, int idx, int depth)
{
    char num[40];
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        sb_str(L, lua_toboolean(L, idx) ? "true" : "false");
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
        sb_str(L, num);
        break;
    case LUA_TSTRING: {
        size_t len;
        const char *str = lua_tolstring(L, idx, &len);
        ser_string(L, str, len);
        break;
    }
    case LUA_TTABLE:
        ser(L, idx, depth + 1);
        break;
    default:
        luaL_error(L, "save: cannot store a %s", luaL_typename(L, idx));
    }
}

static void ser(lua_State *L, int idx, int depth)
{
    if (depth > 16)
        luaL_error(L, "save: tables nested too deep (or a cycle)");
    idx = lua_absindex(L, idx);
    luaL_checkstack(L, 4, "save");
    sb_chr(L, '{');
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        sb_chr(L, '[');
        ser_value(L, -2, depth);
        sb_str(L, "]=");
        ser_value(L, -1, depth);
        sb_chr(L, ',');
        lua_pop(L, 1);
    }
    sb_chr(L, '}');
}

/* save(t): true, or false and a message (no SD card, card full...) */
static int l_save(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    sb.n = 0;
    sb_str(L, "return ");
    ser(L, 1, 0);
    if (fat_mkdirs(SAVE_DIR) != 0 || fat_write_file(SAVE_DIR, rt.save_name, sb.p, sb.n) != 0) {
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
    ksnprintf(path, sizeof path, "save/%s", rt.save_name);
    if (config_find_file(path, &e) != 0 || e.size > SAVE_MAX || fat_load(&e, &data, &len) != 0) {
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

/* "C4", "c#4", "Db3", "A-1", "F#-1" -> MIDI note (C4 = 60). 1 if valid. */
static int note_name(const char *s, float *midi)
{
    static const int8_t pc[7] = { 9, 11, 0, 2, 4, 5, 7 };      /* A B C D E F G */
    char c = (char)(*s >= 'a' ? *s - 32 : *s);
    if (c < 'A' || c > 'G')
        return 0;
    int n = pc[c - 'A'];
    s++;
    if (*s == '#' || *s == 's') { n++; s++; }
    else if (*s == 'b') { n--; s++; }
    if (*s == '-' && s[1] >= '0' && s[1] <= '9' && s[2])
        s++;                                    /* tracker style: "C-4" */
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (*s < '0' || *s > '9')
        return 0;
    int oct = 0;
    while (*s >= '0' && *s <= '9')
        oct = oct * 10 + (*s++ - '0');
    if (*s || oct > 9)
        return 0;
    *midi = (float)(12 * ((neg ? -oct : oct) + 1) + n);
    return 1;
}

/* a pitch: Hz, or a note name ("A4", "C#5", "Bb3") */
static float hz_arg(lua_State *L, int i)
{
    if (lua_type(L, i) == LUA_TSTRING) {
        float m = 0;
        if (!note_name(lua_tostring(L, i), &m))
            luaL_argerror(L, i, "a note name like \"C4\", \"F#3\" or \"Bb2\"");
        return au_note_hz(m);
    }
    lua_Number f = luaL_checknumber(L, i);
    return f <= 0 ? 0.0f : f >= 65535 ? 65535.0f : (float)f;
}

/* hz(note): a MIDI note number (60 = C4, 69 = A4) or a name -> Hz */
static int l_hz(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING) {
        lua_pushnumber(L, hz_arg(L, 1));
        return 1;
    }
    lua_pushnumber(L, au_note_hz((float)luaL_checknumber(L, 1)));
    return 1;
}

static int wave_arg(lua_State *L, int i)
{
    lua_Integer w = luaL_optinteger(L, i, -1);
    return w >= 0 && w < SYNTH_WAVES ? (int)w : -1;
}

/* note(ch, freq, [ms], [wave], [vol]): restarts the envelope; ms > 0
 * releases the note by itself, else it holds until noteoff(ch). freq in
 * Hz or a note name. */
static int l_note(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 0);
    audio_note(ch, hz, ms > 0 ? (uint32_t)ms : 0, wave_arg(L, 4), (int)luaL_optinteger(L, 5, -1));
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

/* playing(ch): true while the voice sounds (release included) or a sound
 * effect or the music holds it */
static int l_playing(lua_State *L)
{
    lua_pushboolean(L, audio_busy(voice_arg(L)));
    return 1;
}

/* apu(ch, reg, [value]): raw register byte, the synthesizer's layout (synth.h) */
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

/* slide(ch, freq, ms): the note of the voice glides to freq */
static int l_slide(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 100);
    audio_slide(ch, hz, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* vibrato(ch, [semitones], [rate_hz]): vibrato(ch) turns it off */
static int l_vibrato(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_vibrato(ch, (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 6));
    return 0;
}

static const char *const chord_names[AU_CHORDS + 1] = {
    "octave", "major", "minor", "sus2", "sus4", "maj7", "min7", "7", "dim", "aug",
    "power", "power8", "major8", "minor8", "down", "octaves", NULL
};

/* arp(ch, chord, [ms]): the note runs through a chord, ms per note
 * (default 50): a name ("major", "minor", "maj7", "min7", "7", "sus2",
 * "sus4", "dim", "aug", "power", "octave"...), or a table of semitones
 * ({0, 4, 7, 12}); arp(ch) turns it off */
static int l_arp(lua_State *L)
{
    unsigned ch = voice_arg(L);
    int8_t semis[8];
    int n = 0;
    if (lua_istable(L, 2)) {
        n = (int)luaL_len(L, 2);
        luaL_argcheck(L, n >= 1 && n <= 8, 2, "1 to 8 semitones");
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, 2, i + 1);
            lua_Integer v = luaL_checkinteger(L, -1);
            semis[i] = (int8_t)(v < -48 ? -48 : v > 48 ? 48 : v);
            lua_pop(L, 1);
        }
    } else if (!lua_isnoneornil(L, 2)) {
        int c = lua_type(L, 2) == LUA_TNUMBER ? (int)lua_tointeger(L, 2) : luaL_checkoption(L, 2, NULL, chord_names);
        luaL_argcheck(L, c >= 0 && c < AU_CHORDS, 2, "chord 0..15");
        n = au_chord_len[c];
        memcpy(semis, au_chord[c], (size_t)n);
    }
    lua_Integer ms = luaL_optinteger(L, 3, 50);
    audio_arp(ch, semis, n, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* sfx(n, [voice], [transpose], [vol]): sound effect n of the cartridge's
 * bank on a voice (nil: a free one), transposed by semitones, at volume
 * 0..1; returns the voice, or nil. sfx(-1, [voice]) stops the effect of a
 * voice, or all of them. */
static int l_sfx(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int ch = -1;
    if (!lua_isnoneornil(L, 2)) {
        lua_Integer v = luaL_checkinteger(L, 2);
        luaL_argcheck(L, v >= 0 && v < 8, 2, "voice 0..7");
        ch = (int)v;
    }
    if (n < 0) {
        audio_sfx_stop(ch);
        return 0;
    }
    int v = audio_sfx((int)n, ch, (int)luaL_optinteger(L, 3, 0), (float)luaL_optnumber(L, 4, 1.0));
    if (v < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, v);
    return 1;
}

/* sfxpos(voice) -> the effect playing on the voice and its step, or nil */
static int l_sfxpos(lua_State *L)
{
    int step = 0, n = audio_sfx_pos((int)voice_arg(L), &step);
    if (n < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, n);
    lua_pushinteger(L, step);
    return 2;
}

/* music(n, [fade_ms], [position]): song n of the bank, from a position
 * (0 = the first pattern), fading in; music(-1, [fade_ms]) stops it;
 * music() -> song, position, step, pattern while it plays, else nil */
static int l_music(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        int song, order, step, pat;
        if (!audio_music_pos(&song, &order, &step, &pat)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, song);
        lua_pushinteger(L, order);
        lua_pushinteger(L, step);
        lua_pushinteger(L, pat);
        return 4;
    }
    lua_Integer n = luaL_checkinteger(L, 1);
    lua_Integer fade = luaL_optinteger(L, 2, 0);
    if (n < 0)
        audio_music_stop((int)fade);
    else
        audio_music((int)n, (int)luaL_optinteger(L, 3, 0), (int)fade);
    return 0;
}

/* tempo(scale): the music plays scale times faster (1 = as written) */
static int l_tempo(lua_State *L)
{
    audio_tempo((float)luaL_checknumber(L, 1));
    return 0;
}

/* mute(track, [on]): a track of the music goes silent (on, the default)
 * or plays again */
static int l_mute(lua_State *L)
{
    lua_Integer t = luaL_checkinteger(L, 1);
    luaL_argcheck(L, t >= 0 && t < 8, 1, "track 0..7");
    audio_mute((int)t, lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2));
    return 0;
}

/* volume([level]) -> the master volume 0..10 (set it with level); the
 * console keeps it in its settings */
static int l_volume(lua_State *L)
{
    if (!lua_isnoneornil(L, 1))
        audio_set_volume((int)luaL_checkinteger(L, 1));
    lua_pushinteger(L, audio_volume());
    return 1;
}

/* audio_bank(data): plays this bank (a string, as cart_audio() gives it)
 * from now on; nil: none. true, or nil and a message. For the editor. */
static int l_audio_bank(lua_State *L)
{
    size_t len = 0;
    const char *d = lua_isnoneornil(L, 1) ? NULL : luaL_checklstring(L, 1, &len);
    char err[64];
    if (audio_bank((const uint8_t *)d, len, err, sizeof err) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* audio_pattern(p, bpm, swing, [step]): pattern p of the bank, looping;
 * audio_pattern(-1) stops. For the editor. */
static int l_audio_pattern(lua_State *L)
{
    lua_Integer p = luaL_checkinteger(L, 1);
    if (p < 0) {
        audio_music_stop(0);
        return 0;
    }
    audio_music_pattern((int)p, (int)luaL_optinteger(L, 2, 120), (int)luaL_optinteger(L, 3, 0),
                        (int)luaL_optinteger(L, 4, 0));
    return 0;
}

/* audio_play(voice, sound, note, [vol], [fx], [ms]): sound of the bank on a
 * voice, as a step plays it (note: MIDI number), released after ms
 * (default 400). For the editor. */
static int l_audio_play(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_play((int)ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
               (int)luaL_optinteger(L, 4, 255), (int)luaL_optinteger(L, 5, 0),
               (uint32_t)luaL_optinteger(L, 6, 400));
    return 0;
}

/* ---------------------------------------------------------------- keys */

/* keyp(): the next key typed, as text ("a", "\n", "\b", "\t"), a name
 * ("up", "down", "left", "right", "home", "end", "pgup", "pgdn", "del",
 * "esc", "f1".."f12") or "^s" for Ctrl+S; nil if none. The first call
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
    else if (c >= HID_KEY_F6 && c <= HID_KEY_F6 + 6) lua_pushfstring(L, "f%d", c - HID_KEY_F6 + 6);
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

/* rawkeys(on): the keyboards stop being controllers for btn() and pad()
 * (the cartridge reads them with keydown(), to map them as it likes); Esc
 * still leaves the cartridge */
static int l_rawkeys(lua_State *L)
{
    rt.raw_keys = lua_toboolean(L, 1);
    return 0;
}

/* keydown(usage): true while the key with this USB HID usage is held on a
 * keyboard (0x04 = A ... 0x1D = Z, 0x28 Enter, 0x4F-0x52 arrows, 0xE0-0xE7
 * Ctrl, Shift, Alt, GUI left then right) */
static int l_keydown(lua_State *L)
{
    lua_Integer u = luaL_checkinteger(L, 1);
    lua_pushboolean(L, u > 0 && u < 256 && hid_usage_held((uint8_t)u));
    return 1;
}

/* keys() -> { usage, ... }: the keys held now */
static int l_keys(lua_State *L)
{
    uint8_t u[16];
    int n = hid_keys_held(u, 16);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, u[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* pad([p]) -> the controller buttons player p (1-4) holds, as bits: 1 left,
 * 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y,
 * 1024 L1, 2048 R1; without p, any player */
static int l_pad(lua_State *L)
{
    uint32_t b = 0;
    if (lua_isnoneornil(L, 1)) {
        for (int p = 0; p < INPUT_PLAYERS; p++)
            b |= rt.praw[p];
    } else {
        lua_Integer p = luaL_checkinteger(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            b = rt.praw[p - 1];
    }
    lua_pushinteger(L, b);
    return 1;
}

/* timeslice(co, [k]): coroutine co yields (resume returns true and
 * nothing) after about k thousand Lua instructions in a frame (default
 * 400), so a long computation goes on over several frames instead of
 * stopping the cartridge; timeslice(nil) turns it off */
static int l_timeslice(lua_State *L)
{
    rt.slice_thread = lua_isthread(L, 1) ? lua_tothread(L, 1) : NULL;
    lua_Integer k = luaL_optinteger(L, 2, 400);
    rt.slice_len = (uint32_t)(k < 10 ? 10 : k > FRAME_BUDGET / 2 ? FRAME_BUDGET / 2 : k);
    rt.slice_at = rt.slice_len;
    return 0;
}

/* cartridge files, for the editor (defined after the asset loader) */
static int l_ls(lua_State *L);
static int l_cart_load(lua_State *L);
static int l_cart_new(lua_State *L);
static int l_cart_save(lua_State *L);
static int l_cart_run(lua_State *L);
static int l_cart_tool(lua_State *L);
static int l_cart_arg(lua_State *L);
static int l_cart_audio(lua_State *L);
static int l_cart_put_audio(lua_State *L);
static int l_cart_data(lua_State *L);
static int l_cart_read(lua_State *L);
static int l_cart_write(lua_State *L);
static int l_cart_meshes(lua_State *L);
static int l_cart_sheet(lua_State *L);

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

/* Lighting by levels, as in Dank Tomb: fades() gives each colour what it
 * becomes at every light level, dark_begin() fills the screen with the
 * ambient level, glow() adds lamps (rings of levels), dark_end() turns
 * every pixel into its colour at its level. */
static int fade_ready(lua_State *L)
{
    if (!rt.fade.lv && g16_fade_init(&rt.fade, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    return 0;
}

/* fades({ {from, l0, l1, ...}, ... }) -> levels: every row has the same
 * length, 3..17 (2..16 levels, l0 the darkest) */
static int l_fades(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    fade_ready(L);
    const lua_Integer n = luaL_len(L, 1);
    int levels = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_geti(L, 1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        const lua_Integer len = luaL_len(L, -1);
        if (i == 1) {
            if (len < 3 || len > G16_FADE_LEVELS + 1)
                return luaL_error(L, "fades: a row is a colour and 2 to %d levels", G16_FADE_LEVELS);
            levels = (int)len - 1;
            g16_fade_reset(&rt.fade, levels);
        } else if (len != levels + 1) {
            return luaL_error(L, "fades: row %d has %d levels, not %d", (int)i, (int)len - 1, levels);
        }
        uint16_t to[G16_FADE_LEVELS];
        lua_geti(L, -1, 1);
        const uint16_t from = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
        lua_pop(L, 1);
        for (int k = 0; k < levels; k++) {
            lua_geti(L, -1, k + 2);
            to[k] = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (g16_fade_colour(&rt.fade, from, to) != 0)
            return luaL_error(L, "fades: more than %d colours", G16_FADE_COLOURS);
    }
    g16_fade_done(&rt.fade);
    lua_pushinteger(L, rt.fade.levels);
    return 1;
}

/* dark_begin([ambient level]): everything drawn so far will be lit by
 * dark_end() (the cartridge draws into RAM from now on) */
static int l_dark_begin(lua_State *L)
{
    fade_ready(L);
    if (video_to_ram(&rt.g) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_fade_clear(&rt.fade, (int)luaL_optinteger(L, 1, 0));
    return 0;
}

/* glow(x, y, radius, level [, dither 0..1]) - world coordinates (camera) */
static int l_glow(lua_State *L)
{
    if (!rt.fade.lv)
        return luaL_error(L, "glow() before dark_begin()");
    const float d = fnum(L, 5, 0.5f);
    g16_fade_glow(&rt.fade, (int)floorf(fnum(L, 1, 0)) - rt.g.cam_x, (int)floorf(fnum(L, 2, 0)) - rt.g.cam_y,
                  (int)fnum(L, 3, 32), (int)luaL_checkinteger(L, 4), (int)(d * 256));
    return 0;
}

static int l_dark_end(lua_State *L)
{
    (void)L;
    if (rt.fade.lv)
        g16_fade_apply(&rt.g, &rt.fade);
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
    { "sget", l_sget }, { "sset", l_sset }, { "print", l_print }, { "font", l_font }, { "camera", l_camera },
    { "prompt", l_prompt }, { "lastinput", l_lastinput },
    { "clip", l_clip }, { "rgb", l_rgb }, { "btn", l_btn }, { "btnp", l_btnp },
    { "players", l_players }, { "stick", l_stick },
    { "time", l_time }, { "stat", l_stat }, { "tri", l_tri },
    { "mesh", l_mesh }, { "mesh_sphere", l_mesh_sphere }, { "mesh_cube", l_mesh_cube },
    { "model", l_model }, { "models", l_models }, { "bounds3d", l_bounds3d },
    { "animate", l_animate }, { "clips", l_clips }, { "bone3d", l_bone3d },
    { "draw3d", l_draw3d }, { "camera3d", l_camera3d }, { "light3d", l_light3d },
    { "fog3d", l_fog3d }, { "project3d", l_project3d }, { "lamp3d", l_lamp3d },
    { "zclear", l_zclear }, { "log", l_log }, { "quit", l_quit },
    { "save", l_save }, { "saved", l_saved },
    { "keyp", l_keyp }, { "keyheld", l_keyheld }, { "rawkeys", l_rawkeys }, { "keydown", l_keydown },
    { "keys", l_keys }, { "pad", l_pad }, { "timeslice", l_timeslice }, { "ls", l_ls }, { "cart_load", l_cart_load }, { "cart_new", l_cart_new },
    { "cart_save", l_cart_save }, { "cart_run", l_cart_run }, { "cart_tool", l_cart_tool }, { "cart_arg", l_cart_arg },
    { "cart_data", l_cart_data },
    { "cart_read", l_cart_read }, { "cart_write", l_cart_write }, { "cart_meshes", l_cart_meshes },
    { "cart_sheet", l_cart_sheet },
    { "light_begin", l_light_begin }, { "light", l_light }, { "light_end", l_light_end },
    { "fades", l_fades }, { "dark_begin", l_dark_begin }, { "glow", l_glow }, { "dark_end", l_dark_end },
    { "note", l_note }, { "noteoff", l_noteoff }, { "freq", l_freq },
    { "envelope", l_envelope }, { "duty", l_duty }, { "playing", l_playing }, { "apu", l_apu },
    { "hz", l_hz }, { "slide", l_slide }, { "vibrato", l_vibrato }, { "arp", l_arp },
    { "sfx", l_sfx }, { "sfxpos", l_sfxpos }, { "music", l_music }, { "tempo", l_tempo },
    { "mute", l_mute }, { "volume", l_volume },
    { "audio_bank", l_audio_bank }, { "audio_pattern", l_audio_pattern }, { "audio_play", l_audio_play },
    { "cart_audio", l_cart_audio }, { "cart_put_audio", l_cart_put_audio },
    { NULL, NULL },
};

/* ---------------------------------------------------------------- state */

static void hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    ++rt.hook_count;
    /* a coroutine given to timeslice(): it stops here and goes on next
     * frame, instead of running into the budget */
    if (L == rt.slice_thread && rt.hook_count >= rt.slice_at && lua_isyieldable(L)) {
        rt.slice_at = rt.hook_count + rt.slice_len;
        lua_yield(L, 0);
        return;
    }
    if (rt.hook_count > FRAME_BUDGET)
        luaL_error(L, "cart timeout: more than %d million instructions in one frame",
                   FRAME_BUDGET * HOOK_EVERY / 1000000);
}

/* nano8 reads its carts from the SD card and draws into the frame */
static int n8_file(const char *path, uint8_t **data, size_t *len)
{
    fat_entry_t e;
    if (fat_find(path, &e) != 0 || e.is_dir || e.size > 4u * 1024 * 1024)
        return -1;
    return fat_load(&e, data, len);
}

static g16_t *n8_target(void)
{
    return &rt.g;
}

static lua_State *new_cart_state(const bm_cart_t *c)
{
    static const n8lua_io_t n8io = { n8_file, n8_target };
    n8lua_set_io(&n8io);
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
    luaL_requiref(L, "n8", luaopen_n8, 1);      /* the nano8 machine (carts/nano8) */
    lua_pop(L, 1);
    ai_lua_open(L);             /* the assistant (M30): idle until asked */
    bm_require_open(L);         /* require "assist": libraries in the kernel */
    static const char *const waves[SYNTH_WAVES] = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL" };
    for (int w = 0; w < SYNTH_WAVES; w++) {
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
    rt.slice_at = rt.slice_len;
    int r = lua_pcall(L, 0, 0, -2);
    rt.frame_instr_k += rt.hook_count;
    if (r != LUA_OK) {
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
 * ESC O P..S F1..F4, ESC [ n ~ (3 delete, 5/6 page up/down, 15 F5,
 * 17-21 F6-F10, 23-24 F11-F12) */
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
        case 17: case 18: case 19: case 20: case 21:
            text_push((uint8_t)(HID_KEY_F6 + rt.esc_num - 17)); break;
        case 23: case 24: text_push((uint8_t)(HID_KEY_F6 + rt.esc_num - 18)); break;
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
    if ((uint8_t)c >= HID_KEY_F6 && (uint8_t)c <= HID_KEY_F6 + 6)
        return;                             /* a UTF-8 byte, not a function key */
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
    for (int k; (k = input_remote_getc()) >= 0; ) {
        char c = (char)k;
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
            case 'u': case 'U': b = SER_L1; break;
            case 'o': case 'O': b = SER_R1; break;
            case '\r': b = BTN_START; break;
            case '\t': b = BTN_SELECT; break;
            case 'p': case 'P': rt.perf_key = 1; continue;
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
    uint32_t pad = input_players(per, rt.text_mode || rt.raw_keys, &quit, &rt.local);
    if (rt.text_mode)
        for (int k; (k = hid_getc()) >= 0;)
            text_push((uint8_t)k);
    uint32_t serial = 0;                    /* HID_* bits of the serial keys held */
    for (int b = 0; b < SER_COUNT; b++)
        if (rt.hold[b]) {
            serial |= b == BTN_X ? HID_X : b == BTN_Y ? HID_Y : b == BTN_START ? HID_START :
                      b == BTN_SELECT ? HID_SELECT : b == SER_L1 ? HID_L1 : b == SER_R1 ? HID_R1 : 1u << b;
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

static int load_assets(const bm_cart_t *c)
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
            if (bm_sheet8_unpack(c, sheet8_set, NULL) != 0)
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

    if (c->mesh) {
        rt.mesh = malloc(c->mesh_size);
        if (!rt.mesh)
            return -1;
        memcpy(rt.mesh, c->mesh, c->mesh_size);
        rt.mesh_size = c->mesh_size;
    }
    if (c->anim) {
        rt.anim = malloc(c->anim_size);
        if (!rt.anim)
            return -1;
        memcpy(rt.anim, c->anim, c->anim_size);
        rt.anim_size = c->anim_size;
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
    free(rt.mesh);
    rt.cell_dirty = NULL;
    rt.map.cells = NULL;
    rt.mesh = NULL;
    rt.mesh_size = 0;
    free(rt.anim);
    rt.anim = NULL;
    rt.anim_size = 0;
}

/* ---------------------------------------------------------------- editor */

/* The project open in the editor lives in the editor's own sprite sheet and
 * map; its cover is kept here as it came. Across the editor's "run" the
 * kernel keeps a request (which file to play) and an argument (the file,
 * and the error it stopped with). */
static uint8_t *proj_cover;
static uint16_t proj_cover_w, proj_cover_h;
/* the sound bank of the project (cart_save keeps it), and of the
 * cartridge playing (a copy: its bytes are freed while it is suspended) */
static uint8_t *proj_audio, *own_audio;
static uint32_t proj_audio_len, own_audio_len;

static void set_copy(uint8_t **dst, uint32_t *dlen, const void *src, uint32_t len)
{
    free(*dst);
    *dst = NULL;
    *dlen = 0;
    if (src && len && (*dst = malloc(len)) != NULL) {
        memcpy(*dst, src, len);
        *dlen = len;
    }
}

/* the project's other sections (3D models and skeletons, and any type this
 * kernel does not know), written back by cart_save */
#define PROJ_EXTRA_MAX 16
static struct { uint32_t type, size; uint8_t *data; } proj_extra[PROJ_EXTRA_MAX];
static int proj_extras;

static void extras_free(void)
{
    for (int i = 0; i < proj_extras; i++)
        free(proj_extra[i].data);
    proj_extras = 0;
}

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* After bm_parse (the section table is known to be in bounds). */
static int extras_keep(const uint8_t *d)
{
    extras_free();
    for (unsigned i = 0; i < d[17]; i++) {
        const uint8_t *e = d + BM_HEADER_SIZE + i * 16;
        uint32_t type = rd32le(e), off = rd32le(e + 4), size = rd32le(e + 8);
        if (type == BM_SEC_LUA || type == BM_SEC_SHEET || type == BM_SEC_SHEET8 ||
            type == BM_SEC_MAP || type == BM_SEC_COVER || !size)
            continue;
        if (type == BM_SEC_AUDIO) {
            if (size >= 4 && memcmp(d + off, "BMAU", 4) == 0)
                continue;                   /* the sound bank: proj_audio */
            type = BM_SEC_MESH;             /* MESH of the first bm Studio files */
        } else if (type == BM_SEC_OLD_ANIM) {
            type = BM_SEC_ANIM;
        }
        if (proj_extras == PROJ_EXTRA_MAX)
            return -1;
        uint8_t *copy = malloc(size);
        if (!copy)
            return -1;
        memcpy(copy, d + off, size);
        proj_extra[proj_extras].type = type;
        proj_extra[proj_extras].size = size;
        proj_extra[proj_extras++].data = copy;
    }
    return 0;
}
static char run_request[64], tool_request[16];
static char arg_path[64], arg_error[512], last_error[512];
static int arg_back = 1;

void bm_set_arg(const char *path, const char *error)
{
    ksnprintf(arg_path, sizeof arg_path, "%s", path ? path : "");
    ksnprintf(arg_error, sizeof arg_error, "%s", error ? error : "");
}

void bm_set_arg_back(int back)
{
    arg_back = back;
}

int bm_take_tool(char *name, size_t n)
{
    if (!tool_request[0])
        return 0;
    ksnprintf(name, n, "%s", tool_request);
    tool_request[0] = 0;
    return 1;
}

int bm_take_run(char *path, size_t n)
{
    if (!run_request[0])
        return 0;
    ksnprintf(path, n, "%s", run_request);
    run_request[0] = 0;
    return 1;
}

const char *bm_last_error(void)
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

/* The resolutions of a cartridge, as the Lua side names them */
static const char *res_name(int w)
{
    return w == 320 ? "320x180" : w == 256 ? "256x256" : "640x360";
}

static int res_width(const char *res)
{
    return strcmp(res, "320x180") == 0 ? 320 : strcmp(res, "256x256") == 0 ? 256 : 640;
}

static void push_project(lua_State *L, const char *title, const char *author, int w, const char *lua,
                         size_t lua_len)
{
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, res_name(w));
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
    bm_cart_t c;
    char err[64];
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    free_assets();
    if (load_assets(&c) != 0 || extras_keep(data) != 0) {
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
    set_copy(&proj_audio, &proj_audio_len, c.audio, c.audio_size);
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    push_project(L, title, author, c.width, c.lua, c.lua_size);
    if (c.sheet8) {                     /* palette = the opaque colours of its SHEET8 palette */
        const uint8_t *p = c.sheet8;
        int nc = p[4] | p[5] << 8, k = 0;
        lua_createtable(L, nc, 0);
        for (int i = 0; i < nc; i++) {
            const uint8_t *e = p + 8 + i * 4;
            if (e[3] < 128)
                continue;
            lua_pushinteger(L, (lua_Integer)(e[0] << 16 | e[1] << 8 | e[2]));
            lua_rawseti(L, -2, ++k);
        }
        lua_setfield(L, -2, "palette");
    }
    free(data);
    return 1;
}

/* cart_sheet([w, h]) -> the width and height of the project's sprite sheet;
 * with w and h (multiples of 8, 8 to 4096) it gets that size: the pixels
 * that fit stay where they are, the new ones are transparent (bm Pixel) */
static int l_cart_sheet(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        int w = (int)luaL_checkinteger(L, 1), h = (int)luaL_checkinteger(L, 2);
        luaL_argcheck(L, w >= G16_CELL && w <= BM_SHEET_MAX && w % G16_CELL == 0, 1, "8 to 4096, a multiple of 8");
        luaL_argcheck(L, h >= G16_CELL && h <= BM_SHEET_MAX && h % G16_CELL == 0, 2, "8 to 4096, a multiple of 8");
        if (w != rt.sheet.w || h != rt.sheet.h) {
            g16_sheet_t ns;
            uint8_t *dirty = calloc((size_t)(w / G16_CELL) * (h / G16_CELL), 1);
            if (!dirty || g16_sheet_alloc(&ns, w, h) != 0) {
                free(dirty);
                return luaL_error(L, "not enough memory for a %dx%d sheet", w, h);
            }
            int cw = w < rt.sheet.w ? w : rt.sheet.w, ch = h < rt.sheet.h ? h : rt.sheet.h;
            for (int y = 0; rt.sheet.px && y < ch; y++) {
                memcpy(ns.px + (size_t)y * w, rt.sheet.px + (size_t)y * rt.sheet.w, (size_t)cw * 2);
                memcpy(ns.alpha + (size_t)y * w, rt.sheet.alpha + (size_t)y * rt.sheet.w, (size_t)cw);
            }
            g16_sheet_free(&rt.sheet);
            rt.sheet = ns;                  /* the same struct: meshes keep their texture */
            free(rt.cell_dirty);
            rt.cell_dirty = dirty;
            memset(rt.cell_dirty, 1, (size_t)(w / G16_CELL) * (h / G16_CELL));
            rt.sheet_dirty = 1;
            sheet_commit();
        }
    }
    lua_pushinteger(L, rt.sheet.w);
    lua_pushinteger(L, rt.sheet.h);
    return 2;
}

/* The project's sheet as a section for cart_write: SHEET8 when it has at
 * most 256 colours (transparent counts as one), else SHEET. A pixel whose
 * RGB565 is the one it had in `old` (the file being rewritten, the same x
 * and y) keeps its 24 bits from there, so what was not drawn on comes back
 * byte for byte; a pixel drawn on takes the 24 bits of the first colour of
 * the palette (the table at index `pal`, bm Pixel's) with its RGB565, else
 * of a colour of the old sheet, else RGB565 widened. The palette's colours
 * come first in the SHEET8 palette, as they are and in their order (also
 * those no pixel uses) while there is room, so it comes back as it was;
 * then the other colours, in the order they appear. NULL without memory. */
#define SHEET_CLEAR 0x01000000u                 /* a transparent pixel */
#define SHEET_HASH  1024                        /* > 256 colours: open addressing */

typedef struct {
    uint32_t key[SHEET_HASH];                   /* colour + 1, 0 = empty */
    int16_t slot[SHEET_HASH];                   /* its palette index, -1 none yet */
    int n;
} colour_set_t;

static int cs_find(colour_set_t *s, uint32_t c, int add)
{
    uint32_t i = (c * 2654435761u) >> 22;
    while (s->key[i] && s->key[i] != c + 1)
        i = (i + 1) & (SHEET_HASH - 1);
    if (!s->key[i]) {
        if (!add || s->n >= 257)
            return -1;
        s->key[i] = c + 1;
        s->slot[i] = -1;
        s->n++;
    }
    return (int)i;
}

static void orig_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    uint32_t *o = ctx;
    uint32_t w = o[0];
    o[1 + (uint32_t)y * w + (uint32_t)x] = rgba[3] >= 128 ? (uint32_t)(rgba[0] << 16 | rgba[1] << 8 | rgba[2])
                                                           : SHEET_CLEAR;
}

static uint8_t *sheet_section(lua_State *L, int pal, const bm_cart_t *old, uint32_t *type, uint32_t *size)
{
    sheet_commit();
    const uint32_t w = (uint32_t)rt.sheet.w, h = (uint32_t)rt.sheet.h, n = w * h;
    const uint32_t ow = old && (old->sheet8 || old->sheet_rgba) ? old->sheet_w : 0;
    const uint32_t oh = ow ? old->sheet_h : 0;
    uint32_t *rgb = malloc(65536 * sizeof *rgb);    /* the 24 bits of an RGB565 drawn (bit 24: not known) */
    uint32_t *fin = malloc((size_t)n * sizeof *fin); /* each pixel's colour, or SHEET_CLEAR */
    uint32_t *orig = ow ? malloc((1 + (size_t)ow * oh) * sizeof *orig) : NULL;
    uint32_t pc[256];                           /* the palette given */
    int npc = 0;
    colour_set_t *set = calloc(1, sizeof *set);
    uint8_t *out = NULL, *idx = NULL;
    if (!rgb || !fin || !set || (ow && !orig))
        goto done;
    for (int k = 0; k < 65536; k++)
        rgb[k] = 1u << 24;
    if (pal && lua_istable(L, pal)) {
        lua_Integer m = luaL_len(L, pal);
        for (lua_Integer i = 1; i <= m && npc < 256; i++) {
            lua_rawgeti(L, pal, i);
            if (lua_isinteger(L, -1)) {
                uint32_t c = (uint32_t)lua_tointeger(L, -1) & 0xFFFFFF;
                pc[npc++] = c;
                if (rgb[g16_rgb24(c)] >> 24)
                    rgb[g16_rgb24(c)] = c;
            }
            lua_pop(L, 1);
        }
    }
    if (orig) {                                 /* the old sheet, pixel by pixel */
        orig[0] = ow;
        if (old->sheet8) {
            if (bm_sheet8_unpack(old, orig_set, orig) != 0)
                goto done;
        } else {
            for (uint32_t i = 0; i < ow * oh; i++) {
                const uint8_t *e = old->sheet_rgba + i * 4;
                orig[1 + i] = e[3] >= 128 ? (uint32_t)(e[0] << 16 | e[1] << 8 | e[2]) : SHEET_CLEAR;
            }
        }
        for (uint32_t i = 0; i < ow * oh; i++)
            if (orig[1 + i] != SHEET_CLEAR && rgb[g16_rgb24(orig[1 + i])] >> 24)
                rgb[g16_rgb24(orig[1 + i])] = orig[1 + i];
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x;
            if (!rt.sheet.alpha[i]) {
                fin[i] = SHEET_CLEAR;
                continue;
            }
            uint16_t k = rt.sheet.px[i];
            uint32_t o = x < ow && y < oh ? orig[1 + y * ow + x] : SHEET_CLEAR;
            if (o != SHEET_CLEAR && g16_rgb24(o) == k)
                fin[i] = o;                     /* not drawn on: as it was */
            else
                fin[i] = rgb[k] >> 24 ? g16_to_rgb24(k) : rgb[k];
        }
    for (uint32_t i = 0; i < n && set->n <= 256; i++)
        cs_find(set, fin[i], 1);
    if (set->n > 256) {                         /* SHEET: RGBA */
        *type = BM_SEC_SHEET;
        *size = 4 + n * 4;
        out = malloc(*size);
        if (!out)
            goto done;
        out[0] = (uint8_t)w; out[1] = (uint8_t)(w >> 8);
        out[2] = (uint8_t)h; out[3] = (uint8_t)(h >> 8);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *e = out + 4 + i * 4;
            uint32_t c = fin[i];
            e[0] = (uint8_t)(c >> 16); e[1] = (uint8_t)(c >> 8); e[2] = (uint8_t)c;
            e[3] = c == SHEET_CLEAR ? 0 : 255;
            if (c == SHEET_CLEAR) e[0] = e[1] = e[2] = 0;
        }
        goto done;
    }
    uint8_t pal_rgba[256 * 4];
    int ncol = 0, waiting = set->n;             /* colours used with no entry yet */
    int clear_at = -1, clr = -1;                /* the old palette's transparent entry keeps its place */
    if (orig && old->sheet8 && old->sheet8_size >= 8) {
        int on = old->sheet8[4] | old->sheet8[5] << 8;
        for (int j = 0; j < on && 12 + (uint32_t)j * 4 <= old->sheet8_size; j++)
            if (old->sheet8[8 + j * 4 + 3] < 128) {
                clear_at = j;
                break;
            }
        clr = clear_at >= 0 ? cs_find(set, SHEET_CLEAR, 0) : -1;
    }
    for (int j = 0; j <= npc; j++) {
        if (clr >= 0 && set->slot[clr] < 0 && ncol == clear_at) {
            memset(pal_rgba + ncol * 4, 0, 4);
            set->slot[clr] = (int16_t)ncol++;
            waiting--;
        }
        if (j == npc)
            break;
        int at = cs_find(set, pc[j], 0);
        int takes = at >= 0 && set->slot[at] < 0;
        if (takes || ncol + waiting < 256) {
            if (takes) {
                set->slot[at] = (int16_t)ncol;
                waiting--;
            }
            uint8_t *e = pal_rgba + ncol++ * 4;
            e[0] = (uint8_t)(pc[j] >> 16); e[1] = (uint8_t)(pc[j] >> 8); e[2] = (uint8_t)pc[j]; e[3] = 255;
        }
    }
    idx = malloc(n ? n : 1);
    if (!idx)
        goto done;
    for (uint32_t i = 0; i < n; i++) {
        int at = cs_find(set, fin[i], 0);
        if (set->slot[at] < 0) {                /* the others, as they appear */
            uint8_t *e = pal_rgba + ncol * 4;
            uint32_t c = fin[i];
            if (c == SHEET_CLEAR) {
                e[0] = e[1] = e[2] = e[3] = 0;
            } else {
                e[0] = (uint8_t)(c >> 16); e[1] = (uint8_t)(c >> 8); e[2] = (uint8_t)c; e[3] = 255;
            }
            set->slot[at] = (int16_t)ncol++;
        }
        idx[i] = (uint8_t)set->slot[at];
    }
    if (ncol == 0) {                            /* no pixels: one transparent colour */
        memset(pal_rgba, 0, 4);
        ncol = 1;
    }
    size_t len = 0;
    out = bm_sheet8_pack((int)w, (int)h, pal_rgba, ncol, idx, &len);
    *type = BM_SEC_SHEET8;
    *size = (uint32_t)len;
done:
    free(idx);
    free(rgb);
    free(fin);
    free(orig);
    free(set);
    return out;
}

/* cart_new(): an empty 256x256 sprite sheet and map, no cover */
static int l_cart_new(lua_State *L)
{
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    free_assets();
    if (load_assets(&c) != 0)
        return luaL_error(L, "not enough memory for the cartridge");
    free(proj_cover);
    proj_cover = NULL;
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    extras_free();
    return 0;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* "/carts/GAME.BM" -> "/carts", "GAME.BM"; a bare name goes to /carts */
static void split_path(const char *path, char *dir, size_t dn, char *name, size_t nn)
{
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n >= dn) n = dn - 1;
        memcpy(dir, path, n);
        dir[n] = 0;
        if (!dir[0]) ksnprintf(dir, dn, "/");
        ksnprintf(name, nn, "%s", slash + 1);
    } else {
        ksnprintf(dir, dn, "/carts");
        ksnprintf(name, nn, "%s", path);
    }
}

static const char *field(lua_State *L, int t, const char *k, const char *def)
{
    lua_getfield(L, t, k);
    const char *s = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return s;
}

/* cart_save(path, {title=, author=, res=, lua=}) -> true, or false and a
 * message. Sprite sheet and map are the running cartridge's; the file
 * name must be 8.3 (e.g. "/carts/MYGAME.BM"). */
static int l_cart_save(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const char *title = field(L, 2, "title", ""), *author = field(L, 2, "author", "");
    const char *res = field(L, 2, "res", "640x360");
    lua_getfield(L, 2, "lua");
    size_t lua_len;
    const char *lua = luaL_checklstring(L, -1, &lua_len);
    int w = res_width(res), h = w == 320 ? 180 : w == 256 ? 256 : 360;

    char dir[64], name[16];
    split_path(path, dir, sizeof dir, name, sizeof name);

    const uint32_t sw = (uint32_t)rt.sheet.w, sh = (uint32_t)rt.sheet.h;
    const uint32_t mw = (uint32_t)rt.map.w, mh = (uint32_t)rt.map.h;
    /* cover (first: the menu reads only the start), code, sheet, map, the
     * sound bank, then the sections kept from the file (3D models...) */
    const int nsec = 5 + proj_extras;
    uint32_t sizes[5 + PROJ_EXTRA_MAX] = { proj_cover ? 4u + (uint32_t)proj_cover_w * proj_cover_h * 4 : 0,
                                           (uint32_t)lua_len, 4 + sw * sh * 4, 4 + mw * mh * 2, proj_audio_len };
    uint32_t types[5 + PROJ_EXTRA_MAX] = { BM_SEC_COVER, BM_SEC_LUA, BM_SEC_SHEET, BM_SEC_MAP, BM_SEC_AUDIO };
    for (int i = 0; i < proj_extras; i++) {
        types[5 + i] = proj_extra[i].type;
        sizes[5 + i] = proj_extra[i].size;
    }
    uint32_t count = 0, total = BM_HEADER_SIZE;
    for (int i = 0; i < nsec; i++)
        if (sizes[i]) { count++; total += 16 + ((sizes[i] + 3) & ~3u); }
    uint8_t *buf = calloc(total, 1);
    if (!buf)
        return luaL_error(L, "not enough memory to save");
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
    for (int i = 0; i < nsec; i++) {
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
        } else if (i == 3) {
            put16(p, mw); put16(p + 2, mh);
            for (uint32_t k = 0; k < mw * mh; k++)
                put16(p + 4 + k * 2, rt.map.cells[k]);
        } else if (i == 4) {
            memcpy(p, proj_audio, proj_audio_len);
        } else {
            memcpy(p, proj_extra[i - 5].data, sizes[i]);
        }
        p += (sizes[i] + 3) & ~3u;
    }
    memcpy(buf, "BMCART\0\0", 8);
    put16(buf + 8, 1);
    put16(buf + 10, BM_HEADER_SIZE);
    put16(buf + 12, (uint32_t)w);
    put16(buf + 14, (uint32_t)h);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)count;
    strncpy((char *)buf + 24, title, 47);
    strncpy((char *)buf + 72, author, 31);
    put32(buf + 20, crc32(buf + BM_HEADER_SIZE, total - BM_HEADER_SIZE));
    int ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
    free(buf);
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_read(path) -> {title, author, res, lua, size}, or nil and a message.
 * Only reads: the running cartridge's sheet and map stay as they are (code
 * editors with several files open). */
static int l_cart_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, res_name(c.width));
    lua_setfield(L, -2, "res");
    lua_pushlstring(L, c.lua, c.lua_size);
    lua_setfield(L, -2, "lua");
    lua_pushinteger(L, (lua_Integer)len);
    lua_setfield(L, -2, "size");
    free(data);
    return 1;
}

lua_State *bm_meshcap_newstate(void)
{
    return luavm_newstate();
}

static int capture_call(lua_State *L)
{
    const bm_cart_t *c = lua_touserdata(L, 1);
    return bm_mesh_capture(L, c->lua, c->lua_size, c->width, c->height, api, c->mesh, c->mesh_size);
}

/* cart_meshes(path) -> { {name=, kind=, verts=, faces=, [uv=]}, ... }, and
 * nil or the first error of the cartridge's code; or nil and a message.
 * The 3D meshes the cartridge builds in its code (mesh(), mesh_sphere(),
 * mesh_cube()), found by running it apart with every other function of bm
 * doing nothing (meshcap.c); the fields are the arguments of mesh(). For
 * bm Mesh. */
static int l_cart_meshes(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushcfunction(L, capture_call);       /* protected: `data` is freed whatever happens */
    lua_pushlightuserdata(L, &c);
    int status = lua_pcall(L, 1, 2, 0);
    free(data);
    if (status != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
    }
    return 2;
}

/* cart_write(path, {[lua=], [title=, author=, res=, from=, sections=,
 * sheet=, palette=]}) -> true, or false and a message. Changes only the
 * code (and the fields given) of the cartridge: its sheet, map, cover, sound
 * bank and any other section stay as they are. lua absent: the code stays
 * too. sections: {[8] = MESH bytes, [9] = ANIM bytes} (false takes them
 * away), checked first (bm Mesh, the 3D tools). sheet = true: the project's
 * sprite sheet (cart_load, sset, cart_sheet) takes the place of the file's,
 * as SHEET8 with `palette` ({0xRRGGBB, ...}) first in its palette when it
 * has at most 256 colours (sheet_section; bm Pixel). from: take the
 * sections from another file ("save as"); from = false: a new cartridge,
 * whatever the file holds now (bm Studio's new project). A file that does not exist yet
 * becomes a new cartridge with the code (its name must then be 8.3; an
 * existing file keeps its long name). */
static int l_cart_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "lua");
    size_t lua_len = 0;
    const char *lua = lua_isnil(L, -1) ? NULL : luaL_checklstring(L, -1, &lua_len);
    bm_put_t put[3];
    int nput = 0;
    lua_getfield(L, 2, "sections");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TTABLE);
        static const uint32_t kinds[2] = { BM_SEC_MESH, BM_SEC_ANIM };
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            lua_Integer t = lua_isinteger(L, -2) ? lua_tointeger(L, -2) : -1;
            if (t != BM_SEC_MESH && t != BM_SEC_ANIM)
                return luaL_error(L, "sections: only 8 (MESH) and 9 (ANIM)");
            lua_pop(L, 1);
        }
        for (int k = 0; k < 2; k++) {
            lua_rawgeti(L, -1, kinds[k]);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            size_t n = 0;
            const uint8_t *s = lua_toboolean(L, -1) ? (const uint8_t *)luaL_checklstring(L, -1, &n) : NULL;
            if (s && (n > 0x1000000 || (kinds[k] == BM_SEC_MESH ? bm_mesh_check(s, (uint32_t)n)
                                                                 : bm_anim_check(s, (uint32_t)n)) < 0)) {
                lua_pushboolean(L, 0);
                lua_pushstring(L, kinds[k] == BM_SEC_MESH ? "broken MESH section" : "broken ANIM section");
                return 2;
            }
            put[nput++] = (bm_put_t){ kinds[k], s, (uint32_t)n };
            lua_pop(L, 1);          /* the string stays alive in the table */
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 2, "from");
    int fresh = lua_isboolean(L, -1) && !lua_toboolean(L, -1);    /* from = false: a new cartridge */
    lua_pop(L, 1);
    const char *base = fresh ? NULL : field(L, 2, "from", path);
    char dir[64], name[FAT_NAME_MAX];
    split_path(path, dir, sizeof dir, name, sizeof name);

    fat_entry_t e;
    uint8_t *old = NULL;
    size_t old_len = 0;
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    char err[64];
    if (base && fat_find(base, &e) == 0 && !e.is_dir) {
        if (fat_load(&e, &old, &old_len) != 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        if (bm_parse(old, old_len, &c, err, sizeof err) != 0) {
            free(old);
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "%s: %s", name, err);
            return 2;
        }
    }
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    const char *t = field(L, 2, "title", old ? title : name);
    const char *a = field(L, 2, "author", author);
    const char *res = field(L, 2, "res", res_name(c.width));
    if (!old && !lua) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "a new cartridge needs its code (lua)");
        return 2;
    }
    uint8_t *sheet = NULL;
    lua_getfield(L, 2, "sheet");
    int want_sheet = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (want_sheet) {
        uint32_t st = 0, ss = 0;
        lua_getfield(L, 2, "palette");
        sheet = sheet_section(L, lua_istable(L, -1) ? lua_gettop(L) : 0, old ? &c : NULL, &st, &ss);
        lua_pop(L, 1);
        if (!sheet) {
            free(old);
            return luaL_error(L, "not enough memory to save the sheet");
        }
        put[nput++] = (bm_put_t){ st, sheet, ss };
    }
    size_t out_len;
    uint8_t *out = bm_rewrite_with(old, old_len, lua, lua_len, t, a, res_width(res), put, nput,
                                   &out_len);
    free(old);
    free(sheet);
    if (!out)
        return luaL_error(L, "not enough memory to save");
    /* an existing file keeps its entry (and its long name); a new one is 8.3 */
    fat_entry_t te;
    int ok;
    if (fat_find(path, &te) == 0 && !te.is_dir)
        ok = fat_replace(path, out, out_len) == 0;
    else
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, out, out_len) == 0;
    free(out);
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

/* cart_tool(name, [path]): leaves this tool for another of the console's
 * ("studio", "animator", "mesh", "pixel", "code", "sdk", "sound") on the
 * file (bm Studio's "Open in bm Animator") */
static int l_cart_tool(lua_State *L)
{
    ksnprintf(tool_request, sizeof tool_request, "%s", luaL_checkstring(L, 1));
    ksnprintf(run_request, sizeof run_request, "%s", luaL_optstring(L, 2, ""));
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
    lua_pushboolean(L, arg_back);
    lua_setfield(L, -2, "back");
    if (arg_error[0]) {
        lua_pushstring(L, arg_error);
        lua_setfield(L, -2, "error");
    }
    return 1;
}

/* cart_audio([path]) -> the sound bank of a .bm file as a string (false
 * if it has none) and its title; nil and a message if it cannot be read.
 * Without a path: the bank of the cartridge playing, or nil. */
static int l_cart_audio(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        if (own_audio)
            lua_pushlstring(L, (const char *)own_audio, own_audio_len);
        else
            lua_pushnil(L);
        return 1;
    }
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    if (c.audio)
        lua_pushlstring(L, (const char *)c.audio, c.audio_size);
    else
        lua_pushboolean(L, 0);
    char title[49];
    memcpy(title, c.title, 48);
    title[48] = 0;
    lua_pushstring(L, title);
    free(data);
    return 2;
}

/* A new cartridge file: header, the Lua source, the bank. */
static uint8_t *new_pack(const char *title, const char *lua, size_t lua_len, const char *audio, size_t alen,
                         uint32_t *total)
{
    uint32_t count = alen ? 2 : 1;
    uint32_t t = BM_HEADER_SIZE + count * 16 + (((uint32_t)lua_len + 3) & ~3u) + (((uint32_t)alen + 3) & ~3u);
    uint8_t *buf = calloc(t, 1);
    if (!buf)
        return NULL;
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
    put32(tab, BM_SEC_LUA);
    put32(tab + 4, (uint32_t)(p - buf));
    put32(tab + 8, (uint32_t)lua_len);
    memcpy(p, lua, lua_len);
    p += ((uint32_t)lua_len + 3) & ~3u;
    if (alen) {
        put32(tab + 16, BM_SEC_AUDIO);
        put32(tab + 20, (uint32_t)(p - buf));
        put32(tab + 24, (uint32_t)alen);
        memcpy(p, audio, alen);
    }
    memcpy(buf, "BMCART\0\0", 8);
    put16(buf + 8, 1);
    put16(buf + 10, BM_HEADER_SIZE);
    put16(buf + 12, 640);
    put16(buf + 14, 360);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)count;
    strncpy((char *)buf + 24, title, 47);
    strncpy((char *)buf + 72, "bm sound", 31);
    put32(buf + 20, crc32(buf + BM_HEADER_SIZE, t - BM_HEADER_SIZE));
    *total = t;
    return buf;
}

/* entry t of the section table of a parsed .bm: the sound bank? */
static int is_bank(const uint8_t *data, const uint8_t *t)
{
    return get32(t) == BM_SEC_AUDIO && get32(t + 8) >= 4 && memcmp(data + get32(t + 4), "BMAU", 4) == 0;
}

/* cart_put_audio(path, bank, [title, lua]): puts the sound bank (a string;
 * nil removes it) into a .bm file, everything else as it was. If the file
 * does not exist, it is made (8.3 name) with that title and Lua source.
 * true, or false and a message. */
static int l_cart_put_audio(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t alen = 0;
    const char *audio = lua_isnoneornil(L, 2) ? NULL : luaL_checklstring(L, 2, &alen);
    char err[64];
    if (audio) {
        au_bank_t *b = malloc(sizeof *b);
        if (!b)
            return luaL_error(L, "not enough memory");
        int bad = au_parse((const uint8_t *)audio, alen, b, err, sizeof err);
        free(b);
        if (bad) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, err);
            return 2;
        }
    }
    fat_entry_t e;
    uint8_t *data, *buf;
    size_t len;
    uint32_t total;
    int ok;
    if (fat_find(path, &e) != 0) {
        size_t lua_len;
        const char *title = luaL_optstring(L, 3, "Sound pack");
        const char *lua = luaL_optlstring(L, 4, NULL, &lua_len);
        if (!lua) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        char dir[64], name[16];
        split_path(path, dir, sizeof dir, name, sizeof name);
        if (!(buf = new_pack(title, lua, lua_len, audio, alen, &total)))
            return luaL_error(L, "not enough memory to save");
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
        free(buf);
    } else {
        bm_cart_t c;
        if (fat_load(&e, &data, &len) != 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        if (bm_parse(data, len, &c, err, sizeof err) != 0) {
            free(data);
            lua_pushboolean(L, 0);
            lua_pushstring(L, err);
            return 2;
        }
        /* the same sections in the same order, the bank last (a type 6
         * section that is not a bank is the MESH of the first bm Studio
         * files: it stays, as MESH; their ANIM of type 7 becomes ANIM) */
        unsigned n = data[17], count = 0;
        total = BM_HEADER_SIZE;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *t = data + BM_HEADER_SIZE + i * 16;
            if (!is_bank(data, t)) {
                count++;
                total += 16 + ((get32(t + 8) + 3) & ~3u);
            }
        }
        if (audio) {
            count++;
            total += 16 + (((uint32_t)alen + 3) & ~3u);
        }
        if (count > 255 || !(buf = calloc(total, 1))) {
            free(data);
            return luaL_error(L, "not enough memory to save");
        }
        memcpy(buf, data, BM_HEADER_SIZE);
        uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *t = data + BM_HEADER_SIZE + i * 16;
            uint32_t size = get32(t + 8), type = get32(t);
            if (is_bank(data, t))
                continue;
            memcpy(tab, t, 16);
            if (type == BM_SEC_AUDIO)
                put32(tab, BM_SEC_MESH);
            else if (type == BM_SEC_OLD_ANIM)
                put32(tab, BM_SEC_ANIM);
            put32(tab + 4, (uint32_t)(p - buf));
            memcpy(p, data + get32(t + 4), size);
            tab += 16;
            p += (size + 3) & ~3u;
        }
        if (audio) {
            put32(tab, BM_SEC_AUDIO);
            put32(tab + 4, (uint32_t)(p - buf));
            put32(tab + 8, (uint32_t)alen);
            put32(tab + 12, 0);
            memcpy(p, audio, alen);
        }
        buf[17] = (uint8_t)count;
        put32(buf + 20, crc32(buf + BM_HEADER_SIZE, total - BM_HEADER_SIZE));
        free(data);
        ok = fat_replace(path, buf, total) == 0;
        free(buf);
    }
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_data(type) -> the bytes of the project's MESH (8) or ANIM (9)
 * section, or nil; cart_data(type, bytes) replaces it (nil or "" takes it
 * away) -> true, or false and a message. The bytes are checked first;
 * model() and animate() see the new ones at once, cart_save writes them.
 * bm Studio and bm Animator of the console edit models and skeletons this way. */
static int l_cart_data(lua_State *L)
{
    int type = (int)luaL_checkinteger(L, 1);
    luaL_argcheck(L, type == BM_SEC_MESH || type == BM_SEC_ANIM, 1, "8 (MESH) or 9 (ANIM)");
    uint8_t **cur = type == BM_SEC_MESH ? &rt.mesh : &rt.anim;
    uint32_t *cur_size = type == BM_SEC_MESH ? &rt.mesh_size : &rt.anim_size;
    if (lua_gettop(L) < 2) {
        if (*cur)
            lua_pushlstring(L, (const char *)*cur, *cur_size);
        else
            lua_pushnil(L);
        return 1;
    }
    size_t n = 0;
    const uint8_t *s = lua_isnil(L, 2) ? NULL : (const uint8_t *)luaL_checklstring(L, 2, &n);
    if (s && !n)
        s = NULL;
    if (s && (n > 0x1000000 || (type == BM_SEC_MESH ? bm_mesh_check(s, (uint32_t)n)
                                                      : bm_anim_check(s, (uint32_t)n)) < 0)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, type == BM_SEC_MESH ? "broken MESH section" : "broken ANIM section");
        return 2;
    }
    int k = 0;
    while (k < proj_extras && proj_extra[k].type != (uint32_t)type)
        k++;
    if (s && k == proj_extras && proj_extras == PROJ_EXTRA_MAX) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "too many sections");
        return 2;
    }
    uint8_t *copy = NULL, *keep = NULL;
    if (s && (!(copy = malloc(n)) || !(keep = malloc(n)))) {
        free(copy);
        return luaL_error(L, "not enough memory for the section");
    }
    free(*cur);
    *cur = copy;
    *cur_size = (uint32_t)n;
    if (s) {
        memcpy(copy, s, n);
        memcpy(keep, s, n);
    }
    if (k < proj_extras) {
        free(proj_extra[k].data);
        if (s) {
            proj_extra[k].data = keep;
            proj_extra[k].size = (uint32_t)n;
        } else {
            for (int i = k; i + 1 < proj_extras; i++)
                proj_extra[i] = proj_extra[i + 1];
            proj_extras--;
        }
    } else if (s) {
        proj_extra[proj_extras].type = (uint32_t)type;
        proj_extra[proj_extras].size = (uint32_t)n;
        proj_extra[proj_extras++].data = keep;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* ---------------------------------------------------------------- player */

/* Two ways to draw a frame:
 * - direct: into the framebuffer's back page (uncached GPU memory; writes
 *   only, pget() is slow);
 * - via RAM: into a cached buffer ("shadow") copied to the back page once
 *   per frame. On the ARM1176 the copy has to read the buffer back from
 *   SDRAM (16 KB data cache, reads ~4x slower than writes), so it is not
 *   obviously a win: bm_bench() measures both. Default: direct. */
static int via_ram;
static int dma_frames;          /* copy by DMA: off until the DMA test passes on the Pi */
static uint16_t *shadow;

void bm_set_via_ram(int on) { via_ram = on; }

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
int bm_via_ram(void) { return via_ram; }
int bm_video_uses_ram(void) { return shadow != NULL; }

/* A square 256x256 cartridge is drawn in the middle of a 480x270 screen
 * (1920x1080 / 4: whole pixels on a 1080p TV), black around it: `box` is
 * the byte offset of its top left corner in a page, 0 for the 16:9 sizes. */
static uint32_t box;

static uint16_t *page_px(const framebuffer_t *fb)
{
    return (uint16_t *)(fb->base + box);
}

int bm_video_enter(framebuffer_t *fb, int w, int h, g16_t *g)
{
    console_suspend(1);
    free(shadow);
    shadow = NULL;
    const int boxed = w == 256 && h == 256;
    const uint32_t fw = boxed ? 480u : (uint32_t)w, fh = boxed ? 270u : (uint32_t)h;
    box = 0;
    if (fb_init_depth(fb, fw, fh, 3, 16) != 0)
        return -1;
    if (boxed) {
        if (fb->width < (uint32_t)w || fb->height < (uint32_t)h)
            return -1;
        box = (fb->height - (uint32_t)h) / 2 * fb->pitch + (fb->width - (uint32_t)w) / 2 * 2;
        memset(fb->mem, 0, (size_t)fb->pitch * fb->height * fb->buffers);
    }
    if (via_ram) {
        shadow = malloc((size_t)w * (size_t)h * 2);
        if (!shadow)
            return -1;
        memset(shadow, 0, (size_t)w * (size_t)h * 2);
        g16_target(g, shadow, (uint32_t)w, w, h, &font_console_8x16);
    } else {
        g16_target(g, page_px(fb), fb->pitch / 2, w, h, &font_console_8x16);
    }
    return 0;
}

uint32_t bm_video_present(framebuffer_t *fb, g16_t *g)
{
    if (!shadow) {
        fb_flip(fb);
        g->px = page_px(fb);
        return 0;
    }
    uint32_t t0 = timer_ticks();
    const uint32_t row = (uint32_t)g->w * 2;
    if (box) {
        uint8_t *dst = fb->base + box;
        for (int y = 0; y < g->h; y++)
            memcpy(dst + (uint32_t)y * fb->pitch, g->px + (uint32_t)y * g->stride, row);
    } else if (fb->pitch == row && dma_ready() && dma_frames) {
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

void bm_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    free(shadow);
    shadow = NULL;
    box = 0;
    fb_init(fb, w, h, 2);
    console_suspend(0);
}

static int enter_mode(framebuffer_t *fb, int w, int h)
{
    return bm_video_enter(fb, w, h, &rt.g);
}

static void leave_mode(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    bm_video_leave(fb, w, h);
    input_flush();              /* keys typed in the game stay in the game */
}

static void present(framebuffer_t *fb, uint32_t *deadline, uint32_t *prev, uint32_t *dropped)
{
    rt.present_us = bm_video_present(fb, &rt.g);
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

/* A cartridge left with Esc / PS / Start+Select (when the caller allows
 * it) stays in memory, frozen: its Lua state, sheet, map, 3D and lights
 * stay as they are; bm_resume() continues from the same frame. */
static struct {
    lua_State *L;
    int active;
    char title[49];
    int w, h;
    g16_t g;                    /* clip and camera at the moment it stopped */
    int used_ram;               /* it was drawing via RAM (lights) */
    uint32_t since;
} susp;

/* Frees what a cartridge holds (after leave_mode). */
static void release(lua_State *L)
{
    audio_reset();
    audio_bank(NULL, 0, NULL, 0);
    set_copy(&own_audio, &own_audio_len, NULL, 0);
    lua_close(L);               /* frees meshes (__gc) before the z-buffer */
    n8lua_close();
    g16_light_free(&rt.light);
    g16_fade_free(&rt.fade);
    if (rt.r3d_ready)
        r3d_free(&rt.r3d);
    rt.r3d_ready = 0;
    free_assets();
}

static int vol_start;                   /* the volume when the cartridge started or resumed */

/* ---------------------------------------------------------------- the dev kit */

/* The performance overlay, over any game (Settings > Performance overlay,
 * F3 on a keyboard, 'p' on the serial line): its frames a second, the CPU
 * time of _update + _draw and the Lua instructions of a frame (the mean and,
 * after ^, the most of the last second), and the time of the last 64 frames
 * against the 16.7 ms of a frame at 60 Hz. Drawn on the 8x16 grid, top right:
 *   60fps 6.1ms ^7.5
 *   lua 9k ^10k        */
#define PERF_N 64
static int perf_on;
static struct { uint16_t us10[PERF_N], k[PERF_N]; uint32_t at; } perf;

void bm_set_perf(int on) { perf_on = on != 0; }
int bm_perf(void) { return perf_on; }

static void perf_frame(void)
{
    uint32_t i = perf.at++ % PERF_N;
    perf.us10[i] = (uint16_t)(rt.last_cpu_us / 10 > 65535 ? 65535 : rt.last_cpu_us / 10);
    perf.k[i] = (uint16_t)(rt.last_instr_k > 65535 ? 65535 : rt.last_instr_k);
    if (!perf_on)
        return;
    uint32_t n = perf.at < 60 ? perf.at : 60, sum = 0, most = 0, ksum = 0, kmost = 0;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t k = (perf.at - 1 - j) % PERF_N;
        sum += perf.us10[k];
        ksum += perf.k[k];
        if (perf.us10[k] > most) most = perf.us10[k];
        if (perf.k[k] > kmost) kmost = perf.k[k];
    }
    uint32_t mean = sum / n, kmean = ksum / n;
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    g->font = &font_console_8x16;
    const int x = g->w - 144;                     /* the text on the 8 x 16 grid (read by the tests) */
    g16_rectfill(g, x - 4, 0, 148, 50, g16_rgb(8, 8, 16));
    char line[32];
    ksnprintf(line, sizeof line, "%lufps %lu.%lums ^%lu.%lu", rt.fps, mean / 100, mean / 10 % 10,
              most / 100, most / 10 % 10);
    g16_text(g, x, 0, line, g16_rgb(232, 232, 216));
    ksnprintf(line, sizeof line, "lua %luk ^%luk", kmean, kmost);
    g16_text(g, x, 16, line, g16_rgb(200, 184, 120));
    /* the last 64 frames, 2 px each; the top is 16.7 ms (a frame at 60 Hz) */
    for (uint32_t j = 0; j < PERF_N && j < perf.at; j++) {
        uint32_t v = perf.us10[(perf.at - 1 - j) % PERF_N];
        int hgt = (int)(v * 14 / 1670);
        if (hgt > 14) hgt = 14;
        if (hgt < 1) hgt = 1;
        uint16_t c = v < 835 ? g16_rgb(72, 200, 96) : v < 1670 ? g16_rgb(232, 200, 64) : g16_rgb(232, 64, 48);
        g16_rectfill(g, x + 140 - (int)j * 2, 48 - hgt, 2, hgt, c);
    }
    g16_line(g, x - 2, 33, x + 141, 33, g16_rgb(72, 72, 96));
    *g = keep;
}

/* The frame loop, then either suspend or close. */
static int run_frames(framebuffer_t *fb, lua_State *L, const char *title, int w, int h,
                      uint32_t con_w, uint32_t con_h, uint32_t seconds, bm_stats_t *st,
                      const char *error, int suspendable)
{
    uint32_t start = timer_ticks(), deadline = start + FRAME_US, prev = start;
    uint32_t fps_t0 = start, fps_frames = 0;
    int left = 0;                           /* Esc, PS, Start+Select, 'q' */
    while (!error) {
        if (rt.quit || timer_ticks() - start >= seconds * 1000000u)
            break;
        if (poll_keys()) {
            left = 1;
            break;
        }
        audio_idle();
        /* F3, or 'p' from the serial line: the dev kit (not while typing: the
         * editors have their own F keys) */
        int f3 = !rt.text_mode && hid_usage_held(0x3C);
        if ((f3 && !rt.f3_held) || rt.perf_key)
            perf_on = !perf_on;
        rt.f3_held = f3;
        rt.perf_key = 0;
        uint32_t t0 = timer_ticks();
        rt.frame_instr_k = 0;
        if (call(L, "_update") != 0 || call(L, "_draw") != 0) {
            error = lua_tostring(L, -1);
            break;
        }
        rt.last_cpu_us = timer_ticks() - t0;
        rt.last_instr_k = rt.frame_instr_k;
        perf_frame();
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

    if (audio_volume() != vol_start)
        config_save();                      /* the volume chosen in the game stays */
    st->frames = (uint32_t)rt.frame;
    st->elapsed_us = timer_ticks() - start;
    st->lua_kb = (uint32_t)(luavm_mem() / 1024);
    st->ok = error == NULL;

    if (!error && left && suspendable) {
        audio_pause(1);                     /* music waits, the bank stays */
        susp.L = L;
        susp.active = 1;
        susp.w = w;
        susp.h = h;
        susp.g = rt.g;
        susp.used_ram = bm_video_uses_ram();
        susp.since = timer_ticks();
        ksnprintf(susp.title, sizeof susp.title, "%s", title);
        leave_mode(fb, con_w, con_h);
        hid_text_mode(0);
        last_error[0] = 0;
        kprintf("bm: \"%s\" suspended (A on its cover resumes it)\n", title);
        return BM_SUSPENDED;
    }

    audio_reset();
    leave_mode(fb, con_w, con_h);
    ksnprintf(last_error, sizeof last_error, "%s", error ? error : "");
    hid_text_mode(0);
    if (error)
        kprintf("\x1b[91mbm: \"%s\" stopped with an error:\n%s\x1b[0m\n", title, error);
    release(L);
    return BM_ENDED;
}

int bm_run(framebuffer_t *fb, const uint8_t *data, size_t len,
            uint32_t seconds, bm_stats_t *st, int suspendable)
{
    bm_cart_t cart;
    char err[64];
    const char *error = NULL;
    lua_State *L = NULL;

    bm_close_suspended();              /* one cartridge in memory at a time */
    memset(st, 0, sizeof *st);
    memset(&rt, 0, sizeof rt);
    free(proj_cover);                  /* no project open yet (cart_load) */
    proj_cover = NULL;
    extras_free();
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    if (bm_parse(data, len, &cart, err, sizeof err) != 0) {
        kprintf("\x1b[91mbm: %s\x1b[0m\n", err);
        return BM_ENDED;
    }
    memcpy(st->title, cart.title, sizeof st->title);
    if (load_assets(&cart) != 0 || !(L = new_cart_state(&cart))) {
        free_assets();
        kprintf("\x1b[91mbm: out of memory\x1b[0m\n");
        return BM_ENDED;
    }

    const uint32_t con_w = fb->width, con_h = fb->height;
    if (enter_mode(fb, cart.width, cart.height) != 0) {
        leave_mode(fb, con_w, con_h);
        lua_close(L);
        free_assets();
        kprintf("\x1b[91mbm: cannot set %ux%u RGB565\x1b[0m\n", cart.width, cart.height);
        return BM_ENDED;
    }

    {
        char path[40];
        bm_save_path(cart.title, cart.author, path, sizeof path);
        ksnprintf(rt.save_name, sizeof rt.save_name, "%s", path + sizeof SAVE_DIR);
    }
    audio_reset();
    vol_start = audio_volume();
    {
        char aerr[64];
        if (audio_bank(cart.audio, cart.audio_size, aerr, sizeof aerr) != 0)
            kprintf("\x1b[91mbm: sound bank not loaded: %s\x1b[0m\n", aerr);
        set_copy(&own_audio, &own_audio_len, cart.audio, cart.audio_size);
    }
    rt.start_us = timer_ticks();
    rt.hook_count = 0;
    if (luaL_loadbuffer(L, cart.lua, cart.lua_size, "=main.lua") != LUA_OK ||
        (lua_pushcfunction(L, traceback), lua_insert(L, -2), lua_pcall(L, 0, 0, -2)) != LUA_OK ||
        call(L, "_init") != 0)
        error = lua_tostring(L, -1);
    return run_frames(fb, L, cart.title, cart.width, cart.height, con_w, con_h, seconds, st,
                      error, suspendable);
}

void bm_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, bm_stats_t *st)
{
    bm_run(fb, data, len, seconds, st, 0);
}

int bm_resume(framebuffer_t *fb, uint32_t seconds, bm_stats_t *st)
{
    if (!susp.active)
        return BM_ENDED;
    memset(st, 0, sizeof *st);
    memcpy(st->title, susp.title, sizeof st->title);
    lua_State *L = susp.L;
    susp.active = 0;
    susp.L = NULL;
    const uint32_t con_w = fb->width, con_h = fb->height;
    if (enter_mode(fb, susp.w, susp.h) != 0) {
        leave_mode(fb, con_w, con_h);
        release(L);
        kprintf("\x1b[91mbm: cannot set %ux%u RGB565\x1b[0m\n", susp.w, susp.h);
        return BM_ENDED;
    }
    /* the same clip, camera and draw target as when it stopped */
    rt.g.cx0 = susp.g.cx0; rt.g.cy0 = susp.g.cy0; rt.g.cx1 = susp.g.cx1; rt.g.cy1 = susp.g.cy1;
    rt.g.cam_x = susp.g.cam_x; rt.g.cam_y = susp.g.cam_y;
    rt.g.font = susp.g.font;
    if (susp.used_ram)
        video_to_ram(&rt.g);
    /* the time spent in the menu does not count for time() */
    rt.start_us += timer_ticks() - susp.since;
    rt.esc = 0;
    memset(rt.hold, 0, sizeof rt.hold);
    input_flush();
    /* buttons still held (the A that resumed) are not new presses */
    rt.now = rt.prev = hid_to_btn(hid_buttons());
    for (int p = 0; p < INPUT_PLAYERS; p++)
        rt.pnow[p] = rt.pprev[p] = rt.now;
    hid_text_mode(rt.text_mode);
    audio_pause(0);
    vol_start = audio_volume();
    kprintf("bm: \"%s\" resumed\n", susp.title);
    return run_frames(fb, L, susp.title, susp.w, susp.h, con_w, con_h, seconds, st, NULL, 1);
}

int bm_suspended(char *title, size_t n)
{
    if (title && n)
        ksnprintf(title, n, "%s", susp.active ? susp.title : "");
    return susp.active;
}

void bm_close_suspended(void)
{
    if (!susp.active)
        return;
    susp.active = 0;
    kprintf("bm: \"%s\" closed, memory freed\n", susp.title);
    release(susp.L);
    susp.L = NULL;
}

void bm_print_stats(const bm_stats_t *st)
{
    if (!st->frames)
        return;
    uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
    uint32_t avg = st->cpu_us_total / st->frames;
    kprintf("bm: \"%s\" %lu frames, %lu.%lu fps, %lu dropped\n",
            st->title, st->frames, fps10 / 10, fps10 % 10, st->dropped);
    uint32_t copy = st->copy_us_total / st->frames;
    kprintf("     update+draw avg %lu.%02lu ms (%lu%% of frame), max %lu.%02lu ms, Lua %lu KiB\n",
            avg / 1000, avg % 1000 / 10, avg * 100 / FRAME_US,
            st->cpu_us_max / 1000, st->cpu_us_max % 1000 / 10, st->lua_kb);
    if (copy)
        kprintf("     copy to screen %lu.%02lu ms per frame\n", copy / 1000, copy % 1000 / 10);
}

/* ---------------------------------------------------------------- C bench */

uint32_t bm_bench(framebuffer_t *fb, uint32_t frames)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    bm_close_suspended();
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
        g16_text(&rt.g, 0, 0, "bm C benchmark: full map + 256 sprites 16x16", 0xFFFF);
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
 * is not chosen automatically: see bm_set_via_ram. */
static void ms2(const char *label, uint32_t us)
{
    kprintf("%s %lu.%02lu ms", label, us / 1000, us % 1000 / 10);
}

void bm_set_dma_frames(int on) { dma_frames = on; }

void bm_bench_report(framebuffer_t *fb, uint32_t frames)
{
    int saved = via_ram;
    via_ram = 0;
    uint32_t direct = bm_bench(fb, frames);
    via_ram = 1;
    uint32_t ram = bm_bench(fb, frames);
    via_ram = saved;
    kprintf("bm bench (map + 256 sprites):");
    ms2(" direct", direct);
    ms2(", via RAM", ram);
    kprintf("\n");
}
