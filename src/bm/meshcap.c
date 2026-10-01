/*
 * Mesh capture (bm Mesh): runs a cartridge's code in a Lua state of its
 * own, where mesh(), mesh_sphere() and mesh_cube() keep a copy of what they
 * are given and every other function of bm does nothing (no files, no
 * screen, no sound). The main chunk, _init, _update and _draw run once
 * each, within an instruction budget; then the globals and the upvalues of
 * the cartridge's functions are searched for the meshes, so each one gets
 * the name of the variable that holds it (M.ship -> "ship").
 */
#include "meshcap.h"
#include "bm.h"
#include "r3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lualib.h"

#define CAP_MAX     64              /* meshes kept */
#define CAP_EVERY   1000            /* instructions between hook calls */
#define CAP_BUDGET  30000           /* x CAP_EVERY = 30 M instructions per phase */
#define CAP_NAME    48

static const char CAP_MT[] = "bm.capture";

typedef struct {
    int nv, nf;
    float *v;                       /* nv x 3 */
    uint16_t *f;                    /* nf x 3, 0-based */
    int32_t *col;                   /* nf: 0xRRGGBB, or -1 = textured */
    float *uv;                      /* nf x 6 (sheet pixels), or NULL */
    uint32_t hash;
    const char *kind;               /* "mesh", "sphere", "cube" */
    char name[CAP_NAME];
} cap_mesh_t;

static struct {
    cap_mesh_t m[CAP_MAX];
    int n;
    long steps;
    int width, height;
    const uint8_t *mesh;
    uint32_t mesh_size;
} cap;

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static void cap_free(void)
{
    for (int i = 0; i < cap.n; i++) {
        free(cap.m[i].v);
        free(cap.m[i].f);
        free(cap.m[i].col);
        free(cap.m[i].uv);
    }
    memset(&cap, 0, sizeof cap);
}

/* keeps a mesh (its arrays become cap's); returns its id from 1, or 0 */
static int cap_keep(cap_mesh_t *m)
{
    m->hash = fnv(fnv(fnv(2166136261u, m->v, (size_t)m->nv * 12), m->f, (size_t)m->nf * 6), m->col,
                  (size_t)m->nf * 4);
    if (m->uv)
        m->hash = fnv(m->hash, m->uv, (size_t)m->nf * 24);
    for (int i = 0; i < cap.n; i++) {
        cap_mesh_t *o = &cap.m[i];
        if (o->hash == m->hash && o->nv == m->nv && o->nf == m->nf && !o->uv == !m->uv &&
            !memcmp(o->v, m->v, (size_t)m->nv * 12) && !memcmp(o->f, m->f, (size_t)m->nf * 6)) {
            free(m->v); free(m->f); free(m->col); free(m->uv);
            return i + 1;               /* the same mesh built again */
        }
    }
    if (cap.n == CAP_MAX) {
        free(m->v); free(m->f); free(m->col); free(m->uv);
        return 0;
    }
    cap.m[cap.n] = *m;
    return ++cap.n;
}

/* the value standing for a mesh: { id = n } (or { model = name }) */
static void cap_handle(lua_State *G, int id, const char *model)
{
    lua_createtable(G, 0, 1);
    if (model) {
        lua_pushstring(G, model);
        lua_setfield(G, -2, "model");
    } else {
        lua_pushinteger(G, id);
        lua_setfield(G, -2, "id");
    }
    luaL_setmetatable(G, CAP_MT);
}

static int cap_alloc(lua_State *G, cap_mesh_t *m, int nv, int nf, int uv)
{
    memset(m, 0, sizeof *m);
    m->nv = nv;
    m->nf = nf;
    m->v = malloc((size_t)nv * 12);
    m->f = malloc((size_t)nf * 6);
    m->col = malloc((size_t)nf * 4);
    m->uv = uv ? malloc((size_t)nf * 24) : NULL;
    if (!m->v || !m->f || !m->col || (uv && !m->uv)) {
        free(m->v); free(m->f); free(m->col); free(m->uv);
        return luaL_error(G, "not enough memory for the mesh");
    }
    return 0;
}

