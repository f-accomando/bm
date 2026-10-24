/*
 * api_world.c: collision worlds (world3d.h), log() and report().
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

/* ---- collision worlds (world3d.h): boxes, rays, moving bodies */

static w3_world_t *check_world(lua_State *L, int i)
{
    return luaL_checkudata(L, i, WORLD_MT);
}

int l_world_gc(lua_State *L)
{
    w3_free(check_world(L, 1));
    return 0;
}

/* world3d() -> an empty collision world */
int l_world3d(lua_State *L)
{
    w3_world_t *w = lua_newuserdatauv(L, sizeof *w, 0);
    w3_init(w);
    luaL_setmetatable(L, WORLD_MT);
    return 1;
}

/* world_box(w, x0, y0, z0, x1, y1, z1, [tag]) -> its number: a solid box */
int l_world_box(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float lo[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, hi[3] = { fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0) };
    int i = w3_add_box(w, lo, hi, (int)luaL_optinteger(L, 8, 0));
    if (i < 0)
        return luaL_error(L, "not enough memory for the world");
    lua_pushinteger(L, i + 1);
    return 1;
}

/* world_ray(w, ox, oy, oz, dx, dy, dz, [maxd, ground]) -> t, nx, ny, nz, box
 * (0 = the ground y = 0, which counts unless ground is false), or nil */
int l_world_ray(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float o[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, d[3] = { fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 1) };
    float t, n[3];
    int ground = lua_isnoneornil(L, 9) || lua_toboolean(L, 9);
    int i = w3_ray(w, o, d, fnum(L, 8, 1000), ground, &t, n);
    if (i == -1) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, t);
    lua_pushnumber(L, n[0]);
    lua_pushnumber(L, n[1]);
    lua_pushnumber(L, n[2]);
    lua_pushinteger(L, i == -2 ? 0 : i + 1);
    return 5;
}

/* world_move(w, x, y, z, r, h, dx, dy, dz, [step, on_ground]) -> x, y, z, flags:
 * 1 on the ground, 2 a wall (4 along x, 8 along z), 16 a ceiling */
int l_world_move(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float p[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, d[3] = { fnum(L, 7, 0), fnum(L, 8, 0), fnum(L, 9, 0) };
    int f = w3_move(w, p, fnum(L, 5, 0.4f), fnum(L, 6, 1.8f), d, fnum(L, 10, 0.45f), lua_toboolean(L, 11));
    lua_pushnumber(L, p[0]);
    lua_pushnumber(L, p[1]);
    lua_pushnumber(L, p[2]);
    lua_pushinteger(L, f);
    return 4;
}

/* world_floor(w, x, z, y, r, [step]) -> the height of the floor under (x, z) */
int l_world_floor(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    lua_pushnumber(L, w3_floor(w, fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0), fnum(L, 5, 0.4f), fnum(L, 6, 0.45f)));
    return 1;
}

/* log(...) - text to the kernel log (serial + console), not the screen */
int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        kprintf("%s%s", i > 1 ? "\t" : "", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    kprintf("\n");
    return 0;
}

/* report(kind, text) - a report for whoever develops bm (src/kernel/reports.c,
 * 2026-10-04): saved in bm/reports on the SD card with the kernel, branch,
 * board and date, then sent to the reports' git repository if it can (a
 * benchmark's numbers instead of a photo). At most 8 a run, 256 KB each.
 * true if it was saved. */
/* The 3D's options in force, one line for the reports (the session's,
 * report()'s): the driver's (gpu3d_options_line), then the runtime's: who
 * draws the 3D (none: no 3D yet), each frame shown a frame later as it is,
 * the 2D in the GPU's job, the impostors' pixels, r3d_fast */
void options_line(char *b, size_t n)
{
    const int gpu = rt.r3d_ready && rt.r3d.backend != NULL;
    const size_t k = (size_t)gpu3d_options_line(b, n);
    if (k + 1 < n)
        ksnprintf(b + k, n - k, " 3d=%s later=%d 2d=%d impostor=%d fast=%d",
                  !rt.r3d_ready ? "none" : gpu ? "gpu" : "arm", gpu && rt.later && gpu3d_queue2(), gpu && rt.gpu2d,
                  rt.r3d_ready ? rt.r3d.impostor : 0, rt.r3d_ready ? rt.r3d.fast : 0);
}

#define REPORTS_MAX     8
#define REPORT_MAX_LEN  (256 * 1024)
int l_report(lua_State *L)
{
    const char *kind = luaL_checkstring(L, 1);
    size_t len;
    const char *text = luaL_checklstring(L, 2, &len);
    if (rt.reports >= REPORTS_MAX || len > REPORT_MAX_LEN) {
        kprintf("report: refused (%s)\n", len > REPORT_MAX_LEN ? "over 256 KB" : "8 in this run already");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!perm_allowed(PERM_REPORT)) {               /* the player said no */
        lua_pushboolean(L, 0);
        return 1;
    }
    rt.reports++;
    /* the 3D's options first (the Overbit bench's runs can be compared) */
    char *t = malloc(len + 256);
    if (!t) {
        lua_pushboolean(L, reports_text(kind, text, len) == 0);
        return 1;
    }
    ksnprintf(t, 256, "options ");
    options_line(t + 8, 240);
    size_t k = strlen(t);
    t[k++] = '\n';
    memcpy(t + k, text, len);
    lua_pushboolean(L, reports_text(kind, t, k + len) == 0);
    free(t);
    return 1;
}
