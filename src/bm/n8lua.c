/*
 * The Lua library "n8" (see n8lua.h). The carts' numbers are Lua floats;
 * the functions read them the way the carts expect (coordinates rounded
 * down, nil as the default, 16.16 for the bit operations) and print them
 * the way the carts do ("1.5", never "1.0").
 */
#include "n8lua.h"
#include "n8.h"
#include "n8cart.h"
#include "audio/n8snd.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"

#define LABEL_SLOTS 48

static n8_t *M;
static n8lua_io_t io;
static uint16_t *labels[LABEL_SLOTS];

void n8lua_set_io(const n8lua_io_t *p)
{
    io = *p;
}

static n8_t *mach(lua_State *L)
{
    if (!M && !(M = calloc(1, sizeof *M)))
        luaL_error(L, "not enough memory for the machine");
    return M;
}

void n8lua_close(void)
{
    n8snd_attach(NULL, 0);
    free(M);
    M = NULL;
    for (int i = 0; i < LABEL_SLOTS; i++) {
        free(labels[i]);
        labels[i] = NULL;
    }
}

/* ------------------------------------------------------------ arguments */

static double num(lua_State *L, int i, double def)
{
    switch (lua_type(L, i)) {
    case LUA_TNUMBER:
        return lua_tonumber(L, i);
    case LUA_TBOOLEAN:
        return lua_toboolean(L, i);
    case LUA_TSTRING: {
        size_t len;
        const char *s = lua_tolstring(L, i, &len);
        double v;
        return n8_tonum(s, len, 0, &v) ? v : def;
    }
    default:
        return def;
    }
}

static int ia(lua_State *L, int i, int def)
{
    return n8_int(num(L, i, def));
}

static int given(lua_State *L, int i)
{
    return !lua_isnoneornil(L, i);
}

/* a colour argument: nil = the pen */
static int col(lua_State *L, int i)
{
    return given(L, i) ? (int)(n8_fix(num(L, i, 0)) >> 16 & 0xFF) : -1;
}

static void pushnum(lua_State *L, double v)
{
    lua_pushnumber(L, v);
}

static void pushfix(lua_State *L, int32_t f)
{
    lua_pushnumber(L, f / 65536.0);
}

/* ------------------------------------------------------------ drawing */

static int l_cls(lua_State *L) { n8_cls(mach(L), ia(L, 1, 0)); return 0; }

static int l_pset(lua_State *L)
{
    n8_pset(mach(L), ia(L, 1, 0), ia(L, 2, 0), col(L, 3));
    return 0;
}

static int l_pget(lua_State *L) { lua_pushinteger(L, n8_pget(mach(L), ia(L, 1, 0), ia(L, 2, 0))); return 1; }
static int l_sget(lua_State *L) { lua_pushinteger(L, n8_sget(mach(L), ia(L, 1, 0), ia(L, 2, 0))); return 1; }

static int l_sset(lua_State *L)
{
    n8_sset(mach(L), ia(L, 1, 0), ia(L, 2, 0), col(L, 3));
    return 0;
}

static int l_fget(lua_State *L)
{
    n8_t *m = mach(L);
    int n = ia(L, 1, 0) & 255, f = m->ram[N8_FLAGS + n];
    if (given(L, 2))
        lua_pushboolean(L, f >> (ia(L, 2, 0) & 7) & 1);
    else
        lua_pushinteger(L, f);
    return 1;
}

static int l_fset(lua_State *L)
{
    n8_t *m = mach(L);
    int n = ia(L, 1, 0) & 255;
    uint8_t *f = &m->ram[N8_FLAGS + n];
    if (given(L, 3)) {
        int b = ia(L, 2, 0) & 7;
        if (lua_toboolean(L, 3) && !(lua_isnumber(L, 3) && lua_tonumber(L, 3) == 0))
            *f |= (uint8_t)(1u << b);
        else
            *f &= (uint8_t)~(1u << b);
    } else {
        *f = (uint8_t)ia(L, 2, 0);
    }
    return 0;
}

static void line_end(n8_t *m, int x, int y)
{
    m->ram[0x5f3c] = (uint8_t)x; m->ram[0x5f3d] = (uint8_t)(x >> 8);
    m->ram[0x5f3e] = (uint8_t)y; m->ram[0x5f3f] = (uint8_t)(y >> 8);
    m->ram[0x5f35] = 0;
}

static int l_line(lua_State *L)
{
    n8_t *m = mach(L);
    int n = lua_gettop(L);
    while (n && lua_isnil(L, n))
        n--;
    if (n == 0) {
        m->ram[0x5f35] = 1;                 /* the next line(x, y) only moves */
        return 0;
    }
    if (n <= 3) {
        int x1 = ia(L, 1, 0), y1 = ia(L, 2, 0);
        if (m->ram[0x5f35]) {
            n8_color(m, col(L, 3));
        } else {
            int x0 = (int16_t)(m->ram[0x5f3c] | m->ram[0x5f3d] << 8);
            int y0 = (int16_t)(m->ram[0x5f3e] | m->ram[0x5f3f] << 8);
            n8_line(m, x0, y0, x1, y1, col(L, 3));
        }
        line_end(m, x1, y1);
        return 0;
    }
    int x1 = ia(L, 3, 0), y1 = ia(L, 4, 0);
    n8_line(m, ia(L, 1, 0), ia(L, 2, 0), x1, y1, col(L, 5));
    line_end(m, x1, y1);
    return 0;
}

static int l_rect(lua_State *L)
{
    n8_rect(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), col(L, 5), 0);
    return 0;
}

