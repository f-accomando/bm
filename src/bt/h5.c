/*
 * H5, the Bluetooth three-wire UART transport (Core spec v5, Vol 4 Part D),
 * as Linux's hci_h5.c speaks it to the Realtek chips: SLIP framing, a
 * 4-byte header with sequence and acknowledgement numbers, reliable
 * commands and ACL data, no CRC from our side (Linux never sends it; we
 * check the controller's when present). Single-threaded and polled, one
 * reliable packet in flight: h5_write() returns when it is acknowledged.
 *
 * The byte layer is btuart.h (8 data bits, even parity on Realtek).
 */
#include "h5.h"
#include "btuart.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#define SLIP_END        0xc0
#define SLIP_ESC        0xdb
#define SLIP_ESC_END    0xdc
#define SLIP_ESC_ESC    0xdd

#define T_ACK           0
#define T_LINK          15

#define MAX_FRAME       (4 + 1100 + 2)
#define RXQ             8
#define RETRANSMIT_US   250000u
#define TRIES           12

enum { UNINIT, INIT, ACTIVE };

static struct {
    int state;
    uint8_t tx_seq;             /* next sequence number we send */
    uint8_t rx_expect;          /* next sequence number we take (our ack field) */
    int ack_pending;
    int outstanding;            /* a reliable packet waits for its ack */
    uint8_t out_seq;
    uint8_t out_type;
    uint16_t out_len;
    uint8_t out[MAX_FRAME];     /* its payload, for retransmissions */
    uint32_t out_time;
    unsigned retransmits;
    /* receive */
    uint8_t frame[MAX_FRAME];
    unsigned flen;
    int in_frame, esc, overflow;
    struct { uint8_t type; uint16_t len; uint8_t data[1028]; } q[RXQ];
    unsigned qh, qt;
    unsigned crc_errors, bad_headers;
    int peer_config;            /* config byte of the CONFIG RESP, -1 none */
} h;

/* --- framing --- */

