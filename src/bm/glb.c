#include "glb.h"
#include "decimate.h"
#include "jpeg.h"
#include "json.h"
#include "png.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURED 0x80000000u
#define MAX_VERTS 4096
#define MAX_FACES 16384

typedef struct {
    float p[3];
    float uv[2];
    uint32_t colour;            /* 0xRRGGBB */
    int has_uv;
} vtx_t;

typedef struct {
    const json_t *js;
    const uint8_t *bin;
    size_t bin_len;
    vtx_t *v;                   /* the corners, three per face */
    int nv, cap;
    int texture_image;          /* the image used by the textured faces, or -1 */
    int any_texture;
    char *err;
    size_t errlen;
} gl_t;

static int fail(gl_t *g, const char *what)
{
    if (g->err && g->errlen) {
        strncpy(g->err, what, g->errlen - 1);
        g->err[g->errlen - 1] = 0;
    }
    return -1;
}

/* ------------------------------------------------------------ accessors */

typedef struct {
    const uint8_t *data;
    int count, comps, ctype, stride, normalized;
} acc_t;

static int accessor(gl_t *g, int index, acc_t *a)
{
    const json_t *acc = json_at(json_get(g->js, "accessors"), index);
    if (!acc)
        return -1;
    const json_t *bv = json_at(json_get(g->js, "bufferViews"), (int)json_num(acc, "bufferView", -1));
    if (!bv)
        return -1;
    const char *type = json_str(acc, "type", "");
    a->comps = !strcmp(type, "SCALAR") ? 1 : !strcmp(type, "VEC2") ? 2 : !strcmp(type, "VEC3") ? 3 :
               !strcmp(type, "VEC4") ? 4 : !strcmp(type, "MAT4") ? 16 : 0;
    a->ctype = (int)json_num(acc, "componentType", 0);
    a->count = (int)json_num(acc, "count", 0);
    a->normalized = json_num(acc, "normalized", 0) != 0;
    int size = a->ctype == 5126 || a->ctype == 5125 ? 4 : a->ctype == 5123 || a->ctype == 5122 ? 2 :
               a->ctype == 5121 || a->ctype == 5120 ? 1 : 0;
    if (!a->comps || !size || a->count < 0)
        return -1;
    size_t off = (size_t)json_num(bv, "byteOffset", 0) + (size_t)json_num(acc, "byteOffset", 0);
    size_t blen = (size_t)json_num(bv, "byteLength", 0);
    a->stride = (int)json_num(bv, "byteStride", 0);
    if (!a->stride)
        a->stride = size * a->comps;
    if (off + (size_t)(a->count ? (a->count - 1) * a->stride + size * a->comps : 0) > g->bin_len ||
        (size_t)json_num(bv, "byteOffset", 0) + blen > g->bin_len)
        return -1;
    a->data = g->bin + off;
    return 0;
}

static float acc_float(const acc_t *a, int i, int k)
{
    const uint8_t *p = a->data + (size_t)i * a->stride;
    switch (a->ctype) {
    case 5126: { float f; memcpy(&f, p + k * 4, 4); return f; }
    case 5125: { uint32_t u; memcpy(&u, p + k * 4, 4); return (float)u; }
    case 5123: { uint16_t u; memcpy(&u, p + k * 2, 2); return a->normalized ? u / 65535.0f : (float)u; }
    case 5122: { int16_t u; memcpy(&u, p + k * 2, 2); return a->normalized ? (u < 0 ? -1.0f : u / 32767.0f) : (float)u; }
    case 5121: return a->normalized ? p[k] / 255.0f : (float)p[k];
    case 5120: { int8_t u = (int8_t)p[k]; return a->normalized ? (u < 0 ? -1.0f : u / 127.0f) : (float)u; }
    }
    return 0;
}

static uint32_t acc_index(const acc_t *a, int i)
{
    const uint8_t *p = a->data + (size_t)i * a->stride;
    if (a->ctype == 5125) { uint32_t u; memcpy(&u, p, 4); return u; }
    if (a->ctype == 5123) { uint16_t u; memcpy(&u, p, 2); return u; }
    return p[0];
}

