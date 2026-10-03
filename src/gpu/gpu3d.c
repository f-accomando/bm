#include "gpu3d.h"
#include "shaders.h"
#include "v3d.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

/* the memory of a job, in one block: the binner wants its own (tile
 * state, tile lists, overflow) within one 256 MiB window */
#define JOB_TSDA     16384          /* 48 bytes per tile: 20 x 12 tiles with MSAA */
#define JOB_ALLOC    (4u << 20)     /* tile lists */
#define JOB_OVERFLOW (2u << 20)
#define JOB_ZBUF     (1u << 20)     /* depth kept between jobs: 640x384, 32 bits */
#define JOB_BCL      (256u << 10)
#define JOB_RCL      (16u << 10)
#define JOB_RECS     (64u << 10)    /* shader records: NV 16 bytes, GL 64 */
#define JOB_UNIF     (128u << 10)   /* uniforms of the vertex shaders */
#define JOB_CODE     (10u << 10)    /* the shaders, 1 KiB each */
#define JOB_VERTS    (4u << 20)
#define PROBE_W      64
#define PROBE_H      64
#define JOB_PROBE    (PROBE_W * PROBE_H * 2)
#define PROBE_TEX    (PROBE_W * PROBE_H * 4 + 4096)
#define JOB_BLOCK    (JOB_TSDA + JOB_ALLOC + JOB_OVERFLOW + JOB_ZBUF + JOB_BCL + JOB_RCL + JOB_RECS + JOB_UNIF + JOB_CODE + \
                      JOB_VERTS + JOB_PROBE + PROBE_TEX)

#define TIMEOUT_US   200000
#define BATCH_MAX    65532          /* vertices of one VERTEX_ARRAY_PRIMITIVES */
#define GUARD        1000.0f        /* margin around the screen inside the 12.4 range */

/* a vertex as the NV shader state wants it: screen x and y in 12.4,
 * z (0 near .. 1 far), 1/w, the varyings of its shader: 3 (colour, or s t
 * k) or 8 (s t, light r g b, fog r g b); the vertices of a batch follow one
 * another with the stride of its shader */
typedef struct {
    int16_t x, y;
    float z, inv_w;
    float v[8];
} gvert_t;

#define VSTRIDE(n) (12 + 4 * (n))
#define VMAX_BYTES ((uint32_t)JOB_VERTS - 21u * (uint32_t)VSTRIDE(8))   /* room for a clipped triangle */

enum { SH_COLOUR, SH_TEX, SH_TEX_ALPHA, SH_SCREEN, SH_TEX_RGB, SH_TEX_RGB_ALPHA, SH_COUNT };
enum { GV_BAKED, GV_TEX_RGB, GV_COUNT };     /* the vertex shaders of meshes (M36) */
#define CODE_CS 6                       /* the coordinate shader's kilobyte of G.code */
#define CODE_VS 7                       /* and the vertex shaders' */

/* discard: the shader may write no pixel (early z off) */
static const struct { const uint32_t *code; size_t size; uint8_t uniforms, varyings, discard; } shaders[SH_COUNT] = {
    { fs_colour, sizeof fs_colour, 0, 3, 0 },
    { fs_tex_lit, sizeof fs_tex_lit, 2, 3, 0 },
    { fs_tex_lit_alpha, sizeof fs_tex_lit_alpha, 2, 3, 1 },
    { fs_colour_screen, sizeof fs_colour_screen, 0, 3, 1 },
    { fs_tex_rgb, sizeof fs_tex_rgb, 2, 8, 0 },
    { fs_tex_rgb_alpha, sizeof fs_tex_rgb_alpha, 2, 8, 1 },
};

/* a sprite sheet as a texture: RGBA32R (raster order, 32 bits a texel) in
 * memory the V3D reads, followed by its parameters P0 P1 (uniforms) */
typedef struct {
    const g16_sheet_t *sheet;
    uint32_t version;
    int w, h;
    uint32_t *texels;
    uint32_t *params;
    size_t size;
    float inv_w, inv_h;             /* texel coordinates to 0..1 */
    int cw, ch;                     /* the sheet's cells */
    uint32_t *holes;                /* (cw+1) x (ch+1): cells not opaque above and left of each corner */
} tex_t;

#define NTEX 2

/* M36: a mesh whose vertices the V3D places. Its corners (3 a face) in
 * memory the V3D reads, made again when the mesh changes (its version):
 * x y z, the face's normal, its colour (0..1, in the order of the colour
 * varyings). */
#define NMESH 256                   /* a map's pieces in view, and what moves on them */
#define MESH_HINTS 512
#define MESH_GROUPS 16
typedef struct {
    uint8_t vs, fs, lamps, lods;        /* shaders, lamps on, levels of detail that show it */
    uint32_t first, n;                  /* its first byte in the corners, its corners */
} ggroup_t;
typedef struct {
    const r3d_mesh_t *m;
    uint32_t version;
    int unlit;
    uint8_t *corners;                   /* NULL: not a mesh the GPU takes */
    ggroup_t g[MESH_GROUPS];
    int ngroups;
    const tex_t *tex;                   /* its texture (textured faces), at version tex_version */
    uint32_t tex_version;
    uint32_t job, used;                 /* the job that drew it last; when, for the least recent */
} gmesh_t;

static struct {
    int ready, failed;
    const char *status;
    char why[160];
    uint8_t *block;
    uint8_t *tsda, *alloc, *overflow, *zbuf, *bcl, *rcl, *recs, *code;
    uint16_t *probe;
    uint32_t *probe_tex;
    uint8_t *verts;                 /* the vertices of the job, */
    uint32_t vbytes;                /* bytes of them so far */
    v3d_cl_t cl;
    uint8_t *rec_next;
    int open;                       /* the job has its binning header */
    int w, h;                       /* its target size */
    int z_saved, z_w, z_h;          /* zbuf holds the depth so far (of a w x h frame) */
    int split;                      /* 3D of this frame drawn before some 2D */
    int page_uniform;               /* the page is page_colour all over (gpu3d_page) */
    uint16_t page_colour;
    int z_wanted;                   /* this cartridge draws 3D after 2D in a frame */
    /* the batch being filled */
    int b_open, b_shader, b_depth;
    uint32_t b_first, b_stride;     /* its first vertex (byte offset), its vertex stride */
    uint32_t b_room;                /* offset it can still start a triangle at (7 corners clipped) */
    const tex_t *b_tex;
    uint8_t *b_start, *b_len;       /* its first packet, its vertex count */
    int clip[4];                    /* clip window written last (x0 y0 x1 y1) */
    int cfg;                        /* configuration written last, -1: none */
    int red_a, tex_swap;            /* colour byte order, found by the probe */
    int tformat, tformat_off;       /* textures in T-format (learned by the probe; off: rows) */
    int msaa;                       /* MSAA 4x asked for (gpu3d_set_msaa) */
    int ms_ok;                      /* the probe: 0 no MSAA, 1 on cleared pages, 2 also on loaded ones */
    int ms, tile;                   /* the job: MSAA, tile size */
    int t_rev;                      /* odd rows of 4 KiB tiles run right to left */
    uint16_t t_inner[2][1024];      /* word of each texel in its tile: even, odd rows of tiles */
    int ia;                         /* varying of the colour's first value: 0, or 2 (red_a 0) */
    const uint8_t *fb_mem;          /* the framebuffer and its bus address */
    uint32_t fb_size, fb_bus;
    tex_t tex[NTEX];
    int tex_next;
    int tex_used[NTEX];             /* by the job being filled */
    uint32_t *unif, *unif_next;     /* uniforms of the vertex shaders, the job's next */
    uint32_t gldraws;               /* meshes the job draws with the vertex shader */
    uint32_t job_no, tick;
    gmesh_t gm[NMESH];
    uint16_t hint[MESH_HINTS];      /* the slot a mesh had last, by its address */
    int gl_ok;                      /* the probe drew with the vertex shader */
    int gl_cw;                      /* CONFIGURATION_BITS clockwise bit for r3d's front faces */
    int gl_on;                      /* asked for (gpu3d_set_vshader) */
    int clip_ok;                    /* the probe saw the GPU clip: 1, 2 with Z_MIN_MAX_CLIPPING_PLANES */
    uint32_t vp;                    /* the job's VIEWPORT_OFFSET (x, y in 12.4) */
    float clipper[2];               /* the job's CLIPPER_XY_SCALING (0: not written yet) */
    gpu3d_stats_t st;
    r3d_backend_t backend;
} G;

static int flush_job(const g16_t *g, int store);

static void disable(const char *why)
{
    G.failed = 1;
    G.b_open = 0;                       /* no triangle goes the fast way into a dead job */
    ksnprintf(G.why, sizeof G.why, "%s", why);
    G.status = G.why;
    kprintf("gpu3d: %s; the 3D is drawn by the ARM again\n", why);
}

void gpu3d_set_fb(const void *mem, uint32_t size, uint32_t bus)
{
    G.fb_mem = mem;
    G.fb_size = size;
    G.fb_bus = bus;
}

/* bus address of a page of the screen or of a buffer in ARM memory */
static uint32_t bus_of(const void *p)
{
    const uint8_t *b = p;
    if (G.fb_mem && b >= G.fb_mem && b < G.fb_mem + G.fb_size)
        return G.fb_bus + (uint32_t)(b - G.fb_mem);
    return v3d_bus(p);
}

/* ---------------------------------------------------------------- textures */

