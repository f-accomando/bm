/*
 * The assistant for Lua (M30): the `ai` table of every cartridge. Nothing
 * runs and nothing is allocated until a function is called; the knowledge
 * base and the network stay in the kernel image (read-only).
 *
 *   hits, us = ai.ask(question, [{n=5, ctx=word, kinds="api,howto"}])
 *                               (kinds: api, howto, error, tip, sprite, action, mesh)
 *   e = ai.entry(id)            {id, kind, title, name, text, code, gen, see={...}}
 *   list = ai.list([kinds])     every entry {id, title, kind}
 *   name, dist = ai.near(word)  the API name closest to a typo
 *   s = ai.sprite(request, [{gen=, size=16, seed=1, outline=true, palette={...}}])
 *                               {w, h, gen, name, seed, px={0xRRGGBB or -1, ...}}
 *                               (the words of the request choose the recipe,
 *                               unless gen; their colours and size win) 
 *   list = ai.recipes()         {id, name} of every sprite recipe
 *   m = ai.mesh(request, [{gen=, seed=1, scale=1, rig=true}])
 *                               the base of a 3D model (mesh.h): {gen, name, seed,
 *                               faces = { {p = {{x,y,z}...}, c = 0xRRGGBB, b = {bone...}} },
 *                               bones = { {name, parent, head, tail} } or nil,
 *                               clips = { {name, loop, length, mode, keys = {{t, pose}}} }}
 *                               as bm Studio and bm Animator keep them (bm3d.lua)
 *   list = ai.recipes("mesh")   {id, name, rigged} of every 3D recipe
 *   crc = ai.checksum(question) CRC-32 of the network's outputs (tests)
 */
#include "lua_ai.h"
#include "assist.h"
#include "sprite.h"
#include "mesh.h"
#include "drivers/timer.h"
#include "lib/crc32.h"

#include <stdlib.h>
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

