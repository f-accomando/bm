/*
 * The preview of the Lib tab (docs/RISORSE.md): the model turning, the
 * picture of a zone (its frames playing) or of a sheet, the map, the
 * colours of a palette, the steps of a sound effect, the positions of a
 * song, the envelope of an instrument; Y plays the sounds. What an item
 * needs (its file, the sheet as pixels, the mesh, the bank) is made when
 * the item changes and kept while it stays.
 */
#include "lib.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "bm/gfx16.h"
#include "bm/r3d.h"
#include "bm/runtime.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define C_BOX   0x101016                /* the box (menu_ui.c's C_PILL) */
#define C_TXT   0xF0F0F4
#define C_DIMT  0x9A9AA8
#define C_CHK1  0x2A2A33                /* the checkerboard of transparency */
#define C_CHK2  0x3A3A44
#define C_BAR1  0x00C8F0
#define C_BAR2  0xFFB040

static const lib_item_t *cur;           /* the item the things below belong to */
static int sheet_src = -1;              /* the source of the decoded sheet */
static g16_sheet_t sheet;
static r3d_mesh_t mesh;
static int have_mesh;
static float mesh_cy, mesh_r, angle;
static g16_t sub;                       /* the box, for the 3D */
static r3d_t r3;
static int r3_w, r3_h;
static uint16_t *map_px;                /* the map drawn small */
static uint8_t *map_on;
static int map_w, map_h;
static au_bank_t *bank;                 /* the bank of the item's file */
static int playing;                     /* 1 sound effect, 2 song, 3 instrument */
static int play_voice;                  /* the voice of the sound effect */
static const lib_item_t *play_item;     /* what plays (the preview may come after Y) */
static uint32_t play_t0;
static uint32_t t0;

static uint16_t c16(uint32_t rgb) { return g16_rgb24(rgb); }

static void set_px(void *ctx, int x, int y, const uint8_t rgba[4])
{
    g16_sheet_set(ctx, x, y, g16_rgb(rgba[0], rgba[1], rgba[2]), rgba[3] >= 128);
}

/* the sheet of a file as pixels (kept for the next item of the same file) */
static int load_sheet(int src, const bm_cart_t *c)
{
    if (src == sheet_src && sheet.px)
        return 0;
    g16_sheet_free(&sheet);
    sheet_src = -1;
    if (!c->sheet_w || g16_sheet_alloc(&sheet, c->sheet_w, c->sheet_h) != 0)
        return -1;
    if (c->sheet8) {
        bm_sheet8_unpack(c, set_px, &sheet);
    } else {
        for (int y = 0; y < c->sheet_h; y++)
            for (int x = 0; x < c->sheet_w; x++)
                set_px(&sheet, x, y, c->sheet_rgba + ((size_t)y * c->sheet_w + x) * 4);
    }
    for (int cy = 0; cy < sheet.h / G16_CELL; cy++)
        for (int cx = 0; cx < sheet.w / G16_CELL; cx++)
            g16_sheet_update_cell(&sheet, cx, cy);
    sheet_src = src;
    return 0;
}

static void uv_inset(float *uv, float inset)       /* as runtime.c does for model() */
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

static int load_mesh(const bm_cart_t *c, int i)
{
    bm_model_t md;
    if (bm_mesh_model(c->mesh, c->mesh_size, i, &md) != 0 || r3d_mesh_alloc(&mesh, md.nverts, md.nfaces) != 0)
        return -1;
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < md.nverts; k++) {
        float xyz[3];
        bm_model_vertex(&md, k, xyz);
        mesh.verts[k] = (v3_t){ xyz[0], xyz[1], xyz[2] };
        if (xyz[1] < lo) lo = xyz[1];
        if (xyz[1] > hi) hi = xyz[1];
    }
    mesh_cy = (lo + hi) * 0.5f;
    mesh_r = 0.01f;
    for (int k = 0; k < md.nverts; k++) {
        v3_t p = mesh.verts[k];
        float d = sqrtf(p.x * p.x + p.z * p.z + (p.y - mesh_cy) * (p.y - mesh_cy));
        if (d > mesh_r) mesh_r = d;
    }
    const float inset = bm_mesh_inset(c->mesh);
    for (int f = 0; f < md.nfaces; f++) {
        uint32_t colour;
        float uv[6];
        bm_model_face(&md, f, mesh.faces + f * 3, &colour, uv);
        if (colour & R3D_TEXTURED) {
            colour = R3D_TEXTURED;
            if (!mesh.uv) {
                if (r3d_mesh_alloc_uv(&mesh) != 0)
                    return -1;
                mesh.tex = &sheet;
            }
            uv_inset(uv, inset);
            memcpy(mesh.uv + f * 6, uv, sizeof uv);
        }
        mesh.colors[f] = colour;
    }
    r3d_mesh_normals(&mesh);
    return 0;
}

