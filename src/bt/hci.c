#include "hci.h"
#include "btuart.h"
#include "drivers/timer.h"

#include <string.h>

#define H4_COMMAND  0x01

#define EV_COMMAND_COMPLETE 0x0E
#define EV_COMMAND_STATUS   0x0F
#define EV_COMPLETED_PKTS   0x13

#define QUEUE 8
static hci_pkt_t queue[QUEUE];
static unsigned q_head, q_tail;
static hci_pkt_t scratch;

/* One packet from the UART (H4); 0, or -1 (timeout or out of sync). */
static int h4_read(hci_pkt_t *p, uint32_t timeout_us)
{
    int type = btuart_read(timeout_us);
    if (type < 0)
        return -1;
    uint32_t hdr = type == HCI_EVENT ? 2 : type == HCI_ACL ? 4 : 0;
    if (!hdr)
        return -1;
    for (uint32_t i = 0; i < hdr; i++) {
        int c = btuart_read(100000);
        if (c < 0)
            return -1;
        p->data[i] = (uint8_t)c;
    }
    uint32_t plen = type == HCI_EVENT ? p->data[1] : (uint32_t)(p->data[2] | p->data[3] << 8);
    for (uint32_t i = 0; i < plen; i++) {
        int c = btuart_read(100000);
        if (c < 0)
            return -1;
        if (hdr + i < sizeof p->data)
            p->data[hdr + i] = (uint8_t)c;
    }
    p->type = (uint8_t)type;
    p->len = (uint16_t)(hdr + plen < sizeof p->data ? hdr + plen : sizeof p->data);
    return 0;
}

static void h4_write(uint8_t type, const uint8_t *hdr, unsigned hlen,
                     const uint8_t *data, unsigned dlen)
{
    btuart_write(&type, 1);
    btuart_write(hdr, hlen);
    if (dlen)
        btuart_write(data, dlen);
}

static const hci_transport_t h4 = { h4_read, h4_write, btuart_ready };
static const hci_transport_t *tr = &h4;

void hci_set_transport(const hci_transport_t *t)
{
    tr = t ? t : &h4;
}

static int read_packet(hci_pkt_t *p, uint32_t timeout_us)
{
    return tr->read(p, timeout_us);
}

static void enqueue(const hci_pkt_t *p)
{
    if ((q_head + 1) % QUEUE == q_tail)
        return;                                 /* full: drop the newest */
    queue[q_head] = *p;
    q_head = (q_head + 1) % QUEUE;
}

void hci_flush(void)
{
    q_head = q_tail = 0;
}

int hci_pending(void)
{
    return q_head != q_tail || tr->ready();
}

int hci_recv(hci_pkt_t *p, uint32_t timeout_us)
{
    if (q_head != q_tail) {
        *p = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE;
        return 0;
    }
    for (;;) {
        if (read_packet(p, timeout_us) != 0)
            return -1;
        /* answers to hci_send() commands and flow-control counts: not ours */
        if (p->type == HCI_EVENT && (p->data[0] == EV_COMMAND_COMPLETE ||
                                     p->data[0] == EV_COMMAND_STATUS ||
                                     p->data[0] == EV_COMPLETED_PKTS))
            continue;
        return 0;
    }
}

void hci_send(uint16_t opcode, const void *params, uint8_t len)
{
    uint8_t hdr[3] = { (uint8_t)opcode, (uint8_t)(opcode >> 8), len };
    tr->write(H4_COMMAND, hdr, 3, params, len);
}

int hci_cmd(uint16_t opcode, const void *params, uint8_t len,
            uint8_t *ret, uint8_t ret_size, uint32_t timeout_us)
{
    hci_send(opcode, params, len);
    const uint32_t t0 = timer_ticks();
    for (;;) {
        hci_pkt_t *p = &scratch;
        /* other packets keep coming: bound the whole wait, not each read */
        if (timer_ticks() - t0 > timeout_us * 2 || read_packet(p, timeout_us) != 0)
            return -1;
        if (p->type == HCI_EVENT) {
            const uint8_t *e = p->data + 2;
            if (p->data[0] == EV_COMMAND_COMPLETE && p->data[1] >= 4 &&
                (e[1] | e[2] << 8) == opcode) {
                if (ret && ret_size) {
                    uint32_t r = p->data[1] - 4u;
                    memcpy(ret, e + 4, r < ret_size ? r : ret_size);
                }
                return e[3];
            }
            if (p->data[0] == EV_COMMAND_STATUS && p->data[1] >= 4 &&
                (e[2] | e[3] << 8) == opcode)
                return e[0];
            if (p->data[0] == EV_COMMAND_COMPLETE || p->data[0] == EV_COMMAND_STATUS ||
                p->data[0] == EV_COMPLETED_PKTS)
                continue;
        }
        enqueue(p);                             /* for the stack, later */
    }
}

void hci_acl_send(uint16_t handle, const void *data, uint16_t len)
{
    uint8_t hdr[4] = { (uint8_t)handle, (uint8_t)((handle >> 8) | 0x20),
                       (uint8_t)len, (uint8_t)(len >> 8) };
    tr->write(HCI_ACL, hdr, 4, data, len);
}

void hci_acl_send_pb(uint16_t handle, int pb, const void *data, uint16_t len)
{
    uint8_t hdr[4] = { (uint8_t)handle, (uint8_t)((handle >> 8) | (pb & 3) << 4),
                       (uint8_t)len, (uint8_t)(len >> 8) };
    tr->write(HCI_ACL, hdr, 4, data, len);
}

const char *hci_reason(uint8_t reason)
{
    switch (reason) {
    case 0x05: return "it refused the saved key: pair it again";
    case 0x06: return "it has no key for this console: pair it again";
    case 0x08: return "radio link lost (distance, WiFi, battery)";
    case 0x13: return "it closed the link (switched off, or another console)";
    case 0x14: return "it closed the link (low resources)";
    case 0x15: return "it is switching off";
    case 0x16: return "bm closed the link";
    case 0x1A: return "it does not support what bm asked";
    case 0x22: return "no answer from it (LMP timeout)";
    case 0x28: return "it took too long to answer";
    case 0x3B: return "it refused the connection parameters";
    case 0x3D: return "encryption failed (MIC)";
    case 0x3E: return "the connection did not start";
    default:   return "reason not known";
    }
}
