#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Blocking read of one key from the serial console or the USB keyboard.
 * While waiting it polls USB and refreshes the uptime in the status bar. */
char input_getc(void);

/* Non-blocking: next key from serial or USB keyboard, or -1. */
int input_key(void);

/* Polls USB and returns the game buttons held on the USB keyboard or
 * gamepad (HID_* bits); *quit is set once per Esc / Start+Select. */
uint32_t input_buttons(int *quit);

/* Drops keys typed on the USB keyboard while a game was running. */
void input_flush(void);

#endif
