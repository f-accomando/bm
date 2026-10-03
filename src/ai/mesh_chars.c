/*
 * The characters of the 3D recipes (see mesh.h): people, animals, machines,
 * each with a skeleton (bones from mesh_int.h, every face on one) and a
 * few animations, as bm Animator makes them. They face -z (the camera of
 * the viewer); their left is +x. The left side is built, then mirrored.
 */
#include "mesh_int.h"

#include <math.h>
#include <string.h>

/* a limb: a square-section bar from a to b, r is half its width */
static void limb(mc_t *c, float ax, float ay, float az, float bx_, float by, float bz, float r, int m)
{
    const float a[3] = { ax, ay, az }, b[3] = { bx_, by, bz };
    tube(c, a, b, r * 1.4142f, r * 1.4142f, 4, m, 1);
}

static void eye(mc_t *c, float x, float y, float z, float s, int m)
{
    box(c, x - s / 2, y - s / 2, z - 0.015f, x + s / 2, y + s / 2, z + 0.015f, m);
}

/* ---------------------------------------------------------------- bipeds */

typedef struct {
    int hips, spine, head, armL, armR, legL, legR;
} biped_t;

static void biped_bones(mc_t *c, biped_t *b, float hipY, float shoulderY, float neckY, float topY,
                        float shX, float legX, float handY)
{
    b->hips = bone(c, "hips", -1, 0, hipY, 0, 0, hipY + 0.1f, 0);
    b->spine = bone(c, "spine", b->hips, 0, hipY + 0.05f, 0, 0, shoulderY, 0);
    b->head = bone(c, "head", b->spine, 0, neckY, 0, 0, topY, 0);
    b->armL = bone(c, "arm.L", b->spine, shX, shoulderY - 0.05f, 0, shX, handY, 0);
    b->legL = bone(c, "leg.L", b->hips, legX, hipY, 0, legX, 0.05f, 0);
    b->armR = b->legR = -1;
}

static void biped_mirror(mc_t *c, biped_t *b, int from)
{
    b->armR = bone_mirror(c, b->armL);
    b->legR = bone_mirror(c, b->legL);
    mirror_x(c, from);
}

/* idle (a breath, a look around) and walk (legs and arms swing, the body
 * bobs); leg and arm in degrees, bob the rise between steps */
static void biped_anims(mc_t *c, const biped_t *b, float leg, float arm, float bob)
{
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    key(k, 0);
    mesh_key_t *f = key(k, 1);
    shift(f, b->hips, 0, -0.02f, 0);
    turn(f, b->armL, 4, 0, 0);
    turn(f, b->armR, 4, 0, 0);
    turn(f, b->head, 0, 8, 0);
    k = clip(c, "walk", 1, 1);
    for (int i = 0; i < 4; i++) {
        f = key(k, 0.25f * (float)i);
        float s = i == 0 ? 1 : i == 2 ? -1 : 0;
        turn(f, b->legL, leg * s, 0, 0);
        turn(f, b->legR, -leg * s, 0, 0);
        turn(f, b->armL, -arm * s, 0, 0);
        turn(f, b->armR, arm * s, 0, 0);
        if (s == 0) shift(f, b->hips, 0, bob, 0);
        turn(f, b->spine, 3, 0, 0);
    }
}

void r_hero(mc_t *c)
{
    mat(c, 1, mpick(c, MP_CLOTH, 6));                /* the shirt */
    mat(c, 2, mpick(c, (const uint32_t[]){ 0x404450, 0x3A4A7A, 0x6A4028, 0x2A6A4A }, 4));
    mat(c, 3, mpick(c, MP_SKIN, 5));
    mat(c, 4, mpick(c, MP_HAIR, 7));
    mat_flat(c, 5, 0x101018);
    mat(c, 6, 0x2A1E18);
    mat(c, 7, 0x8A5A30);
    biped_t b;
    biped_bones(c, &b, 0.9f, 1.45f, 1.5f, 2.0f, 0.33f, 0.13f, 0.86f);
    use(c, b.spine);
    bx(c, 0, 0.92f, 0, 0.5f, 0.58f, 0.3f, 1);
    use(c, b.hips);
    bx(c, 0, 0.9f, 0, 0.52f, 0.08f, 0.32f, 7);
    use(c, b.head);
    bx(c, 0, 1.5f, 0, 0.44f, 0.44f, 0.44f, 3);
    int style = mri(c, 0, 3);
    bx(c, 0, 1.84f, 0, 0.48f, 0.14f, 0.48f, 4);
    if (style == 1) box(c, -0.24f, 1.5f, -0.1f, 0.24f, 1.9f, 0.24f, 4);     /* long */
    if (style == 2) bx(c, 0, 1.96f, 0, 0.3f, 0.16f, 0.3f, 4);              /* a tuft */
    if (style == 3) { mat(c, 8, mpick(c, MP_CLOTH, 6)); bx(c, 0, 1.9f, 0, 0.5f, 0.12f, 0.5f, 8);
                      bx(c, 0, 2.0f, 0, 0.36f, 0.16f, 0.36f, 8); }         /* a hat */
    eye(c, -0.09f, 1.7f, -0.22f, 0.07f, 5);
    eye(c, 0.09f, 1.7f, -0.22f, 0.07f, 5);
    int from = c->m->nfaces;
    use(c, b.armL);
    bx(c, 0.33f, 1.0f, 0, 0.16f, 0.48f, 0.16f, 1);
    bx(c, 0.33f, 0.86f, 0, 0.14f, 0.14f, 0.14f, 3);
    use(c, b.legL);
    bx(c, 0.13f, 0.12f, 0, 0.2f, 0.78f, 0.2f, 2);
    box(c, 0.02f, 0, -0.16f, 0.24f, 0.12f, 0.1f, 6);
    biped_mirror(c, &b, from);
    biped_anims(c, &b, 30, 25, 0.04f);
    mesh_clip_t *k = clip(c, "wave", 1, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, b.armR, 165, 0, -15);
    f = key(k, 0.5f);
    turn(f, b.armR, 165, 0, 20);
}

