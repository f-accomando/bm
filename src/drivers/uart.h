#ifndef UART_H
#define UART_H

#include <stdint.h>

#ifndef UART_BAUD
#define UART_BAUD 115200
#endif

/* PL011 UART0 on GPIO14 (TXD, pin 8) / GPIO15 (RXD, pin 10). */
void     uart_init(void);
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
