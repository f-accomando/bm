/*
 * The dev kit's profiler of functions (R14): see profile.h. It walks the
 * stack of Lua's CallInfo directly (a function's key is its Proto, or the
 * C function's address): no lua_getinfo but for the first sighting, where
 * the name comes from.
 */
#include <stdlib.h>
#include <string.h>
#include "profile.h"
#include "lib/printf.h"

#include "lstate.h"
#include "lobject.h"
#include "ldebug.h"

/* in Lua's ldo.c: called around every C function while not NULL */
extern void (*luai_cprof)(lua_State *L, int leave);

#define ROWS    256
#define SLOTS   512             /* the hash of the rows (power of 2) */
#define DEPTH   48              /* the stack levels credited */
#define WINDOW  60              /* frames in a window */
#define KEEP    32              /* rows kept of the last window */

typedef struct {
    uintptr_t key;              /* Proto *, or the lua_CFunction */
    char name[24], where[24];
    uint8_t c;
    uint32_t self_us, total_us, calls;
} row_t;

static row_t rows[ROWS];
static uint16_t slot[SLOTS];    /* row + 1, 0 empty */
static int nrows;
static int on;
static uint32_t (*now_us)(void);
static uint32_t mark;           /* the time credited up to */
static uint32_t frames;         /* in the window */
static const char *cb_name;     /* the callback running (prof_begin) */

/* the stack of the last event: where the time after it goes at prof_end */
static int last_self = -1, last[DEPTH], nlast;

static prof_row_t kept[KEEP];
static int nkept;
static uint32_t kept_frames;

static unsigned hash(uintptr_t k)
{
    return (unsigned)(((uint64_t)(k >> 2) * 2654435761u) >> 7) & (SLOTS - 1);
}

/* the row of ci (level from L->ci, for lua_getinfo), made at first sight;
 * -1 if none (no function, or the rows are full) */
static int row_of(lua_State *L, CallInfo *ci, int level)
{
    const TValue *f = s2v(ci->func.p);
    uintptr_t key;
    int c = 0;
    if (isLua(ci)) {
        key = (uintptr_t)ci_func(ci)->p;
    } else if (ttislcf(f)) {
        key = (uintptr_t)fvalue(f);
        c = 1;
    } else if (ttisCclosure(f)) {
        key = (uintptr_t)clCvalue(f)->f;
        c = 1;
    } else {
        return -1;
    }
    unsigned h = hash(key);
    for (int i = 0; i < SLOTS; i++, h = (h + 1) & (SLOTS - 1)) {
        if (!slot[h])
            break;
        if (rows[slot[h] - 1].key == key)
            return slot[h] - 1;
    }
    if (nrows >= ROWS || slot[h])
        return -1;
    row_t *r = &rows[nrows];
    memset(r, 0, sizeof *r);
    r->key = key;
    r->c = (uint8_t)c;
    lua_Debug ar;
    const char *name = NULL;
    if (lua_getstack(L, level, &ar) && lua_getinfo(L, "Sn", &ar)) {
        if (c)
            ksnprintf(r->where, sizeof r->where, "[C]");
        else
            ksnprintf(r->where, sizeof r->where, "%s:%d", ar.short_src, ar.linedefined);
        if (ar.name)
            name = ar.name;
        else if (ar.what && !strcmp(ar.what, "main"))
            name = "(main chunk)";
        else if (!c && cb_name && ci->previous == &L->base_ci && L == G(L)->mainthread)
            name = cb_name;             /* _update, _draw: called by the runtime, not by Lua */
    }
    ksnprintf(r->name, sizeof r->name, "%s", name ? name : r->where[0] ? r->where : "?");
    slot[h] = (uint16_t)(++nrows);
    return nrows - 1;
}

/* dt to the stack from ci down: its own time to the first function, the
 * total to each one (once); the stack kept for prof_end. Returns the first
 * row. */
