/*
 * rt_internal.h: what the files of the runtime share.
 *
 * Private to src/bm: the state of the run (rt, d2, held), the macros, the
 * small helpers of the Lua API and the functions that one area of
 * runtime.c calls in another. The areas move to api_<area>.c one by one
 * (docs/NEXT.md, item 0b step 5); each file includes this header. The
 * table api[] and the lists res_name, screen_modes and square_modes stay in
 * runtime.c: scripts/consistency.py, tests/tools/test_mkapi.py and
 * test_meshcap read that text.
 */
#ifndef BM_RT_INTERNAL_H
#define BM_RT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "map3d.h"
#include "platform.h"
#include "drivers/fb.h"
#include "gfx/font.h"
#include "gpu/gpu3d.h"

#include "lauxlib.h"
#include "lua.h"

#define FRAME_US        16667
#define HOLD_FRAMES     10          /* a serial key press counts as held this long */
#define QUIT_FORCE      0x100       /* 'q' on the serial line: leaves without asking _exit() */
#define HOOK_EVERY      1000        /* instructions between hook calls */
#define FRAME_BUDGET    20000       /* x HOOK_EVERY = 20 M instructions per callback */

enum { BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B, BTN_X, BTN_Y, BTN_START, BTN_SELECT,
       BTN_COUNT };
/* the shoulder buttons from the serial keys: pad() only, not btn() */
enum { SER_L1 = BTN_COUNT, SER_R1, SER_COUNT };

#define MESH_MT "bm.mesh"
#define WORLD_MT "bm.world"

/* what a cartridge asks the player for (perm_allowed) */
enum { PERM_NET, PERM_REPORT, PERM_DOCS, PERM_COUNT };

/* ------------------------------------------------------- the state of the run */

