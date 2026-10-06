/*
 * The 3D recipes of the assistant drawn on the PC (M30): a contact sheet
 * of every recipe, or one recipe large from a few angles and in the poses
 * of its animations, as PPM files, to look at them (make test-ai keeps the
 * checks in test_ai.c). A small software rasterizer: z-buffer, one light,
 * the faces that show clockwise (as r3d.c), the skinning of animate().
 *
 *   meshview sheet OUT.ppm                    every recipe, seeds 1-3
 *   meshview one RECIPE OUT.ppm [seed] [words] four views and the clips
 *   meshview script IN.txt OUT.ppm            a model in the part language, the same
 *   meshview json IN.txt OUT.json             the model of a script as JSON (tools/img2mesh.py)
 *   meshview json RECIPE OUT.json             the same for a recipe
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai/mesh.h"

typedef struct {
    int w, h;
    uint8_t *px;
    float *z;
} img_t;

static img_t img_new(int w, int h, uint32_t bg)
{
    img_t i = { w, h, malloc((size_t)w * h * 3), malloc((size_t)w * h * sizeof(float)) };
    for (int k = 0; k < w * h; k++) {
        i.px[k * 3] = (uint8_t)(bg >> 16);
        i.px[k * 3 + 1] = (uint8_t)(bg >> 8);
        i.px[k * 3 + 2] = (uint8_t)bg;
        i.z[k] = 1e9f;
    }
    return i;
}

/* ---------------------------------------------------------------- skinning (as animate()) */

typedef float mat_t[12];

static void qmat(const float *q, float *r)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    r[0] = 1 - 2 * (y * y + z * z); r[1] = 2 * (x * y - z * w);     r[2] = 2 * (x * z + y * w);
    r[3] = 2 * (x * y + z * w);     r[4] = 1 - 2 * (x * x + z * z); r[5] = 2 * (y * z - x * w);
    r[6] = 2 * (x * z - y * w);     r[7] = 2 * (y * z + x * w);     r[8] = 1 - 2 * (x * x + y * y);
}

static void mmul(const float *a, const float *b, float *o)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++)
            o[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
        o[r * 4 + 3] = a[r * 4] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
    }
}

static void slerp(const float *a, const float *b, float u, float *o)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float bb[4] = { b[0], b[1], b[2], b[3] };
    if (d < 0) { d = -d; for (int i = 0; i < 4; i++) bb[i] = -bb[i]; }
    float k0, k1;
    if (d > 0.9995f) { k0 = 1 - u; k1 = u; }
    else { float th = acosf(d), s = sinf(th); k0 = sinf((1 - u) * th) / s; k1 = sinf(u * th) / s; }
    float l = 0;
    for (int i = 0; i < 4; i++) { o[i] = a[i] * k0 + bb[i] * k1; l += o[i] * o[i]; }
    l = sqrtf(l);
    for (int i = 0; i < 4; i++) o[i] /= l;
}

