/*
 * PL011 UART0 wired to the Bluetooth chip (BCM43438) of the Pi Zero W:
 * GPIO30 CTS, 31 RTS, 32 TXD, 33 RXD (ALT3), hardware flow control.
 * The serial console must have moved to the mini UART first.
 */
#ifndef BTUART_H
#define BTUART_H

#include <stdint.h>

void btuart_init(uint32_t baud);
void btuart_set_baud(uint32_t baud);
void btuart_write(const void *buf, uint32_t len);
/* One byte, or -1 after timeout_us. */
int  btuart_read(uint32_t timeout_us);
/* 1 if a byte is waiting. */
int  btuart_ready(void);
/* Drops whatever is waiting in the receive FIFO. */
void btuart_drain(void);

#endif
