/*
 * bt/btuart.h on the RGB30: UART1 (DesignWare 8250 at 0xfe650000, pins M1:
 * TX GPIO3_D6, RX GPIO3_D7, RTS GPIO4_B6, CTS GPIO4_C1) wired to the
 * RTL8821CS. 8 data bits, even parity (H5 on Realtek), from the 24 MHz
 * crystal; received bytes go to a ring from the interrupt (INTID 149), so
 * nothing is lost while a frame is drawn. RTS/CTS flow control goes on with
 * the fast speed (btuart_set_flow).
 */
#ifdef PLAT_RK3566
#include "bt/btuart.h"
#include "kernel/irq.h"
#include "drivers/timer.h"
#include "rk_gpio.h"
#include "io.h"

#define UART1       0xfe650000u
#define CRU         0xfdd20000u
#define GRF         0xfdc60000u
#define IRQ_UART1   149

#define RBR 0x00
#define THR 0x00
#define DLL 0x00
#define IER 0x04
#define DLH 0x04
#define FCR 0x08
#define LCR 0x0c
#define MCR 0x10
#define LSR 0x14
#define USR 0x7c

#define LCR_8E1     0x1b
#define LCR_DLAB    0x80
#define LSR_DR      (1u << 0)
#define LSR_THRE    (1u << 5)

#define RING 8192
static volatile uint8_t ring[RING];
static volatile unsigned r_head, r_tail;
static unsigned overruns;
static int flow, irq_on;

static inline uint32_t rd(uint32_t o)          { return readl(UART1 + o); }
static inline void wr(uint32_t o, uint32_t v)  { writel(UART1 + o, v); }

static void pull(void)
{
    while (rd(LSR) & LSR_DR) {
        uint8_t c = (uint8_t)rd(RBR);
        unsigned next = (r_head + 1) % RING;
        if (next == r_tail) {
            overruns++;
            continue;
        }
        ring[r_head] = c;
        r_head = next;
    }
}

static void uart1_irq(void *arg)
{
    (void)arg;
    pull();
}

static void wait_idle(void)
{
    for (int i = 0; i < 100000 && (rd(USR) & 1); i++)
        ;
}

static void set_line(uint32_t baud)
{
    uint32_t div = (24000000u + 8 * baud) / (16 * baud);
    wr(FCR, 0x07);                          /* FIFOs on and cleared */
    wait_idle();
    wr(LCR, LCR_DLAB | LCR_8E1);
    wr(DLL, div & 0xff);
    wr(DLH, div >> 8);
    wr(LCR, LCR_8E1);
    wr(FCR, 0x81);                          /* FIFO, RX trigger half full */
    wr(MCR, flow ? 0x22 : 0x03);            /* RTS (+ auto flow control) */
}

void btuart_init(uint32_t baud)
{
    /* clock: sclk_uart1 from the crystal, gates open */
    writel(CRU + 0x1d0, 0x30002000u);
    writel(CRU + 0x36c, 0xf0000000u);
    /* pins: UART1 on its M1 pins */
    writel(GRF + 0x05c, 0xff004400u);
    writel(GRF + 0x06c, 0x0f000400u);
    writel(GRF + 0x070, 0x00f00040u);
    writel(GRF + 0x30c, 0x01000100u);
    writel(GRF + 0x0ac, 0xf0005000u);       /* pull-up on TX, RX */
    flow = 0;
    wr(IER, 0);
    set_line(baud);
    r_head = r_tail = 0;
    if (!irq_on) {
        irq_register(IRQ_UART1, uart1_irq, 0);
        irq_on = 1;
    }
    wr(IER, 0x01);                          /* received data (and timeout) */
    irq_enable(IRQ_UART1);
}

void btuart_set_baud(uint32_t baud)
{
    uint32_t daif = irq_save();
    wr(IER, 0);
    set_line(baud);
    wr(IER, 0x01);
    irq_restore(daif);
}

void btuart_set_flow(int on)
{
    flow = on;
    wr(MCR, flow ? 0x22 : 0x03);
}

void btuart_write(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    while (len--) {
        uint32_t t0 = timer_ticks();
        while (!(rd(LSR) & LSR_THRE))
            if (timer_ticks() - t0 > 20000)
                break;                      /* CTS held off too long: drop */
        wr(THR, *p++);
    }
}

int btuart_read(uint32_t timeout_us)
{
    uint32_t t0 = timer_ticks();
    while (r_tail == r_head) {
        uint32_t daif = irq_save();
        pull();
        irq_restore(daif);
        if (r_tail == r_head && timer_ticks() - t0 >= timeout_us)
            return -1;
    }
    uint8_t c = ring[r_tail];
    r_tail = (r_tail + 1) % RING;
    return c;
}

int btuart_ready(void)
{
    if (r_tail == r_head) {
        uint32_t daif = irq_save();
        pull();
        irq_restore(daif);
    }
    return r_tail != r_head;
}

void btuart_drain(void)
{
    uint32_t daif = irq_save();
    pull();
    r_tail = r_head;
    irq_restore(daif);
}

unsigned btuart_overruns(void)
{
    return overruns;
}
#endif
