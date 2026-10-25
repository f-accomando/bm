/*
 * api_editor.c: the cartridge files for the editors (cart_*, res_*, ls, project state, glb3d,
 * picture3d, cutout3d, mesh_reduce).
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

static uint16_t proj_cover_w, proj_cover_h;
/* the sound bank of the project (cart_save keeps it), and of the
 * cartridge playing (a copy: its bytes are freed while it is suspended) */
/* the project's sheet as its file had it, when that is a SHEET8 section
 * (cart_load): cart_save writes that section back as it was while nothing
 * is drawn on the sheet (proj_sheet_drawn), so a big sheet stays small (a
 * 4096x3392 sheet: 5.5 MB as SHEET8, 55 MB as the SHEET it would become) */

void set_copy(uint8_t **dst, uint32_t *dlen, const void *src, uint32_t len)
{
    free(*dst);
    *dst = NULL;
    *dlen = 0;
    if (src && len && (*dst = malloc(len)) != NULL) {
        memcpy(*dst, src, len);
        *dlen = len;
    }
}

/* the project's other sections (3D models and skeletons, and any type this
 * kernel does not know), written back by cart_save */
#define PROJ_EXTRA_MAX 16
static struct { uint32_t type, size; uint8_t *data; } proj_extra[PROJ_EXTRA_MAX];
static int proj_extras;

void extras_free(void)
{
    for (int i = 0; i < proj_extras; i++)
        free(proj_extra[i].data);
    proj_extras = 0;
}

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* After bm_parse (the section table is known to be in bounds). */
static int extras_keep(const uint8_t *d)
{
    extras_free();
    for (unsigned i = 0; i < d[17]; i++) {
        const uint8_t *e = d + BM_HEADER_SIZE + i * 16;
        uint32_t type = rd32le(e), off = rd32le(e + 4), size = rd32le(e + 8);
        if (type == BM_SEC_LUA || type == BM_SEC_SHEET || type == BM_SEC_SHEET8 ||
            type == BM_SEC_MAP || type == BM_SEC_LAYERS || type == BM_SEC_FLAGS || type == BM_SEC_SPRITES ||
            type == BM_SEC_BOXES || type == BM_SEC_COVER || !size)
            continue;                       /* cart_save writes these from the project */
        if (type == BM_SEC_AUDIO) {
            if (size >= 4 && memcmp(d + off, "BMAU", 4) == 0)
                continue;                   /* the sound bank: proj_audio */
            type = BM_SEC_MESH;             /* MESH of the first bm Studio files */
        } else if (type == BM_SEC_OLD_ANIM) {
            type = BM_SEC_ANIM;
        }
        if (proj_extras == PROJ_EXTRA_MAX)
            return -1;
        uint8_t *copy = malloc(size);
        if (!copy)
            return -1;
        memcpy(copy, d + off, size);
        proj_extra[proj_extras].type = type;
        proj_extra[proj_extras].size = size;
        proj_extra[proj_extras++].data = copy;
    }
    return 0;
}
static char run_request[64], tool_request[16];
static char arg_path[64], arg_error[512];
static int arg_back = 1;

void bm_set_arg(const char *path, const char *error)
{
    ksnprintf(arg_path, sizeof arg_path, "%s", path ? path : "");
    ksnprintf(arg_error, sizeof arg_error, "%s", error ? error : "");
}

void bm_set_arg_back(int back)
{
    arg_back = back;
}

static bm_stats_t arg_run;
static int arg_has_run;
static char arg_from[16];

void bm_set_arg_from(const char *tool)
{
    ksnprintf(arg_from, sizeof arg_from, "%s", tool ? tool : "");
}

void bm_set_arg_run(const bm_stats_t *st)
{
    arg_has_run = st != NULL;
    if (st)
        arg_run = *st;
}

int tool_mode;

void bm_set_tool(int on)
{
    tool_mode = on;
}

/* Where a cartridge may write a cartridge file. The tools built into the
 * kernel: projects (.bme) and any other file, but never a game (.bm, .b16:
 * read only, project.h): saving one asks to make its editable copy (in /bme)
 * and writes that, and a new one becomes a project in /bme (a game is made by building
 * one, cart_build). The others (SD card, Market): only .bm and .bme files in
 * /carts, new ones or the ones they made in this run, never the kernel, the
 * settings or another folder. 0 and the file to write in `to`, or -1 with a
 * message on the Lua stack (false, message). */
#define MADE_MAX 16
static char made[MADE_MAX][16];         /* the files this run made (names in /carts) */

static int write_target(lua_State *L, const char *path, char *to, size_t tn)
{
    ksnprintf(to, tn, "%s", path);
    if (tool_mode) {
        if (!bm_is_game(path))
            return 0;
        fat_entry_t e;
        if (fat_find(path, &e) != 0) {
            /* a new game: its project ("/carts/new.bm" -> "/bme/new.bme") */
            bm_project_path(path, to, tn);
            return 0;
        }
        char copy[96], err[64];
        if (bm_copy_name(path, copy, sizeof copy) != 0) {
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "%s is a game, read only, and its copy has no free name", path);
            return -1;
        }
        if (!copy_question(path, copy)) {
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "%s is a game, read only: no editable copy made", path);
            return -1;
        }
        if (bm_make_copy(path, copy, err, sizeof err) != 0) {
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "%s: %s", copy, err);
            return -1;
        }
        ksnprintf(to, tn, "%s", copy);
        return 0;
    }
    const char *name = path;
    if (strncmp(path, "/carts/", 7) == 0)
        name = path + 7;
    size_t n = strlen(name);
    int ok = name[0] && name[0] != '.' && !strchr(name, '/') && !strchr(name, '\\') &&
             (bm_is_project(name) || (n > 3 && name[n - 3] == '.' && (name[n - 2] | 32) == 'b' && (name[n - 1] | 32) == 'm'));
    if (!ok) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "%s: a cartridge writes only .bm and .bme files in /carts", path);
        return -1;
    }
    if (reserved_tool_name(name)) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "%s: that name is kept for one of bm's tools", path);
        return -1;
    }
    /* and only new ones, or the ones it made in this run: a game does not
     * change another (one from the Market could rewrite the code of every
     * game on the card, 2026-10-05); bm's own tools can */
    char full[96];
    fat_entry_t e;
    ksnprintf(full, sizeof full, "/carts/%s", name);
    if (fat_find(full, &e) == 0) {
        for (int i = 0; i < n_made; i++)
            if (strcasecmp(made[i], name) == 0)
                return 0;
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "%s: a cartridge cannot change a file that is there already (bm's tools can)", path);
        return -1;
    }
    if (n_made < MADE_MAX && n < sizeof made[0])
        ksnprintf(made[n_made++], sizeof made[0], "%s", name);
    return 0;
}

/* true, and the file written when it is not the one asked (the editable
 * copy of a game, the project of a new one) */
static int wrote(lua_State *L, const char *path, const char *to)
{
    lua_pushboolean(L, 1);
    if (!strcmp(path, to))
        return 1;
    lua_pushstring(L, to);
    return 2;
}

int bm_take_tool(char *name, size_t n)
{
    if (!tool_request[0])
        return 0;
    ksnprintf(name, n, "%s", tool_request);
    tool_request[0] = 0;
    return 1;
}

int bm_take_run(char *path, size_t n)
{
    if (!run_request[0])
        return 0;
    ksnprintf(path, n, "%s", run_request);
    run_request[0] = 0;
    return 1;
}

const char *bm_last_error(void)
{
    return last_error;
}

/* ls([dir]) -> { {name=, size=, dir=}, ... } */
int l_ls(lua_State *L)
{
    const char *dir = luaL_optstring(L, 1, "/");
    lua_newtable(L);
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, dir) != 0)
        return 1;
    int i = 1;
    while (fat_readdir(&d, &e)) {
        if (e.name[0] == '.')
            continue;
        lua_newtable(L);
        lua_pushstring(L, e.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)e.size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, e.is_dir);
        lua_setfield(L, -2, "dir");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static int res_width(const char *res)
{
    return strcmp(res, "320x180") == 0 ? 320 : strcmp(res, "256x256") == 0 ? 256
         : strcmp(res, "360x360") == 0 ? 360 : strcmp(res, "480x270") == 0 ? 480
         : strcmp(res, "720x720") == 0 ? 720 : 640;
}

