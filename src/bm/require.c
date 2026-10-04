/*
 * require() of the cartridges: the Lua libraries built into the kernel
 * (src/script/embed.S), loaded once per cartridge, like Lua's own require.
 */
#include "require.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

extern const uint8_t bm_lib_assist[], bm_lib_assist_end[];
extern const uint8_t bm_lib_bm3d[], bm_lib_bm3d_end[];
extern const uint8_t bm_lib_bmlib[], bm_lib_bmlib_end[];
extern const uint8_t bm_lib_bmnet[], bm_lib_bmnet_end[];
extern const uint8_t bm_lib_predict[], bm_lib_predict_end[];
extern const uint8_t bm_lib_words[], bm_lib_words_end[];

static const struct {
    const char *name;
    const uint8_t *src, *end;
} libs[] = {
    { "assist", bm_lib_assist, bm_lib_assist_end },
    { "bm3d", bm_lib_bm3d, bm_lib_bm3d_end },        /* bm Studio and bm Animator */
    { "bmlib", bm_lib_bmlib, bm_lib_bmlib_end },     /* what the games share (R10) */
    { "bmnet", bm_lib_bmnet, bm_lib_bmnet_end },     /* games over the network */
    { "predict", bm_lib_predict, bm_lib_predict_end },  /* word completion */
    { "words", bm_lib_words, bm_lib_words_end },        /* its dictionaries */
};

#define LOADED "bm.loaded"

static int l_require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, LOADED);
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1))
        return 1;
    lua_pop(L, 1);
    for (unsigned i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        if (strcmp(name, libs[i].name))
            continue;
        char chunk[40];
        snprintf(chunk, sizeof chunk, "=%s", name);
        if (luaL_loadbuffer(L, (const char *)libs[i].src, (size_t)(libs[i].end - libs[i].src), chunk) != LUA_OK)
            return lua_error(L);
        lua_pushstring(L, name);
        lua_call(L, 1, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);
        return 1;
    }
    return luaL_error(L, "module '%s' not found (built in: assist, bm3d, bmlib, bmnet, predict, words)", name);
}

void bm_require_open(lua_State *L)
{
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, LOADED);
    lua_register(L, "require", l_require);
}
