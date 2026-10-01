/*
 * The RGB30's controls: D-pad, A B X Y, L1 R1 L2 R2, Select, Start, the
 * stick clicks, volume. The serial port can press them too (tests, or a PC
 * on the console's UART):
 *   arrows or w a s d   D-pad          Enter   A        Backspace  B
 *   x  X    y  Y        l  L1   r  R1  L / R   L2 / R2
 *   Tab  Select         Space  Start   +  -    volume
 * Each key holds its button for PAD_SERIAL_MS.
 */
#ifndef RGB30_PAD_H
#define RGB30_PAD_H

#include <stdint.h>

enum {
    PAD_UP = 1u << 0, PAD_DOWN = 1u << 1, PAD_LEFT = 1u << 2, PAD_RIGHT = 1u << 3,
    PAD_A = 1u << 4, PAD_B = 1u << 5, PAD_X = 1u << 6, PAD_Y = 1u << 7,
    PAD_L1 = 1u << 8, PAD_R1 = 1u << 9, PAD_L2 = 1u << 10, PAD_R2 = 1u << 11,
    PAD_SELECT = 1u << 12, PAD_START = 1u << 13, PAD_L3 = 1u << 14, PAD_R3 = 1u << 15,
    PAD_VOLUP = 1u << 16, PAD_VOLDN = 1u << 17,
};
#define PAD_COUNT 18
#define PAD_SERIAL_MS 120

/* the button names, PAD_COUNT of them, bit order */
extern const char *const pad_names[PAD_COUNT];

/* Buttons held now (hardware and serial). Reads the serial port: a
 * character that is not a pad key is kept for pad_serial_char(). */
uint32_t pad_state(void);
/* Buttons pressed since the previous call (edges), with key repeat on the
 * D-pad after 400 ms. */
uint32_t pad_pressed(void);
/* The last serial character that was not a pad key (-1: none); '`' asks
 * for the Lua prompt. */
int pad_serial_char(void);

#endif
