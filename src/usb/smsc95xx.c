#include "smsc95xx.h"
#include "dwc2.h"
#include "arch/cache.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

/* registers (vendor requests 0xA0 write, 0xA1 read, 32 bits, little endian) */
#define ID_REV          0x00
#define INT_STS         0x08
#define TX_CFG          0x10
#define HW_CFG          0x14
#define RX_FIFO_INF     0x18
#define TX_FIFO_INF     0x1C
#define PM_CTRL         0x20
#define LED_GPIO_CFG    0x24
#define AFC_CFG         0x2C
#define BURST_CAP       0x38
#define MAC_CR          0x100
#define ADDRH           0x104
#define ADDRL           0x108
#define MII_ADDR        0x114
#define MII_DATA        0x118
#define FLOW            0x11C
#define COE_CR          0x130

#define TX_CFG_ON       0x00000004u
#define HW_CFG_BIR      0x00001000u     /* bulk IN with nothing to send: empty packet, not NAK */
#define HW_CFG_RXDOFF   0x00000600u
#define HW_CFG_MEF      0x00000020u     /* several frames per transfer: off */
#define HW_CFG_LRST     0x00000008u
#define HW_CFG_BCE      0x00000002u
#define PM_CTL_PHY_RST  0x00000010u
#define LED_SPD_LNK_FDX 0x01110000u
#define AFC_CFG_DEFAULT 0x00F830A1u
#define MAC_CR_RCVOWN   0x00800000u
#define MAC_CR_FDPX     0x00100000u
#define MAC_CR_PRMS     0x00040000u
#define MAC_CR_TXEN     0x00000008u
#define MAC_CR_RXEN     0x00000004u
#define MII_WRITE       0x02u
#define MII_BUSY        0x01u

#define TX_CMD_A_FIRST  0x00002000u
#define TX_CMD_A_LAST   0x00001000u
#define RX_STS_FL(s)    (((s) >> 16) & 0x3FFF)  /* frame length, CRC included */
#define RX_STS_ES       0x00008000u             /* error summary */

/* internal PHY (MII address 1) */
#define PHY_ID          1
#define MII_BMCR        0
#define MII_BMSR        1
#define MII_ADVERTISE   4
#define MII_LPA         5
#define BMCR_SPEED100   0x2000
#define BMCR_ANENABLE   0x1000
#define BMCR_ANRESTART  0x0200
#define BMSR_LSTATUS    0x0004
#define ADV_10_100_CSMA 0x01E1          /* 10/100, half and full duplex */
#define LPA_100FULL     0x0100
#define LPA_100HALF     0x0080
#define LPA_10FULL      0x0040

static struct {
    usb_dev_t *dev;
    int ready, link, mbit, full;
    uint8_t ep_in, ep_out, tog_in, tog_out;
    uint16_t mps_in, mps_out;
    uint8_t mac[6];
    uint32_t chip, mac_cr, last_link;
    uint32_t rx, rx_bad, rx_err, tx, tx_err;
} e;

static uint8_t rxbuf[2048] __attribute__((aligned(CACHE_LINE)));
static uint8_t txbuf[1536 + 32] __attribute__((aligned(CACHE_LINE)));

int eth_match(uint16_t vid, uint16_t pid)
{
    return vid == 0x0424 && (pid == 0xEC00 || pid == 0x9500 || pid == 0x9505 ||
                             pid == 0x9E00 || pid == 0x9E01);
}

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void put_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}

static int read_reg(uint32_t reg, uint32_t *v)
{
    uint8_t b[4];
    if (usb_control(e.dev, 0xC0, 0xA1, 0, (uint16_t)reg, b, 4) < 4)
        return -1;
    *v = le32(b);
    return 0;
}

static int write_reg(uint32_t reg, uint32_t v)
{
    uint8_t b[4];
    put_le32(b, v);
    return usb_control(e.dev, 0x40, 0xA0, 0, (uint16_t)reg, b, 4) < 0 ? -1 : 0;
}

/* Waits for self-clearing bits (a reset) to go: 0, or -1 after ~1 s. */
static int wait_clear(uint32_t reg, uint32_t bits)
{
    for (int i = 0; i < 100; i++) {
        uint32_t v;
        if (read_reg(reg, &v) != 0)
            return -1;
        if (!(v & bits))
            return 0;
        timer_delay_ms(10);
    }
    return -1;
}

static int mii_wait(void)
{
    for (int i = 0; i < 100; i++) {
        uint32_t v;
        if (read_reg(MII_ADDR, &v) != 0)
            return -1;
        if (!(v & MII_BUSY))
            return 0;
        timer_delay_us(100);
    }
    return -1;
}

static int mii_read(uint32_t idx, uint16_t *val)
{
    uint32_t v;
    if (mii_wait() || write_reg(MII_ADDR, PHY_ID << 11 | idx << 6 | MII_BUSY) ||
        mii_wait() || read_reg(MII_DATA, &v))
        return -1;
    *val = (uint16_t)v;
    return 0;
}

