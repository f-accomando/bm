#include "btuart.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "kernel/irq.h"

#define UART0_BASE  (PERIPHERAL_BASE + 0x201000)
#define UART_DR     (UART0_BASE + 0x00)
#define UART_FR     (UART0_BASE + 0x18)
#define UART_IBRD   (UART0_BASE + 0x24)
#define UART_FBRD   (UART0_BASE + 0x28)
#define UART_LCRH   (UART0_BASE + 0x2C)
#define UART_CR     (UART0_BASE + 0x30)
#define UART_IFLS   (UART0_BASE + 0x34)
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
#define IM_RX       (1u << 4)
#define IM_RT       (1u << 6)       /* receive timeout: a few bytes left in the FIFO */

static uint32_t clock_hz;

/* Received bytes, filled by the UART interrupt: the chip never has to wait
 * for the main loop (with polling, the 16-byte FIFO filled, RTS stopped the
 * chip and one packet per frame came through: the DS4 lagged). When the
 * ring is full the interrupt stops reading, the FIFO fills and RTS holds
 * the chip back: nothing is lost. */
#define RING 16384
static uint8_t ring[RING];
static volatile uint32_t r_head, r_tail;
static int irq_on;

/* FIFO -> ring; from the interrupt, or from a reader that finds the ring
 * empty (a few bytes below the interrupt level wait for the receive
 * timeout interrupt, which QEMU does not emulate). */
static void pull(void)
{
    while (!(mmio_read(UART_FR) & FR_RXFE)) {
        uint32_t next = (r_head + 1) % RING;
        if (next == r_tail) {
            mmio_write(UART_IMSC, 0);           /* full: RTS holds the chip */
            break;
        }
        ring[r_head] = (uint8_t)mmio_read(UART_DR);
        r_head = next;
    }
}

static void rx_irq(void *arg)
{
    (void)arg;
    pull();
    mmio_write(UART_ICR, IM_RX | IM_RT);
}

static void pull_now(void)
{
    uint32_t s = irq_save();
    pull();
    irq_restore(s);
}

static void rx_irq_enable(void)
{
    mmio_write(UART_IFLS, 1u << 3);             /* RX interrupt at 1/4 full */
    mmio_write(UART_ICR, 0x7FF);
    mmio_write(UART_IMSC, IM_RX | IM_RT);
    if (!irq_on) {
        irq_register(IRQ_UART, rx_irq, 0);
        irq_enable(IRQ_UART);
        irq_on = 1;
    }
}

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
    mmio_write(UART_CR, CR_UARTEN | CR_TXE | CR_RXE | CR_RTSEN | CR_CTSEN);
    rx_irq_enable();
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
    while (r_tail == r_head) {
        pull_now();
        if (r_tail == r_head && timer_ticks() - t0 >= timeout_us)
            return -1;
    }
    uint8_t c = ring[r_tail];
    r_tail = (r_tail + 1) % RING;
    if (!(mmio_read(UART_IMSC) & IM_RX))
        mmio_write(UART_IMSC, IM_RX | IM_RT);   /* room again */
    return c;
}

int btuart_ready(void)
{
    if (r_tail == r_head)
        pull_now();
    return r_tail != r_head;
}

void btuart_drain(void)
{
    uint32_t s = irq_save();
    while (!(mmio_read(UART_FR) & FR_RXFE))
        (void)mmio_read(UART_DR);
    r_tail = r_head;
    irq_restore(s);
}
