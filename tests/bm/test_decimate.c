/*
 * Host test of the polygon reducer (src/bm/decimate.c): planes, a sphere,
 * a textured plane, a two-colour plane, a rigged cylinder, a model record,
 * and the models of the cartridges named (each halved).
 *
 *   test_decimate [CART.bm ...]
 */
#include "bm.h"
#include "decimate.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURED 0x80000000u

static int checks, fails;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

typedef struct {
    dec_mesh_t m;
    int cv, cf;
} builder_t;

static void bld_init(builder_t *b, int nv, int nf)
{
    memset(b, 0, sizeof *b);
    b->cv = nv;
    b->cf = nf;
    b->m.v = calloc((size_t)nv * 3, sizeof(float));
    b->m.bone = calloc((size_t)nv, 1);
    b->m.f = calloc((size_t)nf * 3, sizeof(uint16_t));
    b->m.colour = calloc((size_t)nf, sizeof(uint32_t));
    b->m.uv = calloc((size_t)nf * 6, sizeof(uint16_t));
}

static int bld_vertex(builder_t *b, float x, float y, float z, int bone)
{
    int i = b->m.nv++;
    b->m.v[i * 3] = x;
    b->m.v[i * 3 + 1] = y;
    b->m.v[i * 3 + 2] = z;
    b->m.bone[i] = (uint8_t)bone;
    return i;
}

static void bld_face(builder_t *b, int p, int q, int r, uint32_t colour, const uint16_t *uv)
{
    int f = b->m.nf++;
    b->m.f[f * 3] = (uint16_t)p;
    b->m.f[f * 3 + 1] = (uint16_t)q;
    b->m.f[f * 3 + 2] = (uint16_t)r;
    b->m.colour[f] = colour;
    if (uv)
        memcpy(b->m.uv + f * 6, uv, 12);
}

static void bld_free(builder_t *b)
{
    free(b->m.v);
    free(b->m.bone);
    free(b->m.f);
    free(b->m.colour);
    free(b->m.uv);
}

static void normal(const dec_mesh_t *m, int f, float *n)
{
    const float *a = m->v + m->f[f * 3] * 3, *b = m->v + m->f[f * 3 + 1] * 3, *c = m->v + m->f[f * 3 + 2] * 3;
    float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
}

static int indices_ok(const dec_mesh_t *m)
{
    for (int f = 0; f < m->nf; f++)
        for (int k = 0; k < 3; k++)
            if (m->f[f * 3 + k] >= m->nv)
                return 0;
    return 1;
}

/* a plane of n x n tiles on y = 0, seen from above (clockwise from +y),
 * colour(x, z) per tile, textured if uv */
static void grid(builder_t *b, int n, uint32_t (*colour)(int, int), int textured)
{
    bld_init(b, (n + 1) * (n + 1), n * n * 2);
    for (int z = 0; z <= n; z++)
        for (int x = 0; x <= n; x++)
            bld_vertex(b, (float)x, 0, (float)z, 0);
    for (int z = 0; z < n; z++)
        for (int x = 0; x < n; x++) {
            int a = z * (n + 1) + x, bb = a + 1, c = a + n + 1, d = c + 1;
            uint32_t col = textured ? TEXTURED : colour(x, z);
            /* seen from +y: (x, z) -> (x, -z) on the screen, clockwise there */
            uint16_t uv1[6] = { (uint16_t)(x * 128), (uint16_t)(z * 128), (uint16_t)((x + 1) * 128), (uint16_t)(z * 128),
                                (uint16_t)(x * 128), (uint16_t)((z + 1) * 128) };
            uint16_t uv2[6] = { (uint16_t)((x + 1) * 128), (uint16_t)(z * 128), (uint16_t)((x + 1) * 128),
                                (uint16_t)((z + 1) * 128), (uint16_t)(x * 128), (uint16_t)((z + 1) * 128) };
            bld_face(b, a, bb, c, col, uv1);
            bld_face(b, bb, d, c, col, uv2);
        }
}

static uint32_t one_colour(int x, int z) { (void)x; (void)z; return 0x40A040; }
static uint32_t two_colours(int x, int z) { (void)z; return x < 5 ? 0xC03030 : 0x3030C0; }