void r_knight(mc_t *c)
{
    mat(c, 1, mpick(c, MP_METAL, 4));                /* the armour */
    mat(c, 2, mpick(c, MP_CLOTH, 6));                /* the cloth */
    mat(c, 3, mpick(c, MP_SKIN, 5));
    mat_flat(c, 4, 0x181820);
    mat(c, 5, 0xE8B830);
    mat(c, 6, 0x6A4028);
    mat(c, 7, 0xD8DCE4);
    biped_t b;
    biped_bones(c, &b, 0.95f, 1.5f, 1.55f, 2.1f, 0.36f, 0.14f, 0.9f);
    use(c, b.spine);
    bx(c, 0, 0.97f, 0, 0.56f, 0.6f, 0.36f, 1);
    bx(c, 0, 1.0f, -0.19f, 0.3f, 0.5f, 0.04f, 2);                  /* the tabard */
    bx(c, 0, 1.52f, 0, 0.7f, 0.12f, 0.4f, 1);                      /* pauldrons */
    use(c, b.hips);
    bx(c, 0, 0.93f, 0, 0.58f, 0.1f, 0.38f, 6);
    bx(c, 0, 0.75f, 0, 0.6f, 0.2f, 0.4f, 2);                       /* the skirt */
    use(c, b.head);
    cyl(c, 0, 1.55f, 0, 0.26f, 0.5f, 8, 1);
    ell(c, 0, 2.05f, 0, 0.26f, 0.12f, 0.26f, 8, 1);
    box(c, -0.18f, 1.74f, -0.27f, 0.18f, 1.82f, -0.2f, 4);         /* the visor slit */
    if (c->seed % 2) {
        const float plume[8] = { 2.1f, 0.0f, 2.5f, -0.1f, 2.45f, 0.5f, 2.05f, 0.4f };
        prism(c, 0, plume, 4, -0.04f, 0.04f, 2);
    }
    int from = c->m->nfaces;
    use(c, b.armL);
    bx(c, 0.36f, 1.0f, 0, 0.18f, 0.5f, 0.18f, 1);
    bx(c, 0.36f, 0.88f, 0, 0.16f, 0.14f, 0.16f, 6);
    use(c, b.legL);
    bx(c, 0.14f, 0.1f, 0, 0.22f, 0.85f, 0.22f, 1);
    box(c, 0.02f, 0, -0.18f, 0.26f, 0.1f, 0.12f, 1);
    biped_mirror(c, &b, from);
    /* the shield on the left arm, the sword in the right hand */
    use(c, b.armL);
    const float heater[10] = { 1.35f, -0.22f, 1.35f, 0.22f, 0.9f, 0.27f, 0.55f, 0, 0.9f, -0.27f };
    prism(c, 0, heater, 5, 0.46f, 0.52f, 2);
    use(c, b.armR);
    const float blade[8] = { -0.36f - 0.05f, 0, -0.36f, 0.02f, -0.36f + 0.05f, 0, -0.36f, -0.02f };
    prism(c, 1, blade, 4, 0.95f, 2.0f, 7);
    bx(c, -0.36f, 0.78f, 0, 0.3f, 0.06f, 0.08f, 5);
    biped_anims(c, &b, 25, 15, 0.04f);
    mesh_clip_t *k = clip(c, "attack", 0.8f, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, b.armR, 150, 0, 0);
    f = key(k, 0.3f);
    turn(f, b.armR, 20, 0, 0);
    turn(f, b.spine, 10, 0, 0);
    f = key(k, 0.55f);
    turn(f, b.armR, 150, 0, 0);
}

void r_robot(mc_t *c)
{
    mat(c, 1, mpick(c, MP_METAL, 4));
    mat(c, 2, mpick(c, MP_BRIGHT, 8));
    mat_flat(c, 3, 0x48E8F0);
    mat(c, 4, 0x30323A);
    mat_flat(c, 5, 0xE03030);
    biped_t b;
    biped_bones(c, &b, 0.9f, 1.5f, 1.55f, 2.05f, 0.4f, 0.16f, 0.85f);
    use(c, b.spine);
    bx(c, 0, 0.95f, 0, 0.64f, 0.6f, 0.4f, 1);
    bx(c, 0, 1.1f, -0.21f, 0.3f, 0.2f, 0.03f, 2);                  /* a panel */
    eye(c, -0.08f, 1.0f, -0.215f, 0.05f, 3);
    eye(c, 0.08f, 1.0f, -0.215f, 0.05f, 5);
    use(c, b.hips);
    bx(c, 0, 0.85f, 0, 0.5f, 0.12f, 0.3f, 4);
    use(c, b.head);
    cyl(c, 0, 1.5f, 0, 0.1f, 0.08f, 6, 4);
    bx(c, 0, 1.58f, 0, 0.48f, 0.42f, 0.44f, 1);
    box(c, -0.18f, 1.76f, -0.235f, 0.18f, 1.88f, -0.2f, 3);        /* the visor */
    cyl(c, 0, 2.0f, 0, 0.025f, 0.22f, 4, 4);
    ell(c, 0, 2.24f, 0, 0.06f, 0.06f, 0.06f, 4, 5);
    int from = c->m->nfaces;
    use(c, b.armL);
    ell(c, 0.4f, 1.45f, 0, 0.12f, 0.12f, 0.12f, 6, 4);
    bx(c, 0.42f, 0.98f, 0, 0.2f, 0.46f, 0.2f, 1);
    bx(c, 0.42f, 0.84f, 0.05f, 0.08f, 0.16f, 0.06f, 4);            /* the claw */
    bx(c, 0.42f, 0.84f, -0.05f, 0.08f, 0.16f, 0.06f, 4);
    use(c, b.legL);
    bx(c, 0.16f, 0.14f, 0, 0.24f, 0.74f, 0.24f, 1);
    box(c, 0.02f, 0, -0.2f, 0.3f, 0.14f, 0.14f, 4);
    biped_mirror(c, &b, from);
    biped_anims(c, &b, 22, 18, 0.02f);
}

