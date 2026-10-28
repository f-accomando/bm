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
#include "loading.h"
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
#define VIDEO_MT "bm.video"

#define SHOT_DIR "/bm/shots"

/* the saves of a cartridge (api_save.c): the folder, the size of one, the slots */
#define SAVE_DIR   "/bm/save"
#define SAVE_MAX   (32 * 1024)
#define SAVE_SLOTS BM_SAVE_SLOTS

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
    uint32_t light_us;          /* this frame's: dark_begin, glow and dark_end, inside draw_us */
    uint32_t fade_us;           /* of light_us: the fade of dark_end alone (QPU or ARM) */
    uint32_t op_us[8];          /* this frame's _draw: the time in each kind of 2D call (OP_MAP ...), and */
    uint32_t op_n[8];           /* how many calls; they include the time of a call that is recorded or goes to the GPU */
    char devinfo[4][19];        /* devinfo(): the game's lines in the session report */
    int ndevinfo;
    uint32_t hook_count;
    uint32_t frame_instr_k, last_instr_k;   /* Lua instructions (thousands): this frame, the last */
    /* the dev kit: the tokens of the code, and the most of this run */
    int tokens;                         /* -1: not counted yet (rt_tokens() counts at the first read: the count is 28 ms on a big script) */
    const char *tokens_src;
    size_t tokens_len;
    uint32_t instr_k_max;       /* Lua instructions (thousands) of the busiest frame */
    size_t lua_peak;            /* bytes of Lua memory, seen at the end of a frame */
    uint32_t slow_frames;       /* frames over 16.7 ms (_update + _draw) */
    int perf_key;               /* 'p' on the serial line: the dev kit's overlay on or off */
    int f11_held;
    int pad_chord_held;         /* L1 + R1 on a pad were down last frame (the small dev kit) */
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
    int screen_hook;            /* the size changed: call _screen(w, h) at the start of the next frame */
    int design_w, design_h;     /* the size the cartridge was made for (its header): safe_area(), aspect() */
    int gpu3d_boxed;            /* the GPU not started: the page was a square inside the screen */
    int cls_pending;            /* a cls() left to the GPU's next job (cls_settle) */
    uint16_t cls_colour;
    int gpu2d;                  /* bm3d 4.8: the 2D over the 3D drawn by the GPU in its job (gpu3d_2d=1) */
    int gpu2d_only;             /* bm3d 7.16: a game with no 3D has its 2D drawn by the GPU too (gpu2d_only=1, off by default) */
    int gpu2d_only_checked;     /* the key read for this run */
    int light_checked, light_key;   /* gpu3d_light (bm3d 7.17): the key read, and on */
    int light_defer;            /* this frame's lamps are recorded, lit by the GPU at dark_end */
    int light_amb;              /* the ambient level of dark_begin */
    int light_n;                /* the lamps recorded */
    struct { int x, y, r, lv, dither; } lamp[64];   /* glow() as it was asked: x, y in the page */
    uint32_t gpu_quads_frame;   /* the 2D quads the GPU took in the last frame (the overlay's "2D" row) */
    int gwin_on;                /* gpu2d_sheet(): the window of the sheet the GPU's 2D draws from (a copy, gwin) */
    int gw_x, gw_y, gw_w, gw_h; /* the window in the sheet, on the 8-pixel cells */
    g16_sheet_t gwin;
    uint32_t gw_ver;            /* the sheet's version the copy was made from */
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
            if ((i & 127) == 127)
                loading_tick();                 /* a big sheet: the loading screen goes on */
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

/* ------------------------------------------------------- meshes of Lua */

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
void later_paint(void);
void later_flush(void);
int poll_keys(void);
uint16_t hid_to_btn(uint32_t pad);
void sys_box(const char *const *lines, int n, int dim_last, int hints, int progress);
extern int tool_mode;           /* a bm tool runs (defined in runtime.c) */
extern const char *box_yes, *box_no;    /* the words of sys_box's two buttons (defined in runtime.c) */
extern framebuffer_t *perm_fb;  /* the screen of the run (defined in runtime.c) */

