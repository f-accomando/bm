/*
 * api_debug.c: the debugger (R13): a game tried from a tool stops at a breakpoint,
 * breakpoint() or its first line; the variables and the stack are shown over its screen.
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

/* ---------------------------------------------------------------- the debugger (R13) */

/* A game tried from a tool (bm Code's F5: cart_run(path, {breaks =, stop =}))
 * is a debug session: at a breakpoint of main.lua (the line hook, on only
 * while there are breakpoints or a step to make), at breakpoint() in its
 * code or, with stop, at its first line, the game stops inside the hook
 * and the debugger covers its screen: the code round the line, the
 * variables (locals, then upvalues) of a function of the stack, the stack.
 * Then: continue (F5, Start), the next line (F10, A: over the calls),
 * into the call (F8, X), out of the function (Shift+F8, Y), stop (Esc,
 * Select: the game ends with "main.lua:N: stopped in the debugger", so the
 * tool comes back on the line). The arrows scroll the variables and
 * choose the function of the stack. The screen of the game comes back as
 * it was. */
static size_t cart_src_len;

static void dbg_hook(lua_State *L)
{
    const int line = dbg.n > 0 || dbg.mode != DBG_RUN;
    lua_sethook(L, hook, LUA_MASKCOUNT | (line ? LUA_MASKLINE : 0), HOOK_EVERY);
}

/* the start of a run: the options cart_run left for it (not for a tool) */
void dbg_begin(lua_State *L, const char *src, size_t len)
{
    memset(&dbg, 0, sizeof dbg);
    cart_src = src;
    cart_src_len = len;
    if (dbg_next.set && !tool_mode) {
        dbg.session = 1;
        for (int i = 0; i < dbg_next.n; i++) {
            const int l = dbg_next.lines[i];
            if (l > 0 && l < DBG_LINES && !(dbg.bits[l >> 3] & (1u << (l & 7)))) {
                dbg.bits[l >> 3] |= (uint8_t)(1u << (l & 7));
                dbg.n++;
            }
        }
        dbg.mode = dbg_next.stop ? DBG_INTO : DBG_RUN;
        kprintf("debug: %d breakpoint%s%s\n", dbg.n, dbg.n == 1 ? "" : "s",
                dbg_next.stop ? ", stops at the first line" : "");
    }
    memset(&dbg_next, 0, sizeof dbg_next);
    dbg_hook(L);
}

static int dbg_depth(lua_State *L)
{
    lua_Debug ar;
    int d = 0;
    while (lua_getstack(L, d, &ar))
        d++;
    return d;
}

/* line n of main.lua (1-based) into out, tabs as spaces */
static void src_line(int n, char *out, size_t size)
{
    const char *p = cart_src, *end = cart_src + cart_src_len;
    for (int i = 1; p && p < end && i < n; i++) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        p = nl ? nl + 1 : end;
    }
    size_t k = 0;
    for (; p && p < end && *p != '\n' && k + 1 < size; p++)
        if (*p == '\t') {
            for (int s = 0; s < 2 && k + 1 < size; s++)
                out[k++] = ' ';
        } else if (*p != '\r') {
            out[k++] = *p;
        }
    out[k] = 0;
}