static void test_plane(void)
{
    builder_t b;
    grid(&b, 10, one_colour, 0);
    int n = dec_reduce(&b.m, 2, 0);
    char msg[80];
    sprintf(msg, "a flat plane of 200 triangles becomes 2 (got %d)", n);
    check(n == 2 && b.m.nv == 4, msg);
    check(indices_ok(&b.m), "plane: indices");
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < b.m.nv; i++)
        for (int k = 0; k < 3; k++) {
            if (b.m.v[i * 3 + k] < lo[k]) lo[k] = b.m.v[i * 3 + k];
            if (b.m.v[i * 3 + k] > hi[k]) hi[k] = b.m.v[i * 3 + k];
        }
    check(lo[0] == 0 && hi[0] == 10 && lo[2] == 0 && hi[2] == 10 && lo[1] == 0 && hi[1] == 0, "plane: the same square");
    for (int f = 0; f < b.m.nf; f++) {
        float nn[3];
        normal(&b.m, f, nn);
        check(nn[1] < 0 && fabsf(nn[1]) > 1, "plane: faces still face +y (the same side)");
    }
    bld_free(&b);
}

static void test_two_colours(void)
{
    builder_t b;
    grid(&b, 10, two_colours, 0);
    int n = dec_reduce(&b.m, 4, 0);
    char msg[80];
    sprintf(msg, "two colours: 4 triangles (got %d)", n);
    check(n == 4, msg);
    for (int f = 0; f < b.m.nf; f++) {
        float cx = 0;
        for (int k = 0; k < 3; k++)
            cx += b.m.v[b.m.f[f * 3 + k] * 3];
        cx /= 3;
        check((cx < 5) == (b.m.colour[f] == 0xC03030), "two colours: the colour line stays at x = 5");
    }
    bld_free(&b);
}

static void test_textured(void)
{
    builder_t b;
    grid(&b, 10, NULL, 1);
    int n = dec_reduce(&b.m, 2, 0);
    char msg[80];
    sprintf(msg, "textured plane: 2 triangles (got %d)", n);
    check(n == 2, msg);
    int ok = 1;
    for (int f = 0; f < b.m.nf; f++)
        for (int k = 0; k < 3; k++) {
            const float *p = b.m.v + b.m.f[f * 3 + k] * 3;
            int u = b.m.uv[f * 6 + k * 2], v = b.m.uv[f * 6 + k * 2 + 1];
            if (abs(u - (int)(p[0] * 128)) > 1 || abs(v - (int)(p[2] * 128)) > 1)
                ok = 0;
        }
    check(ok, "textured plane: the texture corners follow the positions");
    bld_free(&b);
}

/* a texture seam down the middle: the right half's texture starts again at u = 0 */
static void test_seam(void)
{
    builder_t b;
    grid(&b, 10, NULL, 1);
    for (int f = 0; f < b.m.nf; f++) {
        float cx = 0;
        for (int k = 0; k < 3; k++)
            cx += b.m.v[b.m.f[f * 3 + k] * 3];
        if (cx / 3 > 5)
            for (int k = 0; k < 3; k++)
                b.m.uv[f * 6 + k * 2] = (uint16_t)(b.m.uv[f * 6 + k * 2] - 5 * 128);
    }
    int n = dec_reduce(&b.m, 4, 0);
    char msg[80];
    sprintf(msg, "a seam: 4 triangles (got %d)", n);
    check(n == 4, msg);
    int ok = 1;
    for (int f = 0; f < b.m.nf; f++) {
        float cx = 0;
        for (int k = 0; k < 3; k++)
            cx += b.m.v[b.m.f[f * 3 + k] * 3];
        int right = cx / 3 > 5;
        for (int k = 0; k < 3; k++) {
            const float *p = b.m.v + b.m.f[f * 3 + k] * 3;
            int u = b.m.uv[f * 6 + k * 2];
            if (abs(u - (int)((p[0] - (right ? 5 : 0)) * 128)) > 1)
                ok = 0;
        }
    }
    check(ok, "a seam: each side keeps its own texture");
    bld_free(&b);
}