/* the debugger's state (R13; defined in runtime.c, its code is in api_debug.c) */
#define DBG_LINES   65536               /* breakpoints on lines 1..65535 of main.lua */
#define DBG_FRAMES  12
enum { DBG_RUN, DBG_INTO, DBG_OVER, DBG_OUT };

typedef struct {
    int session;                        /* a game tried from a tool */
    int n;                              /* breakpoints */
    int mode, depth;                    /* the step asked, and the stack's depth then */
    int stop;                           /* stopped in the debugger: the run ends */
    const char *cb;                     /* the callback running (_update...) */
    uint8_t bits[DBG_LINES / 8];
} dbg_t;

extern dbg_t dbg;
extern const char *cart_src;            /* main.lua of the run */
void dbg_begin(lua_State *L, const char *src, size_t len);
void hook(lua_State *L, lua_Debug *ar);    /* the count hook of the run (defined in runtime.c) */

/* the project open in the editor, and cart_run's options (state in runtime.c, API in api_editor.c) */
extern uint8_t *proj_cover;
extern uint8_t *proj_audio, *own_audio;
extern uint32_t proj_audio_len, own_audio_len;
extern uint8_t *proj_sheet8;
extern uint32_t proj_sheet8_len;
extern int n_made;
typedef struct {
    int set, stop, n;
    uint16_t lines[64];
} dbg_next_t;                           /* cart_run's options, for the run it asks (the debugger) */
extern dbg_next_t dbg_next;
extern char last_error[512];            /* the error the last run stopped with (bm_last_error) */
int load_assets(const bm_cart_t *c);
void free_assets(void);
int rt_tokens(void);
const luaL_Reg *rt_api(void);           /* api[] of runtime.c */
void set_copy(uint8_t **dst, uint32_t *dlen, const void *src, uint32_t len);
void extras_free(void);
int reserved_tool_name(const char *name);
const char *rt_res_name(int w);         /* res_name() of runtime.c */
extern int proj_sheet_drawn;    /* the sheet was drawn on since cart_load: cart_save writes it whole (defined in runtime.c) */

float fnum(lua_State *L, int i, float def);
void options_line(char *b, size_t n);

/* the 3D helpers (defined with the 3D) */
r3d_t *r3d(lua_State *L);
void settle2d(void);
void flush3d(int keep);
void cls_settle(void);
void gpu3d_to_arm(void *ctx, const char *why);
int merge_cfg(void);
int impostor_px(int px);
void zclear_dma_wait(int claim);
r3d_mesh_t *new_mesh(lua_State *L);
void skel_free(skel_t *s);

/* collision worlds, log() and report() (api_world.c) */
int l_world_gc(lua_State *L);
int l_world3d(lua_State *L);
int l_world_box(lua_State *L);
int l_world_ray(lua_State *L);
int l_world_move(lua_State *L);
int l_world_floor(lua_State *L);
int l_log(lua_State *L);
int l_report(lua_State *L);

/* animation and the 3D bindings (api_anim.c) */
int l_animate(lua_State *L);
int l_clips(lua_State *L);
int l_bone3d(lua_State *L);
int l_bone_turn(lua_State *L);
int l_bones3d(lua_State *L);
int l_hit3d(lua_State *L);
int l_bounds3d(lua_State *L);
int l_mesh_sphere(lua_State *L);
int l_mesh_cube(lua_State *L);
int l_draw3d(lua_State *L);
int l_cel3d(lua_State *L);
int l_dither3d(lua_State *L);
int l_sky3d(lua_State *L);
int l_shine3d(lua_State *L);
int l_shadow3d(lua_State *L);
int l_textures3d(lua_State *L);
int l_point3d(lua_State *L);
int l_line3d(lua_State *L);
int l_sprite3d(lua_State *L);
int l_particles(lua_State *L);
int l_particles_add(lua_State *L);
int l_particles_step(lua_State *L);
int l_particles_draw(lua_State *L);
int l_particles_count(lua_State *L);
int l_camera3d(lua_State *L);
int l_fog3d(lua_State *L);
int l_pvs3d(lua_State *L);
int l_visible3d(lua_State *L);
int l_project3d(lua_State *L);
int l_light3d(lua_State *L);
int l_lamp3d(lua_State *L);
int l_zclear(lua_State *L);
int l_gpu3d(lua_State *L);
int l_gpu3d_opt(lua_State *L);
int l_gpu3d_stats(lua_State *L);

