/*
 * The pointer (M32): one arrow on the screen, moved by a mouse (USB,
 * Bluetooth classic or Bluetooth LE). Never by a controller (the user,
 * 2026-10-05: the right stick and R2 showed it as if a mouse were there).
 *
 * - The whole system can go without it: mouse=off in bm/config.txt (there
 *   is no menu entry: the user does not turn it off). On by default.
 * - It belongs to the screen in front: the menu always has it, a
 *   cartridge only when it asks (mouse(true)); elsewhere it is not there.
 * - It is hidden when no mouse is there; the menu hides it again when the
 *   keys or the cross move the selection (any motion brings it back).
 */
#ifndef POINTER_H
#define POINTER_H

#include <stdint.h>

/* Reads mouse= from bm/config.txt (off, 0 or no: no pointer anywhere). */
void pointer_config(void);
int  pointer_enabled(void);

/* The screen that has the pointer now, w x h pixels; on = 0: nobody (the
 * position is kept, in proportion, for the next one). */
void pointer_env(int on, int w, int h);
int  pointer_env_on(void);

typedef struct {
    int x, y;                   /* in the screen's pixels */
    uint8_t buttons;            /* held: bit 0 left, 1 right, 2 middle */
    uint8_t pressed;            /* pressed this frame (only while shown) */
    uint8_t released;
    int wheel, pan;             /* steps this frame: up / right positive */
    int moved;                  /* it moved this frame */
    int shown;                  /* on screen: enabled, wanted, something moves it, active */
    int available;              /* a mouse is connected */
} pointer_t;

/* Once per frame, by the screen that has it: the mice since
 * the last call. Also when nobody has it (the motion is dropped). */
const pointer_t *pointer_update(void);
const pointer_t *pointer_get(void);

/* The keys or the cross moved the selection: hidden until it moves. */
void pointer_hide(void);

/* The mice connected, for the bar: bit 0 USB, bit 1 Bluetooth (0 when
 * the pointer is off in the config). */
#define POINTER_USB       1
#define POINTER_BLUETOOTH 2
unsigned pointer_devices(void);

/* The arrow over a w x h RGB565 frame, where the pointer is, if shown:
 * 12x20 pixels, or 8x12 on screens less than 288 pixels high. */
void pointer_draw(uint16_t *px, uint32_t stride, int w, int h);

#endif
