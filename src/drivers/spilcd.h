#ifndef SPILCD_DRV_H
#define SPILCD_DRV_H

#include <stdint.h>

/* 3.5" SPI TFT, 480x320, ILI9486 controller (the "RPi Display" type with an
 * XPT2046 touch chip on the same board). Standalone: nothing in the kernel
 * calls it. Only the picture is driven; the touch chip is left idle.
 *
 * Wiring on the 26-pin header (Pi 1 A/B, same on every 40-pin Pi):
 *   LCD CS    GPIO 8  (CE0, SPI0)     LCD DC    GPIO 24
 *   SCLK      GPIO 11                 LCD RESET GPIO 25
 *   MOSI      GPIO 10                 Touch CS  GPIO 7 (CE1), held high
 *   MISO      GPIO 9 (unused)         Touch IRQ GPIO 17 (unused)
 * Backlight: wired to 3.3 V on the board, no pin. */

#define SPILCD_W 480
#define SPILCD_H 320

/* Resets the panel, sends the ILI9486 start-up sequence, clears it to black
 * and turns the display on. Returns 0 on success. Call it once. */
int  spilcd_init(void);

/* Fills the whole screen with one RGB565 colour. */
void spilcd_fill(uint16_t rgb565);

/* Sends a w x h block of RGB565 pixels (row after row, `stride` pixels per
 * source row) to the screen at x,y. Clipped to the screen. */
void spilcd_blit(int x, int y, int w, int h, const uint16_t *src, int stride);

/* Packs 8-bit r,g,b into RGB565. */
static inline uint16_t spilcd_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#endif
