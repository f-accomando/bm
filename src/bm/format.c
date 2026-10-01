#include "bm.h"

#include <string.h>

#include "lib/crc32.h"

int bm_is_cart(const void *head8)
{
    return memcmp(head8, "BMCART\0\0", 8) == 0 || memcmp(head8, "BM33CART", 8) == 0;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int fail(char *err, size_t n, const char *msg)
{
    if (err && n) {
        strncpy(err, msg, n - 1);
        err[n - 1] = '\0';
    }
    return -1;
}

/* Walks the runs of a SHEET8 section; with `set`, draws the pixels. Returns
 * 0 if the runs give exactly w*h valid indices. */
static int sheet8_walk(const uint8_t *p, uint32_t size, void (*set)(void *, int, int, const uint8_t *),
                       void *ctx)
{
    unsigned w = rd16(p), h = rd16(p + 2), ncol = rd16(p + 4);
    const uint8_t *pal = p + 8, *q = pal + ncol * 4, *end = p + size;
    uint32_t n = (uint32_t)w * h, i = 0;
    while (i < n) {
        if (q >= end)
            return -1;
        unsigned t = *q++, run, lit = t < 128;
        run = lit ? t + 1 : t - 126;
        if (i + run > n || q + (lit ? run : 1) > end)
            return -1;
        for (unsigned k = 0; k < run; k++) {
            unsigned idx = lit ? q[k] : q[0];
            if (idx >= ncol)
                return -1;
            if (set)
                set(ctx, (int)(i % w), (int)(i / w), pal + idx * 4);
            i++;
        }
        q += lit ? run : 1;
    }
    return q == end ? 0 : -1;
}

int bm_sheet8_unpack(const bm_cart_t *c, void (*set)(void *ctx, int x, int y, const uint8_t rgba[4]),
                      void *ctx)
{
    if (!c->sheet8)
        return -1;
    return sheet8_walk(c->sheet8, c->sheet8_size, set, ctx);
}

/* ---------------------------------------------------------------- MESH */

static float rdf32(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

#define MESH_HEAD   8u              /* u16 models, u16 inset, u32 reserved */
#define MODEL_HEAD  (BM_MODEL_NAME + 8u)

int bm_mesh_check(const uint8_t *p, uint32_t size)
{
    if (size < MESH_HEAD)
        return -1;
    unsigned count = rd16(p);
    if (!count || count > BM_MODELS_MAX)
        return -1;
    uint32_t off = MESH_HEAD;
    for (unsigned i = 0; i < count; i++) {
        if (off + MODEL_HEAD > size)
            return -1;
        const uint8_t *h = p + off;
        unsigned nv = rd16(h + BM_MODEL_NAME), nf = rd16(h + BM_MODEL_NAME + 2);
        if (!h[0] || !nv || nv > BM_MODEL_VERTS || !nf || nf > BM_MODEL_FACES)
            return -1;
        uint32_t need = MODEL_HEAD + nv * 12u + nf * (uint32_t)BM_MESH_FACE;
        if (need > size - off)
            return -1;
        const uint8_t *v = h + MODEL_HEAD;
        for (unsigned k = 0; k < nv * 3; k++) {
            float f = rdf32(v + k * 4);
            if (!(f > -1e6f && f < 1e6f))          /* also NaN */
                return -1;
        }
        const uint8_t *fc = v + nv * 12u;
        for (unsigned k = 0; k < nf; k++, fc += BM_MESH_FACE)
            if (rd16(fc) >= nv || rd16(fc + 2) >= nv || rd16(fc + 4) >= nv)
                return -1;
        off += need;
    }
    return off == size ? (int)count : -1;
}

int bm_mesh_model(const uint8_t *p, uint32_t size, int i, bm_model_t *m)
{
    if (!p || size < MESH_HEAD || i < 0 || i >= rd16(p))
        return -1;
    uint32_t off = MESH_HEAD;
    for (int k = 0;; k++) {
        const uint8_t *h = p + off;
        unsigned nv = rd16(h + BM_MODEL_NAME), nf = rd16(h + BM_MODEL_NAME + 2);
        if (k == i) {
            memcpy(m->name, h, BM_MODEL_NAME);
            m->name[BM_MODEL_NAME] = 0;
            m->nverts = (uint16_t)nv;
            m->nfaces = (uint16_t)nf;
            m->verts = h + MODEL_HEAD;
            m->faces = m->verts + nv * 12u;
            return 0;
        }
        off += MODEL_HEAD + nv * 12u + nf * (uint32_t)BM_MESH_FACE;
    }
}

float bm_mesh_inset(const uint8_t *p)
{
    return rd16(p + 2) / 256.0f;
}

void bm_model_vertex(const bm_model_t *m, int i, float xyz[3])
{
    for (int k = 0; k < 3; k++)
        xyz[k] = rdf32(m->verts + i * 12 + k * 4);
}

void bm_model_face(const bm_model_t *m, int f, uint16_t idx[3], uint32_t *colour, float uv[6])
{
    const uint8_t *p = m->faces + f * BM_MESH_FACE;
    for (int k = 0; k < 3; k++)
        idx[k] = rd16(p + k * 2);
    *colour = rd32(p + 8);
    for (int k = 0; k < 6; k++)
        uv[k] = rd16(p + 12 + k * 2) / 8.0f;
}

/* ---------------------------------------------------------------- ANIM */

#define ANIM_HEAD   8u
#define RIG_HEAD    (BM_MODEL_NAME + 8u)
#define CLIP_HEAD   (BM_MODEL_NAME + 8u)

static int fin32(float f) { return f > -1e9f && f < 1e9f; }

/* Checks one rig at p (at most `room` bytes): its size, or 0 if broken. */
static uint32_t rig_check(const uint8_t *p, uint32_t room)
{
    if (room < RIG_HEAD || !p[0])
        return 0;
    unsigned nb = rd16(p + BM_MODEL_NAME), nc = rd16(p + BM_MODEL_NAME + 2), nv = rd16(p + BM_MODEL_NAME + 4);
    if (!nb || nb > BM_BONES_MAX || nc > BM_CLIPS_MAX || !nv || nv > BM_MODEL_VERTS)
        return 0;
    uint32_t off = RIG_HEAD;
    if (off + nb * BM_BONE_SIZE + ((nv + 3) & ~3u) > room)
        return 0;
    for (unsigned i = 0; i < nb; i++, off += BM_BONE_SIZE) {
        const uint8_t *b = p + off;
        int parent = (int16_t)rd16(b + BM_MODEL_NAME);
        if (parent >= (int)i || parent < -1)
            return 0;
        for (int k = 0; k < 6; k++)
            if (!fin32(rdf32(b + 20 + k * 4)))
                return 0;
    }
    for (unsigned i = 0; i < nv; i++)
        if (p[off + i] >= nb)
            return 0;
    off += (nv + 3) & ~3u;
    const uint32_t keysize = 4 + nb * BM_POSE_SIZE;
    for (unsigned c = 0; c < nc; c++) {
        if (off + CLIP_HEAD > room)
            return 0;
        const uint8_t *h = p + off;
        unsigned nk = rd16(h + BM_MODEL_NAME), mode = h[BM_MODEL_NAME + 2];
        float length = rdf32(h + BM_MODEL_NAME + 4);
        if (!nk || nk > BM_KEYS_MAX || mode > 2 || !(length > 0 && length < 1e6f))
            return 0;
        off += CLIP_HEAD;
        if ((uint64_t)off + (uint64_t)nk * keysize > room)
            return 0;
        float prev = 0;
        for (unsigned k = 0; k < nk; k++, off += keysize) {
            float t = rdf32(p + off);
            if (!(t >= prev && t <= length + 1e-4f))
                return 0;
            prev = t;
            for (unsigned i = 0; i < nb * 7; i++)
                if (!fin32(rdf32(p + off + 4 + i * 4)))
                    return 0;
        }
    }
    return off;
}

int bm_anim_check(const uint8_t *p, uint32_t size)
{
    if (size < ANIM_HEAD)
        return -1;
    unsigned count = rd16(p);
    uint32_t off = ANIM_HEAD;
    for (unsigned i = 0; i < count; i++) {
        uint32_t n = rig_check(p + off, size - off);
        if (!n)
            return -1;
        off += n;
    }
    return off == size ? (int)count : -1;
}

int bm_rig_read(const uint8_t *p, uint32_t size, bm_rig_t *r)
{
    uint32_t n = rig_check(p, size);
    if (!n)
        return -1;
    memcpy(r->model, p, BM_MODEL_NAME);
    r->model[BM_MODEL_NAME] = 0;
    r->nbones = rd16(p + BM_MODEL_NAME);
    r->nclips = rd16(p + BM_MODEL_NAME + 2);
    r->nverts = rd16(p + BM_MODEL_NAME + 4);
    r->bones = p + RIG_HEAD;
    r->vbones = r->bones + r->nbones * BM_BONE_SIZE;
    r->clips = r->vbones + ((r->nverts + 3) & ~3u);
    r->size = n;
    return 0;
}

int bm_anim_rig(const uint8_t *p, uint32_t size, const char *model, bm_rig_t *r)
{
    if (!p || size < ANIM_HEAD)
        return -1;
    uint32_t off = ANIM_HEAD;
    for (unsigned i = 0; i < rd16(p); i++) {
        if (bm_rig_read(p + off, size - off, r) != 0)
            return -1;
        if (strcmp(r->model, model) == 0)
            return 0;
        off += r->size;
    }
    return -1;
}

void bm_rig_bone(const bm_rig_t *r, int i, char name[BM_MODEL_NAME + 1], int *parent, float head[3],
                 float tail[3])
{
    const uint8_t *b = r->bones + i * BM_BONE_SIZE;
    if (name) {
        memcpy(name, b, BM_MODEL_NAME);
        name[BM_MODEL_NAME] = 0;
    }
    if (parent)
        *parent = (int16_t)rd16(b + BM_MODEL_NAME);
    for (int k = 0; k < 3; k++) {
        if (head) head[k] = rdf32(b + 20 + k * 4);
        if (tail) tail[k] = rdf32(b + 32 + k * 4);
    }
}

int bm_rig_clip(const bm_rig_t *r, int i, bm_clip_t *c)
{
    if (i < 0 || i >= r->nclips)
        return -1;
    const uint8_t *p = r->clips;
    const uint32_t keysize = 4 + r->nbones * BM_POSE_SIZE;
    for (int k = 0;; k++) {
        unsigned nk = rd16(p + BM_MODEL_NAME);
        if (k == i) {
            memcpy(c->name, p, BM_MODEL_NAME);
            c->name[BM_MODEL_NAME] = 0;
            c->nkeys = (uint16_t)nk;
            c->mode = p[BM_MODEL_NAME + 2];
            c->loop = p[BM_MODEL_NAME + 3] & 1;
            c->length = rdf32(p + BM_MODEL_NAME + 4);
            c->keys = p + CLIP_HEAD;
            return 0;
        }
        p += CLIP_HEAD + nk * keysize;
    }
}

float bm_clip_key(const bm_clip_t *c, int nbones, int k, float (*q)[4], float (*t)[3])
{
    const uint8_t *p = c->keys + (uint32_t)k * (4 + nbones * BM_POSE_SIZE);
    for (int i = 0; q && i < nbones; i++)
        for (int j = 0; j < 4; j++)
            q[i][j] = rdf32(p + 4 + i * BM_POSE_SIZE + j * 4);
    for (int i = 0; t && i < nbones; i++)
        for (int j = 0; j < 3; j++)
            t[i][j] = rdf32(p + 4 + i * BM_POSE_SIZE + 16 + j * 4);
    return rdf32(p);
}

/* ---------------------------------------------------------------- parse */

int bm_parse(const uint8_t *d, size_t len, bm_cart_t *c, char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    if (len < BM_HEADER_SIZE || !bm_is_cart(d))
        return fail(err, errlen, "not a .bm cartridge");
    if (rd16(d + 8) != 1 || rd16(d + 10) != BM_HEADER_SIZE)
        return fail(err, errlen, "unsupported .bm version");
    if (crc32(d + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE)) != rd32(d + 20))
        return fail(err, errlen, "CRC mismatch");

    c->width = rd16(d + 12);
    c->height = rd16(d + 14);
    c->pixel_format = d[16];
    if (!((c->width == 640 && c->height == 360) || (c->width == 320 && c->height == 180)))
        return fail(err, errlen, "resolution must be 640x360 or 320x180");
    if (c->pixel_format != BM_FMT_RGB565)
        return fail(err, errlen, "pixel format not supported (only RGB565)");
    memcpy(c->title, d + 24, 48);
    memcpy(c->author, d + 72, 32);

    unsigned count = d[17];
    if (BM_HEADER_SIZE + (uint64_t)count * 16 > len)
        return fail(err, errlen, "truncated section table");
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = d + BM_HEADER_SIZE + i * 16;
        uint32_t type = rd32(e), off = rd32(e + 4), size = rd32(e + 8);
        if ((uint64_t)off + size > len)
            return fail(err, errlen, "section out of bounds");
        const uint8_t *p = d + off;
        switch (type) {
        case BM_SEC_LUA:
            c->lua = (const char *)p;
            c->lua_size = size;
            break;
        case BM_SEC_SHEET:
            if (size < 4) return fail(err, errlen, "bad sheet");
            c->sheet_w = rd16(p);
            c->sheet_h = rd16(p + 2);
            if (!c->sheet_w || !c->sheet_h || c->sheet_w > BM_SHEET_MAX || c->sheet_h > BM_SHEET_MAX ||
                4 + (uint64_t)c->sheet_w * c->sheet_h * 4 != size)
                return fail(err, errlen, "bad sheet size");
            c->sheet_rgba = p + 4;
            break;
        case BM_SEC_SHEET8: {
            if (size < 12) return fail(err, errlen, "bad sheet");
            unsigned w = rd16(p), h = rd16(p + 2), ncol = rd16(p + 4);
            if (!w || !h || w > BM_SHEET_MAX || h > BM_SHEET_MAX || !ncol || ncol > 256 ||
                8 + ncol * 4 > size)
                return fail(err, errlen, "bad sheet size");
            if (sheet8_walk(p, size, NULL, NULL) != 0)
                return fail(err, errlen, "bad sheet data");
            c->sheet_w = (uint16_t)w;
            c->sheet_h = (uint16_t)h;
            c->sheet8 = p;
            c->sheet8_size = size;
            break;
        }
        case BM_SEC_MAP:
            if (size < 4) return fail(err, errlen, "bad map");
            c->map_w = rd16(p);
            c->map_h = rd16(p + 2);
            if (!c->map_w || !c->map_h || 4 + (uint64_t)c->map_w * c->map_h * 2 != size)
                return fail(err, errlen, "bad map size");
            c->map_cells = p + 4;
            break;
        case BM_SEC_COVER:
            if (size < 4) return fail(err, errlen, "bad cover");
            c->cover_w = rd16(p);
            c->cover_h = rd16(p + 2);
            if (!c->cover_w || !c->cover_h || c->cover_w > 512 || c->cover_h > 512 ||
                4 + (uint64_t)c->cover_w * c->cover_h * 4 != size)
                return fail(err, errlen, "bad cover size");
            c->cover_rgba = p + 4;
            break;
        case BM_SEC_AUDIO:
            if (size >= 16 && memcmp(p, "BMAU", 4) == 0) {
                c->audio = p;
                c->audio_size = size;
                break;
            }
            if (bm_mesh_check(p, size) < 0)         /* MESH of the first bm Studio files? */
                return fail(err, errlen, "bad sound bank");
            /* fall through */
        case BM_SEC_MESH: {
            int n = bm_mesh_check(p, size);
            if (n < 0)
                return fail(err, errlen, "bad 3D models (MESH)");
            c->mesh = p;
            c->mesh_size = size;
            c->models = (uint16_t)n;
            break;
        }
        case BM_SEC_OLD_ANIM:
        case BM_SEC_ANIM:
            if (bm_anim_check(p, size) < 0)
                return fail(err, errlen, "bad skeletons (ANIM)");
            c->anim = p;
            c->anim_size = size;
            break;
        default:
            break;      /* unknown sections are ignored (forward compatible) */
        }
    }
    if (!c->lua)
        return fail(err, errlen, "no Lua section");
    if (c->sheet_rgba && c->sheet8)
        return fail(err, errlen, "two sheets");
    return 0;
}
