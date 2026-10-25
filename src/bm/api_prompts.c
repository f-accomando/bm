/*
 * api_prompts.c: prompts, buttons and keymap, controls, players, stat.
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
    /* the RGB30's own A B X Y on any console (what "A"... are on the RGB30) */
    { "RGB30_A", PROMPT_RGB30_A, PROMPT_RGB30_A }, { "RGB30_B", PROMPT_RGB30_B, PROMPT_RGB30_B },
    { "RGB30_X", PROMPT_RGB30_X, PROMPT_RGB30_X }, { "RGB30_Y", PROMPT_RGB30_Y, PROMPT_RGB30_Y },
};

/* the keyboard's keys with a name, lower case (the names of keyp()) */
static const struct { const char *name; uint8_t id; } key_prompts[] = {
    { "up", PROMPT_KEY_UP }, { "down", PROMPT_KEY_DOWN }, { "left", PROMPT_KEY_LEFT },
    { "right", PROMPT_KEY_RIGHT }, { "enter", PROMPT_KEY_ENTER }, { "esc", PROMPT_KEY_ESC },
    { "space", PROMPT_KEY_SPACE }, { "tab", PROMPT_KEY_TAB }, { "backspace", PROMPT_KEY_BACKSPACE },
    { "shift", PROMPT_KEY_SHIFT }, { "ctrl", PROMPT_KEY_CTRL }, { "alt", PROMPT_KEY_ALT },
    { "del", PROMPT_KEY_DEL }, { "home", PROMPT_KEY_HOME }, { "end", PROMPT_KEY_END },
    { "pgup", PROMPT_KEY_PGUP }, { "pgdn", PROMPT_KEY_PGDN },
    { "mouse", PROMPT_MOUSE }, { "mouse_l", PROMPT_MOUSE_L }, { "mouse_r", PROMPT_MOUSE_R },
};

/* the pad the prompts show: the one pressed last; until then a DS4, on
 * the RGB30 its own buttons (A B X Y, the letters in their colours) */
enum { PROMPTS_DS4, PROMPTS_LETTERED, PROMPTS_RGB30 };
#ifdef BM_RGB30
static int prompt_style = PROMPTS_RGB30;
#else
static int prompt_style = PROMPTS_DS4;
#endif

static int pad_prompt_id(size_t i, int style)
{
    return style == PROMPTS_DS4 ? pad_prompts[i].ds4
         : style == PROMPTS_LETTERED ? pad_prompts[i].pad : prompt_rgb30(pad_prompts[i].pad);
}

/* prompt()'s last argument: the menu's set (white keys, 16 px, raised on a
 * lip) instead of the apps' chips */
static int prompt_menu_set;

static const prompt_t *chip_of(int id, int small)
{
    return prompt_menu_set ? prompt_get(id, 0) : prompt_chip(id, small);
}

const prompt_t *find_prompt(const char *n, int small)
{
    int src = hid_last_source();
    if (src == HID_SOURCE_DS4 || src == HID_SOURCE_PAD)
        prompt_style = src == HID_SOURCE_PAD ? PROMPTS_LETTERED : PROMPTS_DS4;
    for (size_t i = 0; i < sizeof pad_prompts / sizeof *pad_prompts; i++)
        if (!strcmp(n, pad_prompts[i].name))
            return chip_of(pad_prompt_id(i, prompt_style), small);
    for (size_t i = 0; i < sizeof key_prompts / sizeof *key_prompts; i++)
        if (!strcmp(n, key_prompts[i].name))
            return chip_of(key_prompts[i].id, small);
    if (n[0] == 'f' && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12 && (n[2] == 0 || (k >= 10 && n[3] == 0)))
            return chip_of(PROMPT_KEY_F1 + k - 1, small);
    }
    if (n[0] && !n[1] && !(n[0] >= 'A' && n[0] <= 'Z'))
        return prompt_menu_set ? prompt_key((unsigned char)n[0]) : prompt_chip_key((unsigned char)n[0], small);
    return NULL;
}

/* a prompt's picture at x, y (its edges blended over the page), each of
 * its pixels scale x scale */
