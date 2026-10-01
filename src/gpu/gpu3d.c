#include "gpu3d.h"
#include "shaders.h"
#include "v3d.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

/* the memory of a job, in one block: the binner wants its own (tile
 * state, tile lists, overflow) within one 256 MiB window */
#define JOB_TSDA     4096           /* 48 bytes per tile: 10 x 6 tiles at most */
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
    int z_wanted;                   /* this cartridge draws 3D after 2D in a frame */
    /* the batch being filled */
    int b_open, b_shader, b_nodepth, b_first;
    const tex_t *b_tex;
    uint8_t *b_start, *b_len;       /* its first packet, its vertex count */
    int clip[4];                    /* clip window written last (x0 y0 x1 y1) */
    int cfg;                        /* configuration written last, -1: none */
    int red_a, tex_swap;            /* colour byte order, found by the probe */
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

/* the RGBA32R texture of a sheet, made again when the sheet changed; NULL
 * if it cannot be one (larger than 2048, no memory). A slot the waiting
 * job still reads is drawn first (into g) before it is made again. */
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
    size_t size = (size_t)s->w * (size_t)s->h * 4 + 16;
    if (!t->texels || t->size < size) {
        free(t->texels);
        t->texels = aligned_alloc(4096, (size + 4095) & ~(size_t)4095);
        t->size = t->texels ? size : 0;
        if (!t->texels) {
            t->sheet = NULL;
            return NULL;
        }
    }
    /* each texel as the bytes a b c d of the tile buffer: red and blue in
     * the order found by the probe, alpha in d */
    const int ra = G.red_a ^ G.tex_swap;
    const uint32_t n = (uint32_t)s->w * (uint32_t)s->h;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = s->px[i];
        uint32_t r = (c >> 11) << 3 | c >> 13, g = (c >> 5 & 63) << 2 | (c >> 9 & 3),
                 b = (c & 31) << 3 | (c >> 2 & 7);
        uint32_t a = s->alpha[i] ? 0xFF000000u : 0;
        t->texels[i] = a | (ra ? b << 16 | g << 8 | r : r << 16 | g << 8 | b);
    }
    /* P0: base (4 KiB units), type RGBA32R = 16 (low bits 0 here, bit 4
     * in P1); P1: type bit 4, height and width (2048 is 0), nearest texel
     * when magnified and minified, clamp in s and t */
    t->params = t->texels + n;
    t->params[0] = v3d_bus(t->texels) & ~0xFFFu;
    t->params[1] = 1u << 31 | (uint32_t)(s->h & 2047) << 20 | (uint32_t)(s->w & 2047) << 8 | 1u << 7 |
                   1u << 4 | 1u << 2 | 1u;
    t->sheet = s;
    t->version = s->version;
    t->w = s->w;
    t->h = s->h;
    t->inv_w = 1.0f / (float)s->w;
    t->inv_h = 1.0f / (float)s->h;
    return t;
}

/* the cells of the sheet a face can sample, one texel larger on each side,
 * are all opaque (big boxes are not checked: the alpha shader then) */
static int tex_opaque(const g16_sheet_t *s, const r3d_corner_t v[3])
{
    float u0 = v[0].a, u1 = u0, v0 = v[0].b, v1 = v0;
    for (int i = 1; i < 3; i++) {
        if (v[i].a < u0) u0 = v[i].a;
        if (v[i].a > u1) u1 = v[i].a;
        if (v[i].b < v0) v0 = v[i].b;
        if (v[i].b > v1) v1 = v[i].b;
    }
    const int cw = s->w / G16_CELL, ch = s->h / G16_CELL;
    int cx0 = u0 < 1 ? 0 : (int)((u0 - 1) / G16_CELL), cx1 = u1 + 1 < 0 ? 0 : (int)((u1 + 1) / G16_CELL);
    int cy0 = v0 < 1 ? 0 : (int)((v0 - 1) / G16_CELL), cy1 = v1 + 1 < 0 ? 0 : (int)((v1 + 1) / G16_CELL);
    if (cx1 >= cw) cx1 = cw - 1;
    if (cy1 >= ch) cy1 = ch - 1;
    if (cx0 > cx1 || cy0 > cy1 || (cx1 - cx0 + 1) * (cy1 - cy0 + 1) > 256)
        return 0;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            if (!s->cell_opaque[cy * cw + cx])
                return 0;
    return 1;
}

/* ---------------------------------------------------------------- the job */

