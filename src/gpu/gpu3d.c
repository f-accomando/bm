#include "gpu3d.h"
#include "shaders.h"
#include "version3d.h"
#include "v3d.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

/* the memory of a job, in one block: the binner wants its own (tile
 * state, tile lists, overflow) within one 256 MiB window */
#define JOB_TSDA     (96u << 10)    /* 48 bytes per tile: 60 x 34 tiles with MSAA (1920x1080) */
#define JOB_ALLOC    (4u << 20)     /* tile lists */
#define JOB_OVERFLOW (2u << 20)
#define JOB_ZBUF     (8u << 20)     /* depth kept between jobs: up to 1920x1088, 32 bits */
#define JOB_BCL      (256u << 10)
#define JOB_RCL      (128u << 10)   /* up to 44 bytes a tile: 2040 tiles (1080p with MSAA) */
#define JOB_RECS     (64u << 10)    /* shader records: NV 16 bytes, GL 64 */
#define JOB_UNIF     (256u << 10)   /* uniforms of the vertex shaders (a hero: one block a bone) */
#define JOB_CODE     (32u << 10)    /* the shaders: 1 KiB each, 2 KiB for a vertex shader */
#define JOB_VERTS    (4u << 20)
#define PROBE_W      64
#define PROBE_H      64
#define JOB_PROBE    (PROBE_W * PROBE_H * 2)
#define PROBE_TEX    (PROBE_W * PROBE_H * 4 + 4096)
#define JOB_BLOCK    (JOB_TSDA + JOB_ALLOC + JOB_OVERFLOW + JOB_ZBUF + JOB_BCL + JOB_RCL + JOB_RECS + JOB_UNIF + JOB_CODE + \
                      JOB_VERTS + JOB_PROBE + PROBE_TEX)
/* the block in whole sections of 1 MiB, which the MMU can map uncached
 * (gpu3d_set_wc, M35) without touching any other memory */
#define JOB_SECTIONS ((JOB_BLOCK + 0xFFFFFu) & ~0xFFFFFu)

#define TIMEOUT_US   200000
#define BATCH_MAX    65532          /* vertices of one VERTEX_ARRAY_PRIMITIVES */
#define GUARD        1000.0f        /* margin around the screen inside the 12.4 range, */
#define GUARD_END    2040           /* less on wide screens: the corners stay under 2048 */

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

enum { SH_COLOUR, SH_TEX, SH_TEX_ALPHA, SH_SCREEN, SH_TEX_RGB, SH_TEX_RGB_ALPHA, SH_ZCLEAR, SH_TEX_SCREEN,
       SH_TEX_RGB_SCREEN, SH_TEX2D, SH_TEXT, SH_COUNT };
#define DEPTH_ZCLEAR 3                  /* after R3D_DEPTH_*: always passes, writes (fs_zclear) */
/* the vertex shaders of meshes (M36); GV_LIT_TEX2: faces on two bones (a skin) */
enum { GV_BAKED, GV_TEX_RGB, GV_LIT, GV_LIT_TEX, GV_LIT_TEX2, GV_COUNT };
#define CODE_CS SH_COUNT                /* the coordinate shader's kilobyte of G.code */
#define CODE_VS (CODE_CS + 1)           /* and the vertex shaders' (two each) */
#define CODE_SHADOW (CODE_VS + 2 * GV_COUNT)    /* the shadows' vertex shader (2), coordinate shader (1) */
#define CODE_CS2 (CODE_SHADOW + 3)      /* on two bones: the coordinate shader, the shadow's two */
#define CODE_SHADOW2 (CODE_CS2 + 1)
#define TWO_BONES(vs) ((vs) == GV_LIT_TEX2)
_Static_assert(sizeof vs_baked <= 2048 && sizeof vs_tex_rgb <= 2048 && sizeof vs_lit <= 2048 &&
               sizeof vs_lit_tex <= 2048 && sizeof vs_lit_tex2 <= 2048 && sizeof vs_shadow <= 2048 &&
               sizeof cs_colour <= 1024 && sizeof cs_shadow <= 1024 && sizeof cs_colour2 <= 1024 &&
               sizeof vs_shadow2 <= 1024 && sizeof cs_shadow2 <= 1024 &&
               CODE_SHADOW2 + 2 <= JOB_CODE / 1024, "shader code slots");

/* discard: the shader may write no pixel (early z off); near: the texture
 * as the nearest texel, whatever gpu3d_set_bilinear says (2D, M37) */
static const struct { const uint32_t *code; size_t size; uint8_t uniforms, varyings, discard, near; } shaders[SH_COUNT] = {
    { fs_colour, sizeof fs_colour, 0, 3, 0, 0 },
    { fs_tex_lit, sizeof fs_tex_lit, 2, 3, 0, 0 },
    { fs_tex_lit_alpha, sizeof fs_tex_lit_alpha, 2, 3, 1, 0 },
    { fs_colour_screen, sizeof fs_colour_screen, 0, 3, 1, 0 },
    { fs_tex_rgb, sizeof fs_tex_rgb, 2, 8, 0, 0 },
    { fs_tex_rgb_alpha, sizeof fs_tex_rgb_alpha, 2, 8, 1, 0 },
    { fs_zclear, sizeof fs_zclear, 0, 0, 0, 0 },
    { fs_tex_lit_screen, sizeof fs_tex_lit_screen, 2, 3, 1, 0 },   /* M34: textured screen-door */
    { fs_tex_rgb_screen, sizeof fs_tex_rgb_screen, 2, 8, 1, 0 },
    { fs_tex_lit_alpha, sizeof fs_tex_lit_alpha, 2, 3, 1, 1 },  /* M37: sprites (k = 1) */
    { fs_text, sizeof fs_text, 2, 5, 1, 1 },                    /* M37: text */
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
    uint32_t used;                  /* when a job last took it (G.tick), for the least recent */
} tex_t;

/* M35: up to 8 sheets in a job (bm3d 4.6; 2 before: a third ended the
 * job, the 3D Bench's texswap ran slower on the GPU than on the ARM) */
#define NTEX 8

/* M36: a mesh whose vertices the V3D places. Its corners (3 a face) in
 * memory the V3D reads, made again when the mesh changes (its version):
 * x y z, the face's normal, its colour (0..1, in the order of the colour
 * varyings). */
#define NMESH 256                   /* a map's pieces in view, and what moves on them */
#define MESH_HINTS 512
#define MESH_GROUPS 255
typedef struct {
    uint8_t vs, fs, lamps, lods;        /* shaders, lamps on, levels of detail that show it */
    uint8_t bone, bone2;                /* its matrix (the faces of a group are on one bone; with
                                         * GV_LIT_TEX2 each corner on bone or bone2) */
    uint32_t first, n;                  /* its first byte in the corners, its corners */
} ggroup_t;
typedef struct {
    const r3d_mesh_t *m;
    uint32_t version;
    int unlit, smooth;                  /* as drawn: no light; the vertices' normals (GV_LIT) */
    uint8_t *corners;                   /* NULL: not a mesh the GPU takes */
    ggroup_t *g;                        /* its groups, sorted by bone */
    int ngroups;
    const tex_t *tex;                   /* its texture (textured faces), at version tex_version */
    uint32_t tex_version;
    uint32_t job, used;                 /* the job that drew it last; when, for the least recent */
} gmesh_t;

static struct {
    int ready, failed;
    const char *status;
    char why[192];
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
    int cleared;                    /* a job cleared the page to it (gpu3d_cleared) */
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
    int bilinear;                   /* M37: textures filtered (gpu3d_set_bilinear) */
    int ms_ok;                      /* the probe: 0 no MSAA, 1 on cleared pages, 2 also on loaded ones */
    int ms, tile;                   /* the job: MSAA, tile size */
    int t_rev;                      /* odd rows of 4 KiB tiles run right to left */
    uint16_t t_inner[2][1024];      /* word of each texel in its tile: even, odd rows of tiles */
    int ia;                         /* varying of the colour's first value: 0, or 2 (red_a 0) */
    const uint8_t *fb_mem;          /* the framebuffer and its bus address */
    uint32_t fb_size, fb_bus;
    tex_t tex[NTEX];
    int tex_used[NTEX];             /* by the job being filled */
    uint32_t *unif, *unif_next;     /* uniforms of the vertex shaders, the job's next */
    uint32_t gldraws;               /* meshes the job draws with the vertex shader */
    uint32_t job_no, tick;
    gmesh_t gm[NMESH];
    uint16_t hint[MESH_HINTS];      /* the slot a mesh had last, by its address */
    int gl_ok;                      /* the probe drew with the vertex shader */
    int gl_cw;                      /* CONFIGURATION_BITS clockwise bit for r3d's front faces */
    int gl_on;                      /* asked for (gpu3d_set_vshader): 1 meshes unlit or with baked
                                     * light, 2 also the models lit by the sun (heroes) */
    int clip_ok;                    /* the probe saw the GPU clip: 1, 2 with Z_MIN_MAX_CLIPPING_PLANES */
    int lit_ok;                     /* the probe saw vs_lit light as r3d (models lit by the sun) */
    int queue_ok;                   /* M35: the probe saw a started job end right (semaphores) */
    int queue_on;                   /* asked for (gpu3d_set_queue) */
    int inflight;                   /* a job started (v3d_start), not waited for yet */
    int zclear_ok;                  /* M35: the probe saw zclear() inside a job work (fs_zclear) */
    int zc;                         /* a zclear() quad in the open job: no early z after it */
    int wc, wc_on;                  /* M35: the block uncached (writes merged); asked for */
    int vpm_bytes;                  /* the GL records' VPM offsets and sizes in bytes (as Mesa), else in
                                     * words: the probe learns it (bm3d 4.3) */
    char plog[640];                 /* what the probes saw (the GPU test's report) */
    int async_now;                  /* flush_job starts the job instead of running it */
    uint32_t vp;                    /* the job's VIEWPORT_OFFSET (x, y in 12.4) */
    int scr_w, scr_h;               /* the screen (gpu3d_set_size) */
    float gx, gy;                   /* its guard band (pixels left and right, above and below) */
    int32_t glo_x, glo_y;           /* where it starts in 12.4 plus 32768 */
    float clipper[2];               /* the job's CLIPPER_XY_SCALING (0: not written yet) */
    gpu3d_stats_t st;
    r3d_backend_t backend;
} G;

