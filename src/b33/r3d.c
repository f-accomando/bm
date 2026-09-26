#include "r3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR 0.1f

/* ---------------------------------------------------------------- raster */

typedef struct { float x, y, z; } sv_t;     /* screen x, y; z = 1/depth */

/*
 * Scanline rasterizer for one triangle. Pixel centres are sampled at
 * (x + 0.5, y + 0.5) so shared edges are drawn once. With zbuf == NULL
 * it is a plain 2D fill.
 */
static uint32_t raster(g16_t *g, uint16_t *zbuf, sv_t a, sv_t b, sv_t c, uint16_t color)
{
    sv_t t;
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (b.y > c.y) { t = b; b = c; c = t; }
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (c.y - a.y < 1e-6f)
        return 0;

    int y0 = (int)ceilf(a.y - 0.5f), y1 = (int)ceilf(c.y - 0.5f);
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 > g->cy1) y1 = g->cy1;
    uint32_t count = 0;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        /* long edge a-c, short edge a-b or b-c */
        float tl = (py - a.y) / (c.y - a.y);
        float xl = a.x + (c.x - a.x) * tl, zl = a.z + (c.z - a.z) * tl;
        float xr, zr;
        if (py < b.y) {
            float ts = (py - a.y) / (b.y - a.y);
            xr = a.x + (b.x - a.x) * ts; zr = a.z + (b.z - a.z) * ts;
        } else {
            float d = c.y - b.y;
            float ts = d > 1e-6f ? (py - b.y) / d : 1.0f;
            xr = b.x + (c.x - b.x) * ts; zr = b.z + (c.z - b.z) * ts;
        }
        if (xl > xr) { float s = xl; xl = xr; xr = s; s = zl; zl = zr; zr = s; }

        int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f);
        if (x0 < g->cx0) x0 = g->cx0;
        if (x1 > g->cx1) x1 = g->cx1;
        if (x0 >= x1)
            continue;

        uint16_t *row = g->px + (uint32_t)y * g->stride;
        if (!zbuf) {
            for (int x = x0; x < x1; x++)
                row[x] = color;
            count += (uint32_t)(x1 - x0);
            continue;
        }
        /* z = 1/depth, scaled to 16 bits: bigger = nearer */
        float dz = (xr - xl) > 1e-6f ? (zr - zl) / (xr - xl) : 0.0f;
        float z = zl + (x0 + 0.5f - xl) * dz;
        int32_t zf = (int32_t)(z * 65535.0f * 256.0f), dzf = (int32_t)(dz * 65535.0f * 256.0f);
        uint16_t *zrow = zbuf + (uint32_t)y * g->w;
        for (int x = x0; x < x1; x++, zf += dzf) {
            int32_t zz = zf >> 8;
            if (zz > 65535) zz = 65535;
            if (zz > zrow[x]) {
                zrow[x] = (uint16_t)zz;
                row[x] = color;
                count++;
            }
        }
    }
    return count;
}

void g16_tri(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    sv_t a = { (float)(x0 - g->cam_x), (float)(y0 - g->cam_y), 0 };
    sv_t b = { (float)(x1 - g->cam_x), (float)(y1 - g->cam_y), 0 };
    sv_t d = { (float)(x2 - g->cam_x), (float)(y2 - g->cam_y), 0 };
    raster(g, NULL, a, b, d, c);
}

/* ---------------------------------------------------------------- 3D */

int r3d_init(r3d_t *r, g16_t *g)
{
    memset(r, 0, sizeof *r);
    r->g = g;
    r->zbuf = malloc((size_t)g->w * g->h * 2);
    if (!r->zbuf)
        return -1;
    r3d_camera(r, 0, 0, -5, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.25f);
    r3d_zclear(r);
    return 0;
}

void r3d_free(r3d_t *r)
{
    free(r->zbuf);
    r->zbuf = NULL;
}

void r3d_zclear(r3d_t *r)
{
    memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
    r->tris_in = r->tris_drawn = r->pixels = 0;
}