/* an RGB565 colour as a texel: the bytes a b c d the tile buffer gets
 * back, red and blue in the order the probe found, alpha in d */
static uint32_t texel_of(uint16_t c, int opaque)
{
    const int ra = G.red_a ^ G.tex_swap;
    uint32_t r = (uint32_t)(c >> 11) << 3 | c >> 13, g = (uint32_t)(c >> 5 & 63) << 2 | (c >> 9 & 3),
             b = (uint32_t)(c & 31) << 3 | (c >> 2 & 7);
    return (opaque ? 0xFF000000u : 0) | (ra ? b << 16 | g << 8 | r : r << 16 | g << 8 | b);
}

/* the word of texel (x, y) in a T-format texture w wide (a multiple of
 * 32), with the layout the probe learned */
static uint32_t t_word(int x, int y, int w)
{
    const int tpr = w / 32, ty = y / 32;
    int tx = x / 32;
    if ((ty & 1) && G.t_rev)
        tx = tpr - 1 - tx;
    return (uint32_t)(ty * tpr + tx) * 1024u + G.t_inner[ty & 1][(y & 31) * 32 + (x & 31)];
}

/* the texture of a sheet, made again when the sheet changed: T-format
 * (RGBA8888 in tiles) when the probe learned it and the sides are
 * multiples of 32, else RGBA32R (rows); NULL if it cannot be one (larger
 * than 2048, no memory). A slot the waiting job still reads is drawn
 * first (into g) before it is made again. */
static const tex_t *tex_get(const g16_t *g, const g16_sheet_t *s)
{
    for (int i = 0; i < NTEX; i++)
        if (G.tex[i].sheet == s && G.tex[i].version == s->version && G.tex[i].texels)
            return &G.tex[i];
    if (s->w > 2048 || s->h > 2048 || s->w < 1 || s->h < 1)
        return NULL;
    int slot = -1;
    for (int i = 0; i < NTEX; i++)
        if (G.tex[i].sheet == s)
            slot = i;                       /* the same sheet, changed */
    if (slot < 0) {
        slot = G.tex_next;
        G.tex_next = (G.tex_next + 1) % NTEX;
    }
    if (G.tex_used[slot] && gpu3d_pending() && flush_job(g, 1) != 0)
        return NULL;
    tex_t *t = &G.tex[slot];
    const int cw = s->w / G16_CELL, ch = s->h / G16_CELL;
    size_t size = (size_t)s->w * (size_t)s->h * 4 + 16 + (size_t)(cw + 1) * (size_t)(ch + 1) * 4;
    if (!t->texels || t->size < size) {
        free(t->texels);
        t->texels = aligned_alloc(4096, (size + 4095) & ~(size_t)4095);
        t->size = t->texels ? size : 0;
        if (!t->texels) {
            t->sheet = NULL;
            return NULL;
        }
    }
    const uint32_t n = (uint32_t)s->w * (uint32_t)s->h;
    const int tiled = G.tformat && !G.tformat_off && s->w % 32 == 0 && s->h % 32 == 0;
    if (tiled) {
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                const uint32_t i = (uint32_t)y * (uint32_t)s->w + (uint32_t)x;
                t->texels[t_word(x, y, s->w)] = texel_of(s->px[i], s->alpha[i]);
            }
    } else {
        for (uint32_t i = 0; i < n; i++)
            t->texels[i] = texel_of(s->px[i], s->alpha[i]);
    }
    /* P0: base (4 KiB units), type: RGBA8888 (0, T-format) or RGBA32R
     * (16: low bits 0 here, bit 4 in P1); P1: type bit 4, height and width
     * (2048 is 0), nearest texel when magnified and minified, clamp in s
     * and t */
    t->params = t->texels + n;
    /* the cells that are not opaque, summed over areas: any box of cells
     * is checked in four reads (tex_opaque) */
    t->cw = cw;
    t->ch = ch;
    t->holes = t->params + 4;
    for (int x = 0; x <= cw; x++)
        t->holes[x] = 0;
    for (int y = 0; y < ch; y++) {
        uint32_t *above = t->holes + y * (cw + 1), *row = above + cw + 1, run = 0;
        row[0] = 0;
        for (int x = 0; x < cw; x++) {
            run += !s->cell_opaque[y * cw + x];
            row[x + 1] = above[x + 1] + run;
        }
    }
    t->params[0] = v3d_bus(t->texels) & ~0xFFFu;
    t->params[1] = (tiled ? 0 : 1u << 31) | (uint32_t)(s->h & 2047) << 20 | (uint32_t)(s->w & 2047) << 8 |
                   1u << 7 | 1u << 4 | 1u << 2 | 1u;
    t->sheet = s;
    t->version = s->version;
    t->w = s->w;
    t->h = s->h;
    t->inv_w = 1.0f / (float)s->w;
    t->inv_h = 1.0f / (float)s->h;
    return t;
}

/* the cells of the sheet a face can sample, one texel larger on each side,
 * are all opaque */
static int tex_opaque(const tex_t *t, const r3d_corner_t v[3])
{
    float u0 = v[0].a, u1 = u0, v0 = v[0].b, v1 = v0;
    for (int i = 1; i < 3; i++) {
        if (v[i].a < u0) u0 = v[i].a;
        if (v[i].a > u1) u1 = v[i].a;
        if (v[i].b < v0) v0 = v[i].b;
        if (v[i].b > v1) v1 = v[i].b;
    }
    const int cw = t->cw, ch = t->ch;
    int cx0 = u0 < 1 ? 0 : (int)((u0 - 1) / G16_CELL), cx1 = u1 + 1 < 0 ? 0 : (int)((u1 + 1) / G16_CELL);
    int cy0 = v0 < 1 ? 0 : (int)((v0 - 1) / G16_CELL), cy1 = v1 + 1 < 0 ? 0 : (int)((v1 + 1) / G16_CELL);
    if (cx1 >= cw) cx1 = cw - 1;
    if (cy1 >= ch) cy1 = ch - 1;
    if (cx0 > cx1 || cy0 > cy1)
        return 0;
    const uint32_t *top = t->holes + cy0 * (cw + 1), *bottom = t->holes + (cy1 + 1) * (cw + 1);
    return bottom[cx1 + 1] - bottom[cx0] - top[cx1 + 1] + top[cx0] == 0;
}

/* ---------------------------------------------------------------- the job */

static void job_begin(int w, int h)
{
    /* MSAA where the probe allows it, never with the depth kept between
     * jobs (it would have 4 samples a pixel) */
    G.ms = G.msaa && !G.z_saved && !G.z_wanted && (G.ms_ok == 2 || (G.ms_ok == 1 && G.page_uniform));
    G.tile = G.ms ? V3D_TILE_MSAA : V3D_TILE;
    const int tx = (w + G.tile - 1) / G.tile, ty = (h + G.tile - 1) / G.tile;
    memset(G.tsda, 0, JOB_TSDA);        /* no stale tile state for the binner */
    v3d_cl_init(&G.cl, G.bcl, JOB_BCL);
    v3d_cl_u8(&G.cl, V3D_TILE_BINNING_MODE_CONFIG);
    v3d_cl_u32(&G.cl, v3d_bus(G.alloc));
    v3d_cl_u32(&G.cl, JOB_ALLOC);
    v3d_cl_u32(&G.cl, v3d_bus(G.tsda));
    v3d_cl_u8(&G.cl, (uint8_t)tx);
    v3d_cl_u8(&G.cl, (uint8_t)ty);
    /* tile state set up by the binner; first blocks of 32 bytes (the
     * rendering list branches to them), then blocks of 128 */
    v3d_cl_u8(&G.cl, (uint8_t)(V3D_BIN_AUTO_INIT_TSDA | 0 << 3 | 2 << 5 | (G.ms ? V3D_BIN_MSAA4 : 0)));
    v3d_cl_u8(&G.cl, V3D_START_TILE_BINNING);
    v3d_cl_u8(&G.cl, V3D_VIEWPORT_OFFSET);
    v3d_cl_u16(&G.cl, 0);
    v3d_cl_u16(&G.cl, 0);
    G.vp = 0;
    G.clipper[0] = G.clipper[1] = 0;
    G.clip[0] = G.clip[1] = G.clip[2] = G.clip[3] = -1;
    G.cfg = -1;
    G.vbytes = 0;
    G.gldraws = 0;
    G.job_no++;
    G.unif_next = G.unif;
    G.rec_next = G.recs;
    G.w = w;
    G.h = h;
    G.b_open = 0;
    G.open = 1;
    memset(G.tex_used, 0, sizeof G.tex_used);
}

static void batch_close(void)
{
    if (!G.b_open)
        return;
    G.b_open = 0;
    uint32_t n = (G.vbytes - G.b_first) / G.b_stride;
    if (!n) {                           /* nothing drawn: no packets */
        G.cl.p = G.b_start;
        return;
    }
    G.b_len[0] = (uint8_t)n;
    G.b_len[1] = (uint8_t)(n >> 8);
    G.b_len[2] = (uint8_t)(n >> 16);
    G.b_len[3] = (uint8_t)(n >> 24);
}

/* the viewport offset (x, y in 12.4), if not the one written last: 0 for
 * the NV corners (absolute), the screen's centre for the vertex shader's */
static void viewport(int x, int y)
{
    const uint32_t vp = (uint16_t)x | (uint32_t)(uint16_t)y << 16;
    if (G.vp != vp) {
        v3d_cl_u8(&G.cl, V3D_VIEWPORT_OFFSET);
        v3d_cl_u16(&G.cl, (uint16_t)x);
        v3d_cl_u16(&G.cl, (uint16_t)y);
        G.vp = vp;
    }
}

