#include "bm.h"

#include <stdlib.h>
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

/* the literal indices waiting, in runs of at most 128 */
static uint8_t *sheet8_flush(uint8_t *q, const uint8_t *idx, uint32_t at, uint32_t lit)
{
    while (lit) {
        uint32_t k = lit > 128 ? 128 : lit;
        *q++ = (uint8_t)(k - 1);
        memcpy(q, idx + at, k);
        q += k;
        at += k;
        lit -= k;
    }
    return q;
}

uint8_t *bm_sheet8_pack(int w, int h, const uint8_t *pal_rgba, int ncol, const uint8_t *idx, size_t *outlen)
{
    uint32_t n = (uint32_t)w * h;
    /* at worst every 128 indices need one more byte */
    uint8_t *out = malloc(8 + (size_t)ncol * 4 + n + n / 128 + 2);
    if (!out)
        return NULL;
    out[0] = (uint8_t)w; out[1] = (uint8_t)(w >> 8);
    out[2] = (uint8_t)h; out[3] = (uint8_t)(h >> 8);
    out[4] = (uint8_t)ncol; out[5] = (uint8_t)(ncol >> 8);
    out[6] = out[7] = 0;
    memcpy(out + 8, pal_rgba, (size_t)ncol * 4);
    uint8_t *q = out + 8 + ncol * 4;
    uint32_t i = 0, lit_at = 0, lit = 0;
    while (i < n) {
        uint32_t j = i;
        while (j < n && j - i < 129 && idx[j] == idx[i])
            j++;
        if (j - i >= 3) {                           /* a run: 3 to 129 times the same index */
            q = sheet8_flush(q, idx, lit_at, lit);
            lit = 0;
            *q++ = (uint8_t)(j - i + 126);
            *q++ = idx[i];
            i = j;
        } else {
            if (!lit)
                lit_at = i;
            lit++;
            i++;
        }
    }
    q = sheet8_flush(q, idx, lit_at, lit);
    *outlen = (size_t)(q - out);
    return out;
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
            m->flags = rd32(h + BM_MODEL_NAME + 4);
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

void bm_model_face_light(const bm_model_t *m, int f, uint8_t rgb[9])
{
    const uint8_t *face = m->faces + f * BM_MESH_FACE;
    if (rd32(face + 8) & 0x80000000u) {
        /* textured: one light in the reserved u16, RGB 5-6-5 (top = 2) */
        unsigned c = rd16(face + 6);
        unsigned r = (c >> 11) * 256 / 31, g = (c >> 5 & 63) * 256 / 63, b = (c & 31) * 256 / 31;
        for (int k = 0; k < 3; k++) {
            rgb[k * 3] = (uint8_t)(r > 255 ? 255 : r);
            rgb[k * 3 + 1] = (uint8_t)(g > 255 ? 255 : g);
            rgb[k * 3 + 2] = (uint8_t)(b > 255 ? 255 : b);
        }
        return;
    }
    const uint8_t *p = face + 12;
    for (int k = 0; k < 3; k++) {
        rgb[k * 3] = p[k * 4];              /* u: R, G */
        rgb[k * 3 + 1] = p[k * 4 + 1];
        rgb[k * 3 + 2] = p[k * 4 + 2];      /* v: B */
    }
}

/* ---------------------------------------------------------------- ANIM */

#define ANIM_HEAD   8u
#define RIG_HEAD    (BM_MODEL_NAME + 8u)
#define CLIP_HEAD   (BM_MODEL_NAME + 8u)

static int fin32(float f) { return f > -1e9f && f < 1e9f; }

/* Checks one rig at p (at most `room` bytes): its size, or 0 if broken.
 * deep = 0: only the sizes (the numbers were checked with the section). */
static uint32_t rig_walk(const uint8_t *p, uint32_t room, int deep)
{
    if (room < RIG_HEAD || !p[0])
        return 0;
    unsigned nb = rd16(p + BM_MODEL_NAME), nc = rd16(p + BM_MODEL_NAME + 2), nv = rd16(p + BM_MODEL_NAME + 4);
    if (!nb || nb > BM_BONES_MAX || nc > BM_CLIPS_MAX || !nv || nv > BM_MODEL_VERTS)
        return 0;
    uint32_t off = RIG_HEAD;
    if (off + nb * BM_BONE_SIZE + ((nv + 3) & ~3u) > room)
        return 0;
    for (unsigned i = 0; deep && i < nb; i++, off += BM_BONE_SIZE) {
        const uint8_t *b = p + off;
        int parent = (int16_t)rd16(b + BM_MODEL_NAME);
        if (parent >= (int)i || parent < -1)
            return 0;
        for (int k = 0; k < 6; k++)
            if (!fin32(rdf32(b + 20 + k * 4)))
                return 0;
    }
    if (!deep)
        off += nb * BM_BONE_SIZE;
    for (unsigned i = 0; deep && i < nv; i++)
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
        if (!deep) {
            off += nk * keysize;
            continue;
        }
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

static uint32_t rig_check(const uint8_t *p, uint32_t room)
{
    return rig_walk(p, room, 1);
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
    /* the rigs before it only skipped (their sizes): every model() of a
     * hero would otherwise check every key of every animation again */
    uint32_t off = ANIM_HEAD;
    for (unsigned i = 0; i < rd16(p); i++) {
        const size_t L = strlen(model);
        if (size - off >= RIG_HEAD && L <= BM_MODEL_NAME && memcmp(p + off, model, L) == 0 &&
            (L == BM_MODEL_NAME || p[off + L] == 0))
            return bm_rig_read(p + off, size - off, r);
        uint32_t n = rig_walk(p + off, size - off, 0);
        if (!n)
            return -1;
        off += n;
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

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

uint8_t *bm_rewrite(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                    const char *title, const char *author, int width, size_t *outlen)
{
    return bm_rewrite_with(old, oldlen, lua, lua_len, title, author, width, NULL, 0, outlen);
}

uint8_t *bm_rewrite_with(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                         const char *title, const char *author, int width, const bm_put_t *put, int nput,
                         size_t *outlen)
{
    (void)oldlen;
    enum { MAXSEC = 32, MAXPUT = 8 };
    uint32_t type[MAXSEC], size[MAXSEC];
    const uint8_t *src[MAXSEC];
    int used[MAXPUT] = { 0 }, put_mesh = 0, put_anim = 0, put_sheet = -1;
    if (nput > MAXPUT)
        nput = MAXPUT;
    for (int k = 0; k < nput; k++) {
        put_mesh |= put[k].type == BM_SEC_MESH;
        put_anim |= put[k].type == BM_SEC_ANIM;
        if (put[k].type == BM_SEC_SHEET || put[k].type == BM_SEC_SHEET8)
            put_sheet = k;
    }
    unsigned n = 0, have_lua = 0, count = old ? old[17] : 0;
    for (unsigned i = 0; i < count && n < MAXSEC; i++) {
        const uint8_t *e = old + BM_HEADER_SIZE + i * 16;
        type[n] = rd32(e);
        src[n] = old + rd32(e + 4);
        size[n] = rd32(e + 8);
        if (type[n] == BM_SEC_LUA) {
            if (have_lua++) continue;           /* one code section */
            if (lua) {
                src[n] = (const uint8_t *)lua;
                size[n] = (uint32_t)lua_len;
            }
        } else if ((put_mesh && type[n] == BM_SEC_AUDIO && (size[n] < 4 || memcmp(src[n], "BMAU", 4) != 0)) ||
                   (put_anim && type[n] == BM_SEC_OLD_ANIM)) {
            continue;                           /* the first bm Studio files: replaced */
        } else if (put_sheet >= 0 && (type[n] == BM_SEC_SHEET || type[n] == BM_SEC_SHEET8)) {
            if (used[put_sheet]++ || !put[put_sheet].data)
                continue;                       /* one sheet, in the place of the first */
            type[n] = put[put_sheet].type;
            src[n] = put[put_sheet].data;
            size[n] = put[put_sheet].size;
        } else {
            int k = 0;
            while (k < nput && put[k].type != type[n])
                k++;
            if (k < nput) {
                if (used[k]++ || !put[k].data)
                    continue;                   /* taken away, or put once */
                src[n] = put[k].data;
                size[n] = put[k].size;
            }
        }
        n++;
    }
    for (int k = 0; k < nput && n < MAXSEC; k++) {
        if (used[k] || !put[k].data)
            continue;
        type[n] = put[k].type;
        src[n] = put[k].data;
        size[n] = put[k].size;
        n++;
    }
    if (!have_lua && lua && n < MAXSEC) {
        type[n] = BM_SEC_LUA;
        src[n] = (const uint8_t *)lua;
        size[n] = (uint32_t)lua_len;
        n++;
    }
    size_t total = BM_HEADER_SIZE + (size_t)n * 16;
    for (unsigned i = 0; i < n; i++)
        total += (size[i] + 3) & ~3u;
    uint8_t *buf = calloc(total, 1);
    if (!buf)
        return NULL;
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + n * 16;
    for (unsigned i = 0; i < n; i++, tab += 16) {
        wr32(tab, type[i]);
        wr32(tab + 4, (uint32_t)(p - buf));
        wr32(tab + 8, size[i]);
        memcpy(p, src[i], size[i]);
        p += (size[i] + 3) & ~3u;
    }
    memcpy(buf, "BMCART\0\0", 8);
    wr16(buf + 8, 1);
    wr16(buf + 10, BM_HEADER_SIZE);
    wr16(buf + 12, width == 320 ? 320 : 640);
    wr16(buf + 14, width == 320 ? 180 : 360);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)n;
    strncpy((char *)buf + 24, title ? title : "", 47);
    strncpy((char *)buf + 72, author ? author : "", 31);
    wr32(buf + 20, crc32(buf + BM_HEADER_SIZE, (uint32_t)(total - BM_HEADER_SIZE)));
    *outlen = total;
    return buf;
}
