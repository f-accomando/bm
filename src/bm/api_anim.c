/*
 * api_anim.c: skeletal animation (bm Animator) and the rest of the 3D bindings.
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

/* ---- skeletal animation (bm Animator): the same arithmetic as
 * carts/animator/main.lua (its pose of a clip) */

static void quat_norm(float q[4])
{
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-12f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int k = 0; k < 4; k++) q[k] /= l;
}

/* the shorter way from a to b */
static void quat_slerp(float out[4], const float a[4], const float b[4], float u)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = 1, k0, k1;
    if (d < 0) { d = -d; s = -1; }
    if (d > 0.9995f) {
        k0 = 1 - u;
        k1 = u;
    } else {
        float th = acosf(d), sn = sinf(th);
        k0 = sinf((1 - u) * th) / sn;
        k1 = sinf(u * th) / sn;
    }
    for (int k = 0; k < 4; k++) out[k] = a[k] * k0 + s * b[k] * k1;
    quat_norm(out);
}

static float pose_q[BM_BONES_MAX][4], pose_t[BM_BONES_MAX][3];
static float pose_q2[BM_BONES_MAX][4], pose_t2[BM_BONES_MAX][3];
static float key_q[BM_BONES_MAX][4], key_tr[BM_BONES_MAX][3];

/* the pose of clip c at time t into q, tr */
static void pose_at(int nb, const bm_clip_t *c, float t, float (*q)[4], float (*tr)[3])
{
    const float L = c->length > 0 ? c->length : 1;
    if (c->loop) {
        t = fmodf(t, L);
        if (t < 0) t += L;
    } else {
        t = t < 0 ? 0 : t > L ? L : t;
    }
    int n = c->nkeys, ka, kb;
    float first = bm_clip_key(c, nb, 0, NULL, NULL), last = bm_clip_key(c, nb, n - 1, NULL, NULL), ta, tb;
    if (n == 1 || (!c->loop && t < first)) {
        bm_clip_key(c, nb, 0, q, tr);
        return;
    }
    if (t < first || t >= last) {
        if (!c->loop) {
            bm_clip_key(c, nb, n - 1, q, tr);
            return;
        }
        ka = n - 1; kb = 0;
        ta = t < first ? last - L : last;
        tb = t < first ? first : first + L;
    } else {
        ka = 0;
        while (ka + 1 < n && bm_clip_key(c, nb, ka + 1, NULL, NULL) <= t)
            ka++;
        kb = ka + 1;
        ta = bm_clip_key(c, nb, ka, NULL, NULL);
        tb = bm_clip_key(c, nb, kb, NULL, NULL);
    }
    float u = tb > ta ? (t - ta) / (tb - ta) : 0;
    if (c->mode == 2) u = 0;                         /* step */
    else if (c->mode == 1) u = u * u * (3 - 2 * u);  /* smooth */
    else if (c->mode == 3) u = u * u;                /* ease-in: slow start */
    else if (c->mode == 4) u = u * (2 - u);          /* ease-out: 1 - (1-u)^2, slow end */
    bm_clip_key(c, nb, ka, q, tr);
    bm_clip_key(c, nb, kb, key_q, key_tr);
    for (int i = 0; i < nb; i++) {
        float a[4] = { q[i][0], q[i][1], q[i][2], q[i][3] };
        quat_slerp(q[i], a, key_q[i], u);
        for (int k = 0; k < 3; k++) tr[i][k] += (key_tr[i][k] - tr[i][k]) * u;
    }
}

/* M[i] = M[parent] * T(head + t) * R(q) * T(-head); every vertex follows its bone */
static void quat_mul(float out[4], const float a[4], const float b[4])
{
    float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

static void skin(lmesh_t *lm, float (*q)[4], float (*tr)[3])
{
    skel_t *s = lm->skel;
    const int nb = s->r.nbones;
    for (int i = 0; i < nb; i++) {
        int parent;
        float h[3], tail[3], r[9];
        bm_rig_bone(&s->r, i, NULL, &parent, h, tail);
        if (s->turned[i]) {                 /* bone_turn(): in the parent's frame, before the pose */
            float a[4] = { q[i][0], q[i][1], q[i][2], q[i][3] };
            quat_mul(q[i], s->turn[i], a);
        }
        quat_norm(q[i]);
        float x = q[i][0], y = q[i][1], z = q[i][2], w = q[i][3];
        r[0] = 1 - 2 * (y * y + z * z); r[1] = 2 * (x * y - z * w); r[2] = 2 * (x * z + y * w);
        r[3] = 2 * (x * y + z * w); r[4] = 1 - 2 * (x * x + z * z); r[5] = 2 * (y * z - x * w);
        r[6] = 2 * (x * z - y * w); r[7] = 2 * (y * z + x * w); r[8] = 1 - 2 * (x * x + y * y);
        float local[12];
        for (int row = 0; row < 3; row++) {
            local[row * 4] = r[row * 3];
            local[row * 4 + 1] = r[row * 3 + 1];
            local[row * 4 + 2] = r[row * 3 + 2];
            local[row * 4 + 3] = h[row] + tr[i][row] - (r[row * 3] * h[0] + r[row * 3 + 1] * h[1] + r[row * 3 + 2] * h[2]);
        }
        float *m = s->mat[i];
        if (parent < 0) {
            memcpy(m, local, sizeof local);
            continue;
        }
        const float *a = s->mat[parent];
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++)
                m[row * 4 + col] = a[row * 4] * local[col] + a[row * 4 + 1] * local[4 + col] + a[row * 4 + 2] * local[8 + col];
            m[row * 4 + 3] = a[row * 4] * local[3] + a[row * 4 + 1] * local[7] + a[row * 4 + 2] * local[11] + a[row * 4 + 3];
        }
    }
    /* the vertices stay at rest: r3d moves each one with its bone while it
     * draws (rigid skinning, only the vertices the level of detail needs) */
}