static int mii_write(uint32_t idx, uint16_t val)
{
    if (mii_wait() || write_reg(MII_DATA, val) ||
        write_reg(MII_ADDR, PHY_ID << 11 | idx << 6 | MII_WRITE | MII_BUSY))
        return -1;
    return mii_wait();
}

/* The Pi's MAC address (b8:27:eb:..., from its serial number); else a
 * locally administered one made from the serial. */
static void board_mac(uint8_t mac[6])
{
    uint32_t v[2] = { 0, 0 };
    if (prop_query(0x00010003, v, 2) == 0 && (v[0] || v[1])) {  /* GET_BOARD_MAC_ADDRESS */
        put_le32(mac, v[0]);
        mac[4] = (uint8_t)v[1];
        mac[5] = (uint8_t)(v[1] >> 8);
        if (!(mac[0] & 1))
            return;
    }
    v[0] = v[1] = 0;
    prop_query(PROP_GET_BOARD_SERIAL, v, 2);
    mac[0] = 0x02;                              /* local, unicast */
    mac[1] = 0x33;
    put_le32(mac + 2, v[0] ^ v[1]);
}

void eth_detach(void)
{
    memset(&e, 0, sizeof e);
}

int eth_present(void)
{
    return e.ready;
}

const unsigned char *eth_mac(void)
{
    return e.mac;
}

int eth_linked(void)
{
    return e.ready && e.link;
}

static const char *step;                        /* for the error message */

static int setup(void)
{
    uint32_t v;
    step = "lite reset";
    if (write_reg(HW_CFG, HW_CFG_LRST) || wait_clear(HW_CFG, HW_CFG_LRST))
        return -1;
    step = "PHY reset";
    if (write_reg(PM_CTRL, PM_CTL_PHY_RST) || wait_clear(PM_CTRL, PM_CTL_PHY_RST))
        return -1;
    step = "MAC address";
    board_mac(e.mac);
    if (write_reg(ADDRL, le32(e.mac)) || write_reg(ADDRH, (uint32_t)e.mac[4] | (uint32_t)e.mac[5] << 8))
        return -1;
    step = "bulk mode";
    if (read_reg(HW_CFG, &v))
        return -1;
    v = (v | HW_CFG_BIR) & ~(HW_CFG_MEF | HW_CFG_BCE | HW_CFG_RXDOFF);
    if (write_reg(HW_CFG, v) || write_reg(BURST_CAP, 0) || write_reg(INT_STS, 0xFFFFFFFFu))
        return -1;
    step = "chip id";
    if (read_reg(ID_REV, &e.chip))
        return -1;
    step = "LEDs and flow control";
    if (write_reg(LED_GPIO_CFG, LED_SPD_LNK_FDX) || write_reg(FLOW, 0) ||
        write_reg(AFC_CFG, AFC_CFG_DEFAULT) || write_reg(COE_CR, 0))
        return -1;
    step = "PHY auto-negotiation";
    if (mii_write(MII_ADVERTISE, ADV_10_100_CSMA) ||
        mii_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART))
        return -1;
    /* every frame (lwIP checks the addresses), no receiving our own in
     * half duplex; the duplex follows the link (eth_poll) */
    step = "transmitter and receiver";
    e.mac_cr = MAC_CR_RCVOWN | MAC_CR_PRMS | MAC_CR_TXEN | MAC_CR_RXEN;
    if (write_reg(MAC_CR, e.mac_cr) || write_reg(TX_CFG, TX_CFG_ON))
        return -1;
    return 0;
}

int eth_attach(usb_dev_t *d, const uint8_t *c, uint16_t len)
{
    eth_detach();
    e.dev = d;
    for (int i = 0; i + 6 < len && c[i]; i += c[i]) {
        if (c[i + 1] != 5 || (c[i + 3] & 3) != EP_BULK)     /* bulk endpoints */
            continue;
        uint16_t mps = (uint16_t)((c[i + 4] | c[i + 5] << 8) & 0x7FF);
        if ((c[i + 2] & 0x80) && !e.ep_in) {
            e.ep_in = c[i + 2] & 0x0F;
            e.mps_in = mps;
        } else if (!(c[i + 2] & 0x80) && !e.ep_out) {
            e.ep_out = c[i + 2] & 0x0F;
            e.mps_out = mps;
        }
    }
    if (!e.ep_in || !e.ep_out || !e.mps_in || !e.mps_out) {
        kprintf("\x1b[91meth: no bulk endpoints (configuration %u bytes)\x1b[0m\n", len);
        return -1;
    }
    e.tog_in = e.tog_out = PID_DATA0;
    if (setup() != 0) {
        kprintf("\x1b[91meth: %s failed\x1b[0m\n", step);
        e.dev = NULL;
        return -1;
    }
    e.ready = 1;
    e.last_link = timer_ticks() - 1000000u;     /* the first poll reads the link */
    return 0;
}

