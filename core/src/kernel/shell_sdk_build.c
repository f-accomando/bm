/*
 * Shell SDK: the builder (see shell_sdk_build.h). Original code.
 */
#include "shell_sdk_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fat.h"
#include "lauxlib.h"
#include "lua.h"
#include "script/luavm.h"
#include "shell_sdk_cc.h"
#include "shell_sdk_music.h"

extern const uint8_t bm_lib_snes_asm[], bm_lib_snes_asm_end[];

/* The Lua that does the build. It gets the assembler module, the source, the header (a Lua table as text)
 * and the project name, and returns the ROM, or nil and the message. */
static const char driver[] =
    "local M, src, hdr, name = ...\n"
    "local h = assert(load('return ' .. hdr, '=header', 't', {}))()\n"
    "local function rd(p)\n"
    "  return bm.read(p, 8 * 1024 * 1024) or bm.read('/bmr/' .. p, 8 * 1024 * 1024)\n"
    "end\n"
    "local opts = { name = 'main.s', read = rd }\n"
    "if h.platform == 'nes' then opts.cpu = '6502' else opts.map = h.map end\n"
    "local res = M.assemble(src, opts)\n"
    "if not res.ok then return nil, M.format_errors(res) end\n"
    "local rom, info\n"
    "if h.platform == 'nes' then\n"
    "  if (h.chr_kb or 0) > 0 then h.chr = bm.read('/bmr/' .. name .. '.chr', 8 * 1024 * 1024) end\n"
    "  rom, info = M.build_nes(res, h)\n"
    "else\n"
    "  rom, info = M.build_rom(res, h)\n"
    "end\n"
    "if not rom then return nil, info end\n"
    "local used = 0\n"
    "for _, s in ipairs(res.segments) do used = used + #s.data end\n"
    "return rom, used\n";

static void first_line(char *err, size_t en, const char *msg)
{
    size_t n = 0;
    while (msg[n] && msg[n] != '\n' && n + 1 < en) {
        err[n] = msg[n];
        n++;
    }
    err[n] = 0;
}

/* The assembler's message for assembly that bm C wrote: "main.s:L: ..." gets the line of main.c. */
static void c_line(const sdk_cc_out_t *c, char *err, size_t en)
{
    const char *colon = strchr(err, ':');
    const int at = colon ? atoi(colon + 1) : 0;
    const char *why = colon ? strchr(colon + 1, ':') : NULL;
    if (at < 1 || at > c->lines || !why)
        return;
    char msg[160];
    snprintf(msg, sizeof msg, "main.c:%d: in the assembly:%s", c->line_of[at - 1], why + 1);
    snprintf(err, en, "%s", msg);
}

static int assemble(const sdk_proj_t *p, const char *name, const char *source, size_t len, uint8_t **rom,
                    size_t *rom_len, unsigned *used, char *err, size_t en);

int sdk_build_is_c(const sdk_proj_t *p) { return p->target != SDK_BM && p->cart_c; }

int sdk_build(const sdk_proj_t *p, const char *name, const char *source, size_t len, uint8_t **rom, size_t *rom_len,
              sdk_build_info_t *info, char *err, size_t en)
{
    unsigned used = 0;
    int rc, ram = -1, ram_size = 0;
    if (sdk_build_is_c(p)) {
        sdk_cc_out_t c;
        if (sdk_cc_compile(source, len, p->target == SDK_SNES, &c, err, en) != 0)
            return -1;
        rc = assemble(p, name, c.text, c.len, rom, rom_len, &used, err, en);
        if (rc != 0)
            c_line(&c, err, en);
        ram = c.ram;
        ram_size = c.ram_size;
        sdk_cc_free(&c);
    } else {
        sdk_music_src_t s = { 0 };
        if (p->target == SDK_NES && sdk_music_expand(source, len, &s, err, en) != 0)
            return -1;
        rc = assemble(p, name, s.text ? s.text : source, s.text ? s.len : len, rom, rom_len, &used, err, en);
        if (rc != 0 && s.text)
            sdk_music_error_line(&s, err, en);
        free(s.text);
    }
    if (rc == 0 && info) {
        info->rom = used;
        info->rom_size = (unsigned)(p->target == SDK_NES ? p->nes_prg_kb : p->snes_rom_kb) * 1024u;
        if (ram >= 0 && p->target == SDK_SNES)
            info->rom_size = 32768;             /* bm C keeps to the bank 0 */
        info->ram_size = ram_size;
        info->ram = ram;
    }
    return rc;
}

static int assemble(const sdk_proj_t *p, const char *name, const char *source, size_t len, uint8_t **rom,
                    size_t *rom_len, unsigned *used, char *err, size_t en)
{
    char header[384];
    if (p->target == SDK_BM) {
        snprintf(err, en, "a bm project has no ROM to build");
        return -1;
    }
    if (sdk_proj_header(p, name, header, sizeof header) < 0) {
        snprintf(err, en, "the header is too long");
        return -1;
    }
    lua_State *L = luavm_state();
    if (!L)
        L = luavm_init();
    if (!L) {
        snprintf(err, en, "no Lua in this kernel");
        return -1;
    }
    const int top = lua_gettop(L);
    int rc = -1;
    if (luaL_loadbufferx(L, (const char *)bm_lib_snes_asm, (size_t)(bm_lib_snes_asm_end - bm_lib_snes_asm),
                         "=snes_asm", "t") != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    if (luaL_loadbufferx(L, driver, sizeof driver - 1, "=sdkbuild", "t") != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    lua_pushvalue(L, -2);
    lua_pushlstring(L, source, len);
    lua_pushstring(L, header);
    lua_pushstring(L, name);
    if (lua_pcall(L, 4, 2, 0) != LUA_OK) {
        first_line(err, en, lua_tostring(L, -1));
        goto done;
    }
    if (lua_isstring(L, -2)) {
        size_t n;
        const char *data = lua_tolstring(L, -2, &n);
        *rom = malloc(n);
        if (!*rom) {
            snprintf(err, en, "not enough memory for the ROM");
            goto done;
        }
        memcpy(*rom, data, n);
        *rom_len = n;
        *used = (unsigned)lua_tointeger(L, -1);
        rc = 0;
    } else {
        first_line(err, en, lua_isstring(L, -1) ? lua_tostring(L, -1) : "the build failed");
    }
done:
    lua_settop(L, top);
    return rc;
}

int sdk_build_write(const sdk_proj_t *p, const char *name, const uint8_t *rom, size_t rom_len, char *err, size_t en)
{
    char path[40];
    const char *dir = sdk_proj_rom_dir(p);
    if (!dir || sdk_proj_rom_path(p, name, path, sizeof path) != 0) {
        snprintf(err, en, "a bm project has no ROM");
        return -1;
    }
    fat_entry_t e;
    int ok;
    if (fat_find(path, &e) == 0 && !e.is_dir) {
        ok = fat_replace(path, rom, rom_len) == 0;
    } else {
        const char *file = strrchr(path, '/') + 1;
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, file, rom, rom_len) == 0;
    }
    if (!ok) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    return 0;
}