static int flush_job(const g16_t *g, int store);
static int job_wait(void);

static void disable(const char *why)
{
    G.failed = 1;
    G.b_open = 0;                       /* no triangle goes the fast way into a dead job */
    ksnprintf(G.why, sizeof G.why, "%s", why);
    G.status = G.why;
    kprintf("gpu3d: %s; the 3D is drawn by the ARM again\n", why);
}

/* a line of what the probes saw ("gpu3d: " and line, to the log), kept
 * for gpu3d_probe_log() (the GPU test writes it in its report) */
static void plog(const char *line)
{
    kprintf("gpu3d: %s\n", line);
    const size_t n = strlen(G.plog);
    if (n + 2 < sizeof G.plog)
        ksnprintf(G.plog + n, sizeof G.plog - n, "%s%s", n ? "; " : "", line);
}

const char *gpu3d_probe_log(void)
{
    return G.plog;
}

/* the guard band of a job of w x h: GUARD, or what keeps the corners
 * (absolute, 12.4) within 2048 pixels */
static void guard_of(int w, int h)
{
    G.gx = GUARD_END - w < GUARD ? (float)(GUARD_END - w) : GUARD;
    G.gy = GUARD_END - h < GUARD ? (float)(GUARD_END - h) : GUARD;
    G.glo_x = (int32_t)(32768 - 16 * G.gx);
    G.glo_y = (int32_t)(32768 - 16 * G.gy);
}

/* the screen's: also r3d's bound for the triangles it sends unchecked,
 * the smaller margin with room for its rounding */
static void set_guard(int w, int h)
{
    G.scr_w = w;
    G.scr_h = h;
    guard_of(w, h);
    G.backend.guard = (G.gx < G.gy ? G.gx : G.gy) * 0.9f;
}

