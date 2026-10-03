/*
 * bmhost: the cartridges' UDP (src/net/cartnet.h) on the PC's sockets, so
 * several bmhost on the same PC play a match together: BMHOST_NET_ID=k
 * (0..3) moves the ports a cartridge opens by 10 x k, and a broadcast goes
 * to 127.0.0.1 on the port of every k (the LAN of the consoles, on one PC).
 * The console's address is 127.0.0.1 + k (BMHOST_IP to say another);
 * BMHOST_NONET: no network at all.
 */
#include "net/cartnet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fds[CARTNET_SOCKETS] = { -1, -1 };
static int inited;

#define NET_IDS 4
static int net_id(void)
{
    const char *e = getenv("BMHOST_NET_ID");
    int k = e ? atoi(e) : 0;
    return k < 0 ? 0 : k >= NET_IDS ? NET_IDS - 1 : k;
}

static void init(void)
{
    if (inited)
        return;
    inited = 1;
    for (int i = 0; i < CARTNET_SOCKETS; i++)
        fds[i] = -1;
}

int cartnet_open(uint16_t port)
{
    init();
    if (getenv("BMHOST_NONET"))
        return -1;
    for (int i = 0; i < CARTNET_SOCKETS; i++) {
        if (fds[i] >= 0)
            continue;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
            return -1;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        struct sockaddr_in a = { 0 };
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port ? (uint16_t)(port + 10 * net_id()) : 0);
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
            close(fd);
            return -1;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fds[i] = fd;
        return i;
    }
    return -1;
}

uint16_t cartnet_port(int s)
{
    if (s < 0 || s >= CARTNET_SOCKETS || fds[s] < 0)
        return 0;
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    getsockname(fds[s], (struct sockaddr *)&a, &n);
    return ntohs(a.sin_port);
}

int cartnet_send(int s, uint32_t ip, uint16_t port, const void *data, int len)
{
    if (s < 0 || s >= CARTNET_SOCKETS || fds[s] < 0 || len < 0 || len > CARTNET_MAX)
        return -1;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    if (ip == CARTNET_BROADCAST) {
        /* every bmhost of this PC: the same port moved by each id */
        int ok = -1;
        a.sin_addr.s_addr = htonl(0x7F000001u);
        for (int k = 0; k < NET_IDS; k++) {
            a.sin_port = htons((uint16_t)(port + 10 * k));
            if (sendto(fds[s], data, (size_t)len, 0, (struct sockaddr *)&a, sizeof a) == len)
                ok = 0;
        }
        return ok;
    }
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port);
    return sendto(fds[s], data, (size_t)len, 0, (struct sockaddr *)&a, sizeof a) == len ? 0 : -1;
}

int cartnet_recv(int s, void *buf, int max, uint32_t *ip, uint16_t *port)
{
    if (s < 0 || s >= CARTNET_SOCKETS || fds[s] < 0)
        return -1;
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    ssize_t r = recvfrom(fds[s], buf, (size_t)max, 0, (struct sockaddr *)&a, &n);
    if (r < 0)
        return -1;
    if (ip) *ip = ntohl(a.sin_addr.s_addr);
    if (port) *port = ntohs(a.sin_port);
    return (int)r;
}

void cartnet_close(int s)
{
    if (s < 0 || s >= CARTNET_SOCKETS || fds[s] < 0)
        return;
    close(fds[s]);
    fds[s] = -1;
}

void cartnet_reset(void)
{
    init();
    for (int i = 0; i < CARTNET_SOCKETS; i++)
        cartnet_close(i);
}

uint32_t cartnet_ip(void)
{
    if (getenv("BMHOST_NONET"))
        return 0;
    const char *e = getenv("BMHOST_IP");
    struct in_addr a;
    if (e && inet_aton(e, &a))
        return ntohl(a.s_addr);
    return 0x7F000001u + (uint32_t)net_id();      /* 127.0.0.1, .2, ... for each BMHOST_NET_ID */
}

uint32_t cartnet_resolve(const char *name)
{
    struct in_addr a;
    if (inet_aton(name, &a))
        return ntohl(a.s_addr);
    struct addrinfo hint = { 0 }, *res = NULL;
    hint.ai_family = AF_INET;
    if (getaddrinfo(name, NULL, &hint, &res) != 0 || !res)
        return CARTNET_BROADCAST;
    uint32_t ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return ip;
}
