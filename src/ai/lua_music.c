/*
 * ai.music (src/ai/music.h) for Lua: music for bm Sound and the games, on
 * the Pi and the RGB30. The recipe comes from opts.gen, a recipe's id, the
 * assistant's network when there is one (ai_music_pick, set by lua_ai.c)
 * or the words (mus_guess). The table it gives: lua_ai.c's comment.
 */
#include "lua_music.h"
#include "music.h"

#include <string.h>

#include "lauxlib.h"

const char *(*ai_music_pick)(lua_State *L, const char *request);

static void field_str(lua_State *L, const char *k, const char *v)
{
    lua_pushstring(L, v);
    lua_setfield(L, -2, k);
}

static uint32_t step_word(const mus_step_t *s)
{
    return (uint32_t)s->note | (uint32_t)s->inst << 8 | (uint32_t)s->vol << 16 | (uint32_t)s->fx << 24;
}

static int opt_int(lua_State *L, int t, const char *k, int def)
{
    lua_getfield(L, t, k);
    int v = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

int ai_lua_music(lua_State *L)
{
    const char *q = luaL_optstring(L, 1, "");
    int has_opts = lua_istable(L, 2);
    const char *gen = NULL;
    if (has_opts) {
        lua_getfield(L, 2, "gen");
        const char *g = lua_tostring(L, -1);
        if (g && mus_find(g) >= 0)
            gen = mus_recipe_id(mus_find(g));
        lua_pop(L, 1);
    }
    if (!gen && mus_find(q) >= 0)
        gen = mus_recipe_id(mus_find(q));
    if (!gen && *q && ai_music_pick)
        gen = ai_music_pick(L, q);
    if (!gen && *q)
        gen = mus_guess(q);
    if (!gen) {
        lua_pushnil(L);
        lua_pushstring(L, "no music recipe for that");
        return 2;
    }
    static mus_req_t r;
    static uint8_t ctx[512], ctx_bar[512];
    mus_req_init(&r, gen);
    mus_parse(q, &r);
    if (has_opts) {
        r.seed = (uint32_t)opt_int(L, 2, "seed", 1);
        r.key = opt_int(L, 2, "key", r.key);
        lua_getfield(L, 2, "minor");
        if (!lua_isnil(L, -1))
            r.minor = lua_toboolean(L, -1);
        lua_pop(L, 1);
        r.bpm = opt_int(L, 2, "bpm", r.bpm);
        r.bars = opt_int(L, 2, "bars", r.bars);
        lua_getfield(L, 2, "inst");
        if (lua_isstring(L, -1)) {
            strncpy(r.inst, lua_tostring(L, -1), 8);
            r.inst[8] = 0;
        }
        lua_pop(L, 1);
        if (lua_getfield(L, 2, "context") == LUA_TTABLE) {
            lua_getfield(L, -1, "notes");
            lua_getfield(L, -2, "bars");
            int n = lua_istable(L, -2) ? (int)lua_rawlen(L, -2) : 0, with_bars = lua_istable(L, -1);
            if (n > 512)
                n = 512;
            for (int i = 0; i < n; i++) {
                lua_rawgeti(L, -2, i + 1);
                ctx[i] = (uint8_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
                if (with_bars) {
                    lua_rawgeti(L, -1, i + 1);
                    ctx_bar[i] = (uint8_t)lua_tointeger(L, -1);
                    lua_pop(L, 1);
                }
            }
            r.ctx = ctx;
            r.ctx_bar = with_bars ? ctx_bar : NULL;
            r.nctx = n;
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    static mus_out_t o;
    if (mus_make(&r, &o) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "no music recipe for that");
        return 2;
    }
    lua_createtable(L, 0, 16);
    field_str(L, "gen", o.gen);
    field_str(L, "name", o.name);
    field_str(L, "kind", o.kind);
    static const char *const ik[] = { "bpm", "swing", "key", "meter", "bars", "echo", "room", "seed" };
    const int iv[] = { o.bpm, o.swing, o.key, o.meter, o.bars, o.echo, o.room, (int)r.seed };
    for (int i = 0; i < 8; i++) {
        lua_pushinteger(L, iv[i]);
        lua_setfield(L, -2, ik[i]);
    }
    lua_pushboolean(L, o.minor);
    lua_setfield(L, -2, "minor");
    lua_createtable(L, o.nchords, 0);
    for (int i = 0; i < o.nchords; i++) {
        lua_pushstring(L, o.chords[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "chords");
    lua_createtable(L, o.ninst, 0);
    for (int i = 0; i < o.ninst; i++) {
        lua_pushstring(L, o.inst[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "instruments");
    lua_createtable(L, o.npat, 0);
    for (int p = 0; p < o.npat; p++) {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, o.pat[p].len);
        lua_setfield(L, -2, "len");
        lua_newtable(L);
        for (int t = 0; t < MUS_TRACKS; t++) {
            if (!(o.pat[p].used >> t & 1) && !o.pat[p].step[t][0].note) {
                int any = 0;
                for (int k = 0; k < o.pat[p].len && !any; k++)
                    any = o.pat[p].step[t][k].note != 0;
                if (!any)
                    continue;
            }
            lua_createtable(L, o.pat[p].len, 0);
            for (int k = 0; k < o.pat[p].len; k++) {
                lua_pushinteger(L, step_word(&o.pat[p].step[t][k]));
                lua_rawseti(L, -2, k + 1);
            }
            lua_rawseti(L, -2, t);
        }
        lua_setfield(L, -2, "tracks");
        lua_rawseti(L, -2, p + 1);
    }
    lua_setfield(L, -2, "patterns");
    if (o.sfx_len) {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, o.sfx_ms);
        lua_setfield(L, -2, "ms");
        lua_createtable(L, 2, 0);
        lua_pushinteger(L, o.sfx_loop0);
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, o.sfx_loop1);
        lua_rawseti(L, -2, 2);
        lua_setfield(L, -2, "loop");
        lua_createtable(L, o.sfx_len, 0);
        for (int k = 0; k < o.sfx_len; k++) {
            lua_pushinteger(L, step_word(&o.sfx[k]));
            lua_rawseti(L, -2, k + 1);
        }
        lua_setfield(L, -2, "steps");
        lua_setfield(L, -2, "sfx");
    }
    return 1;
}

int ai_lua_music_recipes(lua_State *L)
{
    lua_createtable(L, mus_recipes(), 0);
    for (int i = 0; i < mus_recipes(); i++) {
        lua_createtable(L, 0, 2);
        field_str(L, "id", mus_recipe_id(i));
        field_str(L, "name", mus_recipe_name(i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}