static void sphere(builder_t *b, int seg, int rings, float r)
{
    bld_init(b, 2 + seg * (rings - 1), seg * 2 * (rings - 1));
    int top = bld_vertex(b, 0, r, 0, 0);
    for (int i = 1; i < rings; i++) {
        float ph = (float)M_PI * (float)i / (float)rings;
        for (int j = 0; j < seg; j++) {
            float th = 2 * (float)M_PI * (float)j / (float)seg;
            bld_vertex(b, r * sinf(ph) * cosf(th), r * cosf(ph), r * sinf(ph) * sinf(th), i < rings / 2 ? 0 : 1);
        }
    }
    int bot = bld_vertex(b, 0, -r, 0, 1);
    for (int j = 0; j < seg; j++) {
        int j2 = (j + 1) % seg;
        bld_face(b, top, 1 + j, 1 + j2, 0xA0A0A0, NULL);        /* outward: clockwise seen from outside */
        for (int i = 1; i < rings - 1; i++) {
            int a = 1 + (i - 1) * seg + j, aa = 1 + (i - 1) * seg + j2, c = a + seg, cc = aa + seg;
            bld_face(b, a, c, aa, 0xA0A0A0, NULL);
            bld_face(b, aa, c, cc, 0xA0A0A0, NULL);
        }
        int last = 1 + (rings - 2) * seg;
        bld_face(b, last + j, bot, last + j2, 0xA0A0A0, NULL);
    }
}

static int outward(const dec_mesh_t *m, int f, float sign)
{
    float n[3], c[3] = { 0, 0, 0 };
    normal(m, f, n);
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < 3; j++)
            c[j] += m->v[m->f[f * 3 + k] * 3 + j] / 3;
    return sign * (n[0] * c[0] + n[1] * c[1] + n[2] * c[2]) > 0;
}

/* every edge on two faces, the other way round */
static int closed(const dec_mesh_t *m)
{
    int ne = m->nf * 3, bad = 0;
    uint32_t *keys = malloc((size_t)ne * sizeof *keys);
    for (int f = 0; f < m->nf; f++)
        for (int k = 0; k < 3; k++)
            keys[f * 3 + k] = (uint32_t)m->f[f * 3 + k] << 16 | m->f[f * 3 + (k + 1) % 3];
    for (int i = 0; i < ne; i++) {
        uint32_t rev = keys[i] << 16 | keys[i] >> 16;
        int found = 0;
        for (int j = 0; j < ne && !found; j++)
            found = keys[j] == rev;
        bad += !found;
    }
    free(keys);
    return bad == 0;
}

static void test_sphere(void)
{
    builder_t b;
    sphere(&b, 24, 12, 1);
    float sign = outward(&b.m, 0, 1) ? 1 : -1;      /* the test's own winding */
    int nf0 = b.m.nf;
    int n = dec_reduce(&b.m, 200, 0);
    char msg[80];
    sprintf(msg, "sphere: %d -> at most 200 triangles (got %d)", nf0, n);
    check(n <= 200 && n >= 100, msg);
    int out = 1;
    for (int f = 0; f < b.m.nf; f++)
        out = out && outward(&b.m, f, sign);
    check(out, "sphere: every face still looks outwards");
    check(closed(&b.m), "sphere: still closed");
    int onr = 1;
    for (int i = 0; i < b.m.nv; i++) {
        const float *p = b.m.v + i * 3;
        onr = onr && fabsf(sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - 1) < 1e-4f;
    }
    check(onr, "sphere: the vertices stay on the sphere (half-edge collapse)");
    int bones = 1;
    for (int i = 0; i < b.m.nv; i++)
        bones = bones && b.m.bone[i] == (b.m.v[i * 3 + 1] < 0.01f);
    check(bones, "sphere: the bones follow the vertices");
    /* a cap on the error: a cube-like count cannot be reached */
    bld_free(&b);
    sphere(&b, 24, 12, 1);
    n = dec_reduce(&b.m, 8, 0.001f);
    sprintf(msg, "sphere with max_err 0.001: stops early, %d triangles", n);
    check(n > 100, msg);
    bld_free(&b);
}

