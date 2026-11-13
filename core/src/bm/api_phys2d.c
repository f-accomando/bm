/*
 * api_phys2d.c: Lua 5.4 bindings for the compact 2D rigid-body physics engine (API-10).
 */
#include "rt_internal.h"
#include "phys2d.h"
#include <stdlib.h>
#include <string.h>

#define PHYS_WORLD_MT "bm.phys_world"
#define PHYS_BODY_MT  "bm.phys_body"

typedef struct {
    phys_world_t *w;
} lphys_world_t;

typedef struct {
    phys_world_t *w;
    int id;
} lphys_body_t;

static phys_world_t *check_world(lua_State *L, int idx)
{
    lphys_world_t *lw = (lphys_world_t *)luaL_checkudata(L, idx, PHYS_WORLD_MT);
    if (!lw || !lw->w)
        luaL_error(L, "physics world is freed");
    return lw->w;
}

static phys_body_t *check_body(lua_State *L, int idx, phys_world_t **out_w)
{
    lphys_body_t *lb = (lphys_body_t *)luaL_checkudata(L, idx, PHYS_BODY_MT);
    if (!lb || !lb->w || lb->id < 0 || lb->id >= PHYS_MAX_BODIES || !lb->w->bodies[lb->id].active)
        luaL_error(L, "physics body is removed");
    if (out_w) *out_w = lb->w;
    return &lb->w->bodies[lb->id];
}

static int l_pw_gc(lua_State *L)
{
    lphys_world_t *lw = (lphys_world_t *)luaL_checkudata(L, 1, PHYS_WORLD_MT);
    if (lw && lw->w) {
        free(lw->w);
        lw->w = NULL;
    }
    return 0;
}

static phys_body_type_t parse_body_type(lua_State *L, int idx)
{
    if (lua_isinteger(L, idx)) {
        int t = (int)lua_tointeger(L, idx);
        if (t == 0) return PHYS_STATIC;
        if (t == 1) return PHYS_KINEMATIC;
        return PHYS_DYNAMIC;
    }
    const char *s = luaL_checkstring(L, idx);
    if (strcmp(s, "static") == 0) return PHYS_STATIC;
    if (strcmp(s, "kinematic") == 0) return PHYS_KINEMATIC;
    if (strcmp(s, "dynamic") == 0) return PHYS_DYNAMIC;
    luaL_argerror(L, idx, "expected 'static', 'kinematic', or 'dynamic'");
    return PHYS_DYNAMIC;
}