/* the value at the top of the stack, in a few words (no metamethods) */
static void dbg_value(lua_State *L, char *out, size_t n)
{
    switch (lua_type(L, -1)) {
    case LUA_TNIL: ksnprintf(out, n, "nil"); break;
    case LUA_TBOOLEAN: ksnprintf(out, n, "%s", lua_toboolean(L, -1) ? "true" : "false"); break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1))
            snprintf(out, n, "%lld", (long long)lua_tointeger(L, -1));
        else
            snprintf(out, n, "%.6g", lua_tonumber(L, -1));
        break;
    case LUA_TSTRING: {
        size_t len;
        const char *s = lua_tolstring(L, -1, &len);
        size_t k = 0;
        out[k++] = '"';
        for (size_t i = 0; i < len && k + 5 < n; i++)
            out[k++] = (s[i] >= 32 && s[i] < 127) ? s[i] : '.';
        if (len + 5 > n && k + 4 < n) { out[k++] = '.'; out[k++] = '.'; }
        out[k++] = '"';
        out[k] = 0;
        break;
    }
    case LUA_TTABLE: {
        /* {x=1, y=2, 3 more}: the first fields, raw */
        int shown = 0, more = 0;
        size_t k = ksnprintf(out, n, "{");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (shown < 3 && k + 16 < n) {
                char key[24], val[24];
                if (lua_type(L, -2) == LUA_TSTRING)
                    ksnprintf(key, sizeof key, "%s=", lua_tostring(L, -2));
                else if (lua_isinteger(L, -2))
                    snprintf(key, sizeof key, "[%lld]=", (long long)lua_tointeger(L, -2));
                else
                    ksnprintf(key, sizeof key, "[%s]=", luaL_typename(L, -2));
                if (lua_type(L, -1) == LUA_TTABLE)
                    ksnprintf(val, sizeof val, "{...}");
                else
                    dbg_value(L, val, sizeof val);
                k += ksnprintf(out + k, n - k, "%s%s%s", shown ? ", " : "", key, val);
                shown++;
            } else {
                more++;
            }
            lua_pop(L, 1);
        }
        if (more)
            k += ksnprintf(out + k, n - k, "%s%d more", shown ? ", " : "", more);
        if (k < n - 1)
            ksnprintf(out + k, n - k, "}");
        break;
    }
    case LUA_TFUNCTION: {
        lua_Debug ar;
        lua_pushvalue(L, -1);
        if (lua_getinfo(L, ">S", &ar) && ar.linedefined > 0)
            ksnprintf(out, n, "function %s:%d", ar.short_src, ar.linedefined);
        else
            ksnprintf(out, n, "function");
        break;
    }
    default:
        ksnprintf(out, n, "%s", luaL_typename(L, -1));
    }
}

#define DBG_VARS 48
typedef struct {
    char name[20], value[64];
    int up;                             /* an upvalue */
} dbg_var_t;