static int l_rectfill(lua_State *L)
{
    n8_rect(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), col(L, 5), 1);
    return 0;
}

static int l_rrect(lua_State *L)
{
    n8_rrect(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), ia(L, 5, 0), col(L, 6), 0);
    return 0;
}

static int l_rrectfill(lua_State *L)
{
    n8_rrect(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), ia(L, 5, 0), col(L, 6), 1);
    return 0;
}

static int l_circ(lua_State *L)
{
    n8_circ(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 4), col(L, 4), 0);
    return 0;
}

static int l_circfill(lua_State *L)
{
    n8_circ(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 4), col(L, 4), 1);
    return 0;
}

static int l_oval(lua_State *L)
{
    n8_oval(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), col(L, 5), 0);
    return 0;
}

static int l_ovalfill(lua_State *L)
{
    n8_oval(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), col(L, 5), 1);
    return 0;
}

static int l_spr(lua_State *L)
{
    double w = num(L, 4, 1), h = num(L, 5, 1);
    n8_spr(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), n8_int(w * 8), n8_int(h * 8),
           lua_toboolean(L, 6), lua_toboolean(L, 7));
    return 0;
}

static int l_sspr(lua_State *L)
{
    int sw = ia(L, 3, 0), sh = ia(L, 4, 0);
    n8_sspr(mach(L), ia(L, 1, 0), ia(L, 2, 0), sw, sh, ia(L, 5, 0), ia(L, 6, 0), ia(L, 7, sw), ia(L, 8, sh),
            lua_toboolean(L, 9), lua_toboolean(L, 10));
    return 0;
}

static int l_map(lua_State *L)
{
    n8_t *m = mach(L);
    int w, h;
    n8_map_size(m, &w, &h);
    n8_map(m, ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), ia(L, 5, w), ia(L, 6, h), ia(L, 7, 0));
    return 0;
}

static int l_mget(lua_State *L) { lua_pushinteger(L, n8_mget(mach(L), ia(L, 1, 0), ia(L, 2, 0))); return 1; }

static int l_mset(lua_State *L)
{
    n8_mset(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0));
    return 0;
}

static int l_tline(lua_State *L)
{
    n8_tline(mach(L), ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 0), n8_fix(num(L, 5, 0)),
             n8_fix(num(L, 6, 0)), n8_fix(num(L, 7, 0.125)), n8_fix(num(L, 8, 0)), ia(L, 9, 0));
    return 0;
}

/* ------------------------------------------------------------ text */

/* tostr() of one value, pushed */
static void tostr(lua_State *L, int i, int flags)
{
    char buf[40];
    switch (lua_type(L, i)) {
    case LUA_TNONE:
        lua_pushliteral(L, "[no value]");
        break;
    case LUA_TNIL:
        lua_pushliteral(L, "[nil]");
        break;
    case LUA_TBOOLEAN:
        lua_pushstring(L, lua_toboolean(L, i) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        n8_tostr(lua_tonumber(L, i), flags, buf, sizeof buf);
        lua_pushstring(L, buf);
        break;
    case LUA_TSTRING:
        lua_pushvalue(L, i);
        break;
    default:
        if (luaL_callmeta(L, i, "__tostring") && lua_isstring(L, -1))
            break;
        lua_pushfstring(L, "[%s]", luaL_typename(L, i));
        break;
    }
}

static int l_tostr(lua_State *L)
{
    tostr(L, 1, ia(L, 2, 0));
    return 1;
}

static int l_tonum(lua_State *L)
{
    int flags = ia(L, 2, 0);
    double v;
    switch (lua_type(L, 1)) {
    case LUA_TNUMBER:
        lua_pushvalue(L, 1);
        return 1;
    case LUA_TBOOLEAN:
        lua_pushnumber(L, lua_toboolean(L, 1));
        return 1;
    case LUA_TSTRING: {
        size_t len;
        const char *s = lua_tolstring(L, 1, &len);
        if (n8_tonum(s, len, flags, &v)) {
            lua_pushnumber(L, v);
            return 1;
        }
        break;
    }
    }
    if (flags & 4) {
        lua_pushnumber(L, 0);
        return 1;
    }
    return 0;
}

/* print(text, [x, y], [colour]) or print(text, colour) */
static int l_print(lua_State *L)
{
    n8_t *m = mach(L);
    int n = lua_gettop(L);
    while (n > 1 && lua_isnil(L, n))
        n--;
    tostr(L, 1, 0);
    size_t len;
    const uint8_t *s = (const uint8_t *)lua_tolstring(L, -1, &len);
    int r;
    if (n <= 1)
        r = n8_print(m, s, len, 0, 0, -1, 1);
    else if (n == 2)
        r = n8_print(m, s, len, 0, 0, col(L, 2), 1);
    else
        r = n8_print(m, s, len, ia(L, 2, 0), ia(L, 3, 0), col(L, 4), 0);
    lua_pushinteger(L, r);
    return 1;
}

static int l_cursor(lua_State *L)
{
    n8_t *m = mach(L);
    int px = m->ram[0x5f26], py = m->ram[0x5f27], pc = m->ram[N8_PEN];
    m->ram[0x5f26] = (uint8_t)ia(L, 1, 0);
    m->ram[0x5f27] = (uint8_t)ia(L, 2, 0);
    if (given(L, 3))
        n8_color(m, col(L, 3));
    lua_pushinteger(L, px);
    lua_pushinteger(L, py);
    lua_pushinteger(L, pc);
    return 3;
}

static int l_color(lua_State *L)
{
    n8_t *m = mach(L);
    int prev = m->ram[N8_PEN];
    m->ram[N8_PEN] = given(L, 1) ? (uint8_t)col(L, 1) : 6;
    lua_pushinteger(L, prev);
    return 1;
}

/* _cat(a, b, ...): the carts' "..", numbers written their way */
static int l_cat(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        int t = lua_type(L, i);
        if (t != LUA_TSTRING && t != LUA_TNUMBER) {
            /* a table with __concat, or an error as Lua gives it: let Lua
             * join them, from the right */
            for (int k = 1; k <= n; k++)
                if (lua_type(L, k) == LUA_TNUMBER) {
                    tostr(L, k, 0);
                    lua_replace(L, k);
                }
            lua_concat(L, n);
            return 1;
        }
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (lua_type(L, i) == LUA_TNUMBER) {
            char buf[40];
            n8_tostr(lua_tonumber(L, i), 0, buf, sizeof buf);
            luaL_addstring(&b, buf);
        } else {
            size_t len;
            const char *s = lua_tolstring(L, i, &len);
            luaL_addlstring(&b, s, len);
        }
    }
    luaL_pushresult(&b);
    return 1;
}