void draw_prompt(const prompt_t *p, int x, int y, int scale)
{
    for (int j = 0; j < p->h; j++)
        for (int i = 0; i < p->w; i++) {
            uint32_t c = p->px[j * p->w + i], a = c >> 24;
            if (!a)
                continue;
            const int px = x + i * scale, py = y + j * scale;
            if (a < 255) {                      /* the edges: over what is there */
                int b = g16_pget(&rt.g, px, py);
                if (b < 0)
                    continue;
                uint32_t under = g16_to_rgb24((uint16_t)b), out = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    int u = (int)(under >> sh & 255), v = (int)(c >> sh & 255);
                    out |= (uint32_t)(u + (v - u) * (int)a / 255) << sh;
                }
                c = out;
            }
            if (scale == 1)
                g16_pset(&rt.g, px, py, g16_rgb24(c & 0xFFFFFF));
            else
                g16_rectfill(&rt.g, px, py, scale, scale, g16_rgb24(c & 0xFFFFFF));
        }
}


/* prompt(name, x, y [, small, scale, player]): a button or a key as a chip
 * of the apps' set (prompts.c), its top left at (x, y), 16 px high for 8x16
 * text, 12 with small (by default when the font is 6x12), scale times
 * larger (1-8, as print's); returns the x after it. prompt(name [, small,
 * scale, player, menu]) only measures: the width and height. A last argument
 * `true` draws the menu's set instead: white keys 16 px high, on a lip.
 * Upper case the pad's buttons ("A", "B", "X", "Y", "START", "L1",
 * "UPDOWN"...), shown as on the pad pressed last: a DS4 (cross, circle...)
 * until another pad is used; on the RGB30 its own A B X Y (the letters in
 * their colours), "RGB30_A".."RGB30_Y" on any console. Lower case the keyboard's keys, with the
 * names of keyp() ("enter", "esc", "f1", "up") or one character ("s").
 * "ok", "back" and the actions of keymap() are their buttons. With player
 * (1-4) a button as that player's controller shows it: a DS4's symbol,
 * a pad's letter, or the keyboard's key that presses it. */
int l_prompt(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int measure = !lua_isnumber(L, 2), at = measure ? 2 : 4;
    int small = lua_isnoneornil(L, at) ? rt.g.font->height <= 12 : lua_toboolean(L, at);
    int scale = (int)luaL_optinteger(L, at + 1, 1);
    scale = scale < 1 ? 1 : scale > 8 ? 8 : scale;
    int player = (int)luaL_optinteger(L, at + 2, 0);
    prompt_menu_set = lua_toboolean(L, at + 3);
    const prompt_t *p = prompt_for(n, small, player);
    prompt_menu_set = 0;
    if (!p)
        return luaL_argerror(L, 1, "not a button or a key");
    if (measure) {
        lua_pushinteger(L, p->w * scale);
        lua_pushinteger(L, p->h * scale);
        return 2;
    }
    int x = ival(L, 2), y = ival(L, 3);
    int32_t *a;
    if (draw2d(D2_PROMPT, 3 + sizeof(void *) / 4, &a)) {
        a[0] = x;
        a[1] = y;
        a[2] = scale;
        memcpy(a + 3, &p, sizeof p);
    } else {
        draw_prompt(p, x, y, scale);
    }
    lua_pushinteger(L, x + p->w * scale);
    return 1;
}

/* lastinput(): what pressed something last, "keyboard", "ds4" or "pad"
 * (another controller); nil before anything is pressed */
int l_lastinput(lua_State *L)
{
    int s = hid_last_source();
    if (s == HID_SOURCE_NONE)
        lua_pushnil(L);
    else
        lua_pushstring(L, s == HID_SOURCE_KEYBOARD ? "keyboard" : s == HID_SOURCE_DS4 ? "ds4" : "pad");
    return 1;
}

int l_camera(lua_State *L) { g16_camera(&rt.g, oval(L, 1, 0), oval(L, 2, 0)); return 0; }
int l_clip(lua_State *L)   { g16_clip(&rt.g, oval(L, 1, 0), oval(L, 2, 0), oval(L, 3, 0), oval(L, 4, 0)); return 0; }

int l_rgb(lua_State *L)
{
    lua_Integer r = luaL_checkinteger(L, 1) & 255, g = luaL_checkinteger(L, 2) & 255, b = luaL_checkinteger(L, 3) & 255;
    lua_pushinteger(L, r << 16 | g << 8 | b);
    return 1;
}