static lmesh_t *skel_mesh(lua_State *L, int idx)
{
    lmesh_t *lm = luaL_checkudata(L, idx, MESH_MT);
    if (!lm->skel)
        luaL_error(L, "this mesh has no skeleton (make one with bm Animator)");
    return lm;
}

/* the clip named (or numbered, from 1) by argument idx */
static void find_clip(lua_State *L, const skel_t *s, int idx, bm_clip_t *c)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        if (bm_rig_clip(&s->r, (int)luaL_checkinteger(L, idx) - 1, c) == 0)
            return;
        luaL_error(L, "no animation number %d", (int)lua_tointeger(L, idx));
    }
    const char *name = luaL_checkstring(L, idx);
    for (int i = 0; bm_rig_clip(&s->r, i, c) == 0; i++)
        if (strcmp(c->name, name) == 0)
            return;
    luaL_error(L, "no animation \"%s\"", name);
}

/* the bone named (or numbered, from 1) by argument idx: 0-based, or -1 */
static int find_bone(lua_State *L, const bm_rig_t *r, int idx)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        int i = (int)lua_tointeger(L, idx) - 1;
        return i >= 0 && i < r->nbones ? i : -1;
    }
    const char *name = luaL_checkstring(L, idx);
    char bn[BM_MODEL_NAME + 1];
    for (int i = 0; i < r->nbones; i++) {
        bm_rig_bone(r, i, bn, NULL, NULL, NULL);
        if (strcmp(bn, name) == 0)
            return i;
    }
    return -1;
}

/* animate(mesh, [clip, time, [clip2, time2, k, [bone]]]) -> the clip's
 * length: the mesh (from model()) takes the pose of the clip at that time,
 * in seconds (a looping clip goes round); with a second clip, a mix of the
 * two (k = 0 the first, 1 the second), only for `bone` and the bones under
 * it if given (an upper body that shoots on legs that run); with no clip,
 * the rest pose */
int l_animate(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const int nb = lm->skel->r.nbones;
    float len = 0;
    if (lua_isnoneornil(L, 2)) {
        for (int i = 0; i < nb; i++) {
            pose_q[i][0] = pose_q[i][1] = pose_q[i][2] = 0;
            pose_q[i][3] = 1;
            pose_t[i][0] = pose_t[i][1] = pose_t[i][2] = 0;
        }
    } else {
        bm_clip_t c;
        find_clip(L, lm->skel, 2, &c);
        pose_at(nb, &c, (float)luaL_optnumber(L, 3, 0), pose_q, pose_t);
        len = c.length;
        if (!lua_isnoneornil(L, 4)) {
            bm_clip_t c2;
            find_clip(L, lm->skel, 4, &c2);
            pose_at(nb, &c2, (float)luaL_optnumber(L, 5, 0), pose_q2, pose_t2);
            float k = (float)luaL_optnumber(L, 6, 0.5);
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            uint8_t layer[BM_BONES_MAX];
            int top = lua_isnoneornil(L, 7) ? -1 : find_bone(L, &lm->skel->r, 7);
            if (!lua_isnoneornil(L, 7) && top < 0)
                return luaL_error(L, "no bone \"%s\"", lua_tostring(L, 7));
            for (int i = 0; i < nb; i++) {
                int parent;
                bm_rig_bone(&lm->skel->r, i, NULL, &parent, NULL, NULL);
                layer[i] = top < 0 || i == top || (parent >= 0 && layer[parent]);
            }
            for (int i = 0; i < nb; i++) {
                if (!layer[i])
                    continue;
                float a[4] = { pose_q[i][0], pose_q[i][1], pose_q[i][2], pose_q[i][3] };
                quat_slerp(pose_q[i], a, pose_q2[i], k);
                for (int j = 0; j < 3; j++) pose_t[i][j] += (pose_t2[i][j] - pose_t[i][j]) * k;
            }
        }
    }
    skin(lm, pose_q, pose_t);
    lua_pushnumber(L, len);
    return 1;
}