static int l_chr(lua_State *L)
{
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++)
        luaL_addchar(&b, (char)(ia(L, i, 0) & 255));
    luaL_pushresult(&b);
    return 1;
}

static int l_ord(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER)
        return 0;
    lua_settop(L, 3);
    tostr(L, 1, 0);
    size_t len;
    const unsigned char *s = (const unsigned char *)lua_tolstring(L, -1, &len);
    int i = ia(L, 2, 1), n = ia(L, 3, 1), r = 0;
    if (n > 256) n = 256;
    luaL_checkstack(L, n, "ord");
    for (int k = 0; k < n; k++, r++) {
        int p = i + k;
        if (p < 1 || (size_t)p > len)
            lua_pushnil(L);
        else
            lua_pushinteger(L, s[p - 1]);
    }
    return r;
}

static int l_sub(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        return 0;
    lua_settop(L, 3);
    tostr(L, 1, 0);
    size_t len;
    const char *s = lua_tolstring(L, -1, &len);
    long l = (long)len, i = ia(L, 2, 1), j = l;
    if (lua_type(L, 3) == LUA_TBOOLEAN)
        j = i;                              /* sub(s, i, true): one character */
    else if (given(L, 3))
        j = ia(L, 3, (int)l);
    if (i < 0) i = l + i + 1 < 1 ? 1 : l + i + 1;
    if (i == 0) i = 1;
    if (j < 0) j = l + j + 1;
    if (j > l) j = l;
    if (i > j)
        lua_pushliteral(L, "");
    else
        lua_pushlstring(L, s + i - 1, (size_t)(j - i + 1));
    return 1;
}

/* split(text, [separator | group size], [convert numbers]) */
static int l_split(lua_State *L)
{
    lua_settop(L, 3);                       /* the text converted goes above the arguments */
    tostr(L, 1, 0);
    size_t len;
    const char *s = lua_tolstring(L, -1, &len);
    int conv = !given(L, 3) || lua_toboolean(L, 3);
    lua_newtable(L);
    int tab = lua_gettop(L), k = 1;
    size_t group = 0;                       /* > 0: pieces of this many characters */
    char sep = ',';
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int g = ia(L, 2, 1);
        group = g > 0 ? (size_t)g : 1;
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        size_t sl;
        const char *sp = lua_tolstring(L, 2, &sl);
        if (sl)
            sep = sp[0];
        else
            group = 1;                      /* "": every character */
    }
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        int cut = group ? (i - start == group || i == len) : (i == len || s[i] == sep);
        if (!cut)
            continue;
        if (group && i == len && i == start && len)
            break;
        double v;
        if (conv && i > start && n8_tonum(s + start, i - start, 0, &v))
            lua_pushnumber(L, v);
        else
            lua_pushlstring(L, s + start, i - start);
        lua_rawseti(L, tab, k++);
        start = group ? i : i + 1;
        if (group && i == len)
            break;
    }
    return 1;
}

/* ------------------------------------------------------------ state */

static int l_camera(lua_State *L)
{
    n8_t *m = mach(L);
    int px = (int16_t)(m->ram[0x5f28] | m->ram[0x5f29] << 8), py = (int16_t)(m->ram[0x5f2a] | m->ram[0x5f2b] << 8);
    int x = ia(L, 1, 0), y = ia(L, 2, 0);
    m->ram[0x5f28] = (uint8_t)x; m->ram[0x5f29] = (uint8_t)(x >> 8);
    m->ram[0x5f2a] = (uint8_t)y; m->ram[0x5f2b] = (uint8_t)(y >> 8);
    lua_pushinteger(L, px);
    lua_pushinteger(L, py);
    return 2;
}

