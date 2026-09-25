#include "uart.h"
#include "gpio.h"
#include "mmio.h"
#include "prop.h"
#include "timer.h"

#define UART0_BASE  (PERIPHERAL_BASE + 0x201000)
#define UART_DR     (UART0_BASE + 0x00)
#define UART_FR     (UART0_BASE + 0x18)
#define UART_IBRD   (UART0_BASE + 0x24)
#define UART_FBRD   (UART0_BASE + 0x28)
#define UART_LCRH   (UART0_BASE + 0x2C)
#define UART_CR     (UART0_BASE + 0x30)
#define UART_IMSC   (UART0_BASE + 0x38)
#define UART_ICR    (UART0_BASE + 0x44)

#define FR_BUSY     (1u << 3)
#define FR_RXFE     (1u << 4)
#define FR_TXFF     (1u << 5)

#define LCRH_FEN    (1u << 4)
#define LCRH_WLEN8  (3u << 5)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

/* Default of recent firmware; older firmware used 3 MHz. */
#define UART_CLOCK_FALLBACK 48000000u

static uint32_t clock_hz;

void uart_init(void)
{
    mmio_write(UART_CR, 0);
    while (mmio_read(UART_FR) & FR_BUSY)
        ;
    mmio_write(UART_LCRH, 0);           /* flush FIFOs */

    dmb();
    gpio_set_function(14, GPIO_ALT0);
    gpio_set_function(15, GPIO_ALT0);
    gpio_set_pull(14, GPIO_PULL_NONE);
    gpio_set_pull(15, GPIO_PULL_UP);    /* idle-high RX if unconnected */

    dmb();
    clock_hz = prop_clock_rate(CLOCK_UART);
    if (clock_hz == 0)
        clock_hz = UART_CLOCK_FALLBACK;

    dmb();
    /* divisor = clock / (16 * baud), in 1/64 units */
    uint32_t div = (clock_hz * 4 + UART_BAUD / 2) / UART_BAUD;
    mmio_write(UART_ICR, 0x7FF);
    mmio_write(UART_IBRD, div >> 6);
    mmio_write(UART_FBRD, div & 0x3F);
    mmio_write(UART_LCRH, LCRH_FEN | LCRH_WLEN8);
    mmio_write(UART_IMSC, 0);
    mmio_write(UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
}

uint32_t uart_clock(void)
{
    return clock_hz;
}

void uart_putc(char c)
{
    while (mmio_read(UART_FR) & FR_TXFF)
        ;
    mmio_write(UART_DR, (uint8_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_write(const void *buf, uint32_t len)
{
    const char *p = buf;
    while (len--)
        uart_putc(*p++);
}

int uart_rx_ready(void)
{
    return !(mmio_read(UART_FR) & FR_RXFE);
}

char uart_getc(void)
{
    while (!uart_rx_ready())
        ;
    return (char)(mmio_read(UART_DR) & 0xFF);
}

int uart_getc_timeout(uint32_t timeout_us, char *c)
{
    uint32_t start = timer_ticks();
    while (!uart_rx_ready()) {
        if (timer_ticks() - start >= timeout_us)
            return 0;
    }
    *c = (char)(mmio_read(UART_DR) & 0xFF);
    return 1;
}

void uart_flush(void)
{
    while (mmio_read(UART_FR) & FR_BUSY)
        ;
}