/* clips(mesh) -> { {name =, length =, loop =}, ... }: its animations */
int l_clips(lua_State *L)
{
    lmesh_t *lm = luaL_checkudata(L, 1, MESH_MT);
    lua_newtable(L);
    bm_clip_t c;
    for (int i = 0; lm->skel && bm_rig_clip(&lm->skel->r, i, &c) == 0; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, c.name);
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, c.length);
        lua_setfield(L, -2, "length");
        lua_pushboolean(L, c.loop);
        lua_setfield(L, -2, "loop");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* bone3d(mesh, name or number) -> x, y, z, tx, ty, tz: where the head and
 * the tail of the bone are in the mesh's last pose (its own coordinates, as
 * bounds3d), or nil */
int l_bone3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const bm_rig_t *r = &lm->skel->r;
    int found = find_bone(L, r, 2);
    if (found < 0) {
        lua_pushnil(L);
        return 1;
    }
    float h[3], t[3];
    bm_rig_bone(r, found, NULL, NULL, h, t);
    const float *m = lm->skel->mat[found];
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * h[0] + m[k * 4 + 1] * h[1] + m[k * 4 + 2] * h[2] + m[k * 4 + 3]);
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * t[0] + m[k * 4 + 1] * t[1] + m[k * 4 + 2] * t[2] + m[k * 4 + 3]);
    return 6;
}

/* bone_turn(mesh, bone, rx, ry, rz): every later animate() turns the bone by
 * these angles (radians, x then y then z, in its parent's frame) on top of
 * the animation: aiming up and down, a head that looks. bone_turn(mesh,
 * bone) takes it away. */
int l_bone_turn(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    skel_t *sk = lm->skel;
    int b = find_bone(L, &sk->r, 2);
    if (b < 0)
        return luaL_error(L, "no bone \"%s\"", lua_tostring(L, 2));
    if (lua_isnoneornil(L, 3)) {
        sk->turned[b] = 0;
        return 0;
    }
    float h[3] = { (float)luaL_checknumber(L, 3) * 0.5f, (float)luaL_optnumber(L, 4, 0) * 0.5f,
                   (float)luaL_optnumber(L, 5, 0) * 0.5f };
    float qx[4] = { sinf(h[0]), 0, 0, cosf(h[0]) }, qy[4] = { 0, sinf(h[1]), 0, cosf(h[1]) },
          qz[4] = { 0, 0, sinf(h[2]), cosf(h[2]) }, t[4];
    quat_mul(t, qy, qx);
    quat_mul(sk->turn[b], qz, t);
    sk->turned[b] = 1;
    return 0;
}