static int l_clip(lua_State *L)
{
    n8_t *m = mach(L);
    int ox = m->ram[0x5f20], oy = m->ram[0x5f21], ox1 = m->ram[0x5f22], oy1 = m->ram[0x5f23];
    int x0 = 0, y0 = 0, x1 = 128, y1 = 128;
    if (given(L, 1)) {
        x0 = ia(L, 1, 0);
        y0 = ia(L, 2, 0);
        x1 = x0 + ia(L, 3, 128);
        y1 = y0 + ia(L, 4, 128);
        if (lua_toboolean(L, 5)) {
            if (x0 < ox) x0 = ox;
            if (y0 < oy) y0 = oy;
            if (x1 > ox1) x1 = ox1;
            if (y1 > oy1) y1 = oy1;
        }
    }
    x0 = x0 < 0 ? 0 : x0 > 128 ? 128 : x0;
    y0 = y0 < 0 ? 0 : y0 > 128 ? 128 : y0;
    x1 = x1 < x0 ? x0 : x1 > 128 ? 128 : x1;
    y1 = y1 < y0 ? y0 : y1 > 128 ? 128 : y1;
    m->ram[0x5f20] = (uint8_t)x0; m->ram[0x5f21] = (uint8_t)y0;
    m->ram[0x5f22] = (uint8_t)x1; m->ram[0x5f23] = (uint8_t)y1;
    lua_pushinteger(L, ox);
    lua_pushinteger(L, oy);
    lua_pushinteger(L, ox1 - ox);
    lua_pushinteger(L, oy1 - oy);
    return 4;
}

static int set_pal(n8_t *m, int c0, int c1, int p)
{
    c0 &= 15;
    int prev;
    if (p == 1) {
        prev = m->ram[N8_PAL_SCREEN + c0];
        m->ram[N8_PAL_SCREEN + c0] = (uint8_t)(c1 & 0x8F);
    } else if (p == 2) {
        prev = m->ram[0x5f60 + c0];
        m->ram[0x5f60 + c0] = (uint8_t)(c1 & 0x8F);
    } else {
        prev = m->ram[N8_PAL_DRAW + c0] & 15;
        m->ram[N8_PAL_DRAW + c0] = (uint8_t)((m->ram[N8_PAL_DRAW + c0] & 0x10) | (c1 & 15));
    }
    return prev;
}

static int l_pal(lua_State *L)
{
    n8_t *m = mach(L);
    if (lua_istable(L, 1)) {
        int p = ia(L, 2, 0);
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            if (lua_type(L, -2) == LUA_TNUMBER)
                set_pal(m, n8_int(lua_tonumber(L, -2)), n8_int(num(L, -1, 0)), p);
            lua_pop(L, 1);
        }
        return 0;
    }
    if (!given(L, 1)) {
        n8_reset_pal(m);
        return 0;
    }
    if (!given(L, 2)) {
        int p = ia(L, 1, 0);
        for (int i = 0; i < 16; i++) {
            if (p == 1) m->ram[N8_PAL_SCREEN + i] = (uint8_t)i;
            else if (p == 2) m->ram[0x5f60 + i] = (uint8_t)i;
            else m->ram[N8_PAL_DRAW + i] = (uint8_t)((m->ram[N8_PAL_DRAW + i] & 0x10) | i);
        }
        return 0;
    }
    lua_pushinteger(L, set_pal(m, ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0)));
    return 1;
}

static int l_palt(lua_State *L)
{
    n8_t *m = mach(L);
    if (!given(L, 1)) {
        for (int i = 0; i < 16; i++)
            m->ram[N8_PAL_DRAW + i] = (uint8_t)((m->ram[N8_PAL_DRAW + i] & 15) | (i ? 0 : 0x10));
        return 0;
    }
    if (!given(L, 2)) {
        int prev = 0, bits = n8_fix(num(L, 1, 0)) >> 16 & 0xFFFF;
        for (int i = 0; i < 16; i++) {
            prev |= (m->ram[N8_PAL_DRAW + i] & 0x10 ? 1 : 0) << (15 - i);
            int t = bits >> (15 - i) & 1;
            m->ram[N8_PAL_DRAW + i] = (uint8_t)((m->ram[N8_PAL_DRAW + i] & 15) | (t ? 0x10 : 0));
        }
        lua_pushinteger(L, prev);
        return 1;
    }
    int c = ia(L, 1, 0) & 15, prev = !!(m->ram[N8_PAL_DRAW + c] & 0x10);
    int t = lua_toboolean(L, 2) && !(lua_type(L, 2) == LUA_TNUMBER && lua_tonumber(L, 2) == 0);
    m->ram[N8_PAL_DRAW + c] = (uint8_t)((m->ram[N8_PAL_DRAW + c] & 15) | (t ? 0x10 : 0));
    lua_pushboolean(L, prev);
    return 1;
}

static int l_fillp(lua_State *L)
{
    n8_t *m = mach(L);
    int32_t prev = (int32_t)((uint32_t)(m->ram[0x5f31] | m->ram[0x5f32] << 8) << 16);
    if (m->ram[0x5f33] & 1) prev |= 0x8000;
    if (m->ram[0x5f33] & 2) prev |= 0x4000;
    if (m->ram[0x5f33] & 4) prev |= 0x2000;
    int32_t f = n8_fix(num(L, 1, 0));
    uint32_t pat = (uint32_t)f >> 16 & 0xFFFF;
    m->ram[0x5f31] = (uint8_t)pat;
    m->ram[0x5f32] = (uint8_t)(pat >> 8);
    m->ram[0x5f33] = (uint8_t)((f & 0x8000 ? 1 : 0) | (f & 0x4000 ? 2 : 0) | (f & 0x2000 ? 4 : 0));
    pushfix(L, prev);
    return 1;
}

/* ------------------------------------------------------------ memory */

static uint32_t addr(lua_State *L, int i)
{
    return (uint32_t)n8_int(num(L, i, 0)) & 0xFFFF;
}

static int l_peek(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = ia(L, 2, 1);
    if (n > 8192) n = 8192;
    luaL_checkstack(L, n, "peek");
    for (int i = 0; i < n; i++)
        lua_pushinteger(L, m->ram[(a + (uint32_t)i) & 0xFFFF]);
    return n < 0 ? 0 : n;
}

