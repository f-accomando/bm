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
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "net/img3d.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/crc32.h"
#include "kernel/input.h"
#include "kernel/pointer.h"
#include "usb/hid.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "kernel/crumbs.h"
#include "kernel/prompts.h"
#include "kernel/reports.h"
#include "kernel/syskeys.h"
#include "n8lua.h"
#include "ai/lua_ai.h"
#include "ai/net.h"
#include "net/cartnet.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define FRAME_US        16667
#define HOLD_FRAMES     10          /* a serial key press counts as held this long */
#define QUIT_FORCE      0x100       /* 'q' on the serial line: leaves without asking _exit() */
#define HOOK_EVERY      1000        /* instructions between hook calls */
#define FRAME_BUDGET    20000       /* x HOOK_EVERY = 20 M instructions per callback */

enum { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_START, BTN_SELECT,
       BTN_COUNT };
/* the shoulder buttons from the serial keys: pad() only, not btn() */
enum { SER_L1 = BTN_COUNT, SER_R1, SER_COUNT };

static struct {
    g16_t g;
    g16_sheet_t sheet;
    g16_map_t map;                  /* the map's layer 1 (map.cells == layer[0]) */
    uint16_t *layer[BM_LAYERS_MAX]; /* the cells of each layer, map.w x map.h (R11) */
    char layer_name[BM_LAYERS_MAX][BM_LAYER_NAME + 1];
    int nlayers;
    uint8_t *flags;                 /* fget/fset: a byte per 8x8 cell of the sheet */
    int flags_dirty;                /* fset() since the sheet came: cart_write(sheet=) writes them */
    uint8_t *zones;                 /* a copy of the cartridge's SPRITES section (zspr), or NULL */
    int nzones;
    uint8_t *boxes;                 /* a copy of its BOXES section (zboxes), or NULL */
    int nboxes;
    uint8_t *cell_dirty;
    int sheet_dirty;
    uint8_t hold[SER_COUNT];
    int uses_xy;                /* the cart asked for btn(6) or btn(7) */
    uint16_t now, prev;             /* button bits this frame / last frame, any player */
    uint16_t pnow[INPUT_PLAYERS], pprev[INPUT_PLAYERS];     /* the same per player */
    uint32_t praw[INPUT_PLAYERS];   /* HID bits per player (stick() from the cross) */
    uint32_t praw_prev[INPUT_PLAYERS];  /* ...the frame before (btnp() of a name) */
    /* keymap(): the game's actions and their buttons (indices of button_names) */
    struct { char name[16]; uint8_t btn[4], n; } keymap[32];
    int nkeymap;
    int local;                      /* player of the keyboard / USB / serial, or -1 */
    uint32_t start_us, frame;
    uint32_t last_cpu_us, fps;
    uint32_t present_us;        /* last copy of the frame to the framebuffer */
    uint32_t us3d;              /* time in 3D drawing since the last zclear() */
    uint32_t frame_t0;          /* when this frame's _update began */
    int skip_max;               /* frameskip(n): up to n _update before a _draw (0, 1: one) */
    uint32_t updates;           /* the _update that ran before this frame's _draw (stat(15)) */
    uint32_t update_us, draw_us;    /* this frame's: all its _update, its _draw (the dev kit) */
    char devinfo[4][19];        /* devinfo(): the game's lines on the dev kit's detailed page */
    int ndevinfo;
    uint32_t hook_count;
    uint32_t frame_instr_k, last_instr_k;   /* Lua instructions (thousands): this frame, the last */
    /* the dev kit: the tokens of the code, and the most of this run */
    int tokens;
    uint32_t instr_k_max;       /* Lua instructions (thousands) of the busiest frame */
    size_t lua_peak;            /* bytes of Lua memory, seen at the end of a frame */
    uint32_t slow_frames;       /* frames over 16.7 ms (_update + _draw) */
    int perf_key;               /* 'p' on the serial line: the dev kit's overlay on or off */
    int f11_held;
    int keyhelp;                /* keyhelp(): the registry's reference of the cartridge's keys, or 0 */
    int help_page;              /* the page of the keys shown while F12 is held */
    int esc;
    int serial_quit;            /* Ctrl+\ on the serial line: Ctrl+Esc (the cartridge's _exit() asks) */
    int quit;
    r3d_t r3d;
    int r3d_ready;
    int zclear_seen;            /* zclear() called in this frame */
    int early;                  /* in the next frame's _update, run while the GPU draws (M35) */
    int early_touch;            /* ... which drew 3D or needed the page: */
    int no_early;               /* its _update runs after the frame again */
    int hold_frame;             /* this frame is not shown (the 3D went from the GPU to the ARM
                                 * in the middle of it: its depths would be mixed) */
    int zclear_dma;             /* the z-buffer is being (or has been) cleared by the DMA
                                 * for the next frame, and nothing has drawn on it since */
    g16_light_t light;          /* light_begin() .. light_end() */
    g16_fade_t fade;            /* fades(), dark_begin() .. dark_end() */
    int text_mode;              /* keyp() was called: the keyboard types */
    int raw_keys;               /* rawkeys(true): the keyboard is read with keydown() */
    lua_State *slice_thread;    /* timeslice(): this coroutine yields after slice_at */
    uint32_t slice_at, slice_len;
    int esc_wait;               /* frames since a serial Esc */
    int esc_num;                /* ESC [ n ~ */
    uint8_t tq[256];            /* typed keys for keyp() */
    uint8_t tq_head, tq_tail;
    char save_name[13];         /* "1A2B3C4D.SAV": CRC-32 of title and author */
    uint8_t *mesh;              /* copy of the cartridge's MESH section, or NULL */
    uint32_t mesh_size;
    uint8_t *anim;              /* copy of its ANIM section (skeletons), or NULL */
    uint32_t anim_size;
    int mouse, mouse_arrow;     /* mouse(true [, arrow]): the pointer is the cartridge's */
    int reports;                /* report() calls in this run */
    pointer_t ptr;              /* the pointer this frame */
    int want_w, want_h;         /* screen(w, h): the resolution from the next frame */
    int cls_pending;            /* a cls() left to the GPU's next job (cls_settle) */
    uint16_t cls_colour;
    int gpu2d;                  /* M37: the 2D over the 3D drawn by the GPU in its job (gpu3d_2d=1) */
    int online;                 /* online(true): played over the network, PS asks first */
    char online_note[64];       /* online()'s line under the question */
    int leave_ask;              /* "Leave the match?" is open (leave_step) */
    int leave_no;               /* Esc: its answer no */
    int online_left;            /* yes: the game ends (not suspended) */
    uint32_t leave_pad;         /* its buttons and keys last frame (for the presses) */
    uint8_t leave_keys;
    uint32_t leave_held;        /* the buttons it took: the game gets them back released */
    uint32_t raw_all;           /* this frame's HID bits, all players and the serial line */
} rt;

#define MESH_MT "bm.mesh"

/* ---------------------------------------------------------------- helpers */

static uint16_t col(lua_State *L, int idx, uint32_t def)
{
    return g16_rgb24((uint32_t)luaL_optinteger(L, idx, def));
}

static int ival(lua_State *L, int idx)
{
    return (int)luaL_checknumber(L, idx);
}

static int oval(lua_State *L, int idx, int def)
{
    return lua_isnoneornil(L, idx) ? def : (int)luaL_checknumber(L, idx);
}

static void sheet_commit(void)
{
    if (!rt.sheet_dirty)
        return;
    int per_row = rt.sheet.w / G16_CELL, n = per_row * (rt.sheet.h / G16_CELL);
    for (int i = 0; i < n; i++)
        if (rt.cell_dirty[i]) {
            g16_sheet_update_cell(&rt.sheet, i % per_row, i / per_row);
            rt.cell_dirty[i] = 0;
        }
    rt.sheet_dirty = 0;
}

/* ---------------------------------------------------------------- API */

/* M35: 2D drawn on the page while the GPU draws 3D on it (the frame
 * queue on): the GPU's job is started and the 2D is recorded here, then
 * drawn on the page in the same order once the job has ended (flush3d):
 * before more 3D, before the page is read, at the end of the frame. The
 * HUD of a game is drawn (as far as the cartridge goes) while the GPU
 * draws the scene, and the frame's 3D need not wait for it. */
enum { D2_STATE, D2_CLS, D2_PSET, D2_LINE, D2_RECT, D2_RECTFILL, D2_CIRC, D2_CIRCFILL, D2_SPR, D2_SSPR,
       D2_SSPR_ZOOM, D2_MAP, D2_TEXT, D2_PROMPT, D2_TRI, D2_TRI_GOURAUD };

#define D2_MAX (256u << 10)             /* bytes recorded at most: then drawn */

#define D2_NONE 0xFFFFFFFFu

static struct {
    int on;                             /* recording: the GPU has a job on the page */
    int32_t *w;                         /* the records: op | words << 8, then the words */
    uint32_t n, cap;                    /* words */
    uint32_t cut;                       /* the records from here are the next frame's (its
                                         * _update ran early), or D2_NONE */
    int cx0, cy0, cx1, cy1, cam_x, cam_y;   /* the drawing state of the records so far */
    const font_t *font;
    uint32_t ops;                       /* drawn after the GPU (for the log and the bench) */
} d2 = { .cut = D2_NONE };

static void draw_prompt(const prompt_t *p, int x, int y, int scale);

/* the 2D recorded before word upto, on the page (the GPU's job has
 * ended); what follows stays recorded (the next frame's) */
static void d2_replay(uint32_t upto)
{
    const g16_t live = rt.g;
    for (uint32_t i = 0; i < upto;) {
        const int32_t *a = &d2.w[i + 1];
        const int op = d2.w[i] & 255;
        i += 1 + ((uint32_t)d2.w[i] >> 8);
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
 * job was drawing (M35) then goes on the page. */
static void flush3d(int keep)
{
    /* (a frame started on the GPU, M35: waited for, the page has it) */
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

/* A cls() with the GPU drawing the 3D is not drawn by the ARM (at 1080p it
 * writes 4 MB): the GPU's next job clears its tiles to the colour. Before
 * anything else touches the page (2D, a read, the page shown, the ARM's 3D)
 * it is settled: if no job has cleared the page since, the ARM fills it. */
static void cls_settle(void)
{
    if (!rt.cls_pending)
        return;
    rt.cls_pending = 0;
    if (rt.r3d.backend && gpu3d_cleared())
        return;
    g16_cls(&rt.g, rt.cls_colour);
}

/* M35: the end of the frame's 3D started on the GPU and not waited for
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
static void sync3d(void)
{
    flush3d(1);
    cls_settle();
    if (rt.r3d.backend)
        gpu3d_page(0, 0);
}

/* before 3D, or before changing what recorded 2D reads (the map): the
 * recorded 2D goes on the page first */
static void settle2d(void)
{
    if (rt.early)
        rt.early_touch = 1;             /* 3D in the early _update: it lands on this frame */
    if (d2.on)
        flush3d(1);
}

/* M35: the next frame's _update runs while the GPU draws this one: the 2D
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

/* M37: the 2D over the 3D drawn by the GPU in the frame's job, where it
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

static int gpu2d(int op, const int32_t *v)
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
static int draw2d(int op, uint32_t words, int32_t **a)
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
static void gpu3d_to_arm(void *ctx, const char *why)
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
static int l_sspr(lua_State *L)
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

/* ---- the map (R11, 2026-10-04): up to 8 layers of the same size, drawn
 * in their order (layer 1, the MAP section, at the back), and 8 flags for
 * each 8x8 cell of the sheet (fget/fset), which map() can choose by and
 * mflags() reads under a rectangle (collisions with the map) */

static int sheet_cells(void)
{
    return (rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL);
}

/* the layer of argument idx: a number (1..) or a name; absent: layer 1.
 * Returns the index (0-based); an unknown layer is an error. */
static int layer_arg(lua_State *L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return 0;
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *n = lua_tostring(L, idx);
        for (int i = 0; i < rt.nlayers; i++)
            if (!strcmp(rt.layer_name[i], n))
                return i;
        return luaL_error(L, "the map has no layer \"%s\"", n);
    }
    lua_Integer i = luaL_checkinteger(L, idx);
    if (i < 1 || i > rt.nlayers)
        return luaL_error(L, "the map has %d layer%s, not %d", rt.nlayers, rt.nlayers == 1 ? "" : "s", (int)i);
    return (int)i - 1;
}

/* map(mx, my, [x, y, mw, mh, layer, mask]): mask, only the cells whose
 * tile has one of those flags */
static int l_map(lua_State *L)
{
    const int32_t v[8] = { ival(L, 1), ival(L, 2), oval(L, 3, 0), oval(L, 4, 0), oval(L, 5, rt.map.w),
                           oval(L, 6, rt.map.h), layer_arg(L, 7), oval(L, 8, 0) & 255 };
    int32_t *a;
    sheet_commit();
    if (gpu2d(D2_MAP, v))
        return 0;
    if (draw2d(D2_MAP, 8, &a)) {
        memcpy(a, v, sizeof v);
    } else {
        g16_map_t m = { rt.map.w, rt.map.h, rt.layer[v[6]] };
        g16_map_mask(&rt.g, &rt.sheet, &m, v[0], v[1], v[2], v[3], v[4], v[5], rt.flags, (uint8_t)v[7]);
    }
    return 0;
}

static int l_mget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2), l = layer_arg(L, 3);
    lua_pushinteger(L, (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h) ? rt.layer[l][y * rt.map.w + x] : 0);
    return 1;
}

static int l_mset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2), n = ival(L, 3), l = layer_arg(L, 4);
    if (x >= 0 && y >= 0 && x < rt.map.w && y < rt.map.h) {
        if (d2.on)
            flush3d(1);                 /* a map() recorded draws the map as it was */
        rt.layer[l][y * rt.map.w + x] = (uint16_t)n;
    }
    return 0;
}

/* fget(n, [f]): the flags of sprite (cell) n, a byte; with f (0-7), that
 * flag as a boolean */
static int l_fget(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int v = rt.flags && n >= 0 && n < sheet_cells() ? rt.flags[n] : 0;
    if (lua_isnoneornil(L, 2)) {
        lua_pushinteger(L, v);
        return 1;
    }
    lua_Integer f = luaL_checkinteger(L, 2);
    luaL_argcheck(L, f >= 0 && f < 8, 2, "a flag is 0 to 7");
    lua_pushboolean(L, v >> f & 1);
    return 1;
}

/* fset(n, f, on) sets or clears flag f (0-7) of sprite n; fset(n, byte)
 * all its flags at once */
static int l_fset(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int v;
    if (lua_gettop(L) >= 3) {
        lua_Integer f = luaL_checkinteger(L, 2);
        luaL_argcheck(L, f >= 0 && f < 8, 2, "a flag is 0 to 7");
        v = -1 - (int)f;                /* below: that bit */
    } else {
        v = (int)(luaL_checkinteger(L, 2) & 255);
    }
    if (!rt.flags || n < 0 || n >= sheet_cells())
        return 0;
    if (d2.on)
        flush3d(1);                     /* a map() recorded with a mask draws them as they were */
    if (v < 0) {
        uint8_t bit = (uint8_t)(1u << (-1 - v));
        if (lua_toboolean(L, 3)) rt.flags[n] |= bit;
        else rt.flags[n] &= (uint8_t)~bit;
    } else {
        rt.flags[n] = (uint8_t)v;
    }
    rt.flags_dirty = 1;
    return 0;
}

/* mflags(x, y, [w, h, layer]): the flags of the tiles the rectangle
 * touches (pixels of the map drawn at 0, 0; a point without w and h), all
 * together (or); 0 = nothing there. The empty cell (0) and the outside of
 * the map have none. For collisions: mflags(x, y + h, w, 1) & SOLID. */
static int l_mflags(lua_State *L)
{
    lua_Number x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2);
    lua_Number w = luaL_optnumber(L, 3, 0), h = luaL_optnumber(L, 4, 0);
    int l = layer_arg(L, 5);
    lua_Integer c0 = (lua_Integer)floor(x / G16_CELL), r0 = (lua_Integer)floor(y / G16_CELL);
    lua_Integer c1 = w > 0 ? (lua_Integer)ceil((x + w) / G16_CELL) - 1 : c0;
    lua_Integer r1 = h > 0 ? (lua_Integer)ceil((y + h) / G16_CELL) - 1 : r0;
    if (c1 < c0) c1 = c0;
    if (r1 < r0) r1 = r0;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= rt.map.w) c1 = rt.map.w - 1;
    if (r1 >= rt.map.h) r1 = rt.map.h - 1;
    int out = 0;
    const int nc = sheet_cells();
    for (lua_Integer r = r0; rt.flags && r <= r1; r++)
        for (lua_Integer c = c0; c <= c1; c++) {
            int n = rt.layer[l][r * rt.map.w + c];
            if (n > 0 && n < nc)
                out |= rt.flags[n];
        }
    lua_pushinteger(L, out);
    return 1;
}

/* msize([w, h]) -> w, h, layers: the map's size in cells and its layers;
 * with w and h every layer gets that size, the cells that fit staying where
 * they are */
static int l_msize(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        lua_Integer w = luaL_checkinteger(L, 1), h = luaL_checkinteger(L, 2);
        luaL_argcheck(L, w >= 1 && w <= 65535, 1, "1 to 65535 cells");
        luaL_argcheck(L, h >= 1 && h <= 65535, 2, "1 to 65535 cells");
        luaL_argcheck(L, w * h <= (1 << 22), 2, "at most 4194304 cells");
        if (w != rt.map.w || h != rt.map.h) {
            uint16_t *nl[BM_LAYERS_MAX] = { 0 };
            for (int i = 0; i < rt.nlayers; i++)
                if (!(nl[i] = calloc((size_t)(w * h), 2))) {
                    for (int k = 0; k < i; k++)
                        free(nl[k]);
                    return luaL_error(L, "not enough memory for a %dx%d map", (int)w, (int)h);
                }
            if (d2.on)
                flush3d(1);
            int cw = w < rt.map.w ? (int)w : rt.map.w, ch = h < rt.map.h ? (int)h : rt.map.h;
            for (int i = 0; i < rt.nlayers; i++) {
                for (int y = 0; y < ch; y++)
                    memcpy(nl[i] + (size_t)y * w, rt.layer[i] + (size_t)y * rt.map.w, (size_t)cw * 2);
                free(rt.layer[i]);
                rt.layer[i] = nl[i];
            }
            rt.map.w = (int)w;
            rt.map.h = (int)h;
            rt.map.cells = rt.layer[0];
        }
    }
    lua_pushinteger(L, rt.map.w);
    lua_pushinteger(L, rt.map.h);
    lua_pushinteger(L, rt.nlayers);
    return 3;
}

/* mlayers([list]) -> the names of the layers, in their order. With a list
 * (1 to 8 entries) the map gets those layers: an entry is a name (the layer
 * of that name, or a new empty one) or {name, from} (a copy of layer `from`,
 * a number or a name; false: empty), so they can be renamed, moved, added
 * and taken away. */
