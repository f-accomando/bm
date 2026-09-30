/*
 * The transfer server: the receive callback fills a header, then a
 * buffer; the SD card is written from netxfer_poll, outside lwIP.
 */
#include "netxfer.h"
#include "netcon.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "fs/fat.h"
#include "lib/crc32.h"
#include "lib/printf.h"

#include "lwip/tcp.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SIZE (32u << 20)

static struct tcp_pcb *listener, *peer;
static enum { IDLE, HEADER, DATA, DONE, REPLIED } st;
static uint8_t hdr[4 + 1 + 1 + 64 + 1 + 64 + 8];
static unsigned hdr_len, need;
static char op, path[65];
static uint8_t *buf;
static uint32_t size, crc, got;
static uint32_t close_at;
static int reboot_after;
static uint8_t *play_buf;
static size_t play_len;
static unsigned saves;

static void reply(const char *two)
{
    if (peer) {
        tcp_write(peer, two, 2, TCP_WRITE_FLAG_COPY);
        tcp_output(peer);
    }
}

static void fail(const char *two)
{
    reply(two);
    st = REPLIED;
    close_at = timer_ticks();
}

static void reset(void)
{
    free(buf);
    buf = NULL;
    st = IDLE;
    hdr_len = got = 0;
}

static void drop_peer(void)
{
    if (!peer)
        return;
    tcp_arg(peer, NULL);
    tcp_recv(peer, NULL);
    tcp_err(peer, NULL);
    if (tcp_close(peer) != ERR_OK)
        tcp_abort(peer);
    peer = NULL;
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* How long the header is so far, or 0 when complete ("need" set) */
static int parse_header(void)
{
    unsigned n = 6;
    if (hdr_len < n)
        return 1;
    if (memcmp(hdr, "BMXF", 4) != 0 || (hdr[4] != 'S' && hdr[4] != 'P' && hdr[4] != 'K'))
        return -1;
    unsigned pl = hdr[5];
    n += pl + 1;
    if (hdr_len < n)
        return 1;
    unsigned fl = hdr[n - 1];
    if (fl > 64)
        return -1;
    n += fl + 8;
    if (hdr_len < n)
        return 1;
    op = (char)hdr[4];
    const char *pw = netcon_password();
    if (pl != strlen(pw) || memcmp(hdr + 6, pw, pl) != 0)
        return -2;
    memcpy(path, hdr + 6 + pl + 1, fl);
    path[fl] = 0;
    size = le32(hdr + n - 8);
    crc = le32(hdr + n - 4);
    need = n;
    return 0;
}

static void feed(const uint8_t *p, unsigned len)
{
    while (len && (st == HEADER || st == DATA)) {
        if (st == HEADER) {
            if (hdr_len >= sizeof hdr) {
                fail("BH");
                return;
            }
            hdr[hdr_len++] = *p++;
            len--;
            int r = parse_header();
            if (r == 1)
                continue;
            if (r < 0) {
                if (r == -2)
                    kprintf("\x1b[91mnet: transfer with a wrong password from %s\x1b[0m\n",
                            ipaddr_ntoa(&peer->remote_ip));
                fail(r == -2 ? "PW" : "BH");
                return;
            }
            if (size == 0 || size > MAX_SIZE || !(buf = malloc(size))) {
                fail("SZ");
                return;
            }
            reply("OK");
            kprintf("net: receiving %s (%lu bytes)...\n",
                    op == 'K' ? "a new kernel" : path, size);
            st = DATA;
            got = 0;
        } else {
            uint32_t n = size - got < len ? size - got : len;
            memcpy(buf + got, p, n);
            got += n;
            p += n;
            len -= n;
            if (got == size)
                st = DONE;              /* checked and written by netxfer_poll */
        }
    }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p) {
        if (st == DATA)
            kprintf("\x1b[91mnet: transfer cut short (%lu of %lu bytes)\x1b[0m\n", got, size);
        if (st == HEADER || st == DATA)
            reset();                    /* after DONE / REPLIED: the answer (and a reboot) still due */
        drop_peer();
        return ERR_OK;
    }
    if (err == ERR_OK && pcb == peer)
        for (struct pbuf *q = p; q; q = q->next)
            feed(q->payload, q->len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    (void)arg; (void)err;
    peer = NULL;
    if (st == HEADER || st == DATA)
        reset();
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !pcb)
        return ERR_VAL;
    if (peer || st != IDLE) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    peer = pcb;
    st = HEADER;
    hdr_len = 0;
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    return ERR_OK;
}

int netxfer_start(void)
{
    if (listener)
        return 0;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb || tcp_bind(pcb, IP4_ADDR_ANY, NETXFER_PORT) != ERR_OK) {
        if (pcb)
            tcp_close(pcb);
        return -1;
    }
    listener = tcp_listen(pcb);
    if (!listener)
        return -1;
    tcp_accept(listener, on_accept);
    return 0;
}

/* "carts/pong.bm" -> "/carts", "pong.bm" */
static int save(const char *p, const uint8_t *data, uint32_t len)
{
    char dir[66] = "/";
    const char *slash = strrchr(p, '/');
    const char *name = slash ? slash + 1 : p;
    if (slash) {
        size_t n = (size_t)(slash - p);
        if (p[0] == '/') { p++; n--; }
        memcpy(dir + 1, p, n);
        dir[n + 1] = 0;
        if (n && fat_mkdirs(dir) != 0)
            return -1;
    }
    return fat_write_file(dir, name, data, len);
}

void netxfer_poll(void)
{
    if (st == DONE) {
        if (crc32(buf, size) != crc) {
            reply("CE");
            kprintf("\x1b[91mnet: transfer damaged (crc), nothing written\x1b[0m\n");
            reset();
        } else if (op == 'P') {
            free(play_buf);
            play_buf = buf;             /* the monitor or the menu plays it */
            play_len = size;
            buf = NULL;
            reply("OK");
            kprintf("net: cartridge received, starting it\n");
            reset();
        } else {
            const char *where = op == 'K' ? "kernel.img" : path;
            int r = save(where, buf, size);
            reply(r == 0 ? "OK" : "WE");
            if (r == 0) {
                saves++;
                kprintf("\x1b[92mnet: saved %s on the SD card (%lu bytes)\x1b[0m\n", where, size);
                reboot_after = op == 'K';
            } else {
                kprintf("\x1b[91mnet: could not write %s (%s)\x1b[0m\n", where, fat_error());
            }
            reset();
        }
        st = REPLIED;
        close_at = timer_ticks();
    }
    if (st == REPLIED && timer_ticks() - close_at > 300000u) {
        drop_peer();                    /* the answer has gone out */
        st = IDLE;
        if (reboot_after) {
            kprintf("net: restarting with the new kernel...\n");
            timer_delay_ms(200);
            watchdog_reboot();
        }
    }
}

int netxfer_play_announce(void)
{
    static const uint8_t *announced;
    if (!play_buf || play_buf == announced)
        return 0;
    announced = play_buf;
    return 1;
}

int netxfer_take_play(uint8_t **data, size_t *len)
{
    if (!play_buf)
        return 0;
    *data = play_buf;
    *len = play_len;
    play_buf = NULL;
    return 1;
}

unsigned netxfer_saves(void)
{
    return saves;
}
