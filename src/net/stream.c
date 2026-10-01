/*
 * stream_t on lwIP's raw API: received pbufs are chained until read; the
 * waits run net_poll (net_wait_step).
 */
#include "stream.h"
#include "net.h"
#include "drivers/timer.h"

#include "lwip/dns.h"
#include "lwip/tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct stream {
    struct tcp_pcb *pcb;
    struct pbuf *rx;            /* received, not read yet */
    unsigned rx_off;            /* bytes of rx->payload already read */
    int connected, closed, failed;
};

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    stream_t *s = arg;
    (void)pcb;
    if (!p || err != ERR_OK) {
        s->closed = 1;
        if (p)
            pbuf_free(p);
        return ERR_OK;
    }
    if (s->rx)
        pbuf_cat(s->rx, p);
    else
        s->rx = p;
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    stream_t *s = arg;
    (void)err;
    s->pcb = NULL;              /* freed by lwIP */
    s->failed = 1;
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    stream_t *s = arg;
    (void)pcb;
    if (err == ERR_OK)
        s->connected = 1;
    else
        s->failed = 1;
    return ERR_OK;
}

static ip_addr_t dns_addr;
static int dns_state;           /* 0 waiting, 1 found, -1 not found */
static uintptr_t dns_query;     /* the lookup waited for: a late answer to a
                                 * cancelled one does not count */

static void on_dns(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    if ((uintptr_t)arg != dns_query)
        return;
    if (addr) {
        dns_addr = *addr;
        dns_state = 1;
    } else {
        dns_state = -1;
    }
}

static int expired(uint32_t t0, uint32_t ms)
{
    return timer_ticks() - t0 > ms * 1000u;
}

stream_t *stream_open(const char *host, uint16_t port, uint32_t timeout_ms,
                      char *err, size_t err_len)
{
    if (!net_ip()) {
        snprintf(err, err_len, "no network (WiFi not connected)");
        return NULL;
    }
    uint32_t t0 = timer_ticks();
    dns_state = 0;
    err_t e = dns_gethostbyname(host, &dns_addr, on_dns, (void *)++dns_query);
    if (e == ERR_OK) {
        dns_state = 1;
    } else if (e != ERR_INPROGRESS) {
        snprintf(err, err_len, "bad host name %s", host);
        return NULL;
    }
    int cancelled = 0;
    while (dns_state == 0 && !expired(t0, timeout_ms) && !cancelled)
        cancelled = net_wait_step() < 0;
    if (cancelled) {
        snprintf(err, err_len, "cancelled");
        return NULL;
    }
    if (dns_state != 1) {
        snprintf(err, err_len, "%s: %s", host, dns_state ? "unknown name (DNS)" : "no DNS answer");
        return NULL;
    }

    stream_t *s = calloc(1, sizeof *s);
    if (!s || !(s->pcb = tcp_new_ip_type(IP_GET_TYPE(&dns_addr)))) {
        free(s);
        snprintf(err, err_len, "out of memory");
        return NULL;
    }
    tcp_arg(s->pcb, s);
    tcp_recv(s->pcb, on_recv);
    tcp_err(s->pcb, on_err);
    if (tcp_connect(s->pcb, &dns_addr, port, on_connected) != ERR_OK) {
        stream_close(s);
        snprintf(err, err_len, "cannot connect");
        return NULL;
    }
    while (!s->connected && !s->failed && !expired(t0, timeout_ms) && !cancelled)
        cancelled = net_wait_step() < 0;
    if (cancelled) {
        snprintf(err, err_len, "cancelled");
        stream_close(s);
        return NULL;
    }
    if (!s->connected) {
        snprintf(err, err_len, "%s (%s) port %u: %s", host, ipaddr_ntoa(&dns_addr), port,
                 s->failed ? "connection refused" : "no answer in 10 s");
        stream_close(s);
        return NULL;
    }
    return s;
}

int stream_write(stream_t *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t t0 = timer_ticks();
    while (len) {
        if (!s->pcb || s->failed)
            return -1;
        size_t room = tcp_sndbuf(s->pcb);
        if (room == 0) {
            tcp_output(s->pcb);
            if (expired(t0, 30000) || net_wait_step() < 0)
                return -1;
            continue;
        }
        size_t n = len < room ? len : room;
        if (n > 0xFFFF)
            n = 0xFFFF;
        if (tcp_write(s->pcb, p, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            tcp_output(s->pcb);
            if (net_wait_step() < 0)
                return -1;
            continue;
        }
        p += n;
        len -= n;
        t0 = timer_ticks();
    }
    tcp_output(s->pcb);
    return 0;
}

int stream_read(stream_t *s, void *buf, size_t len, uint32_t timeout_ms)
{
    uint32_t t0 = timer_ticks();
    while (!s->rx) {
        if (s->closed)
            return 0;
        if (s->failed || expired(t0, timeout_ms) || net_wait_step() < 0)
            return -1;
    }
    size_t got = 0;
    while (s->rx && got < len) {
        struct pbuf *p = s->rx;
        size_t n = p->len - s->rx_off;
        if (n > len - got)
            n = len - got;
        memcpy((uint8_t *)buf + got, (uint8_t *)p->payload + s->rx_off, n);
        got += n;
        s->rx_off += (unsigned)n;
        if (s->rx_off == p->len) {      /* this pbuf done: next of the chain */
            s->rx = p->next;
            if (s->rx)
                pbuf_ref(s->rx);
            pbuf_free(p);
            s->rx_off = 0;
        }
    }
    if (s->pcb)
        tcp_recved(s->pcb, (u16_t)got);
    return (int)got;
}

void stream_close(stream_t *s)
{
    if (!s)
        return;
    if (s->pcb) {
        tcp_arg(s->pcb, NULL);
        tcp_recv(s->pcb, NULL);
        tcp_err(s->pcb, NULL);
        if (tcp_close(s->pcb) != ERR_OK)
            tcp_abort(s->pcb);
    }
    if (s->rx)
        pbuf_free(s->rx);
    free(s);
}