/* bones3d(mesh) -> { "name", ... }: the bones of its skeleton, in order */
int l_bones3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const bm_rig_t *r = &lm->skel->r;
    lua_createtable(L, r->nbones, 0);
    char bn[BM_MODEL_NAME + 1];
    for (int i = 0; i < r->nbones; i++) {
        bm_rig_bone(r, i, bn, NULL, NULL, NULL);
        lua_pushstring(L, bn);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* hit3d(mesh, x, y, z, ry, scale, ox, oy, oz, dx, dy, dz, [maxd]) -> t, bone:
 * the ray from o along d (any length; t in units of d) against the bones of
 * a mesh with a skeleton in its last pose, drawn at (x, y, z) turned by ry
 * around y and scaled: each bone is a capsule from its head to its tail,
 * wide enough for its vertices. The nearest hit within maxd (default 1000),
 * or nil. For hitboxes that follow the animation (a head shot: bone "head"). */
int l_hit3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const skel_t *sk = lm->skel;
    const float px = fnum(L, 2, 0), py = fnum(L, 3, 0), pz = fnum(L, 4, 0), ry = fnum(L, 5, 0),
                sc = fnum(L, 6, 1);
    float o[3] = { fnum(L, 7, 0), fnum(L, 8, 0), fnum(L, 9, 0) }, d[3] = { fnum(L, 10, 0), fnum(L, 11, 0), fnum(L, 12, 1) };
    const float maxd = fnum(L, 13, 1000);
    /* the ray into the model's own frame: undo the move, the turn and the scale */
    const float c = cosf(ry), s = sinf(ry), is = sc != 0 ? 1.0f / sc : 1;
    float ox = o[0] - px, oy = o[1] - py, oz = o[2] - pz;
    /* draw3d turns with Ry: x' = c x + s z, z' = -s x + c z; back: */
    float lo[3] = { (c * ox - s * oz) * is, oy * is, (s * ox + c * oz) * is };
    float ld[3] = { (c * d[0] - s * d[2]) * is, d[1] * is, (s * d[0] + c * d[2]) * is };
    float best = maxd;
    int hit = -1;
    for (int b = 0; b < sk->r.nbones; b++) {
        float rad = sk->radius[b];
        if (rad <= 0)
            continue;                       /* a bone with no vertices */
        float h[3], t[3], a[3], e[3];
        bm_rig_bone(&sk->r, b, NULL, NULL, h, t);
        const float *m = sk->mat[b];
        for (int k = 0; k < 3; k++) {
            a[k] = m[k * 4] * h[0] + m[k * 4 + 1] * h[1] + m[k * 4 + 2] * h[2] + m[k * 4 + 3];
            e[k] = m[k * 4] * t[0] + m[k * 4 + 1] * t[1] + m[k * 4 + 2] * t[2] + m[k * 4 + 3];
        }
        /* ray against capsule: sample the closest approach of the two lines */
        float u[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] }, w[3] = { lo[0] - a[0], lo[1] - a[1], lo[2] - a[2] };
        float A = ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2], B = ld[0] * u[0] + ld[1] * u[1] + ld[2] * u[2],
              C = u[0] * u[0] + u[1] * u[1] + u[2] * u[2], D = ld[0] * w[0] + ld[1] * w[1] + ld[2] * w[2],
              E = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
        float den = A * C - B * B, tr, tc;
        if (den > 1e-9f) {
            tr = (B * E - C * D) / den;
            tc = (A * E - B * D) / den;
        } else {
            tr = -D / (A > 1e-9f ? A : 1);
            tc = 0;
        }
        tc = tc < 0 ? 0 : tc > 1 ? 1 : tc;
        /* the point of the segment nearest the ray, then the sphere there */
        float q[3] = { a[0] + u[0] * tc - lo[0], a[1] + u[1] * tc - lo[1], a[2] + u[2] * tc - lo[2] };
        float qd = q[0] * ld[0] + q[1] * ld[1] + q[2] * ld[2];
        float qq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        float disc = qd * qd - A * (qq - rad * rad);
        (void)tr;
        if (disc < 0 || A <= 1e-12f)
            continue;
        float th = (qd - sqrtf(disc)) / A;
        if (th < 0)
            th = (qd + sqrtf(disc)) / A >= 0 ? 0 : -1;
        if (th >= 0 && th < best) {
            best = th;
            hit = b;
        }
    }
    if (hit < 0) {
        lua_pushnil(L);
        return 1;
    }
    char bn[BM_MODEL_NAME + 1];
    bm_rig_bone(&sk->r, hit, bn, NULL, NULL, NULL);
    lua_pushnumber(L, best);
    lua_pushstring(L, bn);
    return 2;
}

/* bounds3d(mesh) -> x0, y0, z0, x1, y1, z1: the box around its vertices, in
 * its own coordinates (before draw3d moves, turns and scales it) */
static v3_t posed(const r3d_mesh_t *m, int i)
{
    v3_t p = m->verts[i];
    if (!m->bones)
        return p;
    const float *b = m->bones[m->vbone[i]];
    return (v3_t){ b[0] * p.x + b[1] * p.y + b[2] * p.z + b[3], b[4] * p.x + b[5] * p.y + b[6] * p.z + b[7],
                   b[8] * p.x + b[9] * p.y + b[10] * p.z + b[11] };
}

int l_bounds3d(lua_State *L)
{
    const r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t lo = posed(m, 0), hi = lo;
    for (int i = 1; i < m->nverts; i++) {
        v3_t p = posed(m, i);
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z > hi.z) hi.z = p.z;
    }
    const float v[6] = { lo.x, lo.y, lo.z, hi.x, hi.y, hi.z };
    for (int i = 0; i < 6; i++)
        lua_pushnumber(L, v[i]);
    return 6;
}

int l_mesh_sphere(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_sphere(m, oval(L, 1, 8), oval(L, 2, 16),
                        (uint32_t)luaL_optinteger(L, 3, 0xFFFFFF), (uint32_t)luaL_optinteger(L, 4, 0xC0C0C0)) != 0)
        return luaL_error(L, "cannot build the sphere");
    return 1;
}

int l_mesh_cube(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_cube(m, (uint32_t)luaL_optinteger(L, 1, 0xFFFFFF)) != 0)
        return luaL_error(L, "cannot build the cube");
    return 1;
}

/* the time of a 3D call from t0: stat(6)'s (since zclear) and the dev
 * kit's (never reset: the overlay takes its differences) */
static void us3d_add(uint32_t t0)
{
    const uint32_t us = timer_ticks() - t0;
    rt.us3d += us;
    rt.us3d_all += us;
}

/* draw3d(mesh, x, y, z [, rx, ry, rz, scale, flags]) - flags: 1 no z-buffer
 * (floors and backdrops drawn first), 2 unlit (full colour), 4 smooth
 * (Gouraud) */
int l_draw3d(lua_State *L)
{
    r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t p = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) };
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    r3d_draw_flags(r, m, p, fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0), fnum(L, 8, 1),
                   (unsigned)luaL_optinteger(L, 9, 0));
    us3d_add(t0);
    return 0;
}

