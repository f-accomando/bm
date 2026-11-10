/*
 * phys2d.c: Original compact bare-metal 2D rigid body impulse physics engine.
 * Supports circles, OBB boxes, and convex polygons (<= 8 vertices), SAT with
 * 2-point edge clipping, Coulomb friction, restitution, and distance/revolute/weld joints.
 */
#include "phys2d.h"
#include <math.h>
#include <string.h>

typedef struct {
    int a, b;
    float nx, ny;
    float depth[2];
    float px[2], py[2];
    int count;
    float jn[2], jt[2];
    float restitution;
    float friction;
} manifold_t;

static inline void rot2(float c, float s, float vx, float vy, float *ox, float *oy)
{
    *ox = c * vx - s * vy;
    *oy = s * vx + c * vy;
}

static inline void inv_rot2(float c, float s, float vx, float vy, float *ox, float *oy)
{
    *ox =  c * vx + s * vy;
    *oy = -s * vx + c * vy;
}

static phys_body_t *get_body(phys_world_t *w, int id)
{
    if (!w || id < 0 || id >= PHYS_MAX_BODIES) return NULL;
    return w->bodies[id].active ? &w->bodies[id] : NULL;
}

void phys_world_init(phys_world_t *w, float gx, float gy)
{
    memset(w, 0, sizeof *w);
    w->gx = gx;
    w->gy = gy;
    for (int i = 0; i < PHYS_MAX_BODIES; i++)
        w->bodies[i].id = i;
}

static int alloc_body(phys_world_t *w, phys_body_type_t type, float x, float y)
{
    if (!w) return -1;
    for (int i = 0; i < PHYS_MAX_BODIES; i++) {
        if (!w->bodies[i].active) {
            phys_body_t *b = &w->bodies[i];
            memset(b, 0, sizeof *b);
            b->active = 1;
            b->id = i;
            b->type = type;
            b->x = x;
            b->y = y;
            b->restitution = 0.2f;
            b->friction = 0.4f;
            b->linear_damping = 0.01f;
            b->angular_damping = 0.02f;
            if (i >= w->nbodies) w->nbodies = i + 1;
            return i;
        }
    }
    return -1;
}

static void set_mass(phys_body_t *b, phys_body_type_t type, float mass, float inertia)
{
    if (type != PHYS_DYNAMIC || mass <= 0.0f) {
        b->mass = 0.0f; b->inv_mass = 0.0f;
        b->inertia = 0.0f; b->inv_inertia = 0.0f;
    } else {
        b->mass = mass;
        b->inv_mass = 1.0f / mass;
        b->inertia = inertia > 0.0f ? inertia : mass;
        b->inv_inertia = 1.0f / b->inertia;
    }
}

int phys_add_circle(phys_world_t *w, phys_body_type_t type, float x, float y, float radius, float density)
{
    if (radius <= 0.0f) radius = 1.0f;
    if (density <= 0.0f) density = 1.0f;
    int id = alloc_body(w, type, x, y);
    if (id < 0) return -1;
    phys_body_t *b = &w->bodies[id];
    b->shape = PHYS_SHAPE_CIRCLE;
    b->radius = radius;
    float area = 3.14159265f * radius * radius;
    float mass = area * density;
    float inertia = 0.5f * mass * radius * radius;
    set_mass(b, type, mass, inertia);
    return id;
}

static void compute_poly_normals(phys_body_t *b)
{
    for (int i = 0; i < b->nverts; i++) {
        int j = (i + 1) % b->nverts;
        float ex = b->verts[j * 2] - b->verts[i * 2];
        float ey = b->verts[j * 2 + 1] - b->verts[i * 2 + 1];
        /* Outward normal for CCW polygon: (ey, -ex) normalized */
        float nx = ey, ny = -ex;
        float len = sqrtf(nx * nx + ny * ny);
        if (len > 1e-6f) { nx /= len; ny /= len; }
        b->normals[i * 2] = nx;
        b->normals[i * 2 + 1] = ny;
    }
}