void r_skeleton(mc_t *c)
{
    mat(c, 1, 0xE8E4D8);
    mat_flat(c, 2, 0x181820);
    biped_t b;
    biped_bones(c, &b, 0.9f, 1.45f, 1.5f, 1.95f, 0.3f, 0.13f, 0.86f);
    use(c, b.spine);
    limb(c, 0, 0.95f, 0, 0, 1.5f, 0, 0.05f, 1);
    for (int i = 0; i < 3; i++)
        bx(c, 0, 1.12f + 0.12f * (float)i, 0, 0.44f, 0.05f, 0.26f, 1);
    bx(c, 0, 1.4f, 0, 0.56f, 0.1f, 0.2f, 1);                       /* the collar bones */
    use(c, b.hips);
    bx(c, 0, 0.84f, 0, 0.4f, 0.14f, 0.22f, 1);
    use(c, b.head);
    ell(c, 0, 1.73f, 0, 0.21f, 0.22f, 0.22f, 8, 1);
    bx(c, 0, 1.5f, -0.04f, 0.3f, 0.1f, 0.26f, 1);                  /* the jaw */
    eye(c, -0.08f, 1.74f, -0.2f, 0.09f, 2);
    eye(c, 0.08f, 1.74f, -0.2f, 0.09f, 2);
    int from = c->m->nfaces;
    use(c, b.armL);
    limb(c, 0.3f, 1.42f, 0, 0.3f, 1.15f, 0, 0.045f, 1);
    limb(c, 0.3f, 1.15f, 0, 0.3f, 0.86f, 0, 0.04f, 1);
    bx(c, 0.3f, 0.78f, 0, 0.1f, 0.1f, 0.06f, 1);
    use(c, b.legL);
    limb(c, 0.13f, 0.86f, 0, 0.13f, 0.46f, 0, 0.05f, 1);
    limb(c, 0.13f, 0.46f, 0, 0.13f, 0.06f, 0, 0.045f, 1);
    box(c, 0.05f, 0, -0.16f, 0.21f, 0.06f, 0.06f, 1);
    biped_mirror(c, &b, from);
    biped_anims(c, &b, 28, 20, 0.03f);
}

void r_snowman(mc_t *c)
{
    mat(c, 1, 0xF4F4FA);
    mat(c, 2, mpick(c, MP_CLOTH, 6));                /* the scarf */
    mat(c, 3, 0x30323A);
    mat(c, 4, 0xF08A30);
    mat_flat(c, 5, 0x181820);
    mat(c, 6, 0x6A4028);
    int body = bone(c, "body", -1, 0, 0, 0, 0, 1.0f, 0);
    int head = bone(c, "head", body, 0, 1.55f, 0, 0, 2.1f, 0);
    int armL = bone(c, "arm.L", body, 0.45f, 1.1f, 0, 0.95f, 1.5f, 0);
    use(c, body);
    ell(c, 0, 0.55f, 0, 0.58f, 0.55f, 0.58f, 8, 1);
    ell(c, 0, 1.2f, 0, 0.44f, 0.42f, 0.44f, 8, 1);
    for (int i = 0; i < 3; i++) eye(c, 0, 1.05f + 0.15f * (float)i, -0.43f + 0.02f * (float)i, 0.07f, 5);
    bx(c, 0, 1.5f, 0, 0.7f, 0.12f, 0.7f, 2);
    use(c, head);
    ell(c, 0, 1.85f, 0, 0.32f, 0.3f, 0.32f, 8, 1);
    eye(c, -0.1f, 1.92f, -0.3f, 0.06f, 5);
    eye(c, 0.1f, 1.92f, -0.3f, 0.06f, 5);
    const float a[3] = { 0, 1.82f, -0.28f }, bb[3] = { 0, 1.8f, -0.6f };
    tube(c, a, bb, 0.05f, 0, 5, 4, 1);
    cyl(c, 0, 2.08f, 0, 0.36f, 0.05f, 8, 3);
    cyl(c, 0, 2.12f, 0, 0.22f, 0.3f, 8, 3);
    int from = c->m->nfaces;
    use(c, armL);
    limb(c, 0.42f, 1.1f, 0, 0.95f, 1.5f, 0, 0.03f, 6);
    limb(c, 0.85f, 1.42f, 0, 1.0f, 1.65f, 0, 0.02f, 6);
    int armR = bone_mirror(c, armL);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, body, 0, 0, 3);
    f = key(k, 1);
    turn(f, body, 0, 0, -3);
    turn(f, head, 0, 10, 0);
    k = clip(c, "wave", 1, 1);
    f = key(k, 0);
    turn(f, armR, 0, 0, -35);
    f = key(k, 0.5f);
    turn(f, armR, 0, 0, -70);
}

void r_ghost(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xE8E8F8, 0xC8E0F0, 0xB0F0D0 }, 3));
    mat_flat(c, 2, 0x181830);
    int body = bone(c, "body", -1, 0, 0.5f, 0, 0, 1.5f, 0);
    int armL = bone(c, "arm.L", body, 0.4f, 1.0f, 0, 0.75f, 0.85f, 0);
    use(c, body);
    ell(c, 0, 1.15f, 0, 0.45f, 0.45f, 0.45f, 8, 1);
    const float a[3] = { 0, 1.15f, 0 }, bb[3] = { 0, 0.45f, 0 };
    tube(c, a, bb, 0.45f, 0.42f, 8, 1, 0);
    for (int i = 0; i < 8; i++) {
        float t = 2 * PI_F * ((float)i + 0.5f) / 8;
        const float p[3] = { cosf(t) * 0.3f, 0.45f, sinf(t) * 0.3f }, q[3] = { cosf(t) * 0.32f, 0.2f, sinf(t) * 0.32f };
        tube(c, p, q, 0.16f, 0, 4, 1, 1);
    }
    eye(c, -0.14f, 1.22f, -0.43f, 0.12f, 2);
    eye(c, 0.14f, 1.22f, -0.43f, 0.12f, 2);
    ell(c, 0, 1.0f, -0.42f, 0.07f, 0.1f, 0.04f, 5, 2);
    int from = c->m->nfaces;
    use(c, armL);
    ell(c, 0.55f, 0.95f, 0, 0.2f, 0.12f, 0.12f, 6, 1);
    int armR = bone_mirror(c, armL);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "float", 2, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, armL, 0, 0, 10);
    turn(f, armR, 0, 0, -10);
    f = key(k, 1);
    shift(f, body, 0, 0.25f, 0);
    turn(f, armL, 0, 0, -25);
    turn(f, armR, 0, 0, 25);
    k = clip(c, "idle", 1.5f, 1);
    key(k, 0);
    f = key(k, 0.75f);
    shift(f, body, 0, 0.08f, 0);
    turn(f, body, 0, 12, 0);
}

/* ---------------------------------------------------------------- four legs */

typedef struct {
    int body, head, tail, legFL, legBL, legFR, legBR;
} quad_t;