void gpu3d_set_size(int w, int h)
{
    set_guard(w, h);
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
        if (G.tex[i].sheet == s && G.tex[i].version == s->version && G.tex[i].texels) {
            G.tex[i].used = ++G.tick;
            return &G.tex[i];
        }
    if (s->w > 2048 || s->h > 2048 || s->w < 1 || s->h < 1)
        return NULL;
    int slot = -1;
    for (int i = 0; i < NTEX; i++)
        if (G.tex[i].sheet == s)
            slot = i;                       /* the same sheet, changed */
    for (int i = 0; slot < 0 && i < NTEX; i++)
        if (!G.tex[i].sheet)
            slot = i;                       /* a free one */
    if (slot < 0) {
        /* the least recently used, one the open job does not read if any */
        for (int i = 0; i < NTEX; i++) {
            const int busy = G.tex_used[i] && G.open, old_busy = slot >= 0 && G.tex_used[slot] && G.open;
            if (slot < 0 || (old_busy && !busy) || (busy == old_busy && G.tex[i].used < G.tex[slot].used))
                slot = i;
        }
    }
    if (G.tex_used[slot] && gpu3d_pending() && flush_job(g, 1) != 0)
        return NULL;
    if (job_wait() != 0)                /* a job started last may read it (M35) */
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
                   (G.bilinear ? 0 : 1u << 7 | 1u << 4) | 1u << 2 | 1u;     /* M37: linear (0) or nearest */
    t->params[2] = t->params[0];        /* the same, always the nearest texel: 2D (M37) */
    t->params[3] = t->params[1] | 1u << 7 | 1u << 4;
    t->sheet = s;
    t->version = s->version;
    t->used = ++G.tick;
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
    job_wait();                         /* the job started last reads the same memory */
    guard_of(w, h);
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
    G.zc = 0;
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
    /* early z: never after a zclear() quad in the job (its depth, written
     * farther by the shader, is not the early test's: what comes after
     * would be thrown away, as the Pi showed on 2026-10-05), never with
     * MSAA (Mesa, HW-2905: after a load the early z tracking may hold the
     * previous tile's values) */
    if (G.zc || G.ms)
        cfg &= (uint16_t)~(V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE);
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
                 : depth == DEPTH_ZCLEAR ? V3D_CFG_DEPTH(7) | V3D_CFG_Z_UPDATE
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
    a = t ? v3d_bus(t->params + (shaders[shader].near ? 2 : 0)) : 0;
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
    return check ? ((uint32_t)(ix - G.glo_x) > sx) | ((uint32_t)(iy - G.glo_y) > sy) : 0;
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
    n = clip_line(p, n, q, 0, -1, G.gx);
    n = clip_line(q, n, p, 0, 1, x1);
    n = clip_line(p, n, q, 1, -1, G.gy);
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
        const uint32_t sx = (uint32_t)(16 * (g->w + 2 * (int)G.gx)), sy = (uint32_t)(16 * (g->h + 2 * (int)G.gy));
        uint32_t out = 0;
        for (int i = 0; i < 3; i++, o += G.b_stride)
            out |= put_corner((gvert_t *)o, v[i].x, v[i].y, v[i].z, v[i].a, v[i].b, v[i].c, v[i].l, v[i].f, kind,
                              t, 1, sx, sy);
        if (out) {
            add_clipped(v, kind, t, g->w + G.gx, g->h + G.gy);     /* over the corners just written */
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
    const int inside = kind & R3D_INSIDE, screen = kind & R3D_TEX_SCREEN;
    kind &= ~(R3D_INSIDE | R3D_TEX_SCREEN);
    if (kind == R3D_KIND_TEXTURE || kind == R3D_KIND_TEX_RGB) {
        const tex_t *t = tex ? tex_get(g, tex) : NULL;
        if (t) {
            const int opaque = tex_opaque(t, v);
            if (kind == R3D_KIND_TEXTURE)
                add_tri(g, v, kind, t, depth, screen ? SH_TEX_SCREEN : opaque ? SH_TEX : SH_TEX_ALPHA, inside);
            else
                add_tri(g, v, kind, t, depth, screen ? SH_TEX_RGB_SCREEN : opaque ? SH_TEX_RGB : SH_TEX_RGB_ALPHA,
                        inside);
            return;
        }
        /* no texture on the GPU (too large): grey times the light */
        r3d_corner_t c[3];
        for (int i = 0; i < 3; i++) {
            const float k = kind == R3D_KIND_TEXTURE ? v[i].c : (v[i].l[0] + v[i].l[1] + v[i].l[2]) * (2.0f / 3.0f);
            c[i] = (r3d_corner_t){ v[i].x, v[i].y, v[i].z, 200 * k * (1.0f / 255.0f), 200 * k * (1.0f / 255.0f),
                                   200 * k * (1.0f / 255.0f), { 0, 0, 0 }, { 0, 0, 0 } };
        }
        add_tri(g, c, screen ? R3D_KIND_SCREEN : R3D_KIND_COLOUR, NULL, depth, screen ? SH_SCREEN : SH_COLOUR,
                inside);
        return;
    }
    if (kind == R3D_KIND_SCREEN)
        add_tri(g, v, kind, NULL, depth, SH_SCREEN, inside);
    else
        add_tri(g, v, R3D_KIND_COLOUR, NULL, depth, SH_COLOUR, inside);
}

/* zclear() inside the job (M35): a quad over the whole page that writes
 * the far depth and gives each pixel its colour back (fs_zclear); 0, or
 * -1 if the GPU failed. Its depth a hair under 1 (24 bits: 0xFFFFF0), never
 * at the end of the range where a conversion might wrap: nothing r3d draws
 * is that far (1 - NEAR / depth beyond a million units) */
#define ZCLEAR_Z (1.0f - 1.0f / 1048576.0f)
static int zclear_quad(const g16_t *g)
{
    g16_t all = *g;                     /* the whole depth, whatever the clip rectangle */
    all.cx0 = all.cy0 = 0;
    all.cx1 = g->w;
    all.cy1 = g->h;
    if (!batch_takes(&all, SH_ZCLEAR, DEPTH_ZCLEAR, NULL) && batch_for(&all, SH_ZCLEAR, DEPTH_ZCLEAR, NULL) != 0)
        return -1;
    const int16_t x1 = (int16_t)(g->w * 16), y1 = (int16_t)(g->h * 16);
    const int16_t xy[6][2] = { { 0, 0 }, { x1, 0 }, { x1, y1 }, { 0, 0 }, { x1, y1 }, { 0, y1 } };
    uint8_t *o = G.verts + G.vbytes;
    for (int i = 0; i < 6; i++, o += G.b_stride) {
        gvert_t *v = (gvert_t *)o;
        v->x = xy[i][0];
        v->y = xy[i][1];
        v->z = ZCLEAR_Z;
        v->inv_w = 1.0f;
    }
    G.vbytes += 6 * G.b_stride;
    G.st.zinjob++;
    G.zc = 1;                           /* the batches after it: without early z */
    return 0;
}

static void cb_zclear(void *ctx, const g16_t *g)
{
    (void)ctx;
    /* what is drawn next must not see the depth of what was drawn so far.
     * With the frame queue (M35) and where the probe saw it work, a quad
     * in the same job clears it (a game with first-person arms: one job a
     * frame); else what was drawn goes to the screen now, and the next
     * job starts with a clear depth buffer */
    if (G.open && (G.vbytes || G.gldraws)) {
        if (G.queue_on && G.zclear_ok && !G.ms && g->w == G.w && g->h == G.h && zclear_quad(g) == 0)
            return;                     /* (the depth loaded at the job's start still loaded) */
        gpu3d_flush(g, 0);
    }
    G.z_saved = G.split = 0;
}

/* ---------------------------------------------------------------- meshes (M36) */

/* the vertex shaders of meshes, their words a corner (x y z first) */
static const struct { const uint32_t *code; size_t size; uint8_t words, kb; } vshaders[GV_COUNT] = {
    { vs_baked, sizeof vs_baked, 9, CODE_VS },          /* colour, light */
    { vs_tex_rgb, sizeof vs_tex_rgb, 8, CODE_VS + 2 },  /* s t, light */
    { vs_lit, sizeof vs_lit, 14, CODE_VS + 4 },         /* normal, colour, emissive, glossy, lamp point */
    { vs_lit_tex, sizeof vs_lit_tex, 12, CODE_VS + 6 }, /* normal, s t, emissive, lamp point */
    { vs_lit_tex2, sizeof vs_lit_tex2, 11, CODE_VS + 8 },   /* w, normal, s t, emissive, normal's w */
};

/* the groups of a face: its bones (two for a face of a skin, else the
 * same twice), vertex shader, fragment shader, lamps (0: an emissive face
 * of a "lit" model) and the levels of detail that show it; in the order
 * of the bones */
static uint32_t group_key(int bone, int bone2, int vs, int fs, int lamps, int lods)
{
    return (uint32_t)bone << 24 | (uint32_t)bone2 << 16 | (uint32_t)vs << 12 | (uint32_t)fs << 8 |
           (uint32_t)lamps << 4 | (uint32_t)lods;
}

/* corner k of face f in group key of mesh m into o (smooth: GV_LIT with
 * the vertices' normals) */
static void put_mesh_corner(float *o, const r3d_mesh_t *m, int f, int k, int vs, int emissive, const tex_t *t,
                            int smooth, int bone)
{
    const int vi = m->faces[f * 3 + k];
    const v3_t v = m->verts[vi];
    o[0] = v.x; o[1] = v.y; o[2] = v.z;
    if (vs == GV_LIT_TEX2) {
        /* a corner of a skin: its weight (1 on the group's first bone, 0 on
         * the other), as GV_LIT_TEX without the lamps' point (the corner);
         * a flat face's normal turns with its first corner's bone, as r3d */
        const uint32_t c = m->colors[f];
        const int sm = smooth && !(c & R3D_FLAT);
        const v3_t n = sm ? m->vnormals[vi] : m->normals[f];
        o[3] = m->vbone[vi] == bone ? 1.0f : 0.0f;
        o[4] = n.x; o[5] = n.y; o[6] = n.z;
        o[7] = m->uv[f * 6 + k * 2] * t->inv_w;
        o[8] = m->uv[f * 6 + k * 2 + 1] * t->inv_h;
        o[9] = (float)((c & R3D_EMISSIVE) != 0);
        o[10] = sm ? o[3] : m->vbone[m->faces[f * 3]] == bone ? 1.0f : 0.0f;
        return;
    }
    if (vs == GV_LIT_TEX) {
        /* a textured face lit by the sun: as GV_LIT, s t in place of the colour, no gloss */
        const uint32_t c = m->colors[f];
        const int sm = smooth && !(c & R3D_FLAT);
        const v3_t n = sm ? m->vnormals[vi] : m->normals[f];
        o[3] = n.x; o[4] = n.y; o[5] = n.z;
        o[6] = m->uv[f * 6 + k * 2] * t->inv_w;
        o[7] = m->uv[f * 6 + k * 2 + 1] * t->inv_h;
        o[8] = (float)((c & R3D_EMISSIVE) != 0);
        if (sm) {
            o[9] = v.x; o[10] = v.y; o[11] = v.z;
        } else {
            const uint16_t *fc = m->faces + f * 3;
            const v3_t a = m->verts[fc[0]], b = m->verts[fc[1]], d = m->verts[fc[2]];
            o[9] = (a.x + b.x + d.x) * (1.0f / 3.0f);
            o[10] = (a.y + b.y + d.y) * (1.0f / 3.0f);
            o[11] = (a.z + b.z + d.z) * (1.0f / 3.0f);
        }
        return;
    }
    if (vs == GV_LIT) {
        const uint32_t c = m->colors[f];
        const v3_t n = smooth && !(c & R3D_FLAT) ? m->vnormals[vi] : m->normals[f];
        const int em = (c & R3D_EMISSIVE) != 0;
        o[3] = n.x; o[4] = n.y; o[5] = n.z;
        o[6 + G.ia] = (float)(c >> 16 & 255) * (1.0f / 255.0f);
        o[7] = (float)(c >> 8 & 255) * (1.0f / 255.0f);
        o[8 - G.ia] = (float)(c & 255) * (1.0f / 255.0f);
        o[9] = (float)em;
        o[10] = (float)((c & R3D_GLOSSY) && !em);
        if (smooth && !(c & R3D_FLAT)) {    /* the lamps and the fog: at the corner, or the face's middle */
            o[11] = v.x; o[12] = v.y; o[13] = v.z;
        } else {
            const uint16_t *fc = m->faces + f * 3;
            const v3_t a = m->verts[fc[0]], b = m->verts[fc[1]], d = m->verts[fc[2]];
            o[11] = (a.x + b.x + d.x) * (1.0f / 3.0f);
            o[12] = (a.y + b.y + d.y) * (1.0f / 3.0f);
            o[13] = (a.z + b.z + d.z) * (1.0f / 3.0f);
        }
        return;
    }
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
 * faces of a colour (lit by the light baked at their corners, by the sun
 * and the sky, or not at all) and textured faces (with baked light, or lit
 * by the sun); with a skeleton, the corners of a face on one bone; no
 * textured screen-door; unlit: every face at full light */
static int mesh_build(const g16_t *g, gmesh_t *e, const r3d_mesh_t *m, int unlit, int smooth)
{
    const int skinned = m->bones && m->nbones > 0, lit = !unlit && !m->clight;
    if (m->nfaces <= 0 || m->nfaces > 65535 / 3 || (m->bones && !skinned) || m->nbones > 255 ||
        (skinned && !m->vbone) || (lit && !m->normals) || (smooth && !m->vnormals))
        return 0;
    const tex_t *t = NULL;
    static uint32_t key[MESH_GROUPS];
    int nkeys = 0;
    static uint8_t gidx[65535 / 3];
    for (int f = 0; f < m->nfaces; f++) {
        const uint32_t c = m->colors[f];
        const int textured = (c & R3D_TEXTURED) && m->uv && m->tex;
        const int emissive = unlit || (c & R3D_EMISSIVE);
        int lods = 0;
        for (int d = 0; d < 4; d++)
            lods |= R3D_LOD_SHOWS(c, (unsigned)d) << d;
        int bone = 0, bone2 = 0;
        if (skinned) {
            const uint16_t *fc = m->faces + f * 3;
            const int b0 = m->vbone[fc[0]], b1 = m->vbone[fc[1]], b2 = m->vbone[fc[2]];
            bone = bone2 = b0;
            if (b1 != b0 || b2 != b0) {
                /* a face on two bones: the textured faces of a skin lit by
                 * the sun (vs_lit_tex2); three bones, or other faces: the
                 * ARM's */
                const int o = b1 != b0 ? b1 : b2;
                if ((b1 != b0 && b1 != o) || (b2 != b0 && b2 != o) || !lit || !textured)
                    return 0;
                bone = b0 < o ? b0 : o;
                bone2 = b0 < o ? o : b0;
            }
            if (bone2 >= m->nbones)
                return 0;
        }
        int vs, fs;
        if (lit && !textured) {
            vs = GV_LIT;
            fs = (c & R3D_SCREEN) ? SH_SCREEN : SH_COLOUR;
        } else if (textured) {
            if (unlit)
                return 0;
            if (!t && !(t = tex_get(g, m->tex)))
                return 0;
            r3d_corner_t v[3];
            for (int k = 0; k < 3; k++) {
                v[k].a = m->uv[f * 6 + k * 2];
                v[k].b = m->uv[f * 6 + k * 2 + 1];
            }
            vs = !lit ? GV_TEX_RGB : bone2 != bone ? GV_LIT_TEX2 : GV_LIT_TEX;
            fs = (c & R3D_SCREEN) ? SH_TEX_RGB_SCREEN : tex_opaque(t, v) ? SH_TEX_RGB : SH_TEX_RGB_ALPHA;
        } else {
            vs = GV_BAKED;
            fs = (c & R3D_SCREEN) ? SH_SCREEN : SH_COLOUR;
        }
        /* the emissive faces of a "lit" model: emissive at their corners */
        const uint32_t k = group_key(bone, bone2, vs, fs, lit || (!emissive && !unlit), lods);
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
    /* the groups in the order of their keys (bone by bone: one block of
     * uniforms a bone), one after the other, each with the stride of its
     * shader */
    static uint8_t order[MESH_GROUPS];
    for (int i = 0; i < nkeys; i++)
        order[i] = (uint8_t)i;
    for (int i = 1; i < nkeys; i++)
        for (int j = i; j > 0 && key[order[j - 1]] > key[order[j]]; j--) {
            const uint8_t x = order[j];
            order[j] = order[j - 1];
            order[j - 1] = x;
        }
    static uint32_t count[MESH_GROUPS];
    memset(count, 0, sizeof count);
    uint32_t bytes = 0;
    for (int f = 0; f < m->nfaces; f++)
        count[gidx[f]] += 3;
    for (int i = 0; i < nkeys; i++)
        bytes += count[i] * vshaders[key[i] >> 12 & 15].words * 4u;
    uint8_t *b = aligned_alloc(16, (bytes + 15) & ~15u);
    ggroup_t *gs = malloc((size_t)(nkeys > 0 ? nkeys : 1) * sizeof *gs);
    if (!b || !gs) {
        free(b);
        free(gs);
        return 0;
    }
    uint32_t at = 0;
    for (int j = 0; j < nkeys; j++) {
        const int i = order[j];
        ggroup_t *gr = &gs[j];
        gr->bone = (uint8_t)(key[i] >> 24);
        gr->bone2 = (uint8_t)(key[i] >> 16);
        gr->vs = (uint8_t)(key[i] >> 12 & 15);
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
                put_mesh_corner(o, m, f, k, gr->vs, !gr->lamps, t, smooth, gr->bone);
        }
        at += count[i] * stride;
    }
    e->corners = b;
    e->g = gs;
    e->ngroups = nkeys;
    e->tex = t;
    e->tex_version = t ? t->version : 0;
    return 1;
}

/* the cached corners of m (made again if it or its texture changed), or
 * NULL; a slot the waiting job still reads is drawn first */
static gmesh_t *mesh_get(const g16_t *g, const r3d_mesh_t *m, int unlit, int smooth)
{
    /* a slot for each way it is drawn (unlit or not, Gouraud or flat) */
    uint16_t *h = &G.hint[(((uintptr_t)m >> 4) + (uintptr_t)(unlit * 2 + smooth) * 131) % MESH_HINTS];
    gmesh_t *e = G.gm[*h].m == m && G.gm[*h].unlit == unlit && G.gm[*h].smooth == smooth ? &G.gm[*h] : NULL;
    if (!e) {
        /* the least used slot, one the open job does not read if any */
        gmesh_t *old = NULL;
        for (int i = 0; i < NMESH; i++) {
            gmesh_t *x = &G.gm[i];
            if (x->m == m && x->unlit == unlit && x->smooth == smooth) {
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
        if (e->m != m || e->unlit != unlit || e->smooth != smooth)
            e->m = NULL;                /* made below */
    }
    if (e->m == m && e->version == m->version && e->unlit == unlit && e->smooth == smooth &&
        (!e->tex || (e->tex->sheet == m->tex && e->tex->version == e->tex_version))) {
        e->used = ++G.tick;
        return e->corners ? e : NULL;
    }
    if (e->corners && e->job == G.job_no && G.open && flush_job(g, 1) != 0)
        return NULL;
    if (e->corners && job_wait() != 0)  /* a job started last may read them (M35) */
        return NULL;
    free(e->corners);
    free(e->g);
    e->corners = NULL;
    e->g = NULL;
    e->m = m;
    e->version = m->version;
    e->unlit = unlit;
    e->smooth = smooth;
    e->used = ++G.tick;
    if (!mesh_build(g, e, m, unlit, smooth))
        e->corners = NULL;
    return e->corners ? e : NULL;
}

/* the uniforms of the groups drawn last: their job, bone, shader and
 * lamps (the groups of a bone share them) */
typedef struct {
    uint32_t job;
    int bone, bone2, vs, lamps;
    uint32_t *cs, *vs_u;
} gunif_t;

#define GL_UNIF_MAX (32 + 96)           /* words of uniforms of a draw, at most */

/* v (world axes) in the axes of a bone whose normals turn by N (3x3 by
 * rows): N^T v */
static void bone_axes(float *o, const float N[9], const float *v)
{
    for (int j = 0; j < 3; j++)
        o[j] = N[j] * v[0] + N[3 + j] * v[1] + N[6 + j] * v[2];
}

/* the uniforms a vertex shader reads: vs_lit_tex reads those of vs_lit */
static int unif_kind(int vs) { return vs == GV_LIT_TEX ? GV_LIT : vs; }

/* the uniforms of a group on two bones (vs_lit_tex2, cs_colour2): the two
 * matrices (A = gr->bone, B = gr->bone2), f*16, 0.5, -f*16, 0.5, -NEAR (the
 * coordinate shader then f/(w/2), f/(h/2)); the vertex shader then the
 * sun, up and V in A's axes and in B's, per byte of the colour B D R A,
 * the fog (near, k), four lamps */
static void gl_uniforms2(gunif_t *u, const ggroup_t *gr, const float MA[12], const float NA[9], const float MB[12],
                         const float NB[9], const r3d_env_t *env)
{
    uint32_t *cs = G.unif_next, *vs = cs + 32;
    float *fc = (float *)cs, *fo = (float *)vs;
    const float tail[5] = { env->f * 16.0f, 0.5f, -env->f * 16.0f, 0.5f, env->front ? -0.1f * R3D_NEAR : -R3D_NEAR };
    memcpy(fc, MA, 12 * sizeof *fc);
    memcpy(fc + 12, MB, 12 * sizeof *fc);
    memcpy(fc + 24, tail, sizeof tail);
    fc[29] = env->f / env->cx;
    fc[30] = env->f / env->cy;
    memcpy(fo, fc, 29 * sizeof *fo);
    fo += 29;
    static const float up[3] = { 0, 1, 0 };
    const float *dirs[3] = { env->sun, up, env->view };
    for (int d = 0; d < 3; d++) {
        bone_axes(fo, NA, dirs[d]);
        bone_axes(fo + 3, NB, dirs[d]);
        fo += 6;
    }
    for (int c = 0; c < 3; c++) {
        const int rgb = c == 1 ? 1 : c == G.ia ? 0 : 2;     /* byte c of the colour */
        fo[0] = env->B[rgb]; fo[1] = env->D[rgb]; fo[2] = env->R[rgb]; fo[3] = env->A[rgb];
        fo += 4;
    }
    fo[0] = env->fog_near;
    fo[1] = env->fog_k;
    fo += 2;
    for (int i = 0; i < 4; i++, fo += 7) {
        if (i < env->nlamps && gr->lamps) {
            memcpy(fo, env->lamp[i], 4 * sizeof *fo);
            fo[4 + G.ia] = env->lamp[i][4];
            fo[5] = env->lamp[i][5];
            fo[6 - G.ia] = env->lamp[i][6];
        } else {
            memset(fo, 0, 7 * sizeof *fo);
        }
    }
    G.unif_next = (uint32_t *)fo;
    u->job = G.job_no;
    u->bone = gr->bone;
    u->bone2 = gr->bone2;
    u->vs = gr->vs;
    u->lamps = gr->lamps;
    u->cs = cs;
    u->vs_u = vs;
}

/* the uniforms of a group: the coordinate shader's (cs) and the vertex
 * shader's, in the order the shaders read them (tools/qpuasm.py) */
static void gl_uniforms(gunif_t *u, const ggroup_t *gr, const float M[12], const float N[9], const r3d_env_t *env)
{
    /* the matrix, f*16, 0.5 (rounding), -f*16, 0.5, -NEAR; the coordinate
     * shader's f/(w/2), f/(h/2) */
    uint32_t *cs = G.unif_next, *vs = cs + 20, *o = vs;
    const float place[17] = { M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11],
                              env->f * 16.0f, 0.5f, -env->f * 16.0f, 0.5f,
                              env->front ? -0.1f * R3D_NEAR : -R3D_NEAR };     /* R3D_FRONT: 10x nearer */
    memcpy(cs, place, sizeof place);
    const float clip[2] = { env->f / env->cx, env->f / env->cy };
    memcpy(cs + 17, clip, sizeof clip);
    memcpy(o, place, sizeof place);
    o += 17;
    float *fo = (float *)o;
    if (unif_kind(gr->vs) == GV_LIT) {
        /* the matrix again (the lamps' point); H, the sun, up and V in the
         * bone's axes; p; per byte of the colour B D R A */
        static const float up[3] = { 0, 1, 0 };
        memcpy(fo, M, 12 * sizeof *fo);
        fo += 12;
        bone_axes(fo, N, env->half);
        bone_axes(fo + 3, N, env->sun);
        bone_axes(fo + 6, N, up);
        bone_axes(fo + 9, N, env->view);
        fo[12] = env->spec_p;
        for (int c = 0; c < 3; c++) {
            const int rgb = c == 1 ? 1 : c == G.ia ? 0 : 2;     /* byte c of the colour */
            float *k = fo + 13 + 4 * c;
            k[0] = env->B[rgb]; k[1] = env->D[rgb]; k[2] = env->R[rgb]; k[3] = env->A[rgb];
        }
        fo += 25;
    }
    /* the fog: near, k, colour (bytes a b c); four lamps (k 0: none) */
    fo[0] = env->fog_near;
    fo[1] = env->fog_k;
    fo[2 + G.ia] = env->fog[0];
    fo[3] = env->fog[1];
    fo[4 - G.ia] = env->fog[2];
    for (int i = 0; i < 4; i++) {
        float *L = fo + 5 + 7 * i;
        if (i < env->nlamps && gr->lamps) {
            memcpy(L, env->lamp[i], 4 * sizeof *L);
            L[4 + G.ia] = env->lamp[i][4];
            L[5] = env->lamp[i][5];
            L[6 - G.ia] = env->lamp[i][6];
        } else {
            memset(L, 0, 7 * sizeof *L);
        }
    }
    fo += 5 + 28;
    if (unif_kind(gr->vs) == GV_LIT) {  /* the highlight's colour */
        fo[G.ia] = env->S[0];
        fo[1] = env->S[1];
        fo[2 - G.ia] = env->S[2];
        fo += 3;
    }
    G.unif_next = (uint32_t *)fo;
    u->job = G.job_no;
    u->bone = gr->bone;
    u->bone2 = gr->bone2;
    u->vs = unif_kind(gr->vs);
    u->lamps = gr->lamps;
    u->cs = cs;
    u->vs_u = vs;
}

/* a size or an offset in the VPM of a GL record (attributes' total size,
 * an attribute's VPM offset) from bytes: bytes, as Mesa writes them ("byte
 * offsets for the start of the vertex attributes 0-7, and the total size"
 * in vc4_context.h), or 32-bit words if the probe found that the V3D wants
 * those. bm3d 3.0-4.2 wrote words: the Pi drew nothing (2026-10-05). */
static uint8_t vpm_size(uint32_t bytes)
{
    return (uint8_t)(G.vpm_bytes ? bytes : bytes / 4);
}

/* one GL batch: the V3D reads the corners of group gr, its vertex shader
 * places them with matrix M (normals turned by N), the binner throws the
 * back faces away; u: the uniforms of the group before, if this one can
 * share them */
static int gl_draw(const g16_t *g, gmesh_t *e, const ggroup_t *gr, gunif_t *u, const float M[12],
                   const float N[9], const float M2[12], const float N2[9], const r3d_env_t *env, int depth)
{
    if (G.failed)
        return -1;
    job_for(g);
    batch_close();
    if (G.rec_next + 64 > G.recs + JOB_RECS || G.cl.p + 128 > G.cl.end ||
        G.unif_next + GL_UNIF_MAX > G.unif + JOB_UNIF / 4) {
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
    const int two = TWO_BONES(gr->vs);
    if (u->job != G.job_no || u->bone != gr->bone || u->bone2 != gr->bone2 || u->vs != unif_kind(gr->vs) ||
        u->lamps != gr->lamps) {
        if (two)
            gl_uniforms2(u, gr, M, N, M2, N2, env);
        else
            gl_uniforms(u, gr, M, N, env);
    }
    uint32_t *cs = u->cs, *vs = u->vs_u;
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
    a = gr->vs == GV_TEX_RGB || gr->vs == GV_LIT_TEX || two ? v3d_bus(e->tex->params) : 0; memcpy(r + 8, &a, 4);
    /* the first attribute: x y z (and w on two bones), the coordinate
     * shader's too */
    const uint32_t head = two ? 16 : 12;
    r[14] = 3; r[15] = vpm_size(stride);
    a = v3d_bus(G.code + 1024 * vshaders[gr->vs].kb); memcpy(r + 16, &a, 4);
    a = v3d_bus(vs); memcpy(r + 20, &a, 4);
    r[26] = 1; r[27] = vpm_size(head);
    a = v3d_bus(G.code + 1024 * (two ? CODE_CS2 : CODE_CS)); memcpy(r + 28, &a, 4);
    a = v3d_bus(cs); memcpy(r + 32, &a, 4);
    a = v3d_bus(e->corners + gr->first); memcpy(r + 36, &a, 4);
    r[40] = (uint8_t)(head - 1); r[41] = (uint8_t)stride; r[42] = 0; r[43] = 0;
    a = v3d_bus(e->corners + gr->first + head); memcpy(r + 44, &a, 4);
    r[48] = (uint8_t)(stride - head - 1); r[49] = (uint8_t)stride; r[50] = vpm_size(head); r[51] = 0;
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

static int cb_mesh(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*M)[12], const float (*N)[9],
                   int nbones, const r3d_env_t *env, int depth)
{
    (void)ctx;
    if (!G.gl_ok || !G.gl_on || G.failed || (!env->inside && !G.clip_ok) || (env->lit && (G.gl_on < 2 || !G.lit_ok)))
        return 0;
    if (m->bones && m->nbones > nbones)
        return 0;
    gmesh_t *e = mesh_get(g, m, env->unlit, env->lit && env->smooth);
    if (!e)
        return 0;
    gunif_t u = { .job = 0, .bone = -1 };
    for (int i = 0; i < e->ngroups; i++) {
        const ggroup_t *gr = &e->g[i];
        if (!(gr->lods >> env->detail & 1))
            continue;
        if (gr->bone >= nbones || gr->bone2 >= nbones ||
            gl_draw(g, e, gr, &u, M[gr->bone], N[gr->bone], M[gr->bone2], N[gr->bone2], env, depth) != 0)
            return 0;
    }
    G.st.glmeshes++;
    return 1;
}

/* M36: the shadow of a mesh the GPU has the corners of (any way it was
 * drawn; else made as the model will be drawn), each group but the
 * screen-door faces' (they cast none) flattened by vs_shadow: black on
 * every other pixel, tested against the depth, not written. Only the faces
 * the sun sees, as the ARM's: flattened along the sun they keep the turn
 * they have seen from it, the others are back faces on the ground */
static int cb_shadow(void *ctx, const g16_t *g, const r3d_mesh_t *m, const float (*W)[12], int nbones,
                     const float C[9], v3_t L, float plane, const r3d_env_t *env)
{
    (void)ctx;
    if (!G.gl_ok || G.gl_on < 2 || G.failed || !G.clip_ok || (m->bones && m->nbones > nbones))
        return 0;
    gmesh_t *e = NULL;
    for (int i = 0; i < NMESH && !e; i++)
        if (G.gm[i].m == m && G.gm[i].version == m->version && G.gm[i].corners)
            e = &G.gm[i];
    if (!e && !(e = mesh_get(g, m, 0, !m->clight && m->vnormals != NULL)))
        return 0;
    e->used = ++G.tick;
    int bone = -1, bone2 = -1;
    uint32_t job = 0, *cs = NULL, *vs = NULL;
    for (int i = 0; i < e->ngroups; i++) {
        const ggroup_t *gr = &e->g[i];
        if (!(gr->lods >> env->detail & 1) || gr->fs == SH_SCREEN || gr->fs == SH_TEX_RGB_SCREEN)
            continue;
        const int two = TWO_BONES(gr->vs);
        if (G.failed)
            return 0;
        job_for(g);
        batch_close();
        if (G.rec_next + 64 > G.recs + JOB_RECS || G.cl.p + 128 > G.cl.end ||
            G.unif_next + 100 > G.unif + JOB_UNIF / 4) {
            if (flush_job(g, 1) != 0)
                return 0;
            job_begin(g->w, g->h);
        }
        clip_window(g);
        config(V3D_CFG_FRONT | (G.gl_cw ? V3D_CFG_CW : 0), V3D_CFG_DEPTH(1));
        viewport((int)(env->cx * 16.0f), (int)(env->cy * 16.0f));
        if (G.clipper[0] != env->cx || G.clipper[1] != env->cy) {
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
        if (job != G.job_no || bone != gr->bone || bone2 != (two ? gr->bone2 : -1)) {
            /* the bone's matrix (on two bones: A's and B's), the plane,
             * 1/Ly, Lx, Lz, the plane + 0.01; the camera's turn (no move),
             * f*16, 0.5, -f*16, 0.5, -1.035 NEAR; the coordinate shader's
             * f/(w/2), f/(h/2) */
            const float *B = W[gr->bone], *B2 = W[gr->bone2];
            float u[46];
            int n = 0;
            memcpy(u, B, 12 * sizeof *u);
            n = 12;
            if (two) {
                memcpy(u + n, B2, 12 * sizeof *u);
                n += 12;
            }
            const float rest[22] = { plane, 1.0f / L.y, L.x, L.z, plane + 0.01f,
                                     C[0], C[1], C[2], 0, C[3], C[4], C[5], 0, C[6], C[7], C[8], 0,
                                     env->f * 16.0f, 0.5f, -env->f * 16.0f, 0.5f, -1.035f * R3D_NEAR };
            memcpy(u + n, rest, sizeof rest);
            n += 22;
            cs = G.unif_next;
            vs = cs + n + 2;
            memcpy(cs, u, (size_t)n * sizeof *u);
            const float clip[2] = { env->f / env->cx, env->f / env->cy };
            memcpy(cs + n, clip, sizeof clip);
            memcpy(vs, u, (size_t)n * sizeof *u);
            G.unif_next = vs + n;
            job = G.job_no;
            bone = gr->bone;
            bone2 = two ? gr->bone2 : -1;
        }
        const uint32_t stride = vshaders[gr->vs].words * 4;
        uint8_t *r = G.rec_next;
        G.rec_next += 64;
        memset(r, 0, 64);
        uint32_t a;
        r[0] = 4;                               /* clipping */
        r[3] = shaders[SH_SCREEN].varyings;
        a = v3d_bus(G.code + 1024 * SH_SCREEN); memcpy(r + 4, &a, 4);
        r[14] = 1; r[15] = vpm_size(two ? 16 : 12);
        a = v3d_bus(G.code + 1024 * (two ? CODE_SHADOW2 : CODE_SHADOW)); memcpy(r + 16, &a, 4);
        a = v3d_bus(vs); memcpy(r + 20, &a, 4);
        r[26] = 1; r[27] = vpm_size(two ? 16 : 12);
        a = v3d_bus(G.code + 1024 * (two ? CODE_SHADOW2 + 1 : CODE_SHADOW + 2)); memcpy(r + 28, &a, 4);
        a = v3d_bus(cs); memcpy(r + 32, &a, 4);
        a = v3d_bus(e->corners + gr->first); memcpy(r + 36, &a, 4);
        r[40] = two ? 15 : 11; r[41] = (uint8_t)stride; r[42] = 0; r[43] = 0;
        v3d_cl_u8(&G.cl, V3D_GL_SHADER_STATE);
        v3d_cl_u32(&G.cl, v3d_bus(r) | 1);
        v3d_cl_u8(&G.cl, V3D_VERTEX_ARRAY_PRIMITIVES);
        v3d_cl_u8(&G.cl, 4);
        v3d_cl_u32(&G.cl, gr->n);
        v3d_cl_u32(&G.cl, 0);
        e->job = G.job_no;
        G.gldraws++;
        G.st.tris += gr->n / 3;
        G.st.gltris += gr->n / 3;
    }
    return 1;
}

/* ---------------------------------------------------------------- 2D (M37) */

/* The 2D a cartridge draws over its 3D, in the same job (gpu3d_*2d): no
 * job ends for it, and a frame of 3D, HUD, 3D, HUD is one job. Only what
 * the GPU draws as the ARM does, pixel for pixel: rectangles, sprites (at
 * whole zooms), text, the cells of a map. No depth test, none written:
 * the 3D after it hides it where the 3D before it would be nearer, as on
 * the ARM (whose 2D leaves the depth as it is). */

/* an RGB565 colour as the colour varyings: the store gives it back */
static void col565(uint16_t c, float *v)
{
    v[0] = (float)((c >> 11) << 3 | c >> 13) * (1.0f / 255.0f);
    v[1] = (float)((c >> 5 & 63) << 2 | (c >> 9 & 3)) * (1.0f / 255.0f);
    v[2] = (float)((c & 31) << 3 | (c >> 2 & 7)) * (1.0f / 255.0f);
}

/* the quad [x0, x1) x [y0, y1) of g's page, cut to its clip rectangle
 * here (the corners in the 12.4 range, fewer pixels), its texel at a
 * point (u0 + (x - x0) du, v0 + (y - y0) dv) in texels; 1 if taken (or
 * nothing to draw), 0 if the GPU cannot */
static int quad2d(const g16_t *g, int shader, const tex_t *t, int x0, int y0, int x1, int y1, float u0, float v0,
                  float du, float dv, const float *col)
{
    const int cx0 = x0 < g->cx0 ? g->cx0 : x0, cy0 = y0 < g->cy0 ? g->cy0 : y0,
              cx1 = x1 > g->cx1 ? g->cx1 : x1, cy1 = y1 > g->cy1 ? g->cy1 : y1;
    if (cx0 >= cx1 || cy0 >= cy1)
        return 1;
    if (!batch_takes(g, shader, R3D_DEPTH_NONE, t) && batch_for(g, shader, R3D_DEPTH_NONE, t) != 0)
        return 0;
    const float ua = u0 + (float)(cx0 - x0) * du, ub = u0 + (float)(cx1 - x0) * du,
                va = v0 + (float)(cy0 - y0) * dv, vb = v0 + (float)(cy1 - y0) * dv;
    const int xy[6][2] = { { cx0, cy0 }, { cx1, cy0 }, { cx1, cy1 }, { cx0, cy0 }, { cx1, cy1 }, { cx0, cy1 } };
    uint8_t *o = G.verts + G.vbytes;
    for (int i = 0; i < 6; i++, o += G.b_stride) {
        gvert_t *v = (gvert_t *)o;
        v->x = (int16_t)(xy[i][0] * 16);
        v->y = (int16_t)(xy[i][1] * 16);
        v->z = 0;
        v->inv_w = 1.0f;
        if (shader == SH_COLOUR) {
            v->v[G.ia] = col[0];
            v->v[1] = col[1];
            v->v[2 - G.ia] = col[2];
            continue;
        }
        v->v[0] = (xy[i][0] == cx0 ? ua : ub) * t->inv_w;
        v->v[1] = (xy[i][1] == cy0 ? va : vb) * t->inv_h;
        if (shader == SH_TEXT) {
            v->v[2 + G.ia] = col[0];
            v->v[3] = col[1];
            v->v[4 - G.ia] = col[2];
        } else {
            v->v[2] = 1.0f;                 /* k: the texel as it is */
        }
    }
    G.vbytes += 6 * G.b_stride;
    G.st.tris += 2;
    G.st.quads2d++;
    return 1;
}

int gpu3d_rect2d(const g16_t *g, int x0, int y0, int x1, int y1, uint16_t c)
{
    if (G.failed || !G.ready)
        return 0;
    float col[3];
    col565(c, col);
    return quad2d(g, SH_COLOUR, NULL, x0, y0, x1, y1, 0, 0, 0, 0, col);
}

int gpu3d_blit2d(const g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh, int dx, int dy, int zoom,
                 int flip_x, int flip_y)
{
    if (G.failed || !G.ready || zoom < 1 || sw <= 0 || sh <= 0)
        return 0;
    const tex_t *t = tex_get(g, s);
    if (!t)
        return 0;
    const float k = 1.0f / (float)zoom;
    return quad2d(g, SH_TEX2D, t, dx, dy, dx + sw * zoom, dy + sh * zoom, (float)(flip_x ? sx + sw : sx),
                  (float)(flip_y ? sy + sh : sy), flip_x ? -k : k, flip_y ? -k : k, NULL);
}

/* the glyphs of a font as a sheet: 16 a row, 8 pixels wide (a glyph's row
 * is a byte: a narrower font may light all 8), white where lit */
#define NFONTS 6
static struct { const font_t *font; g16_sheet_t sheet; } fonts[NFONTS];

static const g16_sheet_t *font_sheet(const font_t *f)
{
    int i = 0;
    while (i < NFONTS && fonts[i].font && fonts[i].font != f)
        i++;
    if (i == NFONTS)
        return NULL;
    if (fonts[i].font)
        return &fonts[i].sheet;
    g16_sheet_t *sh = &fonts[i].sheet;
    if (g16_sheet_alloc(sh, 128, 16 * f->height) != 0)
        return NULL;
    for (int c = 0; c < 256; c++)
        for (int r = 0; r < f->height; r++) {
            const uint8_t bits = f->glyphs[c * f->height + r];
            for (int b = 0; b < 8; b++)
                g16_sheet_set(sh, c % 16 * 8 + b, c / 16 * f->height + r, 0xFFFF, bits >> (7 - b) & 1);
        }
    for (int cy = 0; cy < sh->h / G16_CELL; cy++)
        for (int cx = 0; cx < sh->w / G16_CELL; cx++)
            g16_sheet_update_cell(sh, cx, cy);
    fonts[i].font = f;
    return sh;
}

/* a glyph at (sx, sy) of the page, zoom times bigger */
static int glyph2d(const g16_t *g, const tex_t *t, const font_t *f, unsigned ch, int sx, int sy, int zoom,
                   const float *col)
{
    const float k = 1.0f / (float)zoom;
    return quad2d(g, SH_TEXT, t, sx, sy, sx + 8 * zoom, sy + f->height * zoom, (float)(ch % 16 * 8),
                  (float)(ch / 16 * f->height), k, k, col);
}

int gpu3d_text2d(const g16_t *g, int x, int y, const char *str, uint16_t c, int scale)
{
    const font_t *f = g->font;
    if (G.failed || !G.ready || !f || f->width > 8)
        return 0;
    const g16_sheet_t *fs = font_sheet(f);
    const tex_t *t = fs ? tex_get(g, fs) : NULL;
    if (!t)
        return 0;
    float col[3];
    col565(c, col);
    /* the places of g16_text and g16_text_scaled, each with its own way of
     * going to a new line */
    if (scale <= 1) {
        int sx = x - g->cam_x, sy = y - g->cam_y;
        const int cw = f->width;
        for (; *str; str++, sx += cw, x += cw) {
            if (*str == '\n') {
                sy += f->height;
                sx = x = x - cw;
                continue;
            }
            if (sx >= g->cx1 || sx + cw <= g->cx0 || sy >= g->cy1 || sy + f->height <= g->cy0)
                continue;                   /* (as g16_text: by the font's width) */
            if (!glyph2d(g, t, f, (uint8_t)*str, sx, sy, 1, col))
                return 0;
        }
        return 1;
    }
    const int cw = f->width * scale, chh = f->height * scale;
    const int x0 = x;
    for (; *str; str++, x += cw) {
        if (*str == '\n') {
            y += chh;
            x = x0 - cw;
            continue;
        }
        const int sx = x - g->cam_x, sy = y - g->cam_y;
        if (sx >= g->cx1 || sx + cw <= g->cx0 || sy >= g->cy1 || sy + chh <= g->cy0)
            continue;
        if (!glyph2d(g, t, f, (uint8_t)*str, sx, sy, scale, col))
            return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- enlarging (M37) */

/* A frame of RGB565 pixels (the menu's 640x360 layout) as a texture the
 * TMU reads (RGBA32R: the TMU reads no RGB565 in rows), through a table of
 * the 65536 colours: a load and a store a pixel */
static struct {
    uint32_t *lut;
    uint32_t *texels;
    size_t size;
    tex_t t;
} big;

int gpu3d_enlarge(const uint16_t *src, int w, int h, int scale, const g16_t *page)
{
    if (!G.ready || G.failed || w < 1 || h < 1 || w > 2048 || h > 2048 || scale < 1 || page->stride != (uint32_t)page->w)
        return -1;
    job_wait();
    if (!big.lut) {
        big.lut = malloc(65536 * sizeof *big.lut);
        if (!big.lut)
            return -1;
        for (uint32_t c = 0; c < 65536; c++)
            big.lut[c] = texel_of((uint16_t)c, 1);
    }
    const size_t size = (size_t)w * (size_t)h * 4 + 16;
    if (!big.texels || big.size < size) {
        free(big.texels);
        big.texels = aligned_alloc(4096, (size + 4095) & ~(size_t)4095);
        big.size = big.texels ? size : 0;
        if (!big.texels)
            return -1;
    }
    const uint32_t n = (uint32_t)w * (uint32_t)h;
    const uint32_t *lut = big.lut;
    uint32_t *o = big.texels;
    for (uint32_t i = 0; i < n; i++)
        o[i] = lut[src[i]];
    tex_t *t = &big.t;
    t->texels = big.texels;
    t->params = big.texels + n;
    t->params[0] = v3d_bus(big.texels) & ~0xFFFu;
    t->params[1] = 1u << 31 | (uint32_t)(h & 2047) << 20 | (uint32_t)(w & 2047) << 8 | 1u << 7 | 1u << 4 | 1u << 2 | 1u;
    t->params[2] = t->params[0];
    t->params[3] = t->params[1];
    t->w = w;
    t->h = h;
    t->inv_w = 1.0f / (float)w;
    t->inv_h = 1.0f / (float)h;
    /* one job: the tiles start cleared (the quad covers them all), the
     * frame scale times bigger, the nearest texel (as the ARM enlarged it) */
    const int msaa = G.msaa;
    G.msaa = 0;
    gpu3d_drop();
    gpu3d_page(1, 0);
    const float k = 1.0f / (float)scale;
    int r = quad2d(page, SH_TEX2D, t, 0, 0, w * scale, h * scale, 0, 0, k, k, NULL) ? flush_job(page, 0) : -1;
    G.page_uniform = 0;
    G.msaa = msaa;
    return r;
}

/* ---------------------------------------------------------------- run */

/* Rendering list for a frame at bus address fb (w x h, BGR565): every
 * tile loaded from the frame (load), drawn from its tile list (bin) and
 * stored back. The depth starts cleared, or loaded from zbuf (zload);
 * zstore keeps it there for the next job. Two loads of a tile take place
 * one at a time (tile coordinates, then a store of nothing that clears
 * nothing), and so do two stores, as Linux's vc4 does. */
/* sem (M35, a job started with v3d_start): the rendering waits on the
 * binner's semaphore before the first tile reads the tile lists */
static uint32_t rcl_build(uint32_t fb, int w, int h, int bin, int load, uint32_t clear, int zload, int zstore,
                          int ms, int sem)
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
                if (sem && x == 0 && y == 0)
                    v3d_cl_u8(&cl, V3D_WAIT_ON_SEMAPHORE);
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

/* M35: the job started (v3d_start) and not waited for: wait now. The
 * ARM must not touch what it reads (the job's lists, records, uniforms
 * and vertices, the meshes' corners, the textures) nor the page it draws
 * on before this */
static int job_wait(void)
{
    if (!G.inflight)
        return 0;
    G.inflight = 0;
    uint32_t bus_us = 0, rus = 0;
    const int err = v3d_wait(TIMEOUT_US, &bus_us, &rus);
    G.st.render_us += rus;
    if (rus > G.st.max_us)
        G.st.max_us = rus;
    if (err) {
        static char buf[640];
        v3d_dump(buf, sizeof buf);
        kprintf("gpu3d: the V3D did not finish a started job:\n%s", buf);
        disable("the GPU did not finish a frame started early (registers in the log)");
        return -1;
    }
    return 0;
}

static int run_async(uint32_t rcl_end)
{
    v3d_set_overflow(v3d_bus(G.overflow), JOB_OVERFLOW);
    if (v3d_start(v3d_bus(G.bcl), v3d_bus(G.cl.p), v3d_bus(G.rcl), rcl_end) != 0)
        return -1;
    G.inflight = 1;
    G.st.jobs++;
    G.st.queued++;
    return 0;
}

int gpu3d_sync(void)
{
    return job_wait();
}

int gpu3d_pending(void)
{
    return G.open && (G.vbytes || G.b_open || G.gldraws);
}

void gpu3d_drop(void)
{
    job_wait();
    batch_close();
    G.open = 0;
    G.z_saved = G.split = G.z_wanted = 0;
    G.page_uniform = 0;
}

void gpu3d_set_msaa(int on)
{
    G.msaa = on;
}

/* M35: the memory of the jobs uncached: the ARM writes the lists, records,
 * uniforms and vertices of a frame once and never reads them, so a cache
 * line read for each (write-allocate) and pushed out later is lost work,
 * and the lines of the Lua and of r3d it pushes out come back as misses.
 * Uncached, the writes go out merged by the write buffer. Changed between
 * jobs (the one started last waited for) */
static void wc_apply(void)
{
    if (!G.block || G.wc == G.wc_on)
        return;
    job_wait();
    const int n = v3d_uncached(G.block, JOB_SECTIONS, G.wc_on);
    G.wc = G.wc_on && n > 0;
}

void gpu3d_set_wc(int on)
{
    G.wc_on = on;
    if (G.ready && !G.failed)
        wc_apply();
}

int gpu3d_wc(void)
{
    return G.wc;
}

int gpu3d_msaa(void)
{
    return G.ms_ok;
}

int gpu3d_msaa_on(void)
{
    return G.msaa && G.ms_ok > 0;
}

/* M37: the textures filtered (bilinear: the four texels around a point
 * mixed) or the nearest texel, as the ARM draws them; the textures made so
 * far are made again with the other parameters (a slot the waiting job
 * reads is drawn first, tex_get) */
void gpu3d_set_bilinear(int on)
{
    if (G.bilinear == !!on)
        return;
    G.bilinear = !!on;
    for (int i = 0; i < NTEX; i++)
        G.tex[i].version = ~G.tex[i].version;   /* made again, in the same slot */
}

int gpu3d_bilinear(void)
{
    return G.bilinear;
}

void gpu3d_tiled_textures(int on)
{
    G.tformat_off = !on;
    for (int i = 0; i < NTEX; i++)
        G.tex[i].sheet = NULL;          /* made again in the other layout */
}

int gpu3d_vshader(void)
{
    return !G.gl_ok ? 0 : G.clip_ok && G.lit_ok ? 2 : 1;
}

void gpu3d_set_vshader(int on)
{
    G.gl_on = on;
}

int gpu3d_vshader_on(void)
{
    return G.gl_ok ? G.gl_on : 0;
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

int gpu3d_cleared(void)
{
    const int c = G.cleared;
    G.cleared = 0;
    return c;
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
    const int async = G.async_now;
    if (async)
        v3d_cl_u8(&G.cl, V3D_INCREMENT_SEMAPHORE);  /* the rendering waits on it (M35) */
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
    uint32_t end = rcl_build(fb, g->w, g->h, 1, load, load ? 0 : clear_of(G.page_colour), zload, zstore, G.ms,
                             async);
    if (G.ms)
        G.st.msjobs++;
    G.page_uniform = 0;                 /* now it has the 3D too */
    if (!load)
        G.st.cleared++;
    if (async ? run_async(end) != 0 : run(1, end) != 0) {
        disable("the GPU did not finish a frame (registers in the log)");
        return -1;
    }
    G.cleared |= !load;
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
    if (job_wait() != 0)                /* a job started before: the page has it when this returns */
        r = -1;
    return r;
}

int gpu3d_submit(const g16_t *g, int keep)
{
    if (!G.queue_ok || !G.queue_on || G.failed)
        return gpu3d_flush(g, keep);
    const int had = G.open && (G.vbytes || G.gldraws);
    if (had) {                          /* (nothing new: a job started stays started) */
        if (job_wait() != 0)
            return -1;
        G.async_now = 1;
    }
    const int r = flush_job(g, keep && G.z_wanted);
    G.async_now = 0;
    if (!keep) {
        G.z_saved = 0;
        G.split = 0;
    } else if (had) {
        G.split = 1;
    }
    return r;
}

int gpu3d_inflight(void)
{
    return G.inflight;
}

void gpu3d_set_queue(int on)
{
    G.queue_on = on;
}

int gpu3d_queue(void)
{
    return G.queue_ok && G.queue_on;
}

int gpu3d_queue_ok(void)
{
    return G.queue_ok;
}

int gpu3d_zclear_ok(void)
{
    return G.zclear_ok;
}

void gpu3d_take_stats(gpu3d_stats_t *s)
{
    *s = G.st;
    memset(&G.st, 0, sizeof G.st);
}

void gpu3d_peek_stats(gpu3d_stats_t *s)
{
    *s = G.st;
}

/* ---------------------------------------------------------------- init */

static uint8_t *block_alloc(void)
{
    uint8_t *b = aligned_alloc(1u << 20, JOB_SECTIONS);
    if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + JOB_SECTIONS - 1) >> 28)) {
        uint8_t *again = aligned_alloc(1u << 20, JOB_SECTIONS);     /* across 256 MiB: another */
        free(b);
        b = again;
        if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + JOB_SECTIONS - 1) >> 28)) {
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
    if (run(0, rcl_build(bus, PROBE_W, PROBE_H, 0, 0, clear_of(0x001F), 0, 0, 1, 0)) != 0 ||
        G.probe[10 * PROBE_W + 10] != 0x001F || G.probe[PROBE_W * PROBE_H - 1] != 0x001F)
        return 0;
    for (int i = 0; i < PROBE_W * PROBE_H; i++)
        G.probe[i] = 0x07E0;
    if (run(0, rcl_build(bus, PROBE_W, PROBE_H, 0, 1, clear_of(0xF800), 0, 0, 1, 0)) != 0)
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

static const float I3[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };      /* normals unturned */

/* After a probe's job that did not end (G.failed): 0 and the GPU on
 * again if a clear still runs, else -1 (the GPU stays off) */
static int recover(void)
{
    G.failed = 0;
    G.open = G.inflight = 0;
    memset(G.probe, 0x55, JOB_PROBE);
    if (run(0, rcl_build(v3d_bus(G.probe), PROBE_W, PROBE_H, 0, 0, clear_of(0x07E0), 0, 0, 0, 0)) != 0 ||
        G.probe[PROBE_W * 10 + 10] != 0x07E0) {
        disable("the GPU stopped answering after a probe (registers in the log)");
        return -1;
    }
    return 0;
}

/* M36: two triangles placed by the vertex shader on a cleared buffer, red
 * on the left as r3d's front faces turn (clockwise on the screen), green on
 * the right the other way round. Whichever the V3D keeps tells the
 * clockwise bit; points inside and outside the red one check where the
 * shader placed it. Tried with the GL record's VPM offsets and sizes in
 * bytes (as Mesa), then in words (bm3d 3.0-4.2: on the Pi nothing was
 * drawn); a job that does not end only rules its way out. 1 if all of it
 * holds, else 0 (the meshes stay on the ARM's path). */
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
    char line[160];
    for (int bytes = 1; bytes >= 0; bytes--) {
        const char *unit = bytes ? "bytes" : "words";
        G.vpm_bytes = bytes;
        for (int cw = 0; cw < 2; cw++) {
            G.gl_cw = cw;
            memset(G.probe, 0, JOB_PROBE);
            gmesh_t *e = mesh_get(pg, &m, 1, 0);
            gunif_t u = { .job = 0, .bone = -1 };
            if (!e || e->ngroups != 1) {
                ksnprintf(line, sizeof line, "vertex shader probe: mesh %s, groups %d", e ? "made" : "not made",
                          e ? e->ngroups : 0);
                plog(line);
                return 0;
            }
            if (gl_draw(pg, e, &e->g[0], &u, M, I3, M, I3, &env, R3D_DEPTH_WRITE) != 0 || gpu3d_flush(pg, 0) != 0) {
                ksnprintf(line, sizeof line, "vertex shader probe (VPM in %s): the job did not end", unit);
                plog(line);
                if (recover() != 0)
                    return 0;
                break;                  /* the other unit */
            }
            const uint16_t left = G.probe[20 * PROBE_W + 10], right = G.probe[20 * PROBE_W + 40],
                           in = G.probe[40 * PROBE_W + 10], out = G.probe[40 * PROBE_W + 20];
            if (left == 0xF800 && right == 0 && in == 0xF800 && out == 0) {
                ksnprintf(line, sizeof line, "vertex shader probe: drawn (VPM in %s, clockwise bit %d)", unit, cw);
                plog(line);
                return 1;               /* red only, where it should be */
            }
            if (!(left == 0 && right == 0x07E0)) {
                ksnprintf(line, sizeof line, "vertex shader probe (VPM in %s): %04x %04x %04x %04x (clockwise bit %d)",
                          unit, left, right, in, out, cw);
                plog(line);
                break;
            }
            if (cw) {
                ksnprintf(line, sizeof line, "vertex shader probe (VPM in %s): only the green triangle with either "
                          "clockwise bit", unit);
                plog(line);
            }
        }
    }
    G.vpm_bytes = 1;
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
        gmesh_t *e = mesh_get(pg, &m, 1, 0);
        gunif_t u = { .job = 0, .bone = -1 };
        if (!e || e->ngroups != 1 || gl_draw(pg, e, &e->g[0], &u, M, I3, M, I3, &env, R3D_DEPTH_NONE) != 0 ||
            gpu3d_flush(pg, 0) != 0) {
            G.clip_ok = 0;
            return 0;
        }
        unsigned wrong = 0;
        for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++)
            wrong |= (G.probe[at[i].y * PROBE_W + at[i].x] != at[i].want) << i;
        if (!wrong)
            return zplanes;
        char line[96];
        ksnprintf(line, sizeof line, "clipping probe (Z planes %s): pixels %04x wrong", zplanes == 2 ? "on" : "off",
                  wrong);
        plog(line);
    }
    G.clip_ok = 0;
    return 0;
}

/* M36: vs_lit against r3d's light_fast, worked out here: a grey face lit
 * by the sun from the front with a highlight (n.H = 0.8, p 4), and one lit
 * from behind (no sun, no highlight: the ambient and the rim) */
static int probe_lit(const g16_t *pg)
{
    static r3d_mesh_t m;
    static v3_t v[6], n[2];
    static uint16_t f[6] = { 0, 1, 2, 3, 4, 5 };
    static uint32_t c[2] = { 0x808080 | R3D_GLOSSY, 0x808080 | R3D_GLOSSY };
    static const float s[6][2] = { { 6, 10 }, { 28, 10 }, { 6, 54 }, { 36, 10 }, { 58, 10 }, { 36, 54 } };
    for (int i = 0; i < 6; i++)
        v[i] = (v3_t){ (s[i][0] - 32) / 16.0f, (32 - s[i][1]) / 16.0f, 0 };
    n[0] = (v3_t){ 0, 0, -1 };
    n[1] = (v3_t){ 0, 0, 1 };
    m.nverts = 6;
    m.nfaces = 2;
    m.verts = v;
    m.faces = f;
    m.colors = c;
    m.normals = n;
    m.version = 0xFFFFFFFDu;
    const float M[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2 };
    r3d_env_t env;
    memset(&env, 0, sizeof env);
    env.f = env.cx = env.cy = 32;
    env.inside = 1;
    env.lit = 1;
    env.sun[2] = -1;
    env.view[2] = -1;
    env.half[1] = 0.6f;
    env.half[2] = -0.8f;
    env.spec_p = 4;
    for (int k = 0; k < 3; k++) {
        env.A[k] = 0.2f; env.B[k] = 0.3f; env.D[k] = 0.4f; env.R[k] = 0.5f; env.S[k] = 0.25f;
    }
    const float grey = 128.0f / 255.0f, want[2] = { grey * 0.6f + 0.25f * 0.4096f, grey * 0.7f };
    memset(G.probe, 0, JOB_PROBE);
    gmesh_t *e = mesh_get(pg, &m, 0, 0);
    gunif_t u = { .job = 0, .bone = -1 };
    if (!e || e->ngroups != 1 || gl_draw(pg, e, &e->g[0], &u, M, I3, M, I3, &env, R3D_DEPTH_WRITE) != 0 ||
        gpu3d_flush(pg, 0) != 0)
        return 0;
    for (int i = 0; i < 2; i++) {
        const uint16_t px = G.probe[20 * PROBE_W + (i ? 40 : 10)];
        const int r = px >> 11, g = px >> 5 & 63, b = px & 31;
        const int wr = (int)(want[i] * 31 + 0.5f), wg = (int)(want[i] * 63 + 0.5f);
        if (r < wr - 2 || r > wr + 2 || b < wr - 2 || b > wr + 2 || g < wg - 3 || g > wg + 3) {
            char line[96];
            ksnprintf(line, sizeof line, "lit probe: face %d %04x, expected about %02x %02x %02x", i, px, wr, wg, wr);
            plog(line);
            return 0;
        }
    }
    return 1;
}

/* The order of red and blue: a clear with byte a of the colour full says
 * where byte a lands in BGR565; a texture whose texels have byte a full
 * says whether the TMU keeps the bytes in place. */
/* M35: a job started with v3d_start (the binner's semaphore, the
 * rendering waiting on it) on a cleared buffer, a green quad over all of
 * it; 1 if the V3D ends it and the buffer is green, else 0 (every job is
 * run and waited for, as before) */
static int probe_queue(const g16_t *pg)
{
    const float z = 0.5f;
    const r3d_corner_t v[4] = {
        { 0, 0, z, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } }, { PROBE_W, 0, z, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } },
        { PROBE_W, PROBE_H, z, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } }, { 0, PROBE_H, z, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } },
    };
    const r3d_corner_t t1[3] = { v[0], v[1], v[2] }, t2[3] = { v[0], v[2], v[3] };
    memset(G.probe, 0, JOB_PROBE);
    G.page_uniform = 1;
    G.page_colour = 0;
    add_tri(pg, t1, R3D_KIND_COLOUR, NULL, 1, SH_COLOUR, 0);
    add_tri(pg, t2, R3D_KIND_COLOUR, NULL, 1, SH_COLOUR, 0);
    G.async_now = 1;
    int r = flush_job(pg, 0);
    G.async_now = 0;
    if (r != 0 || G.failed)
        return 0;
    uint32_t b = 0, us = 0;
    G.inflight = 0;
    if (v3d_wait(TIMEOUT_US, &b, &us) != 0) {
        plog("a started job did not end: every job runs to its end as before");
        return 0;
    }
    G.page_uniform = 0;
    const uint16_t c = G.probe[PROBE_W * 20 + 20], d = G.probe[PROBE_W * 50 + 40];
    return c == 0x07E0 && d == 0x07E0;
}

