/*
 * H5 (three-wire UART) transport for the HCI layer: the RGB30's Realtek
 * RTL8821CS. h5_open() establishes the link (SYNC, CONFIG); then
 * hci_set_transport(&h5_transport) and hci.c works as over H4.
 */
#ifndef H5_H
#define H5_H

#include <stdint.h>
#include "hci.h"

/* 0 when the link is active; -1 no answer, -2 SYNC answered but no CONFIG */
int  h5_open(uint32_t timeout_ms);
int  h5_active(void);
/* config byte the controller sent in CONFIG RESP (-1: none) */
int  h5_peer_config(void);
void h5_stats(unsigned *retransmits, unsigned *crc_errors, unsigned *bad_headers);
/* the CRC of a frame (header + payload), as sent after it, MSB first */
uint16_t h5_crc(const uint8_t *p, unsigned n);

extern const hci_transport_t h5_transport;

#endif
