/* RTL8821C MAC and radio set-up (rtw_init.c) and its tables (rtw8821c_table.c). */
#ifndef RTW_PHY_H
#define RTW_PHY_H

#include <stdint.h>

extern const uint32_t rtw8821c_mac[], rtw8821c_agc[], rtw8821c_agc_btg_type2[],
                      rtw8821c_bb[], rtw8821c_rf_a[];
extern const unsigned rtw8821c_mac_len, rtw8821c_agc_len, rtw8821c_agc_btg_type2_len,
                      rtw8821c_bb_len, rtw8821c_rf_a_len;

/* After rtw_chip_start: queues, FIFO pages, H2C queue, MAC, BB/RF tables,
 * WiFi-only antenna, security engine, RX filter. 0 or -1 (err). */
int rtw_init_radio(char *err, unsigned errlen);
/* 2.4 GHz channel 1..14, 20 MHz */
void rtw_set_channel(unsigned ch);
uint32_t rtw_read_rf(uint32_t addr, uint32_t mask);
void rtw_write_rf(uint32_t addr, uint32_t mask, uint32_t data);
/* receive every beacon (scan) or only those of our BSS */
void rtw_rx_all_bss(int all);
/* H2C mailbox command (8 bytes: cmd id in byte 0) */
int rtw_h2c_box(const uint8_t cmd[8]);
/* H2C packet (sub-id, payload of up to 24 bytes after the 8-byte header) */
int rtw_h2c_pkt(uint16_t sub_id, const uint8_t *payload, unsigned len);
/* TX power index for every rate, from the efuse (path A) */
void rtw_set_tx_power(unsigned ch);

#endif