/* The buttons by name (btn("jump"), keymap(), prompt("ok"), 2026-10-04):
 * the pad's, how each shows on a pad (prompt()'s upper-case names) and the
 * keyboard's key that presses it in a game (hid.c's key_button) */
static const struct { const char *name; uint32_t hid; const char *chip, *key; } button_names[] = {
    { "left", HID_LEFT, "LEFT", "left" }, { "right", HID_RIGHT, "RIGHT", "right" },
    { "up", HID_UP, "UP", "up" }, { "down", HID_DOWN, "DOWN", "down" },
    { "a", HID_A, "A", "space" }, { "b", HID_B, "B", "x" }, { "x", HID_X, "X", "c" }, { "y", HID_Y, "Y", "v" },
    { "start", HID_START, "START", "enter" }, { "select", HID_SELECT, "SELECT", "tab" },
    { "l1", HID_L1, "L1", "q" }, { "r1", HID_R1, "R1", "e" },
    { "l2", HID_L2, "L2", NULL }, { "r2", HID_R2, "R2", NULL },
    { "l3", HID_L3, "L3", NULL }, { "r3", HID_R3, "R3", NULL },
};
#define NBUTTONS ((int)(sizeof button_names / sizeof button_names[0]))
_Static_assert(NBUTTONS == BUTTON_OK, "BUTTON_OK (rt_internal.h) follows button_names");

static int name_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a | 0x20) != (*b | 0x20))
            return 0;
    return *a == *b;
}

/* a button's index (BUTTON_OK, BUTTON_BACK too), or -1 */
static int button_index(const char *n)
{
    for (int i = 0; i < NBUTTONS; i++)
        if (name_is(n, button_names[i].name))
            return i;
    if (name_is(n, "ok"))
        return BUTTON_OK;
    if (name_is(n, "back"))
        return BUTTON_BACK;
    return -1;
}

/* the index of a button for the game: "ok" and "back" are A or B */
int button_real(int i)
{
    if (i < NBUTTONS)
        return i;
    const uint32_t bit = input_ok_bit(i == BUTTON_BACK);
    return bit == HID_A ? 4 : 5;                /* "a", "b" in button_names */
}

/* the HID bits of a name: a button, "ok" / "back", or an action of
 * keymap(); 0 if it is none of them */
static uint32_t name_mask(const char *n)
{
    int i = button_index(n);
    if (i >= 0)
        return button_names[button_real(i)].hid;
    for (int k = 0; k < rt.nkeymap; k++)
        if (!strcmp(rt.keymap[k].name, n)) {
            uint32_t m = 0;
            for (int j = 0; j < rt.keymap[k].n; j++)
                m |= button_names[button_real(rt.keymap[k].btn[j])].hid;
            return m;
        }
    return 0;
}

/* the first button of a name (for its picture), or -1 */
static int name_button(const char *n)
{
    int i = button_index(n);
    if (i >= 0)
        return button_real(i);
    for (int k = 0; k < rt.nkeymap; k++)
        if (!strcmp(rt.keymap[k].name, n) && rt.keymap[k].n)
            return button_real(rt.keymap[k].btn[0]);
    return -1;
}

/* btn(name [, p]), btnp(name [, p]) with a name: the HID bits of the
 * players (any of them without p) */
static int named_button(lua_State *L, int pressed)
{
    const char *n = luaL_checkstring(L, 1);
    uint32_t mask = name_mask(n);
    if (!mask)
        return luaL_argerror(L, 1, "not a button, \"ok\", \"back\" or an action of keymap()");
    if (mask & (HID_X | HID_Y))
        rt.uses_xy = 1;                         /* it asks for X or Y: they are its own */
    uint32_t now = 0, prev = 0;
    if (lua_isnoneornil(L, 2)) {
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            now |= rt.praw[p];
            prev |= rt.praw_prev[p];
        }
    } else {
        int p = ival(L, 2);
        if (p >= 1 && p <= INPUT_PLAYERS) {
            now = rt.praw[p - 1];
            prev = rt.praw_prev[p - 1];
        }
    }
    lua_pushboolean(L, pressed ? (now & ~prev & mask) != 0 : (now & mask) != 0);
    return 1;
}

