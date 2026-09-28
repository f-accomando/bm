#include "menu3d.h"
#include "b33/b33.h"
#include "b33/r3d.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 360
#define VIEW_W 416                  /* 3D view: columns 0-51, rows 1-16 */
#define VIEW_H 256
#define VIEW_Y 16
#define LIST_X 416                  /* title list: columns 52-79 */
#define FRAME_US 16667

#define C_HEADER    0x101828
#define C_PANEL     0x0A0E1A
#define C_LIST      0x121A2E
#define C_SEL_BG    0xE0E6F0
#define C_SEL_FG    0x101828
#define C_TEXT      0xE8ECF4
#define C_DIM       0x8088A0
#define C_TITLE     0x55FFFF
#define C_YELLOW    0xFFFF55
#define C_BODY      0x2A2E38
#define C_COPPER    0xD8904A

static g16_t g2, g3;                /* whole screen (text), 3D view */
static r3d_t r3;
static r3d_mesh_t stick, floor_mesh;
static int ready, meshes;
static uint32_t con_w, con_h, t0, deadline;
static float pos;                   /* carousel position, eases to sel */

/* ---------------------------------------------------------------- mesh */

typedef struct {
    r3d_mesh_t *m;
    int nv, nf;
} build_t;

/* adds triangle abc turned so that its normal points along `want` */
static void tri(build_t *b, v3_t a, v3_t c1, v3_t c2, v3_t want, uint32_t color, const float *uv)
{
    v3_t u = { c1.x - a.x, c1.y - a.y, c1.z - a.z }, v = { c2.x - a.x, c2.y - a.y, c2.z - a.z };
    v3_t n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
    int flip = n.x * want.x + n.y * want.y + n.z * want.z < 0;
    int i = b->nv;
    b->m->verts[i] = a;
    b->m->verts[i + 1] = flip ? c2 : c1;
    b->m->verts[i + 2] = flip ? c1 : c2;
    uint16_t *f = b->m->faces + b->nf * 3;
    f[0] = (uint16_t)i; f[1] = (uint16_t)(i + 1); f[2] = (uint16_t)(i + 2);
    b->m->colors[b->nf] = color;
    if (uv) {
        float *d = b->m->uv + b->nf * 6;
        d[0] = uv[0]; d[1] = uv[1];
        d[2] = flip ? uv[4] : uv[2]; d[3] = flip ? uv[5] : uv[3];
        d[4] = flip ? uv[2] : uv[4]; d[5] = flip ? uv[3] : uv[5];
    }
    b->nv += 3;
    b->nf++;
}

/* rectangle x0..x1, y0..y1 in the plane z, facing -z (front) or +z (back) */
static void rect(build_t *b, float x0, float y0, float x1, float y1, float z, int front,
                 uint32_t color, int textured)
{
    v3_t want = { 0, 0, front ? -1.0f : 1.0f };
    v3_t p00 = { x0, y0, z }, p10 = { x1, y0, z }, p11 = { x1, y1, z }, p01 = { x0, y1, z };
    /* texel (0,0) at the top left of the label: x0, y1 */
    float uv1[6] = { 0, (float)B33_COVER_H, (float)B33_COVER_W, (float)B33_COVER_H, (float)B33_COVER_W, 0 };
    float uv2[6] = { 0, (float)B33_COVER_H, (float)B33_COVER_W, 0, 0, 0 };
    tri(b, p00, p10, p11, want, color, textured ? uv1 : NULL);
    tri(b, p00, p11, p01, want, color, textured ? uv2 : NULL);
}

/*
 * Memory Stick Duo, 31 x 20 x 1.6 mm, one unit per millimetre: the
 * contacts are at the +x end, with the corner cut at +x +y. Front (label)
 * towards -z, the side the camera looks at.
 */
