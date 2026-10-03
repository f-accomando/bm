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
#define JOB_RECS     (16u << 10)    /* NV shader records, 16 bytes each */
#define JOB_CODE     (3u << 10)     /* the shaders, 1 KiB each */
#define JOB_VERTS    (4u << 20)
#define PROBE_W      64
#define PROBE_H      64
#define JOB_PROBE    (PROBE_W * PROBE_H * 2)
#define PROBE_TEX    (PROBE_W * PROBE_H * 4 + 4096)
#define JOB_BLOCK    (JOB_TSDA + JOB_ALLOC + JOB_OVERFLOW + JOB_ZBUF + JOB_BCL + JOB_RCL + JOB_RECS + JOB_CODE + \
                      JOB_VERTS + JOB_PROBE + PROBE_TEX)

#define TIMEOUT_US   200000
#define BATCH_MAX    65532          /* vertices of one VERTEX_ARRAY_PRIMITIVES */
#define GUARD        1000.0f        /* margin around the screen inside the 12.4 range */

/* a vertex as the NV shader state wants it: screen x and y in 12.4,
 * z (0 near .. 1 far), 1/w, three varyings (colour, or s t k) */
typedef struct {
    int16_t x, y;
    float z, inv_w;
    float v[3];
} gvert_t;

#define MAX_VERTS (int)(JOB_VERTS / sizeof(gvert_t))

enum { SH_COLOUR, SH_TEX, SH_TEX_ALPHA, SH_COUNT };

static const struct { const uint32_t *code; size_t size; uint8_t uniforms; } shaders[SH_COUNT] = {
    { fs_colour, sizeof fs_colour, 0 },
    { fs_tex_lit, sizeof fs_tex_lit, 2 },
    { fs_tex_lit_alpha, sizeof fs_tex_lit_alpha, 2 },
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

static struct {
    int ready, failed;
    const char *status;
    char why[128];
    uint8_t *block;
    uint8_t *tsda, *alloc, *overflow, *zbuf, *bcl, *rcl, *recs, *code;
    uint16_t *probe;
    uint32_t *probe_tex;
    gvert_t *verts;
    int nverts;
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
    int b_open, b_shader, b_nodepth, b_first;
    int b_room;                     /* vertices it can still start a triangle at (7 corners clipped) */
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
    G.clip[0] = G.clip[1] = G.clip[2] = G.clip[3] = -1;
    G.cfg = -1;
    G.nverts = 0;
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
    uint32_t n = (uint32_t)(G.nverts - G.b_first);
    if (!n) {                           /* nothing drawn: no packets */
        G.cl.p = G.b_start;
        return;
    }
    G.b_len[0] = (uint8_t)n;
    G.b_len[1] = (uint8_t)(n >> 8);
    G.b_len[2] = (uint8_t)(n >> 16);
    G.b_len[3] = (uint8_t)(n >> 24);
}

/* 0, or -1 if the job is full */
static int batch_open(const g16_t *g, int shader, int nodepth, const tex_t *t)
{
    if (G.rec_next + 16 > G.recs + JOB_RECS || G.cl.p + 64 > G.cl.end)
        return -1;
    G.b_start = G.cl.p;
    if (G.clip[0] != g->cx0 || G.clip[1] != g->cy0 || G.clip[2] != g->cx1 || G.clip[3] != g->cy1) {
        v3d_cl_u8(&G.cl, V3D_CLIP_WINDOW);
        v3d_cl_u16(&G.cl, (uint16_t)g->cx0);
        v3d_cl_u16(&G.cl, (uint16_t)g->cy0);
        v3d_cl_u16(&G.cl, (uint16_t)(g->cx1 - g->cx0));
        v3d_cl_u16(&G.cl, (uint16_t)(g->cy1 - g->cy0));
        G.clip[0] = g->cx0; G.clip[1] = g->cy0; G.clip[2] = g->cx1; G.clip[3] = g->cy1;
    }
    /* depth: nearer wins (less than, as the software's strict test), early
     * z except where texels may be thrown away; NOZ: neither test nor write */
    uint16_t cfg = nodepth ? V3D_CFG_DEPTH(7)
                 : shader == SH_TEX_ALPHA ? V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE
                 : V3D_CFG_DEPTH(1) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE;
    if (G.cfg != cfg) {
        v3d_cl_u8(&G.cl, V3D_CONFIGURATION_BITS);
        /* r3d culled the back faces; MSAA: the rasteriser takes 4 samples */
        v3d_cl_u8(&G.cl, (uint8_t)(V3D_CFG_FRONT | V3D_CFG_BACK | (G.ms ? V3D_CFG_MSAA4 : 0)));
        v3d_cl_u16(&G.cl, cfg);
        G.cfg = cfg;
    }
    /* NV shader record: flags, vertex stride, uniforms, varyings, code,
     * uniforms, vertices (those of this batch) */
    uint8_t *r = G.rec_next;
    G.rec_next += 16;
    r[0] = 0;
    r[1] = sizeof(gvert_t);
    r[2] = shaders[shader].uniforms;
    r[3] = 3;
    uint32_t a = v3d_bus(G.code + 1024 * shader);
    memcpy(r + 4, &a, 4);
    a = t ? v3d_bus(t->params) : 0;
    memcpy(r + 8, &a, 4);
    a = v3d_bus(&G.verts[G.nverts]);
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
    G.b_nodepth = nodepth;
    G.b_tex = t;
    G.b_first = G.nverts;
    G.b_room = (MAX_VERTS < G.nverts + BATCH_MAX ? MAX_VERTS : G.nverts + BATCH_MAX) - 21;
    if (t >= G.tex && t < G.tex + NTEX)
        G.tex_used[t - G.tex] = 1;
    return 0;
}

/* ---------------------------------------------------------------- vertices */

/* a corner while clipping: attributes multiplied by 1/w (they are linear
 * in screen space that way) */
typedef struct { float x, y, iw, a, b, c; } cvert_t;

static cvert_t cvert(const r3d_corner_t *v)
{
    float iw = v->z > 1e-9f ? v->z : 1e-9f;
    return (cvert_t){ v->x, v->y, iw, v->a * iw, v->b * iw, v->c * iw };
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
            out[m++] = (cvert_t){ p->x + (q->x - p->x) * t, p->y + (q->y - p->y) * t,
                                  p->iw + (q->iw - p->iw) * t, p->a + (q->a - p->a) * t,
                                  p->b + (q->b - p->b) * t, p->c + (q->c - p->c) * t };
        }
    }
    return m;
}

