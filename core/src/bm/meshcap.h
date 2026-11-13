/*
 * Mesh capture (bm Mesh): the 3D meshes a cartridge builds in its code
 * (mesh(), mesh_sphere(), mesh_cube()), found by running its Lua in a
 * separate, closed Lua state where those functions only record what they
 * get and every other function of bm does nothing.
 */
#ifndef BM_MESHCAP_H
#define BM_MESHCAP_H

#include <stddef.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

/* Runs `lua` (len bytes, a cartridge's code) with the stand-ins, then its
 * _init, _update and _draw once each, and pushes on L:
 *   { {name=, kind=, verts={x,y,z,...}, faces={a,b,c,colour,...}, [uv={...}]}, ... }
 * (the arguments of mesh(): 1-based indices, colour -1 = textured), and a
 * message (the first error of the cartridge's code) or nil. `api`: the
 * functions of bm (their names get stand-ins); mesh/mesh_size: the
 * cartridge's MESH section, for model() and models() (may be NULL).
 * Returns 2 (values pushed). */
int bm_mesh_capture(lua_State *L, const char *lua, size_t len, int width, int height, const luaL_Reg *api,
                    const uint8_t *mesh, uint32_t mesh_size);

/* the new Lua state for the capture (luavm_newstate on the console) */
lua_State *bm_meshcap_newstate(void);

#endif
