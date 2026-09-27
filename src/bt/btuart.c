#include "btuart.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"

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
#define CR_RTSEN    (1u << 14)
#define CR_CTSEN    (1u << 15)

static uint32_t clock_hz;

void btuart_set_baud(uint32_t baud)
{
    while (mmio_read(UART_FR) & FR_BUSY)
        ;
    mmio_write(UART_CR, 0);
    uint32_t div = (clock_hz * 4 + baud / 2) / baud;       /* 1/64 units */
    mmio_write(UART_ICR, 0x7FF);
    mmio_write(UART_IBRD, div >> 6);
    mmio_write(UART_FBRD, div & 0x3F);
    mmio_write(UART_LCRH, LCRH_FEN | LCRH_WLEN8);
    mmio_write(UART_IMSC, 0);
    mmio_write(UART_CR, CR_UARTEN | CR_TXE | CR_RXE | CR_RTSEN | CR_CTSEN);
}

void btuart_init(uint32_t baud)
{
    mmio_write(UART_CR, 0);
    mmio_write(UART_LCRH, 0);
    dmb();
    /* the PL011 leaves GPIO14/15 (the console is on the mini UART now) */
    for (unsigned pin = 30; pin <= 33; pin++)
        gpio_set_function(pin, GPIO_ALT3);
    gpio_set_pull(30, GPIO_PULL_UP);
    gpio_set_pull(31, GPIO_PULL_NONE);
    gpio_set_pull(32, GPIO_PULL_NONE);
    gpio_set_pull(33, GPIO_PULL_UP);
    dmb();
    clock_hz = prop_clock_rate(CLOCK_UART);
    if (!clock_hz)
        clock_hz = 48000000u;
    dmb();
    btuart_set_baud(baud);
}

void btuart_write(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    while (len--) {
        while (mmio_read(UART_FR) & FR_TXFF)
            ;
        mmio_write(UART_DR, *p++);
    }
}

int btuart_read(uint32_t timeout_us)
{
    uint32_t t0 = timer_ticks();
    while (mmio_read(UART_FR) & FR_RXFE)
        if (timer_ticks() - t0 >= timeout_us)
            return -1;
    return (int)(mmio_read(UART_DR) & 0xFF);
}

void btuart_drain(void)
{
    while (!(mmio_read(UART_FR) & FR_RXFE))
        (void)mmio_read(UART_DR);
}