static int build_stick(r3d_mesh_t *m)
{
    static const float ol[5][2] = { { -15.5f, -10 }, { 15.5f, -10 }, { 15.5f, 7.5f }, { 13, 10 }, { -15.5f, 10 } };
    const float zf = -0.8f, zb = 0.8f, lift = 0.25f;
    const int pads = 10;
    int nf = 3 + 3 + 10 + 2 + pads * 2 + 2 + 1;
    if (r3d_mesh_alloc(m, nf * 3, nf) != 0 || r3d_mesh_alloc_uv(m) != 0)
        return -1;
    build_t b = { m, 0, 0 };
    v3_t fr[5], bk[5];
    for (int i = 0; i < 5; i++) {
        fr[i] = (v3_t){ ol[i][0], ol[i][1], zf };
        bk[i] = (v3_t){ ol[i][0], ol[i][1], zb };
    }
    for (int i = 1; i < 4; i++) {
        tri(&b, fr[0], fr[i], fr[i + 1], (v3_t){ 0, 0, -1 }, C_BODY, NULL);
        tri(&b, bk[0], bk[i], bk[i + 1], (v3_t){ 0, 0, 1 }, C_BODY, NULL);
    }
    for (int i = 0; i < 5; i++) {               /* sides, outwards */
        int j = (i + 1) % 5;
        v3_t out = { (ol[i][0] + ol[j][0]) * 0.5f, (ol[i][1] + ol[j][1]) * 0.5f, 0 };
        tri(&b, fr[i], fr[j], bk[j], out, 0x3A404C, NULL);
        tri(&b, fr[i], bk[j], bk[i], out, 0x3A404C, NULL);
    }
    /* the cover: 27 x 17, as 128 x 80 */
    rect(&b, -14.8f, -8.5f, 12.2f, 8.5f, zf - lift, 1, R3D_TEXTURED, 1);
    /* copper contacts on the back, at the +x end */
    for (int i = 0; i < pads; i++) {
        float y = -8.6f + i * 1.6f;
        rect(&b, 12.2f, y, 15.0f, y + 1.1f, zb + lift, 0, C_COPPER, 0);
    }
    /* a grey sticker on the back and an arrow on the front */
    rect(&b, -13.5f, -7.5f, 9.5f, 7.5f, zb + lift, 0, 0x4A5060, 0);
    tri(&b, (v3_t){ 13.2f, -1.4f, zf - lift }, (v3_t){ 13.2f, 1.4f, zf - lift },
        (v3_t){ 15.0f, 0, zf - lift }, (v3_t){ 0, 0, -1 }, 0xE0E0E0, NULL);
    m->nverts = b.nv;
    m->nfaces = b.nf;
    r3d_mesh_normals(m);
    return 0;
}

static int build_floor(r3d_mesh_t *m)
{
    if (r3d_mesh_alloc(m, 6, 2) != 0)
        return -1;
    build_t b = { m, 0, 0 };
    v3_t p0 = { -400, 0, -60 }, p1 = { 400, 0, -60 }, p2 = { 400, 0, 400 }, p3 = { -400, 0, 400 };
    tri(&b, p0, p1, p2, (v3_t){ 0, 1, 0 }, 0x1A2238, NULL);
    tri(&b, p0, p2, p3, (v3_t){ 0, 1, 0 }, 0x1A2238, NULL);
    r3d_mesh_normals(m);
    return 0;
}

/* ---------------------------------------------------------------- covers */

int menu3d_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h)
{
    if (w != B33_COVER_W || h != B33_COVER_H || g16_sheet_alloc(s, w, h) != 0)
        return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
            g16_sheet_set(s, x, y, g16_rgb(p[0], p[1], p[2]), 1);
        }
    return 0;
}