/* the map at the size of the box: each pixel from its tile, on the top
 * layer that has one there (R11: layer 1 at the back) */
static int load_map(const bm_cart_t *c, int bw, int bh)
{
    int mw = c->map_w * G16_CELL, mh = c->map_h * G16_CELL;
    float k = fminf((float)bw / mw, (float)bh / mh);
    if (k > 4) k = 4;
    map_w = (int)(mw * k);
    map_h = (int)(mh * k);
    if (map_w < 1) map_w = 1;
    if (map_h < 1) map_h = 1;
    map_px = malloc((size_t)map_w * map_h * 2);
    map_on = malloc((size_t)map_w * map_h);
    if (!map_px || !map_on)
        return -1;
    int per = sheet.w / G16_CELL, ncells = per * (sheet.h / G16_CELL);
    const uint8_t *layer[BM_LAYERS_MAX];
    char name[BM_LAYER_NAME + 1];
    int nl = 0;
    while (nl < c->nlayers && bm_layer(c, nl, name, &layer[nl]) == 0)
        nl++;
    for (int y = 0; y < map_h; y++) {
        int sy = (int)(y / k);
        for (int x = 0; x < map_w; x++) {
            int sx = (int)(x / k);
            int i = y * map_w + x;
            map_on[i] = 0;
            for (int l = nl - 1; l >= 0 && !map_on[i]; l--) {
                const uint8_t *p = layer[l] + 2 * ((size_t)(sy / G16_CELL) * c->map_w + sx / G16_CELL);
                int n = p[0] | p[1] << 8;
                if (n <= 0 || n >= ncells)
                    continue;
                uint32_t s = (uint32_t)(n / per * G16_CELL + sy % G16_CELL) * sheet.w + n % per * G16_CELL +
                             sx % G16_CELL;
                map_px[i] = sheet.px[s];
                map_on[i] = sheet.alpha[s];
            }
        }
    }
    return 0;
}

static void forget(const lib_item_t *next)
{
    if (play_item != next)
        lib_stop();
    if (have_mesh)
        r3d_mesh_free(&mesh);
    have_mesh = 0;
    free(map_px);
    free(map_on);
    map_px = NULL;
    map_on = NULL;
    free(bank);
    bank = NULL;
    cur = NULL;
}

/* what the item needs: -1 if it cannot be shown */
static int prepare(const lib_item_t *it, int bw, int bh)
{
    forget(it);
    cur = it;
    angle = 0;
    t0 = timer_ticks();
    const bm_cart_t *c = lib_open(it->source);
    if (!c)
        return -1;
    switch (it->what) {
    case LIB_W_MODEL:
        if (c->sheet_w)
            load_sheet(it->source, c);
        have_mesh = load_mesh(c, it->index) == 0;
        return have_mesh ? 0 : -1;
    case LIB_W_ZONE:
    case LIB_W_SHEET:
        return load_sheet(it->source, c);
    case LIB_W_MAP:
        if (load_sheet(it->source, c) != 0)
            return -1;
        return load_map(c, bw, bh);
    case LIB_W_SONG:
    case LIB_W_SFX:
    case LIB_W_SOUND: {
        char err[64];
        bank = malloc(sizeof *bank);
        if (!bank || !c->audio || au_parse(c->audio, c->audio_size, bank, err, sizeof err) != 0) {
            free(bank);
            bank = NULL;
            return -1;
        }
        return 0;
    }
    }
    return 0;
}

static void checker(g16_t *g, int x, int y, int w, int h, int cell)
{
    for (int j = 0; j < h; j += cell)
        for (int i = 0; i < w; i += cell)
            g16_rectfill(g, x + i, y + j, i + cell > w ? w - i : cell, j + cell > h ? h - j : cell,
                         c16(((i / cell + j / cell) & 1) ? C_CHK2 : C_CHK1));
}