/* mesh(v, f, [uv]): as the real one (the same checks), kept */
static int cap_mesh(lua_State *G)
{
    luaL_checktype(G, 1, LUA_TTABLE);
    luaL_checktype(G, 2, LUA_TTABLE);
    int nv = (int)(luaL_len(G, 1) / 3), nf = (int)(luaL_len(G, 2) / 4);
    luaL_argcheck(G, nv > 0 && nv <= 4096, 1, "1 to 4096 vertices");
    luaL_argcheck(G, nf > 0 && nf <= 16384, 2, "1 to 16384 faces");
    int has_uv = lua_istable(G, 3);
    if (has_uv)
        luaL_argcheck(G, luaL_len(G, 3) >= (lua_Integer)nf * 6, 3, "6 texture coordinates per face");
    cap_mesh_t m;
    cap_alloc(G, &m, nv, nf, has_uv);
    m.kind = "mesh";
    for (int i = 0; i < nv * 3; i++) {
        lua_rawgeti(G, 1, i + 1);
        m.v[i] = (float)lua_tonumber(G, -1);
        lua_pop(G, 1);
    }
    for (int f = 0; f < nf; f++) {
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(G, 2, f * 4 + k + 1);
            lua_Integer idx = lua_tointeger(G, -1);
            lua_pop(G, 1);
            if (idx < 1 || idx > nv) {
                free(m.v); free(m.f); free(m.col); free(m.uv);
                return luaL_error(G, "face %d: vertex index %d out of range", f + 1, (int)idx);
            }
            m.f[f * 3 + k] = (uint16_t)(idx - 1);
        }
        lua_rawgeti(G, 2, f * 4 + 4);
        lua_Integer c = lua_tointeger(G, -1);
        m.col[f] = c == -1 ? -1 : (int32_t)(c & 0xFFFFFF);
        lua_pop(G, 1);
        if (has_uv)
            for (int k = 0; k < 6; k++) {
                lua_rawgeti(G, 3, f * 6 + k + 1);
                m.uv[f * 6 + k] = (float)lua_tonumber(G, -1);
                lua_pop(G, 1);
            }
    }
    if (!has_uv)
        for (int f = 0; f < nf; f++)
            if (m.col[f] == -1)
                m.col[f] = 0xFFFFFF;    /* textured with no texture: white */
    cap_handle(G, cap_keep(&m), NULL);
    return 1;
}

static int cap_from_r3d(lua_State *G, r3d_mesh_t *r, const char *kind)
{
    cap_mesh_t m;
    cap_alloc(G, &m, r->nverts, r->nfaces, 0);
    m.kind = kind;
    for (int i = 0; i < r->nverts; i++) {
        m.v[i * 3] = r->verts[i].x;
        m.v[i * 3 + 1] = r->verts[i].y;
        m.v[i * 3 + 2] = r->verts[i].z;
    }
    memcpy(m.f, r->faces, (size_t)r->nfaces * 6);
    for (int f = 0; f < r->nfaces; f++)
        m.col[f] = (int32_t)(r->colors[f] & 0xFFFFFF);
    r3d_mesh_free(r);
    cap_handle(G, cap_keep(&m), NULL);
    return 1;
}

static int opt_int(lua_State *G, int i, int def)
{
    return lua_isnoneornil(G, i) ? def : (int)luaL_checknumber(G, i);
}

static int cap_sphere(lua_State *G)
{
    r3d_mesh_t r;
    memset(&r, 0, sizeof r);
    if (r3d_mesh_sphere(&r, opt_int(G, 1, 8), opt_int(G, 2, 16), (uint32_t)luaL_optinteger(G, 3, 0xFFFFFF),
                        (uint32_t)luaL_optinteger(G, 4, 0xC0C0C0)) != 0)
        return luaL_error(G, "cannot build the sphere");
    return cap_from_r3d(G, &r, "sphere");
}

