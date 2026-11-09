/*
 * api_psx.c: the Lua side of the PlayStation core (psx.h): what the cartridge carts/psx calls. The files are read here
 * in C because a disc image is far bigger than the 4 MB that doc_read gives: /psx holds the BIOS (a 512 KB file), PS-EXE
 * files and disc images (.cue + .bin, .bin, .img, .iso), and a disc is read one sector at a time from the card.
 *
 *   psx_list()                  -> { {name =, size =, kind =}, ... }   kind: "bios", "exe", "disc", "cue", "other"
 *   psx_boot(name)              -> true, or nil and why        loads the BIOS (the first 512 KB file of /psx) and the game
 *   psx_reset()                                                 a reset of the machine (the same game)
 *   psx_frame(pad1, pad2, [render])                             one frame; a pad is bits (PSX_PAD_* in psx.h)
 *   psx_draw(target, [w, h])    -> pw, ph                       copies the picture, scaled, to the top left of a render target
 *   psx_info()                  -> {bios, game, kind, disc_sectors}
 *   psx_debug()                 -> a table of the machine's state
 *   psx_tty()                   -> the text a program printed
 */
#include "rt_internal.h"
#include "audio/audio.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "psx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lauxlib.h"
#include "lua.h"

#define PSX_DIR "/psx"
#define BIOS_SIZE (512u * 1024u)

static char bios_name[96], game_name[96], game_kind[8];
static fat_entry_t disc_file;
static uint32_t disc_secsize;
static int booted;
static psx_disc_t disc;

static uint32_t now_us(void) { return timer_ticks(); }

static const char *kind_of(const char *name, uint32_t size)
{
    const char *dot = strrchr(name, '.');
    if (size == BIOS_SIZE) return "bios";
    if (!dot) return "other";
    if (!strcasecmp(dot, ".exe") || !strcasecmp(dot, ".psx") || !strcasecmp(dot, ".ps-exe")) return "exe";
    if (!strcasecmp(dot, ".cue")) return "cue";
    if (!strcasecmp(dot, ".bin") || !strcasecmp(dot, ".img") || !strcasecmp(dot, ".iso")) return "disc";
    return "other";
}

static int denied(lua_State *L)
{
    if (perm_allowed(PERM_DOCS))
        return 0;
    lua_pushnil(L);
    lua_pushstring(L, "the player did not allow this app the documents");
    return 2;
}

int l_psx_list(lua_State *L)
{
    if (denied(L))
        return 2;
    lua_newtable(L);
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, PSX_DIR) != 0)
        return 1;
    int n = 0;
    while (fat_readdir(&d, &e) > 0) {
        if (e.is_dir)
            continue;
        lua_createtable(L, 0, 3);
        lua_pushstring(L, e.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)e.size);
        lua_setfield(L, -2, "size");
        lua_pushstring(L, kind_of(e.name, e.size));
        lua_setfield(L, -2, "kind");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int disc_read(void *user, uint32_t lba, uint8_t *out)
{
    (void)user;
    return fat_read_at(&disc_file, lba * disc_secsize, out, disc_secsize);
}

/* the first 512 KB file of /psx is the BIOS (a name like scph1001.bin first); 0 and the reason if there is none */
static int find_bios(const char **why)
{
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, PSX_DIR) != 0) { *why = "no /psx folder on the card"; return 0; }
    char best[FAT_NAME_MAX] = "";
    int best_score = -1;
    while (fat_readdir(&d, &e) > 0) {
        if (e.is_dir || e.size != BIOS_SIZE) continue;
        const int score = !strncasecmp(e.name, "scph", 4) ? 2 : 1;
        if (score > best_score) { best_score = score; snprintf(best, sizeof best, "%s", e.name); }
    }
    if (best_score < 0) { *why = "no BIOS in /psx (a 512 KB file, like scph1001.bin)"; return 0; }
    char path[128];
    snprintf(path, sizeof path, "%s/%s", PSX_DIR, best);
    uint8_t *data;
    size_t len;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) { *why = fat_error(); return 0; }
    const int r = psx_load_bios(data, len, why);
    free(data);
    if (r != 0) return 0;
    snprintf(bios_name, sizeof bios_name, "%s", best);
    return 1;
}

/* the file name of the first FILE line of a cue sheet, and whether its track is 2048 bytes a sector */
static int parse_cue(const char *text, char *file, size_t fn, int *cooked)
{
    const char *p = strstr(text, "FILE");
    if (!p) return -1;
    p += 4;
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        const char *q = strchr(p, '"');
        if (!q || (size_t)(q - p) >= fn) return -1;
        memcpy(file, p, (size_t)(q - p));
        file[q - p] = 0;
    } else {
        size_t k = 0;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n' && k + 1 < fn) file[k++] = *p++;
        file[k] = 0;
    }
    *cooked = strstr(text, "MODE1/2048") != NULL;
    return 0;
}