int phys_add_box(phys_world_t *w, phys_body_type_t type, float x, float y, float bw, float bh, float density)
{
    if (bw <= 0.0f) bw = 1.0f;
    if (bh <= 0.0f) bh = 1.0f;
    if (density <= 0.0f) density = 1.0f;
    int id = alloc_body(w, type, x, y);
    if (id < 0) return -1;
    phys_body_t *b = &w->bodies[id];
    b->shape = PHYS_SHAPE_BOX;
    b->w = bw; b->h = bh;
    b->nverts = 4;
    float hw = bw * 0.5f, hh = bh * 0.5f;
    b->verts[0] = -hw; b->verts[1] = -hh;
    b->verts[2] =  hw; b->verts[3] = -hh;
    b->verts[4] =  hw; b->verts[5] =  hh;
    b->verts[6] = -hw; b->verts[7] =  hh;
    compute_poly_normals(b);
    float mass = bw * bh * density;
    float inertia = mass * (bw * bw + bh * bh) / 12.0f;
    set_mass(b, type, mass, inertia);
    return id;
}

int phys_add_poly(phys_world_t *w, phys_body_type_t type, float x, float y, const float *pts, int nverts, float density)
{
    if (!pts || nverts < 3) return -1;
    if (nverts > PHYS_MAX_VERTS) nverts = PHYS_MAX_VERTS;
    if (density <= 0.0f) density = 1.0f;
    int id = alloc_body(w, type, x, y);
    if (id < 0) return -1;
    phys_body_t *b = &w->bodies[id];
    b->shape = PHYS_SHAPE_POLY;
    b->nverts = nverts;

    /* Ensure CCW winding and recenter around centroid */
    float area2 = 0.0f, cx = 0.0f, cy = 0.0f, numer_i = 0.0f;
    for (int i = 0; i < nverts; i++) {
        int j = (i + 1) % nverts;
        float x0 = pts[i * 2], y0 = pts[i * 2 + 1];
        float x1 = pts[j * 2], y1 = pts[j * 2 + 1];
        float cr = x0 * y1 - x1 * y0;
        area2 += cr;
        cx += (x0 + x1) * cr;
        cy += (y0 + y1) * cr;
    }
    if (fabsf(area2) > 1e-6f) {
        cx /= (3.0f * area2);
        cy /= (3.0f * area2);
    }
    if (area2 >= 0.0f) {
        for (int i = 0; i < nverts; i++) {
            b->verts[i * 2]     = pts[i * 2] - cx;
            b->verts[i * 2 + 1] = pts[i * 2 + 1] - cy;
        }
    } else {
        for (int i = 0; i < nverts; i++) {
            int r = nverts - 1 - i;
            b->verts[i * 2]     = pts[r * 2] - cx;
            b->verts[i * 2 + 1] = pts[r * 2 + 1] - cy;
        }
    }
    area2 = fabsf(area2);
    for (int i = 0; i < nverts; i++) {
        int j = (i + 1) % nverts;
        float x0 = b->verts[i * 2], y0 = b->verts[i * 2 + 1];
        float x1 = b->verts[j * 2], y1 = b->verts[j * 2 + 1];
        float cr = fabsf(x0 * y1 - x1 * y0);
        numer_i += cr * (x0 * x0 + y0 * y0 + x0 * x1 + y0 * y1 + x1 * x1 + y1 * y1);
    }
    float mass = 0.5f * area2 * density;
    float inertia = (area2 > 1e-6f) ? (density / 12.0f) * numer_i : mass * 10.0f;
    compute_poly_normals(b);
    set_mass(b, type, mass, inertia);
    return id;
}

void phys_remove_body(phys_world_t *w, int id)
{
    phys_body_t *b = get_body(w, id);
    if (!b) return;
    b->active = 0;
    for (int i = 0; i < PHYS_MAX_JOINTS; i++) {
        if (w->joints[i].active && (w->joints[i].a == id || w->joints[i].b == id))
            w->joints[i].active = 0;
    }
}

