/*
 * A V3D for the tests on the PC (M30): the functions of src/gpu/v3d.h
 * over a small emulator. It reads the control lists bm writes (only the
 * packets bm uses: anything else is an error), keeps the triangles of the
 * binning list with their state, and runs the rendering list tile by tile
 * on a 64x64 tile buffer (bytes a b c d, 24-bit depth): loads, the
 * triangles of the tile (the branch must go to the right tile list),
 * stores. The fragment shaders are recognised by their code (shaders.h)
 * and done in C. It checks what bm writes against what bm believes about
 * the V3D, not the V3D itself: that only the Pi can say.
 *
 * Two hidden choices the backend must find with its probe: which byte of
 * the tile buffer is red in BGR565 (emu_red_a) and whether the TMU swaps
 * bytes a and c of a texel (emu_tex_swap).
 *
 * Bus addresses: the memory of the backend comes from an arena
 * (test_aligned_alloc), bus = 0x40000000 + offset in the arena.
 */
#include "v3d_emu.h"
#include "gpu/shaders.h"
#include "gpu/v3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int emu_red_a = 1, emu_tex_swap = 0;
emu_stats_t emu_stats;
char emu_error[256];

/* ---------------------------------------------------------------- arena */

#define ARENA (96u << 20)
static uint8_t *arena;
static size_t arena_used;

void *test_aligned_alloc(size_t align, size_t size)
{
    if (!arena)
        arena = aligned_alloc(4096, ARENA);
    size_t at = (arena_used + align - 1) & ~(align - 1);
    if (at + size > ARENA)
        return NULL;
    arena_used = at + size;
    return arena + at;
}

void test_free(void *p)
{
    (void)p;                            /* the arena is not given back */
}

uint32_t v3d_bus(const void *p)
{
    const uint8_t *b = p;
    if (!arena || b < arena || b >= arena + ARENA) {
        fprintf(stderr, "v3d_emu: address outside the arena: %p\n", p);
        exit(2);
    }
    return 0x40000000u + (uint32_t)(b - arena);
}

static void *ptr(uint32_t bus)
{
    uint32_t off = bus - 0x40000000u;
    if (bus < 0x40000000u || off >= ARENA)
        return NULL;
    return arena + off;
}

/* ---------------------------------------------------------------- v3d.h */

static uint32_t overflow_bus, overflow_size;

int v3d_init(void) { return 0; }
const char *v3d_status(void) { return "emulated"; }
uint32_t v3d_ident(int i) { return i == 0 ? 0x02443356u : 0; }
void v3d_set_overflow(uint32_t bus, uint32_t size) { overflow_bus = bus; overflow_size = size; }

void v3d_dump(char *buf, size_t n)
{
    snprintf(buf, n, "emulator: %s\n", emu_error);
}

static int err(const char *fmt, unsigned a, unsigned b)
{
    snprintf(emu_error, sizeof emu_error, fmt, a, b);
    return -1;
}

/* ---------------------------------------------------------------- binning */

enum { SH_COLOUR, SH_TEX, SH_TEX_ALPHA };

typedef struct { float x, y, z, iw, v[3]; } evert_t;

typedef struct {
    evert_t v[3];
    int shader;
    const uint32_t *params;
    int depth_func, z_update;
    int clip[4];
} eprim_t;

static eprim_t *prims;
static int nprims, cap;
static uint32_t bin_alloc, bin_tsda;
static int bin_tx, bin_ty;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static int shader_of(const uint8_t *code)
{
    if (!memcmp(code, fs_colour, sizeof fs_colour)) return SH_COLOUR;
    if (!memcmp(code, fs_tex_lit, sizeof fs_tex_lit)) return SH_TEX;
    if (!memcmp(code, fs_tex_lit_alpha, sizeof fs_tex_lit_alpha)) return SH_TEX_ALPHA;
    return -1;
}