/* ------------------------------------------------------------ matrices */

static void mat_mul(const float *a, const float *b, float *o)   /* column-major 4x4 */
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(o, t, sizeof t);
}

static void node_matrix(const json_t *n, float *m)
{
    const json_t *mat = json_get(n, "matrix");
    if (mat && mat->count == 16) {
        for (int i = 0; i < 16; i++)
            m[i] = (float)json_num(json_at(mat, i), NULL, 0);
        return;
    }
    float t[3] = { 0, 0, 0 }, r[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };
    const json_t *jt = json_get(n, "translation"), *jr = json_get(n, "rotation"), *js = json_get(n, "scale");
    for (int i = 0; i < 3; i++) {
        if (jt) t[i] = (float)json_num(json_at(jt, i), NULL, 0);
        if (js) s[i] = (float)json_num(json_at(js, i), NULL, 1);
    }
    if (jr)
        for (int i = 0; i < 4; i++)
            r[i] = (float)json_num(json_at(jr, i), NULL, i == 3);
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                   2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                   2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };   /* column-major */
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++)
            m[c * 4 + rr] = R[c * 3 + rr] * s[c];
    m[3] = m[7] = m[11] = 0;
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    m[15] = 1;
}

static float det3(const float *m)
{
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

/* ------------------------------------------------------------ the mesh */

static int push_corner(gl_t *g, const vtx_t *v)
{
    if (g->nv == g->cap) {
        int cap = g->cap ? g->cap * 2 : 4096;
        vtx_t *nv = realloc(g->v, (size_t)cap * sizeof *nv);
        if (!nv)
            return fail(g, "no memory for the mesh");
        g->v = nv;
        g->cap = cap;
    }
    g->v[g->nv++] = *v;
    return 0;
}

/* the image of a material's base colour texture, or -1; the factor too */
static int material_texture(gl_t *g, int mat, float factor[4])
{
    factor[0] = factor[1] = factor[2] = factor[3] = 1;
    const json_t *m = json_at(json_get(g->js, "materials"), mat);
    const json_t *pbr = json_get(m, "pbrMetallicRoughness");
    const json_t *f = json_get(pbr, "baseColorFactor");
    if (f)
        for (int i = 0; i < 4; i++)
            factor[i] = (float)json_num(json_at(f, i), NULL, 1);
    const json_t *bt = json_get(pbr, "baseColorTexture");
    if (!bt)
        return -1;
    const json_t *tex = json_at(json_get(g->js, "textures"), (int)json_num(bt, "index", -1));
    int src = (int)json_num(tex, "source", -1);
    return json_at(json_get(g->js, "images"), src) ? src : -1;
}

static int primitive(gl_t *g, const json_t *prim, const float *m)
{
    if ((int)json_num(prim, "mode", 4) != 4)
        return 0;                               /* lines, points: not for the console */
    const json_t *attr = json_get(prim, "attributes");
    acc_t pos, uv, col, idx;
    if (!attr || accessor(g, (int)json_num(attr, "POSITION", -1), &pos) < 0 || pos.comps != 3)
        return fail(g, "a mesh without positions");
    int has_uv = accessor(g, (int)json_num(attr, "TEXCOORD_0", -1), &uv) == 0 && uv.comps == 2 && uv.count == pos.count;
    int has_col = accessor(g, (int)json_num(attr, "COLOR_0", -1), &col) == 0 && col.comps >= 3 && col.count == pos.count;
    int has_idx = accessor(g, (int)json_num(prim, "indices", -1), &idx) == 0 && idx.comps == 1;
    float factor[4];
    int image = material_texture(g, (int)json_num(prim, "material", -1), factor);
    int textured = image >= 0 && has_uv;
    if (textured) {
        if (g->texture_image < 0)
            g->texture_image = image;
        if (image != g->texture_image)
            textured = 0;                       /* one texture goes on the sheet: the others as colours */
        else
            g->any_texture = 1;
    }
    int n = has_idx ? idx.count : pos.count;
    int mirrored = det3(m) < 0;
    for (int t = 0; t + 2 < n; t += 3) {
        vtx_t c[3];
        for (int k = 0; k < 3; k++) {
            /* the console shows a face clockwise, glTF's front is counter-clockwise, and z
             * changes sign: swap two corners, unless the node mirrors */
            int kk = mirrored ? k : (k == 1 ? 2 : k == 2 ? 1 : 0);
            uint32_t i = has_idx ? acc_index(&idx, t + kk) : (uint32_t)(t + kk);
            if (i >= (uint32_t)pos.count)
                return fail(g, "an index out of the mesh");
            float p[3] = { acc_float(&pos, (int)i, 0), acc_float(&pos, (int)i, 1), acc_float(&pos, (int)i, 2) };
            for (int j = 0; j < 3; j++)
                c[k].p[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
            c[k].has_uv = textured;
            c[k].uv[0] = has_uv ? acc_float(&uv, (int)i, 0) : 0;
            c[k].uv[1] = has_uv ? acc_float(&uv, (int)i, 1) : 0;
            float rgb[3] = { factor[0], factor[1], factor[2] };
            if (has_col)
                for (int j = 0; j < 3; j++)
                    rgb[j] *= acc_float(&col, (int)i, j);
            else if (!textured && image < 0 && !json_get(json_get(json_at(json_get(g->js, "materials"),
                     (int)json_num(prim, "material", -1)), "pbrMetallicRoughness"), "baseColorFactor"))
                rgb[0] = rgb[1] = rgb[2] = 0.6f;    /* no colour at all: grey */
            uint32_t cc = 0;
            for (int j = 0; j < 3; j++) {
                float v = rgb[j] < 0 ? 0 : rgb[j] > 1 ? 1 : rgb[j];
                /* linear -> sRGB, as the sheet's colours are */
                v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
                cc = cc << 8 | (uint32_t)(v * 255 + 0.5f);
            }
            c[k].colour = cc;
        }
        for (int k = 0; k < 3; k++)
            if (push_corner(g, &c[k]) < 0)
                return -1;
    }
    return 0;
}

static int node(gl_t *g, int index, const float *parent, int depth)
{
    const json_t *n = json_at(json_get(g->js, "nodes"), index);
    if (!n || depth > 32)
        return 0;
    float local[16], m[16];
    node_matrix(n, local);
    mat_mul(parent, local, m);
    const json_t *mesh = json_at(json_get(g->js, "meshes"), (int)json_num(n, "mesh", -1));
    if (mesh)
        for (const json_t *p = json_get(mesh, "primitives") ? json_get(mesh, "primitives")->child : NULL; p; p = p->next)
            if (primitive(g, p, m) < 0)
                return -1;
    const json_t *ch = json_get(n, "children");
    for (const json_t *c = ch ? ch->child : NULL; c; c = c->next)
        if (node(g, (int)json_num(c, NULL, -1), m, depth + 1) < 0)
            return -1;
    return 0;
}

/* ----------------------------------------------------------- the texture */

/* the image decoded: RGBA w x h */
static uint8_t *decode_image(gl_t *g, int image, int *w, int *h)
{
    const json_t *im = json_at(json_get(g->js, "images"), image);
    const json_t *bv = json_at(json_get(g->js, "bufferViews"), (int)json_num(im, "bufferView", -1));
    if (!bv)
        return NULL;                            /* a file apart (uri): no texture */
    size_t off = (size_t)json_num(bv, "byteOffset", 0), len = (size_t)json_num(bv, "byteLength", 0);
    if (off + len > g->bin_len)
        return NULL;
    const uint8_t *data = g->bin + off;
    uint8_t *rgba = NULL;
    if (jpeg_is(data, len)) {
        uint8_t *rgb;
        if (jpeg_decode(data, len, &rgb, w, h, NULL, 0) < 0)
            return NULL;
        rgba = malloc((size_t)*w * *h * 4);
        if (rgba)
            for (size_t i = 0; i < (size_t)*w * *h; i++) {
                memcpy(rgba + i * 4, rgb + i * 3, 3);
                rgba[i * 4 + 3] = 255;
            }
        free(rgb);
        return rgba;
    }
    if (png_rgba(data, len, &rgba, w, h) < 0)
        return NULL;
    return rgba;
}

/* w x h RGBA shrunk (or stretched) to side x side, each pixel the mean of its box */
static uint8_t *resize(const uint8_t *src, int w, int h, int side)
{
    uint8_t *out = malloc((size_t)side * side * 4);
    if (!out)
        return NULL;
    for (int y = 0; y < side; y++) {
        int y0 = y * h / side, y1 = (y + 1) * h / side;
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < side; x++) {
            int x0 = x * w / side, x1 = (x + 1) * w / side;
            if (x1 <= x0)
                x1 = x0 + 1;
            uint32_t sum[4] = { 0, 0, 0, 0 }, n = 0;
            for (int yy = y0; yy < y1 && yy < h; yy++)
                for (int xx = x0; xx < x1 && xx < w; xx++, n++)
                    for (int k = 0; k < 4; k++)
                        sum[k] += src[((size_t)yy * w + xx) * 4 + k];
            for (int k = 0; k < 4; k++)
                out[((size_t)y * side + x) * 4 + k] = (uint8_t)(n ? sum[k] / n : 0);
        }
    }
    return out;
}

static uint32_t sample(const uint8_t *rgba, int w, int h, float u, float v)
{
    int x = (int)(u * w), y = (int)(v * h);
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

/* ------------------------------------------------------------ the record */

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

static uint8_t *record(const char *name, const dec_mesh_t *m, const uint32_t *colours, size_t *len)
{
    *len = 24 + (size_t)m->nv * 12 + (size_t)m->nf * 24;
    uint8_t *out = calloc(*len, 1);
    if (!out)
        return NULL;
    strncpy((char *)out, name, 15);
    wr16(out + 16, (uint32_t)m->nv);
    wr16(out + 18, (uint32_t)m->nf);
    uint8_t *o = out + 24;
    for (int i = 0; i < m->nv * 3; i++, o += 4) {
        uint32_t u;
        memcpy(&u, &m->v[i], 4);
        wr32(o, u);
    }
    for (int f = 0; f < m->nf; f++, o += 24) {
        for (int k = 0; k < 3; k++)
            wr16(o + k * 2, m->f[f * 3 + k]);
        wr32(o + 8, colours ? colours[f] : m->colour[f]);
        if (!colours)
            for (int k = 0; k < 6; k++)
                wr16(o + 12 + k * 2, m->uv[f * 6 + k]);
    }
    return out;
}

void glb_model_free(glb_model_t *m)
{
    free(m->record);
    free(m->flat);
    free(m->texture);
    memset(m, 0, sizeof *m);
}

int glb_pack(const char *name, const dec_mesh_t *m, const uint8_t *tex, int tw, int th, int side, glb_model_t *out)
{
    memset(out, 0, sizeof *out);
    out->textured = tex != NULL;
    if (tex) {
        /* the texture on the sheet; the flat twin with the colours under each face */
        out->texture = resize(tex, tw, th, side);
        uint32_t *flat_colours = malloc((size_t)m->nf * sizeof *flat_colours);
        if (!out->texture || !flat_colours) {
            free(flat_colours);
            glb_model_free(out);
            return -1;
        }
        for (int f = 0; f < m->nf; f++) {
            if (m->colour[f] != TEXTURED) {
                flat_colours[f] = m->colour[f];
                continue;
            }
            float u = 0, v = 0;
            for (int k = 0; k < 3; k++) {
                u += m->uv[f * 6 + k * 2] / (8.0f * side) / 3;
                v += m->uv[f * 6 + k * 2 + 1] / (8.0f * side) / 3;
            }
            flat_colours[f] = sample(tex, tw, th, u, v);
        }
        out->flat = record(name, m, flat_colours, &out->flat_len);
        free(flat_colours);
    }
    out->record = record(name, m, NULL, &out->record_len);
    if (!out->record || (tex && !out->flat)) {
        glb_model_free(out);
        return -1;
    }
    out->nv = m->nv;
    out->nf = m->nf;
    return 0;
}

static uint32_t hash_pos(const float *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 3; i++) {
        uint32_t b;
        memcpy(&b, &p[i], 4);
        h = (h ^ b) * 16777619u;
    }
    return h;
}

int glb_to_model(const uint8_t *glb, size_t len, const char *name, const glb_opts_t *opts, glb_model_t *out,
                 char *err, size_t errlen)
{
    gl_t g;
    memset(&g, 0, sizeof g);
    g.err = err;
    g.errlen = errlen;
    g.texture_image = -1;
    memset(out, 0, sizeof *out);
    if (err && errlen)
        err[0] = 0;
    uint32_t magic = len >= 12 ? glb[0] | glb[1] << 8 | glb[2] << 16 | (uint32_t)glb[3] << 24 : 0;
    if (magic != 0x46546C67u)
        return fail(&g, "not a .glb file");
    const char *jstext = NULL;
    size_t jslen = 0;
    for (size_t p = 12; p + 8 <= len;) {
        uint32_t clen = glb[p] | glb[p + 1] << 8 | glb[p + 2] << 16 | (uint32_t)glb[p + 3] << 24;
        uint32_t ctype = glb[p + 4] | glb[p + 5] << 8 | glb[p + 6] << 16 | (uint32_t)glb[p + 7] << 24;
        if (clen > len - p - 8)
            return fail(&g, "a broken .glb chunk");
        if (ctype == 0x4E4F534Au) {
            jstext = (const char *)glb + p + 8;
            jslen = clen;
        } else if (ctype == 0x004E4942u) {
            g.bin = glb + p + 8;
            g.bin_len = clen;
        }
        p += 8 + clen;
    }
    if (!jstext)
        return fail(&g, "a .glb without its JSON");
    char jerr[64];
    json_t *js = json_parse(jstext, jslen, jerr, sizeof jerr);
    if (!js)
        return fail(&g, "broken JSON in the .glb");
    g.js = js;
    int ret = -1;
    dec_mesh_t m;
    memset(&m, 0, sizeof m);
    uint8_t *tex = NULL;
    int tw = 0, th = 0;
    /* the scene's nodes (or every node without a parent) */
    static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const json_t *scene = json_at(json_get(js, "scenes"), (int)json_num(js, "scene", 0));
    const json_t *roots = json_get(scene, "nodes");
    if (roots) {
        for (const json_t *r = roots->child; r; r = r->next)
            if (node(&g, (int)json_num(r, NULL, -1), I, 0) < 0)
                goto out;
    } else {
        const json_t *nodes = json_get(js, "nodes");
        int n = nodes ? nodes->count : 0;
        uint8_t *child = calloc((size_t)n + 1, 1);
        for (const json_t *nd = nodes ? nodes->child : NULL; nd; nd = nd->next) {
            const json_t *ch = json_get(nd, "children");
            for (const json_t *c = ch ? ch->child : NULL; c; c = c->next) {
                int i = (int)json_num(c, NULL, -1);
                if (i >= 0 && i < n && child)
                    child[i] = 1;
            }
        }
        for (int i = 0; i < n; i++)
            if (!(child && child[i]) && node(&g, i, I, 0) < 0) {
                free(child);
                goto out;
            }
        free(child);
    }
    if (g.nv < 3) {
        fail(&g, "no triangles in the .glb");
        goto out;
    }
    /* the frame: feet at y = 0, the height asked, z towards the viewer */
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < g.nv; i++)
        for (int k = 0; k < 3; k++) {
            if (g.v[i].p[k] < lo[k]) lo[k] = g.v[i].p[k];
            if (g.v[i].p[k] > hi[k]) hi[k] = g.v[i].p[k];
        }
    float s = opts->height > 0 && hi[1] - lo[1] > 1e-9f ? opts->height / (hi[1] - lo[1]) : 1;
    float cx = (lo[0] + hi[0]) / 2, cz = (lo[2] + hi[2]) / 2;
    for (int i = 0; i < g.nv; i++) {
        float *p = g.v[i].p;
        p[0] = (p[0] - cx) * s;
        p[1] = (p[1] - lo[1]) * s;
        p[2] = -(p[2] - cz) * s;
        for (int k = 0; k < 3; k++)
            p[k] = roundf(p[k] * 100000) / 100000;
    }
    /* the positions welded, the faces with their own texture corners */
    int nf = g.nv / 3;
    m.nf = nf;
    m.v = malloc((size_t)g.nv * 3 * sizeof *m.v);
    m.f = malloc((size_t)nf * 3 * sizeof *m.f);
    m.colour = malloc((size_t)nf * sizeof *m.colour);
    m.uv = calloc((size_t)nf * 6, sizeof *m.uv);
    int cap = 1;
    while (cap < g.nv * 2)
        cap <<= 1;
    int *table = malloc((size_t)cap * sizeof *table);
    if (!m.v || !m.f || !m.colour || !m.uv || !table) {
        free(table);
        fail(&g, "no memory for the model");
        goto out;
    }
    for (int i = 0; i < cap; i++)
        table[i] = -1;
    m.nv = 0;
    int side = opts->sheet > 0 ? opts->sheet : 256;
    for (int i = 0; i < g.nv; i++) {
        const vtx_t *v = &g.v[i];
        uint32_t h = hash_pos(v->p) & (uint32_t)(cap - 1);
        int id = -1;
        while (table[h] >= 0) {
            if (memcmp(m.v + table[h] * 3, v->p, 12) == 0) {
                id = table[h];
                break;
            }
            h = (h + 1) & (uint32_t)(cap - 1);
        }
        if (id < 0) {
            id = m.nv++;
            table[h] = id;
            memcpy(m.v + id * 3, v->p, 12);
        }
        if (id >= 65535) {
            free(table);
            fail(&g, "more than 65535 vertices");
            goto out;
        }
        m.f[i] = (uint16_t)id;
        int f = i / 3, k = i % 3;
        if (k == 0)
            m.colour[f] = v->has_uv ? TEXTURED : v->colour;
        if (v->has_uv) {
            float u = v->uv[0] < 0 ? 0 : v->uv[0] > 1 ? 1 : v->uv[0];
            float w = v->uv[1] < 0 ? 0 : v->uv[1] > 1 ? 1 : v->uv[1];
            m.uv[f * 6 + k * 2] = (uint16_t)(u * side * 8 + 0.5f);
            m.uv[f * 6 + k * 2 + 1] = (uint16_t)(w * side * 8 + 0.5f);
        }
    }
    free(table);
    /* the texture, decoded now: the flat colours come from it */
    if (g.any_texture)
        tex = decode_image(&g, g.texture_image, &tw, &th);
    if (!tex && g.any_texture) {
        /* the picture could not be read: the faces go grey */
        for (int f = 0; f < nf; f++)
            if (m.colour[f] == TEXTURED)
                m.colour[f] = 0x8A8A9A;
        g.any_texture = 0;
    }
    /* fewer triangles: the reducer; the format's limits in any case */
    int target = opts->max_faces > 0 && opts->max_faces < nf ? opts->max_faces : 0;
    if (!target && (nf > MAX_FACES || m.nv > MAX_VERTS))
        target = MAX_FACES < nf ? MAX_FACES : nf;
    while (target && target >= 4) {
        if (dec_reduce(&m, target, 0) < 0) {
            fail(&g, "no memory for the reducer");
            goto out;
        }
        if (m.nv <= MAX_VERTS)
            break;
        target /= 2;                            /* still too many vertices: fewer faces */
    }
    if (m.nv > MAX_VERTS || m.nf > MAX_FACES) {
        fail(&g, "too many vertices for a model (4096)");
        goto out;
    }
    if (m.nf < 1) {
        fail(&g, "nothing left of the model");
        goto out;
    }
    if (glb_pack(name, &m, g.any_texture ? tex : NULL, tw, th, side, out) < 0) {
        fail(&g, "no memory for the model");
        goto out;
    }
    ret = 0;
out:
    if (ret < 0)
        glb_model_free(out);
    free(tex);
    free(m.v);
    free(m.f);
    free(m.colour);
    free(m.uv);
    free(g.v);
    json_free(js);
    return ret;
}