static void test_record(void)
{
    builder_t b;
    sphere(&b, 16, 8, 2);
    size_t len = 24 + (size_t)b.m.nv * 12 + (size_t)b.m.nf * 24;
    uint8_t *rec = calloc(len, 1), *out = malloc(len), *vb = malloc((size_t)b.m.nv), *vb2 = malloc((size_t)b.m.nv);
    memcpy(rec, "ball", 4);
    rec[16] = (uint8_t)b.m.nv;
    rec[17] = (uint8_t)(b.m.nv >> 8);
    rec[18] = (uint8_t)b.m.nf;
    rec[19] = (uint8_t)(b.m.nf >> 8);
    memcpy(rec + 24, b.m.v, (size_t)b.m.nv * 12);
    uint8_t *p = rec + 24 + b.m.nv * 12;
    for (int f = 0; f < b.m.nf; f++, p += 24) {
        for (int k = 0; k < 3; k++)
            memcpy(p + k * 2, &b.m.f[f * 3 + k], 2);
        memcpy(p + 8, &b.m.colour[f], 4);
    }
    memcpy(vb, b.m.bone, (size_t)b.m.nv);
    size_t outlen = 0;
    int n = bm_model_reduce(rec, len, vb, 60, 0, out, &outlen, vb2);
    char msg[80];
    sprintf(msg, "record: %d triangles (asked 60)", n);
    check(n > 0 && n <= 60, msg);
    int nv2 = out[16] | out[17] << 8, nf2 = out[18] | out[19] << 8;
    check(nf2 == n && outlen == 24 + (size_t)nv2 * 12 + (size_t)nf2 * 24 && memcmp(out, "ball", 4) == 0,
          "record: the header and the length");
    int bones = 1;
    for (int i = 0; i < nv2; i++) {
        float y;
        memcpy(&y, out + 24 + i * 12 + 4, 4);
        bones = bones && vb2[i] == (y < 0.01f);
    }
    check(bones, "record: the bones of the vertices kept");
    check(bm_model_reduce(rec, len - 1, NULL, 60, 0, out, &outlen, NULL) == -1, "record: a broken length is refused");
    free(rec);
    free(out);
    free(vb);
    free(vb2);
    bld_free(&b);
}

static void test_cart(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        check(0, path);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)len);
    if (fread(data, 1, (size_t)len, fp) != (size_t)len)
        len = 0;
    fclose(fp);
    bm_cart_t c;
    char err[64];
    if (bm_parse(data, (size_t)len, &c, err, sizeof err) != 0) {
        printf("FAIL %s: %s\n", path, err);
        fails++;
        free(data);
        return;
    }
    for (int i = 0; i < c.models; i++) {
        bm_model_t m;
        bm_mesh_model(c.mesh, c.mesh_size, i, &m);
        const uint8_t *rec = m.verts - 24;
        size_t len_rec = 24 + (size_t)m.nverts * 12 + (size_t)m.nfaces * 24, outlen = 0;
        uint8_t *out = malloc(len_rec);
        int n = bm_model_reduce(rec, len_rec, NULL, m.nfaces / 2, 0, out, &outlen, NULL);
        char msg[160];
        sprintf(msg, "%s %s: %d -> %d triangles (asked %d)", path, m.name, m.nfaces, n, m.nfaces / 2);
        check(n > 0 && n <= m.nfaces, msg);
        printf("  %s\n", msg + strlen(path) + 1);
        dec_mesh_t dm;
        dm.nv = out[16] | out[17] << 8;
        dm.nf = n;
        dm.f = malloc((size_t)n * 6);
        for (int f = 0; f < n; f++)
            for (int k = 0; k < 3; k++)
                dm.f[f * 3 + k] = (uint16_t)(out[24 + dm.nv * 12 + f * 24 + k * 2] | out[24 + dm.nv * 12 + f * 24 + k * 2 + 1] << 8);
        check(indices_ok(&dm), "a cartridge model: indices");
        free(dm.f);
        free(out);
    }
    free(data);
}

int main(int argc, char **argv)
{
    test_plane();
    test_two_colours();
    test_textured();
    test_seam();
    test_sphere();
    test_record();
    for (int i = 1; i < argc; i++)
        test_cart(argv[i]);
    printf("decimate: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