static void job_begin(int w, int h)
{
    const int tx = (w + V3D_TILE - 1) / V3D_TILE, ty = (h + V3D_TILE - 1) / V3D_TILE;
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
    v3d_cl_u8(&G.cl, V3D_BIN_AUTO_INIT_TSDA | 0 << 3 | 2 << 5);
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
        v3d_cl_u8(&G.cl, V3D_CFG_FRONT | V3D_CFG_BACK);     /* r3d culled the back faces */
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
 * 1/w, the attributes (as r3d gives them) scaled to 0..1 */
static inline void put_corner(gvert_t *o, float x, float y, float iw, float a, float b, float c, int kind,
                              const tex_t *t)
{
    o->x = (int16_t)((int32_t)(x * 16.0f + 32768.5f) - 32768);
    o->y = (int16_t)((int32_t)(y * 16.0f + 32768.5f) - 32768);
    float z = 1.0f - R3D_NEAR * iw;     /* 0 at the near plane, towards 1 far away */
    o->z = z < 0 ? 0 : z;
    o->inv_w = iw;
    if (kind == R3D_KIND_COLOUR) {
        const float k = 1.0f / 255.0f;
        o->v[G.ia] = a * k;             /* red or blue in byte a, as the probe found */
        o->v[1] = b * k;
        o->v[2 - G.ia] = c * k;
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

/* a triangle into the job (clipped to the guard band if it reaches out of
 * the range of the 12.4 coordinates) */
static void add_tri(const g16_t *g, const r3d_corner_t v[3], int kind, const tex_t *t, int nodepth, int shader)
{
    if (G.failed)
        return;
    if (G.open && (g->w != G.w || g->h != G.h)) {
        batch_close();                  /* another screen size: the old job is dropped */
        G.open = 0;
        G.z_saved = 0;
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
            return;
        job_begin(g->w, g->h);
    }
    if (!G.b_open || G.b_shader != shader || G.b_nodepth != nodepth || G.b_tex != t ||
        G.clip[0] != g->cx0 || G.clip[1] != g->cy0 || G.clip[2] != g->cx1 || G.clip[3] != g->cy1 ||
        G.nverts - G.b_first + 21 > BATCH_MAX) {
        batch_close();
        if (batch_open(g, shader, nodepth, t) != 0) {
            flush_job(g, 1);
            if (G.failed)
                return;
            job_begin(g->w, g->h);
            batch_open(g, shader, nodepth, t);
        }
    }
    const float x0 = -GUARD, y0 = -GUARD, x1 = g->w + GUARD, y1 = g->h + GUARD;
    int out = 0;
    for (int i = 0; i < 3; i++)
        out |= v[i].x < x0 || v[i].x > x1 || v[i].y < y0 || v[i].y > y1;
    if (!out) {                         /* nearly every triangle: straight in */
        gvert_t *o = &G.verts[G.nverts];
        for (int i = 0; i < 3; i++)
            put_corner(&o[i], v[i].x, v[i].y, v[i].z, v[i].a, v[i].b, v[i].c, kind, t);
        G.nverts += 3;
        G.st.tris++;
        return;
    }
    cvert_t p[16], q[16];
    int n = 3;
    for (int i = 0; i < 3; i++)
        p[i] = cvert(&v[i]);
    {
        n = clip_line(p, n, q, 0, -1, -x0);
        n = clip_line(q, n, p, 0, 1, x1);
        n = clip_line(p, n, q, 1, -1, -y0);
        n = clip_line(q, n, p, 1, 1, y1);
        if (n < 3)
            return;
    }
    for (int i = 1; i + 1 < n; i++) {   /* a fan */
        put(&p[0], kind, t);
        put(&p[i], kind, t);
        put(&p[i + 1], kind, t);
    }
    G.st.tris += (uint32_t)(n - 2);
}

static void cb_tri(void *ctx, const g16_t *g, const r3d_corner_t v[3], int kind, const g16_sheet_t *tex,
                   int nodepth)
{
    (void)ctx;
    if (kind == R3D_KIND_TEXTURE) {
        const tex_t *t = tex ? tex_get(g, tex) : NULL;
        if (t) {
            add_tri(g, v, kind, t, nodepth, tex_opaque(tex, v) ? SH_TEX : SH_TEX_ALPHA);
            return;
        }
        /* no texture on the GPU (too large): grey times the light */
        r3d_corner_t c[3];
        for (int i = 0; i < 3; i++)
            c[i] = (r3d_corner_t){ v[i].x, v[i].y, v[i].z, 200 * v[i].c, 200 * v[i].c, 200 * v[i].c };
        add_tri(g, c, R3D_KIND_COLOUR, NULL, nodepth, SH_COLOUR);
        return;
    }
    add_tri(g, v, kind, NULL, nodepth, SH_COLOUR);
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
static uint32_t rcl_build(uint32_t fb, int w, int h, int bin, int load, uint32_t clear, int zload, int zstore)
{
    const int tx = (w + V3D_TILE - 1) / V3D_TILE, ty = (h + V3D_TILE - 1) / V3D_TILE;
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
    v3d_cl_u16(&cl, V3D_RENDER_BGR565);
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
    const int zload = G.z_saved && G.z_w == g->w && G.z_h == g->h;
    const int zstore = store && zbuf_fits(g->w, g->h);
    uint32_t fb = bus_of(g->px);
    uint32_t end = rcl_build(fb, g->w, g->h, 1, 1, 0, zload, zstore);
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
    if (run(0, rcl_build(v3d_bus(G.probe), PROBE_W, PROBE_H, 0, 0, 0x000000FFu, 0, 0)) != 0) {
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
    add_tri(&pg, t1, R3D_KIND_TEXTURE, &pt, 0, SH_TEX);
    add_tri(&pg, t2, R3D_KIND_TEXTURE, &pt, 0, SH_TEX);
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
    if (probe() != 0)
        return -1;
    ksnprintf(G.why, sizeof G.why, "ready (byte a = %s, texels %s)", G.red_a ? "red" : "blue",
              G.tex_swap ? "swapped" : "in place");
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