/* M35: zclear() inside a job. Green over the whole buffer, the depth
 * cleared by fs_zclear, then blue farther away on the left half: blue
 * there (the depth was cleared), green on the right (the colour kept) */
static int probe_zclear(const g16_t *pg)
{
    const float nz = 0.5f, fz = 0.25f;      /* 1/w: the blue is farther */
    const r3d_corner_t v[4] = {
        { 0, 0, nz, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } }, { PROBE_W, 0, nz, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } },
        { PROBE_W, PROBE_H, nz, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } }, { 0, PROBE_H, nz, 0, 1, 0, { 0, 0, 0 }, { 0, 0, 0 } },
    };
    const r3d_corner_t h[4] = {
        { 0, 0, fz, 0, 0, 1, { 0, 0, 0 }, { 0, 0, 0 } }, { PROBE_W / 2, 0, fz, 0, 0, 1, { 0, 0, 0 }, { 0, 0, 0 } },
        { PROBE_W / 2, PROBE_H, fz, 0, 0, 1, { 0, 0, 0 }, { 0, 0, 0 } }, { 0, PROBE_H, fz, 0, 0, 1, { 0, 0, 0 }, { 0, 0, 0 } },
    };
    const r3d_corner_t t1[3] = { v[0], v[1], v[2] }, t2[3] = { v[0], v[2], v[3] };
    const r3d_corner_t t3[3] = { h[0], h[1], h[2] }, t4[3] = { h[0], h[2], h[3] };
    memset(G.probe, 0, JOB_PROBE);
    G.page_uniform = 1;
    G.page_colour = 0;
    add_tri(pg, t1, R3D_KIND_COLOUR, NULL, R3D_DEPTH_WRITE, SH_COLOUR, 0);
    add_tri(pg, t2, R3D_KIND_COLOUR, NULL, R3D_DEPTH_WRITE, SH_COLOUR, 0);
    if (zclear_quad(pg) != 0)
        return 0;
    add_tri(pg, t3, R3D_KIND_COLOUR, NULL, R3D_DEPTH_WRITE, SH_COLOUR, 0);
    add_tri(pg, t4, R3D_KIND_COLOUR, NULL, R3D_DEPTH_WRITE, SH_COLOUR, 0);
    if (flush_job(pg, 0) != 0 || G.failed) {
        plog("zclear() inside a job did not work: a zclear() ends the job as before");
        return 0;
    }
    G.page_uniform = 0;
    const uint16_t l = G.probe[PROBE_W * 30 + 10], r = G.probe[PROBE_W * 30 + 50];
    if (l == 0x001F && r == 0x07E0)
        return 1;
    char line[120];
    ksnprintf(line, sizeof line, "zclear() inside a job gave %04x %04x (expected 001f 07e0): a zclear() ends the job",
              l, r);
    plog(line);
    return 0;
}

