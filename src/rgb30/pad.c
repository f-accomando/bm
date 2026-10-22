#include "pad.h"
#include "kernel/controls.h"
#include "plat.h"
#include "drivers/timer.h"
#include "bt/bt.h"
#include "usb/hid.h"
#include "net/net.h"
#include "net/netcon.h"
#include "kernel/config.h"

/* the menus: B confirms (A with confirm=a), Y goes back; A is no second back */
uint32_t pad_ok = PAD_B, pad_back = PAD_Y;

/* the face button a controls= code (mo, mc: Settings > Controls) names, or 0 */
static uint32_t face(const char *b)
{
    if (b && b[0] && !b[1])
        switch (b[0]) {
        case 'a': return PAD_A;
        case 'b': return PAD_B;
        case 'x': return PAD_X;
        case 'y': return PAD_Y;
        }
    return 0;
}

/* The menus' OK and Cancel: mo and mc of controls= (a, b, x or y), else the
 * old confirm=a (A is OK), else B and Y; the Cancel is never the OK button. */
void pad_config(void)
{
    const char *v = config_get("confirm");
    pad_ok = face(controls_lookup("system", "menu_ok"));
    if (!pad_ok)
        pad_ok = v && (v[0] == 'a' || v[0] == 'A') ? PAD_A : PAD_B;
    pad_back = face(controls_lookup("system", "menu_cancel"));
    if (!pad_back || pad_back == pad_ok)
        pad_back = pad_ok == PAD_Y ? PAD_A : PAD_Y;
}

static const char *face_name(uint32_t p) { return p == PAD_A ? "A" : p == PAD_B ? "B" : p == PAD_X ? "X" : "Y"; }
const char *pad_ok_name(void)   { return face_name(pad_ok); }
const char *pad_back_name(void) { return face_name(pad_back); }

/* Bluetooth pads and keyboards (usb/hid.h buttons) on the button in the
 * same place: their lower face button (the DS4's cross, a keyboard's
 * space) is the RGB30's B, the right one A, the left one Y, the top one X */
static uint32_t from_hid(uint32_t h)
{
    static const struct { uint32_t hid, pad; } map[] = {
        { HID_LEFT, PAD_LEFT }, { HID_RIGHT, PAD_RIGHT }, { HID_UP, PAD_UP },
        { HID_DOWN, PAD_DOWN }, { HID_A, PAD_B }, { HID_B, PAD_A },
        { HID_START, PAD_START }, { HID_SELECT, PAD_SELECT }, { HID_X, PAD_Y },
        { HID_Y, PAD_X }, { HID_L1, PAD_L1 }, { HID_R1, PAD_R1 },
    };
    uint32_t p = 0;
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (h & map[i].hid)
            p |= map[i].pad;
    return p;
}

const char *const pad_names[PAD_COUNT] = {
    "Up", "Down", "Left", "Right", "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2", "Select", "Start", "L3", "R3", "Vol+", "Vol-",
};

static uint32_t serial_held[PAD_COUNT];     /* ms timestamps (0: not held) */
static int esc_state;                       /* 0, 1 after ESC, 2 after ESC [ */
static int other_char = -1;

static uint32_t key_button(int c)
{
    switch (c) {
    case 'w': return PAD_UP;
    case 's': return PAD_DOWN;
    case 'a': return PAD_LEFT;
    case 'd': return PAD_RIGHT;
    case '\r': case '\n': return pad_ok;
    case 8: case 127: return pad_back;
    case 'x': return PAD_X;
    case 'y': return PAD_Y;
    case 'l': return PAD_L1;
    case 'r': return PAD_R1;
    case 'L': return PAD_L2;
    case 'R': return PAD_R2;
    case '\t': return PAD_SELECT;
    case ' ': return PAD_START;
    case '+': return PAD_VOLUP;
    case '-': return PAD_VOLDN;
    default: return 0;
    }
}

static void hold(uint32_t button, uint32_t now)
{
    for (int i = 0; i < PAD_COUNT; i++)
        if (button & (1u << i))
            serial_held[i] = now ? now : 1;
}

/* one character from the serial port or the network console */
static void key_in(int c, uint32_t now)
{
    if (esc_state == 1) {
        esc_state = c == '[' ? 2 : 0;
        if (!esc_state)
            hold(pad_back, now);        /* a lone Esc: back */
        return;
    }
    if (esc_state == 2) {
        esc_state = 0;
        hold(c == 'A' ? PAD_UP : c == 'B' ? PAD_DOWN : c == 'C' ? PAD_RIGHT :
             c == 'D' ? PAD_LEFT : 0, now);
        return;
    }
    if (c == 27) {
        esc_state = 1;
        return;
    }
    uint32_t b = key_button(c);
    if (b)
        hold(b, now);
    else
        other_char = c;
}

static void serial_poll(uint32_t now)
{
    int c;
    /* the network console's keys are the buttons, everywhere (netcon_focus) */
    netcon_focus("the buttons (w a s d, Enter, Backspace, x y, l r the tabs, Tab Select, "
                 "space Start)", NULL);
    while ((c = plat_uart_getc()) >= 0)
        key_in(c, now);
    while ((c = netcon_getc()) >= 0)
        key_in(c, now);
}

uint32_t pad_state(void)
{
    uint32_t now = timer_ticks() / 1000;
    serial_poll(now);
    uint32_t held = 0;
    for (int i = 0; i < PAD_COUNT; i++)
        if (serial_held[i]) {
            if (now - serial_held[i] < PAD_SERIAL_MS)
                held |= 1u << i;
            else
                serial_held[i] = 0;
        }
    bt_poll();
    net_poll();
    held |= plat_buttons() | from_hid(hid_buttons());
    volume_keys(held, 1);
    return held;
}

uint32_t pad_pressed(void)
{
    static uint32_t prev, repeat_at;
    uint32_t now = timer_ticks() / 1000;
    uint32_t cur = pad_state();
    uint32_t edges = cur & ~prev;
    const uint32_t dpad = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    if (edges & dpad)
        repeat_at = now + 400;
    else if ((cur & dpad) && (int32_t)(now - repeat_at) >= 0) {
        edges |= cur & dpad;
        repeat_at = now + 80;
    }
    prev = cur;
    return edges;
}

int pad_serial_char(void)
{
    int c = other_char;
    other_char = -1;
    return c;
}