static int cap_cube(lua_State *G)
{
    r3d_mesh_t r;
    memset(&r, 0, sizeof r);
    if (r3d_mesh_cube(&r, (uint32_t)luaL_optinteger(G, 1, 0xFFFFFF)) != 0)
        return luaL_error(G, "cannot build the cube");
    return cap_from_r3d(G, &r, "cube");
}

/* model(name or number): a stand-in that bounds3d() understands; nil if the
 * cartridge has no such model */
static int cap_model(lua_State *G)
{
    bm_model_t md;
    for (int i = 0; cap.mesh && bm_mesh_model(cap.mesh, cap.mesh_size, i, &md) == 0; i++)
        if (lua_type(G, 1) == LUA_TNUMBER ? lua_tointeger(G, 1) == i + 1 : !strcmp(md.name, luaL_checkstring(G, 1))) {
            cap_handle(G, 0, md.name);
            return 1;
        }
    lua_pushnil(G);
    return 1;
}

static int cap_models(lua_State *G)
{
    lua_newtable(G);
    bm_model_t md;
    for (int i = 0; cap.mesh && bm_mesh_model(cap.mesh, cap.mesh_size, i, &md) == 0; i++) {
        lua_pushstring(G, md.name);
        lua_rawseti(G, -2, i + 1);
    }
    return 1;
}

/* bounds3d(m): the box of a kept mesh or of a model of the cartridge */
static int cap_bounds(lua_State *G)
{
    float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
    int first = 1;
    if (lua_istable(G, 1)) {
        lua_getfield(G, 1, "id");
        int id = (int)lua_tointeger(G, -1);
        lua_getfield(G, 1, "model");
        const char *model = lua_tostring(G, -1);
        if (id >= 1 && id <= cap.n) {
            const cap_mesh_t *m = &cap.m[id - 1];
            for (int i = 0; i < m->nv; i++)
                for (int k = 0; k < 3; k++) {
                    float v = m->v[i * 3 + k];
                    if (first || v < lo[k]) lo[k] = v;
                    if (first || v > hi[k]) hi[k] = v;
                    if (k == 2) first = 0;
                }
        } else if (model) {
            bm_model_t md;
            for (int j = 0; cap.mesh && bm_mesh_model(cap.mesh, cap.mesh_size, j, &md) == 0; j++) {
                if (strcmp(md.name, model))
                    continue;
                for (int i = 0; i < md.nverts; i++) {
                    float p[3];
                    bm_model_vertex(&md, i, p);
                    for (int k = 0; k < 3; k++) {
                        if (first || p[k] < lo[k]) lo[k] = p[k];
                        if (first || p[k] > hi[k]) hi[k] = p[k];
                    }
                    first = 0;
                }
                break;
            }
        }
    }
    for (int k = 0; k < 3; k++)
        lua_pushnumber(G, lo[k]);
    for (int k = 0; k < 3; k++)
        lua_pushnumber(G, hi[k]);
    return 6;
}

static int cap_rgb(lua_State *G)
{
    lua_Integer r = luaL_optinteger(G, 1, 0), g = luaL_optinteger(G, 2, 0), b = luaL_optinteger(G, 3, 0);
    lua_pushinteger(G, (r & 255) << 16 | (g & 255) << 8 | (b & 255));
    return 1;
}

static int cap_none(lua_State *G) { (void)G; return 0; }
static int cap_zero(lua_State *G) { lua_pushinteger(G, 0); return 1; }
static int cap_table(lua_State *G) { lua_newtable(G); return 1; }
static int cap_two(lua_State *G) { lua_pushinteger(G, 1); lua_pushinteger(G, 1); return 2; }
static int cap_stick(lua_State *G) { lua_pushnumber(G, 0); lua_pushnumber(G, 0); return 2; }