static int fail(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

int l_psx_boot(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *why = "";
    if (denied(L))
        return 2;
    if (!name[0] || name[0] == '.' || strpbrk(name, "/\\"))
        return fail(L, "not a file name");
    psx_set_clock(now_us);
    audio_set_ext(psx_audio_mix);
    char path[160];
    fat_entry_t e;
    snprintf(path, sizeof path, "%s/%s", PSX_DIR, name);
    if (fat_find(path, &e) != 0 || e.is_dir)
        return fail(L, "no such file in /psx");
    const char *kind = kind_of(name, e.size);
    if (!strcmp(kind, "bios") || !strcmp(kind, "other"))
        return fail(L, "not a game (a PS-EXE, or a disc image .cue .bin .img .iso)");
    bios_name[0] = 0;
    if (!find_bios(&why) && strcmp(kind, "exe") != 0)
        return fail(L, why);                                /* a PS-EXE can run without a BIOS; a disc cannot */
    psx_set_disc(NULL);
    if (!strcmp(kind, "exe")) {
        uint8_t *data;
        size_t len;
        if (e.size > 2u * 1024 * 1024 + 2048)
            return fail(L, "a PS-EXE of more than 2 MB");
        if (fat_load(&e, &data, &len) != 0)
            return fail(L, fat_error());
        const int r = psx_load_exe(data, len, &why);
        free(data);
        if (r != 0)
            return fail(L, why);
    } else {
        fat_entry_t img = e;
        const char *dot = strrchr(name, '.');
        int cooked = dot && !strcasecmp(dot, ".iso");
        if (!strcmp(kind, "cue")) {
            uint8_t *data;
            size_t len;
            if (e.size > 65536 || fat_load(&e, &data, &len) != 0)
                return fail(L, "cannot read the cue sheet");
            char *txt = (char *)malloc(len + 1);
            if (!txt) { free(data); return fail(L, "out of memory"); }
            memcpy(txt, data, len);
            txt[len] = 0;
            free(data);
            char file[96];
            const int r = parse_cue(txt, file, sizeof file, &cooked);
            free(txt);
            snprintf(path, sizeof path, "%s/%s", PSX_DIR, file);
            if (r != 0 || fat_find(path, &img) != 0) {
                /* a renamed image: try the cue's own name with .bin (cb2.cue -> cb2.bin) */
                char alt[160];
                snprintf(alt, sizeof alt, "%s/%s", PSX_DIR, name);
                char *dot2 = strrchr(alt, '.');
                if (dot2) strcpy(dot2, ".bin");
                if (!dot2 || fat_find(alt, &img) != 0)
                    return fail(L, "the cue sheet names no file that can be read");
            }
        }
        disc_file = img;
        disc_secsize = cooked ? 2048u : 2352u;
        disc.read = disc_read;
        disc.user = NULL;
        disc.sectors = img.size / disc_secsize;
        disc.raw = !cooked;
        psx_reset();
        psx_set_disc(&disc);
    }
    snprintf(game_name, sizeof game_name, "%s", name);
    snprintf(game_kind, sizeof game_kind, "%s", kind);
    booted = 1;
    lua_pushboolean(L, 1);
    return 1;
}

int l_psx_reset(lua_State *L)
{
    (void)L;
    if (booted)
        psx_reset();
    return 0;
}

int l_psx_frame(lua_State *L)
{
    psx_frame((uint16_t)luaL_optinteger(L, 1, 0), (uint16_t)luaL_optinteger(L, 2, 0), lua_isnone(L, 3) || lua_toboolean(L, 3));
    return 0;
}

int l_psx_draw(lua_State *L)
{
    int tw, th, pw, ph;
    uint16_t *dst = target_pixels(L, 1, &tw, &th);
    if (!dst)
        return luaL_error(L, "psx_draw: a render target is needed");
    const uint16_t *src = psx_picture(&pw, &ph);
    const int dw = (int)luaL_optinteger(L, 2, pw), dh = (int)luaL_optinteger(L, 3, ph);
    if (dw < 1 || dh < 1 || dw > tw || dh > th)
        return luaL_error(L, "psx_draw: the size %dx%d does not fit the target (%dx%d)", dw, dh, tw, th);
    static uint16_t xmap[1280];
    if (dw > 1280)
        return luaL_error(L, "psx_draw: at most 1280 pixels wide");
    for (int x = 0; x < dw; x++)
        xmap[x] = (uint16_t)(x * pw / dw);
    int prev = -1;
    for (int y = 0; y < dh; y++) {
        const int sy = y * ph / dh;
        uint16_t *row = dst + (size_t)y * (size_t)tw;
        if (sy == prev) {
            memcpy(row, row - tw, (size_t)dw * sizeof(uint16_t));
            continue;
        }
        const uint16_t *s = src + (size_t)sy * (size_t)pw;
        for (int x = 0; x < dw; x++)
            row[x] = s[xmap[x]];
        prev = sy;
    }
    lua_pushinteger(L, pw);
    lua_pushinteger(L, ph);
    return 2;
}

int l_psx_info(lua_State *L)
{
    lua_createtable(L, 0, 4);
    lua_pushstring(L, bios_name[0] ? bios_name : "none");
    lua_setfield(L, -2, "bios");
    lua_pushstring(L, game_name);
    lua_setfield(L, -2, "game");
    lua_pushstring(L, game_kind);
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, (lua_Integer)disc.sectors);
    lua_setfield(L, -2, "disc_sectors");
    return 1;
}

