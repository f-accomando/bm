/*
 * api_docs.c: documents (bm Write): the files of /docs on the SD card.
 * Moved from runtime.c (docs/NEXT.md, item 0b step 5). It shares the run
 * state through rt_internal.h.
 */
#ifdef BM_RGB30
#include "display.h"            /* fb_init_game */
#endif
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "world3d.h"
#include "map3d.h"
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "platform.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "vid.h"
#include "rec.h"
#include "lib/crc32.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "audio/presets.h"
#include "audio/lua_tone.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "n8lua.h"
#include "shot.h"
#include "webfile.h"
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"
#include "profile.h"
#include "project.h"
#include "rt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* ---- documents (bm Write, 2026-10-06): the files of /docs on the SD card,
 * shared by the apps that ask for them (the player allows it the first
 * time: PERM_DOCS). Names 8.3, no folders ("letter.bmd", "letter.pdf"). */

#define DOCS_DIR    "/docs"
#define DOC_MAX     (4u * 1024 * 1024)

/* a valid 8.3 name, lower case, in out (13 bytes); 0, or -1 */
static int doc_name(const char *in, char out[13])
{
    int base = 0, ext = -1, n = 0;
    for (const char *p = in; *p; p++) {
        const char c = *p;
        if (c == '.') {
            if (ext >= 0 || !base)
                return -1;
            ext = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-') {
            if (ext >= 0 ? ++ext > 3 : ++base > 8)
                return -1;
        } else {
            return -1;
        }
        out[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[n] = 0;
    return base && ext != 0 ? 0 : -1;
}

static int doc_check(lua_State *L, int idx, char name[13])
{
    const char *in = luaL_checkstring(L, idx);
    if (doc_name(in, name) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: a document's name is 8.3, like letter.bmd", in);
        return 2;
    }
    if (!perm_allowed(PERM_DOCS)) {
        lua_pushnil(L);
        lua_pushstring(L, "the player did not allow this app the documents");
        return 2;
    }
    return 0;
}

/* doc_list() -> { {name =, size =}, ... } of /docs (none: empty); nil and
 * why if not allowed */
int l_doc_list(lua_State *L)
{
    if (!perm_allowed(PERM_DOCS)) {
        lua_pushnil(L);
        lua_pushstring(L, "the player did not allow this app the documents");
        return 2;
    }
    lua_newtable(L);
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, DOCS_DIR) != 0)
        return 1;
    int n = 0;
    while (fat_readdir(&d, &e) > 0) {
        if (e.is_dir)
            continue;
        lua_createtable(L, 0, 2);
        lua_pushstring(L, e.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)e.size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

/* doc_read(name) -> its bytes, or nil and why */
int l_doc_read(lua_State *L)
{
    char name[13], path[24];
    const int r = doc_check(L, 1, name);
    if (r)
        return r;
    ksnprintf(path, sizeof path, "%s/%s", DOCS_DIR, name);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    if (fat_find(path, &e) != 0 || e.is_dir) {
        lua_pushnil(L);
        lua_pushfstring(L, "no document %s", name);
        return 2;
    }
    if (e.size > DOC_MAX || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.size > DOC_MAX ? "more than 4 MB" : fat_error());
        return 2;
    }
    lua_pushlstring(L, (const char *)data, len);
    free(data);
    return 1;
}

/* doc_write(name, bytes) -> true, or nil and why (new or replaced) */
int l_doc_write(lua_State *L)
{
    char name[13];
    size_t len;
    luaL_checklstring(L, 2, &len);
    const int r = doc_check(L, 1, name);
    if (r)
        return r;
    const char *data = lua_tolstring(L, 2, &len);
    if (len > DOC_MAX) {
        lua_pushnil(L);
        lua_pushstring(L, "more than 4 MB");
        return 2;
    }
    if (sd_write_file("doc", DOCS_DIR, name, data, len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* doc_delete(name) -> true, or nil and why */
int l_doc_delete(lua_State *L)
{
    char name[13], path[24];
    const int r = doc_check(L, 1, name);
    if (r)
        return r;
    ksnprintf(path, sizeof path, "%s/%s", DOCS_DIR, name);
    fat_entry_t e;
    if (fat_find(path, &e) != 0 || fat_delete(path) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "no document %s", name);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
