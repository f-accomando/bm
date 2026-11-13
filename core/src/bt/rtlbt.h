/*
 * Realtek Bluetooth firmware (rtlbt.c): the RTL8821CS of the RGB30.
 */
#ifndef RTLBT_H
#define RTLBT_H

#include <stddef.h>
#include <stdint.h>

/* The image for the chip: the patch for rom_version (vendor command 0xFC6D)
 * with the firmware version in its last 4 bytes, then the config file.
 * *out is malloc'd. 0, or -1 with the reason in *err. */
int rtlbt_patch(const uint8_t *fw, size_t fwlen, const uint8_t *cfg, size_t cfglen,
                unsigned rom_version, uint8_t **out, size_t *outlen, const char **err);

/* The UART entry of the config file: the word for vendor command 0xFC17,
 * the baud rate it means and whether flow control goes on. 0, or -1. */
int rtlbt_uart_config(const uint8_t *cfg, size_t cfglen, uint32_t *word, uint32_t *baud, int *flow);

/* Sends the image with 0xFC20 (hci_cmd). 0, -1 no answer, -2 refused;
 * *fragments: how many went through. */
int rtlbt_download(const uint8_t *img, size_t len, unsigned *fragments);

#endif
