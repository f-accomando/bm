/*
 * lwIP on the WiFi chip: one Ethernet interface ("wl") whose frames go
 * through wifi_send / wifi_recv, NO_SYS and polled (net_poll from the
 * input loops). DHCP gives the address; the hostname is "bm33".
 */
#include "net.h"
#include "netcon.h"
#include "netxfer.h"
#include "wifi/wifi.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/apps/sntp.h"
#include "netif/ethernet.h"

#include <string.h>
#include <time.h>

static struct netif nif;
static int started;
static uint32_t last_poll, shown_ip;
static char ip_text[16] = "-";

/* lwIP's clock, in ms: the 1 MHz counter wraps every 71 minutes, the
 * milliseconds only after 49 days */
static uint32_t now_ms, now_rest, now_last;

u32_t sys_now(void)
{
    uint32_t t = timer_ticks();
    now_rest += t - now_last;
    now_last = t;
    now_ms += now_rest / 1000;
    now_rest %= 1000;
    return now_ms;
}

static err_t link_output(struct netif *n, struct pbuf *p)
{
    static uint8_t buf[1536];
    (void)n;
    if (p->tot_len > sizeof buf)
        return ERR_BUF;
    pbuf_copy_partial(p, buf, p->tot_len, 0);
    return wifi_send(buf, p->tot_len) == 0 ? ERR_OK : ERR_IF;
}

static err_t wl_init(struct netif *n)
{
    n->name[0] = 'w';
    n->name[1] = 'l';
    n->hwaddr_len = 6;
    memcpy(n->hwaddr, wifi_mac(), 6);
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    n->output = etharp_output;
    n->linkoutput = link_output;
    netif_set_hostname(n, "bm33");
    return ERR_OK;
}

static void show_ip(void)
{
    uint32_t ip = netif_is_up(&nif) ? ip4_addr_get_u32(netif_ip4_addr(&nif)) : 0;
    if (ip == shown_ip)
        return;
    shown_ip = ip;
    if (!ip) {
        strcpy(ip_text, "-");
        kprintf("net: address lost\n");
        return;
    }
    ip4addr_ntoa_r(netif_ip4_addr(&nif), ip_text, sizeof ip_text);
    char gw[16];
    ip4addr_ntoa_r(netif_ip4_gw(&nif), gw, sizeof gw);
    kprintf("\x1b[92mnet: IP %s\x1b[0m (gateway %s, name bm33)\n", ip_text, gw);
    if (!sntp_enabled()) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_init();
    }
    if (netcon_start() == 0) {
        netxfer_start();
        kprintf("net: console on port %d, password %s\n"
                "     from the PC: python3 tools/bm33_net.py %s\n",
                NETCON_PORT, netcon_password(), ip_text);
    }
}

int net_start(void)
{
    if (!wifi_linked())
        return -1;
    if (!started) {
        now_last = timer_ticks();
        lwip_init();
        if (!netif_add(&nif, NULL, NULL, NULL, NULL, wl_init, ethernet_input)) {
            kprintf("\x1b[91mnet: interface not added\x1b[0m\n");
            return -1;
        }
        netif_set_default(&nif);
        started = 1;
    } else {
        dhcp_release_and_stop(&nif);
    }
    netif_set_up(&nif);
    netif_set_link_up(&nif);
    kprintf("net: asking for an address (DHCP)...\n");
    return dhcp_start(&nif) == ERR_OK ? 0 : -1;
}

static void poll_now(void)
{
    static uint8_t buf[1536];
    wifi_poll();
    int n;
    while ((n = wifi_recv(buf, sizeof buf)) > 0) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
        if (!p)
            break;
        pbuf_take(p, buf, (u16_t)n);
        if (nif.input(p, &nif) != ERR_OK)
            pbuf_free(p);
    }
    if (!wifi_linked() && netif_is_link_up(&nif)) {
        dhcp_release_and_stop(&nif);
        netif_set_link_down(&nif);
        netif_set_down(&nif);
    }
    sys_check_timeouts();
    show_ip();
    netcon_poll();
    netxfer_poll();
}

void net_wait_step(void)
{
    if (!started)
        return;
    poll_now();
    timer_delay_us(200);
}

static unsigned long time_base;         /* seconds at time_ms */
static uint32_t time_ms;

void net_time_set(unsigned long sec)
{
    int first = time_base == 0;
    time_base = sec;
    time_ms = sys_now();
    if (first)
        kprintf("net: time %s\n", net_time_text());
}

unsigned long net_time(void)
{
    if (!time_base)
        return 0;
    return time_base + (sys_now() - time_ms) / 1000;
}

const char *net_time_text(void)
{
    static char buf[32];
    time_t t = (time_t)net_time();
    if (!t)
        return "unknown";
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M UTC", &tm);
    return buf;
}

void net_poll(void)
{
    if (!started)
        return;
    uint32_t t = timer_ticks();
    if (t - last_poll < 1000)
        return;
    last_poll = t;
    poll_now();
}

int net_wait_ip(uint32_t ms)
{
    uint32_t t0 = timer_ticks();
    while (started && timer_ticks() - t0 < ms * 1000u) {
        poll_now();
        if (shown_ip)
            return 0;
        timer_delay_us(500);
    }
    if (started)
        kprintf("\x1b[91mnet: no address from DHCP in %lu s\x1b[0m\n", ms / 1000);
    return -1;
}

uint32_t net_ip(void)
{
    return shown_ip;
}

const char *net_ip_text(void)
{
    return ip_text;
}
