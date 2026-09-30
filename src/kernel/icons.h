/*
 * Status icons of the menu bar (M27): keyboard, controller (DualShock 4
 * style), WiFi, Ethernet, all in the same box. Drawn once from shapes with
 * anti-aliasing (4x4 samples per pixel) into coverage masks; the player
 * number goes on a small disc over the bottom middle, with a gap cut
 * around it so the icon stays readable. Plain C, no kernel dependencies
 * (a host program can preview them).
 */
#ifndef ICONS_H
#define ICONS_H

#include <stdint.h>

#define ICON_W   27                 /* the icon box */
#define ICON_H   18
#define ICON_BH  25                 /* with the number disc below */

enum { ICON_KEYBOARD, ICON_PAD, ICON_WIFI, ICON_ETHERNET, ICON_COUNT };

/* Coverage (0..255) of `icon` with the number `num` (1..4, 0: none) over
 * the background: ICON_W x ICON_BH, row after row. Made on first use. */
const uint8_t *icon_mask(int icon, int num);

#endif
