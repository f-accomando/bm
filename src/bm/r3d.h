/*
 * Software 3D for native cartridges: z-buffered triangles in RGB565, flat or
 * Gouraud-shaded (dithered), perspective projection, back-face culling,
 * clipping on the near plane, one directional light and point lamps, optional
 * distance fog, textured faces (perspective correct, from a sprite sheet).
 * Everything runs on the ARM (VFP for the transforms, fixed point in the
 * inner loops); the VideoCore 3D unit is not used.
 */
#ifndef R3D_H
#define R3D_H

#include <stdint.h>

#include "gfx16.h"

typedef struct { float x, y, z; } v3_t;

typedef struct {
    int nverts, nfaces;
    v3_t *verts;
    uint16_t *faces;            /* 3 vertex indices per face */
    uint32_t *colors;           /* 0xRRGGBB per face, or R3D_TEXTURED */
    v3_t *normals;              /* per face, object space */
    v3_t *vnormals;             /* per vertex (average of its faces), for Gouraud */
    float *uv;                  /* NULL, or 6 per face: u0 v0 u1 v1 u2 v2 in texels */
    const g16_sheet_t *tex;     /* texture of the R3D_TEXTURED faces */
    float radius;               /* of a sphere around (0,0,0) holding every vertex, set by
                                 * r3d_mesh_normals (< 0: unknown): meshes out of view are
                                 * skipped before their vertices are transformed */
} r3d_mesh_t;

#define R3D_TEXTURED 0x80000000u

#define R3D_LAMPS 4
#define R3D_NEAR 0.1f               /* near plane: nothing nearer is drawn */

/*
 * A drawing backend other than the software rasterizer (M30: the GPU).
 * r3d still transforms, lights, culls the back faces and clips on the near
 * plane; the backend gets screen triangles. A corner: screen x, y, z =
 * 1/depth, and either a colour (r, g, b in 0..255, light and fog applied)
 * or texture coordinates (u, v in texels of tex) and the light k (0..1).
 */
typedef struct { float x, y, z, a, b, c; } r3d_corner_t;

enum { R3D_KIND_COLOUR, R3D_KIND_TEXTURE };

typedef struct {
    /* nodepth: no depth test and no depth write (R3D_NOZ) */
    void (*tri)(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind,
                const g16_sheet_t *tex, int nodepth);
    void (*zclear)(void *ctx, const g16_t *g);      /* what follows ignores what was drawn */
    void *ctx;
} r3d_backend_t;

typedef struct {
    g16_t *g;
    uint16_t *zbuf;             /* g->w * g->h, 0 = far */
    v3_t cam_pos;
    float cam_yaw, cam_pitch, cam_roll;
    float focal;                /* pixels: (w/2) / tan(fov/2) */
    v3_t light;                 /* unit vector towards the light, world space */
    float ambient;
    struct { v3_t pos; float r2, k; int on; } lamp[R3D_LAMPS];  /* point lights, per face */
    uint32_t fog_rgb;           /* faces fade to this colour ... */
    float fog_near, fog_far;    /* ... between these depths (off if far <= near) */
    /* statistics of the last frame (reset by r3d_zclear); pixels are
     * counted by the software rasterizer only */
    uint32_t tris_in, tris_drawn, pixels;
    const r3d_backend_t *backend;   /* NULL: the software rasterizer */
} r3d_t;

int  r3d_init(r3d_t *r, g16_t *g);
void r3d_free(r3d_t *r);
void r3d_zclear(r3d_t *r);
void r3d_camera(r3d_t *r, float x, float y, float z, float yaw, float pitch, float fov_deg);
void r3d_camera_roll(r3d_t *r, float roll);
void r3d_light(r3d_t *r, float x, float y, float z, float ambient);
void r3d_fog(r3d_t *r, uint32_t rgb, float near, float far);
/* Point light i (0..R3D_LAMPS-1): faces whose centre is within `radius` get
 * up to `k` more light, fading with the distance; radius <= 0 turns it off. */
void r3d_lamp(r3d_t *r, int i, float x, float y, float z, float radius, float k);

/* World point -> screen. Returns 0 if it is behind the camera. */
int r3d_project(const r3d_t *r, v3_t p, float *sx, float *sy, float *depth);

/* Draws a mesh at position p, rotated by (rx, ry, rz) radians, scaled. */
void r3d_draw(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz, float scale);
/* The same with flags: R3D_NOZ ignores the z-buffer (no test, no write:
 * for floors and backdrops drawn before the rest), R3D_UNLIT full colour,
 * R3D_SMOOTH Gouraud shading (light per vertex, colour interpolated and
 * dithered; faces that share vertex indices look like one curved surface). */
#define R3D_NOZ    1u
#define R3D_UNLIT  2u
#define R3D_SMOOTH 4u
void r3d_draw_flags(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz,
                    float scale, unsigned flags);

/* Mesh helpers */
int  r3d_mesh_alloc(r3d_mesh_t *m, int nverts, int nfaces);
int  r3d_mesh_alloc_uv(r3d_mesh_t *m);         /* adds the uv array (zeroed) */
void r3d_mesh_free(r3d_mesh_t *m);
void r3d_mesh_normals(r3d_mesh_t *m);
int  r3d_mesh_sphere(r3d_mesh_t *m, int rings, int segments, uint32_t c1, uint32_t c2);
int  r3d_mesh_cube(r3d_mesh_t *m, uint32_t color);

/* 2D filled triangle (no z), screen coordinates + camera of g. */
void g16_tri(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
/* The same with a colour (0xRRGGBB) at each corner, blended and dithered. */
void g16_tri_gouraud(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2,
                     uint32_t c0, uint32_t c1, uint32_t c2);

#endif
