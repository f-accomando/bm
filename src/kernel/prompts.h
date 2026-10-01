/*
 * Button prompts (bm-ui): the buttons of a DualShock 4, of a generic pad
 * and the keys of a keyboard as small flat icons, in the style of the
 * menu's status icons (icons.c): shapes with anti-aliasing, whole-pixel
 * edges wherever possible.
 *
 * Every prompt is a raised button: a white face 14 px high on a grey lip
 * 2 px deep, the symbol or the label cut out of the face (what is behind
 * shows through). The four face buttons of the DS4 also come in colour: a
 * dark face with a grey rim and lip, the symbol in its colour (cross blue,
 * circle red, square pink, triangle green). Labels use the console fonts:
 * 8x16 bold for one character, 6x12 for words.
 *
 * Plain C, no kernel dependencies (a host program can preview them).
 */
#ifndef PROMPTS_H
#define PROMPTS_H

#include <stdint.h>

#define PROMPT_H      16            /* the face 14 px, the lip 2 px under it */
#define PROMPT_MAX_W  56

enum {
    /* DualShock 4 */
    PROMPT_CROSS, PROMPT_CIRCLE, PROMPT_SQUARE, PROMPT_TRIANGLE,
    PROMPT_DPAD,                            /* the cross, every direction */
    PROMPT_DPAD_UP, PROMPT_DPAD_DOWN, PROMPT_DPAD_LEFT, PROMPT_DPAD_RIGHT,
    PROMPT_DPAD_UPDOWN, PROMPT_DPAD_LEFTRIGHT,
    PROMPT_L1, PROMPT_R1, PROMPT_L2, PROMPT_R2,
    PROMPT_L3, PROMPT_R3,                   /* the sticks pressed */
    PROMPT_LSTICK, PROMPT_RSTICK,           /* the sticks moved */
    PROMPT_OPTIONS, PROMPT_SHARE, PROMPT_PS, PROMPT_TOUCHPAD,
    /* a generic pad (Xbox 360, HID gamepads): lettered buttons */
    PROMPT_PAD_A, PROMPT_PAD_B, PROMPT_PAD_X, PROMPT_PAD_Y,
    PROMPT_PAD_START, PROMPT_PAD_SELECT,
    /* keyboard; the keys with a character: prompt_key() */
    PROMPT_KEY_UP, PROMPT_KEY_DOWN, PROMPT_KEY_LEFT, PROMPT_KEY_RIGHT,
    PROMPT_KEY_ENTER, PROMPT_KEY_ESC, PROMPT_KEY_SPACE, PROMPT_KEY_TAB,
    PROMPT_KEY_BACKSPACE, PROMPT_KEY_SHIFT, PROMPT_KEY_CTRL, PROMPT_KEY_ALT,
    PROMPT_KEY_DEL, PROMPT_KEY_HOME, PROMPT_KEY_END, PROMPT_KEY_PGUP, PROMPT_KEY_PGDN,
    PROMPT_KEY_F1,                          /* .. F12: PROMPT_KEY_F1 + 11 */
    PROMPT_COUNT = PROMPT_KEY_F1 + 12
};

/* A prompt: w x PROMPT_H pixels, 0xAARRGGBB, not premultiplied (the
 * caller blends them over its background). */
typedef struct {
    int w;
    const uint32_t *px;
} prompt_t;

/* The prompt `id` (PROMPT_*); colour != 0 picks the coloured face buttons
 * of the DS4 (the other prompts have one look). NULL if out of range or
 * out of memory. Made on first use, then kept. */
const prompt_t *prompt_get(int id, int colour);

/* A keyboard key with the character c (33..126; letters in upper case),
 * 16 px wide. NULL for other characters. */
const prompt_t *prompt_key(int c);

#endif
