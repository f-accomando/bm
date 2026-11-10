/* The bmvm machine for Lua (EXPERIMENT, claude/bm-vm-test): a cartridge carries typed hot functions as data (a blob made by
 * scripts/bmtvm.py) and the kernel runs them. No native code goes into the cartridge.
 *
 *   m, why = vm_load(blob)          a module, or nil and the reason
 *   f = vm_fn(m, "name")            the number of a function, or nil
 *   b = vm_buf("i32" or "f64", n)   an array that functions can read and write (n elements, all 0)
 *   vm_buf_fill(b, table [, at])    table[1], table[2] ... into b from element at (1 if missing); numbers, true and false
 *   vm_bind(m, f, buffers...)       binds the array parameters of f, in order, once; vm_run then takes only the numbers
 *   r = vm_run(m, f, args...)       a number for a number parameter, a buffer for an array parameter (none if f is bound);
 *                                   gives the result
 *   vm_buf_get(b, i)                element i of b
 *
 * Native ARMv6 A32 compiler and loader (Stages 5-9):
 *   blob, why = native_compile(src) compiles .b source into a BMN1 ARMv6 A32 module in RAM, or nil and the error
 *   m, why = native_load(blob)      loads a BMN1 module or raw ARMv6 words into executable RAM (cleans D-cache, invalidates I-cache)
 *   f = native_fn(m, "name")        the number of a function, or nil
 *   r = native_run(m, f, args...)   calls function f (number or "name") with i32/bool/f32/array/struct arguments and returns its result
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmlc.h"
#include "bmnative.h"
#include "bmvm.h"
#include "lauxlib.h"
#include "lua.h"
#include "rt_internal.h"

#define MOD_MT    "bmvm.mod"
#define BUF_MT    "bmvm.buf"
#define NAT_MT    "bmnative.mod"
#define VM_BUDGET 20000000u

typedef struct {
    bmvm_mod *m;
    struct { int na; bmvm_arr a[16]; } *bnd;     /* the arrays bound to each function (vm_bind), or NULL */
} vmmod_t;
typedef struct { bmvm_arr a; } vmbuf_t;

static int mod_gc(lua_State *L)
{
    vmmod_t *u = (vmmod_t *)luaL_checkudata(L, 1, MOD_MT);
    bmvm_free(u->m);
    u->m = NULL;
    free(u->bnd);
    u->bnd = NULL;
    return 0;
}

static int buf_gc(lua_State *L)
{
    vmbuf_t *u = (vmbuf_t *)luaL_checkudata(L, 1, BUF_MT);
    free(u->a.data);
    u->a.data = NULL;
    u->a.n = 0;
    return 0;
}

