/*
 * 3D map engine for bm cartridges (Tropic Trail, AGY-01).
 * Transforms 2D tilemap layers into chunked 3D meshes with frustum culling
 * and collision queries.
 *
 * MIT License / permissive console license. No GPL code.
 */

#include "map3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <lua.h>
#include <lauxlib.h>

#define MAP3D_MT "bm.map3d"
#define CHUNK_SIZE 16

/* Helper for sphere-AABB intersection */
static inline int sphere_box_intersect(float cx, float cy, float cz, float r,
                                       float x0, float y0, float z0,
                                       float x1, float y1, float z1)
{
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (cx < x0) dx = x0 - cx;
    else if (cx > x1) dx = cx - x1;

    if (cy < y0) dy = y0 - cy;
    else if (cy > y1) dy = cy - y1;

    if (cz < z0) dz = z0 - cz;
    else if (cz > z1) dz = cz - z1;

    return (dx * dx + dy * dy + dz * dz) <= (r * r);
}

/* Helper to get kind name from placeholder id */
static const char *object_kind_name(int id)
{
    static char kit[16];                /* a map kit's object: "OBJ1040" (docs/design/map-kit.md) */
    if (id >= 1040 && id < 65536) {
        snprintf(kit, sizeof kit, "OBJ%d", id);
        return kit;
    }
    switch (id) {
        case 1024: return "PLAYER_START";
        case 1026: return "APPLE";
        case 1028: return "CRATE";
        case 1030: return "GOAL";
        case 1032: return "LAMP";
        case 1034: return "CRATE_BOUNCE";
        default:   return NULL;
    }
}

/* Add a quad (2 triangles) to the mesh building buffers */
typedef struct {
    v3_t *verts;
    uint16_t *faces;
    float *uv;
    uint32_t *colors;
    int v_count, f_count;
} mesh_builder_t;

static void add_quad(mesh_builder_t *b,
                     v3_t v0, v3_t v1, v3_t v2, v3_t v3,
                     float u0, float v_0, float u1, float v_1,
                     uint32_t color)
{
    int base_v = b->v_count;
    b->verts[base_v + 0] = v0;
    b->verts[base_v + 1] = v1;
    b->verts[base_v + 2] = v2;
    b->verts[base_v + 3] = v3;
    b->v_count += 4;

    int base_f = b->f_count;
    /* Triangle 1: (v0, v2, v1) */
    b->faces[base_f * 3 + 0] = (uint16_t)(base_v + 0);
    b->faces[base_f * 3 + 1] = (uint16_t)(base_v + 2);
    b->faces[base_f * 3 + 2] = (uint16_t)(base_v + 1);
    b->colors[base_f] = color;
    b->uv[base_f * 6 + 0] = u0;  b->uv[base_f * 6 + 1] = v_0;
    b->uv[base_f * 6 + 2] = u1;  b->uv[base_f * 6 + 3] = v_1;
    b->uv[base_f * 6 + 4] = u1;  b->uv[base_f * 6 + 5] = v_0;

    /* Triangle 2: (v0, v3, v2) */
    b->faces[(base_f + 1) * 3 + 0] = (uint16_t)(base_v + 0);
    b->faces[(base_f + 1) * 3 + 1] = (uint16_t)(base_v + 3);
    b->faces[(base_f + 1) * 3 + 2] = (uint16_t)(base_v + 2);
    b->colors[base_f + 1] = color;
    b->uv[(base_f + 1) * 6 + 0] = u0;  b->uv[(base_f + 1) * 6 + 1] = v_0;
    b->uv[(base_f + 1) * 6 + 2] = u0;  b->uv[(base_f + 1) * 6 + 3] = v_1;
    b->uv[(base_f + 1) * 6 + 4] = u1;  b->uv[(base_f + 1) * 6 + 5] = v_1;

    b->f_count += 2;
}

/* A vertical face in bands: with wall_tile > 0 the texture covers wall_tile units of height and repeats down the face
 * (a tall wall no longer stretches one 16 x 16 texture over its whole height); 0 stretches it, as before. t0, t1 are
 * the top corners, b2, b3 the bottom ones (b2 under t1, b3 under t0). */
static int wall_bands(float ytop, float ybot, float tile)
{
    if (tile <= 0.0f)
        return 1;
    int n = (int)ceilf((ytop - ybot) / tile - 0.001f);
    return n < 1 ? 1 : n;
}