static void push_project(lua_State *L, const char *title, const char *author, int w, const char *lua,
                         size_t lua_len)
{
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, rt_res_name(w));
    lua_setfield(L, -2, "res");
    lua_pushlstring(L, lua, lua_len);
    lua_setfield(L, -2, "lua");
    lua_pushinteger(L, rt.sheet.w);
    lua_setfield(L, -2, "sheet_w");
    lua_pushinteger(L, rt.sheet.h);
    lua_setfield(L, -2, "sheet_h");
    lua_pushinteger(L, rt.map.w);
    lua_setfield(L, -2, "map_w");
    lua_pushinteger(L, rt.map.h);
    lua_setfield(L, -2, "map_h");
    lua_createtable(L, rt.nlayers, 0);
    for (int i = 0; i < rt.nlayers; i++) {
        lua_pushstring(L, rt.layer_name[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "layers");
}

/* cart_load(path) -> project table, or nil and a message. The cartridge's
 * sprite sheet and map replace the running cartridge's own. */
int l_cart_load(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    later_paint();                      /* the frame held's 2D with the sheet and map it had (bm3d 6.9) */
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    free_assets();
    if (load_assets(&c) != 0 || extras_keep(data) != 0) {
        free(data);
        return luaL_error(L, "not enough memory for the cartridge");
    }
    free(proj_cover);
    proj_cover = NULL;
    if (c.cover_rgba && (proj_cover = malloc((size_t)c.cover_w * c.cover_h * 4)) != NULL) {
        memcpy(proj_cover, c.cover_rgba, (size_t)c.cover_w * c.cover_h * 4);
        proj_cover_w = c.cover_w;
        proj_cover_h = c.cover_h;
    }
    set_copy(&proj_audio, &proj_audio_len, c.audio, c.audio_size);
    set_copy(&proj_sheet8, &proj_sheet8_len, c.sheet8, c.sheet8 ? c.sheet8_size : 0);
    proj_sheet_drawn = 0;
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    push_project(L, title, author, c.width, c.lua, c.lua_size);
    if (c.sheet8) {                     /* palette = the opaque colours of its SHEET8 palette */
        const uint8_t *p = c.sheet8;
        int nc = p[4] | p[5] << 8, k = 0;
        lua_createtable(L, nc, 0);
        for (int i = 0; i < nc; i++) {
            const uint8_t *e = p + 8 + i * 4;
            if (e[3] < 128)
                continue;
            lua_pushinteger(L, (lua_Integer)(e[0] << 16 | e[1] << 8 | e[2]));
            lua_rawseti(L, -2, ++k);
        }
        lua_setfield(L, -2, "palette");
    }
    free(data);
    return 1;
}

/* cart_sheet([w, h]) -> the width and height of the project's sprite sheet;
 * with w and h (multiples of 8, 8 to 4096) it gets that size: the pixels
 * that fit stay where they are, the new ones are transparent (bm Pixel) */
int l_cart_sheet(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        int w = (int)luaL_checkinteger(L, 1), h = (int)luaL_checkinteger(L, 2);
        luaL_argcheck(L, w >= G16_CELL && w <= BM_SHEET_MAX && w % G16_CELL == 0, 1, "8 to 4096, a multiple of 8");
        luaL_argcheck(L, h >= G16_CELL && h <= BM_SHEET_MAX && h % G16_CELL == 0, 2, "8 to 4096, a multiple of 8");
        if (w != rt.sheet.w || h != rt.sheet.h) {
            later_paint();              /* the frame held's sprites as they were (bm3d 6.9) */
            sync3d();                   /* the waiting 3D may use the sheet as texture */
            g16_sheet_t ns;
            const int nper = w / G16_CELL, nrows = h / G16_CELL, oper = rt.sheet.w / G16_CELL;
            uint8_t *dirty = calloc((size_t)nper * nrows, 1);
            uint8_t *flags = calloc((size_t)nper * nrows, 1);
            if (!dirty || !flags || g16_sheet_alloc(&ns, w, h) != 0) {
                free(dirty);
                free(flags);
                return luaL_error(L, "not enough memory for a %dx%d sheet", w, h);
            }
            for (int i = 0; rt.flags && i < sheet_cells(); i++)      /* the flags stay with their cell */
                if (i % oper < nper && i / oper < nrows)
                    flags[i / oper * nper + i % oper] = rt.flags[i];
            free(rt.flags);
            rt.flags = flags;
            int cw = w < rt.sheet.w ? w : rt.sheet.w, ch = h < rt.sheet.h ? h : rt.sheet.h;
            for (int y = 0; rt.sheet.px && y < ch; y++) {
                memcpy(ns.px + (size_t)y * w, rt.sheet.px + (size_t)y * rt.sheet.w, (size_t)cw * 2);
                memcpy(ns.alpha + (size_t)y * w, rt.sheet.alpha + (size_t)y * rt.sheet.w, (size_t)cw);
            }
            g16_sheet_free(&rt.sheet);
            rt.sheet = ns;                  /* the same struct: meshes keep their texture */
            free(rt.cell_dirty);
            rt.cell_dirty = dirty;
            memset(rt.cell_dirty, 1, (size_t)(w / G16_CELL) * (h / G16_CELL));
            rt.sheet_dirty = 1;
            proj_sheet_drawn = 1;
            sheet_commit();
        }
    }
    lua_pushinteger(L, rt.sheet.w);
    lua_pushinteger(L, rt.sheet.h);
    return 2;
}

/* The project's sheet as a section for cart_write: SHEET8 when it has at
 * most 256 colours (transparent counts as one), else SHEET. A pixel whose
 * RGB565 is the one it had in `old` (the file being rewritten, the same x
 * and y) keeps its 24 bits from there, so what was not drawn on comes back
 * byte for byte; a pixel drawn on takes the 24 bits of the first colour of
 * the palette (the table at index `pal`, bm Pixel's) with its RGB565, else
 * of a colour of the old sheet, else RGB565 widened. The palette's colours
 * come first in the SHEET8 palette, as they are and in their order (also
 * those no pixel uses) while there is room, so it comes back as it was;
 * then the other colours, in the order they appear. NULL without memory. */
#define SHEET_CLEAR 0x01000000u                 /* a transparent pixel */
#define SHEET_HASH  1024                        /* > 256 colours: open addressing */

typedef struct {
    uint32_t key[SHEET_HASH];                   /* colour + 1, 0 = empty */
    int16_t slot[SHEET_HASH];                   /* its palette index, -1 none yet */
    int n;
} colour_set_t;

static int cs_find(colour_set_t *s, uint32_t c, int add)
{
    uint32_t i = (c * 2654435761u) >> 22;
    while (s->key[i] && s->key[i] != c + 1)
        i = (i + 1) & (SHEET_HASH - 1);
    if (!s->key[i]) {
        if (!add || s->n >= 257)
            return -1;
        s->key[i] = c + 1;
        s->slot[i] = -1;
        s->n++;
    }
    return (int)i;
}

static void orig_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    uint32_t *o = ctx;
    uint32_t w = o[0];
    o[1 + (uint32_t)y * w + (uint32_t)x] = rgba[3] >= 128 ? (uint32_t)(rgba[0] << 16 | rgba[1] << 8 | rgba[2])
                                                           : SHEET_CLEAR;
}

static uint8_t *sheet_section(lua_State *L, int pal, const bm_cart_t *old, uint32_t *type, uint32_t *size)
{
    sheet_commit();
    const uint32_t w = (uint32_t)rt.sheet.w, h = (uint32_t)rt.sheet.h, n = w * h;
    const uint32_t ow = old && (old->sheet8 || old->sheet_rgba) ? old->sheet_w : 0;
    const uint32_t oh = ow ? old->sheet_h : 0;
    uint32_t *rgb = malloc(65536 * sizeof *rgb);    /* the 24 bits of an RGB565 drawn (bit 24: not known) */
    uint32_t *fin = malloc((size_t)n * sizeof *fin); /* each pixel's colour, or SHEET_CLEAR */
    uint32_t *orig = ow ? malloc((1 + (size_t)ow * oh) * sizeof *orig) : NULL;
    uint32_t pc[256];                           /* the palette given */
    int npc = 0;
    colour_set_t *set = calloc(1, sizeof *set);
    uint8_t *out = NULL, *idx = NULL;
    if (!rgb || !fin || !set || (ow && !orig))
        goto done;
    for (int k = 0; k < 65536; k++)
        rgb[k] = 1u << 24;
    if (pal && lua_istable(L, pal)) {
        lua_Integer m = luaL_len(L, pal);
        for (lua_Integer i = 1; i <= m && npc < 256; i++) {
            lua_rawgeti(L, pal, i);
            if (lua_isinteger(L, -1)) {
                uint32_t c = (uint32_t)lua_tointeger(L, -1) & 0xFFFFFF;
                pc[npc++] = c;
                if (rgb[g16_rgb24(c)] >> 24)
                    rgb[g16_rgb24(c)] = c;
            }
            lua_pop(L, 1);
        }
    }
    if (orig) {                                 /* the old sheet, pixel by pixel */
        orig[0] = ow;
        if (old->sheet8) {
            if (bm_sheet8_unpack(old, orig_set, orig) != 0)
                goto done;
        } else {
            for (uint32_t i = 0; i < ow * oh; i++) {
                const uint8_t *e = old->sheet_rgba + i * 4;
                orig[1 + i] = e[3] >= 128 ? (uint32_t)(e[0] << 16 | e[1] << 8 | e[2]) : SHEET_CLEAR;
            }
        }
        for (uint32_t i = 0; i < ow * oh; i++)
            if (orig[1 + i] != SHEET_CLEAR && rgb[g16_rgb24(orig[1 + i])] >> 24)
                rgb[g16_rgb24(orig[1 + i])] = orig[1 + i];
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x;
            if (!rt.sheet.alpha[i]) {
                fin[i] = SHEET_CLEAR;
                continue;
            }
            uint16_t k = rt.sheet.px[i];
            uint32_t o = x < ow && y < oh ? orig[1 + y * ow + x] : SHEET_CLEAR;
            if (o != SHEET_CLEAR && g16_rgb24(o) == k)
                fin[i] = o;                     /* not drawn on: as it was */
            else
                fin[i] = rgb[k] >> 24 ? g16_to_rgb24(k) : rgb[k];
        }
    for (uint32_t i = 0; i < n && set->n <= 256; i++)
        cs_find(set, fin[i], 1);
    if (set->n > 256) {                         /* SHEET: RGBA */
        *type = BM_SEC_SHEET;
        *size = 4 + n * 4;
        out = malloc(*size);
        if (!out)
            goto done;
        out[0] = (uint8_t)w; out[1] = (uint8_t)(w >> 8);
        out[2] = (uint8_t)h; out[3] = (uint8_t)(h >> 8);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *e = out + 4 + i * 4;
            uint32_t c = fin[i];
            e[0] = (uint8_t)(c >> 16); e[1] = (uint8_t)(c >> 8); e[2] = (uint8_t)c;
            e[3] = c == SHEET_CLEAR ? 0 : 255;
            if (c == SHEET_CLEAR) e[0] = e[1] = e[2] = 0;
        }
        goto done;
    }
    uint8_t pal_rgba[256 * 4];
    int ncol = 0, waiting = set->n;             /* colours used with no entry yet */
    int clear_at = -1, clr = -1;                /* the old palette's transparent entry keeps its place */
    if (orig && old->sheet8 && old->sheet8_size >= 8) {
        int on = old->sheet8[4] | old->sheet8[5] << 8;
        for (int j = 0; j < on && 12 + (uint32_t)j * 4 <= old->sheet8_size; j++)
            if (old->sheet8[8 + j * 4 + 3] < 128) {
                clear_at = j;
                break;
            }
        clr = clear_at >= 0 ? cs_find(set, SHEET_CLEAR, 0) : -1;
    }
    for (int j = 0; j <= npc; j++) {
        if (clr >= 0 && set->slot[clr] < 0 && ncol == clear_at) {
            memset(pal_rgba + ncol * 4, 0, 4);
            set->slot[clr] = (int16_t)ncol++;
            waiting--;
        }
        if (j == npc)
            break;
        int at = cs_find(set, pc[j], 0);
        int takes = at >= 0 && set->slot[at] < 0;
        if (takes || ncol + waiting < 256) {
            if (takes) {
                set->slot[at] = (int16_t)ncol;
                waiting--;
            }
            uint8_t *e = pal_rgba + ncol++ * 4;
            e[0] = (uint8_t)(pc[j] >> 16); e[1] = (uint8_t)(pc[j] >> 8); e[2] = (uint8_t)pc[j]; e[3] = 255;
        }
    }
    idx = malloc(n ? n : 1);
    if (!idx)
        goto done;
    for (uint32_t i = 0; i < n; i++) {
        int at = cs_find(set, fin[i], 0);
        if (set->slot[at] < 0) {                /* the others, as they appear */
            uint8_t *e = pal_rgba + ncol * 4;
            uint32_t c = fin[i];
            if (c == SHEET_CLEAR) {
                e[0] = e[1] = e[2] = e[3] = 0;
            } else {
                e[0] = (uint8_t)(c >> 16); e[1] = (uint8_t)(c >> 8); e[2] = (uint8_t)c; e[3] = 255;
            }
            set->slot[at] = (int16_t)ncol++;
        }
        idx[i] = (uint8_t)set->slot[at];
    }
    if (ncol == 0) {                            /* no pixels: one transparent colour */
        memset(pal_rgba, 0, 4);
        ncol = 1;
    }
    size_t len = 0;
    out = bm_sheet8_pack((int)w, (int)h, pal_rgba, ncol, idx, &len);
    *type = BM_SEC_SHEET8;
    *size = (uint32_t)len;
