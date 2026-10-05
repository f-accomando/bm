/*
 * Software 3D for native cartridges: z-buffered triangles in RGB565, flat or
 * Gouraud-shaded (dithered), perspective projection, back-face culling,
 * clipping on the near plane, a sun with coloured sky and ground light,
 * specular highlights and rim light, point lamps, optional distance fog,
 * textured faces (perspective correct, from a sprite sheet), per-face
 * materials (emissive, glossy, screen-door transparent, levels of detail),
 * planar shadows, a first-person layer always in front, and 3D effects
 * (points, lines, billboard sprites).
 * The ARM transforms, lights, culls and clips (VFP), and rasterizes in
 * fixed point; or a backend (the GPU, src/gpu/gpu3d.c) draws the screen
 * triangles, for what it can draw (r3d_t.arm_hook).
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
    uint32_t *colors;           /* 0xRRGGBB per face + material bits, or R3D_TEXTURED */
    v3_t *normals;              /* per face, object space */
    v3_t *vnormals;             /* per vertex (average of its faces), for Gouraud */
    float *uv;                  /* NULL, or 6 per face: u0 v0 u1 v1 u2 v2 in texels */
    const g16_sheet_t *tex;     /* texture of the R3D_TEXTURED faces */
    uint8_t *vlod;              /* per vertex: bit d set if a face shown at detail d uses it */
    uint8_t *clight;            /* NULL, or 9 per face: the light baked at its corners
                                 * (R G B, 128 = 1): drawn smooth with it (and the lamps) */
    /* a mesh with a skeleton (rigid skinning, done while drawing): verts,
     * normals and vnormals are at rest, each vertex follows the 3x4 matrix
     * (row major) of its bone; NULL for a plain mesh */
    const float (*bones)[12];
    const uint8_t *vbone;
    int nbones;
    uint32_t version;           /* new at each r3d_mesh_normals (a backend copies meshes) */
    float radius;               /* of a sphere around centre holding every vertex, set by
                                 * r3d_mesh_normals (< 0: unknown): meshes out of view are
                                 * skipped before their vertices are transformed */
    v3_t centre;                /* the middle of the vertices' box (a map's piece in world
                                 * coordinates is far from (0,0,0)) */
} r3d_mesh_t;

/* Face colours: 0xRRGGBB in the low 24 bits; the high bits are the
 * material (0 = a plain lit face, as before):
 *   bit 31  textured with the sprite sheet (the colour is ignored)
 *   bit 30  emissive: full colour, no light (glowing parts, screens)
 *   bit 29  glossy: specular highlight of the sun
 *   bit 28  screen-door transparent: every other pixel (shields, glass)
 *   bit 27  flat: lit as one plane even in a smooth (Gouraud) draw, so
 *           hard edges can share their corners with the faces around
 *   bits 24-25  a level of detail k (0..3), and bit 26: the face shows at
 *           the levels below k (a coarse stand-in) if set, else at k and
 *           above (a detail); 0 = at every level. */
#define R3D_TEXTURED  0x80000000u
#define R3D_EMISSIVE  0x40000000u
#define R3D_GLOSSY    0x20000000u
#define R3D_SCREEN    0x10000000u
#define R3D_FLAT      0x08000000u
#define R3D_LOD_SHOWS(c, d) (((c) >> 26 & 1u) ? (d) < ((c) >> 24 & 3u) : (d) >= ((c) >> 24 & 3u))

#define R3D_LAMPS 4
#define R3D_NEAR 0.1f               /* near plane: nothing nearer is drawn */

/*
 * A drawing backend other than the software rasterizer (M33: the GPU).
 * r3d still transforms, lights, culls the back faces and clips on the near
 * plane; the backend gets screen triangles. A corner: screen x, y, z =
 * 1/depth, and either a colour (r, g, b in 0..1, light and fog applied)
 * or texture coordinates (u, v in texels of tex) and the light k (0..1);
 * R3D_KIND_TEX_RGB (M34): u, v, and the light l (r, g, b, 0..1 for 0..2,
 * fog already taken off) and the fog f to add (r, g, b in 0..1).
 */
typedef struct { float x, y, z, a, b, c; float l[3], f[3]; } r3d_corner_t;

/* R3D_KIND_SCREEN: a colour on the pixels with x + y even only (screen-door).
 * R3D_INSIDE, added to the kind: the mesh's bounding sphere is in front of
 * the near plane and its corners are no farther than the backend's guard
 * from the screen (rounding aside), so the backend need not check them.
 * R3D_TEX_SCREEN, added to R3D_KIND_TEXTURE or R3D_KIND_TEX_RGB: a textured
 * screen-door face (only to a backend with tex_screen) */
