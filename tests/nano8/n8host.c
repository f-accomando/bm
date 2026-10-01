/*
 * nano8 on the PC: the nano8 cartridge (its main.lua) with the same
 * machine (src/bm/n8*.c), sound (src/audio/n8snd.c) and Lua as the
 * console, under a small stand-in for bm's runtime: drawing with gfx16
 * into a 640x360 frame, the SD card as a folder, input from a script.
 *
 *   n8host main.lua --root DIR [--frames N] [--exec LUA]...
 *          [--at F:pad=BITS] [--at F:keys=HEX,HEX] [--at F:exec=LUA]
 *          [--shot F:out.ppm]... [--wav out.wav] [--quiet]
 *
 * F is a frame number (60 per second); the pad and keys stay as set until
 * the next --at. --exec runs Lua after _init (e.g. NANO8.Ui.play(1)).
 * The exit status is 1 if the cart ended on an error screen.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "bm/gfx16.h"
#include "bm/n8lua.h"
#include "audio/n8snd.h"
#include "gfx/font.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define FW 640
#define FH 360
#define RATE 48000

static uint16_t frame_px[FW * FH];
static g16_t g;
static const char *root = ".";
static int frame;
static uint32_t pad_bits;
static uint8_t keys_held[16];
static int nkeys;
static int volume_level = 8;
static int quiet;
static long long instr;                 /* Lua instructions, in thousands (--perf) */

static void count_hook(lua_State *L, lua_Debug *ar)
{
    (void)L;
    (void)ar;
    instr++;
}

/* ------------------------------------------------------------ files */

static void host_path(const char *p, char *out, size_t n)
{
    snprintf(out, n, "%s%s%s", root, p[0] == '/' ? "" : "/", p);
}

static int load_file(const char *path, uint8_t **data, size_t *len)
{
    char hp[1024];
    host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc((size_t)n + 1);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(*data);
        return -1;
    }
    fclose(f);
    *len = (size_t)n;
    return 0;
}

static g16_t *target(void)
{
    return &g;
}

/* ------------------------------------------------------------ bm API */

static int ival(lua_State *L, int i) { return (int)luaL_checknumber(L, i); }
static int oval(lua_State *L, int i, int d) { return lua_isnoneornil(L, i) ? d : (int)luaL_checknumber(L, i); }
static uint16_t col(lua_State *L, int i, uint32_t d) { return g16_rgb24((uint32_t)luaL_optinteger(L, i, d)); }

