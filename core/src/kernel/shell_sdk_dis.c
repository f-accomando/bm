/*
 * Shell SDK: the ASM listing of a ROM (see shell_sdk_dis.h). Original code.
 */
#include "shell_sdk_dis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "script/luavm.h"

extern const uint8_t bm_lib_snes_asm[], bm_lib_snes_asm_end[];
extern const uint8_t bm_lib_snesrom[], bm_lib_snesrom_end[];
extern const uint8_t bm_lib_sdk_dis[], bm_lib_sdk_dis_end[];

/* The Lua that joins the three modules. snesrom finds the assembler as SNES_ASM in an environment of its own. */
static const char driver[] =
    "local A, rom_src, D, bytes = ...\n"
    "local env = setmetatable({ SNES_ASM = A }, { __index = _G })\n"
    "local R = assert(load(rom_src, '=snesrom', 't', env))()\n"
    "return D.listing(A, R, bytes)\n";

static int load_chunk(lua_State *L, const uint8_t *from, const uint8_t *to, const char *name)
{
    return luaL_loadbufferx(L, (const char *)from, (size_t)(to - from), name, "t") != LUA_OK ? -1 : 0;
}

static void first_line(char *err, size_t en, const char *msg)
{
    size_t n = 0;
    while (msg && msg[n] && msg[n] != '\n' && n + 1 < en) {
        err[n] = msg[n];
        n++;
    }
    err[n] = 0;
}

int sdk_dis(const uint8_t *rom, size_t len, char **text, size_t *text_len, char *err, size_t en)
{
    lua_State *L = luavm_state();
    if (!L)
        L = luavm_init();
    if (!L) {
        snprintf(err, en, "no Lua in this kernel");
        return -1;
    }
    const int top = lua_gettop(L);
    int rc = -1;
    if (load_chunk(L, (const uint8_t *)driver, (const uint8_t *)driver + sizeof driver - 1, "=sdkdis") ||
        load_chunk(L, bm_lib_snes_asm, bm_lib_snes_asm_end, "=snes_asm") || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    lua_pushlstring(L, (const char *)bm_lib_snesrom, (size_t)(bm_lib_snesrom_end - bm_lib_snesrom));
    if (load_chunk(L, bm_lib_sdk_dis, bm_lib_sdk_dis_end, "=sdk_dis") || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    lua_pushlstring(L, (const char *)rom, len);
    if (lua_pcall(L, 4, 2, 0) != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    if (lua_isstring(L, -2)) {
        size_t n;
        const char *s = lua_tolstring(L, -2, &n);
        *text = malloc(n + 1);
        if (!*text) {
            snprintf(err, en, "not enough memory for the listing");
            goto done;
        }
        memcpy(*text, s, n + 1);
        *text_len = n;
        rc = 0;
    } else {
        first_line(err, en, lua_isstring(L, -1) ? lua_tostring(L, -1) : "the listing failed");
    }
done:
    lua_settop(L, top);
    lua_gc(L, LUA_GCCOLLECT);                     /* the copy of the ROM and the tables of the sweep */
    return rc;
}