int l_vm_load(lua_State *L)
{
    size_t n;
    const char *s = luaL_checklstring(L, 1, &n);
    bmvm_mod *m;
    const char *why = "";
    if (bmvm_load(&m, (const uint8_t *)s, n, &why)) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    vmmod_t *u = (vmmod_t *)lua_newuserdatauv(L, sizeof *u, 1);
    u->m = m;
    u->bnd = NULL;
    lua_newtable(L);                    /* the buffers that are bound: kept alive here */
    lua_setiuservalue(L, -2, 1);
    if (luaL_newmetatable(L, MOD_MT)) {
        lua_pushcfunction(L, mod_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int l_vm_fn(lua_State *L)
{
    vmmod_t *u = (vmmod_t *)luaL_checkudata(L, 1, MOD_MT);
    const int f = u->m ? bmvm_find(u->m, luaL_checkstring(L, 2)) : -1;
    if (f < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, f);
    return 1;
}

int l_vm_buf(lua_State *L)
{
    const char *kind = luaL_checkstring(L, 1);
    const lua_Integer n = luaL_checkinteger(L, 2);
    if (n < 1 || n > (1 << 22) || (strcmp(kind, "i32") && strcmp(kind, "f64")))
        return luaL_error(L, "vm_buf: kind i32 or f64, 1 to 4194304 elements");
    const int is_f = kind[0] == 'f';
    void *data = calloc((size_t)n, is_f ? 8 : 4);
    if (!data)
        return luaL_error(L, "vm_buf: out of memory");
    vmbuf_t *u = (vmbuf_t *)lua_newuserdatauv(L, sizeof *u, 0);
    u->a.data = data;
    u->a.n = (int32_t)n;
    u->a.is_f64 = is_f;
    if (luaL_newmetatable(L, BUF_MT)) {
        lua_pushcfunction(L, buf_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int l_vm_buf_fill(lua_State *L)
{
    vmbuf_t *u = (vmbuf_t *)luaL_checkudata(L, 1, BUF_MT);
    luaL_checktype(L, 2, LUA_TTABLE);
    const lua_Integer at = luaL_optinteger(L, 3, 1);
    const lua_Integer len = (lua_Integer)lua_rawlen(L, 2);
    for (lua_Integer i = 1; i <= len; i++) {
        const lua_Integer e = at + i - 1;
        if (e < 1 || e > u->a.n)
            return luaL_error(L, "vm_buf_fill: past the end");
        lua_rawgeti(L, 2, i);
        if (u->a.is_f64)
            ((double *)u->a.data)[e - 1] = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : lua_tonumber(L, -1);
        else
            ((int32_t *)u->a.data)[e - 1] = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : (int32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return 0;
}

int l_vm_buf_get(lua_State *L)
{
    vmbuf_t *u = (vmbuf_t *)luaL_checkudata(L, 1, BUF_MT);
    const lua_Integer i = luaL_checkinteger(L, 2);
    if (i < 1 || i > u->a.n)
        return luaL_error(L, "vm_buf_get: out of range");
    if (u->a.is_f64)
        lua_pushnumber(L, ((double *)u->a.data)[i - 1]);
    else
        lua_pushinteger(L, ((int32_t *)u->a.data)[i - 1]);
    return 1;
}

int l_vm_bind(lua_State *L)
{
    vmmod_t *u = (vmmod_t *)luaL_checkudata(L, 1, MOD_MT);
    const int fn = (int)luaL_checkinteger(L, 2);
    if (!u->m || fn < 0 || fn >= u->m->nfn)
        return luaL_error(L, "vm_bind: no such function");
    const bmvm_fn *f = &u->m->fn[fn];
    int want = 0;
    for (int i = 0; i < f->nparams; i++)
        if (f->ptype[i] >= BMVM_ARR_I32)
            want++;
    if (lua_gettop(L) - 2 != want)
        return luaL_error(L, "vm_bind: %s has %d array parameters", f->name, want);
    if (!u->bnd) {
        u->bnd = calloc((size_t)u->m->nfn, sizeof *u->bnd);
        if (!u->bnd)
            return luaL_error(L, "vm_bind: out of memory");
    }
    lua_getiuservalue(L, 1, 1);
    int k = 0;
    for (int i = 0; i < f->nparams; i++) {
        if (f->ptype[i] < BMVM_ARR_I32)
            continue;
        vmbuf_t *b = (vmbuf_t *)luaL_checkudata(L, 3 + k, BUF_MT);
        if (b->a.is_f64 != (f->ptype[i] == BMVM_ARR_F64))
            return luaL_error(L, "vm_bind: array %d is of the wrong kind", k + 1);
        u->bnd[fn].a[k] = b->a;
        lua_pushvalue(L, 3 + k);
        lua_rawseti(L, -2, fn * 16 + k + 1);
        k++;
    }
    u->bnd[fn].na = k;
    return 0;
}

int l_vm_run(lua_State *L)
{
    vmmod_t *u = (vmmod_t *)luaL_checkudata(L, 1, MOD_MT);
    const int fn = (int)luaL_checkinteger(L, 2);
    if (!u->m || fn < 0 || fn >= u->m->nfn)
        return luaL_error(L, "vm_run: no such function");
    const bmvm_fn *f = &u->m->fn[fn];
    const int n = f->nparams;
    const int bound = u->bnd && u->bnd[fn].na > 0;
    int scalars = 0;
    for (int i = 0; i < n; i++)
        scalars += f->ptype[i] < BMVM_ARR_I32;
    if (lua_gettop(L) - 2 != (bound ? scalars : n))
        return luaL_error(L, "vm_run: %s takes %d arguments", f->name, bound ? scalars : n);
    bmvm_val args[16];
    bmvm_arr arrs[16];
    int na = 0, li = 3;
    for (int i = 0; i < n; i++) {
        const int pt = f->ptype[i];
        if (pt == BMVM_I32) {
            args[i].i = lua_isboolean(L, li) ? lua_toboolean(L, li) : (int32_t)luaL_checkinteger(L, li);
            li++;
        } else if (pt == BMVM_F64) {
            args[i].d = lua_tonumber(L, li);
            li++;
        } else if (bound) {
            arrs[na] = u->bnd[fn].a[na];
            args[i].i = na++;
        } else {
            vmbuf_t *b = (vmbuf_t *)luaL_checkudata(L, li++, BUF_MT);
            arrs[na] = b->a;
            args[i].i = na++;
        }
    }
    bmvm_val ret;
    ret.i = 0;
    const int e = bmvm_call(u->m, fn, args, n, arrs, na, &ret, VM_BUDGET);
    if (e)
        return luaL_error(L, "vm_run: %s stopped (%d)", f->name, e);
    if (f->ret == BMVM_NONE)
        return 0;
    if (f->ret == BMVM_F64)
        lua_pushnumber(L, ret.d);
    else
        lua_pushinteger(L, ret.i);
    return 1;
}

typedef struct { bmnative_mod *m; } natmod_t;

static int nat_gc(lua_State *L)
{
    natmod_t *u = (natmod_t *)luaL_checkudata(L, 1, NAT_MT);
    bmnative_free(u->m);
    u->m = NULL;
    return 0;
}

int bmnative_lua_push_mod(lua_State *L, void *mod)
{
    natmod_t *u = (natmod_t *)lua_newuserdatauv(L, sizeof *u, 0);
    u->m = (bmnative_mod *)mod;
    if (luaL_newmetatable(L, NAT_MT)) {
        lua_pushcfunction(L, nat_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int l_native_load(lua_State *L)
{
    size_t n;
    const char *s = luaL_checklstring(L, 1, &n);
    bmnative_mod *m;
    const char *why = "";
    if (bmnative_load(&m, (const uint8_t *)s, n, &why)) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    return bmnative_lua_push_mod(L, m);
}

int l_native_fn(lua_State *L)
{
    natmod_t *u = (natmod_t *)luaL_checkudata(L, 1, NAT_MT);
    const int f = u->m ? bmnative_find(u->m, luaL_checkstring(L, 2)) : -1;
    if (f < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, f);
    return 1;
}

static uint32_t lua_val_to_word(lua_State *L, int idx, uint8_t kind)
{
    if (kind == BMN_F32) {
        float fv = (float)luaL_checknumber(L, idx);
        uint32_t u;
        memcpy(&u, &fv, 4);
        return u;
    }
    if (kind == BMN_BOOL || lua_isboolean(L, idx))
        return (uint32_t)(lua_toboolean(L, idx) ? 1 : 0);
    return (uint32_t)(int32_t)luaL_checkinteger(L, idx);
}

static void push_word_to_lua(lua_State *L, uint32_t w, uint8_t kind)
{
    if (kind == BMN_F32) {
        float fv;
        memcpy(&fv, &w, 4);
        lua_pushnumber(L, (lua_Number)fv);
    } else if (kind == BMN_BOOL) {
        lua_pushboolean(L, w != 0);
    } else {
        lua_pushinteger(L, (int32_t)w);
    }
}

static lua_State *g_active_lua = NULL;

lua_State *bmnative_lua_set_state(lua_State *L)
{
    lua_State *prev = g_active_lua;
    g_active_lua = L;
    return prev;
}

int32_t bmnative_lua_host_dispatch(int32_t host_id, const int32_t *args, int32_t nargs)
{
    lua_State *L = g_active_lua;
    if (!L)
        return 0;
    static const char *const names[] = {
        "cls", "pix", "rectfill", "spr", "btn",
        "btnp", "line", "circ", "circfill", "rect",
        "sfx", "music", "time", "print",
        "sin", "cos", "atan2"
    };
    if (host_id < 0 || host_id >= (int)(sizeof names / sizeof names[0]))
        return 0;
    lua_getglobal(L, names[host_id]);
    if (!lua_isfunction(L, -1)) {
        if (host_id == BMN_HOST_PIX) {
            lua_pop(L, 1);
            lua_getglobal(L, "pset");
        } else if (host_id == BMN_HOST_SIN || host_id == BMN_HOST_COS || host_id == BMN_HOST_ATAN2) {
            lua_pop(L, 1);
            lua_getglobal(L, "math");
            if (lua_istable(L, -1)) {
                if (host_id == BMN_HOST_ATAN2) {
                    lua_getfield(L, -1, "atan2");
                    if (!lua_isfunction(L, -1)) {
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "atan");
                    }
                } else {
                    lua_getfield(L, -1, names[host_id]);
                }
                lua_remove(L, -2);
            }
        }
    }
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    if (host_id == BMN_HOST_SIN || host_id == BMN_HOST_COS || host_id == BMN_HOST_ATAN2) {
        for (int32_t i = 0; i < nargs; i++) {
            float farg = 0.0f;
            memcpy(&farg, &args[i], 4);
            lua_pushnumber(L, (lua_Number)farg);
        }
        if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return 0;
        }
        float val = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        uint32_t bits = 0;
        memcpy(&bits, &val, 4);
        return (int32_t)bits;
    }
    if (host_id == BMN_HOST_PRINT) {
        if (nargs > 0)
            lua_pushstring(L, bmnative_get_string(args[0]));
        for (int32_t i = 1; i < nargs; i++)
            lua_pushinteger(L, args[i]);
        if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return 0;
        }
        int32_t res = (int32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
        return res;
    }
    for (int32_t i = 0; i < nargs; i++)
        lua_pushinteger(L, args[i]);
    if (host_id == BMN_HOST_BTN || host_id == BMN_HOST_BTNP) {
        if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return 0;
        }
        int32_t res = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
        return res;
    }
    if (host_id == BMN_HOST_TIME) {
        if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return 0;
        }
        float val = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        uint32_t bits = 0;
        memcpy(&bits, &val, 4);
        return (int32_t)bits;
    }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK)
        lua_pop(L, 1);
    return 0;
}

int l_native_run(lua_State *L)
{
    natmod_t *u = (natmod_t *)luaL_checkudata(L, 1, NAT_MT);
    if (!u->m)
        return luaL_error(L, "native_run: invalid module");

    int fn = -1;
    if (lua_isinteger(L, 2))
        fn = (int)lua_tointeger(L, 2);
    else if (lua_isstring(L, 2))
        fn = bmnative_find(u->m, lua_tostring(L, 2));

    if (fn < 0 || fn >= u->m->nfn)
        return luaL_error(L, "native_run: no such function");

    const bmnative_fn *f = &u->m->fn[fn];
    const int nargs = lua_gettop(L) - 2;
    if (f->nparams == BMN_VARARGS) {
        if (nargs < 0 || nargs > BMN_MAX_ARGS)
            return luaL_error(L, "native_run: takes at most %d arguments", BMN_MAX_ARGS);
    } else if (nargs != f->nparams) {
        return luaL_error(L, "native_run: %s takes %d arguments", f->name, f->nparams);
    }

    int32_t args[BMN_MAX_ARGS];
    bmnative_buf bufs[BMN_MAX_ARGS];
    uint32_t *temp_allocs[BMN_MAX_ARGS];
    vmbuf_t *f64_vmbufs[BMN_MAX_ARGS];
    memset(args, 0, sizeof args);
    memset(bufs, 0, sizeof bufs);
    memset(temp_allocs, 0, sizeof temp_allocs);
    memset(f64_vmbufs, 0, sizeof f64_vmbufs);

    for (int i = 0; i < nargs; i++) {
        const int li = 3 + i;
        const uint8_t pt = (f->nparams == BMN_VARARGS) ? BMN_I32 : f->ptype[i];
        if (pt >= BMN_ARR_I32 && pt <= BMN_ARR_F32) {
            const uint8_t elem_kind = (pt == BMN_ARR_F32) ? BMN_F32 : ((pt == BMN_ARR_BOOL) ? BMN_BOOL : BMN_I32);
            vmbuf_t *vb = (vmbuf_t *)luaL_testudata(L, li, BUF_MT);
            if (vb) {
                if (!vb->a.is_f64 && elem_kind != BMN_F32) {
                    bufs[i].data = (uint32_t *)vb->a.data;
                    bufs[i].nwords = (uint32_t)vb->a.n;
                } else if (vb->a.is_f64 && elem_kind == BMN_F32) {
                    uint32_t nw = (uint32_t)vb->a.n;
                    uint32_t *tmp = calloc(nw, sizeof(uint32_t));
                    if (!tmp) {
                        for (int k = 0; k < i; k++) free(temp_allocs[k]);
                        return luaL_error(L, "native_run: out of memory");
                    }
                    const double *src = (const double *)vb->a.data;
                    for (uint32_t j = 0; j < nw; j++) {
                        float fv = (float)src[j];
                        memcpy(&tmp[j], &fv, 4);
                    }
                    temp_allocs[i] = tmp;
                    f64_vmbufs[i] = vb;
                    bufs[i].data = tmp;
                    bufs[i].nwords = nw;
                } else {
                    for (int k = 0; k < i; k++) free(temp_allocs[k]);
                    return luaL_error(L, "native_run: vm_buf kind mismatch at arg %d", i + 1);
                }
            } else if (lua_istable(L, li)) {
                uint32_t nw = (uint32_t)lua_rawlen(L, li);
                uint32_t *tmp = calloc(nw ? nw : 1u, sizeof(uint32_t));
                if (!tmp) {
                    for (int k = 0; k < i; k++) free(temp_allocs[k]);
                    return luaL_error(L, "native_run: out of memory");
                }
                for (uint32_t j = 0; j < nw; j++) {
                    lua_rawgeti(L, li, (lua_Integer)(j + 1));
                    tmp[j] = lua_val_to_word(L, -1, elem_kind);
                    lua_pop(L, 1);
                }
                temp_allocs[i] = tmp;
                bufs[i].data = tmp;
                bufs[i].nwords = nw;
            } else {
                for (int k = 0; k < i; k++) free(temp_allocs[k]);
                return luaL_error(L, "native_run: expected table or vm_buf at arg %d", i + 1);
            }
        } else if (pt & BMN_STRUCT_BASE) {
            const uint32_t s_idx = pt & 0x7Fu;
            const bmnative_struct *st = &u->m->structs[s_idx];
            vmbuf_t *vb = (vmbuf_t *)luaL_testudata(L, li, BUF_MT);
            if (vb && !vb->a.is_f64 && (uint32_t)vb->a.n >= st->nfields) {
                bufs[i].data = (uint32_t *)vb->a.data;
                bufs[i].nwords = st->nfields;
            } else if (lua_istable(L, li)) {
                uint32_t *tmp = calloc(st->nfields, sizeof(uint32_t));
                if (!tmp) {
                    for (int k = 0; k < i; k++) free(temp_allocs[k]);
                    return luaL_error(L, "native_run: out of memory");
                }
                for (uint32_t k = 0; k < st->nfields; k++) {
                    lua_getfield(L, li, st->fname[k]);
                    if (lua_isnil(L, -1)) {
                        lua_pop(L, 1);
                        lua_rawgeti(L, li, (lua_Integer)(k + 1));
                    }
                    tmp[k] = lua_val_to_word(L, -1, st->ftype[k]);
                    lua_pop(L, 1);
                }
                temp_allocs[i] = tmp;
                bufs[i].data = tmp;
                bufs[i].nwords = st->nfields;
            } else {
                for (int k = 0; k < i; k++) free(temp_allocs[k]);
                return luaL_error(L, "native_run: expected struct table at arg %d", i + 1);
            }
        } else {
            args[i] = (int32_t)lua_val_to_word(L, li, pt);
        }
    }

    int32_t ret = 0;
    lua_State *prev_lua = g_active_lua;
    bmnative_host_fn prev_host = bmnative_get_host_fn();
    g_active_lua = L;
    if (!prev_host)
        bmnative_set_host_fn(bmnative_lua_host_dispatch);
    const int e = bmnative_call_buf(u->m, fn, args, bufs, nargs, &ret);
    if (!prev_host)
        bmnative_set_host_fn(NULL);
    g_active_lua = prev_lua;

    /* Write back modified array/struct elements to Lua tables or f64 vm_bufs. */
    if (e == BMN_OK) {
        for (int i = 0; i < nargs; i++) {
            if (!temp_allocs[i])
                continue;
            const int li = 3 + i;
            const uint8_t pt = f->ptype[i];
            if (f64_vmbufs[i]) {
                double *dst = (double *)f64_vmbufs[i]->a.data;
                for (uint32_t j = 0; j < bufs[i].nwords; j++) {
                    float fv;
                    memcpy(&fv, &temp_allocs[i][j], 4);
                    dst[j] = (double)fv;
                }
            } else if (pt >= BMN_ARR_I32 && pt <= BMN_ARR_F32) {
                const uint8_t elem_kind = (pt == BMN_ARR_F32) ? BMN_F32 : ((pt == BMN_ARR_BOOL) ? BMN_BOOL : BMN_I32);
                for (uint32_t j = 0; j < bufs[i].nwords; j++) {
                    push_word_to_lua(L, temp_allocs[i][j], elem_kind);
                    lua_rawseti(L, li, (lua_Integer)(j + 1));
                }
            } else if (pt & BMN_STRUCT_BASE) {
                const bmnative_struct *st = &u->m->structs[pt & 0x7Fu];
                for (uint32_t k = 0; k < st->nfields; k++) {
                    push_word_to_lua(L, temp_allocs[i][k], st->ftype[k]);
                    lua_setfield(L, li, st->fname[k]);
                }
            }
        }
    }

    for (int i = 0; i < nargs; i++)
        free(temp_allocs[i]);

    if (e)
        return luaL_error(L, "native_run: %s stopped (%d)", f->name, e);

    if (f->ret == BMN_NONE)
        return 0;
    push_word_to_lua(L, (uint32_t)ret, f->ret);
    return 1;
}

int l_native_compile(lua_State *L)
{
    size_t n = 0;
    const char *src = luaL_checklstring(L, 1, &n);
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    char err[256];
    if (bml_compile_native(src, n, &blob, &blob_len, err, sizeof err) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushlstring(L, (const char *)blob, blob_len);
    free(blob);
    return 1;
}

int l_native_disasm(lua_State *L)
{
    size_t n = 0;
    const char *s = luaL_checklstring(L, 1, &n);
    bmnative_mod *m = NULL;
    const char *why = "";
    if (bmnative_load(&m, (const uint8_t *)s, n, &why) != BMN_OK) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }

    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)m->ncode);
    lua_setfield(L, -2, "ncode");
    lua_pushinteger(L, m->nfn);
    lua_setfield(L, -2, "nfn");
    lua_pushinteger(L, (lua_Integer)m->nrodata);
    lua_setfield(L, -2, "nrodata");

    lua_createtable(L, m->nfn, 0);
    for (int f = 0; f < m->nfn; f++) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, m->fn[f].name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, m->fn[f].nparams);
        lua_setfield(L, -2, "nparams");
        lua_pushinteger(L, m->fn[f].ret);
        lua_setfield(L, -2, "ret");
        lua_pushinteger(L, (lua_Integer)m->fn[f].word_off);
        lua_setfield(L, -2, "word_off");
        lua_rawseti(L, -2, f + 1);
    }
    lua_setfield(L, -2, "functions");

    lua_newtable(L);
    int ln = 1;
    for (uint32_t i = 0; i < m->ncode; i++) {
        char buf[128];
        for (int f = 0; f < m->nfn; f++) {
            if (m->fn[f].word_off == i) {
                snprintf(buf, sizeof buf, "; function %s(%d params) -> %d @ 0x%04X",
                         m->fn[f].name, m->fn[f].nparams, m->fn[f].ret, (unsigned)(i * 4u));
                lua_pushstring(L, buf);
                lua_rawseti(L, -2, ln++);
            }
        }
        char dstr[64];
        bmnative_disasm_word(m->code[i], dstr, sizeof dstr);
        snprintf(buf, sizeof buf, "  0x%04X: %08X  %s", (unsigned)(i * 4u), (unsigned)m->code[i], dstr);
        lua_pushstring(L, buf);
        lua_rawseti(L, -2, ln++);
    }
    lua_setfield(L, -2, "lines");

    bmnative_free(m);
    return 1;
}