/* the clip window of g, if not the one written last */
static void clip_window(const g16_t *g)
{
    if (G.clip[0] != g->cx0 || G.clip[1] != g->cy0 || G.clip[2] != g->cx1 || G.clip[3] != g->cy1) {
        v3d_cl_u8(&G.cl, V3D_CLIP_WINDOW);
        v3d_cl_u16(&G.cl, (uint16_t)g->cx0);
        v3d_cl_u16(&G.cl, (uint16_t)g->cy0);
        v3d_cl_u16(&G.cl, (uint16_t)(g->cx1 - g->cx0));
        v3d_cl_u16(&G.cl, (uint16_t)(g->cy1 - g->cy0));
        G.clip[0] = g->cx0; G.clip[1] = g->cy0; G.clip[2] = g->cx1; G.clip[3] = g->cy1;
    }
}

/* CONFIGURATION_BITS (faces: which ones, MSAA added here; cfg: depth),
 * if not the ones written last */
static void config(int faces, uint16_t cfg)
{
    const int key = (faces | (G.ms ? V3D_CFG_MSAA4 : 0)) << 16 | cfg;
    if (G.cfg != key) {
        v3d_cl_u8(&G.cl, V3D_CONFIGURATION_BITS);
        v3d_cl_u8(&G.cl, (uint8_t)(key >> 16));
        v3d_cl_u16(&G.cl, cfg);
        G.cfg = key;
    }
}

/* 0, or -1 if the job is full */
static int batch_open(const g16_t *g, int shader, int depth, const tex_t *t)
{
    if (G.rec_next + 16 > G.recs + JOB_RECS || G.cl.p + 64 > G.cl.end)
        return -1;
    G.b_start = G.cl.p;
    clip_window(g);
    /* depth: nearer wins (less than, as the software's strict test), early
     * z except where pixels may be thrown away; NOZ: neither test nor
     * write; the effects and shadows: tested, not written */
    uint16_t cfg = depth == R3D_DEPTH_NONE ? V3D_CFG_DEPTH(7)
                 : depth == R3D_DEPTH_TEST ? V3D_CFG_DEPTH(1)
                 : shaders[shader].discard ? V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE
                 : V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE;
    /* r3d culled the back faces; MSAA: the rasteriser takes 4 samples */
    config(V3D_CFG_FRONT | V3D_CFG_BACK, cfg);
    viewport(0, 0);
    /* NV shader record: flags, vertex stride, uniforms, varyings, code,
     * uniforms, vertices (those of this batch) */
    uint8_t *r = G.rec_next;
    G.rec_next += 16;
    const uint32_t stride = VSTRIDE(shaders[shader].varyings);
    G.vbytes = (G.vbytes + 3) & ~3u;
    r[0] = 0;
    r[1] = (uint8_t)stride;
    r[2] = shaders[shader].uniforms;
    r[3] = shaders[shader].varyings;
    uint32_t a = v3d_bus(G.code + 1024 * shader);
    memcpy(r + 4, &a, 4);
    a = t ? v3d_bus(t->params) : 0;
    memcpy(r + 8, &a, 4);
    a = v3d_bus(G.verts + G.vbytes);
    memcpy(r + 12, &a, 4);
    v3d_cl_u8(&G.cl, V3D_NV_SHADER_STATE);
    v3d_cl_u32(&G.cl, v3d_bus(r));
    v3d_cl_u8(&G.cl, V3D_VERTEX_ARRAY_PRIMITIVES);
    v3d_cl_u8(&G.cl, 4);                /* triangles */
    G.b_len = G.cl.p;
    v3d_cl_u32(&G.cl, 0);               /* vertex count, at batch_close */
    v3d_cl_u32(&G.cl, 0);               /* first vertex */
    G.b_open = 1;
    G.b_shader = shader;
    G.b_depth = depth;
    G.b_tex = t;
    G.b_first = G.vbytes;
    G.b_stride = stride;
    G.b_room = G.vbytes + (BATCH_MAX - 21) * stride;
    if (G.b_room > VMAX_BYTES)
        G.b_room = VMAX_BYTES;
    if (t >= G.tex && t < G.tex + NTEX)
        G.tex_used[t - G.tex] = 1;
    return 0;
}

/* ---------------------------------------------------------------- vertices */

/* a corner while clipping: attributes (a b c, then l and f) multiplied by
 * 1/w (they are linear in screen space that way) */
typedef struct { float x, y, iw, at[9]; } cvert_t;

static cvert_t cvert(const r3d_corner_t *v)
{
    float iw = v->z > 1e-9f ? v->z : 1e-9f;
    return (cvert_t){ v->x, v->y, iw, { v->a * iw, v->b * iw, v->c * iw, v->l[0] * iw, v->l[1] * iw,
                                        v->l[2] * iw, v->f[0] * iw, v->f[1] * iw, v->f[2] * iw } };
}

/* the part of polygon in[n] where s * (x or y) <= lim, s = 1 or -1 */
static int clip_line(const cvert_t *in, int n, cvert_t *out, int axis_y, float s, float lim)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const cvert_t *p = &in[i], *q = &in[(i + 1) % n];
        float dp = s * (axis_y ? p->y : p->x) - lim, dq = s * (axis_y ? q->y : q->x) - lim;
        if (dp <= 0)
            out[m++] = *p;
        if ((dp <= 0) != (dq <= 0)) {
            float t = dp / (dp - dq);
            cvert_t *o = &out[m++];
            o->x = p->x + (q->x - p->x) * t;
            o->y = p->y + (q->y - p->y) * t;
            o->iw = p->iw + (q->iw - p->iw) * t;
            for (int k = 0; k < 9; k++)
                o->at[k] = p->at[k] + (q->at[k] - p->at[k]) * t;
        }
    }
    return m;
}