static int bin(uint32_t start, uint32_t end)
{
    const uint8_t *p = ptr(start), *e = ptr(end);
    if (!p || !e || e < p)
        return err("binning list %08x..%08x outside memory", start, end);
    nprims = 0;
    int cfg_seen = 0, started = 0, flushed = 0, depth_func = -1, z_update = 0, clip[4] = { -1, 0, 0, 0 };
    const uint8_t *rec = NULL;
    while (p < e) {
        uint8_t id = *p++;
        if (flushed && id != 1 && id != 0)
            return err("binning list: packet %u after FLUSH", id, 0);
        switch (id) {
        case 0: p = e; break;                                   /* HALT */
        case 1: break;                                          /* NOP */
        case 4: flushed = 1; break;                             /* FLUSH */
        case 112:                                               /* TILE_BINNING_MODE_CONFIG */
            bin_alloc = rd32(p); bin_tsda = rd32(p + 8);
            bin_tx = p[12]; bin_ty = p[13];
            if (!ptr(bin_alloc) || !ptr(bin_tsda) || (bin_alloc >> 28) != (bin_tsda >> 28))
                return err("binning memory %08x / tile state %08x", bin_alloc, bin_tsda);
            if ((p[14] & 0x04) == 0 || (p[14] >> 3 & 3) != 0)
                return err("binning flags %02x: auto tile state, first blocks of 32", p[14], 0);
            cfg_seen = 1;
            p += 15;
            break;
        case 6:                                                 /* START_TILE_BINNING */
            if (!cfg_seen)
                return err("START_TILE_BINNING before the binning configuration", 0, 0);
            started = 1;
            break;
        case 103: p += 4; break;                                /* VIEWPORT_OFFSET */
        case 102:                                               /* CLIP_WINDOW */
            clip[0] = rd16(p); clip[1] = rd16(p + 2); clip[2] = rd16(p + 4); clip[3] = rd16(p + 6);
            p += 8;
            break;
        case 96:                                                /* CONFIGURATION_BITS */
            if ((p[0] & 3) != 3)
                return err("configuration %02x: both faces expected", p[0], 0);
            depth_func = rd16(p + 1) >> 4 & 7;
            z_update = rd16(p + 1) >> 7 & 1;
            p += 3;
            break;
        case 65:                                                /* NV_SHADER_STATE */
            rec = ptr(rd32(p));
            if (!rec || (rd32(p) & 15))
                return err("shader record %08x not 16-byte aligned", rd32(p), 0);
            p += 4;
            break;
        case 33: {                                              /* VERTEX_ARRAY_PRIMITIVES */
            uint32_t n = rd32(p + 1), first = rd32(p + 5);
            if (!started || !rec || depth_func < 0 || clip[0] < 0)
                return err("primitives before the state (started %u, record %u)", started, rec != NULL);
            if (p[0] != 4 || n % 3 || n > 65535)
                return err("primitives: mode %u, %u vertices", p[0], n);
            int sh = shader_of(ptr(rd32(rec + 4)));
            if (sh < 0)
                return err("unknown shader at %08x", rd32(rec + 4), 0);
            int stride = rec[1], nvary = rec[3];
            if (nvary != 3 || stride != 24 || rec[2] != (sh == SH_COLOUR ? 0 : 2))
                return err("shader record: stride %u, varyings %u", stride, nvary);
            const uint8_t *vb = ptr(rd32(rec + 12));
            const uint32_t *params = sh == SH_COLOUR ? NULL : ptr(rd32(rec + 8));
            if (!vb || (sh != SH_COLOUR && !params))
                return err("vertices %08x, uniforms %08x", rd32(rec + 12), rd32(rec + 8));
            for (uint32_t i = 0; i < n; i += 3) {
                if (nprims == cap) {
                    cap = cap ? cap * 2 : 4096;
                    prims = realloc(prims, (size_t)cap * sizeof *prims);
                }
                eprim_t *pr = &prims[nprims++];
                for (int k = 0; k < 3; k++) {
                    const uint8_t *v = vb + (size_t)(first + i + (uint32_t)k) * (size_t)stride;
                    pr->v[k] = (evert_t){ (int16_t)rd16(v) / 16.0f, (int16_t)rd16(v + 2) / 16.0f, rdf(v + 4),
                                          rdf(v + 8), { rdf(v + 12), rdf(v + 16), rdf(v + 20) } };
                }
                pr->shader = sh;
                pr->params = params;
                pr->depth_func = depth_func;
                pr->z_update = z_update;
                memcpy(pr->clip, clip, sizeof clip);
            }
            emu_stats.prims += n / 3;
            emu_stats.batches++;
            p += 9;
            break;
        }
        default:
            return err("binning list: unknown packet %u", id, 0);
        }
    }
    if (!flushed)
        return err("binning list without FLUSH", 0, 0);
    return 0;
}