static void world_to_local(const phys_body_t *b, float wx, float wy, float *lx, float *ly)
{
    inv_rot2(cosf(b->angle), sinf(b->angle), wx - b->x, wy - b->y, lx, ly);
}

static void local_to_world(const phys_body_t *b, float lx, float ly, float *wx, float *wy)
{
    float rx, ry;
    rot2(cosf(b->angle), sinf(b->angle), lx, ly, &rx, &ry);
    *wx = b->x + rx;
    *wy = b->y + ry;
}

static int alloc_joint(phys_world_t *w, phys_joint_type_t type, int a, int b)
{
    if (!get_body(w, a) || !get_body(w, b) || a == b) return -1;
    for (int i = 0; i < PHYS_MAX_JOINTS; i++) {
        if (!w->joints[i].active) {
            phys_joint_t *j = &w->joints[i];
            memset(j, 0, sizeof *j);
            j->active = 1;
            j->type = type;
            j->a = a;
            j->b = b;
            if (i >= w->njoints) w->njoints = i + 1;
            return i;
        }
    }
    return -1;
}

int phys_add_joint_distance(phys_world_t *w, int a, int b, float wx1, float wy1, float wx2, float wy2, float len)
{
    int id = alloc_joint(w, PHYS_JOINT_DISTANCE, a, b);
    if (id < 0) return -1;
    phys_joint_t *j = &w->joints[id];
    const phys_body_t *ba = &w->bodies[a], *bb = &w->bodies[b];
    world_to_local(ba, wx1, wy1, &j->lx1, &j->ly1);
    world_to_local(bb, wx2, wy2, &j->lx2, &j->ly2);
    if (len <= 0.0f) {
        float dx = wx2 - wx1, dy = wy2 - wy1;
        len = sqrtf(dx * dx + dy * dy);
    }
    j->rest_len = len;
    return id;
}

int phys_add_joint_revolute(phys_world_t *w, int a, int b, float wx, float wy)
{
    int id = alloc_joint(w, PHYS_JOINT_REVOLUTE, a, b);
    if (id < 0) return -1;
    phys_joint_t *j = &w->joints[id];
    world_to_local(&w->bodies[a], wx, wy, &j->lx1, &j->ly1);
    world_to_local(&w->bodies[b], wx, wy, &j->lx2, &j->ly2);
    return id;
}

int phys_add_joint_weld(phys_world_t *w, int a, int b, float wx, float wy)
{
    int id = alloc_joint(w, PHYS_JOINT_WELD, a, b);
    if (id < 0) return -1;
    phys_joint_t *j = &w->joints[id];
    world_to_local(&w->bodies[a], wx, wy, &j->lx1, &j->ly1);
    world_to_local(&w->bodies[b], wx, wy, &j->lx2, &j->ly2);
    j->ref_angle = w->bodies[b].angle - w->bodies[a].angle;
    return id;
}

void phys_apply_force(phys_world_t *w, int id, float fx, float fy, float px, float py, int has_pt)
{
    phys_body_t *b = get_body(w, id);
    if (!b || b->type != PHYS_DYNAMIC) return;
    b->fx += fx;
    b->fy += fy;
    if (has_pt)
        b->torque += (px - b->x) * fy - (py - b->y) * fx;
}

void phys_apply_impulse(phys_world_t *w, int id, float ix, float iy, float px, float py, int has_pt)
{
    phys_body_t *b = get_body(w, id);
    if (!b || b->type != PHYS_DYNAMIC) return;
    b->vx += ix * b->inv_mass;
    b->vy += iy * b->inv_mass;
    if (has_pt)
        b->av += ((px - b->x) * iy - (py - b->y) * ix) * b->inv_inertia;
}

/* ---- Narrowphase Collision Detection ---- */