static void add_wall(mesh_builder_t *b, v3_t t0, v3_t t1, v3_t b2, v3_t b3,
                     float u0, float v0, float u1, float v1, float tile, uint32_t color)
{
    if (tile <= 0.0f) {
        add_quad(b, t0, t1, b2, b3, u0, v0, u1, v1, color);
        return;
    }
    const float ytop = t0.y, ybot = b2.y;
    const int n = wall_bands(ytop, ybot, tile);
    for (int k = 0; k < n; k++) {
        float yt = ytop - tile * k;
        float yb = yt - tile;
        if (yb < ybot) yb = ybot;
        v3_t a0 = { t0.x, yt, t0.z }, a1 = { t1.x, yt, t1.z };
        v3_t a2 = { b2.x, yb, b2.z }, a3 = { b3.x, yb, b3.z };
        add_quad(b, a0, a1, a2, a3, u0, v0, u1, v0 + (v1 - v0) * (yt - yb) / tile, color);
    }
}

static void build_chunk_mesh(map3d_t *m, map3d_chunk_t *chunk)
{
    int tx0 = chunk->tx0, tx1 = chunk->tx1;
    int tz0 = chunk->tz0, tz1 = chunk->tz1;
    int w = m->w;
    float cell = m->cell;
    float step = m->step;

    int sheet_cols = (m->sheet && m->sheet->w > 0) ? (m->sheet->w / 8) : 16;
    if (sheet_cols <= 0) sheet_cols = 16;
    const float tile = m->wall_tile;

    /* First pass: count required quads */
    int quads = 0;
    for (int tz = tz0; tz < tz1; tz++) {
        for (int tx = tx0; tx < tx1; tx++) {
            int c_idx = (tz * 2) * w + (tx * 2);
            int f_id = m->floor ? m->floor[c_idx] : 0;
            int h_id = m->height ? (m->height[c_idx] & 255) : 0;
            int w_id = m->walls ? (m->walls[c_idx] & 255) : 0;

            if (f_id != 0) {
                quads++; /* floor top */

                float y_floor = h_id * step;

                /* North neighbor */
                float y_n = -1.0f;
                int n_has_f = 0;
                if (tz > 0) {
                    int nc = ((tz - 1) * 2) * w + (tx * 2);
                    if (m->floor && m->floor[nc] != 0) {
                        n_has_f = 1;
                        y_n = (m->height ? (m->height[nc] & 255) : 0) * step;
                    }
                }
                if (!n_has_f) { if (y_floor > 0.0f) quads += wall_bands(y_floor, 0.0f, tile); }
                else if (y_n < y_floor) quads += wall_bands(y_floor, y_n, tile);

                /* South neighbor */
                float y_s = -1.0f;
                int s_has_f = 0;
                if (tz + 1 < m->nt_z) {
                    int sc = ((tz + 1) * 2) * w + (tx * 2);
                    if (m->floor && m->floor[sc] != 0) {
                        s_has_f = 1;
                        y_s = (m->height ? (m->height[sc] & 255) : 0) * step;
                    }
                }
                if (!s_has_f) { if (y_floor > 0.0f) quads += wall_bands(y_floor, 0.0f, tile); }
                else if (y_s < y_floor) quads += wall_bands(y_floor, y_s, tile);

                /* West neighbor */
                float y_w = -1.0f;
                int w_has_f = 0;
                if (tx > 0) {
                    int wc = (tz * 2) * w + ((tx - 1) * 2);
                    if (m->floor && m->floor[wc] != 0) {
                        w_has_f = 1;
                        y_w = (m->height ? (m->height[wc] & 255) : 0) * step;
                    }
                }
                if (!w_has_f) { if (y_floor > 0.0f) quads += wall_bands(y_floor, 0.0f, tile); }
                else if (y_w < y_floor) quads += wall_bands(y_floor, y_w, tile);

                /* East neighbor */
                float y_e = -1.0f;
                int e_has_f = 0;
                if (tx + 1 < m->nt_x) {
                    int ec = (tz * 2) * w + ((tx + 1) * 2);
                    if (m->floor && m->floor[ec] != 0) {
                        e_has_f = 1;
                        y_e = (m->height ? (m->height[ec] & 255) : 0) * step;
                    }
                }
                if (!e_has_f) { if (y_floor > 0.0f) quads += wall_bands(y_floor, 0.0f, tile); }
                else if (y_e < y_floor) quads += wall_bands(y_floor, y_e, tile);
            }

            if (w_id != 0) {
                float y_base = h_id * step;
                quads += 1 + 4 * wall_bands(y_base + w_id * step, y_base, tile);  /* top + 4 sides of wall block */
            }
        }
    }

    if (quads == 0) {
        chunk->empty = 1;
        return;
    }

    int nverts = quads * 4;
    int nfaces = quads * 2;

    if (r3d_mesh_alloc(&chunk->mesh, nverts, nfaces) != 0) {
        chunk->empty = 1;
        return;
    }
    r3d_mesh_alloc_uv(&chunk->mesh);
    chunk->mesh.tex = m->sheet;

    mesh_builder_t b;
    b.verts = chunk->mesh.verts;
    b.faces = chunk->mesh.faces;
    b.uv = chunk->mesh.uv;
    b.colors = chunk->mesh.colors;
    b.v_count = 0;
    b.f_count = 0;

    uint32_t col_textured = (m->sheet && m->sheet->px) ? R3D_TEXTURED : 0x7E9A60;

    /* Second pass: construct quads */
    for (int tz = tz0; tz < tz1; tz++) {
        for (int tx = tx0; tx < tx1; tx++) {
            int c_idx = (tz * 2) * w + (tx * 2);
            int f_id = m->floor ? m->floor[c_idx] : 0;
            int h_id = m->height ? (m->height[c_idx] & 255) : 0;
            int w_id = m->walls ? (m->walls[c_idx] & 255) : 0;

            float x0 = tx * cell, x1 = (tx + 1) * cell;
            float z0 = tz * cell, z1 = (tz + 1) * cell;
            float y_floor = h_id * step;

            if (f_id != 0) {
                float u0 = (float)((f_id % sheet_cols) * 8);
                float v0 = (float)((f_id / sheet_cols) * 8);
                float u1 = u0 + 16.0f, v1 = v0 + 16.0f;

                /* Floor top */
                v3_t ft0 = { x0, y_floor, z0 };
                v3_t ft1 = { x1, y_floor, z0 };
                v3_t ft2 = { x1, y_floor, z1 };
                v3_t ft3 = { x0, y_floor, z1 };
                add_quad(&b, ft0, ft1, ft2, ft3, u0, v0, u1, v1, col_textured);

                /* North vertical wall */
                float y_n = 0.0f;
                int n_has_f = 0;
                if (tz > 0) {
                    int nc = ((tz - 1) * 2) * w + (tx * 2);
                    if (m->floor && m->floor[nc] != 0) {
                        n_has_f = 1;
                        y_n = (m->height ? (m->height[nc] & 255) : 0) * step;
                    }
                }
                if (!n_has_f && y_floor > 0.0f) {
                    v3_t v_n0 = { x1, y_floor, z0 }, v_n1 = { x0, y_floor, z0 };
                    v3_t v_n2 = { x0, 0.0f,    z0 }, v_n3 = { x1, 0.0f,    z0 };
                    add_wall(&b, v_n0, v_n1, v_n2, v_n3, u0, v0, u1, v1, tile, col_textured);
                } else if (n_has_f && y_n < y_floor) {
                    v3_t v_n0 = { x1, y_floor, z0 }, v_n1 = { x0, y_floor, z0 };
                    v3_t v_n2 = { x0, y_n,     z0 }, v_n3 = { x1, y_n,     z0 };
                    add_wall(&b, v_n0, v_n1, v_n2, v_n3, u0, v0, u1, v1, tile, col_textured);
                }

                /* South vertical wall */
                float y_s = 0.0f;
                int s_has_f = 0;
                if (tz + 1 < m->nt_z) {
                    int sc = ((tz + 1) * 2) * w + (tx * 2);
                    if (m->floor && m->floor[sc] != 0) {
                        s_has_f = 1;
                        y_s = (m->height ? (m->height[sc] & 255) : 0) * step;
                    }
                }
                if (!s_has_f && y_floor > 0.0f) {
                    v3_t v_s0 = { x0, y_floor, z1 }, v_s1 = { x1, y_floor, z1 };
                    v3_t v_s2 = { x1, 0.0f,    z1 }, v_s3 = { x0, 0.0f,    z1 };
                    add_wall(&b, v_s0, v_s1, v_s2, v_s3, u0, v0, u1, v1, tile, col_textured);
                } else if (s_has_f && y_s < y_floor) {
                    v3_t v_s0 = { x0, y_floor, z1 }, v_s1 = { x1, y_floor, z1 };
                    v3_t v_s2 = { x1, y_s,     z1 }, v_s3 = { x0, y_s,     z1 };
                    add_wall(&b, v_s0, v_s1, v_s2, v_s3, u0, v0, u1, v1, tile, col_textured);
                }

                /* West vertical wall */
                float y_w = 0.0f;
                int w_has_f = 0;
                if (tx > 0) {
                    int wc = (tz * 2) * w + ((tx - 1) * 2);
                    if (m->floor && m->floor[wc] != 0) {
                        w_has_f = 1;
                        y_w = (m->height ? (m->height[wc] & 255) : 0) * step;
                    }
                }
                if (!w_has_f && y_floor > 0.0f) {
                    v3_t v_w0 = { x0, y_floor, z0 }, v_w1 = { x0, y_floor, z1 };
                    v3_t v_w2 = { x0, 0.0f,    z1 }, v_w3 = { x0, 0.0f,    z0 };
                    add_wall(&b, v_w0, v_w1, v_w2, v_w3, u0, v0, u1, v1, tile, col_textured);
                } else if (w_has_f && y_w < y_floor) {
                    v3_t v_w0 = { x0, y_floor, z0 }, v_w1 = { x0, y_floor, z1 };
                    v3_t v_w2 = { x0, y_w,     z1 }, v_w3 = { x0, y_w,     z0 };
                    add_wall(&b, v_w0, v_w1, v_w2, v_w3, u0, v0, u1, v1, tile, col_textured);
                }

                /* East vertical wall */
                float y_e = 0.0f;
                int e_has_f = 0;
                if (tx + 1 < m->nt_x) {
                    int ec = (tz * 2) * w + ((tx + 1) * 2);
                    if (m->floor && m->floor[ec] != 0) {
                        e_has_f = 1;
                        y_e = (m->height ? (m->height[ec] & 255) : 0) * step;
                    }
                }
                if (!e_has_f && y_floor > 0.0f) {
                    v3_t v_e0 = { x1, y_floor, z1 }, v_e1 = { x1, y_floor, z0 };
                    v3_t v_e2 = { x1, 0.0f,    z0 }, v_e3 = { x1, 0.0f,    z1 };
                    add_wall(&b, v_e0, v_e1, v_e2, v_e3, u0, v0, u1, v1, tile, col_textured);
                } else if (e_has_f && y_e < y_floor) {
                    v3_t v_e0 = { x1, y_floor, z1 }, v_e1 = { x1, y_floor, z0 };
                    v3_t v_e2 = { x1, y_e,     z0 }, v_e3 = { x1, y_e,     z1 };
                    add_wall(&b, v_e0, v_e1, v_e2, v_e3, u0, v0, u1, v1, tile, col_textured);
                }
            }

            /* Wall block */
            if (w_id != 0) {
                float y_top = y_floor + w_id * step;
                float u0 = (float)((w_id % sheet_cols) * 8);
                float v0 = (float)((w_id / sheet_cols) * 8);
                float u1 = u0 + 16.0f, v1 = v0 + 16.0f;

                /* Top */
                v3_t wt0 = { x0, y_top, z0 }, wt1 = { x1, y_top, z0 };
                v3_t wt2 = { x1, y_top, z1 }, wt3 = { x0, y_top, z1 };
                add_quad(&b, wt0, wt1, wt2, wt3, u0, v0, u1, v1, col_textured);

                /* North side */
                v3_t wn0 = { x1, y_top,   z0 }, wn1 = { x0, y_top,   z0 };
                v3_t wn2 = { x0, y_floor, z0 }, wn3 = { x1, y_floor, z0 };
                add_wall(&b, wn0, wn1, wn2, wn3, u0, v0, u1, v1, tile, col_textured);

                /* South side */
                v3_t ws0 = { x0, y_top,   z1 }, ws1 = { x1, y_top,   z1 };
                v3_t ws2 = { x1, y_floor, z1 }, ws3 = { x0, y_floor, z1 };
                add_wall(&b, ws0, ws1, ws2, ws3, u0, v0, u1, v1, tile, col_textured);

                /* West side */
                v3_t ww0 = { x0, y_top,   z0 }, ww1 = { x0, y_top,   z1 };
                v3_t ww2 = { x0, y_floor, z1 }, ww3 = { x0, y_floor, z0 };
                add_wall(&b, ww0, ww1, ww2, ww3, u0, v0, u1, v1, tile, col_textured);

                /* East side */
                v3_t we0 = { x1, y_top,   z1 }, we1 = { x1, y_top,   z0 };
                v3_t we2 = { x1, y_floor, z0 }, we3 = { x1, y_floor, z1 };
                add_wall(&b, we0, we1, we2, we3, u0, v0, u1, v1, tile, col_textured);
            }
        }
    }

    /* Compute face & vertex normals, centre and bounding radius */
    r3d_mesh_normals(&chunk->mesh);
    chunk->empty = 0;
}