/* keymap({action = "a" or {"a", "x"}, ...}): the game's actions on the
 * buttons (and "ok" / "back"), read with btn("action"), btnp() and drawn
 * with prompt("action"); the game remaps by calling it again (its options
 * menu, saved with save()). keymap() gives the table back, keymap(nil)
 * forgets it. */
/* The player's remap (Settings > Controls, kernel/controls.c): an action of
 * the game's scheme (or the pause) the player set to a button takes that
 * button instead of the game's own; the others keep the game's. Run again
 * when keymap() or controls() changes something and when the game resumes. */
void keymap_apply_controls(void)
{
    const char *scheme = rt.scheme[0] ? rt.scheme : "scroller";
    for (int k = 0; k < rt.nkeymap; k++) {
        memcpy(rt.keymap[k].btn, rt.keymap[k].gbtn, sizeof rt.keymap[k].btn);
        rt.keymap[k].n = rt.keymap[k].gn;
        const char *b = !strcmp(rt.keymap[k].name, "pause") ? controls_lookup("system", "pause")
                                                            : controls_lookup(scheme, rt.keymap[k].name);
        if (!b)
            continue;
        const char *name = !strcmp(b, "st") ? "start" : !strcmp(b, "se") ? "select" : b;
        const int i = button_index(name);
        if (i < 0)
            continue;
        if (button_real(i) == 6 || button_real(i) == 7)
            rt.uses_xy = 1;
        rt.keymap[k].btn[0] = (uint8_t)i;
        rt.keymap[k].n = 1;
    }
}

/* controls(): the player's settings as a table { scheme = "scroller", c1 = "default" | "<button>",
 * ... } for the codes of kernel/controls.h; controls(scheme) sets the scheme
 * of the running game ("scroller", "topdown" or "stick") and applies the
 * player's remap to its keymap. */
int l_controls(lua_State *L)
{
    if (lua_gettop(L) >= 1) {
        const char *s = luaL_checkstring(L, 1);
        if (strcmp(s, "scroller") && strcmp(s, "topdown") && strcmp(s, "stick"))
            return luaL_error(L, "controls: the scheme is \"scroller\", \"topdown\" or \"stick\"");
        ksnprintf(rt.scheme, sizeof rt.scheme, "%s", s);
        keymap_apply_controls();
        return 0;
    }
    lua_createtable(L, 0, CONTROLS_COUNT + 1);
    lua_pushstring(L, rt.scheme[0] ? rt.scheme : "scroller");
    lua_setfield(L, -2, "scheme");
    for (int i = 0; i < controls_count(); i++) {
        const char *b = controls_get(i);
        lua_pushstring(L, b ? b : "default");
        lua_setfield(L, -2, controls_code(i));
    }
    return 1;
}

int l_keymap(lua_State *L)
{
    if (lua_isnone(L, 1)) {
        lua_createtable(L, 0, rt.nkeymap);
        for (int k = 0; k < rt.nkeymap; k++) {
            lua_createtable(L, rt.keymap[k].n, 0);
            for (int j = 0; j < rt.keymap[k].n; j++) {
                int b = rt.keymap[k].btn[j];
                lua_pushstring(L, b == BUTTON_OK ? "ok" : b == BUTTON_BACK ? "back" : button_names[b].name);
                lua_rawseti(L, -2, j + 1);
            }
            lua_setfield(L, -2, rt.keymap[k].name);
        }
        return 1;
    }
    rt.nkeymap = 0;
    if (lua_isnil(L, 1))
        return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            return luaL_error(L, "keymap: the actions are names (strings)");
        const char *action = lua_tostring(L, -2);
        if (rt.nkeymap >= (int)(sizeof rt.keymap / sizeof rt.keymap[0]))
            return luaL_error(L, "keymap: at most %d actions", (int)(sizeof rt.keymap / sizeof rt.keymap[0]));
        if (button_index(action) >= 0)
            return luaL_error(L, "keymap: \"%s\" is a button's name, not an action's", action);
        __typeof__(rt.keymap[0]) *k = &rt.keymap[rt.nkeymap];
        memset(k, 0, sizeof *k);
        ksnprintf(k->name, sizeof k->name, "%s", action);
        int one = lua_type(L, -1) == LUA_TSTRING, count = one ? 1 : (int)luaL_len(L, -1);
        if (!one)
            luaL_checktype(L, -1, LUA_TTABLE);
        for (int j = 0; j < count && k->n < 4; j++) {
            if (!one)
                lua_rawgeti(L, -1, j + 1);
            const char *b = lua_tostring(L, -1);
            int i = b ? button_index(b) : -1;
            if (i < 0)
                return luaL_error(L, "keymap: \"%s\": no button \"%s\"", action, b ? b : "?");
            if (button_real(i) == 6 || button_real(i) == 7)
                rt.uses_xy = 1;                 /* "x", "y" */
            k->btn[k->n++] = (uint8_t)i;
            if (!one)
                lua_pop(L, 1);
        }
        memcpy(k->gbtn, k->btn, sizeof k->gbtn);
        k->gn = k->n;
        rt.nkeymap++;
        lua_pop(L, 1);
    }
    keymap_apply_controls();
    return 0;
}

