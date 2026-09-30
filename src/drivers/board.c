#include "board.h"
#include "prop.h"

/* Also in the chainloader: no libc here. */
static void put(char *dst, unsigned *n, const char *s)
{
    while (*s && *n + 1 < sizeof ((board_t *)0)->name)
        dst[(*n)++] = *s++;
    dst[*n] = '\0';
}

static const char hex[] = "0123456789abcdef";

void board_decode(uint32_t rev, board_t *b)
{
    unsigned n = 0;
    const char *pcb = 0;

    b->revision = rev;
    b->model = BOARD_UNKNOWN;
    b->name[0] = '\0';
    if (rev & (1u << 23)) {
        /* new style: ...PPPP TTTTTTTT RRRR (processor, type, PCB revision) */
        switch ((rev >> 4) & 0xFF) {
        case 0x00: b->model = BOARD_PI1_A; break;
        case 0x01: b->model = BOARD_PI1_B; break;
        case 0x02: b->model = BOARD_PI1_APLUS; break;
        case 0x03: b->model = BOARD_PI1_BPLUS; break;
        case 0x06: b->model = BOARD_CM1; break;
        case 0x09: b->model = BOARD_ZERO; break;
        case 0x0C: b->model = BOARD_ZERO_W; break;
        default:   b->model = BOARD_OTHER; break;
        }
    } else {
        /* old style (Pi 1 only); bit 24 = warranty void, ignored */
        switch (rev & 0xFFFF) {
        case 0x02: case 0x03:
            b->model = BOARD_PI1_B; pcb = "1.0"; break;
        case 0x04: case 0x05: case 0x06: case 0x0D: case 0x0E: case 0x0F:
            b->model = BOARD_PI1_B; pcb = "2.0"; break;
        case 0x07: case 0x08: case 0x09:
            b->model = BOARD_PI1_A; break;
        case 0x10: case 0x13: b->model = BOARD_PI1_BPLUS; break;
        case 0x11: case 0x14: b->model = BOARD_CM1; break;
        case 0x12: case 0x15: b->model = BOARD_PI1_APLUS; break;
        default: break;
        }
    }

    /* ACT LED: GPIO 16 active low on the first Pi 1 A/B, GPIO 47 on the
     * later boards (active high on the Pi 1 A+/B+, low on the Zero) */
    b->led_pin = 47;
    b->led_active_high = 0;
    b->wireless = 1;
    b->ethernet = 0;
    switch (b->model) {
    case BOARD_PI1_A:     put(b->name, &n, "Pi 1 A"); break;
    case BOARD_PI1_B:     put(b->name, &n, "Pi 1 B"); break;
    case BOARD_PI1_APLUS: put(b->name, &n, "Pi 1 A+"); break;
    case BOARD_PI1_BPLUS: put(b->name, &n, "Pi 1 B+"); break;
    case BOARD_CM1:       put(b->name, &n, "Compute Module 1"); break;
    case BOARD_ZERO:      put(b->name, &n, "Pi Zero"); break;
    case BOARD_ZERO_W:    put(b->name, &n, "Pi Zero W"); break;
    default:              put(b->name, &n, "Pi (unknown board)"); break;
    }
    if (pcb) {
        put(b->name, &n, " rev ");
        put(b->name, &n, pcb);
    }
    switch (b->model) {
    case BOARD_PI1_A: case BOARD_PI1_B:
        b->led_pin = 16;
        break;
    case BOARD_PI1_APLUS: case BOARD_PI1_BPLUS:
        b->led_active_high = 1;
        break;
    case BOARD_CM1:
        b->led_pin = 0;
        break;
    default:
        break;
    }
    /* The Pi 1 has no radio. The plain Zero has none either, but QEMU's
     * raspi0 says "Zero" and the WiFi/Bluetooth tests run there: it keeps
     * the probe, which fails quietly on a real Zero. */
    if (b->model >= BOARD_PI1_A && b->model <= BOARD_CM1)
        b->wireless = 0;
    b->ethernet = b->model == BOARD_PI1_B || b->model == BOARD_PI1_BPLUS;
    if (b->model == BOARD_UNKNOWN || b->model == BOARD_OTHER) {
        put(b->name, &n, " ");
        char h[9];
        for (int i = 0; i < 8; i++)
            h[i] = hex[(rev >> (28 - 4 * i)) & 0xF];
        h[8] = '\0';
        put(b->name, &n, h);
    }
}

const board_t *board(void)
{
    static board_t b;
    static int ready;
    if (!ready) {
        /* the first thing the kernel asks the firmware: the answer counts
         * only when two reads in a row agree (a Zero W once came out as a
         * board without radio: no WiFi, the LED on the wrong pin) */
        uint32_t rev = 0, prev = 0;
        int same = 0;
        for (int i = 0; i < 8 && !same; i++) {
            uint32_t v[1] = { 0 };
            if (prop_query(PROP_GET_BOARD_REVISION, v, 1) != 0)
                continue;
            same = i > 0 && v[0] == prev;
            prev = rev = v[0];
        }
        board_decode(same ? rev : 0, &b);
        ready = 1;
    }
    return &b;
}
