/*
 * .bm runtime: sandboxed Lua 5.4 state + drawing API in C (gfx16) + frame
 * loop. Lua only runs game logic; every pixel is drawn by C.
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

/* The state of the run (declared in rt_internal.h). */
rt_t rt;

/* The records of the 2D drawn after the GPU, and the held frame. */
d2_t d2 = { .cut = D2_NONE };
held_t held;

/* ---------------------------------------------------------------- API */

/* the 2D records w[0..upto) on the page of rt.g (its job has ended) */
static void d2_play(const int32_t *w, uint32_t upto)
{
    const g16_t live = rt.g;
    for (uint32_t i = 0; i < upto;) {
        const int32_t *a = &w[i + 1];
        const int op = w[i] & 255;
        i += 1 + ((uint32_t)w[i] >> 8);
        switch (op) {
        case D2_STATE:
            rt.g.cx0 = a[0]; rt.g.cy0 = a[1]; rt.g.cx1 = a[2]; rt.g.cy1 = a[3];
            rt.g.cam_x = a[4]; rt.g.cam_y = a[5];
            memcpy(&rt.g.font, a + 6, sizeof rt.g.font);
            break;
        case D2_CLS: g16_cls(&rt.g, (uint16_t)a[0]); break;
        case D2_PSET: g16_pset(&rt.g, a[0], a[1], (uint16_t)a[2]); break;
        case D2_LINE: g16_line(&rt.g, a[0], a[1], a[2], a[3], (uint16_t)a[4]); break;
        case D2_RECT: g16_rect(&rt.g, a[0], a[1], a[2], a[3], (uint16_t)a[4]); break;
        case D2_RECTFILL: g16_rectfill(&rt.g, a[0], a[1], a[2], a[3], (uint16_t)a[4]); break;
        case D2_CIRC: g16_circ(&rt.g, a[0], a[1], a[2], (uint16_t)a[3]); break;
        case D2_CIRCFILL: g16_circfill(&rt.g, a[0], a[1], a[2], (uint16_t)a[3]); break;
        case D2_SPR: g16_spr(&rt.g, &rt.sheet, a[0], a[1], a[2], a[3], a[4], a[5], a[6]); break;
        case D2_SSPR:
            g16_sspr(&rt.g, &rt.sheet, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
            break;
        case D2_SSPR_ZOOM: {
            float zoom;
            memcpy(&zoom, a + 8, sizeof zoom);
            g16_sspr_zoom(&rt.g, &rt.sheet, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], zoom);
            break;
        }
        case D2_STACK: {
            float f[3];
            memcpy(f, a + 7, sizeof f);
            g16_stack(&rt.g, &rt.sheet, a[0], a[1], a[2], a[3], a[4], a[5], a[6], f[0], f[1], f[2]);
            break;
        }
        case D2_STACKV: {
            float f[3];
            memcpy(f, a + 7, sizeof f);
            g16_stackv(&rt.g, &rt.sheet, a[0], a[1], a[2], a[3], a[4], a[5], a[6], f[0], f[1], f[2]);
            break;
        }
        case D2_MAP: {
            g16_map_t m = { rt.map.w, rt.map.h, rt.layer[a[6]] };
            g16_map_mask(&rt.g, &rt.sheet, &m, a[0], a[1], a[2], a[3], a[4], a[5], rt.flags, (uint8_t)a[7]);
            break;
        }
        case D2_TEXT: g16_text_scaled(&rt.g, a[0], a[1], (const char *)(a + 4), (uint16_t)a[2], a[3]); break;
        case D2_PROMPT: {
            const prompt_t *pr;
            memcpy(&pr, a + 3, sizeof pr);
            draw_prompt(pr, a[0], a[1], a[2]);
            break;
        }
        case D2_TRI: g16_tri(&rt.g, a[0], a[1], a[2], a[3], a[4], a[5], (uint16_t)a[6]); break;
        case D2_TRI_GOURAUD:
            g16_tri_gouraud(&rt.g, a[0], a[1], a[2], a[3], a[4], a[5], (uint32_t)a[6], (uint32_t)a[7],
                            (uint32_t)a[8]);
            break;
        }
        d2.ops++;
    }
    rt.g.cx0 = live.cx0; rt.g.cy0 = live.cy0; rt.g.cx1 = live.cx1; rt.g.cy1 = live.cy1;
    rt.g.cam_x = live.cam_x; rt.g.cam_y = live.cam_y;
    rt.g.font = live.font;
}

/* the 2D recorded before word upto, on the page (the GPU's job has
 * ended); what follows stays recorded (the next frame's) */
static void d2_replay(uint32_t upto)
{
    d2_play(d2.w, upto);
    memmove(d2.w, d2.w + upto, (d2.n - upto) * sizeof *d2.w);
    d2.n -= upto;
    d2.on = d2.n > 0;
    if (d2.cut != D2_NONE)
        d2.cut = 0;
}

/* The 3D drawn by the GPU waits in a job until something else touches the
 * page: then the job goes first (2D drawn after the 3D lands on it, pget
 * reads the 3D). keep: in the middle of a frame, the depth stays for the
 * 3D drawn after; 0 at the end of the frame. The 2D recorded while the
 * job was drawing (bm3d 4.1) then goes on the page. */
void flush3d(int keep)
{
    /* (a frame started on the GPU, bm3d 4.1: waited for, the page has it) */
    if (rt.r3d.backend &&
        (((gpu3d_pending() || !keep) && gpu3d_flush(&rt.g, keep) != 0) || gpu3d_sync_page(rt.g.px) != 0 ||
         gpu3d_failed()))
        rt.r3d.backend = NULL;          /* the GPU failed: the ARM draws the 3D again */
    if (!d2.on)
        return;
    if (rt.early && d2.cut < d2.n) {
        /* the early _update needs the page while it holds 2D for the next
         * frame: drawn now (this frame shows it) */
        rt.early_touch = 1;
        d2.cut = D2_NONE;
    }
    d2_replay(d2.cut == D2_NONE || rt.early ? d2.n : d2.cut);
}

/* bm3d 6.9: the job of the frame held (later_hold) waited for, if it is still
 * drawing: only that one (its page is not the one being drawn) */
static void later_wait(void)
{
    if (held.on && (gpu3d_sync_page(held.px) != 0 || (rt.r3d.backend && gpu3d_failed())))
        rt.r3d.backend = NULL;          /* the GPU failed: the ARM draws the 3D again */
}

/* the 2D recorded for the frame held on its page, after its job: before it
 * is shown, and before anything that 2D reads changes (settle_reads: the
 * sheet, the map, the flags). Nothing recorded: no wait. */
void later_paint(void)
{
    if (!held.on || !held.n)
        return;
    later_wait();
    uint16_t *const px = rt.g.px;
    rt.g.px = held.px;
    d2_play(held.w, held.n);
    rt.g.px = px;
    held.n = 0;
}

/* before changing what recorded 2D reads (the map, the flags, the sheet):
 * the 2D recorded so far goes on its page first, the frame held's too */
void settle_reads(void)
{
    later_paint();
    if (d2.on)
        flush3d(1);
}

/* A cls() with the GPU drawing the 3D is not drawn by the ARM (at 1080p it
 * writes 4 MB): the GPU's next job clears its tiles to the colour. Before
 * anything else touches the page (2D, a read, the page shown, the ARM's 3D)
 * it is settled: if no job has cleared the page since, the ARM fills it. */
void cls_settle(void)
{
    if (!rt.cls_pending)
        return;
    rt.cls_pending = 0;
    if (rt.r3d.backend && gpu3d_cleared())
        return;
    g16_cls(&rt.g, rt.cls_colour);
}

/* bm3d 4.1: the end of the frame's 3D started on the GPU and not waited for
 * (gpu3d_queue on): the next _update runs meanwhile; flush3d waits */
static int submit3d(void)
{
    if (!rt.r3d.backend || !gpu3d_queue() || rt.no_early)
        return 0;
    if (gpu3d_submit(&rt.g, 0) != 0 || gpu3d_failed()) {
        rt.r3d.backend = NULL;
        return 0;
    }
    return 1;
}

/* before drawing on the page (or reading it): the 3D waiting goes first,
 * and the page is no longer one colour */
void sync3d(void)
{
    flush3d(1);
    cls_settle();
    if (rt.r3d.backend)
        gpu3d_page(0, 0);
}

/* before 3D, or before changing what recorded 2D reads (the map): the
 * recorded 2D goes on the page first */
void settle2d(void)
{
    if (rt.early)
        rt.early_touch = 1;             /* 3D in the early _update: it lands on this frame */
    if (d2.on)
        flush3d(1);
}

/* bm3d 4.1: the next frame's _update runs while the GPU draws this one: the 2D
 * it draws is recorded, for the next frame's page (begin 1); after the
 * page is shown, the records are that page's (begin 0) */
static void d2_early(int begin)
{
    if (begin) {
        d2.cut = d2.n;
        d2.font = NULL;                 /* the next frame's records start with their state */
    } else {
        d2.cut = D2_NONE;
    }
}

/* bm3d 4.8: the 2D over the 3D drawn by the GPU in the frame's job, where it
 * draws it as the ARM does (gpu3d_2d=1): while the job is open (3D waits
 * in it) and nothing is recorded for after it. Then no job ends for a HUD
 * and a frame of 3D, 2D, 3D, 2D is one job. 1 if the GPU took op (its
 * arguments v as the records keep them), else 0: the ARM draws it as
 * before (draw2d), after the job. */
static int gpu2d_open(void)
{
    return rt.gpu2d && rt.r3d.backend && !d2.on && !rt.early && gpu3d_pending();
}

/* sspr's source rectangle inside the sheet, as gfx16 cuts it */
static int sheet_rect(int *sx, int *sy, int *sw, int *sh)
{
    if (*sx < 0 || *sy < 0 || *sw <= 0 || *sh <= 0 || !rt.sheet.px)
        return 0;
    if (*sx + *sw > rt.sheet.w) *sw = rt.sheet.w - *sx;
    if (*sy + *sh > rt.sheet.h) *sh = rt.sheet.h - *sy;
    return *sw > 0 && *sh > 0;
}

int gpu2d(int op, const int32_t *v)
{
    if (!gpu2d_open())
        return 0;
    const g16_t *g = &rt.g;
    const int cx = g->cam_x, cy = g->cam_y;
    switch (op) {
    case D2_RECTFILL:
        if (v[2] <= 0 || v[3] <= 0)
            return 1;
        return gpu3d_rect2d(g, v[0] - cx, v[1] - cy, v[0] - cx + v[2], v[1] - cy + v[3], (uint16_t)v[4]);
    case D2_RECT: {
        if (v[2] <= 0 || v[3] <= 0)
            return 1;
        const int x = v[0] - cx, y = v[1] - cy, w = v[2], h = v[3];
        const uint16_t c = (uint16_t)v[4];
        return gpu3d_rect2d(g, x, y, x + w, y + 1, c) && gpu3d_rect2d(g, x, y + h - 1, x + w, y + h, c) &&
               gpu3d_rect2d(g, x, y + 1, x + 1, y + h - 1, c) && gpu3d_rect2d(g, x + w - 1, y + 1, x + w, y + h - 1, c);
    }
    case D2_PSET:
        return gpu3d_rect2d(g, v[0] - cx, v[1] - cy, v[0] - cx + 1, v[1] - cy + 1, (uint16_t)v[2]);
    case D2_LINE:
        if (v[1] == v[3]) {             /* a row, both ends in */
            const int a = v[0] < v[2] ? v[0] : v[2], b = v[0] < v[2] ? v[2] : v[0];
            return gpu3d_rect2d(g, a - cx, v[1] - cy, b - cx + 1, v[1] - cy + 1, (uint16_t)v[4]);
        }
        if (v[0] == v[2]) {             /* a column: Bresenham plots each row once */
            const int a = v[1] < v[3] ? v[1] : v[3], b = v[1] < v[3] ? v[3] : v[1];
            return gpu3d_rect2d(g, v[0] - cx, a - cy, v[0] - cx + 1, b - cy + 1, (uint16_t)v[4]);
        }
        return 0;
    case D2_SPR: {
        if (!rt.sheet.px || v[0] < 0)
            return 1;
        const int per_row = rt.sheet.w / G16_CELL;
        int sx = v[0] % per_row * G16_CELL, sy = v[0] / per_row * G16_CELL, sw = v[3] * G16_CELL,
            sh = v[4] * G16_CELL;
        if (!sheet_rect(&sx, &sy, &sw, &sh))
            return 1;
        return gpu3d_blit2d(g, &rt.sheet, sx, sy, sw, sh, v[1] - cx, v[2] - cy, 1, v[5], v[6]);
    }
    case D2_SSPR: {
        int sx = v[0], sy = v[1], sw = v[2], sh = v[3];
        if (!sheet_rect(&sx, &sy, &sw, &sh))
            return 1;
        return gpu3d_blit2d(g, &rt.sheet, sx, sy, sw, sh, v[4] - cx, v[5] - cy, 1, v[6], v[7]);
    }
    case D2_SSPR_ZOOM: {
        float zoom;
        memcpy(&zoom, v + 8, sizeof zoom);
        int sx = v[0], sy = v[1], sw = v[2], sh = v[3];
        if (!(zoom > 0.0f) || zoom > 4096.0f || !sheet_rect(&sx, &sy, &sw, &sh))
            return 1;
        /* whole zooms only: the GPU's texel at a pixel's middle is gfx16's
         * then (other zooms round their steps another way) */
        const int dw = (int)((float)sw * zoom + 0.5f), dh = (int)((float)sh * zoom + 0.5f), k = dw / sw;
        if (dw <= 0 || dh <= 0)
            return 1;
        if (k < 1 || dw != k * sw || dh != k * sh)
            return 0;
        return gpu3d_blit2d(g, &rt.sheet, sx, sy, sw, sh, v[4] - cx, v[5] - cy, k, v[6], v[7]);
    }
    case D2_STACK:
    case D2_STACKV:
        return 0;                       /* the ARM draws it after the job (sprite stacking is CPU only) */
    case D2_MAP: {
        /* g16_map_mask's cells, each a sprite */
        const uint16_t *cells = rt.layer[v[6]];
        const uint8_t mask = rt.flags ? (uint8_t)v[7] : 0;
        if (!rt.sheet.px || !cells)
            return 1;
        const int per_row = rt.sheet.w / G16_CELL, ncells = per_row * (rt.sheet.h / G16_CELL);
        const int x = v[2] - cx, y = v[3] - cy;
        for (int my = 0; my < v[5]; my++) {
            const int row = v[1] + my, py = y + my * G16_CELL;
            if (row < 0 || row >= rt.map.h || py >= g->cy1 || py + G16_CELL <= g->cy0)
                continue;
            for (int mx = 0; mx < v[4]; mx++) {
                const int c = v[0] + mx, px = x + mx * G16_CELL;
                if (c < 0 || c >= rt.map.w || px >= g->cx1 || px + G16_CELL <= g->cx0)
                    continue;
                const int n = cells[row * rt.map.w + c];
                if (n == 0 || n >= ncells || (mask && !(rt.flags[n] & mask)))
                    continue;
                if (!gpu3d_blit2d(g, &rt.sheet, n % per_row * G16_CELL, n / per_row * G16_CELL, G16_CELL, G16_CELL,
                                  px, py, 1, 0, 0))
                    return 0;           /* the ARM draws it all again after the job: the same pixels */
            }
        }
        return 1;
    }
    }
    return 0;
}

/* Before 2D on the page: 1 if it is to be recorded (n words of arguments
 * at *a, after the op): the GPU has 3D for the page and the queue is on,
 * so the 3D is started and the 2D waits for it. Else 0, the 3D waiting
 * drawn first (sync3d). */
int draw2d(int op, uint32_t words, int32_t **a)
{
    if (!d2.on && rt.early) {           /* 2D in the early _update: the next frame's */
        d2.on = 1;
        d2.font = NULL;
    } else if (!d2.on) {
        if (!rt.r3d.backend || !gpu3d_queue() || !gpu3d_pending()) {
            sync3d();
            return 0;
        }
        if (gpu3d_submit(&rt.g, 1) != 0 || gpu3d_failed())
            rt.r3d.backend = NULL;
        cls_settle();
        gpu3d_page(0, 0);
        if (!rt.r3d.backend || !gpu3d_inflight()) {
            sync3d();
            return 0;
        }
        d2.on = 1;
        d2.font = NULL;                 /* the first record sets the state */
    }
    const int state = rt.g.cx0 != d2.cx0 || rt.g.cy0 != d2.cy0 || rt.g.cx1 != d2.cx1 || rt.g.cy1 != d2.cy1 ||
                      rt.g.cam_x != d2.cam_x || rt.g.cam_y != d2.cam_y || rt.g.font != d2.font;
    const uint32_t need = 1 + words + (state ? 7 + sizeof(void *) / 4 : 0);
    if (d2.n + need > d2.cap) {
        uint32_t cap = d2.cap ? d2.cap * 2 : 4096;
        while (cap < d2.n + need)
            cap *= 2;
        int32_t *w = cap * 4 <= D2_MAX ? realloc(d2.w, cap * 4) : NULL;
        if (!w) {                       /* too much: what is recorded goes on the page */
            if (rt.early)
                rt.early_touch = 1;
            flush3d(1);
            return 0;
        }
        d2.w = w;
        d2.cap = cap;
    }
    if (state) {
        int32_t *s = &d2.w[d2.n];
        s[0] = D2_STATE | (int32_t)((6 + sizeof(void *) / 4) << 8);
        s[1] = d2.cx0 = rt.g.cx0; s[2] = d2.cy0 = rt.g.cy0; s[3] = d2.cx1 = rt.g.cx1; s[4] = d2.cy1 = rt.g.cy1;
        s[5] = d2.cam_x = rt.g.cam_x; s[6] = d2.cam_y = rt.g.cam_y;
        d2.font = rt.g.font;
        memcpy(s + 7, &d2.font, sizeof d2.font);
        d2.n += 7 + sizeof(void *) / 4;
    }
    d2.w[d2.n] = op | (int32_t)(words << 8);
    *a = &d2.w[d2.n + 1];
    d2.n += 1 + words;
    return 1;
}

/* r3d needs the ARM (shadows, 3D effects, materials the GPU lacks: see
 * r3d_t.arm_hook): the GPU draws what it holds, and the ARM draws the 3D
 * of this cartridge from here on */
void gpu3d_to_arm(void *ctx, const char *why)
{
    (void)ctx;
    /* what the GPU drew in this frame is not in the ARM's z-buffer: the
     * frame would be wrong, so it is not shown (one frame dropped, once) */
    if (rt.r3d.tris_drawn || gpu3d_pending())
        rt.hold_frame = 1;
    flush3d(0);
    cls_settle();
    if (rt.r3d.backend) {
        gpu3d_drop();
        gpu3d_page(0, 0);
    }
    kprintf("bm: the 3D is drawn by the ARM from here: the cartridge uses %s\n", why);
}

static int l_cls(lua_State *L)
{
    const uint16_t c = col(L, 1, 0);
    int32_t *a;
    if (draw2d(D2_CLS, 1, &a)) {
        a[0] = c;
        return 0;
    }
    if (rt.r3d.backend && !bm_video_uses_ram()) {
        gpu3d_cleared();                /* (a job before this cls cleared to another colour) */
        rt.cls_pending = 1;             /* the GPU's next job clears to it (cls_settle) */
        rt.cls_colour = c;
        gpu3d_page(1, c);
        return 0;
    }
    g16_cls(&rt.g, c);
    if (rt.r3d.backend)
        gpu3d_page(1, c);               /* the GPU's next job clears to it */
    return 0;
}

