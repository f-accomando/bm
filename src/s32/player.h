#ifndef S32_PLAYER_H
#define S32_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/fb.h"

typedef struct {
    char title[65];
    uint32_t ticks;
    uint32_t elapsed_us;
    uint64_t instructions;
    uint32_t cpu_us;        /* total time in s32_tick */
    uint32_t render_us;     /* total time in s32_render + conversion */
    uint32_t dropped;
    int status;             /* enum s32_status of the last tick, or -1 = bad cart */
    int attract;            /* input came from the built-in script */
} s32_play_stats_t;

/*
 * Runs a .cart on the screen at 320x224 (scaled by the GPU), 60 ticks per
 * second, then restores the 640x360 console.
 * Input from the serial port: w/a/s/d or arrow keys, space = action,
 * q = quit. With `attract` set, until a key arrives a built-in script
 * moves the player (boot demo without a serial cable). Stops after `seconds`, on 'q', or when the cart crashes.
 */
void s32_play(framebuffer_t *fb, const uint8_t *cart, size_t len,
              uint32_t seconds, int attract, s32_play_stats_t *st);
void s32_play_print(const s32_play_stats_t *st);

#endif
