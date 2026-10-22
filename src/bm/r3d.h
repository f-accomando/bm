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

typedef struct r3d_mesh_s {
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
    /* R3D_FLATTEX: each face's colour with the texture left out (a textured
     * face: the average of its texels, made by the first draw that wants it, again
     * after r3d_mesh_normals or a change of the texture); NULL until then */
    uint32_t *flatcol;
    struct { uint32_t key, tver; const g16_sheet_t *tex; } flat;
    /* what an impostor (r3d_t.impostor) needs, made by the first draw that
     * wants one, again after r3d_mesh_normals or a change of the texture:
     * the vertices' box, the average colour (0..255, by area) of the lit
     * and of the emissive faces, the emissive share, the baked light */
    struct {
        uint32_t key, tver;
        const g16_sheet_t *tex;
        float lo[3], hi[3], lit[3], emit[3], bake[3], we;
        int baked;
    } imp;
    /* and with a skeleton: the box of each bone's vertices at rest (lo xyz,
     * hi xyz; lo > hi: none), which the pose moves; made by the first draw
     * that wants an impostor, again after r3d_mesh_normals or a new vbone;
     * freed by r3d_mesh_free */
    struct {
        float (*box)[6];
        const uint8_t *vbone;
        uint32_t key;
        int nbones, cap, ok;
    } bimp;
    /* the outline (flag R3D_OUTLINE, bm3d 7.5): the inverted hull, made by the first draw that wants
     * it (a mesh of the same bones and levels of detail, its faces turned round, each vertex pushed
     * out along the smooth normal by the width), again after r3d_mesh_normals or a new width;
     * freed by r3d_mesh_free */
    struct r3d_mesh_s *hull;
    struct { uint32_t key, rgb; float width; } hullk;
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

/* what a backend mesh (bm3d 3.0) needs of the scene: the projection (screen x =
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
    /* bm3d 7.5, the cel light on a model lit by the sun (r3d_t.cel_bands > 0, flag R3D_CEL): the
     * number of bands (0: none) and the sun term of the darkest one; the highlight becomes a step
     * (on above 0.5). Only to a backend with cel */
    int cel;
    float cel_floor;
} r3d_env_t;