/* the players' colours: those of the pads' light bars (bt.c), bright */
static const uint32_t player_rgb[INPUT_PLAYERS] = { 0x3070FF, 0xFF3C28, 0x28D848, 0xFF38A8 };

/* controller([p]): what player p (1-4, the first by default) plays with:
 * {kind = "keyboard" | "ds4" | "xbox" | "pad" | "builtin" | "none",
 *  layout = "keyboard" | "ds4" | "xbox" | "nintendo" | "none",
 *  bluetooth = bool, ok = "a" | "b", back = "b" | "a" (the game's
 * buttons that say yes and go back, as the system's menus), color = the
 * player's colour (the light bar's: 1 blue, 2 red, 3 green, 4 pink)} */
int l_controller(lua_State *L)
{
    int p = (int)luaL_optinteger(L, 1, 1);
    int d = p >= 1 && p <= INPUT_PLAYERS ? input_device(p - 1) : INPUT_DEV_NONE;
    int kind = d & INPUT_DEV_KIND;
    const char *k = "none", *layout = "none";
    if (kind == INPUT_DEV_KEYBOARD) {
        k = layout = "keyboard";
    } else if (kind == INPUT_DEV_PAD) {
        k = d & INPUT_DEV_DS4 ? "ds4" : d & INPUT_DEV_XBOX ? "xbox" : d & INPUT_DEV_BUILTIN ? "builtin" : "pad";
        layout = d & INPUT_DEV_DS4 ? "ds4" : d & INPUT_DEV_BUILTIN ? "nintendo" : "xbox";
    }
    lua_createtable(L, 0, 6);
    lua_pushstring(L, k);
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, layout);
    lua_setfield(L, -2, "layout");
    lua_pushboolean(L, (d & INPUT_DEV_BLUETOOTH) != 0);
    lua_setfield(L, -2, "bluetooth");
    lua_pushstring(L, input_ok_bit(0) == HID_A ? "a" : "b");
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, input_ok_bit(1) == HID_A ? "a" : "b");
    lua_setfield(L, -2, "back");
    lua_pushinteger(L, player_rgb[(p >= 1 && p <= INPUT_PLAYERS ? p : 1) - 1]);
    lua_setfield(L, -2, "color");
    return 1;
}

/* a pad's button as a chip: the DS4's symbol or the letter (find_prompt
 * chooses by the pad pressed last; here the caller does) */
static const prompt_t *pad_chip(const char *chip, int style, int small)
{
    for (size_t i = 0; i < sizeof pad_prompts / sizeof *pad_prompts; i++)
        if (!strcmp(chip, pad_prompts[i].name))
            return chip_of(pad_prompt_id(i, style), small);
    return NULL;
}

/* the chip of button b (button_names) on a device: INPUT_DEV_* flags, or
 * -1 for the one pressed last */
