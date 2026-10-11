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

/* Since boot (2026-10-11, a one-off +502 ms stall seen on the Pi): the
 * transfers tried again after a failure, and the waits that ran out (each
 * one 0.5 s or more of the card stopping everything). The SDHOST driver's;
 * 0 on the other drivers. */
void sd_counters(uint32_t *retries, uint32_t *timeouts);

#endif