/* the 2D of pset ... circfill: drawn, or recorded (draw2d) */
static void shape2d(lua_State *L, int op, int n)
{
    int32_t v[5], *a;
    for (int i = 0; i < n - 1; i++)
        v[i] = ival(L, i + 1);
    v[n - 1] = col(L, n, 0xFFFFFF);
    if (gpu2d(op, v))
        return;
    if (draw2d(op, (uint32_t)n, &a)) {
        memcpy(a, v, (size_t)n * sizeof *v);
        return;
    }
    switch (op) {
    case D2_PSET: g16_pset(&rt.g, v[0], v[1], (uint16_t)v[2]); break;
    case D2_LINE: g16_line(&rt.g, v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case D2_RECT: g16_rect(&rt.g, v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case D2_RECTFILL: g16_rectfill(&rt.g, v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case D2_CIRC: g16_circ(&rt.g, v[0], v[1], v[2], (uint16_t)v[3]); break;
    case D2_CIRCFILL: g16_circfill(&rt.g, v[0], v[1], v[2], (uint16_t)v[3]); break;
    }
}

static int l_pset(lua_State *L)     { shape2d(L, D2_PSET, 3); return 0; }
static int l_line(lua_State *L)     { shape2d(L, D2_LINE, 5); return 0; }
static int l_rect(lua_State *L)     { shape2d(L, D2_RECT, 5); return 0; }
static int l_rectfill(lua_State *L) { shape2d(L, D2_RECTFILL, 5); return 0; }
static int l_circ(lua_State *L)     { shape2d(L, D2_CIRC, 4); return 0; }
static int l_circfill(lua_State *L) { shape2d(L, D2_CIRCFILL, 4); return 0; }

static int l_pget(lua_State *L)
{
    if (rt.early)
        rt.early_touch = 1;             /* the early _update would read this frame's page */
    sync3d();
    int c = g16_pget(&rt.g, ival(L, 1), ival(L, 2));
    if (c < 0) lua_pushnil(L);
    else lua_pushinteger(L, g16_to_rgb24((uint16_t)c));
    return 1;
}

static int l_spr(lua_State *L)
{
    const int32_t v[7] = { ival(L, 1), ival(L, 2), ival(L, 3), oval(L, 4, 1), oval(L, 5, 1), lua_toboolean(L, 6),
                           lua_toboolean(L, 7) };
    int32_t *a;
    sheet_commit();
    if (gpu2d(D2_SPR, v))
        return 0;
    if (draw2d(D2_SPR, 7, &a))
        memcpy(a, v, sizeof v);
    else
        g16_spr(&rt.g, &rt.sheet, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
    return 0;
}

/* sspr(sx, sy, sw, sh, dx, dy, [flip_x, flip_y, zoom]): zoom (default 1)
 * draws it that many times bigger, or smaller below 1 (nearest pixel) */
int l_sspr(lua_State *L)
{
    const int32_t v[8] = { ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
                           lua_toboolean(L, 7), lua_toboolean(L, 8) };
    const float zoom = (float)luaL_optnumber(L, 9, 1);
    int32_t *a;
    sheet_commit();
    int32_t z[9];
    memcpy(z, v, sizeof v);
    memcpy(z + 8, &zoom, sizeof zoom);
    if (gpu2d(zoom == 1 ? D2_SSPR : D2_SSPR_ZOOM, z))
        return 0;
    if (zoom == 1) {
        if (draw2d(D2_SSPR, 8, &a))
            memcpy(a, v, sizeof v);
        else
            g16_sspr(&rt.g, &rt.sheet, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    } else if (draw2d(D2_SSPR_ZOOM, 9, &a)) {
        memcpy(a, v, sizeof v);
        memcpy(a + 8, &zoom, sizeof zoom);
    } else {
        g16_sspr_zoom(&rt.g, &rt.sheet, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], zoom);
    }
    return 0;
}

/* the record or the draw of one stack, shared by stack(), stackv() and zstack() */
void stack_draw(const int32_t v[7], const float f[3], int vert)
{
    int32_t *a;
    sheet_commit();
    if (draw2d(vert ? D2_STACKV : D2_STACK, 10, &a)) {
        memcpy(a, v, 7 * sizeof v[0]);
        memcpy(a + 7, f, 3 * sizeof f[0]);
    } else if (vert) {
        g16_stackv(&rt.g, &rt.sheet, v[0], v[1], v[2], v[3], v[4], v[5], v[6], f[0], f[1], f[2]);
    } else {
        g16_stack(&rt.g, &rt.sheet, v[0], v[1], v[2], v[3], v[4], v[5], v[6], f[0], f[1], f[2]);
    }
}

/* stack(sx, sy, w, h, n, x, y, [rz=0, scale=1, step=1]): sprite stacking, n slices of w x h
 * side by side from (sx, sy); the first is the bottom, centred on (x, y); rz in radians */
static int stack_lua(lua_State *L, int vert)
{
    const int32_t v[7] = { ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4), ival(L, 5), ival(L, 6),
                           ival(L, 7) };
    const float f[3] = { (float)luaL_optnumber(L, 8, 0), (float)luaL_optnumber(L, 9, 1),
                         (float)luaL_optnumber(L, 10, 1) };
    stack_draw(v, f, vert);
    return 0;
}

static int l_stack(lua_State *L) { return stack_lua(L, 0); }

/* stackv(sx, sy, w, h, n, x, y, [rz=0, scale=1, step=1]): as stack(), the n slices one under
 * the other on the sheet from (sx, sy), top to bottom; the top one is the bottom of the stack */
static int l_stackv(lua_State *L) { return stack_lua(L, 1); }

static int l_sget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h || !rt.sheet.alpha[y * rt.sheet.w + x])
        lua_pushnil(L);
    else
        lua_pushinteger(L, g16_to_rgb24(rt.sheet.px[y * rt.sheet.w + x]));
    return 1;
}

/* the sheet was drawn on since cart_load (sset, cart_sheet): cart_save
 * writes it whole, not the section the file had (proj_sheet8, below) */
int proj_sheet_drawn;

/* sset(x, y, colour) - nil colour = transparent */
static int l_sset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h)
        return 0;
    int opaque = !lua_isnoneornil(L, 3);
    later_paint();                      /* the frame held's sprites as they were (bm3d 6.9) */
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    g16_sheet_set(&rt.sheet, x, y, opaque ? col(L, 3, 0) : 0, opaque);
    rt.cell_dirty[(y / G16_CELL) * (rt.sheet.w / G16_CELL) + x / G16_CELL] = 1;
    rt.sheet_dirty = 1;
    proj_sheet_drawn = 1;
    return 0;
}

/* print(text, x, y [, colour, scale]) */
static int l_print(lua_State *L)
{
    /* read the numeric arguments first: luaL_tolstring pushes a value */
    int x = ival(L, 2), y = ival(L, 3);
    uint16_t c = col(L, 4, 0xFFFFFF);
    int scale = oval(L, 5, 1);
    if (scale > 8) scale = 8;
    size_t len;
    const char *s = luaL_tolstring(L, 1, &len);
    int32_t *a;
    if (gpu2d_open() && gpu3d_text2d(&rt.g, x, y, s, c, scale)) {
        g16_t m = rt.g;                 /* where it ends, as drawn (bm3d 4.8: the GPU drew it) */
        m.cx0 = m.cy0 = 0x40000000;
        m.cx1 = m.cy1 = -0x40000000;
        lua_pushinteger(L, g16_text_scaled(&m, x, y, s, c, scale));
        return 1;
    }
    if (draw2d(D2_TEXT, 4 + (uint32_t)(len + 4) / 4, &a)) {
        a[0] = x; a[1] = y; a[2] = c; a[3] = scale;
        memcpy(a + 4, s, len + 1);
        /* where it ends, as drawn (nothing drawn: no clip rectangle) */
        g16_t m = rt.g;
        m.cx0 = m.cy0 = 0x40000000;
        m.cx1 = m.cy1 = -0x40000000;
        lua_pushinteger(L, g16_text_scaled(&m, x, y, s, c, scale));
        return 1;
    }
    lua_pushinteger(L, g16_text_scaled(&rt.g, x, y, s, c, scale));
    return 1;
}

/* font([name]): the font of print() from now on, "8x16" (the default),
 * "8x14" or "6x12" (also "large", "medium", "small"); returns the width
 * and height of a character of the current font */
static int l_font(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        const char *n = luaL_checkstring(L, 1);
        if (!strcmp(n, "6x12") || !strcmp(n, "small")) rt.g.font = &font_console_6x12;
        else if (!strcmp(n, "8x14") || !strcmp(n, "medium")) rt.g.font = &font_console_8x14;
        else if (!strcmp(n, "8x16") || !strcmp(n, "large")) rt.g.font = &font_console_8x16;
        else return luaL_argerror(L, 1, "\"6x12\", \"8x14\" or \"8x16\"");
    }
    lua_pushinteger(L, rt.g.font->width);
    lua_pushinteger(L, rt.g.font->height);
    return 2;
}

/* ---- 3D (software rasterizer, see r3d.h) */

float fnum(lua_State *L, int i, float def)
{
    return (float)luaL_optnumber(L, i, def);
}

/* bm_next_run: options of the next run only */
static struct { int w, h, gpu3d, bench; } next = { 0, 0, -1, 0 }, cur = { 0, 0, -1, 0 };

void bm_next_run(int w, int h, int gpu3d, int bench)
{
    next.w = w;
    next.h = h;
    next.gpu3d = gpu3d;
    next.bench = bench;
}

/* The V3D draws the 3D of the cartridges (M33) if it starts and passes its
 * probe; gpu3d=0 in bm/config.txt (Settings > Graphics > 3D of the games: ARM) keeps
 * the ARM's rasterizer */
/* gpu3d_merge (bm3d 7.6): draws of static meshes with the same sheet become one draw; on unless
 * gpu3d_merge=0 (r3d.c "draw merging"; the Mali backend has no merge hook, so it never merges) */
int merge_cfg(void)
{
    const char *m = config_get("gpu3d_merge");
    return !m || strcmp(m, "0") != 0;
}

static void gpu3d_maybe(void)
{
    const char *v = config_get("gpu3d");
    if (cur.gpu3d == 0 || (cur.gpu3d < 0 && v && strcmp(v, "0") == 0))
        return;
    if (rt.g.stride != (uint32_t)rt.g.w) {      /* 256x256 in the middle of the screen: whole pages only */
        rt.gpu3d_boxed = 1;                     /* screen() to a whole page tries again (screen_apply) */
        kprintf("bm: the 3D is drawn by the ARM as bm3d %s: the page is a square inside the screen\n",
                bm3d_mode(0, 0));
        return;
    }
    if (gpu3d_init() == 0) {
        gpu3d_stats_t st;
        gpu3d_drop();                   /* nothing learned from the game before (depth kept) */
        gpu3d_take_stats(&st);          /* this game's from here */
        const char *aa = config_get("gpu3d_aa");
        gpu3d_set_msaa(aa && strcmp(aa, "1") == 0);     /* anti-aliasing: hidden key (SYS-9) */
        const char *vs = config_get("gpu3d_vs");
        gpu3d_set_vshader(vs ? atoi(vs) : 2);           /* vertex shader: hidden key (0, 1, 2; every model
                                                         * from bm3d 6.6, where the boot's probes saw it) */
        const char *q = config_get("gpu3d_queue");
        gpu3d_set_queue(!q ? 1 : (q[0] == '1' || q[0] == '2') ? q[0] - '0' : 0);  /* the frame in the queue (bm3d 4.1,
                                                                                     on without the key; "0" = off),
                                                                                     two jobs in flight (bm3d 5.1) */
        const char *wc = config_get("gpu3d_wc");
        gpu3d_set_wc(wc && strcmp(wc, "1") == 0);       /* the jobs' memory uncached (bm3d 4.1): hidden key (SYS-9) */
        const char *bf = config_get("gpu3d_filter");
        gpu3d_set_bilinear(bf && strcmp(bf, "1") == 0); /* textures filtered (bm3d 4.8): hidden key (SYS-9) */
        const char *t16 = config_get("gpu3d_tex16");
        gpu3d_set_tex16(t16 && strcmp(t16, "1") == 0);  /* opaque textures in 16 bits (bm3d 5.2): hidden key (SYS-9) */
        const char *e1 = config_get("gpu3d_etc1");
        gpu3d_set_etc1(e1 && strcmp(e1, "1") == 0);     /* opaque textures as ETC1 (bm3d 7.3): hidden key (SYS-9) */
        const char *fs2 = config_get("gpu3d_fs2");
        gpu3d_set_fs2(!fs2 || strcmp(fs2, "0") != 0);   /* two-thread pixel shaders (bm3d 5.3; on from 6.4) */
        const char *so = config_get("gpu3d_sort");
        gpu3d_set_sort(so && strcmp(so, "1") == 0);     /* opaque meshes nearest first (bm3d 5.4): hidden key (SYS-9) */
        const char *at = config_get("gpu3d_attr");
        gpu3d_set_attr(at && strcmp(at, "1") == 0);     /* compact attributes (bm3d 7.1; off by default, the
                                                         * Pi measured no gain; the boot's probe_attr decides
                                                         * when on): hidden key (SYS-9) */
        const char *g2 = config_get("gpu3d_2d");
        rt.gpu2d = !g2 || strcmp(g2, "0") != 0;          /* the 2D over the 3D in the job (bm3d 4.8): on by default (+6.7% boost, 3569+ sprites); 0 = off (SYS-9b) */
        const char *lt = config_get("gpu3d_later");
        rt.later = lt && strcmp(lt, "1") == 0;          /* each frame shown a frame later (bm3d 6.9): hidden key (SYS-9) */
        rt.r3d.backend = gpu3d_backend();
        rt.r3d.arm_hook = gpu3d_to_arm;
        rt.r3d.merge = merge_cfg();
        if (!cur.bench)                 /* a benchmark has its own report */
            kprintf("bm: the 3D is drawn by the GPU as bm3d %s (%s)\n",
                    bm3d_mode_l(1, gpu3d_vshader_on(), gpu3d_queue(), rt.later && gpu3d_queue2()), gpu3d_status());
        if (rt.later && !gpu3d_queue2())
            kprintf("bm: gpu3d_later needs the frame queue with two jobs (gpu3d_queue=2): each frame shown at once\n");
    } else {
        kprintf("bm: the 3D is drawn by the ARM as bm3d %s: %s\n", bm3d_mode(0, 0), gpu3d_status());
    }
}

/* r3d_impostor / gpu3d()'s fifth: 0 off, else pixels (2..64) */
int impostor_px(int px)
{
    return px <= 0 ? 0 : px < 2 ? 2 : px > 64 ? 64 : px;
}

r3d_t *r3d(lua_State *L)
{
    if (!rt.r3d_ready) {
        if (r3d_init(&rt.r3d, &rt.g) != 0)
            luaL_error(L, "not enough memory for the z-buffer");
        rt.r3d_ready = 1;
        const char *fast = config_get("r3d_fast");
        rt.r3d.fast = fast && strcmp(fast, "1") == 0;   /* bm3d 4.9: one matrix, light in the object's axes */
        const char *imp = config_get("r3d_impostor");
        rt.r3d.impostor = imp ? impostor_px(atoi(imp)) : 0;   /* far small meshes as one quad: hidden key (SYS-9) */
        gpu3d_maybe();
    }
    if (rt.cls_pending && !rt.r3d.backend)
        cls_settle();                   /* the GPU stopped: the ARM's 3D goes on the cleared page */
    return &rt.r3d;
}

int rt_get_map_layer(const char *name, const uint16_t **cells, int *w, int *h)
{
    if (w) *w = rt.map.w;
    if (h) *h = rt.map.h;
    if (!name || !*name) return -1;
    for (int i = 0; i < rt.nlayers; i++) {
        if (!strcmp(rt.layer_name[i], name)) {
            if (cells) *cells = rt.layer[i];
            return i;
        }
    }
    return -1;
}

const g16_sheet_t *rt_get_sheet(void)
{
    return &rt.sheet;
}

r3d_t *rt_get_r3d(lua_State *L)
{
    return r3d(L);
}

/* The DMA clear started at the end of the frame, if any, is waited for
 * (it ran during _update). claim = the buffer is about to be drawn on:
 * a later zclear() in the same frame clears it again. */
void zclear_dma_wait(int claim)
{
    if (!rt.zclear_dma)
        return;
    dma_wait();
    if (claim)
        rt.zclear_dma = 0;
}

/* After a frame that cleared the z-buffer, the next clear starts at once
 * on the DMA, while the cartridge runs _update (zclear() then only waits
 * for it): about 1 ms saved per frame at 640x360. dma_zclear=0 in
 * bm/config.txt turns it off. */
static void zclear_dma_start(void)
{
    const int seen = rt.zclear_seen;
    rt.zclear_seen = 0;
    if (!seen || !rt.r3d_ready || rt.r3d.backend || rt.zclear_dma || !dma_ready())
        return;
    static int off = -1;
    if (off < 0) {
        const char *v = config_get("dma_zclear");
        off = v && strcmp(v, "0") == 0;
    }
    if (off)
        return;
    const uint32_t bytes = (uint32_t)rt.r3d.g->w * (uint32_t)rt.r3d.g->h * 2;
    if (bytes & 15)
        return;
    /* no dirty line of the z-buffer may be written back over the DMA's
     * zeros, and none may stay cached: the whole data cache is cleaned and
     * dropped (16 KB, cheaper than the 460 KB range) */
    dcache_clean_invalidate_all();
    dma_fill(rt.r3d.zbuf, 0, bytes);
    rt.zclear_dma = 1;
}

void skel_free(skel_t *s)
{
    if (!s)
        return;
    free(s->data);
    free(s->mat);
    free(s->radius);
    free(s);
}

r3d_mesh_t *new_mesh(lua_State *L)
{
    lmesh_t *lm = lua_newuserdatauv(L, sizeof *lm, 0);
    memset(lm, 0, sizeof *lm);
    luaL_setmetatable(L, MESH_MT);
    return &lm->m;
}

static int l_mesh_gc(lua_State *L)
{
    lmesh_t *lm = luaL_checkudata(L, 1, MESH_MT);
    r3d_mesh_free(&lm->m);
    skel_free(lm->skel);
    lm->skel = NULL;
    return 0;
}

/* mesh({x,y,z, x,y,z, ...}, {a,b,c,colour, ...} [, {u0,v0,u1,v1,u2,v2, ...}])
 * - 1-based vertex indices, faces clockwise seen from outside (on screen). With
 * the third table (6 numbers per face, sprite-sheet pixels), faces whose
 * colour is -1 are textured with the sprite sheet. */
