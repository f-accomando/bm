/*
 * Unit test for 3D map engine (map3d.c, AGY-01).
 * Verifies geometry generation, height queries, collision detection,
 * objects parsing, chunk partitioning, and Lua API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "map3d.h"
#include "r3d.h"
#include "gfx16.h"

/* Test map data storage for runtime accessors stubs */
static int test_map_w = 16;
static int test_map_h = 80;
static uint16_t *test_floor = NULL;
static uint16_t *test_height = NULL;
static uint16_t *test_walls = NULL;
static uint16_t *test_objects = NULL;
static g16_sheet_t test_sheet;
static r3d_t test_r3d;
static g16_t test_g;

int rt_get_map_layer(const char *name, const uint16_t **cells, int *w, int *h)
{
    if (w) *w = test_map_w;
    if (h) *h = test_map_h;
    if (!name) return -1;
    if (!strcmp(name, "floor")) {
        if (cells) *cells = test_floor;
        return 0;
    }
    if (!strcmp(name, "height")) {
        if (cells) *cells = test_height;
        return 1;
    }
    if (!strcmp(name, "walls")) {
        if (cells) *cells = test_walls;
        return 2;
    }
    if (!strcmp(name, "objects")) {
        if (cells) *cells = test_objects;
        return 3;
    }
    return -1;
}

const g16_sheet_t *rt_get_sheet(void)
{
    return &test_sheet;
}

r3d_t *rt_get_r3d(struct lua_State *L)
{
    (void)L;
    return &test_r3d;
}