/* cel3d([bands, [floor, outline_rgb, outline_width]]) -> bands, floor, outline_rgb, outline_width:
 * cuts the sun light of draw3d calls with flag 256 into 2..8 bands (0 or false off, the default);
 * floor 0..1 (default 0.35) is the sun term of the darkest band; the outline values (rgb or
 * nil/false none, width 0.005..0.05, default 0.02) are used by flag 512 (the inverted hull).
 * No argument: only the values in force. State of the cart: it resets when the cart ends. */
int l_cel3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    r3d_flush(r);                       /* (the draws held keep the cel values they were made with) */
    if (lua_gettop(L) > 0) {
        int bands = lua_isboolean(L, 1) || lua_isnil(L, 1) ? 0 : (int)luaL_checkinteger(L, 1);
        r3d_cel(r, bands, fnum(L, 2, 0.35f));
        r->cel_rgb = lua_isnoneornil(L, 3) || lua_isboolean(L, 3) ? R3D_CEL_NO_OUTLINE
                                                                   : (uint32_t)luaL_checkinteger(L, 3) & 0xFFFFFF;
        float w = fnum(L, 4, 0.02f);
        r->cel_width = w < 0.005f ? 0.005f : w > 0.05f ? 0.05f : w;
    }
    lua_pushinteger(L, r->cel_bands);
    lua_pushnumber(L, r->cel_floor);
    if (r->cel_rgb == R3D_CEL_NO_OUTLINE)
        lua_pushboolean(L, 0);
    else
        lua_pushinteger(L, r->cel_rgb);
    lua_pushnumber(L, r->cel_width);
    return 4;
}

/* dither3d([on]) -> the old state: the ARM raster's 4x4 dither of smooth faces (default on).
 * The GPU ignores it. */
int l_dither3d(lua_State *L)
{
    r3d(L);
    const int old = r3d_dither(1);
    r3d_dither(lua_isnoneornil(L, 1) ? old : lua_toboolean(L, 1));
    lua_pushboolean(L, old);
    return 1;
}

/* sky3d(sun, sky, ground): colours (0xRRGGBB) of the sunlight and of the
 * ambient light from above and from below; sky3d() back to white */
int l_sky3d(lua_State *L)
{
    r3d_sky(r3d(L), (uint32_t)luaL_optinteger(L, 1, 0xFFFFFF), (uint32_t)luaL_optinteger(L, 2, 0xFFFFFF),
            (uint32_t)luaL_optinteger(L, 3, 0xFFFFFF));
    return 0;
}

/* shine3d(spec, exponent, rim): highlights on glossy faces and rim light */
int l_shine3d(lua_State *L)
{
    r3d_shine(r3d(L), fnum(L, 1, 0.6f), (int)luaL_optinteger(L, 2, 16), fnum(L, 3, 0));
    return 0;
}

/* shadow3d(style): 0 darkens (default), 1 a dithered black (no reads of the
 * screen) */
int l_shadow3d(lua_State *L)
{
    r3d(L)->shadow_style = (int)luaL_optinteger(L, 1, 0);
    return 0;
}

/* textures3d([on]) -> on: with false every draw3d draws the textured faces as
 * flat colours (each the average of its texels), as with the flag 128 on
 * each; textures3d(true) the textures (the default) */
int l_textures3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    r3d_flush(r);
    if (!lua_isnoneornil(L, 1))
        r->notex = !lua_toboolean(L, 1);
    lua_pushboolean(L, !r->notex);
    return 1;
}

/* point3d(x, y, z, radius, colour, [flags]) -> pixels: a round point of
 * world radius, behind what is nearer; flags 1 = every other pixel */
int l_point3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_point(r, (v3_t){ fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) }, fnum(L, 4, 0.1f),
                           (uint32_t)luaL_optinteger(L, 5, 0xFFFFFF), (unsigned)luaL_optinteger(L, 6, 0));
    us3d_add(t0);
    lua_pushinteger(L, n);
    return 1;
}

/* line3d(x0, y0, z0, x1, y1, z1, colour, [width, flags]) -> pixels */
int l_line3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_line(r, (v3_t){ fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) },
                          (v3_t){ fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 0) },
                          (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF), (int)luaL_optinteger(L, 8, 1),
                          (unsigned)luaL_optinteger(L, 9, 0));
    us3d_add(t0);
    lua_pushinteger(L, n);
    return 1;
}

/* sprite3d(sx, sy, sw, sh, x, y, z, size, [flags]) -> pixels: a rectangle of
 * the sprite sheet facing the camera, `size` world units wide */
int l_sprite3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    sheet_commit();
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_sprite(r, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4),
                            (v3_t){ fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0) }, fnum(L, 8, 1),
                            (unsigned)luaL_optinteger(L, 9, 0));
    us3d_add(t0);
    lua_pushinteger(L, n);
    return 1;
}