static int l_mesh(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    int nv = (int)(luaL_len(L, 1) / 3), nf = (int)(luaL_len(L, 2) / 4);
    luaL_argcheck(L, nv > 0 && nv <= 65535, 1, "1 to 65535 vertices");
    luaL_argcheck(L, nf > 0 && nf <= 65535, 2, "1 to 65535 faces");
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_alloc(m, nv, nf) != 0)
        return luaL_error(L, "not enough memory for the mesh");
    for (int i = 0; i < nv * 3; i++) {
        lua_rawgeti(L, 1, i + 1);
        ((float *)m->verts)[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    for (int f = 0; f < nf; f++) {
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(L, 2, f * 4 + k + 1);
            lua_Integer idx = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (idx < 1 || idx > nv)
                return luaL_error(L, "face %d: vertex index %d out of range", f + 1, (int)idx);
            m->faces[f * 3 + k] = (uint16_t)(idx - 1);
        }
        lua_rawgeti(L, 2, f * 4 + 4);
        lua_Integer c = lua_tointeger(L, -1);
        m->colors[f] = c < 0 ? R3D_TEXTURED : (uint32_t)c;     /* -1: textured */
        lua_pop(L, 1);
    }
    if (lua_istable(L, 3)) {
        luaL_argcheck(L, luaL_len(L, 3) >= (lua_Integer)nf * 6, 3, "6 texture coordinates per face");
        if (r3d_mesh_alloc_uv(m) != 0)
            return luaL_error(L, "not enough memory for the mesh");
        for (int i = 0; i < nf * 6; i++) {
            lua_rawgeti(L, 3, i + 1);
            m->uv[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        m->tex = &rt.sheet;             /* live: sset() changes the texture too */
    }
    r3d_mesh_normals(m);
    return 1;
}

/* code_tokens(src) -> the tokens of a piece of Lua code, counted as for
 * stat(11) (tokens.h): the SDK's dev kit */
static int l_code_tokens(lua_State *L)
{
    size_t n;
    const char *s = luaL_checklstring(L, 1, &n);
    lua_pushinteger(L, lua_tokens(s, n));
    return 1;
}

/* models() -> { "name", ... }: the 3D models of the cartridge (MESH
 * section, made with bm Studio), in order */
static int l_models(lua_State *L)
{
    lua_newtable(L);
    bm_model_t m;
    for (int i = 0; rt.mesh && bm_mesh_model(rt.mesh, rt.mesh_size, i, &m) == 0; i++) {
        lua_pushstring(L, m.name);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* Moves the texture corners of a face `inset` pixels towards its middle, on
 * each axis (not past it), so the next tile of the sheet never shows. */
static void uv_inset(float *uv, float inset)
{
    for (int axis = 0; axis < 2; axis++) {
        float lo = uv[axis], hi = uv[axis];
        for (int k = 1; k < 3; k++) {
            float t = uv[k * 2 + axis];
            if (t < lo) lo = t;
            if (t > hi) hi = t;
        }
        if (hi - lo <= 2 * inset)
            continue;
        float mid = (lo + hi) * 0.5f;
        for (int k = 0; k < 3; k++) {
            float *t = &uv[k * 2 + axis];
            if (*t < mid - inset) *t += inset;
            else if (*t > mid + inset) *t -= inset;
        }
    }
}

/* model(name or number) -> a mesh built from the cartridge's MESH section
 * (textured faces use the sprite sheet), or nil if there is no such model */
static int l_model(lua_State *L)
{
    bm_model_t md;
    int found = -1;
    if (rt.mesh) {
        if (lua_type(L, 1) == LUA_TNUMBER) {
            int i = (int)luaL_checkinteger(L, 1) - 1;
            if (bm_mesh_model(rt.mesh, rt.mesh_size, i, &md) == 0)
                found = i;
        } else {
            const char *name = luaL_checkstring(L, 1);
            for (int i = 0; bm_mesh_model(rt.mesh, rt.mesh_size, i, &md) == 0; i++)
                if (strcmp(md.name, name) == 0) {
                    found = i;
                    break;
                }
        }
    } else if (lua_type(L, 1) != LUA_TNUMBER) {
        luaL_checkstring(L, 1);
    }
    if (found < 0) {
        lua_pushnil(L);
        return 1;
    }
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_alloc(m, md.nverts, md.nfaces) != 0)
        return luaL_error(L, "not enough memory for the model");
    for (int i = 0; i < md.nverts; i++) {
        float xyz[3];
        bm_model_vertex(&md, i, xyz);
        m->verts[i] = (v3_t){ xyz[0], xyz[1], xyz[2] };
    }
    const float inset = bm_mesh_inset(rt.mesh);
    for (int f = 0; f < md.nfaces; f++) {
        uint32_t colour;
        float uv[6];
        bm_model_face(&md, f, m->faces + f * 3, &colour, uv);
        if (colour & R3D_TEXTURED) {
            colour &= 0xFF000000u;          /* textured, with its material bits */
            if (!m->uv) {
                if (r3d_mesh_alloc_uv(m) != 0)
                    return luaL_error(L, "not enough memory for the model");
                m->tex = &rt.sheet;     /* live, as for mesh() */
            }
            uv_inset(uv, inset);
            memcpy(m->uv + f * 6, uv, sizeof uv);
        }
        m->colors[f] = colour;
    }
    /* a model with its light baked (the world of a map): the light of each corner */
    if (md.flags & BM_MODEL_LIT) {
        if (!(m->clight = malloc((size_t)md.nfaces * 9)))
            return luaL_error(L, "not enough memory for the model");
        for (int f = 0; f < md.nfaces; f++)
            bm_model_face_light(&md, f, m->clight + f * 9);
    }
    r3d_mesh_normals(m);
    /* a skeleton made for this model (the same vertices) comes with it */
    bm_rig_t r;
    if (rt.anim && bm_anim_rig(rt.anim, rt.anim_size, md.name, &r) == 0 && r.nverts == md.nverts) {
        skel_t *sk = calloc(1, sizeof *sk);
        const uint8_t *start = r.bones - (BM_MODEL_NAME + 8);
        if (!sk || !(sk->data = malloc(r.size)) || !(sk->mat = malloc(r.nbones * sizeof *sk->mat)) ||
            !(sk->radius = calloc(r.nbones, sizeof(float)))) {
            skel_free(sk);
            return luaL_error(L, "not enough memory for the model");
        }
        memcpy(sk->data, start, r.size);
        bm_rig_read(sk->data, r.size, &sk->r);
        for (int i = 0; i < r.nbones; i++) {
            static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            memcpy(sk->mat[i], id, sizeof id);
            sk->turn[i][0] = sk->turn[i][1] = sk->turn[i][2] = 0;
            sk->turn[i][3] = 1;
        }
        /* the capsule of each bone: its segment, wide enough for its vertices */
        for (int v = 0; v < md.nverts; v++) {
            int b = sk->r.vbones[v];
            float h[3], t[3];
            bm_rig_bone(&sk->r, b, NULL, NULL, h, t);
            float d[3] = { t[0] - h[0], t[1] - h[1], t[2] - h[2] }, q[3] = { m->verts[v].x - h[0],
                           m->verts[v].y - h[1], m->verts[v].z - h[2] };
            float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], u = dd > 1e-12f ? (q[0] * d[0] + q[1] * d[1] + q[2] * d[2]) / dd : 0;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            float ex = q[0] - d[0] * u, ey = q[1] - d[1] * u, ez = q[2] - d[2] * u, e = sqrtf(ex * ex + ey * ey + ez * ez);
            if (e > sk->radius[b])
                sk->radius[b] = e;
        }
        ((lmesh_t *)m)->skel = sk;
        m->bones = (const float (*)[12])sk->mat;     /* r3d moves each vertex with its bone */
        m->vbone = sk->r.vbones;
        m->nbones = r.nbones;
    }
    return 1;
}

/* The resolutions screen() can change to: 16:9, whole pixels on a 1080p
 * TV but for 1280x720 (1.5x). The cartridge's own (its header) is one. */
static const uint16_t screen_modes[][2] = {
    { 320, 180 }, { 384, 216 }, { 480, 270 }, { 640, 360 }, { 960, 540 }, { 1280, 720 }, { 1920, 1080 },
};
#define SCREEN_MODES (int)(sizeof screen_modes / sizeof screen_modes[0])
/* On a square panel (the RGB30's 720x720) the modes are the .b16's two
 * (docs/B16.md §0): 360x360 shown twice as big and 720x720, whole pixels
 * both; there a page is never boxed (bm_video_enter). The third is the 16:9
 * page 720x405, in the middle of the panel with black bars above and below
 * (the display centres it). The order is fixed: Overbit indexes the list.
 * bmhost --square plays the same on the PC. */
static const uint16_t square_modes[][2] = { { 360, 360 }, { 720, 720 }, { 720, 405 } };
#define SQUARE_MODES (int)(sizeof square_modes / sizeof square_modes[0])
#ifdef BM_RGB30
static int square_panel = 1;
#else
static int square_panel;
#endif

void bm_set_square_panel(int on)
{
    square_panel = on;
}

/* screen(w, h) -> true: the cartridge's resolution changes to w x h from the
 * next frame (SCREEN_W and SCREEN_H then say it; if the console cannot
 * set it they stay); false: not one of the modes. screen() -> w, h now.
 * screen(i) -> the i-th mode w, h (1 = 320x180 ... 7 = 1920x1080; on a
 * square panel 1 = 360x360, 2 = 720x720, 3 = 720x405), or nil. A square cartridge
 * (256x256, 360x360) changes as the others (2026-10-10, Overbit's .b16 on
 * the Pi: a whole 16:9 page, where the GPU draws). */
static int l_screen(lua_State *L)
{
    const uint16_t (*modes)[2] = square_panel ? square_modes : screen_modes;
    const int nmodes = square_panel ? SQUARE_MODES : SCREEN_MODES;
    if (lua_isnoneornil(L, 1)) {
        lua_pushinteger(L, rt.g.w);
        lua_pushinteger(L, rt.g.h);
        return 2;
    }
    const int w = ival(L, 1);
    if (lua_isnoneornil(L, 2)) {
        if (w < 1 || w > nmodes)
            return 0;
        lua_pushinteger(L, modes[w - 1][0]);
        lua_pushinteger(L, modes[w - 1][1]);
        return 2;
    }
    const int h = ival(L, 2);
    int ok = 0;
    for (int i = 0; i < nmodes; i++)
        ok |= modes[i][0] == w && modes[i][1] == h;
    if (ok) {
        rt.want_w = w;
        rt.want_h = h;
    }
    lua_pushboolean(L, ok);
    return 1;
}

/* safe_area() -> x, y, w, h: the part of the screen that has the aspect the cartridge was made for (its
 * header size), in the middle: the whole screen if the aspect is the same; a design wider than the screen
 * takes the whole width and the middle of the height; any other takes the whole height and the middle of
 * the width (a 360x360 game on 640x360: 140, 0, 360, 360). A cartridge made for another aspect lays its
 * HUD out in this area. */
static int l_safe_area(lua_State *L)
{
    const int sw = rt.g.w, sh = rt.g.h, dw = rt.design_w, dh = rt.design_h;
    int x = 0, y = 0, w = sw, h = sh;
    if (dw > 0 && dh > 0) {
        if ((long)dw * sh > (long)dh * sw) {            /* wider than the screen */
            h = (int)((long)sw * dh / dw);
            y = (sh - h) / 2;
        } else if ((long)dw * sh < (long)dh * sw) {     /* narrower */
            w = (int)((long)sh * dw / dh);
            x = (sw - w) / 2;
        }
    }
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

/* aspect() -> the screen's width / height, and "wide" (wider than tall), "square" or "tall" */
static int l_aspect(lua_State *L)
{
    const int w = rt.g.w, h = rt.g.h;
    lua_pushnumber(L, h > 0 ? (lua_Number)w / (lua_Number)h : 1);
    lua_pushstring(L, w > h ? "wide" : w == h ? "square" : "tall");
    return 2;
}

/* saved([slot]): the saved table, or nil. (Not "load": that is Lua's code
 * loader, which the sandbox removes.) */
char perm_title[49];             /* the game asking */
framebuffer_t *perm_fb;          /* the screen of the run (api_perm.c shows its questions on it) */

/* The state of the screenshots (api_shot.c). */
int shot_req;                    /* a frame to save: the one drawn next */
char shot_file[13];              /* its name, "pong0007.png" */
int shot_note;                   /* frames left for the box that says it was saved */
char shot_line[2][40];
int shot_net;                    /* asked over the network (bm_shot_ask): plan it at the next key poll */
int shot_res;                    /* its answer: 2 waiting for the frame, 1 saved, -1 not saved, 0 none asked */
char shot_net_file[13];          /* the name of the file it saved */

static const luaL_Reg api[] = {
    { "video_open", l_video_open },
    { "cls", l_cls }, { "pset", l_pset }, { "pget", l_pget }, { "line", l_line },
    { "rect", l_rect }, { "rectfill", l_rectfill }, { "circ", l_circ }, { "circfill", l_circfill },
    { "spr", l_spr }, { "sspr", l_sspr }, { "stack", l_stack }, { "stackv", l_stackv }, { "zstack", l_zstack }, { "map", l_map }, { "mget", l_mget }, { "mset", l_mset },
    { "fget", l_fget }, { "fset", l_fset }, { "mflags", l_mflags }, { "msize", l_msize },
    { "mlayers", l_mlayers }, { "zone", l_zone }, { "zones", l_zones }, { "zspr", l_zspr },
    { "zboxes", l_zboxes },
    { "sget", l_sget }, { "sset", l_sset }, { "print", l_print }, { "font", l_font }, { "camera", l_camera },
    { "prompt", l_prompt }, { "lastinput", l_lastinput },
    { "clip", l_clip }, { "rgb", l_rgb }, { "btn", l_btn }, { "btnp", l_btnp },
    { "players", l_players }, { "stick", l_stick },
    { "time", l_time }, { "stat", l_stat }, { "frameskip", l_frameskip }, { "devkit", l_devkit }, { "profile", l_profile }, { "breakpoint", l_breakpoint },
    { "devinfo", l_devinfo }, { "code_tokens", l_code_tokens }, { "tri", l_tri },
    { "battery", l_battery }, { "battery_low", l_battery_low },
    { "mesh", l_mesh }, { "mesh_sphere", l_mesh_sphere }, { "mesh_cube", l_mesh_cube },
    { "model", l_model }, { "models", l_models }, { "bounds3d", l_bounds3d },
    { "animate", l_animate }, { "clips", l_clips }, { "bone3d", l_bone3d },
    { "draw3d", l_draw3d }, { "textures3d", l_textures3d }, { "camera3d", l_camera3d }, { "light3d", l_light3d },
    { "sky3d", l_sky3d }, { "cel3d", l_cel3d }, { "dither3d", l_dither3d }, { "shine3d", l_shine3d }, { "shadow3d", l_shadow3d },
    { "point3d", l_point3d }, { "line3d", l_line3d }, { "sprite3d", l_sprite3d },
    { "particles", l_particles }, { "particles_add", l_particles_add }, { "particles_step", l_particles_step },
    { "particles_draw", l_particles_draw }, { "particles_count", l_particles_count },
    { "bone_turn", l_bone_turn }, { "bones3d", l_bones3d }, { "hit3d", l_hit3d },
    { "world3d", l_world3d }, { "world_box", l_world_box }, { "world_ray", l_world_ray },
    { "world_move", l_world_move }, { "world_floor", l_world_floor },
    { "map3d_build", l_map3d_build }, { "map3d_draw", l_map3d_draw },
    { "map3d_height", l_map3d_height }, { "map3d_blocked", l_map3d_blocked },
    { "map3d_objects", l_map3d_objects }, { "map3d_size", l_map3d_size },
    { "fog3d", l_fog3d }, { "project3d", l_project3d }, { "visible3d", l_visible3d }, { "pvs3d", l_pvs3d }, { "lamp3d", l_lamp3d },
    { "zclear", l_zclear }, { "gpu3d", l_gpu3d }, { "gpu3d_opt", l_gpu3d_opt }, { "gpu3d_stats", l_gpu3d_stats }, { "screen", l_screen }, { "safe_area", l_safe_area }, { "aspect", l_aspect },{ "log", l_log }, { "report", l_report }, { "keyhelp", l_keyhelp }, { "showkeys", l_showkeys }, { "shot", l_shot }, { "quit", l_quit },
    { "keymap", l_keymap }, { "controls", l_controls }, { "controller", l_controller }, { "online", l_online },
    { "udp_open", l_udp_open }, { "udp_send", l_udp_send }, { "udp_recv", l_udp_recv }, { "udp_close", l_udp_close },
    { "net_ip", l_net_ip }, { "net_resolve", l_net_resolve },
    { "web_get", l_web_get }, { "web_poll", l_web_poll }, { "web_info", l_web_info }, { "web_body", l_web_body },
    { "web_cancel", l_web_cancel }, { "image_load", l_image_load }, { "image_file", l_image_file }, { "sheet_png", l_sheet_png }, { "sheet_gif", l_sheet_gif }, { "file_write", l_file_write }, { "file_state", l_file_state },
    { "save", l_save }, { "saved", l_saved }, { "saves", l_saves }, { "delsave", l_delsave },
    { "doc_list", l_doc_list }, { "doc_read", l_doc_read }, { "doc_write", l_doc_write }, { "doc_delete", l_doc_delete },
    { "keyp", l_keyp }, { "keyheld", l_keyheld }, { "rawkeys", l_rawkeys }, { "keydown", l_keydown },
    { "keys", l_keys }, { "pad", l_pad }, { "mouse", l_mouse }, { "mousep", l_mousep }, { "timeslice", l_timeslice }, { "ls", l_ls }, { "cart_load", l_cart_load }, { "cart_new", l_cart_new },
    { "cart_save", l_cart_save }, { "cart_build", l_cart_build }, { "cart_run", l_cart_run }, { "cart_tool", l_cart_tool }, { "cart_arg", l_cart_arg }, { "cart_config", l_cart_config },
    { "cart_data", l_cart_data },
    { "cart_read", l_cart_read }, { "cart_write", l_cart_write }, { "cart_meshes", l_cart_meshes },
    { "mesh_reduce", l_mesh_reduce }, { "picture3d", l_picture3d }, { "cutout3d", l_cutout3d }, { "glb3d", l_glb3d }, { "res_write", l_res_write }, { "res_read", l_res_read },
    { "cart_sheet", l_cart_sheet },
    { "light_begin", l_light_begin }, { "light", l_light }, { "light_end", l_light_end },
    { "fades", l_fades }, { "dark_begin", l_dark_begin }, { "glow", l_glow }, { "dark_end", l_dark_end },
    { "note", l_note }, { "noteoff", l_noteoff }, { "freq", l_freq },
    { "envelope", l_envelope }, { "duty", l_duty }, { "playing", l_playing }, { "apu", l_apu },
    { "hz", l_hz }, { "slide", l_slide }, { "vibrato", l_vibrato }, { "arp", l_arp },
    { "sfx", l_sfx }, { "sfxpos", l_sfxpos }, { "music", l_music }, { "tempo", l_tempo },
    { "mute", l_mute }, { "volume", l_volume },
    { "tone", l_tone }, { "play", l_play }, { "instruments", au_lua_instruments }, { "instrument", au_lua_instrument },
    { "reverb", l_reverb }, { "echo", l_echo }, { "chorus", l_chorus }, { "retro", l_retro },
    { "audio_time", l_audio_time }, { "play_at", l_play_at }, { "play_cancel", l_play_cancel },
    { "play_voices", l_play_voices },
    { "audio_bank", l_audio_bank }, { "audio_pattern", l_audio_pattern }, { "audio_play", l_audio_play },
    { "audio_samples", l_audio_samples }, { "audio_depth", l_audio_depth },
    { "cart_audio", l_cart_audio }, { "cart_put_audio", l_cart_put_audio },
    { NULL, NULL },
};

/* the table for api_editor.c (cart_meshes runs a cartridge apart with these names) */
const luaL_Reg *rt_api(void)
{
    return api;
}

/* ---------------------------------------------------------------- state */

/* bmhost's profiler of the Lua (BMHOST_LUAPROF): where the count hook
 * finds the cartridge; not in the kernel (a weak symbol left undefined) */
extern void bm_lua_sample(lua_State *L, lua_Debug *ar) __attribute__((weak));

/* the debugger's state (R13; its code is further down, by the questions
 * over the game) */
#define DBG_LINES   65536               /* breakpoints on lines 1..65535 of main.lua */
#define DBG_FRAMES  12
enum { DBG_RUN, DBG_INTO, DBG_OVER, DBG_OUT };

static struct {
    int session;                        /* a game tried from a tool */
    int n;                              /* breakpoints */
    int mode, depth;                    /* the step asked, and the stack's depth then */
    int stop;                           /* stopped in the debugger: the run ends */
    const char *cb;                     /* the callback running (_update...) */
    uint8_t bits[DBG_LINES / 8];
} dbg;


static void hook(lua_State *L, lua_Debug *ar)
{
    if (ar->event == LUA_HOOKLINE) {    /* the debugger's (R13), only in a debug session */
        dbg_line(L, ar);
        return;
    }
    if (bm_lua_sample)
        bm_lua_sample(L, ar);
    if (prof_enabled())
        prof_sample(L);                 /* profile(): the profiler of functions */
    ++rt.hook_count;
    if (loading_active())
        loading_tick();                 /* _init: the loading screen goes on */
    /* a coroutine given to timeslice(): it stops here and goes on next
     * frame, instead of running into the budget */
    if (L == rt.slice_thread && rt.hook_count >= rt.slice_at && lua_isyieldable(L)) {
        rt.slice_at = rt.hook_count + rt.slice_len;
        lua_yield(L, 0);
        return;
    }
    if (rt.hook_count > FRAME_BUDGET)
        luaL_error(L, "cart timeout: more than %d million instructions in one frame",
                   FRAME_BUDGET * HOOK_EVERY / 1000000);
}

/* nano8 reads its carts from the SD card and draws into the frame */
static int n8_file(const char *path, uint8_t **data, size_t *len)
{
    fat_entry_t e;
    if (fat_find(path, &e) != 0 || e.is_dir || e.size > 4u * 1024 * 1024)
        return -1;
    return fat_load(&e, data, len);
}

static g16_t *n8_target(void)
{
    return &rt.g;
}

static lua_State *new_cart_state(const bm_cart_t *c)
{
    static const n8lua_io_t n8io = { n8_file, n8_target };
    n8lua_set_io(&n8io);
    lua_State *L = luavm_newstate();
    if (!L)
        return NULL;
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
        { LUA_COLIBNAME, luaopen_coroutine }, { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL },
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    /* sandbox: no file or code loading from outside the cartridge */
    static const char *const removed[] = { "dofile", "loadfile", "load", NULL };
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(L);
        lua_setglobal(L, *r);
    }
    luaL_newmetatable(L, MESH_MT);
    lua_pushcfunction(L, l_mesh_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    luaL_newmetatable(L, WORLD_MT);
    lua_pushcfunction(L, l_world_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    luaL_newmetatable(L, VIDEO_MT);
    lua_pushcfunction(L, l_clip_gc);
    lua_setfield(L, -2, "__gc");
    lua_newtable(L);
    lua_pushcfunction(L, l_clip_info);
    lua_setfield(L, -2, "info");
    lua_pushcfunction(L, l_clip_frame_at);
    lua_setfield(L, -2, "frame_at");
    lua_pushcfunction(L, l_clip_draw);
    lua_setfield(L, -2, "draw");
    lua_pushcfunction(L, l_clip_close);
    lua_setfield(L, -2, "close");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    lua_pushglobaltable(L);
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);
    luaL_requiref(L, "n8", luaopen_n8, 1);      /* the nano8 machine (carts/nano8) */
    lua_pop(L, 1);
    ai_lua_open(L);             /* the assistant (M30): idle until asked */
    {
        /* its language (R18): assist_lang=it or en fixes it; auto (the
         * default) follows the questions, from the language last used */
        const char *al = config_get("assist_lang");
        if (al && (!strcmp(al, "it") || !strcmp(al, "en")))
            ai_set_lang(!strcmp(al, "en") ? AI_LANG_EN : AI_LANG_IT, 0);
        else if (!ai_lang_follows())
            ai_set_lang(ai_lang(), 1);
    }
    nnet_lua_open(L);           /* small INT8 networks of the carts (M38.4) */
    bm_require_open(L);         /* require "assist": libraries in the kernel */
    au_lua_bank = audio_bank_now;       /* play("lead"): the bank's sounds by name */
    static const char *const waves[] = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL", "FM", "PLUCK",
                                         "SUPERSAW", "ORGAN", "SAMPLE", "PINK", "BROWN", "CRACKLE" };
    _Static_assert(sizeof waves / sizeof waves[0] == SYNTH_WAVES, "a name for every wave");
    for (int w = 0; w < SYNTH_WAVES; w++) {
        lua_pushinteger(L, w);
        lua_setglobal(L, waves[w]);
    }
    lua_pushinteger(L, c->width);
    lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, c->height);
    lua_setglobal(L, "SCREEN_H");
    if (cur.bench) {                    /* a kernel benchmark (bm_next_run) */
        lua_pushinteger(L, cur.bench);
        lua_setglobal(L, "BENCH");
    }
    lua_gc(L, LUA_GCGEN, 0, 0);         /* generational GC: short pauses */
    lua_sethook(L, hook, LUA_MASKCOUNT, HOOK_EVERY);
    return L;
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

/* Calls global `name` if it exists. Returns 0 on success, -1 on error
 * (message left on the stack). */
static int call(lua_State *L, const char *name)
{
    lua_pushcfunction(L, traceback);
    if (lua_getglobal(L, name) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return 0;
    }
    rt.hook_count = 0;
    rt.slice_at = rt.slice_len;
    prof_begin(name);
    dbg.cb = name;
    int r = lua_pcall(L, 0, 0, -2);
    prof_end();
    rt.frame_instr_k += rt.hook_count;
    if (r != LUA_OK) {
        lua_remove(L, -2);
        return -1;
    }
    lua_pop(L, 1);
    return 0;
}

/* ---------------------------------------------------------------- input */

/* ---------------------------------------------------------------- typing */

static void text_push(uint8_t c)
{
    if ((uint8_t)(rt.tq_head + 1) != rt.tq_tail)
        rt.tq[rt.tq_head++] = c;
}

/* serial terminal: ESC [ A..D arrows, ESC [ H / F home and end,
 * ESC O P..S F1..F4, ESC [ n ~ (3 delete, 5/6 page up/down, 15 F5,
 * 17-21 F6-F10, 23-24 F11-F12; ours: 28 Ctrl+Enter, 29 Ctrl+.) */
static void serial_text(char c)
{
    if (rt.esc == 1) {
        if (c == '[') { rt.esc = 2; rt.esc_num = 0; return; }
        if (c == 'O') { rt.esc = 3; return; }
        text_push(0x1B);
        rt.esc = 0;
    }
    if (rt.esc == 3) {
        rt.esc = 0;
        if (c >= 'P' && c <= 'S') text_push((uint8_t)(HID_KEY_F1 + (c - 'P')));
        return;
    }
    if (rt.esc == 2 && c >= '0' && c <= '9') {
        rt.esc_num = rt.esc_num * 10 + (c - '0');
        return;
    }
    if (rt.esc == 2 && c == '~') {
        rt.esc = 0;
        switch (rt.esc_num) {
        case 3: text_push(HID_KEY_DEL); break;
        case 5: text_push(HID_KEY_PGUP); break;
        case 6: text_push(HID_KEY_PGDN); break;
        case 15: text_push(HID_KEY_F1 + 4); break;
        case 17: case 18: case 19: case 20: case 21:
            text_push((uint8_t)(HID_KEY_F6 + rt.esc_num - 17)); break;
        case 23: case 24: text_push((uint8_t)(HID_KEY_F6 + rt.esc_num - 18)); break;
        case 1: text_push(HID_KEY_HOME); break;
        case 4: text_push(HID_KEY_END); break;
        case 28: text_push(HID_KEY_CTRL_ENTER); break;
        case 29: text_push(HID_KEY_CTRL_DOT); break;
        }
        return;
    }
    if (rt.esc == 2) {
        rt.esc = 0;
        switch (c) {
        case 'A': text_push(HID_KEY_UP); return;
        case 'B': text_push(HID_KEY_DOWN); return;
        case 'C': text_push(HID_KEY_RIGHT); return;
        case 'D': text_push(HID_KEY_LEFT); return;
        case 'H': text_push(HID_KEY_HOME); return;
        case 'F': text_push(HID_KEY_END); return;
        }
        return;
    }
    if (c == 0x1B) { rt.esc = 1; return; }
    if (c == 0x1C) { rt.serial_quit = 1; return; }  /* Ctrl+\: Ctrl+Esc on the serial line */
    if (c == '\n') return;                  /* terminals send \r or \r\n */
    if (c == 0x08) c = 0x7F;
    if ((uint8_t)c >= HID_KEY_CTRL_ENTER && (uint8_t)c <= HID_KEY_CTRL_DOT)
        return;                             /* a UTF-8 byte, not one of our keys */
    text_push((uint8_t)c);
}

/* HID_* bits -> btn() bits: 0-5 are the same; X and Y are 6 and 7, or A
 * and B for a cartridge that never asked for them */
uint16_t hid_to_btn(uint32_t pad)
{
    uint16_t b = pad & 0x3F;
    if (pad & HID_START) b |= 1u << BTN_START;
    if (pad & HID_SELECT) b |= 1u << BTN_SELECT;
    if (pad & HID_X) b |= rt.uses_xy ? 1u << BTN_X : 1u << BTN_A;
    if (pad & HID_Y) b |= rt.uses_xy ? 1u << BTN_Y : 1u << BTN_B;
    return b;
}

int poll_keys(void)
{
    for (int k; (k = input_remote_getc()) >= 0; ) {
        char c = (char)k;
        int b = -1;
        if (rt.text_mode) {
            serial_text(c);
            continue;
        }
        if (rt.esc == 1) { rt.esc = c == '[' ? 2 : 0; continue; }
        if (rt.esc == 2) {
            rt.esc = 0;
            b = c == 'A' ? BTN_UP : c == 'B' ? BTN_DOWN : c == 'C' ? BTN_RIGHT : c == 'D' ? BTN_LEFT : -1;
        } else {
            switch (c) {
            case 0x1B: rt.esc = 1; continue;
            case 'a': case 'A': b = BTN_LEFT; break;
            case 'd': case 'D': b = BTN_RIGHT; break;
            case 'w': case 'W': b = BTN_UP; break;
            case 's': case 'S': b = BTN_DOWN; break;
            case ' ': case 'j': case 'J': b = BTN_A; break;
            case 'k': case 'K': case 'x': case 'X': b = BTN_B; break;
            case 'c': case 'C': case 'l': case 'L': b = BTN_X; break;
            case 'v': case 'V': case 'i': case 'I': b = BTN_Y; break;
            case 'u': case 'U': b = SER_L1; break;
            case 'o': case 'O': b = SER_R1; break;
            case '\r': b = BTN_START; break;
            case '\t': b = BTN_SELECT; break;
            case 'p': case 'P': rt.perf_key = 1; continue;
            case 'q': case 'Q': return QUIT_FORCE;
            case 0x1C: rt.serial_quit = 1; continue;    /* Ctrl+\: Ctrl+Esc (asks _exit()) */
            }
        }
        if (b >= 0)
            rt.hold[b] = HOLD_FRAMES;
    }
    /* a lone Esc, not the start of a sequence: after 6 frames of nothing;
     * in a game it is Start, its menu (the system's keys) */
    if (rt.esc == 1 && ++rt.esc_wait >= 6) {
        if (rt.leave_ask)
            rt.leave_no = 1;                /* the question's no */
        else if (rt.text_mode)
            text_push(0x1B);
        else
            rt.hold[BTN_START] = HOLD_FRAMES;
        rt.esc = 0;
    }
    if (rt.esc != 1)
        rt.esc_wait = 0;
    int quit = 0;
    uint32_t per[INPUT_PLAYERS];
    uint32_t pad = input_players(per, rt.text_mode || rt.raw_keys, &quit, &rt.local);
    {                                       /* the system's screenshot: Print Screen, or Select held 3 s on a pad */
        static int shot_held;
        static uint32_t sel_t0;             /* when Select alone went down (0: it is not) */
        static int sel_shot;                /* the shot of this hold is taken: wait for the release */
        int down = hid_usage_held(0x46);
        if (!(pad & HID_SELECT)) {          /* released: a new hold can start */
            sel_t0 = 0;
            sel_shot = 0;
        } else if (pad != HID_SELECT || sel_shot || rt.leave_ask) {
            sel_t0 = 0;                     /* another button, or the shot is taken: the 3 s start again */
        } else {
            if (!sel_t0)
                sel_t0 = timer_ticks() | 1; /* never 0 */
            if (timer_ticks() - sel_t0 >= 3000000u) {
                down = 1;
                sel_shot = 1;
            }
        }
        if (down && !shot_held && !rt.leave_ask)
            shot_plan(NULL);
        shot_held = down;
        if (shot_net == 1) {                /* the PC asked (op 'T' of the transfer port) */
            shot_net = 0;
            if (shot_plan(NULL) != 0)
                shot_res = -1;
            else
                shot_res = 2;
        }
    }
    if (rt.help_open == 1 && (quit & HID_QUIT_ESC)) {   /* Esc closes the keys opened by showkeys() */
        rt.help_open = 2;
        quit &= ~HID_QUIT_ESC;
    }
    if (quit & HID_QUIT_ESC) {              /* Esc in a game: Start, its menu */
        if (rt.leave_ask)
            rt.leave_no = 1;
        else
            rt.hold[BTN_START] = HOLD_FRAMES;
        quit &= ~HID_QUIT_ESC;
    }
    if (rt.text_mode)
        for (int k; (k = hid_getc()) >= 0;) {
            /* the system's: F11 and F12 never reach the cartridge; while F12
             * is held the arrows turn the pages of its keys */
            if (rt.help_open == 1 && (k == HID_KEY_F6 + 6 || k == 27)) {
                rt.help_open = 2;           /* F12 or Esc closes the keys opened by showkeys() */
                continue;
            }
            if (k == HID_KEY_F6 + 5 || k == HID_KEY_F6 + 6 || rt.leave_ask)
                continue;                   /* (and the keys of the leave question) */
            if ((hid_usage_held(0x45) || rt.help_open == 1) && (k == HID_KEY_DOWN || k == HID_KEY_UP || k == HID_KEY_PGDN ||
                                         k == HID_KEY_PGUP || k == HID_KEY_RIGHT || k == HID_KEY_LEFT)) {
                rt.help_page += k == HID_KEY_DOWN || k == HID_KEY_PGDN || k == HID_KEY_RIGHT ? 1 : -1;
                continue;
            }
            if (rt.help_open == 1)
                continue;                   /* the open keys take the other keys */
            text_push((uint8_t)k);
        }
    uint32_t serial = 0;                    /* HID_* bits of the serial keys held */
    for (int b = 0; b < SER_COUNT; b++)
        if (rt.hold[b]) {
            serial |= b == BTN_X ? HID_X : b == BTN_Y ? HID_Y : b == BTN_START ? HID_START :
                      b == BTN_SELECT ? HID_SELECT : b == SER_L1 ? HID_L1 : b == SER_R1 ? HID_R1 : 1u << b;
            rt.hold[b]--;
        }
    rt.ptr = *pointer_update();             /* also when unused: the motion is dropped */
    if (rt.help_open == 1 && rt.ptr.pressed)
        rt.help_open = 2;                   /* a click closes the keys opened by showkeys() */
    if (rt.help_open == 2 && !rt.ptr.buttons)
        rt.help_open = 0;                   /* (the cartridge sees no button until the click is up) */
    rt.prev = rt.now;
    rt.raw_all = pad | serial;
    rt.now = hid_to_btn(pad | serial);
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        if (p == rt.local)
            per[p] |= serial;
        rt.praw_prev[p] = rt.praw[p];
        rt.praw[p] = per[p];
        rt.pprev[p] = rt.pnow[p];
        rt.pnow[p] = hid_to_btn(per[p]);
    }
    if (rt.serial_quit) {
        rt.serial_quit = 0;
        quit |= HID_QUIT_PS;
    }
    return quit;
}

/* Before a second _update in the same frame (frameskip()): the buttons
 * held stay held, but what was pressed this frame (btnp, mousep, the
 * wheel) counts once */
static void input_repeat(void)
{
    rt.prev = rt.now;
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        rt.pprev[p] = rt.pnow[p];
        rt.praw_prev[p] = rt.praw[p];
    }
    rt.ptr.pressed = rt.ptr.released = 0;
    rt.ptr.wheel = rt.ptr.pan = rt.ptr.moved = 0;
}