typedef struct {
    /* depth: R3D_DEPTH_* */
    void (*tri)(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind,
                const g16_sheet_t *tex, int depth);
    /* bm3d 3.0, optional: a whole mesh placed by the GPU (its vertex shader),
     * M[b] object -> camera with bone b (3x4 by rows; one without a
     * skeleton), N[b] its turn of the normals to world axes (3x3); 1 if
     * the backend took it, 0 if r3d draws it a triangle at a time (the
     * backend may say no: gpu3d.c, which takes the meshes not R3D_INSIDE
     * if the GPU clips) */
    int (*mesh)(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                int nbones, const r3d_env_t *env, int depth);
    /* bm3d 3.0, optional: the shadow of a mesh (R3D_SHADOW) by the GPU: W[b]
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
    int cel;                    /* bm3d 7.5: its mesh cuts the sun's light by the bands of r3d_env_t.cel
                                 * (cel3d); else a lit cel draw goes to the ARM triangle by triangle */
    int colour_only;            /* M41: draws flat colours with depth R3D_DEPTH_WRITE only (the Mali, phase 2): anything
                                 * else makes r3d hand over to the ARM (to_arm) */
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
    int notex;                  /* 1: every draw has R3D_FLATTEX (textures3d(false)) */
    int fast;                   /* bm3d 4.9: object -> camera in one matrix and a model without bones lit
                                 * in its own axes, also on the ARM (fewer instructions a vertex;
                                 * the pixels not bit for bit as before): r3d_fast=1 */
    /* cel light (bm3d 7.4): the sun term cut into cel_bands steps by a table of 64 bytes (0 = off);
     * the outline values are kept for the outline stage (flag R3D_OUTLINE, not drawn yet) */
    int cel_bands;
    float cel_floor, cel_width;
    uint32_t cel_rgb;           /* 0xRRGGBB, or CEL_NO_OUTLINE */
    uint8_t cel_lut[64];        /* the sun term (0..255) for n.L in steps of 1/64 */
    int impostor;               /* impostors: a mesh whose box on the screen is at most this many
                                 * pixels wide and high, pixels of a 1080-line picture (g->h * this
                                 * / 1080 of the target's), is drawn as one quad facing the camera in
                                 * its average colour (2 triangles, by the ARM or the backend);
                                 * a mesh with a skeleton by the box of its pose (and then also
                                 * skipped out of view, as one without);
                                 * 0 off (default): r3d_impostor in bm/config.txt, gpu3d() */
    /* statistics of the last frame (reset by r3d_zclear); pixels are
     * counted by the software rasterizer only */
    uint32_t tris_in, tris_drawn, pixels, verts;
    uint32_t impostors;         /* meshes drawn as an impostor */
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

/* bm3d 5.5: 0 if a sphere (centre p, radius rad) is out of the camera's view:
 * behind it or beyond an edge of the screen. */
int r3d_visible(const r3d_t *r, v3_t p, float rad);

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
/* R3D_FLATTEX: the textured faces drawn as flat colours, each the average of
 * its texels (without the texture: the same mesh, the same bones; the lit
 * faces keep their light). Faces of the sheet's holes (transparent) are
 * left grey. Also set for every draw by r3d_t.notex. */
#define R3D_FLATTEX 128u
/* R3D_CEL: this draw uses the cel light (r3d_cel; nothing happens with the bands at 0).
 * R3D_OUTLINE (bm3d 7.5): after the mesh, its outline by the inverted hull: the mesh again with
 * its faces turned round, pushed out along its normals by r3d_t.cel_width (a share of the mesh's
 * size), flat in r3d_t.cel_rgb, unlit. Nothing happens without an outline colour (cel_rgb), for
 * a shadow, a first-person model (R3D_FRONT), a mesh drawn as an impostor or a camera inside the
 * mesh's sphere. The levels of detail and the bones are the mesh's. */
#define R3D_CEL     256u
#define R3D_OUTLINE 512u
#define R3D_CEL_NO_OUTLINE 0xFFFFFFFFu
/* cel light: bands 0 (off) or 2..8, floor 0..1 = the sun term of the darkest band. Builds the
 * 64-entry table; returns the bands in force. */
int r3d_cel(r3d_t *r, int bands, float floor_);
/* the cut of one sun term dd (0..1) by the table of r3d_cel (for the tests) */
float r3d_cel_cut(const r3d_t *r, float dd);
/* the ARM raster's 4x4 ordered dither: on (default) or off (a constant half step instead);
 * returns the old state. Reset by r3d_init. */
int r3d_dither(int on);
void r3d_draw_flags(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz,
                    float scale, unsigned flags);

/* 3D effects, z-tested against what is drawn (they do not write depth):
 * a round point of world radius `radius`; a line from a to b `width`
 * pixels wide; a rectangle of the sprite sheet facing the camera, `size`
 * world units wide (transparent texels skipped). flags: R3D_FX_SCREEN draws
 * every other pixel. Each returns the pixels drawn. */
#define R3D_FX_SCREEN 1u
uint32_t r3d_point(r3d_t *r, v3_t p, float radius, uint32_t rgb, unsigned flags);
/* A pool of 3D particles in structure-of-arrays form (7 float arrays of cap entries: x y z vx vy vz life; cap a
 * multiple of 16, at most 4096). One step, single precision: vy = vy - g; x = x + vx; y = y + vy; z = z + vz;
 * life = life - 1.0f. Entries with life <= 0 are skipped. The ARM does the step; there is no cost without a pool. */
#define R3D_PFX_MAX 4096
typedef struct { int cap, next; float *a; uint32_t *rgb; } r3d_pfx_t;
r3d_pfx_t *r3d_pfx_new(int cap);                 /* NULL if cap < 1 or > R3D_PFX_MAX or no memory; cap rounded up to 16 */
void r3d_pfx_free(r3d_pfx_t *p);
int r3d_pfx_add(r3d_pfx_t *p, v3_t pos, v3_t vel, float life, uint32_t rgb);   /* the next slot, round robin; its index */
void r3d_pfx_step(r3d_pfx_t *p, float gravity);
uint32_t r3d_pfx_draw(r3d_t *r, const r3d_pfx_t *p, float radius);   /* r3d_point for each entry with life > 0; the pixels */
int r3d_pfx_live(const r3d_pfx_t *p);            /* entries with life > 0 */
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
