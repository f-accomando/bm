/*
 * api_save.c: save() and the Lua-source saves in /bm/save.
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

/* ---- save() / saved(): tables of a cartridge in /bm/save, as Lua source
 * ("return {...}") read back in an empty environment: data only. Up to
 * SAVE_SLOTS slots (R12): slot 1 is the file of before ("1a2b3c4d.sav"),
 * the others "1a2b3c4d.s02" ... ".s08". */

void bm_save_path(const char *title, const char *author, char *out, size_t n)
{
    char id[96];
    int len = ksnprintf(id, sizeof id, "%s\n%s", title, author);
    ksnprintf(out, n, "%s/%08lx.sav", SAVE_DIR, crc32(id, (uint32_t)len));
}

void bm_save_slot(const char *path, int slot, char *out, size_t n)
{
    ksnprintf(out, n, "%s", path);
    char *dot = strrchr(out, '.');
    if (slot > 1 && dot && (size_t)(dot - out) + 5 <= n - 1)
        ksnprintf(dot, 5, ".s%02d", slot);
}

/* The text is built in a fixed buffer: a luaL_Buffer cannot be used here,
 * it may sit on the Lua stack while the serializer walks the tables with
 * lua_next (a save of more than about 1 KB then broke the stack). */
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

void ser(lua_State *L, int idx, int depth)
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

/* the slot argument at idx (1 when absent) */
int save_slot(lua_State *L, int idx)
{
    lua_Integer slot = luaL_optinteger(L, idx, 1);
    luaL_argcheck(L, slot >= 1 && slot <= SAVE_SLOTS, idx, "a save slot is 1 to 8");
    return (int)slot;
}

/* the file of a slot in SAVE_DIR: "1a2b3c4d.sav", "1a2b3c4d.s02"... */
void slot_name(int slot, char out[16])
{
    bm_save_slot(rt.save_name, slot, out, 16);
}

/* the slot's file on the card (in /bm, or the folder of before the rename) */
int slot_find(int slot, fat_entry_t *e)
{
    char name[16], path[40];
    slot_name(slot, name);
    ksnprintf(path, sizeof path, "save/%s", name);
    return config_find_file(path, e);
}

/* save(t, [slot]): true, or false and a message (no SD card, card full...) */
int l_save(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int slot = save_slot(L, 2);
    lua_settop(L, 1);
    sb.n = 0;
    sb_str(L, "return ");
    ser(L, 1, 0);
    char name[16];
    slot_name(slot, name);
    rec_note_game_io();                 /* the recorder leaves the card to the save */
    if (sd_write_file("save", SAVE_DIR, name, sb.p, sb.n) != 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