static void push_v3(lua_State *L, const float *v)
{
    lua_createtable(L, 3, 0);
    for (int i = 0; i < 3; i++) {
        lua_pushnumber(L, v[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

static int l_mesh(lua_State *L)
{
    const char *q = luaL_checkstring(L, 1);
    int has_opts = lua_istable(L, 2);
    ready(L);
    const char *gen = NULL, *title = NULL;
    if (has_opts) {
        lua_getfield(L, 2, "gen");
        const char *g = lua_tostring(L, -1);
        if (g && mesh_find(g) >= 0)
            gen = mesh_recipe_id(mesh_find(g));
        lua_pop(L, 1);
    }
    if (!gen && mesh_find(q) >= 0)
        gen = mesh_recipe_id(mesh_find(q));
    if (!gen) {
        ai_hit_t hit;
        if (ai_ask(q, NULL, AI_KIND_MESH, &hit, 1) == 1 && hit.score >= 0.15f) {
            ai_entry_t e;
            ai_get(hit.entry, &e);
            if (mesh_find(e.gen) >= 0) {
                gen = mesh_recipe_id(mesh_find(e.gen));
                title = e.title;
            }
        }
    }
    if (!gen) {
        lua_pushnil(L);
        lua_pushstring(L, "no 3D recipe for that");
        return 2;
    }
    mesh_req_t r;
    mesh_req_init(&r, gen);
    mesh_parse(q, &r);
    if (has_opts) {
        lua_getfield(L, 2, "seed");
        if (!lua_isnil(L, -1)) r.seed = (uint32_t)luaL_checkinteger(L, -1);
        lua_getfield(L, 2, "scale");
        if (!lua_isnil(L, -1)) r.scale *= (float)luaL_checknumber(L, -1);
        lua_getfield(L, 2, "rig");
        if (!lua_isnil(L, -1)) r.rig = r.rig && lua_toboolean(L, -1);
        lua_pop(L, 3);
    }
    /* the model is big (MESH_MAX_FACES): it lives only while the table is made */
    mesh_model_t *mp = malloc(sizeof *mp);
    if (!mp)
        return luaL_error(L, "assistant: no memory for the model");
    mesh_make(&r, mp);
#define m (*mp)
    lua_createtable(L, 0, 7);
    field_str(L, "gen", gen);
    field_str(L, "name", title ? title : mesh_recipe_name(mesh_find(gen)));
    lua_pushinteger(L, r.seed);
    lua_setfield(L, -2, "seed");
    lua_createtable(L, m.nfaces, 0);
    for (int i = 0; i < m.nfaces; i++) {
        const mesh_face_t *f = &m.faces[i];
        lua_createtable(L, 0, 3);
        lua_createtable(L, f->n, 0);
        for (int k = 0; k < f->n; k++) {
            push_v3(L, f->p[k]);
            lua_rawseti(L, -2, k + 1);
        }
        lua_setfield(L, -2, "p");
        lua_pushinteger(L, f->c);
        lua_setfield(L, -2, "c");
        if (m.nbones) {
            lua_createtable(L, f->n, 0);
            for (int k = 0; k < f->n; k++) {
                lua_pushinteger(L, f->b[k] + 1);
                lua_rawseti(L, -2, k + 1);
            }
            lua_setfield(L, -2, "b");
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "faces");
    if (m.nbones) {
        lua_createtable(L, m.nbones, 0);
        for (int i = 0; i < m.nbones; i++) {
            const mesh_bone_t *b = &m.bones[i];
            lua_createtable(L, 0, 4);
            field_str(L, "name", b->name);
            lua_pushinteger(L, b->parent + 1);
            lua_setfield(L, -2, "parent");
            push_v3(L, b->head);
            lua_setfield(L, -2, "head");
            push_v3(L, b->tail);
            lua_setfield(L, -2, "tail");
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "bones");
        lua_createtable(L, m.nclips, 0);
        for (int k = 0; k < m.nclips; k++) {
            const mesh_clip_t *c = &m.clips[k];
            lua_createtable(L, 0, 5);
            field_str(L, "name", c->name);
            lua_pushboolean(L, c->loop);
            lua_setfield(L, -2, "loop");
            lua_pushnumber(L, c->length);
            lua_setfield(L, -2, "length");
            lua_pushinteger(L, 1);
            lua_setfield(L, -2, "mode");
            lua_createtable(L, c->nkeys, 0);
            for (int j = 0; j < c->nkeys; j++) {
                const mesh_key_t *key = &c->keys[j];
                lua_createtable(L, 0, 2);
                lua_pushnumber(L, key->t);
                lua_setfield(L, -2, "t");
                lua_createtable(L, m.nbones, 0);
                for (int b = 0; b < m.nbones; b++) {
                    lua_createtable(L, 0, 2);
                    lua_createtable(L, 4, 0);
                    for (int i = 0; i < 4; i++) {
                        lua_pushnumber(L, key->pose[b].q[i]);
                        lua_rawseti(L, -2, i + 1);
                    }
                    lua_setfield(L, -2, "q");
                    push_v3(L, key->pose[b].t);
                    lua_setfield(L, -2, "t");
                    lua_rawseti(L, -2, b + 1);
                }
                lua_setfield(L, -2, "pose");
                lua_rawseti(L, -2, j + 1);
            }
            lua_setfield(L, -2, "keys");
            lua_rawseti(L, -2, k + 1);
        }
        lua_setfield(L, -2, "clips");
    }
#undef m
    free(mp);
    return 1;
}

static int l_recipes(lua_State *L)
{
    const char *what = luaL_optstring(L, 1, "sprite");
    if (!strcmp(what, "mesh")) {
        lua_createtable(L, mesh_recipes(), 0);
        for (int i = 0; i < mesh_recipes(); i++) {
            lua_createtable(L, 0, 3);
            field_str(L, "id", mesh_recipe_id(i));
            field_str(L, "name", mesh_recipe_name(i));
            lua_pushboolean(L, mesh_recipe_rigged(i));
            lua_setfield(L, -2, "rigged");
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }
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
    { "sprite", l_sprite }, { "recipes", l_recipes }, { "checksum", l_checksum }, { "mesh", l_mesh },
    { NULL, NULL },
};

void ai_lua_open(lua_State *L)
{
    luaL_newlib(L, fns);
    lua_setglobal(L, "ai");
}
