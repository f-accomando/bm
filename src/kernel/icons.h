/*
 * Status icons of the menu bar (M27): keyboard, controller (DualShock 4
 * style), mouse (M32, an outline), WiFi, Ethernet, the battery (the
 * RGB30), all in the same box. Drawn once from shapes with
 * anti-aliasing (4x4 samples per pixel) into coverage masks; the player
 * number goes on a small disc over the bottom middle, with a gap cut
 * around it so the icon stays readable (white for USB, blue for
 * Bluetooth in the menu). Plain C, no kernel dependencies
 * (a host program can preview them).
 */
#ifndef ICONS_H
#define ICONS_H

#include <stdint.h>

#define ICON_W   27                 /* the icon box */
#define ICON_H   18
#define ICON_BH  25                 /* with the number disc below */

enum { ICON_KEYBOARD, ICON_PAD, ICON_WIFI, ICON_ETHERNET, ICON_MOUSE, ICON_COUNT };

/* num: the disc of the numbers without a number (the Bluetooth mouse;
 * any icon can have it) */
#define ICON_DOT 5

/* `icon` with the number `num` (1..4, 0: none, ICON_DOT): three planes of ICON_W x
 * ICON_BH, row after row. The icon's coverage (0..255) with the gap around
 * the disc already cut, the disc's coverage, and the digit (1 on its
 * pixels, all inside the disc). The caller picks the colours: a white or
 * a blue disc. Made on first use. */
typedef struct {
    uint8_t icon[ICON_W * ICON_BH];
    uint8_t disc[ICON_W * ICON_BH];
    uint8_t digit[ICON_W * ICON_BH];
} icon_mask_t;

const icon_mask_t *icon_mask(int icon, int num);

/* The battery (the RGB30's bar): its outline with `bars` (0..4) of charge
 * inside, or a bolt instead when `charging`; no disc. */
const icon_mask_t *icon_battery(int bars, int charging);

#endif