done:
    free(idx);
    free(rgb);
    free(fin);
    free(orig);
    free(set);
    return out;
}

/* cart_new(): an empty 256x256 sprite sheet and map, no cover */
int l_cart_new(lua_State *L)
{
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    later_paint();                      /* the frame held's 2D with the sheet and map it had (bm3d 6.9) */
    sync3d();
    free_assets();
    if (load_assets(&c) != 0)
        return luaL_error(L, "not enough memory for the cartridge");
    free(proj_cover);
    proj_cover = NULL;
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    set_copy(&proj_sheet8, &proj_sheet8_len, NULL, 0);
    proj_sheet_drawn = 0;
    extras_free();
    return 0;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* "/carts/game.bm" -> "/carts", "game.bm"; a bare name goes to /carts */
static void split_path(const char *path, char *dir, size_t dn, char *name, size_t nn)
{
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n >= dn) n = dn - 1;
        memcpy(dir, path, n);
        dir[n] = 0;
        if (!dir[0]) ksnprintf(dir, dn, "/");
        ksnprintf(name, nn, "%s", slash + 1);
    } else {
        ksnprintf(dir, dn, "/carts");
        ksnprintf(name, nn, "%s", path);
    }
}

static const char *field(lua_State *L, int t, const char *k, const char *def)
{
    lua_getfield(L, t, k);
    const char *s = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return s;
}

/* cart_save(path, {title=, author=, res=, lua=}) -> true, or false and a
 * message. Sprite sheet, map (its layers), the flags of the tiles and the
 * named zones are the running cartridge's; the file name must be 8.3 (e.g.
 * "/carts/mygame.bm"). */
int l_cart_save(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    char to[96];
    if (write_target(L, path, to, sizeof to))
        return 2;
    const char *title = field(L, 2, "title", ""), *author = field(L, 2, "author", "");
    const char *res = field(L, 2, "res", "640x360");
    lua_getfield(L, 2, "lua");
    size_t lua_len;
    const char *lua = luaL_checklstring(L, -1, &lua_len);
    int w = res_width(res), h = w == 256 || w == 360 || w == 720 ? w : w * 9 / 16;

    char dir[64], name[16];
    split_path(to, dir, sizeof dir, name, sizeof name);

    const uint32_t sw = (uint32_t)rt.sheet.w, sh = (uint32_t)rt.sheet.h;
    const uint32_t mw = (uint32_t)rt.map.w, mh = (uint32_t)rt.map.h;
    /* the map's other layers (or the name of the only one), and the flags
     * of the sheet's cells up to the last row that has some */
    const int named = rt.nlayers > 1 || strcmp(rt.layer_name[0], "main");
    const uint32_t per = sw / G16_CELL;
    uint32_t frows = 0;
    for (uint32_t i = 0; rt.flags && i < (uint32_t)sheet_cells(); i++)
        if (rt.flags[i])
            frows = i / per + 1;
    /* the sheet as the file had it (SHEET8, byte for byte) while nothing is
     * drawn on it, else whole as SHEET */
    const int keep8 = proj_sheet8 && !proj_sheet_drawn && proj_sheet8_len >= 8 &&
                      (uint32_t)(proj_sheet8[0] | proj_sheet8[1] << 8) == sw &&
                      (uint32_t)(proj_sheet8[2] | proj_sheet8[3] << 8) == sh;
    /* cover (first: the menu reads only the start), code, sheet, map, its
     * layers, the sound bank, the flags, the sheet's named zones and their
     * boxes, then the sections kept from the file (3D models...) */
    enum { FIXED = 9 };
    const int nsec = FIXED + proj_extras;
    uint32_t sizes[FIXED + PROJ_EXTRA_MAX] = {
        proj_cover ? 4u + (uint32_t)proj_cover_w * proj_cover_h * 4 : 0, (uint32_t)lua_len,
        keep8 ? proj_sheet8_len : 4 + sw * sh * 4,
        4 + mw * mh * 2, named ? 8u + (uint32_t)rt.nlayers * BM_LAYER_NAME + (uint32_t)(rt.nlayers - 1) * mw * mh * 2 : 0,
        proj_audio_len, frows ? 4 + per * frows : 0, rt.nzones ? 4u + (uint32_t)rt.nzones * BM_SPRITE_SIZE : 0,
        rt.nzones && rt.nboxes ? 4u + (uint32_t)rt.nboxes * BM_BOX_SIZE : 0 };
    uint32_t types[FIXED + PROJ_EXTRA_MAX] = { BM_SEC_COVER, BM_SEC_LUA, keep8 ? BM_SEC_SHEET8 : BM_SEC_SHEET,
                                               BM_SEC_MAP, BM_SEC_LAYERS, BM_SEC_AUDIO, BM_SEC_FLAGS, BM_SEC_SPRITES,
                                               BM_SEC_BOXES };
    for (int i = 0; i < proj_extras; i++) {
        types[FIXED + i] = proj_extra[i].type;
        sizes[FIXED + i] = proj_extra[i].size;
    }
    uint32_t count = 0, total = BM_HEADER_SIZE;
    for (int i = 0; i < nsec; i++)
        if (sizes[i]) { count++; total += 16 + ((sizes[i] + 3) & ~3u); }
    uint8_t *buf = calloc(total, 1);
    if (!buf)
        return luaL_error(L, "not enough memory to save");
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
    for (int i = 0; i < nsec; i++) {
        if (!sizes[i]) continue;
        put32(tab, types[i]);
        put32(tab + 4, (uint32_t)(p - buf));
        put32(tab + 8, sizes[i]);
        tab += 16;
        if (i == 0) {
            put16(p, proj_cover_w); put16(p + 2, proj_cover_h);
            memcpy(p + 4, proj_cover, sizes[i] - 4);
        } else if (i == 1) {
            memcpy(p, lua, lua_len);
        } else if (i == 2 && keep8) {
            memcpy(p, proj_sheet8, proj_sheet8_len);
        } else if (i == 2) {
            sheet_commit();
            put16(p, sw); put16(p + 2, sh);
            for (uint32_t k = 0; k < sw * sh; k++) {
                uint32_t rgb = g16_to_rgb24(rt.sheet.px[k]);
                uint8_t *q = p + 4 + k * 4;
                q[0] = (uint8_t)(rgb >> 16); q[1] = (uint8_t)(rgb >> 8); q[2] = (uint8_t)rgb;
                q[3] = rt.sheet.alpha[k] ? 255 : 0;
            }
        } else if (i == 3) {
            put16(p, mw); put16(p + 2, mh);
            for (uint32_t k = 0; k < mw * mh; k++)
                put16(p + 4 + k * 2, rt.map.cells[k]);
        } else if (i == 4) {
            put16(p, mw); put16(p + 2, mh); put16(p + 4, (uint32_t)rt.nlayers);
            uint8_t *q = p + 8;
            for (int l = 0; l < rt.nlayers; l++, q += BM_LAYER_NAME)
                memcpy(q, rt.layer_name[l], name_len(rt.layer_name[l], BM_LAYER_NAME));
            for (int l = 1; l < rt.nlayers; l++)
                for (uint32_t k = 0; k < mw * mh; k++, q += 2)
                    put16(q, rt.layer[l][k]);
        } else if (i == 5) {
            memcpy(p, proj_audio, proj_audio_len);
        } else if (i == 6) {
            put16(p, per); put16(p + 2, frows);
            memcpy(p + 4, rt.flags, per * frows);
        } else if (i == 7) {
            memcpy(p, rt.zones, sizes[i]);
        } else if (i == 8) {
            memcpy(p, rt.boxes, sizes[i]);
        } else {
            memcpy(p, proj_extra[i - FIXED].data, sizes[i]);
        }
        p += (sizes[i] + 3) & ~3u;
    }
    memcpy(buf, "BMCART\0\0", 8);
    put16(buf + 8, 1);
    put16(buf + 10, BM_HEADER_SIZE);
    put16(buf + 12, (uint32_t)w);
    put16(buf + 14, (uint32_t)h);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)count;
    strncpy((char *)buf + 24, title, 47);
    strncpy((char *)buf + 72, author, 31);
    if (bm_is_project(to))
        buf[18] |= BM_FLAG_PROJECT;
    put32(buf + 20, crc32(buf + BM_HEADER_SIZE, total - BM_HEADER_SIZE));
    int ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
    free(buf);
    if (ok)
        return wrote(L, path, to);
    lua_pushboolean(L, 0);
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_build(project, [target]) -> true and the game's path, or false and a
 * message: the .bm of a project (.bme), or its .b16 when target is "b16" (from
 * a .bme or .bm), next to it, the one it built before replaced (project.h).
 * bm's tools only (the SDK's "Build .bm" / "Export .b16"). */