/* particles([max=1024]) -> cap: alloc or replace the 3D particle pool */
int l_particles(lua_State *L)
{
    if (rt.pfx) {
        r3d_pfx_free(rt.pfx);
        rt.pfx = NULL;
    }
    int cap = (int)luaL_optinteger(L, 1, 1024);
    rt.pfx = r3d_pfx_new(cap);
    lua_pushinteger(L, rt.pfx ? rt.pfx->cap : 0);
    return 1;
}

/* particles_add(x, y, z, vx, vy, vz, life, [rgb=0xFFFFFF]) -> slot or nil */
int l_particles_add(lua_State *L)
{
    if (!rt.pfx)
        return 0;
    v3_t pos = { fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) };
    v3_t vel = { fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 0) };
    float life = fnum(L, 7, 0);
    uint32_t rgb = (uint32_t)luaL_optinteger(L, 8, 0xFFFFFF);
    int slot = r3d_pfx_add(rt.pfx, pos, vel, life, rgb);
    lua_pushinteger(L, slot);
    return 1;
}

/* particles_step([gravity=0.0]) */
int l_particles_step(lua_State *L)
{
    if (rt.pfx) {
        float gravity = fnum(L, 1, 0.0f);
        r3d_pfx_step(rt.pfx, gravity);
    }
    return 0;
}

/* particles_draw([radius=0.1]) -> pixels */
int l_particles_draw(lua_State *L)
{
    if (!rt.pfx) {
        lua_pushinteger(L, 0);
        return 1;
    }
    r3d_t *r = r3d(L);
    settle2d();
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_pfx_draw(r, rt.pfx, fnum(L, 1, 0.1f));
    us3d_add(t0);
    lua_pushinteger(L, n);
    return 1;
}

/* particles_count() -> count */
int l_particles_count(lua_State *L)
{
    lua_pushinteger(L, rt.pfx ? r3d_pfx_live(rt.pfx) : 0);
    return 1;
}

/* camera3d(x, y, z [, yaw, pitch, fov, roll]) */
int l_camera3d(lua_State *L)
{
    r3d_camera(r3d(L), fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, -5), fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 60));
    r3d_camera_roll(r3d(L), fnum(L, 7, 0));
    return 0;
}

/* fog3d(colour, near, far): faces fade into the colour with the distance;
 * fog3d() turns it off */
int l_fog3d(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        r3d_fog(r3d(L), 0, 0, 0);
    else
        r3d_fog(r3d(L), (uint32_t)luaL_checkinteger(L, 1), fnum(L, 2, 10), fnum(L, 3, 100));
    return 0;
}

/* bm3d 5.5: the precomputed visibility of a map (pvs3d): a grid of cells on
 * the ground, for each the pieces of the map seen from it (their numbers,
 * from 1), and the pieces' boxes on the ground; one block of memory */
struct pvs {
    float x0, z0, cell, max_y;
    int nx, nz, npieces;
    float *box;                         /* x0 z0 x1 z1 a piece */
    uint32_t *at;                       /* nx * nz + 1: where each cell's pieces start in ids */
    uint8_t *ids;
};

/* pvs3d{x0 =, z0 =, cell =, nx =, nz =, max_y =, sets = {...}, boxes = {...}}:
 * sets has a string a cell (row by row along x), each byte a piece seen from
 * it; boxes six numbers a piece (x0 y0 z0 x1 y1 z1). pvs3d() forgets it. */