static int collide_circle_circle(const phys_body_t *a, const phys_body_t *b, manifold_t *m)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    float rsum = a->radius + b->radius;
    float d2 = dx * dx + dy * dy;
    if (d2 >= rsum * rsum) return 0;

    float d = sqrtf(d2);
    m->count = 1;
    if (d > 1e-6f) {
        m->nx = dx / d;
        m->ny = dy / d;
        m->depth[0] = rsum - d;
        m->px[0] = a->x + m->nx * (a->radius - m->depth[0] * 0.5f);
        m->py[0] = a->y + m->ny * (a->radius - m->depth[0] * 0.5f);
    } else {
        m->nx = 0.0f; m->ny = 1.0f;
        m->depth[0] = rsum;
        m->px[0] = a->x; m->py[0] = a->y;
    }
    return 1;
}

static int collide_circle_poly(const phys_body_t *c, const phys_body_t *p, int flip_ab, manifold_t *m)
{
    float cp = cosf(p->angle), sp = sinf(p->angle);
    float cx, cy;
    inv_rot2(cp, sp, c->x - p->x, c->y - p->y, &cx, &cy);

    float sep = -1e30f;
    int face = 0;
    for (int i = 0; i < p->nverts; i++) {
        float s = p->normals[i * 2] * (cx - p->verts[i * 2]) +
                  p->normals[i * 2 + 1] * (cy - p->verts[i * 2 + 1]);
        if (s > c->radius) return 0;
        if (s > sep) { sep = s; face = i; }
    }

    int i1 = face;
    int i2 = (face + 1) % p->nverts;
    float v1x = p->verts[i1 * 2], v1y = p->verts[i1 * 2 + 1];
    float v2x = p->verts[i2 * 2], v2y = p->verts[i2 * 2 + 1];

    float lnx, lny, lpx, lpy, depth;
    if (sep < 1e-6f) {
        /* Circle center is inside polygon */
        lnx = -p->normals[face * 2];
        lny = -p->normals[face * 2 + 1];
        depth = c->radius - sep;
        lpx = cx; lpy = cy;
    } else {
        float ex = v2x - v1x, ey = v2y - v1y;
        float elen2 = ex * ex + ey * ey;
        float t = elen2 > 1e-8f ? ((cx - v1x) * ex + (cy - v1y) * ey) / elen2 : 0.0f;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        float qx = v1x + ex * t, qy = v1y + ey * t;
        float dx = qx - cx, dy = qy - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= c->radius * c->radius) return 0;
        float d = sqrtf(d2);
        if (d > 1e-6f) {
            lnx = dx / d; lny = dy / d;
            depth = c->radius - d;
        } else {
            lnx = -p->normals[face * 2]; lny = -p->normals[face * 2 + 1];
            depth = c->radius;
        }
        lpx = qx; lpy = qy;
    }

    float wnx, wny, wpx, wpy;
    rot2(cp, sp, lnx, lny, &wnx, &wny);
    rot2(cp, sp, lpx, lpy, &wpx, &wpy);
    if (flip_ab) { wnx = -wnx; wny = -wny; }

    m->nx = wnx; m->ny = wny;
    m->px[0] = p->x + wpx; m->py[0] = p->y + wpy;
    m->depth[0] = depth;
    m->count = 1;
    return 1;
}

static float sat_axis_sep(const phys_body_t *a, const phys_body_t *b, int *best_face)
{
    float ca = cosf(a->angle), sa = sinf(a->angle);
    float cb = cosf(b->angle), sb = sinf(b->angle);
    float max_sep = -1e30f;
    *best_face = 0;

    for (int i = 0; i < a->nverts; i++) {
        float nx, ny, vx, vy;
        rot2(ca, sa, a->normals[i * 2], a->normals[i * 2 + 1], &nx, &ny);
        rot2(ca, sa, a->verts[i * 2], a->verts[i * 2 + 1], &vx, &vy);
        vx += a->x; vy += a->y;

        /* Most extreme vertex of B in direction (-nx, -ny) */
        float inv_nx, inv_ny;
        inv_rot2(cb, sb, -nx, -ny, &inv_nx, &inv_ny);
        float best_proj = -1e30f;
        int best_v = 0;
        for (int j = 0; j < b->nverts; j++) {
            float p = b->verts[j * 2] * inv_nx + b->verts[j * 2 + 1] * inv_ny;
            if (p > best_proj) { best_proj = p; best_v = j; }
        }
        float bvx, bvy;
        rot2(cb, sb, b->verts[best_v * 2], b->verts[best_v * 2 + 1], &bvx, &bvy);
        bvx += b->x; bvy += b->y;
        float sep = (bvx - vx) * nx + (bvy - vy) * ny;
        if (sep > max_sep) {
            max_sep = sep;
            *best_face = i;
        }
    }
    return max_sep;
}

