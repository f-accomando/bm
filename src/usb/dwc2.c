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
#define HCINT_XACTERR   (1u << 7)
#define HCINT_BBLERR    (1u << 8)
#define HCINT_DTERR     (1u << 10)

#define HCCHAR_CHENA    (1u << 31)
#define HCCHAR_CHDIS    (1u << 30)
#define HCCHAR_ODDFRM   (1u << 29)

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

int dwc2_transfer(int ch, const dwc2_pipe_t *pp, uint8_t pid, void *buf,
                  uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    const uint32_t t0 = timer_ticks();
    uint32_t mps = pp->mps ? pp->mps : 8;
    int errors = 0;

    for (;;) {
        uint32_t pkts = len ? (len + mps - 1) / mps : 1;
        if (!pp->in && len)
            dcache_clean_range(buf, len);
        else if (pp->in && len)
            dcache_clean_invalidate_range(buf, len);

        uint32_t chr = mps | (uint32_t)pp->ep << 11 | (uint32_t)(pp->in ? 1 : 0) << 15 |
                       (uint32_t)(pp->speed == USB_SPEED_LOW) << 17 | (uint32_t)pp->type << 18 |
                       1u << 20 | (uint32_t)pp->addr << 22;
        if (pp->type == EP_INTERRUPT || pp->type == EP_ISO)
            chr |= (dwc2_frame() & 1) ? 0 : HCCHAR_ODDFRM;

        mmio_write(HC(ch, HCINT), 0xFFFFFFFF);
        mmio_write(HC(ch, HCSPLT), 0);
        mmio_write(HC(ch, HCTSIZ), len | pkts << 19 | (uint32_t)pid << 29);
        mmio_write(HC(ch, HCDMA), ARM_TO_BUS(buf));
        dmb();
        mmio_write(HC(ch, HCCHAR), chr | HCCHAR_CHENA);

        if (wait_bits(HC(ch, HCINT), HCINT_CHHLTD, HCINT_CHHLTD, 200000)) {
            /* stuck: disable the channel */
            mmio_write(HC(ch, HCCHAR), mmio_read(HC(ch, HCCHAR)) | HCCHAR_CHDIS);
            wait_bits(HC(ch, HCINT), HCINT_CHHLTD, HCINT_CHHLTD, 10000);
            return XFER_TIMEOUT;
        }
        uint32_t st = mmio_read(HC(ch, HCINT));
        uint32_t tsiz = mmio_read(HC(ch, HCTSIZ));

        if (st & HCINT_XFERCOMPL) {
            uint32_t done = pp->in ? len - (tsiz & 0x7FFFF) : len;
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
        if (st & HCINT_NAK) {
            if (pp->type == EP_INTERRUPT)
                return XFER_NAK;
        } else if (st & (HCINT_XACTERR | HCINT_DTERR | HCINT_BBLERR | HCINT_AHBERR)) {
            if (++errors > 3)
                return XFER_ERROR;
        }
        if (timer_ticks() - t0 > timeout_ms * 1000u)
            return XFER_TIMEOUT;
        timer_delay_us(200);
    }
}