/* project3d(): the middle of the screen, in front of the camera */
static int cap_project(lua_State *G)
{
    lua_pushnumber(G, cap.width / 2);
    lua_pushnumber(G, cap.height / 2);
    lua_pushnumber(G, 1);
    return 3;
}

static int cap_bone(lua_State *G)
{
    for (int k = 0; k < 6; k++)
        lua_pushnumber(G, 0);
    return 6;
}

/* require(name): a library whose every function does nothing */
static int cap_lib_index(lua_State *G)
{
    lua_pushcfunction(G, cap_none);
    return 1;
}

static int cap_require(lua_State *G)
{
    lua_newtable(G);
    lua_createtable(G, 0, 1);
    lua_pushcfunction(G, cap_lib_index);
    lua_setfield(G, -2, "__index");
    lua_setmetatable(G, -2);
    return 1;
}

static void cap_hook(lua_State *G, lua_Debug *ar)
{
    (void)ar;
    if (++cap.steps > CAP_BUDGET)
        luaL_error(G, "more than %d million instructions", CAP_BUDGET * CAP_EVERY / 1000000);
}

/* the names: globals first, then tables and upvalues, breadth first, the
 * keys of each table in order (the same names every time, whatever the
 * order of Lua's hash tables); in a table found in an array the names take
 * the array's along (chef[2].body -> "chef2_body") */
static const char WALK[] =
    "local dbg, mt, G, type, getmetatable, next, mtype, sort = ...\n"
    "local names, seen, queue = {}, {}, {}\n"
    "local function before(a, b)\n"
    "  local ta, tb = type(a), type(b)\n"
    "  if ta ~= tb then return ta == 'number' end\n"
    "  return a < b\n"
    "end\n"
    "local function visit(v, name, inarray)\n"
    "  if type(v) == 'table' then\n"
    "    if getmetatable(v) == mt then\n"
    "      if v.id and v.id > 0 and not names[v.id] then names[v.id] = name end\n"
    "      return\n"
    "    end\n"
    "    if not seen[v] then seen[v] = true; queue[#queue + 1] = { v, name, inarray } end\n"
    "  elseif type(v) == 'function' and not seen[v] then\n"
    "    seen[v] = true\n"
    "    for i = 1, 255 do\n"
    "      local un, uv = dbg.getupvalue(v, i)\n"
    "      if not un then break end\n"
    "      if un ~= '_ENV' then visit(uv, un) end\n"
    "    end\n"
    "  end\n"
    "end\n"
    "seen[G] = true\n"
    "queue[1] = { G, '' }\n"
    "local i = 1\n"
    "while i <= #queue and i <= 20000 do\n"
    "  local t, base, inarray = queue[i][1], queue[i][2], queue[i][3]\n"
    "  i = i + 1\n"
    "  local keys = {}\n"
    "  for k in next, t do\n"
    "    if type(k) == 'string' or mtype(k) == 'integer' then keys[#keys + 1] = k end\n"
    "  end\n"
    "  sort(keys, before)\n"
    "  for _, k in next, keys do\n"
    "    local v = t[k]\n"
    "    if type(k) == 'string' then visit(v, inarray and base .. '_' .. k or k)\n"
    "    else visit(v, base .. k, true) end\n"
    "  end\n"
    "end\n"
    "return names\n";

/* the message of an error, with the line of the cartridge's code where it
 * happened when the message does not say it (an error inside a function of
 * Lua's libraries, such as table.sort) */
static int cap_msgh(lua_State *G)
{
    const char *msg = lua_tostring(G, 1);
    if (!msg || !strncmp(msg, "main.lua:", 9))
        return 1;
    lua_Debug ar;
    for (int level = 1; lua_getstack(G, level, &ar); level++) {
        lua_getinfo(G, "Sl", &ar);
        if (ar.currentline > 0 && !strcmp(ar.short_src, "main.lua")) {
            lua_pushfstring(G, "main.lua:%d: %s", ar.currentline, msg);
            return 1;
        }
    }
    return 1;
}

