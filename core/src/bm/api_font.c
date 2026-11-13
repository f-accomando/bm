/*
 * api_font.c: Lua font management APIs for BareMetal (/bmf directory).
 */
#include "runtime.h"
#include "rt_internal.h"
#include "gfx/font.h"
#include "gfx/font_bmf.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "lib/printf.h"
#include "lauxlib.h"
#include "lua.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/* font_list() -> { {name="terminus", file="terminus.bmf", size=4128}, ... } */
int l_font_list(lua_State *L)
{
    lua_newtable(L);
    fat_dir_t d;
    fat_entry_t e;
    int idx = 1;

    if (fat_opendir(&d, BMF_DIR) == 0) {
        while (fat_readdir(&d, &e) == 1) {
            if (e.is_dir)
                continue;
            size_t l = strlen(e.name);
            if (l > 4 && strcasecmp(e.name + l - 4, ".bmf") == 0) {
                char stem[16];
                size_t stem_len = l - 4 < 15 ? l - 4 : 15;
                memcpy(stem, e.name, stem_len);
                stem[stem_len] = 0;

                lua_newtable(L);
                lua_pushstring(L, stem);
                lua_setfield(L, -2, "name");
                lua_pushstring(L, e.name);
                lua_setfield(L, -2, "file");
                lua_pushinteger(L, (lua_Integer)e.size);
                lua_setfield(L, -2, "size");
                lua_rawseti(L, -2, idx++);
            }
        }
    }
    return 1;
}

/* font_read(name) -> bytes, or nil and error */
int l_font_read(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char path[32];
    size_t nl = strlen(name);
    if (nl > 4 && strcasecmp(name + nl - 4, ".bmf") == 0)
        ksnprintf(path, sizeof path, "%s/%s", BMF_DIR, name);
    else
        ksnprintf(path, sizeof path, "%s/%s.bmf", BMF_DIR, name);

    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    lua_pushlstring(L, (const char *)data, len);
    free(data);
    return 1;
}

/* font_write(name, bytes) -> true, or nil and error */
int l_font_write(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    size_t len = 0;
    const char *data = luaL_checklstring(L, 2, &len);

    char file[16];
    size_t nl = strlen(name);
    if (nl > 4 && strcasecmp(name + nl - 4, ".bmf") == 0)
        ksnprintf(file, sizeof file, "%s", name);
    else
        ksnprintf(file, sizeof file, "%s.bmf", name);

    if (sd_write_file("font", BMF_DIR, file, data, len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* font_delete(name) -> true, or nil and error */
int l_font_delete(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char path[32];
    size_t nl = strlen(name);
    if (nl > 4 && strcasecmp(name + nl - 4, ".bmf") == 0)
        ksnprintf(path, sizeof path, "%s/%s", BMF_DIR, name);
    else
        ksnprintf(path, sizeof path, "%s/%s.bmf", BMF_DIR, name);

    if (fat_delete(path) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