static int l_mlayers(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_Integer n = luaL_len(L, 1);
        luaL_argcheck(L, n >= 1 && n <= BM_LAYERS_MAX, 1, "1 to 8 layers");
        char names[BM_LAYERS_MAX][BM_LAYER_NAME + 1];
        int from[BM_LAYERS_MAX];
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, 1, i + 1);
            int t = lua_gettop(L);
            const char *name;
            from[i] = -1;
            if (lua_istable(L, t)) {
                lua_rawgeti(L, t, 1);
                name = luaL_checkstring(L, -1);
                lua_rawgeti(L, t, 2);
                if (lua_type(L, -1) == LUA_TSTRING || lua_isinteger(L, -1))
                    from[i] = layer_arg(L, lua_gettop(L));
                lua_pop(L, 2);
            } else {
                name = luaL_checkstring(L, t);
                for (int k = 0; k < rt.nlayers; k++)
                    if (!strcmp(rt.layer_name[k], name))
                        from[i] = k;
            }
            if (!name[0] || strlen(name) > BM_LAYER_NAME)
                return luaL_error(L, "mlayers: a layer's name is 1 to %d bytes", BM_LAYER_NAME);
            ksnprintf(names[i], sizeof names[i], "%s", name);
            for (int k = 0; k < i; k++)
                if (!strcmp(names[k], names[i]))
                    return luaL_error(L, "mlayers: two layers called \"%s\"", name);
            lua_settop(L, t - 1);
        }
        uint16_t *nl[BM_LAYERS_MAX] = { 0 };
        int taken[BM_LAYERS_MAX] = { 0 };
        const size_t cells = (size_t)rt.map.w * rt.map.h;
        for (int i = 0; i < n; i++) {
            if (from[i] >= 0 && !taken[from[i]]) {
                nl[i] = rt.layer[from[i]];      /* the layer itself */
                taken[from[i]] = 1;
                continue;
            }
            if (!(nl[i] = calloc(cells, 2))) {
                for (int k = 0; k < i; k++)
                    if (!(from[k] >= 0 && nl[k] == rt.layer[from[k]]))
                        free(nl[k]);
                return luaL_error(L, "not enough memory for the map's layers");
            }
            if (from[i] >= 0)
                memcpy(nl[i], rt.layer[from[i]], cells * 2);
        }
        if (d2.on)
            flush3d(1);
        for (int k = 0; k < rt.nlayers; k++)
            if (!taken[k])
                free(rt.layer[k]);
        for (int i = 0; i < BM_LAYERS_MAX; i++) {
            rt.layer[i] = i < n ? nl[i] : NULL;
            ksnprintf(rt.layer_name[i], sizeof rt.layer_name[i], "%s", i < n ? names[i] : "");
        }
        rt.nlayers = (int)n;
        rt.map.cells = rt.layer[0];
    }
    lua_createtable(L, rt.nlayers, 0);
    for (int i = 0; i < rt.nlayers; i++) {
        lua_pushstring(L, rt.layer_name[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* ---- named zones of the sheet (the SPRITES section: bm Pixel, bmres.py,
 * mkbm.py --sprites): sprites and animations by their name */

/* the zone called `name`, or -1 */
static int zone_find(const char *name)
{
    if (strlen(name) > BM_MODEL_NAME)
        return -1;
    for (int i = 0; i < rt.nzones; i++)
        if (!strncmp((const char *)rt.zones + 4 + i * BM_SPRITE_SIZE, name, BM_MODEL_NAME))
            return i;
    return -1;
}

static int zone_arg(lua_State *L, int idx, bm_zone_t *z)
{
    const char *name = luaL_checkstring(L, idx);
    int i = zone_find(name);
    if (i < 0)
        return luaL_error(L, "the sheet has no sprite zone \"%s\"", name);
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    c.sprites = rt.zones;
    c.zones = (uint16_t)rt.nzones;
    bm_zone(&c, i, z);
    return i;
}

/* zone(name) -> x, y, w, h, frames, fps: where the zone is in the sheet (its
 * first frame), or nil */
static int l_zone(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (zone_find(name) < 0) {
        lua_pushnil(L);
        return 1;
    }
    bm_zone_t z;
    zone_arg(L, 1, &z);
    lua_pushinteger(L, z.x);
    lua_pushinteger(L, z.y);
    lua_pushinteger(L, z.w);
    lua_pushinteger(L, z.h);
    lua_pushinteger(L, z.frames);
    lua_pushinteger(L, z.fps);
    return 6;
}

/* zones() -> the names of the zones, in their order */
static int l_zones(lua_State *L)
{
    lua_createtable(L, rt.nzones, 0);
    for (int i = 0; i < rt.nzones; i++) {
        lua_pushlstring(L, (const char *)rt.zones + 4 + i * BM_SPRITE_SIZE,
                        strnlen((const char *)rt.zones + 4 + i * BM_SPRITE_SIZE, BM_MODEL_NAME));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static const char *const box_kinds[] = { "hurt", "hit", "body" };

/* zboxes(name, [frame, kind]) -> the boxes of a zone's frame (hitboxes and
 * hurtboxes, the BOXES section): a list of {x, y, w, h, kind, frame}, x and
 * y from the frame's top-left corner, kind "hurt", "hit", "body" or the
 * game's number (3..255). With frame (1..frames) its boxes and those of
 * every frame (frame 0); without, all the zone's. kind (a name or a
 * number) keeps those of one kind. An empty list if there are none. */
static int l_zboxes(lua_State *L)
{
    bm_zone_t z;
    zone_arg(L, 1, &z);
    int frame = 0, kind = -1;
    if (!lua_isnoneornil(L, 2))
        frame = (int)((luaL_checkinteger(L, 2) - 1) % z.frames + z.frames) % z.frames + 1;
    if (lua_type(L, 3) == LUA_TSTRING) {
        const char *k = lua_tostring(L, 3);
        for (int i = 0; i < 3; i++)
            if (!strcmp(k, box_kinds[i]))
                kind = i;
        if (kind < 0)
            return luaL_error(L, "zboxes: kind \"%s\" (hurt, hit, body or a number)", k);
    } else if (!lua_isnoneornil(L, 3)) {
        kind = (int)luaL_checkinteger(L, 3);
    }
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    c.boxes = rt.boxes;
    c.nboxes = (uint16_t)rt.nboxes;
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < rt.nboxes; i++) {
        bm_box_t b;
        bm_box(&c, i, &b);
        if (strcmp(b.zone, z.name) || (frame && b.frame && b.frame != frame) || (kind >= 0 && b.kind != kind))
            continue;
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, b.x); lua_setfield(L, -2, "x");
        lua_pushinteger(L, b.y); lua_setfield(L, -2, "y");
        lua_pushinteger(L, b.w); lua_setfield(L, -2, "w");
        lua_pushinteger(L, b.h); lua_setfield(L, -2, "h");
        if (b.kind < 3)
            lua_pushstring(L, box_kinds[b.kind]);
        else
            lua_pushinteger(L, b.kind);
        lua_setfield(L, -2, "kind");
        lua_pushinteger(L, b.frame); lua_setfield(L, -2, "frame");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int l_sspr(lua_State *L);

/* zspr(name, x, y, [frame, flip_x, flip_y, zoom]) draws a zone: frame 1..
 * frames, or (nil) the one its fps gives at time(); returns the frame */
static int l_zspr(lua_State *L)
{
    bm_zone_t z;
    zone_arg(L, 1, &z);
    int f;
    if (lua_isnoneornil(L, 4))
        f = z.fps ? (int)((uint64_t)(uint32_t)(timer_ticks() - rt.start_us) * z.fps / 1000000u % z.frames) : 0;
    else
        f = (int)((luaL_checkinteger(L, 4) - 1) % z.frames + z.frames) % z.frames;
    lua_Number x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3);
    int fx = lua_toboolean(L, 5), fy = lua_toboolean(L, 6);
    lua_Number zoom = luaL_optnumber(L, 7, 1);
    lua_settop(L, 0);
    lua_pushinteger(L, z.x + f * z.w);
    lua_pushinteger(L, z.y);
    lua_pushinteger(L, z.w);
    lua_pushinteger(L, z.h);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushboolean(L, fx);
    lua_pushboolean(L, fy);
    lua_pushnumber(L, zoom);
    l_sspr(L);
    lua_pushinteger(L, f + 1);
    return 1;
}

static int l_sget(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h || !rt.sheet.alpha[y * rt.sheet.w + x])
        lua_pushnil(L);
    else
        lua_pushinteger(L, g16_to_rgb24(rt.sheet.px[y * rt.sheet.w + x]));
    return 1;
}

/* sset(x, y, colour) - nil colour = transparent */
static int l_sset(lua_State *L)
{
    int x = ival(L, 1), y = ival(L, 2);
    if (x < 0 || y < 0 || x >= rt.sheet.w || y >= rt.sheet.h)
        return 0;
    int opaque = !lua_isnoneornil(L, 3);
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    g16_sheet_set(&rt.sheet, x, y, opaque ? col(L, 3, 0) : 0, opaque);
    rt.cell_dirty[(y / G16_CELL) * (rt.sheet.w / G16_CELL) + x / G16_CELL] = 1;
    rt.sheet_dirty = 1;
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
        g16_t m = rt.g;                 /* where it ends, as drawn (M37: the GPU drew it) */
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

/* ---------------------------------------------------------------- prompts */

/* the pad's buttons, upper case: as on a DS4, or on a lettered pad */
static const struct { const char *name; uint8_t ds4, pad; } pad_prompts[] = {
    { "A", PROMPT_CROSS, PROMPT_PAD_A }, { "B", PROMPT_CIRCLE, PROMPT_PAD_B },
    { "X", PROMPT_SQUARE, PROMPT_PAD_X }, { "Y", PROMPT_TRIANGLE, PROMPT_PAD_Y },
    { "START", PROMPT_OPTIONS, PROMPT_PAD_START }, { "SELECT", PROMPT_SHARE, PROMPT_PAD_SELECT },
    { "L1", PROMPT_L1, PROMPT_L1 }, { "R1", PROMPT_R1, PROMPT_R1 },
    { "L2", PROMPT_L2, PROMPT_L2 }, { "R2", PROMPT_R2, PROMPT_R2 },
    { "L3", PROMPT_L3, PROMPT_L3 }, { "R3", PROMPT_R3, PROMPT_R3 },
    { "LSTICK", PROMPT_LSTICK, PROMPT_LSTICK }, { "RSTICK", PROMPT_RSTICK, PROMPT_RSTICK },
    { "DPAD", PROMPT_DPAD, PROMPT_DPAD }, { "UP", PROMPT_DPAD_UP, PROMPT_DPAD_UP },
    { "DOWN", PROMPT_DPAD_DOWN, PROMPT_DPAD_DOWN }, { "LEFT", PROMPT_DPAD_LEFT, PROMPT_DPAD_LEFT },
    { "RIGHT", PROMPT_DPAD_RIGHT, PROMPT_DPAD_RIGHT }, { "UPDOWN", PROMPT_DPAD_UPDOWN, PROMPT_DPAD_UPDOWN },
    { "LEFTRIGHT", PROMPT_DPAD_LEFTRIGHT, PROMPT_DPAD_LEFTRIGHT },
    { "PS", PROMPT_PS, PROMPT_PS }, { "TOUCHPAD", PROMPT_TOUCHPAD, PROMPT_TOUCHPAD },
    { "CROSS", PROMPT_CROSS, PROMPT_CROSS }, { "CIRCLE", PROMPT_CIRCLE, PROMPT_CIRCLE },
    { "SQUARE", PROMPT_SQUARE, PROMPT_SQUARE }, { "TRIANGLE", PROMPT_TRIANGLE, PROMPT_TRIANGLE },
    { "OPTIONS", PROMPT_OPTIONS, PROMPT_OPTIONS }, { "SHARE", PROMPT_SHARE, PROMPT_SHARE },
};

/* the keyboard's keys with a name, lower case (the names of keyp()) */
static const struct { const char *name; uint8_t id; } key_prompts[] = {
    { "up", PROMPT_KEY_UP }, { "down", PROMPT_KEY_DOWN }, { "left", PROMPT_KEY_LEFT },
    { "right", PROMPT_KEY_RIGHT }, { "enter", PROMPT_KEY_ENTER }, { "esc", PROMPT_KEY_ESC },
    { "space", PROMPT_KEY_SPACE }, { "tab", PROMPT_KEY_TAB }, { "backspace", PROMPT_KEY_BACKSPACE },
    { "shift", PROMPT_KEY_SHIFT }, { "ctrl", PROMPT_KEY_CTRL }, { "alt", PROMPT_KEY_ALT },
    { "del", PROMPT_KEY_DEL }, { "home", PROMPT_KEY_HOME }, { "end", PROMPT_KEY_END },
    { "pgup", PROMPT_KEY_PGUP }, { "pgdn", PROMPT_KEY_PGDN },
};

/* the pad the prompts show: the one pressed last, a DS4 until then */
static int prompt_lettered;

static const prompt_t *find_prompt(const char *n, int small)
{
    int src = hid_last_source();
    if (src == HID_SOURCE_DS4 || src == HID_SOURCE_PAD)
        prompt_lettered = src == HID_SOURCE_PAD;
    for (size_t i = 0; i < sizeof pad_prompts / sizeof *pad_prompts; i++)
        if (!strcmp(n, pad_prompts[i].name))
            return prompt_chip(prompt_lettered ? pad_prompts[i].pad : pad_prompts[i].ds4, small);
    for (size_t i = 0; i < sizeof key_prompts / sizeof *key_prompts; i++)
        if (!strcmp(n, key_prompts[i].name))
            return prompt_chip(key_prompts[i].id, small);
    if (n[0] == 'f' && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12 && (n[2] == 0 || (k >= 10 && n[3] == 0)))
            return prompt_chip(PROMPT_KEY_F1 + k - 1, small);
    }
    if (n[0] && !n[1] && !(n[0] >= 'A' && n[0] <= 'Z'))
        return prompt_chip_key((unsigned char)n[0], small);
    return NULL;
}

/* a prompt's picture at x, y (its edges blended over the page), each of
 * its pixels scale x scale */
static void draw_prompt(const prompt_t *p, int x, int y, int scale)
{
    for (int j = 0; j < p->h; j++)
        for (int i = 0; i < p->w; i++) {
            uint32_t c = p->px[j * p->w + i], a = c >> 24;
            if (!a)
                continue;
            const int px = x + i * scale, py = y + j * scale;
            if (a < 255) {                      /* the edges: over what is there */
                int b = g16_pget(&rt.g, px, py);
                if (b < 0)
                    continue;
                uint32_t under = g16_to_rgb24((uint16_t)b), out = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    int u = (int)(under >> sh & 255), v = (int)(c >> sh & 255);
                    out |= (uint32_t)(u + (v - u) * (int)a / 255) << sh;
                }
                c = out;
            }
            if (scale == 1)
                g16_pset(&rt.g, px, py, g16_rgb24(c & 0xFFFFFF));
            else
                g16_rectfill(&rt.g, px, py, scale, scale, g16_rgb24(c & 0xFFFFFF));
        }
}

static const prompt_t *prompt_for(const char *n, int small, int player);

/* prompt(name, x, y [, small, scale, player]): a button or a key as a chip
 * of the apps' set (prompts.c), its top left at (x, y), 16 px high for 8x16
 * text, 12 with small (by default when the font is 6x12), scale times
 * larger (1-8, as print's); returns the x after it. prompt(name [, small,
 * scale, player]) only measures: the width and height.
 * Upper case the pad's buttons ("A", "B", "X", "Y", "START", "L1",
 * "UPDOWN"...), shown as on the pad pressed last: a DS4 (cross, circle...)
 * until another pad is used. Lower case the keyboard's keys, with the
 * names of keyp() ("enter", "esc", "f1", "up") or one character ("s").
 * "ok", "back" and the actions of keymap() are their buttons. With player
 * (1-4) a button as that player's controller shows it: a DS4's symbol,
 * a pad's letter, or the keyboard's key that presses it. */
static int l_prompt(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int measure = !lua_isnumber(L, 2), at = measure ? 2 : 4;
    int small = lua_isnoneornil(L, at) ? rt.g.font->height <= 12 : lua_toboolean(L, at);
    int scale = (int)luaL_optinteger(L, at + 1, 1);
    scale = scale < 1 ? 1 : scale > 8 ? 8 : scale;
    int player = (int)luaL_optinteger(L, at + 2, 0);
    const prompt_t *p = prompt_for(n, small, player);
    if (!p)
        return luaL_argerror(L, 1, "not a button or a key");
    if (measure) {
        lua_pushinteger(L, p->w * scale);
        lua_pushinteger(L, p->h * scale);
        return 2;
    }
    int x = ival(L, 2), y = ival(L, 3);
    int32_t *a;
    if (draw2d(D2_PROMPT, 3 + sizeof(void *) / 4, &a)) {
        a[0] = x;
        a[1] = y;
        a[2] = scale;
        memcpy(a + 3, &p, sizeof p);
    } else {
        draw_prompt(p, x, y, scale);
    }
    lua_pushinteger(L, x + p->w * scale);
    return 1;
}

/* lastinput(): what pressed something last, "keyboard", "ds4" or "pad"
 * (another controller); nil before anything is pressed */
static int l_lastinput(lua_State *L)
{
    int s = hid_last_source();
    if (s == HID_SOURCE_NONE)
        lua_pushnil(L);
    else
        lua_pushstring(L, s == HID_SOURCE_KEYBOARD ? "keyboard" : s == HID_SOURCE_DS4 ? "ds4" : "pad");
    return 1;
}

static int l_camera(lua_State *L) { g16_camera(&rt.g, oval(L, 1, 0), oval(L, 2, 0)); return 0; }
static int l_clip(lua_State *L)   { g16_clip(&rt.g, oval(L, 1, 0), oval(L, 2, 0), oval(L, 3, 0), oval(L, 4, 0)); return 0; }

static int l_rgb(lua_State *L)
{
    lua_Integer r = luaL_checkinteger(L, 1) & 255, g = luaL_checkinteger(L, 2) & 255, b = luaL_checkinteger(L, 3) & 255;
    lua_pushinteger(L, r << 16 | g << 8 | b);
    return 1;
}

/* The buttons by name (btn("jump"), keymap(), prompt("ok"), 2026-10-04):
 * the pad's, how each shows on a pad (prompt()'s upper-case names) and the
 * keyboard's key that presses it in a game (hid.c's key_button) */
static const struct { const char *name; uint32_t hid; const char *chip, *key; } button_names[] = {
    { "left", HID_LEFT, "LEFT", "left" }, { "right", HID_RIGHT, "RIGHT", "right" },
    { "up", HID_UP, "UP", "up" }, { "down", HID_DOWN, "DOWN", "down" },
    { "a", HID_A, "A", "space" }, { "b", HID_B, "B", "x" }, { "x", HID_X, "X", "c" }, { "y", HID_Y, "Y", "v" },
    { "start", HID_START, "START", "enter" }, { "select", HID_SELECT, "SELECT", "tab" },
    { "l1", HID_L1, "L1", "q" }, { "r1", HID_R1, "R1", "e" },
    { "l2", HID_L2, "L2", NULL }, { "r2", HID_R2, "R2", NULL },
    { "l3", HID_L3, "L3", NULL }, { "r3", HID_R3, "R3", NULL },
};
#define NBUTTONS ((int)(sizeof button_names / sizeof button_names[0]))
#define BUTTON_OK   NBUTTONS                    /* "ok" and "back": the system's yes and back */
#define BUTTON_BACK (NBUTTONS + 1)

static int name_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a | 0x20) != (*b | 0x20))
            return 0;
    return *a == *b;
}

/* a button's index (BUTTON_OK, BUTTON_BACK too), or -1 */
static int button_index(const char *n)
{
    for (int i = 0; i < NBUTTONS; i++)
        if (name_is(n, button_names[i].name))
            return i;
    if (name_is(n, "ok"))
        return BUTTON_OK;
    if (name_is(n, "back"))
        return BUTTON_BACK;
    return -1;
}

/* the index of a button for the game: "ok" and "back" are A or B */
static int button_real(int i)
{
    if (i < NBUTTONS)
        return i;
    const uint32_t bit = input_ok_bit(i == BUTTON_BACK);
    return bit == HID_A ? 4 : 5;                /* "a", "b" in button_names */
}

/* the HID bits of a name: a button, "ok" / "back", or an action of
 * keymap(); 0 if it is none of them */
static uint32_t name_mask(const char *n)
{
    int i = button_index(n);
    if (i >= 0)
        return button_names[button_real(i)].hid;
    for (int k = 0; k < rt.nkeymap; k++)
        if (!strcmp(rt.keymap[k].name, n)) {
            uint32_t m = 0;
            for (int j = 0; j < rt.keymap[k].n; j++)
                m |= button_names[button_real(rt.keymap[k].btn[j])].hid;
            return m;
        }
    return 0;
}

/* the first button of a name (for its picture), or -1 */
static int name_button(const char *n)
{
    int i = button_index(n);
    if (i >= 0)
        return button_real(i);
    for (int k = 0; k < rt.nkeymap; k++)
        if (!strcmp(rt.keymap[k].name, n) && rt.keymap[k].n)
            return button_real(rt.keymap[k].btn[0]);
    return -1;
}

/* btn(name [, p]), btnp(name [, p]) with a name: the HID bits of the
 * players (any of them without p) */
static int named_button(lua_State *L, int pressed)
{
    const char *n = luaL_checkstring(L, 1);
    uint32_t mask = name_mask(n);
    if (!mask)
        return luaL_argerror(L, 1, "not a button, \"ok\", \"back\" or an action of keymap()");
    if (mask & (HID_X | HID_Y))
        rt.uses_xy = 1;                         /* it asks for X or Y: they are its own */
    uint32_t now = 0, prev = 0;
    if (lua_isnoneornil(L, 2)) {
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            now |= rt.praw[p];
            prev |= rt.praw_prev[p];
        }
    } else {
        int p = ival(L, 2);
        if (p >= 1 && p <= INPUT_PLAYERS) {
            now = rt.praw[p - 1];
            prev = rt.praw_prev[p - 1];
        }
    }
    lua_pushboolean(L, pressed ? (now & ~prev & mask) != 0 : (now & mask) != 0);
    return 1;
}

/* keymap({action = "a" or {"a", "x"}, ...}): the game's actions on the
 * buttons (and "ok" / "back"), read with btn("action"), btnp() and drawn
 * with prompt("action"); the game remaps by calling it again (its options
 * menu, saved with save()). keymap() gives the table back, keymap(nil)
 * forgets it. */