/* ---------------------------------------------------------------- loading */

static void sheet8_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    (void)ctx;
    static unsigned n;
    if ((++n & 8191) == 0)
        loading_tick();                 /* a big sheet takes a while */
    g16_sheet_set(&rt.sheet, x, y, g16_rgb(rgba[0], rgba[1], rgba[2]), rgba[3] >= 128);
}

int load_assets(const bm_cart_t *c)
{
    int has = c->sheet_rgba || c->sheet8;
    int sw = has ? c->sheet_w : 256, sh = has ? c->sheet_h : 256;
    if (g16_sheet_alloc(&rt.sheet, sw, sh) != 0)
        return -1;
    int cells = (rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL);
    rt.cell_dirty = calloc((size_t)cells, 1);
    if (!rt.cell_dirty)
        return -1;
    if (has) {
        if (c->sheet8) {
            if (bm_sheet8_unpack(c, sheet8_set, NULL) != 0)
                return -1;
        } else {
            for (int y = 0; y < c->sheet_h; y++)
                for (int x = 0; x < c->sheet_w; x++) {
                    const uint8_t *p = c->sheet_rgba + ((uint32_t)y * c->sheet_w + x) * 4;
                    g16_sheet_set(&rt.sheet, x, y, g16_rgb(p[0], p[1], p[2]), p[3] >= 128);
                }
        }
        for (int i = 0; i < cells; i++)
            rt.cell_dirty[i] = 1;
        rt.sheet_dirty = 1;
        sheet_commit();
    }

    if (c->mesh) {
        rt.mesh = malloc(c->mesh_size);
        if (!rt.mesh)
            return -1;
        memcpy(rt.mesh, c->mesh, c->mesh_size);
        rt.mesh_size = c->mesh_size;
    }
    if (c->anim) {
        rt.anim = malloc(c->anim_size);
        if (!rt.anim)
            return -1;
        memcpy(rt.anim, c->anim, c->anim_size);
        rt.anim_size = c->anim_size;
    }

    rt.map.w = c->map_cells ? c->map_w : 256;
    rt.map.h = c->map_cells ? c->map_h : 256;
    rt.nlayers = c->map_cells ? c->nlayers : 1;
    for (int l = 0; l < rt.nlayers; l++) {
        const uint8_t *src = NULL;
        if (c->map_cells)
            bm_layer(c, l, rt.layer_name[l], &src);
        else
            strcpy(rt.layer_name[l], "main");
        rt.layer[l] = calloc((size_t)rt.map.w * rt.map.h, 2);
        if (!rt.layer[l])
            return -1;
        for (uint32_t i = 0; src && i < (uint32_t)rt.map.w * rt.map.h; i++)
            rt.layer[l][i] = (uint16_t)(src[i * 2] | src[i * 2 + 1] << 8);
    }
    rt.map.cells = rt.layer[0];

    /* the flags by the cell's place: a sheet grown since keeps them */
    const int per = rt.sheet.w / G16_CELL;
    rt.flags = calloc((size_t)cells, 1);
    if (!rt.flags)
        return -1;
    for (int i = 0; c->flags && i < cells; i++)
        rt.flags[i] = bm_cell_flags(c, i % per, i / per);
    rt.flags_dirty = 0;

    if (c->sprites && c->zones) {
        uint32_t n = 4u + c->zones * BM_SPRITE_SIZE;
        if (!(rt.zones = malloc(n)))
            return -1;
        memcpy(rt.zones, c->sprites, n);
        rt.nzones = c->zones;
    }
    if (c->boxes && c->nboxes) {
        uint32_t n = 4u + c->nboxes * BM_BOX_SIZE;
        if (!(rt.boxes = malloc(n)))
            return -1;
        memcpy(rt.boxes, c->boxes, n);
        rt.nboxes = c->nboxes;
    }
    return 0;
}

void free_assets(void)
{
    g16_sheet_free(&rt.sheet);
    free(rt.cell_dirty);
    for (int l = 0; l < BM_LAYERS_MAX; l++) {
        free(rt.layer[l]);
        rt.layer[l] = NULL;
    }
    rt.nlayers = 0;
    free(rt.flags);
    rt.flags = NULL;
    free(rt.zones);
    rt.zones = NULL;
    rt.nzones = 0;
    free(rt.boxes);
    rt.boxes = NULL;
    rt.nboxes = 0;
    free(rt.mesh);
    rt.cell_dirty = NULL;
    rt.map.cells = NULL;
    rt.mesh = NULL;
    rt.mesh_size = 0;
    free(rt.anim);
    rt.anim = NULL;
    rt.anim_size = 0;
    free(rt.pvs);
    rt.pvs = NULL;
}

/* ---------------------------------------------------------------- project state (the editor API is in api_editor.c) */

/* The project open in the editor, the sound bank of the cartridge playing
 * and the options of the next cart_run are shared with the player: they are
 * defined here (rt_internal.h). */
uint8_t *proj_cover;
uint8_t *proj_audio, *own_audio;
uint32_t proj_audio_len, own_audio_len;
uint8_t *proj_sheet8;
uint32_t proj_sheet8_len;
int n_made;
char last_error[512];
dbg_next_t dbg_next;

/* The names under which the kernel runs an SD file as one of bm's tools, with a tool's rights (devtool.c
 * load_sd_tool, carts.c tool_id_of): a game must not create a file called so (it would be run as the tool,
 * with write access everywhere and no permission questions, the next time the tool is opened) */
int reserved_tool_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    const size_t k = (size_t)(dot - name);
    for (size_t i = 0; i < BM_TOOLNAMES; i++)      /* kernel/toolnames.h: the one list */
        if (strlen(bm_toolnames[i].stem) == k && strncasecmp(name, bm_toolnames[i].stem, k) == 0)
            return 1;
    return 0;
}

/* The resolutions of a cartridge, as the Lua side names them */
static const char *res_name(int w)
{
    return w == 320 ? "320x180" : w == 256 ? "256x256" : w == 360 ? "360x360" : w == 480 ? "480x270"
         : w == 720 ? "720x720" : "640x360";
}

const char *rt_res_name(int w)
{
    return res_name(w);
}

/* ---------------------------------------------------------------- player */

/* Two ways to draw a frame:
 * - direct: into the framebuffer's back page (uncached GPU memory; writes
 *   only, pget() is slow);
 * - via RAM: into a cached buffer ("shadow") copied to the back page once
 *   per frame. On the ARM1176 the copy has to read the buffer back from
 *   SDRAM (16 KB data cache, reads ~4x slower than writes), so it is not
 *   obviously a win: bm_bench() measures both. Default: direct. */
static int via_ram;
static int dma_frames;          /* copy by DMA: off until the DMA test passes on the Pi */
static uint16_t *shadow;

void bm_set_via_ram(int on) { via_ram = on; }

/* Switches a running cartridge to the RAM buffer (keeps clip and camera). */
int video_to_ram(g16_t *g)
{
    if (shadow)
        return 0;
    shadow = malloc((size_t)g->w * (size_t)g->h * 2);
    if (!shadow)
        return -1;
    memset(shadow, 0, (size_t)g->w * (size_t)g->h * 2);
    g16_t keep = *g;
    g16_target(g, shadow, (uint32_t)g->w, g->w, g->h, keep.font);
    g->cx0 = keep.cx0; g->cy0 = keep.cy0; g->cx1 = keep.cx1; g->cy1 = keep.cy1;
    g->cam_x = keep.cam_x; g->cam_y = keep.cam_y;
    return 0;
}
int bm_via_ram(void) { return via_ram; }
int bm_video_uses_ram(void) { return shadow != NULL; }

/* A square 256x256 cartridge is drawn in the middle of a 480x270 screen (a 360x360 one in a 640x360)
 * (1920x1080 / 4: whole pixels on a 1080p TV), black around it: `box` is
 * the byte offset of its top left corner in a page, 0 for the 16:9 sizes. */
static uint32_t box;

static uint16_t *page_px(const framebuffer_t *fb)
{
    return (uint16_t *)(fb->base + box);
}

/* The console (the monitor) is hidden for a game and comes back as it was: a game
 * started from the menu leaves the menu to draw itself, not the log flashing for
 * the frames between (2026-10-10). A mode changed in the middle of a game
 * (screen()) does not save it again. */
static int con_in_video, con_was;

int bm_video_enter(framebuffer_t *fb, int w, int h, g16_t *g)
{
    if (!con_in_video) {
        con_in_video = 1;
        con_was = console_suspended();
    }
    console_suspend(1);
    free(shadow);
    shadow = NULL;
#ifdef BM_RGB30
    /* the cartridge's own size, made as big as the panel by the display
     * controller (rgb30/display.h): no box around a square screen */
    const int boxed = 0;
    box = 0;
    if (fb_init_game(fb, w, h) != 0)
        return -1;
#else
    /* a square cartridge sits in the middle of a 16:9 screen with black around it:
     * 256x256 in 480x270, the .b16's 360x360 in 640x360, a 720x720 in 1280x720 (whole on a square
     * panel: bmhost --square) */
    const int boxed = !square_panel && w == h && h <= 720;
    const uint32_t bh = h <= 270 ? 270u : h <= 360 ? 360u : 720u;
    const uint32_t fw = boxed ? bh * 16 / 9 : (uint32_t)w, fh = boxed ? bh : (uint32_t)h;
    box = 0;
    int fb_err = fb_init_depth(fb, fw, fh, 3, 16);
    if (fb_err != 0) {
        kprintf("bm_video_enter: fb_init_depth(%lu,%lu,3,16) failed: %d\n",
                (unsigned long)fw, (unsigned long)fh, fb_err);
        return -1;
    }
#endif
    gpu3d_set_fb(fb->mem, fb->size, fb->bus);   /* its pages, for the GPU's 3D */
    gpu3d_set_size((int)fb->width, (int)fb->height);
    if (boxed) {
        if (fb->width < (uint32_t)w || fb->height < (uint32_t)h)
            return -1;
        box = (fb->height - (uint32_t)h) / 2 * fb->pitch + (fb->width - (uint32_t)w) / 2 * 2;
        memset(fb->mem, 0, (size_t)fb->pitch * fb->height * fb->buffers);
    }
    if (via_ram) {
        shadow = malloc((size_t)w * (size_t)h * 2);
        if (!shadow)
            return -1;
        memset(shadow, 0, (size_t)w * (size_t)h * 2);
        g16_target(g, shadow, (uint32_t)w, w, h, &font_console_8x16);
    } else {
        g16_target(g, page_px(fb), fb->pitch / 2, w, h, &font_console_8x16);
    }
    return 0;
}

uint32_t bm_video_present(framebuffer_t *fb, g16_t *g)
{
    if (!shadow) {
        if (crt_filter_is_active())
            crt_apply_filter(page_px(fb), g->w, g->h, fb->pitch);
        if (rt.mouse && rt.mouse_arrow)         /* on the page about to be shown */
            pointer_draw(page_px(fb), fb->pitch / 2, g->w, g->h);
        fb_flip(fb);
        g->px = page_px(fb);
        return 0;
    }
    uint32_t t0 = timer_ticks();
    const uint32_t row = (uint32_t)g->w * 2;
    uint8_t *dst = fb->base;
    if (box) {
        dst = fb->base + box;
        for (int y = 0; y < g->h; y++)
            memcpy(dst + (uint32_t)y * fb->pitch, g->px + (uint32_t)y * g->stride, row);
    } else if (fb->pitch == row && dma_ready() && dma_frames) {
        /* the DMA reads the SDRAM much faster than the ARM1176 */
        dcache_clean_all();
        dma_copy(fb->base, g->px, row * (uint32_t)g->h);
        dma_wait();
    } else if (fb->pitch == row) {
        memcpy(fb->base, g->px, row * (uint32_t)g->h);
    } else {
        for (int y = 0; y < g->h; y++)
            memcpy(fb->base + (uint32_t)y * fb->pitch, g->px + (uint32_t)y * g->stride, row);
    }
    if (crt_filter_is_active())
        crt_apply_filter(dst, g->w, g->h, fb->pitch);
    uint32_t us = timer_ticks() - t0;
    if (rt.mouse && rt.mouse_arrow)             /* over the copy: never in the cartridge's buffer */
        pointer_draw(page_px(fb), fb->pitch / 2, g->w, g->h);
    fb_flip(fb);
    return us;
}

