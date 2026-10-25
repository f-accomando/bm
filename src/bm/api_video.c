/*
 * api_video.c: video clips (.bmv): video_open and the clip object.
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

/* ---------------------------------------------------------------- video clips (.bmv) */

#define CLIP_DIR   "/bm/clips/"
#define REC_CLIP_DIR "/bm/rec/"
#define CLIP_MAX   (48u << 20)          /* a clip is read whole into memory: 48 MB at most */

typedef struct {
    bmv_t v;
    uint8_t *file;                      /* the clip as read from the card, or NULL once closed */
    uint16_t *px;                       /* the frame decoded last */
    int64_t shown;                      /* its number, or -1 */
} clip_t;

static clip_t *check_clip(lua_State *L, int i)
{
    clip_t *c = luaL_checkudata(L, i, VIDEO_MT);
    if (!c->file)
        luaL_error(L, "the clip is closed");
    return c;
}

static void clip_free(clip_t *c)
{
    free(c->file);
    free(c->px);
    c->file = NULL;
    c->px = NULL;
}

int l_clip_gc(lua_State *L)
{
    clip_free(luaL_checkudata(L, 1, VIDEO_MT));
    return 0;
}

/* video_open("intro.bmv") -> clip, or nil and a message. The file is in /bm/clips on the card
 * (made by scripts/mkvid.py), read whole into memory (48 MB at most). */
int l_video_open(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *dir = CLIP_DIR;
    char path[FAT_NAME_MAX + 16], err[80];
    if (!strncmp(name, "rec/", 4)) {      /* the recorder's chunks (bm/rec) play as clips too */
        dir = REC_CLIP_DIR;
        name += 4;
    }
    if (!*name || strchr(name, '/') || strchr(name, '\\') || strstr(name, "..") || strlen(name) >= FAT_NAME_MAX)
        return luaL_error(L, "video_open: a file name in %s (or rec/ and a name), no other path", CLIP_DIR);
    ksnprintf(path, sizeof path, "%s%s", dir, name);
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    const char *why = NULL;
    clip_t *c = lua_newuserdatauv(L, sizeof *c, 0);     /* first: a memory error then leaks nothing */
    memset(c, 0, sizeof *c);
    luaL_setmetatable(L, VIDEO_MT);
    if (fat_find(path, &e) != 0 || e.is_dir)
        why = "no such clip";
    else if (e.size > CLIP_MAX)
        why = "the clip is over 48 MB";
    else if (fat_load(&e, &data, &len) != 0)
        why = "cannot read the clip";
    if (why) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    if (bmv_open(&c->v, data, len, err, sizeof err) != 0) {
        free(data);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    c->file = data;
    c->px = malloc((size_t)c->v.width * (size_t)c->v.height * 2);
    c->shown = -1;
    if (!c->px) {
        clip_free(c);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "not enough memory for a frame");
        return 2;
    }
    return 1;
}

/* clip:info() -> width, height, fps, frames, milliseconds, has_audio */
int l_clip_info(lua_State *L)
{
    const clip_t *c = check_clip(L, 1);
    lua_pushinteger(L, c->v.width);
    lua_pushinteger(L, c->v.height);
    lua_pushinteger(L, c->v.fps);
    lua_pushinteger(L, c->v.frames);
    lua_pushinteger(L, bmv_duration_ms(&c->v));
    lua_pushboolean(L, c->v.audio_channels != 0);
    return 6;
}

/* clip:frame_at(ms) -> the frame (from 0) to show ms milliseconds after the start; the last one after the end */
int l_clip_frame_at(lua_State *L)
{
    const clip_t *c = check_clip(L, 1);
    const lua_Number ms = luaL_checknumber(L, 2);
    lua_pushinteger(L, !(ms > 0) ? 0 : bmv_frame_at(&c->v, ms > 2147483647.0 ? 0x7FFFFFFFu : (uint32_t)ms));
    return 1;
}

/* clip:draw(frame, x, y [, scale 1..8]) -> true, or false and a message. Frame 0 is the first. The
 * frame is decoded to RGB565 (kept: the same frame again costs only the copy) and copied, opaque,
 * with its top left corner at (x, y). */
int l_clip_draw(lua_State *L)
{
    clip_t *c = check_clip(L, 1);
    const lua_Integer f = luaL_checkinteger(L, 2);
    lua_Number fx = luaL_checknumber(L, 3), fy = luaL_checknumber(L, 4);
    fx = fx > -1e6 && fx < 1e6 ? fx : 1e6;      /* (NaN and the huge are far off the screen) */
    fy = fy > -1e6 && fy < 1e6 ? fy : 1e6;
    const int x = (int)fx, y = (int)fy, scale = (int)luaL_optinteger(L, 5, 1);
    if (f < 0 || f >= (lua_Integer)c->v.frames)
        return luaL_error(L, "clip:draw: frame %d of %d", (int)f, (int)c->v.frames);
    if (scale < 1 || scale > 8)
        return luaL_error(L, "clip:draw: scale 1..8");
    if (c->shown != f) {
        char err[80];
        c->shown = -1;
        if (bmv_decode(&c->v, (uint32_t)f, c->px, err, sizeof err) != 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, err);
            return 2;
        }
        c->shown = f;
    }
    if (rt.early)
        rt.early_touch = 1;
    sync3d();                           /* the 3D and the recorded 2D are on the page first */
    g16_blit565(&rt.g, c->px, c->v.width, c->v.height, x, y, scale);
    lua_pushboolean(L, 1);
    return 1;
}

int l_clip_close(lua_State *L)
{
    clip_free(luaL_checkudata(L, 1, VIDEO_MT));
    return 0;
}
