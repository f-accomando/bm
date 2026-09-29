/* lwIP configuration for bm33: no operating system (NO_SYS), polled from
 * the main loop, IPv4 only, one network interface (the WiFi chip). */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_ICMP               1
#define LWIP_RAW                0
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_DHCP               1
#define LWIP_DNS                1
#define LWIP_ACD                0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_AUTOIP             0
#define LWIP_IGMP               0
#define LWIP_NETIF_HOSTNAME     1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_STATS              0

/* memory from lwIP's own pools (heap: malloc) */
#define MEM_LIBC_MALLOC         1
#define MEMP_MEM_MALLOC         1
#define MEM_ALIGNMENT           4
#define PBUF_POOL_SIZE          32
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_UDP_PCB        6
#define MEMP_NUM_TCP_SEG        64

#define TCP_MSS                 1460
#define TCP_WND                 (8 * TCP_MSS)
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        32

#define LWIP_CHKSUM_ALGORITHM   3

/* network time (M19: certificates have dates), from pool.ntp.org */
#define SNTP_SERVER_DNS         1
#define SNTP_SERVER_ADDRESS     "pool.ntp.org"
#define SNTP_STARTUP_DELAY      0
#define SNTP_UPDATE_DELAY       3600000
#define SNTP_SET_SYSTEM_TIME(sec) net_time_set((unsigned long)(sec))
void net_time_set(unsigned long sec);
#define DNS_MAX_SERVERS         2

#ifdef BM33_HOST_TEST   /* tests/net: a loopback interface instead of the chip */
#define LWIP_HAVE_LOOPIF        1
#define LWIP_NETIF_LOOPBACK     1
#endif

#endif