/* a rectangle of the sheet, as big as fits (whole pixels when it is small) */
static void picture(g16_t *g, int x, int y, int bw, int bh, int sx, int sy, int sw, int sh)
{
    float k = fminf((float)(bw - 16) / sw, (float)(bh - 16) / sh);
    if (k >= 1) k = floorf(k > 12 ? 12 : k);
    int w = (int)(sw * k), h = (int)(sh * k);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int dx = x + (bw - w) / 2, dy = y + (bh - h) / 2;
    checker(g, dx, dy, w, h, k >= 4 ? (int)k * 2 : 8);
    g16_sspr_zoom(g, &sheet, sx, sy, sw, sh, dx, dy, 0, 0, k);
}

static void draw_model(g16_t *g, int x, int y, int bw, int bh)
{
    if (r3_w != bw || r3_h != bh) {
        if (r3_w)
            r3d_free(&r3);
        r3_w = r3_h = 0;
        g16_target(&sub, g->px + (uint32_t)y * g->stride + x, g->stride, bw, bh, g->font);
        if (r3d_init(&r3, &sub) != 0)
            return;
        r3_w = bw;
        r3_h = bh;
    }
    sub.px = g->px + (uint32_t)y * g->stride + x;        /* the page drawn now */
    r3d_zclear(&r3);
    const float pitch = -0.45f, fov = 50;
    r3d_camera(&r3, 0, 0, 0, 0, pitch, fov);
    float vhalf = atanf((bh * 0.5f) / r3.focal);
    float d = mesh_r / sinf(vhalf) * 1.08f;
    r3d_camera(&r3, 0, mesh_cy + d * sinf(-pitch), -d * cosf(pitch), 0, pitch, fov);
    r3d_light(&r3, 0.4f, 0.8f, -0.45f, 0.42f);
    r3d_draw(&r3, &mesh, (v3_t){ 0, 0, 0 }, 0, angle, 0, 1);
    angle += 0.025f;
}

static void draw_palette(g16_t *g, const bm_cart_t *c, int x, int y, int bw, int bh)
{
    if (!c->sheet8)
        return;
    int nc = c->sheet8[4] | c->sheet8[5] << 8, n = 0;
    uint32_t col[256];
    for (int i = 0; i < nc && i < 256; i++) {
        const uint8_t *e = c->sheet8 + 8 + i * 4;
        if (e[3] >= 128)
            col[n++] = (uint32_t)e[0] << 16 | e[1] << 8 | e[2];
    }
    if (!n)
        return;
    int s = 32;                                 /* the biggest squares that fit */
    while (s > 4) {
        int per = (bw - 8) / s, rows = (n + per - 1) / per;
        if (per > 0 && rows * s <= bh - 8)
            break;
        s--;
    }
    int per = (bw - 8) / s;
    int x0 = x + (bw - per * s) / 2, y0 = y + 4;
    for (int i = 0; i < n; i++)
        g16_rectfill(g, x0 + i % per * s + 1, y0 + i / per * s + 1, s - 2, s - 2, c16(col[i]));
}

/* the steps of a sound effect or a pattern track: a bar for each note */
static void draw_steps(g16_t *g, const au_step_t *st, int len, int x, int y, int bw, int bh, int now)
{
    int lo = 127, hi = 0;
    for (int i = 0; i < len; i++)
        if (st[i].note > 0 && st[i].note < AU_NOTE_OFF) {
            if (st[i].note < lo) lo = st[i].note;
            if (st[i].note > hi) hi = st[i].note;
        }
    if (lo > hi)
        return;
    if (hi - lo < 12) { lo -= 6; hi += 6; }
    int w = (bw - 16) / (len ? len : 1);
    if (w < 2) w = 2;
    if (w > 24) w = 24;
    x += (bw - 16 - w * len) / 2;
    for (int i = 0; i < len; i++) {
        int n = st[i].note;
        if (n <= 0 || n >= AU_NOTE_OFF)
            continue;
        int bar = 6 + (bh - 28) * (n - lo) / (hi - lo);
        g16_rectfill(g, x + 8 + i * w, y + bh - 8 - bar, w > 3 ? w - 1 : w, bar, c16(i == now ? C_BAR2 : C_BAR1));
    }
}