map3d_t *map3d_create(int w, int h,
                      const uint16_t *floor,
                      const uint16_t *height,
                      const uint16_t *walls,
                      const uint16_t *objects,
                      float cell, float step,
                      const g16_sheet_t *sheet)
{
    return map3d_create_opt(w, h, floor, height, walls, objects, cell, step, sheet, 0.0f);
}

map3d_t *map3d_create_opt(int w, int h,
                          const uint16_t *floor,
                          const uint16_t *height,
                          const uint16_t *walls,
                          const uint16_t *objects,
                          float cell, float step,
                          const g16_sheet_t *sheet, float wall_tile)
{
    if (w <= 0 || h <= 0)
        return NULL;

    map3d_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

    m->w = w;
    m->h = h;
    m->nt_x = w / 2;
    m->nt_z = h / 2;
    m->cell = (cell > 0.0f) ? cell : 1.0f;
    m->step = (step > 0.0f) ? step : 0.25f;
    m->wall_tile = wall_tile > 0.0f ? wall_tile : 0.0f;
    m->sheet = sheet;

    size_t cells_size = (size_t)w * (size_t)h * sizeof(uint16_t);

    if (floor) {
        m->floor = malloc(cells_size);
        if (m->floor) memcpy(m->floor, floor, cells_size);
    }
    if (height) {
        m->height = malloc(cells_size);
        if (m->height) memcpy(m->height, height, cells_size);
    }
    if (walls) {
        m->walls = malloc(cells_size);
        if (m->walls) memcpy(m->walls, walls, cells_size);
    }
    if (objects) {
        m->objects = malloc(cells_size);
        if (m->objects) memcpy(m->objects, objects, cells_size);
    }

    /* Extract objects list */
    if (m->objects) {
        int obj_count = 0;
        for (int tz = 0; tz < m->nt_z; tz++) {
            for (int tx = 0; tx < m->nt_x; tx++) {
                int c_idx = (tz * 2) * w + (tx * 2);
                int oid = m->objects[c_idx];
                if (oid >= 1024 && object_kind_name(oid) != NULL)
                    obj_count++;
            }
        }

        if (obj_count > 0) {
            m->objs = calloc((size_t)obj_count, sizeof(map3d_obj_t));
            if (m->objs) {
                m->nobjs = obj_count;
                int oi = 0;
                for (int tz = 0; tz < m->nt_z; tz++) {
                    for (int tx = 0; tx < m->nt_x; tx++) {
                        int c_idx = (tz * 2) * w + (tx * 2);
                        int oid = m->objects[c_idx];
                        const char *kname = object_kind_name(oid);
                        if (oid >= 1024 && kname != NULL && oi < obj_count) {
                            strncpy(m->objs[oi].kind, kname, sizeof(m->objs[oi].kind) - 1);
                            m->objs[oi].x = (tx + 0.5f) * m->cell;
                            m->objs[oi].z = (tz + 0.5f) * m->cell;
                            float yh = 0.0f;
                            if (m->height)
                                yh = (m->height[c_idx] & 255) * m->step;
                            m->objs[oi].y = yh;
                            oi++;
                        }
                    }
                }
            }
        }
    }

    /* Partition into 16x16 chunks */
    m->chunks_x = (m->nt_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
    m->chunks_z = (m->nt_z + CHUNK_SIZE - 1) / CHUNK_SIZE;
    m->nchunks = m->chunks_x * m->chunks_z;

    m->chunks = calloc((size_t)m->nchunks, sizeof(map3d_chunk_t));
    if (m->chunks) {
        for (int cz = 0; cz < m->chunks_z; cz++) {
            for (int cx = 0; cx < m->chunks_x; cx++) {
                int ci = cz * m->chunks_x + cx;
                map3d_chunk_t *chunk = &m->chunks[ci];
                chunk->tx0 = cx * CHUNK_SIZE;
                chunk->tz0 = cz * CHUNK_SIZE;
                chunk->tx1 = (chunk->tx0 + CHUNK_SIZE < m->nt_x) ? chunk->tx0 + CHUNK_SIZE : m->nt_x;
                chunk->tz1 = (chunk->tz0 + CHUNK_SIZE < m->nt_z) ? chunk->tz0 + CHUNK_SIZE : m->nt_z;

                build_chunk_mesh(m, chunk);
            }
        }
    }

    return m;
}

void map3d_free(map3d_t *m)
{
    if (!m) return;
    if (m->chunks) {
        for (int i = 0; i < m->nchunks; i++) {
            if (!m->chunks[i].empty)
                r3d_mesh_free(&m->chunks[i].mesh);
        }
        free(m->chunks);
    }
    if (m->floor) free(m->floor);
    if (m->height) free(m->height);
    if (m->walls) free(m->walls);
    if (m->objects) free(m->objects);
    if (m->objs) free(m->objs);
    free(m);
}

void map3d_draw(const map3d_t *m, r3d_t *r, unsigned flags)
{
    if (!m || !r || !m->chunks)
        return;

    v3_t zero_pos = { 0.0f, 0.0f, 0.0f };

    for (int i = 0; i < m->nchunks; i++) {
        const map3d_chunk_t *chunk = &m->chunks[i];
        if (chunk->empty)
            continue;

        /* Frustum culling: skip pieces out of camera view */
        if (!r3d_visible(r, chunk->mesh.centre, chunk->mesh.radius))
            continue;

        r3d_draw_flags(r, &chunk->mesh, zero_pos, 0.0f, 0.0f, 0.0f, 1.0f, flags);
    }
}

int map3d_get_height(const map3d_t *m, float x, float z, float *out_h)
{
    if (!m) return 0;

    int tx = (int)floorf(x / m->cell);
    int tz = (int)floorf(z / m->cell);

    if (tx < 0 || tx >= m->nt_x || tz < 0 || tz >= m->nt_z)
        return 0;

    int c_idx = (tz * 2) * m->w + (tx * 2);
    int f_id = m->floor ? m->floor[c_idx] : 0;
    if (f_id == 0)
        return 0; /* Hole / gap */

    if (out_h) {
        int h_id = m->height ? (m->height[c_idx] & 255) : 0;
        *out_h = h_id * m->step;
    }
    return 1;
}

int map3d_is_blocked(const map3d_t *m, float x, float y, float z, float r)
{
    if (!m) return 0;

    int tx_min = (int)floorf((x - r) / m->cell);
    int tx_max = (int)floorf((x + r) / m->cell);
    int tz_min = (int)floorf((z - r) / m->cell);
    int tz_max = (int)floorf((z + r) / m->cell);

    if (tx_min < 0) tx_min = 0;
    if (tz_min < 0) tz_min = 0;
    if (tx_max >= m->nt_x) tx_max = m->nt_x - 1;
    if (tz_max >= m->nt_z) tz_max = m->nt_z - 1;

    for (int tz = tz_min; tz <= tz_max; tz++) {
        for (int tx = tx_min; tx <= tx_max; tx++) {
            int c_idx = (tz * 2) * m->w + (tx * 2);
            float x0 = tx * m->cell, x1 = (tx + 1) * m->cell;
            float z0 = tz * m->cell, z1 = (tz + 1) * m->cell;

            float f_h = (m->height ? (m->height[c_idx] & 255) : 0) * m->step;
            int w_val = m->walls ? (m->walls[c_idx] & 255) : 0;

            /* Solid wall block */
            if (w_val > 0) {
                float y0 = f_h;
                float y1 = f_h + w_val * m->step;
                if (sphere_box_intersect(x, y, z, r, x0, y0, z0, x1, y1, z1))
                    return 1;
            }

            /* Solid ground cliff higher than sphere bottom */
            if (f_h > y) {
                if (sphere_box_intersect(x, y, z, r, x0, 0.0f, z0, x1, f_h, z1))
                    return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------ Lua Bindings */

static map3d_t *check_map3d(lua_State *L, int idx)
{
    map3d_t **u = (map3d_t **)luaL_checkudata(L, idx, MAP3D_MT);
    if (!u || !*u)
        luaL_error(L, "invalid map3d handle");
    return *u;
}

static int l_map3d_gc(lua_State *L)
{
    map3d_t **u = (map3d_t **)luaL_checkudata(L, 1, MAP3D_MT);
    if (u && *u) {
        map3d_free(*u);
        *u = NULL;
    }
    return 0;
}

int l_map3d_build(lua_State *L)
{
    const char *floor_name = "floor";
    const char *height_name = "height";
    const char *walls_name = "walls";
    float cell = 1.0f;
    float step = 0.25f;
    float wall_tile = 0.0f;

    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "floor");
        if (lua_isstring(L, -1)) floor_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "height");
        if (lua_isstring(L, -1)) height_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "walls");
        if (lua_isstring(L, -1)) walls_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "cell");
        if (lua_isnumber(L, -1)) cell = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "step");
        if (lua_isnumber(L, -1)) step = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "wall_tile");            /* units of height a wall texture covers (0: stretched) */
        if (lua_isnumber(L, -1)) wall_tile = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    int w = 0, h = 0;
    const uint16_t *f_cells = NULL;
    const uint16_t *h_cells = NULL;
    const uint16_t *w_cells = NULL;
    const uint16_t *o_cells = NULL;

    rt_get_map_layer(floor_name, &f_cells, &w, &h);
    rt_get_map_layer(height_name, &h_cells, &w, &h);
    rt_get_map_layer(walls_name, &w_cells, &w, &h);
    rt_get_map_layer("objects", &o_cells, &w, &h);

    if (w <= 0 || h <= 0)
        return luaL_error(L, "map3d_build: map is empty or not initialized");

    const g16_sheet_t *sheet = rt_get_sheet();

    map3d_t *m = map3d_create_opt(w, h, f_cells, h_cells, w_cells, o_cells, cell, step, sheet, wall_tile);
    if (!m)
        return luaL_error(L, "map3d_build: out of memory");

    map3d_t **u = (map3d_t **)lua_newuserdatauv(L, sizeof(map3d_t *), 0);
    *u = m;

    if (luaL_newmetatable(L, MAP3D_MT)) {
        lua_pushcfunction(L, l_map3d_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);

    return 1;
}

