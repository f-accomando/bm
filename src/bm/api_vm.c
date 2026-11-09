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
 */
#include <stdlib.h>
#include <string.h>

#include "bmvm.h"
#include "lauxlib.h"
#include "lua.h"

#define MOD_MT "bmvm.mod"
#define BUF_MT "bmvm.buf"
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
