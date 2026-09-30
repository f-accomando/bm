#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Blocking read of one key from the serial console or the USB keyboard.
 * While waiting it polls USB and refreshes the uptime in the status bar. */
char input_getc(void);

/* Non-blocking: next key from serial or USB keyboard, or -1. */
int input_key(void);
/* For the tools opened from the menu, input_key also turns controller
 * buttons into keys: B or Start is Esc; with INPUT_PAD_NAV (the pager) also
 * up/down arrows, left/right PgUp/PgDn, A Enter. 0 turns it off. */
#define INPUT_PAD_ESC 1
#define INPUT_PAD_NAV 2
void input_pad_keys(int mode);
/* Keys typed on a remote terminal: the serial port or the network
 * console (bm33_net.py); -1 if none. Menus and games read them as text. */
int input_remote_getc(void);
int input_remote_ready(void);
/* After a remote Esc: does more follow within 30 ms (an arrow key)? */
int input_remote_follows(void);
/* After an Esc from input_getc: if a terminal sequence follows (an arrow,
 * PgUp: ESC [ ... final letter or ~), reads it all; returns 1. A lone Esc
 * (the USB keyboard's): 0, nothing read. */
int input_skip_sequence(void);
/* A line typed on the keyboard, the serial port or the network console,
 * echoed (as '*' if secret); Enter ends it. Its length, -1 on Esc. */
int input_read_line(char *buf, int max, int secret);
/* What input_key returns once when a cartridge sent over the network
 * (bm33_net.py --play) waits to be played: the monitor plays it. */
#define INPUT_NET_PLAY 0x10

/* Polls USB and returns the game buttons held on the USB keyboard or
 * gamepad (HID_* bits); *quit is set once per Esc / Start+Select. */
uint32_t input_buttons(int *quit);

/* The same, gamepads only (the keyboard types text). */
uint32_t input_pad_buttons(int *quit);

#define INPUT_PLAYERS 4

/* Local multiplayer (M16): Bluetooth pad n is player n; the USB keyboard or
 * gamepad and the serial console play as the first player without a pad
 * (0-based here), or -1 when all four players have one. */
int input_local_player(void);

/* Like input_buttons, per player: out[0] = player 1. Returns the OR of all;
 * *local = input_local_player(). text: the keyboard types (left out). */
uint32_t input_players(uint32_t out[INPUT_PLAYERS], int text, int *quit, int *local);

/* Players with a controller: bit n = player n+1. The local player counts
 * when a USB keyboard or gamepad is attached, or when no pad is connected
 * (then it is the serial console, or nobody yet). */
unsigned input_connected(void);

/* Left stick of player p (0-based), -1..1 each (x right, y down); from the
 * direction buttons for keyboards and pads without a stick. */
void input_stick(int p, uint32_t buttons, float *x, float *y);

/* What player p (0-based) plays with: a Bluetooth pad or a USB gamepad,
 * a USB keyboard, or nothing (the serial console does not count);
 * INPUT_DEV_BLUETOOTH is added when it comes over Bluetooth. */
#define INPUT_DEV_NONE      0
#define INPUT_DEV_KEYBOARD  1
#define INPUT_DEV_PAD       2
#define INPUT_DEV_BLUETOOTH 0x10
int input_device(int p);
/* "pads: 1 2 - -" for status lines. */
void input_status(char *buf, unsigned size);

/* Monitor 'Y': the buttons each player holds, live for `seconds`. */
void input_live_test(uint32_t seconds);

/* Drops keys typed on the USB keyboard while a game was running. */
void input_flush(void);

#endif