void bm_video_leave(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    free(shadow);
    shadow = NULL;
    box = 0;
    fb_init(fb, w, h, 2);
    /* hidden still, if the menu had it so: it draws itself next. A rendering test
     * stopped (Start+Select, Ctrl+Esc, PS: syskeys.h) goes straight back to the
     * menu, as a game does: not the log flashing between; the monitor shows
     * itself again at its prompt (monitor_run) */
    console_suspend(con_was || syskeys_test_stopped());
    con_in_video = 0;
}

static int enter_mode(framebuffer_t *fb, int w, int h)
{
    return bm_video_enter(fb, w, h, &rt.g);
}

static void leave_mode(framebuffer_t *fb, uint32_t w, uint32_t h)
{
    pointer_env(0, 0, 0);
    bm_video_leave(fb, w, h);
    input_flush();              /* keys typed in the game stay in the game */
}

/* after a frame went to the screen: the wait for its time (60 a second) */
static void pace(uint32_t *deadline, uint32_t *prev, uint32_t *dropped)
{
    zclear_dma_start();
    rec_pump(*deadline);                /* the recorder writes in the time the frame has left */
    while ((int32_t)(timer_ticks() - *deadline) < 0)
        ;
    uint32_t now = timer_ticks();
    if (dropped && now - *prev > FRAME_US * 3 / 2)
        (*dropped)++;
    *prev = now;
    *deadline += FRAME_US;
    if ((int32_t)(now - *deadline) > 0)
        *deadline = now + FRAME_US;
}


/* The frame drawn, before the overlays, saved if a screenshot was asked for: a PNG in /bm/shots */
static void shot_grab(void)
{
    if (rec_due()) {                    /* the recorder takes the frame as it is, with the screenshot */
        flush3d(0);
        cls_settle();
        rec_frame(rt.g.px, rt.g.stride, rt.g.w, rt.g.h);
    }
    if (!shot_req)
        return;
    shot_req = 0;
    rec_note_game_io();
    flush3d(0);                         /* its 3D done, as present() does */
    cls_settle();
    size_t n = 0;
    uint8_t *png = shot_png(rt.g.px, (int)rt.g.stride, rt.g.w, rt.g.h, &n);
    const int ok = png && fat_mkdirs(SHOT_DIR) == 0 && fat_write_file(SHOT_DIR, shot_file, png, n) == 0;
    free(png);
    if (shot_res == 2) {                /* answers a bm_shot_ask() that waits (a shot made by a key leaves it alone) */
        shot_res = ok ? 1 : -1;
        memcpy(shot_net_file, shot_file, sizeof shot_net_file);
    }
    if (ok) {
        ksnprintf(shot_line[0], sizeof shot_line[0], "Screenshot saved");
        ksnprintf(shot_line[1], sizeof shot_line[1], "bm/shots/%s", shot_file);
    } else {
        ksnprintf(shot_line[0], sizeof shot_line[0], "Screenshot not saved");
        ksnprintf(shot_line[1], sizeof shot_line[1], "%s", png ? fat_error() : "not enough memory");
    }
    kprintf("bm: %s: %s\n", shot_line[0], shot_line[1]);
    shot_note = 120;
}

static void present(framebuffer_t *fb, uint32_t *deadline, uint32_t *prev, uint32_t *dropped)
{
    flush3d(0);
    cls_settle();
    if (rt.hold_frame) {
        rt.hold_frame = 0;
        rt.present_us = 0;
    } else {
        rt.present_us = bm_video_present(fb, &rt.g);
    }
    d2_early(0);                        /* what the early _update drew: this page's */
    if (rt.r3d.backend)
        gpu3d_page(0, 0);               /* another page: what it holds is not known */
    perf_end();                         /* the dev kit: the frame's work ends here */
    pace(deadline, prev, dropped);
}

/* bm3d 6.9: the frame held (later_hold) on the screen: its job waited for, its
 * 2D on its page, with the overlays drawn after a frame (the dev kit's,
 * the battery, the leave question, the notice, F12's keys) when L is
 * given, and its page shown. Then nothing is held. Its page is the
 * framebuffer's next (later_hold leaves it there): no copy, a flip. */

static void later_show(lua_State *L)
{
    if (!held.on)
        return;
    later_wait();
    later_paint();
    framebuffer_t *fb = held.fb;
    held.on = 0;
    rt.present_us = 0;
    if (page_px(fb) != held.px) {       /* (should not be: the pages turned without it) */
        kprintf("bm: the frame held was not the next page to show\n");
        fb->base = (uint8_t *)held.px - box;    /* shown all the same; the one drawn is the next */
    }
    const g16_t live = rt.g;            /* the page being drawn (or the buffer in RAM) */
    rt.g.px = held.px;
    shot_grab();                        /* (the frame as it is, without the overlays) */
    if (L) {
        perf_frame();
        battery_draw();
        leave_draw();
        notice_draw();
        shot_note_draw();
        keys_help(L);
    }
    if (rt.mouse && rt.mouse_arrow)
        pointer_draw(held.px, fb->pitch / 2, rt.g.w, rt.g.h);
    fb_flip(fb);
    rt.g = live;
}

/* the frame held shown now, before the screen changes hands (a question of
 * the system, the debugger, the end of the run) */
void later_flush(void)
{
    later_show(NULL);
}

/* bm3d 6.9: this frame is shown a frame later: the option asked for (gpu3d_later,
 * gpu3d()), the GPU drawing the 3D with two jobs in flight, straight on the
 * screen's pages (three of them, whole: not via RAM nor a square inside the
 * screen), not online (lockstep: the option is left out), no screen() to
 * apply, the frame to be shown */
static int later_on(const framebuffer_t *fb)
{
    return rt.later && rt.r3d_ready && rt.r3d.backend && gpu3d_queue2() && !bm_video_uses_ram() && !box &&
           fb->buffers >= 3 && rt.g.stride == (uint32_t)rt.g.w && !rt.online && !rt.want_w && !rt.hold_frame;
}

/* the frame's 3D started on the GPU and not waited for; 0 if the GPU
 * failed (the ARM draws the 3D again) */
static int later_submit(void)
{
    if (gpu3d_submit(&rt.g, 0) != 0 || gpu3d_failed()) {
        rt.r3d.backend = NULL;
        return 0;
    }
    return 1;
}

/* the frame drawn is held: its page (the framebuffer's next to show) and
 * the 2D recorded for it; the next frame is drawn on the page after */
static void later_hold(framebuffer_t *fb)
{
    held.fb = fb;
    held.px = rt.g.px;
    int32_t *w = held.w;
    const uint32_t cap = held.cap;
    held.w = d2.w;                      /* the records go with their page */
    held.cap = d2.cap;
    held.n = d2.n;
    d2.w = w;
    d2.cap = cap;
    d2.n = 0;
    d2.on = 0;
    d2.font = NULL;
    d2.cut = D2_NONE;
    held.on = 1;
    if (!held.frames++)
        kprintf("bm: each frame shown a frame later (gpu3d_later): the next _draw runs while the GPU draws it\n");
    const uint32_t page = fb->height * fb->pitch;
    const uint32_t i = (uint32_t)((const uint8_t *)held.px - box - fb->mem) / page;
    rt.g.px = (uint16_t *)(fb->mem + (i + 1) % fb->buffers * page + box);
    gpu3d_page(0, 0);                   /* another page: what it holds is not known */
}

/* The buttons that left a cartridge (Start+Select, a game's own "quit" on A...)
 * let go before the menu comes back. At most 3 s. */
static void wait_buttons_up(void)
{
    const uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 3000000) {
        uint32_t per[INPUT_PLAYERS];
        int quit = 0, local;
        uint32_t held = input_players(per, rt.text_mode || rt.raw_keys, &quit, &local);
        for (int p = 0; p < INPUT_PLAYERS; p++)
            held |= per[p];
        if (!held)
            break;
        timer_delay_ms(5);
    }
}

/* A cartridge left with Esc / PS / Start+Select (when the caller allows
 * it) stays in memory, frozen: its Lua state, sheet, map, 3D and lights
 * stay as they are; bm_resume() continues from the same frame. */
static struct {
    lua_State *L;
    int active;
    char title[49];
    int w, h;
    g16_t g;                    /* clip and camera at the moment it stopped */
    int used_ram;               /* it was drawing via RAM (lights) */
    uint32_t since;
} susp;

/* Frees what a cartridge holds (the screen may still be in its mode: a
 * closed one is freed with its last frame still shown, then leave_mode). */
static void release(lua_State *L)
{
    cartnet_reset();            /* the cartridge's sockets */
    web_reset();                /* its web requests and their results */
    audio_reset();
    audio_bank(NULL, 0, NULL, 0);
    set_copy(&own_audio, &own_audio_len, NULL, 0);
    lua_close(L);               /* frees meshes (__gc) before the z-buffer */
    n8lua_close();
    if (rt.r3d.backend)
        gpu3d_drop();           /* an error in the middle of a frame */
    rt.r3d.backend = NULL;
    free(d2.w);                 /* 2D recorded in the middle of a frame (bm3d 4.1) */
    d2.w = NULL;
    d2.n = d2.cap = d2.ops = 0;
    d2.on = 0;
    d2.cut = D2_NONE;
    free(held.w);               /* ... and for a frame held (bm3d 6.9) */
    held.w = NULL;
    held.n = held.cap = 0;
    held.on = 0;
    g16_light_free(&rt.light);
    zclear_dma_wait(1);         /* the DMA may still be clearing it */
    rt.zclear_seen = 0;
    g16_fade_free(&rt.fade);
    if (rt.pfx) {
        r3d_pfx_free(rt.pfx);
        rt.pfx = NULL;
    }
    if (rt.r3d_ready)
        r3d_free(&rt.r3d);
    rt.r3d_ready = 0;
    rt.gpu3d_boxed = 0;
    free_assets();
}

static int vol_start;                   /* the volume when the cartridge started or resumed */

/* ---------------------------------------------------------------- the dev kit */

/* The performance overlay, over any game (Settings > Performance overlay,
 * F11 on a keyboard, a system key, 'p' on the serial line, devkit() from the
 * game): F11 once at the top right, again at the bottom right, again off.
 * Off by default. One small block on the 8x16 grid, bigger on the big
 * screens (x2 from 1280 wide, x3 at 1920):
 *   60fps 8.2ms
 *   cpu [=====---]  49%
 *   gpu [===-----]  31%
 * The frames a second and the ms of work of a frame; then the CPU's frame
 * in its parts, each a colour: _update, _draw without its 3D (and the
 * page's clear and copy), the 3D calls (stat(6) without the GPU wait), the
 * audio interrupt, the wait for the GPU (darker), the rest free; the GPU's
 * busy time (its row only while the GPU draws the 3D). The % are of the
 * 16.7 ms of a frame at 60 Hz, not of the frame, the means of the last
 * second; their colour: green under 50%, yellow to 80%, red over (and red
 * when a frame was skipped). Measured with a few timestamps a frame (perf_*
 * below); off, nothing (but the session report's, when it is on). */
#define PERF_MODES 3                    /* off, top, bottom */
static int perf_on;                     /* 0 off, 1 top, 2 bottom: now */
static int perf_user;                   /* the same in Settings (config perf): each run starts with it */

/* KB of the cartridge's data in memory, besides its Lua: the sprite sheet
 * (RGB565 + alpha + the cells), the map, the models and skeletons, the sound
 * bank and the z-buffer of the 3D (stat(13)) */
uint32_t assets_kb(void)
{
    size_t b = (size_t)rt.sheet.w * rt.sheet.h * 3;
    b += (size_t)(rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL) * 2;
    b += (size_t)rt.map.w * rt.map.h * 2;
    b += rt.mesh_size + rt.anim_size + own_audio_len;
    if (rt.r3d_ready && rt.r3d.zbuf)
        b += (size_t)rt.g.w * rt.g.h * 2;
    return (uint32_t)((b + 1023) / 1024);
}

/* 0 off, 1 top, 2 bottom; 3 (the old functions page) and more: the top */
static int perf_level(int level) { return level <= 0 ? 0 : level == 2 ? 2 : 1; }

/* Settings' choice: every game and tool starts (or resumes) with it. F11,
 * 'p' and devkit() change the overlay of the run only: one game that turned
 * it on (Overbit's Select) left it on in the next ones (2026-10-05). */
void bm_set_perf(int level) { perf_on = perf_user = perf_level(level); }
int bm_perf(void) { return perf_user; }

/* devkit([mode]) -> the dev kit's overlay: 0 off, 1 top right, 2 bottom
 * right (3, the old functions page: the top); a mode shows it in this run
 * (a game's own key for it, e.g. Select on a pad: F11 is the keyboard's) */
int l_devkit(lua_State *L)
{
    const int old = perf_on;
    if (!lua_isnoneornil(L, 1))
        perf_on = perf_level(ival(L, 1));
    lua_pushinteger(L, old);
    return 1;
}

/* devinfo(line, ...): up to 4 lines of the game in the session report
 * (its quality, its actors...), 18 characters each; devinfo() none */
int l_devinfo(lua_State *L)
{
    int n = lua_gettop(L);
    if (n > 4)
        n = 4;
    rt.ndevinfo = 0;
    for (int i = 1; i <= n; i++) {
        const char *t = luaL_tolstring(L, i, NULL);
        ksnprintf(rt.devinfo[rt.ndevinfo++], sizeof rt.devinfo[0], "%s", t);
        lua_pop(L, 1);
    }
    return 0;
}

/* The profiler of functions on while the game asked (profile(true)); from
 * the frame loop */
static void prof_update(void)
{
    const int want = rt.prof_want;
    if (want != prof_enabled())
        prof_enable(want, timer_ticks);
}

/* profile([on]) -> the functions that cost most in the last whole second
 * (60 frames), the costliest first: {name, where ("main.lua:120", "[C]"),
 * c (a C function), self and total (ms a frame: its own time, and with the
 * functions it calls), calls (a frame, C functions only)}, and the frames
 * measured (0 while the first second goes by). true starts the profiler
 * of functions for this run (the session report then lists the ten
 * costliest), false stops it. */
int l_profile(lua_State *L)
{
    if (lua_isboolean(L, 1)) {
        rt.prof_want = lua_toboolean(L, 1);
        prof_update();
    }
    prof_row_t r[32];
    uint32_t frames = 0;
    const int n = prof_rows(r, 32, &frames);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 6);
        lua_pushstring(L, r[i].name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, r[i].where);
        lua_setfield(L, -2, "where");
        lua_pushboolean(L, r[i].c);
        lua_setfield(L, -2, "c");
        lua_pushnumber(L, r[i].self_us / 1000.0);
        lua_setfield(L, -2, "self");
        lua_pushnumber(L, r[i].total_us / 1000.0);
        lua_setfield(L, -2, "total");
        lua_pushinteger(L, (lua_Integer)r[i].calls);
        lua_setfield(L, -2, "calls");
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, (lua_Integer)frames);
    return 2;
}

/* ---------------------------------------------------------------- F12: the keys */

/* Ctrl+Esc, PS or Start+Select: the cartridge may ask first (_exit(): true
 * leaves now; false: it shows its own question, and quit() leaves later) */