/* a corner as the shader wants it: x and y rounded to 12.4 (within the
 * guard band, so x * 16 + 32768 is positive and the cast floors), z from
 * 1/w, the colour as r3d gives it (0..1), texel coordinates scaled to 0..1 */
static inline void put_corner(gvert_t *o, float x, float y, float iw, float a, float b, float c, int kind,
                              const tex_t *t)
{
    o->x = (int16_t)((int32_t)(x * 16.0f + 32768.5f) - 32768);
    o->y = (int16_t)((int32_t)(y * 16.0f + 32768.5f) - 32768);
    float z = 1.0f - R3D_NEAR * iw;     /* 0 at the near plane, towards 1 far away */
    o->z = z < 0 ? 0 : z;
    o->inv_w = iw;
    if (kind == R3D_KIND_COLOUR) {
        o->v[G.ia] = a;                 /* red or blue in byte a, as the probe found */
        o->v[1] = b;
        o->v[2 - G.ia] = c;
    } else {
        o->v[0] = a * t->inv_w;
        o->v[1] = b * t->inv_h;
        o->v[2] = c < 0 ? 0 : c > 1 ? 1 : c;
    }
}

/* a corner made while clipping (attributes times 1/w) */
static void put(const cvert_t *c, int kind, const tex_t *t)
{
    const float w = 1.0f / c->iw;
    put_corner(&G.verts[G.nverts++], c->x, c->y, c->iw, c->a * w, c->b * w, c->c * w, kind, t);
}

/* the batch being filled takes a triangle of this state (same screen,
 * clip window, shader, depth and texture, and room for 7 corners) */
static inline int batch_takes(const g16_t *g, int shader, int nodepth, const tex_t *t)
{
    return G.b_open && G.nverts <= G.b_room && G.b_shader == shader && G.b_tex == t && G.b_nodepth == nodepth &&
           G.w == g->w && G.h == g->h && G.clip[0] == g->cx0 && G.clip[1] == g->cy0 && G.clip[2] == g->cx1 &&
           G.clip[3] == g->cy1;
}

/* a job and a batch open for this state, with room: 0, or -1 if the GPU
 * failed */