/* ---------------------------------------------------------------- rendering */

static uint8_t tcol[64 * 64][4];        /* tile buffer: bytes a b c d */
static uint32_t tz[64 * 64];
static uint32_t clear_col, clear_z;

static void tile_clear(void)
{
    for (int i = 0; i < 64 * 64; i++) {
        memcpy(tcol[i], &clear_col, 4);
        tz[i] = clear_z;
    }
}

static uint16_t to565(const uint8_t *c)
{
    const uint8_t *r = emu_red_a ? &c[0] : &c[2], *b = emu_red_a ? &c[2] : &c[0];
    return (uint16_t)((*r >> 3) << 11 | (c[1] >> 2) << 5 | (*b >> 3));
}

static void from565(uint16_t v, uint8_t *c)
{
    uint32_t r = v >> 11, g = v >> 5 & 63, b = v & 31;
    uint8_t r8 = (uint8_t)(r << 3 | r >> 2), g8 = (uint8_t)(g << 2 | g >> 4), b8 = (uint8_t)(b << 3 | b >> 2);
    c[0] = emu_red_a ? r8 : b8;
    c[1] = g8;
    c[2] = emu_red_a ? b8 : r8;
    c[3] = 255;
}

static uint8_t unit8(float f)
{
    return (uint8_t)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f);
}

static void shade(const eprim_t *pr, const float *va, uint8_t *out, int *discard)
{
    *discard = 0;
    if (pr->shader == SH_COLOUR) {
        out[0] = unit8(va[0]); out[1] = unit8(va[1]); out[2] = unit8(va[2]); out[3] = 255;
        return;
    }
    uint32_t p0 = pr->params[0], p1 = pr->params[1];
    int w = (int)(p1 >> 8 & 2047), h = (int)(p1 >> 20 & 2047);
    if (!w) w = 2048;
    if (!h) h = 2048;
    const uint32_t *tex = ptr(p0 & ~0xFFFu);
    int tx = (int)floorf(va[0] * (float)w), ty = (int)floorf(va[1] * (float)h);
    tx = tx < 0 ? 0 : tx >= w ? w - 1 : tx;                     /* clamp */
    ty = ty < 0 ? 0 : ty >= h ? h - 1 : ty;
    uint32_t t = tex[ty * w + tx];
    if (emu_tex_swap)
        t = (t & 0xFF00FF00u) | (t >> 16 & 0xFF) | (t & 0xFF) << 16;
    uint8_t c[4] = { (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16), (uint8_t)(t >> 24) };
    uint32_t k = unit8(va[2]);
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)((c[i] * k + 127) / 255);              /* v8muld */
    if (pr->shader == SH_TEX_ALPHA && c[3] == 0)
        *discard = 1;
}

