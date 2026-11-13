/*
 * The organic characters of the 3D recipes: ONE continuous skin. Rings of
 * vertices are bridged by quads, so a limb grows out of vertices of the body
 * (a patch of the torso for an arm or a tail, two half rings of the pelvis
 * for the legs) and a bent joint stretches the faces instead of opening a
 * gap. Every vertex has one bone (rigid skinning): the ring shared by two
 * segments belongs to the distal one. The vertices live in a static table
 * (the Pi Zero has little stack); faces go out through face_b, a bone for
 * each corner. They face -z; their left is +x. Numbers in metres, 1.9 tall.
 * The same shapes as .claude/skills/bm-forge/examples/charkit/charkit.py.
 */
#include "mesh_int.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SK_MAXV 400
#define SK_RING 12

typedef struct {
    float p[3];
    uint8_t b;
} sk_v_t;

typedef struct {
    int n;
    int v[SK_RING];
} sk_ring_t;

static sk_v_t SV[SK_MAXV];
static int sk_nv;

static int sk_vert(float x, float y, float z, int b)
{
    int i = sk_nv < SK_MAXV ? sk_nv++ : SK_MAXV - 1;
    SV[i].p[0] = x;
    SV[i].p[1] = y;
    SV[i].p[2] = z;
    SV[i].b = (uint8_t)b;
    return i;
}

/* a face on vertices, the side that shows away from ref */
static void sk_face(mc_t *c, const int *v, int n, int m, const float *ref)
{
    float p[4][3], nn[3] = { 0, 0, 0 }, mid[3] = { 0, 0, 0 };
    uint8_t b[4];
    for (int k = 0; k < n; k++) {
        memcpy(p[k], SV[v[k]].p, sizeof p[k]);
        b[k] = SV[v[k]].b;
        for (int d = 0; d < 3; d++) mid[d] += p[k][d] / (float)n;
    }
    for (int k = 0; k < n; k++) {
        const float *a = p[k], *e = p[(k + 1) % n];
        nn[0] += (a[1] - e[1]) * (a[2] + e[2]);
        nn[1] += (a[2] - e[2]) * (a[0] + e[0]);
        nn[2] += (a[0] - e[0]) * (a[1] + e[1]);
    }
    if (nn[0] * (mid[0] - ref[0]) + nn[1] * (mid[1] - ref[1]) + nn[2] * (mid[2] - ref[2]) < 0) {
        float q[4][3];
        uint8_t s[4];
        for (int k = 0; k < n; k++) {
            memcpy(q[k], p[n - 1 - k], sizeof q[k]);
            s[k] = b[n - 1 - k];
        }
        memcpy(p, q, sizeof p);
        memcpy(b, s, sizeof b);
    }
    face_b(c, (const float (*)[3])p, n, m, b);
}

static void ring_mid(const sk_ring_t *r, float *o)
{
    o[0] = o[1] = o[2] = 0;
    for (int k = 0; k < r->n; k++)
        for (int d = 0; d < 3; d++) o[d] += SV[r->v[k]].p[d] / (float)r->n;
}

/* a horizontal ring of n vertices (k = 0 straight ahead, towards -z; the
 * angle grows towards +x) */
static void ring_h(sk_ring_t *r, int n, float cx, float y, float cz, float rx, float rz, int bone)
{
    r->n = n;
    for (int k = 0; k < n; k++) {
        float a = 2 * PI_F * (float)k / (float)n;
        r->v[k] = sk_vert(cx + rx * sinf(a), y, cz - rz * cosf(a), bone);
    }
}

/* quads between two rings of the same size; the sectors of skip are left open */
static void strip(mc_t *c, const sk_ring_t *a, const sk_ring_t *b, int m, unsigned skip)
{
    float ra[3], rb[3], ref[3];
    ring_mid(a, ra);
    ring_mid(b, rb);
    for (int d = 0; d < 3; d++) ref[d] = (ra[d] + rb[d]) / 2;
    for (int k = 0; k < a->n; k++) {
        if (skip >> k & 1u) continue;
        int j = (k + 1) % a->n;
        int q[4] = { a->v[k], a->v[j], b->v[j], b->v[k] };
        sk_face(c, q, 4, m, ref);
    }
}