static uint16_t crc_ccitt(uint16_t crc, const uint8_t *p, unsigned n)
{
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static uint16_t bitrev16(uint16_t v)
{
    uint16_t r = 0;
    for (int i = 0; i < 16; i++)
        if (v & (1u << i))
            r |= (uint16_t)(1u << (15 - i));
    return r;
}

uint16_t h5_crc(const uint8_t *p, unsigned n)
{
    return bitrev16(crc_ccitt(0xffff, p, n));
}

static void put_slip(const uint8_t *p, unsigned n)
{
    uint8_t buf[64];
    unsigned k = 0;
    for (unsigned i = 0; i < n; i++) {
        if (k > sizeof buf - 2) {
            btuart_write(buf, k);
            k = 0;
        }
        if (p[i] == SLIP_END) {
            buf[k++] = SLIP_ESC;
            buf[k++] = SLIP_ESC_END;
        } else if (p[i] == SLIP_ESC) {
            buf[k++] = SLIP_ESC;
            buf[k++] = SLIP_ESC_ESC;
        } else {
            buf[k++] = p[i];
        }
    }
    if (k)
        btuart_write(buf, k);
}

static void send_frame(int reliable, uint8_t seq, uint8_t type,
                       const uint8_t *a, unsigned alen, const uint8_t *b, unsigned blen)
{
    unsigned len = alen + blen;
    uint8_t hdr[4];
    hdr[0] = (uint8_t)((reliable ? 0x80 : 0) | (h.rx_expect & 7) << 3 | (reliable ? seq & 7 : 0));
    hdr[1] = (uint8_t)(type | (len & 0xf) << 4);
    hdr[2] = (uint8_t)(len >> 4);
    hdr[3] = (uint8_t)~(hdr[0] + hdr[1] + hdr[2]);
    static const uint8_t end = SLIP_END;
    btuart_write(&end, 1);
    put_slip(hdr, 4);
    if (alen)
        put_slip(a, alen);
    if (blen)
        put_slip(b, blen);
    btuart_write(&end, 1);
    h.ack_pending = 0;
}

static void send_link(const uint8_t *msg, unsigned n)
{
    send_frame(0, 0, T_LINK, msg, n, NULL, 0);
}

static const uint8_t m_sync[] = { 0x01, 0x7e }, m_sync_resp[] = { 0x02, 0x7d };
static const uint8_t m_config[] = { 0x03, 0xfc, 0x14 }, m_config_resp[] = { 0x04, 0x7b };
static const uint8_t m_woken[] = { 0x06, 0xf9 };

/* --- receiving --- */

static void link_msg(const uint8_t *p, unsigned n)
{
    if (n < 2)
        return;
    if (p[0] == 0x01 && p[1] == 0x7e) {             /* SYNC */
        send_link(m_sync_resp, 2);
        if (h.state == ACTIVE) {                    /* the controller restarted */
            h.state = UNINIT;
            h.tx_seq = h.rx_expect = 0;
            h.outstanding = 0;
        }
    } else if (p[0] == 0x02 && p[1] == 0x7d) {      /* SYNC RESP */
        if (h.state == UNINIT)
            h.state = INIT;
    } else if (p[0] == 0x03 && p[1] == 0xfc) {      /* CONFIG */
        send_link(m_config_resp, 2);
    } else if (p[0] == 0x04 && p[1] == 0x7b) {      /* CONFIG RESP */
        h.peer_config = n > 2 ? p[2] : -1;
        if (h.state == INIT)
            h.state = ACTIVE;
    } else if (p[0] == 0x05 && p[1] == 0xfa) {      /* WAKEUP */
        send_link(m_woken, 2);
    }
}

static void frame_done(void)
{
    const uint8_t *f = h.frame;
    unsigned n = h.flen;
    if (n < 4 || (uint8_t)(f[0] + f[1] + f[2] + f[3]) != 0xff) {
        h.bad_headers++;
        return;
    }
    unsigned len = (f[1] >> 4) | (unsigned)f[2] << 4;
    int has_crc = f[0] & 0x40;
    if (n != 4 + len + (has_crc ? 2u : 0u)) {
        h.bad_headers++;
        return;
    }
    if (has_crc && h5_crc(f, 4 + len) != (uint16_t)(f[4 + len] << 8 | f[5 + len])) {
        h.crc_errors++;
        return;
    }
    unsigned type = f[1] & 15;
    if (f[0] & 0x80) {                              /* reliable */
        int full = (h.qh + 1) % RXQ == h.qt;
        if ((f[0] & 7) != h.rx_expect || full) {
            h.ack_pending = 1;                      /* not taken: re-ack the old number */
            return;
        }
        h.rx_expect = (h.rx_expect + 1) & 7;
        h.ack_pending = 1;
    }
    uint8_t ack = (f[0] >> 3) & 7;
    if (h.outstanding && ack == ((h.out_seq + 1) & 7))
        h.outstanding = 0;
    if (type == T_LINK) {
        link_msg(f + 4, len);
    } else if ((type == 2 || type == 4) && h.state == ACTIVE) {
        if ((h.qh + 1) % RXQ == h.qt || len > sizeof h.q[0].data)
            return;
        h.q[h.qh].type = (uint8_t)type;
        h.q[h.qh].len = (uint16_t)len;
        memcpy(h.q[h.qh].data, f + 4, len);
        h.qh = (h.qh + 1) % RXQ;
    }
}

static void rx_byte(uint8_t c)
{
    if (c == SLIP_END) {
        if (h.in_frame && h.flen && !h.overflow)
            frame_done();
        h.in_frame = 1;
        h.flen = 0;
        h.esc = h.overflow = 0;
        return;
    }
    if (!h.in_frame)
        return;
    if (h.esc) {
        h.esc = 0;
        if (c == SLIP_ESC_END)
            c = SLIP_END;
        else if (c == SLIP_ESC_ESC)
            c = SLIP_ESC;
        else {
            h.in_frame = 0;                         /* bad escape: drop */
            return;
        }
    } else if (c == SLIP_ESC) {
        h.esc = 1;
        return;
    }
    if (h.flen < sizeof h.frame)
        h.frame[h.flen++] = c;
    else
        h.overflow = 1;
}

/* Reads what the UART has (waiting up to wait_us for the first byte),
 * acks, retransmits. */
static void poll(uint32_t wait_us)
{
    int c = btuart_read(wait_us);
    while (c >= 0) {
        rx_byte((uint8_t)c);
        c = btuart_ready() ? btuart_read(0) : -1;
    }
    if (h.outstanding && timer_ticks() - h.out_time > RETRANSMIT_US) {
        h.retransmits++;
        send_frame(1, h.out_seq, h.out_type, h.out, h.out_len, NULL, 0);
        h.out_time = timer_ticks();
    }
    if (h.ack_pending)
        send_frame(0, 0, T_ACK, NULL, 0, NULL, 0);
}

/* --- the link --- */

int h5_open(uint32_t timeout_ms)
{
    memset(&h, 0, sizeof h);
    h.peer_config = -1;
    btuart_drain();
    uint32_t t0 = timer_ticks(), last = 0;
    int sent = 0;
    while (h.state != ACTIVE) {
        uint32_t now = timer_ticks();
        if (now - t0 > timeout_ms * 1000u)
            return h.state == INIT ? -2 : -1;
        if (!sent || now - last > 100000) {         /* every 100 ms, as Linux */
            if (h.state == UNINIT)
                send_link(m_sync, 2);
            else
                send_link(m_config, 3);
            last = now;
            sent = 1;
        }
        poll(10000);
    }
    return 0;
}

int h5_active(void)
{
    return h.state == ACTIVE;
}

int h5_peer_config(void)
{
    return h.peer_config;
}

void h5_stats(unsigned *retransmits, unsigned *crc_errors, unsigned *bad_headers)
{
    *retransmits = h.retransmits;
    *crc_errors = h.crc_errors;
    *bad_headers = h.bad_headers;
}

static int h5_read(hci_pkt_t *p, uint32_t timeout_us)
{
    uint32_t t0 = timer_ticks();
    for (;;) {
        if (h.qt != h.qh) {
            p->type = h.q[h.qt].type;
            p->len = h.q[h.qt].len;
            memcpy(p->data, h.q[h.qt].data, p->len);
            h.qt = (h.qt + 1) % RXQ;
            return 0;
        }
        uint32_t spent = timer_ticks() - t0;
        if (spent >= timeout_us) {
            poll(0);
            return h.qt != h.qh ? h5_read(p, 0) : -1;
        }
        uint32_t left = timeout_us - spent;
        poll(left < 2000 ? left : 2000);
    }
}

static void h5_write(uint8_t type, const uint8_t *hdr, unsigned hlen,
                     const uint8_t *data, unsigned dlen)
{
    if (h.state != ACTIVE || hlen + dlen > sizeof h.out)
        return;
    uint32_t t0 = timer_ticks();
    while (h.outstanding && timer_ticks() - t0 < RETRANSMIT_US * TRIES)
        poll(1000);
    memcpy(h.out, hdr, hlen);
    if (dlen)
        memcpy(h.out + hlen, data, dlen);
    h.out_len = (uint16_t)(hlen + dlen);
    h.out_type = type;
    h.out_seq = h.tx_seq;
    h.tx_seq = (h.tx_seq + 1) & 7;
    h.outstanding = 1;
    h.out_time = timer_ticks();
    send_frame(1, h.out_seq, type, h.out, h.out_len, NULL, 0);
    t0 = timer_ticks();
    while (h.outstanding && timer_ticks() - t0 < RETRANSMIT_US * TRIES)
        poll(1000);
    h.outstanding = 0;                              /* given up: carry on */
}

static int h5_ready(void)
{
    if (h.qt != h.qh)
        return 1;
    if (btuart_ready())
        poll(0);
    return h.qt != h.qh;
}

const hci_transport_t h5_transport = { h5_read, h5_write, h5_ready };
