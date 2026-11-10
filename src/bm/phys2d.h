/*
 * phys2d.h: Original compact bare-metal 2D rigid body impulse physics engine.
 * Fixed pools: 128 bodies, 256 contacts, 64 joints (~32 KB total per world).
 */
#ifndef BM_PHYS2D_H
#define BM_PHYS2D_H

#include <stdint.h>

#define PHYS_MAX_BODIES   128
#define PHYS_MAX_CONTACTS 256
#define PHYS_MAX_JOINTS   64
#define PHYS_MAX_VERTS    8

typedef enum {
    PHYS_STATIC    = 0,
    PHYS_KINEMATIC = 1,
    PHYS_DYNAMIC   = 2
} phys_body_type_t;

typedef enum {
    PHYS_SHAPE_CIRCLE = 0,
    PHYS_SHAPE_BOX    = 1,
    PHYS_SHAPE_POLY   = 2
} phys_shape_type_t;

typedef enum {
    PHYS_JOINT_DISTANCE = 0,
    PHYS_JOINT_REVOLUTE = 1,
    PHYS_JOINT_WELD     = 2
} phys_joint_type_t;

typedef struct {
    int active;
    int id;
    phys_body_type_t type;
    phys_shape_type_t shape;

    float x, y, angle;
    float vx, vy, av;
    float fx, fy, torque;

    float mass, inv_mass;
    float inertia, inv_inertia;
    float restitution;
    float friction;
    float linear_damping;
    float angular_damping;

    float radius;
    float w, h;
    int nverts;
    float verts[PHYS_MAX_VERTS * 2];
    float normals[PHYS_MAX_VERTS * 2];
} phys_body_t;

typedef struct {
    int a, b;
    float nx, ny;
    float px, py;
    float depth;
} phys_contact_t;

typedef struct {
    int active;
    phys_joint_type_t type;
    int a, b;
    float lx1, ly1;
    float lx2, ly2;
    float rest_len;
    float ref_angle;
} phys_joint_t;

typedef struct {
    float gx, gy;
    int nbodies;
    int njoints;
    int ncontacts;
    phys_body_t bodies[PHYS_MAX_BODIES];
    phys_joint_t joints[PHYS_MAX_JOINTS];
    phys_contact_t contacts[PHYS_MAX_CONTACTS];
} phys_world_t;

void phys_world_init(phys_world_t *w, float gx, float gy);
int  phys_add_circle(phys_world_t *w, phys_body_type_t type, float x, float y, float radius, float density);
int  phys_add_box(phys_world_t *w, phys_body_type_t type, float x, float y, float bw, float bh, float density);
int  phys_add_poly(phys_world_t *w, phys_body_type_t type, float x, float y, const float *pts, int nverts, float density);
void phys_remove_body(phys_world_t *w, int id);

int  phys_add_joint_distance(phys_world_t *w, int a, int b, float wx1, float wy1, float wx2, float wy2, float len);
int  phys_add_joint_revolute(phys_world_t *w, int a, int b, float wx, float wy);
int  phys_add_joint_weld(phys_world_t *w, int a, int b, float wx, float wy);

void phys_apply_force(phys_world_t *w, int id, float fx, float fy, float px, float py, int has_pt);
void phys_apply_impulse(phys_world_t *w, int id, float ix, float iy, float px, float py, int has_pt);
void phys_step(phys_world_t *w, float dt, int iterations);

#endif