static int exit_ok(lua_State *L)
{
    if (lua_getglobal(L, "_exit") != LUA_TFUNCTION) {
        lua_pop(L, 1);
        return 1;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        kprintf("bm: _exit: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 1;
    }
    const int ok = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ok;
}

/* PS, Ctrl+Esc or Start+Select in a game played online (online(true),
 * 2026-10-04): not suspended (the others play on) but a question to the
 * player leaving, on this console only, over the game that goes on; while
 * it is open the game sees none of the buttons. Yes calls the cartridge's
 * _leave() (it tells the server) and ends the game. 1: leave now. */
static int leave_step(lua_State *L, int q)
{
    uint32_t held = rt.raw_all;
    for (int p = 0; p < INPUT_PLAYERS; p++)
        held |= rt.praw[p];
    /* the keyboard's too (rawkeys() games): Enter, Space yes; Esc, Backspace no */
    static const uint8_t usage[4] = { 0x28, 0x2C, 0x29, 0x2A };
    uint8_t keys = 0;
    for (int i = 0; i < 4; i++)
        if (hid_usage_held(usage[i]))
            keys |= (uint8_t)(1u << i);
    const int was = rt.leave_ask;
    int answer = 0;
    if (q & QUIT_FORCE) {
        answer = 1;
    } else if (rt.leave_ask) {
        const uint32_t hit = held & ~rt.leave_pad;
        const uint8_t khit = keys & ~rt.leave_keys;
        if (q || (hit & input_ok_bit(0)) || (khit & 3))     /* PS again: yes */
            answer = 1;
        else if (rt.leave_no || (hit & input_ok_bit(1)) || (khit & 12))
            answer = -1;
    } else if (q) {
        if (!rt.online)
            return exit_ok(L);
        rt.leave_ask = 1;
        kprintf("bm: leave the online game? (ok: yes, back: no)\n");
    }
    rt.leave_no = 0;
    rt.leave_pad = held;
    rt.leave_keys = keys;
    if (answer > 0) {
        if (rt.online) {
            rt.online_left = 1;
            if (lua_getglobal(L, "_leave") == LUA_TFUNCTION) {
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    kprintf("bm: _leave: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
            kprintf("bm: left the online game\n");
        }
        rt.leave_ask = 0;
        return 1;
    }
    if (answer < 0) {
        rt.leave_ask = 0;
        kprintf("bm: stays in the online game\n");
    }
    /* the buttons the question takes: none for the game while it is open
     * (the frame of the answer too), then the ones still held until they
     * are released */
    rt.leave_held = was || rt.leave_ask ? held : rt.leave_held & held;
    if (rt.leave_held) {
        const uint32_t m = rt.leave_held;
        rt.now = hid_to_btn(rt.raw_all & ~m);
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            rt.praw[p] &= ~m;
            rt.pnow[p] = hid_to_btn(rt.praw[p]);
        }
    }
    return 0;
}

/* A box over the frame (after the overlay, before F12's keys): a title
 * (orange), lines, the last one dim (a note), and either the yes / back
 * hints of a question (their words: "Leave" "Stay"...) or a progress bar
 * (0..1000; -1 none) */
const char *box_yes = "Leave", *box_no = "Stay";
void sys_box(const char *const *lines, int n, int dim_last, int hints, int progress)
{
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    const int small = g->h < 270, sc = g->h >= 540 ? g->h / 270 : 1;
    g->font = small ? &font_console_6x12 : &font_console_8x16;
    const int fw = g->font->width * sc, rowh = g->font->height * sc;
    /* the answers: the system's yes and back on the controller used last,
     * Enter and Esc on a keyboard */
    const prompt_t *yes = NULL, *no = NULL;
    if (hints) {
        const int dev = input_device(rt.local >= 0 && rt.local < INPUT_PLAYERS ? rt.local : 0);
        const int kb = hid_last_source() == HID_SOURCE_KEYBOARD || (dev & INPUT_DEV_KIND) == INPUT_DEV_KEYBOARD;
        yes = kb ? find_prompt("enter", small) : button_chip(button_real(BUTTON_OK), dev, small);
        no = kb ? find_prompt("esc", small) : button_chip(button_real(BUTTON_BACK), dev, small);
    }
    int cols = 0;
    for (int i = 0; i < n; i++) {
        const int len = (int)strlen(lines[i]);
        cols = len > cols ? len : cols;
    }
    const int maxcols = (g->w - 4 * fw) / fw;
    cols = cols > maxcols ? maxcols : cols;
    /* on the font's grid: the title, a row, the lines, a row, the hints */
    const int pw = (cols + 4) * fw, ph = (n + (hints || progress >= 0 ? 5 : 3)) * rowh;
    const int px = (g->w - pw) / 2 / fw * fw, py = (g->h - ph) / 2 / rowh * rowh;
    const uint16_t ink = g16_rgb(232, 232, 236), dim = g16_rgb(150, 150, 165), head = g16_rgb(255, 176, 64);
    g16_rectfill(g, px, py, pw, ph, g16_rgb(12, 14, 22));
    g16_rect(g, px, py, pw, ph, head);
    for (int i = 0; i < n; i++) {
        char buf[64];
        ksnprintf(buf, sizeof buf, "%s", lines[i]);
        if ((int)strlen(buf) > cols)
            buf[cols] = 0;
        const int y = py + (i ? i + 2 : 1) * rowh;
        g16_text_scaled(g, px + 2 * fw, y, buf, i == 0 ? head : dim_last && i == n - 1 ? dim : ink, sc);
    }
    const int y = py + (n + 3) * rowh;
    if (progress >= 0) {
        const int bw = pw - 4 * fw, fill = bw * (progress > 1000 ? 1000 : progress) / 1000;
        g16_rectfill(g, px + 2 * fw, y + rowh / 4, bw, rowh / 2, g16_rgb(58, 58, 70));
        g16_rectfill(g, px + 2 * fw, y + rowh / 4, fill, rowh / 2, head);
    }
    /* the hints, on the font's grid after each chip */
    int x = px + 2 * fw;
    for (int i = 0; hints && i < 2; i++) {
        const prompt_t *c = i ? no : yes;
        if (c) {
            draw_prompt(c, x, y, sc);
            x += c->w * sc + fw / 2;
            x = px + (x - px + fw - 1) / fw * fw;
        }
        x = g16_text_scaled(g, x, y, i ? box_no : box_yes, ink, sc) + 2 * fw;
    }
    *g = keep;
}

/* the question to the player leaving an online game */
void leave_draw(void)
{
    if (!rt.leave_ask)
        return;
    const char *lines[4] = { "Leave the match?", "You will leave the game and", "disconnect from the server.",
                             rt.online_note[0] ? rt.online_note : NULL };
    box_yes = "Leave";
    box_no = "Stay";
    sys_box(lines, lines[3] ? 4 : 3, lines[3] != NULL, 1, -1);
}

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
static const char *cart_src;            /* main.lua of the run, and its size */
static size_t cart_src_len;

static void dbg_hook(lua_State *L)
{
    const int line = dbg.n > 0 || dbg.mode != DBG_RUN;
    lua_sethook(L, hook, LUA_MASKCOUNT | (line ? LUA_MASKLINE : 0), HOOK_EVERY);
}

/* the start of a run: the options cart_run left for it (not for a tool) */
static void dbg_begin(lua_State *L, const char *src, size_t len)
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

/* the system's notice over the game (a kernel arriving, the restart
 * counted down): from the kernel (bm_set_notice) */
static int (*notice_fn)(char *title, char *detail, int *progress);

void bm_set_notice(int (*fn)(char *title, char *detail, int *progress))
{
    notice_fn = fn;
}

void notice_draw(void)
{
    char title[64], detail[64];
    int progress = -1;
    if (!notice_fn || !notice_fn(title, detail, &progress))
        return;
    const char *lines[2] = { title, detail };
    sys_box(lines, 2, 0, 0, progress);
}

/* the box that says a screenshot was saved, for 2 seconds */
void shot_note_draw(void)
{
    if (rec_active()) {                 /* not in the recording: it is drawn after the frame was taken */
        char t[24];
        rec_overlay_text(t, sizeof t);
        g16_text(&rt.g, 2, 2, t, 0xF800);
    }
    if (shot_note <= 0)
        return;
    shot_note--;
    const char *lines[2] = { shot_line[0], shot_line[1] };
    sys_box(lines, 2, 1, 0, -1);
}

/* The console's battery (2026-10-10, a handheld's habit): from the kernel
 * (bm_set_battery, the RGB30); the Pi has none. Information only: each
 * console has its own, so a game played in lockstep never lets it change
 * what it simulates. */
static int (*battery_fn)(int *pct, int *charging, int *low);

void bm_set_battery(int (*fn)(int *pct, int *charging, int *low))
{
    battery_fn = fn;
}

/* battery() -> its charge (0..100) and true on the charger; nil without a
 * battery (the Pi) */
int l_battery(lua_State *L)
{
    int pct, charging, low;
    if (!battery_fn || !battery_fn(&pct, &charging, &low)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, pct);
    lua_pushboolean(L, charging);
    return 2;
}

/* battery_low() -> true while the system shows its low battery (20% or
 * less off the charger, until 23%), whether or not the icon is on */
int l_battery_low(lua_State *L)
{
    int pct, charging, low = 0;
    lua_pushboolean(L, battery_fn && battery_fn(&pct, &charging, &low) && low);
    return 1;
}

/* While it is low: a small red battery over the frame, top right (left of
 * the dev kit's overlay when that is at the top), its charge inside, blinking under
 * 5%. After the frame is drawn (the 3D finished: flush3d), as the overlay:
 * the game's own drawing and state are not touched. Settings > Screen and
 * sound > Low battery icon (battery_icon=0) turns it off. */
void battery_draw(void)
{
    int pct, charging, low = 0;
    if (!battery_fn || !battery_fn(&pct, &charging, &low) || !low)
        return;
    const char *off = config_get("battery_icon");
    if (off && strcmp(off, "0") == 0)
        return;
    if (pct <= 5 && timer_ticks() / 500000u % 2)
        return;
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    const int z = g->w >= 1280 ? g->w / 640 : 1;
    int right = g->w - 4 * z;                   /* the overlay at the top: 148 wide, from the right */
    if (perf_on == 1)
        right = g->w - 152 * z;
    const int w = 16 * z, h = 8 * z, x = right - w - 2 * z, y = 4 * z;
    const uint16_t red = g16_rgb(255, 72, 64), dark = g16_rgb(8, 8, 16);
    g16_rectfill(g, x - z, y - z, w + 4 * z, h + 2 * z, dark);     /* a border, for any picture */
    g16_rectfill(g, x, y, w, h, red);                               /* the outline... */
    g16_rectfill(g, x + z, y + z, w - 2 * z, h - 2 * z, dark);
    g16_rectfill(g, x + w, y + 2 * z, 2 * z, h - 4 * z, red);       /* ...its terminal */
    const int fill = (w - 4 * z) * (pct < 0 ? 0 : pct > 20 ? 20 : pct) / 20;
    if (fill > 0)
        g16_rectfill(g, x + 2 * z, y + 2 * z, fill, h - 4 * z, red);
    *g = keep;
}

/* keys ("ctrl shift s", "f5 / ctrl r", "f1 - f4") as the keys' pictures
 * from x on the row at y; the x after them (measured only, draw 0) */
static int keys_chips(const char *keys, int x, int y, int small, int draw, uint16_t ink)
{
    const int fw = rt.g.font->width;
    int first = 1;
    for (const char *p = keys; *p; first = 0) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        char tok[16];
        size_t n = 0;
        while (p[n] && p[n] != ' ') {
            if (n + 1 < sizeof tok)
                tok[n] = p[n];
            n++;
        }
        tok[n < sizeof tok ? n : sizeof tok - 1] = 0;
        p += n;
        /* "/" between alternatives, "-" between the ends of a range ("1 - 5");
         * a "-" first or last is the key ("+ / -") */
        const char *rest = p;
        while (*rest == ' ')
            rest++;
        if (!strcmp(tok, "/") || (!strcmp(tok, "-") && !first && *rest)) {
            if (draw)
                g16_text(&rt.g, x + 2, y + (small ? 0 : 0), tok, ink);
            x += fw + 4;
            continue;
        }
        const prompt_t *pr = find_prompt(tok, small);
        if (pr) {
            if (draw)
                draw_prompt(pr, x, y, 1);
            x += pr->w + 2;
        } else {
            if (draw)
                g16_text(&rt.g, x, y, tok, ink);
            x += (int)strlen(tok) * fw + 2;
        }
    }
    return x;
}

/* While F12 is held: the system's keys, then the cartridge's (keyhelp()),
 * with their pictures, in columns; more than a page: the arrows turn them.
 * Over the frame, after the overlay of the dev kit. */
void keys_help(lua_State *L)
{
    if (!hid_usage_held(0x45) && rt.help_open != 1) {
        rt.help_page = 0;
        return;
    }
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    const int small = g->w < 480;
    g->font = small ? &font_console_6x12 : &font_console_8x16;
    const int fw = g->font->width, rowh = g->font->height;     /* the chips are as high */
    const int cols = g->w >= 960 ? 3 : g->w >= 360 ? 2 : 1;
    const int colw = (g->w - 2 * fw) / cols / fw * fw;
    const int y0 = 2 * rowh, rows = (g->h - y0 - rowh) / rowh;
    const uint16_t ink = g16_rgb(232, 232, 236), dim = g16_rgb(150, 150, 165), head = g16_rgb(255, 176, 64);
    const uint16_t bad = g16_rgb(255, 90, 80);
    g16_rectfill(g, 0, 0, g->w, g->h, g16_rgb(12, 14, 22));

    /* the items: the system's heading and keys, then the cartridge's */
    int list = 0, napp = 0;
    const char *title = "this cartridge";
    if (rt.keyhelp) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, rt.keyhelp);
        lua_rawgeti(L, -1, 2);
        title = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 1);
        list = lua_gettop(L);
        napp = (int)luaL_len(L, list);
    }
    const int nsys = syskeys_count();
    const int total = 1 + nsys + (napp ? 1 + napp : 0), per = rows * cols;
    /* the places: the cartridge's keys from the top of a new column when
     * there is one; a heading never last in its column; a key whose words
     * do not fit beside it takes two rows (the words under it) */
    int slots = 0;
    for (int pass = 0; pass < 2; pass++) {
        const int pages = (slots + per - 1) / per;
        if (pass == 1) {
            if (rt.help_page < 0) rt.help_page = 0;
            if (rt.help_page >= pages) rt.help_page = pages - 1;
            g16_text(g, fw, 0, "Keys", head);
            g16_text(g, 6 * fw, 0, "(F12 held)", dim);
            if (pages > 1) {
                char pg[24];
                ksnprintf(pg, sizeof pg, "%d/%d", rt.help_page + 1, pages);
                int x = g->w - fw - (int)strlen(pg) * fw;
                g16_text(g, x, 0, pg, dim);
                const prompt_t *dn = find_prompt("down", small), *up = find_prompt("up", small);
                if (dn && up) {
                    draw_prompt(dn, x - fw - dn->w, 0, 1);
                    draw_prompt(up, x - fw - dn->w - 2 - up->w, 0, 1);
                }
            }
        }
        int slot = 0;
        for (int i = 0; i < total; i++) {
            const char *keys = NULL, *what = NULL, *text = NULL;
            int clash = 0;
            if (i == 0) {
                text = "bm";
            } else if (i <= nsys) {
                keys = syskey(i - 1)->keys;
                what = syskey(i - 1)->what;
            } else if (i == nsys + 1) {
                text = title ? title : "";
            } else {
                /* the strings stay valid: the list (on the stack) holds them */
                const int t = lua_rawgeti(L, list, i - nsys - 1);
                if (t == LUA_TSTRING) {
                    text = lua_tostring(L, -1);
                } else if (t == LUA_TTABLE) {
                    lua_rawgeti(L, -1, 1);
                    lua_rawgeti(L, -2, 2);
                    keys = lua_tostring(L, -2);
                    what = lua_tostring(L, -1);
                    lua_pop(L, 2);
                    clash = keys && syskeys_reserved(keys);
                }
                lua_pop(L, 1);
                if (!text && !keys)
                    continue;
            }
            int tx = 0, two = 0;
            if (keys) {
                tx = keys_chips(keys, 0, 0, small, 0, ink);
                tx = (tx + fw + fw - 1) / fw * fw;      /* the words on the font's grid (the tests read them) */
                two = tx + (int)strlen(what ? what : "") * fw > colw - fw;
            }
            if (i == nsys + 1 && cols > 1 && slot % rows)
                slot += rows - slot % rows;
            else if ((text || two) && slot % rows == rows - 1)
                slot++;
            const int page = slot / per, c = slot % per / rows, r = slot % rows;
            slot += 1 + two;
            if (pass == 0 || page != rt.help_page)
                continue;
            const int x = fw + c * colw, y = y0 + r * rowh, right = x + colw - fw;
            if (text) {
                g16_text(g, x, y, text, head);
                continue;
            }
            keys_chips(keys, x, y, small, 1, ink);
            const int wx = two ? x + 2 * fw : x + tx, wy = two ? y + rowh : y;
            char buf[64];
            ksnprintf(buf, sizeof buf, "%s", what ? what : "");
            int room = (right - wx) / fw;
            if (room < (int)strlen(buf))
                buf[room > 0 ? room : 0] = 0;
            g16_text(g, wx, wy, buf, clash ? bad : dim);
        }
        slots = slot;
    }
    if (rt.keyhelp)
        lua_pop(L, 2);
    *g = keep;
}

/* "0.42", "12.5": ms in a few columns (the session report's functions) */
static void ms_cols(char *out, size_t n, uint32_t us)
{
    if (us < 10000)
        ksnprintf(out, n, "%lu.%02lu", (unsigned long)(us / 1000), (unsigned long)(us / 10 % 100));
    else
        ksnprintf(out, n, "%lu.%lu", (unsigned long)(us / 1000), (unsigned long)(us / 100 % 10));
}

/* The session report (2026-10-10, the user's request: the freezes on the
 * RGB30 had no trace once the overlay's 64 frames had gone by). The dev kit
 * keeps, for the run of a game or tool, how long each frame really lasted
 * (from one picture on screen to the next, so a stall OUTSIDE _update and
 * _draw shows too: the card, the GPU, the machine) in a few classes, and
 * the worst frames with their parts; the frame's parts as the overlay
 * measures them (the audio interrupt, the wait for the GPU); the costliest
 * functions when profile(true) is on. At the end of the run it goes to the
 * reports as one file that the next run of the same game replaces:
 * reports/<branch>/session_<game>_<board>.txt. bm/config.txt session_report:
 * 0 never, 1 always; without it, when there is a github_token. */
#define SESS_WORST 12
#define SESS_CLASSES 6
#define SESS_FUNCS 10
#define PERF_GPU_BOUND_US 2000          /* a frame whose ARM waited longer for the GPU: the GPU set its pace */
enum { SEG_UPDATE, SEG_DRAW, SEG_3D, SEG_AUDIO, SEG_WAIT, SEG_USED };
typedef struct {
    uint32_t frame, at_ms, wall_us, cpu_us, upd_us, draw_us, us3d, copy_us, instr_k, lua_kb, updates;
} sess_frame_t;
static struct {
    uint32_t hist[SESS_CLASSES];        /* <= 18, 25, 34, 50, 100 ms, more */
    sess_frame_t worst[SESS_WORST];     /* the longest first */
    int n;
    int w, h;                           /* the screen of the run */
    char info[4][19];                   /* the game's own lines (devinfo) as the run ended */
    int ninfo;
    char opts[192];                     /* the 3D's options as the run ended (options_line) */
    int want;                          /* the report is written (session_wanted): measure the parts */
    uint64_t seg_us[SEG_USED];          /* the frames' parts, summed (perf_account) */
    uint64_t wall_us;                   /* the frames measured, from start to start */
    uint32_t measured, gpu_bound;       /* frames measured; those the GPU set the pace of */
    uint32_t audio_max10;               /* the audio's most of a second, tenths of % of the CPU's time */
    prof_row_t fn[SESS_FUNCS];          /* profile(true): the costliest functions as the run ended */
    int nfn;
    uint32_t fn_frames;
    int resumed;                        /* the run is a resumed one: the report covers it only */
} sess;
static const uint32_t sess_limit_us[SESS_CLASSES - 1] = { 18000, 25000, 34000, 50000, 100000 };

static void sess_frame(uint32_t wall_us)
{
    int c = 0;
    while (c < SESS_CLASSES - 1 && wall_us > sess_limit_us[c])
        c++;
    sess.hist[c]++;
    if (wall_us <= 18000 || (sess.n == SESS_WORST && wall_us <= sess.worst[SESS_WORST - 1].wall_us))
        return;
    int i = sess.n < SESS_WORST ? sess.n++ : SESS_WORST - 1;
    while (i > 0 && sess.worst[i - 1].wall_us < wall_us) {
        sess.worst[i] = sess.worst[i - 1];
        i--;
    }
    sess.worst[i] = (sess_frame_t){
        .frame = (uint32_t)rt.frame, .at_ms = (timer_ticks() - rt.start_us) / 1000, .wall_us = wall_us,
        .cpu_us = rt.last_cpu_us, .upd_us = rt.update_us, .draw_us = rt.draw_us, .us3d = rt.us3d,
        .copy_us = rt.present_us, .instr_k = rt.last_instr_k, .lua_kb = (uint32_t)(luavm_mem() / 1024),
        .updates = rt.updates,
    };
}


/* ---- the frame in its parts (the overlay, the session report)
 *
 * A few marks a frame, each the time and three counters: the audio
 * interrupt's time (audio_busy_us: it stops whatever runs, so what it adds
 * during a phase is taken out of that phase and given to the audio), the
 * ARM's wait for the GPU (gpu3d_wait_us), the 3D calls' time (us3d_all).
 * m0 the frame's start, m1 after its _update, m2 after its _draw, m3
 * before the pace (the page shown); e0..e1 the early _update of the frame
 * queue (bm3d 4.1), run in this frame's time. A frame is summed at the next
 * one's start (the audio of the whole frame, the pace's too). Nothing here
 * changes what the game does: only times are read. */
typedef struct { uint32_t t, au, wt, d3; } perf_mark_t;

static struct {
    int on;                             /* this frame is measured */
    int ended, early;                   /* m3 taken; an early _update ran */
    perf_mark_t m0, m1, m3, e0, e1;
} pf;

/* the means of the last second (the overlay shows them) */
static struct {
    uint32_t t0, frames, skipped;       /* the second being summed */
    uint32_t sum[SEG_USED], wall;
    gpu3d_stats_t g;                    /* the GPU's totals at its start */
    int gpu_seen;
    uint32_t seg[SEG_USED], gpu_us;     /* the last whole second: means a frame */
    int skip, ready;                    /* a frame was skipped in it; the means exist */
} pw;

static void perf_mark(perf_mark_t *m)
{
    m->t = timer_ticks();
    m->au = audio_busy_us;
    m->wt = gpu3d_wait_us();
    m->d3 = rt.us3d_all;
}

/* a phase from a to b, less what lay inside it from c to d (the early
 * _update, or nothing): its own time without the audio's and the GPU
 * wait's, and of that its 3D calls' (their time less the wait, which
 * happens mostly inside them; the rest of the wait was in the 2D's) */
typedef struct { uint32_t cpu, d3, au, wt; } perf_phase_t;

static perf_phase_t perf_phase(const perf_mark_t *a, const perf_mark_t *b, const perf_mark_t *c, const perf_mark_t *d)
{
    perf_phase_t p;
    uint32_t t = b->t - a->t;
    p.au = b->au - a->au;
    p.wt = b->wt - a->wt;
    uint32_t d3 = b->d3 - a->d3;
    if (c) {
        t -= d->t - c->t;
        p.au -= d->au - c->au;
        p.wt -= d->wt - c->wt;
        d3 -= d->d3 - c->d3;
    }
    const uint32_t off = p.au + p.wt;
    p.cpu = t > off ? t - off : 0;
    d3 = d3 > p.wt ? d3 - p.wt : 0;
    p.d3 = d3 < p.cpu ? d3 : p.cpu;
    p.cpu -= p.d3;
    return p;
}

/* stat(16): the GPU's job time the driver accounted since the frame before
 * (the jobs that ended, with the queue a frame's may end in the next one).
 * The totals zeroed meanwhile (a game's start, a resume): from zero */
static void gpu_frame_account(void)
{
    gpu3d_stats_t g, d;
    gpu3d_peek_stats(&g);
    const gpu3d_stats_t *p = &rt.gpu_prev;
    if (g.jobs < p->jobs || g.untimed < p->untimed || g.bin_us < p->bin_us ||
        g.render_us < p->render_us || g.late_us < p->late_us) {
        d = g;
    } else {
        memset(&d, 0, sizeof d);
        d.jobs = g.jobs - p->jobs;
        d.untimed = g.untimed - p->untimed;
        d.bin_us = g.bin_us - p->bin_us;
        d.render_us = g.render_us - p->render_us;
        d.late_us = g.late_us - p->late_us;
    }
    rt.gpu_frame_us = gpu3d_busy_us(&d);    /* (the ARM's 3D: no jobs, 0) */
    rt.gpu_prev = g;
    /* stat(17), stat(18): the audio interrupt's time and the ARM's wait
     * for the GPU since the frame before (a total that went back: from zero) */
    const uint32_t au = audio_busy_us, wt = gpu3d_wait_us();
    rt.audio_frame_us = au >= rt.audio_prev ? au - rt.audio_prev : au;
    rt.gpuwait_frame_us = wt >= rt.gpuwait_prev ? wt - rt.gpuwait_prev : wt;
    rt.audio_prev = au;
    rt.gpuwait_prev = wt;
}

/* the frame that ended, summed; next: the start of the new one */
static void perf_account(const perf_mark_t *next)
{
    perf_phase_t u = perf_phase(&pf.m0, &pf.m1, NULL, NULL);
    perf_phase_t d = perf_phase(&pf.m1, &pf.m3, pf.early ? &pf.e0 : NULL, &pf.e1);
    if (pf.early) {
        const perf_phase_t e = perf_phase(&pf.e0, &pf.e1, NULL, NULL);
        u.cpu += e.cpu;
        u.d3 += e.d3;
        u.wt += e.wt;
    }
    uint32_t seg[SEG_USED];
    seg[SEG_UPDATE] = u.cpu;
    seg[SEG_DRAW] = d.cpu;
    seg[SEG_3D] = u.d3 + d.d3;
    seg[SEG_WAIT] = u.wt + d.wt;
    seg[SEG_AUDIO] = next->au - pf.m0.au;       /* the whole frame's, the pace's too */
    const uint32_t wall = next->t - pf.m0.t;
    for (int i = 0; i < SEG_USED; i++) {
        pw.sum[i] += seg[i];
        sess.seg_us[i] += seg[i];
    }
    pw.wall += wall;
    pw.frames++;
    if (wall > FRAME_US * 3 / 2)
        pw.skipped++;
    sess.wall_us += wall;
    sess.measured++;
    if (seg[SEG_WAIT] > PERF_GPU_BOUND_US)
        sess.gpu_bound++;
    if (next->t - pw.t0 < 1000000)
        return;
    /* a second: its means, then the next one */
    gpu3d_stats_t g;
    gpu3d_peek_stats(&g);
    const uint32_t busy = gpu3d_busy_us(&g), was = gpu3d_busy_us(&pw.g);
    const uint32_t gpu = pw.gpu_seen && busy >= was ? busy - was : busy;
    for (int i = 0; i < SEG_USED; i++)
        pw.seg[i] = pw.sum[i] / pw.frames;
    pw.gpu_us = gpu / pw.frames;
    pw.skip = pw.skipped > 0;
    pw.ready = 1;
    if (pw.wall) {
        const uint32_t a10 = (uint32_t)((uint64_t)pw.sum[SEG_AUDIO] * 1000u / pw.wall);
        if (a10 > sess.audio_max10)
            sess.audio_max10 = a10;
    }
    pw.g = g;
    pw.gpu_seen = 1;
    pw.t0 = next->t;
    pw.frames = pw.skipped = pw.wall = 0;
    memset(pw.sum, 0, sizeof pw.sum);
}

