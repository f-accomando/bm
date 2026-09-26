/*
 * SD card on the BCM2835 EMMC controller (Arasan SDHCI), polled PIO,
 * read only. Blocks are 512 bytes.
 */
#ifndef SD_H
#define SD_H

#include <stdint.h>

/* Initialises controller and card. Returns 0 on success. */
int sd_init(void);

/* Reads `count` 512-byte blocks starting at `lba`. Returns 0 on success. */
int sd_read(uint32_t lba, uint32_t count, void *buf);

/* Card size in 512-byte blocks (0 if unknown), 1 if SDHC/SDXC. */
uint32_t sd_blocks(void);
int      sd_is_hc(void);

/* Human-readable reason of the last failure. */
const char *sd_error(void);

#endif
