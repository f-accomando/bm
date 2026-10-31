#ifndef MAP3D_H
#define MAP3D_H

/*
 * 3D map engine for bm cartridges (Tropic Trail, AGY-01):
 * transforms 2D tilemap layers (floor, height, walls, objects) into
 * chunked 3D meshes (16x16 tiles per piece) with frustum culling (r3d_visible)
 * and collision queries (floor height, sphere-wall blocking, object list).
 * Portable C, no per-frame dynamic allocations, no GPL code.
 */

#include <stdint.h>
#include <stddef.h>

#include "r3d.h"
#include "gfx16.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

typedef struct {
    char kind[32];
    float x, y, z;
} map3d_obj_t;

typedef struct {
    r3d_mesh_t mesh;
    int tx0, tz0;
    int tx1, tz1;
    int empty;
} map3d_chunk_t;

typedef struct {
    int w, h;                   /* map grid size in 8x8 cells */
    int nt_x, nt_z;             /* map grid size in 16x16 tiles (w/2, h/2) */
    float cell;                 /* tile size in world units (default 1.0) */
    float step;                 /* height step in world units (default 0.25) */
    float wall_tile;            /* units of height a wall texture covers, repeated down the face (0: stretched over it) */

    uint16_t *floor;            /* cells copy of floor layer */
    uint16_t *height;           /* cells copy of height layer */
    uint16_t *walls;            /* cells copy of walls layer */
    uint16_t *objects;          /* cells copy of objects layer */

    map3d_chunk_t *chunks;
    int chunks_x, chunks_z, nchunks;

    map3d_obj_t *objs;
    int nobjs;

    const g16_sheet_t *sheet;
} map3d_t;

/* Runtime accessors implemented by runtime.c */
int rt_get_map_layer(const char *name, const uint16_t **cells, int *w, int *h);
const g16_sheet_t *rt_get_sheet(void);
r3d_t *rt_get_r3d(struct lua_State *L);

/* C API */
map3d_t *map3d_create(int w, int h,
                      const uint16_t *floor,
                      const uint16_t *height,
                      const uint16_t *walls,
                      const uint16_t *objects,
                      float cell, float step,
                      const g16_sheet_t *sheet);

map3d_t *map3d_create_opt(int w, int h,
                          const uint16_t *floor,
                          const uint16_t *height,
                          const uint16_t *walls,
                          const uint16_t *objects,
                          float cell, float step,
                          const g16_sheet_t *sheet, float wall_tile);

void map3d_free(map3d_t *m);

void map3d_draw(const map3d_t *m, r3d_t *r, unsigned flags);

/* Returns 1 if floor is present, 0 if hole or outside map */
int  map3d_get_height(const map3d_t *m, float x, float z, float *out_h);

/* Returns 1 if sphere of radius r touches a wall or vertical cliff, 0 otherwise */
int  map3d_is_blocked(const map3d_t *m, float x, float y, float z, float r);

/* Lua registration & bindings */
int l_map3d_build(struct lua_State *L);
int l_map3d_draw(struct lua_State *L);
int l_map3d_height(struct lua_State *L);
int l_map3d_blocked(struct lua_State *L);
int l_map3d_objects(struct lua_State *L);
int l_map3d_size(struct lua_State *L);

void map3d_register(struct lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* MAP3D_H */