/* the start of a frame: the last one summed, this one measured if the
 * overlay or the session report wants it (off: nothing but the test) */
static void perf_begin(void)
{
    if (!perf_on && !sess.want) {
        pf.on = 0;
        return;
    }
    perf_mark_t m;
    perf_mark(&m);
    if (pf.on && pf.ended) {
        perf_account(&m);
    } else if (!pf.on) {                /* (on again: a second from now, nothing of before) */
        pw.t0 = m.t;
        pw.frames = pw.skipped = pw.wall = 0;
        memset(pw.sum, 0, sizeof pw.sum);
        gpu3d_peek_stats(&pw.g);
        pw.gpu_seen = 1;
    }
    pf.on = 1;
    pf.ended = pf.early = 0;
    pf.m0 = m;
}

/* the frame's work done: before the pace (or at the end of a frame held) */
void perf_end(void)
{
    if (pf.on && !pf.ended) {
        perf_mark(&pf.m3);
        pf.ended = 1;
    }
}

/* a new run (or a resumed one): nothing measured yet */
static void perf_reset(void)
{
    memset(&pf, 0, sizeof pf);
    memset(&pw, 0, sizeof pw);
}

static void sess_reset(void)
{
    memset(&sess, 0, sizeof sess);
    sess.want = session_wanted();
}

/* green under 50% of a frame, yellow to 80%, red over (or a frame skipped) */
static uint16_t perf_colour(uint32_t pct, int skip)
{
    if (skip || pct > 80)
        return g16_rgb(232, 64, 48);
    return pct >= 50 ? g16_rgb(232, 200, 64) : g16_rgb(72, 200, 96);
}

/* a row: its name, the bar of parts us[0..n) (of a frame's 16.7 ms) in a
 * frame of the total's colour, the % at the right; on the 8x16 grid */
static void perf_bar(g16_t *g, int x, int y, int z, const char *name, const uint32_t *us,
                     const uint16_t *col, int n, int skip)
{
    uint32_t used = 0;
    for (int i = 0; i < n; i++)
        used += us[i];
    uint32_t pct = (used * 100 + FRAME_US / 2) / FRAME_US;
    if (pct > 999)
        pct = 999;
    const uint16_t c = perf_colour(pct, skip);
    g16_text_scaled(g, x, y, name, g16_rgb(150, 150, 170), z);
    /* the bar and its frame inside columns 4 to 12: the name and the % keep their cells */
    const int bx = x + 4 * 8 * z + z, bw = 9 * 8 * z - 2 * z, by = y + 3 * z, bh = 10 * z;
    g16_rectfill(g, bx - z, by - z, bw + 2 * z, bh + 2 * z, c);
    g16_rectfill(g, bx, by, bw, bh, g16_rgb(28, 28, 40));          /* free */
    int at = 0;
    uint32_t sum = 0;
    for (int i = 0; i < n && at < bw; i++) {
        sum += us[i];
        int to = (int)((uint64_t)sum * (uint32_t)bw / FRAME_US);
        if (to > bw)
            to = bw;
        if (to > at)
            g16_rectfill(g, bx + at, by, to - at, bh, col[i]);
        at = to > at ? to : at;
    }
    char t[8];
    ksnprintf(t, sizeof t, "%3lu%%", (unsigned long)pct);
    g16_text_scaled(g, x + 14 * 8 * z, y, t, c, z);  /* columns 14 to 17 */
}

/* The overlay (perf_on: 1 top right, 2 bottom right), after the frame is
 * drawn; and the profiler of functions kept in step */
void perf_frame(void)
{
    prof_update();
    prof_frame();
    if (!perf_on)
        return;
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    g->font = &font_console_8x16;
    /* x2 from 1280 wide, x3 at 1920: the same size on a TV as at 640 */
    const int z = g->w >= 1280 ? g->w / 640 : 1;
    const int x = g->w - 144 * z;                 /* the text on the 8 x 16 grid (read by the tests) */
    const int gpu = rt.r3d_ready && rt.r3d.backend;
    const int rows = gpu ? 3 : 2, rowh = 16 * z;
    int y = 0;
    if (perf_on == 2) {
        y = (g->h / rowh - rows) * rowh;
        if (y < 0)
            y = 0;
    }
    g16_rectfill(g, x - 4 * z, y ? y - 2 * z : 0, 148 * z, rows * rowh + 2 * z, g16_rgb(8, 8, 16));
    uint32_t used = 0;
    for (int i = 0; i < SEG_USED; i++)
        used += pw.seg[i];
    char line[32];
    ksnprintf(line, sizeof line, "%lufps %lu.%lums", (unsigned long)rt.fps, (unsigned long)(used / 1000),
              (unsigned long)(used / 100 % 10));
    g16_text_scaled(g, x, y, line, g16_rgb(232, 232, 216), z);
    /* the CPU: _update, _draw, 3D calls, audio, GPU wait (darker), free */
    static const uint8_t order[5] = { SEG_UPDATE, SEG_DRAW, SEG_3D, SEG_AUDIO, SEG_WAIT };
    const uint16_t col[5] = { g16_rgb(90, 150, 255), g16_rgb(80, 210, 220), g16_rgb(190, 120, 255),
                              g16_rgb(255, 150, 60), g16_rgb(84, 84, 120) };
    uint32_t us[5];
    for (int i = 0; i < 5; i++)
        us[i] = pw.seg[order[i]];
    perf_bar(g, x, y + rowh, z, "cpu", us, col, 5, pw.skip);
    if (gpu) {
        const uint16_t busy = g16_rgb(190, 120, 255);
        perf_bar(g, x, y + 2 * rowh, z, "gpu", &pw.gpu_us, &busy, 1, 0);
    }
    *g = keep;
}

/* screen(w, h): the new resolution between two frames (the page shown, no
 * 3D waiting on the GPU): the framebuffer again, the z-buffer, the 2D
 * buffers of the screen's size; the font and the 2D camera stay, the clip
 * is the whole screen. If the console cannot set it, the old one again
 * (-1 only if not even that). */
static int screen_apply(framebuffer_t *fb, lua_State *L)
{
    const int w = rt.want_w, h = rt.want_h, ow = rt.g.w, oh = rt.g.h;
    rt.want_w = rt.want_h = 0;
    if (w == ow && h == oh)
        return 0;
    flush3d(0);
    zclear_dma_wait(1);                 /* the DMA may be clearing the old z-buffer */
    rt.zclear_seen = 0;
    const g16_t keep = rt.g;
    const int via = bm_video_uses_ram();
    int nw = w, nh = h;
    if (bm_video_enter(fb, w, h, &rt.g) != 0) {
        kprintf("\x1b[91mbm: cannot set %dx%d RGB565: %dx%d again\x1b[0m\n", w, h, ow, oh);
        nw = ow;
        nh = oh;
        if (bm_video_enter(fb, ow, oh, &rt.g) != 0)
            return -1;
    }
    if (via)
        video_to_ram(&rt.g);
    rt.g.font = keep.font;
    rt.g.cam_x = keep.cam_x;
    rt.g.cam_y = keep.cam_y;
    if (rt.r3d_ready && (nw != ow || nh != oh) && r3d_resize(&rt.r3d, ow) != 0)
        return -1;
    /* a square page inside the screen that became a whole one (a .b16 on the
     * Pi's TV): the GPU as Settings say, now that it can draw on it */
    if (rt.gpu3d_boxed && rt.r3d_ready && !rt.r3d.backend && rt.g.stride == (uint32_t)rt.g.w) {
        rt.gpu3d_boxed = 0;
        gpu3d_maybe();
    }
    if (rt.r3d_ready && rt.r3d.backend)
        gpu3d_page(0, 0);
    if (rt.light.rgb) {                 /* made again at the next light_begin() */
        g16_light_free(&rt.light);
    }
    if (rt.fade.lv) {                   /* the levels of the new size, the colours kept */
        uint8_t *lv = malloc((size_t)nw * (size_t)nh);
        if (!lv)
            return -1;
        free(rt.fade.lv);
        memset(lv, 0, (size_t)nw * (size_t)nh);
        rt.fade.lv = lv;
        rt.fade.w = nw;
        rt.fade.h = nh;
    }
    pointer_env(rt.mouse, nw, nh);
    lua_pushinteger(L, nw);
    lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, nh);
    lua_setglobal(L, "SCREEN_H");
    kprintf("bm: screen %dx%d\n", nw, nh);
    return 0;
}

/* The frame loop, then either suspend or close. */
static int run_frames(framebuffer_t *fb, lua_State *L, const char *title,
                      uint32_t con_w, uint32_t con_h, uint32_t seconds, bm_stats_t *st,
                      const char *error, int suspendable)
{
    uint32_t start = timer_ticks(), deadline = start + FRAME_US, prev = start;
    uint32_t fps_t0 = start, fps_frames = 0;
    int left = 0;                           /* Esc, PS, Start+Select, 'q' */
    int updated = 0;                        /* this frame's _update ran while the GPU drew the last (bm3d 4.1) */
    uint32_t early_us = 0;                  /* its time */
    /* the time limit in 64 bits: seconds * 10^6 overflows 32 bits after
     * 71 minutes (24 hours used to end a game after 8 min 20 s), and so
     * does the microsecond counter */
    uint64_t played_us = 0, limit_us = (uint64_t)seconds * 1000000u;
    uint32_t played_at = start;
    uint32_t lag = 0, lag_at = start;       /* frameskip(): the time not yet run by an _update */
    int fresh = 0;                          /* the keys read and not yet seen by an _update */
    const uint32_t frame0 = rt.frame;       /* this run's frames: elapsed, GPU and held ones are too */
    const uint32_t slow0 = rt.slow_frames;
    prof_enable(0, NULL);                   /* the profiler of functions starts from nothing */
    perf_reset();                           /* the overlay: this run's frames only (a resumed one's too) */
    held.on = held.now = 0;                 /* nothing held (bm3d 6.9), this run's count */
    held.n = 0;
    held.frames = 0;
    held.fb = fb;
    {
        rec_cfg_t rc;
        if (rec_config(&rc))
            rec_start(&rc);             /* bm/config.txt rec=on: this run is recorded to /bm/rec */
    }
    while (!error) {
        uint32_t now_us = timer_ticks();
        played_us += now_us - played_at;
        played_at = now_us;
        if (rt.quit || played_us >= limit_us)
            break;
        const int q = poll_keys();
        fresh = 1;
        if ((q || rt.leave_ask || rt.leave_held) && leave_step(L, q)) {
            left = 1;
            break;
        }
        audio_idle();
        /* F11, or 'p' from the serial line: the dev kit's overlay (a system
         * key: also while typing) */
        int f11 = hid_usage_held(0x44);
        if ((f11 && !rt.f11_held) || rt.perf_key)
            perf_on = (perf_on + 1) % PERF_MODES;   /* top, bottom, off */
        rt.f11_held = f11;
        rt.perf_key = 0;
        /* frameskip(n): the 1/60 s that went by since the last frame, each
         * an _update (up to n; past n the time is let go: the game slows
         * down rather than never drawing) */
        int ups = 1;
        {
            const uint32_t t = timer_ticks();
            lag += t - lag_at;
            lag_at = t;
            if (rt.skip_max > 1) {
                ups = (int)(lag / FRAME_US);
                if (ups < 1)
                    ups = 1;
                if (ups >= rt.skip_max) {
                    ups = rt.skip_max;
                    lag = 0;
                }
                lag = lag > (uint32_t)ups * FRAME_US ? lag - (uint32_t)ups * FRAME_US : 0;
            } else {
                lag = 0;
            }
        }
        /* with this frame's _update, if it ran during the last (stat(8)
         * counts as if it had run just now; its Lua instructions too) */
        perf_begin();                       /* the dev kit: the frame in its parts */
        const uint32_t t0 = timer_ticks() - early_us;
        rt.frame_t0 = t0;
        rt.update_us = early_us;
        early_us = 0;
        if (!updated)
            rt.frame_instr_k = 0;
        rt.updates = (uint32_t)ups;
        for (int u = updated; u < ups && !error; u++) {
            const uint32_t tu = timer_ticks();
            if (!fresh)
                input_repeat();             /* what was pressed counts once */
            fresh = 0;
            if (call(L, "_update") != 0)
                error = lua_tostring(L, -1);
            rt.update_us += timer_ticks() - tu;
        }
        if (error)
            break;
        if (pf.on)
            perf_mark(&pf.m1);
        const uint32_t td = timer_ticks();
        if (call(L, "_draw") != 0) {
            error = lua_tostring(L, -1);
            break;
        }
        rt.draw_us = timer_ticks() - td;
        updated = 0;
        rt.frame++;                         /* (before the next _update, if it runs early) */
        const uint32_t instr_k = rt.frame_instr_k;
        /* bm3d 6.9: this frame shown a frame later: its 3D started on
         * the GPU, the frame held before shown meanwhile, then this one held
         * while the next _update and _draw run on the next page */
        const int hold = later_on(fb) && later_submit();
        held.now = hold;
        if (!hold)
            later_flush();                  /* (the option just went off: the frame held first) */
        if (!hold && submit3d()) {
            /* the frame in the queue (bm3d 4.1): the GPU draws it while the next
             * frame's _update runs; flush3d waits for it before the page
             * is shown */
            if (pf.on)
                perf_mark(&pf.e0);
            const uint32_t t1 = timer_ticks();
            rt.frame_t0 = t1;
            rt.frame_instr_k = 0;
            d2_early(1);
            rt.early = 1;
            if (!fresh)
                input_repeat();
            fresh = 0;
            const int err = call(L, "_update");
            rt.early = 0;
            if (err != 0) {
                error = lua_tostring(L, -1);
                break;
            }
            updated = 1;
            early_us = timer_ticks() - t1;
            if (pf.on) {
                perf_mark(&pf.e1);
                pf.early = 1;
            }
            if (rt.early_touch) {
                rt.no_early = 1;
                kprintf("bm: _update draws 3D or reads the page: it runs after the frame again (frame queue)\n");
            }
        }
        if (!hold)
            flush3d(0);                     /* the GPU's 3D counts in the frame's time */
        cls_settle();
        rt.last_cpu_us = timer_ticks() - t0 - early_us;
        gpu_frame_account();
        rt.last_instr_k = instr_k;
        if (instr_k > rt.instr_k_max)
            rt.instr_k_max = instr_k;
        if (rt.last_cpu_us > 16667)
            rt.slow_frames++;
        {
            const size_t lua = luavm_mem();
            if (lua > rt.lua_peak)
                rt.lua_peak = lua;
        }
        st->cpu_us_total += rt.last_cpu_us;
        if (rt.last_cpu_us > st->cpu_us_max)
            st->cpu_us_max = rt.last_cpu_us;
        if (rt.r3d_ready)
            r3d_flush(&rt.r3d);
        st->tris3d = rt.r3d_ready ? rt.r3d.tris_drawn : 0;
        crumb_frame(rt.frame);
        const uint32_t shown_before = prev;
        int shown = 1;
        if (hold) {
            shown = held.on;                /* (the first frame held: nothing shown yet) */
            rt.present_us = 0;              /* (a flip, no copy) */
            if (shown) {
                later_show(L);              /* the frame before, with the overlays */
                perf_end();
                pace(&deadline, &prev, &st->dropped);
            }
            later_hold(fb);
            perf_end();                     /* (nothing shown yet: no pace) */
        } else {
            shot_grab();                    /* (the frame as it is, without the overlays) */
            perf_frame();
            battery_draw();
            leave_draw();
            notice_draw();
            shot_note_draw();
            keys_help(L);
            present(fb, &deadline, &prev, &st->dropped);
        }
        if (shown && rt.frame > 1)
            sess_frame(prev - shown_before);
        st->copy_us_total += rt.present_us;
        if (rt.want_w && screen_apply(fb, L) != 0) {
            error = "not enough memory for the screen";
            break;
        }
        if (++fps_frames, timer_ticks() - fps_t0 >= 1000000) {
            rt.fps = fps_frames;
            fps_frames = 0;
            fps_t0 = timer_ticks();
        }
    }

    later_flush();                          /* the frame held, shown: its job done, its page the last */
    const int suspend = !error && left && suspendable && !rt.online_left;
    if (audio_volume() != vol_start)
        config_save();                      /* the volume chosen in the game stays */
    {                                       /* profile(true): its last second, for the session report */
        uint32_t fr = 0;
        const int nfn = prof_enabled() ? prof_rows(sess.fn, SESS_FUNCS, &fr) : 0;
        if (nfn > 0 && fr) {
            sess.nfn = nfn;
            sess.fn_frames = fr;
        }
    }
    prof_enable(0, NULL);                   /* not over the menu nor the next game */
    memset(&dbg, 0, sizeof dbg);            /* nor the debugger (R13) */
    cart_src = NULL;
    rec_stop();                             /* the rest to the card, the report */
    st->frames = rt.frame - frame0;
    st->elapsed_us = timer_ticks() - start;
    st->lua_kb = (uint32_t)(luavm_mem() / 1024);
    st->lua_peak_kb = (uint32_t)((rt.lua_peak > luavm_mem() ? rt.lua_peak : luavm_mem()) / 1024);
    st->assets_kb = assets_kb();
    st->instr_k_max = rt.instr_k_max;
    st->slow = rt.slow_frames - slow0;
    st->tokens = (uint32_t)rt.tokens;
    st->gpu3d = rt.r3d_ready && rt.r3d.backend != NULL;
    st->left = left;
    sess.w = rt.g.w;
    sess.h = rt.g.h;
    sess.ninfo = rt.ndevinfo;
    memcpy(sess.info, rt.devinfo, sizeof sess.info);
    options_line(sess.opts, sizeof sess.opts);
    st->d2_ops = d2.ops;
    st->later = held.frames;
    st->ok = error == NULL;

    if (suspend) {
        audio_pause(1);                     /* music waits, the bank stays */
        susp.L = L;
        susp.active = 1;
        susp.w = rt.g.w;                    /* (screen() may have changed it) */
        susp.h = rt.g.h;
        susp.g = rt.g;
        susp.used_ram = bm_video_uses_ram();
        susp.since = timer_ticks();
        ksnprintf(susp.title, sizeof susp.title, "%s", title);
        leave_mode(fb, con_w, con_h);
        hid_text_mode(0);
        last_error[0] = 0;
        kprintf("bm: \"%s\" suspended (A on its cover resumes it)\n", title);
        return BM_SUSPENDED;
    }

    /* closed: freed with its last frame and the banner still on the screen,
     * then the console's mode (2026-10-10: it used to go first, and the
     * screen stayed black while the memory was freed) */
    audio_reset();
    ksnprintf(last_error, sizeof last_error, "%s", error ? error : "");
    if (error)                              /* (its text is in the state: before release) */
        kprintf("\x1b[91mbm: \"%s\" stopped with an error:\n%s\x1b[0m\n", title, error);
    release(L);
    wait_buttons_up();                      /* the one that left it, so the menu does not take it */
    leave_mode(fb, con_w, con_h);
    hid_text_mode(0);
    return BM_ENDED;
}

/* The cartridge's code given to Lua's parser a piece at a time: the
 * loading screen goes on while a big one compiles */
typedef struct {
    const char *p;
    size_t left;
} chunk_reader_t;

static const char *read_chunk(lua_State *L, void *ud, size_t *size)
{
    chunk_reader_t *r = ud;
    (void)L;
    if (!r->left) {
        *size = 0;
        return NULL;
    }
    loading_tick();
    const size_t n = r->left < 16384 ? r->left : 16384;
    const char *p = r->p;
    r->p += n;
    r->left -= n;
    *size = n;
    return p;
}