static inline float unit(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* the guard band in 12.4 plus 32768: GUARD_LO .. GUARD_LO + span */
#define GUARD_LO ((int32_t)(32768 - 16 * GUARD))

/* a corner as the shader wants it: x and y rounded to 12.4 (within the
 * guard band, so x * 16 + 32768 is positive and the cast floors), z from
 * 1/w, the colour as r3d gives it (0..1), texel coordinates scaled to 0..1;
 * the light and the fog of R3D_KIND_TEX_RGB in the colour's byte order.
 * With check, nonzero if x or y is out of the guard band (spans sx, sy in
 * 12.4): then the corner is not usable (out of the 12.4 range) */
static inline __attribute__((always_inline)) uint32_t put_corner(gvert_t *o, float x, float y, float iw, float a,
                                                                 float b, float c, const float *l, const float *f,
                                                                 int kind, const tex_t *t, int check, uint32_t sx,
                                                                 uint32_t sy)
{
    const int32_t ix = (int32_t)(x * 16.0f + 32768.5f), iy = (int32_t)(y * 16.0f + 32768.5f);
    o->x = (int16_t)(ix - 32768);
    o->y = (int16_t)(iy - 32768);
    float z = 1.0f - R3D_NEAR * iw;     /* 0 at the near plane, towards 1 far away */
    o->z = z < 0 ? 0 : z;
    o->inv_w = iw;
    if (kind == R3D_KIND_COLOUR || kind == R3D_KIND_SCREEN) {
        o->v[G.ia] = a;                 /* red or blue in byte a, as the probe found */
        o->v[1] = b;
        o->v[2 - G.ia] = c;
    } else {
        o->v[0] = a * t->inv_w;
        o->v[1] = b * t->inv_h;
        if (kind == R3D_KIND_TEX_RGB) {
            o->v[2 + G.ia] = unit(l[0]);
            o->v[3] = unit(l[1]);
            o->v[4 - G.ia] = unit(l[2]);
            o->v[5 + G.ia] = unit(f[0]);
            o->v[6] = unit(f[1]);
            o->v[7 - G.ia] = unit(f[2]);
        } else {
            o->v[2] = unit(c);
        }
    }
    return check ? ((uint32_t)(ix - GUARD_LO) > sx) | ((uint32_t)(iy - GUARD_LO) > sy) : 0;
}

/* a corner made while clipping (attributes times 1/w) */
static void put(const cvert_t *c, int kind, const tex_t *t)
{
    const float w = 1.0f / c->iw;
    const float l[3] = { c->at[3] * w, c->at[4] * w, c->at[5] * w }, f[3] = { c->at[6] * w, c->at[7] * w,
                                                                               c->at[8] * w };
    put_corner((gvert_t *)(G.verts + G.vbytes), c->x, c->y, c->iw, c->at[0] * w, c->at[1] * w, c->at[2] * w, l, f,
               kind, t, 0, 0, 0);
    G.vbytes += G.b_stride;
}

/* the batch being filled takes a triangle of this state (same screen,
 * clip window, shader, depth and texture, and room for 7 corners) */
static inline int batch_takes(const g16_t *g, int shader, int depth, const tex_t *t)
{
    return G.b_open && G.vbytes <= G.b_room && G.b_shader == shader && G.b_tex == t && G.b_depth == depth &&
           G.w == g->w && G.h == g->h && G.clip[0] == g->cx0 && G.clip[1] == g->cy0 && G.clip[2] == g->cx1 &&
           G.clip[3] == g->cy1;
}

/* a job and a batch open for this state, with room: 0, or -1 if the GPU
 * failed */
/* a job open for g */
static void job_for(const g16_t *g)
{
    if (G.open && (g->w != G.w || g->h != G.h)) {
        batch_close();                  /* another screen size: the old job is dropped */
        G.open = 0;
        G.z_saved = 0;
        G.page_uniform = 0;
    }
    if (!G.open) {
        if (G.split && !G.z_saved && !G.z_wanted) {
            /* 3D after 2D in a frame, without its depth: kept from now on */
            G.z_wanted = 1;
            kprintf("gpu3d: 3D drawn after 2D in a frame: its depth is kept from the next frame\n");
        }
        job_begin(g->w, g->h);
    }
}

static int batch_for(const g16_t *g, int shader, int depth, const tex_t *t)
{
    if (G.failed)
        return -1;
    job_for(g);
    if (G.vbytes > VMAX_BYTES) {        /* a clipped triangle is up to 7 corners */
        flush_job(g, 1);                /* the next job goes on with this depth */
        if (G.failed)
            return -1;
        job_begin(g->w, g->h);
    }
    if (!batch_takes(g, shader, depth, t)) {
        batch_close();
        if (batch_open(g, shader, depth, t) != 0) {
            flush_job(g, 1);
            if (G.failed)
                return -1;
            job_begin(g->w, g->h);
            batch_open(g, shader, depth, t);
        }
    }
    return 0;
}

/* a triangle that reaches out of the guard band, clipped to it */
static void add_clipped(const r3d_corner_t v[3], int kind, const tex_t *t, float x1, float y1)
{
    cvert_t p[16], q[16];
    int n = 3;
    for (int i = 0; i < 3; i++)
        p[i] = cvert(&v[i]);
    n = clip_line(p, n, q, 0, -1, GUARD);
    n = clip_line(q, n, p, 0, 1, x1);
    n = clip_line(p, n, q, 1, -1, GUARD);
    n = clip_line(q, n, p, 1, 1, y1);
    if (n < 3)
        return;
    for (int i = 1; i + 1 < n; i++) {   /* a fan */
        put(&p[0], kind, t);
        put(&p[i], kind, t);
        put(&p[i + 1], kind, t);
    }
    G.st.tris += (uint32_t)(n - 2);
}

/* a triangle into the job (clipped to the guard band if it reaches out of
 * the range of the 12.4 coordinates; inside: r3d found it cannot). Inlined
 * in cb_tri for each kind: no test of the kind for every corner */
static inline __attribute__((always_inline)) void add_tri(const g16_t *g, const r3d_corner_t v[3], int kind,
                                                          const tex_t *t, int depth, int shader, int inside)
{
    if (!batch_takes(g, shader, depth, t) && batch_for(g, shader, depth, t) != 0)
        return;
    /* nearly every triangle: straight in; the corners tell on the way if
     * one is out of the guard band */
    uint8_t *o = G.verts + G.vbytes;
    if (inside) {
        for (int i = 0; i < 3; i++, o += G.b_stride)
            put_corner((gvert_t *)o, v[i].x, v[i].y, v[i].z, v[i].a, v[i].b, v[i].c, v[i].l, v[i].f, kind, t, 0, 0,
                       0);
    } else {
        const uint32_t sx = (uint32_t)(16 * (g->w + 2 * (int)GUARD)), sy = (uint32_t)(16 * (g->h + 2 * (int)GUARD));
        uint32_t out = 0;
        for (int i = 0; i < 3; i++, o += G.b_stride)
            out |= put_corner((gvert_t *)o, v[i].x, v[i].y, v[i].z, v[i].a, v[i].b, v[i].c, v[i].l, v[i].f, kind,
                              t, 1, sx, sy);
        if (out) {
            add_clipped(v, kind, t, g->w + GUARD, g->h + GUARD);   /* over the corners just written */
            return;
        }
    }
    G.vbytes += 3 * G.b_stride;
    G.st.tris++;
}

static void cb_tri(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind, const g16_sheet_t *tex,
                   int depth)
{
    (void)ctx;
    const int inside = kind & R3D_INSIDE;
    kind &= ~R3D_INSIDE;
    if (kind == R3D_KIND_TEXTURE || kind == R3D_KIND_TEX_RGB) {
        const tex_t *t = tex ? tex_get(g, tex) : NULL;
        if (t) {
            const int opaque = tex_opaque(t, v);
            if (kind == R3D_KIND_TEXTURE)
                add_tri(g, v, kind, t, depth, opaque ? SH_TEX : SH_TEX_ALPHA, inside);
            else
                add_tri(g, v, kind, t, depth, opaque ? SH_TEX_RGB : SH_TEX_RGB_ALPHA, inside);
            return;
        }
        /* no texture on the GPU (too large): grey times the light */
        r3d_corner_t c[3];
        for (int i = 0; i < 3; i++) {
            const float k = kind == R3D_KIND_TEXTURE ? v[i].c : (v[i].l[0] + v[i].l[1] + v[i].l[2]) * (2.0f / 3.0f);
            c[i] = (r3d_corner_t){ v[i].x, v[i].y, v[i].z, 200 * k * (1.0f / 255.0f), 200 * k * (1.0f / 255.0f),
                                   200 * k * (1.0f / 255.0f), { 0, 0, 0 }, { 0, 0, 0 } };
        }
        add_tri(g, c, R3D_KIND_COLOUR, NULL, depth, SH_COLOUR, inside);
        return;
    }
    if (kind == R3D_KIND_SCREEN)
        add_tri(g, v, kind, NULL, depth, SH_SCREEN, inside);
    else
        add_tri(g, v, R3D_KIND_COLOUR, NULL, depth, SH_COLOUR, inside);
}

static void cb_zclear(void *ctx, const g16_t *g)
{
    (void)ctx;
    /* what is drawn next must not see the depth of what was drawn so far:
     * that goes to the screen now, and the next job starts with a clear
     * depth buffer */
    if (G.open && (G.vbytes || G.gldraws))
        gpu3d_flush(g, 0);
    G.z_saved = G.split = 0;
}

/* ---------------------------------------------------------------- meshes (M36) */

/* the vertex shaders of meshes, their words a corner (x y z first) */
static const struct { const uint32_t *code; size_t size; uint8_t words, kb; } vshaders[GV_COUNT] = {
    { vs_baked, sizeof vs_baked, 9, CODE_VS },          /* colour, light */
    { vs_tex_rgb, sizeof vs_tex_rgb, 8, CODE_VS + 1 },  /* s t, light */
};

/* the groups of a face: vertex shader, fragment shader, lamps (0: an
 * emissive face) and the levels of detail that show it */
static uint32_t group_key(int vs, int fs, int lamps, int lods)
{
    return (uint32_t)vs << 12 | (uint32_t)fs << 8 | (uint32_t)lamps << 4 | (uint32_t)lods;
}

/* corner k of face f in group key of mesh m into o */
static void put_mesh_corner(float *o, const r3d_mesh_t *m, int f, int k, int vs, int emissive, const tex_t *t)
{
    const v3_t v = m->verts[m->faces[f * 3 + k]];
    o[0] = v.x; o[1] = v.y; o[2] = v.z;
    const uint8_t *bl = m->clight ? m->clight + f * 9 + k * 3 : NULL;
    float l[3] = { 1, 1, 1 };
    if (bl && !emissive) {
        l[G.ia] = bl[0] * (1.0f / 128);         /* red where the probe found it */
        l[1] = bl[1] * (1.0f / 128);
        l[2 - G.ia] = bl[2] * (1.0f / 128);
    }
    if (vs == GV_TEX_RGB) {
        o[3] = m->uv[f * 6 + k * 2] * t->inv_w;
        o[4] = m->uv[f * 6 + k * 2 + 1] * t->inv_h;
        o[5] = l[0]; o[6] = l[1]; o[7] = l[2];
    } else {
        const uint32_t c = m->colors[f];
        o[3 + G.ia] = (float)(c >> 16 & 255) * (1.0f / 255.0f);
        o[4] = (float)(c >> 8 & 255) * (1.0f / 255.0f);
        o[5 - G.ia] = (float)(c & 255) * (1.0f / 255.0f);
        o[6] = l[0]; o[7] = l[1]; o[8] = l[2];
    }
}

/* the corners of m in groups (gmesh_t), or 0 if the GPU cannot take it:
 * faces of a colour (lit by the light baked at their corners, or not at
 * all) and textured faces of a "lit" model; no skeleton, no textured
 * screen-door; unlit: every face at full light */
static int mesh_build(const g16_t *g, gmesh_t *e, const r3d_mesh_t *m, int unlit)
{
    if (m->bones || m->nfaces <= 0 || m->nfaces > 65535 / 3 || (!unlit && !m->clight))
        return 0;
    const tex_t *t = NULL;
    uint32_t key[64];
    int nkeys = 0;
    static uint8_t gidx[65535 / 3];
    for (int f = 0; f < m->nfaces; f++) {
        const uint32_t c = m->colors[f];
        const int textured = (c & R3D_TEXTURED) && m->uv && m->tex;
        const int emissive = unlit || (c & R3D_EMISSIVE);
        int lods = 0;
        for (int d = 0; d < 4; d++)
            lods |= R3D_LOD_SHOWS(c, (unsigned)d) << d;
        int vs, fs;
        if (textured) {
            if ((c & R3D_SCREEN) || unlit)
                return 0;
            if (!t && !(t = tex_get(g, m->tex)))
                return 0;
            r3d_corner_t v[3];
            for (int k = 0; k < 3; k++) {
                v[k].a = m->uv[f * 6 + k * 2];
                v[k].b = m->uv[f * 6 + k * 2 + 1];
            }
            vs = GV_TEX_RGB;
            fs = tex_opaque(t, v) ? SH_TEX_RGB : SH_TEX_RGB_ALPHA;
        } else {
            vs = GV_BAKED;
            fs = (c & R3D_SCREEN) ? SH_SCREEN : SH_COLOUR;
        }
        const uint32_t k = group_key(vs, fs, !emissive && !unlit, lods);
        int i = 0;
        while (i < nkeys && key[i] != k)
            i++;
        if (i == nkeys) {
            if (nkeys == MESH_GROUPS)
                return 0;
            key[nkeys++] = k;
        }
        gidx[f] = (uint8_t)i;
    }
    /* the groups one after the other, each with the stride of its shader */
    uint32_t bytes = 0, count[MESH_GROUPS] = { 0 };
    for (int f = 0; f < m->nfaces; f++)
        count[gidx[f]] += 3;
    for (int i = 0; i < nkeys; i++)
        bytes += count[i] * vshaders[key[i] >> 12].words * 4u;
    uint8_t *b = aligned_alloc(16, (bytes + 15) & ~15u);
    if (!b)
        return 0;
    uint32_t at = 0;
    for (int i = 0; i < nkeys; i++) {
        ggroup_t *gr = &e->g[i];
        gr->vs = (uint8_t)(key[i] >> 12);
        gr->fs = (uint8_t)(key[i] >> 8 & 15);
        gr->lamps = (uint8_t)(key[i] >> 4 & 1);
        gr->lods = (uint8_t)(key[i] & 15);
        gr->first = at;
        gr->n = count[i];
        const uint32_t stride = vshaders[gr->vs].words * 4u;
        float *o = (float *)(b + at);
        for (int f = 0; f < m->nfaces; f++) {
            if (gidx[f] != i)
                continue;
            for (int k = 0; k < 3; k++, o += stride / 4)
                put_mesh_corner(o, m, f, k, gr->vs, !gr->lamps, t);
        }
        at += count[i] * stride;
    }
    e->corners = b;
    e->ngroups = nkeys;
    e->tex = t;
    e->tex_version = t ? t->version : 0;
    return 1;
}

/* the cached corners of m (made again if it or its texture changed), or
 * NULL; a slot the waiting job still reads is drawn first */
static gmesh_t *mesh_get(const g16_t *g, const r3d_mesh_t *m, int unlit)
{
    uint16_t *h = &G.hint[((uintptr_t)m >> 4) % MESH_HINTS];
    gmesh_t *e = G.gm[*h].m == m ? &G.gm[*h] : NULL;
    if (!e) {
        /* the least used slot, one the open job does not read if any */
        gmesh_t *old = NULL;
        for (int i = 0; i < NMESH; i++) {
            gmesh_t *x = &G.gm[i];
            if (x->m == m) {
                e = x;
                break;
            }
            const int busy = x->corners && x->job == G.job_no && G.open;
            const int old_busy = old && old->corners && old->job == G.job_no && G.open;
            if (!old || (old_busy && !busy) || (busy == old_busy && x->used < old->used))
                old = x;
        }
        if (!e)
            e = old;
        *h = (uint16_t)(e - G.gm);
        if (e->m != m)
            e->version = 0;             /* made below */
    }
    if (e->m == m && e->version == m->version && e->unlit == unlit &&
        (!e->tex || (e->tex->sheet == m->tex && e->tex->version == e->tex_version))) {
        e->used = ++G.tick;
        return e->corners ? e : NULL;
    }
    if (e->corners && e->job == G.job_no && G.open && flush_job(g, 1) != 0)
        return NULL;
    free(e->corners);
    e->corners = NULL;
    e->m = m;
    e->version = m->version;
    e->unlit = unlit;
    e->used = ++G.tick;
    if (!mesh_build(g, e, m, unlit))
        e->corners = NULL;
    return e->corners ? e : NULL;
}

/* one GL batch: the V3D reads the corners of group gr, its vertex shader
 * places them with matrix M, the binner throws the back faces away */
static int gl_draw(const g16_t *g, gmesh_t *e, const ggroup_t *gr, const float M[12], const r3d_env_t *env,
                   int depth)
{
    if (G.failed)
        return -1;
    job_for(g);
    batch_close();
    if (G.rec_next + 64 > G.recs + JOB_RECS || G.cl.p + 128 > G.cl.end ||
        G.unif_next + 80 > G.unif + JOB_UNIF / 4) {
        if (flush_job(g, 1) != 0)
            return -1;
        job_begin(g->w, g->h);
    }
    clip_window(g);
    const int fs = gr->fs;
    const uint16_t cfg = depth == R3D_DEPTH_NONE ? V3D_CFG_DEPTH(7)
                       : depth == R3D_DEPTH_TEST ? V3D_CFG_DEPTH(1)
                       : shaders[fs].discard ? V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE
                       : V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE;
    config(V3D_CFG_FRONT | (G.gl_cw ? V3D_CFG_CW : 0), cfg);
    /* the shaders place the corners from the screen's centre; corners
     * made by the clipper: Xc / Wc times the half width (Xc = x f / (w/2),
     * Wc = depth), y down the screen, Zs = Zc / Wc + 1 (Zc = -NEAR) */
    viewport((int)(env->cx * 16.0f), (int)(env->cy * 16.0f));
    const int clipping = !env->inside;
    if (clipping && (G.clipper[0] != env->cx || G.clipper[1] != env->cy)) {
        v3d_cl_u8(&G.cl, V3D_CLIPPER_XY_SCALING);
        v3d_cl_f32(&G.cl, env->cx * 16.0f);
        v3d_cl_f32(&G.cl, -env->cy * 16.0f);
        v3d_cl_u8(&G.cl, V3D_CLIPPER_Z_SCALING);
        v3d_cl_f32(&G.cl, 1.0f);
        v3d_cl_f32(&G.cl, 1.0f);
        if (G.clip_ok == 2) {
            v3d_cl_u8(&G.cl, V3D_Z_MIN_MAX_CLIPPING_PLANES);
            v3d_cl_f32(&G.cl, 0.0f);
            v3d_cl_f32(&G.cl, 1.0f);
        }
        G.clipper[0] = env->cx;
        G.clipper[1] = env->cy;
    }
    /* uniforms, in the order the shaders read them (tools/qpuasm.py): the
     * matrix, f*16, 0.5 (rounding), -f*16, 0.5, -NEAR; then the coordinate
     * shader's f/(w/2), f/(h/2), and the vertex shader's fog and lamps */
    uint32_t *cs = G.unif_next, *vs = cs + 20;
    const float place[17] = { M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11],
                              env->f * 16.0f, 0.5f, -env->f * 16.0f, 0.5f, -R3D_NEAR };
    memcpy(cs, place, sizeof place);
    const float clip[2] = { env->f / env->cx, env->f / env->cy };
    memcpy(cs + 17, clip, sizeof clip);
    memcpy(vs, place, sizeof place);
    float fl[5 + 4 * 7];
    fl[0] = env->fog_near;
    fl[1] = env->fog_k;
    fl[2 + G.ia] = env->fog[0];                 /* bytes a b c */
    fl[3] = env->fog[1];
    fl[4 - G.ia] = env->fog[2];
    for (int i = 0; i < 4; i++) {
        float *L = fl + 5 + 7 * i;
        if (i < env->nlamps && gr->lamps) {
            memcpy(L, env->lamp[i], 4 * sizeof *L);
            L[4 + G.ia] = env->lamp[i][4];
            L[5] = env->lamp[i][5];
            L[6 - G.ia] = env->lamp[i][6];
        } else {
            memset(L, 0, 7 * sizeof *L);        /* k 0: no light from it */
        }
    }
    memcpy(vs + 17, fl, sizeof fl);
    G.unif_next += 20 + 17 + 5 + 28;
    /* GL shader record: flags (clipping for meshes not inside the guard
     * band and the near plane), the fragment shader (its varyings, its uniforms: the
     * texture), the vertex shader (x y z, then the rest of the corner),
     * the coordinate shader (x y z), the two attributes (address, bytes -
     * 1, stride, VPM row of each shader) */
    const uint32_t words = vshaders[gr->vs].words, stride = words * 4;
    uint8_t *r = G.rec_next;
    G.rec_next += 64;
    memset(r, 0, 64);
    uint32_t a;
    r[0] = clipping ? 4 : 0;
    r[3] = shaders[fs].varyings;
    a = v3d_bus(G.code + 1024 * fs); memcpy(r + 4, &a, 4);
    a = gr->vs == GV_TEX_RGB ? v3d_bus(e->tex->params) : 0; memcpy(r + 8, &a, 4);
    r[14] = 3; r[15] = (uint8_t)words;
    a = v3d_bus(G.code + 1024 * vshaders[gr->vs].kb); memcpy(r + 16, &a, 4);
    a = v3d_bus(vs); memcpy(r + 20, &a, 4);
    r[26] = 1; r[27] = 3;
    a = v3d_bus(G.code + 1024 * CODE_CS); memcpy(r + 28, &a, 4);
    a = v3d_bus(cs); memcpy(r + 32, &a, 4);
    a = v3d_bus(e->corners + gr->first); memcpy(r + 36, &a, 4);
    r[40] = 11; r[41] = (uint8_t)stride; r[42] = 0; r[43] = 0;
    a = v3d_bus(e->corners + gr->first + 12); memcpy(r + 44, &a, 4);
    r[48] = (uint8_t)(stride - 12 - 1); r[49] = (uint8_t)stride; r[50] = 3; r[51] = 0;
    v3d_cl_u8(&G.cl, V3D_GL_SHADER_STATE);
    v3d_cl_u32(&G.cl, v3d_bus(r) | 2);
    v3d_cl_u8(&G.cl, V3D_VERTEX_ARRAY_PRIMITIVES);
    v3d_cl_u8(&G.cl, 4);
    v3d_cl_u32(&G.cl, gr->n);
    v3d_cl_u32(&G.cl, 0);
    e->job = G.job_no;
    if (e->tex && e->tex >= G.tex && e->tex < G.tex + NTEX)
        G.tex_used[e->tex - G.tex] = 1;
    G.gldraws++;
    G.st.tris += gr->n / 3;
    G.st.gltris += gr->n / 3;
    return 0;
}