static void draw_sound(g16_t *g, const lib_item_t *it, int x, int y, int bw, int bh)
{
    if (!bank)
        return;
    if (it->what == LIB_W_SFX && it->index < bank->nsfx) {
        const au_sfx_t *s = &bank->sfx[it->index];
        int step = -1;
        if (playing != 1 || audio_sfx_pos(play_voice, &step) < 0)
            step = -1;
        draw_steps(g, s->step, s->len, x, y, bw, bh, step);
    } else if (it->what == LIB_W_SONG && it->index < bank->nsongs) {
        const au_song_t *s = &bank->song[it->index];
        int song = -1, order = -1, step = 0, pat = 0;
        if (playing == 2)
            audio_music_pos(&song, &order, &step, &pat);
        int w = (bw - 16) / (s->len ? s->len : 1);
        if (w > 40) w = 40;
        for (int i = 0; i < s->len; i++) {
            int p = s->order[i], on = song == it->index && order == i;
            int hgt = 24 + (p * 37) % (bh - 56);   /* a block per position, its pattern's height */
            g16_rectfill(g, x + 8 + i * w, y + bh - 8 - hgt, w > 3 ? w - 2 : w, hgt, c16(on ? C_BAR2 : C_BAR1));
        }
        /* the notes of the pattern playing (or of the first one), track by track */
        int pi = song == it->index && order >= 0 ? s->order[order] : s->len ? s->order[0] : -1;
        if (pi >= 0 && pi < bank->npatterns) {
            const au_pattern_t *pt = &bank->pat[pi];
            for (int tr = 0; tr < AU_TRACKS; tr++)
                for (int i = 0; i < pt->len; i++)
                    if (pt->step[tr][i].note > 0 && pt->step[tr][i].note < AU_NOTE_OFF)
                        g16_rectfill(g, x + 8 + i * (bw - 16) / pt->len, y + 6 + tr * 4, 3, 3,
                                     c16(song == it->index && i == step ? C_BAR2 : C_DIMT));
        }
    } else if (it->what == LIB_W_SOUND && it->index < bank->nsounds) {
        const au_sound_t *s = &bank->sound[it->index];
        /* the envelope: attack, decay to the sustain, hold, release */
        int a = s->attack, d = s->decay, r = s->release, sus = s->sustain;
        int total = a + d + r + 64;
        float k = (float)(bw - 24) / (total ? total : 1);
        int base = y + bh - 12, top = y + 12, sy = base - (base - top) * sus / 255;
        int x0 = x + 12, x1 = x0 + (int)(a * k), x2 = x1 + (int)(d * k), x3 = x2 + (int)(64 * k), x4 = x3 + (int)(r * k);
        uint16_t c = c16(playing == 3 ? C_BAR2 : C_BAR1);
        for (int i = 0; i < 3; i++) {
            g16_line(g, x0, base - i, x1, top - i, c);
            g16_line(g, x1, top - i, x2, sy - i, c);
            g16_line(g, x2, sy - i, x3, sy - i, c);
            g16_line(g, x3, sy - i, x4, base - i, c);
        }
    }
}

/* a kit: what it holds, a line each, on the text rows of the box */
static void draw_kit(g16_t *g, const bm_cart_t *c, int x, int y)
{
    char line[48];
    int row = (y + 15) / 16, col = (x + 15) / 8;
    if (c->models) {
        int anim = 0;
        for (int i = 0; i < c->models; i++) {
            bm_model_t m;
            bm_rig_t r;
            if (bm_mesh_model(c->mesh, c->mesh_size, i, &m) == 0 && c->anim &&
                bm_anim_rig(c->anim, c->anim_size, m.name, &r) == 0)
                anim++;
        }
        ksnprintf(line, sizeof line, "%u models, %d animated", c->models, anim);
        g16_text(g, col * 8, row++ * 16, line, c16(C_TXT));
    }
    if (c->sheet_w) {
        ksnprintf(line, sizeof line, "sheet %ux%u, %u zones", c->sheet_w, c->sheet_h, c->zones);
        g16_text(g, col * 8, row++ * 16, line, c16(C_TXT));
    }
    if (c->audio && c->audio_size >= 16) {
        ksnprintf(line, sizeof line, "%u sounds, %u effects, %u songs", c->audio[5], c->audio[6], c->audio[8]);
        g16_text(g, col * 8, row++ * 16, line, c16(C_TXT));
    }
    if (c->map_cells) {
        ksnprintf(line, sizeof line, "map %ux%u", c->map_w, c->map_h);
        g16_text(g, col * 8, row++ * 16, line, c16(C_TXT));
    }
}