static void quad_anims(mc_t *c, const quad_t *q, float swing, float bob, float wag)
{
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, q->tail, 0, wag, 0);
    f = key(k, 0.6f);
    turn(f, q->tail, 0, -wag, 0);
    turn(f, q->head, 8, 0, 0);
    f = key(k, 1.2f);
    turn(f, q->tail, 0, wag, 0);
    turn(f, q->head, 0, 15, 0);
    k = clip(c, "walk", 0.8f, 1);
    for (int i = 0; i < 4; i++) {
        f = key(k, 0.2f * (float)i);
        float s = i == 0 ? 1 : i == 2 ? -1 : 0;
        turn(f, q->legFL, swing * s, 0, 0);
        turn(f, q->legBR, swing * s, 0, 0);
        turn(f, q->legFR, -swing * s, 0, 0);
        turn(f, q->legBL, -swing * s, 0, 0);
        if (s == 0) shift(f, q->body, 0, bob, 0);
        turn(f, q->head, -4 * s, 0, 0);
        turn(f, q->tail, 0, wag * 0.5f * s, 0);
    }
}

void r_dog(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xC8A070, 0x6A4028, 0xE8E4D8, 0x30323A, 0xB06030 }, 5));
    mat(c, 2, mshade(c->col[1], 1));
    mat_flat(c, 3, 0x181820);
    mat(c, 4, 0xE0B0A0);
    quad_t q;
    q.body = bone(c, "body", -1, 0, 0.55f, 0.1f, 0, 0.55f, -0.4f);
    q.head = bone(c, "head", q.body, 0, 0.62f, -0.45f, 0, 0.8f, -0.8f);
    q.tail = bone(c, "tail", q.body, 0, 0.6f, 0.45f, 0, 0.9f, 0.75f);
    q.legFL = bone(c, "legF.L", q.body, 0.15f, 0.5f, -0.3f, 0.15f, 0.05f, -0.3f);
    q.legBL = bone(c, "legB.L", q.body, 0.15f, 0.5f, 0.3f, 0.15f, 0.05f, 0.3f);
    use(c, q.body);
    ell(c, 0, 0.55f, 0, 0.26f, 0.25f, 0.5f, 8, 1);
    use(c, q.head);
    ell(c, 0, 0.72f, -0.62f, 0.2f, 0.19f, 0.22f, 7, 1);
    bx(c, 0, 0.58f, -0.86f, 0.18f, 0.16f, 0.22f, 2);              /* the muzzle */
    eye(c, 0, 0.64f, -0.97f, 0.07f, 3);
    eye(c, -0.09f, 0.78f, -0.8f, 0.05f, 3);
    eye(c, 0.09f, 0.78f, -0.8f, 0.05f, 3);
    box(c, -0.05f, 0.49f, -0.9f, 0.05f, 0.5f, -0.78f, 4);          /* the tongue */
    int style = mri(c, 0, 1);
    use(c, q.tail);
    limb(c, 0, 0.6f, 0.45f, 0, 0.9f, 0.72f, 0.04f, 2);
    int from = c->m->nfaces;
    use(c, q.head);
    if (style == 0) bx(c, 0.15f, 0.82f, -0.6f, 0.08f, 0.16f, 0.1f, 2);       /* an ear up */
    else box(c, 0.17f, 0.5f, -0.68f, 0.24f, 0.8f, -0.52f, 2);                /* a floppy ear */
    use(c, q.legFL);
    limb(c, 0.15f, 0.5f, -0.3f, 0.15f, 0.02f, -0.3f, 0.06f, 1);
    box(c, 0.08f, 0, -0.4f, 0.22f, 0.06f, -0.24f, 2);
    use(c, q.legBL);
    limb(c, 0.15f, 0.5f, 0.3f, 0.15f, 0.02f, 0.3f, 0.06f, 1);
    box(c, 0.08f, 0, 0.2f, 0.22f, 0.06f, 0.36f, 2);
    q.legFR = bone_mirror(c, q.legFL);
    q.legBR = bone_mirror(c, q.legBL);
    mirror_x(c, from);
    quad_anims(c, &q, 30, 0.03f, 30);
}

void r_horse(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x8A5A30, 0x30323A, 0xE8E4D8, 0xC8A070, 0x6A4028 }, 5));
    mat(c, 2, mpick(c, (const uint32_t[]){ 0x3A2A20, 0xE8D070, 0x181820 }, 3));
    mat(c, 3, 0x30323A);
    mat_flat(c, 4, 0x181820);
    quad_t q;
    q.body = bone(c, "body", -1, 0, 1.1f, 0.2f, 0, 1.1f, -0.5f);
    int neck = bone(c, "neck", q.body, 0, 1.3f, -0.65f, 0, 1.9f, -1.0f);
    q.head = bone(c, "head", neck, 0, 1.9f, -1.0f, 0, 1.85f, -1.5f);
    q.tail = bone(c, "tail", q.body, 0, 1.25f, 0.8f, 0, 0.6f, 1.1f);
    q.legFL = bone(c, "legF.L", q.body, 0.2f, 1.0f, -0.55f, 0.2f, 0.05f, -0.55f);
    q.legBL = bone(c, "legB.L", q.body, 0.2f, 1.0f, 0.55f, 0.2f, 0.05f, 0.55f);
    use(c, q.body);
    ell(c, 0, 1.1f, 0, 0.36f, 0.4f, 0.82f, 8, 1);
    use(c, neck);
    const float n0[3] = { 0, 1.3f, -0.6f }, n1[3] = { 0, 1.95f, -1.0f };
    tube(c, n0, n1, 0.2f, 0.16f, 6, 1, 1);
    for (int i = 0; i < 4; i++)
        bx(c, 0, 1.45f + 0.15f * (float)i, -0.56f - 0.11f * (float)i, 0.08f, 0.16f, 0.14f, 2);   /* the mane */
    use(c, q.head);
    bx(c, 0, 1.75f, -1.25f, 0.26f, 0.3f, 0.55f, 1);
    bx(c, 0, 1.68f, -1.5f, 0.2f, 0.2f, 0.12f, 3);                 /* the muzzle */
    eye(c, -0.135f, 1.95f, -1.1f, 0.06f, 4);
    eye(c, 0.135f, 1.95f, -1.1f, 0.06f, 4);
    use(c, q.tail);
    const float t0[3] = { 0, 1.25f, 0.8f }, t1[3] = { 0, 0.6f, 1.05f };
    tube(c, t0, t1, 0.08f, 0.03f, 5, 2, 1);
    int from = c->m->nfaces;
    use(c, q.head);
    bx(c, 0.1f, 2.05f, -1.05f, 0.06f, 0.16f, 0.08f, 1);           /* an ear */
    use(c, q.legFL);
    limb(c, 0.2f, 1.05f, -0.55f, 0.2f, 0.08f, -0.55f, 0.08f, 1);
    bx(c, 0.2f, 0, -0.55f, 0.18f, 0.1f, 0.2f, 3);
    use(c, q.legBL);
    limb(c, 0.2f, 1.05f, 0.55f, 0.2f, 0.08f, 0.55f, 0.08f, 1);
    bx(c, 0.2f, 0, 0.55f, 0.18f, 0.1f, 0.2f, 3);
    q.legFR = bone_mirror(c, q.legFL);
    q.legBR = bone_mirror(c, q.legBL);
    mirror_x(c, from);
    quad_anims(c, &q, 28, 0.05f, 20);
}