static int cb_mesh(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float M[12], const r3d_env_t *env,
                   int depth)
{
    (void)ctx;
    if (!G.gl_ok || !G.gl_on || G.failed || (!env->inside && !G.clip_ok))
        return 0;
    gmesh_t *e = mesh_get(g, m, env->unlit);
    if (!e)
        return 0;
    for (int i = 0; i < e->ngroups; i++)
        if ((e->g[i].lods >> env->detail & 1) && gl_draw(g, e, &e->g[i], M, env, depth) != 0)
            return 0;
    G.st.glmeshes++;
    return 1;
}

/* ---------------------------------------------------------------- run */

/* Rendering list for a frame at bus address fb (w x h, BGR565): every
 * tile loaded from the frame (load), drawn from its tile list (bin) and
 * stored back. The depth starts cleared, or loaded from zbuf (zload);
 * zstore keeps it there for the next job. Two loads of a tile take place
 * one at a time (tile coordinates, then a store of nothing that clears
 * nothing), and so do two stores, as Linux's vc4 does. */
static uint32_t rcl_build(uint32_t fb, int w, int h, int bin, int load, uint32_t clear, int zload, int zstore,
                          int ms)
{
    const int ts = ms ? V3D_TILE_MSAA : V3D_TILE;
    const int tx = (w + ts - 1) / ts, ty = (h + ts - 1) / ts;
    const uint32_t zb = v3d_bus(G.zbuf);
    v3d_cl_t cl;
    v3d_cl_init(&cl, G.rcl, JOB_RCL);
    v3d_cl_u8(&cl, V3D_CLEAR_COLORS);
    v3d_cl_u32(&cl, clear);
    v3d_cl_u32(&cl, clear);
    v3d_cl_u32(&cl, 0x00FFFFFF);        /* depth 1.0, VG mask 0 */
    v3d_cl_u8(&cl, 0);
    v3d_cl_u8(&cl, V3D_TILE_RENDERING_MODE_CONFIG);
    v3d_cl_u32(&cl, fb);
    v3d_cl_u16(&cl, (uint16_t)w);
    v3d_cl_u16(&cl, (uint16_t)h);
    v3d_cl_u16(&cl, (uint16_t)(V3D_RENDER_BGR565 | (ms ? V3D_RENDER_MSAA4 : 0)));
    v3d_cl_u8(&cl, V3D_TILE_COORDINATES);   /* a store of nothing: the tile */
    v3d_cl_u8(&cl, 0);                      /* buffer takes the clear values */
    v3d_cl_u8(&cl, 0);
    v3d_cl_u8(&cl, V3D_STORE_TILE_BUFFER_GENERAL);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u32(&cl, 0);
    for (int y = 0; y < ty; y++)
        for (int x = 0; x < tx; x++) {
            if (load) {                     /* takes place at the tile coordinates */
                v3d_cl_u8(&cl, V3D_LOAD_TILE_BUFFER_GENERAL);
                v3d_cl_u16(&cl, V3D_LOAD_COLOUR_BGR565);
                v3d_cl_u32(&cl, fb);
            }
            if (zload) {
                if (load) {
                    v3d_cl_u8(&cl, V3D_TILE_COORDINATES);
                    v3d_cl_u8(&cl, (uint8_t)x);
                    v3d_cl_u8(&cl, (uint8_t)y);
                    v3d_cl_u8(&cl, V3D_STORE_TILE_BUFFER_GENERAL);
                    v3d_cl_u16(&cl, V3D_LOADSTORE_NONE | V3D_STORE_NO_COLOUR_CLEAR | V3D_STORE_NO_ZS_CLEAR |
                                    V3D_STORE_NO_VG_CLEAR);
                    v3d_cl_u32(&cl, 0);
                }
                v3d_cl_u8(&cl, V3D_LOAD_TILE_BUFFER_GENERAL);
                v3d_cl_u16(&cl, V3D_LOADSTORE_ZS_TFORMAT);
                v3d_cl_u32(&cl, zb);
            }
            v3d_cl_u8(&cl, V3D_TILE_COORDINATES);
            v3d_cl_u8(&cl, (uint8_t)x);
            v3d_cl_u8(&cl, (uint8_t)y);
            if (bin) {
                v3d_cl_u8(&cl, V3D_BRANCH_TO_SUB_LIST);
                v3d_cl_u32(&cl, v3d_bus(G.alloc) + (uint32_t)(y * tx + x) * 32);
            }
            if (zstore) {                   /* the colour stays for the store below */
                v3d_cl_u8(&cl, V3D_STORE_TILE_BUFFER_GENERAL);
                v3d_cl_u16(&cl, V3D_LOADSTORE_ZS_TFORMAT | V3D_STORE_NO_COLOUR_CLEAR);
                v3d_cl_u32(&cl, zb);
                v3d_cl_u8(&cl, V3D_TILE_COORDINATES);
                v3d_cl_u8(&cl, (uint8_t)x);
                v3d_cl_u8(&cl, (uint8_t)y);
            }
            v3d_cl_u8(&cl, x == tx - 1 && y == ty - 1 ? V3D_STORE_MS_TILE_BUFFER_EOF
                                                     : V3D_STORE_MS_TILE_BUFFER);
        }
    return v3d_bus(cl.p);
}

