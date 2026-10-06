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
extern const uint8_t bm_lib_padtype[], bm_lib_padtype_end[];
extern const uint8_t bm_lib_riff[], bm_lib_riff_end[];

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
    { "padtype", bm_lib_padtype, bm_lib_padtype_end },  /* typing with the pad */
    { "riff", bm_lib_riff, bm_lib_riff_end },           /* the language of patterns (music) */
};

#define LOADED "bm.loaded"

/* For riff's live code (riff.code): Lua text, never bytecode, compiled
 * with env as its globals. The sandbox has no load(): a cartridge can run
 * only text, which is what it could write in its own code. Returns the
 * function, or nil and the message. */
static int l_loadtext(lua_State *L)
{
    size_t n;
    const char *src = luaL_checklstring(L, 1, &n);
    const char *name = luaL_optstring(L, 2, "=riff");
    if (luaL_loadbufferx(L, src, n, name, "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (!lua_isnoneornil(L, 3)) {
        lua_pushvalue(L, 3);
        if (!lua_setupvalue(L, -2, 1))
            lua_pop(L, 1);
    }
    return 1;
}

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
        int nargs = 1;
        if (!strcmp(name, "riff")) {
            lua_pushcfunction(L, l_loadtext);
            nargs = 2;
        }
        lua_call(L, nargs, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);
        return 1;
    }
    return luaL_error(L, "module '%s' not found (built in: assist, bm3d, bmlib, bmnet, predict, words, padtype, riff)", name);
}

void bm_require_open(lua_State *L)
{
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, LOADED);
    lua_register(L, "require", l_require);
}
