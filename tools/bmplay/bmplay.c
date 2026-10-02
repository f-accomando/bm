/*
 * bmplay: a Lua cartridge (.bm) played on the PC with the console's own
 * drawing (src/bm/gfx16.c: sprites, map, text, the lighting by levels) and
 * sound (src/audio/synth.c, player.c), its buttons pressed by a bot in Lua.
 * It writes the frames as raw RGB (for ffmpeg) and the sound as 48 kHz
 * signed 16-bit mono, to make videos of the games (tools/bmplay/video.sh).
 *
 *   bmplay CART.bm [options]
 *     --bot FILE      a Lua file run after the cartridge's _init: every frame
 *                     bmplay calls its global bot(frame), which returns the
 *                     buttons held (bits as btn(): 0 left, 1 right, 2 up,
 *                     3 down, 4 A, 5 B, 6 X, 7 Y, 8 Start, 9 Select; 10 L1,
 *                     11 R1) and true as a second value to stop
 *     --frames N      at most N frames (60 a second)
 *     --seed N        math.randomseed(N) before the cartridge runs (default 1)
 *     --video FILE    the frames, raw RGB24 ("-" for stdout)
 *     --every K       one frame in K (2: 30 frames a second)
 *     --scale S       every pixel S x S
 *     --audio FILE    the sound, raw s16le mono at 48 kHz
 *     --png FILE      the last frame drawn, as a PPM picture
 *     --size          prints the screen size ("256x256") and stops
 *
 * The API is the part of the console's (src/bm/runtime.c) the games of the
 * console need: drawing, map, text, lighting, buttons, notes; prompt()
 * draws a plain chip. timeslice() does nothing: a coroutine runs through.
 */
#include "bm/bm.h"
#include "bm/gfx16.h"
#include "audio/synth.h"
#include "audio/player.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define FPS 60
#define BLOCK 64

static struct {
    g16_t g;
    uint16_t *px;
    g16_sheet_t sheet;
    g16_map_t map;
    g16_fade_t fade;
    uint16_t now, prev;         /* btn() bits */
    uint32_t raw;               /* pad() bits */
    long frame;
    int quit;
    uint8_t regs[SYNTH_REG_BYTES];
    synth_t synth;
    player_t player;
} rt;

static uint16_t col(lua_State *L, int idx, uint32_t def) { return g16_rgb24((uint32_t)luaL_optinteger(L, idx, def)); }
static int ival(lua_State *L, int idx) { return (int)luaL_checknumber(L, idx); }
static int oval(lua_State *L, int idx, int def) { return lua_isnoneornil(L, idx) ? def : (int)luaL_checknumber(L, idx); }
static float fnum(lua_State *L, int idx, float def) { return (float)luaL_optnumber(L, idx, def); }