/* w:body(type, x, y, shape, ...) */
static int l_pw_body(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    phys_body_type_t btype = parse_body_type(L, 2);
    float x = (float)luaL_checknumber(L, 3);
    float y = (float)luaL_checknumber(L, 4);
    const char *shape = luaL_checkstring(L, 5);
    int id = -1;

    if (strcmp(shape, "circle") == 0) {
        float r = (float)luaL_checknumber(L, 6);
        float dens = (float)luaL_optnumber(L, 7, 1.0);
        id = phys_add_circle(w, btype, x, y, r, dens);
    } else if (strcmp(shape, "box") == 0) {
        float bw = (float)luaL_checknumber(L, 6);
        float bh = (float)luaL_checknumber(L, 7);
        float dens = (float)luaL_optnumber(L, 8, 1.0);
        id = phys_add_box(w, btype, x, y, bw, bh, dens);
    } else if (strcmp(shape, "poly") == 0) {
        luaL_checktype(L, 6, LUA_TTABLE);
        float dens = (float)luaL_optnumber(L, 7, 1.0);
        int len = (int)luaL_len(L, 6);
        int n = len / 2;
        if (n < 3) return luaL_error(L, "polygon needs at least 3 vertices");
        if (n > PHYS_MAX_VERTS) n = PHYS_MAX_VERTS;
        float pts[PHYS_MAX_VERTS * 2];
        for (int i = 0; i < n * 2; i++) {
            lua_rawgeti(L, 6, i + 1);
            pts[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        id = phys_add_poly(w, btype, x, y, pts, n, dens);
    } else {
        return luaL_argerror(L, 5, "expected 'circle', 'box', or 'poly'");
    }

    if (id < 0)
        return luaL_error(L, "physics world body limit reached");

    lphys_body_t *lb = (lphys_body_t *)lua_newuserdatauv(L, sizeof *lb, 1);
    lb->w = w;
    lb->id = id;
    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1); /* pin world lifetime to body */
    luaL_getmetatable(L, PHYS_BODY_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int l_pw_step(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    float dt = (float)luaL_optnumber(L, 2, 1.0 / 60.0);
    int iters = (int)luaL_optinteger(L, 3, 8);
    phys_step(w, dt, iters);
    return 0;
}

static int l_pw_gravity(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    if (lua_gettop(L) >= 3) {
        w->gx = (float)luaL_checknumber(L, 2);
        w->gy = (float)luaL_checknumber(L, 3);
        return 0;
    }
    lua_pushnumber(L, (lua_Number)w->gx);
    lua_pushnumber(L, (lua_Number)w->gy);
    return 2;
}

static int l_pw_joint_distance(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    phys_world_t *wa = NULL, *wb = NULL;
    phys_body_t *a = check_body(L, 2, &wa);
    phys_body_t *b = check_body(L, 3, &wb);
    if (wa != w || wb != w) return luaL_error(L, "bodies belong to another world");
    float ax1 = (float)luaL_optnumber(L, 4, a->x);
    float ay1 = (float)luaL_optnumber(L, 5, a->y);
    float ax2 = (float)luaL_optnumber(L, 6, b->x);
    float ay2 = (float)luaL_optnumber(L, 7, b->y);
    float len = (float)luaL_optnumber(L, 8, 0.0);
    int jid = phys_add_joint_distance(w, a->id, b->id, ax1, ay1, ax2, ay2, len);
    if (jid < 0) return luaL_error(L, "cannot create joint");
    lua_pushinteger(L, jid);
    return 1;
}

static int l_pw_joint_revolute(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    phys_world_t *wa = NULL, *wb = NULL;
    phys_body_t *a = check_body(L, 2, &wa);
    phys_body_t *b = check_body(L, 3, &wb);
    if (wa != w || wb != w) return luaL_error(L, "bodies belong to another world");
    float ax = (float)luaL_optnumber(L, 4, (a->x + b->x) * 0.5f);
    float ay = (float)luaL_optnumber(L, 5, (a->y + b->y) * 0.5f);
    int jid = phys_add_joint_revolute(w, a->id, b->id, ax, ay);
    if (jid < 0) return luaL_error(L, "cannot create joint");
    lua_pushinteger(L, jid);
    return 1;
}

static int l_pw_joint_weld(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    phys_world_t *wa = NULL, *wb = NULL;
    phys_body_t *a = check_body(L, 2, &wa);
    phys_body_t *b = check_body(L, 3, &wb);
    if (wa != w || wb != w) return luaL_error(L, "bodies belong to another world");
    float ax = (float)luaL_optnumber(L, 4, (a->x + b->x) * 0.5f);
    float ay = (float)luaL_optnumber(L, 5, (a->y + b->y) * 0.5f);
    int jid = phys_add_joint_weld(w, a->id, b->id, ax, ay);
    if (jid < 0) return luaL_error(L, "cannot create joint");
    lua_pushinteger(L, jid);
    return 1;
}

static int l_pw_contacts(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    lua_createtable(L, w->ncontacts, 0);
    for (int i = 0; i < w->ncontacts; i++) {
        const phys_contact_t *c = &w->contacts[i];
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, c->a); lua_setfield(L, -2, "a");
        lua_pushinteger(L, c->b); lua_setfield(L, -2, "b");
        lua_pushnumber(L, (lua_Number)c->nx); lua_setfield(L, -2, "nx");
        lua_pushnumber(L, (lua_Number)c->ny); lua_setfield(L, -2, "ny");
        lua_pushnumber(L, (lua_Number)c->px); lua_setfield(L, -2, "x");
        lua_pushnumber(L, (lua_Number)c->py); lua_setfield(L, -2, "y");
        lua_pushnumber(L, (lua_Number)c->depth); lua_setfield(L, -2, "depth");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_pw_count(lua_State *L)
{
    phys_world_t *w = check_world(L, 1);
    int count = 0;
    for (int i = 0; i < w->nbodies; i++)
        if (w->bodies[i].active) count++;
    lua_pushinteger(L, count);
    return 1;
}

/* ---- Body Methods ---- */

static int l_pb_pos(lua_State *L)
{
    phys_body_t *b = check_body(L, 1, NULL);
    if (lua_gettop(L) >= 3) {
        b->x = (float)luaL_checknumber(L, 2);
        b->y = (float)luaL_checknumber(L, 3);
        if (!lua_isnoneornil(L, 4))
            b->angle = (float)luaL_checknumber(L, 4);
        return 0;
    }
    lua_pushnumber(L, (lua_Number)b->x);
    lua_pushnumber(L, (lua_Number)b->y);
    lua_pushnumber(L, (lua_Number)b->angle);
    return 3;
}

static int l_pb_vel(lua_State *L)
{
    phys_body_t *b = check_body(L, 1, NULL);
    if (lua_gettop(L) >= 3) {
        b->vx = (float)luaL_checknumber(L, 2);
        b->vy = (float)luaL_checknumber(L, 3);
        if (!lua_isnoneornil(L, 4))
            b->av = (float)luaL_checknumber(L, 4);
        return 0;
    }
    lua_pushnumber(L, (lua_Number)b->vx);
    lua_pushnumber(L, (lua_Number)b->vy);
    lua_pushnumber(L, (lua_Number)b->av);
    return 3;
}

static int l_pb_apply_force(lua_State *L)
{
    phys_world_t *w = NULL;
    phys_body_t *b = check_body(L, 1, &w);
    float fx = (float)luaL_checknumber(L, 2);
    float fy = (float)luaL_checknumber(L, 3);
    int has_pt = lua_gettop(L) >= 5;
    float px = has_pt ? (float)luaL_checknumber(L, 4) : 0.0f;
    float py = has_pt ? (float)luaL_checknumber(L, 5) : 0.0f;
    phys_apply_force(w, b->id, fx, fy, px, py, has_pt);
    return 0;
}

static int l_pb_apply_impulse(lua_State *L)
{
    phys_world_t *w = NULL;
    phys_body_t *b = check_body(L, 1, &w);
    float ix = (float)luaL_checknumber(L, 2);
    float iy = (float)luaL_checknumber(L, 3);
    int has_pt = lua_gettop(L) >= 5;
    float px = has_pt ? (float)luaL_checknumber(L, 4) : 0.0f;
    float py = has_pt ? (float)luaL_checknumber(L, 5) : 0.0f;
    phys_apply_impulse(w, b->id, ix, iy, px, py, has_pt);
    return 0;
}

static int l_pb_material(lua_State *L)
{
    phys_body_t *b = check_body(L, 1, NULL);
    if (lua_gettop(L) >= 2) {
        if (!lua_isnoneornil(L, 2)) b->restitution = (float)luaL_checknumber(L, 2);
        if (!lua_isnoneornil(L, 3)) b->friction = (float)luaL_checknumber(L, 3);
        return 0;
    }
    lua_pushnumber(L, (lua_Number)b->restitution);
    lua_pushnumber(L, (lua_Number)b->friction);
    return 2;
}

static int l_pb_mass(lua_State *L)
{
    phys_body_t *b = check_body(L, 1, NULL);
    lua_pushnumber(L, (lua_Number)b->mass);
    lua_pushnumber(L, (lua_Number)b->inertia);
    return 2;
}

static int l_pb_id(lua_State *L)
{
    phys_body_t *b = check_body(L, 1, NULL);
    lua_pushinteger(L, b->id);
    return 1;
}

static int l_pb_remove(lua_State *L)
{
    phys_world_t *w = NULL;
    phys_body_t *b = check_body(L, 1, &w);
    phys_remove_body(w, b->id);
    return 0;
}

void phys2d_type_init(lua_State *L)
{
    if (luaL_newmetatable(L, PHYS_WORLD_MT)) {
        lua_pushcfunction(L, l_pw_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        lua_pushcfunction(L, l_pw_body);           lua_setfield(L, -2, "body");
        lua_pushcfunction(L, l_pw_step);           lua_setfield(L, -2, "step");
        lua_pushcfunction(L, l_pw_gravity);        lua_setfield(L, -2, "gravity");
        lua_pushcfunction(L, l_pw_joint_distance); lua_setfield(L, -2, "joint_distance");
        lua_pushcfunction(L, l_pw_joint_revolute); lua_setfield(L, -2, "joint_revolute");
        lua_pushcfunction(L, l_pw_joint_weld);     lua_setfield(L, -2, "joint_weld");
        lua_pushcfunction(L, l_pw_contacts);       lua_setfield(L, -2, "contacts");
        lua_pushcfunction(L, l_pw_count);          lua_setfield(L, -2, "count");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, PHYS_BODY_MT)) {
        lua_newtable(L);
        lua_pushcfunction(L, l_pb_pos);           lua_setfield(L, -2, "pos");
        lua_pushcfunction(L, l_pb_vel);           lua_setfield(L, -2, "vel");
        lua_pushcfunction(L, l_pb_apply_force);   lua_setfield(L, -2, "apply_force");
        lua_pushcfunction(L, l_pb_apply_impulse); lua_setfield(L, -2, "apply_impulse");
        lua_pushcfunction(L, l_pb_material);      lua_setfield(L, -2, "material");
        lua_pushcfunction(L, l_pb_mass);          lua_setfield(L, -2, "mass");
        lua_pushcfunction(L, l_pb_id);            lua_setfield(L, -2, "id");
        lua_pushcfunction(L, l_pb_remove);        lua_setfield(L, -2, "remove");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int l_phys_world(lua_State *L)
{
    float gx = (float)luaL_optnumber(L, 1, 0.0);
    float gy = (float)luaL_optnumber(L, 2, 9.8);
    phys_world_t *w = (phys_world_t *)malloc(sizeof *w);
    if (!w)
        return luaL_error(L, "not enough memory for physics world");
    phys_world_init(w, gx, gy);
    lphys_world_t *lw = (lphys_world_t *)lua_newuserdatauv(L, sizeof *lw, 0);
    lw->w = w;
    luaL_getmetatable(L, PHYS_WORLD_MT);
    lua_setmetatable(L, -2);
    return 1;
}