void lib_preview(g16_t *g, int x, int y, int w, int h, void *ctx)
{
    const lib_item_t *it = ctx;
    if (!it)
        return;
    if (it != cur && prepare(it, w, h) != 0)
        return;
    const bm_cart_t *c = lib_open(it->source);
    if (!c)
        return;
    if (playing) {
        audio_idle();                   /* without HDMI audio the player moves anyway */
        int song, order, step, pat;
        if ((playing == 1 && audio_sfx_pos(play_voice, &step) < 0) ||
            (playing == 2 && !audio_music_pos(&song, &order, &step, &pat)) ||
            (playing == 3 && (timer_ticks() - play_t0) > 900000u))
            lib_stop();
    }
    switch (it->what) {
    case LIB_W_MODEL:
        if (have_mesh)
            draw_model(g, x, y, w, h);
        break;
    case LIB_W_ZONE: {
        bm_zone_t z;
        if (!sheet.px || bm_zone(c, it->index, &z) != 0)
            break;
        int k = 0;
        if (z.frames > 1 && z.fps)
            k = (int)((uint64_t)(timer_ticks() - t0) * z.fps / 1000000u) % z.frames;
        picture(g, x, y, w, h, z.x + k * z.w, z.y, z.w, z.h);
        break;
    }
    case LIB_W_SHEET:
        if (sheet.px)
            picture(g, x, y, w, h, 0, 0, c->sheet_w, c->sheet_h);
        break;
    case LIB_W_MAP:
        if (map_px) {
            int dx = x + (w - map_w) / 2, dy = y + (h - map_h) / 2;
            for (int j = 0; j < map_h; j++) {
                uint16_t *row = g->px + (uint32_t)(dy + j) * g->stride + dx;
                for (int i = 0; i < map_w; i++)
                    if (map_on[j * map_w + i])
                        row[i] = map_px[j * map_w + i];
            }
        }
        break;
    case LIB_W_PALETTE:
        draw_palette(g, c, x, y, w, h);
        break;
    case LIB_W_SONG:
    case LIB_W_SFX:
    case LIB_W_SOUND:
        draw_sound(g, it, x, y, w, h);
        break;
    case LIB_W_KIT:
        draw_kit(g, c, x, y);
        break;
    }
}

const char *lib_play_label(const lib_item_t *it)
{
    if (!it || (it->what != LIB_W_SONG && it->what != LIB_W_SFX && it->what != LIB_W_SOUND))
        return NULL;
    if (bm_suspended(NULL, 0))          /* the game's own bank waits in the player */
        return NULL;
    return playing && it == play_item ? "Stop" : "Play";
}

void lib_play(const lib_item_t *it)
{
    if (!lib_play_label(it))
        return;
    if (playing && it == play_item) {
        lib_stop();
        return;
    }
    const bm_cart_t *c = lib_open(it->source);
    char err[64];
    if (!c || !c->audio)
        return;
    lib_stop();
    if (audio_bank(c->audio, c->audio_size, err, sizeof err) != 0)
        return;
    play_t0 = timer_ticks();
    play_item = it;
    if (it->what == LIB_W_SFX && (play_voice = audio_sfx(it->index, -1, 0, 1.0f)) >= 0) {
        playing = 1;
    } else if (it->what == LIB_W_SONG) {
        audio_music(it->index, 0, 0);
        playing = 2;
    } else if (it->what == LIB_W_SOUND) {
        audio_play(0, it->index, 60, 255, 0, 600);
        playing = 3;
    }
    kprintf("lib: playing %s %s\n", lib_info_type(it), it->name);
}

void lib_stop(void)
{
    if (!playing)
        return;
    playing = 0;
    play_item = NULL;
    audio_music_stop(0);
    audio_sfx_stop(-1);
    audio_bank(NULL, 0, NULL, 0);
}

void lib_view_reset(void)
{
    forget(NULL);
    lib_stop();
    g16_sheet_free(&sheet);
    sheet_src = -1;
}