static int clip_seg(float nx, float ny, float c, float pts[2][2])
{
    int sp = 0;
    float out[2][2];
    float d0 = nx * pts[0][0] + ny * pts[0][1] - c;
    float d1 = nx * pts[1][0] + ny * pts[1][1] - c;
    if (d0 <= 0.0f) { out[sp][0] = pts[0][0]; out[sp][1] = pts[0][1]; sp++; }
    if (d1 <= 0.0f) { out[sp][0] = pts[1][0]; out[sp][1] = pts[1][1]; sp++; }
    if (d0 * d1 < 0.0f && sp < 2) {
        float t = d0 / (d0 - d1);
        out[sp][0] = pts[0][0] + t * (pts[1][0] - pts[0][0]);
        out[sp][1] = pts[0][1] + t * (pts[1][1] - pts[0][1]);
        sp++;
    }
    for (int i = 0; i < sp; i++) {
        pts[i][0] = out[i][0];
        pts[i][1] = out[i][1];
    }
    return sp;
}

static int collide_poly_poly(const phys_body_t *a, const phys_body_t *b, manifold_t *m)
{
    int faceA = 0, faceB = 0;
    float sepA = sat_axis_sep(a, b, &faceA);
    if (sepA > 0.0f) return 0;
    float sepB = sat_axis_sep(b, a, &faceB);
    if (sepB > 0.0f) return 0;

    const phys_body_t *ref = a, *inc = b;
    int ref_face = faceA;
    int flip = 0;
    if (sepB > sepA + 0.001f) {
        ref = b; inc = a; ref_face = faceB; flip = 1;
    }

    float cr = cosf(ref->angle), sr = sinf(ref->angle);
    float rnx, rny;
    rot2(cr, sr, ref->normals[ref_face * 2], ref->normals[ref_face * 2 + 1], &rnx, &rny);

    /* Find most anti-parallel edge on incident poly */
    float ci = cosf(inc->angle), si = sinf(inc->angle);
    int inc_face = 0;
    float min_dot = 1e30f;
    for (int i = 0; i < inc->nverts; i++) {
        float inx, iny;
        rot2(ci, si, inc->normals[i * 2], inc->normals[i * 2 + 1], &inx, &iny);
        float d = inx * rnx + iny * rny;
        if (d < min_dot) { min_dot = d; inc_face = i; }
    }

    float pts[2][2];
    int inc_next = (inc_face + 1) % inc->nverts;
    rot2(ci, si, inc->verts[inc_face * 2], inc->verts[inc_face * 2 + 1], &pts[0][0], &pts[0][1]);
    pts[0][0] += inc->x; pts[0][1] += inc->y;
    rot2(ci, si, inc->verts[inc_next * 2], inc->verts[inc_next * 2 + 1], &pts[1][0], &pts[1][1]);
    pts[1][0] += inc->x; pts[1][1] += inc->y;

    float v1x, v1y, v2x, v2y;
    int ref_next = (ref_face + 1) % ref->nverts;
    rot2(cr, sr, ref->verts[ref_face * 2], ref->verts[ref_face * 2 + 1], &v1x, &v1y);
    v1x += ref->x; v1y += ref->y;
    rot2(cr, sr, ref->verts[ref_next * 2], ref->verts[ref_next * 2 + 1], &v2x, &v2y);
    v2x += ref->x; v2y += ref->y;

    float sx = v2x - v1x, sy = v2y - v1y;
    float slen = sqrtf(sx * sx + sy * sy);
    if (slen < 1e-6f) return 0;
    sx /= slen; sy /= slen;

    /* Clip against both side planes of reference face */
    if (clip_seg(-sx, -sy, -(sx * v1x + sy * v1y), pts) < 2) return 0;
    if (clip_seg( sx,  sy,   sx * v2x + sy * v2y,  pts) < 2) return 0;

    float ref_c = rnx * v1x + rny * v1y;
    m->count = 0;
    for (int i = 0; i < 2; i++) {
        float sep = rnx * pts[i][0] + rny * pts[i][1] - ref_c;
        if (sep <= 0.0f) {
            int k = m->count++;
            m->px[k] = pts[i][0];
            m->py[k] = pts[i][1];
            m->depth[k] = -sep;
        }
    }
    if (m->count == 0) return 0;
    m->nx = flip ? -rnx : rnx;
    m->ny = flip ? -rny : rny;
    return 1;
}

