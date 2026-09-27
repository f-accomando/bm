#include "player.h"
#include "s32.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "kernel/input.h"
#include "usb/hid.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "audio/audio.h"

#include <stdlib.h>
#include <string.h>

#define TICK_US     16667
#define HOLD_TICKS  10          /* a serial key press counts as held this long */

#define IN_UP       0x01
#define IN_DOWN     0x02
#define IN_LEFT     0x04
#define IN_RIGHT    0x08
#define IN_ACTION   0x10

static uint8_t *machine_mem;

/* Attract mode: walk around a rectangle, press action at the corners. */
static uint8_t attract_input(uint32_t tick)
{
    static const struct { uint8_t in; uint16_t len; } script[] = {
        { IN_RIGHT, 80 }, { IN_ACTION, 6 }, { IN_DOWN, 45 }, { IN_ACTION, 6 },
        { IN_LEFT, 80 }, { IN_ACTION, 6 }, { IN_UP, 45 }, { IN_ACTION, 6 },
        { IN_RIGHT | IN_DOWN, 40 }, { IN_LEFT | IN_UP, 40 }, { 0, 20 },
    };
    uint32_t total = 0;
    for (size_t i = 0; i < sizeof script / sizeof *script; i++)
        total += script[i].len;
    tick %= total;
    for (size_t i = 0; i < sizeof script / sizeof *script; i++) {
        if (tick < script[i].len)
            return script[i].in;
        tick -= script[i].len;
    }
    return 0;
}

/* Serial keys -> held bits. Returns 1 if 'q' was pressed. */
static int poll_keys(uint8_t hold[5], int *esc_state, int *seen)
{
    while (uart_rx_ready()) {
        char c = uart_getc();
        int bitn = -1;
        *seen = 1;
        if (*esc_state == 1) { *esc_state = c == '[' ? 2 : 0; continue; }
        if (*esc_state == 2) {
            *esc_state = 0;
            if (c == 'A') bitn = 0;
            else if (c == 'B') bitn = 1;
            else if (c == 'D') bitn = 2;
            else if (c == 'C') bitn = 3;
        } else {
            switch (c) {
            case 0x1B: *esc_state = 1; continue;
            case 'w': case 'W': bitn = 0; break;
            case 's': case 'S': bitn = 1; break;
            case 'a': case 'A': bitn = 2; break;
            case 'd': case 'D': bitn = 3; break;
            case ' ': case 'j': case 'J': bitn = 4; break;
            case 'q': case 'Q': return 1;
            }
        }
        if (bitn >= 0)
            hold[bitn] = HOLD_TICKS;
    }
    return 0;
}

/* The PPU draws a band of BAND rows into a small buffer that stays in the
 * data cache; each band is converted to the framebuffer's pixel order and
 * written out. Nothing is read back from uncached GPU memory (~100 ns per
 * read) nor from the SDRAM (the ARM1176 reads it about 4x slower than it
 * writes it): 8 ms per tick when a whole frame was rendered then copied. */
#define BAND 8
static uint32_t band[S32_SCREEN_W * BAND];

static void render_frame(const s32_machine_t *m, framebuffer_t *fb)
{
    s32_render_begin(m);
    for (int y0 = 0; y0 < S32_SCREEN_H; y0 += BAND) {
        int y1 = y0 + BAND < S32_SCREEN_H ? y0 + BAND : S32_SCREEN_H;
        s32_render_rows(m, band, S32_SCREEN_W, y0, y1);
        for (int y = y0; y < y1; y++) {
            const uint32_t *src = band + (uint32_t)(y - y0) * S32_SCREEN_W;
            uint32_t *dst = (uint32_t *)(fb->base + (uint32_t)y * fb->pitch);
            if (fb->is_rgb) {
                for (uint32_t x = 0; x < S32_SCREEN_W; x++) {
                    uint32_t c = src[x];
                    dst[x] = (c >> 16 & 0xFF) | (c & 0xFF00) | (c & 0xFF) << 16 | 0xFF000000u;
                }
            } else {
                for (uint32_t x = 0; x < S32_SCREEN_W; x++)
                    dst[x] = src[x] | 0xFF000000u;
            }
        }
    }
}