static int l_keymap(lua_State *L)
{
    if (lua_isnone(L, 1)) {
        lua_createtable(L, 0, rt.nkeymap);
        for (int k = 0; k < rt.nkeymap; k++) {
            lua_createtable(L, rt.keymap[k].n, 0);
            for (int j = 0; j < rt.keymap[k].n; j++) {
                int b = rt.keymap[k].btn[j];
                lua_pushstring(L, b == BUTTON_OK ? "ok" : b == BUTTON_BACK ? "back" : button_names[b].name);
                lua_rawseti(L, -2, j + 1);
            }
            lua_setfield(L, -2, rt.keymap[k].name);
        }
        return 1;
    }
    rt.nkeymap = 0;
    if (lua_isnil(L, 1))
        return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            return luaL_error(L, "keymap: the actions are names (strings)");
        const char *action = lua_tostring(L, -2);
        if (rt.nkeymap >= (int)(sizeof rt.keymap / sizeof rt.keymap[0]))
            return luaL_error(L, "keymap: at most %d actions", (int)(sizeof rt.keymap / sizeof rt.keymap[0]));
        if (button_index(action) >= 0)
            return luaL_error(L, "keymap: \"%s\" is a button's name, not an action's", action);
        __typeof__(rt.keymap[0]) *k = &rt.keymap[rt.nkeymap];
        memset(k, 0, sizeof *k);
        ksnprintf(k->name, sizeof k->name, "%s", action);
        int one = lua_type(L, -1) == LUA_TSTRING, count = one ? 1 : (int)luaL_len(L, -1);
        if (!one)
            luaL_checktype(L, -1, LUA_TTABLE);
        for (int j = 0; j < count && k->n < 4; j++) {
            if (!one)
                lua_rawgeti(L, -1, j + 1);
            const char *b = lua_tostring(L, -1);
            int i = b ? button_index(b) : -1;
            if (i < 0)
                return luaL_error(L, "keymap: \"%s\": no button \"%s\"", action, b ? b : "?");
            if (button_real(i) == 6 || button_real(i) == 7)
                rt.uses_xy = 1;                 /* "x", "y" */
            k->btn[k->n++] = (uint8_t)i;
            if (!one)
                lua_pop(L, 1);
        }
        rt.nkeymap++;
        lua_pop(L, 1);
    }
    return 0;
}

/* the players' colours: those of the pads' light bars (bt.c), bright */
static const uint32_t player_rgb[INPUT_PLAYERS] = { 0x3070FF, 0xFF3C28, 0x28D848, 0xFF38A8 };

/* controller([p]): what player p (1-4, the first by default) plays with:
 * {kind = "keyboard" | "ds4" | "xbox" | "pad" | "builtin" | "none",
 *  layout = "keyboard" | "ds4" | "xbox" | "nintendo" | "none",
 *  bluetooth = bool, ok = "a" | "b", back = "b" | "a" (the game's
 * buttons that say yes and go back, as the system's menus), color = the
 * player's colour (the light bar's: 1 blue, 2 red, 3 green, 4 pink)} */
static int l_controller(lua_State *L)
{
    int p = (int)luaL_optinteger(L, 1, 1);
    int d = p >= 1 && p <= INPUT_PLAYERS ? input_device(p - 1) : INPUT_DEV_NONE;
    int kind = d & INPUT_DEV_KIND;
    const char *k = "none", *layout = "none";
    if (kind == INPUT_DEV_KEYBOARD) {
        k = layout = "keyboard";
    } else if (kind == INPUT_DEV_PAD) {
        k = d & INPUT_DEV_DS4 ? "ds4" : d & INPUT_DEV_XBOX ? "xbox" : d & INPUT_DEV_BUILTIN ? "builtin" : "pad";
        layout = d & INPUT_DEV_DS4 ? "ds4" : d & INPUT_DEV_BUILTIN ? "nintendo" : "xbox";
    }
    lua_createtable(L, 0, 6);
    lua_pushstring(L, k);
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, layout);
    lua_setfield(L, -2, "layout");
    lua_pushboolean(L, (d & INPUT_DEV_BLUETOOTH) != 0);
    lua_setfield(L, -2, "bluetooth");
    lua_pushstring(L, input_ok_bit(0) == HID_A ? "a" : "b");
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, input_ok_bit(1) == HID_A ? "a" : "b");
    lua_setfield(L, -2, "back");
    lua_pushinteger(L, player_rgb[(p >= 1 && p <= INPUT_PLAYERS ? p : 1) - 1]);
    lua_setfield(L, -2, "color");
    return 1;
}

/* a pad's button as a chip: the DS4's symbol or the letter (find_prompt
 * chooses by the pad pressed last; here the caller does) */
static const prompt_t *pad_chip(const char *chip, int lettered, int small)
{
    for (size_t i = 0; i < sizeof pad_prompts / sizeof *pad_prompts; i++)
        if (!strcmp(chip, pad_prompts[i].name))
            return prompt_chip(lettered ? pad_prompts[i].pad : pad_prompts[i].ds4, small);
    return NULL;
}

/* the chip of button b (button_names) on a device: INPUT_DEV_* flags, or
 * -1 for the one pressed last */
static const prompt_t *button_chip(int b, int dev, int small)
{
    int kb = dev < 0 ? hid_last_source() == HID_SOURCE_KEYBOARD || hid_last_source() == HID_SOURCE_NONE
                     : (dev & INPUT_DEV_KIND) == INPUT_DEV_KEYBOARD;
    if (kb && button_names[b].key)
        return find_prompt(button_names[b].key, small);
    /* a pad with letters shows the letter the game's button is under */
    uint32_t shown = input_face_shown(button_names[b].hid);
    for (int i = 0; i < NBUTTONS; i++)
        if (button_names[i].hid == shown)
            b = i;
    if (dev < 0 || (dev & INPUT_DEV_KIND) != INPUT_DEV_PAD)
        return find_prompt(button_names[b].chip, small);
    return pad_chip(button_names[b].chip, !(dev & INPUT_DEV_DS4), small);
}

static const prompt_t *prompt_for(const char *n, int small, int player)
{
    int b = -1;
    if (name_is(n, "ok") || name_is(n, "back")) {
        b = name_button(n);
    } else {
        for (int k = 0; k < rt.nkeymap && b < 0; k++)
            if (!strcmp(rt.keymap[k].name, n))
                b = name_button(n);
        /* a pad's button by its upper-case name, for a player */
        for (int i = 0; b < 0 && player && i < NBUTTONS; i++)
            if (!strcmp(n, button_names[i].chip))
                b = i;
    }
    if (b < 0)
        return find_prompt(n, small);
    int dev = player >= 1 && player <= INPUT_PLAYERS ? input_device(player - 1) : -1;
    return button_chip(b, dev, small);
}

/* A cartridge that never asks for X or Y gets them as A and B (square and
 * triangle keep working in the older games). */
static void note_xy(int b)
{
    if (b == BTN_X || b == BTN_Y)
        rt.uses_xy = 1;
}

/* btn(i [, p]): without p any player; p = 1..4 that player only */
static int buttons(lua_State *L, uint16_t *now, uint16_t *prev)
{
    int b = ival(L, 1);
    note_xy(b);
    if (lua_isnoneornil(L, 2)) {
        *now = rt.now;
        *prev = rt.prev;
    } else {
        int p = ival(L, 2);
        *now = p >= 1 && p <= INPUT_PLAYERS ? rt.pnow[p - 1] : 0;
        *prev = p >= 1 && p <= INPUT_PLAYERS ? rt.pprev[p - 1] : 0;
    }
    return b >= 0 && b < BTN_COUNT ? b : -1;
}

static int l_btn(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING)
        return named_button(L, 0);
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1));
    return 1;
}

static int l_btnp(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING)
        return named_button(L, 1);
    uint16_t now, prev;
    int b = buttons(L, &now, &prev);
    lua_pushboolean(L, b >= 0 && (now >> b & 1) && !(prev >> b & 1));
    return 1;
}

/* players() -> how many players have a controller, and which (bit n =
 * player n+1) */
static int l_players(lua_State *L)
{
    unsigned m = input_connected(), n = 0;
    for (int p = 0; p < INPUT_PLAYERS; p++)
        n += m >> p & 1;
    lua_pushinteger(L, n ? n : 1);
    lua_pushinteger(L, m ? m : 1);
    return 2;
}

static void stick_of(int p, int right, float *x, float *y)
{
    if (right)
        input_stick_r(p, x, y);
    else
        input_stick(p, rt.praw[p], x, y);
}

/* stick([p, [n]]) -> x, y in -1..1 (x right, y down): the left stick of
 * player p, or the cross; with n = 1 the right stick (0, 0 without one);
 * without p the one pushed furthest */
static int l_stick(lua_State *L)
{
    float x = 0, y = 0;
    const int right = (int)luaL_optinteger(L, 2, 0) == 1;
    if (rt.leave_ask) {
        /* the leave question is open (online()): none of the controls */
    } else if (lua_isnoneornil(L, 1)) {
        float best = -1;
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            float px, py;
            stick_of(p, right, &px, &py);
            if (px * px + py * py > best) {
                best = px * px + py * py;
                x = px;
                y = py;
            }
        }
    } else {
        int p = ival(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            stick_of(p - 1, right, &x, &y);
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int l_time(lua_State *L)
{
    lua_pushnumber(L, (timer_ticks() - rt.start_us) / 1e6);
    return 1;
}

static uint32_t assets_kb(void);

/* stat(n): 0 Lua KiB, 1 last frame CPU ms (update+draw), 2 fps, 3 frame number,
 *         4 3D triangles drawn and 5 3D pixels written since the last zclear()
 *         (0 with the GPU), 6 ms spent in 3D drawing (draw3d and the effects;
 *         with the GPU, the ARM's part) since then, 7 3D vertices transformed
 *         since then, 8 ms since this frame began, 9 1 if the GPU draws the 3D,
 *         10 Lua instructions of the last frame (update+draw, to the thousand);
 *         the dev kit: 11 tokens of the cartridge's code (tokens.h), 12 the
 *         most Lua KiB of this run, 13 KiB of the cartridge's data in memory
 *         (sprite sheet, map, models, sound bank, z-buffer), 14 the Lua
 *         instructions of the busiest frame of this run */
static int l_stat(lua_State *L)
{
    switch (ival(L, 1)) {
    case 0: lua_pushinteger(L, (lua_Integer)(luavm_mem() / 1024)); break;
    case 1: lua_pushnumber(L, rt.last_cpu_us / 1000.0); break;
    case 2: lua_pushinteger(L, rt.fps); break;
    case 3: lua_pushinteger(L, rt.frame); break;
    case 4: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.tris_drawn : 0); break;
    case 5: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.pixels : 0); break;
    case 6: lua_pushnumber(L, rt.us3d / 1000.0); break;
    case 7: lua_pushinteger(L, rt.r3d_ready ? rt.r3d.verts : 0); break;
    case 8: lua_pushnumber(L, (timer_ticks() - rt.frame_t0) / 1000.0); break;
    case 9: lua_pushinteger(L, rt.r3d_ready && rt.r3d.backend); break;
    case 10: lua_pushinteger(L, (lua_Integer)rt.last_instr_k * 1000); break;
    case 11: lua_pushinteger(L, rt.tokens); break;
    case 12: {
        size_t now = luavm_mem();
        lua_pushinteger(L, (lua_Integer)((now > rt.lua_peak ? now : rt.lua_peak) / 1024));
        break;
    }
    case 13: lua_pushinteger(L, (lua_Integer)assets_kb()); break;
    case 14: lua_pushinteger(L, (lua_Integer)rt.instr_k_max * 1000); break;
    case 15: lua_pushinteger(L, rt.updates); break;
    default: lua_pushnil(L);
    }
    return 1;
}

/* frameskip([n]) -> the old n: the game's time at 60 _update a second
 * whatever its _draw costs. When a frame takes longer than 1/60 s, up to n
 * _update run before the next _draw (the frames not drawn are skipped), so
 * a game that moves 1/60 s per _update does not slow down; 1 (the default)
 * is one _update a frame, as before. stat(15): how many ran this frame. */
static int l_frameskip(lua_State *L)
{
    const int old = rt.skip_max > 1 ? rt.skip_max : 1;
    if (!lua_isnoneornil(L, 1)) {
        int n = ival(L, 1);
        rt.skip_max = n < 1 ? 1 : n > 8 ? 8 : n;
    }
    lua_pushinteger(L, old);
    return 1;
}

/* tri(x0, y0, x1, y1, x2, y2, c [, c1, c2]): with three colours, one per
 * corner, blended across the triangle (Gouraud, dithered) */
static int l_tri(lua_State *L)
{
    int32_t v[9], *a;
    for (int i = 0; i < 6; i++)
        v[i] = ival(L, i + 1);
    if (!lua_isnoneornil(L, 8)) {
        const uint32_t c0 = (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF);
        v[6] = (int32_t)c0;
        v[7] = (int32_t)(uint32_t)luaL_checkinteger(L, 8);
        v[8] = (int32_t)(uint32_t)luaL_optinteger(L, 9, (lua_Integer)c0);
        if (draw2d(D2_TRI_GOURAUD, 9, &a))
            memcpy(a, v, sizeof v);
        else
            g16_tri_gouraud(&rt.g, v[0], v[1], v[2], v[3], v[4], v[5], (uint32_t)v[6], (uint32_t)v[7],
                            (uint32_t)v[8]);
        return 0;
    }
    v[6] = col(L, 7, 0xFFFFFF);
    if (draw2d(D2_TRI, 7, &a))
        memcpy(a, v, 7 * sizeof *v);
    else
        g16_tri(&rt.g, v[0], v[1], v[2], v[3], v[4], v[5], (uint16_t)v[6]);
    return 0;
}

/* ---- 3D (software rasterizer, see r3d.h) */

static float fnum(lua_State *L, int i, float def)
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
static void gpu3d_maybe(void)
{
    const char *v = config_get("gpu3d");
    if (cur.gpu3d == 0 || (cur.gpu3d < 0 && v && strcmp(v, "0") == 0))
        return;
    if (rt.g.stride != (uint32_t)rt.g.w) {      /* 256x256 in the middle of the screen: whole pages only */
        kprintf("bm: the 3D is drawn by the ARM as bm3d %s: the page is a square inside the screen\n",
                bm3d_mode(0, 0));
        return;
    }
    if (gpu3d_init() == 0) {
        gpu3d_stats_t st;
        gpu3d_drop();                   /* nothing learned from the game before (depth kept) */
        gpu3d_take_stats(&st);          /* this game's from here */
        const char *aa = config_get("gpu3d_aa");
        gpu3d_set_msaa(aa && strcmp(aa, "1") == 0);     /* anti-aliasing: Settings */
        const char *vs = config_get("gpu3d_vs");
        gpu3d_set_vshader(vs ? atoi(vs) : 0);           /* vertex shader: Settings (0, 1, 2) */
        const char *q = config_get("gpu3d_queue");
        gpu3d_set_queue(q && (q[0] == '1' || q[0] == '2') ? q[0] - '0' : 0);  /* the frame in the queue (M35),
                                                                                 two jobs in flight (M39) */
        const char *wc = config_get("gpu3d_wc");
        gpu3d_set_wc(wc && strcmp(wc, "1") == 0);       /* the jobs' memory uncached (M35): Settings */
        const char *bf = config_get("gpu3d_filter");
        gpu3d_set_bilinear(bf && strcmp(bf, "1") == 0); /* textures filtered (M37): Settings */
        const char *t16 = config_get("gpu3d_tex16");
        gpu3d_set_tex16(t16 && strcmp(t16, "1") == 0);  /* opaque textures in 16 bits (M39): Settings */
        const char *g2 = config_get("gpu3d_2d");
        rt.gpu2d = g2 && strcmp(g2, "1") == 0;          /* the 2D over the 3D in the job (M37): Settings */
        rt.r3d.backend = gpu3d_backend();
        rt.r3d.arm_hook = gpu3d_to_arm;
        if (!cur.bench)                 /* a benchmark has its own report */
            kprintf("bm: the 3D is drawn by the GPU as bm3d %s (%s)\n", bm3d_mode_q(1, gpu3d_vshader_on(), gpu3d_queue()),
                    gpu3d_status());
    } else {
        kprintf("bm: the 3D is drawn by the ARM as bm3d %s: %s\n", bm3d_mode(0, 0), gpu3d_status());
    }
}

static r3d_t *r3d(lua_State *L)
{
    if (!rt.r3d_ready) {
        if (r3d_init(&rt.r3d, &rt.g) != 0)
            luaL_error(L, "not enough memory for the z-buffer");
        rt.r3d_ready = 1;
        const char *fast = config_get("r3d_fast");
        rt.r3d.fast = fast && strcmp(fast, "1") == 0;   /* M37: one matrix, light in the object's axes */
        gpu3d_maybe();
    }
    if (rt.cls_pending && !rt.r3d.backend)
        cls_settle();                   /* the GPU stopped: the ARM's 3D goes on the cleared page */
    return &rt.r3d;
}

/* The DMA clear started at the end of the frame, if any, is waited for
 * (it ran during _update). claim = the buffer is about to be drawn on:
 * a later zclear() in the same frame clears it again. */
static void zclear_dma_wait(int claim)
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
     * dropped (16 KiB, cheaper than the 460 KiB range) */
    dcache_clean_invalidate_all();
    dma_fill(rt.r3d.zbuf, 0, bytes);
    rt.zclear_dma = 1;
}

/* What animate() needs for a model with a skeleton (bm Animator): its own
 * copy of the rig, the vertices at rest, the bone matrices of the pose. */
typedef struct {
    uint8_t *data;
    bm_rig_t r;
    float (*mat)[12];           /* the pose: where each bone carries its rest vertices (r3d skins while drawing) */
    float *radius;              /* per bone: the capsule around its vertices (hit3d) */
    float turn[BM_BONES_MAX][4];    /* bone_turn(): an extra turn of each bone (quaternion) */
    uint8_t turned[BM_BONES_MAX];
} skel_t;

/* A mesh of Lua: r3d_mesh_t first, so every function can see just that. */
typedef struct {
    r3d_mesh_t m;
    skel_t *skel;
} lmesh_t;

static void skel_free(skel_t *s)
{
    if (!s)
        return;
    free(s->data);
    free(s->mat);
    free(s->radius);
    free(s);
}

static r3d_mesh_t *new_mesh(lua_State *L)
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

/* ---- skeletal animation (bm Animator): the same arithmetic as
 * sdk/studio/js/rig.js */

static void quat_norm(float q[4])
{
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-12f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int k = 0; k < 4; k++) q[k] /= l;
}

/* the shorter way from a to b */
static void quat_slerp(float out[4], const float a[4], const float b[4], float u)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = 1, k0, k1;
    if (d < 0) { d = -d; s = -1; }
    if (d > 0.9995f) {
        k0 = 1 - u;
        k1 = u;
    } else {
        float th = acosf(d), sn = sinf(th);
        k0 = sinf((1 - u) * th) / sn;
        k1 = sinf(u * th) / sn;
    }
    for (int k = 0; k < 4; k++) out[k] = a[k] * k0 + s * b[k] * k1;
    quat_norm(out);
}

static float pose_q[BM_BONES_MAX][4], pose_t[BM_BONES_MAX][3];
static float pose_q2[BM_BONES_MAX][4], pose_t2[BM_BONES_MAX][3];
static float key_q[BM_BONES_MAX][4], key_tr[BM_BONES_MAX][3];

/* the pose of clip c at time t into q, tr */
static void pose_at(int nb, const bm_clip_t *c, float t, float (*q)[4], float (*tr)[3])
{
    const float L = c->length > 0 ? c->length : 1;
    if (c->loop) {
        t = fmodf(t, L);
        if (t < 0) t += L;
    } else {
        t = t < 0 ? 0 : t > L ? L : t;
    }
    int n = c->nkeys, ka, kb;
    float first = bm_clip_key(c, nb, 0, NULL, NULL), last = bm_clip_key(c, nb, n - 1, NULL, NULL), ta, tb;
    if (n == 1 || (!c->loop && t < first)) {
        bm_clip_key(c, nb, 0, q, tr);
        return;
    }
    if (t < first || t >= last) {
        if (!c->loop) {
            bm_clip_key(c, nb, n - 1, q, tr);
            return;
        }
        ka = n - 1; kb = 0;
        ta = t < first ? last - L : last;
        tb = t < first ? first : first + L;
    } else {
        ka = 0;
        while (ka + 1 < n && bm_clip_key(c, nb, ka + 1, NULL, NULL) <= t)
            ka++;
        kb = ka + 1;
        ta = bm_clip_key(c, nb, ka, NULL, NULL);
        tb = bm_clip_key(c, nb, kb, NULL, NULL);
    }
    float u = tb > ta ? (t - ta) / (tb - ta) : 0;
    if (c->mode == 2) u = 0;                         /* step */
    else if (c->mode == 1) u = u * u * (3 - 2 * u);  /* smooth */
    bm_clip_key(c, nb, ka, q, tr);
    bm_clip_key(c, nb, kb, key_q, key_tr);
    for (int i = 0; i < nb; i++) {
        float a[4] = { q[i][0], q[i][1], q[i][2], q[i][3] };
        quat_slerp(q[i], a, key_q[i], u);
        for (int k = 0; k < 3; k++) tr[i][k] += (key_tr[i][k] - tr[i][k]) * u;
    }
}

