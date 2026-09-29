/*
 * The two SD card drivers behind sd.h: SDHOST (default, leaves the Arasan
 * controller to the WiFi chip) and the Arasan EMMC one (fallback).
 */
#ifndef SD_BACKEND_H
#define SD_BACKEND_H

#include <stdint.h>

int emmc_sd_init(void);
int emmc_sd_read(uint32_t lba, uint32_t count, void *buf);
int emmc_sd_write(uint32_t lba, uint32_t count, const void *buf);
uint32_t emmc_sd_blocks(void);
int emmc_sd_is_hc(void);
const char *emmc_sd_error(void);

int sdhost_init(void);
int sdhost_read(uint32_t lba, uint32_t count, void *buf);
int sdhost_write(uint32_t lba, uint32_t count, const void *buf);
uint32_t sdhost_blocks(void);
int sdhost_is_hc(void);
const char *sdhost_error(void);

#endif