static float edge(const evert_t *a, const evert_t *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static void draw_tile(int tx, int ty, int fw, int fh)
{
    for (int n = 0; n < nprims; n++) {
        const eprim_t *pr = &prims[n];
        const evert_t *v = pr->v;
        float area = edge(&v[0], &v[1], v[2].x, v[2].y);
        if (area == 0)
            continue;
        int x0 = tx * 64, y0 = ty * 64, x1 = x0 + 64, y1 = y0 + 64;
        if (x1 > fw) x1 = fw;
        if (y1 > fh) y1 = fh;
        /* only the pixels of the triangle's bounding box (centres at +0.5) */
        float bx0 = fminf(v[0].x, fminf(v[1].x, v[2].x)), bx1 = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
        float by0 = fminf(v[0].y, fminf(v[1].y, v[2].y)), by1 = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
        if ((float)x0 < bx0 - 1) x0 = (int)floorf(bx0 - 1);
        if ((float)y0 < by0 - 1) y0 = (int)floorf(by0 - 1);
        if ((float)x1 > bx1 + 1) x1 = (int)ceilf(bx1 + 1);
        if ((float)y1 > by1 + 1) y1 = (int)ceilf(by1 + 1);
        if (x0 < pr->clip[0]) x0 = pr->clip[0];
        if (y0 < pr->clip[1]) y0 = pr->clip[1];
        if (x1 > pr->clip[0] + pr->clip[2]) x1 = pr->clip[0] + pr->clip[2];
        if (y1 > pr->clip[1] + pr->clip[3]) y1 = pr->clip[1] + pr->clip[3];
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = edge(&v[1], &v[2], px, py) / area, w1 = edge(&v[2], &v[0], px, py) / area,
                      w2 = edge(&v[0], &v[1], px, py) / area;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                float zs = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                float iw = w0 * v[0].iw + w1 * v[1].iw + w2 * v[2].iw;
                float va[3];
                for (int k = 0; k < 3; k++)
                    va[k] = (w0 * v[0].v[k] * v[0].iw + w1 * v[1].v[k] * v[1].iw + w2 * v[2].v[k] * v[2].iw) / iw;
                uint32_t zz = (uint32_t)(zs <= 0 ? 0 : zs >= 1 ? 0xFFFFFF : zs * 16777215.0f);
                int i = (y - ty * 64) * 64 + (x - tx * 64);
                int pass = pr->depth_func == 7 || (pr->depth_func == 1 && zz < tz[i]) ||
                           (pr->depth_func == 3 && zz <= tz[i]);
                if (!pass)
                    continue;
                uint8_t c[4];
                int discard;
                shade(pr, va, c, &discard);
                if (discard)
                    continue;
                memcpy(tcol[i], c, 4);
                if (pr->z_update)
                    tz[i] = zz;
                emu_stats.pixels++;
            }
    }
}

/* a depth buffer in T-format as the emulator lays it out: 4 KiB tiles of
 * 32x32 pixels, a row of tiles after the other (the real order inside
 * differs, the size is the same); NULL if it leaves memory */
static uint32_t toff(int x, int y, int fw)
{
    const int tw = (fw + 31) / 32;
    return (uint32_t)(((y / 32) * tw + x / 32) * 1024 + (y % 32) * 32 + x % 32);
}

static uint32_t *zbuf_at(uint32_t a, int fw, int fh)
{
    const uint32_t size = (uint32_t)((fw + 31) / 32) * (uint32_t)((fh + 31) / 32) * 4096u;
    if ((a & 0xFFF) || !ptr(a) || !ptr(a + size - 1))
        return NULL;
    return ptr(a);
}

