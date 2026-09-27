#include "hci.h"
#include "btuart.h"

#include <string.h>

#define H4_COMMAND  0x01
#define H4_ACL      0x02
#define H4_EVENT    0x04

#define EV_COMMAND_COMPLETE 0x0E
#define EV_COMMAND_STATUS   0x0F

/* Reads one H4 packet; returns its type, or -1. */
static int read_packet(uint8_t *buf, uint32_t size, uint32_t *len, uint32_t timeout_us)
{
    int type = btuart_read(timeout_us);
    if (type < 0)
        return -1;
    uint32_t hdr = type == H4_EVENT ? 2 : type == H4_ACL ? 4 : 0;
    if (!hdr)
        return -1;                              /* out of sync */
    for (uint32_t i = 0; i < hdr; i++) {
        int c = btuart_read(100000);
        if (c < 0)
            return -1;
        buf[i] = (uint8_t)c;
    }
    uint32_t plen = type == H4_EVENT ? buf[1] : (uint32_t)(buf[2] | buf[3] << 8);
    for (uint32_t i = 0; i < plen; i++) {
        int c = btuart_read(100000);
        if (c < 0)
            return -1;
        if (hdr + i < size)
            buf[hdr + i] = (uint8_t)c;
    }
    *len = hdr + plen;
    return type;
}

int hci_cmd(uint16_t opcode, const void *params, uint8_t len,
            uint8_t *ret, uint8_t ret_size, uint32_t timeout_us)
{
    uint8_t hdr[4] = { H4_COMMAND, (uint8_t)opcode, (uint8_t)(opcode >> 8), len };
    btuart_write(hdr, 4);
    if (len)
        btuart_write(params, len);

    static uint8_t pkt[300];
    for (;;) {
        uint32_t n;
        int type = read_packet(pkt, sizeof pkt, &n, timeout_us);
        if (type < 0)
            return -1;
        if (type != H4_EVENT)
            continue;
        const uint8_t *p = pkt + 2;
        if (pkt[0] == EV_COMMAND_COMPLETE && pkt[1] >= 4 &&
            (p[1] | p[2] << 8) == opcode) {
            if (ret && ret_size) {
                uint32_t r = pkt[1] - 4u;
                memcpy(ret, p + 4, r < ret_size ? r : ret_size);
            }
            return p[3];
        }
        if (pkt[0] == EV_COMMAND_STATUS && pkt[1] >= 4 && (p[2] | p[3] << 8) == opcode)
            return p[0];
        /* another event: not the answer, keep waiting */
    }
}

int hci_event(uint8_t *buf, uint32_t size, uint32_t timeout_us)
{
    for (;;) {
        uint32_t n;
        int type = read_packet(buf, size, &n, timeout_us);
        if (type < 0)
            return -1;
        if (type == H4_EVENT)
            return (int)(n < size ? n : size);
    }
}
