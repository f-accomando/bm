#include "pointer.h"
#include "config.h"
#include "input.h"
#include "bt/bt.h"
#include "drivers/timer.h"
#include "usb/hid.h"
#include "usb/usb.h"

#include <math.h>
#include <string.h>

/* motion is scaled as if the screen were 640x360 (the menu), whatever the
 * size of the screen that has the pointer: the same hand movement crosses
 * the same part of the screen in the menu and in a 320x180 game */
#define REF_W        640.0f
#define REF_H        360.0f
#define STICK_DEAD   0.20f
#define STICK_SPEED  560.0f         /* pixels per second, stick all the way (at 640x360) */

static struct {
    int enabled;
    int on, w, h;
    float nx, ny;                   /* position, 0..1 of the screen */
    int active;                     /* moved since the last pointer_hide() */
    unsigned had;                   /* mice connected at the last update */
    uint32_t last_us;
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
    pt.last_us = timer_ticks();
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

/* The right stick pushed furthest among the pads, -1..1 (dead zone out);
 * 0 if none has one. */
static int stick(float *sx, float *sy)
{
    int any = 0;
    float best = 0;
    *sx = *sy = 0;
    unsigned pads = bt_pads();
    int usb = usb_info()->kind == USB_GAMEPAD || usb_info()->kind == USB_XBOX360;
    for (int s = -1; s < INPUT_PLAYERS; s++) {
        int8_t xy[2];
        if ((s < 0 && !usb) || (s >= 0 && !(pads >> s & 1)) || !hid_stick2(s, xy))
            continue;
        any = 1;
        float x = xy[0] / 127.0f, y = xy[1] / 127.0f, m = x * x + y * y;
        if (m > best) {
            best = m;
            *sx = x;
            *sy = y;
        }
    }
    float m = sqrtf(best);
    if (m < STICK_DEAD) {
        *sx = *sy = 0;
    } else {
        float k = ((m > 1 ? 1 : m) - STICK_DEAD) / (1 - STICK_DEAD);
        k = k * k / m;                          /* slow near the centre, quick at the edge */
        *sx *= k;
        *sy *= k;
    }
    return any;
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
    uint32_t now = timer_ticks();
    float dt = (float)(now - pt.last_us) * 1e-6f;
    pt.last_us = now;
    if (dt > 0.05f)
        dt = 0.05f;

    hid_mouse_t m;
    hid_mouse_take(&m);
    uint32_t pad = hid_pointer_buttons();
    unsigned mice = pointer_devices();
    float sx, sy;
    int sticks = stick(&sx, &sy);
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
    if (sx != 0 || sy != 0) {
        pt.nx += sx * STICK_SPEED * dt / REF_W;
        pt.ny += sy * STICK_SPEED * dt / REF_H;
    }
    pt.nx = pt.nx < 0 ? 0 : pt.nx > 0.9999f ? 0.9999f : pt.nx;
    pt.ny = pt.ny < 0 ? 0 : pt.ny > 0.9999f ? 0.9999f : pt.ny;
    int ox_px = p->x, oy_px = p->y;
    place();
    if (pt.nx != ox || pt.ny != oy) {
        pt.active = 1;
        p->moved = p->x != ox_px || p->y != oy_px;
    }

    /* buttons: the mice's, and R2 / R3 (left) and L2 (right) on a pad */
    uint8_t b = m.buttons;
    if (pad & (HID_R2 | HID_R3)) b |= 1;
    if (pad & HID_L2) b |= 2;
    uint8_t press = (uint8_t)((b & ~before) | m.pressed);
    if (press)
        pt.active = 1;
    p->buttons = b;
    p->available = mice != 0 || sticks;
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
