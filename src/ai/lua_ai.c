/*
 * The assistant for Lua (M30): the `ai` table of every cartridge. Nothing
 * runs and nothing is allocated until a function is called; the knowledge
 * base and the network stay in the kernel image (read-only).
 *
 *   hits, us = ai.ask(question, [{n=5, ctx=word, kinds="api,howto"}])
 *   e = ai.entry(id)            {id, kind, title, name, text, code, gen, see={...}}
 *   list = ai.list([kinds])     every entry {id, title, kind}
 *   name, dist = ai.near(word)  the API name closest to a typo
 *   s = ai.sprite(request, [{gen=, size=16, seed=1, outline=true, palette={...}}])
 *                               {w, h, gen, name, seed, px={0xRRGGBB or -1, ...}}
 *                               (the words of the request choose the recipe,
 *                               unless gen; their colours and size win) 
 *   list = ai.recipes()         {id, name} of every sprite recipe
 *   crc = ai.checksum(question) CRC-32 of the network's outputs (tests)
 */
#include "lua_ai.h"
#include "assist.h"
#include "sprite.h"
#include "drivers/timer.h"
#include "lib/crc32.h"

#include <string.h>

#include "lauxlib.h"
#include "lua.h"

#ifdef BM_HOST_TEST
const uint8_t *ai_host_blob;            /* the tests load build/assist.bin */
uint32_t ai_host_len;
#define BLOB ai_host_blob
#define BLOB_LEN ai_host_len
#else
extern const uint8_t bm_assist_bin[], bm_assist_bin_end[];
#define BLOB bm_assist_bin
#define BLOB_LEN ((uint32_t)(bm_assist_bin_end - bm_assist_bin))
#endif

static void ready(lua_State *L)
{
    if (ai_is_open())
        return;
    int r = ai_open(BLOB, BLOB_LEN);
    if (r)
        luaL_error(L, "assistant: %s", ai_error(r));
}

static void field_str(lua_State *L, const char *k, const char *v)
{
    lua_pushstring(L, v);
    lua_setfield(L, -2, k);
}

static unsigned opt_kinds(lua_State *L, int idx)
{
    const char *k = lua_tostring(L, idx);
    unsigned m = k ? (unsigned)ai_kind_mask(k) : 0;
    return m ? m : AI_KIND_ALL;
}

static int l_ask(lua_State *L)
{
    const char *q = luaL_checkstring(L, 1);
    int n = 5;
    const char *ctx = NULL;
    unsigned kinds = AI_KIND_ALL;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "n");
        n = (int)luaL_optinteger(L, -1, 5);
        lua_getfield(L, 2, "ctx");
        ctx = lua_tostring(L, -1);
        lua_getfield(L, 2, "kinds");
        kinds = opt_kinds(L, -1);
        lua_pop(L, 3);
    }
    if (n < 1) n = 1;
    if (n > 20) n = 20;
    ready(L);
    ai_hit_t hits[20];
    uint32_t t0 = timer_ticks();
    int k = ai_ask(q, ctx, kinds, hits, n);
    uint32_t us = timer_ticks() - t0;
    lua_createtable(L, k, 0);
    for (int i = 0; i < k; i++) {
        ai_entry_t e;
        ai_get(hits[i].entry, &e);
        lua_createtable(L, 0, 4);
        field_str(L, "id", e.id);
        field_str(L, "title", e.title);
        field_str(L, "kind", e.kind);
        lua_pushnumber(L, hits[i].score);
        lua_setfield(L, -2, "score");
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, us);
    return 2;
}

static int l_entry(lua_State *L)
{
    const char *id = luaL_checkstring(L, 1);
    ready(L);
    ai_entry_t e;
    if (ai_get(ai_find(id), &e)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 9);
    field_str(L, "id", e.id);
    field_str(L, "kind", e.kind);
    field_str(L, "title", e.title);
    field_str(L, "name", e.name);
    field_str(L, "text", e.text);
    field_str(L, "code", e.code);
    field_str(L, "gen", e.gen);
    lua_newtable(L);
    int n = 0;
    for (const char *p = e.see; *p;) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        lua_pushlstring(L, p, len);
        lua_rawseti(L, -2, ++n);
        p += len + (c ? 1 : 0);
    }
    lua_setfield(L, -2, "see");
    return 1;
}