enum { R3D_KIND_COLOUR, R3D_KIND_TEXTURE, R3D_KIND_SCREEN, R3D_KIND_TEX_RGB, R3D_INSIDE = 4, R3D_TEX_SCREEN = 8 };

/* the depth of a backend triangle: tested and written, neither (R3D_NOZ),
 * or tested only (3D effects, shadows) */
enum { R3D_DEPTH_WRITE, R3D_DEPTH_NONE, R3D_DEPTH_TEST };

/* what a backend mesh (M36) needs of the scene: the projection (screen x =
 * cx + f x / z, y = cy - f y / z), the fog (f = clamp((depth - near) * k,
 * 0, 1), k 0 for none; its colour 0..1), the lamps (camera x y z, 1/r^2,
 * colour times k), the level of detail (0..3) */
typedef struct {
    float f, cx, cy;
    float fog_near, fog_k, fog[3];
    int nlamps;
    float lamp[R3D_LAMPS][7];
    unsigned detail;
    int unlit;                  /* R3D_UNLIT: every face at full light */
    int inside;                 /* R3D_INSIDE: no corner needs clipping (else the backend
                                 * clips, or says no) */
    int front;                  /* R3D_FRONT: depths 10x nearer (r3d cleared the depth) */
    /* a model lit by the sun (not unlit, no baked light): light_fast at
     * each corner, l = A + B n.y + D max(n.sun, 0) + R (1 - max(n.view, 0))^2
     * (r g b each) plus the lamps, the highlight S (n.half)^spec_p where
     * the sun lights a glossy face; smooth: the vertices' normals */
    int lit, smooth;
    float sun[3], view[3], half[3];     /* world axes */
    float A[3], B[3], D[3], R[3], S[3], spec_p;
} r3d_env_t;

typedef struct {
    /* depth: R3D_DEPTH_* */
    void (*tri)(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind,
                const g16_sheet_t *tex, int depth);
    /* M36, optional: a whole mesh placed by the GPU (its vertex shader),
     * M[b] object -> camera with bone b (3x4 by rows; one without a
     * skeleton), N[b] its turn of the normals to world axes (3x3); 1 if
     * the backend took it, 0 if r3d draws it a triangle at a time (the
     * backend may say no: gpu3d.c, which takes the meshes not R3D_INSIDE
     * if the GPU clips) */
    int (*mesh)(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                int nbones, const r3d_env_t *env, int depth);
    /* M36, optional: the shadow of a mesh (R3D_SHADOW) by the GPU: W[b]
     * object -> world relative to the camera with bone b (3x4), C the
     * camera's turn (3x3 by rows), down along L (L.y >= 0.25) to the plane
     * y = plane (relative to the camera); 1 if the backend took it */
    int (*shadow)(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*W)[12], int nbones,
                  const float C[9], v3_t L, float plane, const r3d_env_t *env);
    void (*zclear)(void *ctx, const g16_t *g);      /* what follows ignores what was drawn */
    void *ctx;
    float guard;                /* pixels around the screen for R3D_INSIDE; 0: never */
    int tex_screen;             /* draws textured screen-door faces (R3D_TEX_SCREEN, M34); else
                                 * they go to the ARM */
} r3d_backend_t;

typedef struct { float r, g, b; } r3d_rgb_t;

typedef struct {
    g16_t *g;
    uint16_t *zbuf;             /* g->w * g->h, 0 = far */
    uint8_t *mask;              /* g->w * g->h, for shadows (allocated when needed) */
    v3_t cam_pos;
    float cam_yaw, cam_pitch, cam_roll;
    float focal;                /* pixels: (w/2) / tan(fov/2) */
    v3_t light;                 /* unit vector towards the light, world space */
    float ambient;
    r3d_rgb_t sun, sky, ground; /* colours of the sun and of the light from above and below (1 = white) */
    float spec_k;               /* strength of the highlight on glossy faces */
    int spec_shift;             /* exponent = 2^spec_shift */
    float rim_k;                /* light on the edges seen from the camera */
    struct { v3_t pos; float r2, k; r3d_rgb_t c; int on; } lamp[R3D_LAMPS];  /* point lights */
    uint32_t fog_rgb;           /* faces fade to this colour ... */
    float fog_near, fog_far;    /* ... between these depths (off if far <= near) */
    int shadow_style;           /* 0 darken, 1 dither */
    int fast;                   /* M37: object -> camera in one matrix and a model without bones lit
                                 * in its own axes, also on the ARM (fewer instructions a vertex;
                                 * the pixels not bit for bit as before): r3d_fast=1 */
    /* statistics of the last frame (reset by r3d_zclear); pixels are
     * counted by the software rasterizer only */
    uint32_t tris_in, tris_drawn, pixels, verts;
    const r3d_backend_t *backend;   /* NULL: the software rasterizer */
    /* called (with arm_ctx) when a draw needs the software rasterizer
     * while a backend is set (textured screen-door faces for a backend
     * without tex_screen: since bm3d 4.7 the GPU's has it). It draws what
     * the backend holds; then r3d sets backend to NULL and the ARM draws
     * from there on. */
    void (*arm_hook)(void *ctx, const char *why);
    void *arm_ctx;
} r3d_t;

