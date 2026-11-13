/*
 * 3D recipes of the assistant (M30): the base of a model for bm Studio and
 * bm Animator from a recipe ("cube", "tree", "house", "hero", "mech"...)
 * and a seed. Low-poly procedural models in the style of the console's
 * tiles and blocks: boxes, tubes, spheres and prisms with flat colours (a
 * lighter top, a darker bottom, as the sprite recipes shade), facing -z,
 * standing on y = 0, one unit = one block of bm Studio. People, animals and
 * machines come with a skeleton (bones, each face on one) and a few
 * animations (idle, walk...), ready for bm Animator. The colours, the
 * size and the proportions can come from the words of a request ("casa
 * rossa grande", "tall tree"); the same seed always gives the same model,
 * the next seed a variant.
 */
#ifndef AI_MESH_H
#define AI_MESH_H

#include <stdint.h>

#define MESH_MAX_FACES  2048
#define MESH_MAX_BONES  40
#define MESH_MAX_CLIPS  4
#define MESH_MAX_KEYS   6
#define MESH_NO_COLOR   0xFFFFFFFFu

typedef struct {
    float p[4][3];              /* the corners, clockwise from the side that shows */
    uint32_t c;                 /* 0xRRGGBB */
    uint8_t b[4];               /* the bone of each corner (0 = the first) */
    uint8_t n;                  /* 3 or 4 corners */
} mesh_face_t;

typedef struct {
    char name[16];
    int parent;                 /* -1: none */
    float head[3], tail[3];
} mesh_bone_t;

typedef struct {
    float q[4];                 /* x, y, z, w: the turn, relative to the parent */
    float t[3];                 /* the move */
} mesh_pose_t;

typedef struct {
    float t;
    mesh_pose_t pose[MESH_MAX_BONES];
} mesh_key_t;

typedef struct {
    char name[16];
    int loop;
    float length;
    int nkeys;
    mesh_key_t keys[MESH_MAX_KEYS];
} mesh_clip_t;

typedef struct {
    int nfaces;
    mesh_face_t faces[MESH_MAX_FACES];
    int nbones;
    mesh_bone_t bones[MESH_MAX_BONES];
    int nclips;
    mesh_clip_t clips[MESH_MAX_CLIPS];
} mesh_model_t;

typedef struct {
    const char *gen;            /* recipe id */
    uint32_t seed;
    uint32_t color[2];          /* main / second colour, 0xRRGGBB or MESH_NO_COLOR */
    float scale;                /* 1: the recipe's size */
    float tall, wide;           /* proportions: "alto" 1.3, "basso" 0.75, "largo"... */
    int rig;                    /* 1: the skeleton and the animations, when the recipe has them */
} mesh_req_t;

/* the defaults of a request: seed 1, no colours, size 1, with the skeleton */
void mesh_req_init(mesh_req_t *r, const char *gen);

/* colours ("rosso", "blue"...), size ("piccolo", "big", "huge"), proportions
 * ("alto", "tall", "largo"...) and "senza scheletro" from the words of a request */
void mesh_parse(const char *text, mesh_req_t *r);

/* 0, or -1 for an unknown recipe */
int mesh_make(const mesh_req_t *r, mesh_model_t *out);

/* a model written in the part language (mesh_script.c): 0, or -1 with the
 * error ("line 12: ...") in err */
int mesh_script(const char *text, mesh_model_t *out, char *err, int errlen);

/* the recipes: id, a short English name, and whether it has a skeleton */
int mesh_recipes(void);
const char *mesh_recipe_id(int i);
const char *mesh_recipe_name(int i);
int mesh_recipe_rigged(int i);
int mesh_find(const char *gen);

#endif