int l_pvs3d(lua_State *L)
{
    free(rt.pvs);
    rt.pvs = NULL;
    if (lua_isnoneornil(L, 1))
        return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    float f[4];
    int n[2];
    static const char *const fk[4] = { "x0", "z0", "cell", "max_y" }, *const nk[2] = { "nx", "nz" };
    for (int i = 0; i < 4; i++) {
        lua_getfield(L, 1, fk[i]);
        f[i] = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    for (int i = 0; i < 2; i++) {
        lua_getfield(L, 1, nk[i]);
        n[i] = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    if (n[0] <= 0 || n[1] <= 0 || n[0] > 4096 || n[1] > 4096 || n[0] * n[1] > 1 << 16 || !(f[2] > 0))
        return luaL_error(L, "pvs3d: a grid of %d x %d cells of %f", n[0], n[1], (double)f[2]);
    lua_getfield(L, 1, "sets");
    lua_getfield(L, 1, "boxes");
    luaL_checktype(L, -2, LUA_TTABLE);
    luaL_checktype(L, -1, LUA_TTABLE);
    const int cells = n[0] * n[1], np = (int)(lua_rawlen(L, -1) / 6);
    size_t ids = 0;
    for (int i = 0; i < cells; i++) {
        lua_rawgeti(L, -2, i + 1);
        size_t len = 0;
        if (lua_type(L, -1) == LUA_TSTRING)
            lua_tolstring(L, -1, &len);
        ids += len;
        lua_pop(L, 1);
    }
    struct pvs *p = malloc(sizeof *p + (size_t)np * 4 * sizeof(float) + (size_t)(cells + 1) * 4 + ids);
    if (!p)
        return luaL_error(L, "pvs3d: no memory");
    p->x0 = f[0]; p->z0 = f[1]; p->cell = f[2]; p->max_y = f[3];
    p->nx = n[0]; p->nz = n[1]; p->npieces = np;
    p->box = (float *)(p + 1);
    p->at = (uint32_t *)(p->box + 4 * np);
    p->ids = (uint8_t *)(p->at + cells + 1);
    for (int i = 0; i < np; i++) {
        static const int k[4] = { 1, 3, 4, 6 };     /* x0 z0 x1 z1 of x0 y0 z0 x1 y1 z1 */
        for (int j = 0; j < 4; j++) {
            lua_rawgeti(L, -1, i * 6 + k[j]);
            p->box[i * 4 + j] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    uint32_t at = 0;
    for (int i = 0; i < cells; i++) {
        p->at[i] = at;
        lua_rawgeti(L, -2, i + 1);
        size_t len = 0;
        const char *str = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &len) : NULL;
        if (str)
            memcpy(p->ids + at, str, len);
        at += (uint32_t)len;
        lua_pop(L, 1);
    }
    p->at[cells] = at;
    lua_pop(L, 2);
    rt.pvs = p;
    return 0;
}

/* piece i's box on the ground reaches the disc (x, z, r) */
static int pvs_touches(const struct pvs *p, int i, float x, float z, float r)
{
    const float *b = p->box + 4 * i;
    const float dx = x < b[0] ? b[0] - x : x > b[2] ? x - b[2] : 0, dz = z < b[1] ? b[1] - z : z > b[3] ? z - b[3] : 0;
    return dx * dx + dz * dz <= r * r;
}

/* visible3d(x, y, z, r) -> false if a sphere cannot be seen: out of the
 * camera's view, or (with pvs3d) on pieces of the map the camera's cell
 * does not see. Off the map's pieces: seen. */
int l_visible3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    const v3_t c = { fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) };
    const float rad = fnum(L, 4, 1);
    int seen = r3d_visible(r, c, rad);
    const struct pvs *p = rt.pvs;
    if (seen && p && r->cam_pos.y < p->max_y) {
        const int i = (int)floorf((r->cam_pos.x - p->x0) / p->cell), j = (int)floorf((r->cam_pos.z - p->z0) / p->cell);
        if (i >= 0 && i < p->nx && j >= 0 && j < p->nz) {
            const uint32_t a = p->at[j * p->nx + i], b = p->at[j * p->nx + i + 1];
            int in_seen = 0, on_map = 0;
            for (uint32_t k = a; k < b && !in_seen; k++)
                in_seen = p->ids[k] >= 1 && p->ids[k] <= p->npieces && pvs_touches(p, p->ids[k] - 1, c.x, c.z, rad);
            for (int k = 0; k < p->npieces && !in_seen && !on_map; k++)
                on_map = pvs_touches(p, k, c.x, c.z, rad);
            seen = in_seen || !on_map;
        }
    }
    lua_pushboolean(L, seen);
    return 1;
}

/* project3d(x, y, z) -> screen x, y and depth, or nil behind the camera */
int l_project3d(lua_State *L)
{
    float sx, sy, d;
    v3_t p = { fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) };
    if (!r3d_project(r3d(L), p, &sx, &sy, &d)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, sx + rt.g.cam_x);
    lua_pushnumber(L, sy + rt.g.cam_y);
    lua_pushnumber(L, d);
    return 3;
}

/* light3d(x, y, z [, ambient]) - direction towards the light */
int l_light3d(lua_State *L)
{
    r3d_light(r3d(L), fnum(L, 1, 0), fnum(L, 2, 1), fnum(L, 3, 0), fnum(L, 4, 0.25f));
    return 0;
}

/* lamp3d(i, x, y, z, radius [, k]) - point light i (1-4) that brightens the
 * faces near it by up to k (default 1); lamp3d(i) or lamp3d() turns it (them) off */
int l_lamp3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    if (lua_isnoneornil(L, 1)) {
        for (int i = 0; i < R3D_LAMPS; i++)
            r3d_lamp(r, i, 0, 0, 0, 0, 0);
        return 0;
    }
    int i = ival(L, 1) - 1;
    luaL_argcheck(L, i >= 0 && i < R3D_LAMPS, 1, "lamp 1 to 4");
    if (lua_isnoneornil(L, 2))
        r3d_lamp(r, i, 0, 0, 0, 0, 0);
    else
        r3d_lamp_rgb(r, i, fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0), fnum(L, 5, 3), fnum(L, 6, 1),
                     (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF));
    return 0;
}