/* the matrices of every bone for clip k at time t (identity without a rig) */
static void pose_matrices(const mesh_model_t *m, int k, float t, mat_t *out)
{
    mesh_pose_t pose[MESH_MAX_BONES];
    for (int i = 0; i < m->nbones; i++) {
        memset(&pose[i], 0, sizeof pose[i]);
        pose[i].q[3] = 1;
    }
    if (k >= 0 && k < m->nclips && m->clips[k].nkeys > 0) {
        const mesh_clip_t *c = &m->clips[k];
        float L = c->length > 0 ? c->length : 1;
        if (c->loop) t = fmodf(t, L);
        const mesh_key_t *a = &c->keys[c->nkeys - 1], *b = &c->keys[0];
        float ta = a->t - L, tb = b->t;
        for (int i = 0; i + 1 < c->nkeys; i++)
            if (c->keys[i].t <= t && t < c->keys[i + 1].t) { a = &c->keys[i]; b = &c->keys[i + 1]; ta = a->t; tb = b->t; }
        if (t >= c->keys[c->nkeys - 1].t) { a = &c->keys[c->nkeys - 1]; b = &c->keys[0]; ta = a->t; tb = b->t + L; }
        float u = tb > ta ? (t - ta) / (tb - ta) : 0;
        u = u * u * (3 - 2 * u);
        for (int i = 0; i < m->nbones; i++) {
            slerp(a->pose[i].q, b->pose[i].q, u, pose[i].q);
            for (int j = 0; j < 3; j++) pose[i].t[j] = a->pose[i].t[j] + (b->pose[i].t[j] - a->pose[i].t[j]) * u;
        }
    }
    for (int i = 0; i < m->nbones; i++) {
        const mesh_bone_t *b = &m->bones[i];
        float r[9], local[12];
        qmat(pose[i].q, r);
        const float *h = b->head;
        float c[3] = { h[0] + pose[i].t[0], h[1] + pose[i].t[1], h[2] + pose[i].t[2] };
        for (int row = 0; row < 3; row++) {
            local[row * 4] = r[row * 3]; local[row * 4 + 1] = r[row * 3 + 1]; local[row * 4 + 2] = r[row * 3 + 2];
            local[row * 4 + 3] = c[row] - (r[row * 3] * h[0] + r[row * 3 + 1] * h[1] + r[row * 3 + 2] * h[2]);
        }
        if (b->parent >= 0) mmul(out[b->parent], local, out[i]);
        else memcpy(out[i], local, sizeof local);
    }
    if (m->nbones == 0) {
        static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        memcpy(out[0], id, sizeof id);
    }
}

/* ---------------------------------------------------------------- drawing */

typedef struct { float x, y, z; } v3;

static void tri(img_t *im, const float *a, const float *b, const float *c, uint32_t col)
{
    /* a, b, c: screen x, y and depth; flat colour */
    float minx = fminf(a[0], fminf(b[0], c[0])), maxx = fmaxf(a[0], fmaxf(b[0], c[0]));
    float miny = fminf(a[1], fminf(b[1], c[1])), maxy = fmaxf(a[1], fmaxf(b[1], c[1]));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= im->w) x1 = im->w - 1;
    if (y1 >= im->h) y1 = im->h - 1;
    float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (fabsf(area) < 1e-6f) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
            float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a[2] + w1 * b[2] + w2 * c[2];
            int k = y * im->w + x;
            if (z >= im->z[k]) continue;
            im->z[k] = z;
            im->px[k * 3] = (uint8_t)(col >> 16);
            im->px[k * 3 + 1] = (uint8_t)(col >> 8);
            im->px[k * 3 + 2] = (uint8_t)col;
        }
}

/* the model in the box (x, y, w, h) of the image, seen from yaw (around y)
 * at a 3/4 height, in the pose of clip k at time t */
