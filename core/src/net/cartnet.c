/*
 * UDP for the cartridges on lwIP (see cartnet.h).
 */
#include "cartnet.h"
#include "net.h"

#include <string.h>

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

typedef struct {
    uint32_t ip;
    uint16_t port, len;
    uint8_t data[CARTNET_MAX];
} packet_t;

typedef struct {
    struct udp_pcb *pcb;
    int head, count;
    packet_t q[CARTNET_QUEUE];
} sock_t;

static sock_t socks[CARTNET_SOCKETS];

static void on_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;
    sock_t *s = (sock_t *)arg;
    if (s->count < CARTNET_QUEUE && p->tot_len <= CARTNET_MAX) {
        packet_t *k = &s->q[(s->head + s->count) % CARTNET_QUEUE];
        k->len = (uint16_t)pbuf_copy_partial(p, k->data, p->tot_len, 0);
        k->ip = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr)));
        k->port = port;
        s->count++;
    }
    pbuf_free(p);                       /* a full queue drops the packet (UDP) */
}

int cartnet_open(uint16_t port)
{
    if (!net_ip())
        return -1;
    for (int i = 0; i < CARTNET_SOCKETS; i++) {
        sock_t *s = &socks[i];
        if (s->pcb)
            continue;
        struct udp_pcb *pcb = udp_new();
        if (!pcb)
            return -1;
        if (udp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
            udp_remove(pcb);
            return -1;
        }
        s->pcb = pcb;
        s->head = s->count = 0;
        udp_recv(pcb, on_recv, s);
        return i;
    }
    return -1;
}

static sock_t *get(int i)
{
    return i >= 0 && i < CARTNET_SOCKETS && socks[i].pcb ? &socks[i] : NULL;
}

uint16_t cartnet_port(int i)
{
    sock_t *s = get(i);
    return s ? s->pcb->local_port : 0;
}

int cartnet_send(int i, uint32_t ip, uint16_t port, const void *data, int len)
{
    sock_t *s = get(i);
    if (!s || len < 0 || len > CARTNET_MAX)
        return -1;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p)
        return -1;
    memcpy(p->payload, data, (size_t)len);
    ip_addr_t to;
    ip_addr_set_ip4_u32(&to, lwip_htonl(ip));
    err_t e = udp_sendto(s->pcb, p, &to, port);
    pbuf_free(p);
    return e == ERR_OK ? 0 : -1;
}

int cartnet_recv(int i, void *buf, int max, uint32_t *ip, uint16_t *port)
{
    net_poll();
    sock_t *s = get(i);
    if (!s || !s->count)
        return -1;
    packet_t *k = &s->q[s->head];
    s->head = (s->head + 1) % CARTNET_QUEUE;
    s->count--;
    int n = k->len < max ? k->len : max;
    memcpy(buf, k->data, (size_t)n);
    if (ip) *ip = k->ip;
    if (port) *port = k->port;
    return n;
}

void cartnet_close(int i)
{
    sock_t *s = get(i);
    if (!s)
        return;
    udp_remove(s->pcb);
    s->pcb = NULL;
    s->count = 0;
}

void cartnet_reset(void)
{
    for (int i = 0; i < CARTNET_SOCKETS; i++)
        cartnet_close(i);
}

uint32_t cartnet_ip(void)
{
    return lwip_ntohl(net_ip());
}

/* names: one lookup at a time */
static char dns_name[64];
static uint32_t dns_result;           /* 0 waiting, else the answer */

static void on_dns(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    (void)arg;
    dns_result = addr ? lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr))) : CARTNET_BROADCAST;
}

uint32_t cartnet_resolve(const char *name)
{
    if (!net_ip())
        return CARTNET_BROADCAST;
    ip4_addr_t lit;
    if (ip4addr_aton(name, &lit))
        return lwip_ntohl(ip4_addr_get_u32(&lit));
    if (strcmp(name, dns_name) == 0) {
        net_poll();
        return dns_result;
    }
    strncpy(dns_name, name, sizeof dns_name - 1);
    dns_name[sizeof dns_name - 1] = 0;
    dns_result = 0;
    ip_addr_t a;
    err_t e = dns_gethostbyname(dns_name, &a, on_dns, NULL);
    if (e == ERR_OK)
        dns_result = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&a)));
    else if (e != ERR_INPROGRESS)
        dns_result = CARTNET_BROADCAST;
    return dns_result;
}
