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

/* Mini UART (AUX): the console moves here when the PL011 goes to the
 * Bluetooth chip (see uart_use_mini). Its clock is the core clock, kept
 * stable by enable_uart=1 in config.txt. */
#define AUX_BASE    (PERIPHERAL_BASE + 0x215000)
#define AUX_ENABLES (AUX_BASE + 0x04)
#define MU_IO       (AUX_BASE + 0x40)
#define MU_IER      (AUX_BASE + 0x44)
#define MU_IIR      (AUX_BASE + 0x48)
#define MU_LCR      (AUX_BASE + 0x4C)
#define MU_MCR      (AUX_BASE + 0x50)
#define MU_LSR      (AUX_BASE + 0x54)
#define MU_CNTL     (AUX_BASE + 0x60)
#define MU_BAUD     (AUX_BASE + 0x68)
#define LSR_RX_READY (1u << 0)
#define LSR_TX_EMPTY (1u << 5)
#define LSR_TX_IDLE  (1u << 6)

static int mini;

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

void uart_use_mini(void)
{
    uart_flush();
    uint32_t core = prop_clock_rate(CLOCK_CORE);
    if (core == 0)
        core = 250000000u;
    dmb();
    mmio_write(AUX_ENABLES, mmio_read(AUX_ENABLES) | 1);
    mmio_write(MU_CNTL, 0);
    mmio_write(MU_IER, 0);
    mmio_write(MU_LCR, 3);                      /* 8 bits */
    mmio_write(MU_MCR, 0);
    mmio_write(MU_IIR, 0xC6);                   /* clear both FIFOs */
    mmio_write(MU_BAUD, (core + 4 * UART_BAUD) / (8 * UART_BAUD) - 1);
    dmb();
    gpio_set_function(14, GPIO_ALT5);
    gpio_set_function(15, GPIO_ALT5);
    dmb();
    mmio_write(MU_CNTL, 3);                     /* RX and TX on */
    clock_hz = core;
    mini = 1;
}

int uart_is_mini(void)
{
    return mini;
}

void uart_putc(char c)
{
    if (mini) {
        while (!(mmio_read(MU_LSR) & LSR_TX_EMPTY))
            ;
        mmio_write(MU_IO, (uint8_t)c);
        return;
    }
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
    if (mini)
        return mmio_read(MU_LSR) & LSR_RX_READY;
    return !(mmio_read(UART_FR) & FR_RXFE);
}

static char rx_byte(void)
{
    return (char)(mmio_read(mini ? MU_IO : UART_DR) & 0xFF);
}

char uart_getc(void)
{
    while (!uart_rx_ready())
        ;
    return rx_byte();
}

int uart_getc_timeout(uint32_t timeout_us, char *c)
{
    uint32_t start = timer_ticks();
    while (!uart_rx_ready()) {
        if (timer_ticks() - start >= timeout_us)
            return 0;
    }
    *c = rx_byte();
    return 1;
}

void uart_flush(void)
{
    if (mini) {
        while (!(mmio_read(MU_LSR) & LSR_TX_IDLE))
            ;
        return;
    }
    while (mmio_read(UART_FR) & FR_BUSY)
        ;
}
