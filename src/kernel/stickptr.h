/*
 * The stick as a pointer (RGB30, D05): the right stick moves a point on the
 * cartridge's screen, R3 and L3 are its two buttons. Pure logic, no hardware
 * and no config access; the runtime (stickmouse()) and Settings
 * (stick_mouse=on|off) use it. The kernel pointer (pointer.h) stays for a
 * real mouse.
 */
#ifndef BM_STICKPTR_H
#define BM_STICKPTR_H

#include <stdint.h>

typedef struct { float x, y; uint8_t buttons, pressed; } stickptr_t;

/* "on" -> 1; NULL, "off" or anything else -> 0 */
int stickptr_parse(const char *v);
/* in the middle of w x h, no buttons */
void stickptr_reset(stickptr_t *s, int w, int h);
/* one frame: sx, sy the right stick (-1..1), pad the raw buttons
 * (32768 = button 1, 16384 = button 2) */
void stickptr_step(stickptr_t *s, float sx, float sy, uint32_t pad, int w, int h);

#endif
