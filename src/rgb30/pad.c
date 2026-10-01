#include "pad.h"
#include "plat.h"
#include "drivers/timer.h"

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
    case '\r': case '\n': return PAD_A;
    case 8: case 127: return PAD_B;
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

static void serial_poll(uint32_t now)
{
    int c;
    while ((c = plat_uart_getc()) >= 0) {
        if (esc_state == 1) {
            esc_state = c == '[' ? 2 : 0;
            if (!esc_state)
                hold(PAD_B, now);           /* a lone Esc: B */
            continue;
        }
        if (esc_state == 2) {
            esc_state = 0;
            hold(c == 'A' ? PAD_UP : c == 'B' ? PAD_DOWN : c == 'C' ? PAD_RIGHT :
                 c == 'D' ? PAD_LEFT : 0, now);
            continue;
        }
        if (c == 27) {
            esc_state = 1;
            continue;
        }
        uint32_t b = key_button(c);
        if (b)
            hold(b, now);
        else
            other_char = c;
    }
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
    return held | plat_buttons();
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