static int l_poke(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; i++)
        n8_poke(m, a + (uint32_t)(i - 2), ia(L, i, 0));
    return 0;
}

static int l_peek2(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = ia(L, 2, 1);
    if (n > 4096) n = 4096;
    luaL_checkstack(L, n, "peek2");
    for (int i = 0; i < n; i++, a += 2)
        lua_pushinteger(L, (int16_t)(m->ram[a & 0xFFFF] | m->ram[(a + 1) & 0xFFFF] << 8));
    return n < 0 ? 0 : n;
}

static int l_poke2(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; i++, a += 2) {
        int v = ia(L, i, 0);
        n8_poke(m, a, v);
        n8_poke(m, a + 1, v >> 8);
    }
    return 0;
}

static int l_peek4(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = ia(L, 2, 1);
    if (n > 2048) n = 2048;
    luaL_checkstack(L, n, "peek4");
    for (int i = 0; i < n; i++, a += 4) {
        uint32_t v = 0;
        for (int k = 3; k >= 0; k--)
            v = v << 8 | m->ram[(a + (uint32_t)k) & 0xFFFF];
        pushfix(L, (int32_t)v);
    }
    return n < 0 ? 0 : n;
}

static int l_poke4(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; i++, a += 4) {
        uint32_t v = (uint32_t)n8_fix(num(L, i, 0));
        for (int k = 0; k < 4; k++)
            n8_poke(m, a + (uint32_t)k, (int)(v >> (8 * k)));
    }
    return 0;
}

static int l_memcpy(lua_State *L)
{
    n8_memcpy(mach(L), addr(L, 1), addr(L, 2), ia(L, 3, 0));
    return 0;
}

static int l_memset(lua_State *L)
{
    n8_memset(mach(L), addr(L, 1), ia(L, 2, 0), ia(L, 3, 0));
    return 0;
}

/* reload(dst, src, len): from the cart as it was loaded */
static int l_reload(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t d = addr(L, 1), s = addr(L, 2);
    int n = ia(L, 3, N8_ROM_SIZE);
    for (int i = 0; i < n; i++)
        m->ram[(d + (uint32_t)i) & 0xFFFF] = s + (uint32_t)i < N8_ROM_SIZE ? m->rom[s + (uint32_t)i] : 0;
    return 0;
}

static int l_cstore(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t d = addr(L, 1), s = addr(L, 2);
    int n = ia(L, 3, N8_ROM_SIZE);
    for (int i = 0; i < n && d + (uint32_t)i < N8_ROM_SIZE; i++)
        m->rom[d + (uint32_t)i] = m->ram[(s + (uint32_t)i) & 0xFFFF];
    return 0;
}

/* ------------------------------------------------------------ math */

static int l_flr(lua_State *L) { pushnum(L, floor(num(L, 1, 0))); return 1; }
static int l_ceil(lua_State *L) { pushnum(L, ceil(num(L, 1, 0))); return 1; }
static int l_abs(lua_State *L) { pushnum(L, fabs(num(L, 1, 0))); return 1; }
static int l_sgn(lua_State *L) { pushnum(L, num(L, 1, 0) < 0 ? -1 : 1); return 1; }

static int l_min(lua_State *L)
{
    double a = num(L, 1, 0), b = num(L, 2, 0);
    pushnum(L, a < b ? a : b);
    return 1;
}

static int l_max(lua_State *L)
{
    double a = num(L, 1, 0), b = num(L, 2, 0);
    pushnum(L, a > b ? a : b);
    return 1;
}

static int l_mid(lua_State *L)
{
    double a = num(L, 1, 0), b = num(L, 2, 0), c = num(L, 3, 0);
    double lo = a < b ? a : b, hi = a < b ? b : a;
    pushnum(L, c < lo ? lo : c > hi ? hi : c);
    return 1;
}

static int l_sqrt(lua_State *L)
{
    double v = num(L, 1, 0);
    pushnum(L, v > 0 ? sqrt(v) : 0);
    return 1;
}

static double round16(double v)
{
    return floor(v * 65536.0 + 0.5) / 65536.0;
}

static int l_sin(lua_State *L) { pushnum(L, round16(-sin(num(L, 1, 0) * 2 * M_PI))); return 1; }
static int l_cos(lua_State *L) { pushnum(L, round16(cos(num(L, 1, 0) * 2 * M_PI))); return 1; }

static int l_atan2(lua_State *L)
{
    double dx = num(L, 1, 0), dy = num(L, 2, 0);
    if (dx == 0 && dy == 0) {
        pushnum(L, 0.25);
        return 1;
    }
    double a = atan2(-dy, dx) / (2 * M_PI);
    if (a < 0)
        a += 1;
    pushnum(L, round16(a >= 1 ? 0 : a));
    return 1;
}

static int l_rnd(lua_State *L)
{
    n8_t *m = mach(L);
    if (lua_istable(L, 1)) {
        lua_Integer n = (lua_Integer)lua_rawlen(L, 1);
        if (!n)
            return 0;
        lua_rawgeti(L, 1, 1 + n8_rnd(m, (int32_t)n << 16) / 65536);
        return 1;
    }
    pushfix(L, n8_rnd(m, n8_fix(num(L, 1, 1))));
    return 1;
}

static int l_srand(lua_State *L)
{
    n8_srand(mach(L), n8_fix(num(L, 1, 0)));
    return 0;
}