/* After a probe of something optional (the vertex shader, its clipping,
 * the queue, zclear() in a job): if the GPU failed in it (a job that did
 * not end), only that is off, as long as a clear still runs; else the GPU
 * is off as before */
static int soft(int ok, const char *what)
{
    if (!G.failed)
        return ok;
    if (recover() != 0)
        return 0;
    char line[160];
    ksnprintf(line, sizeof line, "%s: the GPU did not end its job; that stays off, the rest goes on", what);
    plog(line);
    return 0;
}

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
    if (run(0, rcl_build(v3d_bus(G.probe), PROBE_W, PROBE_H, 0, 0, 0x000000FFu, 0, 0, 0, 0)) != 0) {
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
    G.gl_ok = soft(probe_gl(&pg), "the vertex shader's probe");
    G.clip_ok = G.gl_ok && !G.failed ? soft(probe_clip(&pg), "the clipping probe") : 0;
    G.lit_ok = G.gl_ok && !G.failed ? soft(probe_lit(&pg), "the lit models' probe") : 0;
    G.queue_ok = !G.failed ? soft(probe_queue(&pg), "the queue's probe") : 0;
    G.zclear_ok = !G.failed ? soft(probe_zclear(&pg), "the probe of zclear() in a job") : 0;
    if (G.failed)
        return -1;
    memset(&G.st, 0, sizeof G.st);
    return 0;
}