static int detect_pair(const phys_body_t *a, const phys_body_t *b, manifold_t *m)
{
    memset(m, 0, sizeof *m);
    m->a = a->id;
    m->b = b->id;
    int hit = 0;
    if (a->shape == PHYS_SHAPE_CIRCLE && b->shape == PHYS_SHAPE_CIRCLE) {
        hit = collide_circle_circle(a, b, m);
    } else if (a->shape == PHYS_SHAPE_CIRCLE) {
        hit = collide_circle_poly(a, b, 0, m);
    } else if (b->shape == PHYS_SHAPE_CIRCLE) {
        hit = collide_circle_poly(b, a, 1, m);
    } else {
        hit = collide_poly_poly(a, b, m);
    }
    if (hit) {
        m->restitution = a->restitution > b->restitution ? a->restitution : b->restitution;
        m->friction = sqrtf(a->friction * b->friction);
    }
    return hit;
}

/* ---- Solver ---- */

static void apply_pair_impulse(phys_body_t *a, phys_body_t *b, float rax, float ray, float rbx, float rby,
                               float ix, float iy)
{
    if (a->inv_mass > 0.0f) {
        a->vx -= ix * a->inv_mass;
        a->vy -= iy * a->inv_mass;
        a->av -= (rax * iy - ray * ix) * a->inv_inertia;
    }
    if (b->inv_mass > 0.0f) {
        b->vx += ix * b->inv_mass;
        b->vy += iy * b->inv_mass;
        b->av += (rbx * iy - rby * ix) * b->inv_inertia;
    }
}