/* the network for the games (api_net.c) */
int l_udp_open(lua_State *L);
int l_udp_send(lua_State *L);
int l_udp_recv(lua_State *L);
int l_udp_close(lua_State *L);
int l_net_ip(lua_State *L);
int l_net_resolve(lua_State *L);

extern char perm_title[49];      /* the game asking (defined in runtime.c) */

/* the state of the screenshots (defined in runtime.c, used by api_shot.c) */
extern int shot_req;                    /* a frame to save: the one drawn next */
extern char shot_file[13];              /* its name, "pong0007.png" */
extern int shot_note;                   /* frames left for the box that says it was saved */
extern char shot_line[2][40];
extern int shot_net;                    /* asked over the network (bm_shot_ask): plan it at the next key poll */
extern int shot_res;                    /* its answer: 2 waiting for the frame, 1 saved, -1 not saved, 0 none asked */
extern char shot_net_file[13];          /* the name of the file it saved */

/* saves (api_save.c) */
int save_slot(lua_State *L, int idx);
void slot_name(int slot, char out[16]);
int slot_find(int slot, fat_entry_t *e);
int l_save(lua_State *L);

/* screenshots (api_shot.c) */
int shot_plan(const char *prefix);
int l_shot(lua_State *L);
int l_cart_config(lua_State *L);
int l_saved(lua_State *L);
int l_saves(lua_State *L);
int l_delsave(lua_State *L);

/* documents (api_docs.c) */
int l_doc_list(lua_State *L);
int l_doc_read(lua_State *L);
int l_doc_write(lua_State *L);
int l_doc_delete(lua_State *L);

/* sound (api_sound.c) */
int l_hz(lua_State *L);
int l_note(lua_State *L);
int l_noteoff(lua_State *L);
int l_freq(lua_State *L);
int l_envelope(lua_State *L);
int l_duty(lua_State *L);
int l_playing(lua_State *L);
int l_apu(lua_State *L);
int l_slide(lua_State *L);
int l_vibrato(lua_State *L);
int l_arp(lua_State *L);
int l_sfx(lua_State *L);
int l_sfxpos(lua_State *L);
int l_music(lua_State *L);
int l_tempo(lua_State *L);
int l_mute(lua_State *L);
int l_volume(lua_State *L);
int l_audio_bank(lua_State *L);
int l_audio_samples(lua_State *L);
int l_audio_depth(lua_State *L);
int l_audio_pattern(lua_State *L);
int l_audio_play(lua_State *L);
int l_tone(lua_State *L);
int l_play(lua_State *L);
int l_audio_time(lua_State *L);
int l_play_at(lua_State *L);
int l_play_cancel(lua_State *L);
int l_play_voices(lua_State *L);
int l_reverb(lua_State *L);
int l_echo(lua_State *L);
int l_chorus(lua_State *L);
int l_retro(lua_State *L);

/* light (api_light.c) */
void sync3d(void);              /* the waiting 3D drawn first (defined in runtime.c) */
int l_light_begin(lua_State *L);
int l_light(lua_State *L);
int l_light_end(lua_State *L);
int l_fades(lua_State *L);
int l_dark_begin(lua_State *L);
int l_online(lua_State *L);
int l_glow(lua_State *L);
int l_dark_end(lua_State *L);
int l_quit(lua_State *L);

