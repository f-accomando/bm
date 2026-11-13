/*
 * Realtek RTL8821CS WiFi on the RGB30 (SDIO, rk_sdio.c): a bare-metal port
 * of the parts of Linux's rtw88 driver a station needs. rtw88 is
 * GPL-2.0 OR BSD-3-Clause, Copyright(c) 2018-2019 Realtek Corporation,
 * SDIO parts Copyright (C) Martin Blumenstingl, Jernej Skrabec; used here
 * under BSD-3-Clause (docs/RGB30.md).
 */
#ifndef RTW_H
#define RTW_H

#include <stddef.h>
#include <stdint.h>

/* --- registers (rtw88 reg.h, sdio.h) --- */
#define REG_SYS_FUNC_EN     0x0002
#define REG_SYS_CLK_CTRL    0x0008
#define REG_RSV_CTRL        0x001c
#define REG_RF_CTRL         0x001f
#define REG_AFE_XTAL_CTRL   0x0024
#define REG_AFE_PLL_CTRL    0x0028
#define REG_EFUSE_CTRL      0x0030
#define REG_LDO_EFUSE_CTRL  0x0034
#define REG_GPIO_MUXCFG     0x0040
#define REG_LED_CFG         0x004c
#define REG_PAD_CTRL1       0x0064
#define REG_HCI_OPT_CTRL    0x0074
#define REG_MCUFW_CTRL      0x0080
#define REG_WLRF1           0x00ec
#define REG_SYS_CFG1        0x00f0
#define REG_CR              0x0100
#define REG_TXDMA_PQ_MAP    0x010c
#define REG_C2HEVT          0x01a0
#define REG_FIFOPAGE_CTRL_2 0x0204
#define REG_TXDMA_STATUS    0x0210
#define REG_RQPN_CTRL_2     0x022c
#define REG_FIFOPAGE_INFO_1 0x0230
#define REG_BCN_CTRL        0x0550
#define REG_CPU_DMEM_CON    0x1080
#define REG_FW_DBG7         0x10fc
#define REG_CR_EXT          0x1100
#define REG_DDMA_CH0SA      0x1200
#define REG_DDMA_CH0DA      0x1204
#define REG_DDMA_CH0CTRL    0x1208
#define REG_H2CQ_CSR        0x1330

/* SDIO local registers (bus address = offset, device id 0) */
#define SDIO_TX_CTRL        0x0000
#define SDIO_HIMR           0x0014
#define SDIO_HISR           0x0018
#define SDIO_RX0_REQ_LEN    0x001c
#define SDIO_FREE_TXPG      0x0020
#define SDIO_HRPWM1         0x0080
#define SDIO_HSUS_CTRL      0x0086

/* FIFO bus addresses (device ids 4..7) */
#define FIFO_HIGH           0x8000      /* beacon / reserved page, H2C */
#define FIFO_NORMAL         0xa000
#define FIFO_LOW            0xc000      /* data (best effort) */
#define FIFO_EXTRA          0xe000      /* management; also RX FIFO 0 */

#define TX_DESC_SIZE        48
#define RX_DESC_SIZE        24
#define QSEL_BEACON         0x10
#define QSEL_HIGH           0x11
#define QSEL_MGMT           0x12
#define QSEL_H2C            0x13

/* --- register access (rtw_io.c) --- */
uint8_t  rtw_r8(uint32_t addr);
uint16_t rtw_r16(uint32_t addr);
uint32_t rtw_r32(uint32_t addr);
void rtw_w8(uint32_t addr, uint8_t v);
void rtw_w16(uint32_t addr, uint16_t v);
void rtw_w32(uint32_t addr, uint32_t v);
static inline void rtw_set8(uint32_t a, uint8_t b)   { rtw_w8(a, rtw_r8(a) | b); }
static inline void rtw_clr8(uint32_t a, uint8_t b)   { rtw_w8(a, rtw_r8(a) & (uint8_t)~b); }
static inline void rtw_set32(uint32_t a, uint32_t b) { rtw_w32(a, rtw_r32(a) | b); }
static inline void rtw_clr32(uint32_t a, uint32_t b) { rtw_w32(a, rtw_r32(a) & ~b); }
void rtw_w32_mask(uint32_t addr, uint32_t mask, uint32_t v);
uint8_t  rtw_local_r8(uint32_t off);
uint32_t rtw_local_r32(uint32_t off);
void rtw_local_w8(uint32_t off, uint8_t v);
void rtw_local_w32(uint32_t off, uint32_t v);
/* 1 when (reg & mask) == want within 1000 x 10 us */
int rtw_poll32(uint32_t addr, uint32_t mask, uint32_t want);
void rtw_set_poweron(int on);       /* 32-bit access with CMD53 from now on */
int rtw_io_errors(void);

/* TX: a packet (48-byte descriptor + data) to a FIFO; buf 8-byte aligned,
 * the descriptor's checksum filled here. 0, or -1. */
typedef struct {
    uint32_t pkt_size;              /* bytes after the descriptor */
    uint8_t qsel, rate, rate_id, macid;
    uint8_t use_rate, dis_rate_fb, dis_qselseq, en_hwseq, bmc, offset;
    uint8_t sec_type;
    uint16_t seq;
} rtw_txinfo_t;
void rtw_fill_txdesc(uint8_t *desc, const rtw_txinfo_t *t);
int rtw_write_port(uint32_t fifo, uint8_t *buf, uint32_t len);
/* free pages of the queue behind fifo (own + public) */
int rtw_free_pages(uint32_t fifo);
/* RX: the bytes waiting (0 if none), and reading them */
uint32_t rtw_rx_len(void);
int rtw_read_port(uint8_t *buf, uint32_t len);
/* RX: drops len waiting bytes (more than the caller's buffer) */
int rtw_rx_discard(uint32_t len);

/* --- chip (rtw_mac.c) --- */
typedef struct {
    int powered, fw_running;
    unsigned cut;                   /* chip version (SYS_CFG1 15:12) */
    uint8_t mac[6];
    uint8_t rfe, pkg, xtal, thermal, swing_2g, channel_plan;
    uint8_t efuse[512];             /* logical map */
    unsigned fw_major, fw_minor, fw_patch;
    uint32_t hw_feature[4];
} rtw_chip_t;

extern rtw_chip_t rtw;

/* power on, firmware (fw/len: rtw8821c_fw.bin), efuse; 0 or -1 with the
 * reason in err */
int rtw_chip_start(const uint8_t *fw, size_t len, char *err, unsigned errlen);

#endif