/* ---------------------------------------------------------------- others */

void r_bird(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x3A62D8, 0xD83A3A, 0xF0D040, 0x30323A, 0xE8E4D8, 0x3CB043 }, 6));
    mat(c, 2, mshade(c->col[1], 1));
    mat(c, 3, 0xF08A30);
    mat_flat(c, 4, 0x181820);
    int body = bone(c, "body", -1, 0, 0.35f, 0.1f, 0, 0.35f, -0.3f);
    int head = bone(c, "head", body, 0, 0.45f, -0.25f, 0, 0.6f, -0.4f);
    int wingL = bone(c, "wing.L", body, 0.15f, 0.4f, 0.05f, 0.65f, 0.4f, 0.05f);
    use(c, body);
    ell(c, 0, 0.35f, 0, 0.2f, 0.19f, 0.3f, 7, 1);
    const float tf2[8] = { -0.12f, 0.25f, 0.12f, 0.25f, 0.2f, 0.55f, -0.2f, 0.55f };
    prism(c, 1, tf2, 4, 0.33f, 0.37f, 2);
    use(c, head);
    ell(c, 0, 0.55f, -0.3f, 0.15f, 0.15f, 0.15f, 7, 1);
    const float b0[3] = { 0, 0.54f, -0.42f }, b1[3] = { 0, 0.52f, -0.62f };
    tube(c, b0, b1, 0.05f, 0, 4, 3, 1);
    eye(c, -0.1f, 0.6f, -0.38f, 0.05f, 4);
    eye(c, 0.1f, 0.6f, -0.38f, 0.05f, 4);
    int from = c->m->nfaces;
    use(c, wingL);
    const float wing[8] = { 0.14f, -0.12f, 0.62f, -0.06f, 0.68f, 0.2f, 0.14f, 0.26f };
    prism(c, 1, wing, 4, 0.38f, 0.42f, 2);
    use(c, body);
    cyl(c, 0.07f, 0, -0.02f, 0.025f, 0.18f, 4, 3);
    box(c, 0.03f, 0, -0.1f, 0.11f, 0.02f, 0.02f, 3);
    int wingR = bone_mirror(c, wingL);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    key(k, 0);
    mesh_key_t *f = key(k, 0.7f);
    turn(f, head, 0, 25, 0);
    f = key(k, 1.4f);
    turn(f, head, 20, 0, 0);
    k = clip(c, "fly", 0.5f, 1);
    f = key(k, 0);
    turn(f, wingL, 0, 0, 45);
    turn(f, wingR, 0, 0, -45);
    f = key(k, 0.25f);
    turn(f, wingL, 0, 0, -35);
    turn(f, wingR, 0, 0, 35);
    shift(f, body, 0, 0.08f, 0);
}

void r_fish(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xF08A30, 0x3A62D8, 0x48B8E8, 0xD83A3A, 0xF0D040 }, 5));
    mat(c, 2, mshade(c->col[1], 1));
    mat_flat(c, 3, 0x181820);
    int body = bone(c, "body", -1, 0, 0.5f, 0, 0, 0.5f, -0.5f);
    int tail = bone(c, "tail", body, 0, 0.5f, 0.45f, 0, 0.5f, 0.95f);
    use(c, body);
    ell(c, 0, 0.5f, 0, 0.14f, 0.3f, 0.55f, 8, 1);
    const float dorsal[6] = { 0.72f, -0.15f, 0.95f, 0.1f, 0.6f, 0.25f };
    prism(c, 0, dorsal, 3, -0.02f, 0.02f, 2);
    const float finL[6] = { 0.12f, -0.1f, 0.4f, 0.05f, 0.12f, 0.15f };
    prism(c, 1, finL, 3, 0.42f, 0.46f, 2);
    const float finR[6] = { -0.12f, -0.1f, -0.4f, 0.05f, -0.12f, 0.15f };
    prism(c, 1, finR, 3, 0.42f, 0.46f, 2);
    eye(c, -0.13f, 0.58f, -0.3f, 0.06f, 3);
    eye(c, 0.13f, 0.58f, -0.3f, 0.06f, 3);
    use(c, tail);
    const float t0[3] = { 0, 0.5f, 0.45f }, t1[3] = { 0, 0.5f, 0.7f };
    tube(c, t0, t1, 0.12f, 0.05f, 6, 1, 0);
    const float fin[8] = { 0.5f, 0.65f, 0.85f, 1.0f, 0.5f, 0.85f, 0.15f, 1.0f };
    prism(c, 0, fin, 4, -0.02f, 0.02f, 2);
    mesh_clip_t *k = clip(c, "swim", 0.8f, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, tail, 0, 30, 0);
    turn(f, body, 0, -6, 0);
    f = key(k, 0.4f);
    turn(f, tail, 0, -30, 0);
    turn(f, body, 0, 6, 0);
    k = clip(c, "idle", 2, 1);
    key(k, 0);
    f = key(k, 1);
    shift(f, body, 0, 0.1f, 0);
    turn(f, tail, 0, 12, 0);
}

void r_slime(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x3CB043, 0x48B8E8, 0xD83A3A, 0x8A4AD0, 0xF0D040 }, 5));
    mat_flat(c, 2, 0x181820);
    mat_flat(c, 3, mshade(c->col[1], 4));
    int body = bone(c, "body", -1, 0, 0, 0, 0, 0.6f, 0);
    use(c, body);
    ell(c, 0, 0.42f, 0, 0.5f, 0.42f, 0.5f, 8, 1);
    eye(c, -0.15f, 0.5f, -0.44f, 0.1f, 2);
    eye(c, 0.15f, 0.5f, -0.44f, 0.1f, 2);
    if (c->seed % 2) box(c, -0.1f, 0.32f, -0.47f, 0.1f, 0.36f, -0.44f, 2);
    box(c, -0.3f, 0.66f, -0.2f, -0.15f, 0.72f, -0.05f, 3);
    mesh_clip_t *k = clip(c, "idle", 1.5f, 1);
    key(k, 0);
    mesh_key_t *f = key(k, 0.75f);
    shift(f, body, 0, 0.06f, 0);
    k = clip(c, "bounce", 0.8f, 1);
    f = key(k, 0);
    shift(f, body, 0, 0, 0);
    f = key(k, 0.4f);
    shift(f, body, 0, 0.45f, 0);
}