static int render(uint32_t start, uint32_t end, int have_bin)
{
    const uint8_t *p = ptr(start), *e = ptr(end);
    if (!p || !e || e < p)
        return err("rendering list %08x..%08x outside memory", start, end);
    uint16_t *fb = NULL;
    int fw = 0, fh = 0, tx = -1, ty = -1, load = 0, zload = 0, eof = 0, have_cfg = 0, tiles = 0;
    int loaded = 0;                     /* a load took place: a store before the next load */
    uint32_t load_addr = 0, zload_addr = 0;
    while (p < e) {
        uint8_t id = *p++;
        if (eof && id != 1)
            return err("rendering list: packet %u after the end of frame", id, 0);
        switch (id) {
        case 1: break;
        case 114:                                               /* CLEAR_COLORS */
            clear_col = rd32(p); clear_z = rd32(p + 8) & 0xFFFFFF;
            p += 13;
            break;
        case 113:                                               /* TILE_RENDERING_MODE_CONFIG */
            fb = ptr(rd32(p)); fw = rd16(p + 4); fh = rd16(p + 6);
            if (!fb || (rd32(p) & 15) || (rd16(p + 8) >> 2 & 3) != 2)
                return err("frame %08x, flags %04x (BGR565 expected)", rd32(p), rd16(p + 8));
            have_cfg = 1;
            tile_clear();
            p += 10;
            break;
        case 29:                                                /* LOAD_TILE_BUFFER_GENERAL */
            if (load || zload || loaded)
                return err("load %04x: a load is pending (tile coordinates and a store first)", rd16(p), 0);
            if (rd16(p) == 0x0201) {
                load = 1; load_addr = rd32(p + 2);
            } else if (rd16(p) == 0x0012) {
                zload = 1; zload_addr = rd32(p + 2);
            } else {
                return err("load %04x: colour (raster BGR565) or depth (T-format) expected", rd16(p), 0);
            }
            p += 6;
            break;
        case 115:                                               /* TILE_COORDINATES */
            tx = p[0]; ty = p[1];
            p += 2;
            if (load) {
                const uint16_t *src = ptr(load_addr);
                if (!src)
                    return err("load from %08x", load_addr, 0);
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        int X = tx * 64 + x, Y = ty * 64 + y;
                        if (X < fw && Y < fh)
                            from565(src[Y * fw + X], tcol[y * 64 + x]);
                    }
                load = 0;
                loaded = 1;
            }
            if (zload) {
                const uint32_t *src = zbuf_at(zload_addr, fw, fh);
                if (!src)
                    return err("depth load from %08x", zload_addr, 0);
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        int X = tx * 64 + x, Y = ty * 64 + y;
                        if (X < fw && Y < fh)
                            tz[y * 64 + x] = src[toff(X, Y, fw)] >> 8;
                    }
                zload = 0;
                loaded = 1;
            }
            break;
        case 28: {                                              /* STORE_TILE_BUFFER_GENERAL */
            uint16_t bits = rd16(p);
            uint32_t a = rd32(p + 2);
            if (load || zload)
                return err("general store %04x with a load pending", bits, 0);
            if ((bits & 0x1FFF) == 0 && a == 0) {               /* a store of nothing */
            } else if ((bits & 0x1FFF) == 0x0012 && !(a & 15)) {   /* depth, T-format */
                uint32_t *dst = zbuf_at(a, fw, fh);
                if (!dst || tx < 0)
                    return err("depth store to %08x", a, 0);
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        int X = tx * 64 + x, Y = ty * 64 + y;
                        if (X < fw && Y < fh)
                            dst[toff(X, Y, fw)] = tz[y * 64 + x] << 8;
                    }
                emu_stats.zstores++;
            } else {
                return err("general store %04x %08x: nothing, or the depth in T-format", bits, a);
            }
            for (int i = 0; i < 64 * 64; i++) {             /* the clears not turned off */
                if (!(bits & 1u << 13))
                    memcpy(tcol[i], &clear_col, 4);
                if (!(bits & 1u << 14))
                    tz[i] = clear_z;
            }
            loaded = 0;
            p += 6;
            break;
        }
        case 17: {                                              /* BRANCH_TO_SUB_LIST */
            uint32_t a = rd32(p);
            if (!have_bin || a != bin_alloc + (uint32_t)(ty * bin_tx + tx) * 32)
                return err("branch to %08x for tile %u", a, (unsigned)(ty * 100 + tx));
            draw_tile(tx, ty, fw, fh);
            p += 4;
            break;
        }
        case 24: case 25:                                       /* STORE_MS_TILE_BUFFER (EOF) */
            if (!have_cfg || tx < 0)
                return err("store before the configuration", 0, 0);
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++) {
                    int X = tx * 64 + x, Y = ty * 64 + y;
                    if (X < fw && Y < fh)
                        fb[Y * fw + X] = to565(tcol[y * 64 + x]);
                }
            tile_clear();
            tiles++;
            loaded = 0;
            eof = id == 25;
            break;
        default:
            return err("rendering list: unknown packet %u", id, 0);
        }
    }
    if (!eof)
        return err("rendering list without end of frame", 0, 0);
    if (tiles != ((fw + 63) / 64) * ((fh + 63) / 64))
        return err("%u tiles stored, frame of %u", (unsigned)tiles, (unsigned)(((fw + 63) / 64) * ((fh + 63) / 64)));
    return 0;
}

int v3d_run(uint32_t bin_start, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end, uint32_t timeout_us,
            uint32_t *bin_us, uint32_t *rnd_us)
{
    (void)timeout_us;
    if (bin_us) *bin_us = 1;
    if (rnd_us) *rnd_us = 1;
    emu_stats.jobs++;
    int have_bin = bin_end != bin_start;
    if (have_bin && bin(bin_start, bin_end) != 0)
        return -1;
    if (!have_bin)
        nprims = 0;
    return render(rnd, rnd_end, have_bin);
}