int l_cart_build(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const char *target = luaL_optstring(L, 2, NULL);
    int b16 = target && strcasecmp(target, "b16") == 0;
    char out[96], err[64];
    if (!tool_mode) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "only bm's tools build a game");
        return 2;
    }
    if ((b16 ? bm_build_b16(path, out, sizeof out, err, sizeof err)
             : bm_build(path, out, sizeof out, err, sizeof err)) != 0) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "%s: %s", path, err);
        return 2;
    }
    kprintf("bm: built %s from %s\n", out, path);
    lua_pushboolean(L, 1);
    lua_pushstring(L, out);
    return 2;
}

/* cart_read(path) -> {title, author, res, lua, size}, or nil and a message.
 * Only reads: the running cartridge's sheet and map stay as they are (code
 * editors with several files open). */
int l_cart_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, rt_res_name(c.width));
    lua_setfield(L, -2, "res");
    lua_pushlstring(L, c.lua, c.lua_size);
    lua_setfield(L, -2, "lua");
    lua_pushinteger(L, (lua_Integer)len);
    lua_setfield(L, -2, "size");
    free(data);
    return 1;
}

lua_State *bm_meshcap_newstate(void)
{
    return luavm_newstate();
}

static int capture_call(lua_State *L)
{
    const bm_cart_t *c = lua_touserdata(L, 1);
    return bm_mesh_capture(L, c->lua, c->lua_size, c->width, c->height, rt_api(), c->mesh, c->mesh_size);
}

/* cart_meshes(path) -> { {name=, kind=, verts=, faces=, [uv=]}, ... }, and
 * nil or the first error of the cartridge's code; or nil and a message.
 * The 3D meshes the cartridge builds in its code (mesh(), mesh_sphere(),
 * mesh_cube()), found by running it apart with every other function of bm
 * doing nothing (meshcap.c); the fields are the arguments of mesh(). For
 * bm Mesh. */
int l_cart_meshes(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushcfunction(L, capture_call);       /* protected: `data` is freed whatever happens */
    lua_pushlightuserdata(L, &c);
    int status = lua_pcall(L, 1, 2, 0);
    free(data);
    if (status != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
    }
    return 2;
}

/* mesh_reduce(record, triangles, [bones, [max_err]]) -> record, bones, n
 * or nil and a message. Fewer triangles for one model of the MESH section
 * (src/bm/decimate.c: quadric edge collapse): `record` is the model's part
 * of the section (as bm3d.lua's encode_mesh writes it), `bones` the bone
 * of each vertex (one byte each, the ANIM section's), `max_err` stops
 * before a costlier collapse (0: none). The record comes back with at most
 * `triangles` triangles (more if nothing else can go without turning a
 * face over), with the bones of its vertices (nil without `bones`) and
 * the number of triangles. bm Studio's models page. */
int l_mesh_reduce(lua_State *L)
{
    size_t len = 0, vblen = 0;
    const uint8_t *rec = (const uint8_t *)luaL_checklstring(L, 1, &len);
    int target = (int)luaL_checkinteger(L, 2);
    const uint8_t *vb = lua_isnoneornil(L, 3) ? NULL : (const uint8_t *)luaL_checklstring(L, 3, &vblen);
    float max_err = (float)luaL_optnumber(L, 4, 0);
    if (len < 24 || len > 0x1000000) {
        lua_pushnil(L);
        lua_pushstring(L, "broken model record");
        return 2;
    }
    uint32_t nv = rec[16] | rec[17] << 8;
    if (vb && vblen != nv) {
        lua_pushnil(L);
        lua_pushstring(L, "the bones do not fit the model");
        return 2;
    }
    uint8_t *out = malloc(len), *vb_out = vb ? malloc(nv) : NULL;
    if (!out || (vb && !vb_out)) {
        free(out);
        free(vb_out);
        return luaL_error(L, "not enough memory for the model");
    }
    size_t outlen = 0;
    int n = bm_model_reduce(rec, len, vb, target, max_err, out, &outlen, vb_out);
    if (n < 0) {
        free(out);
        free(vb_out);
        lua_pushnil(L);
        lua_pushstring(L, n == -1 ? "broken model record" : "not enough memory for the model");
        return 2;
    }
    lua_pushlstring(L, (const char *)out, outlen);
    if (vb)
        lua_pushlstring(L, (const char *)vb_out, out[16] | out[17] << 8);
    else
        lua_pushnil(L);
    lua_pushinteger(L, n);
    free(out);
    free(vb_out);
    return 3;
}

/* the model table picture3d("take") and cutout3d give */
static void push_glb_model(lua_State *L, glb_model_t *m)
{
    lua_newtable(L);
    lua_pushlstring(L, (const char *)m->record, m->record_len);
    lua_setfield(L, -2, "record");
    if (m->flat) {
        lua_pushlstring(L, (const char *)m->flat, m->flat_len);
        lua_setfield(L, -2, "flat");
    }
    if (m->texture) {
        lua_pushlstring(L, (const char *)m->texture, (size_t)(m->sheet > 0 ? m->sheet : 256) * (size_t)(m->sheet > 0 ? m->sheet : 256) * 4);
        lua_setfield(L, -2, "texture");
    }
    lua_pushinteger(L, m->nv);
    lua_setfield(L, -2, "nv");
    lua_pushinteger(L, m->nf);
    lua_setfield(L, -2, "nf");
    lua_pushboolean(L, m->textured);
    lua_setfield(L, -2, "textured");
}

/* cutout3d(picture, {name=, lathe=, height=, depth=, segments=, faces=})
 * -> the same table as picture3d("take"), or nil and a message: a model
 * from the picture's outline, made here (src/bm/cutout.c, no network): a
 * cutout with thickness `depth` (a fraction of the height) or, with
 * lathe = true, the outline turned around the vertical axis in
 * `segments` steps. picture: a .png / .jpg on the SD card. */