int gpu3d_init(void)
{
    if (G.ready || G.failed)
        return G.failed ? -1 : 0;
    G.status = "starting";
    G.vpm_bytes = 1;
    G.plog[0] = 0;
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
    memcpy(G.code + 1024 * CODE_SHADOW, vs_shadow, sizeof vs_shadow);
    memcpy(G.code + 1024 * (CODE_SHADOW + 2), cs_shadow, sizeof cs_shadow);
    memcpy(G.code + 1024 * CODE_CS2, cs_colour2, sizeof cs_colour2);
    memcpy(G.code + 1024 * CODE_SHADOW2, vs_shadow2, sizeof vs_shadow2);
    memcpy(G.code + 1024 * (CODE_SHADOW2 + 1), cs_shadow2, sizeof cs_shadow2);
    G.backend.tri = cb_tri;
    G.backend.mesh = cb_mesh;
    G.backend.shadow = cb_shadow;
    G.backend.zclear = cb_zclear;
    G.backend.tex_screen = 1;
    set_guard(G.scr_w ? G.scr_w : 640, G.scr_h ? G.scr_h : 360);   /* r3d's bound too */
    if (probe() != 0)
        return -1;
    set_guard(G.scr_w, G.scr_h);        /* the probe's jobs had theirs */
    wc_apply();                         /* the probes ran with the block cached */
    static const char *const ms[3] = { "no", "on cleared pages", "on any page" };
    static const char *const clips[3] = { "no", "yes", "yes (Z planes)" };
    ksnprintf(G.why, sizeof G.why, "bm3d " BM3D_VERSION " ready (byte a = %s, texels %s, textures %s, MSAA %s, "
              "vertex shader %s, "
              "clipping %s, lit models %s, queue %s, zclear in job %s)", G.red_a ? "red" : "blue",
              G.tex_swap ? "swapped" : "in place", G.tformat ? "in tiles" : "in rows", ms[G.ms_ok],
              G.gl_ok ? (G.vpm_bytes ? (G.gl_cw ? "yes (cw)" : "yes") : (G.gl_cw ? "yes (cw, VPM in words)"
                                                                                 : "yes (VPM in words)")) : "no",
              clips[G.clip_ok], G.lit_ok ? "yes" : "no",
              G.queue_ok ? "yes" : "no", G.zclear_ok ? "yes" : "no");
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
