/* SDIO on SDMMC2, function 1 (rk_sdio.c): the RTL8821CS's WiFi. */
#ifndef RK_SDIO_H
#define RK_SDIO_H

#include <stdint.h>

int  sdio_init(void);                   /* 0, or -1: sdio_error() */
int  sdio_ready(void);
const char *sdio_error(void);
int  sdio_cmd52(int write, unsigned fn, uint32_t addr, uint8_t data);
int  sdio_read8(uint32_t addr);         /* function 1; -1 on error */
int  sdio_write8(uint32_t addr, uint8_t v);
/* function 1, incrementing address; len a multiple of 4, buf 4-aligned */
int  sdio_cmd53(int write, uint32_t addr, void *buf, uint32_t len);

#endif
