#include "dwc2.h"
#include "arch/cache.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#define USB_BASE        (PERIPHERAL_BASE + 0x980000)
#define GAHBCFG         (USB_BASE + 0x008)
#define GUSBCFG         (USB_BASE + 0x00C)
#define GRSTCTL         (USB_BASE + 0x010)
#define GINTSTS         (USB_BASE + 0x014)
#define GRXFSIZ         (USB_BASE + 0x024)
#define GNPTXFSIZ       (USB_BASE + 0x028)
#define GSNPSID         (USB_BASE + 0x040)
#define GHWCFG2         (USB_BASE + 0x048)
#define HPTXFSIZ        (USB_BASE + 0x100)
#define HCFG            (USB_BASE + 0x400)
#define HFNUM           (USB_BASE + 0x408)
#define HPRT            (USB_BASE + 0x440)
#define HC(n, off)      (USB_BASE + 0x500 + (n) * 0x20 + (off))
#define HCCHAR          0x00
#define HCSPLT          0x04
#define HCINT           0x08
#define HCINTMSK        0x0C
#define HCTSIZ          0x10
#define HCDMA           0x14
#define PCGCCTL         (USB_BASE + 0xE00)

#define GAHBCFG_DMA_EN          (1u << 5)
#define GAHBCFG_WAIT_AXI_WRITES (1u << 4)       /* BCM2835 specific */
#define GUSBCFG_PHYIF16         (1u << 3)
#define GUSBCFG_ULPI_UTMI_SEL   (1u << 4)
#define GUSBCFG_FORCE_HOST      (1u << 29)
#define GUSBCFG_FORCE_DEV       (1u << 30)
#define GRSTCTL_CSRST           (1u << 0)
#define GRSTCTL_RXFFLSH         (1u << 4)
#define GRSTCTL_TXFFLSH         (1u << 5)
#define GRSTCTL_AHBIDLE         (1u << 31)

#define HPRT_CONN_STS   (1u << 0)
#define HPRT_CONN_DET   (1u << 1)
#define HPRT_ENA        (1u << 2)
#define HPRT_ENA_CHNG   (1u << 3)
#define HPRT_OVC_CHNG   (1u << 5)
#define HPRT_RST        (1u << 8)
#define HPRT_PWR        (1u << 12)
#define HPRT_W1C        (HPRT_CONN_DET | HPRT_ENA | HPRT_ENA_CHNG | HPRT_OVC_CHNG)

#define HCINT_XFERCOMPL (1u << 0)
#define HCINT_CHHLTD    (1u << 1)
#define HCINT_AHBERR    (1u << 2)
#define HCINT_STALL     (1u << 3)
#define HCINT_NAK       (1u << 4)
#define HCINT_ACK       (1u << 5)
#define HCINT_NYET      (1u << 6)
#define HCINT_XACTERR   (1u << 7)
#define HCINT_BBLERR    (1u << 8)
#define HCINT_FRMOVRUN  (1u << 9)
#define HCINT_DTERR     (1u << 10)
#define HCINT_ERRORS    (HCINT_XACTERR | HCINT_DTERR | HCINT_BBLERR | HCINT_AHBERR | HCINT_FRMOVRUN)

#define HCCHAR_CHENA    (1u << 31)
#define HCCHAR_CHDIS    (1u << 30)
#define HCCHAR_ODDFRM   (1u << 29)

#define HCSPLT_SPLTENA  (1u << 31)
#define HCSPLT_COMPSPLT (1u << 16)
#define HCSPLT_XACT_ALL (3u << 14)

#define HCTSIZ_DOPNG    (1u << 31)

#define USB_POWER_DEVICE 3

static int wait_bits(uint32_t reg, uint32_t mask, uint32_t value, uint32_t timeout_us)
{
    uint32_t t0 = timer_ticks();
    while ((mmio_read(reg) & mask) != value)
        if (timer_ticks() - t0 > timeout_us)
            return -1;
    return 0;
}

static int core_reset(void)
{
    if (wait_bits(GRSTCTL, GRSTCTL_AHBIDLE, GRSTCTL_AHBIDLE, 100000))
        return -1;
    mmio_write(GRSTCTL, GRSTCTL_CSRST);
    if (wait_bits(GRSTCTL, GRSTCTL_CSRST, 0, 100000))
        return -1;
    timer_delay_ms(100);
    return 0;
}

static void flush_fifos(void)
{
    mmio_write(GRSTCTL, GRSTCTL_TXFFLSH | (0x10u << 6));    /* all TX FIFOs */
    wait_bits(GRSTCTL, GRSTCTL_TXFFLSH, 0, 10000);
    mmio_write(GRSTCTL, GRSTCTL_RXFFLSH);
    wait_bits(GRSTCTL, GRSTCTL_RXFFLSH, 0, 10000);
}