void r3d_camera(r3d_t *r, float x, float y, float z, float yaw, float pitch, float fov_deg)
{
    r->cam_pos = (v3_t){ x, y, z };
    r->cam_yaw = yaw;
    r->cam_pitch = pitch;
    if (fov_deg < 10) fov_deg = 10;
    if (fov_deg > 150) fov_deg = 150;
    r->focal = (r->g->w * 0.5f) / tanf(fov_deg * 3.14159265f / 360.0f);
}

void r3d_light(r3d_t *r, float x, float y, float z, float ambient)
{
    float l = sqrtf(x * x + y * y + z * z);
    if (l < 1e-6f) l = 1;
    r->light = (v3_t){ x / l, y / l, z / l };
    r->ambient = ambient < 0 ? 0 : ambient > 1 ? 1 : ambient;
}

/* rotation matrix R = Rz * Ry * Rx, row major */
static void rot_matrix(float m[9], float rx, float ry, float rz)
{
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    m[0] = cz * cy; m[1] = cz * sy * sx - sz * cx; m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy; m[4] = sz * sy * sx + cz * cx; m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;     m[7] = cy * sx;                m[8] = cy * cx;
}

static uint16_t shade(uint32_t rgb, float k)
{
    uint32_t r = (uint32_t)((rgb >> 16 & 0xFF) * k), g = (uint32_t)((rgb >> 8 & 0xFF) * k), b = (uint32_t)((rgb & 0xFF) * k);
    return g16_rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

#define MAX_VERTS 4096

void r3d_draw(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz, float scale)
{
    static sv_t sv[MAX_VERTS];
    static uint8_t behind[MAX_VERTS];
    if (m->nverts > MAX_VERTS)
        return;

    float R[9];
    rot_matrix(R, rx, ry, rz);
    const float cyw = cosf(r->cam_yaw), syw = sinf(r->cam_yaw);
    const float cpt = cosf(r->cam_pitch), spt = sinf(r->cam_pitch);
    const float hw = r->g->w * 0.5f, hh = r->g->h * 0.5f, f = r->focal;

    for (int i = 0; i < m->nverts; i++) {
        v3_t v = m->verts[i];
        /* object -> world */
        float wx = (R[0] * v.x + R[1] * v.y + R[2] * v.z) * scale + p.x - r->cam_pos.x;
        float wy = (R[3] * v.x + R[4] * v.y + R[5] * v.z) * scale + p.y - r->cam_pos.y;
        float wz = (R[6] * v.x + R[7] * v.y + R[8] * v.z) * scale + p.z - r->cam_pos.z;
        /* world -> camera: yaw around y, then pitch around x */
        float x1 = cyw * wx - syw * wz, z1 = syw * wx + cyw * wz;
        float y2 = cpt * wy - spt * z1, z2 = spt * wy + cpt * z1;
        behind[i] = z2 < NEAR;
        if (!behind[i]) {
            float iz = 1.0f / z2;
            sv[i].x = hw + x1 * f * iz;
            sv[i].y = hh - y2 * f * iz;
            sv[i].z = iz;
        }
    }

    for (int t = 0; t < m->nfaces; t++) {
        const uint16_t *fc = m->faces + t * 3;
        r->tris_in++;
        if (behind[fc[0]] || behind[fc[1]] || behind[fc[2]])
            continue;                       /* no near-plane clipping: drop it */
        sv_t a = sv[fc[0]], b = sv[fc[1]], c = sv[fc[2]];
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (area <= 0)
            continue;                       /* back face (y points down on screen) */
        /* flat Lambert shading with the face normal rotated to world space */
        v3_t n = m->normals[t];
        float nx = R[0] * n.x + R[1] * n.y + R[2] * n.z;
        float ny = R[3] * n.x + R[4] * n.y + R[5] * n.z;
        float nz = R[6] * n.x + R[7] * n.y + R[8] * n.z;
        float d = nx * r->light.x + ny * r->light.y + nz * r->light.z;
        float k = r->ambient + (1.0f - r->ambient) * (d > 0 ? d : 0);
        r->pixels += raster(r->g, r->zbuf, a, b, c, shade(m->colors[t], k));
        r->tris_drawn++;
    }
}

/* ---------------------------------------------------------------- meshes */

int r3d_mesh_alloc(r3d_mesh_t *m, int nverts, int nfaces)
{
    memset(m, 0, sizeof *m);
    m->verts = calloc((size_t)nverts, sizeof *m->verts);
    m->faces = calloc((size_t)nfaces * 3, sizeof *m->faces);
    m->colors = calloc((size_t)nfaces, sizeof *m->colors);
    m->normals = calloc((size_t)nfaces, sizeof *m->normals);
    if (!m->verts || !m->faces || !m->colors || !m->normals) {
        r3d_mesh_free(m);
        return -1;
    }
    m->nverts = nverts;
    m->nfaces = nfaces;
    return 0;
}

void r3d_mesh_free(r3d_mesh_t *m)
{
    free(m->verts); free(m->faces); free(m->colors); free(m->normals);
    memset(m, 0, sizeof *m);
}

void r3d_mesh_normals(r3d_mesh_t *m)
{
    for (int t = 0; t < m->nfaces; t++) {
        v3_t a = m->verts[m->faces[t * 3]], b = m->verts[m->faces[t * 3 + 1]], c = m->verts[m->faces[t * 3 + 2]];
        v3_t u = { b.x - a.x, b.y - a.y, b.z - a.z }, v = { c.x - a.x, c.y - a.y, c.z - a.z };
        /* outward normal for the winding used by the meshes (u x v) */
        v3_t n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (l < 1e-12f) l = 1;
        m->normals[t] = (v3_t){ n.x / l, n.y / l, n.z / l };
    }
}

int r3d_mesh_sphere(r3d_mesh_t *m, int rings, int segs, uint32_t c1, uint32_t c2)
{
    if (rings < 2) rings = 2;
    if (segs < 3) segs = 3;
    int nv = (rings + 1) * segs, nf = rings * segs * 2;
    if (nv > MAX_VERTS || r3d_mesh_alloc(m, nv, nf) != 0)
        return -1;
    for (int r = 0; r <= rings; r++) {
        float th = 3.14159265f * r / rings;
        for (int s = 0; s < segs; s++) {
            float ph = 2 * 3.14159265f * s / segs;
            m->verts[r * segs + s] = (v3_t){ sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
        }
    }
    int f = 0;
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            uint16_t a = (uint16_t)(r * segs + s), b = (uint16_t)(r * segs + (s + 1) % segs);
            uint16_t c = (uint16_t)(a + segs), d = (uint16_t)(b + segs);
            uint32_t col = ((r + s) & 1) ? c1 : c2;
            m->faces[f * 3] = a; m->faces[f * 3 + 1] = b; m->faces[f * 3 + 2] = c; m->colors[f++] = col;
            m->faces[f * 3] = b; m->faces[f * 3 + 1] = d; m->faces[f * 3 + 2] = c; m->colors[f++] = col;
        }
    r3d_mesh_normals(m);
    return 0;
}

int r3d_mesh_cube(r3d_mesh_t *m, uint32_t color)
{
    static const float v[8][3] = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1},
    };
    static const uint16_t f[12][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7}, {0,1,5}, {0,5,4},
        {3,6,2}, {3,7,6}, {0,4,7}, {0,7,3}, {1,2,6}, {1,6,5},
    };
    if (r3d_mesh_alloc(m, 8, 12) != 0)
        return -1;
    for (int i = 0; i < 8; i++) m->verts[i] = (v3_t){ v[i][0], v[i][1], v[i][2] };
    for (int i = 0; i < 12; i++) {
        memcpy(m->faces + i * 3, f[i], sizeof f[i]);
        m->colors[i] = color;
    }
    r3d_mesh_normals(m);
    return 0;
}