static int batch_for(const g16_t *g, int shader, int nodepth, const tex_t *t)
{
    if (G.failed)
        return -1;
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
    if (G.nverts + 21 > MAX_VERTS) {    /* a clipped triangle is up to 7 of them */
        flush_job(g, 1);                /* the next job goes on with this depth */
        if (G.failed)
            return -1;
        job_begin(g->w, g->h);
    }
    if (!batch_takes(g, shader, nodepth, t)) {
        batch_close();
        if (batch_open(g, shader, nodepth, t) != 0) {
            flush_job(g, 1);
            if (G.failed)
                return -1;
            job_begin(g->w, g->h);
            batch_open(g, shader, nodepth, t);
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
 * the range of the 12.4 coordinates; inside: r3d found it cannot) */
static inline void add_tri(const g16_t *g, const r3d_corner_t v[3], int kind, const tex_t *t, int nodepth,
                           int shader, int inside)
{
    if (!batch_takes(g, shader, nodepth, t) && batch_for(g, shader, nodepth, t) != 0)
        return;
    if (!inside) {
        const float x0 = -GUARD, y0 = -GUARD, x1 = g->w + GUARD, y1 = g->h + GUARD;
        int out = 0;
        for (int i = 0; i < 3; i++)
            out |= v[i].x < x0 || v[i].x > x1 || v[i].y < y0 || v[i].y > y1;
        if (out) {
            add_clipped(v, kind, t, x1, y1);
            return;
        }
    }
    gvert_t *o = &G.verts[G.nverts];    /* nearly every triangle: straight in */
    for (int i = 0; i < 3; i++)
        put_corner(&o[i], v[i].x, v[i].y, v[i].z, v[i].a, v[i].b, v[i].c, kind, t);
    G.nverts += 3;
    G.st.tris++;
}

static void cb_tri(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind, const g16_sheet_t *tex,
                   int nodepth)
{
    (void)ctx;
    const int inside = kind & R3D_INSIDE;
    if ((kind & ~R3D_INSIDE) == R3D_KIND_TEXTURE) {
        const tex_t *t = tex ? tex_get(g, tex) : NULL;
        if (t) {
            add_tri(g, v, R3D_KIND_TEXTURE, t, nodepth, tex_opaque(t, v) ? SH_TEX : SH_TEX_ALPHA, inside);
            return;
        }
        /* no texture on the GPU (too large): grey times the light */
        r3d_corner_t c[3];
        for (int i = 0; i < 3; i++)
            c[i] = (r3d_corner_t){ v[i].x, v[i].y, v[i].z, 200 * v[i].c * (1.0f / 255.0f),
                                   200 * v[i].c * (1.0f / 255.0f), 200 * v[i].c * (1.0f / 255.0f) };
        add_tri(g, c, R3D_KIND_COLOUR, NULL, nodepth, SH_COLOUR, inside);
        return;
    }
    add_tri(g, v, R3D_KIND_COLOUR, NULL, nodepth, SH_COLOUR, inside);
}

static void cb_zclear(void *ctx, const g16_t *g)
{
    (void)ctx;
    /* what is drawn next must not see the depth of what was drawn so far:
     * that goes to the screen now, and the next job starts with a clear
     * depth buffer */
    if (G.open && G.nverts)
        gpu3d_flush(g, 0);
    G.z_saved = G.split = 0;
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
    return G.open && (G.nverts || G.b_open);
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

void gpu3d_tiled_textures(int on)
{
    G.tformat_off = !on;
    for (int i = 0; i < NTEX; i++)
        G.tex[i].sheet = NULL;          /* made again in the other layout */
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
    if (!G.nverts)
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
    const int had = G.open && G.nverts;
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
        v[i] = (r3d_corner_t){ q[i][0], q[i][1], 1.0f, q[i][0], q[i][1], 1.0f };
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
    G.code = G.recs + JOB_RECS;
    G.verts = (gvert_t *)(G.code + JOB_CODE);
    G.probe = (uint16_t *)((uint8_t *)G.verts + JOB_VERTS);
    G.probe_tex = (uint32_t *)(((uintptr_t)G.probe + JOB_PROBE + 4095) & ~(uintptr_t)4095);
    for (int i = 0; i < SH_COUNT; i++)
        memcpy(G.code + 1024 * i, shaders[i].code, shaders[i].size);
    G.backend.tri = cb_tri;
    G.backend.zclear = cb_zclear;
    G.backend.guard = GUARD * 0.9f;     /* r3d's bound, with room for its rounding */
    if (probe() != 0)
        return -1;
    static const char *const ms[3] = { "no", "on cleared pages", "on any page" };
    ksnprintf(G.why, sizeof G.why, "ready (byte a = %s, texels %s, textures %s, MSAA %s)",
              G.red_a ? "red" : "blue", G.tex_swap ? "swapped" : "in place", G.tformat ? "in tiles" : "in rows",
              ms[G.ms_ok]);
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