int l_map3d_draw(lua_State *L)
{
    map3d_t *m = check_map3d(L, 1);
    unsigned flags = (unsigned)luaL_optinteger(L, 2, 0);

    r3d_t *r = rt_get_r3d(L);
    if (r) {
        map3d_draw(m, r, flags);
    }
    return 0;
}

int l_map3d_height(lua_State *L)
{
    map3d_t *m = check_map3d(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);

    float h = 0.0f;
    if (map3d_get_height(m, x, z, &h)) {
        lua_pushnumber(L, h);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_map3d_blocked(lua_State *L)
{
    map3d_t *m = check_map3d(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    float r = (float)luaL_checknumber(L, 5);

    int blocked = map3d_is_blocked(m, x, y, z, r);
    lua_pushboolean(L, blocked);
    return 1;
}

int l_map3d_objects(lua_State *L)
{
    map3d_t *m = check_map3d(L, 1);
    const char *kind = luaL_optstring(L, 2, NULL);

    lua_newtable(L);
    int idx = 1;

    for (int i = 0; i < m->nobjs; i++) {
        if (!kind || !strcmp(kind, m->objs[i].kind)) {
            lua_newtable(L);

            lua_pushstring(L, m->objs[i].kind);
            lua_setfield(L, -2, "kind");

            lua_pushnumber(L, m->objs[i].x);
            lua_setfield(L, -2, "x");

            lua_pushnumber(L, m->objs[i].y);
            lua_setfield(L, -2, "y");

            lua_pushnumber(L, m->objs[i].z);
            lua_setfield(L, -2, "z");

            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

int l_map3d_size(lua_State *L)
{
    map3d_t *m = check_map3d(L, 1);
    float nx = m->nt_x * m->cell;
    float nz = m->nt_z * m->cell;

    lua_pushnumber(L, nx);
    lua_pushnumber(L, nz);
    return 2;
}

void map3d_register(lua_State *L)
{
    lua_register(L, "map3d_build", l_map3d_build);
    lua_register(L, "map3d_draw", l_map3d_draw);
    lua_register(L, "map3d_height", l_map3d_height);
    lua_register(L, "map3d_blocked", l_map3d_blocked);
    lua_register(L, "map3d_objects", l_map3d_objects);
    lua_register(L, "map3d_size", l_map3d_size);
}