const prompt_t *button_chip(int b, int dev, int small)
{
#ifdef BM_RGB30
    /* nothing pressed yet: the console's own buttons, not the keyboard */
    int kb = dev < 0 ? hid_last_source() == HID_SOURCE_KEYBOARD : (dev & INPUT_DEV_KIND) == INPUT_DEV_KEYBOARD;
#else
    int kb = dev < 0 ? hid_last_source() == HID_SOURCE_KEYBOARD || hid_last_source() == HID_SOURCE_NONE
                     : (dev & INPUT_DEV_KIND) == INPUT_DEV_KEYBOARD;
#endif
    if (kb && button_names[b].key)
        return find_prompt(button_names[b].key, small);
    /* a pad with letters shows the letter the game's button is under */
    uint32_t shown = input_face_shown(button_names[b].hid);
    for (int i = 0; i < NBUTTONS; i++)
        if (button_names[i].hid == shown)
            b = i;
    /* the RGB30's own controls (a Bluetooth pad may be pressing them):
     * as the pad pressed last, its own A B X Y until another one is used */
    if (dev < 0 || (dev & INPUT_DEV_KIND) != INPUT_DEV_PAD || (dev & INPUT_DEV_BUILTIN))
        return find_prompt(button_names[b].chip, small);
    return pad_chip(button_names[b].chip, dev & INPUT_DEV_DS4 ? PROMPTS_DS4 : PROMPTS_LETTERED, small);
}

const prompt_t *prompt_for(const char *n, int small, int player)
{
    int b = -1;
    if (name_is(n, "ok") || name_is(n, "back")) {
        b = name_button(n);
    } else {
        for (int k = 0; k < rt.nkeymap && b < 0; k++)
            if (!strcmp(rt.keymap[k].name, n))
                b = name_button(n);
        /* a pad's button by its upper-case name, for a player */
        for (int i = 0; b < 0 && player && i < NBUTTONS; i++)
            if (!strcmp(n, button_names[i].chip))
                b = i;
    }
    if (b < 0)
        return find_prompt(n, small);
    int dev = player >= 1 && player <= INPUT_PLAYERS ? input_device(player - 1) : -1;
    return button_chip(b, dev, small);
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

int l_btn(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING)
        return named_button(L, 0);
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1));
    return 1;
}

int l_btnp(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING)
        return named_button(L, 1);
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1) && !(prev >> b & 1));
    return 1;
}

/* players() -> how many players have a controller, and which (bit n =
 * player n+1) */
int l_players(lua_State *L)
{
    unsigned m = input_connected(), n = 0;
    for (int p = 0; p < INPUT_PLAYERS; p++)
        n += m >> p & 1;
    lua_pushinteger(L, n ? n : 1);
    lua_pushinteger(L, m ? m : 1);
    return 2;
}

static void stick_of(int p, int right, float *x, float *y)
{
    if (right)
        input_stick_r(p, x, y);
    else
        input_stick(p, rt.praw[p], x, y);
}

/* stick([p, [n]]) -> x, y in -1..1 (x right, y down): the left stick of
 * player p, or the cross; with n = 1 the right stick (0, 0 without one);
 * without p the one pushed furthest */