static int l_cls(lua_State *L) { g16_cls(&g, col(L, 1, 0)); return 0; }
static int l_rectfill(lua_State *L) { g16_rectfill(&g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_rect(lua_State *L) { g16_rect(&g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_line(lua_State *L) { g16_line(&g, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), col(L, 5, 0xFFFFFF)); return 0; }
static int l_pset(lua_State *L) { g16_pset(&g, ival(L, 1), ival(L, 2), col(L, 3, 0xFFFFFF)); return 0; }

static int l_print(lua_State *L)
{
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    int s = oval(L, 5, 1);
    const char *str = luaL_tolstring(L, 1, NULL);
    lua_pushinteger(L, g16_text_scaled(&g, x, y, str, c, s));
    return 1;
}

static int l_time(lua_State *L) { lua_pushnumber(L, frame / 60.0); return 1; }

static int l_log(lua_State *L)
{
    if (!quiet)
        fprintf(stderr, "log: %s\n", luaL_tolstring(L, 1, NULL));
    return 0;
}

static int l_ls(lua_State *L)
{
    char hp[1024];
    host_path(luaL_optstring(L, 1, "/"), hp, sizeof hp);
    lua_newtable(L);
    DIR *d = opendir(hp);
    if (!d)
        return 1;
    int i = 1;
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.')
            continue;
        char full[2048];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", hp, e->d_name);
        if (stat(full, &st) != 0)
            continue;
        lua_newtable(L);
        lua_pushstring(L, e->d_name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)st.st_size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, S_ISDIR(st.st_mode));
        lua_setfield(L, -2, "dir");
        lua_rawseti(L, -2, i++);
    }
    closedir(d);
    return 1;
}

static int l_save(lua_State *L)
{
    /* the console writes the table as Lua; here only that it was asked */
    (void)L;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_saved(lua_State *L) { lua_pushnil(L); return 1; }

static int l_volume(lua_State *L)
{
    if (!lua_isnoneornil(L, 1))
        volume_level = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, volume_level);
    return 1;
}

static int l_rawkeys(lua_State *L) { (void)L; return 0; }

static int l_keydown(lua_State *L)
{
    lua_Integer u = luaL_checkinteger(L, 1);
    for (int i = 0; i < nkeys; i++)
        if (keys_held[i] == u) {
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

static int l_keys(lua_State *L)
{
    lua_createtable(L, nkeys, 0);
    for (int i = 0; i < nkeys; i++) {
        lua_pushinteger(L, keys_held[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_pad(lua_State *L)
{
    lua_Integer p = luaL_optinteger(L, 1, 0);
    lua_pushinteger(L, p == 0 || p == 1 ? (lua_Integer)pad_bits : 0);
    return 1;
}

static int l_quit(lua_State *L) { (void)L; return 0; }

/* the console stops a long frame and goes on next time (timeslice);
 * here a frame runs to its end */
static int l_timeslice(lua_State *L) { (void)L; return 0; }

static int l_stick(lua_State *L)
{
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

static const luaL_Reg api[] = {
    { "cls", l_cls }, { "rectfill", l_rectfill }, { "rect", l_rect }, { "line", l_line }, { "pset", l_pset },
    { "print", l_print }, { "time", l_time }, { "log", l_log }, { "ls", l_ls }, { "save", l_save },
    { "saved", l_saved }, { "volume", l_volume }, { "rawkeys", l_rawkeys }, { "keydown", l_keydown },
    { "keys", l_keys }, { "pad", l_pad }, { "quit", l_quit }, { "stick", l_stick }, { "timeslice", l_timeslice },
    { NULL, NULL },
};

/* ------------------------------------------------------------ output */

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6 %d %d 255\n", FW, FH);
    for (int i = 0; i < FW * FH; i++) {
        uint32_t c = g16_to_rgb24(frame_px[i]);
        fputc((int)(c >> 16 & 255), f);
        fputc((int)(c >> 8 & 255), f);
        fputc((int)(c & 255), f);
    }
    fclose(f);
}

static void put32le(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((int)(v >> (8 * i) & 255), f); }
static void put16le(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

/* ------------------------------------------------------------ main */

typedef struct {
    int frame;
    char what[16];
    char arg[1024];
} event_t;

static int traceback(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

static int call(lua_State *L, const char *name)
{
    lua_pushcfunction(L, traceback);
    if (lua_getglobal(L, name) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return 0;
    }
    if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", name, lua_tostring(L, -1));
        return -1;
    }
    lua_pop(L, 1);
    return 0;
}

static int run_lua(lua_State *L, const char *code)
{
    lua_pushcfunction(L, traceback);
    if (luaL_loadstring(L, code) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
        fprintf(stderr, "exec: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    lua_pop(L, 1);
    return 0;
}

int main(int argc, char **argv)
{
    static event_t ev[256];
    int nev = 0, frames = 120, perf = 0;
    const char *script = NULL, *wav = NULL;
    const char *execs[32];
    int nexec = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(a, "--exec") && i + 1 < argc && nexec < 32) execs[nexec++] = argv[++i];
        else if (!strcmp(a, "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else if (!strcmp(a, "--perf")) perf = 1;
        else if ((!strcmp(a, "--at") || !strcmp(a, "--shot")) && i + 1 < argc && nev < 256) {
            const char *s = argv[++i], *colon = strchr(s, ':');
            if (!colon) continue;
            ev[nev].frame = atoi(s);
            if (!strcmp(a, "--shot")) {
                strcpy(ev[nev].what, "shot");
                snprintf(ev[nev].arg, sizeof ev[nev].arg, "%s", colon + 1);
            } else {
                const char *eq = strchr(colon, '=');
                if (!eq) continue;
                snprintf(ev[nev].what, sizeof ev[nev].what, "%.*s", (int)(eq - colon - 1), colon + 1);
                snprintf(ev[nev].arg, sizeof ev[nev].arg, "%s", eq + 1);
            }
            nev++;
        } else if (!script) script = a;
    }
    if (!script) {
        fprintf(stderr, "usage: n8host main.lua --root DIR [options]\n");
        return 2;
    }
    g16_target(&g, frame_px, FW, FW, FH, &font_console_8x16);
    n8lua_io_t io = { load_file, target };
    n8lua_set_io(&io);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushglobaltable(L);
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);
    luaL_requiref(L, "n8", luaopen_n8, 1);
    lua_pop(L, 1);
    lua_pushinteger(L, FW);
    lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, FH);
    lua_setglobal(L, "SCREEN_H");
    lua_pushcfunction(L, traceback);
    if (luaL_loadfile(L, script) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        return 1;
    }
    lua_pop(L, 1);
    if (perf)                           /* before the cart's coroutine: it inherits the hook */
        lua_sethook(L, count_hook, LUA_MASKCOUNT, 1000);
    if (call(L, "_init") != 0)
        return 1;
    for (int i = 0; i < nexec; i++)
        if (run_lua(L, execs[i]) != 0)
            return 1;

    FILE *wf = NULL;
    if (wav && (wf = fopen(wav, "wb")) != NULL) {
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wf);
        put32le(wf, 16); put16le(wf, 1); put16le(wf, 1); put32le(wf, RATE); put32le(wf, RATE * 2);
        put16le(wf, 2); put16le(wf, 16);
        fwrite("data\0\0\0\0", 1, 8, wf);
    }
    uint32_t samples = 0;
    int status = 0;
    long long peak = 0, start_at = 0;
    double busy = 0, busy_peak = 0;
    for (frame = 0; frame < frames; frame++) {
        long long before = instr;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int e = 0; e < nev; e++) {
            if (ev[e].frame != frame || !strcmp(ev[e].what, "shot"))
                continue;
            if (!strcmp(ev[e].what, "pad")) {
                pad_bits = (uint32_t)strtoul(ev[e].arg, NULL, 0);
            } else if (!strcmp(ev[e].what, "keys")) {
                nkeys = 0;
                for (char *p = ev[e].arg; *p && nkeys < 16;) {
                    keys_held[nkeys++] = (uint8_t)strtoul(p, &p, 16);
                    if (*p == ',') p++;
                }
            } else if (!strcmp(ev[e].what, "exec")) {
                run_lua(L, ev[e].arg);
            }
        }
        if (call(L, "_update") != 0 || call(L, "_draw") != 0) {
            status = 1;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
        if (frame > 60) {
            busy += us;
            if (us > busy_peak) busy_peak = us;
        }
        if (frame == 60)
            start_at = instr;           /* past the start (translation, _init) */
        if (frame > 60 && instr - before > peak)
            peak = instr - before;
        int16_t pcm[RATE / 60];
        memset(pcm, 0, sizeof pcm);
        n8snd_mix(pcm, RATE / 60, volume_level / 10.0f);
        if (wf) {
            fwrite(pcm, 2, RATE / 60, wf);
            samples += RATE / 60;
        }
        for (int e = 0; e < nev; e++)
            if (ev[e].frame == frame && !strcmp(ev[e].what, "shot"))
                write_ppm(ev[e].arg);
    }
    if (wf) {
        fseek(wf, 4, SEEK_SET);
        put32le(wf, 36 + samples * 2);
        fseek(wf, 40, SEEK_SET);
        put32le(wf, samples * 2);
        fclose(wf);
    }
    if (perf && frames > 61)
        printf("perf: %lld k Lua instructions per frame (60 Hz) on average, %lld k at most; start %lld k; "
               "%.0f us per frame on this PC, %.0f at most\n",
               (instr - start_at) / (frames - 61), peak, start_at, busy / (frames - 61), busy_peak);
    /* report where it ended */
    run_lua(L, "local m = NANO8.Ui.mode local v = NANO8.Vm "
               "io.write('mode=', m, ' state=', tostring(v.state), ' frames=', tostring(v.frames), '\\n') "
               "if v.err then io.write('error: ', v.err.title, ': ', v.err.msg, '\\n') end "
               "if m == 'error' then os.exit(1) end");
    lua_close(L);
    n8lua_close();
    return status;
}