static int run(int bin, uint32_t rcl_end)
{
    uint32_t bus_us = 0, rus = 0;
    v3d_set_overflow(v3d_bus(G.overflow), JOB_OVERFLOW);    /* the GPU test sets its own */
    int err = v3d_run(bin ? v3d_bus(G.bcl) : 0, bin ? v3d_bus(G.cl.p) : 0, v3d_bus(G.rcl), rcl_end,
                      TIMEOUT_US, &bus_us, &rus);
    G.st.jobs++;
    G.st.bin_us += bus_us;
    G.st.render_us += rus;
    if (bus_us + rus > G.st.max_us)
        G.st.max_us = bus_us + rus;
    if (err) {
        static char buf[640];
        v3d_dump(buf, sizeof buf);
        kprintf("gpu3d: the V3D did not finish:\n%s", buf);
    }
    return err;
}

int gpu3d_pending(void)
{
    return G.open && (G.vbytes || G.b_open || G.gldraws);
}

void gpu3d_drop(void)
{
    batch_close();
    G.open = 0;
    G.z_saved = G.split = G.z_wanted = 0;
    G.page_uniform = 0;
}

void gpu3d_set_msaa(int on)
{
    G.msaa = on;
}

int gpu3d_msaa(void)
{
    return G.ms_ok;
}

int gpu3d_msaa_on(void)
{
    return G.msaa && G.ms_ok > 0;
}

void gpu3d_tiled_textures(int on)
{
    G.tformat_off = !on;
    for (int i = 0; i < NTEX; i++)
        G.tex[i].sheet = NULL;          /* made again in the other layout */
}

int gpu3d_vshader(void)
{
    return G.gl_ok;
}

void gpu3d_set_vshader(int on)
{
    G.gl_on = on;
}

int gpu3d_vshader_on(void)
{
    return G.gl_ok && G.gl_on;
}

int gpu3d_tiles(void)
{
    return G.tformat;
}

void gpu3d_page(int uniform, uint16_t c)
{
    G.page_uniform = uniform;
    G.page_colour = c;
}

/* an RGB565 colour as the tile buffer's clear colour (bytes a b c d, red
 * in byte a or c as the probe found) */
static uint32_t clear_of(uint16_t c)
{
    uint32_t r = (c >> 11) << 3 | c >> 13, g = (c >> 5 & 63) << 2 | (c >> 9 & 3),
             b = (c & 31) << 3 | (c >> 2 & 7);
    return 0xFF000000u | (G.red_a ? b << 16 | g << 8 | r : r << 16 | g << 8 | b);
}

/* the depth of a w x h frame fits in zbuf (T-format: sides rounded up to 32) */
static int zbuf_fits(int w, int h)
{
    return (uint32_t)((w + 31) & ~31) * (uint32_t)((h + 31) & ~31) * 4u <= JOB_ZBUF;
}