static int credit(lua_State *L, CallInfo *ci, int level, uint32_t dt)
{
    int first = -1, n = 0;
    for (; ci && ci != &L->base_ci && n < DEPTH; ci = ci->previous, level++) {
        int r = row_of(L, ci, level);
        if (r < 0)
            continue;
        if (first < 0) {
            first = r;
            rows[r].self_us += dt;
        }
        int dup = 0;
        for (int i = 0; i < n && !dup; i++)
            dup = last[i] == r;
        if (!dup) {
            last[n++] = r;
            rows[r].total_us += dt;
        }
    }
    last_self = first;
    nlast = n;
    return first;
}

static uint32_t take(void)
{
    const uint32_t t = now_us(), dt = t - mark;
    mark = t;
    return dt;
}

/* around a C function (ldo.c): before it, the time so far is its caller's;
 * after it, the time since the last event is its own */
static void cprof(lua_State *L, int leave)
{
    if (!leave) {
        if (L->ci->previous)
            credit(L, L->ci->previous, 1, take());
        return;
    }
    const int r = credit(L, L->ci, 0, take());
    if (r >= 0)
        rows[r].calls++;
    if (nlast > 0 && last[0] == r) {    /* it returns: the time after it is its caller's */
        memmove(last, last + 1, (size_t)--nlast * sizeof last[0]);
        last_self = nlast ? last[0] : -1;
    }
}

void prof_enable(int want, uint32_t (*clock)(void))
{
    if (want && clock)
        now_us = clock;
    want = want && now_us;
    if (want == on)
        return;
    on = want;
    nrows = 0;
    memset(slot, 0, sizeof slot);
    frames = 0;
    nkept = 0;
    kept_frames = 0;
    last_self = -1;
    nlast = 0;
    luai_cprof = on ? cprof : NULL;
    if (on)
        mark = now_us();
}

int prof_enabled(void) { return on; }

void prof_begin(const char *name)
{
    cb_name = name;
    if (!on)
        return;
    mark = now_us();
    last_self = -1;
    nlast = 0;
}

void prof_end(void)
{
    if (!on)
        return;
    const uint32_t dt = take();
    if (last_self >= 0)
        rows[last_self].self_us += dt;
    for (int i = 0; i < nlast; i++)
        rows[last[i]].total_us += dt;
    last_self = -1;
    nlast = 0;
}

void prof_sample(lua_State *L)
{
    if (on)
        credit(L, L->ci, 0, take());
}

static int by_self(const void *a, const void *b)
{
    const row_t *x = &rows[*(const uint16_t *)a], *y = &rows[*(const uint16_t *)b];
    if (x->self_us != y->self_us)
        return x->self_us < y->self_us ? 1 : -1;
    return x->total_us < y->total_us ? 1 : x->total_us > y->total_us ? -1 : 0;
}

void prof_frame(void)
{
    if (!on || ++frames < WINDOW)
        return;
    static uint16_t idx[ROWS];
    int n = 0;
    for (int i = 0; i < nrows; i++)
        if (rows[i].self_us || rows[i].total_us)
            idx[n++] = (uint16_t)i;
    qsort(idx, (size_t)n, sizeof idx[0], by_self);
    nkept = n < KEEP ? n : KEEP;
    for (int i = 0; i < nkept; i++) {
        const row_t *r = &rows[idx[i]];
        prof_row_t *k = &kept[i];
        memcpy(k->name, r->name, sizeof k->name);
        memcpy(k->where, r->where, sizeof k->where);
        k->c = r->c;
        k->self_us = r->self_us / frames;
        k->total_us = r->total_us / frames;
        k->calls = (r->calls + frames / 2) / frames;
    }
    kept_frames = frames;
    for (int i = 0; i < nrows; i++)
        rows[i].self_us = rows[i].total_us = rows[i].calls = 0;
    frames = 0;
}

int prof_rows(prof_row_t *out, int n, uint32_t *nframes)
{
    if (n > nkept)
        n = nkept;
    if (n > 0)
        memcpy(out, kept, (size_t)n * sizeof *out);
    if (nframes)
        *nframes = kept_frames;
    return n;
}
