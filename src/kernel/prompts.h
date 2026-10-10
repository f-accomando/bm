/*
 * Button prompts (bm-ui): the buttons of a DualShock 4, of a generic pad
 * and the keys of a keyboard as small flat icons, in the style of the
 * menu's status icons (icons.c): shapes with anti-aliasing, whole-pixel
 * edges wherever possible.
 *
 * Two sets of the same buttons:
 * - the menu's: raised buttons, a white face 14 px high on a grey lip 2 px
 *   deep, the symbol or the label cut out of the face (what is behind
 *   shows through). The four face buttons of the DS4 also come in colour:
 *   a dark face with a grey rim and lip, the symbol in its colour (cross
 *   blue, circle red, square pink, triangle green).
 * - the apps' (the Dev tools): flat chips filled with a colour, the label
 *   cut out; 16 px high for text rows of 8x16, 12 for 6x12. The DS4's face
 *   buttons in their colours, the lettered pads' A B X Y in theirs (green,
 *   red, blue, yellow), the pad's other buttons light grey, the keyboard's
 *   keys amber (the apps' accent).
 * The RGB30's A B X Y have one look in both sets: a dark button with the
 * letter in its colour, as on the console (A green, B blue, X red, Y
 * yellow); raised on its lip in the menu, flat in the apps.
 * Labels use the console fonts: one character 8x16 bold (6x12 bold at 12
 * px), words 6x12.
 *
 * Plain C, no kernel dependencies (a host program can preview them).
 */
#ifndef PROMPTS_H
#define PROMPTS_H

#include <stdint.h>

#define PROMPT_H       16           /* the face 14 px, the lip 2 px under it */
#define PROMPT_SMALL_H 12           /* the apps' small chips: a face 10 px high */
#define PROMPT_MAX_W   56

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
    /* the RGB30's own face buttons (Nintendo layout: X top, A right, B
     * bottom, Y left): a dark button, the letter in its colour (A green,
     * B blue, X red, Y yellow), in both sets */
    PROMPT_RGB30_A = PROMPT_KEY_F1 + 12, PROMPT_RGB30_B, PROMPT_RGB30_X, PROMPT_RGB30_Y,
    PROMPT_COUNT
};

/* The RGB30's button for a lettered pad's (PROMPT_PAD_A..PROMPT_PAD_Y);
 * any other prompt as it is. */
static inline int prompt_rgb30(int id)
{
    return id >= PROMPT_PAD_A && id <= PROMPT_PAD_Y ? id - PROMPT_PAD_A + PROMPT_RGB30_A : id;
}

/* A prompt: w x h pixels, 0xAARRGGBB, not premultiplied (the caller
 * blends them over its background). */
typedef struct {
    int w, h;
    const uint32_t *px;
} prompt_t;

/* The prompt `id` (PROMPT_*); colour != 0 picks the coloured face buttons
 * of the DS4 (the other prompts have one look). NULL if out of range or
 * out of memory. Made on first use, then kept. */
const prompt_t *prompt_get(int id, int colour);

/* A keyboard key with the character c (33..126; letters in upper case),
 * 16 px wide. NULL for other characters. */
const prompt_t *prompt_key(int c);

/* The apps' chips: the same prompts and keys, 16 px high, or 12 with
 * small != 0. */
const prompt_t *prompt_chip(int id, int small);
const prompt_t *prompt_chip_key(int c, int small);

#endif