void phys_step(phys_world_t *w, float dt, int iterations)
{
    if (!w || dt <= 0.0f) return;
    if (iterations < 1) iterations = 1;
    if (iterations > 32) iterations = 32;

    /* 1. Integrate forces -> velocities */
    for (int i = 0; i < w->nbodies; i++) {
        phys_body_t *b = &w->bodies[i];
        if (!b->active || b->type != PHYS_DYNAMIC) continue;
        b->vx += (w->gx + b->fx * b->inv_mass) * dt;
        b->vy += (w->gy + b->fy * b->inv_mass) * dt;
        b->av += b->torque * b->inv_inertia * dt;
        b->vx /= (1.0f + b->linear_damping * dt);
        b->vy /= (1.0f + b->linear_damping * dt);
        b->av /= (1.0f + b->angular_damping * dt);
    }

    /* 2. Broadphase/Narrowphase contact generation */
    manifold_t manifolds[PHYS_MAX_CONTACTS];
    int nman = 0;
    w->ncontacts = 0;

    for (int i = 0; i < w->nbodies && nman < PHYS_MAX_CONTACTS; i++) {
        const phys_body_t *a = &w->bodies[i];
        if (!a->active) continue;
        for (int j = i + 1; j < w->nbodies && nman < PHYS_MAX_CONTACTS; j++) {
            const phys_body_t *b = &w->bodies[j];
            if (!b->active) continue;
            if (a->type != PHYS_DYNAMIC && b->type != PHYS_DYNAMIC) continue;
            if (detect_pair(a, b, &manifolds[nman])) {
                const manifold_t *m = &manifolds[nman];
                for (int k = 0; k < m->count && w->ncontacts < PHYS_MAX_CONTACTS; k++) {
                    phys_contact_t *c = &w->contacts[w->ncontacts++];
                    c->a = a->id; c->b = b->id;
                    c->nx = m->nx; c->ny = m->ny;
                    c->px = m->px[k]; c->py = m->py[k];
                    c->depth = m->depth[k];
                }
                nman++;
            }
        }
    }

    /* 3. Sequential impulse iterations (contacts + joints) */
    const float inv_dt = 1.0f / dt;
    for (int it = 0; it < iterations; it++) {
        /* Contacts */
        for (int mi = 0; mi < nman; mi++) {
            manifold_t *m = &manifolds[mi];
            phys_body_t *a = &w->bodies[m->a], *b = &w->bodies[m->b];
            for (int k = 0; k < m->count; k++) {
                float rax = m->px[k] - a->x, ray = m->py[k] - a->y;
                float rbx = m->px[k] - b->x, rby = m->py[k] - b->y;

                float rvx = (b->vx - b->av * rby) - (a->vx - a->av * ray);
                float rvy = (b->vy + b->av * rbx) - (a->vy + a->av * rax);
                float vn = rvx * m->nx + rvy * m->ny;

                float ran = rax * m->ny - ray * m->nx;
                float rbn = rbx * m->ny - rby * m->nx;
                float k_norm = a->inv_mass + b->inv_mass +
                               ran * ran * a->inv_inertia + rbn * rbn * b->inv_inertia;
                if (k_norm <= 1e-8f) continue;

                float e = (it == 0 && vn < -15.0f) ? m->restitution : 0.0f;
                float jn = (-(1.0f + e) * vn) / k_norm;
                float old_jn = m->jn[k];
                float new_jn = old_jn + jn;
                if (new_jn < 0.0f) new_jn = 0.0f;
                jn = new_jn - old_jn;
                m->jn[k] = new_jn;

                apply_pair_impulse(a, b, rax, ray, rbx, rby, m->nx * jn, m->ny * jn);

                /* Tangential Coulomb friction */
                rvx = (b->vx - b->av * rby) - (a->vx - a->av * ray);
                rvy = (b->vy + b->av * rbx) - (a->vy + a->av * rax);
                float tx = -m->ny, ty = m->nx;
                float vt = rvx * tx + rvy * ty;
                float rat = rax * ty - ray * tx;
                float rbt = rbx * ty - rby * tx;
                float k_tan = a->inv_mass + b->inv_mass +
                              rat * rat * a->inv_inertia + rbt * rbt * b->inv_inertia;
                if (k_tan > 1e-8f) {
                    float jt = -vt / k_tan;
                    float max_f = m->friction * new_jn;
                    float old_jt = m->jt[k];
                    float new_jt = old_jt + jt;
                    if (new_jt >  max_f) new_jt =  max_f;
                    if (new_jt < -max_f) new_jt = -max_f;
                    jt = new_jt - old_jt;
                    m->jt[k] = new_jt;
                    apply_pair_impulse(a, b, rax, ray, rbx, rby, tx * jt, ty * jt);
                }
            }
        }

        /* Joints */
        for (int ji = 0; ji < w->njoints; ji++) {
            phys_joint_t *j = &w->joints[ji];
            if (!j->active) continue;
            phys_body_t *a = &w->bodies[j->a], *b = &w->bodies[j->b];
            if (!a->active || !b->active) continue;

            float p1x, p1y, p2x, p2y;
            local_to_world(a, j->lx1, j->ly1, &p1x, &p1y);
            local_to_world(b, j->lx2, j->ly2, &p2x, &p2y);
            float rax = p1x - a->x, ray = p1y - a->y;
            float rbx = p2x - b->x, rby = p2y - b->y;

            if (j->type == PHYS_JOINT_DISTANCE) {
                float dx = p2x - p1x, dy = p2y - p1y;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < 1e-6f) continue;
                float nx = dx / d, ny = dy / d;
                float ran = rax * ny - ray * nx;
                float rbn = rbx * ny - rby * nx;
                float k_eff = a->inv_mass + b->inv_mass +
                              ran * ran * a->inv_inertia + rbn * rbn * b->inv_inertia;
                if (k_eff <= 1e-8f) continue;
                float rvx = (b->vx - b->av * rby) - (a->vx - a->av * ray);
                float rvy = (b->vy + b->av * rbx) - (a->vy + a->av * rax);
                float cdot = rvx * nx + rvy * ny;
                float bias = 0.2f * inv_dt * (d - j->rest_len);
                float lam = -(cdot + bias) / k_eff;
                apply_pair_impulse(a, b, rax, ray, rbx, rby, nx * lam, ny * lam);
            } else {
                /* 2D Anchor pin (REVOLUTE & WELD) */
                float k11 = a->inv_mass + b->inv_mass + ray * ray * a->inv_inertia + rby * rby * b->inv_inertia;
                float k12 = -rax * ray * a->inv_inertia - rbx * rby * b->inv_inertia;
                float k22 = a->inv_mass + b->inv_mass + rax * rax * a->inv_inertia + rbx * rbx * b->inv_inertia;
                float det = k11 * k22 - k12 * k12;
                if (fabsf(det) > 1e-8f) {
                    float inv_det = 1.0f / det;
                    float rvx = (b->vx - b->av * rby) - (a->vx - a->av * ray);
                    float rvy = (b->vy + b->av * rbx) - (a->vy + a->av * rax);
                    float bias_x = 0.2f * inv_dt * (p2x - p1x);
                    float bias_y = 0.2f * inv_dt * (p2y - p1y);
                    float cx = rvx + bias_x, cy = rvy + bias_y;
                    float ix = -( k22 * cx - k12 * cy) * inv_det;
                    float iy = -(-k12 * cx + k11 * cy) * inv_det;
                    apply_pair_impulse(a, b, rax, ray, rbx, rby, ix, iy);
                }
                if (j->type == PHYS_JOINT_WELD) {
                    float ia_sum = a->inv_inertia + b->inv_inertia;
                    if (ia_sum > 1e-8f) {
                        float ang_err = (b->angle - a->angle) - j->ref_angle;
                        float ang_bias = 0.2f * inv_dt * ang_err;
                        float wlam = -((b->av - a->av) + ang_bias) / ia_sum;
                        a->av -= wlam * a->inv_inertia;
                        b->av += wlam * b->inv_inertia;
                    }
                }
            }
        }
    }

    /* 4. Baumgarte positional sink correction on contacts */
    for (int mi = 0; mi < nman; mi++) {
        const manifold_t *m = &manifolds[mi];
        phys_body_t *a = &w->bodies[m->a], *b = &w->bodies[m->b];
        float max_d = m->depth[0];
        if (m->count > 1 && m->depth[1] > max_d) max_d = m->depth[1];
        float corr_mag = max_d - 0.05f;
        if (corr_mag > 0.0f && (a->inv_mass + b->inv_mass) > 0.0f) {
            float scalar = (0.4f * corr_mag) / (a->inv_mass + b->inv_mass);
            float cx = m->nx * scalar, cy = m->ny * scalar;
            if (a->type == PHYS_DYNAMIC) { a->x -= cx * a->inv_mass; a->y -= cy * a->inv_mass; }
            if (b->type == PHYS_DYNAMIC) { b->x += cx * b->inv_mass; b->y += cy * b->inv_mass; }
        }
    }

    /* 5. Integrate velocities -> positions and clear accumulated forces */
    for (int i = 0; i < w->nbodies; i++) {
        phys_body_t *b = &w->bodies[i];
        if (!b->active || b->type == PHYS_STATIC) continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->angle += b->av * dt;
        b->fx = 0.0f;
        b->fy = 0.0f;
        b->torque = 0.0f;
    }
}