static int32_t fx(lua_State *L, int i) { return n8_fix(num(L, i, 0)); }

static int l_band(lua_State *L) { pushfix(L, fx(L, 1) & fx(L, 2)); return 1; }
static int l_bor(lua_State *L) { pushfix(L, fx(L, 1) | fx(L, 2)); return 1; }
static int l_bxor(lua_State *L) { pushfix(L, fx(L, 1) ^ fx(L, 2)); return 1; }
static int l_bnot(lua_State *L) { pushfix(L, ~fx(L, 1)); return 1; }
static int l_shl(lua_State *L) { pushfix(L, n8_shl(fx(L, 1), ia(L, 2, 0))); return 1; }
static int l_shr(lua_State *L) { pushfix(L, n8_shr(fx(L, 1), ia(L, 2, 0))); return 1; }
static int l_lshr(lua_State *L) { pushfix(L, n8_lshr(fx(L, 1), ia(L, 2, 0))); return 1; }
static int l_rotl(lua_State *L) { pushfix(L, n8_rotl(fx(L, 1), ia(L, 2, 0))); return 1; }
static int l_rotr(lua_State *L) { pushfix(L, n8_rotr(fx(L, 1), ia(L, 2, 0))); return 1; }

/* ------------------------------------------------------------ tables */

static int l_add(lua_State *L)
{
    if (!lua_istable(L, 1))
        return 0;
    lua_Integer n = (lua_Integer)lua_rawlen(L, 1);
    if (given(L, 3)) {
        lua_Integer i = n8_int(num(L, 3, 0));
        if (i < 1) i = 1;
        if (i > n + 1) i = n + 1;
        for (lua_Integer k = n; k >= i; k--) {
            lua_rawgeti(L, 1, k);
            lua_rawseti(L, 1, k + 1);
        }
        lua_pushvalue(L, 2);
        lua_rawseti(L, 1, i);
    } else {
        lua_pushvalue(L, 2);
        lua_rawseti(L, 1, n + 1);
    }
    lua_settop(L, 2);
    return 1;
}

static void remove_at(lua_State *L, lua_Integer i, lua_Integer n)
{
    for (lua_Integer k = i; k < n; k++) {
        lua_rawgeti(L, 1, k + 1);
        lua_rawseti(L, 1, k);
    }
    lua_pushnil(L);
    lua_rawseti(L, 1, n);
}

static int l_del(lua_State *L)
{
    if (!lua_istable(L, 1))
        return 0;
    lua_Integer n = (lua_Integer)lua_rawlen(L, 1);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        int eq = lua_compare(L, -1, 2, LUA_OPEQ);
        lua_pop(L, 1);
        if (eq) {
            lua_rawgeti(L, 1, i);
            remove_at(L, i, n);
            return 1;
        }
    }
    return 0;
}

static int l_deli(lua_State *L)
{
    if (!lua_istable(L, 1))
        return 0;
    lua_Integer n = (lua_Integer)lua_rawlen(L, 1);
    lua_Integer i = given(L, 2) ? n8_int(num(L, 2, 0)) : n;
    if (i < 1 || i > n)
        return 0;
    lua_rawgeti(L, 1, i);
    remove_at(L, i, n);
    return 1;
}

static int l_count(lua_State *L)
{
    if (!lua_istable(L, 1)) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_Integer n = (lua_Integer)lua_rawlen(L, 1);
    if (!given(L, 2)) {
        lua_pushinteger(L, n);
        return 1;
    }
    lua_Integer c = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        c += lua_compare(L, -1, 2, LUA_OPEQ);
        lua_pop(L, 1);
    }
    lua_pushinteger(L, c);
    return 1;
}

/* ------------------------------------------------------------ input */

static int l_btn(lua_State *L)
{
    n8_t *m = mach(L);
    if (!given(L, 1)) {
        lua_pushinteger(L, (m->btn[0] & 0x7F) | (m->btn[1] & 0x7F) << 8);
        return 1;
    }
    lua_pushboolean(L, n8_btn(m, ia(L, 1, 0), ia(L, 2, 0)));
    return 1;
}

static int l_btnp(lua_State *L)
{
    n8_t *m = mach(L);
    if (!given(L, 1)) {
        int bits = 0;
        for (int p = 0; p < 2; p++)
            for (int b = 0; b < 7; b++)
                bits |= n8_btnp(m, b, p) << (b + 8 * p);
        lua_pushinteger(L, bits);
        return 1;
    }
    lua_pushboolean(L, n8_btnp(m, ia(L, 1, 0), ia(L, 2, 0)));
    return 1;
}

/* ------------------------------------------------------------ sound */

static int l_sfx(lua_State *L)
{
    n8snd_sfx(ia(L, 1, -1), ia(L, 2, -1), ia(L, 3, 0), ia(L, 4, 0));
    return 0;
}

static int l_music(lua_State *L)
{
    n8snd_music(ia(L, 1, 0), ia(L, 2, 0), ia(L, 3, 0));
    return 0;
}

/* ------------------------------------------------------------ the host side */

static g16_t *target(lua_State *L)
{
    g16_t *g = io.target ? io.target() : NULL;
    if (!g)
        luaL_error(L, "nano8: no screen to draw on");
    return g;
}

/* scaled copy of a 128x128 RGB565 picture into the frame (nearest pixel) */
static void put_picture(g16_t *g, const uint16_t *src, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= g->h)
            continue;
        const uint16_t *row = src + (j * 128 / h) * 128;
        uint16_t *o = g->px + (uint32_t)dy * g->stride;
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx >= 0 && dx < g->w)
                o[dx] = row[i * 128 / w];
        }
    }
}