/* M[i] = M[parent] * T(head + t) * R(q) * T(-head); every vertex follows its bone */
static void quat_mul(float out[4], const float a[4], const float b[4])
{
    float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

static void skin(lmesh_t *lm, float (*q)[4], float (*tr)[3])
{
    skel_t *s = lm->skel;
    const int nb = s->r.nbones;
    for (int i = 0; i < nb; i++) {
        int parent;
        float h[3], tail[3], r[9];
        bm_rig_bone(&s->r, i, NULL, &parent, h, tail);
        if (s->turned[i]) {                 /* bone_turn(): in the parent's frame, before the pose */
            float a[4] = { q[i][0], q[i][1], q[i][2], q[i][3] };
            quat_mul(q[i], s->turn[i], a);
        }
        quat_norm(q[i]);
        float x = q[i][0], y = q[i][1], z = q[i][2], w = q[i][3];
        r[0] = 1 - 2 * (y * y + z * z); r[1] = 2 * (x * y - z * w); r[2] = 2 * (x * z + y * w);
        r[3] = 2 * (x * y + z * w); r[4] = 1 - 2 * (x * x + z * z); r[5] = 2 * (y * z - x * w);
        r[6] = 2 * (x * z - y * w); r[7] = 2 * (y * z + x * w); r[8] = 1 - 2 * (x * x + y * y);
        float local[12];
        for (int row = 0; row < 3; row++) {
            local[row * 4] = r[row * 3];
            local[row * 4 + 1] = r[row * 3 + 1];
            local[row * 4 + 2] = r[row * 3 + 2];
            local[row * 4 + 3] = h[row] + tr[i][row] - (r[row * 3] * h[0] + r[row * 3 + 1] * h[1] + r[row * 3 + 2] * h[2]);
        }
        float *m = s->mat[i];
        if (parent < 0) {
            memcpy(m, local, sizeof local);
            continue;
        }
        const float *a = s->mat[parent];
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++)
                m[row * 4 + col] = a[row * 4] * local[col] + a[row * 4 + 1] * local[4 + col] + a[row * 4 + 2] * local[8 + col];
            m[row * 4 + 3] = a[row * 4] * local[3] + a[row * 4 + 1] * local[7] + a[row * 4 + 2] * local[11] + a[row * 4 + 3];
        }
    }
    /* the vertices stay at rest: r3d moves each one with its bone while it
     * draws (rigid skinning, only the vertices the level of detail needs) */
}

static lmesh_t *skel_mesh(lua_State *L, int idx)
{
    lmesh_t *lm = luaL_checkudata(L, idx, MESH_MT);
    if (!lm->skel)
        luaL_error(L, "this mesh has no skeleton (make one with bm Animator)");
    return lm;
}

/* the clip named (or numbered, from 1) by argument idx */
static void find_clip(lua_State *L, const skel_t *s, int idx, bm_clip_t *c)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        if (bm_rig_clip(&s->r, (int)luaL_checkinteger(L, idx) - 1, c) == 0)
            return;
        luaL_error(L, "no animation number %d", (int)lua_tointeger(L, idx));
    }
    const char *name = luaL_checkstring(L, idx);
    for (int i = 0; bm_rig_clip(&s->r, i, c) == 0; i++)
        if (strcmp(c->name, name) == 0)
            return;
    luaL_error(L, "no animation \"%s\"", name);
}

/* the bone named (or numbered, from 1) by argument idx: 0-based, or -1 */
static int find_bone(lua_State *L, const bm_rig_t *r, int idx)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        int i = (int)lua_tointeger(L, idx) - 1;
        return i >= 0 && i < r->nbones ? i : -1;
    }
    const char *name = luaL_checkstring(L, idx);
    char bn[BM_MODEL_NAME + 1];
    for (int i = 0; i < r->nbones; i++) {
        bm_rig_bone(r, i, bn, NULL, NULL, NULL);
        if (strcmp(bn, name) == 0)
            return i;
    }
    return -1;
}

/* animate(mesh, [clip, time, [clip2, time2, k, [bone]]]) -> the clip's
 * length: the mesh (from model()) takes the pose of the clip at that time,
 * in seconds (a looping clip goes round); with a second clip, a mix of the
 * two (k = 0 the first, 1 the second), only for `bone` and the bones under
 * it if given (an upper body that shoots on legs that run); with no clip,
 * the rest pose */
static int l_animate(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const int nb = lm->skel->r.nbones;
    float len = 0;
    if (lua_isnoneornil(L, 2)) {
        for (int i = 0; i < nb; i++) {
            pose_q[i][0] = pose_q[i][1] = pose_q[i][2] = 0;
            pose_q[i][3] = 1;
            pose_t[i][0] = pose_t[i][1] = pose_t[i][2] = 0;
        }
    } else {
        bm_clip_t c;
        find_clip(L, lm->skel, 2, &c);
        pose_at(nb, &c, (float)luaL_optnumber(L, 3, 0), pose_q, pose_t);
        len = c.length;
        if (!lua_isnoneornil(L, 4)) {
            bm_clip_t c2;
            find_clip(L, lm->skel, 4, &c2);
            pose_at(nb, &c2, (float)luaL_optnumber(L, 5, 0), pose_q2, pose_t2);
            float k = (float)luaL_optnumber(L, 6, 0.5);
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            uint8_t layer[BM_BONES_MAX];
            int top = lua_isnoneornil(L, 7) ? -1 : find_bone(L, &lm->skel->r, 7);
            if (!lua_isnoneornil(L, 7) && top < 0)
                return luaL_error(L, "no bone \"%s\"", lua_tostring(L, 7));
            for (int i = 0; i < nb; i++) {
                int parent;
                bm_rig_bone(&lm->skel->r, i, NULL, &parent, NULL, NULL);
                layer[i] = top < 0 || i == top || (parent >= 0 && layer[parent]);
            }
            for (int i = 0; i < nb; i++) {
                if (!layer[i])
                    continue;
                float a[4] = { pose_q[i][0], pose_q[i][1], pose_q[i][2], pose_q[i][3] };
                quat_slerp(pose_q[i], a, pose_q2[i], k);
                for (int j = 0; j < 3; j++) pose_t[i][j] += (pose_t2[i][j] - pose_t[i][j]) * k;
            }
        }
    }
    skin(lm, pose_q, pose_t);
    lua_pushnumber(L, len);
    return 1;
}

/* clips(mesh) -> { {name =, length =, loop =}, ... }: its animations */
static int l_clips(lua_State *L)
{
    lmesh_t *lm = luaL_checkudata(L, 1, MESH_MT);
    lua_newtable(L);
    bm_clip_t c;
    for (int i = 0; lm->skel && bm_rig_clip(&lm->skel->r, i, &c) == 0; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, c.name);
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, c.length);
        lua_setfield(L, -2, "length");
        lua_pushboolean(L, c.loop);
        lua_setfield(L, -2, "loop");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* bone3d(mesh, name or number) -> x, y, z, tx, ty, tz: where the head and
 * the tail of the bone are in the mesh's last pose (its own coordinates, as
 * bounds3d), or nil */
static int l_bone3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const bm_rig_t *r = &lm->skel->r;
    int found = find_bone(L, r, 2);
    if (found < 0) {
        lua_pushnil(L);
        return 1;
    }
    float h[3], t[3];
    bm_rig_bone(r, found, NULL, NULL, h, t);
    const float *m = lm->skel->mat[found];
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * h[0] + m[k * 4 + 1] * h[1] + m[k * 4 + 2] * h[2] + m[k * 4 + 3]);
    for (int k = 0; k < 3; k++)
        lua_pushnumber(L, m[k * 4] * t[0] + m[k * 4 + 1] * t[1] + m[k * 4 + 2] * t[2] + m[k * 4 + 3]);
    return 6;
}

/* bone_turn(mesh, bone, rx, ry, rz): every later animate() turns the bone by
 * these angles (radians, x then y then z, in its parent's frame) on top of
 * the animation: aiming up and down, a head that looks. bone_turn(mesh,
 * bone) takes it away. */
static int l_bone_turn(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    skel_t *sk = lm->skel;
    int b = find_bone(L, &sk->r, 2);
    if (b < 0)
        return luaL_error(L, "no bone \"%s\"", lua_tostring(L, 2));
    if (lua_isnoneornil(L, 3)) {
        sk->turned[b] = 0;
        return 0;
    }
    float h[3] = { (float)luaL_checknumber(L, 3) * 0.5f, (float)luaL_optnumber(L, 4, 0) * 0.5f,
                   (float)luaL_optnumber(L, 5, 0) * 0.5f };
    float qx[4] = { sinf(h[0]), 0, 0, cosf(h[0]) }, qy[4] = { 0, sinf(h[1]), 0, cosf(h[1]) },
          qz[4] = { 0, 0, sinf(h[2]), cosf(h[2]) }, t[4];
    quat_mul(t, qy, qx);
    quat_mul(sk->turn[b], qz, t);
    sk->turned[b] = 1;
    return 0;
}

/* bones3d(mesh) -> { "name", ... }: the bones of its skeleton, in order */
static int l_bones3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const bm_rig_t *r = &lm->skel->r;
    lua_createtable(L, r->nbones, 0);
    char bn[BM_MODEL_NAME + 1];
    for (int i = 0; i < r->nbones; i++) {
        bm_rig_bone(r, i, bn, NULL, NULL, NULL);
        lua_pushstring(L, bn);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* hit3d(mesh, x, y, z, ry, scale, ox, oy, oz, dx, dy, dz, [maxd]) -> t, bone:
 * the ray from o along d (any length; t in units of d) against the bones of
 * a mesh with a skeleton in its last pose, drawn at (x, y, z) turned by ry
 * around y and scaled: each bone is a capsule from its head to its tail,
 * wide enough for its vertices. The nearest hit within maxd (default 1000),
 * or nil. For hitboxes that follow the animation (a head shot: bone "head"). */
static int l_hit3d(lua_State *L)
{
    lmesh_t *lm = skel_mesh(L, 1);
    const skel_t *sk = lm->skel;
    const float px = fnum(L, 2, 0), py = fnum(L, 3, 0), pz = fnum(L, 4, 0), ry = fnum(L, 5, 0),
                sc = fnum(L, 6, 1);
    float o[3] = { fnum(L, 7, 0), fnum(L, 8, 0), fnum(L, 9, 0) }, d[3] = { fnum(L, 10, 0), fnum(L, 11, 0), fnum(L, 12, 1) };
    const float maxd = fnum(L, 13, 1000);
    /* the ray into the model's own frame: undo the move, the turn and the scale */
    const float c = cosf(ry), s = sinf(ry), is = sc != 0 ? 1.0f / sc : 1;
    float ox = o[0] - px, oy = o[1] - py, oz = o[2] - pz;
    /* draw3d turns with Ry: x' = c x + s z, z' = -s x + c z; back: */
    float lo[3] = { (c * ox - s * oz) * is, oy * is, (s * ox + c * oz) * is };
    float ld[3] = { (c * d[0] - s * d[2]) * is, d[1] * is, (s * d[0] + c * d[2]) * is };
    float best = maxd;
    int hit = -1;
    for (int b = 0; b < sk->r.nbones; b++) {
        float rad = sk->radius[b];
        if (rad <= 0)
            continue;                       /* a bone with no vertices */
        float h[3], t[3], a[3], e[3];
        bm_rig_bone(&sk->r, b, NULL, NULL, h, t);
        const float *m = sk->mat[b];
        for (int k = 0; k < 3; k++) {
            a[k] = m[k * 4] * h[0] + m[k * 4 + 1] * h[1] + m[k * 4 + 2] * h[2] + m[k * 4 + 3];
            e[k] = m[k * 4] * t[0] + m[k * 4 + 1] * t[1] + m[k * 4 + 2] * t[2] + m[k * 4 + 3];
        }
        /* ray against capsule: sample the closest approach of the two lines */
        float u[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] }, w[3] = { lo[0] - a[0], lo[1] - a[1], lo[2] - a[2] };
        float A = ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2], B = ld[0] * u[0] + ld[1] * u[1] + ld[2] * u[2],
              C = u[0] * u[0] + u[1] * u[1] + u[2] * u[2], D = ld[0] * w[0] + ld[1] * w[1] + ld[2] * w[2],
              E = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
        float den = A * C - B * B, tr, tc;
        if (den > 1e-9f) {
            tr = (B * E - C * D) / den;
            tc = (A * E - B * D) / den;
        } else {
            tr = -D / (A > 1e-9f ? A : 1);
            tc = 0;
        }
        tc = tc < 0 ? 0 : tc > 1 ? 1 : tc;
        /* the point of the segment nearest the ray, then the sphere there */
        float q[3] = { a[0] + u[0] * tc - lo[0], a[1] + u[1] * tc - lo[1], a[2] + u[2] * tc - lo[2] };
        float qd = q[0] * ld[0] + q[1] * ld[1] + q[2] * ld[2];
        float qq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        float disc = qd * qd - A * (qq - rad * rad);
        (void)tr;
        if (disc < 0 || A <= 1e-12f)
            continue;
        float th = (qd - sqrtf(disc)) / A;
        if (th < 0)
            th = (qd + sqrtf(disc)) / A >= 0 ? 0 : -1;
        if (th >= 0 && th < best) {
            best = th;
            hit = b;
        }
    }
    if (hit < 0) {
        lua_pushnil(L);
        return 1;
    }
    char bn[BM_MODEL_NAME + 1];
    bm_rig_bone(&sk->r, hit, bn, NULL, NULL, NULL);
    lua_pushnumber(L, best);
    lua_pushstring(L, bn);
    return 2;
}

/* bounds3d(mesh) -> x0, y0, z0, x1, y1, z1: the box around its vertices, in
 * its own coordinates (before draw3d moves, turns and scales it) */
static v3_t posed(const r3d_mesh_t *m, int i)
{
    v3_t p = m->verts[i];
    if (!m->bones)
        return p;
    const float *b = m->bones[m->vbone[i]];
    return (v3_t){ b[0] * p.x + b[1] * p.y + b[2] * p.z + b[3], b[4] * p.x + b[5] * p.y + b[6] * p.z + b[7],
                   b[8] * p.x + b[9] * p.y + b[10] * p.z + b[11] };
}

static int l_bounds3d(lua_State *L)
{
    const r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t lo = posed(m, 0), hi = lo;
    for (int i = 1; i < m->nverts; i++) {
        v3_t p = posed(m, i);
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z > hi.z) hi.z = p.z;
    }
    const float v[6] = { lo.x, lo.y, lo.z, hi.x, hi.y, hi.z };
    for (int i = 0; i < 6; i++)
        lua_pushnumber(L, v[i]);
    return 6;
}

static int l_mesh_sphere(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_sphere(m, oval(L, 1, 8), oval(L, 2, 16),
                        (uint32_t)luaL_optinteger(L, 3, 0xFFFFFF), (uint32_t)luaL_optinteger(L, 4, 0xC0C0C0)) != 0)
        return luaL_error(L, "cannot build the sphere");
    return 1;
}

static int l_mesh_cube(lua_State *L)
{
    r3d_mesh_t *m = new_mesh(L);
    if (r3d_mesh_cube(m, (uint32_t)luaL_optinteger(L, 1, 0xFFFFFF)) != 0)
        return luaL_error(L, "cannot build the cube");
    return 1;
}


/* draw3d(mesh, x, y, z [, rx, ry, rz, scale, flags]) - flags: 1 no z-buffer
 * (floors and backdrops drawn first), 2 unlit (full colour), 4 smooth
 * (Gouraud) */
static int l_draw3d(lua_State *L)
{
    r3d_mesh_t *m = luaL_checkudata(L, 1, MESH_MT);
    v3_t p = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) };
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    r3d_draw_flags(r, m, p, fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0), fnum(L, 8, 1),
                   (unsigned)luaL_optinteger(L, 9, 0));
    rt.us3d += timer_ticks() - t0;
    return 0;
}

/* sky3d(sun, sky, ground): colours (0xRRGGBB) of the sunlight and of the
 * ambient light from above and from below; sky3d() back to white */
static int l_sky3d(lua_State *L)
{
    r3d_sky(r3d(L), (uint32_t)luaL_optinteger(L, 1, 0xFFFFFF), (uint32_t)luaL_optinteger(L, 2, 0xFFFFFF),
            (uint32_t)luaL_optinteger(L, 3, 0xFFFFFF));
    return 0;
}

/* shine3d(spec, exponent, rim): highlights on glossy faces and rim light */
static int l_shine3d(lua_State *L)
{
    r3d_shine(r3d(L), fnum(L, 1, 0.6f), (int)luaL_optinteger(L, 2, 16), fnum(L, 3, 0));
    return 0;
}

/* shadow3d(style): 0 darkens (default), 1 a dithered black (no reads of the
 * screen) */
static int l_shadow3d(lua_State *L)
{
    r3d(L)->shadow_style = (int)luaL_optinteger(L, 1, 0);
    return 0;
}

/* point3d(x, y, z, radius, colour, [flags]) -> pixels: a round point of
 * world radius, behind what is nearer; flags 1 = every other pixel */
static int l_point3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_point(r, (v3_t){ fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) }, fnum(L, 4, 0.1f),
                           (uint32_t)luaL_optinteger(L, 5, 0xFFFFFF), (unsigned)luaL_optinteger(L, 6, 0));
    rt.us3d += timer_ticks() - t0;
    lua_pushinteger(L, n);
    return 1;
}

/* line3d(x0, y0, z0, x1, y1, z1, colour, [width, flags]) -> pixels */
static int l_line3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_line(r, (v3_t){ fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) },
                          (v3_t){ fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 0) },
                          (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF), (int)luaL_optinteger(L, 8, 1),
                          (unsigned)luaL_optinteger(L, 9, 0));
    rt.us3d += timer_ticks() - t0;
    lua_pushinteger(L, n);
    return 1;
}

/* sprite3d(sx, sy, sw, sh, x, y, z, size, [flags]) -> pixels: a rectangle of
 * the sprite sheet facing the camera, `size` world units wide */
static int l_sprite3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    sheet_commit();
    settle2d();                         /* recorded 2D first: the 3D goes over it */
    zclear_dma_wait(1);
    uint32_t t0 = timer_ticks();
    uint32_t n = r3d_sprite(r, &rt.sheet, ival(L, 1), ival(L, 2), ival(L, 3), ival(L, 4),
                            (v3_t){ fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0) }, fnum(L, 8, 1),
                            (unsigned)luaL_optinteger(L, 9, 0));
    rt.us3d += timer_ticks() - t0;
    lua_pushinteger(L, n);
    return 1;
}

/* camera3d(x, y, z [, yaw, pitch, fov, roll]) */
static int l_camera3d(lua_State *L)
{
    r3d_camera(r3d(L), fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, -5), fnum(L, 4, 0), fnum(L, 5, 0), fnum(L, 6, 60));
    r3d_camera_roll(r3d(L), fnum(L, 7, 0));
    return 0;
}

/* fog3d(colour, near, far): faces fade into the colour with the distance;
 * fog3d() turns it off */
static int l_fog3d(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        r3d_fog(r3d(L), 0, 0, 0);
    else
        r3d_fog(r3d(L), (uint32_t)luaL_checkinteger(L, 1), fnum(L, 2, 10), fnum(L, 3, 100));
    return 0;
}

/* project3d(x, y, z) -> screen x, y and depth, or nil behind the camera */
static int l_project3d(lua_State *L)
{
    float sx, sy, d;
    v3_t p = { fnum(L, 1, 0), fnum(L, 2, 0), fnum(L, 3, 0) };
    if (!r3d_project(r3d(L), p, &sx, &sy, &d)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, sx + rt.g.cam_x);
    lua_pushnumber(L, sy + rt.g.cam_y);
    lua_pushnumber(L, d);
    return 3;
}

/* light3d(x, y, z [, ambient]) - direction towards the light */
static int l_light3d(lua_State *L)
{
    r3d_light(r3d(L), fnum(L, 1, 0), fnum(L, 2, 1), fnum(L, 3, 0), fnum(L, 4, 0.25f));
    return 0;
}