static int l_cls(lua_State *L)      { g16_cls(&rt.g, col(L, 1, 0)); return 0; }
static int l_pset(lua_State *L)     { g16_pset(&rt.g, ival(L, 1), ival(L, 2), col(L, 3, 0xFFFFFF)); return 0; }
static int l_line(lua_State *L)     { g16_line(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_rect(lua_State *L)     { g16_rect(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_rectfill(lua_State *L) { g16_rectfill(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_circ(lua_State *L)     { g16_circ(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), col(L, 4, 0xFFFFFF)); return 0; }
static int l_circfill(lua_State *L) { g16_circfill(&rt.g, ival(L, 1), ival(L, 2), ival(L, 3), col(L, 4, 0xFFFFFF)); return 0; }
static int l_camera(lua_State *L)   { g16_camera(&rt.g, oval(L, 1, 0), oval(L, 2, 0)); return 0; }
static int l_clip(lua_State *L)     { g16_clip(&rt.g, oval(L, 1, 0), oval(L, 2, 0), oval(L, 3, 0), oval(L, 4, 0)); return 0; }

static int l_pget(lua_State *L)
{
    int c = g16_pget(&rt.g, ival(L, 1), ival(L, 2));
    if (c < 0) lua_pushnil(L);
    else lua_pushinteger(L, g16_to_rgb24((uint16_t)c));
    return 1;
}

static int l_spr(lua_State *L)
{
    g16_spr(&rt.g, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), oval(L, 4, 1), oval(L, 5, 1),
            lua_toboolean(L, 6), lua_toboolean(L, 7));
    return 0;
}

static int l_sspr(lua_State *L)
{
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

static int l_print(lua_State *L)
{
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    int scale = oval(L, 5, 1);
    if (scale > 8) scale = 8;
    const char *s = luaL_tolstring(L, 1, NULL);
    lua_pushinteger(L, g16_text_scaled(&rt.g, x, y, s, c, scale));
    return 1;
}

static int l_font(lua_State *L)
{
    lua_pushinteger(L, rt.g.font->width);
    lua_pushinteger(L, rt.g.font->height);
    return 2;
}

/* a plain chip with the button's name (the console draws its icon) */
static int l_prompt(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushinteger(L, 16);
        lua_pushinteger(L, 16);
        return 2;
    }
    int x = ival(L, 2), y = ival(L, 3);
    g16_rectfill(&rt.g, x, y + 1, 15, 14, g16_rgb24(0xE8E8E8));
    char s[2] = { n[0], 0 };
    g16_text(&rt.g, x + 4, y, s, g16_rgb24(0x202020));
    lua_pushinteger(L, x + 16);
    return 1;
}

static int l_lastinput(lua_State *L) { lua_pushstring(L, "pad"); return 1; }

static int btn_index(lua_State *L) { return (int)luaL_checkinteger(L, 1); }

static int l_btn(lua_State *L)
{
    int b = btn_index(L);
    lua_pushboolean(L, b >= 0 && b < 16 && (rt.now >> b & 1));
    return 1;
}

static int l_btnp(lua_State *L)
{
    int b = btn_index(L);
    lua_pushboolean(L, b >= 0 && b < 16 && (rt.now >> b & 1) && !(rt.prev >> b & 1));
    return 1;
}

static int l_pad(lua_State *L) { lua_pushinteger(L, rt.raw); return 1; }
static int l_time(lua_State *L) { lua_pushnumber(L, (double)rt.frame / FPS); return 1; }

static int l_stat(lua_State *L)
{
    int n = ival(L, 1);
    if (n == 2) lua_pushinteger(L, FPS);
    else if (n == 3) lua_pushinteger(L, rt.frame);
    else lua_pushinteger(L, 0);
    return 1;
}

static int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        fprintf(stderr, "%s%s", i > 1 ? "\t" : "", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    fputc('\n', stderr);
    return 0;
}

static int l_quit(lua_State *L) { (void)L; rt.quit = 1; return 0; }
static int l_nothing(lua_State *L) { (void)L; return 0; }

/* ---- sound */
static unsigned voice_arg(lua_State *L) { return (unsigned)luaL_checkinteger(L, 1) % SYNTH_VOICES; }
static volatile uint8_t *voice(unsigned ch) { return rt.regs + (ch % SYNTH_VOICES) * SYNTH_VOICE_BYTES; }
static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static float hz_arg(lua_State *L, int i)
{
    lua_Number f = luaL_checknumber(L, i);
    return f <= 0 ? 0.0f : f >= 65535 ? 65535.0f : (float)f;
}

static int l_note(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 0);
    lua_Integer w = luaL_optinteger(L, 4, -1);
    int vol = (int)luaL_optinteger(L, 5, -1);
    volatile uint8_t *v = voice(ch);
    if (w >= 0)
        v[SYNTH_WAVEFORM] = (uint8_t)(w < SYNTH_WAVES ? w : 0);
    if (vol >= 0)
        v[SYNTH_VOLUME] = clamp8(vol);
    player_lua_note(&rt.player, (int)ch, hz, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

static int l_noteoff(lua_State *L) { player_lua_off(&rt.player, (int)voice_arg(L)); return 0; }
static int l_freq(lua_State *L) { player_lua_freq(&rt.player, (int)voice_arg(L), hz_arg(L, 2)); return 0; }

static int l_envelope(lua_State *L)
{
    volatile uint8_t *v = voice(voice_arg(L));
    v[SYNTH_ATTACK] = clamp8((int)luaL_checkinteger(L, 2));
    v[SYNTH_DECAY] = clamp8((int)luaL_checkinteger(L, 3));
    v[SYNTH_SUSTAIN] = clamp8((int)luaL_checkinteger(L, 4));
    v[SYNTH_RELEASE] = clamp8((int)luaL_checkinteger(L, 5));
    return 0;
}

static int l_duty(lua_State *L) { voice(voice_arg(L))[SYNTH_DUTY] = clamp8((int)luaL_checkinteger(L, 2)); return 0; }

/* ---- lighting by levels */
static int fade_ready(lua_State *L)
{
    if (!rt.fade.lv && g16_fade_init(&rt.fade, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    return 0;
}

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

static int l_dark_begin(lua_State *L)
{
    fade_ready(L);
    g16_fade_clear(&rt.fade, (int)luaL_optinteger(L, 1, 0));
    return 0;
}

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

static const luaL_Reg api[] = {
    { "cls", l_cls }, { "pset", l_pset }, { "pget", l_pget }, { "line", l_line }, { "rect", l_rect },
    { "rectfill", l_rectfill }, { "circ", l_circ }, { "circfill", l_circfill }, { "camera", l_camera },
    { "clip", l_clip }, { "spr", l_spr }, { "sspr", l_sspr }, { "map", l_map }, { "mget", l_mget },
    { "mset", l_mset }, { "print", l_print }, { "font", l_font }, { "prompt", l_prompt },
    { "lastinput", l_lastinput }, { "btn", l_btn }, { "btnp", l_btnp }, { "pad", l_pad }, { "time", l_time },
    { "stat", l_stat }, { "log", l_log }, { "quit", l_quit }, { "timeslice", l_nothing },
    { "note", l_note }, { "noteoff", l_noteoff }, { "freq", l_freq }, { "envelope", l_envelope },
    { "duty", l_duty }, { "fades", l_fades }, { "dark_begin", l_dark_begin }, { "glow", l_glow },
    { "dark_end", l_dark_end }, { NULL, NULL },
};

/* ---- loading */
static void sheet8_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    (void)ctx;
    g16_sheet_set(&rt.sheet, x, y, g16_rgb(rgba[0], rgba[1], rgba[2]), rgba[3] >= 128);
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static int call(lua_State *L, const char *fn)
{
    if (lua_getglobal(L, fn) != LUA_TFUNCTION) {
        lua_pop(L, 1);
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "bmplay: %s: %s\n", fn, lua_tostring(L, -1));
        return -1;
    }
    return 0;
}

static int traceback(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

static void write_frame(FILE *f, int scale)
{
    static uint8_t *row;
    const int w = rt.g.w, h = rt.g.h;
    if (!row) row = malloc((size_t)w * scale * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = g16_to_rgb24(rt.px[y * w + x]);
            for (int k = 0; k < scale; k++) {
                uint8_t *p = row + ((size_t)x * scale + k) * 3;
                p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c;
            }
        }
        for (int k = 0; k < scale; k++)
            fwrite(row, 3, (size_t)w * scale, f);
    }
}

int main(int argc, char **argv)
{
    const char *cart = NULL, *bot = NULL, *video = NULL, *audio = NULL, *png = NULL;
    int size_only = 0;
    long frames = 60L * 60 * 30;
    int every = 1, scale = 1;
    lua_Integer seed = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--bot") && v) bot = argv[++i];
        else if (!strcmp(a, "--frames") && v) frames = atol(argv[++i]);
        else if (!strcmp(a, "--seed") && v) seed = atol(argv[++i]);
        else if (!strcmp(a, "--video") && v) video = argv[++i];
        else if (!strcmp(a, "--every") && v) every = atoi(argv[++i]);
        else if (!strcmp(a, "--scale") && v) scale = atoi(argv[++i]);
        else if (!strcmp(a, "--audio") && v) audio = argv[++i];
        else if (!strcmp(a, "--png") && v) png = argv[++i];
        else if (!strcmp(a, "--size")) size_only = 1;
        else if (a[0] != '-' && !cart) cart = a;
        else { fprintf(stderr, "bmplay: what is %s?\n", a); return 2; }
    }
    if (!cart) {
        fprintf(stderr, "usage: bmplay CART.bm [--bot FILE] [--frames N] [--seed N] [--video FILE] [--every K]"
                        " [--scale S] [--audio FILE] [--png FILE]\n");
        return 2;
    }
    if (every < 1) every = 1;
    if (scale < 1) scale = 1;

    size_t len;
    uint8_t *data = slurp(cart, &len);
    if (!data) return 1;
    bm_cart_t c;
    char err[128];
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "bmplay: %s: %s\n", cart, err);
        return 1;
    }
    const int w = c.width ? c.width : 640, h = c.height ? c.height : 360;
    if (size_only) {
        printf("%dx%d\n", w, h);
        return 0;
    }
    rt.px = calloc((size_t)w * h, 2);
    g16_target(&rt.g, rt.px, (uint32_t)w, w, h, &font_console_8x16);
    int has = c.sheet_rgba || c.sheet8;
    if (g16_sheet_alloc(&rt.sheet, has ? c.sheet_w : 256, has ? c.sheet_h : 256) != 0) return 1;
    if (c.sheet8) {
        if (bm_sheet8_unpack(&c, sheet8_set, NULL) != 0) return 1;
    } else if (c.sheet_rgba) {
        for (int y = 0; y < c.sheet_h; y++)
            for (int x = 0; x < c.sheet_w; x++) {
                const uint8_t *p = c.sheet_rgba + ((uint32_t)y * c.sheet_w + x) * 4;
                g16_sheet_set(&rt.sheet, x, y, g16_rgb(p[0], p[1], p[2]), p[3] >= 128);
            }
    }
    for (int cy = 0; cy < rt.sheet.h / G16_CELL; cy++)
        for (int cx = 0; cx < rt.sheet.w / G16_CELL; cx++)
            g16_sheet_update_cell(&rt.sheet, cx, cy);
    rt.map.w = c.map_cells ? c.map_w : 256;
    rt.map.h = c.map_cells ? c.map_h : 256;
    rt.map.cells = calloc((size_t)rt.map.w * rt.map.h, 2);
    if (c.map_cells)
        for (uint32_t i = 0; i < (uint32_t)rt.map.w * rt.map.h; i++)
            rt.map.cells[i] = (uint16_t)(c.map_cells[i * 2] | c.map_cells[i * 2 + 1] << 8);

    synth_init(&rt.synth, RATE);
    player_init(&rt.player, RATE, rt.regs, &rt.synth);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    for (const luaL_Reg *r = api; r->name; r++) {
        lua_pushcfunction(L, r->func);
        lua_setglobal(L, r->name);
    }
    lua_pushinteger(L, w); lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, h); lua_setglobal(L, "SCREEN_H");
    const char *waves[] = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL" };
    for (int i = 0; i < 6; i++) { lua_pushinteger(L, i); lua_setglobal(L, waves[i]); }
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, seed);
    lua_call(L, 1, 0);
    lua_pop(L, 1);

    lua_pushcfunction(L, traceback);
    if (luaL_loadbuffer(L, c.lua, c.lua_size, "=main.lua") != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
        fprintf(stderr, "bmplay: %s\n", lua_tostring(L, -1));
        return 1;
    }
    lua_pop(L, 1);
    if (call(L, "_init") != 0) return 1;
    if (bot && luaL_dofile(L, bot) != LUA_OK) {
        fprintf(stderr, "bmplay: %s\n", lua_tostring(L, -1));
        return 1;
    }

    FILE *vf = video ? (!strcmp(video, "-") ? stdout : fopen(video, "wb")) : NULL;
    FILE *af = audio ? fopen(audio, "wb") : NULL;
    if ((video && !vf) || (audio && !af)) { perror("bmplay"); return 1; }
    static int16_t pcm[RATE / FPS];
    for (rt.frame = 0; rt.frame < frames && !rt.quit; rt.frame++) {
        uint32_t held = 0;
        int stop = 0;
        if (bot) {
            if (lua_getglobal(L, "bot") != LUA_TFUNCTION) { fprintf(stderr, "bmplay: no bot()\n"); return 1; }
            lua_pushinteger(L, rt.frame);
            if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
                fprintf(stderr, "bmplay: bot: %s\n", lua_tostring(L, -1));
                return 1;
            }
            held = (uint32_t)lua_tointeger(L, -2);
            stop = lua_toboolean(L, -1);
            lua_pop(L, 2);
        }
        if (stop) break;
        rt.prev = rt.now;
        rt.now = (uint16_t)(held & 0x3FF);
        /* pad(): 1 left, 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y, 1024 L1, 2048 R1 */
        static const uint8_t to_pad[10] = { 0, 1, 2, 3, 4, 5, 8, 9, 6, 7 };
        rt.raw = held & 0xC00;
        for (int b = 0; b < 10; b++)
            if (held >> b & 1) rt.raw |= 1u << to_pad[b];
        if (call(L, "_update") != 0 || call(L, "_draw") != 0) return 1;
        for (int k = 0; k < RATE / FPS; k += BLOCK) {
            unsigned m = RATE / FPS - k < BLOCK ? (unsigned)(RATE / FPS - k) : BLOCK;
            player_advance(&rt.player, m);
            synth_render(&rt.synth, rt.regs, pcm + k, m);
        }
        if (af) fwrite(pcm, 2, RATE / FPS, af);
        if (vf && rt.frame % every == 0) write_frame(vf, scale);
    }
    fprintf(stderr, "bmplay: %ld frames\n", rt.frame);
    if (png) {
        FILE *f = fopen(png, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w * scale, h * scale);
            write_frame(f, scale);
            fclose(f);
        }
    }
    if (vf && vf != stdout) fclose(vf);
    if (af) fclose(af);
    lua_close(L);
    return 0;
}