int l_zclear(lua_State *L)
{
    r3d_t *r = r3d(L);
    rt.zclear_seen = 1;
    rt.us3d = 0;
    if (rt.zclear_dma) {
        zclear_dma_wait(1);
        r->tris_in = r->tris_drawn = r->pixels = r->verts = r->impostors = 0;
        return 0;
    }
    r3d_zclear(r);
    return 0;
}

/* gpu3d([on, [aa, [vs, [queue, [impostor, [later]]]]]]) -> on, aa, vs, version, queue, impostor, later, probe:
 * whether the GPU draws the 3D, whether with anti-aliasing (MSAA 4x, where the GPU allows it) and whether
 * its vertex shader places the corners of the models (bm3d 3.0, where the GPU's
 * probe drew with it): false, 1 the scenery (meshes unlit or with baked
 * light), 2 (or true) every model; version: the bm3d version that reproduces
 * (src/gpu/version3d.h: "0.2" the ARM, "2.1", "3.0", "3.4", "4.1"). With on, the 3D
 * goes to the GPU (if the console has one that answers) or to the ARM from
 * here, whatever Settings > Graphics says: for benchmarks; switch between
 * frames (what was drawn so far in a frame is not in the other's depth).
 * queue (bm3d 4.1): the end of a frame's 3D started on the GPU while the next
 * _update runs (where the GPU's probe saw it work); 2: two jobs in flight
 * (bm3d 5.1); true: the console's own level (gpu3d_queue in Settings, 1 or 2),
 * false 0; returned fifth as the level in force (1, 2) or false.
 * impostor: a mesh whose box on the screen is at most that many pixels (2..64) of a 1080-line picture
 * (N * h / 1080 of the game's screen: 8 is 2.7 at 360 lines) is drawn as one quad in its average colour, by the GPU or
 * the ARM; false or 0 off (the default, or r3d_impostor in bm/config.txt);
 * returned sixth. later (bm3d 6.9): each frame shown a frame later, so
 * the next _update and _draw run while the GPU draws it (with queue 2; not
 * online); returned seventh. probe (eighth): the level the console's boot test saw work
 * (0 or false: none, 1 meshes unlit or with baked light, 2 also lit models), whatever vs asks for:
 * vs 2 asked on a console whose probe saw 1 draws the lit models one triangle at a time. */
int l_gpu3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    r3d_flush(r);
    if (!lua_isnoneornil(L, 1)) {
        const int on = lua_toboolean(L, 1);
        if (on && !r->backend && rt.g.stride == (uint32_t)rt.g.w && gpu3d_init() == 0 && !gpu3d_failed()) {
            gpu3d_drop();
            gpu3d_page(0, 0);
            r->backend = gpu3d_backend();
            r->arm_hook = gpu3d_to_arm;
            r->merge = merge_cfg();
        } else if (!on && r->backend) {
            flush3d(0);
            cls_settle();
            gpu3d_drop();
            r->backend = NULL;
            zclear_dma_wait(1);
            memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
        }
        if (!lua_isnoneornil(L, 2))
            gpu3d_set_msaa(lua_toboolean(L, 2));
        if (!lua_isnoneornil(L, 3))
            gpu3d_set_vshader(lua_isboolean(L, 3) ? 2 * lua_toboolean(L, 3) : (int)luaL_checkinteger(L, 3));
        if (!lua_isnoneornil(L, 4)) {           /* true: the console's own level (Settings), not 1 */
            const char *cq = config_get("gpu3d_queue");
            gpu3d_set_queue(lua_isinteger(L, 4) ? (int)lua_tointeger(L, 4)
                            : !lua_toboolean(L, 4) ? 0 : cq && cq[0] == '2' ? 2 : 1);
        }
        if (!lua_isnoneornil(L, 6))
            rt.later = lua_toboolean(L, 6);
    }
    if (!lua_isnoneornil(L, 5))         /* also with on nil: only the impostors */
        r->impostor = lua_isboolean(L, 5) ? 0 : impostor_px((int)luaL_checkinteger(L, 5));
    const int gpu = r->backend != NULL, later = gpu && rt.later && gpu3d_queue2();
    lua_pushboolean(L, gpu);
    lua_pushboolean(L, gpu && gpu3d_msaa_on());
    const int vs = gpu ? gpu3d_vshader_on() : 0;
    if (vs)
        lua_pushinteger(L, vs);
    else
        lua_pushboolean(L, 0);
    lua_pushstring(L, bm3d_mode_l(gpu, vs, gpu && gpu3d_queue(), later));     /* reproduced */
    if (gpu && gpu3d_queue())           /* the level (1, 2), or false: as vs */
        lua_pushinteger(L, gpu3d_queue2() ? 2 : 1);
    else
        lua_pushboolean(L, 0);
    lua_pushinteger(L, r->impostor);
    lua_pushboolean(L, later);
    const int probe = gpu ? gpu3d_vshader() : 0;     /* what the boot test saw, not what was asked */
    if (probe)
        lua_pushinteger(L, probe);
    else
        lua_pushboolean(L, 0);
    return 8;
}