typedef struct {
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
    struct pvs *pvs;                /* bm3d 5.5: the map's visibility given by pvs3d(), or NULL */
    uint8_t *cell_dirty;
    int sheet_dirty;
    uint8_t hold[SER_COUNT];
    int uses_xy;                /* the cart asked for btn(6) or btn(7) */
    uint16_t now, prev;             /* button bits this frame / last frame, any player */
    uint16_t pnow[INPUT_PLAYERS], pprev[INPUT_PLAYERS];     /* the same per player */
    uint32_t praw[INPUT_PLAYERS];   /* HID bits per player (stick() from the cross) */
    uint32_t praw_prev[INPUT_PLAYERS];  /* ...the frame before (btnp() of a name) */
    /* keymap(): the game's actions and their buttons (indices of button_names) */
    struct { char name[16]; uint8_t btn[4], n, gbtn[4], gn; } keymap[32];  /* btn: in effect; gbtn: the game's own */
    int nkeymap;
    char scheme[12];                /* controls(): "scroller" (default when empty), "topdown", "stick" */
    int local;                      /* player of the keyboard / USB / serial, or -1 */
    uint32_t start_us, frame;
    uint32_t last_cpu_us, fps;
    uint32_t gpu_frame_us;      /* stat(16): the GPU's job time accounted in the last frame */
    gpu3d_stats_t gpu_prev;     /* the driver's totals at the end of the frame before */
    uint32_t audio_frame_us;    /* stat(17): the audio interrupt's time in the last frame */
    uint32_t gpuwait_frame_us;  /* stat(18): the ARM's wait for the GPU in the last frame */
    uint32_t audio_prev, gpuwait_prev;  /* their totals at the end of the frame before */
    uint32_t present_us;        /* last copy of the frame to the framebuffer */
    uint32_t us3d;              /* time in 3D drawing since the last zclear() */
    uint32_t us3d_all;          /* the same, never reset (the dev kit's overlay) */
    uint32_t frame_t0;          /* when this frame's _update began */
    int skip_max;               /* frameskip(n): up to n _update before a _draw (0, 1: one) */
    uint32_t updates;           /* the _update that ran before this frame's _draw (stat(15)) */
    uint32_t update_us, draw_us;    /* this frame's: all its _update, its _draw (the dev kit) */
    char devinfo[4][19];        /* devinfo(): the game's lines in the session report */
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
    int prof_want;              /* profile(true): the profiler of functions on without its page */
    int keyhelp;                /* keyhelp(): the registry's reference of the cartridge's keys, or 0 */
    int help_page;              /* the page of the keys shown while F12 is held */
    int help_open;              /* showkeys(): 1 the keys stay open (a click, F12 or Esc closes them), 2 closed, the click still down */
    int esc;
    int serial_quit;            /* Ctrl+\ on the serial line: Ctrl+Esc (the cartridge's _exit() asks) */
    int quit;
    r3d_t r3d;
    int r3d_ready;
    int zclear_seen;            /* zclear() called in this frame */
    int early;                  /* in the next frame's _update, run while the GPU draws (bm3d 4.1) */
    int early_touch;            /* ... which drew 3D or needed the page: */
    int no_early;               /* its _update runs after the frame again */
    int hold_frame;             /* this frame is not shown (the 3D went from the GPU to the ARM
                                 * in the middle of it: its depths would be mixed) */
    int zclear_dma;             /* the z-buffer is being (or has been) cleared by the DMA
                                 * for the next frame, and nothing has drawn on it since */
    g16_light_t light;          /* light_begin() .. light_end() */
    g16_fade_t fade;            /* fades(), dark_begin() .. dark_end() */
    r3d_pfx_t *pfx;             /* particles() pool */
    int text_mode;              /* keyp() was called: the keyboard types */
    int raw_keys;               /* rawkeys(true): the keyboard is read with keydown() */
    lua_State *slice_thread;    /* timeslice(): this coroutine yields after slice_at */
    uint32_t slice_at, slice_len;
    int esc_wait;               /* frames since a serial Esc */
    int esc_num;                /* ESC [ n ~ */
    uint8_t tq[256];            /* typed keys for keyp() */
    uint8_t tq_head, tq_tail;
    char save_name[13];         /* "1a2b3c4d.sav": CRC-32 of title and author */
    uint8_t *mesh;              /* copy of the cartridge's MESH section, or NULL */
    uint32_t mesh_size;
    uint8_t *anim;              /* copy of its ANIM section (skeletons), or NULL */
    uint32_t anim_size;
    int mouse, mouse_arrow;     /* mouse(true [, arrow]): the pointer is the cartridge's */
    int reports;                /* report() calls in this run */
    pointer_t ptr;              /* the pointer this frame */
    int want_w, want_h;         /* screen(w, h): the resolution from the next frame */
    int gpu3d_boxed;            /* the GPU not started: the page was a square inside the screen */
    int cls_pending;            /* a cls() left to the GPU's next job (cls_settle) */
    uint16_t cls_colour;
    int gpu2d;                  /* bm3d 4.8: the 2D over the 3D drawn by the GPU in its job (gpu3d_2d=1) */
    int later;                  /* bm3d 6.9: each frame shown a frame later asked for (gpu3d_later=1,
                                 * gpu3d()): later_on() says when it is */
    int online;                 /* online(true): played over the network, PS asks first */
    char online_note[64];       /* online()'s line under the question */
    int leave_ask;              /* "Leave the match?" is open (leave_step) */
    int leave_no;               /* Esc: its answer no */
    int online_left;            /* yes: the game ends (not suspended) */
    uint32_t leave_pad;         /* its buttons and keys last frame (for the presses) */
    uint8_t leave_keys;
    uint32_t leave_held;        /* the buttons it took: the game gets them back released */
    uint32_t raw_all;           /* this frame's HID bits, all players and the serial line */
} rt_t;

extern rt_t rt;

/* ------------------------------------------------------- helpers */

static inline uint16_t col(lua_State *L, int idx, uint32_t def)
{
    return g16_rgb24((uint32_t)luaL_optinteger(L, idx, def));
}

static inline int ival(lua_State *L, int idx)
{
    return (int)luaL_checknumber(L, idx);
}

static inline int oval(lua_State *L, int idx, int def)
{
    return lua_isnoneornil(L, idx) ? def : (int)luaL_checknumber(L, idx);
}

static inline void sheet_commit(void)
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

/* ------------------------------------------------------- 2D after the GPU */

/* bm3d 4.1: 2D drawn on the page while the GPU draws 3D on it (the frame
 * queue on): the GPU's job is started and the 2D is recorded here, then
 * drawn on the page in the same order once the job has ended (flush3d):
 * before more 3D, before the page is read, at the end of the frame. The
 * HUD of a game is drawn (as far as the cartridge goes) while the GPU
 * draws the scene, and the frame's 3D need not wait for it. */

