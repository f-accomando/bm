/*
 * Which Raspberry Pi we run on, from the firmware's revision code. One
 * kernel for every BCM2835 board: the Pi Zero / Zero W (the reference) and
 * the Pi 1 (A, B, A+, B+), whose USB ports sit behind a LAN951x hub with
 * the Ethernet controller (A and A+: one port, no hub).
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

enum board_model {
    BOARD_UNKNOWN, BOARD_PI1_A, BOARD_PI1_B, BOARD_PI1_APLUS, BOARD_PI1_BPLUS,
    BOARD_CM1, BOARD_ZERO, BOARD_ZERO_W, BOARD_OTHER,
};

typedef struct {
    uint32_t revision;          /* as the firmware gives it */
    uint8_t model;              /* enum board_model */
    uint8_t led_pin;            /* ACT LED GPIO, 0 = none */
    uint8_t led_active_high;
    uint8_t wireless;           /* may have the WiFi/Bluetooth chip (Zero W) */
    uint8_t ethernet;           /* LAN951x: USB hub + Ethernet (Pi 1 B, B+) */
    char name[28];              /* "Pi 1 B rev 2.0", "Pi Zero W" */
} board_t;

/* Decodes a revision code; no hardware access (tested on the PC). */
void board_decode(uint32_t revision, board_t *b);

/* The board we run on: the revision is asked to the firmware once. */
const board_t *board(void);

#endif
