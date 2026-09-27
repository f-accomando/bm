#ifndef UART_H
#define UART_H

#include <stdint.h>

#ifndef UART_BAUD
#define UART_BAUD 115200
#endif

/* Serial console on GPIO14 (TXD, pin 8) / GPIO15 (RXD, pin 10): the PL011
 * UART0 after uart_init, the mini UART after uart_use_mini (when the PL011
 * is given to the Bluetooth chip). Same pins and baud rate either way. */
void     uart_init(void);
void     uart_use_mini(void);
int      uart_is_mini(void);
uint32_t uart_clock(void);
void     uart_putc(char c);
void     uart_puts(const char *s);
void     uart_write(const void *buf, uint32_t len);
int      uart_rx_ready(void);
char     uart_getc(void);
/* Returns 1 and stores the byte if one arrives within timeout_us. */
int      uart_getc_timeout(uint32_t timeout_us, char *c);
void     uart_flush(void);

#endif