/* runs the job; store: its depth goes to zbuf for the next one */
static int flush_job(const g16_t *g, int store)
{
    if (!G.open)
        return G.failed ? -1 : 0;
    batch_close();
    G.open = 0;
    if (G.failed)
        return -1;
    if (!G.vbytes && !G.gldraws)
        return 0;
    if (g->stride != (uint32_t)g->w || g->w != G.w || g->h != G.h) {
        disable("the page is not as wide as the screen");
        return -1;
    }
    v3d_cl_u8(&G.cl, V3D_FLUSH);        /* ends the tile lists */
    v3d_cl_u8(&G.cl, V3D_NOP);
    if (G.cl.overflow) {
        disable("binning list overflow");
        return -1;
    }
    const int zload = !G.ms && G.z_saved && G.z_w == g->w && G.z_h == g->h;
    const int zstore = !G.ms && store && zbuf_fits(g->w, g->h);
    /* a page of one colour is not read back: the tiles start with it */
    const int load = !G.page_uniform;
    uint32_t fb = bus_of(g->px);
    uint32_t end = rcl_build(fb, g->w, g->h, 1, load, load ? 0 : clear_of(G.page_colour), zload, zstore, G.ms);
    if (G.ms)
        G.st.msjobs++;
    G.page_uniform = 0;                 /* now it has the 3D too */
    if (!load)
        G.st.cleared++;
    if (run(1, end) != 0) {
        disable("the GPU did not finish a frame (registers in the log)");
        return -1;
    }
    G.z_saved = zstore;
    G.z_w = g->w;
    G.z_h = g->h;
    if (zload || zstore)
        G.st.zjobs++;
    return 0;
}

int gpu3d_flush(const g16_t *g, int keep)
{
    const int had = G.open && (G.vbytes || G.gldraws);
    /* the depth is stored only for cartridges that need it: most draw all
     * their 3D, then the HUD, and a store is 1 MiB of memory traffic */
    int r = flush_job(g, keep && G.z_wanted);
    if (!keep) {
        G.z_saved = 0;                  /* the next job starts from a clear depth */
        G.split = 0;
    } else if (had) {
        G.split = 1;
    }
    return r;
}

void gpu3d_take_stats(gpu3d_stats_t *s)
{
    *s = G.st;
    memset(&G.st, 0, sizeof G.st);
}

/* ---------------------------------------------------------------- init */

static uint8_t *block_alloc(void)
{
    uint8_t *b = aligned_alloc(4096, JOB_BLOCK);
    if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + JOB_BLOCK - 1) >> 28)) {
        uint8_t *again = aligned_alloc(4096, JOB_BLOCK);    /* across 256 MiB: another */
        free(b);
        b = again;
        if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + JOB_BLOCK - 1) >> 28)) {
            free(b);
            b = NULL;
        }
    }
    return b;
}

/* MSAA 4x: multisample jobs without binning on the probe's buffer. A
 * clear alone must store its colour; then a load of the buffer: if it
 * fills the 4 samples of a pixel, the store (their mean) gives the buffer
 * back; if it fills one, a mix with the clear colour. 2: MSAA on any page,
 * 1: only on pages of one colour (cleared, not loaded), 0: none. A job
 * that does not end only turns MSAA off. */
static int probe_ms(void)
{
    const uint32_t bus = v3d_bus(G.probe);
    for (int i = 0; i < PROBE_W * PROBE_H; i++)
        G.probe[i] = 0x1234;
    if (run(0, rcl_build(bus, PROBE_W, PROBE_H, 0, 0, clear_of(0x001F), 0, 0, 1)) != 0 ||
        G.probe[10 * PROBE_W + 10] != 0x001F || G.probe[PROBE_W * PROBE_H - 1] != 0x001F)
        return 0;
    for (int i = 0; i < PROBE_W * PROBE_H; i++)
        G.probe[i] = 0x07E0;
    if (run(0, rcl_build(bus, PROBE_W, PROBE_H, 0, 1, clear_of(0xF800), 0, 0, 1)) != 0)
        return 1;
    return G.probe[10 * PROBE_W + 10] == 0x07E0 && G.probe[PROBE_W * PROBE_H - 1] == 0x07E0 ? 2 : 1;
}

/* G.probe holds, for each texel of a 64x64 T-format texture, the word the
 * TMU read: 1 if it is a layout of 4 KiB tiles in rows (learned into
 * t_inner and t_rev), 0 if not */
static int tformat_learn(void)
{
    static uint8_t seen[PROBE_W * PROBE_H / 8];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < PROBE_W * PROBE_H; i++) {
        const uint16_t w = G.probe[i];
        if (w >= PROBE_W * PROBE_H || (seen[w / 8] >> (w % 8) & 1))
            return 0;                   /* not each word once */
        seen[w / 8] |= (uint8_t)(1u << (w % 8));
    }
    int pos[2][2];                      /* the place of each tile, [row][column] */
    for (int ty = 0; ty < 2; ty++)
        for (int tx = 0; tx < 2; tx++) {
            pos[ty][tx] = G.probe[ty * 32 * PROBE_W + tx * 32] / 1024;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    if (G.probe[(ty * 32 + y) * PROBE_W + tx * 32 + x] / 1024 != pos[ty][tx])
                        return 0;       /* a tile is not 4 KiB in one piece */
        }
    if (pos[0][0] != 0 || pos[0][1] != 1)
        return 0;
    if (pos[1][0] == 2 && pos[1][1] == 3)
        G.t_rev = 0;
    else if (pos[1][0] == 3 && pos[1][1] == 2)
        G.t_rev = 1;
    else
        return 0;
    for (int row = 0; row < 2; row++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                const int i = (row * 32 + y) * PROBE_W + x, j = i + 32;   /* both tiles of the row */
                const uint16_t a = (uint16_t)(G.probe[i] % 1024), b = (uint16_t)(G.probe[j] % 1024);
                if (a != b)
                    return 0;           /* tiles of a row laid out differently */
                G.t_inner[row][y * 32 + x] = a;
            }
    return 1;
}

/* M36: two triangles placed by the vertex shader on a cleared buffer, red
 * on the left as r3d's front faces turn (clockwise on the screen), green on
 * the right the other way round. Whichever the V3D keeps tells the
 * clockwise bit; points inside and outside the red one check where the
 * shader placed it. 1 if all of it holds, else 0 (the meshes stay on the
 * ARM's path). */
static int probe_gl(const g16_t *pg)
{
    static r3d_mesh_t m;
    static v3_t v[6], n[2];
    static uint16_t f[6] = { 0, 1, 2, 3, 4, 5 };
    static uint32_t c[2] = { 0xFF0000, 0x00FF00 };
    /* camera space at depth 2, f = 32, centre (32, 32): x = (sx - 32) / 16 */
    static const float s[6][2] = { { 6, 10 }, { 28, 10 }, { 6, 54 }, { 36, 10 }, { 36, 54 }, { 58, 10 } };
    for (int i = 0; i < 6; i++)
        v[i] = (v3_t){ (s[i][0] - 32) / 16.0f, (32 - s[i][1]) / 16.0f, 0 };
    n[0] = n[1] = (v3_t){ 0, 0, -1 };
    m.nverts = 6;
    m.nfaces = 2;
    m.verts = v;
    m.faces = f;
    m.colors = c;
    m.normals = n;
    m.version = 0xFFFFFFFFu;            /* not one of r3d's */
    const float M[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2 };
    r3d_env_t env;
    memset(&env, 0, sizeof env);
    env.f = env.cx = env.cy = 32;
    env.unlit = 1;
    env.inside = 1;
    for (int cw = 0; cw < 2; cw++) {
        G.gl_cw = cw;
        memset(G.probe, 0, JOB_PROBE);
        gmesh_t *e = mesh_get(pg, &m, 1);
        if (!e || e->ngroups != 1 || gl_draw(pg, e, &e->g[0], M, &env, R3D_DEPTH_WRITE) != 0 ||
            gpu3d_flush(pg, 0) != 0)
            return 0;
        const uint16_t left = G.probe[20 * PROBE_W + 10], right = G.probe[20 * PROBE_W + 40],
                       in = G.probe[40 * PROBE_W + 10], out = G.probe[40 * PROBE_W + 20];
        if (left == 0xF800 && right == 0 && in == 0xF800 && out == 0)
            return 1;                   /* red only, where it should be */
        if (!(left == 0 && right == 0x07E0)) {
            kprintf("gpu3d: vertex shader probe: %04x %04x %04x %04x (clockwise bit %d)\n", left, right, in, out, cw);
            return 0;
        }
    }
    return 0;
}

/* M36: whether the GPU clips what the vertex shader placed (the record's
 * flag 4), as GL wants: a floor that goes behind the camera (red, both
 * ways round) cut at the near plane, a triangle reaching thousands of
 * pixels to the right (green: out of the 12.4 range) cut at the guard
 * band, one nearer than the near plane (blue, over the green) not drawn.
 * Tried without and then with Z_MIN_MAX_CLIPPING_PLANES (0..1); 0 if
 * neither drew the right pixels. Without the depth test: what is drawn
 * does not hang on how the GPU stores a depth below 0. */
