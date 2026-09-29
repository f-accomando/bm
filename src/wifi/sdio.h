/*
 * SDIO host for the WiFi half of the BCM43438 (Arasan controller on
 * GPIO34-39), polled. M18.
 */
#ifndef SDIO_H
#define SDIO_H

#include <stdint.h>

/* Powers the chip, identifies it (CMD5, CMD3, CMD7), 4-bit bus at 25 MHz.
 * Each step is reported through `say` (value: a register or 0). */
int sdio_init(void (*say)(const char *step, int ok, uint32_t value));

/* CMD52: one byte of function `fn` at `addr`. */
int sdio_rw_byte(int write, unsigned fn, uint32_t addr, uint8_t in, uint8_t *out);
int sdio_read8(unsigned fn, uint32_t addr, uint8_t *out);
int sdio_write8(unsigned fn, uint32_t addr, uint8_t v);

/* Block sizes set by sdio_init; a byte-mode CMD53 carries at most one. */
#define SDIO_F1_BLOCK 64
#define SDIO_F2_BLOCK 512

/* CMD53 in byte mode: up to one block (multiple of 4); incr: the address
 * advances (memory) or not (FIFO). */
int sdio_rw_block(int write, unsigned fn, uint32_t addr, int incr, void *buf, uint32_t len);

const char *sdio_error(void);

#endif