int l_cutout3d(lua_State *L)
{
    const char *picture = luaL_checkstring(L, 1);
    const char *name = "model";
    cutout_opts_t o = { 0, 2.0f, 0.2f, 12, 1200, 256, 0.02f };
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "name");
        if (lua_isstring(L, -1))
            name = lua_tostring(L, -1);
        lua_getfield(L, 2, "lathe");
        o.lathe = lua_toboolean(L, -1);
        lua_getfield(L, 2, "height");
        if (lua_isnumber(L, -1))
            o.height = (float)lua_tonumber(L, -1);
        lua_getfield(L, 2, "depth");
        if (lua_isnumber(L, -1))
            o.depth = (float)lua_tonumber(L, -1);
        lua_getfield(L, 2, "segments");
        if (lua_isinteger(L, -1))
            o.segments = (int)lua_tointeger(L, -1);
        lua_getfield(L, 2, "faces");
        if (lua_isinteger(L, -1))
            o.max_faces = (int)lua_tointeger(L, -1);
        lua_pop(L, 6);
    }
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    memset(&e, 0, sizeof e);
    if (fat_find(picture, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", picture, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    glb_model_t m;
    char err[128];
    int r = cutout_from_file(data, len, name, &o, &m, err, sizeof err);
    free(data);
    if (r < 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    push_glb_model(L, &m);
    glb_model_free(&m);
    return 1;
}

/* glb3d(path, {name=, height=, faces=, sheet=}) -> the same table as
 * picture3d("take") and cutout3d, or nil and a message: a model from a
 * .glb on the SD card (the editors use /glb/NAME.glb), read only, made here
 * (src/bm/glb.c, no network). height: the model's height in blocks (2);
 * faces: reduce to at most this many triangles (0: keep all); sheet: the
 * side of the texture on the sheet (256; 8 to 1024). Only the format's
 * limits: 65535 vertices and 65535 triangles per model, refused with a
 * message. A file over GLB3D_MAX_FILE (16 MB) is refused: the file, its
 * decoded corners and the texture must fit in the RAM of the Pi Zero W. */
#define GLB3D_MAX_FILE (16u << 20)
int l_glb3d(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const char *name = NULL;
    char base[17];
    glb_opts_t o = { 2.0f, 0, 256, 1 };
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "name");
        if (lua_isstring(L, -1))
            name = lua_tostring(L, -1);
        lua_getfield(L, 2, "height");
        if (lua_isnumber(L, -1))
            o.height = (float)lua_tonumber(L, -1);
        lua_getfield(L, 2, "faces");
        if (lua_isinteger(L, -1))
            o.max_faces = (int)lua_tointeger(L, -1);
        lua_getfield(L, 2, "sheet");
        if (lua_isinteger(L, -1))
            o.sheet = (int)lua_tointeger(L, -1);
        lua_pop(L, 4);
    }
    if (o.sheet < 8 || o.sheet > 1024) {
        lua_pushnil(L);
        lua_pushstring(L, "sheet: 8 to 1024");
        return 2;
    }
    if (!name) {
        /* the file's name without its folder and its extension */
        const char *b = strrchr(path, '/');
        b = b ? b + 1 : path;
        size_t n = 0;
        while (b[n] && b[n] != '.' && n < sizeof base - 1) {
            base[n] = b[n];
            n++;
        }
        base[n] = 0;
        name = n ? base : "model";
    }
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (e.size > GLB3D_MAX_FILE) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %d MB is too big, the limit is 16 MB (the console RAM)", path, (int)(e.size >> 20));
        return 2;
    }
    if (fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, fat_error());
        return 2;
    }
    glb_model_t m;
    char err[128];
    int r = glb_to_model(data, len, name, &o, &m, err, sizeof err);
    free(data);
    if (r < 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    push_glb_model(L, &m);
    glb_model_free(&m);
    return 1;
}

/* res_write(path, kind, {sections = {[8] = MESH, [9] = ANIM, [2] = SHEET | [5] = SHEET8, [10] = INFO},
 * name =, author =, replace = false}) -> true, or nil and a message: a model file (BMRES, kind 1 or
 * "model") in /bmm, for bm's tools only. The path is /bmm/NAME.bmm, NAME 1 to 8 lower case letters, digits,
 * _ or -. The sections are strings, as cart_write takes them (the whole MESH section: the models and their
 * header); MESH is needed, the rest is optional; each is checked as the kernel loads a file (the same
 * parse). The folder is made if it is not there. A file that is there is not changed unless replace.
 * name: the name shown (48 bytes; NAME by default); author: 32 bytes. */
#define RES_NAME_MAX 8
static int res_model_path(const char *path, char *file, size_t fn)
{
    if (strncmp(path, "/bmm/", 5) != 0)
        return 0;
    const char *n = path + 5;
    size_t k = 0;
    while (n[k] && n[k] != '.') {
        const char c = n[k];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
        k++;
    }
    if (k < 1 || k > RES_NAME_MAX || strcmp(n + k, ".bmm") != 0 || k + 5 > fn)
        return 0;
    ksnprintf(file, fn, "%s", n);
    return 1;
}

int l_res_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 3, LUA_TTABLE);
    if (!tool_mode) {
        lua_pushnil(L);
        lua_pushstring(L, "res_write is for bm's tools only");
        return 2;
    }
    int kind = -1;
    if (lua_isinteger(L, 2))
        kind = (int)lua_tointeger(L, 2);
    else if (lua_isstring(L, 2) && strcmp(lua_tostring(L, 2), "model") == 0)
        kind = BM_RES_MODEL;
    if (kind != BM_RES_MODEL) {
        lua_pushnil(L);
        lua_pushstring(L, "res_write: only kind 1 (\"model\", a .bmm in /bmm)");
        return 2;
    }
    char file[16];
    if (!res_model_path(path, file, sizeof file)) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: a model goes to /bmm/NAME.bmm (NAME: 1 to 8 of a-z 0-9 _ -, lower case)", path);
        return 2;
    }
    lua_getfield(L, 3, "replace");
    const int replace = lua_toboolean(L, -1);
    lua_getfield(L, 3, "name");
    const char *name = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
    lua_getfield(L, 3, "author");
    const char *author = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    char stem[16];
    if (!name) {
        ksnprintf(stem, sizeof stem, "%s", file);
        stem[strlen(stem) - 4] = 0;
        name = stem;
    }
    lua_getfield(L, 3, "sections");
    if (!lua_istable(L, -1))
        return luaL_error(L, "res_write: sections = { [8] = MESH, ... }");
    bm_put_t put[5];
    int nput = 0, have_sheet = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        const lua_Integer t = lua_isinteger(L, -2) ? lua_tointeger(L, -2) : -1;
        if (t != BM_SEC_MESH && t != BM_SEC_ANIM && t != BM_SEC_SHEET && t != BM_SEC_SHEET8 && t != BM_SEC_INFO)
            return luaL_error(L, "sections: a model file holds 8 (MESH), 9 (ANIM), 2 (SHEET), 5 (SHEET8), 10 (INFO)");
        lua_pop(L, 1);
    }
    /* in the order of a file bmres.py makes: MESH, ANIM, the sheet, INFO */
    static const uint32_t order[5] = { BM_SEC_MESH, BM_SEC_ANIM, BM_SEC_SHEET, BM_SEC_SHEET8, BM_SEC_INFO };
    for (int k = 0; k < 5; k++) {
        lua_rawgeti(L, -1, order[k]);
        if (lua_isnil(L, -1) || (lua_isboolean(L, -1) && !lua_toboolean(L, -1))) {
            lua_pop(L, 1);
            continue;
        }
        size_t n = 0;
        const uint8_t *s = (const uint8_t *)luaL_checklstring(L, -1, &n);
        const char *bad = NULL;
        if (n > 0x1000000)
            bad = "a section over 16 MB";
        else if (order[k] == BM_SEC_MESH && bm_mesh_check(s, (uint32_t)n) < 0)
            bad = "broken MESH section";
        else if (order[k] == BM_SEC_ANIM && bm_anim_check(s, (uint32_t)n) < 0)
            bad = "broken ANIM section";
        else if (order[k] == BM_SEC_INFO && n > BM_INFO_MAX)
            bad = "INFO over 16 KB";
        if (bad) {
            lua_pushnil(L);
            lua_pushstring(L, bad);
            return 2;
        }
        have_sheet += order[k] == BM_SEC_SHEET || order[k] == BM_SEC_SHEET8;
        put[nput++] = (bm_put_t){ order[k], s, (uint32_t)n };
        lua_pop(L, 1);              /* the string stays alive in the table */
    }
    if (nput == 0 || put[0].type != BM_SEC_MESH || have_sheet > 1) {
        lua_pushnil(L);
        lua_pushstring(L, nput == 0 || put[0].type != BM_SEC_MESH ? "a model file needs its MESH section (8)"
                                                                  : "one sheet only (2 or 5)");
        return 2;
    }
    size_t out_len = 0;
    uint8_t *out = bm_res_build(BM_RES_MODEL, name, author, put, nput, &out_len);
    if (!out)
        return luaL_error(L, "not enough memory to save");
    bm_cart_t c;
    char err[64] = "";
    if (bm_parse_any(out, out_len, &c, err, sizeof err) != 0) {
        free(out);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    fat_entry_t e;
    int ok;
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) == 0) {
        if (e.is_dir || !replace) {
            free(out);
            lua_pushnil(L);
            lua_pushfstring(L, "%s: %s", path, e.is_dir ? "a directory" : "is there already (replace = true changes it)");
            return 2;
        }
        ok = fat_replace(path, out, out_len) == 0;
    } else {
        ok = fat_mkdirs("/bmm") == 0 && fat_write_file("/bmm", file, out, out_len) == 0;
    }
    free(out);
    if (!ok) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* res_read(path) -> { kind =, name =, author =, sections = { [type] = string, ... } }, or nil and a
 * message: a resource file (BMRES: a .bmm, .bmi, ...) read and checked as the kernel loads it, read only.
 * kind: 1 model, 2 image, 3 sounds, 4 map, 5 palette, 6 kit. The sections are the file's, whole (MESH is
 * the whole section res_write takes). A file over 16 MB is refused. */
int l_res_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (e.size > GLB3D_MAX_FILE) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %d MB is too big, the limit is 16 MB (the console RAM)", path, (int)(e.size >> 20));
        return 2;
    }
    if (fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, fat_error());
        return 2;
    }
    bm_cart_t c;
    char err[64] = "";
    const int bad = bm_parse_any(data, len, &c, err, sizeof err);
    if (bad != 0 || c.kind == 0) {
        free(data);
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, bad ? err : "not a resource file");
        return 2;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, c.kind);
    lua_setfield(L, -2, "kind");
    char text[49];
    memcpy(text, c.title, 48);
    text[48] = 0;
    lua_pushstring(L, text);
    lua_setfield(L, -2, "name");
    memcpy(text, c.author, 32);
    text[32] = 0;
    lua_pushstring(L, text);
    lua_setfield(L, -2, "author");
    lua_createtable(L, 0, 4);
    for (unsigned i = 0, n = data[17]; i < n; i++) {
        const uint8_t *t = data + BM_HEADER_SIZE + i * 16;
        lua_pushlstring(L, (const char *)data + (t[4] | t[5] << 8 | t[6] << 16 | (uint32_t)t[7] << 24),
                        t[8] | t[9] << 8 | t[10] << 16 | (uint32_t)t[11] << 24);
        lua_rawseti(L, -2, t[0] | t[1] << 8 | t[2] << 16);
    }
    lua_setfield(L, -2, "sections");
    free(data);
    return 1;
}