/* lamp3d(i, x, y, z, radius [, k]) - point light i (1-4) that brightens the
 * faces near it by up to k (default 1); lamp3d(i) or lamp3d() turns it (them) off */
static int l_lamp3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    if (lua_isnoneornil(L, 1)) {
        for (int i = 0; i < R3D_LAMPS; i++)
            r3d_lamp(r, i, 0, 0, 0, 0, 0);
        return 0;
    }
    int i = ival(L, 1) - 1;
    luaL_argcheck(L, i >= 0 && i < R3D_LAMPS, 1, "lamp 1 to 4");
    if (lua_isnoneornil(L, 2))
        r3d_lamp(r, i, 0, 0, 0, 0, 0);
    else
        r3d_lamp_rgb(r, i, fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0), fnum(L, 5, 3), fnum(L, 6, 1),
                     (uint32_t)luaL_optinteger(L, 7, 0xFFFFFF));
    return 0;
}

static int l_zclear(lua_State *L)
{
    r3d_t *r = r3d(L);
    rt.zclear_seen = 1;
    rt.us3d = 0;
    if (rt.zclear_dma) {
        zclear_dma_wait(1);
        r->tris_in = r->tris_drawn = r->pixels = r->verts = 0;
        return 0;
    }
    r3d_zclear(r);
    return 0;
}

/* gpu3d([on, [aa, [vs, [queue]]]]) -> on, aa, vs, version, queue: whether the GPU draws the 3D,
 * whether with anti-aliasing (MSAA 4x, where the GPU allows it) and whether
 * its vertex shader places the corners of the models (M36, where the GPU's
 * probe drew with it): false, 1 the scenery (meshes unlit or with baked
 * light), 2 (or true) every model; version: the bm3d version that reproduces
 * (src/gpu/version3d.h: "0.2" the ARM, "2.1", "3.0", "3.4", "4.1"). With on, the 3D
 * goes to the GPU (if the console has one that answers) or to the ARM from
 * here, whatever Settings > Graphics says: for benchmarks; switch between
 * frames (what was drawn so far in a frame is not in the other's depth).
 * queue (M35): the end of a frame's 3D started on the GPU while the next
 * _update runs (where the GPU's probe saw it work). */
static int l_gpu3d(lua_State *L)
{
    r3d_t *r = r3d(L);
    if (!lua_isnoneornil(L, 1)) {
        const int on = lua_toboolean(L, 1);
        if (on && !r->backend && rt.g.stride == (uint32_t)rt.g.w && gpu3d_init() == 0 && !gpu3d_failed()) {
            gpu3d_drop();
            gpu3d_page(0, 0);
            r->backend = gpu3d_backend();
            r->arm_hook = gpu3d_to_arm;
        } else if (!on && r->backend) {
            flush3d(0);
            cls_settle();
            gpu3d_drop();
            r->backend = NULL;
            zclear_dma_wait(1);
            memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
        }
        if (!lua_isnoneornil(L, 2))
            gpu3d_set_msaa(lua_toboolean(L, 2));
        if (!lua_isnoneornil(L, 3))
            gpu3d_set_vshader(lua_isboolean(L, 3) ? 2 * lua_toboolean(L, 3) : (int)luaL_checkinteger(L, 3));
        if (!lua_isnoneornil(L, 4))
            gpu3d_set_queue(lua_isinteger(L, 4) ? (int)lua_tointeger(L, 4) : lua_toboolean(L, 4));
    }
    lua_pushboolean(L, r->backend != NULL);
    lua_pushboolean(L, r->backend != NULL && gpu3d_msaa_on());
    const int vs = r->backend != NULL ? gpu3d_vshader_on() : 0;
    if (vs)
        lua_pushinteger(L, vs);
    else
        lua_pushboolean(L, 0);
    lua_pushstring(L, bm3d_mode_q(r->backend != NULL, vs, r->backend != NULL && gpu3d_queue()));  /* reproduced */
    lua_pushboolean(L, r->backend != NULL && gpu3d_queue());
    return 5;
}

/* The resolutions screen() can change to: 16:9, whole pixels on a 1080p
 * TV but for 1280x720 (1.5x). The cartridge's own (its header) is one. */
static const uint16_t screen_modes[][2] = {
    { 320, 180 }, { 384, 216 }, { 480, 270 }, { 640, 360 }, { 960, 540 }, { 1280, 720 }, { 1920, 1080 },
};
#define SCREEN_MODES (int)(sizeof screen_modes / sizeof screen_modes[0])

/* screen(w, h) -> true: the cartridge's resolution changes to w x h from the
 * next frame (SCREEN_W and SCREEN_H then say it; if the console cannot
 * set it they stay); false: not one of the modes. screen() -> w, h now.
 * screen(i) -> the i-th mode w, h (1 = 320x180 ... 7 = 1920x1080), or nil.
 * A square 256x256 cartridge keeps its screen. */
static int l_screen(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        lua_pushinteger(L, rt.g.w);
        lua_pushinteger(L, rt.g.h);
        return 2;
    }
    const int w = ival(L, 1);
    if (lua_isnoneornil(L, 2)) {
        if (w < 1 || w > SCREEN_MODES)
            return 0;
        lua_pushinteger(L, screen_modes[w - 1][0]);
        lua_pushinteger(L, screen_modes[w - 1][1]);
        return 2;
    }
    const int h = ival(L, 2);
    int ok = !(rt.g.w == 256 && rt.g.h == 256);
    int known = 0;
    for (int i = 0; i < SCREEN_MODES; i++)
        known |= screen_modes[i][0] == w && screen_modes[i][1] == h;
    ok = ok && known;
    if (ok) {
        rt.want_w = w;
        rt.want_h = h;
    }
    lua_pushboolean(L, ok);
    return 1;
}

/* ---- collision worlds (world3d.h): boxes, rays, moving bodies */

#define WORLD_MT "bm.world"

static w3_world_t *check_world(lua_State *L, int i)
{
    return luaL_checkudata(L, i, WORLD_MT);
}

static int l_world_gc(lua_State *L)
{
    w3_free(check_world(L, 1));
    return 0;
}

/* world3d() -> an empty collision world */
static int l_world3d(lua_State *L)
{
    w3_world_t *w = lua_newuserdatauv(L, sizeof *w, 0);
    w3_init(w);
    luaL_setmetatable(L, WORLD_MT);
    return 1;
}

/* world_box(w, x0, y0, z0, x1, y1, z1, [tag]) -> its number: a solid box */
static int l_world_box(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float lo[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, hi[3] = { fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 0) };
    int i = w3_add_box(w, lo, hi, (int)luaL_optinteger(L, 8, 0));
    if (i < 0)
        return luaL_error(L, "not enough memory for the world");
    lua_pushinteger(L, i + 1);
    return 1;
}

/* world_ray(w, ox, oy, oz, dx, dy, dz, [maxd, ground]) -> t, nx, ny, nz, box
 * (0 = the ground y = 0, which counts unless ground is false), or nil */
static int l_world_ray(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float o[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, d[3] = { fnum(L, 5, 0), fnum(L, 6, 0), fnum(L, 7, 1) };
    float t, n[3];
    int ground = lua_isnoneornil(L, 9) || lua_toboolean(L, 9);
    int i = w3_ray(w, o, d, fnum(L, 8, 1000), ground, &t, n);
    if (i == -1) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, t);
    lua_pushnumber(L, n[0]);
    lua_pushnumber(L, n[1]);
    lua_pushnumber(L, n[2]);
    lua_pushinteger(L, i == -2 ? 0 : i + 1);
    return 5;
}

/* world_move(w, x, y, z, r, h, dx, dy, dz, [step, on_ground]) -> x, y, z, flags:
 * 1 on the ground, 2 a wall (4 along x, 8 along z), 16 a ceiling */
static int l_world_move(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    float p[3] = { fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0) }, d[3] = { fnum(L, 7, 0), fnum(L, 8, 0), fnum(L, 9, 0) };
    int f = w3_move(w, p, fnum(L, 5, 0.4f), fnum(L, 6, 1.8f), d, fnum(L, 10, 0.45f), lua_toboolean(L, 11));
    lua_pushnumber(L, p[0]);
    lua_pushnumber(L, p[1]);
    lua_pushnumber(L, p[2]);
    lua_pushinteger(L, f);
    return 4;
}

/* world_floor(w, x, z, y, r, [step]) -> the height of the floor under (x, z) */
static int l_world_floor(lua_State *L)
{
    w3_world_t *w = check_world(L, 1);
    lua_pushnumber(L, w3_floor(w, fnum(L, 2, 0), fnum(L, 3, 0), fnum(L, 4, 0), fnum(L, 5, 0.4f), fnum(L, 6, 0.45f)));
    return 1;
}

/* log(...) - text to the kernel log (serial + console), not the screen */
static int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        kprintf("%s%s", i > 1 ? "\t" : "", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    kprintf("\n");
    return 0;
}

/* report(kind, text) - a report for whoever develops bm (src/kernel/reports.c,
 * 2026-10-04): saved in bm/reports on the SD card with the kernel, branch,
 * board and date, then sent to the reports' git repository if it can (a
 * benchmark's numbers instead of a photo). At most 8 a run, 256 KiB each.
 * true if it was saved. */
#define REPORTS_MAX     8
#define REPORT_MAX_LEN  (256 * 1024)
static int l_report(lua_State *L)
{
    const char *kind = luaL_checkstring(L, 1);
    size_t len;
    const char *text = luaL_checklstring(L, 2, &len);
    if (rt.reports >= REPORTS_MAX || len > REPORT_MAX_LEN) {
        kprintf("report: refused (%s)\n", len > REPORT_MAX_LEN ? "over 256 KiB" : "8 in this run already");
        lua_pushboolean(L, 0);
        return 1;
    }
    rt.reports++;
    lua_pushboolean(L, reports_text(kind, text, len) == 0);
    return 1;
}

/* ---- the network for the games (M38.5): UDP sockets (src/net/cartnet.h).
 * Addresses as text, "192.168.1.23"; "*" is the broadcast of the LAN. */

static void push_ip(lua_State *L, uint32_t ip)
{
    char t[16];
    ksnprintf(t, sizeof t, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255),
              (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
    lua_pushstring(L, t);
}

/* udp_open([port]): a socket, or nil and why */
static int l_udp_open(lua_State *L)
{
    int port = (int)luaL_optinteger(L, 1, 0);
    luaL_argcheck(L, port >= 0 && port <= 65535, 1, "a port is 0..65535");
    if (!cartnet_ip()) {
        lua_pushnil(L);
        lua_pushstring(L, "no network");
        return 2;
    }
    int sk = cartnet_open((uint16_t)port);
    if (sk < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "no free socket");
        return 2;
    }
    lua_pushinteger(L, sk);
    lua_pushinteger(L, cartnet_port(sk));
    return 2;
}

/* udp_send(s, address, port, data): true when sent */
static int l_udp_send(lua_State *L)
{
    int sk = (int)luaL_checkinteger(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);
    size_t len;
    const char *d = luaL_checklstring(L, 4, &len);
    luaL_argcheck(L, len <= CARTNET_MAX, 4, "at most 1024 bytes");
    uint32_t ip = strcmp(to, "*") == 0 ? CARTNET_BROADCAST : cartnet_resolve(to);
    lua_pushboolean(L, ip && (ip != CARTNET_BROADCAST || strcmp(to, "*") == 0) &&
                           cartnet_send(sk, ip, (uint16_t)port, d, (int)len) == 0);
    return 1;
}

/* udp_recv(s): data, address, port of the next packet, or nil */
static int l_udp_recv(lua_State *L)
{
    static char buf[CARTNET_MAX];
    int sk = (int)luaL_checkinteger(L, 1);
    uint32_t ip;
    uint16_t port;
    int n = cartnet_recv(sk, buf, sizeof buf, &ip, &port);
    if (n < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, buf, (size_t)n);
    push_ip(L, ip);
    lua_pushinteger(L, port);
    return 3;
}

static int l_udp_close(lua_State *L)
{
    cartnet_close((int)luaL_checkinteger(L, 1));
    return 0;
}

/* net_ip(): the console's address, or nil without network */
static int l_net_ip(lua_State *L)
{
    uint32_t ip = cartnet_ip();
    if (ip)
        push_ip(L, ip);
    else
        lua_pushnil(L);
    return 1;
}

/* net_resolve(name): its address once known; nil while looking (ask
 * again), false if there is no such name */
static int l_net_resolve(lua_State *L)
{
    uint32_t ip = cartnet_resolve(luaL_checkstring(L, 1));
    if (ip == CARTNET_BROADCAST)
        lua_pushboolean(L, 0);
    else if (!ip)
        lua_pushnil(L);
    else
        push_ip(L, ip);
    return 1;
}

/* ---- save() / saved(): one table per cartridge in /bm/save, as Lua
 * source ("return {...}") read back in an empty environment: data only. */

#define SAVE_DIR   "/bm/save"
#define SAVE_MAX   (32 * 1024)

void bm_save_path(const char *title, const char *author, char *out, size_t n)
{
    char id[96];
    int len = ksnprintf(id, sizeof id, "%s\n%s", title, author);
    ksnprintf(out, n, "%s/%08lX.SAV", SAVE_DIR, crc32(id, (uint32_t)len));
}

/* The text is built in a fixed buffer: a luaL_Buffer cannot be used here,
 * it may sit on the Lua stack while the serializer walks the tables with
 * lua_next (a save of more than about 1 KiB then broke the stack). */
static struct {
    char p[SAVE_MAX + 64];
    size_t n;
} sb;

static void sb_add(lua_State *L, const char *s, size_t len)
{
    if (sb.n + len > SAVE_MAX)
        luaL_error(L, "save: more than %d bytes", SAVE_MAX);
    memcpy(sb.p + sb.n, s, len);
    sb.n += len;
}

static void sb_str(lua_State *L, const char *s) { sb_add(L, s, strlen(s)); }
static void sb_chr(lua_State *L, char c) { sb_add(L, &c, 1); }

static void ser(lua_State *L, int idx, int depth);

static void ser_string(lua_State *L, const char *str, size_t len)
{
    sb_chr(L, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"' || c == '\\') {
            sb_chr(L, '\\');
            sb_chr(L, (char)c);
        } else if (c < 32 || c == 127) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\%03u", c);
            sb_str(L, esc);
        } else {
            sb_chr(L, (char)c);
        }
    }
    sb_chr(L, '"');
}

static void ser_value(lua_State *L, int idx, int depth)
{
    char num[40];
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        sb_str(L, lua_toboolean(L, idx) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
            snprintf(num, sizeof num, "%lld", (long long)lua_tointeger(L, idx));
        } else {
            double d = lua_tonumber(L, idx);
            if (d != d || d - d != 0)
                luaL_error(L, "save: cannot store nan or inf");
            snprintf(num, sizeof num, "%.17g", d);
            if (!strpbrk(num, ".eEn"))
                strcat(num, ".0");              /* stays a float when read back */
        }
        sb_str(L, num);
        break;
    case LUA_TSTRING: {
        size_t len;
        const char *str = lua_tolstring(L, idx, &len);
        ser_string(L, str, len);
        break;
    }
    case LUA_TTABLE:
        ser(L, idx, depth + 1);
        break;
    default:
        luaL_error(L, "save: cannot store a %s", luaL_typename(L, idx));
    }
}

static void ser(lua_State *L, int idx, int depth)
{
    if (depth > 16)
        luaL_error(L, "save: tables nested too deep (or a cycle)");
    idx = lua_absindex(L, idx);
    luaL_checkstack(L, 4, "save");
    sb_chr(L, '{');
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        sb_chr(L, '[');
        ser_value(L, -2, depth);
        sb_str(L, "]=");
        ser_value(L, -1, depth);
        sb_chr(L, ',');
        lua_pop(L, 1);
    }
    sb_chr(L, '}');
}

/* save(t): true, or false and a message (no SD card, card full...) */
static int l_save(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    sb.n = 0;
    sb_str(L, "return ");
    ser(L, 1, 0);
    if (fat_mkdirs(SAVE_DIR) != 0 || fat_write_file(SAVE_DIR, rt.save_name, sb.p, sb.n) != 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, fat_error());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* saved(): the saved table, or nil. (Not "load": that is Lua's code loader,
 * which the sandbox removes.) */
static int l_saved(lua_State *L)
{
    char path[40];
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    ksnprintf(path, sizeof path, "save/%s", rt.save_name);
    if (config_find_file(path, &e) != 0 || e.size > SAVE_MAX || fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        return 1;
    }
    int ok = luaL_loadbufferx(L, (const char *)data, len, "=save", "t") == LUA_OK;
    free(data);
    if (!ok) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);                            /* empty environment: data only */
    lua_setupvalue(L, -2, 1);
    if (lua_pcall(L, 0, 1, 0) != LUA_OK || !lua_istable(L, -1)) {
        lua_pushnil(L);
        return 1;
    }
    return 1;
}

/* ---------------------------------------------------------------- sound */

static unsigned voice_arg(lua_State *L)
{
    lua_Integer ch = luaL_checkinteger(L, 1);
    luaL_argcheck(L, ch >= 0 && ch < 8, 1, "voice 0..7");
    return (unsigned)ch;
}

/* "C4", "c#4", "Db3", "A-1", "F#-1" -> MIDI note (C4 = 60). 1 if valid. */
static int note_name(const char *s, float *midi)
{
    static const int8_t pc[7] = { 9, 11, 0, 2, 4, 5, 7 };      /* A B C D E F G */
    char c = (char)(*s >= 'a' ? *s - 32 : *s);
    if (c < 'A' || c > 'G')
        return 0;
    int n = pc[c - 'A'];
    s++;
    if (*s == '#' || *s == 's') { n++; s++; }
    else if (*s == 'b') { n--; s++; }
    if (*s == '-' && s[1] >= '0' && s[1] <= '9' && s[2])
        s++;                                    /* tracker style: "C-4" */
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (*s < '0' || *s > '9')
        return 0;
    int oct = 0;
    while (*s >= '0' && *s <= '9')
        oct = oct * 10 + (*s++ - '0');
    if (*s || oct > 9)
        return 0;
    *midi = (float)(12 * ((neg ? -oct : oct) + 1) + n);
    return 1;
}

/* a pitch: Hz, or a note name ("A4", "C#5", "Bb3") */
static float hz_arg(lua_State *L, int i)
{
    if (lua_type(L, i) == LUA_TSTRING) {
        float m = 0;
        if (!note_name(lua_tostring(L, i), &m))
            luaL_argerror(L, i, "a note name like \"C4\", \"F#3\" or \"Bb2\"");
        return au_note_hz(m);
    }
    lua_Number f = luaL_checknumber(L, i);
    return f <= 0 ? 0.0f : f >= 65535 ? 65535.0f : (float)f;
}

/* hz(note): a MIDI note number (60 = C4, 69 = A4) or a name -> Hz */
static int l_hz(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING) {
        lua_pushnumber(L, hz_arg(L, 1));
        return 1;
    }
    lua_pushnumber(L, au_note_hz((float)luaL_checknumber(L, 1)));
    return 1;
}

static int wave_arg(lua_State *L, int i)
{
    lua_Integer w = luaL_optinteger(L, i, -1);
    return w >= 0 && w < SYNTH_WAVES ? (int)w : -1;
}

/* note(ch, freq, [ms], [wave], [vol]): restarts the envelope; ms > 0
 * releases the note by itself, else it holds until noteoff(ch). freq in
 * Hz or a note name. */
static int l_note(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 0);
    audio_note(ch, hz, ms > 0 ? (uint32_t)ms : 0, wave_arg(L, 4), (int)luaL_optinteger(L, 5, -1));
    return 0;
}

static int l_noteoff(lua_State *L)
{
    audio_note_off(voice_arg(L));
    return 0;
}

/* freq(ch, hz): changes the pitch without restarting (slides, vibrato) */
static int l_freq(lua_State *L)
{
    audio_freq(voice_arg(L), hz_arg(L, 2));
    return 0;
}