int dwc2_init(void)
{
    /* the USB block is powered off by default */
    uint32_t v[2] = { USB_POWER_DEVICE, 3 };    /* on, wait */
    if (prop_query(0x00028001, v, 2) != 0 || !(v[1] & 1)) {
        kprintf("usb: cannot power the controller\n");
        return -1;
    }
    uint32_t id = mmio_read(GSNPSID);
    if ((id & 0xFFFFF000u) != 0x4F542000u) {
        kprintf("usb: unexpected controller id %08lx\n", id);
        return -1;
    }

    mmio_write(PCGCCTL, 0);
    uint32_t cfg = mmio_read(GUSBCFG);
    cfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16 | GUSBCFG_FORCE_DEV);
    mmio_write(GUSBCFG, cfg);
    if (core_reset()) {
        kprintf("usb: core reset timeout\n");
        return -1;
    }
    mmio_write(GUSBCFG, mmio_read(GUSBCFG) | GUSBCFG_FORCE_HOST);
    timer_delay_ms(50);

    mmio_write(GAHBCFG, GAHBCFG_DMA_EN | GAHBCFG_WAIT_AXI_WRITES);  /* no interrupts */
    mmio_write(HCFG, mmio_read(HCFG) & ~3u);    /* PHY clock 30/60 MHz (UTMI) */

    /* FIFOs (in 32-bit words): RX 1024, non-periodic TX 1024, periodic TX 1024 */
    mmio_write(GRXFSIZ, 1024);
    mmio_write(GNPTXFSIZ, (1024u << 16) | 1024);
    mmio_write(HPTXFSIZ, (1024u << 16) | 2048);
    flush_fifos();

    /* halt every channel */
    int nch = ((mmio_read(GHWCFG2) >> 14) & 0xF) + 1;
    for (int n = 0; n < nch; n++) {
        uint32_t c = mmio_read(HC(n, HCCHAR));
        if (c & HCCHAR_CHENA) {
            mmio_write(HC(n, HCCHAR), (c | HCCHAR_CHDIS) & ~HCCHAR_CHENA);
            mmio_write(HC(n, HCCHAR), c | HCCHAR_CHDIS | HCCHAR_CHENA);
            wait_bits(HC(n, HCCHAR), HCCHAR_CHENA, 0, 10000);
        }
        mmio_write(HC(n, HCINT), 0xFFFFFFFF);
        mmio_write(HC(n, HCINTMSK), 0);
    }

    /* port power */
    uint32_t p = mmio_read(HPRT) & ~HPRT_W1C;
    mmio_write(HPRT, p | HPRT_PWR);
    timer_delay_ms(100);
    return 0;
}

int dwc2_port_connected(void)
{
    return mmio_read(HPRT) & HPRT_CONN_STS;
}

uint32_t dwc2_frame(void)
{
    return mmio_read(HFNUM) & 0x3FFF;
}

int dwc2_port_reset(enum usb_speed *speed)
{
    uint32_t p = mmio_read(HPRT) & ~HPRT_W1C;
    mmio_write(HPRT, p | HPRT_RST);
    timer_delay_ms(60);
    mmio_write(HPRT, p & ~HPRT_RST);
    timer_delay_ms(20);
    if (wait_bits(HPRT, HPRT_ENA, HPRT_ENA, 200000))
        return -1;
    p = mmio_read(HPRT);
    mmio_write(HPRT, (p & ~HPRT_ENA) | HPRT_CONN_DET | HPRT_ENA_CHNG | HPRT_OVC_CHNG);
    *speed = (enum usb_speed)((p >> 17) & 3);
    return 0;
}

static uint32_t hcchar(const dwc2_pipe_t *pp, uint32_t mps)
{
    return mps | (uint32_t)pp->ep << 11 | (uint32_t)(pp->in ? 1 : 0) << 15 |
           (uint32_t)(pp->speed == USB_SPEED_LOW) << 17 | (uint32_t)pp->type << 18 |
           1u << 20 | (uint32_t)pp->addr << 22;
}

/* Starts the channel and waits for it to halt: the HCINT bits, 0 if it
 * never halted (it is disabled then). odd: -1 for a non-periodic pipe, else
 * the parity of the (micro)frame to run in. */