void s32_play(framebuffer_t *fb, const uint8_t *data, size_t len,
              uint32_t seconds, int attract, s32_play_stats_t *st)
{
    static s32_cart_t cart;
    static s32_machine_t m;
    char err[48];

    memset(st, 0, sizeof *st);
    st->status = -1;
    if (s32_cart_parse(data, len, &cart, err, sizeof err) != 0) {
        kprintf("s32: bad cartridge: %s\n", err);
        return;
    }
    memcpy(st->title, cart.title, sizeof st->title);
    if (cart.code_type != 0) {
        kprintf("s32: code type %u not supported yet\n", cart.code_type);
        return;
    }
    if (cart.screen_mode != 0)       /* decided in s32-bm33.md, not implemented yet */
        kprintf("s32: 16:9 mode not supported yet, playing at 320x224\n");
    if (!machine_mem && !(machine_mem = malloc(S32_MEM_SIZE))) {
        kprintf("s32: out of memory\n");
        return;
    }
    s32_init(&m, machine_mem);
    s32_install(&m, &cart);

    const uint32_t con_w = fb->width, con_h = fb->height;
    console_suspend(1);
    if (fb_init(fb, S32_SCREEN_W, S32_SCREEN_H, 2) != 0) {
        fb_init(fb, con_w, con_h, 2);
        console_suspend(0);
        kprintf("s32: cannot set 320x224\n");
        return;
    }

    audio_reset();
    audio_use_regs(machine_mem + S32_APU_BASE);     /* the APU is the synth's registers */

    uint8_t hold[5] = { 0 };
    int esc = 0, seen_serial = !attract;
    uint32_t start = timer_ticks(), deadline = start + TICK_US, prev = start;

    for (;;) {
        if (poll_keys(hold, &esc, &seen_serial))
            break;
        int quit = 0;
        uint32_t pad = input_buttons(&quit);
        if (quit)
            break;
        if (pad)
            seen_serial = 1;
        if (timer_ticks() - start >= seconds * 1000000u)
            break;

        uint8_t in[8] = { 0 };
        if (seen_serial) {
            for (int b = 0; b < 5; b++)
                if (hold[b]) { in[0] |= 1u << b; hold[b]--; }
            if (pad & HID_UP)    in[0] |= 1u << 0;
            if (pad & HID_DOWN)  in[0] |= 1u << 1;
            if (pad & HID_LEFT)  in[0] |= 1u << 2;
            if (pad & HID_RIGHT) in[0] |= 1u << 3;
            if (pad & HID_A)     in[0] |= 1u << 4;
        } else {
            in[0] = attract_input(st->ticks);
        }

        uint32_t t0 = timer_ticks();
        enum s32_status s = s32_tick(&m, in);
        uint32_t t1 = timer_ticks();
        st->cpu_us += t1 - t0;
        st->instructions += m.steps;
        st->status = s;
        st->ticks++;
        if (s != S32_OK)
            break;

        render_frame(&m, fb);
        st->render_us += timer_ticks() - t1;

        fb_flip(fb);
        while ((int32_t)(timer_ticks() - deadline) < 0)
            ;
        uint32_t now = timer_ticks();
        if (now - prev > TICK_US * 3 / 2)
            st->dropped++;
        prev = now;
        deadline += TICK_US;
        if ((int32_t)(now - deadline) > 0)
            deadline = now + TICK_US;
    }

    st->elapsed_us = timer_ticks() - start;
    st->attract = !seen_serial;
    audio_use_regs(0);
    audio_reset();
    input_flush();
    fb_init(fb, con_w, con_h, 2);
    console_suspend(0);
    if (st->status > 0)
        kprintf("s32: cartridge crashed: %s at pc %06lx\n",
                s32_status_str((enum s32_status)st->status), m.crash_pc);
}

void s32_play_print(const s32_play_stats_t *st)
{
    if (st->status < 0 || st->ticks == 0)
        return;
    uint32_t ms = st->elapsed_us / 1000;
    uint32_t fps10 = ms ? st->ticks * 10000u / ms : 0;
    uint32_t ns_per_insn = st->instructions ? (uint32_t)((uint64_t)st->cpu_us * 1000 / st->instructions) : 0;
    kprintf("s32: \"%s\" %lu ticks, %lu.%lu fps%s, %lu dropped\n",
            st->title, st->ticks, fps10 / 10, fps10 % 10,
            st->attract ? " (attract)" : "", st->dropped);
    kprintf("     cpu %lu us/tick (%lu ns/insn), render %lu us/tick\n",
            st->cpu_us / st->ticks, ns_per_insn, st->render_us / st->ticks);
}