/* map, cell flags and zones (api_map.c) */
void settle_reads(void);      /* defined in runtime.c: 2D reads the page */
int gpu2d(int op, const int32_t *v);
int gpu2d_open(void);
int light_gpu_wanted(void);       /* gpu3d_light=1 and the GPU there: dark_begin records the lamps */
int light_gpu_run(void);          /* dark_end: the GPU lights the page; 0 if it did not (the ARM does) */
int l_gpu2d_sheet(lua_State *L);
void gpu2d_submit(void);          /* starts the frame job on the GPU now (the ARM goes on: dark_begin) */
int draw2d(int op, uint32_t words, int32_t **a);
void stack_draw(const int32_t v[7], const float f[3], int vert);
int sheet_cells(void);
enum { OP_MAP, OP_SPR, OP_SSPR, OP_STACK, OP_TEXT, OP_SHAPE, OP_N };    /* the kinds of 2D call the session report times */
int l_map(lua_State *L);
int l_mget(lua_State *L);
int l_mset(lua_State *L);
int l_fget(lua_State *L);
int l_fset(lua_State *L);
int l_mflags(lua_State *L);
int l_msize(lua_State *L);
int l_mlayers(lua_State *L);
int l_zone(lua_State *L);
size_t name_len(const char *s, size_t max);
int l_zones(lua_State *L);
int l_zboxes(lua_State *L);
int l_zspr(lua_State *L);
int l_zstack(lua_State *L);

/* prompts, buttons, controls and stat (api_prompts.c) */
#define BUTTON_OK   16                  /* "ok" and "back": the system's yes and back; the index after the last of button_names */
#define BUTTON_BACK (BUTTON_OK + 1)
const prompt_t *find_prompt(const char *n, int small);
int l_prompt(lua_State *L);
int l_lastinput(lua_State *L);
int l_camera(lua_State *L);
int l_clip(lua_State *L);
int l_rgb(lua_State *L);
int button_real(int i);
void keymap_apply_controls(void);
int l_controls(lua_State *L);
int l_keymap(lua_State *L);
int l_controller(lua_State *L);
const prompt_t *button_chip(int b, int dev, int small);
int l_btn(lua_State *L);
int l_btnp(lua_State *L);
int l_players(lua_State *L);
int l_stick(lua_State *L);
int l_time(lua_State *L);
int l_stat(lua_State *L);
int l_frameskip(lua_State *L);
int l_tri(lua_State *L);

/* keys (api_keys.c) */
int l_keyp(lua_State *L);
int l_keyhelp(lua_State *L);
int l_showkeys(lua_State *L);
int l_keyheld(lua_State *L);
int l_rawkeys(lua_State *L);
int l_keydown(lua_State *L);
int l_keys(lua_State *L);
int l_mouse(lua_State *L);
int l_mousep(lua_State *L);
int l_pad(lua_State *L);
int l_timeslice(lua_State *L);

/* the web, images and files for a browser cart (api_web.c) */
int l_web_get(lua_State *L);
int l_web_poll(lua_State *L);
int l_web_info(lua_State *L);
int l_web_body(lua_State *L);
int l_web_cancel(lua_State *L);
int l_image_load(lua_State *L);
int l_sheet_png(lua_State *L);
int l_sheet_gif(lua_State *L);
int l_image_file(lua_State *L);
int l_file_write(lua_State *L);
int l_file_state(lua_State *L);

/* video clips (api_video.c) */
int l_video_open(lua_State *L);
int l_clip_gc(lua_State *L);
int l_clip_info(lua_State *L);
int l_clip_frame_at(lua_State *L);
int l_clip_draw(lua_State *L);
int l_clip_close(lua_State *L);

/* the sheet's pixels and the text (api_text.c) */
int l_sget(lua_State *L);
int l_sset(lua_State *L);
int l_print(lua_State *L);
int l_font(lua_State *L);

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
int l_cart_publish(lua_State *L);
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