static void draw_model(img_t *im, const mesh_model_t *m, int x, int y, int w, int h, float yaw, int k, float t)
{
    mat_t mats[MESH_MAX_BONES + 1];
    pose_matrices(m, k, t, mats);
    /* the bounds at rest, to frame it */
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < m->nfaces; i++)
        for (int j = 0; j < m->faces[i].n; j++)
            for (int a = 0; a < 3; a++) {
                lo[a] = fminf(lo[a], m->faces[i].p[j][a]);
                hi[a] = fmaxf(hi[a], m->faces[i].p[j][a]);
            }
    if (m->nfaces == 0) return;
    float cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2, cz = (lo[2] + hi[2]) / 2;
    float size = fmaxf(hi[0] - lo[0], fmaxf(hi[1] - lo[1], hi[2] - lo[2]));
    float dist = size * 2.2f + 0.5f;
    float pitch = -0.45f;
    float cyaw = cosf(yaw), syaw = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    /* camera at distance, looking at the centre; the light from the top left front */
    float lx = -0.5f, ly = 0.8f, lz = -0.6f;
    float ll = sqrtf(lx * lx + ly * ly + lz * lz);
    lx /= ll; ly /= ll; lz /= ll;
    float f = (float)h * 1.1f;
    for (int i = 0; i < m->nfaces; i++) {
        const mesh_face_t *fc = &m->faces[i];
        float p[4][3], s[4][3];
        for (int j = 0; j < fc->n; j++) {
            const float *q = fc->p[j];
            const float *M = mats[fc->b[j] < m->nbones ? fc->b[j] : 0];
            float wx = M[0] * q[0] + M[1] * q[1] + M[2] * q[2] + M[3] - cx;
            float wy = M[4] * q[0] + M[5] * q[1] + M[6] * q[2] + M[7] - cy;
            float wz = M[8] * q[0] + M[9] * q[1] + M[10] * q[2] + M[11] - cz;
            /* turn by yaw around y, then pitch around x, then push away */
            float rx = wx * cyaw - wz * syaw, rz = wx * syaw + wz * cyaw;
            float ry = wy * cp - rz * sp, rz2 = wy * sp + rz * cp + dist;
            p[j][0] = rx; p[j][1] = ry; p[j][2] = rz2;
            s[j][0] = (float)x + (float)w / 2 + rx / rz2 * f;
            s[j][1] = (float)y + (float)h / 2 - ry / rz2 * f;
            s[j][2] = rz2;
        }
        /* the normal of the face as placed, and the light on it */
        float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
        float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 1e-9f) continue;
        n[0] /= nl; n[1] /= nl; n[2] /= nl;
        /* the camera is at the origin of this frame: the face shows if its normal faces it */
        if (n[0] * p[0][0] + n[1] * p[0][1] + n[2] * p[0][2] > 0) continue;
        /* the light in the same frame */
        float lrx = lx * cyaw - lz * syaw, lrz = lx * syaw + lz * cyaw;
        float lry = ly * cp - lrz * sp, lrz2 = ly * sp + lrz * cp;
        float d = n[0] * lrx + n[1] * lry + n[2] * lrz2;
        float k2 = 0.6f + 0.4f * fmaxf(0, d);
        uint32_t col = fc->c;
        int r = (int)((float)(col >> 16 & 255) * k2), g = (int)((float)(col >> 8 & 255) * k2), b = (int)((float)(col & 255) * k2);
        col = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        tri(im, s[0], s[1], s[2], col);
        if (fc->n == 4) tri(im, s[0], s[2], s[3], col);
    }
}

static void save(const img_t *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
    fwrite(im->px, 3, (size_t)im->w * im->h, f);
    fclose(f);
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    s[n] = 0;
    fclose(f);
    return s;
}

/* a script file, or a recipe's id */
static int load(const char *what, mesh_model_t *m)
{
    if (mesh_find(what) >= 0) {
        mesh_req_t r;
        mesh_req_init(&r, what);
        return mesh_make(&r, m);
    }
    char *text = read_text(what);
    char err[128];
    int r = mesh_script(text, m, err, sizeof err);
    free(text);
    if (r) fprintf(stderr, "%s: %s\n", what, err);
    return r;
}

/* the four views and the poses of a model into an image file */
static void views(const mesh_model_t *m, const char *path)
{
    enum { S = 300 };
    int nposes = 4 * m->nclips;
    int rows = 1 + (nposes + 3) / 4;
    img_t im = img_new(S * 4, S * rows, 0x202830);
    static const float yaws[4] = { 0, 0.7f, 1.5708f, 3.1416f };
    for (int v = 0; v < 4; v++) draw_model(&im, m, v * S, 0, S, S, yaws[v], -1, 0);
    int slot = 4;
    for (int k = 0; k < m->nclips; k++)
        for (int j = 0; j < 4; j++, slot++)
            draw_model(&im, m, (slot % 4) * S, (slot / 4) * S, S, S, 0.7f, k, m->clips[k].length * (float)j / 4);
    save(&im, path);
}

