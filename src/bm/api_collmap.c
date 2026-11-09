/* A collision map for games seen from above (EXPERIMENT, claude/bm-vm-test): the generic form of typed_ysync and
 * typed_yblocked. The game gives the geometry once; the chunks are copied in as they are ready. The function that tests a
 * body is made by scripts/bmt.py from tests/typed/collmap.bmt (src/bm/typed_collmap_gen.h: do not edit).
 *
 *   cm = collmap_new{ map = {w, h}, chunk = tiles in a side, tile = pixels, cols = obstacles in a chunk (at most),
 *                     foot = {x0, y0, x1, y1}, margin = pixels, pad = {px, py} }
 *   ok = collmap_chunk(cm, cx, cy, solid, cols)  solid: a string, one byte (0 or 1) for each tile, row by row;
 *                                                cols: a flat table x, y, rx, ry, off for each obstacle (off 0 or 1)
 *   r  = collmap_blocked(cm, x, y)               true, false, or nil when a chunk it needs is not copied in
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "typed_collmap_gen.h"

#define CM_MT "bm.collmap"

typedef struct {
    int tile, ctiles, mw, mh, maxcols;
    double fx0, fy0, fx1, fy1, margin, padx, pady;
    int32_t *solid, *ready, *ccount, *coloff;
    double *colx, *coly, *colrx, *colry;
} collmap_t;

static void cm_free(collmap_t *c)
{
    free(c->solid);
    free(c->ready);
    free(c->ccount);
    free(c->coloff);
    free(c->colx);
    free(c->coly);
    free(c->colrx);
    free(c->colry);
    memset(c, 0, sizeof *c);
}

static int cm_gc(lua_State *L)
{
    cm_free((collmap_t *)luaL_checkudata(L, 1, CM_MT));
    return 0;
}

static double field_num(lua_State *L, int idx, const char *name, int sub, double def)
{
    lua_getfield(L, idx, name);
    if (sub > 0 && lua_istable(L, -1)) {
        lua_rawgeti(L, -1, sub);
        lua_remove(L, -2);
    }
    const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

int l_collmap_new(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    collmap_t t;
    memset(&t, 0, sizeof t);
    t.mw = (int)field_num(L, 1, "map", 1, 0);
    t.mh = (int)field_num(L, 1, "map", 2, 0);
    t.ctiles = (int)field_num(L, 1, "chunk", 0, 0);
    t.tile = (int)field_num(L, 1, "tile", 0, 0);
    t.maxcols = (int)field_num(L, 1, "cols", 0, 0);
    t.fx0 = field_num(L, 1, "foot", 1, 0);
    t.fy0 = field_num(L, 1, "foot", 2, 0);
    t.fx1 = field_num(L, 1, "foot", 3, 0);
    t.fy1 = field_num(L, 1, "foot", 4, 0);
    t.margin = field_num(L, 1, "margin", 0, 0);
    t.padx = field_num(L, 1, "pad", 1, 0);
    t.pady = field_num(L, 1, "pad", 2, 0);
    if (t.mw < 1 || t.mh < 1 || t.mw > 64 || t.mh > 64 || t.ctiles < 1 || t.ctiles > 64 || t.tile < 1 || t.tile > 256 ||
        t.maxcols < 0 || t.maxcols > 512)
        return luaL_error(L, "collmap_new: map 1..64 x 1..64 chunks, chunk 1..64 tiles, tile 1..256 pixels, cols 0..512");
    const size_t nch = (size_t)t.mw * t.mh;
    t.solid = calloc(nch * t.ctiles * t.ctiles, 4);
    t.ready = calloc(nch, 4);
    t.ccount = calloc(nch, 4);
    t.coloff = calloc(nch * (t.maxcols + 1), 4);
    t.colx = calloc(nch * (t.maxcols + 1), 8);
    t.coly = calloc(nch * (t.maxcols + 1), 8);
    t.colrx = calloc(nch * (t.maxcols + 1), 8);
    t.colry = calloc(nch * (t.maxcols + 1), 8);
    collmap_t *u = (collmap_t *)lua_newuserdatauv(L, sizeof *u, 0);
    *u = t;
    if (luaL_newmetatable(L, CM_MT)) {
        lua_pushcfunction(L, cm_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    if (!t.solid || !t.ready || !t.ccount || !t.coloff || !t.colx || !t.coly || !t.colrx || !t.colry) {
        cm_free(u);
        return luaL_error(L, "collmap_new: out of memory");
    }
    return 1;
}

int l_collmap_chunk(lua_State *L)
{
    collmap_t *c = (collmap_t *)luaL_checkudata(L, 1, CM_MT);
    const int cx = (int)luaL_checkinteger(L, 2), cy = (int)luaL_checkinteger(L, 3);
    size_t n;
    const char *s = luaL_checklstring(L, 4, &n);
    luaL_checktype(L, 5, LUA_TTABLE);
    const size_t tiles = (size_t)c->ctiles * c->ctiles;
    const int nc = (int)lua_rawlen(L, 5) / 5;
    if (cx < 0 || cy < 0 || cx >= c->mw || cy >= c->mh || n != tiles || nc > c->maxcols) {
        if (cx >= 0 && cy >= 0 && cx < c->mw && cy < c->mh)
            c->ready[cy * c->mw + cx] = 0;      /* it does not fit: the game runs its Lua for it */
        lua_pushboolean(L, 0);
        return 1;
    }
    const int ci = cy * c->mw + cx;
    for (size_t i = 0; i < tiles; i++)
        c->solid[ci * tiles + i] = s[i] != 0;
    for (int i = 0; i < nc; i++) {
        double v[5];
        for (int k = 0; k < 5; k++) {
            lua_rawgeti(L, 5, i * 5 + k + 1);
            v[k] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        const size_t slot = (size_t)ci * c->maxcols + i;
        c->colx[slot] = v[0]; c->coly[slot] = v[1]; c->colrx[slot] = v[2]; c->colry[slot] = v[3];
        c->coloff[slot] = v[4] != 0;
    }
    c->ccount[ci] = nc;
    c->ready[ci] = 1;
    lua_pushboolean(L, 1);
    return 1;
}

int l_collmap_blocked(lua_State *L)
{
    collmap_t *c = (collmap_t *)luaL_checkudata(L, 1, CM_MT);
    const int r = bmt_cm_blocked(luaL_checknumber(L, 2), luaL_checknumber(L, 3), c->solid, c->ready, c->colx, c->coly,
                                 c->colrx, c->colry, c->coloff, c->ccount, c->tile, c->ctiles, c->mw, c->mh, c->maxcols,
                                 c->fx0, c->fy0, c->fx1, c->fy1, c->margin, c->padx, c->pady);
    if (r < 0)
        lua_pushnil(L);
    else
        lua_pushboolean(L, r);
    return 1;
}