int l_stick(lua_State *L)
{
    float x = 0, y = 0;
    const int right = (int)luaL_optinteger(L, 2, 0) == 1;
    if (rt.leave_ask) {
        /* the leave question is open (online()): none of the controls */
    } else if (lua_isnoneornil(L, 1)) {
        float best = -1;
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            float px, py;
            stick_of(p, right, &px, &py);
            if (px * px + py * py > best) {
                best = px * px + py * py;
                x = px;
                y = py;
            }
        }
    } else {
        int p = ival(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            stick_of(p - 1, right, &x, &y);
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

int l_time(lua_State *L)
{
    lua_pushnumber(L, (timer_ticks() - rt.start_us) / 1e6);
    return 1;
}


/* stat(n): 0 Lua KB, 1 last frame CPU ms (update+draw), 2 fps, 3 frame number,
 *         4 3D triangles drawn and 5 3D pixels written since the last zclear()
 *         (0 with the GPU), 6 ms spent in 3D drawing (draw3d and the effects;
 *         with the GPU, the ARM's part) since then, 7 3D vertices transformed
 *         since then, 8 ms since this frame began, 9 1 if the GPU draws the 3D,
 *         10 Lua instructions of the last frame (update+draw, to the thousand);
 *         the dev kit: 11 tokens of the cartridge's code (tokens.h), 12 the
 *         most Lua KB of this run, 13 KB of the cartridge's data in memory
 *         (sprite sheet, map, models, sound bank, z-buffer), 14 the Lua
 *         instructions of the busiest frame of this run, 15 the _update
 *         run this frame (frameskip), 16 ms of GPU job time (bin + render)
 *         accounted in the last frame, 0 when the ARM draws the 3D,
 *         17 ms of audio processing in the last frame, 18 ms the ARM
 *         waited for the GPU in the last frame (0 when the ARM draws the
 *         3D; 0 on the RGB30 and in QEMU, where no such source exists):
 *         measurements only, never game logic (they differ between consoles) */
int l_stat(lua_State *L)
{
    if (rt.r3d_ready)
        r3d_flush(&rt.r3d);             /* (the draws held for merging are counted) */
    switch (ival(L, 1)) {
    case 0: lua_pushinteger(L, (lua_Integer)(luavm_mem() / 1024)); break;
    case 1: lua_pushnumber(L, rt.last_cpu_us / 1000.0); break;
    case 2: lua_pushinteger(L, rt.fps); break;
    case 3: lua_pushinteger(L, rt.frame); break;
    case 4: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.tris_drawn : 0); break;
    case 5: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.pixels : 0); break;
    case 6: lua_pushnumber(L, rt.us3d / 1000.0); break;
    case 7: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.verts : 0); break;
    case 8: lua_pushnumber(L, (timer_ticks() - rt.frame_t0) / 1000.0); break;
    case 9: lua_pushinteger(L, rt.r3d_ready && rt.r3d.backend); break;
    case 10: lua_pushinteger(L, (lua_Integer)rt.last_instr_k * 1000); break;
    case 11: lua_pushinteger(L, rt.tokens); break;
    case 12: {
        size_t now = luavm_mem();
        lua_pushinteger(L, (lua_Integer)((now > rt.lua_peak ? now : rt.lua_peak) / 1024));
        break;
    }
    case 13: lua_pushinteger(L, (lua_Integer)assets_kb()); break;
    case 14: lua_pushinteger(L, (lua_Integer)rt.instr_k_max * 1000); break;
    case 15: lua_pushinteger(L, rt.updates); break;
    case 16: lua_pushnumber(L, rt.gpu_frame_us / 1000.0); break;
    case 17: lua_pushnumber(L, rt.audio_frame_us / 1000.0); break;
    case 18: lua_pushnumber(L, rt.gpuwait_frame_us / 1000.0); break;
    default: lua_pushnil(L);
    }
    return 1;
}

/* frameskip([n]) -> the old n: the game's time at 60 _update a second
 * whatever its _draw costs. When a frame takes longer than 1/60 s, up to n
 * _update run before the next _draw (the frames not drawn are skipped), so
 * a game that moves 1/60 s per _update does not slow down; 1 (the default)
 * is one _update a frame, as before. stat(15): how many ran this frame. */
int l_frameskip(lua_State *L)
{
    const int old = rt.skip_max > 1 ? rt.skip_max : 1;
    if (!lua_isnoneornil(L, 1)) {
        int n = ival(L, 1);
        rt.skip_max = n < 1 ? 1 : n > 8 ? 8 : n;
    }
    lua_pushinteger(L, old);
    return 1;
}

/* tri(x0, y0, x1, y1, x2, y2, c [, c1, c2]): with three colours, one per
 * corner, blended across the triangle (Gouraud, dithered) */
int l_tri(lua_State *L)
{
    int32_t v[9], *a;
    for (int i = 0; i < 6; i++)
        v[i] = ival(L, i + 1);
    if (!lua_isnoneornil(L, 8)) {
        const uint32_t c0 = (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF);
        v[6] = (int32_t)c0;
        v[7] = (int32_t)(uint32_t)luaL_checkinteger(L, 8);
        v[8] = (int32_t)(uint32_t)luaL_optinteger(L, 9, (lua_Integer)c0);
        if (draw2d(D2_TRI_GOURAUD, 9, &a))
            memcpy(a, v, sizeof v);
        else
            g16_tri_gouraud(&rt.g, v[0], v[1], v[2], v[3], v[4], v[5], (uint32_t)v[6], (uint32_t)v[7],
                            (uint32_t)v[8]);
        return 0;
    }
    v[6] = col(L, 7, 0xFFFFFF);
    if (draw2d(D2_TRI, 7, &a))
        memcpy(a, v, 7 * sizeof *v);
    else
        g16_tri(&rt.g, v[0], v[1], v[2], v[3], v[4], v[5], (uint16_t)v[6]);
    return 0;
}
