/* Host tests for src/usb/smsc95xx.c against a simulated LAN9512
 * (lan9512_sim.c): set-up, link and duplex, frames in both directions. */
#include "usb/smsc95xx.h"
#include "usb/dwc2.h"
#include "lan9512_sim.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static char logbuf[4096];
int kprintf(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(logbuf);
    va_start(ap, fmt);
    int r = vsnprintf(logbuf + n, sizeof logbuf - n, fmt, ap);
    va_end(ap);
    return r;
}

static uint8_t out[8][1600];
static uint32_t out_len[8];
static int nout;

static void on_out(const uint8_t *data, uint32_t len)
{
    CHECK(nout < 8 && len <= sizeof out[0]);
    if (nout < 8) {
        memcpy(out[nout], data, len);
        out_len[nout++] = len;
    }
}

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void link_now(int up, uint16_t lpa)
{
    sim_link(up, lpa);
    sim_now_us += 600000;
    logbuf[0] = '\0';
    eth_poll();
}

int main(void)
{
    uint8_t frame[1600], got[1600];
    for (int i = 0; i < (int)sizeof frame; i++)
        frame[i] = (uint8_t)(i * 7 + 3);
    sim_on_out = on_out;

    CHECK(eth_match(0x0424, 0xEC00));
    CHECK(!eth_match(0x0424, 0x9512));          /* the hub itself */
    CHECK(!eth_present() && eth_recv(got, sizeof got) == 0 && eth_send(frame, 60) == -1);

    /* set up as Linux does: resets, MAC, empty-packet reads, PHY, TX/RX on */
    CHECK(eth_attach(&sim_dev, sim_cfg, sizeof sim_cfg) == 0);
    CHECK(eth_present());
    CHECK(sim_lrsts == 1 && sim_phyrsts == 1);
    CHECK(sim_reg[SIM_HW_CFG / 4] & 0x1000);    /* BIR */
    CHECK(!(sim_reg[SIM_HW_CFG / 4] & 0x22));   /* one frame per transfer */
    CHECK(sim_reg[SIM_ADDRL / 4] == 0x12eb27b8 && sim_reg[SIM_ADDRH / 4] == 0x5634);
    CHECK(memcmp(eth_mac(), sim_board_mac, 6) == 0);
    CHECK((sim_reg[SIM_MAC_CR / 4] & 0x0C) == 0x0C);   /* TXEN, RXEN */
    CHECK(sim_reg[SIM_TX_CFG / 4] & 4);
    CHECK(sim_phy[4] == 0x01E1 && (sim_phy[0] & 0x1200) == 0x1200);
    eth_print();
    CHECK(strstr(logbuf, "MAC b8:27:eb:12:34:56, no link yet") != NULL);

    /* no cable */
    link_now(0, 0);
    CHECK(!eth_linked());
    /* link: 100 Mbit/s full duplex; the MAC follows */
    link_now(1, 0x45E1);
    CHECK(eth_linked());
    CHECK(strstr(logbuf, "link up, 100 Mbit/s full duplex") != NULL);
    CHECK((sim_reg[SIM_MAC_CR / 4] & 0x00100000) && !(sim_reg[SIM_MAC_CR / 4] & 0x00800000));
    /* polled again within 500 ms: no MII traffic */
    int w = sim_reg_writes;
    eth_poll();
    CHECK(sim_reg_writes == w);

    /* receive: nothing, a frame, a bad one skipped */
    CHECK(eth_recv(got, sizeof got) == 0);
    sim_queue_rx(frame, 60, 0);
    CHECK(eth_recv(got, sizeof got) == 60 && memcmp(got, frame, 60) == 0);
    sim_queue_rx(frame, 100, 1);
    sim_queue_rx(frame + 1, 1514, 0);
    CHECK(eth_recv(got, sizeof got) == 1514 && memcmp(got, frame + 1, 1514) == 0);
    CHECK(eth_recv(got, sizeof got) == 0);
    sim_queue_rx(frame, 200, 0);
    CHECK(eth_recv(got, 100) == 0);             /* bigger than the caller's buffer */

    /* send: 8-byte command header; an empty packet after whole packets */
    CHECK(eth_send(frame, 100) == 0);
    CHECK(nout == 1 && out_len[0] == 108);
    CHECK(le32(out[0]) == (100 | 0x3000) && le32(out[0] + 4) == 100);
    CHECK(memcmp(out[0] + 8, frame, 100) == 0);
    CHECK(eth_send(frame, 504) == 0);
    CHECK(nout == 3 && out_len[1] == 512 && out_len[2] == 0);
    CHECK(eth_send(frame, 1514) == 0);
    CHECK(nout == 4 && out_len[3] == 1522);
    CHECK(eth_send(frame, 2000) == -1 && nout == 4);

    /* cable out, then in on a 100 Mbit/s half-duplex hub */
    link_now(0, 0);
    CHECK(!eth_linked() && strstr(logbuf, "link down") != NULL);
    link_now(1, 0x00A1);
    CHECK(eth_linked() && strstr(logbuf, "100 Mbit/s half duplex") != NULL);
    CHECK(!(sim_reg[SIM_MAC_CR / 4] & 0x00100000) && (sim_reg[SIM_MAC_CR / 4] & 0x00800000));
    /* a loss between two polls, the link back already: still up (the
     * second BMSR read is the state now) */
    sim_link(0, 0x00A1);
    sim_link(1, 0x00A1);
    sim_now_us += 600000;
    eth_poll();
    CHECK(eth_linked());

    /* a device without bulk endpoints is refused */
    eth_detach();
    CHECK(eth_attach(&sim_dev, sim_cfg, 18) == -1 && !eth_present());

    fails += sim_fails;
    printf("test_eth: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