static int probe_clip(const g16_t *pg)
{
    static r3d_mesh_t m;
    static v3_t v[9], n[4];
    static uint16_t f[12] = { 0, 1, 2, 0, 2, 1, 3, 5, 4, 6, 7, 8 };
    static uint32_t c[4] = { 0xFF0000, 0xFF0000, 0x00FF00, 0x0000FF };
    /* f = 32, centre (32, 32): the floor y = -1 from depth 4 to -4 (at
     * row 32 + 32 / depth), the green at depth 2 from (4, 4) and (4, 28)
     * to 3000 pixels right of the centre, the blue (44, 4) (60, 4) (44, 20)
     * at depth 0.06 */
    static const v3_t p[9] = { { -2, -1, 4 }, { 2, -1, 4 }, { 0, -1, -4 },
                               { -1.75f, 1.75f, 2 }, { -1.75f, 0.25f, 2 }, { 187.5f, 1.0f, 2 },
                               { 0.0225f, 0.0525f, 0.06f }, { 0.0525f, 0.0525f, 0.06f }, { 0.0225f, 0.0225f, 0.06f } };
    static const struct { uint8_t x, y; uint16_t want; } at[] = {
        { 32, 44, 0xF800 }, { 2, 60, 0xF800 }, { 61, 60, 0xF800 }, { 32, 60, 0xF800 },
        { 8, 44, 0 }, { 32, 36, 0 }, { 32, 30, 0 },
        { 40, 16, 0x07E0 }, { 10, 16, 0x07E0 }, { 40, 2, 0 }, { 2, 16, 0 }, { 40, 31, 0 }, { 48, 8, 0x07E0 },
    };
    memcpy(v, p, sizeof p);
    for (int i = 0; i < 4; i++)
        n[i] = (v3_t){ 0, 0, -1 };
    m.nverts = 9;
    m.nfaces = 4;
    m.verts = v;
    m.faces = f;
    m.colors = c;
    m.normals = n;
    m.version = 0xFFFFFFFEu;            /* not one of r3d's */
    const float M[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    r3d_env_t env;
    memset(&env, 0, sizeof env);
    env.f = env.cx = env.cy = 32;
    env.unlit = 1;
    for (int zplanes = 1; zplanes <= 2; zplanes++) {
        G.clip_ok = zplanes;
        memset(G.probe, 0, JOB_PROBE);
        gmesh_t *e = mesh_get(pg, &m, 1);
        if (!e || e->ngroups != 1 || gl_draw(pg, e, &e->g[0], M, &env, R3D_DEPTH_NONE) != 0 ||
            gpu3d_flush(pg, 0) != 0) {
            G.clip_ok = 0;
            return 0;
        }
        unsigned wrong = 0;
        for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++)
            wrong |= (G.probe[at[i].y * PROBE_W + at[i].x] != at[i].want) << i;
        if (!wrong)
            return zplanes;
        kprintf("gpu3d: clipping probe (Z planes %s): pixels %04x wrong\n", zplanes == 2 ? "on" : "off", wrong);
    }
    G.clip_ok = 0;
    return 0;
}

/* The order of red and blue: a clear with byte a of the colour full says
 * where byte a lands in BGR565; a texture whose texels have byte a full
 * says whether the TMU keeps the bytes in place. */
static int probe(void)
{
    g16_t pg;
    memset(&pg, 0, sizeof pg);
    pg.px = G.probe;
    pg.stride = PROBE_W;
    pg.w = PROBE_W;
    pg.h = PROBE_H;
    pg.cx1 = PROBE_W;
    pg.cy1 = PROBE_H;

    memset(G.probe, 0x55, JOB_PROBE);
    if (run(0, rcl_build(v3d_bus(G.probe), PROBE_W, PROBE_H, 0, 0, 0x000000FFu, 0, 0, 0)) != 0) {
        disable("probe: a clear did not finish");
        return -1;
    }
    uint16_t c = G.probe[PROBE_W * 10 + 10];
    if (c == 0xF800)
        G.red_a = 1;
    else if (c == 0x001F)
        G.red_a = 0;
    else {
        ksnprintf(G.why, sizeof G.why, "probe: a clear gave %04x (expected f800 or 001f)", c);
        disable(G.why);
        return -1;
    }
    G.ia = G.red_a ? 0 : 2;

    /* a quad over the whole buffer, textured by texels with byte a full */
    static tex_t pt;
    pt.texels = G.probe_tex;
    pt.w = PROBE_W;
    pt.h = PROBE_H;
    pt.params = G.probe_tex + PROBE_W * PROBE_H;
    pt.inv_w = 1.0f / PROBE_W;
    pt.inv_h = 1.0f / PROBE_H;
    for (int i = 0; i < PROBE_W * PROBE_H; i++)
        pt.texels[i] = 0xFF0000FFu;
    pt.params[0] = v3d_bus(pt.texels) & ~0xFFFu;
    pt.params[1] = 1u << 31 | (uint32_t)PROBE_H << 20 | (uint32_t)PROBE_W << 8 | 1u << 7 | 1u << 4 |
                   1u << 2 | 1u;
    memset(G.probe, 0, JOB_PROBE);
    static const float q[4][2] = { { 0, 0 }, { PROBE_W, 0 }, { PROBE_W, PROBE_H }, { 0, PROBE_H } };
    r3d_corner_t v[4];
    for (int i = 0; i < 4; i++)
        v[i] = (r3d_corner_t){ q[i][0], q[i][1], 1.0f, q[i][0], q[i][1], 1.0f, { 0, 0, 0 }, { 0, 0, 0 } };
    const r3d_corner_t t1[3] = { v[0], v[1], v[2] }, t2[3] = { v[0], v[2], v[3] };
    add_tri(&pg, t1, R3D_KIND_TEXTURE, &pt, 0, SH_TEX, 0);
    add_tri(&pg, t2, R3D_KIND_TEXTURE, &pt, 0, SH_TEX, 0);
    if (gpu3d_flush(&pg, 0) != 0)
        return -1;
    c = G.probe[PROBE_W * 10 + 10];
    uint16_t same = G.red_a ? 0xF800 : 0x001F, other = G.red_a ? 0x001F : 0xF800;
    if (c == same)
        G.tex_swap = 0;
    else if (c == other)
        G.tex_swap = 1;
    else {
        ksnprintf(G.why, sizeof G.why, "probe: a texture gave %04x (expected f800 or 001f)", c);
        disable(G.why);
        return -1;
    }

    /* T-format, the TMU's tiled layout (a texture read in tiles fits its
     * cache better than one in rows): the same quad with a 64x64 RGBA8888
     * texture whose word i shows as the RGB565 value i tells, texel by
     * texel, which word the TMU reads: the layout of a 4 KiB tile (32x32
     * texels) in even and in odd rows of tiles, and the order of the
     * tiles. Textures go in T-format only if all of it holds together. */
    for (int i = 0; i < PROBE_W * PROBE_H; i++)
        pt.texels[i] = texel_of((uint16_t)i, 1);
    pt.params[1] &= ~(1u << 31);        /* type RGBA8888 */
    memset(G.probe, 0, JOB_PROBE);
    add_tri(&pg, t1, R3D_KIND_TEXTURE, &pt, 0, SH_TEX, 0);
    add_tri(&pg, t2, R3D_KIND_TEXTURE, &pt, 0, SH_TEX, 0);
    if (gpu3d_flush(&pg, 0) != 0)
        return -1;
    G.tformat = tformat_learn();
    G.ms_ok = probe_ms();
    G.gl_ok = probe_gl(&pg);
    G.clip_ok = G.gl_ok ? probe_clip(&pg) : 0;
    memset(&G.st, 0, sizeof G.st);
    return 0;
}

int gpu3d_init(void)
{
    if (G.ready || G.failed)
        return G.failed ? -1 : 0;
    G.status = "starting";
    if (v3d_init() != 0) {
        disable(v3d_status());
        return -1;
    }
    uint8_t *b = block_alloc();
    if (!b) {
        disable("no memory for the GPU");
        return -1;
    }
    G.block = b;
    G.tsda = b;
    G.alloc = G.tsda + JOB_TSDA;
    G.overflow = G.alloc + JOB_ALLOC;
    G.zbuf = G.overflow + JOB_OVERFLOW;
    G.bcl = G.zbuf + JOB_ZBUF;
    G.rcl = G.bcl + JOB_BCL;
    G.recs = G.rcl + JOB_RCL;
    G.unif = (uint32_t *)(G.recs + JOB_RECS);
    G.code = G.recs + JOB_RECS + JOB_UNIF;
    G.verts = G.code + JOB_CODE;
    G.probe = (uint16_t *)(G.verts + JOB_VERTS);
    G.probe_tex = (uint32_t *)(((uintptr_t)G.probe + JOB_PROBE + 4095) & ~(uintptr_t)4095);
    for (int i = 0; i < SH_COUNT; i++)
        memcpy(G.code + 1024 * i, shaders[i].code, shaders[i].size);
    memcpy(G.code + 1024 * CODE_CS, cs_colour, sizeof cs_colour);
    for (int i = 0; i < GV_COUNT; i++)
        memcpy(G.code + 1024 * vshaders[i].kb, vshaders[i].code, vshaders[i].size);
    G.backend.tri = cb_tri;
    G.backend.mesh = cb_mesh;
    G.backend.zclear = cb_zclear;
    G.backend.guard = GUARD * 0.9f;     /* r3d's bound, with room for its rounding */
    if (probe() != 0)
        return -1;
    static const char *const ms[3] = { "no", "on cleared pages", "on any page" };
    static const char *const clips[3] = { "no", "yes", "yes (Z planes)" };
    ksnprintf(G.why, sizeof G.why, "ready (byte a = %s, texels %s, textures %s, MSAA %s, vertex shader %s, "
              "clipping %s)", G.red_a ? "red" : "blue", G.tex_swap ? "swapped" : "in place",
              G.tformat ? "in tiles" : "in rows", ms[G.ms_ok], G.gl_ok ? (G.gl_cw ? "yes (cw)" : "yes") : "no",
              clips[G.clip_ok]);
    G.status = G.why;
    G.ready = 1;
    return 0;
}

const char *gpu3d_status(void)
{
    return G.status ? G.status : "not started";
}

int gpu3d_ready(void)
{
    return G.ready && !G.failed;
}

int gpu3d_failed(void)
{
    return G.failed;
}

const r3d_backend_t *gpu3d_backend(void)
{
    return &G.backend;
}