static uint32_t run(int ch, uint32_t chr, uint32_t split, uint32_t tsiz, void *buf,
                    int odd, uint32_t halt_us)
{
    if (odd > 0)
        chr |= HCCHAR_ODDFRM;
    mmio_write(HC(ch, HCINT), 0xFFFFFFFF);
    mmio_write(HC(ch, HCSPLT), split);
    mmio_write(HC(ch, HCTSIZ), tsiz);
    mmio_write(HC(ch, HCDMA), ARM_TO_BUS(buf));
    dmb();
    mmio_write(HC(ch, HCCHAR), chr | HCCHAR_CHENA);
    if (wait_bits(HC(ch, HCINT), HCINT_CHHLTD, HCINT_CHHLTD, halt_us)) {
        /* stuck: disable the channel */
        mmio_write(HC(ch, HCCHAR), mmio_read(HC(ch, HCCHAR)) | HCCHAR_CHDIS);
        wait_bits(HC(ch, HCINT), HCINT_CHHLTD, HCINT_CHHLTD, 10000);
        return 0;
    }
    return mmio_read(HC(ch, HCINT));
}

/* A device on the root port, or behind a hub of its own speed. */
static int transfer_direct(int ch, const dwc2_pipe_t *pp, uint8_t pid, uint8_t *buf,
                           uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    const uint32_t t0 = timer_ticks();
    const uint32_t mps = pp->mps ? pp->mps : 8;
    const int periodic = pp->type == EP_INTERRUPT || pp->type == EP_ISO;
    uint32_t halt_us = timeout_ms < 200 ? timeout_ms * 1000u : 200000u;
    uint32_t done = 0, ping = 0;
    int errors = 0;

    if (halt_us < 1000)
        halt_us = 1000;
    if (!pp->in && len)
        dcache_clean_range(buf, len);
    else if (pp->in && len)
        dcache_clean_invalidate_range(buf, len);

    for (;;) {
        uint32_t rest = len - done;
        uint32_t pkts = rest ? (rest + mps - 1) / mps : 1;
        uint32_t st = run(ch, hcchar(pp, mps), 0, rest | pkts << 19 | (uint32_t)pid << 29 | ping,
                          buf + done, periodic ? !(dwc2_frame() & 1) : -1, halt_us);
        if (!st)
            return XFER_TIMEOUT;
        uint32_t tsiz = mmio_read(HC(ch, HCTSIZ));
        /* what went through: IN counts bytes, OUT whole packets */
        uint32_t got = pp->in ? rest - (tsiz & 0x7FFFF)
                              : (pkts - ((tsiz >> 19) & 0x3FF)) * mps;
        if (got > rest)
            got = rest;

        if (st & HCINT_XFERCOMPL) {
            done += pp->in ? got : rest;
            if (pp->in && done)
                dcache_clean_invalidate_range(buf, done);
            if (actual)
                *actual = done;
            if (pp->toggle)
                *pp->toggle = (uint8_t)((tsiz >> 29) & 3);
            return XFER_OK;
        }
        if (st & HCINT_STALL)
            return XFER_STALL;
        if (got) {                      /* go on after the packets that made it */
            done += got;
            pid = (uint8_t)((tsiz >> 29) & 3);
        }
        if (st & HCINT_NAK) {
            if (pp->type == EP_INTERRUPT)
                return XFER_NAK;
        } else if (st & HCINT_NYET) {
            /* high-speed OUT: taken, but the device is full: PING first */
            ping = pp->in ? 0 : HCTSIZ_DOPNG;
        } else if (st & HCINT_ERRORS) {
            if (++errors > 3)
                return XFER_ERROR;
        }
        if (timer_ticks() - t0 > timeout_ms * 1000u)
            return XFER_TIMEOUT;
        if (!(st & HCINT_NYET))
            timer_delay_us(200);
    }
}

/* Split transactions: a low/full-speed device behind a high-speed hub. The
 * hub's transaction translator runs the transaction on the slow bus: the
 * host sends it a start split (SSPLIT), then asks for the outcome with
 * complete splits (CSPLIT; NYET = not done yet). One packet each time. */

static uint8_t split_buf[64] __attribute__((aligned(CACHE_LINE)));

/* Microframe 0-7 (a high-speed root port counts microframes). */
static uint32_t uframe(void)
{
    return mmio_read(HFNUM) & 7;
}

static void wait_uframe(uint32_t f)
{
    uint32_t t0 = timer_ticks();
    while (uframe() != f && timer_ticks() - t0 < 2000)
        ;
}

/* One packet of n <= mps bytes (OUT: already in split_buf). Interrupt
 * pipes follow the microframes: start split in Y (never 6), complete
 * splits from Y+2, as USPi and Circle do on the Pi 1. A device NAK, or no
 * answer in time, is XFER_NAK. *pid flips on a data toggle error. */