int main(void)
{
    /* 1. Build test map 8x40 tiles = 16x80 cells */
    const int tiles_x = 8;
    const int tiles_z = 40;
    test_map_w = tiles_x * 2; /* 16 */
    test_map_h = tiles_z * 2; /* 80 */
    size_t ncells = (size_t)test_map_w * (size_t)test_map_h;

    test_floor = calloc(ncells, sizeof(uint16_t));
    test_height = calloc(ncells, sizeof(uint16_t));
    test_walls = calloc(ncells, sizeof(uint16_t));
    test_objects = calloc(ncells, sizeof(uint16_t));

    assert(test_floor && test_height && test_walls && test_objects);

    /* Fill default floor on all tiles (tile ID = 1) */
    for (int tz = 0; tz < tiles_z; tz++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int c_idx = (tz * 2) * test_map_w + (tx * 2);
            test_floor[c_idx] = 1;
        }
    }

    /* Hole at tile (2, 5): floor = 0 */
    int hole_c = (5 * 2) * test_map_w + (2 * 2);
    test_floor[hole_c] = 0;

    /* Step at tile (3, 10): height = 2 -> 2 * 0.25 = 0.50 units */
    int step_c = (10 * 2) * test_map_w + (3 * 2);
    test_height[step_c] = 2;

    /* Wall block at tile (4, 15): walls = 4 -> height 4 * 0.25 = 1.0 unit */
    int wall_c = (15 * 2) * test_map_w + (4 * 2);
    test_walls[wall_c] = 4;

    /* 3 objects:
     * - tile (1, 2): 1024 PLAYER_START
     * - tile (3, 8): 1026 APPLE
     * - tile (5, 20): 1028 CRATE
     */
    test_objects[(2 * 2) * test_map_w + (1 * 2)] = 1024;
    test_objects[(8 * 2) * test_map_w + (3 * 2)] = 1026;
    test_objects[(20 * 2) * test_map_w + (5 * 2)] = 1028;

    /* 2. Test C API directly */
    map3d_t *m = map3d_create(test_map_w, test_map_h,
                              test_floor, test_height, test_walls, test_objects,
                              1.0f, 0.25f, NULL);
    assert(m != NULL);
    assert(m->nt_x == 8);
    assert(m->nt_z == 40);
    assert(m->nchunks > 0);

    /* Check height query */
    float h = -1.0f;
    /* Normal ground */
    assert(map3d_get_height(m, 1.5f, 1.5f, &h) == 1 && fabsf(h - 0.0f) < 1e-4f);
    /* Hole */
    assert(map3d_get_height(m, 2.5f, 5.5f, &h) == 0);
    /* Step */
    assert(map3d_get_height(m, 3.5f, 10.5f, &h) == 1 && fabsf(h - 0.50f) < 1e-4f);
    /* Outside */
    assert(map3d_get_height(m, -1.0f, 5.0f, &h) == 0);

    /* Check blocked query */
    /* Touches wall at tile (4, 15) */
    assert(map3d_is_blocked(m, 4.5f, 0.5f, 15.5f, 0.4f) == 1);
    /* Clear ground away from wall */
    assert(map3d_is_blocked(m, 1.5f, 0.5f, 1.5f, 0.4f) == 0);

    /* Check objects */
    assert(m->nobjs == 3);
    assert(!strcmp(m->objs[0].kind, "PLAYER_START"));
    assert(fabsf(m->objs[0].x - 1.5f) < 1e-4f && fabsf(m->objs[0].z - 2.5f) < 1e-4f);
    assert(!strcmp(m->objs[1].kind, "APPLE"));
    assert(fabsf(m->objs[1].x - 3.5f) < 1e-4f && fabsf(m->objs[1].z - 8.5f) < 1e-4f);
    assert(!strcmp(m->objs[2].kind, "CRATE"));
    assert(fabsf(m->objs[2].x - 5.5f) < 1e-4f && fabsf(m->objs[2].z - 20.5f) < 1e-4f);

    /* Test draw call without crash */
    memset(&test_g, 0, sizeof(test_g));
    test_g.w = 64;
    test_g.h = 64;
    uint16_t *fake_px = calloc(64 * 64, sizeof(uint16_t));
    test_g.px = fake_px;
    r3d_init(&test_r3d, &test_g);
    map3d_draw(m, &test_r3d, 0);
    /* the far limit: a tiny one skips every piece, a huge one keeps what the plain draw gave */
    const uint32_t all = test_r3d.tris_in;
    test_r3d.tris_in = 0;
    map3d_draw_far(m, &test_r3d, 0, 1e-3f);
    assert(test_r3d.tris_in == 0);
    map3d_draw_far(m, &test_r3d, 0, 1e6f);
    assert(test_r3d.tris_in == all);
    r3d_free(&test_r3d);
    free(fake_px);

    map3d_free(m);

    /* 3. Test Lua API */
    lua_State *L = luaL_newstate();
    assert(L != NULL);
    luaL_openlibs(L);

    map3d_register(L);

    const char *test_lua =
        "local w = map3d_build({cell = 1.0, step = 0.25})\n"
        "assert(w ~= nil, 'w must not be nil')\n"
        "local nx, nz = map3d_size(w)\n"
        "assert(nx == 8 and nz == 40, 'size must be 8x40')\n"
        "assert(map3d_height(w, 1.5, 1.5) == 0.0, 'height at 1.5, 1.5 must be 0')\n"
        "assert(map3d_height(w, 2.5, 5.5) == nil, 'hole must be nil')\n"
        "assert(map3d_height(w, 3.5, 10.5) == 0.5, 'step must be 0.5')\n"
        "assert(map3d_blocked(w, 4.5, 0.5, 15.5, 0.4) == true, 'wall must be blocked')\n"
        "assert(map3d_blocked(w, 1.5, 0.5, 1.5, 0.4) == false, 'open ground must not be blocked')\n"
        "local objs = map3d_objects(w)\n"
        "assert(#objs == 3, 'must have 3 objects')\n"
        "local apples = map3d_objects(w, 'APPLE')\n"
        "assert(#apples == 1 and apples[1].kind == 'APPLE', 'must have 1 apple')\n";

    if (luaL_dostring(L, test_lua) != LUA_OK) {
        fprintf(stderr, "Lua test error: %s\n", lua_tostring(L, -1));
        return 1;
    }

    lua_close(L);

    free(test_floor);
    free(test_height);
    free(test_walls);
    free(test_objects);

    printf("map3d OK\n");
    return 0;
}