static int l_list(lua_State *L)
{
    unsigned kinds = lua_isnoneornil(L, 1) ? AI_KIND_ALL : opt_kinds(L, 1);
    ready(L);
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < ai_count(); i++) {
        ai_entry_t e;
        ai_get(i, &e);
        if (!(e.kmask & kinds))
            continue;
        lua_createtable(L, 0, 3);
        field_str(L, "id", e.id);
        field_str(L, "title", e.title);
        field_str(L, "kind", e.kind);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int l_near(lua_State *L)
{
    const char *w = luaL_checkstring(L, 1);
    ready(L);
    int d;
    const char *s = ai_near(w, &d);
    if (!s) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, s);
    lua_pushinteger(L, d);
    return 2;
}

static int l_sprite(lua_State *L)
{
    const char *q = luaL_checkstring(L, 1);
    int has_opts = lua_istable(L, 2);
    ready(L);
    /* the recipe: opts.gen, a recipe id, or the best sprite entry for the words */
    const char *gen = NULL, *title = NULL;
    if (has_opts) {
        lua_getfield(L, 2, "gen");
        const char *g = lua_tostring(L, -1);
        if (g && spr_find(g) >= 0)
            gen = spr_recipe_id(spr_find(g));
        lua_pop(L, 1);
    }
    if (!gen && spr_find(q) >= 0)
        gen = spr_recipe_id(spr_find(q));
    if (!gen) {
        ai_hit_t hit;
        if (ai_ask(q, NULL, AI_KIND_SPRITE, &hit, 1) == 1 && hit.score >= 0.15f) {
            ai_entry_t e;
            ai_get(hit.entry, &e);
            if (spr_find(e.gen) >= 0) {
                gen = spr_recipe_id(spr_find(e.gen));
                title = e.title;
            }
        }
    }
    if (!gen) {
        lua_pushnil(L);
        lua_pushstring(L, "no sprite recipe for that");
        return 2;
    }
    spr_req_t r;
    spr_req_init(&r, gen);
    static uint32_t pal[256];
    if (has_opts) {             /* the size asked for, unless the words say one */
        lua_getfield(L, 2, "size");
        if (!lua_isnil(L, -1)) r.w = r.h = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    spr_parse(q, &r);
    if (has_opts) {
        lua_getfield(L, 2, "seed");
        if (!lua_isnil(L, -1)) r.seed = (uint32_t)luaL_checkinteger(L, -1);
        lua_getfield(L, 2, "outline");
        if (!lua_isnil(L, -1)) r.outline = lua_toboolean(L, -1);
        lua_getfield(L, 2, "palette");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            if (n > 256) n = 256;
            for (int i = 0; i < n; i++) {
                lua_rawgeti(L, -1, i + 1);
                pal[i] = (uint32_t)lua_tointeger(L, -1) & 0xFFFFFF;
                lua_pop(L, 1);
            }
            r.palette = pal;
            r.npalette = n;
        }
        lua_pop(L, 3);
    }
    static spr_img_t img;
    spr_make(&r, &img);
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, img.w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, img.h);
    lua_setfield(L, -2, "h");
    field_str(L, "gen", gen);
    field_str(L, "name", title ? title : spr_recipe_name(spr_find(gen)));
    lua_pushinteger(L, r.seed);
    lua_setfield(L, -2, "seed");
    lua_createtable(L, img.w * img.h, 0);
    for (int i = 0; i < img.w * img.h; i++) {
        lua_pushinteger(L, img.px[i] == SPR_CLEAR ? -1 : (lua_Integer)img.px[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "px");
    return 1;
}

/* ai.checksum(question): CRC-32 of the network's outputs (int32, little
 * endian) for a question: the tests compare it with the Python reference */
static int l_checksum(lua_State *L)
{
    const char *q = luaL_checkstring(L, 1);
    ready(L);
    static int32_t out[1024];
    int n = ai_logits(q, out, 1024);
    lua_pushinteger(L, (lua_Integer)crc32(out, (uint32_t)n * 4u));
    return 1;
}

static int l_recipes(lua_State *L)
{
    lua_createtable(L, spr_recipes(), 0);
    for (int i = 0; i < spr_recipes(); i++) {
        lua_createtable(L, 0, 2);
        field_str(L, "id", spr_recipe_id(i));
        field_str(L, "name", spr_recipe_name(i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static const luaL_Reg fns[] = {
    { "ask", l_ask }, { "entry", l_entry }, { "list", l_list }, { "near", l_near },
    { "sprite", l_sprite }, { "recipes", l_recipes }, { "checksum", l_checksum }, { NULL, NULL },
};

void ai_lua_open(lua_State *L)
{
    luaL_newlib(L, fns);
    lua_setglobal(L, "ai");
}