/* blit(x, y, w, h): the machine's screen into the frame */
static int l_blit(lua_State *L)
{
    n8_t *m = mach(L);
    g16_t *g = target(L);
    int x = ia(L, 1, 0), y = ia(L, 2, 0), w = ia(L, 3, 128), h = ia(L, 4, 128);
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > g->w || y + h > g->h)
        return luaL_error(L, "blit outside the screen");
    n8_blit(m, g->px + (uint32_t)y * g->stride + (uint32_t)x, g->stride, w, h);
    return 0;
}

static int read_cart(lua_State *L, const char *path, int flags, n8_cart_t *c)
{
    uint8_t *data;
    size_t len;
    char err[96];
    if (!io.load || io.load(path, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "cannot read %s", path);
        return -1;
    }
    int r = n8_cart_load(data, len, flags, c, err, sizeof err);
    free(data);
    if (r != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return -1;
    }
    return 0;
}

static void info(lua_State *L, const n8_cart_t *c)
{
    lua_newtable(L);
    lua_pushstring(L, c->title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, c->author);
    lua_setfield(L, -2, "author");
    lua_pushinteger(L, c->version);
    lua_setfield(L, -2, "version");
}

/* load(path) -> {title, author, version, code}: the cart becomes the ROM
 * of the machine (power() starts it); nil and a message if it cannot */
static int l_load(lua_State *L)
{
    n8_t *m = mach(L);
    n8_cart_t c;
    if (read_cart(L, luaL_checkstring(L, 1), 0, &c) != 0)
        return 2;
    memcpy(m->rom, c.rom, N8_ROM_SIZE);
    info(L, &c);
    lua_pushlstring(L, c.code, c.code_len);
    lua_setfield(L, -2, "code");
    lua_pushboolean(L, c.label != NULL);
    lua_setfield(L, -2, "label");
    if (c.label) {
        free(labels[0]);
        labels[0] = c.label;
        c.label = NULL;
    }
    n8_cart_free(&c);
    return 1;
}

/* preview(path, slot) -> {title, author, version, label}: the label goes
 * into slot (1..47) for drawlabel */
static int l_preview(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int slot = (int)luaL_checkinteger(L, 2);
    luaL_argcheck(L, slot >= 1 && slot < LABEL_SLOTS, 2, "slot 1..47");
    n8_cart_t c;
    if (read_cart(L, path, N8_LOAD_LABEL, &c) != 0)
        return 2;
    info(L, &c);
    lua_pushboolean(L, c.label != NULL);
    lua_setfield(L, -2, "label");
    free(labels[slot]);
    labels[slot] = c.label;
    c.label = NULL;
    n8_cart_free(&c);
    return 1;
}

/* drawlabel(slot, x, y, w, h) -> false if the slot has no label */
static int l_drawlabel(lua_State *L)
{
    int slot = (int)luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= LABEL_SLOTS || !labels[slot]) {
        lua_pushboolean(L, 0);
        return 1;
    }
    put_picture(target(L), labels[slot], ia(L, 2, 0), ia(L, 3, 0), ia(L, 4, 128), ia(L, 5, 128));
    lua_pushboolean(L, 1);
    return 1;
}

/* snapshot(slot): the screen as it is now becomes a label (the pause
 * menu's picture, the save of a label) */
static int l_snapshot(lua_State *L)
{
    n8_t *m = mach(L);
    int slot = (int)luaL_checkinteger(L, 1);
    luaL_argcheck(L, slot >= 0 && slot < LABEL_SLOTS, 1, "slot 0..47");
    if (!labels[slot] && !(labels[slot] = malloc(128 * 128 * 2)))
        return luaL_error(L, "not enough memory");
    n8_blit(m, labels[slot], 128, 128, 128);
    return 0;
}

static int l_power(lua_State *L)
{
    n8_t *m = mach(L);
    n8_power(m);
    n8snd_attach(NULL, 0);
    n8snd_attach(m->ram, 0);
    return 0;
}

/* present(): the frame is complete, it goes on show (flip) */
static int l_present(lua_State *L)
{
    n8_present(mach(L));
    return 0;
}

static int l_setfps(lua_State *L)
{
    mach(L)->fps = ia(L, 1, 30) >= 60 ? 60 : 30;
    return 0;
}

/* buttons(b0, b1, ... b7): this frame's buttons per player */
static int l_buttons(lua_State *L)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (uint8_t)ia(L, i + 1, 0);
    n8_buttons(mach(L), b);
    return 0;
}

/* compile(source, name, env) -> function, or nil and the message: text
 * only, its globals in env */