enum { D2_STATE, D2_CLS, D2_PSET, D2_LINE, D2_RECT, D2_RECTFILL, D2_CIRC, D2_CIRCFILL, D2_SPR, D2_SSPR,
       D2_SSPR_ZOOM, D2_MAP, D2_TEXT, D2_PROMPT, D2_TRI, D2_TRI_GOURAUD, D2_STACK, D2_STACKV };

#define D2_MAX (256u << 10)             /* bytes recorded at most: then drawn */

#define D2_NONE 0xFFFFFFFFu


typedef struct {
    int on;                             /* recording: the GPU has a job on the page */
    int32_t *w;                         /* the records: op | words << 8, then the words */
    uint32_t n, cap;                    /* words */
    uint32_t cut;                       /* the records from here are the next frame's (its
                                         * _update ran early), or D2_NONE */
    int cx0, cy0, cx1, cy1, cam_x, cam_y;   /* the drawing state of the records so far */
    const font_t *font;
    uint32_t ops;                       /* drawn after the GPU (for the log and the bench) */
} d2_t;

extern d2_t d2;

/* bm3d 6.9: the frame shown a frame later (gpu3d_later=1, with two
 * jobs in flight). At the end of a frame its 3D is started on the GPU and
 * the frame is held: its page, and the 2D recorded after its job started
 * (d2's records, moved here). The next _update and _draw run meanwhile on
 * the next page (three pages: one shown, one held, one drawn); at the end
 * of that frame the held one gets its 2D and is shown (later_show). The
 * cartridge sees nothing else: the same frames, each one a frame later. */
typedef struct {
    int on;                             /* a frame is held */
    int now;                            /* the frames are held (the last frame was: the overlay) */
    uint16_t *px;                       /* its page */
    int32_t *w;                         /* its 2D, recorded while its job draws (as d2's) */
    uint32_t n, cap;                    /* words */
    framebuffer_t *fb;                  /* the run's screen */
    uint32_t frames;                    /* frames shown a frame later in this run */
} held_t;

extern held_t held;

/* ------------------------------------------------------- calls between areas */

/* Defined in one area of runtime.c, used in another. */
void draw_prompt(const prompt_t *p, int x, int y, int scale);
int  l_sspr(lua_State *L);
const prompt_t *prompt_for(const char *n, int small, int player);
uint32_t assets_kb(void);
int perm_allowed(int what);
void ser(lua_State *L, int idx, int depth);
int video_to_ram(g16_t *g);
void dbg_line(lua_State *L, lua_Debug *ar);
int copy_question(const char *game, const char *copy);
void perf_end(void);
void perf_frame(void);
void battery_draw(void);
void leave_draw(void);
void notice_draw(void);
void shot_note_draw(void);
void keys_help(lua_State *L);
int session_wanted(void);

float fnum(lua_State *L, int i, float def);
void options_line(char *b, size_t n);

/* collision worlds, log() and report() (api_world.c) */
int l_world_gc(lua_State *L);
int l_world3d(lua_State *L);
int l_world_box(lua_State *L);
int l_world_ray(lua_State *L);
int l_world_move(lua_State *L);
int l_world_floor(lua_State *L);
int l_log(lua_State *L);
int l_report(lua_State *L);

/* the dev kit's overlay (defined with it) */
int l_devkit(lua_State *L);
int l_profile(lua_State *L);
int l_breakpoint(lua_State *L);
int l_devinfo(lua_State *L);
/* the console's battery (defined with the system's notice) */
int l_battery(lua_State *L);
int l_battery_low(lua_State *L);

/* cartridge files, for the editor (defined after the asset loader) */
int l_ls(lua_State *L);
int l_cart_load(lua_State *L);
int l_cart_new(lua_State *L);
int l_cart_save(lua_State *L);
int l_cart_build(lua_State *L);
int l_cart_run(lua_State *L);
int l_cart_tool(lua_State *L);
int l_cart_arg(lua_State *L);
int l_cart_audio(lua_State *L);
int l_cart_put_audio(lua_State *L);
int l_cart_data(lua_State *L);
int l_cart_read(lua_State *L);
int l_cart_write(lua_State *L);
int l_cart_meshes(lua_State *L);
int l_mesh_reduce(lua_State *L);
int l_picture3d(lua_State *L);
int l_cutout3d(lua_State *L);
int l_glb3d(lua_State *L);
int l_res_write(lua_State *L);
int l_res_read(lua_State *L);
int l_cart_sheet(lua_State *L);

#endif