int  r3d_init(r3d_t *r, g16_t *g);
void r3d_free(r3d_t *r);
int  r3d_resize(r3d_t *r, int old_w);   /* after the screen of r->g changed size */
void r3d_zclear(r3d_t *r);
void r3d_camera(r3d_t *r, float x, float y, float z, float yaw, float pitch, float fov_deg);
void r3d_camera_roll(r3d_t *r, float roll);
void r3d_light(r3d_t *r, float x, float y, float z, float ambient);
/* Colours (0xRRGGBB) of the sun, of the ambient light from the sky (faces
 * looking up) and from the ground (faces looking down); white = as before. */
void r3d_sky(r3d_t *r, uint32_t sun, uint32_t sky, uint32_t ground);
/* Highlights on glossy faces (k 0..2, exponent 4..64) and rim light (0..1). */
void r3d_shine(r3d_t *r, float spec_k, int exponent, float rim_k);
void r3d_fog(r3d_t *r, uint32_t rgb, float near, float far);
/* Point light i (0..R3D_LAMPS-1): faces whose centre is within `radius` get
 * up to `k` more light (of colour rgb), fading with the distance; radius <= 0
 * turns it off. */
void r3d_lamp(r3d_t *r, int i, float x, float y, float z, float radius, float k);
void r3d_lamp_rgb(r3d_t *r, int i, float x, float y, float z, float radius, float k, uint32_t rgb);

/* World point -> screen. Returns 0 if it is behind the camera. */
int r3d_project(const r3d_t *r, v3_t p, float *sx, float *sy, float *depth);

/* Draws a mesh at position p, rotated by (rx, ry, rz) radians, scaled. */
void r3d_draw(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz, float scale);
/* The same with flags: R3D_NOZ ignores the z-buffer (no test, no write:
 * for floors and backdrops drawn before the rest), R3D_UNLIT full colour,
 * R3D_SMOOTH Gouraud shading (light per vertex, colour interpolated and
 * dithered; faces that share vertex indices look like one curved surface),
 * R3D_SHADOW the shadow of the mesh on the plane y = p.y along the sun
 * (darkens what is already drawn there, the mesh itself is not drawn),
 * R3D_FRONT in front of everything drawn before (first-person arms and
 * weapons: the z-buffer under it is cleared, its own depths are exact from
 * 0.1 units), R3D_DETAIL(n) only the faces of level of detail n (0 the
 * lowest; without it, 3: every face). */
#define R3D_NOZ    1u
#define R3D_UNLIT  2u
#define R3D_SMOOTH 4u
#define R3D_SHADOW 8u
#define R3D_DETAIL(n) ((3u - (unsigned)(n)) << 4)
#define R3D_FRONT  64u
void r3d_draw_flags(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz,
                    float scale, unsigned flags);

/* 3D effects, z-tested against what is drawn (they do not write depth):
 * a round point of world radius `radius`; a line from a to b `width`
 * pixels wide; a rectangle of the sprite sheet facing the camera, `size`
 * world units wide (transparent texels skipped). flags: R3D_FX_SCREEN draws
 * every other pixel. Each returns the pixels drawn. */
#define R3D_FX_SCREEN 1u
uint32_t r3d_point(r3d_t *r, v3_t p, float radius, uint32_t rgb, unsigned flags);
uint32_t r3d_line(r3d_t *r, v3_t a, v3_t b, uint32_t rgb, int width, unsigned flags);
uint32_t r3d_sprite(r3d_t *r, const g16_sheet_t *s, int sx, int sy, int sw, int sh, v3_t p,
                    float size, unsigned flags);

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
