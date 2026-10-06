#include "input.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "bt/bt.h"
#include "net/net.h"
#include "net/netcon.h"
#include "net/netxfer.h"

#include <math.h>

static void update_uptime(void)
{
    char buf[80], pads[48];
    uint32_t s = timer_ticks() / 1000000;
    input_status(pads, sizeof pads);
    ksnprintf(buf, sizeof buf, "%s  up %02lu:%02lu:%02lu", pads, s / 3600, s / 60 % 60, s % 60);
    console_set_status(0, buf);
}

static int pad_keys;
static uint32_t pad_prev;
static int given_back = -1;             /* input_unget */

void input_unget(int c)
{
    given_back = c;
}

void input_pad_keys(int mode)
{
    pad_keys = mode;
    pad_prev = hid_pad_buttons();
}

/* with input_pad_keys: a button just pressed on a controller, as a key */
static int pad_key(void)
{
    uint32_t b = hid_pad_buttons(), p = b & ~pad_prev;
    pad_prev = b;
    if (p & (HID_B | HID_START)) return 0x1B;
    if (pad_keys != INPUT_PAD_NAV) return -1;
    if (p & HID_UP) return HID_KEY_UP;
    if (p & HID_DOWN) return HID_KEY_DOWN;
    if (p & HID_LEFT) return HID_KEY_PGUP;
    if (p & HID_RIGHT) return HID_KEY_PGDN;
    if (p & HID_A) return '\r';
    return -1;
}

int input_key(void)
{
    if (given_back >= 0) {
        int c = given_back;
        given_back = -1;
        return c;
    }
    if (uart_rx_ready())
        return (unsigned char)uart_getc();
    usb_poll();
    bt_poll();
    net_poll();
    int c = netcon_getc();
    if (c >= 0)
        return c;
    if (netxfer_play_announce())
        return INPUT_NET_PLAY;
    c = hid_getc();
    if (c < 0 && pad_keys)
        c = pad_key();
    return c;
}

int input_remote_ready(void)
{
    if (given_back >= 0 || uart_rx_ready())
        return 1;
    net_poll();
    return netcon_pending();
}

int input_remote_getc(void)
{
    if (given_back >= 0) {
        int c = given_back;
        given_back = -1;
        return c;
    }
    if (uart_rx_ready())
        return (unsigned char)uart_getc();
    net_poll();
    return netcon_getc();
}

int input_remote_follows(void)
{
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 30000)
        if (input_remote_ready())
            return 1;
    return 0;
}

int input_skip_sequence(void)
{
    if (!input_remote_follows())
        return 0;
    int c = input_remote_getc();
    if (c != '[' && c != 'O')
        return 1;                       /* Esc + one key (Alt-key): both dropped */
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 50000) {        /* parameters, then the final byte */
        c = input_remote_getc();
        if (c >= 0x40 && c <= 0x7E)
            break;
    }
    return 1;
}

char input_getc(void)
{
    uint32_t last = timer_ticks();
    int c;

    update_uptime();
    while ((c = input_key()) < 0) {
        if (timer_ticks() - last >= 1000000) {
            last += 1000000;
            update_uptime();
        }
    }
    return (char)c;
}

int input_getc_home(void)
{
    uint32_t last = timer_ticks();
    int c;

    update_uptime();
    while ((c = input_key()) < 0) {
        if (hid_quit_pressed() & HID_QUIT_PS)
            return INPUT_HOME;
        if (timer_ticks() - last >= 1000000) {
            last += 1000000;
            update_uptime();
        }
    }
    return c;
}

uint32_t input_buttons(int *quit)
{
    usb_poll();
    bt_poll();
    net_poll();
    *quit |= hid_quit_pressed();
    return hid_buttons();
}

uint32_t input_pad_buttons(int *quit)
{
    usb_poll();
    bt_poll();
    net_poll();
    *quit |= hid_quit_pressed();
    return hid_pad_buttons();
}

void input_flush(void)
{
    usb_poll();
    bt_poll();
    net_poll();
    while (hid_getc() >= 0)
        ;
    hid_quit_pressed();
}