int bm_run(framebuffer_t *fb, const uint8_t *data, size_t len,
            uint32_t seconds, bm_stats_t *st, int suspendable)
{
    bm_cart_t cart;
    char err[64];
    const char *error = NULL;
    lua_State *L = NULL;
    const uint32_t ld0 = timer_ticks();        /* load profile (sd-stall-plan S0): one log line at the end */

    bm_close_suspended();              /* one cartridge in memory at a time */
    const uint32_t ld_close = timer_ticks();
    memset(st, 0, sizeof *st);
    if (rt.pfx) {
        r3d_pfx_free(rt.pfx);
        rt.pfx = NULL;
    }
    memset(&rt, 0, sizeof rt);
    rt.audio_prev = audio_busy_us;     /* stat(17), stat(18) count from this run */
    rt.gpuwait_prev = gpu3d_wait_us();
    n_made = 0;
    rt.prof_want = 0;
    free(proj_cover);                  /* no project open yet (cart_load) */
    proj_cover = NULL;
    extras_free();
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    set_copy(&proj_sheet8, &proj_sheet8_len, NULL, 0);
    proj_sheet_drawn = 0;
    cur = next;                         /* the options of this run, then none */
    bm_next_run(0, 0, -1, 0);
    if (bm_parse(data, len, &cart, err, sizeof err) != 0) {
        kprintf("\x1b[91mbm: %s\x1b[0m\n", err);
        loading_stop();
        return BM_ENDED;
    }
    {                                   /* a cartridge that needs a newer bm says so (INFO "spec": bm.h BM_SPEC_REV) */
        char need[16];
        if (bm_info_get(&cart, NULL, NULL, "spec", need, sizeof need) && atoi(need) > BM_SPEC_REV) {
            kprintf("\x1b[91mbm: this game needs revision %d of the bm specification, this kernel has %d: "
                    "update the kernel\x1b[0m\n", atoi(need), BM_SPEC_REV);
            loading_stop();
            return BM_ENDED;
        }
    }
    {                                   /* not made for this kernel: the menu and the Market grey it and ask first; here
                                         * (any way to start a game) the console only says so (compat-policy.toml) */
#ifdef BM_RGB30
        const unsigned me = BM_TARGET_RGB30;
#else
        const unsigned me = BM_TARGET_PI;
#endif
        if (!bm_compat_runs_on(&cart.compat, me))
            kprintf("\x1b[93mbm: made for the %s only, it may not run well here\x1b[0m\n",
                    me == BM_TARGET_PI ? "RGB30" : "Pi Zero W");
    }
    const uint32_t ld_parse = timer_ticks();
    loading_tick();
    if (cur.w > 0 && cur.h > 0) {
        cart.width = cur.w;
        cart.height = cur.h;
    }
    rt.design_w = (int)cart.width;      /* before the mode is entered: safe_area(), aspect() */
    rt.design_h = (int)cart.height;
    memcpy(st->title, cart.title, sizeof st->title);
    perm_fb = fb;
    ksnprintf(perm_title, sizeof perm_title, "%s", cart.title);
    if (load_assets(&cart) != 0 || !(L = new_cart_state(&cart))) {
        free_assets();
        kprintf("\x1b[91mbm: out of memory\x1b[0m\n");
        loading_stop();
        return BM_ENDED;
    }
    const uint32_t ld_state = timer_ticks();
    loading_tick();

    const uint32_t con_w = fb->width, con_h = fb->height;
    if (enter_mode(fb, cart.width, cart.height) != 0) {
        leave_mode(fb, con_w, con_h);
        lua_close(L);
        free_assets();
        kprintf("\x1b[91mbm: cannot set %ux%u RGB565\x1b[0m\n", cart.width, cart.height);
        loading_stop();
        return BM_ENDED;
    }
    loading_page(fb, &rt.g);            /* the loading screen on the game's page now */
    const uint32_t ld_mode = timer_ticks();

    {
        char path[40];
        bm_save_path(cart.title, cart.author, path, sizeof path);
        ksnprintf(rt.save_name, sizeof rt.save_name, "%s", path + sizeof SAVE_DIR);
    }
    audio_reset();
    vol_start = audio_volume();
    {
        char aerr[64];
        if (audio_bank(cart.audio, cart.audio_size, aerr, sizeof aerr) != 0)
            kprintf("\x1b[91mbm: sound bank not loaded: %s\x1b[0m\n", aerr);
        set_copy(&own_audio, &own_audio_len, cart.audio, cart.audio_size);
    }
    const uint32_t ld_audio = timer_ticks();
    rt.start_us = ld_audio;
    rt.hook_count = 0;
    rt.tokens = lua_tokens(cart.lua, cart.lua_size);
    sess_reset();                       /* the session report: this cartridge's frames only */
    perf_on = perf_user;                /* the overlay as Settings has it */
    dbg_begin(L, cart.lua, cart.lua_size);  /* a game tried from a tool: the debugger (R13) */
    chunk_reader_t rd = { cart.lua, cart.lua_size };
    if (lua_load(L, read_chunk, &rd, "=main.lua", "t") != LUA_OK ||
        (lua_pushcfunction(L, traceback), lua_insert(L, -2), lua_pcall(L, 0, 0, -2)) != LUA_OK ||
        call(L, "_init") != 0)
        error = lua_tostring(L, -1);
    {                                   /* ms of each step; "assets" has the state of the state */
        const uint32_t ld_end = timer_ticks();
        kprintf("load: %s %lu ms (close %lu, parse %lu, assets+state %lu, mode %lu, audio %lu, lua %lu)\n", cart.title,
                (unsigned long)((ld_end - ld0) / 1000), (unsigned long)((ld_close - ld0) / 1000),
                (unsigned long)((ld_parse - ld_close) / 1000), (unsigned long)((ld_state - ld_parse) / 1000),
                (unsigned long)((ld_mode - ld_state) / 1000), (unsigned long)((ld_audio - ld_mode) / 1000),
                (unsigned long)((ld_end - ld_audio) / 1000));
    }
    if (error)
        loading_stop();
    else
        loading_end();                  /* loaded: the intro to its end, then the game */
    return run_frames(fb, L, cart.title, con_w, con_h, seconds, st,
                      error, suspendable && !cur.bench);
}

void bm_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, bm_stats_t *st)
{
    bm_run(fb, data, len, seconds, st, 0);
}

int bm_resume(framebuffer_t *fb, uint32_t seconds, bm_stats_t *st)
{
    if (!susp.active)
        return BM_ENDED;
    keymap_apply_controls();                    /* the player may have changed Settings > Controls meanwhile */
    memset(st, 0, sizeof *st);
    memcpy(st->title, susp.title, sizeof st->title);
    perm_fb = fb;
    ksnprintf(perm_title, sizeof perm_title, "%s", susp.title);
    lua_State *L = susp.L;
    susp.active = 0;
    susp.L = NULL;
    const uint32_t con_w = fb->width, con_h = fb->height;
    if (enter_mode(fb, susp.w, susp.h) != 0) {
        leave_mode(fb, con_w, con_h);
        release(L);
        kprintf("\x1b[91mbm: cannot set %ux%u RGB565\x1b[0m\n", susp.w, susp.h);
        return BM_ENDED;
    }
    /* the same clip, camera and draw target as when it stopped */
    rt.g.cx0 = susp.g.cx0; rt.g.cy0 = susp.g.cy0; rt.g.cx1 = susp.g.cx1; rt.g.cy1 = susp.g.cy1;
    rt.g.cam_x = susp.g.cam_x; rt.g.cam_y = susp.g.cam_y;
    rt.g.font = susp.g.font;
    if (susp.used_ram)
        video_to_ram(&rt.g);
    pointer_env(rt.mouse, susp.w, susp.h);      /* the pointer, if it had asked for it */
    /* the time spent in the menu does not count for time() */
    rt.start_us += timer_ticks() - susp.since;
    rt.esc = 0;
    memset(rt.hold, 0, sizeof rt.hold);
    input_flush();
    /* buttons still held (the A that resumed) are not new presses */
    rt.now = rt.prev = hid_to_btn(hid_buttons());
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        rt.pnow[p] = rt.pprev[p] = rt.now;
        rt.praw_prev[p] = rt.praw[p] = hid_buttons();
    }
    hid_text_mode(rt.text_mode);
    audio_pause(0);
    vol_start = audio_volume();
    perf_on = perf_user;                /* the overlay as Settings has it, as at the start */
    sess_reset();                       /* the session report: this run's frames, as the time and GPU's */
    sess.resumed = 1;
    {
        gpu3d_stats_t gs;
        gpu3d_take_stats(&gs);          /* not the menu's jobs (its pictures enlarged by the GPU) */
    }
    kprintf("bm: \"%s\" resumed\n", susp.title);
    return run_frames(fb, L, susp.title, con_w, con_h, seconds, st, NULL, 1);
}

int bm_suspended(char *title, size_t n)
{
    if (title && n)
        ksnprintf(title, n, "%s", susp.active ? susp.title : "");
    return susp.active;
}

void bm_close_suspended(void)
{
    if (!susp.active)
        return;
    susp.active = 0;
    kprintf("bm: \"%s\" closed, memory freed\n", susp.title);
    release(susp.L);
    susp.L = NULL;
}

/* ms with one decimal, from microseconds ("16.7") */
#define MS1(us) (unsigned long)((us) / 1000), (unsigned long)((us) % 1000 / 100)

int session_wanted(void)
{
    const char *v = config_get("session_report");
    if (v && v[0])
        return v[0] != '0';
    const char *t = config_get("github_token");
    return t && t[0];
}

/* the run just ended: its frames, as a text for the reports */
static void session_report(const bm_stats_t *st, const gpu3d_stats_t *g)
{
    if (st->frames < 30 || !session_wanted())
        return;
    char *t = malloc(8192);
    if (!t)
        return;
    size_t n = 0;
#define PUT(...) do { if (n < 8192) n += (size_t)ksnprintf(t + n, 8192 - n, __VA_ARGS__); } while (0)
    const uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
    const uint32_t avg = st->cpu_us_total / st->frames;
    PUT("session of \"%s\"%s (the last one: the next run of this game replaces this file)\n", st->title,
        sess.resumed ? " since it was resumed" : "");
    PUT("options %s\n", sess.opts);
    PUT("screen %dx%d, %lu frames in %lu.%lu s, %lu.%lu fps\n", sess.w, sess.h, (unsigned long)st->frames,
        (unsigned long)(ms / 1000), (unsigned long)(ms % 1000 / 100), (unsigned long)(fps10 / 10),
        (unsigned long)(fps10 % 10));
    PUT("3D: %s\n", st->gpu3d ? "GPU" : "ARM or none");
    PUT("\ntime of _update + _draw (+ the 3D's wait): avg %lu.%lu ms, max %lu.%lu ms, %lu frames over 16.7 ms\n",
        MS1(avg), MS1(st->cpu_us_max), (unsigned long)st->slow);
    PUT("memory: Lua peak %lu KB, data %lu KB; code %lu tokens; busiest frame %luk Lua instructions\n",
        (unsigned long)st->lua_peak_kb, (unsigned long)st->assets_kb, (unsigned long)st->tokens,
        (unsigned long)st->instr_k_max);
    PUT("frames that were shown after more than 1.5 frames: %lu\n", (unsigned long)st->dropped);
    for (int i = 0; i < sess.ninfo; i++)
        PUT("the game says: %s\n", sess.info[i]);
    static const char *const cls[SESS_CLASSES] = { "<= 18 ms (60 fps)", "<= 25 ms", "<= 34 ms (30 fps)",
                                                   "<= 50 ms", "<= 100 ms", "> 100 ms (a freeze)" };
    PUT("\ntime from one picture to the next:\n");
    for (int i = 0; i < SESS_CLASSES; i++)
        PUT("  %-20s %lu\n", cls[i], (unsigned long)sess.hist[i]);
    if (sess.measured) {
        /* the overlay's parts: ms a frame, and % of the 16.7 ms of a frame at 60 Hz */
        const uint32_t m = sess.measured;
        uint32_t seg[SEG_USED], used = 0;
        for (int i = 0; i < SEG_USED; i++) {
            seg[i] = (uint32_t)(sess.seg_us[i] / m);
            used += seg[i];
        }
        PUT("\nthe frame in its parts (as the overlay, %lu frames; ms a frame, %% of the 16.7 ms of a frame):\n",
            (unsigned long)m);
        static const char *const names[SEG_USED] = { "update", "draw (no 3D)", "3D calls", "audio", "GPU wait" };
        for (int i = 0; i < SEG_USED; i++)
            PUT("  %-14s %3lu.%lu  %3lu%%\n", names[i], MS1(seg[i]), (unsigned long)(seg[i] * 100 / FRAME_US));
        PUT("  %-14s %3lu.%lu  %3lu%%\n", "all", MS1(used), (unsigned long)(used * 100 / FRAME_US));
        const uint32_t a10 = sess.wall_us ? (uint32_t)(sess.seg_us[SEG_AUDIO] * 1000u / sess.wall_us) : 0;
        PUT("audio interrupt: %lu.%lu%% of the CPU's time (mean), %lu.%lu%% at most in a second\n",
            (unsigned long)(a10 / 10), (unsigned long)(a10 % 10), (unsigned long)(sess.audio_max10 / 10),
            (unsigned long)(sess.audio_max10 % 10));
        uint64_t work = 0;
        for (int i = 0; i < SEG_USED; i++)
            work += sess.seg_us[i];
        const uint32_t w10 = work ? (uint32_t)(sess.seg_us[SEG_WAIT] * 1000u / work) : 0;
        const uint32_t b10 = sess.gpu_bound * 1000u / m;
        PUT("GPU wait: %lu.%lu%% of the frames' work; the GPU set the pace (the ARM waited more than %lu ms for "
            "it) in %lu.%lu%% of the frames\n", (unsigned long)(w10 / 10), (unsigned long)(w10 % 10),
            (unsigned long)(PERF_GPU_BOUND_US / 1000), (unsigned long)(b10 / 10), (unsigned long)(b10 % 10));
    }
    if (sess.n) {
        PUT("\nthe longest frames (wall = picture to picture; cpu = _update + _draw; outside = wall - cpu - copy:\n"
            "time spent in neither, e.g. the SD card, the GPU or the machine itself; ms):\n");
        PUT("  frame  at s    wall    cpu   upd  draw    3D  copy outside  k-instr  lua KB  updates\n");
        for (int i = 0; i < sess.n; i++) {
            const sess_frame_t *f = &sess.worst[i];
            const uint32_t used = f->cpu_us + f->copy_us;
            const uint32_t out = f->wall_us > used ? f->wall_us - used : 0;
            PUT("  %5lu %5lu.%lu %5lu.%lu %4lu.%lu %3lu.%lu %4lu.%lu %3lu.%lu %3lu.%lu %5lu.%lu %8lu %8lu %8lu\n",
                (unsigned long)f->frame, (unsigned long)(f->at_ms / 1000), (unsigned long)(f->at_ms % 1000 / 100),
                MS1(f->wall_us), MS1(f->cpu_us), MS1(f->upd_us), MS1(f->draw_us), MS1(f->us3d), MS1(f->copy_us),
                MS1(out), (unsigned long)f->instr_k, (unsigned long)f->lua_kb, (unsigned long)f->updates);
        }
    }
    if (g && g->jobs) {
        const uint32_t per = gpu3d_busy_us(g) / st->frames;
        PUT("\nGPU 3D: %lu jobs, %lu triangles a frame, %lu.%lu ms a frame, longest job seen to its end %lu.%lu ms\n",
            (unsigned long)g->jobs, (unsigned long)(g->tris / st->frames), MS1(per), MS1(g->max_us));
        if (g->untimed)
            PUT("(a job's time: from its start to the end the ARM saw; %lu jobs had ended before it looked, "
                "counted at the others' mean)\n", (unsigned long)g->untimed);
        PUT("binner memory: at most %lu KB of the tile lists' 4096, %lu KB of the overflow's 2048, %lu jobs "
            "in the overflow\n", (unsigned long)((g->pool_max + 1023) / 1024),
            (unsigned long)((g->ovf_max + 1023) / 1024), (unsigned long)g->ovf_jobs);
        if (g->clean_cyc || g->inval_cyc)
            PUT("ARM cache upkeep: %lu k cycles a frame before the jobs, %lu k after\n",
                (unsigned long)(g->clean_cyc / st->frames / 1000), (unsigned long)(g->inval_cyc / st->frames / 1000));
        if (g->queued || st->later)
            PUT("frame queue: %lu jobs started and waited for later, %lu filled while the GPU drew the one before, "
                "%lu frames shown a frame later (gpu3d_later)\n", (unsigned long)g->queued,
                (unsigned long)g->overlapped, (unsigned long)st->later);
    }
    if (sess.nfn) {
        PUT("\nthe costliest functions (profile(true): the last whole second, %lu frames; ms a frame of their own "
            "and with what they call; [C]: the console's and Lua's own):\n", (unsigned long)sess.fn_frames);
        PUT("  %-22s %6s %6s %6s  %s\n", "function", "self", "all", "calls", "where");
        for (int i = 0; i < sess.nfn; i++) {
            char a[12], b[12];
            ms_cols(a, sizeof a, sess.fn[i].self_us);
            ms_cols(b, sizeof b, sess.fn[i].total_us);
            PUT("  %-22s %6s %6s %6lu  %s\n", sess.fn[i].name, a, b, (unsigned long)sess.fn[i].calls,
                sess.fn[i].where);
        }
    }
#undef PUT
    if (n > 8191)
        n = 8191;                       /* (cut: the last line may be partial) */
    reports_session(st->title, t, n);
    free(t);
}

void bm_print_stats(const bm_stats_t *st)
{
    if (!st->frames)
        return;
    uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
    uint32_t avg = st->cpu_us_total / st->frames;
    kprintf("bm: \"%s\" %lu frames, %lu.%lu fps, %lu dropped\n",
            st->title, st->frames, fps10 / 10, fps10 % 10, st->dropped);
    uint32_t copy = st->copy_us_total / st->frames;
    kprintf("     update+draw avg %lu.%02lu ms (%lu%% of frame), max %lu.%02lu ms, Lua %lu KB\n",
            avg / 1000, avg % 1000 / 10, avg * 100 / FRAME_US,
            st->cpu_us_max / 1000, st->cpu_us_max % 1000 / 10, st->lua_kb);
    if (copy)
        kprintf("     copy to screen %lu.%02lu ms per frame\n", copy / 1000, copy % 1000 / 10);
    kprintf("     dev kit: Lua peak %lu KB, data %lu KB, busiest frame %luk instructions, "
            "%lu frames over 16.7 ms, %lu tokens\n",
            st->lua_peak_kb, st->assets_kb, st->instr_k_max, st->slow, st->tokens);
    gpu3d_stats_t g;
    gpu3d_take_stats(&g);
    if (g.jobs) {
        uint32_t per = gpu3d_busy_us(&g) / st->frames;
        kprintf("     GPU 3D %lu jobs, %lu triangles a frame, %lu.%02lu ms a frame (bin %lu%%), max job %lu us\n",
                g.jobs, g.tris / st->frames, per / 1000, per % 1000 / 10,
                g.bin_us + g.render_us ? g.bin_us * 100 / (g.bin_us + g.render_us) : 0, g.max_us);
    }
    session_report(st, &g);
}

/* ---------------------------------------------------------------- C bench */

uint32_t bm_bench(framebuffer_t *fb, uint32_t frames)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    bm_close_suspended();
    if (rt.pfx) {
        r3d_pfx_free(rt.pfx);
        rt.pfx = NULL;
    }
    memset(&rt, 0, sizeof rt);
    if (g16_sheet_alloc(&rt.sheet, 128, 128) != 0)
        return 0;
    /* half of the cells opaque, half with holes */
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int cell = (y / 8) * 16 + x / 8;
            int hole = (cell & 1) && ((x + y) % 5 == 0);
            g16_sheet_set(&rt.sheet, x, y, g16_rgb((uint32_t)x * 2, (uint32_t)y * 2, (uint32_t)cell), !hole);
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(&rt.sheet, cx, cy);
    rt.map.w = 80;
    rt.map.h = 45;
    rt.map.cells = calloc(80 * 45, 2);
    if (!rt.map.cells || enter_mode(fb, 640, 360) != 0) {
        g16_sheet_free(&rt.sheet);
        free(rt.map.cells);
        leave_mode(fb, con_w, con_h);
        return 0;
    }
    for (int i = 0; i < 80 * 45; i++)
        rt.map.cells[i] = (uint16_t)(1 + (i * 7) % 255);

    uint32_t total = 0, deadline = timer_ticks() + FRAME_US, prev = 0, done = 0;
    for (uint32_t f = 0; f < frames && !syskeys_test_stop(); f++, done++) {   /* (Start+Select, PS) */
        uint32_t t0 = timer_ticks();
        g16_cls(&rt.g, 0);
        g16_map(&rt.g, &rt.sheet, &rt.map, 0, 0, -(int)(f % 8), 0, 81, 45);
        for (int i = 0; i < 256; i++) {
            int x = (i * 37 + (int)f * 3) % 660 - 10, y = (i * 53 + (int)f * 2) % 380 - 10;
            g16_spr(&rt.g, &rt.sheet, (i * 2) % 224, x, y, 2, 2, i & 1, i & 2);
        }
        g16_rectfill(&rt.g, 0, 0, 640, 16, 0);
        g16_text(&rt.g, 0, 0, "bm C benchmark: full map + 256 sprites 16x16", 0xFFFF);
        total += timer_ticks() - t0;
        present(fb, &deadline, &prev, NULL);
        total += rt.present_us;             /* the frame is on screen only after the copy */
    }
    leave_mode(fb, con_w, con_h);
    g16_sheet_free(&rt.sheet);
    free(rt.map.cells);
    rt.map.cells = NULL;
    return done ? total / done : 0;
}

/* The benchmark both ways (direct and via RAM), one line; the faster one
 * is not chosen automatically: see bm_set_via_ram. */
static void ms2(const char *label, uint32_t us)
{
    kprintf("%s %lu.%02lu ms", label, us / 1000, us % 1000 / 10);
}

void bm_set_dma_frames(int on) { dma_frames = on; }

void bm_bench_report(framebuffer_t *fb, uint32_t frames)
{
    int saved = via_ram;
    syskeys_test_begin();               /* Start+Select, Ctrl+Esc, PS stop it (syskeys.h) */
    via_ram = 0;
    uint32_t direct = bm_bench(fb, frames);
    via_ram = 1;
    uint32_t ram = syskeys_test_stopped() ? 0 : bm_bench(fb, frames);
    via_ram = saved;
    if (syskeys_test_stopped()) {
        kprintf("bm bench stopped (Start+Select, Ctrl+Esc or PS)\n");
        return;
    }
    kprintf("bm bench (map + 256 sprites):");
    ms2(" direct", direct);
    ms2(", via RAM", ram);
    kprintf("\n");
}