/* picture3d(action, ...): a picture becomes a 3D model through an
 * image-to-3D service (src/net/img3d.c, the .glb read by src/bm/glb.c).
 *   picture3d("providers") -> { "meshy", ... }
 *   picture3d("ready", provider) -> true, or false and why (no key in
 *     bm/config.txt: meshy_key=...)
 *   picture3d("start", picture, {provider=, polycount=}) -> the job's id,
 *     or nil and a message; picture: a .png / .jpg on the SD card, or an
 *     https URL the service fetches
 *   picture3d("status", job, provider) -> "running", progress (0..100);
 *     "done", the .glb's URL; or nil and a message
 *   picture3d("take", url, {name=, faces=, height=}) -> { record =
 *     (the MESH model record, textured if the .glb has a texture), flat =
 *     (the same with colours sampled from the texture, or nil), texture =
 *     (256 x 256 RGBA bytes, or nil), nv =, nf =, textured = }, or nil
 *     and a message. The calls block while the network works (bm Studio
 *     shows what it is doing before each one). */
int l_picture3d(lua_State *L)
{
    const char *action = luaL_checkstring(L, 1);
    char err[160];
    /* the services go out with the user's keys (meshy_key): bm's own tools
     * only, never a game (one from the Market could spend them, 2026-10-05) */
    if (!tool_mode && strcmp(action, "providers") != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "the picture services are for bm's tools only");
        return 2;
    }
    if (strcmp(action, "providers") == 0) {
        lua_newtable(L);
        for (int i = 0; img3d_provider_name(i); i++) {
            lua_pushstring(L, img3d_provider_name(i));
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }
    if (strcmp(action, "ready") == 0) {
        const img3d_provider_t *p = img3d_provider(luaL_optstring(L, 2, "meshy"));
        const char *key = p ? config_get(img3d_key_name(p)) : NULL;
        if (!p || !key || !key[0]) {
            lua_pushboolean(L, 0);
            if (!p)
                lua_pushstring(L, "no such service");
            else {
                snprintf(err, sizeof err, "%s: put the key in bm/config.txt on the SD card as %s=...",
                         luaL_optstring(L, 2, "meshy"), img3d_key_name(p));
                lua_pushstring(L, err);
            }
            return 2;
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    if (strcmp(action, "start") == 0) {
        const char *picture = luaL_checkstring(L, 2);
        const char *pname = "meshy";
        int polycount = 2000;
        if (lua_istable(L, 3)) {
            lua_getfield(L, 3, "provider");
            if (lua_isstring(L, -1))
                pname = lua_tostring(L, -1);
            lua_getfield(L, 3, "polycount");
            if (lua_isinteger(L, -1))
                polycount = (int)lua_tointeger(L, -1);
            lua_pop(L, 2);
        }
        const img3d_provider_t *p = img3d_provider(pname);
        const char *key = p ? config_get(img3d_key_name(p)) : NULL;
        if (!p || !key) {
            lua_pushnil(L);
            lua_pushstring(L, p ? "no key for the service (bm/config.txt)" : "no such service");
            return 2;
        }
        uint8_t *data = NULL;
        size_t len = 0;
        int is_url = strncmp(picture, "http://", 7) == 0 || strncmp(picture, "https://", 8) == 0;
        if (!is_url) {
            fat_entry_t e;
            memset(&e, 0, sizeof e);
            if (fat_find(picture, &e) != 0 || e.is_dir || fat_load(&e, &data, &len) != 0) {
                lua_pushnil(L);
                lua_pushfstring(L, "%s: %s", picture, e.is_dir ? "a directory" : fat_error());
                return 2;
            }
        }
        char task[64];
        int r = img3d_start(p, key, data, len, is_url ? picture : NULL, polycount, task, sizeof task, err, sizeof err);
        free(data);
        if (r < 0) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        lua_pushstring(L, task);
        return 1;
    }
    if (strcmp(action, "status") == 0) {
        const char *task = luaL_checkstring(L, 2);
        const img3d_provider_t *p = img3d_provider(luaL_optstring(L, 3, "meshy"));
        const char *key = p ? config_get(img3d_key_name(p)) : NULL;
        if (!p || !key) {
            lua_pushnil(L);
            lua_pushstring(L, "no such service, or no key");
            return 2;
        }
        int progress = 0;
        char url[512];
        int r = img3d_status(p, key, task, &progress, url, sizeof url, err, sizeof err);
        if (r < 0) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        lua_pushstring(L, r ? "done" : "running");
        if (r)
            lua_pushstring(L, url);
        else
            lua_pushinteger(L, progress);
        return 2;
    }
    if (strcmp(action, "take") == 0) {
        const char *url = luaL_checkstring(L, 2);
        const char *name = "model";
        glb_opts_t o = { 2.0f, 1200, 256 };
        if (lua_istable(L, 3)) {
            lua_getfield(L, 3, "name");
            if (lua_isstring(L, -1))
                name = lua_tostring(L, -1);
            lua_getfield(L, 3, "faces");
            if (lua_isinteger(L, -1))
                o.max_faces = (int)lua_tointeger(L, -1);
            lua_getfield(L, 3, "height");
            if (lua_isnumber(L, -1))
                o.height = (float)lua_tonumber(L, -1);
            lua_pop(L, 3);
        }
        uint8_t *glb = NULL;
        size_t len = 0;
        if (img3d_download(url, 24u << 20, &glb, &len, err, sizeof err) < 0) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        glb_model_t m;
        int r = glb_to_model(glb, len, name, &o, &m, err, sizeof err);
        free(glb);
        if (r < 0) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        push_glb_model(L, &m);
        glb_model_free(&m);
        return 1;
    }
    return luaL_error(L, "picture3d: providers, ready, start, status or take");
}

/* cart_write(path, {[lua=], [title=, author=, res=, from=, sections=,
 * sheet=, palette=]}) -> true, or false and a message. Changes only the
 * code (and the fields given) of the cartridge: its sheet, map, cover, sound
 * bank and any other section stay as they are. lua absent: the code stays
 * too. sections: {[8] = MESH bytes, [9] = ANIM bytes, [11] = SPRITES bytes}
 * (false takes them away), checked first (bm Mesh, the 3D tools, bm Pixel's
 * Stack page). sheet = true: the project's
 * sprite sheet (cart_load, sset, cart_sheet) takes the place of the file's,
 * as SHEET8 with `palette` ({0xRRGGBB, ...}) first in its palette when it
 * has at most 256 colours (sheet_section; bm Pixel). from: take the
 * sections from another file ("save as"); from = false: a new cartridge,
 * whatever the file holds now (bm Studio's new project). A file that does not exist yet
 * becomes a new cartridge with the code (its name must then be 8.3; an
 * existing file keeps its long name). */
int l_cart_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    char to[96];
    if (write_target(L, path, to, sizeof to))
        return 2;
    lua_getfield(L, 2, "lua");
    size_t lua_len = 0;
    const char *lua = lua_isnil(L, -1) ? NULL : luaL_checklstring(L, -1, &lua_len);
    bm_put_t put[5];
    int nput = 0;
    lua_getfield(L, 2, "sections");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TTABLE);
        static const uint32_t kinds[3] = { BM_SEC_MESH, BM_SEC_ANIM, BM_SEC_SPRITES };
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            lua_Integer t = lua_isinteger(L, -2) ? lua_tointeger(L, -2) : -1;
            if (t != BM_SEC_MESH && t != BM_SEC_ANIM && t != BM_SEC_SPRITES)
                return luaL_error(L, "sections: only 8 (MESH), 9 (ANIM) and 11 (SPRITES)");
            lua_pop(L, 1);
        }
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(L, -1, kinds[k]);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            size_t n = 0;
            const uint8_t *s = lua_toboolean(L, -1) ? (const uint8_t *)luaL_checklstring(L, -1, &n) : NULL;
            if (s && (n > 0x1000000 || (kinds[k] == BM_SEC_MESH ? bm_mesh_check(s, (uint32_t)n)
                                        : kinds[k] == BM_SEC_SPRITES ? bm_sprites_check(s, (uint32_t)n)
                                                                     : bm_anim_check(s, (uint32_t)n)) < 0)) {
                lua_pushboolean(L, 0);
                lua_pushstring(L, kinds[k] == BM_SEC_MESH ? "broken MESH section"
                                  : kinds[k] == BM_SEC_SPRITES ? "broken SPRITES section" : "broken ANIM section");
                return 2;
            }
            put[nput++] = (bm_put_t){ kinds[k], s, (uint32_t)n };
            lua_pop(L, 1);          /* the string stays alive in the table */
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 2, "from");
    int fresh = lua_isboolean(L, -1) && !lua_toboolean(L, -1);    /* from = false: a new cartridge */
    lua_pop(L, 1);
    const char *base = fresh ? NULL : field(L, 2, "from", to);
    char dir[64], name[FAT_NAME_MAX];
    split_path(to, dir, sizeof dir, name, sizeof name);

    fat_entry_t e;
    uint8_t *old = NULL;
    size_t old_len = 0;
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    char err[64];
    if (base && fat_find(base, &e) == 0 && !e.is_dir) {
        if (fat_load(&e, &old, &old_len) != 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        if (bm_parse(old, old_len, &c, err, sizeof err) != 0) {
            free(old);
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "%s: %s", name, err);
            return 2;
        }
    }
    char title[49], author[33];
    memcpy(title, c.title, sizeof title);
    memcpy(author, c.author, sizeof author);
    title[48] = author[32] = 0;
    const char *t = field(L, 2, "title", old ? title : name);
    const char *a = field(L, 2, "author", author);
    const char *res = field(L, 2, "res", rt_res_name(c.width));
    if (!old && !lua) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "a new cartridge needs its code (lua)");
        return 2;
    }
    uint8_t *sheet = NULL;
    lua_getfield(L, 2, "sheet");
    int want_sheet = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (want_sheet) {
        uint32_t st = 0, ss = 0;
        lua_getfield(L, 2, "palette");
        sheet = sheet_section(L, lua_istable(L, -1) ? lua_gettop(L) : 0, old ? &c : NULL, &st, &ss);
        lua_pop(L, 1);
        if (!sheet) {
            free(old);
            return luaL_error(L, "not enough memory to save the sheet");
        }
        put[nput++] = (bm_put_t){ st, sheet, ss };
    }
    uint8_t *flags = NULL;
    if (want_sheet && rt.flags_dirty) {     /* fset() since the sheet came: its flags too */
        const uint32_t per = (uint32_t)rt.sheet.w / G16_CELL;
        uint32_t rows = 0;
        for (uint32_t i = 0; i < (uint32_t)sheet_cells(); i++)
            if (rt.flags[i])
                rows = i / per + 1;
        if (rows && (flags = malloc(4 + per * rows)) != NULL) {
            put16(flags, per);
            put16(flags + 2, rows);
            memcpy(flags + 4, rt.flags, per * rows);
        }
        put[nput++] = (bm_put_t){ BM_SEC_FLAGS, flags, flags ? 4 + per * rows : 0 };
    }
    size_t out_len;
    uint8_t *out = bm_rewrite_with(old, old_len, lua, lua_len, t, a, res_width(res), put, nput,
                                   &out_len);
    free(old);
    free(sheet);
    free(flags);
    if (!out)
        return luaL_error(L, "not enough memory to save");
    if (bm_is_project(to))
        out[18] |= BM_FLAG_PROJECT;
    /* an existing file keeps its entry (and its long name); a new one is 8.3 */
    fat_entry_t te;
    int ok;
    if (fat_find(to, &te) == 0 && !te.is_dir)
        ok = fat_replace(to, out, out_len) == 0;
    else
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, out, out_len) == 0;
    free(out);
    if (ok)
        return wrote(L, path, to);
    lua_pushboolean(L, 0);
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_run(path, [{breaks = {lines}, stop = true}]): leaves the editor,
 * plays the cartridge, then comes back to the editor with cart_arg() =
 * { path =, error = }. The game is a debug session (R13): it stops at the
 * breakpoints (lines of its main.lua), at breakpoint() and, with stop, at
 * its first line. */
