/*
 * The controls of the Pi's cartridges (kernel/input.h, as src/bm reads
 * them) on the RGB30: its own buttons and sticks are player 1, together
 * with a Bluetooth pad or keyboard; the serial port and the network
 * console type the Pi's keys (w a s d, space, k, q ...).
 *
 * The face buttons are the game's by their letters (what a game writes,
 * "A: start", is the button with that letter); game_buttons=position in
 * bm/config.txt puts them by position instead, as on the Pi with a DS4:
 * the lower one (B, the DS4's cross) is the game's A, the right one (A)
 * its B, the left one (Y) its X, the top one (X) its Y. Start + Select
 * leaves the game.
 */
#include "kernel/input.h"
#include "usb/hid.h"
#include "net/netcon.h"
#include "plat.h"
#include "pad.h"
#include "drivers/timer.h"
#include "kernel/config.h"

#include <string.h>

static uint32_t to_hid(uint32_t p)
{
    static const struct { uint32_t pad, hid; } map[] = {
        { PAD_LEFT, HID_LEFT }, { PAD_RIGHT, HID_RIGHT }, { PAD_UP, HID_UP }, { PAD_DOWN, HID_DOWN },
        { PAD_A, HID_A }, { PAD_B, HID_B }, { PAD_X, HID_X }, { PAD_Y, HID_Y },
        { PAD_START, HID_START }, { PAD_SELECT, HID_SELECT }, { PAD_L1, HID_L1 }, { PAD_R1, HID_R1 },
        { PAD_L2, HID_L2 }, { PAD_R2, HID_R2 }, { PAD_L3, HID_L3 }, { PAD_R3, HID_R3 },
    };
    const char *v = config_get("game_buttons");
    if (v && !strcmp(v, "position")) {         /* turned a quarter: B A Y X */
        uint32_t q = p & ~(uint32_t)(PAD_A | PAD_B | PAD_X | PAD_Y);
        if (p & PAD_B) q |= PAD_A;
        if (p & PAD_A) q |= PAD_B;
        if (p & PAD_Y) q |= PAD_X;
        if (p & PAD_X) q |= PAD_Y;
        p = q;
    }
    uint32_t h = 0;
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (p & map[i].pad)
            h |= map[i].hid;
    return h;
}

int input_remote_getc(void)
{
    int c = plat_uart_getc();
    return c >= 0 ? c : netcon_getc();
}

static int by_position(void)
{
    const char *v = config_get("game_buttons");
    return v && !strcmp(v, "position");
}

/* player 1 is the console's own controls (and a Bluetooth pad with them) */
int input_device(int p)
{
    return p == 0 ? INPUT_DEV_PAD | INPUT_DEV_BUILTIN : INPUT_DEV_NONE;
}

/* the menus' confirm button (pad_ok: B, the lower one, unless confirm=a)
 * as the game sees it: its letter, or its place with game_buttons=position */
uint32_t input_ok_bit(int back)
{
    uint32_t phys = back ? pad_back : pad_ok;
    if (by_position())
        return phys == PAD_B ? HID_A : HID_B;
    return phys == PAD_B ? HID_B : HID_A;
}

uint32_t input_face_shown(uint32_t bit)
{
    if (!by_position())
        return bit;
    return bit == HID_A ? HID_B : bit == HID_B ? HID_A : bit == HID_X ? HID_Y : bit == HID_Y ? HID_X : bit;
}

static uint32_t prev_held;

uint32_t input_players(uint32_t out[INPUT_PLAYERS], int text, int *quit, int *local)
{
    (void)text;
    uint32_t held = to_hid(plat_buttons()) | hid_buttons();
    const uint32_t leave = HID_START | HID_SELECT;
    if ((held & leave) == leave && (prev_held & leave) != leave)
        *quit |= HID_QUIT_KEY;
    prev_held = held;
    out[0] = held;
    for (int p = 1; p < INPUT_PLAYERS; p++)
        out[p] = 0;
    *local = 0;
    return held;
}

unsigned input_connected(void)
{
    return 1;
}

/* -1..1, with a dead zone around the centre */
static float axis(int16_t v)
{
    const int dead = 4000;
    if (v > -dead && v < dead)
        return 0;
    float f = (float)(v > 0 ? v - dead : v + dead) / (32767.0f - dead);
    return f > 1 ? 1 : f < -1 ? -1 : f;
}

/* the sticks, read once a frame (four ADC conversions: half a millisecond) */
static const int16_t *sticks(void)
{
    static int16_t a[4];
    static uint32_t at;
    static int have;
    uint32_t now = timer_ticks();
    if (!have || now - at > 4000) {
        plat_sticks(a);
        at = now;
        have = 1;
    }
    return a;
}

void input_stick(int p, uint32_t buttons, float *x, float *y)
{
    *x = *y = 0;
    if (p == 0) {
        const int16_t *a = sticks();
        *x = axis(a[0]);
        *y = axis(a[1]);
    }
    if (*x == 0 && *y == 0) {                   /* the cross */
        *x = (buttons & HID_RIGHT) ? 1.0f : (buttons & HID_LEFT) ? -1.0f : 0.0f;
        *y = (buttons & HID_DOWN) ? 1.0f : (buttons & HID_UP) ? -1.0f : 0.0f;
    }
}

void input_stick_r(int p, float *x, float *y)
{
    *x = *y = 0;
    if (p != 0)
        return;
    const int16_t *a = sticks();
    *x = axis(a[2]);
    *y = axis(a[3]);
}

void input_flush(void)
{
}
