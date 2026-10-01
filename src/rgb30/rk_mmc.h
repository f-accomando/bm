/*
 * Synopsys DesignWare MSHC (Rockchip "dw-mshc") in polled PIO mode: what
 * the SD card (SDMMC0, rk_sd.c) and the WiFi chip's SDIO (SDMMC2,
 * rk_sdio.c) have in common. The CRU gives each controller 24 MHz, which
 * it halves internally: 12 MHz in, 400 kHz with divider 15, 12 MHz with 0.
 */
#ifndef RK_MMC_H
#define RK_MMC_H

#include <stdint.h>

typedef struct {
    uintptr_t base;
    char err[64];
} dwmmc_t;

/* command flags */
#define MMC_RESP    (1u << 6)
#define MMC_LONG    (1u << 7)
#define MMC_CRC     (1u << 8)
#define MMC_DATA    (1u << 9)
#define MMC_WRITE   (1u << 10)
#define MMC_STOP    (1u << 14)    /* stop/abort command (CMD12) */
#define MMC_INIT    (1u << 15)

#define MMC_R1      (MMC_RESP | MMC_CRC)
#define MMC_R2      (MMC_RESP | MMC_LONG | MMC_CRC)
#define MMC_R3      (MMC_RESP)

/* Resets the controller for PIO (no DMA, no interrupts), card clock off.
 * 0, or -1 with h->err set. */
int dwmmc_reset(dwmmc_t *h);
/* Card clock = 12 MHz / (2 * div), or 12 MHz with div 0. */
int dwmmc_clock(dwmmc_t *h, uint32_t div);
void dwmmc_bus_width(dwmmc_t *h, int four_bits);
/* Sends a command (data commands: after dwmmc_data_setup). 0, -2 when the
 * card did not answer, -1 on other errors (h->err). resp gets 1 or 4 words
 * (long: resp[0] = bits 127..96). */
int dwmmc_cmd(dwmmc_t *h, uint32_t idx, uint32_t arg, uint32_t flags, uint32_t *resp);
void dwmmc_data_setup(dwmmc_t *h, uint32_t blksz, uint32_t bytes);
/* Moves the data of the command just sent: len bytes (a multiple of 4),
 * buf 4-byte aligned. 0, or -1. */
int dwmmc_data(dwmmc_t *h, int write, uint32_t *buf, uint32_t len);
/* 1 while the card holds DAT0 low (busy) */
int dwmmc_busy(dwmmc_t *h);
int dwmmc_card_present(dwmmc_t *h);

#endif