int l_cart_run(lua_State *L)
{
    if (!tool_mode) {           /* not a tool: nothing for the console to do, and no request left behind */
        rt.quit = 1;
        return 0;
    }
    ksnprintf(run_request, sizeof run_request, "%s", luaL_checkstring(L, 1));
    memset(&dbg_next, 0, sizeof dbg_next);
    dbg_next.set = 1;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "stop");
        dbg_next.stop = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (lua_getfield(L, 2, "breaks") == LUA_TTABLE)
            for (int i = 1; dbg_next.n < (int)(sizeof dbg_next.lines / sizeof dbg_next.lines[0]); i++) {
                lua_rawgeti(L, -1, i);
                const int ok = lua_isinteger(L, -1);
                const lua_Integer line = lua_tointeger(L, -1);
                lua_pop(L, 1);
                if (!ok)
                    break;
                if (line > 0 && line < 65536)
                    dbg_next.lines[dbg_next.n++] = (uint16_t)line;
            }
        lua_pop(L, 1);
    }
    rt.quit = 1;
    return 0;
}

/* cart_tool(name, [path]): leaves this tool for another of the console's
 * ("studio", "animator", "mesh", "pixel", "code", "sdk", "sound") on the
 * file (bm Studio's "Open in bm Animator") */
int l_cart_tool(lua_State *L)
{
    if (!tool_mode) {           /* as cart_run: a request nobody takes would run in the next tool session */
        rt.quit = 1;
        return 0;
    }
    ksnprintf(tool_request, sizeof tool_request, "%s", luaL_checkstring(L, 1));
    ksnprintf(run_request, sizeof run_request, "%s", luaL_optstring(L, 2, ""));
    rt.quit = 1;
    return 0;
}

int l_cart_arg(lua_State *L)
{
    if (!arg_path[0]) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, arg_path);
    lua_setfield(L, -2, "path");
    lua_pushboolean(L, arg_back);
    lua_setfield(L, -2, "back");
    if (arg_error[0]) {
        lua_pushstring(L, arg_error);
        lua_setfield(L, -2, "error");
    }
    if (arg_from[0]) {
        lua_pushstring(L, arg_from);
        lua_setfield(L, -2, "from");
    }
    if (arg_has_run) {
        /* run = { frames, secs, fps, ms (mean of _update + _draw), ms_max,
         *         slow (frames over 16.7 ms), lua_kb, lua_peak_kb, data_kb,
         *         instr_max, tokens, tris (the last frame's 3D), gpu } */
        const bm_stats_t *r = &arg_run;
        lua_createtable(L, 0, 13);
        lua_pushinteger(L, r->frames);
        lua_setfield(L, -2, "frames");
        lua_pushnumber(L, r->elapsed_us / 1e6);
        lua_setfield(L, -2, "secs");
        lua_pushnumber(L, r->elapsed_us ? r->frames * 1e6 / r->elapsed_us : 0);
        lua_setfield(L, -2, "fps");
        lua_pushnumber(L, r->frames ? r->cpu_us_total / 1000.0 / r->frames : 0);
        lua_setfield(L, -2, "ms");
        lua_pushnumber(L, r->cpu_us_max / 1000.0);
        lua_setfield(L, -2, "ms_max");
        lua_pushinteger(L, r->slow);
        lua_setfield(L, -2, "slow");
        lua_pushinteger(L, r->lua_kb);
        lua_setfield(L, -2, "lua_kb");
        lua_pushinteger(L, r->lua_peak_kb);
        lua_setfield(L, -2, "lua_peak_kb");
        lua_pushinteger(L, r->assets_kb);
        lua_setfield(L, -2, "data_kb");
        lua_pushinteger(L, (lua_Integer)r->instr_k_max * 1000);
        lua_setfield(L, -2, "instr_max");
        lua_pushinteger(L, r->tokens);
        lua_setfield(L, -2, "tokens");
        lua_pushinteger(L, r->tris3d);
        lua_setfield(L, -2, "tris");
        lua_pushboolean(L, r->gpu3d);
        lua_setfield(L, -2, "gpu");
        lua_setfield(L, -2, "run");
    }
    return 1;
}

/* cart_audio([path]) -> the sound bank of a .bm file as a string (false
 * if it has none) and its title; nil and a message if it cannot be read.
 * Without a path: the bank of the cartridge playing, or nil. */
int l_cart_audio(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        if (own_audio)
            lua_pushlstring(L, (const char *)own_audio, own_audio_len);
        else
            lua_pushnil(L);
        return 1;
    }
    const char *path = luaL_checkstring(L, 1);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    bm_cart_t c;
    char err[64];
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, fat_error());
        return 2;
    }
    if (bm_parse(data, len, &c, err, sizeof err) != 0) {
        free(data);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    if (c.audio)
        lua_pushlstring(L, (const char *)c.audio, c.audio_size);
    else
        lua_pushboolean(L, 0);
    char title[49];
    memcpy(title, c.title, 48);
    title[48] = 0;
    lua_pushstring(L, title);
    free(data);
    return 2;
}

/* A new cartridge file: header, the Lua source, the bank. */
static uint8_t *new_pack(const char *title, const char *lua, size_t lua_len, const char *audio, size_t alen,
                         uint32_t *total)
{
    uint32_t count = alen ? 2 : 1;
    uint32_t t = BM_HEADER_SIZE + count * 16 + (((uint32_t)lua_len + 3) & ~3u) + (((uint32_t)alen + 3) & ~3u);
    uint8_t *buf = calloc(t, 1);
    if (!buf)
        return NULL;
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
    put32(tab, BM_SEC_LUA);
    put32(tab + 4, (uint32_t)(p - buf));
    put32(tab + 8, (uint32_t)lua_len);
    memcpy(p, lua, lua_len);
    p += ((uint32_t)lua_len + 3) & ~3u;
    if (alen) {
        put32(tab + 16, BM_SEC_AUDIO);
        put32(tab + 20, (uint32_t)(p - buf));
        put32(tab + 24, (uint32_t)alen);
        memcpy(p, audio, alen);
    }
    memcpy(buf, "BMCART\0\0", 8);
    put16(buf + 8, 1);
    put16(buf + 10, BM_HEADER_SIZE);
    put16(buf + 12, 640);
    put16(buf + 14, 360);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)count;
    strncpy((char *)buf + 24, title, 47);
    strncpy((char *)buf + 72, "bm sound", 31);
    put32(buf + 20, crc32(buf + BM_HEADER_SIZE, t - BM_HEADER_SIZE));
    *total = t;
    return buf;
}