static int split_packet(int ch, const dwc2_pipe_t *pp, uint8_t *pid, uint32_t n,
                        uint32_t mps, uint32_t *got, uint32_t t0, uint32_t timeout_ms)
{
    const int periodic = pp->type == EP_INTERRUPT;
    const uint32_t chr = hcchar(pp, mps);
    const uint32_t split = HCSPLT_SPLTENA | HCSPLT_XACT_ALL |
                           (uint32_t)pp->hub_addr << 7 | pp->hub_port;
    const uint32_t size = pp->in ? mps : n;     /* IN: room for a whole packet */
    int errors = 0;

    for (;;) {
        if (timer_ticks() - t0 > timeout_ms * 1000u)
            return XFER_TIMEOUT;
        uint32_t f = 0;
        if (periodic) {
            f = (uframe() + 1) & 7;
            if (f == 6)
                f = 7;
            wait_uframe(f);
        }
        if (pp->in)
            dcache_clean_invalidate_range(split_buf, size);
        else if (n)
            dcache_clean_range(split_buf, n);
        uint32_t st = run(ch, chr, split, size | 1u << 19 | (uint32_t)*pid << 29, split_buf,
                          periodic ? (int)(f & 1) : -1, 10000);
        if (!st)
            return XFER_TIMEOUT;
        if (st & HCINT_STALL)
            return XFER_STALL;
        if (!(st & HCINT_ACK)) {
            if (st & HCINT_NAK) {               /* the translator is busy */
                if (periodic)
                    return XFER_NAK;
                timer_delay_us(125);
            } else if (++errors > 3) {
                return XFER_ERROR;
            }
            continue;
        }

        const int tries = f != 5 ? 3 : 2;
        uint32_t cf = (f + 2) & 7;
        for (int k = 0;; k++) {
            if (periodic) {
                if (k >= tries)
                    return XFER_NAK;            /* missed: the next poll asks again */
                wait_uframe(cf);
            }
            if (pp->in)
                dcache_clean_invalidate_range(split_buf, size);
            st = run(ch, chr, split | HCSPLT_COMPSPLT,
                     (pp->in ? size : 0) | 1u << 19 | (uint32_t)*pid << 29, split_buf,
                     periodic ? (int)(cf & 1) : -1, 10000);
            if (!st)
                return XFER_TIMEOUT;
            if (st & HCINT_XFERCOMPL) {
                uint32_t tsiz = mmio_read(HC(ch, HCTSIZ));
                *got = pp->in ? size - (tsiz & 0x7FFFF) : n;
                if (pp->in)
                    dcache_clean_invalidate_range(split_buf, size);
                return XFER_OK;
            }
            if (st & HCINT_STALL)
                return XFER_STALL;
            if (st & HCINT_NYET) {
                cf = (cf + 1) & 7;
                if (!periodic) {
                    if (timer_ticks() - t0 > timeout_ms * 1000u)
                        return XFER_TIMEOUT;
                    timer_delay_us(20);
                }
                continue;
            }
            if (st & HCINT_NAK) {               /* the device said NAK */
                if (periodic)
                    return XFER_NAK;
                timer_delay_us(125);
                break;                          /* again from the start split */
            }
            if (st & HCINT_DTERR)
                *pid = *pid == PID_DATA0 ? PID_DATA1 : PID_DATA0;
            if (++errors > 3)
                return XFER_ERROR;
            break;
        }
    }
}

static int transfer_split(int ch, const dwc2_pipe_t *pp, uint8_t pid, uint8_t *buf,
                          uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    const uint32_t t0 = timer_ticks();
    uint32_t mps = pp->mps ? pp->mps : 8;
    uint32_t done = 0;

    if (mps > sizeof split_buf)
        mps = sizeof split_buf;         /* low/full speed: at most 64 anyway */
    for (;;) {
        uint32_t n = len - done < mps ? len - done : mps, got = 0;
        if (!pp->in && n)
            memcpy(split_buf, buf + done, n);
        int r = split_packet(ch, pp, &pid, n, mps, &got, t0, timeout_ms);
        if (r != XFER_OK)
            return r;
        if (pp->in) {
            if (got > n)
                got = n;
            memcpy(buf + done, split_buf, got);
        } else {
            got = n;
        }
        done += got;
        if (pid != PID_SETUP)
            pid = pid == PID_DATA0 ? PID_DATA1 : PID_DATA0;
        if (done >= len || (pp->in && got < mps))
            break;                      /* all there, or a short packet */
    }
    if (actual)
        *actual = done;
    if (pp->toggle)
        *pp->toggle = pid;
    return XFER_OK;
}

int dwc2_transfer(int ch, const dwc2_pipe_t *pp, uint8_t pid, void *buf,
                  uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    if (pp->hub_addr)
        return transfer_split(ch, pp, pid, buf, len, actual, timeout_ms);
    return transfer_direct(ch, pp, pid, buf, len, actual, timeout_ms);
}