void eth_print(void)
{
    if (!e.ready)
        return;
    kprintf("usb: port %u: Ethernet %04x:%04x (chip %04lx rev %lu), MAC %02x:%02x:%02x:%02x:%02x:%02x",
            e.dev->port, e.dev->vid, e.dev->pid, e.chip >> 16, e.chip & 0xFFFF,
            e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
    if (e.link)
        kprintf(", link %d Mbit/s %s duplex\n", e.mbit, e.full ? "full" : "half");
    else
        kprintf(", no link yet\n");
}

void eth_diag(void)
{
    uint32_t hw = 0, mac = 0, rxf = 0, txf = 0;
    uint16_t bmsr = 0, lpa = 0;
    if (!e.ready) {
        kprintf("eth: no Ethernet controller (Pi 1 B / B+: LAN951x on the USB hub)\n");
        return;
    }
    eth_print();
    read_reg(HW_CFG, &hw);
    read_reg(MAC_CR, &mac);
    read_reg(RX_FIFO_INF, &rxf);
    read_reg(TX_FIFO_INF, &txf);
    mii_read(MII_BMSR, &bmsr);
    mii_read(MII_BMSR, &bmsr);
    mii_read(MII_LPA, &lpa);
    kprintf("eth: received %lu (bad %lu, errors %lu), sent %lu (errors %lu)\n",
            e.rx, e.rx_bad, e.rx_err, e.tx, e.tx_err);
    kprintf("eth: HW_CFG %08lx MAC_CR %08lx RX_FIFO %08lx TX_FIFO %08lx BMSR %04x LPA %04x\n",
            hw, mac, rxf, txf, bmsr, lpa);
}

void eth_poll(void)
{
    if (!e.ready)
        return;
    uint32_t now = timer_ticks();
    if (now - e.last_link < 500000)
        return;
    e.last_link = now;
    uint16_t bmsr;
    /* the link bit latches a loss: the second read is the state now */
    if (mii_read(MII_BMSR, &bmsr) || mii_read(MII_BMSR, &bmsr))
        return;
    int up = (bmsr & BMSR_LSTATUS) != 0;
    if (up && !e.link) {
        uint16_t adv = 0, lpa = 0, bmcr = 0;
        mii_read(MII_ADVERTISE, &adv);
        mii_read(MII_LPA, &lpa);
        uint16_t both = adv & lpa;
        if (both & LPA_100FULL)      { e.mbit = 100; e.full = 1; }
        else if (both & LPA_100HALF) { e.mbit = 100; e.full = 0; }
        else if (both & LPA_10FULL)  { e.mbit = 10;  e.full = 1; }
        else if (lpa & 0x03E0)       { e.mbit = 10;  e.full = 0; }
        else {                      /* the other end does not negotiate */
            mii_read(MII_BMCR, &bmcr);
            e.mbit = (bmcr & BMCR_SPEED100) ? 100 : 10;
            e.full = 0;
        }
        /* the MAC's duplex must match the link's */
        e.mac_cr = e.full ? (e.mac_cr | MAC_CR_FDPX) & ~MAC_CR_RCVOWN
                          : (e.mac_cr & ~MAC_CR_FDPX) | MAC_CR_RCVOWN;
        write_reg(MAC_CR, e.mac_cr);
        kprintf("eth: link up, %d Mbit/s %s duplex\n", e.mbit, e.full ? "full" : "half");
    } else if (!up && e.link) {
        kprintf("eth: link down (cable unplugged?)\n");
    }
    e.link = up;
}

int eth_recv(void *buf, int max)
{
    if (!e.ready)
        return 0;
    for (int tries = 0; tries < 8; tries++) {
        uint32_t got = 0;
        int r = usb_bulk(e.dev, e.ep_in, 1, e.mps_in, &e.tog_in, rxbuf, sizeof rxbuf, &got, 1);
        if (r != XFER_OK) {
            if (r != XFER_TIMEOUT)
                e.rx_err++;
            return 0;
        }
        if (got < 4)
            return 0;                           /* empty packet: nothing waiting */
        uint32_t sts = le32(rxbuf), len = RX_STS_FL(sts);
        if ((sts & RX_STS_ES) || len < 14 + 4 || len + 4 > got || (int)(len - 4) > max) {
            e.rx_bad++;
            continue;
        }
        len -= 4;                               /* the CRC */
        memcpy(buf, rxbuf + 4, len);
        e.rx++;
        return (int)len;
    }
    return 0;
}

int eth_send(const void *frame, int len)
{
    if (!e.ready || len < 14 || len > 1518)
        return -1;
    put_le32(txbuf, (uint32_t)len | TX_CMD_A_FIRST | TX_CMD_A_LAST);
    put_le32(txbuf + 4, (uint32_t)len);
    memcpy(txbuf + 8, frame, (size_t)len);
    uint32_t n = (uint32_t)len + 8;
    int r = usb_bulk(e.dev, e.ep_out, 0, e.mps_out, &e.tog_out, txbuf, n, NULL, 50);
    /* a transfer of whole packets ends with an empty one (as Linux does) */
    if (r == XFER_OK && n % e.mps_out == 0)
        r = usb_bulk(e.dev, e.ep_out, 0, e.mps_out, &e.tog_out, txbuf, 0, NULL, 50);
    if (r != XFER_OK) {
        e.tx_err++;
        return -1;
    }
    e.tx++;
    return 0;
}