/* the variables of the function at level: its locals, then its upvalues */
static int dbg_vars(lua_State *L, int level, dbg_var_t *v, int max)
{
    lua_Debug ar;
    int n = 0;
    if (!lua_getstack(L, level, &ar))
        return 0;
    for (int i = 1; n < max; i++) {
        const char *name = lua_getlocal(L, &ar, i);
        if (!name)
            break;
        if (name[0] != '(') {
            ksnprintf(v[n].name, sizeof v[n].name, "%s", name);
            dbg_value(L, v[n].value, sizeof v[n].value);
            v[n++].up = 0;
        }
        lua_pop(L, 1);
    }
    if (lua_getinfo(L, "f", &ar)) {
        for (int i = 1; n < max; i++) {
            const char *name = lua_getupvalue(L, -1, i);
            if (!name)
                break;
            if (name[0] && strcmp(name, "_ENV")) {
                ksnprintf(v[n].name, sizeof v[n].name, "%s", name);
                dbg_value(L, v[n].value, sizeof v[n].value);
                v[n++].up = 1;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return n;
}

typedef struct {
    char name[24], where[24];
    int line, game;                     /* its line; a function of main.lua */
} dbg_frame_t;

static int dbg_frames(lua_State *L, int from, dbg_frame_t *f, int max)
{
    lua_Debug ar;
    int n = 0;
    for (int lv = from; n < max && lua_getstack(L, lv, &ar); lv++) {
        if (!lua_getinfo(L, "Sln", &ar))
            continue;
        dbg_frame_t *x = &f[n++];
        x->line = ar.currentline;
        x->game = !strcmp(ar.source, "=main.lua");
        if (ar.what && !strcmp(ar.what, "C"))
            ksnprintf(x->where, sizeof x->where, "[C]");
        else
            ksnprintf(x->where, sizeof x->where, "%s:%d", ar.short_src, ar.currentline);
        const char *nm = ar.name ? ar.name : ar.what && !strcmp(ar.what, "main") ? "(main chunk)"
                       : !lua_getstack(L, lv + 1, &ar) && dbg.cb ? dbg.cb : "?";
        ksnprintf(x->name, sizeof x->name, "%s", nm);
    }
    return n;
}

static void dbg_draw(const dbg_frame_t *fr, int nfr, int sel, const dbg_var_t *var, int nvar, int vtop,
                     const char *why)
{
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    const int small = g->h < 270, sc = g->h >= 540 ? g->h / 270 : 1;
    g->font = small ? &font_console_6x12 : &font_console_8x16;
    const int fw = g->font->width * sc, rh = g->font->height * sc;
    const int cols = g->w / fw, rows = g->h / rh;
    const uint16_t bg = g16_rgb(12, 14, 22), ink = g16_rgb(232, 232, 236), dim = g16_rgb(130, 130, 150),
                   head = g16_rgb(255, 176, 64), cur = g16_rgb(64, 56, 16), brk = g16_rgb(232, 72, 64),
                   up = g16_rgb(150, 190, 232), sel_bg = g16_rgb(36, 40, 64);
    g16_rectfill(g, 0, 0, g->w, g->h, bg);
    char line[160], code[140];
    const dbg_frame_t *f = &fr[sel];
    /* the title */
    ksnprintf(line, sizeof line, "DEBUG  %s  %s  (%s)", f->where, f->name, why);
    line[cols < (int)sizeof line ? cols : (int)sizeof line - 1] = 0;
    g16_text_scaled(g, 0, 0, line, head, sc);
    /* the code round the line of the function chosen */
    const int ncode = (rows - 3) / 2 > 3 ? (rows - 3) / 2 : 3;
    const int digits = 4;
    for (int r = 0; r < ncode; r++) {
        const int n = f->line - ncode / 2 + r;
        const int y = (1 + r) * rh;
        if (n < 1 || !f->game)
            continue;
        src_line(n, code, sizeof code);
        if (n == f->line)
            g16_rectfill(g, 0, y, g->w, rh, cur);
        const int bp = n < DBG_LINES && (dbg.bits[n >> 3] & (1u << (n & 7)));
        snprintf(line, sizeof line, "%c %*d %s", n == f->line ? '>' : ' ', digits, n, code);
        line[cols < (int)sizeof line ? cols : (int)sizeof line - 1] = 0;
        g16_text_scaled(g, 0, y, line, n == f->line ? ink : dim, sc);
        if (bp)
            g16_text_scaled(g, fw, y, "*", brk, sc);      /* (column 1: the > of the line stays) */
    }
    /* the variables (left) and the stack (right) */
    const int y0 = (1 + ncode) * rh, vrows = rows - 2 - ncode - 1;
    const int vcols = cols * 3 / 5, sx = vcols * fw;
    g16_rectfill(g, 0, y0, g->w, rh, g16_rgb(28, 30, 44));
    ksnprintf(line, sizeof line, "variables of %s", f->name);
    line[vcols - 1 > 0 ? vcols - 1 : 0] = 0;
    g16_text_scaled(g, 0, y0, line, head, sc);
    g16_text_scaled(g, sx, y0, "stack", head, sc);
    if (!nvar)
        g16_text_scaled(g, fw, y0 + rh, "(none)", dim, sc);
    for (int i = 0; i < vrows && vtop + i < nvar; i++) {
        const dbg_var_t *v = &var[vtop + i];
        ksnprintf(line, sizeof line, "%s = %s", v->name, v->value);
        line[vcols - 2 > 0 ? vcols - 2 : 0] = 0;
        g16_text_scaled(g, fw, y0 + (1 + i) * rh, line, v->up ? up : ink, sc);
    }
    if (vtop + vrows < nvar)
        g16_text_scaled(g, 0, y0 + vrows * rh, "+", dim, sc);
    for (int i = 0; i < nfr && i < vrows; i++) {
        const int y = y0 + (1 + i) * rh;
        if (i == sel)
            g16_rectfill(g, sx, y, g->w - sx, rh, sel_bg);
        if (fr[i].game)
            ksnprintf(line, sizeof line, "%s  %d", fr[i].name, fr[i].line);
        else
            ksnprintf(line, sizeof line, "%s  %s", fr[i].name, fr[i].where);
        line[cols - vcols - 1 > 0 ? cols - vcols - 1 : 0] = 0;
        g16_text_scaled(g, sx + fw, y, line, fr[i].game ? ink : dim, sc);
    }
    /* the keys */
    const int kb = hid_last_source() == HID_SOURCE_KEYBOARD;
    ksnprintf(line, sizeof line, "%s", kb ? "F5 go  F10 next  F8 into  Shift+F8 out  Esc stop"
                                          : "Start go  A next  X into  Y out  Select stop");
    line[cols < (int)sizeof line ? cols : (int)sizeof line - 1] = 0;
    g16_rectfill(g, 0, (rows - 1) * rh, g->w, rh, g16_rgb(28, 30, 44));
    g16_text_scaled(g, 0, (rows - 1) * rh, line, dim, sc);
    *g = keep;
}

/* stopped at the function of `level` (0 in the line hook, 1 in
 * breakpoint()): the debugger until a key says what next */
static void dbg_pause(lua_State *L, int level, const char *why)
{
    static dbg_frame_t fr[DBG_FRAMES];
    static dbg_var_t var[DBG_VARS];
    const int nfr = dbg_frames(L, level, fr, DBG_FRAMES);
    if (!nfr)
        return;
    int sel = 0, nvar = dbg_vars(L, level, var, DBG_VARS), vtop = 0;
    kprintf("debug: %s at %s in %s\n", why, fr[0].where, fr[0].name);
    for (int i = 0; i < nvar; i++)
        kprintf("debug:   %s%s = %s\n", var[i].up ? "^" : "", var[i].name, var[i].value);
    later_flush();                          /* the frame held first: the debugger turns the pages */
    sync3d();                               /* nothing of the GPU's left to land on the page */
    const size_t bytes = (size_t)rt.g.stride * (size_t)rt.g.h * 2;
    uint16_t *keep = malloc(bytes);
    if (keep)
        memcpy(keep, rt.g.px, bytes);
    audio_pause(1);
    const int text = rt.text_mode;
    rt.text_mode = 0;                       /* the serial keys are buttons here */
    rt.leave_ask = 1;                       /* Esc: rt.leave_no (stop), not Start */
    rt.leave_no = 0;
    static const uint8_t usage[] = { 0x3E, 0x43, 0x41, 0x29, 0x52, 0x51, 0x50, 0x4F, 0x4B, 0x4E };
    enum { K_F5, K_F10, K_F8, K_ESC, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN };
    uint32_t prev = ~0u, kprev = ~0u;       /* what is held now is not an answer */
    int act = -1;
    while (act < 0) {
        const int q = poll_keys();
        uint32_t held = rt.raw_all, keys = 0;
        for (int p = 0; p < INPUT_PLAYERS; p++)
            held |= rt.praw[p];
        for (unsigned i = 0; i < sizeof usage; i++)
            if (hid_usage_held(usage[i]))
                keys |= 1u << i;
        const uint32_t hit = held & ~prev, khit = keys & ~kprev;
        const int shift = hid_usage_held(0xE1) || hid_usage_held(0xE5);
        if ((q & QUIT_FORCE) || rt.leave_no || (khit & (1u << K_ESC)) || (hit & HID_SELECT))
            act = 4;
        else if ((khit & (1u << K_F5)) || (hit & HID_START))
            act = DBG_RUN;
        else if ((khit & (1u << K_F10)) || (hit & HID_A))
            act = DBG_OVER;
        else if (((khit & (1u << K_F8)) && shift) || (hit & HID_Y))
            act = DBG_OUT;
        else if ((khit & (1u << K_F8)) || (hit & HID_X))
            act = DBG_INTO;
        int vstep = 0;
        if ((khit & (1u << K_UP)) || (hit & (1u << 2))) vstep = -1;
        if ((khit & (1u << K_DOWN)) || (hit & (1u << 3))) vstep = 1;
        if (khit & (1u << K_PGUP)) vstep = -8;
        if (khit & (1u << K_PGDN)) vstep = 8;
        vtop += vstep;
        if (vtop > nvar - 1) vtop = nvar - 1;
        if (vtop < 0) vtop = 0;
        int fstep = ((khit & (1u << K_RIGHT)) || (hit & (1u << 1))) ? 1
                  : ((khit & (1u << K_LEFT)) || (hit & 1u)) ? -1 : 0;
        if (fstep && sel + fstep >= 0 && sel + fstep < nfr) {
            sel += fstep;
            nvar = dbg_vars(L, level + sel, var, DBG_VARS);
            vtop = 0;
        }
        prev = held;
        kprev = keys;
        dbg_draw(fr, nfr, sel, var, nvar, vtop, why);
        if (perm_fb)
            bm_video_present(perm_fb, &rt.g);
        audio_idle();
        timer_delay_ms(16);
        rt.leave_held = held;               /* the button of the answer is not the game's */
    }
    rt.leave_ask = 0;
    rt.leave_no = 0;
    rt.text_mode = text;
    if (text)
        while (hid_getc() >= 0)
            ;                               /* F5, F10... were not typed for the game */
    if (keep) {
        memcpy(rt.g.px, keep, bytes);
        free(keep);
    }
    audio_pause(0);
    rt.hook_count = 0;                      /* the time stopped is not the frame's */
    if (act == 4) {
        dbg.stop = 1;
        dbg.mode = DBG_RUN;
        rt.quit = 1;
        kprintf("debug: stopped at %s\n", fr[0].where);
        lua_pushfstring(L, "%s: stopped in the debugger", fr[0].where);
        lua_error(L);
    }
    dbg.mode = act;
    dbg.depth = dbg_depth(L) - level;
    kprintf("debug: %s\n", act == DBG_RUN ? "go on" : act == DBG_OVER ? "next line" :
                           act == DBG_INTO ? "into" : "out");
    dbg_hook(L);
}

/* the line hook: a breakpoint, or the step asked */
void dbg_line(lua_State *L, lua_Debug *ar)
{
    const int line = ar->currentline;
    int hit = 0;
    if (dbg.mode == DBG_INTO)
        hit = 1;
    else if (dbg.mode == DBG_OVER || dbg.mode == DBG_OUT) {
        const int d = dbg_depth(L);
        hit = dbg.mode == DBG_OVER ? d <= dbg.depth : d < dbg.depth;
    }
    const int bp = line > 0 && line < DBG_LINES && (dbg.bits[line >> 3] & (1u << (line & 7)));
    if (!hit && !bp)
        return;
    if (!lua_getinfo(L, "S", ar) || strcmp(ar->source, "=main.lua"))
        return;                             /* the kernel's libraries: on to the game's code */
    dbg_pause(L, 0, bp ? "breakpoint" : "step");
}

/* breakpoint([why]): in a game tried from a tool, the debugger stops here */
int l_breakpoint(lua_State *L)
{
    if (dbg.session)
        dbg_pause(L, 1, luaL_optstring(L, 1, "breakpoint()"));
    return 0;
}