int input_local_player(void)
{
    unsigned pads = bt_pads();
    for (int p = 0; p < INPUT_PLAYERS; p++)
        if (!(pads >> p & 1))
            return p;
    return -1;
}

static int usb_input(void)
{
    int k = usb_info()->kind;
    return k == USB_KEYBOARD || k == USB_GAMEPAD || k == USB_XBOX360;
}

int input_ble_player(void)
{
    int local = input_local_player();
    if (!bt_keyboard() || local < 0)
        return -1;
    if (!usb_input())
        return local;                           /* nothing on USB: the first free player */
    unsigned pads = bt_pads();
    for (int p = local + 1; p < INPUT_PLAYERS; p++)
        if (!(pads >> p & 1))
            return p;
    return -1;                                  /* no free player: it types with the USB one */
}

uint32_t input_players(uint32_t out[INPUT_PLAYERS], int text, int *quit, int *local)
{
    usb_poll();
    bt_poll();
    net_poll();
    *quit |= hid_quit_pressed();
    *local = input_local_player();
    return hid_players(out, text, *local, input_ble_player());
}

int input_device(int p)
{
    if (bt_pads() >> p & 1)                     /* the Bluetooth pads: DS4s */
        return INPUT_DEV_PAD | INPUT_DEV_BLUETOOTH | INPUT_DEV_DS4;
    if (p == input_ble_player())                /* never the USB player's own */
        return INPUT_DEV_KEYBOARD | INPUT_DEV_BLUETOOTH;
    if (p != input_local_player())
        return INPUT_DEV_NONE;
    const usb_info_t *u = usb_info();
    if (u->kind == USB_KEYBOARD)
        return INPUT_DEV_KEYBOARD;
    if (u->kind == USB_XBOX360)
        return INPUT_DEV_PAD | INPUT_DEV_XBOX;
    if (u->kind == USB_GAMEPAD)                 /* a DS4 on its cable: Sony's vendor id */
        return INPUT_DEV_PAD | (u->vid == 0x054C ? INPUT_DEV_DS4 : 0);
    return INPUT_DEV_NONE;
}

/* the Pi's pads: yes is the lower face button (cross, A), back the right one */
uint32_t input_ok_bit(int back)
{
    return back ? HID_B : HID_A;
}

uint32_t input_face_shown(uint32_t bit)
{
    return bit;
}

unsigned input_connected(void)
{
    unsigned m = bt_pads();
    int local = input_local_player(), ble = input_ble_player();
    if (local >= 0 && (usb_input() || !m))
        m |= 1u << local;
    if (ble >= 0)
        m |= 1u << ble;
    return m;
}

void input_stick(int p, uint32_t b, float *x, float *y)
{
    int8_t xy[2] = { 0, 0 };
    int analog = (bt_pads() >> p & 1) ? hid_stick(p, xy)
               : p == input_local_player() ? hid_stick(-1, xy) : 0;
    if (analog) {
        /* dead zone, then the rest of the range scaled back to 0..1 */
        float ax = xy[0] / 127.0f, ay = xy[1] / 127.0f, m = sqrtf(ax * ax + ay * ay);
        if (m < 0.25f) {
            ax = ay = 0;
        } else {
            float k = (m > 1 ? 1 : m) - 0.25f;
            k = k / 0.75f / m;
            ax *= k;
            ay *= k;
        }
        if (ax != 0 || ay != 0 || !(b & (HID_LEFT | HID_RIGHT | HID_UP | HID_DOWN))) {
            *x = ax;
            *y = ay;
            return;
        }
    }
    /* the cross (or keys): 8 directions of length 1 */
    float dx = (b & HID_RIGHT ? 1.0f : 0) - (b & HID_LEFT ? 1.0f : 0);
    float dy = (b & HID_DOWN ? 1.0f : 0) - (b & HID_UP ? 1.0f : 0);
    if (dx != 0 && dy != 0) {
        dx *= 0.7071f;
        dy *= 0.7071f;
    }
    *x = dx;
    *y = dy;
}