/* entry t of the section table of a parsed .bm: the sound bank? */
static int is_bank(const uint8_t *data, const uint8_t *t)
{
    return get32(t) == BM_SEC_AUDIO && get32(t + 8) >= 4 && memcmp(data + get32(t + 4), "BMAU", 4) == 0;
}

/* cart_put_audio(path, bank, [title, lua]): puts the sound bank (a string;
 * nil removes it) into a .bm file, everything else as it was. If the file
 * does not exist, it is made (8.3 name) with that title and Lua source.
 * true, or false and a message. */
int l_cart_put_audio(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    char to[96];
    if (write_target(L, path, to, sizeof to))
        return 2;
    size_t alen = 0;
    const char *audio = lua_isnoneornil(L, 2) ? NULL : luaL_checklstring(L, 2, &alen);
    char err[64];
    if (audio) {
        au_bank_t *b = malloc(sizeof *b);
        if (!b)
            return luaL_error(L, "not enough memory");
        int bad = au_parse((const uint8_t *)audio, alen, b, err, sizeof err);
        free(b);
        if (bad) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, err);
            return 2;
        }
    }
    fat_entry_t e;
    uint8_t *data, *buf;
    size_t len;
    uint32_t total;
    int ok;
    if (fat_find(to, &e) != 0) {
        size_t lua_len;
        const char *title = luaL_optstring(L, 3, "Sound pack");
        const char *lua = luaL_optlstring(L, 4, NULL, &lua_len);
        if (!lua) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        char dir[64], name[16];
        split_path(to, dir, sizeof dir, name, sizeof name);
        if (!(buf = new_pack(title, lua, lua_len, audio, alen, &total)))
            return luaL_error(L, "not enough memory to save");
        if (bm_is_project(to))
            buf[18] |= BM_FLAG_PROJECT;
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
        free(buf);
    } else {
        bm_cart_t c;
        if (fat_load(&e, &data, &len) != 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        if (bm_parse(data, len, &c, err, sizeof err) != 0) {
            free(data);
            lua_pushboolean(L, 0);
            lua_pushstring(L, err);
            return 2;
        }
        /* the same sections in the same order, the bank last (a type 6
         * section that is not a bank is the MESH of the first bm Studio
         * files: it stays, as MESH; their ANIM of type 7 becomes ANIM) */
        unsigned n = data[17], count = 0;
        total = BM_HEADER_SIZE;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *t = data + BM_HEADER_SIZE + i * 16;
            if (!is_bank(data, t)) {
                count++;
                total += 16 + ((get32(t + 8) + 3) & ~3u);
            }
        }
        if (audio) {
            count++;
            total += 16 + (((uint32_t)alen + 3) & ~3u);
        }
        if (count > 255 || !(buf = calloc(total, 1))) {
            free(data);
            return luaL_error(L, "not enough memory to save");
        }
        memcpy(buf, data, BM_HEADER_SIZE);
        uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + count * 16;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *t = data + BM_HEADER_SIZE + i * 16;
            uint32_t size = get32(t + 8), type = get32(t);
            if (is_bank(data, t))
                continue;
            memcpy(tab, t, 16);
            if (type == BM_SEC_AUDIO)
                put32(tab, BM_SEC_MESH);
            else if (type == BM_SEC_OLD_ANIM)
                put32(tab, BM_SEC_ANIM);
            put32(tab + 4, (uint32_t)(p - buf));
            memcpy(p, data + get32(t + 4), size);
            tab += 16;
            p += (size + 3) & ~3u;
        }
        if (audio) {
            put32(tab, BM_SEC_AUDIO);
            put32(tab + 4, (uint32_t)(p - buf));
            put32(tab + 8, (uint32_t)alen);
            put32(tab + 12, 0);
            memcpy(p, audio, alen);
        }
        buf[17] = (uint8_t)count;
        put32(buf + 20, crc32(buf + BM_HEADER_SIZE, total - BM_HEADER_SIZE));
        free(data);
        ok = fat_replace(to, buf, total) == 0;
        free(buf);
    }
    if (ok)
        return wrote(L, path, to);
    lua_pushboolean(L, 0);
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_data(type) -> the bytes of the project's MESH (8) or ANIM (9)
 * section, or nil; cart_data(type, bytes) replaces it (nil or "" takes it
 * away) -> true, or false and a message. The bytes are checked first;
 * model() and animate() see the new ones at once, cart_save writes them.
 * bm Studio and bm Animator of the console edit models and skeletons this way. */
int l_cart_data(lua_State *L)
{
    int type = (int)luaL_checkinteger(L, 1);
    luaL_argcheck(L, type == BM_SEC_MESH || type == BM_SEC_ANIM ||
                     type == BM_SEC_SPRITES || type == BM_SEC_BOXES, 1,
                  "8 (MESH), 9 (ANIM), 11 (SPRITES) or 14 (BOXES)");
    if (type == BM_SEC_SPRITES) {
        if (lua_gettop(L) < 2) {
            if (rt.zones && rt.nzones)
                lua_pushlstring(L, (const char *)rt.zones, 4u + (uint32_t)rt.nzones * BM_SPRITE_SIZE);
            else
                lua_pushnil(L);
            return 1;
        }
        size_t n = 0;
        const uint8_t *s = lua_isnil(L, 2) ? NULL : (const uint8_t *)luaL_checklstring(L, 2, &n);
        if (s && !n) s = NULL;
        if (!s) {
            free(rt.zones);
            rt.zones = NULL;
            rt.nzones = 0;
            lua_pushboolean(L, 1);
            return 1;
        }
        int nz = bm_sprites_check(s, (uint32_t)n);
        if (nz < 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "broken SPRITES section");
            return 2;
        }
        uint8_t *copy = malloc(n);
        if (!copy)
            return luaL_error(L, "not enough memory for the section");
        memcpy(copy, s, n);
        free(rt.zones);
        rt.zones = copy;
        rt.nzones = (uint16_t)nz;
        lua_pushboolean(L, 1);
        return 1;
    }
    if (type == BM_SEC_BOXES) {
        if (lua_gettop(L) < 2) {
            if (rt.boxes && rt.nboxes)
                lua_pushlstring(L, (const char *)rt.boxes, 4u + (uint32_t)rt.nboxes * BM_BOX_SIZE);
            else
                lua_pushnil(L);
            return 1;
        }
        size_t n = 0;
        const uint8_t *s = lua_isnil(L, 2) ? NULL : (const uint8_t *)luaL_checklstring(L, 2, &n);
        if (s && !n) s = NULL;
        if (!s) {
            free(rt.boxes);
            rt.boxes = NULL;
            rt.nboxes = 0;
            lua_pushboolean(L, 1);
            return 1;
        }
        int nb = bm_boxes_check(s, (uint32_t)n);
        if (nb < 0) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "broken BOXES section");
            return 2;
        }
        uint8_t *copy = malloc(n);
        if (!copy)
            return luaL_error(L, "not enough memory for the section");
        memcpy(copy, s, n);
        free(rt.boxes);
        rt.boxes = copy;
        rt.nboxes = (uint16_t)nb;
        lua_pushboolean(L, 1);
        return 1;
    }
    uint8_t **cur = type == BM_SEC_MESH ? &rt.mesh : &rt.anim;
    uint32_t *cur_size = type == BM_SEC_MESH ? &rt.mesh_size : &rt.anim_size;
    if (lua_gettop(L) < 2) {
        if (*cur)
            lua_pushlstring(L, (const char *)*cur, *cur_size);
        else
            lua_pushnil(L);
        return 1;
    }
    size_t n = 0;
    const uint8_t *s = lua_isnil(L, 2) ? NULL : (const uint8_t *)luaL_checklstring(L, 2, &n);
    if (s && !n)
        s = NULL;
    if (s && (n > 0x1000000 || (type == BM_SEC_MESH ? bm_mesh_check(s, (uint32_t)n)
                                                      : bm_anim_check(s, (uint32_t)n)) < 0)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, type == BM_SEC_MESH ? "broken MESH section" : "broken ANIM section");
        return 2;
    }
    int k = 0;
    while (k < proj_extras && proj_extra[k].type != (uint32_t)type)
        k++;
    if (s && k == proj_extras && proj_extras == PROJ_EXTRA_MAX) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "too many sections");
        return 2;
    }
    uint8_t *copy = NULL, *keep = NULL;
    if (s && (!(copy = malloc(n)) || !(keep = malloc(n)))) {
        free(copy);
        return luaL_error(L, "not enough memory for the section");
    }
    free(*cur);
    *cur = copy;
    *cur_size = (uint32_t)n;
    if (s) {
        memcpy(copy, s, n);
        memcpy(keep, s, n);
    }
    if (k < proj_extras) {
        free(proj_extra[k].data);
        if (s) {
            proj_extra[k].data = keep;
            proj_extra[k].size = (uint32_t)n;
        } else {
            for (int i = k; i + 1 < proj_extras; i++)
                proj_extra[i] = proj_extra[i + 1];
            proj_extras--;
        }
    } else if (s) {
        proj_extra[proj_extras].type = (uint32_t)type;
        proj_extra[proj_extras].size = (uint32_t)n;
        proj_extra[proj_extras++].data = keep;
    }
    lua_pushboolean(L, 1);
    return 1;
}