static int l_envelope(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_envelope(ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                   (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

static int l_duty(lua_State *L)
{
    audio_duty(voice_arg(L), (int)luaL_checkinteger(L, 2));
    return 0;
}

/* playing(ch): true while the voice sounds (release included) or a sound
 * effect or the music holds it */
static int l_playing(lua_State *L)
{
    lua_pushboolean(L, audio_busy(voice_arg(L)));
    return 1;
}

/* apu(ch, reg, [value]): raw register byte, the synthesizer's layout (synth.h) */
static int l_apu(lua_State *L)
{
    unsigned ch = voice_arg(L);
    lua_Integer reg = luaL_checkinteger(L, 2);
    luaL_argcheck(L, reg >= 0 && reg < 16, 2, "register 0..15");
    volatile uint8_t *r = audio_regs() + ch * 16 + reg;
    if (lua_gettop(L) >= 3) {
        *r = (uint8_t)luaL_checkinteger(L, 3);
        return 0;
    }
    lua_pushinteger(L, *r);
    return 1;
}

/* slide(ch, freq, ms): the note of the voice glides to freq */
static int l_slide(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 100);
    audio_slide(ch, hz, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* vibrato(ch, [semitones], [rate_hz]): vibrato(ch) turns it off */
static int l_vibrato(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_vibrato(ch, (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 6));
    return 0;
}

static const char *const chord_names[AU_CHORDS + 1] = {
    "octave", "major", "minor", "sus2", "sus4", "maj7", "min7", "7", "dim", "aug",
    "power", "power8", "major8", "minor8", "down", "octaves", NULL
};

/* arp(ch, chord, [ms]): the note runs through a chord, ms per note
 * (default 50): a name ("major", "minor", "maj7", "min7", "7", "sus2",
 * "sus4", "dim", "aug", "power", "octave"...), or a table of semitones
 * ({0, 4, 7, 12}); arp(ch) turns it off */
static int l_arp(lua_State *L)
{
    unsigned ch = voice_arg(L);
    int8_t semis[8];
    int n = 0;
    if (lua_istable(L, 2)) {
        n = (int)luaL_len(L, 2);
        luaL_argcheck(L, n >= 1 && n <= 8, 2, "1 to 8 semitones");
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, 2, i + 1);
            lua_Integer v = luaL_checkinteger(L, -1);
            semis[i] = (int8_t)(v < -48 ? -48 : v > 48 ? 48 : v);
            lua_pop(L, 1);
        }
    } else if (!lua_isnoneornil(L, 2)) {
        int c = lua_type(L, 2) == LUA_TNUMBER ? (int)lua_tointeger(L, 2) : luaL_checkoption(L, 2, NULL, chord_names);
        luaL_argcheck(L, c >= 0 && c < AU_CHORDS, 2, "chord 0..15");
        n = au_chord_len[c];
        memcpy(semis, au_chord[c], (size_t)n);
    }
    lua_Integer ms = luaL_optinteger(L, 3, 50);
    audio_arp(ch, semis, n, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* sfx(n, [voice], [transpose], [vol]): sound effect n of the cartridge's
 * bank on a voice (nil: a free one), transposed by semitones, at volume
 * 0..1; returns the voice, or nil. sfx(-1, [voice]) stops the effect of a
 * voice, or all of them. */
static int l_sfx(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int ch = -1;
    if (!lua_isnoneornil(L, 2)) {
        lua_Integer v = luaL_checkinteger(L, 2);
        luaL_argcheck(L, v >= 0 && v < 8, 2, "voice 0..7");
        ch = (int)v;
    }
    if (n < 0) {
        audio_sfx_stop(ch);
        return 0;
    }
    int v = audio_sfx((int)n, ch, (int)luaL_optinteger(L, 3, 0), (float)luaL_optnumber(L, 4, 1.0));
    if (v < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, v);
    return 1;
}

/* sfxpos(voice) -> the effect playing on the voice and its step, or nil */
static int l_sfxpos(lua_State *L)
{
    int step = 0, n = audio_sfx_pos((int)voice_arg(L), &step);
    if (n < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, n);
    lua_pushinteger(L, step);
    return 2;
}

/* music(n, [fade_ms], [position]): song n of the bank, from a position
 * (0 = the first pattern), fading in; music(-1, [fade_ms]) stops it;
 * music() -> song, position, step, pattern while it plays, else nil */
static int l_music(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        int song, order, step, pat;
        if (!audio_music_pos(&song, &order, &step, &pat)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, song);
        lua_pushinteger(L, order);
        lua_pushinteger(L, step);
        lua_pushinteger(L, pat);
        return 4;
    }
    lua_Integer n = luaL_checkinteger(L, 1);
    lua_Integer fade = luaL_optinteger(L, 2, 0);
    if (n < 0)
        audio_music_stop((int)fade);
    else
        audio_music((int)n, (int)luaL_optinteger(L, 3, 0), (int)fade);
    return 0;
}

/* tempo(scale): the music plays scale times faster (1 = as written) */
static int l_tempo(lua_State *L)
{
    audio_tempo((float)luaL_checknumber(L, 1));
    return 0;
}

/* mute(track, [on]): a track of the music goes silent (on, the default)
 * or plays again */
static int l_mute(lua_State *L)
{
    lua_Integer t = luaL_checkinteger(L, 1);
    luaL_argcheck(L, t >= 0 && t < 8, 1, "track 0..7");
    audio_mute((int)t, lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2));
    return 0;
}

/* volume([level]) -> the master volume 0..10 (set it with level); the
 * console keeps it in its settings */
static int l_volume(lua_State *L)
{
    if (!lua_isnoneornil(L, 1))
        audio_set_volume((int)luaL_checkinteger(L, 1));
    lua_pushinteger(L, audio_volume());
    return 1;
}

/* audio_bank(data): plays this bank (a string, as cart_audio() gives it)
 * from now on; nil: none. true, or nil and a message. For the editor. */
static int l_audio_bank(lua_State *L)
{
    size_t len = 0;
    const char *d = lua_isnoneornil(L, 1) ? NULL : luaL_checklstring(L, 1, &len);
    char err[64];
    if (audio_bank((const uint8_t *)d, len, err, sizeof err) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* audio_pattern(p, bpm, swing, [step]): pattern p of the bank, looping;
 * audio_pattern(-1) stops. For the editor. */
static int l_audio_pattern(lua_State *L)
{
    lua_Integer p = luaL_checkinteger(L, 1);
    if (p < 0) {
        audio_music_stop(0);
        return 0;
    }
    audio_music_pattern((int)p, (int)luaL_optinteger(L, 2, 120), (int)luaL_optinteger(L, 3, 0),
                        (int)luaL_optinteger(L, 4, 0));
    return 0;
}

/* audio_play(voice, sound, note, [vol], [fx], [ms]): sound of the bank on a
 * voice, as a step plays it (note: MIDI number), released after ms
 * (default 400). For the editor. */
static int l_audio_play(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_play((int)ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
               (int)luaL_optinteger(L, 4, 255), (int)luaL_optinteger(L, 5, 0),
               (uint32_t)luaL_optinteger(L, 6, 400));
    return 0;
}

/* ---------------------------------------------------------------- keys */

/* keyp(): the next key typed, as text ("a", "\n", "\b", "\t"), a name
 * ("up", "down", "left", "right", "home", "end", "pgup", "pgdn", "del",
 * "esc", "f1".."f10") or "^s" for Ctrl+S, "^S" for Ctrl+Shift+S; nil if
 * none. F11, F12 and Ctrl+Esc are the system's (they never come). The first call
 * turns on typing: the keyboard stops being a gamepad for btn(), Esc no
 * longer leaves the cartridge (Start+Select and PS still do). */
static int l_keyp(lua_State *L)
{
    if (!rt.text_mode) {
        rt.text_mode = 1;
        hid_text_mode(1);
    }
    if (rt.tq_tail == rt.tq_head || rt.leave_ask) {
        lua_pushnil(L);
        return 1;
    }
    uint8_t c = rt.tq[rt.tq_tail++];
    static const char *const nav[] = { "up", "down", "left", "right", "home", "end", "pgup", "pgdn",
                                       "del", "f1", "f2", "f3", "f4", "f5" };
    char buf[4];
    if (c == HID_KEY_CTRL_SHIFT) {          /* Ctrl+Shift+S: "^S" (the next code is the Ctrl letter) */
        if (rt.tq_tail == rt.tq_head) {
            lua_pushnil(L);
            return 1;
        }
        c = rt.tq[rt.tq_tail++];
        if (c >= 1 && c <= 26) {
            buf[0] = '^';
            buf[1] = (char)('A' + c - 1);
            buf[2] = 0;
            lua_pushstring(L, buf);
            return 1;
        }
    }
    if (c >= HID_KEY_UP && c <= HID_KEY_F1 + 4) lua_pushstring(L, nav[c - HID_KEY_UP]);
    else if (c >= HID_KEY_F6 && c <= HID_KEY_F6 + 6) lua_pushfstring(L, "f%d", c - HID_KEY_F6 + 6);
    else if (c == 0x1B) lua_pushstring(L, "esc");
    else if (c == '\r') lua_pushstring(L, "\n");
    else if (c == 0x7F) lua_pushstring(L, "\b");
    else if (c == '\t') lua_pushstring(L, "\t");
    else if (c >= 1 && c <= 26) { buf[0] = '^'; buf[1] = (char)('a' + c - 1); buf[2] = 0; lua_pushstring(L, buf); }
    else { buf[0] = (char)c; buf[1] = 0; lua_pushstring(L, buf); }
    return 1;
}

/* keyhelp(keys [, title]): the cartridge's own keys, shown under the
 * system's while F12 is held (the system's keys, src/kernel/syskeys.h):
 * a list of { "keys", "what" } ("ctrl d", "x", "shift w", "1 - 5", "a / d")
 * and strings, the headings of its groups. The system's keys are not for
 * the cartridge to give another meaning: one in the list is said in the log
 * and shown in red. Returns how many of them there are. */
static int l_keyhelp(lua_State *L)
{
    if (rt.keyhelp) {
        luaL_unref(L, LUA_REGISTRYINDEX, rt.keyhelp);
        rt.keyhelp = 0;
    }
    if (lua_isnoneornil(L, 1)) {
        lua_pushinteger(L, 0);
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    int clashes = 0;
    const lua_Integer n = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= n; i++) {
        if (lua_rawgeti(L, 1, i) == LUA_TTABLE) {
            lua_rawgeti(L, -1, 1);
            const char *k = lua_tostring(L, -1);
            if (k && syskeys_reserved(k)) {
                kprintf("keyhelp: \"%s\" is a system key: not for the cartridge's own use\n", k);
                clashes++;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_newtable(L);                        /* { list, title } */
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    lua_pushstring(L, luaL_optstring(L, 2, "this cartridge"));
    lua_rawseti(L, -2, 2);
    rt.keyhelp = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushinteger(L, clashes);
    return 1;
}

/* keyheld(name): true while a key is held on the USB keyboard: "f1".."f12",
 * "tab", "space", "enter", "esc" */
static int l_keyheld(lua_State *L)
{
    const char *n = luaL_checkstring(L, 1);
    int u = 0;
    if ((n[0] == 'f' || n[0] == 'F') && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12) u = 0x3A + k - 1;
    } else if (!strcmp(n, "tab")) u = 0x2B;
    else if (!strcmp(n, "space")) u = 0x2C;
    else if (!strcmp(n, "enter")) u = 0x28;
    else if (!strcmp(n, "esc")) u = 0x29;
    lua_pushboolean(L, u && hid_usage_held((uint8_t)u));
    return 1;
}

/* rawkeys(on): the keyboards stop being controllers for btn() and pad()
 * (the cartridge reads them with keydown(), to map them as it likes); Esc
 * still leaves the cartridge */
static int l_rawkeys(lua_State *L)
{
    rt.raw_keys = lua_toboolean(L, 1);
    return 0;
}

/* keydown(usage): true while the key with this USB HID usage is held on a
 * keyboard (0x04 = A ... 0x1D = Z, 0x28 Enter, 0x4F-0x52 arrows, 0xE0-0xE7
 * Ctrl, Shift, Alt, GUI left then right) */
static int l_keydown(lua_State *L)
{
    lua_Integer u = luaL_checkinteger(L, 1);
    lua_pushboolean(L, u > 0 && u < 256 && !rt.leave_ask && hid_usage_held((uint8_t)u));
    return 1;
}

/* keys() -> { usage, ... }: the keys held now */
static int l_keys(lua_State *L)
{
    uint8_t u[16];
    int n = rt.leave_ask ? 0 : hid_keys_held(u, 16);
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, u[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* pad([p]) -> the controller buttons player p (1-4) holds, as bits: 1 left,
 * 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y,
 * 1024 L1, 2048 R1; without p, any player */
/* mouse(on [, arrow]): the cartridge wants the pointer (M32; without it
 * there is none in a cartridge), with the system's arrow drawn over the
 * frame unless arrow is false; returns false when the console has it off
 * (mouse=off in bm/config.txt). mouse() -> x, y, buttons (1 left, 2 right,
 * 4 middle), wheel steps this frame (up positive), shown; nil when the
 * cartridge did not ask or nothing can move it (no mouse, no right stick). */
static int l_mouse(lua_State *L)
{
    if (lua_gettop(L) >= 1) {
        rt.mouse = lua_toboolean(L, 1);
        rt.mouse_arrow = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
        pointer_env(rt.mouse, rt.g.w, rt.g.h);
        rt.ptr = *pointer_get();
        lua_pushboolean(L, pointer_enabled());
        return 1;
    }
    if (!rt.mouse || !pointer_enabled() || !rt.ptr.available) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, rt.ptr.x);
    lua_pushinteger(L, rt.ptr.y);
    lua_pushinteger(L, rt.leave_ask ? 0 : rt.ptr.buttons);
    lua_pushinteger(L, rt.leave_ask ? 0 : rt.ptr.wheel);
    lua_pushboolean(L, rt.ptr.shown);
    return 5;
}

/* mousep([i]): the button i (0 left, 1 right, 2 middle; default 0) was
 * pressed this frame, with the pointer shown */
static int l_mousep(lua_State *L)
{
    lua_Integer i = luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, rt.mouse && !rt.leave_ask && i >= 0 && i < 3 && (rt.ptr.pressed >> i & 1));
    return 1;
}

static int l_pad(lua_State *L)
{
    uint32_t b = 0;
    if (lua_isnoneornil(L, 1)) {
        for (int p = 0; p < INPUT_PLAYERS; p++)
            b |= rt.praw[p];
    } else {
        lua_Integer p = luaL_checkinteger(L, 1);
        if (p >= 1 && p <= INPUT_PLAYERS)
            b = rt.praw[p - 1];
    }
    lua_pushinteger(L, b);
    return 1;
}

/* timeslice(co, [k]): coroutine co yields (resume returns true and
 * nothing) after about k thousand Lua instructions in a frame (default
 * 400), so a long computation goes on over several frames instead of
 * stopping the cartridge; timeslice(nil) turns it off */
static int l_timeslice(lua_State *L)
{
    rt.slice_thread = lua_isthread(L, 1) ? lua_tothread(L, 1) : NULL;
    lua_Integer k = luaL_optinteger(L, 2, 400);
    rt.slice_len = (uint32_t)(k < 10 ? 10 : k > FRAME_BUDGET / 2 ? FRAME_BUDGET / 2 : k);
    rt.slice_at = rt.slice_len;
    return 0;
}

/* the dev kit's overlay (defined with it) */
static int l_devkit(lua_State *L);
static int l_devinfo(lua_State *L);

/* cartridge files, for the editor (defined after the asset loader) */
static int l_ls(lua_State *L);
static int l_cart_load(lua_State *L);
static int l_cart_new(lua_State *L);
static int l_cart_save(lua_State *L);
static int l_cart_run(lua_State *L);
static int l_cart_tool(lua_State *L);
static int l_cart_arg(lua_State *L);
static int l_cart_audio(lua_State *L);
static int l_cart_put_audio(lua_State *L);
static int l_cart_data(lua_State *L);
static int l_cart_read(lua_State *L);
static int l_cart_write(lua_State *L);
static int l_cart_meshes(lua_State *L);
static int l_mesh_reduce(lua_State *L);
static int l_picture3d(lua_State *L);
static int l_cutout3d(lua_State *L);
static int l_cart_sheet(lua_State *L);

/* ---------------------------------------------------------------- light */

static int video_to_ram(g16_t *g);

/* light_begin([ambient]): starts the lights of this frame; everything drawn
 * so far will be lit by light_end(). The cartridge draws into RAM from now
 * on (lighting reads the picture back). */
static int l_light_begin(lua_State *L)
{
    sync3d();
    if (!rt.light.rgb && g16_light_init(&rt.light, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    if (video_to_ram(&rt.g) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_light_clear(&rt.light, (uint32_t)luaL_optinteger(L, 1, 0x000000));
    return 0;
}

/* light(x, y, radius, colour [, intensity]) - world coordinates (camera) */
static int l_light(lua_State *L)
{
    if (!rt.light.rgb)
        return luaL_error(L, "light() before light_begin()");
    g16_light_add(&rt.light, fnum(L, 1, 0) - (float)rt.g.cam_x, fnum(L, 2, 0) - (float)rt.g.cam_y,
                  fnum(L, 3, 32), (uint32_t)luaL_optinteger(L, 4, 0xFFFFFF), fnum(L, 5, 1));
    return 0;
}

static int l_light_end(lua_State *L)
{
    (void)L;
    sync3d();
    if (rt.light.rgb)
        g16_light_apply(&rt.g, &rt.light);
    return 0;
}

/* Lighting by levels, as in Dank Tomb: fades() gives each colour what it
 * becomes at every light level, dark_begin() fills the screen with the
 * ambient level, glow() adds lamps (rings of levels), dark_end() turns
 * every pixel into its colour at its level. */
static int fade_ready(lua_State *L)
{
    if (!rt.fade.lv && g16_fade_init(&rt.fade, rt.g.w, rt.g.h) != 0)
        return luaL_error(L, "not enough memory for lighting");
    return 0;
}

/* fades({ {from, l0, l1, ...}, ... }) -> levels: every row has the same
 * length, 3..17 (2..16 levels, l0 the darkest) */
static int l_fades(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    fade_ready(L);
    const lua_Integer n = luaL_len(L, 1);
    int levels = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_geti(L, 1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        const lua_Integer len = luaL_len(L, -1);
        if (i == 1) {
            if (len < 3 || len > G16_FADE_LEVELS + 1)
                return luaL_error(L, "fades: a row is a colour and 2 to %d levels", G16_FADE_LEVELS);
            levels = (int)len - 1;
            g16_fade_reset(&rt.fade, levels);
        } else if (len != levels + 1) {
            return luaL_error(L, "fades: row %d has %d levels, not %d", (int)i, (int)len - 1, levels);
        }
        uint16_t to[G16_FADE_LEVELS];
        lua_geti(L, -1, 1);
        const uint16_t from = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
        lua_pop(L, 1);
        for (int k = 0; k < levels; k++) {
            lua_geti(L, -1, k + 2);
            to[k] = g16_rgb24((uint32_t)luaL_checkinteger(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (g16_fade_colour(&rt.fade, from, to) != 0)
            return luaL_error(L, "fades: more than %d colours", G16_FADE_COLOURS);
    }
    g16_fade_done(&rt.fade);
    lua_pushinteger(L, rt.fade.levels);
    return 1;
}

/* dark_begin([ambient level]): everything drawn so far will be lit by
 * dark_end() (the cartridge draws into RAM from now on) */
static int l_dark_begin(lua_State *L)
{
    fade_ready(L);
    if (video_to_ram(&rt.g) != 0)
        return luaL_error(L, "not enough memory for lighting");
    g16_fade_clear(&rt.fade, (int)luaL_optinteger(L, 1, 0));
    return 0;
}

/* online([on [, note]]): the game is played over the network. PS, Ctrl+Esc
 * and Start+Select do not suspend it (the others play on) but ask the
 * player leaving, on this console only, "Leave the match?" over the game
 * (which sees none of the buttons while it is open): yes calls the
 * cartridge's _leave() (it tells the server) and ends the game, back
 * stays. note: a line under the question ("You are the host: the match
 * ends for everyone."). Returns whether it was online and whether the
 * question is open (the player may be away). */
static int l_online(lua_State *L)
{
    const int was = rt.online;
    if (!lua_isnone(L, 1)) {
        rt.online = lua_toboolean(L, 1);
        ksnprintf(rt.online_note, sizeof rt.online_note, "%s", rt.online ? luaL_optstring(L, 2, "") : "");
        if (!rt.online)
            rt.leave_ask = 0;                   /* (the buttons it took come back released) */
    }
    lua_pushboolean(L, was);
    lua_pushboolean(L, rt.leave_ask);
    return 2;
}

/* glow(x, y, radius, level [, dither 0..1]) - world coordinates (camera) */
static int l_glow(lua_State *L)
{
    if (!rt.fade.lv)
        return luaL_error(L, "glow() before dark_begin()");
    const float d = fnum(L, 5, 0.5f);
    g16_fade_glow(&rt.fade, (int)floorf(fnum(L, 1, 0)) - rt.g.cam_x, (int)floorf(fnum(L, 2, 0)) - rt.g.cam_y,
                  (int)fnum(L, 3, 32), (int)luaL_checkinteger(L, 4), (int)(d * 256));
    return 0;
}

static int l_dark_end(lua_State *L)
{
    (void)L;
    if (rt.fade.lv) {
        sync3d();                       /* it reads the page: the 3D (and a cls) on it first */
        g16_fade_apply(&rt.g, &rt.fade);
    }
    return 0;
}

static int l_quit(lua_State *L)
{
    (void)L;
    rt.quit = 1;
    return 0;
}

static const luaL_Reg api[] = {
    { "cls", l_cls }, { "pset", l_pset }, { "pget", l_pget }, { "line", l_line },
    { "rect", l_rect }, { "rectfill", l_rectfill }, { "circ", l_circ }, { "circfill", l_circfill },
    { "spr", l_spr }, { "sspr", l_sspr }, { "map", l_map }, { "mget", l_mget }, { "mset", l_mset },
    { "fget", l_fget }, { "fset", l_fset }, { "mflags", l_mflags }, { "msize", l_msize },
    { "mlayers", l_mlayers }, { "zone", l_zone }, { "zones", l_zones }, { "zspr", l_zspr },
    { "zboxes", l_zboxes },
    { "sget", l_sget }, { "sset", l_sset }, { "print", l_print }, { "font", l_font }, { "camera", l_camera },
    { "prompt", l_prompt }, { "lastinput", l_lastinput },
    { "clip", l_clip }, { "rgb", l_rgb }, { "btn", l_btn }, { "btnp", l_btnp },
    { "players", l_players }, { "stick", l_stick },
    { "time", l_time }, { "stat", l_stat }, { "frameskip", l_frameskip }, { "devkit", l_devkit },
    { "devinfo", l_devinfo }, { "code_tokens", l_code_tokens }, { "tri", l_tri },
    { "mesh", l_mesh }, { "mesh_sphere", l_mesh_sphere }, { "mesh_cube", l_mesh_cube },
    { "model", l_model }, { "models", l_models }, { "bounds3d", l_bounds3d },
    { "animate", l_animate }, { "clips", l_clips }, { "bone3d", l_bone3d },
    { "draw3d", l_draw3d }, { "camera3d", l_camera3d }, { "light3d", l_light3d },
    { "sky3d", l_sky3d }, { "shine3d", l_shine3d }, { "shadow3d", l_shadow3d },
    { "point3d", l_point3d }, { "line3d", l_line3d }, { "sprite3d", l_sprite3d },
    { "bone_turn", l_bone_turn }, { "bones3d", l_bones3d }, { "hit3d", l_hit3d },
    { "world3d", l_world3d }, { "world_box", l_world_box }, { "world_ray", l_world_ray },
    { "world_move", l_world_move }, { "world_floor", l_world_floor },
    { "fog3d", l_fog3d }, { "project3d", l_project3d }, { "lamp3d", l_lamp3d },
    { "zclear", l_zclear }, { "gpu3d", l_gpu3d }, { "screen", l_screen }, { "log", l_log }, { "report", l_report }, { "keyhelp", l_keyhelp }, { "quit", l_quit },
    { "keymap", l_keymap }, { "controller", l_controller }, { "online", l_online },
    { "udp_open", l_udp_open }, { "udp_send", l_udp_send }, { "udp_recv", l_udp_recv }, { "udp_close", l_udp_close },
    { "net_ip", l_net_ip }, { "net_resolve", l_net_resolve },
    { "save", l_save }, { "saved", l_saved },
    { "keyp", l_keyp }, { "keyheld", l_keyheld }, { "rawkeys", l_rawkeys }, { "keydown", l_keydown },
    { "keys", l_keys }, { "pad", l_pad }, { "mouse", l_mouse }, { "mousep", l_mousep }, { "timeslice", l_timeslice }, { "ls", l_ls }, { "cart_load", l_cart_load }, { "cart_new", l_cart_new },
    { "cart_save", l_cart_save }, { "cart_run", l_cart_run }, { "cart_tool", l_cart_tool }, { "cart_arg", l_cart_arg },
    { "cart_data", l_cart_data },
    { "cart_read", l_cart_read }, { "cart_write", l_cart_write }, { "cart_meshes", l_cart_meshes },
    { "mesh_reduce", l_mesh_reduce }, { "picture3d", l_picture3d }, { "cutout3d", l_cutout3d },
    { "cart_sheet", l_cart_sheet },
    { "light_begin", l_light_begin }, { "light", l_light }, { "light_end", l_light_end },
    { "fades", l_fades }, { "dark_begin", l_dark_begin }, { "glow", l_glow }, { "dark_end", l_dark_end },
    { "note", l_note }, { "noteoff", l_noteoff }, { "freq", l_freq },
    { "envelope", l_envelope }, { "duty", l_duty }, { "playing", l_playing }, { "apu", l_apu },
    { "hz", l_hz }, { "slide", l_slide }, { "vibrato", l_vibrato }, { "arp", l_arp },
    { "sfx", l_sfx }, { "sfxpos", l_sfxpos }, { "music", l_music }, { "tempo", l_tempo },
    { "mute", l_mute }, { "volume", l_volume },
    { "audio_bank", l_audio_bank }, { "audio_pattern", l_audio_pattern }, { "audio_play", l_audio_play },
    { "cart_audio", l_cart_audio }, { "cart_put_audio", l_cart_put_audio },
    { NULL, NULL },
};

/* ---------------------------------------------------------------- state */

/* bmhost's profiler of the Lua (BMHOST_LUAPROF): where the count hook
 * finds the cartridge; not in the kernel (a weak symbol left undefined) */
extern void bm_lua_sample(lua_State *L, lua_Debug *ar) __attribute__((weak));

static void hook(lua_State *L, lua_Debug *ar)
{
    if (bm_lua_sample)
        bm_lua_sample(L, ar);
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
    lua_pushglobaltable(L);
    luaL_setfuncs(L, api, 0);
    lua_pop(L, 1);
    luaL_requiref(L, "n8", luaopen_n8, 1);      /* the nano8 machine (carts/nano8) */
    lua_pop(L, 1);
    ai_lua_open(L);             /* the assistant (M30): idle until asked */
    nnet_lua_open(L);           /* small INT8 networks of the carts (M38.4) */
    bm_require_open(L);         /* require "assist": libraries in the kernel */
    static const char *const waves[SYNTH_WAVES] = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL" };
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
    int r = lua_pcall(L, 0, 0, -2);
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
 * 17-21 F6-F10, 23-24 F11-F12) */
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
    if ((uint8_t)c >= HID_KEY_F6 && (uint8_t)c <= HID_KEY_F6 + 6)
        return;                             /* a UTF-8 byte, not a function key */
    text_push((uint8_t)c);
}

/* HID_* bits -> btn() bits: 0-5 are the same; X and Y are 6 and 7, or A
 * and B for a cartridge that never asked for them */
static uint16_t hid_to_btn(uint32_t pad)
{
    uint16_t b = pad & 0x3F;
    if (pad & HID_START) b |= 1u << BTN_START;
    if (pad & HID_SELECT) b |= 1u << BTN_SELECT;
    if (pad & HID_X) b |= rt.uses_xy ? 1u << BTN_X : 1u << BTN_A;
    if (pad & HID_Y) b |= rt.uses_xy ? 1u << BTN_Y : 1u << BTN_B;
    return b;
}

static int poll_keys(void)
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
            if (k == HID_KEY_F6 + 5 || k == HID_KEY_F6 + 6 || rt.leave_ask)
                continue;                   /* (and the keys of the leave question) */
            if (hid_usage_held(0x45) && (k == HID_KEY_DOWN || k == HID_KEY_UP || k == HID_KEY_PGDN ||
                                         k == HID_KEY_PGUP || k == HID_KEY_RIGHT || k == HID_KEY_LEFT)) {
                rt.help_page += k == HID_KEY_DOWN || k == HID_KEY_PGDN || k == HID_KEY_RIGHT ? 1 : -1;
                continue;
            }
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

static int load_assets(const bm_cart_t *c)
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

static void free_assets(void)
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
}

/* ---------------------------------------------------------------- editor */

/* The project open in the editor lives in the editor's own sprite sheet and
 * map; its cover is kept here as it came. Across the editor's "run" the
 * kernel keeps a request (which file to play) and an argument (the file,
 * and the error it stopped with). */
static uint8_t *proj_cover;
static uint16_t proj_cover_w, proj_cover_h;
/* the sound bank of the project (cart_save keeps it), and of the
 * cartridge playing (a copy: its bytes are freed while it is suspended) */
static uint8_t *proj_audio, *own_audio;
static uint32_t proj_audio_len, own_audio_len;

static void set_copy(uint8_t **dst, uint32_t *dlen, const void *src, uint32_t len)
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

static void extras_free(void)
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
static char arg_path[64], arg_error[512], last_error[512];
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

static int tool_mode;

void bm_set_tool(int on)
{
    tool_mode = on;
}

/* Where a cartridge may write a cartridge file: anywhere for the tools
 * built into the kernel; for the others (SD card, Market) only .bm files in
 * /carts, never the kernel, the settings or another folder. Returns 0, or
 * -1 with a message on the Lua stack (false, message). */
static int write_refused(lua_State *L, const char *path)
{
    if (tool_mode)
        return 0;
    const char *name = path;
    if (strncmp(path, "/carts/", 7) == 0)
        name = path + 7;
    size_t n = strlen(name);
    int ok = name[0] && name[0] != '.' && !strchr(name, '/') && !strchr(name, '\\') &&
             n > 3 && (name[n - 3] == '.') && (name[n - 2] | 32) == 'b' && (name[n - 1] | 32) == 'm';
    if (ok)
        return 0;
    lua_pushboolean(L, 0);
    lua_pushfstring(L, "%s: a cartridge writes only .bm files in /carts", path);
    return -1;
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
static int l_ls(lua_State *L)
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

/* The resolutions of a cartridge, as the Lua side names them */
static const char *res_name(int w)
{
    return w == 320 ? "320x180" : w == 256 ? "256x256" : w == 480 ? "480x270" : "640x360";
}

static int res_width(const char *res)
{
    return strcmp(res, "320x180") == 0 ? 320 : strcmp(res, "256x256") == 0 ? 256
         : strcmp(res, "480x270") == 0 ? 480 : 640;
}

static void push_project(lua_State *L, const char *title, const char *author, int w, const char *lua,
                         size_t lua_len)
{
    lua_newtable(L);
    lua_pushstring(L, title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, res_name(w));
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
static int l_cart_load(lua_State *L)
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
static int l_cart_sheet(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        int w = (int)luaL_checkinteger(L, 1), h = (int)luaL_checkinteger(L, 2);
        luaL_argcheck(L, w >= G16_CELL && w <= BM_SHEET_MAX && w % G16_CELL == 0, 1, "8 to 4096, a multiple of 8");
        luaL_argcheck(L, h >= G16_CELL && h <= BM_SHEET_MAX && h % G16_CELL == 0, 2, "8 to 4096, a multiple of 8");
        if (w != rt.sheet.w || h != rt.sheet.h) {
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
static int l_cart_new(lua_State *L)
{
    bm_cart_t c;
    memset(&c, 0, sizeof c);
    sync3d();
    free_assets();
    if (load_assets(&c) != 0)
        return luaL_error(L, "not enough memory for the cartridge");
    free(proj_cover);
    proj_cover = NULL;
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    extras_free();
    return 0;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* "/carts/GAME.BM" -> "/carts", "GAME.BM"; a bare name goes to /carts */
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
 * "/carts/MYGAME.BM"). */
static int l_cart_save(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (write_refused(L, path))
        return 2;
    const char *title = field(L, 2, "title", ""), *author = field(L, 2, "author", "");
    const char *res = field(L, 2, "res", "640x360");
    lua_getfield(L, 2, "lua");
    size_t lua_len;
    const char *lua = luaL_checklstring(L, -1, &lua_len);
    int w = res_width(res), h = w == 256 ? 256 : w * 9 / 16;

    char dir[64], name[16];
    split_path(path, dir, sizeof dir, name, sizeof name);

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
    /* cover (first: the menu reads only the start), code, sheet, map, its
     * layers, the sound bank, the flags, the sheet's named zones and their
     * boxes, then the sections kept from the file (3D models...) */
    enum { FIXED = 9 };
    const int nsec = FIXED + proj_extras;
    uint32_t sizes[FIXED + PROJ_EXTRA_MAX] = {
        proj_cover ? 4u + (uint32_t)proj_cover_w * proj_cover_h * 4 : 0, (uint32_t)lua_len, 4 + sw * sh * 4,
        4 + mw * mh * 2, named ? 8u + (uint32_t)rt.nlayers * BM_LAYER_NAME + (uint32_t)(rt.nlayers - 1) * mw * mh * 2 : 0,
        proj_audio_len, frows ? 4 + per * frows : 0, rt.nzones ? 4u + (uint32_t)rt.nzones * BM_SPRITE_SIZE : 0,
        rt.nzones && rt.nboxes ? 4u + (uint32_t)rt.nboxes * BM_BOX_SIZE : 0 };
    uint32_t types[FIXED + PROJ_EXTRA_MAX] = { BM_SEC_COVER, BM_SEC_LUA, BM_SEC_SHEET, BM_SEC_MAP, BM_SEC_LAYERS,
                                               BM_SEC_AUDIO, BM_SEC_FLAGS, BM_SEC_SPRITES, BM_SEC_BOXES };
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
                memcpy(q, rt.layer_name[l], strnlen(rt.layer_name[l], BM_LAYER_NAME));
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
    put32(buf + 20, crc32(buf + BM_HEADER_SIZE, total - BM_HEADER_SIZE));
    int ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, buf, total) == 0;
    free(buf);
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_read(path) -> {title, author, res, lua, size}, or nil and a message.
 * Only reads: the running cartridge's sheet and map stay as they are (code
 * editors with several files open). */
static int l_cart_read(lua_State *L)
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
    lua_pushstring(L, res_name(c.width));
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
    return bm_mesh_capture(L, c->lua, c->lua_size, c->width, c->height, api, c->mesh, c->mesh_size);
}

/* cart_meshes(path) -> { {name=, kind=, verts=, faces=, [uv=]}, ... }, and
 * nil or the first error of the cartridge's code; or nil and a message.
 * The 3D meshes the cartridge builds in its code (mesh(), mesh_sphere(),
 * mesh_cube()), found by running it apart with every other function of bm
 * doing nothing (meshcap.c); the fields are the arguments of mesh(). For
 * bm Mesh. */
static int l_cart_meshes(lua_State *L)
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
static int l_mesh_reduce(lua_State *L)
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
        lua_pushlstring(L, (const char *)m->texture, 256 * 256 * 4);
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
static int l_cutout3d(lua_State *L)
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
static int l_picture3d(lua_State *L)
{
    const char *action = luaL_checkstring(L, 1);
    char err[160];
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
 * too. sections: {[8] = MESH bytes, [9] = ANIM bytes} (false takes them
 * away), checked first (bm Mesh, the 3D tools). sheet = true: the project's
 * sprite sheet (cart_load, sset, cart_sheet) takes the place of the file's,
 * as SHEET8 with `palette` ({0xRRGGBB, ...}) first in its palette when it
 * has at most 256 colours (sheet_section; bm Pixel). from: take the
 * sections from another file ("save as"); from = false: a new cartridge,
 * whatever the file holds now (bm Studio's new project). A file that does not exist yet
 * becomes a new cartridge with the code (its name must then be 8.3; an
 * existing file keeps its long name). */
static int l_cart_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (write_refused(L, path))
        return 2;
    lua_getfield(L, 2, "lua");
    size_t lua_len = 0;
    const char *lua = lua_isnil(L, -1) ? NULL : luaL_checklstring(L, -1, &lua_len);
    bm_put_t put[4];
    int nput = 0;
    lua_getfield(L, 2, "sections");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TTABLE);
        static const uint32_t kinds[2] = { BM_SEC_MESH, BM_SEC_ANIM };
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            lua_Integer t = lua_isinteger(L, -2) ? lua_tointeger(L, -2) : -1;
            if (t != BM_SEC_MESH && t != BM_SEC_ANIM)
                return luaL_error(L, "sections: only 8 (MESH) and 9 (ANIM)");
            lua_pop(L, 1);
        }
        for (int k = 0; k < 2; k++) {
            lua_rawgeti(L, -1, kinds[k]);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            size_t n = 0;
            const uint8_t *s = lua_toboolean(L, -1) ? (const uint8_t *)luaL_checklstring(L, -1, &n) : NULL;
            if (s && (n > 0x1000000 || (kinds[k] == BM_SEC_MESH ? bm_mesh_check(s, (uint32_t)n)
                                                                 : bm_anim_check(s, (uint32_t)n)) < 0)) {
                lua_pushboolean(L, 0);
                lua_pushstring(L, kinds[k] == BM_SEC_MESH ? "broken MESH section" : "broken ANIM section");
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
    const char *base = fresh ? NULL : field(L, 2, "from", path);
    char dir[64], name[FAT_NAME_MAX];
    split_path(path, dir, sizeof dir, name, sizeof name);

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
    const char *res = field(L, 2, "res", res_name(c.width));
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
    /* an existing file keeps its entry (and its long name); a new one is 8.3 */
    fat_entry_t te;
    int ok;
    if (fat_find(path, &te) == 0 && !te.is_dir)
        ok = fat_replace(path, out, out_len) == 0;
    else
        ok = fat_mkdirs(dir) == 0 && fat_write_file(dir, name, out, out_len) == 0;
    free(out);
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_run(path): leaves the editor, plays the cartridge, then comes back
 * to the editor with cart_arg() = { path =, error = } */
static int l_cart_run(lua_State *L)
{
    ksnprintf(run_request, sizeof run_request, "%s", luaL_checkstring(L, 1));
    rt.quit = 1;
    return 0;
}

/* cart_tool(name, [path]): leaves this tool for another of the console's
 * ("studio", "animator", "mesh", "pixel", "code", "sdk", "sound") on the
 * file (bm Studio's "Open in bm Animator") */
static int l_cart_tool(lua_State *L)
{
    ksnprintf(tool_request, sizeof tool_request, "%s", luaL_checkstring(L, 1));
    ksnprintf(run_request, sizeof run_request, "%s", luaL_optstring(L, 2, ""));
    rt.quit = 1;
    return 0;
}

static int l_cart_arg(lua_State *L)
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
static int l_cart_audio(lua_State *L)
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
static int l_cart_put_audio(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (write_refused(L, path))
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
    if (fat_find(path, &e) != 0) {
        size_t lua_len;
        const char *title = luaL_optstring(L, 3, "Sound pack");
        const char *lua = luaL_optlstring(L, 4, NULL, &lua_len);
        if (!lua) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, fat_error());
            return 2;
        }
        char dir[64], name[16];
        split_path(path, dir, sizeof dir, name, sizeof name);
        if (!(buf = new_pack(title, lua, lua_len, audio, alen, &total)))
            return luaL_error(L, "not enough memory to save");
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
        ok = fat_replace(path, buf, total) == 0;
        free(buf);
    }
    lua_pushboolean(L, ok);
    if (ok)
        return 1;
    lua_pushstring(L, fat_error());
    return 2;
}

/* cart_data(type) -> the bytes of the project's MESH (8) or ANIM (9)
 * section, or nil; cart_data(type, bytes) replaces it (nil or "" takes it
 * away) -> true, or false and a message. The bytes are checked first;
 * model() and animate() see the new ones at once, cart_save writes them.
 * bm Studio and bm Animator of the console edit models and skeletons this way. */
static int l_cart_data(lua_State *L)
{
    int type = (int)luaL_checkinteger(L, 1);
    luaL_argcheck(L, type == BM_SEC_MESH || type == BM_SEC_ANIM, 1, "8 (MESH) or 9 (ANIM)");
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
static int video_to_ram(g16_t *g)
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

/* A square 256x256 cartridge is drawn in the middle of a 480x270 screen
 * (1920x1080 / 4: whole pixels on a 1080p TV), black around it: `box` is
 * the byte offset of its top left corner in a page, 0 for the 16:9 sizes. */
static uint32_t box;

static uint16_t *page_px(const framebuffer_t *fb)
{
    return (uint16_t *)(fb->base + box);
}

int bm_video_enter(framebuffer_t *fb, int w, int h, g16_t *g)
{
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
    const int boxed = w == 256 && h == 256;
    const uint32_t fw = boxed ? 480u : (uint32_t)w, fh = boxed ? 270u : (uint32_t)h;
    box = 0;
    if (fb_init_depth(fb, fw, fh, 3, 16) != 0)
        return -1;
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
        if (rt.mouse && rt.mouse_arrow)         /* on the page about to be shown */
            pointer_draw(page_px(fb), fb->pitch / 2, g->w, g->h);
        fb_flip(fb);
        g->px = page_px(fb);
        return 0;
    }
    uint32_t t0 = timer_ticks();
    const uint32_t row = (uint32_t)g->w * 2;
    if (box) {
        uint8_t *dst = fb->base + box;
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
    console_suspend(0);
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
    zclear_dma_start();
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

/* Frees what a cartridge holds (after leave_mode). */
static void release(lua_State *L)
{
    cartnet_reset();            /* the cartridge's sockets */
    audio_reset();
    audio_bank(NULL, 0, NULL, 0);
    set_copy(&own_audio, &own_audio_len, NULL, 0);
    lua_close(L);               /* frees meshes (__gc) before the z-buffer */
    n8lua_close();
    if (rt.r3d.backend)
        gpu3d_drop();           /* an error in the middle of a frame */
    rt.r3d.backend = NULL;
    free(d2.w);                 /* 2D recorded in the middle of a frame (M35) */
    d2.w = NULL;
    d2.n = d2.cap = d2.ops = 0;
    d2.on = 0;
    d2.cut = D2_NONE;
    g16_light_free(&rt.light);
    zclear_dma_wait(1);         /* the DMA may still be clearing it */
    rt.zclear_seen = 0;
    g16_fade_free(&rt.fade);
    if (rt.r3d_ready)
        r3d_free(&rt.r3d);
    rt.r3d_ready = 0;
    free_assets();
}

static int vol_start;                   /* the volume when the cartridge started or resumed */

/* ---------------------------------------------------------------- the dev kit */

/* The performance overlay, over any game (Settings > Performance overlay,
 * F11 on a keyboard, a system key, 'p' on the serial line, devkit() from the
 * game): F11 once the simple page, again the detailed one, again off.
 * Simple: its frames a second, the CPU time of _update + _draw and the Lua
 * instructions of a frame (the mean and, after ^, the most of the last
 * second), and the time of the last 64 frames against the 16.7 ms of a frame
 * at 60 Hz; the memory it uses (its Lua now and, after ^, the most of this
 * run, plus its data: stat(0), 12, 13) and the tokens of its code
 * (stat(11)). Detailed, below: the last frame in its phases (its _update,
 * how many when frameskip() skips frames, its _draw, the 3D: stat(6)), the
 * GPU's work (ms and jobs) or the pixels the ARM drew, the triangles and
 * vertices, the 3D driver, and the game's own lines (devinfo()). On the 8x16
 * grid, top right, bigger on the big screens (x2 from 1280 wide, x3 at 1920):
 *   60fps 6.1ms ^7.5
 *   lua 9k ^10k
 *   ram 612k ^700k
 *   1234 tokens
 *   (the graph)
 *   update 2.1ms
 *   draw   3.8ms
 *   3D     2.5ms
 *   gpu 1.9ms 1 job
 *   tri 3620 vtx 5699
 *   bm3d 2.1 GPU       */
#define PERF_N 64
static int perf_on;                     /* 0 off, 1 simple, 2 detailed */
static struct { uint16_t us10[PERF_N], k[PERF_N]; uint32_t at; } perf;
static gpu3d_stats_t perf_gpu;          /* the GPU's totals at the last frame (the detailed page) */

/* KiB of the cartridge's data in memory, besides its Lua: the sprite sheet
 * (RGB565 + alpha + the cells), the map, the models and skeletons, the sound
 * bank and the z-buffer of the 3D (stat(13), the overlay's RAM) */
static uint32_t assets_kb(void)
{
    size_t b = (size_t)rt.sheet.w * rt.sheet.h * 3;
    b += (size_t)(rt.sheet.w / G16_CELL) * (rt.sheet.h / G16_CELL) * 2;
    b += (size_t)rt.map.w * rt.map.h * 2;
    b += rt.mesh_size + rt.anim_size + own_audio_len;
    if (rt.r3d_ready && rt.r3d.zbuf)
        b += (size_t)rt.g.w * rt.g.h * 2;
    return (uint32_t)((b + 1023) / 1024);
}

/* "512k", "1.5M", "12M": KiB on the overlay's few columns */
static void kib_text(char *out, size_t n, uint32_t kb)
{
    if (kb < 1000)
        ksnprintf(out, n, "%luk", (unsigned long)kb);
    else if (kb < 10240)
        ksnprintf(out, n, "%lu.%luM", (unsigned long)(kb / 1024), (unsigned long)(kb % 1024 * 10 / 1024));
    else
        ksnprintf(out, n, "%luM", (unsigned long)(kb / 1024));
}

void bm_set_perf(int level) { perf_on = level < 0 ? 0 : level > 2 ? 2 : level; }
int bm_perf(void) { return perf_on; }

/* devkit([mode]) -> the dev kit's overlay: 0 off, 1 simple, 2 detailed; a
 * mode shows that page (a game's own key for it, e.g. Select on a pad: F11
 * is the keyboard's) */
static int l_devkit(lua_State *L)
{
    const int old = perf_on;
    if (!lua_isnoneornil(L, 1))
        bm_set_perf(ival(L, 1));
    lua_pushinteger(L, old);
    return 1;
}

/* devinfo(line, ...): up to 4 lines of the game on the dev kit's detailed
 * page (its quality, its actors...), 18 characters each; devinfo() none */
static int l_devinfo(lua_State *L)
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
 * hints (the leave question) or a progress bar (0..1000; -1 none) */
static void sys_box(const char *const *lines, int n, int dim_last, int hints, int progress)
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
        x = g16_text_scaled(g, x, y, i ? "Stay" : "Leave", ink, sc) + 2 * fw;
    }
    *g = keep;
}

/* the question to the player leaving an online game */
static void leave_draw(void)
{
    if (!rt.leave_ask)
        return;
    const char *lines[4] = { "Leave the match?", "You will leave the game and", "disconnect from the server.",
                             rt.online_note[0] ? rt.online_note : NULL };
    sys_box(lines, lines[3] ? 4 : 3, lines[3] != NULL, 1, -1);
}

/* the system's notice over the game (a kernel arriving, the restart
 * counted down): from the kernel (bm_set_notice) */
static int (*notice_fn)(char *title, char *detail, int *progress);

void bm_set_notice(int (*fn)(char *title, char *detail, int *progress))
{
    notice_fn = fn;
}

static void notice_draw(void)
{
    char title[64], detail[64];
    int progress = -1;
    if (!notice_fn || !notice_fn(title, detail, &progress))
        return;
    const char *lines[2] = { title, detail };
    sys_box(lines, 2, 0, 0, progress);
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
static void keys_help(lua_State *L)
{
    if (!hid_usage_held(0x45)) {
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

/* "12.3ms" and the like: tenths of a millisecond from microseconds */
static void ms_text(char *out, size_t n, uint32_t us)
{
    ksnprintf(out, n, "%lu.%lums", (unsigned long)(us / 1000), (unsigned long)(us / 100 % 10));
}

/* a count in a few columns: 3620, 12k */
static void count_text(char *out, size_t n, uint32_t v)
{
    if (v < 10000)
        ksnprintf(out, n, "%lu", (unsigned long)v);
    else
        ksnprintf(out, n, "%luk", (unsigned long)(v / 1000));
}

/* the detailed page's lines, under the simple one */
static int perf_detail(char lines[][24])
{
    int n = 0;
    char a[16], b[16];
    ms_text(a, sizeof a, rt.update_us);
    if (rt.updates > 1)
        ksnprintf(lines[n++], 24, "update %s x%lu", a, (unsigned long)rt.updates);
    else
        ksnprintf(lines[n++], 24, "update %s", a);
    ms_text(a, sizeof a, rt.draw_us);
    ksnprintf(lines[n++], 24, "draw   %s", a);
    ms_text(a, sizeof a, rt.us3d);
    ksnprintf(lines[n++], 24, "3D     %s", a);
    const int gpu = rt.r3d_ready && rt.r3d.backend;
    if (gpu) {
        gpu3d_stats_t st;
        gpu3d_peek_stats(&st);
        const int fresh = st.jobs >= perf_gpu.jobs && st.bin_us + st.render_us >= perf_gpu.bin_us + perf_gpu.render_us;
        const uint32_t jobs = fresh ? st.jobs - perf_gpu.jobs : st.jobs;
        const uint32_t us = fresh ? st.bin_us + st.render_us - perf_gpu.bin_us - perf_gpu.render_us
                                  : st.bin_us + st.render_us;
        perf_gpu = st;
        ms_text(b, sizeof b, us);
        ksnprintf(lines[n++], 24, "gpu %s %lu job%s", b, (unsigned long)jobs, jobs == 1 ? "" : "s");
    } else if (rt.r3d_ready) {
        count_text(a, sizeof a, rt.r3d.pixels);
        ksnprintf(lines[n++], 24, "px %s", a);
    }
    if (rt.r3d_ready) {
        count_text(a, sizeof a, rt.r3d.tris_drawn);
        count_text(b, sizeof b, rt.r3d.verts);
        ksnprintf(lines[n++], 24, "tri %s vtx %s", a, b);
    }
    if (rt.r3d_ready)
        ksnprintf(lines[n++], 24, "bm3d %s %s",
                  bm3d_mode_q(gpu, gpu ? gpu3d_vshader_on() : 0, gpu && gpu3d_queue()),
                  !gpu ? "ARM" : gpu3d_msaa_on() ? "GPU+AA" : "GPU");
    for (int i = 0; i < rt.ndevinfo; i++)
        ksnprintf(lines[n++], 24, "%s", rt.devinfo[i]);
    return n;
}

static void perf_frame(void)
{
    uint32_t i = perf.at++ % PERF_N;
    perf.us10[i] = (uint16_t)(rt.last_cpu_us / 10 > 65535 ? 65535 : rt.last_cpu_us / 10);
    perf.k[i] = (uint16_t)(rt.last_instr_k > 65535 ? 65535 : rt.last_instr_k);
    if (!perf_on)
        return;
    uint32_t n = perf.at < 60 ? perf.at : 60, sum = 0, most = 0, ksum = 0, kmost = 0;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t k = (perf.at - 1 - j) % PERF_N;
        sum += perf.us10[k];
        ksum += perf.k[k];
        if (perf.us10[k] > most) most = perf.us10[k];
        if (perf.k[k] > kmost) kmost = perf.k[k];
    }
    uint32_t mean = sum / n, kmean = ksum / n;
    char more[11][24];
    const int nmore = perf_on == 2 ? perf_detail(more) : 0;
    g16_t *g = &rt.g;
    const g16_t keep = *g;
    g16_camera(g, 0, 0);
    g16_clip(g, 0, 0, 0, 0);
    g->font = &font_console_8x16;
    /* x2 from 1280 wide, x3 at 1920: the same size on a TV as at 640 */
    const int z = g->w >= 1280 ? g->w / 640 : 1;
    const int x = g->w - 144 * z;                 /* the text on the 8 x 16 grid (read by the tests) */
    g16_rectfill(g, x - 4 * z, 0, 148 * z, (nmore ? 98 + 16 * nmore : 82) * z, g16_rgb(8, 8, 16));
    char line[32], now[12], top[12];
    ksnprintf(line, sizeof line, "%lufps %lu.%lums ^%lu.%lu", rt.fps, mean / 100, mean / 10 % 10,
              most / 100, most / 10 % 10);
    g16_text_scaled(g, x, 0, line, g16_rgb(232, 232, 216), z);
    ksnprintf(line, sizeof line, "lua %luk ^%luk", kmean, kmost);
    g16_text_scaled(g, x, 16 * z, line, g16_rgb(200, 184, 120), z);
    const size_t lua = luavm_mem();
    const uint32_t data_kb = assets_kb();
    kib_text(now, sizeof now, (uint32_t)(lua / 1024) + data_kb);
    kib_text(top, sizeof top, (uint32_t)((lua > rt.lua_peak ? lua : rt.lua_peak) / 1024) + data_kb);
    ksnprintf(line, sizeof line, "ram %s ^%s", now, top);
    g16_text_scaled(g, x, 32 * z, line, g16_rgb(120, 200, 232), z);
    ksnprintf(line, sizeof line, "%d tokens", rt.tokens);
    g16_text_scaled(g, x, 48 * z, line, g16_rgb(184, 160, 232), z);
    /* the last 64 frames, 2 px each; the top is 16.7 ms (a frame at 60 Hz) */
    for (uint32_t j = 0; j < PERF_N && j < perf.at; j++) {
        uint32_t v = perf.us10[(perf.at - 1 - j) % PERF_N];
        int hgt = (int)(v * 14 / 1670);
        if (hgt > 14) hgt = 14;
        if (hgt < 1) hgt = 1;
        uint16_t c = v < 835 ? g16_rgb(72, 200, 96) : v < 1670 ? g16_rgb(232, 200, 64) : g16_rgb(232, 64, 48);
        g16_rectfill(g, x + (140 - (int)j * 2) * z, (80 - hgt) * z, 2 * z, hgt * z, c);
    }
    g16_rectfill(g, x - 2 * z, 65 * z, 144 * z, z, g16_rgb(72, 72, 96));
    /* the detailed page: from the next row of the grid (96) */
    for (int k = 0; k < nmore; k++) {
        more[k][18] = 0;                            /* the box's 18 columns */
        const uint16_t c = k < 3 ? g16_rgb(232, 232, 216) : k < nmore - rt.ndevinfo ? g16_rgb(160, 200, 160)
                                                                                    : g16_rgb(255, 224, 112);
        g16_text_scaled(g, x, (96 + 16 * k) * z, more[k], c, z);
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
    int updated = 0;                        /* this frame's _update ran while the GPU drew the last (M35) */
    uint32_t early_us = 0;                  /* its time */
    /* the time limit in 64 bits: seconds * 10^6 overflows 32 bits after
     * 71 minutes (24 hours used to end a game after 8 min 20 s), and so
     * does the microsecond counter */
    uint64_t played_us = 0, limit_us = (uint64_t)seconds * 1000000u;
    uint32_t played_at = start;
    uint32_t lag = 0, lag_at = start;       /* frameskip(): the time not yet run by an _update */
    int fresh = 0;                          /* the keys read and not yet seen by an _update */
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
            perf_on = (perf_on + 1) % 3;    /* simple, detailed, off */
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
        const uint32_t td = timer_ticks();
        if (call(L, "_draw") != 0) {
            error = lua_tostring(L, -1);
            break;
        }
        rt.draw_us = timer_ticks() - td;
        updated = 0;
        rt.frame++;                         /* (before the next _update, if it runs early) */
        const uint32_t instr_k = rt.frame_instr_k;
        if (submit3d()) {
            /* the frame in the queue (M35): the GPU draws it while the next
             * frame's _update runs; flush3d waits for it before the page
             * is shown */
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
            if (rt.early_touch) {
                rt.no_early = 1;
                kprintf("bm: _update draws 3D or reads the page: it runs after the frame again (frame queue)\n");
            }
        }
        flush3d(0);                         /* the GPU's 3D counts in the frame's time */
        cls_settle();
        rt.last_cpu_us = timer_ticks() - t0 - early_us;
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
        perf_frame();
        leave_draw();
        notice_draw();
        keys_help(L);
        st->cpu_us_total += rt.last_cpu_us;
        if (rt.last_cpu_us > st->cpu_us_max)
            st->cpu_us_max = rt.last_cpu_us;
        st->tris3d = rt.r3d_ready ? rt.r3d.tris_drawn : 0;
        crumb_frame(rt.frame);
        present(fb, &deadline, &prev, &st->dropped);
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

    if (audio_volume() != vol_start)
        config_save();                      /* the volume chosen in the game stays */
    st->frames = (uint32_t)rt.frame;
    st->elapsed_us = timer_ticks() - start;
    st->lua_kb = (uint32_t)(luavm_mem() / 1024);
    st->lua_peak_kb = (uint32_t)((rt.lua_peak > luavm_mem() ? rt.lua_peak : luavm_mem()) / 1024);
    st->assets_kb = assets_kb();
    st->instr_k_max = rt.instr_k_max;
    st->slow = rt.slow_frames;
    st->tokens = (uint32_t)rt.tokens;
    st->gpu3d = rt.r3d_ready && rt.r3d.backend != NULL;
    st->d2_ops = d2.ops;
    st->ok = error == NULL;

    if (!error && left && suspendable && !rt.online_left) {
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

    audio_reset();
    leave_mode(fb, con_w, con_h);
    ksnprintf(last_error, sizeof last_error, "%s", error ? error : "");
    hid_text_mode(0);
    if (error)
        kprintf("\x1b[91mbm: \"%s\" stopped with an error:\n%s\x1b[0m\n", title, error);
    release(L);
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

    bm_close_suspended();              /* one cartridge in memory at a time */
    memset(st, 0, sizeof *st);
    memset(&rt, 0, sizeof rt);
    free(proj_cover);                  /* no project open yet (cart_load) */
    proj_cover = NULL;
    extras_free();
    set_copy(&proj_audio, &proj_audio_len, NULL, 0);
    cur = next;                         /* the options of this run, then none */
    bm_next_run(0, 0, -1, 0);
    if (bm_parse(data, len, &cart, err, sizeof err) != 0) {
        kprintf("\x1b[91mbm: %s\x1b[0m\n", err);
        loading_stop();
        return BM_ENDED;
    }
    loading_tick();
    if (cur.w > 0 && cur.h > 0) {
        cart.width = cur.w;
        cart.height = cur.h;
    }
    memcpy(st->title, cart.title, sizeof st->title);
    if (load_assets(&cart) != 0 || !(L = new_cart_state(&cart))) {
        free_assets();
        kprintf("\x1b[91mbm: out of memory\x1b[0m\n");
        loading_stop();
        return BM_ENDED;
    }
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
    rt.start_us = timer_ticks();
    rt.hook_count = 0;
    rt.tokens = lua_tokens(cart.lua, cart.lua_size);
    perf.at = 0;                        /* the overlay: this cartridge's frames only */
    chunk_reader_t rd = { cart.lua, cart.lua_size };
    if (lua_load(L, read_chunk, &rd, "=main.lua", NULL) != LUA_OK ||
        (lua_pushcfunction(L, traceback), lua_insert(L, -2), lua_pcall(L, 0, 0, -2)) != LUA_OK ||
        call(L, "_init") != 0)
        error = lua_tostring(L, -1);
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
    memset(st, 0, sizeof *st);
    memcpy(st->title, susp.title, sizeof st->title);
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

void bm_print_stats(const bm_stats_t *st)
{
    if (!st->frames)
        return;
    uint32_t ms = st->elapsed_us / 1000, fps10 = ms ? st->frames * 10000u / ms : 0;
    uint32_t avg = st->cpu_us_total / st->frames;
    kprintf("bm: \"%s\" %lu frames, %lu.%lu fps, %lu dropped\n",
            st->title, st->frames, fps10 / 10, fps10 % 10, st->dropped);
    uint32_t copy = st->copy_us_total / st->frames;
    kprintf("     update+draw avg %lu.%02lu ms (%lu%% of frame), max %lu.%02lu ms, Lua %lu KiB\n",
            avg / 1000, avg % 1000 / 10, avg * 100 / FRAME_US,
            st->cpu_us_max / 1000, st->cpu_us_max % 1000 / 10, st->lua_kb);
    if (copy)
        kprintf("     copy to screen %lu.%02lu ms per frame\n", copy / 1000, copy % 1000 / 10);
    kprintf("     dev kit: Lua peak %lu KiB, data %lu KiB, busiest frame %luk instructions, "
            "%lu frames over 16.7 ms, %lu tokens\n",
            st->lua_peak_kb, st->assets_kb, st->instr_k_max, st->slow, st->tokens);
    gpu3d_stats_t g;
    gpu3d_take_stats(&g);
    if (g.jobs) {
        uint32_t per = (g.bin_us + g.render_us) / st->frames;
        kprintf("     GPU 3D %lu jobs, %lu triangles a frame, %lu.%02lu ms a frame (bin %lu%%), max job %lu us\n",
                g.jobs, g.tris / st->frames, per / 1000, per % 1000 / 10,
                g.bin_us + g.render_us ? g.bin_us * 100 / (g.bin_us + g.render_us) : 0, g.max_us);
    }
}

/* ---------------------------------------------------------------- C bench */

uint32_t bm_bench(framebuffer_t *fb, uint32_t frames)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    bm_close_suspended();
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

    uint32_t total = 0, deadline = timer_ticks() + FRAME_US, prev = 0;
    for (uint32_t f = 0; f < frames; f++) {
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
    return frames ? total / frames : 0;
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
    via_ram = 0;
    uint32_t direct = bm_bench(fb, frames);
    via_ram = 1;
    uint32_t ram = bm_bench(fb, frames);
    via_ram = saved;
    kprintf("bm bench (map + 256 sprites):");
    ms2(" direct", direct);
    ms2(", via RAM", ram);
    kprintf("\n");
}