/* a fan from a ring to a point */
static void cap(mc_t *c, const sk_ring_t *r, int apex, int m)
{
    float mid[3], ref[3];
    ring_mid(r, mid);
    for (int d = 0; d < 3; d++) ref[d] = 2 * mid[d] - SV[apex].p[d];
    for (int k = 0; k < r->n; k++) {
        int q[3] = { r->v[k], r->v[(k + 1) % r->n], apex };
        sk_face(c, q, 3, m, ref);
    }
}

/* ---------------------------------------------------------------- limbs */

typedef struct {
    float x, y, z;              /* the centre of the ring */
    float ru, rv;               /* its radii along u and v */
    int bone, mat;              /* of the ring, and of the strip that reaches it */
} sk_pt_t;

static void norm3(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

/* A limb that grows out of a patch of 6 vertices of the body: rings of the
 * same shape follow the path pts[0] (the centre of the patch) .. pts[np-1],
 * each seen from the direction of the path; u is a fixed side vector, v the
 * other one (vs gives its sign); the last ring ends in a point tip. */
static void limb(mc_t *c, const sk_ring_t *patch, const float *u, float vs, const sk_pt_t *pts, int np,
                 const float *tip, int tipbone, int tipmat)
{
    float off[SK_RING][2], hu = 1e-4f, hv = 1e-4f;
    for (int j = 0; j < 6; j++) {
        const float *q = SV[patch->v[j]].p;
        off[j][0] = (q[0] - pts[0].x) * u[0] + (q[1] - pts[0].y) * u[1] + (q[2] - pts[0].z) * u[2];
    }
    /* the first ring's v: the path's direction to the next centre decides it */
    float d0[3] = { pts[1].x - pts[0].x, pts[1].y - pts[0].y, pts[1].z - pts[0].z };
    norm3(d0);
    float v0[3] = { vs * (u[1] * d0[2] - u[2] * d0[1]), vs * (u[2] * d0[0] - u[0] * d0[2]),
                    vs * (u[0] * d0[1] - u[1] * d0[0]) };
    for (int j = 0; j < 6; j++) {
        const float *q = SV[patch->v[j]].p;
        off[j][1] = (q[0] - pts[0].x) * v0[0] + (q[1] - pts[0].y) * v0[1] + (q[2] - pts[0].z) * v0[2];
        if (fabsf(off[j][0]) > hu) hu = fabsf(off[j][0]);
        if (fabsf(off[j][1]) > hv) hv = fabsf(off[j][1]);
    }
    sk_ring_t prev = *patch, cur;
    for (int i = 1; i < np; i++) {
        float d[3] = { pts[i < np - 1 ? i + 1 : i].x - pts[i - 1].x, pts[i < np - 1 ? i + 1 : i].y - pts[i - 1].y,
                       pts[i < np - 1 ? i + 1 : i].z - pts[i - 1].z };
        norm3(d);
        float v[3] = { vs * (u[1] * d[2] - u[2] * d[1]), vs * (u[2] * d[0] - u[0] * d[2]),
                       vs * (u[0] * d[1] - u[1] * d[0]) };
        cur.n = 6;
        for (int j = 0; j < 6; j++) {
            float a = off[j][0] / hu * pts[i].ru, b = off[j][1] / hv * pts[i].rv;
            cur.v[j] = sk_vert(pts[i].x + a * u[0] + b * v[0], pts[i].y + a * u[1] + b * v[1],
                               pts[i].z + a * u[2] + b * v[2], pts[i].bone);
        }
        strip(c, &prev, &cur, pts[i].mat, 0);
        prev = cur;
    }
    if (tip) cap(c, &prev, sk_vert(tip[0], tip[1], tip[2], tipbone), tipmat);
}

/* ---------------------------------------------------------------- the legs */

/* a ring of 7 for a leg: the half ring of the pelvis (t = 0, a D) opens into a full
 * ellipse around the leg (t = 1); the vertices 0 and 6 are the ones of the crotch */
static void leg_ring(sk_ring_t *r, float t, float sg, float cx, float y, float cz, float rx, float rz, int bone)
{
    r->n = 7;
    for (int k = 0; k < 7; k++) {
        float a = (30.0f * (float)k * (1 - t) + (-60.0f + 50.0f * (float)k) * t) * PI_F / 180.0f;
        r->v[k] = sk_vert(sg * (cx + rx * sinf(a)), y, cz - rz * cosf(a), bone);
    }
}

/* ---------------------------------------------------------------- the character */

enum { B_HIPS, B_SPINE, B_HEAD, B_ARM, B_FORE, B_HAND, B_LEG, B_SHIN, B_FOOT, B_TAIL, B_N };

#define SKM_BODY 1              /* the materials: shirt, trousers, skin, hair, eyes, shoes */
#define SKM_PANTS 2
#define SKM_SKIN 3
#define SKM_HAIR 4
#define SKM_EYE 5
#define SKM_SHOE 6

static void build(mc_t *c, int tail)
{
    static const float AX_Z[3] = { 0, 0, 1 }, AX_X[3] = { 1, 0, 0 };
    int bn[B_N][2];             /* the left and the right bone of each kind */
    int hips = bone(c, "hips", -1, 0, 0.88f, 0, 0, 0.98f, 0);
    int spine = bone(c, "spine", hips, 0, 0.93f, 0, 0, 1.6f, 0);
    int head = bone(c, "head", spine, 0, 1.66f, 0, 0, 2.0f, 0);
    bn[B_HIPS][0] = hips;
    bn[B_SPINE][0] = spine;
    bn[B_HEAD][0] = head;
    static const char *const side[2] = { "L", "R" };
    for (int s = 0; s < 2; s++) {
        float sg = s ? -1.0f : 1.0f;
        char nm[16];
        snprintf(nm, sizeof nm, "arm.%s", side[s]);
        bn[B_ARM][s] = bone(c, nm, spine, sg * 0.19f, 1.53f, 0, sg * 0.3168f, 1.2916f, 0);
        snprintf(nm, sizeof nm, "forearm.%s", side[s]);
        bn[B_FORE][s] = bone(c, nm, bn[B_ARM][s], sg * 0.3168f, 1.2916f, 0, sg * 0.3941f, 1.0538f, 0);
        snprintf(nm, sizeof nm, "hand.%s", side[s]);
        bn[B_HAND][s] = bone(c, nm, bn[B_FORE][s], sg * 0.3941f, 1.0538f, 0, sg * 0.4355f, 0.8993f, 0);
        snprintf(nm, sizeof nm, "leg.%s", side[s]);
        bn[B_LEG][s] = bone(c, nm, hips, sg * 0.095f, 0.86f, 0, sg * 0.105f, 0.46f, 0);
        snprintf(nm, sizeof nm, "shin.%s", side[s]);
        bn[B_SHIN][s] = bone(c, nm, bn[B_LEG][s], sg * 0.105f, 0.46f, 0, sg * 0.105f, 0.09f, 0);
        snprintf(nm, sizeof nm, "foot.%s", side[s]);
        bn[B_FOOT][s] = bone(c, nm, bn[B_SHIN][s], sg * 0.105f, 0.09f, 0, sg * 0.105f, 0.03f, -0.14f);
    }
    int tl[3] = { 0, 0, 0 };

    /* the tail path: it leaves the back of the pelvis and curls up, tighter to the tip (length 1,
     * 270 degrees); the rings at a sixth of the way each */
    sk_pt_t tp[8];
    if (tail) {
        float y = 0.925f, z = 0.1f, ang;
        int ring = 1;
        tp[0] = (sk_pt_t){ 0, y, z, 0, 0, 0, 0 };
        for (int i = 1; i <= 120; i++) {
            float s = (float)(i - 1) / 120.0f;
            ang = 270.0f * PI_F / 180.0f * s * s;
            y += sinf(ang) / 120.0f;
            z += cosf(ang) / 120.0f;
            if (i % 20 == 0) {
                float r = 0.06f * (1 - 0.75f * (float)i / 120.0f);
                tp[ring++] = (sk_pt_t){ 0, y, z, r, r, 0, SKM_BODY };
            }
        }
        /* the bones of the tail: tail1 .. tail3, two rings each (the joint ring is the first of the next) */
        float jy[4] = { tp[0].y, tp[2].y, tp[4].y, tp[6].y }, jz[4] = { tp[0].z, tp[2].z, tp[4].z, tp[6].z };
        int par = hips;
        for (int i = 0; i < 3; i++) {
            char nm[16];
            snprintf(nm, sizeof nm, "tail%d", i + 1);
            tl[i] = bone(c, nm, par, 0, jy[i], jz[i], 0, jy[i + 1], jz[i + 1]);
            par = tl[i];
        }
        for (int i = 1; i <= 6; i++)
            tp[i].bone = tl[i / 2 > 2 ? 2 : i / 2];         /* a joint ring goes to the distal bone */
    }

    sk_nv = 0;
    /* the torso: 7 rings of 12 */
    sk_ring_t h, w1, w2, ch, s1, s2, sh, nk;
    ring_h(&h, SK_RING, 0, 0.88f, 0, 0.17f, 0.105f, hips);
    ring_h(&w1, SK_RING, 0, 0.97f, 0, 0.165f, 0.105f, hips);
    ring_h(&w2, SK_RING, 0, 1.12f, 0, 0.14f, 0.095f, spine);
    ring_h(&ch, SK_RING, 0, 1.30f, 0, 0.165f, 0.11f, spine);
    ring_h(&s1, SK_RING, 0, 1.48f, 0, 0.19f, 0.115f, spine);
    ring_h(&s2, SK_RING, 0, 1.58f, 0, 0.185f, 0.105f, spine);
    ring_h(&sh, SK_RING, 0, 1.64f, 0, 0.10f, 0.07f, spine);
    ring_h(&nk, SK_RING, 0, 1.68f, 0, 0.055f, 0.055f, head);
    /* the shoulder patches belong to the arms, the tail patch to the first bone of the tail */
    sk_ring_t pl, pr, pt;
    int patchk[3] = { 2, 3, 4 };
    pl.n = pr.n = 6;
    for (int i = 0; i < 3; i++) {
        pl.v[i] = s1.v[patchk[i]];
        pl.v[5 - i] = s2.v[patchk[i]];
        pr.v[i] = s1.v[(SK_RING - patchk[i]) % SK_RING];
        pr.v[5 - i] = s2.v[(SK_RING - patchk[i]) % SK_RING];
    }
    for (int j = 0; j < 6; j++) {
        SV[pl.v[j]].b = (uint8_t)bn[B_ARM][0];
        SV[pr.v[j]].b = (uint8_t)bn[B_ARM][1];
    }
    unsigned tail_skip = 0;
    if (tail) {
        pt.n = 6;
        for (int i = 0; i < 3; i++) {
            pt.v[i] = h.v[5 + i];
            pt.v[5 - i] = w1.v[5 + i];
        }
        for (int j = 0; j < 6; j++) SV[pt.v[j]].b = (uint8_t)tl[0];
        tail_skip = (1u << 5) | (1u << 6);
    }
    strip(c, &h, &w1, SKM_PANTS, tail_skip);
    strip(c, &w1, &w2, SKM_BODY, 0);
    strip(c, &w2, &ch, SKM_BODY, 0);
    strip(c, &ch, &s1, SKM_BODY, 0);
    strip(c, &s1, &s2, SKM_BODY, (1u << 2) | (1u << 3) | (1u << 8) | (1u << 9));
    strip(c, &s2, &sh, SKM_BODY, 0);
    strip(c, &sh, &nk, SKM_SKIN, 0);

    /* the head */
    sk_ring_t jw, ck, br, tp_;
    ring_h(&jw, SK_RING, 0, 1.74f, -0.005f, 0.065f, 0.08f, head);
    ring_h(&ck, SK_RING, 0, 1.82f, 0, 0.10f, 0.105f, head);
    ring_h(&br, SK_RING, 0, 1.91f, 0.005f, 0.095f, 0.10f, head);
    ring_h(&tp_, SK_RING, 0, 1.97f, 0.005f, 0.06f, 0.07f, head);
    SV[ck.v[0]].p[2] -= 0.035f;                         /* the nose */
    SV[jw.v[0]].p[2] -= 0.01f;
    strip(c, &nk, &jw, SKM_SKIN, 0);
    strip(c, &jw, &ck, SKM_SKIN, 0);
    strip(c, &ck, &br, SKM_SKIN, 0);
    strip(c, &br, &tp_, tail ? SKM_SKIN : SKM_HAIR, 0);
    cap(c, &tp_, sk_vert(0, 2.0f, 0.005f, head), tail ? SKM_SKIN : SKM_HAIR);
    {                                                   /* the eyes: two quads of the cheek ring, dark */
        float ref[3] = { 0, 1.85f, 0 };
        int ks[2] = { 1, 10 };
        for (int e = 0; e < 2; e++) {
            int k = ks[e], j = k + 1;
            int q[4] = { ck.v[k], ck.v[j % SK_RING], br.v[j % SK_RING], br.v[k] };
            sk_face(c, q, 4, SKM_EYE, ref);
        }
    }

    /* the arms: shirt to the elbow, then skin */
    for (int s = 0; s < 2; s++) {
        float sg = s ? -1.0f : 1.0f;
        const sk_ring_t *patch = s ? &pr : &pl;
        sk_pt_t ap[5] = {
            { sg * 0.19f, 1.53f, 0, 0, 0, bn[B_ARM][s], 0 },
            { sg * 0.2135f, 1.486f, 0, 0.055f, 0.055f, bn[B_ARM][s], SKM_BODY },
            { sg * 0.3168f, 1.2916f, 0, 0.047f, 0.047f, bn[B_FORE][s], SKM_BODY },
            { sg * 0.3941f, 1.0538f, 0, 0.036f, 0.036f, bn[B_HAND][s], SKM_SKIN },
            { sg * 0.4122f, 0.9862f, 0, 0.05f, 0.03f, bn[B_HAND][s], SKM_SKIN },
        };
        const float tipp[3] = { sg * 0.4355f, 0.8993f, 0 };
        limb(c, patch, AX_Z, sg, ap, 5, tipp, bn[B_HAND][s], SKM_SKIN);
    }

    /* the legs: the two half rings of the pelvis open into the thighs; the shoes are the last rings */
    for (int s = 0; s < 2; s++) {
        float sg = s ? -1.0f : 1.0f;
        sk_ring_t top, l1, kn, cf, an, ft;
        top.n = 7;
        for (int k = 0; k < 7; k++) top.v[k] = h.v[s ? (SK_RING - k) % SK_RING : k];
        leg_ring(&l1, 1, sg, 0.095f, 0.76f, 0, 0.085f, 0.09f, bn[B_LEG][s]);
        leg_ring(&kn, 1, sg, 0.105f, 0.46f, 0, 0.065f, 0.07f, bn[B_SHIN][s]);
        leg_ring(&cf, 1, sg, 0.105f, 0.27f, 0, 0.06f, 0.07f, bn[B_SHIN][s]);
        leg_ring(&an, 1, sg, 0.105f, 0.09f, 0, 0.045f, 0.05f, bn[B_FOOT][s]);
        leg_ring(&ft, 1, sg, 0.105f, 0.045f, -0.04f, 0.055f, 0.10f, bn[B_FOOT][s]);
        strip(c, &top, &l1, SKM_PANTS, 0);
        strip(c, &l1, &kn, SKM_PANTS, 0);
        strip(c, &kn, &cf, SKM_PANTS, 0);
        strip(c, &cf, &an, SKM_PANTS, 0);
        strip(c, &an, &ft, SKM_SHOE, 0);
        cap(c, &ft, sk_vert(sg * 0.105f, 0, -0.04f, bn[B_FOOT][s]), SKM_SHOE);
    }

    if (tail) {
        float tipp[3];
        float ang = 270.0f * PI_F / 180.0f;
        tipp[0] = 0;
        tipp[1] = tp[6].y + 0.05f * sinf(ang);
        tipp[2] = tp[6].z + 0.05f * cosf(ang);
        limb(c, &pt, AX_X, -1, tp, 7, tipp, tl[2], SKM_BODY);
    }
}

/* idle, walk and wave: the same swing as the other bipeds, the tail follows the steps */
static void clips(mc_t *c)
{
    mesh_model_t *M = c->m;
    int b[B_N][2];
    memset(b, -1, sizeof b);
    int n = M->nbones;
    int tl[3] = { -1, -1, -1 };
    for (int i = 0; i < n; i++) {
        const char *nm = M->bones[i].name;
        int s = nm[strlen(nm) - 1] == 'R';
        if (!strcmp(nm, "hips")) b[B_HIPS][0] = i;
        else if (!strcmp(nm, "spine")) b[B_SPINE][0] = i;
        else if (!strcmp(nm, "head")) b[B_HEAD][0] = i;
        else if (!strncmp(nm, "arm.", 4)) b[B_ARM][s] = i;
        else if (!strncmp(nm, "forearm.", 8)) b[B_FORE][s] = i;
        else if (!strncmp(nm, "hand.", 5)) b[B_HAND][s] = i;
        else if (!strncmp(nm, "leg.", 4)) b[B_LEG][s] = i;
        else if (!strncmp(nm, "shin.", 5)) b[B_SHIN][s] = i;
        else if (!strncmp(nm, "foot.", 5)) b[B_FOOT][s] = i;
        else if (!strncmp(nm, "tail", 4)) tl[nm[4] - '1'] = i;
    }
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    key(k, 0);
    mesh_key_t *f = key(k, 1);
    shift(f, b[B_HIPS][0], 0, -0.02f, 0);
    for (int s = 0; s < 2; s++) turn(f, b[B_ARM][s], 4, 0, 0);
    turn(f, b[B_HEAD][0], 0, 8, 0);
    for (int i = 0; i < 3; i++)
        if (tl[i] >= 0) turn(f, tl[i], 0, 0, 6);
    k = clip(c, "walk", 1, 1);
    for (int i = 0; i < 4; i++) {
        f = key(k, 0.25f * (float)i);
        float s = i == 0 ? 1 : i == 2 ? -1 : 0;
        turn(f, b[B_LEG][0], 30 * s, 0, 0);
        turn(f, b[B_LEG][1], -30 * s, 0, 0);
        turn(f, b[B_SHIN][0], s < 0 ? -25.0f : 0, 0, 0);
        turn(f, b[B_SHIN][1], s > 0 ? -25.0f : 0, 0, 0);
        turn(f, b[B_ARM][0], -25 * s, 0, 0);
        turn(f, b[B_ARM][1], 25 * s, 0, 0);
        if (s == 0) shift(f, b[B_HIPS][0], 0, 0.04f, 0);
        turn(f, b[B_SPINE][0], 3, 0, 0);
        for (int t = 0; t < 3; t++)
            if (tl[t] >= 0) turn(f, tl[t], 0, 0, 10 * s);
    }
    k = clip(c, "wave", 1, 1);
    f = key(k, 0);
    turn(f, b[B_ARM][1], 165, 0, -15);
    f = key(k, 0.5f);
    turn(f, b[B_ARM][1], 165, 0, 20);
}

void r_biped(mc_t *c)
{
    mat(c, SKM_BODY, mpick(c, MP_CLOTH, 6));
    mat(c, SKM_PANTS, mpick(c, (const uint32_t[]){ 0x404450, 0x3A4A7A, 0x6A4028, 0x2A6A4A }, 4));
    mat(c, SKM_SKIN, mpick(c, MP_SKIN, 5));
    mat(c, SKM_HAIR, mpick(c, MP_HAIR, 7));
    mat_flat(c, SKM_EYE, 0x101018);
    mat(c, SKM_SHOE, 0x2A1E18);
    build(c, 0);
    clips(c);
}

void r_chameleon(mc_t *c)
{
    uint32_t main_ = mpick(c, (const uint32_t[]){ 0x4A9A3A, 0x2E8A6A, 0x8AA032, 0x3A8AA0 }, 4);
    mat(c, SKM_BODY, main_);
    mat(c, SKM_PANTS, mshade(main_, 1));
    mat(c, SKM_SKIN, mshade(main_, 3));
    mat(c, SKM_HAIR, main_);
    mat_flat(c, SKM_EYE, 0xE8C020);
    mat(c, SKM_SHOE, mshade(main_, 1));
    build(c, 1);
    clips(c);
}
