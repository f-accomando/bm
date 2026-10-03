/*
 * The inside of the 3D recipes (mesh.c, mesh_chars.c): the canvas a recipe
 * draws on and its primitives. Every primitive takes a material (1 = the
 * main colour, 2 = the second, others the recipe's own); a face gets the
 * material's colour shaded by where it looks (a lighter top, a darker
 * bottom) unless the material is flat, and the bone of the moment.
 */
#ifndef AI_MESH_INT_H
#define AI_MESH_INT_H

#include "mesh.h"

#define NMAT 16
#define PI_F 3.14159265f

typedef struct {
    mesh_model_t *m;
    uint32_t rng, seed;
    uint32_t col[NMAT];
    uint8_t flat[NMAT];         /* one shade: eyes, glass, lights */
    int bone;                   /* the bone of the faces made now */
    int bmir[MESH_MAX_BONES];   /* the mirror of each bone (mirror_x) */
    float tall, wide;           /* the request's proportions */
    int rig;                    /* the request wants the skeleton */
    int tf_on;
    float tf[12];               /* the current transform: p' = R p + t */
} mc_t;

/* random: [0, 1), [a, b] integers, [a, b] floats, one of a list */
float mrnd(mc_t *c);
int mri(mc_t *c, int a, int b);
float mrf(mc_t *c, float a, float b);
uint32_t mpick(mc_t *c, const uint32_t *list, int n);

/* the material colours: the request's, or the palettes */
void mat(mc_t *c, int m, uint32_t rgb);
void mat_flat(mc_t *c, int m, uint32_t rgb);
uint32_t mshade(uint32_t c, int level);     /* 0 dark .. 2 the colour .. 4 light */
uint32_t mmix(uint32_t a, uint32_t b, float k);

extern const uint32_t MP_BRIGHT[8], MP_SKIN[5], MP_HAIR[7], MP_METAL[4], MP_WOOD[4], MP_LEAF[4],
    MP_STONE[4], MP_CLOTH[6];

/* the transform of what comes next: turned (degrees around x, y, z, as
 * draw3d) then moved; tf_off() goes back to the model's own frame */
void tf_set(mc_t *c, float x, float y, float z, float rx, float ry, float rz);
void tf_off(mc_t *c);

/* a face from its corners (3 or 4), clockwise from the side that shows */
void face(mc_t *c, const float (*p)[3], int n, int m);
/* the same, the side that shows away from the point (ox, oy, oz) */
void face_out(mc_t *c, const float (*p)[3], int n, int m, float ox, float oy, float oz);

/* a box from corner to corner */
void box(mc_t *c, float x0, float y0, float z0, float x1, float y1, float z1, int m);
/* a box centred on x, z, standing on y: width, height, depth */
void bx(mc_t *c, float x, float y, float z, float w, float h, float d, int m);
/* a tube from a to b with the radii ra and rb (0: a point), n sides, with
 * caps; cylinders, cones, legs, barrels */
void tube(mc_t *c, const float *a, const float *b, float ra, float rb, int n, int m, int caps);
/* a vertical cylinder standing on y with radius r and height h */
void cyl(mc_t *c, float x, float y, float z, float r, float h, int n, int m);
/* an ellipsoid of radii rx, ry, rz with n sides and n / 2 rings */
void ell(mc_t *c, float x, float y, float z, float rx, float ry, float rz, int n, int m);
/* a convex polygon (u, v pairs) on the plane of `axis` (0 x, 1 y, 2 z),
 * extruded from w0 to w1 along it; the other two axes in order (y, z for
 * axis x; x, z for y; x, y for z) */
void prism(mc_t *c, int axis, const float *uv, int n, float w0, float w1, int m);
/* a wedge: a box whose top slopes from height h0 at z0 to h1 at z1 */
void wedge(mc_t *c, float x0, float y0, float z0, float x1, float z1, float h0, float h1, int m);

/* the faces made since `from` mirrored in x (and their bones, by bmir) */
void mirror_x(mc_t *c, int from);

/* a bone; parent -1 for a root. Returns its index and makes it current. */
int bone(mc_t *c, const char *name, int parent, float hx, float hy, float hz, float tx, float ty, float tz);
/* the mirror of bone i and its children: ".L" <-> ".R"; sets bmir */
int bone_mirror(mc_t *c, int i);
void use(mc_t *c, int b);                   /* the bone of what comes next */

/* an animation; a keyframe at t (the rest pose); a bone turned (degrees
 * around x, y, z: x forward swings a leg, hanging down, towards -z) or moved */
mesh_clip_t *clip(mc_t *c, const char *name, float length, int loop);
mesh_key_t *key(mesh_clip_t *k, float t);
void turn(mesh_key_t *k, int b, float rx, float ry, float rz);
void shift(mesh_key_t *k, int b, float x, float y, float z);

/* before and after a recipe or a script (mesh.c) */
void mesh_reset_materials(mc_t *c);
void mesh_finish(mesh_model_t *out, float scale, float tall, float wide, int rig);

/* the characters (mesh_chars.c) */
void r_hero(mc_t *c);
void r_knight(mc_t *c);
void r_robot(mc_t *c);
void r_mech(mc_t *c);
void r_dog(mc_t *c);
void r_horse(mc_t *c);
void r_bird(mc_t *c);
void r_fish(mc_t *c);
void r_slime(mc_t *c);
void r_spider(mc_t *c);
void r_dragon(mc_t *c);
void r_ghost(mc_t *c);
void r_skeleton(mc_t *c);
void r_snowman(mc_t *c);

#endif