void r_spider(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x30323A, 0x6A4028, 0x8A4AD0, 0x3CB043 }, 4));
    mat(c, 2, mshade(c->col[1], 1));
    mat_flat(c, 3, 0xE03030);
    int body = bone(c, "body", -1, 0, 0.45f, 0.2f, 0, 0.45f, -0.3f);
    use(c, body);
    ell(c, 0, 0.5f, 0.35f, 0.38f, 0.3f, 0.42f, 8, 1);
    ell(c, 0, 0.42f, -0.25f, 0.22f, 0.2f, 0.25f, 7, 2);
    eye(c, -0.08f, 0.5f, -0.48f, 0.06f, 3);
    eye(c, 0.08f, 0.5f, -0.48f, 0.06f, 3);
    eye(c, -0.15f, 0.44f, -0.45f, 0.04f, 3);
    eye(c, 0.15f, 0.44f, -0.45f, 0.04f, 3);
    int legs[4];
    int from = c->m->nfaces;
    for (int i = 0; i < 4; i++) {
        char name[16];
        strcpy(name, "leg1.L");
        name[3] = (char)('1' + i);
        float z = -0.35f + 0.22f * (float)i, zz = z + (i < 2 ? -0.2f : 0.2f);
        legs[i] = bone(c, name, body, 0.15f, 0.4f, z, 0.85f, 0, zz);
        limb(c, 0.15f, 0.4f, z, 0.55f, 0.7f, zz, 0.035f, 2);
        limb(c, 0.55f, 0.7f, zz, 0.85f, 0, zz, 0.03f, 2);
    }
    int legsR[4];
    for (int i = 0; i < 4; i++) legsR[i] = bone_mirror(c, legs[i]);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    key(k, 0);
    mesh_key_t *f = key(k, 1);
    shift(f, body, 0, -0.08f, 0);
    k = clip(c, "walk", 0.6f, 1);
    for (int i = 0; i < 2; i++) {
        f = key(k, 0.3f * (float)i);
        float s = i ? -1 : 1;
        for (int j = 0; j < 4; j++) {
            float a = 14 * s * ((j & 1) ? 1 : -1);
            turn(f, legs[j], 0, a, 0);
            turn(f, legsR[j], 0, -a, 0);
        }
    }
}

void r_dragon(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x3CB043, 0xD83A3A, 0x8A4AD0, 0x30323A, 0x3A62D8 }, 5));
    mat(c, 2, mshade(c->col[1], 1));                 /* the wings */
    mat(c, 3, 0xE8D8A0);                             /* the belly */
    mat_flat(c, 4, 0xF0D040);
    mat(c, 5, 0xE8E4D8);
    int body = bone(c, "body", -1, 0, 0.95f, 0.3f, 0, 0.95f, -0.5f);
    int neck = bone(c, "neck", body, 0, 1.15f, -0.7f, 0, 1.85f, -1.3f);
    int head = bone(c, "head", neck, 0, 1.85f, -1.3f, 0, 1.85f, -1.8f);
    int tail = bone(c, "tail", body, 0, 0.9f, 0.85f, 0, 0.9f, 2.1f);
    int wingL = bone(c, "wing.L", body, 0.3f, 1.35f, 0, 1.6f, 1.35f, -0.3f);
    int legFL = bone(c, "legF.L", body, 0.3f, 0.75f, -0.45f, 0.3f, 0.05f, -0.45f);
    int legBL = bone(c, "legB.L", body, 0.3f, 0.75f, 0.45f, 0.3f, 0.05f, 0.45f);
    use(c, body);
    ell(c, 0, 0.95f, 0, 0.45f, 0.45f, 0.9f, 8, 1);
    bx(c, 0, 0.5f, 0, 0.5f, 0.3f, 1.3f, 3);
    for (int i = 0; i < 4; i++) {                    /* the spikes on the back */
        const float s0[3] = { 0, 1.3f, -0.45f + 0.3f * (float)i }, s1[3] = { 0, 1.6f, -0.4f + 0.3f * (float)i };
        tube(c, s0, s1, 0.06f, 0, 4, 5, 1);
    }
    use(c, neck);
    const float n0[3] = { 0, 1.15f, -0.65f }, n1[3] = { 0, 1.85f, -1.3f };
    tube(c, n0, n1, 0.22f, 0.17f, 6, 1, 1);
    use(c, head);
    bx(c, 0, 1.7f, -1.5f, 0.36f, 0.3f, 0.6f, 1);
    bx(c, 0, 1.62f, -1.6f, 0.3f, 0.1f, 0.5f, 3);
    const float h0[3] = { 0.1f, 1.98f, -1.3f }, h1[3] = { 0.15f, 2.25f, -1.15f };
    tube(c, h0, h1, 0.05f, 0, 4, 5, 1);
    const float h2[3] = { -0.1f, 1.98f, -1.3f }, h3[3] = { -0.15f, 2.25f, -1.15f };
    tube(c, h2, h3, 0.05f, 0, 4, 5, 1);
    eye(c, -0.185f, 1.9f, -1.6f, 0.07f, 4);
    eye(c, 0.185f, 1.9f, -1.6f, 0.07f, 4);
    use(c, tail);
    const float t0[3] = { 0, 0.9f, 0.85f }, t1[3] = { 0, 0.75f, 1.5f }, t2[3] = { 0, 0.9f, 2.1f };
    tube(c, t0, t1, 0.2f, 0.12f, 6, 1, 0);
    tube(c, t1, t2, 0.12f, 0, 6, 1, 0);
    int from = c->m->nfaces;
    use(c, wingL);
    const float wing[10] = { 0.3f, -0.3f, 1.6f, -0.9f, 1.95f, 0.25f, 1.2f, 0.75f, 0.3f, 0.55f };
    prism(c, 1, wing, 5, 1.33f, 1.37f, 2);
    limb(c, 0.3f, 1.35f, -0.1f, 1.6f, 1.35f, -0.85f, 0.04f, 1);
    use(c, legFL);
    limb(c, 0.3f, 0.75f, -0.45f, 0.32f, 0.05f, -0.45f, 0.1f, 1);
    box(c, 0.18f, 0, -0.65f, 0.46f, 0.1f, -0.35f, 2);
    use(c, legBL);
    limb(c, 0.3f, 0.75f, 0.45f, 0.32f, 0.05f, 0.45f, 0.11f, 1);
    box(c, 0.18f, 0, 0.25f, 0.46f, 0.1f, 0.55f, 2);
    int wingR = bone_mirror(c, wingL);
    int legFR = bone_mirror(c, legFL);
    int legBR = bone_mirror(c, legBL);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "idle", 2.5f, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, tail, 0, 10, 0);
    f = key(k, 1.25f);
    turn(f, tail, 0, -10, 0);
    turn(f, neck, 6, 0, 0);
    turn(f, head, 0, 15, 0);
    turn(f, wingL, 0, 0, 8);
    turn(f, wingR, 0, 0, -8);
    k = clip(c, "fly", 0.7f, 1);
    f = key(k, 0);
    turn(f, wingL, 0, 0, 40);
    turn(f, wingR, 0, 0, -40);
    turn(f, legFL, -30, 0, 0); turn(f, legFR, -30, 0, 0);
    turn(f, legBL, -20, 0, 0); turn(f, legBR, -20, 0, 0);
    f = key(k, 0.35f);
    turn(f, wingL, 0, 0, -35);
    turn(f, wingR, 0, 0, 35);
    turn(f, legFL, -30, 0, 0); turn(f, legFR, -30, 0, 0);
    turn(f, legBL, -20, 0, 0); turn(f, legBR, -20, 0, 0);
    shift(f, body, 0, 0.15f, 0);
    turn(f, tail, 10, 0, 0);
    k = clip(c, "walk", 1, 1);
    for (int i = 0; i < 4; i++) {
        f = key(k, 0.25f * (float)i);
        float s = i == 0 ? 1 : i == 2 ? -1 : 0;
        turn(f, legFL, 25 * s, 0, 0); turn(f, legBR, 25 * s, 0, 0);
        turn(f, legFR, -25 * s, 0, 0); turn(f, legBL, -25 * s, 0, 0);
        turn(f, tail, 0, 12 * s, 0);
        turn(f, neck, 0, -6 * s, 0);
        if (s == 0) shift(f, body, 0, 0.04f, 0);
    }
}

