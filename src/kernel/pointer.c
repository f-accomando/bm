#include "pointer.h"
#include "config.h"
#include "bt/bt.h"
#include "usb/hid.h"
#include "usb/usb.h"

#include <math.h>
#include <string.h>

/* motion is scaled as if the screen were 640x360 (the menu), whatever the
 * size of the screen that has the pointer: the same hand movement crosses
 * the same part of the screen in the menu and in a 320x180 game */
#define REF_W        640.0f
#define REF_H        360.0f

static struct {
    int enabled;
    int on, w, h;
    float nx, ny;                   /* position, 0..1 of the screen */
    int active;                     /* moved since the last pointer_hide() */
    unsigned had;                   /* mice connected at the last update */
    pointer_t p;
} pt = { .enabled = 1, .nx = 0.5f, .ny = 0.5f };

void pointer_config(void)
{
    const char *v = config_get("mouse");
    pt.enabled = !(v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcmp(v, "no") == 0));
}

int pointer_enabled(void)
{
    return pt.enabled;
}

static void place(void)
{
    pt.p.x = pt.w > 0 ? (int)(pt.nx * (float)pt.w) : 0;
    pt.p.y = pt.h > 0 ? (int)(pt.ny * (float)pt.h) : 0;
    if (pt.p.x >= pt.w) pt.p.x = pt.w - 1;
    if (pt.p.y >= pt.h) pt.p.y = pt.h - 1;
    if (pt.p.x < 0) pt.p.x = 0;
    if (pt.p.y < 0) pt.p.y = 0;
}

void pointer_env(int on, int w, int h)
{
    pt.on = on && w > 0 && h > 0;
    if (pt.on) {
        pt.w = w;
        pt.h = h;
    }
    place();
    pt.p.buttons = pt.p.pressed = pt.p.released = 0;
    pt.p.wheel = pt.p.pan = pt.p.moved = 0;
    pt.p.shown = 0;
    hid_mouse_t old;
    hid_mouse_take(&old);                       /* what the mice did elsewhere */
}

int pointer_env_on(void)
{
    return pt.on;
}

unsigned pointer_devices(void)
{
    if (!pt.enabled)
        return 0;
    unsigned m = 0;
    /* a USB mouse once it has done something: the dongle of a wireless
     * keyboard has a mouse interface too, with no mouse behind it (the
     * icon was there on a Pi 1 B with no mouse, 2026-10-04) */
    if (usb_info()->mouse && hid_mouse_seen(HID_MOUSE_USB))
        m |= POINTER_USB;
    if (bt_mouse())
        m |= POINTER_BLUETOOTH;
    return m;
}

/* mouse counts -> pixels at 640x360: slow movements precise, fast ones go far */
static float accel(int32_t dx, int32_t dy)
{
    float v = sqrtf((float)dx * dx + (float)dy * dy);
    float t = (v - 4.0f) / 30.0f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return 0.4f + 0.8f * t;
}

const pointer_t *pointer_update(void)
{
    /* only a mouse moves it: a controller never shows the arrow (the
     * user, 2026-10-05; the right stick and R2 moved and clicked it) */
    hid_mouse_t m;
    hid_mouse_take(&m);
    unsigned mice = pointer_devices();
    pointer_t *p = &pt.p;
    int was_shown = p->shown;
    uint8_t before = p->buttons;

    p->moved = 0;
    p->pressed = p->released = 0;
    p->wheel = p->pan = 0;
    if (!pt.enabled) {
        p->buttons = 0;
        p->shown = p->available = 0;
        return p;
    }
    if (mice & ~pt.had)
        pt.active = 1;                          /* a mouse just connected: show it */
    pt.had = mice;

    float ox = pt.nx, oy = pt.ny;
    if (m.abs) {
        pt.nx = m.ax / 65536.0f;
        pt.ny = m.ay / 65536.0f;
    }
    if (m.dx || m.dy) {
        float k = accel(m.dx, m.dy);
        pt.nx += (float)m.dx * k / REF_W;
        pt.ny += (float)m.dy * k / REF_H;
    }
    pt.nx = pt.nx < 0 ? 0 : pt.nx > 0.9999f ? 0.9999f : pt.nx;
    pt.ny = pt.ny < 0 ? 0 : pt.ny > 0.9999f ? 0.9999f : pt.ny;
    int ox_px = p->x, oy_px = p->y;
    place();
    if (pt.nx != ox || pt.ny != oy) {
        pt.active = 1;
        p->moved = p->x != ox_px || p->y != oy_px;
    }

    /* buttons: the mice's */
    uint8_t b = m.buttons;
    uint8_t press = (uint8_t)((b & ~before) | m.pressed);
    if (press)
        pt.active = 1;
    p->buttons = b;
    p->available = mice != 0;
    p->shown = pt.on && p->available && pt.active;
    /* a click that shows a hidden pointer only shows it */
    p->pressed = was_shown && p->shown ? press : 0;
    p->released = (uint8_t)(before & ~b);
    p->wheel = (int)m.wheel;
    p->pan = (int)m.pan;
    return p;
}

const pointer_t *pointer_get(void)
{
    return &pt.p;
}

void pointer_hide(void)
{
    pt.active = 0;
    pt.p.shown = 0;
}

/* ---------------------------------------------------------------- arrow */

static const char *const big[] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X..........X",
    "X......XXXXX",
    "X...X..X    ",
    "X..XX..X    ",
    "X.X  X..X   ",
    "XX   X..X   ",
    "X     X..X  ",
    "      X..X  ",
    "       XX   ",
};

static const char *const small[] = {
    "X       ",
    "XX      ",
    "X.X     ",
    "X..X    ",
    "X...X   ",
    "X....X  ",
    "X.....X ",
    "X......X",
    "X...XXXX",
    "X..X    ",
    "X.X     ",
    "XX      ",
};

void pointer_draw(uint16_t *px, uint32_t stride, int w, int h)
{
    if (!pt.p.shown || !px)
        return;
    const char *const *a = h >= 288 ? big : small;
    int rows = h >= 288 ? (int)(sizeof big / sizeof *big) : (int)(sizeof small / sizeof *small);
    /* the tip on the pointer; the screen of the pointer may be another size */
    int x0 = pt.w > 0 ? pt.p.x * w / pt.w : pt.p.x, y0 = pt.h > 0 ? pt.p.y * h / pt.h : pt.p.y;
    for (int j = 0; j < rows; j++) {
        int y = y0 + j;
        if (y < 0 || y >= h)
            continue;
        uint16_t *row = px + (uint32_t)y * stride;
        for (int i = 0; a[j][i]; i++) {
            int x = x0 + i;
            if (x < 0 || x >= w || a[j][i] == ' ')
                continue;
            row[x] = a[j][i] == 'X' ? 0x0000 : 0xFFFF;
        }
    }
}