int l_psx_debug(lua_State *L)
{
    psx_debug_t d;
    psx_debug(&d);
    lua_createtable(L, 0, 16);
#define FIELD2(name, val) lua_pushinteger(L, (lua_Integer)(val)); lua_setfield(L, -2, name)
#define FIELD(name, val) lua_pushinteger(L, (lua_Integer)(val)); lua_setfield(L, -2, name)
    FIELD("pc", d.pc); FIELD("ra", d.ra); FIELD("sp", d.sp); FIELD("sr", d.sr); FIELD("cause", d.cause);
    FIELD("istat", d.istat); FIELD("imask", d.imask); FIELD("frames", d.frames);
    FIELD("gpu_cmds", d.gpu_cmds); FIELD("dma_runs", d.dma_runs);
    FIELD("us_frame", d.us_frame); FIELD("us_cpu", d.us_cpu); FIELD("us_gpu", d.us_gpu); FIELD("unhandled", d.unhandled);
    FIELD("last_unhandled", d.last_unhandled);
    FIELD("cd_cmds", d.cd_cmds); FIELD("cd_sectors", d.cd_sectors); FIELD("cd_last_cmd", d.cd_last_cmd);
    FIELD("cd_irq_flag", d.cd_irq_flag); FIELD("cd_irq_en", d.cd_irq_en); FIELD("cd_events", d.cd_events); FIELD("cd_read_fail", d.cd_read_fail);
#undef FIELD
    {
        char tb[160];
        int n = 0;
        for (int i = 5; i < 12; i++) n += snprintf(tb + n, sizeof tb - (size_t)n, "%02X.%02X%02X%02X ", (unsigned)(d.cd_trace[i] & 255), (unsigned)((d.cd_trace[i] >> 8) & 255), (unsigned)((d.cd_trace[i] >> 16) & 255), (unsigned)(d.cd_trace[i] >> 24));
        lua_pushstring(L, tb); lua_setfield(L, -2, "cd_trace");
    }
    FIELD2("cd_lba", d.cd_lba); FIELD2("cd_loc", d.cd_loc);
    uint32_t under, over, kons;
    psx_audio_stats(&under, &over, &kons);
    char buf[96];
    snprintf(buf, sizeof buf, "audio: %s  voices started %lu  under %lu over %lu", audio_ready() ? audio_status() : "no output",
             (unsigned long)kons, (unsigned long)under, (unsigned long)over);
    lua_pushstring(L, buf);
    lua_setfield(L, -2, "audio");
    return 1;
}

int l_psx_jit(lua_State *L)
{
    if (!lua_isnone(L, 1))
        psx_jit_on = lua_toboolean(L, 1) && psx_jit_available();
    lua_pushboolean(L, psx_jit_on);
    lua_pushboolean(L, psx_jit_available());
    return 2;
}

/* psx_selftest([trials]) -> number of trials that differ, a message: random programs run by the interpreter and by compiled
 * code (it uses the machine: call it with no game running) */
int l_psx_selftest(lua_State *L)
{
    char msg[160];
    const int bad = psx_jit_selftest((int)luaL_optinteger(L, 1, 200), msg, sizeof msg);
    lua_pushinteger(L, bad);
    lua_pushstring(L, msg);
    return 2;
}

int l_psx_tty(lua_State *L)
{
    lua_pushstring(L, psx_tty());
    return 1;
}