static int l_compile(lua_State *L)
{
    size_t len;
    const char *src = luaL_checklstring(L, 1, &len);
    const char *name = luaL_optstring(L, 2, "=cart");
    if (luaL_loadbufferx(L, src, len, name, "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (lua_istable(L, 3)) {
        lua_pushvalue(L, 3);
        if (!lua_setupvalue(L, -2, 1))
            lua_pop(L, 1);
    }
    return 1;
}

static int l_sndstat(lua_State *L)
{
    lua_pushinteger(L, n8snd_stat(ia(L, 1, 0)));
    return 1;
}

static int l_pause(lua_State *L)
{
    n8snd_pause(lua_toboolean(L, 1));
    return 0;
}

/* peekstr(addr, len) / pokestr(addr, bytes): blocks of the RAM as strings
 * (persistent data, screenshots) */
static int l_peekstr(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    int n = ia(L, 2, 0);
    if (n < 0) n = 0;
    if (a + (uint32_t)n > N8_RAM_SIZE) n = (int)(N8_RAM_SIZE - a);
    lua_pushlstring(L, (const char *)m->ram + a, (size_t)n);
    return 1;
}

static int l_pokestr(lua_State *L)
{
    n8_t *m = mach(L);
    uint32_t a = addr(L, 1);
    size_t n;
    const char *s = luaL_checklstring(L, 2, &n);
    if (a + n > N8_RAM_SIZE) n = N8_RAM_SIZE - a;
    memcpy(m->ram + a, s, n);
    return 0;
}

/* glyph(c) -> width and the five rows: the font, for the console's own
 * drawing in the nano8 style */
static int l_glyph(lua_State *L)
{
    uint8_t rows[5];
    int w = n8_glyph(ia(L, 1, 0), rows);
    lua_pushinteger(L, w);
    for (int i = 0; i < 5; i++)
        lua_pushinteger(L, rows[i]);
    return 6;
}

/* reset(): the draw state as at power on (palettes, clip, camera...) */
static int l_reset(lua_State *L)
{
    n8_reset_draw(mach(L));
    return 0;
}

/* text(s, x, y, rgb, [scale]) -> end x: text in the nano8 font, into the
 * console's frame (titles, labels of the menus) */
static int l_text(lua_State *L)
{
    size_t len;
    const unsigned char *s = (const unsigned char *)luaL_checklstring(L, 1, &len);
    g16_t *g = target(L);
    int x0 = ia(L, 2, 0), x = x0, y = ia(L, 3, 0), sc = ia(L, 5, 1);
    uint16_t c = g16_rgb24((uint32_t)luaL_optinteger(L, 4, 0xFFFFFF));
    if (sc < 1) sc = 1;
    if (sc > 16) sc = 16;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            x = x0;
            y += 6 * sc;
            continue;
        }
        uint8_t rows[5];
        int w = n8_glyph(s[i], rows);
        for (int j = 0; j < 5; j++)
            for (int k = 0; k < w; k++)
                if (rows[j] >> k & 1)
                    g16_rectfill(g, x + k * sc, y + j * sc, sc, sc, c);
        x += (s[i] >= 128 ? 8 : 4) * sc;
    }
    lua_pushinteger(L, x);
    return 1;
}

/* traceback(msg): the message with the stack of the code that failed (an
 * xpcall handler) */
static int l_traceback(lua_State *L)
{
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

static const luaL_Reg lib[] = {
    /* the carts' own */
    { "cls", l_cls }, { "pset", l_pset }, { "pget", l_pget }, { "sget", l_sget }, { "sset", l_sset },
    { "fget", l_fget }, { "fset", l_fset }, { "line", l_line }, { "rect", l_rect }, { "rectfill", l_rectfill },
    { "rrect", l_rrect }, { "rrectfill", l_rrectfill }, { "circ", l_circ }, { "circfill", l_circfill },
    { "oval", l_oval }, { "ovalfill", l_ovalfill }, { "spr", l_spr }, { "sspr", l_sspr }, { "map", l_map },
    { "mget", l_mget }, { "mset", l_mset }, { "tline", l_tline }, { "print", l_print }, { "cursor", l_cursor },
    { "color", l_color }, { "camera", l_camera }, { "clip", l_clip }, { "pal", l_pal }, { "palt", l_palt },
    { "fillp", l_fillp }, { "peek", l_peek }, { "poke", l_poke }, { "peek2", l_peek2 }, { "poke2", l_poke2 },
    { "peek4", l_peek4 }, { "poke4", l_poke4 }, { "memcpy", l_memcpy }, { "memset", l_memset },
    { "reload", l_reload }, { "cstore", l_cstore }, { "flr", l_flr }, { "ceil", l_ceil }, { "abs", l_abs },
    { "sgn", l_sgn }, { "min", l_min }, { "max", l_max }, { "mid", l_mid }, { "sqrt", l_sqrt }, { "sin", l_sin },
    { "cos", l_cos }, { "atan2", l_atan2 }, { "rnd", l_rnd }, { "srand", l_srand }, { "band", l_band },
    { "bor", l_bor }, { "bxor", l_bxor }, { "bnot", l_bnot }, { "shl", l_shl }, { "shr", l_shr },
    { "lshr", l_lshr }, { "rotl", l_rotl }, { "rotr", l_rotr }, { "tostr", l_tostr }, { "tonum", l_tonum },
    { "chr", l_chr }, { "ord", l_ord }, { "sub", l_sub }, { "split", l_split }, { "add", l_add },
    { "del", l_del }, { "deli", l_deli }, { "count", l_count }, { "btn", l_btn }, { "btnp", l_btnp },
    { "sfx", l_sfx }, { "music", l_music }, { "_cat", l_cat },
    /* the console's */
    { "blit", l_blit }, { "load", l_load }, { "preview", l_preview }, { "drawlabel", l_drawlabel },
    { "snapshot", l_snapshot }, { "power", l_power }, { "setfps", l_setfps }, { "buttons", l_buttons },
    { "compile", l_compile }, { "sndstat", l_sndstat }, { "pause", l_pause }, { "peekstr", l_peekstr },
    { "pokestr", l_pokestr }, { "glyph", l_glyph }, { "reset", l_reset }, { "text", l_text },
    { "traceback", l_traceback }, { "present", l_present },
    { NULL, NULL },
};

int luaopen_n8(lua_State *L)
{
    luaL_newlib(L, lib);
    return 1;
}