void input_stick_r(int p, float *x, float *y)
{
    int8_t xy[2] = { 0, 0 };
    int analog = (bt_pads() >> p & 1) ? hid_stick_r(p, xy)
               : p == input_local_player() ? hid_stick_r(-1, xy) : 0;
    float ax = analog ? xy[0] / 127.0f : 0, ay = analog ? xy[1] / 127.0f : 0;
    float m = sqrtf(ax * ax + ay * ay);
    if (m < 0.2f) {
        ax = ay = 0;
    } else {
        float k = (m > 1 ? 1 : m) - 0.2f;
        k = k / 0.8f / m;
        ax *= k;
        ay *= k;
    }
    *x = ax;
    *y = ay;
}

void input_status(char *buf, unsigned size)
{
    /* a number: that player's pad is connected; k: the player who uses
     * the keyboard (or the USB pad) */
    unsigned m = input_connected(), pads = bt_pads();
    int n = ksnprintf(buf, size, "pads:");
    for (int p = 0; p < INPUT_PLAYERS && n + 3 < (int)size; p++) {
        if (pads >> p & 1)
            n += ksnprintf(buf + n, size - (unsigned)n, " %d", p + 1);
        else
            n += ksnprintf(buf + n, size - (unsigned)n, (m >> p & 1) ? " k" : " -");
    }
    if (net_ip() && n + 18 < (int)size)          /* on the network: its address */
        ksnprintf(buf + n, size - (unsigned)n, "  IP %s", net_ip_text());
}

void input_live_test(uint32_t seconds)
{
    static const char *const names[] = { "<", ">", "^", "v", "A", "B", "St", "Se", "X", "Y", "L1", "R1",
                                          "L2", "R2", "L3", "R3" };
    kprintf("input test for %lu s: players and the buttons they hold\n", seconds);
    for (int p = 0; p < INPUT_PLAYERS; p++) {
        char a[18];
        int on = bt_pad_addr(p, a);
        if (a[0])
            kprintf("  player %d: pad %s%s\n", p + 1, a, on ? "" : " (not connected: press PS)");
    }
    kprintf("  keyboard / USB / serial: the first player without a pad\n");
    uint32_t t0 = timer_ticks(), shown = 0, seen[INPUT_PLAYERS] = { 0 };
    while (timer_ticks() - t0 < seconds * 1000000u) {
        uint32_t out[INPUT_PLAYERS];
        int quit = 0, local;
        input_players(out, 0, &quit, &local);
        for (int p = 0; p < INPUT_PLAYERS; p++)
            seen[p] |= out[p];
        if (timer_ticks() - shown < 200000)
            continue;
        shown = timer_ticks();
        char line[128];
        int n = 0;
        unsigned m = input_connected();
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            n += ksnprintf(line + n, sizeof line - (unsigned)n, " P%d%s:", p + 1,
                           p == local ? "*" : "");
            if (!(m >> p & 1)) {
                n += ksnprintf(line + n, sizeof line - (unsigned)n, " -   ");
                continue;
            }
            int any = 0;
            for (int b = 0; b < (int)(sizeof names / sizeof names[0]); b++)
                if (seen[p] >> b & 1) {
                    n += ksnprintf(line + n, sizeof line - (unsigned)n, "%s", names[b]);
                    any = 1;
                }
            n += ksnprintf(line + n, sizeof line - (unsigned)n, any ? " " : " .   ");
            seen[p] = 0;
        }
        kprintf("\r%s\x1b[K", line);
    }
    kprintf("\n");
    input_flush();
}

int input_read_line(char *buf, int max, int secret)
{
    int n = 0;
    for (;;) {
        char c = input_getc();
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            kprintf("\n");
            return n;
        }
        if (c == 0x1B) {
            if (input_skip_sequence())
                continue;               /* an arrow key, not Esc */
            kprintf("  (cancelled)\n");
            return -1;
        }
        if ((c == 0x7F || c == 0x08) && n > 0) {
            n--;
            kprintf("\b \b");
            continue;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127 && n < max - 1) {
            buf[n++] = c;
            kprintf("%c", secret ? '*' : c);
        }
    }
}