int menu3d_make_cover(g16_sheet_t *s, const char *title, const char *kind)
{
    if (g16_sheet_alloc(s, B33_COVER_W, B33_COVER_H) != 0)
        return -1;
    uint32_t h = 2166136261u;
    for (const char *p = title; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    g16_t g;
    g16_target(&g, s->px, (uint32_t)s->w, s->w, s->h, &font_console_8x16);
    uint32_t r = 40 + (h & 63), gg = 40 + (h >> 6 & 63), b = 70 + (h >> 12 & 95);
    for (int y = 0; y < s->h; y++) {
        uint32_t k = 100 + (uint32_t)y * 2;
        g16_rectfill(&g, 0, y, s->w, 1, g16_rgb(r * k / 160, gg * k / 160, b * k / 160));
    }
    g16_rect(&g, 2, 2, s->w - 4, s->h - 4, g16_rgb(220, 220, 230));
    /* the title on up to three lines of 14 characters, split at spaces */
    char lines[3][15] = { "", "", "" };
    int n = 0;
    const char *p = title;
    while (*p && n < 3) {
        while (*p == ' ') p++;
        int len = (int)strlen(p);
        int take = len <= 14 ? len : 14;
        if (len > 14)
            for (int i = 14; i > 0; i--)
                if (p[i] == ' ') { take = i; break; }
        memcpy(lines[n], p, (size_t)take);
        lines[n][take] = 0;
        n++;
        p += take;
    }
    int y0 = (s->h - n * 16) / 2 - 4;
    for (int i = 0; i < n; i++) {
        int x = (s->w - (int)strlen(lines[i]) * 8) / 2;
        g16_text(&g, x + 1, y0 + i * 16 + 1, lines[i], 0);
        g16_text(&g, x, y0 + i * 16, lines[i], g16_rgb(255, 230, 120));
    }
    g16_text(&g, s->w - 30, s->h - 20, kind, g16_rgb(200, 210, 230));
    memset(s->alpha, 1, (size_t)s->w * s->h);
    return 0;
}

/* ---------------------------------------------------------------- screen */

static void targets(framebuffer_t *fb)
{
    g16_target(&g2, (uint16_t *)fb->base, fb->pitch / 2, SW, SH, &font_console_8x16);
    g16_target(&g3, (uint16_t *)(fb->base + VIEW_Y * fb->pitch), fb->pitch / 2, VIEW_W, VIEW_H,
               &font_console_8x16);
}

int menu3d_open(framebuffer_t *fb)
{
    if (!meshes) {
        if (build_stick(&stick) != 0 || build_floor(&floor_mesh) != 0)
            return -1;
        meshes = 1;
    }
    con_w = fb->width;
    con_h = fb->height;
    console_suspend(1);
    if (fb_init_depth(fb, SW, SH, 2, 16) != 0) {
        fb_init(fb, con_w, con_h, 2);
        console_suspend(0);
        return -1;
    }
    targets(fb);
    if (!r3.zbuf && r3d_init(&r3, &g3) != 0) {
        fb_init(fb, con_w, con_h, 2);
        console_suspend(0);
        return -1;
    }
    r3.g = &g3;
    ready = 1;
    t0 = timer_ticks();
    deadline = t0 + FRAME_US;
    return 0;
}

void menu3d_close(framebuffer_t *fb)
{
    if (!ready)
        return;
    ready = 0;
    fb_init(fb, con_w, con_h, 2);
    console_suspend(0);
}

static uint16_t c16(uint32_t rgb)
{
    return g16_rgb24(rgb);
}

/* text on the 8x16 grid, on its own background */
static void cell_text(int col, int row, const char *s, uint32_t fg, uint32_t bg, int width)
{
    char buf[81];
    int n = width < 80 ? width : 80, i = 0;
    for (; i < n && s[i]; i++)
        buf[i] = s[i];
    buf[i] = 0;
    g16_rectfill(&g2, col * 8, row * 16, n * 8, 16, c16(bg));
    g16_text(&g2, col * 8, row * 16, buf, c16(fg));
}

static void draw_view(const menu_item_t *items, int n, int sel, float t)
{
    /* background: dusk gradient, then the floor and the cards */
    for (int y = 0; y < VIEW_H; y += 4) {
        uint32_t k = (uint32_t)y * 255 / VIEW_H;
        g16_rectfill(&g3, 0, y, VIEW_W, 4, g16_rgb(16 + k / 10, 20 + k / 12, 44 + k / 6));
    }
    r3d_zclear(&r3);
    r3d_camera(&r3, 0, 8, -74, 0, -0.1f, 50);
    r3d_light(&r3, -0.35f, 0.55f, -0.75f, 0.5f);
    r3d_fog(&r3, 0x1A2238, 70, 160);
    r3d_draw(&r3, &floor_mesh, (v3_t){ 0, -16, 0 }, 0, 0, 0, 1);

    pos += ((float)sel - pos) * 0.18f;
    for (int i = 0; i < n; i++) {
        float d = (float)i - pos;
        if (d < -3.5f || d > 3.5f)
            continue;
        float ad = fabsf(d);
        float x = d * 30.0f, z = ad * 24.0f, yaw = d < 0 ? 0.55f : -0.55f;
        float scale = 1.0f, pitch = -0.1f;
        if (ad < 1) {
            yaw *= ad;
            if (i == sel) {
                /* the selected card sways; every 7 s it turns over to show
                 * the contacts on the back */
                float c = fmodf(t, 7.0f);
                float turn = c < 1.6f ? (1 - cosf(c / 1.6f * 3.14159265f)) * 3.14159265f : 0;
                yaw += 0.3f * sinf(t * 1.3f) + turn;
                pitch = -0.1f + 0.08f * sinf(t * 0.9f);
                scale = 1.0f + 0.1f * (1 - ad);
            }
        }
        stick.tex = items[i].cover;
        r3d_draw(&r3, &stick, (v3_t){ x, 0, z }, pitch, yaw, 0, scale);
    }
}

void menu3d_frame(framebuffer_t *fb, const menu_item_t *items, int n, int sel,
                  const char *header, const char *footer_note)
{
    if (!ready)
        return;
    targets(fb);
    float t = (float)(timer_ticks() - t0) * 1e-6f;

    draw_view(items, n, sel, t);

    /* header */
    char buf[96];
    g16_rectfill(&g2, 0, 0, SW, 16, c16(C_HEADER));
    g16_text(&g2, 0, 0, " bm33 - cartridges", c16(C_TITLE));
    g16_text(&g2, 19 * 8, 0, header, c16(C_DIM));

    /* list on the right */
    const int rows = VIEW_H / 16, cols = (SW - LIST_X) / 8;
    int top = sel - rows / 2;
    if (top > n - rows) top = n - rows;
    if (top < 0) top = 0;
    for (int r = 0; r < rows; r++) {
        int i = top + r;
        if (i < n) {
            ksnprintf(buf, sizeof buf, "%c %s", i == sel ? '>' : ' ', items[i].title);
            cell_text(LIST_X / 8, 1 + r, buf, i == sel ? C_SEL_FG : C_TEXT, i == sel ? C_SEL_BG : C_LIST, cols);
        } else {
            cell_text(LIST_X / 8, 1 + r, "", C_TEXT, C_LIST, cols);
        }
    }

    /* details, last game, keys */
    g16_rectfill(&g2, 0, (1 + rows) * 16, SW, SH - (1 + rows) * 16, c16(C_PANEL));
    if (sel < n) {
        const menu_item_t *it = &items[sel];
        ksnprintf(buf, sizeof buf, " %s", it->title);
        cell_text(0, 17, buf, C_YELLOW, C_PANEL, 80);
        ksnprintf(buf, sizeof buf, " %s   %s   %lu KiB", it->author[0] ? it->author : "-", it->kind,
                  (it->size + 1023) / 1024);
        cell_text(0, 18, buf, C_TEXT, C_PANEL, 80);
        ksnprintf(buf, sizeof buf, " %s", it->path);
        cell_text(0, 19, buf, C_DIM, C_PANEL, 80);
    }
    ksnprintf(buf, sizeof buf, " %s", footer_note);
    cell_text(0, 20, buf, C_DIM, C_PANEL, 80);
    cell_text(0, 21, " up/down choose   Enter/A play   Esc or Start+Select: monitor   R rescan SD",
              C_YELLOW, C_PANEL, 80);

    fb_flip(fb);
    /* pacing when the firmware does not wait for the vertical blank */
    while ((int32_t)(timer_ticks() - deadline) < 0)
        ;
    uint32_t now = timer_ticks();
    deadline += FRAME_US;
    if ((int32_t)(now - deadline) > 0)
        deadline = now + FRAME_US;
}