/* the mech: a big rounded body with the pilot in the cockpit at the front,
 * on two bent legs with wide feet, a gun pod on each arm, fins on top,
 * thrusters at the back (an anime mech, as the sprite recipe "robot") */
void r_mech(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xF080B0, 0xF080B0, 0x48B8E8, 0xD83A3A, 0xF0D040, 0xB8C0CC }, 6));
    mat(c, 2, mshade(c->col[1], 1));                 /* the darker plates */
    mat(c, 3, 0x30323A);                             /* joints, barrels */
    mat(c, 4, 0xB8C0CC);                             /* metal */
    mat_flat(c, 5, 0x1E5048);                        /* inside the cockpit */
    mat_flat(c, 6, 0x40F0A0);                        /* the lights */
    mat_flat(c, 7, 0xF0D040);                        /* the thruster rings */
    mat(c, 8, mpick(c, MP_SKIN, 5));
    mat(c, 9, mpick(c, MP_HAIR, 7));
    mat(c, 10, 0x2A3A8A);                            /* the pilot's suit */
    mat_flat(c, 11, 0x80E8D0);                       /* the canopy rim glass */
    int body = bone(c, "body", -1, 0, 2.2f, 0.3f, 0, 2.2f, -0.6f);
    int thighL = bone(c, "thigh.L", body, 0.85f, 1.75f, 0.1f, 1.05f, 1.0f, -0.6f);
    int shinL = bone(c, "shin.L", thighL, 1.05f, 1.0f, -0.6f, 1.0f, 0.3f, 0.2f);
    int footL = bone(c, "foot.L", shinL, 1.0f, 0.3f, 0.2f, 1.0f, 0.05f, -0.5f);
    int armL = bone(c, "arm.L", body, 1.05f, 2.1f, -0.2f, 1.4f, 1.5f, -0.6f);

    /* the hull: a chamfered body across x, rounded sides, a top plate */
    use(c, body);
    const float hull[16] = { 1.65f, -0.9f, 1.9f, -1.25f, 2.15f, -1.35f, 2.7f, -0.8f, 2.85f, 0.3f, 2.7f, 1.1f, 2.2f, 1.3f, 1.7f, 0.9f };
    prism(c, 0, hull, 8, -0.85f, 0.85f, 1);
    ell(c, -0.85f, 2.25f, 0.1f, 0.4f, 0.55f, 1.05f, 8, 1);
    ell(c, 0.85f, 2.25f, 0.1f, 0.4f, 0.55f, 1.05f, 8, 1);
    bx(c, 0, 2.82f, 0.35f, 1.1f, 0.1f, 1.2f, 2);
    box(c, -0.25f, 2.9f, 0.0f, 0.25f, 2.95f, 0.7f, 11);             /* the top light */
    /* the cockpit: the inside, a dark plate on the front chamfer, the
     * pilot before it, a rim of glass around */
    {
        const float nx = 0.7071f, nz = -0.7071f;             /* out of the chamfer */
        float ay = 2.15f, az = -1.35f, by = 2.7f, bz = -0.8f;
        const float in[8] = { ay + nx * 0.01f, az + nz * 0.01f, by + nx * 0.01f, bz + nz * 0.01f,
                              by + nx * 0.04f, bz + nz * 0.04f, ay + nx * 0.04f, az + nz * 0.04f };
        prism(c, 0, in, 4, -0.55f, 0.55f, 5);
        const float rim[8] = { ay - 0.05f, az - 0.05f, by + 0.05f, bz + 0.05f,
                               by + nx * 0.07f + 0.05f, bz + nz * 0.07f + 0.05f, ay + nx * 0.07f - 0.05f, az + nz * 0.07f - 0.05f };
        prism(c, 0, rim, 4, -0.66f, -0.55f, 11);
        prism(c, 0, rim, 4, 0.55f, 0.66f, 11);
        const float top[8] = { by, bz, by + 0.08f, bz, by + nx * 0.08f + 0.08f, bz + nz * 0.08f, by + nx * 0.08f, bz + nz * 0.08f };
        prism(c, 0, top, 4, -0.66f, 0.66f, 2);
    }
    bx(c, 0, 1.98f, -0.98f, 0.46f, 0.4f, 0.3f, 10);                 /* the pilot */
    ell(c, 0, 2.52f, -1.08f, 0.14f, 0.15f, 0.14f, 6, 8);
    bx(c, 0, 2.58f, -1.08f, 0.3f, 0.12f, 0.3f, 9);
    bx(c, 0, 1.6f, 0.1f, 1.5f, 0.1f, 1.9f, 2);                      /* the under plate */
    for (int s = -1; s <= 1; s += 2) {
        const float t0[3] = { s * 0.5f, 2.25f, 1.2f }, t1[3] = { s * 0.5f, 2.25f, 1.7f };
        tube(c, t0, t1, 0.24f, 0.24f, 8, 4, 1);                      /* the thrusters */
        const float r0[3] = { s * 0.5f, 2.25f, 1.65f }, r1[3] = { s * 0.5f, 2.25f, 1.75f };
        tube(c, r0, r1, 0.28f, 0.28f, 8, 7, 1);
        tf_set(c, s * 0.5f, 2.75f, 0.45f, 0, 0, s * -22.0f);         /* the top fins, swept back */
        const float fin[8] = { 0, 0.05f, 1.25f, -0.55f, 1.25f, -0.25f, 0.15f, 0.9f };
        prism(c, 0, fin, 4, -0.04f, 0.04f, 1);
        tf_off(c);
        const float side[8] = { s * 1.0f, 0.35f, s * 2.1f, 0.85f, s * 2.1f, 1.05f, s * 1.0f, 1.15f };
        prism(c, 1, side, 4, 2.48f, 2.55f, 1);                       /* the side wings */
        eye(c, s * 0.62f, 1.95f, -1.26f, 0.12f, 6);                  /* the lights */
    }
    int from = c->m->nfaces;
    /* the left leg: a chunky thigh down and forward, the knee, the shin
     * down and back, a wide flat foot with a toe */
    use(c, thighL);
    ell(c, 0.85f, 1.75f, 0.1f, 0.32f, 0.3f, 0.32f, 6, 3);
    limb(c, 0.88f, 1.7f, 0.05f, 1.05f, 1.0f, -0.6f, 0.3f, 1);
    tf_set(c, 1.17f, 1.37f, -0.3f, 40, 0, 0);
    bx(c, 0, -0.45f, 0, 0.5f, 0.9f, 0.74f, 2);
    tf_off(c);
    use(c, shinL);
    ell(c, 1.05f, 1.0f, -0.6f, 0.26f, 0.26f, 0.26f, 6, 3);
    limb(c, 1.05f, 1.0f, -0.6f, 1.0f, 0.3f, 0.2f, 0.19f, 2);
    ell(c, 1.0f, 0.3f, 0.2f, 0.2f, 0.2f, 0.2f, 6, 3);
    use(c, footL);
    box(c, 0.65f, 0, -0.5f, 1.35f, 0.3f, 0.45f, 1);
    const float toe[3] = { 1.0f, 0.15f, -0.5f }, toe2[3] = { 1.0f, 0.08f, -0.9f };
    tube(c, toe, toe2, 0.2f, 0.07f, 4, 3, 1);
    box(c, 0.75f, 0.3f, -0.3f, 1.25f, 0.4f, 0.3f, 2);
    /* the left arm: the shoulder, a short arm, the gun pod with three barrels */
    use(c, armL);
    ell(c, 1.05f, 2.1f, -0.2f, 0.25f, 0.25f, 0.25f, 6, 3);
    limb(c, 1.05f, 2.1f, -0.2f, 1.4f, 1.55f, -0.5f, 0.17f, 1);
    ell(c, 1.4f, 1.55f, -0.5f, 0.2f, 0.2f, 0.2f, 6, 3);
    const float g0[3] = { 1.42f, 1.5f, -0.05f }, g1[3] = { 1.42f, 1.5f, -1.3f };
    tube(c, g0, g1, 0.32f, 0.3f, 8, 1, 1);
    bx(c, 1.42f, 1.78f, -0.65f, 0.4f, 0.1f, 0.9f, 2);
    const float l0[3] = { 1.42f, 1.5f, -1.3f }, l1[3] = { 1.42f, 1.5f, -1.38f };
    tube(c, l0, l1, 0.33f, 0.33f, 8, 6, 1);
    const float d0[3] = { 1.42f, 1.5f, -1.38f }, d1[3] = { 1.42f, 1.5f, -1.45f };
    tube(c, d0, d1, 0.26f, 0.26f, 8, 3, 1);
    for (int i = 0; i < 3; i++) {
        float t = 2 * PI_F * (float)i / 3 + PI_F / 2;
        const float b0[3] = { 1.42f + cosf(t) * 0.13f, 1.5f + sinf(t) * 0.13f, -1.4f };
        const float b1[3] = { b0[0], b0[1], -1.85f };
        tube(c, b0, b1, 0.07f, 0.07f, 6, 3, 1);
    }
    int thighR = bone_mirror(c, thighL);
    int shinR = c->bmir[shinL], footR = c->bmir[footL];
    int armR = bone_mirror(c, armL);
    mirror_x(c, from);
    mesh_clip_t *k = clip(c, "idle", 2, 1);
    mesh_key_t *f = key(k, 0);
    turn(f, armL, 0, 4, 0);
    turn(f, armR, 0, -4, 0);
    f = key(k, 1);
    shift(f, body, 0, -0.06f, 0);
    turn(f, thighL, 3, 0, 0); turn(f, thighR, 3, 0, 0);
    turn(f, shinL, -3, 0, 0); turn(f, shinR, -3, 0, 0);
    k = clip(c, "walk", 1.2f, 1);
    for (int i = 0; i < 4; i++) {
        f = key(k, 0.3f * (float)i);
        float s = i == 0 ? 1 : i == 2 ? -1 : 0;
        turn(f, thighL, 28 * s, 0, 0);
        turn(f, thighR, -28 * s, 0, 0);
        turn(f, shinL, s > 0 ? -35 : s < 0 ? 15 : -10, 0, 0);
        turn(f, shinR, s < 0 ? -35 : s > 0 ? 15 : -10, 0, 0);
        turn(f, footL, s > 0 ? 10 : 0, 0, 0);
        turn(f, footR, s < 0 ? 10 : 0, 0, 0);
        turn(f, armL, 0, -5 * s, 0);
        turn(f, armR, 0, -5 * s, 0);
        turn(f, body, 0, 0, 3 * s);
        if (s == 0) shift(f, body, 0, 0.08f, 0);
    }
    k = clip(c, "fire", 0.4f, 1);
    f = key(k, 0);
    shift(f, armL, 0, 0, 0.12f);
    f = key(k, 0.2f);
    shift(f, armR, 0, 0, 0.12f);
    turn(f, body, 2, 0, 0);
}