static void json(const mesh_model_t *m, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "{\"faces\": [");
    for (int i = 0; i < m->nfaces; i++) {
        const mesh_face_t *fc = &m->faces[i];
        fprintf(f, "%s{\"p\": [", i ? ",\n" : "\n");
        for (int k = 0; k < fc->n; k++)
            fprintf(f, "%s[%g, %g, %g]", k ? ", " : "", fc->p[k][0], fc->p[k][1], fc->p[k][2]);
        fprintf(f, "], \"c\": %u, \"b\": [", fc->c);
        for (int k = 0; k < fc->n; k++) fprintf(f, "%s%d", k ? ", " : "", fc->b[k]);
        fprintf(f, "]}");
    }
    fprintf(f, "],\n\"bones\": [");
    for (int i = 0; i < m->nbones; i++) {
        const mesh_bone_t *b = &m->bones[i];
        fprintf(f, "%s{\"name\": \"%s\", \"parent\": %d, \"head\": [%g, %g, %g], \"tail\": [%g, %g, %g]}",
                i ? ",\n" : "\n", b->name, b->parent, b->head[0], b->head[1], b->head[2], b->tail[0], b->tail[1], b->tail[2]);
    }
    fprintf(f, "],\n\"clips\": [");
    for (int k = 0; k < m->nclips; k++) {
        const mesh_clip_t *c = &m->clips[k];
        fprintf(f, "%s{\"name\": \"%s\", \"loop\": %s, \"length\": %g, \"keys\": [", k ? ",\n" : "\n",
                c->name, c->loop ? "true" : "false", c->length);
        for (int j = 0; j < c->nkeys; j++) {
            fprintf(f, "%s{\"t\": %g, \"pose\": [", j ? ", " : "", c->keys[j].t);
            for (int b = 0; b < m->nbones; b++) {
                const mesh_pose_t *p = &c->keys[j].pose[b];
                fprintf(f, "%s{\"q\": [%g, %g, %g, %g], \"t\": [%g, %g, %g]}", b ? ", " : "",
                        p->q[0], p->q[1], p->q[2], p->q[3], p->t[0], p->t[1], p->t[2]);
            }
            fprintf(f, "]}");
        }
        fprintf(f, "]}");
    }
    fprintf(f, "]}\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    static mesh_model_t m;
    if (argc >= 4 && !strcmp(argv[1], "script")) {
        if (load(argv[2], &m)) return 1;
        views(&m, argv[3]);
        printf("%s: %d faces, %d bones, %d clips\n", argv[2], m.nfaces, m.nbones, m.nclips);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "json")) {
        if (load(argv[2], &m)) return 1;
        json(&m, argv[3]);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "sheet")) {
        enum { CW = 160, RH = 160, COLS = 6 };
        int n = mesh_recipes();
        int rows = (n + COLS - 1) / COLS;
        img_t im = img_new(CW * COLS, RH * rows, 0x202830);
        for (int i = 0; i < n; i++) {
            mesh_req_t r;
            mesh_req_init(&r, mesh_recipe_id(i));
            mesh_make(&r, &m);
            draw_model(&im, &m, (i % COLS) * CW, (i / COLS) * RH, CW, RH, 0.6f, -1, 0);
            printf("%-10s %4d faces %2d bones %d clips\n", mesh_recipe_id(i), m.nfaces, m.nbones, m.nclips);
        }
        save(&im, argv[2]);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "one")) {
        mesh_req_t r;
        mesh_req_init(&r, argv[2]);
        if (argc >= 5) r.seed = (uint32_t)atoi(argv[4]);
        if (argc >= 6) mesh_parse(argv[5], &r);
        if (mesh_make(&r, &m)) { fprintf(stderr, "no recipe %s\n", argv[2]); return 1; }
        enum { S = 300 };
        int nposes = 0;
        for (int k = 0; k < m.nclips; k++) nposes += 4;
        int cols = 4, rows = 1 + (nposes + 3) / 4;
        img_t im = img_new(S * cols, S * rows, 0x202830);
        static const float yaws[4] = { 0, 0.7f, 1.5708f, 3.1416f };
        for (int v = 0; v < 4; v++) draw_model(&im, &m, v * S, 0, S, S, yaws[v], -1, 0);
        int slot = 4;
        for (int k = 0; k < m.nclips; k++)
            for (int j = 0; j < 4; j++, slot++)
                draw_model(&im, &m, (slot % 4) * S, (slot / 4) * S, S, S, 0.7f, k, m.clips[k].length * (float)j / 4);
        save(&im, argv[3]);
        printf("%s: %d faces, %d bones, %d clips", argv[2], m.nfaces, m.nbones, m.nclips);
        for (int k = 0; k < m.nclips; k++) printf(" %s", m.clips[k].name);
        printf("\n");
        return 0;
    }
    fprintf(stderr, "usage: meshview sheet OUT.ppm | one RECIPE OUT.ppm [seed] [words] | script IN.txt OUT.ppm | json IN.txt|RECIPE OUT.json\n");
    return 2;
}