static void cap_phase(lua_State *G, char *err, size_t errlen)
{
    cap.steps = 0;
    lua_pushcfunction(G, cap_msgh);
    lua_insert(G, -2);
    if (lua_pcall(G, 0, 0, -2) != LUA_OK) {
        if (!err[0])
            snprintf(err, errlen, "%s", lua_tostring(G, -1) ? lua_tostring(G, -1) : "error");
        lua_pop(G, 1);
    }
    lua_pop(G, 1);                      /* cap_msgh */
}

int bm_mesh_capture(lua_State *L, const char *lua, size_t len, int width, int height, const luaL_Reg *api,
                    const uint8_t *mesh, uint32_t mesh_size)
{
    char err[200] = "";
    cap_free();
    cap.mesh = mesh;
    cap.mesh_size = mesh_size;
    cap.width = width;
    cap.height = height;
    lua_State *G = bm_meshcap_newstate();
    if (!G)
        return luaL_error(L, "not enough memory to read the code");
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
        { LUA_COLIBNAME, luaopen_coroutine }, { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL },
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(G, l->name, l->func, 1);
        lua_pop(G, 1);
    }
    static const char *const removed[] = { "dofile", "loadfile", "load", NULL };
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(G);
        lua_setglobal(G, *r);
    }
    luaL_newmetatable(G, CAP_MT);
    lua_pop(G, 1);
    /* the walk's tools, before the cartridge's code can change them */
    static const char *const std[] = { "type", "getmetatable", "next", NULL };
    lua_createtable(G, 6, 0);
    lua_pushglobaltable(G);
    lua_rawseti(G, -2, 1);
    for (int i = 0; std[i]; i++) {
        lua_getglobal(G, std[i]);
        lua_rawseti(G, -2, i + 2);
    }
    lua_getglobal(G, "math");
    lua_getfield(G, -1, "type");
    lua_rawseti(G, -3, 5);
    lua_pop(G, 1);
    lua_getglobal(G, "table");
    lua_getfield(G, -1, "sort");
    lua_rawseti(G, -3, 6);
    lua_pop(G, 1);
    lua_setfield(G, LUA_REGISTRYINDEX, "bm.capture.std");
    /* every function of bm does nothing; a few give what the code expects */
    for (const luaL_Reg *f = api; f->name; f++) {
        lua_pushcfunction(G, cap_none);
        lua_setglobal(G, f->name);
    }
    static const luaL_Reg stand[] = {
        { "mesh", cap_mesh }, { "mesh_sphere", cap_sphere }, { "mesh_cube", cap_cube },
        { "model", cap_model }, { "models", cap_models }, { "bounds3d", cap_bounds },
        { "rgb", cap_rgb }, { "time", cap_zero }, { "stat", cap_zero }, { "print", cap_zero },
        { "mget", cap_zero }, { "pget", cap_zero }, { "sget", cap_zero },
        { "project3d", cap_project }, { "animate", cap_zero }, { "clips", cap_table },
        { "ls", cap_table }, { "keys", cap_table }, { "pad", cap_zero }, { "players", cap_two },
        { "stick", cap_stick }, { "bone3d", cap_bone },
        { "require", cap_require },
        { NULL, NULL },
    };
    for (const luaL_Reg *f = stand; f->name; f++) {
        lua_pushcfunction(G, f->func);
        lua_setglobal(G, f->name);
    }
    static const char *const waves[] = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL", NULL };
    for (int w = 0; waves[w]; w++) {
        lua_pushinteger(G, w);
        lua_setglobal(G, waves[w]);
    }
    lua_pushinteger(G, width);
    lua_setglobal(G, "SCREEN_W");
    lua_pushinteger(G, height);
    lua_setglobal(G, "SCREEN_H");
    lua_sethook(G, cap_hook, LUA_MASKCOUNT, CAP_EVERY);

    /* the code, then _init, _update and _draw once */
    if (luaL_loadbuffer(G, lua, len, "=main.lua") == LUA_OK) {
        cap_phase(G, err, sizeof err);
        static const char *const calls[] = { "_init", "_update", "_draw", NULL };
        for (const char *const *c = calls; *c; c++) {
            if (lua_getglobal(G, *c) == LUA_TFUNCTION)
                cap_phase(G, err, sizeof err);
            else
                lua_pop(G, 1);
        }
    } else {
        snprintf(err, sizeof err, "%s", lua_tostring(G, -1));
        lua_pop(G, 1);
    }

    /* the names, from the variables that hold the meshes */
    lua_sethook(G, NULL, 0, 0);
    if (cap.n && luaL_loadbuffer(G, WALK, sizeof WALK - 1, "=walk") == LUA_OK) {
        luaL_requiref(G, "debug", luaopen_debug, 0);
        luaL_getmetatable(G, CAP_MT);
        lua_getfield(G, LUA_REGISTRYINDEX, "bm.capture.std");
        for (int i = 1; i <= 6; i++)
            lua_rawgeti(G, -i, i);
        lua_remove(G, -7);
        if (lua_pcall(G, 8, 1, 0) == LUA_OK) {
            for (int i = 0; i < cap.n; i++) {
                lua_rawgeti(G, -1, i + 1);
                const char *s = lua_tostring(G, -1);
                if (s && s[0])
                    snprintf(cap.m[i].name, CAP_NAME, "%s", s);
                lua_pop(G, 1);
            }
        }
        lua_pop(G, 1);
    }
    lua_close(G);

    /* to the caller: the arguments of mesh() */
    lua_createtable(L, cap.n, 0);
    for (int i = 0; i < cap.n; i++) {
        cap_mesh_t *m = &cap.m[i];
        if (!m->name[0])
            snprintf(m->name, CAP_NAME, "%s%d", m->kind, i + 1);
        for (int j = 0; j < i; j++)         /* two variables of the same name */
            if (!strcmp(cap.m[j].name, m->name)) {
                size_t n = strlen(m->name);
                snprintf(m->name + (n > CAP_NAME - 4 ? CAP_NAME - 4 : n), 4, "%d", i + 1);
                break;
            }
        lua_createtable(L, 0, 5);
        lua_pushstring(L, m->name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, m->kind);
        lua_setfield(L, -2, "kind");
        lua_createtable(L, m->nv * 3, 0);
        for (int k = 0; k < m->nv * 3; k++) {
            lua_pushnumber(L, m->v[k]);
            lua_rawseti(L, -2, k + 1);
        }
        lua_setfield(L, -2, "verts");
        lua_createtable(L, m->nf * 4, 0);
        for (int f = 0; f < m->nf; f++) {
            for (int k = 0; k < 3; k++) {
                lua_pushinteger(L, m->f[f * 3 + k] + 1);
                lua_rawseti(L, -2, f * 4 + k + 1);
            }
            lua_pushinteger(L, m->col[f]);
            lua_rawseti(L, -2, f * 4 + 4);
        }
        lua_setfield(L, -2, "faces");
        if (m->uv) {
            lua_createtable(L, m->nf * 6, 0);
            for (int k = 0; k < m->nf * 6; k++) {
                lua_pushnumber(L, m->uv[k]);
                lua_rawseti(L, -2, k + 1);
            }
            lua_setfield(L, -2, "uv");
        }
        lua_rawseti(L, -2, i + 1);
    }
    cap_free();
    if (err[0])
        lua_pushstring(L, err);
    else
        lua_pushnil(L);
    return 2;
}
