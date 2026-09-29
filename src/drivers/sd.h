/*
 * SD card, polled PIO: SDHOST controller, or the Arasan EMMC one as a
 * fallback (see sd.c). Blocks are 512 bytes.
 */
#ifndef SD_H
#define SD_H

#include <stdint.h>

/* Initialises controller and card. Returns 0 on success. */
int sd_init(void);

/* Reads `count` 512-byte blocks starting at `lba`. Returns 0 on success. */
int sd_read(uint32_t lba, uint32_t count, void *buf);

/* Writes `count` blocks; returns when the card has programmed them. */
int sd_write(uint32_t lba, uint32_t count, const void *buf);

/* Card size in 512-byte blocks (0 if unknown); sd_is_hc: 1 if SDHC/SDXC. */
uint32_t sd_blocks(void);
int      sd_is_hc(void);

/* Human-readable reason of the last failure. */
const char *sd_error(void);

/* "sdhost" or "emmc": the controller in use. */
const char *sd_controller(void);

#endif
