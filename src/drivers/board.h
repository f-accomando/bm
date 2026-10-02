/*
 * Which Raspberry Pi we run on, from the firmware's revision code. One
 * kernel for every BCM2835 board (kernel.img): the Pi Zero / Zero W (the
 * reference) and the Pi 1 (A, B, A+, B+), whose USB ports sit behind a
 * LAN951x hub with the Ethernet controller (A and A+: one port, no hub).
 * The Pi Zero 2 W (BCM2710A1) has its own build, kernel7.img (BM_ZERO2);
 * the Pi 2 B is known for QEMU's raspi2b, where kernel7.img is tested.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

enum board_model {
    BOARD_UNKNOWN, BOARD_PI1_A, BOARD_PI1_B, BOARD_PI1_APLUS, BOARD_PI1_BPLUS,
    BOARD_CM1, BOARD_ZERO, BOARD_ZERO_W, BOARD_ZERO_2W, BOARD_PI2_B, BOARD_OTHER,
};

/* The SoC this kernel is built for, and where the ACT LED is until the
 * board is known (on the Zero 2 W GPIO 47 is the power chip's I2C). */
#ifdef BM_ZERO2
#define BOARD_SOC       "BCM2710A1"
#define BOARD_LED_PIN   29
#else
#define BOARD_SOC       "BCM2835"
#define BOARD_LED_PIN   47
#endif

typedef struct {
    uint32_t revision;          /* as the firmware gives it */
    uint8_t model;              /* enum board_model */
    uint8_t led_pin;            /* ACT LED GPIO, 0 = none */
    uint8_t led_active_high;
    uint8_t wireless;           /* may have the WiFi/Bluetooth chip (Zero W, 2 W) */
    uint8_t bt_on_pin;          /* its BT_REG_ON: GPIO 45 (Zero W), 42 (Zero 2 W) */
    uint8_t ethernet;           /* LAN951x: USB hub + Ethernet (Pi 1 B, B+, Pi 2 B) */
    char name[28];              /* "Pi 1 B rev 2.0", "Pi Zero W" */
} board_t;

/* Decodes a revision code; no hardware access (tested on the PC). */
void board_decode(uint32_t revision, board_t *b);

/* The board we run on: the revision is asked to the firmware once. */
const board_t *board(void);

#endif
