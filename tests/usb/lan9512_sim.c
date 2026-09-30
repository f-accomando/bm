#include "lan9512_sim.h"
#include "usb/dwc2.h"
#include "drivers/prop.h"

#include <stdio.h>
#include <string.h>

#define MII_ADDR 0x114
#define MII_DATA 0x118

uint32_t sim_reg[0x140 / 4];
uint16_t sim_phy[32];
int sim_lrsts, sim_phyrsts, sim_reg_writes, sim_fails;
uint32_t sim_now_us;
void (*sim_on_out)(const uint8_t *data, uint32_t len);

static int lrst_reads, phyrst_reads, link_up, link_lost;

#define SIM_CHECK(c) do { if (!(c)) { sim_fails++; printf("SIM FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

usb_dev_t sim_dev = { .addr = 2, .speed = USB_SPEED_HIGH, .port = 1,
                      .vid = 0x0424, .pid = 0xEC00 };

/* the LAN9512's Ethernet function: bulk IN 1, bulk OUT 2, interrupt IN 3 */
const uint8_t sim_cfg[39] = {
    9, 2, 39, 0, 1, 1, 0, 0xE0, 1,
    9, 4, 0, 0, 3, 0xFF, 0x00, 0xFF, 0,
    7, 5, 0x81, 2, 0x00, 0x02, 0,
    7, 5, 0x02, 2, 0x00, 0x02, 0,
    7, 5, 0x83, 3, 0x10, 0x00, 4,
};

const uint8_t sim_board_mac[6] = { 0xb8, 0x27, 0xeb, 0x12, 0x34, 0x56 };

/* ---- the kernel as the driver sees it ---- */
uint32_t timer_ticks(void) { return sim_now_us; }
void timer_delay_us(uint32_t us) { sim_now_us += us; }
void timer_delay_ms(uint32_t ms) { sim_now_us += ms * 1000; }

int prop_query(uint32_t tag, uint32_t *v, unsigned n)
{
    if (tag != 0x00010003 || n < 2)
        return -1;
    v[0] = (uint32_t)sim_board_mac[0] | (uint32_t)sim_board_mac[1] << 8 |
           (uint32_t)sim_board_mac[2] << 16 | (uint32_t)sim_board_mac[3] << 24;
    v[1] = (uint32_t)sim_board_mac[4] | (uint32_t)sim_board_mac[5] << 8;
    return 0;
}

void sim_link(int up, uint16_t lpa)
{
    if (!up && link_up)
        link_lost = 1;
    link_up = up;
    sim_phy[5] = lpa;
}

/* ---- registers ---- */
static uint32_t rd(uint32_t r)
{
    uint32_t v = sim_reg[r / 4];
    if (r == SIM_HW_CFG && lrst_reads > 0 && lrst_reads--)
        v |= 0x08;
    if (r == SIM_PM_CTRL && phyrst_reads > 0 && phyrst_reads--)
        v |= 0x10;
    return v;
}

static void wr(uint32_t r, uint32_t v)
{
    sim_reg_writes++;
    if (r == SIM_HW_CFG && (v & 0x08)) {        /* lite reset */
        sim_lrsts++;
        lrst_reads = 2;
        memset(sim_reg, 0, sizeof sim_reg);
        sim_reg[0] = 0xEC000002;                /* ID_REV */
        return;
    }
    if (r == SIM_PM_CTRL && (v & 0x10)) {
        sim_phyrsts++;
        phyrst_reads = 1;
        return;
    }
    if (r == MII_ADDR && (v & 1)) {
        uint32_t id = (v >> 11) & 0x1F, idx = (v >> 6) & 0x1F;
        SIM_CHECK(id == 1);
        if (v & 2) {
            sim_phy[idx] = (uint16_t)sim_reg[MII_DATA / 4];
        } else {
            uint16_t val = sim_phy[idx];
            if (idx == 1) {                     /* BMSR: the link bit latches a loss */
                val = (uint16_t)((val & ~4u) | (link_up && !link_lost ? 4u : 0u));
                link_lost = 0;
            }
            sim_reg[MII_DATA / 4] = val;
        }
        sim_reg[r / 4] = v & ~1u;               /* done at once */
        return;
    }
    sim_reg[r / 4] = v;
}

int usb_control(usb_dev_t *d, uint8_t req_type, uint8_t req, uint16_t value,
                uint16_t index, void *data, uint16_t len)
{
    uint8_t *b = data;
    SIM_CHECK(d == &sim_dev);
    SIM_CHECK(value == 0 && len == 4 && index < sizeof sim_reg);
    if (req_type == 0xC0 && req == 0xA1) {
        uint32_t v = rd(index);
        b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
        return 4;
    }
    if (req_type == 0x40 && req == 0xA0) {
        wr(index, (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
        return 4;
    }
    SIM_CHECK(!"unexpected control request");
    return XFER_STALL;
}

/* ---- frames ---- */
static struct { uint8_t data[1600]; uint32_t len, status; } rxq[16];
static int rx_head, rx_tail;

void sim_queue_rx(const uint8_t *frame, uint32_t len, int error)
{
    SIM_CHECK((rx_tail + 1) % 16 != rx_head && len <= 1600);
    memcpy(rxq[rx_tail].data, frame, len);
    rxq[rx_tail].len = len;
    rxq[rx_tail].status = (len + 4) << 16 | (error ? 0x8002u : 0);
    rx_tail = (rx_tail + 1) % 16;
}

int usb_bulk(usb_dev_t *d, uint8_t ep, int in, uint16_t mps, uint8_t *toggle,
             void *buf, uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    uint8_t *b = buf;
    (void)timeout_ms;
    SIM_CHECK(d == &sim_dev && mps == 512);
    if (in) {
        SIM_CHECK(ep == 1);
        if (!(sim_reg[SIM_MAC_CR / 4] & 4) || rx_head == rx_tail) {    /* RXEN */
            if (!(sim_reg[SIM_HW_CFG / 4] & 0x1000))
                return XFER_TIMEOUT;            /* NAK until the timeout */
            *actual = 0;
            return XFER_OK;
        }
        uint32_t fl = rxq[rx_head].len, s = rxq[rx_head].status;
        SIM_CHECK(len >= fl + 8);
        b[0] = (uint8_t)s; b[1] = (uint8_t)(s >> 8); b[2] = (uint8_t)(s >> 16); b[3] = (uint8_t)(s >> 24);
        memcpy(b + 4, rxq[rx_head].data, fl);
        memset(b + 4 + fl, 0xCC, 4);            /* the CRC */
        *actual = fl + 8;
        *toggle ^= PID_DATA1;
        rx_head = (rx_head + 1) % 16;
        return XFER_OK;
    }
    SIM_CHECK(ep == 2);
    SIM_CHECK((sim_reg[SIM_MAC_CR / 4] & 8) && (sim_reg[SIM_TX_CFG / 4] & 4));  /* TXEN, TX on */
    if (sim_on_out)
        sim_on_out(b, len);
    if (actual)
        *actual = len;
    return XFER_OK;
}
