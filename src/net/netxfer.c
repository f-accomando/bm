/*
 * The transfer server: the receive callback fills a header, then a
 * buffer; the SD card is written from netxfer_poll, outside lwIP.
 * S saves a file, P plays a cartridge, K writes the kernel, C changes
 * bm/config.txt ("key=value" lines; the answer has the settings after).
 */
#include "netxfer.h"
#include "netcon.h"
#include "net.h"
#include "kernel/config.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "wifi/wifi.h"
#include "fs/fat.h"
#include "lib/crc32.h"
#include "lib/printf.h"

#include "lwip/tcp.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SIZE (100u << 20)           /* a .bm has no limit: as the Market's files (catalog.h) */
#define CONFIG_MAX (16u << 10)          /* the lines of a C */
#define CONFIG_REPLY (8u << 10)         /* the settings after it: under TCP_SND_BUF */

static struct tcp_pcb *listener, *peer;
static enum { IDLE, HEADER, DATA, DONE, WRITING, REPLIED } st;
static uint8_t hdr[4 + 1 + 1 + 64 + 1 + 64 + 8];
static unsigned hdr_len, need;
static char op, path[65];
static uint8_t *buf;
static uint32_t size, crc, got;
static uint32_t close_at;
static uint32_t last_rx;                /* timer_ticks() of the last bytes received */
static int reboot_after;
static uint32_t restart_at;             /* the restart with a new kernel (timer_ticks) */
static int said;                        /* the tenth of a kernel, or the second, said last */
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

/* the answer and a length-prefixed text after it (C) */
static void reply_text(const char *two, const char *text, uint32_t len)
{
    if (!peer)
        return;
    const uint8_t n[4] = { (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24) };
    tcp_write(peer, two, 2, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    tcp_write(peer, n, 4, TCP_WRITE_FLAG_COPY | (len ? TCP_WRITE_FLAG_MORE : 0));
    if (len)
        tcp_write(peer, text, (u16_t)len, TCP_WRITE_FLAG_COPY);
    tcp_output(peer);
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
    if (memcmp(hdr, "BMXF", 4) != 0 || (hdr[4] != 'S' && hdr[4] != 'P' && hdr[4] != 'K' && hdr[4] != 'C'))
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
            if (size == 0 || size > (op == 'C' ? CONFIG_MAX : MAX_SIZE) || !(buf = malloc(size))) {
                fail("SZ");
                return;
            }
            reply("OK");
            kprintf("net: receiving %s (%lu bytes)...\n",
                    op == 'K' ? "a new kernel" : op == 'C' ? "settings" : path, size);
            st = DATA;
            got = 0;
            said = -1;
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
    if (err == ERR_OK && pcb == peer) {
        last_rx = timer_ticks();
        for (struct pbuf *q = p; q; q = q->next)
            feed(q->payload, q->len);
    }
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
    last_rx = timer_ticks();
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

/* After each piece of a file written: the network goes on meanwhile (the
 * RGB30 took more than a minute for a kernel, its WiFi then thought the
 * network gone and the PC never had its answer). netxfer_poll, called
 * again from inside, has nothing to do while the state is WRITING. */
static int keep_net(void)
{
    net_poll();
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

/* The kernel this build starts from, and whether an image is one for it:
 * "bmK6" (kernel.img) or "bmK7" (kernel7.img, the Pi Zero 2 W) at +4
 * (src/boot/start.S). A kernel.img from before the mark has none, and
 * still goes on the BCM2835 boards. The RGB30's kernel8.img is an arm64
 * Image: "ARM\x64" at +56 (src/rgb30/start.S), the file U-Boot starts. */
#if defined(BM_RGB30)
#define KERNEL_FILE "kernel8.img"
#define KERNEL_MARK '8'
#elif defined(BM_ZERO2)
#define KERNEL_FILE "kernel7.img"
#define KERNEL_MARK '7'
#else
#define KERNEL_FILE "kernel.img"
#define KERNEL_MARK '6'
#endif

static int kernel_fits(const uint8_t *img, uint32_t len)
{
    int arm64 = len >= 64 && memcmp(img + 56, "ARM\x64", 4) == 0;
    if (KERNEL_MARK == '8' || arm64)                /* the RGB30's, only on the RGB30 */
        return KERNEL_MARK == '8' && arm64;
    int marked = len >= 8 && memcmp(img + 4, "bmK", 3) == 0;
    if (!marked)
        return KERNEL_MARK == '6';
    return img[7] == KERNEL_MARK;
}

int netxfer_kernel_state(uint32_t *done, uint32_t *total, int *secs)
{
    if (reboot_after) {
        if (secs) {
            const int32_t left = (int32_t)(restart_at - timer_ticks());
            *secs = left <= 0 ? 0 : (int)((left + 999999) / 1000000);
        }
        return NETXFER_K_RESTART;
    }
    if (op == 'K' && (st == DATA || st == DONE || st == WRITING)) {
        if (done)
            *done = got;
        if (total)
            *total = size;
        return NETXFER_K_RECEIVING;
    }
    return 0;
}

void netxfer_poll(void)
{
    /* a kernel's progress on the console, a line every tenth (the menu
     * and the games show it in a box: notice.c) */
    if (op == 'K' && st == DATA && size) {
        const int tenth = (int)((uint64_t)got * 10 / size);
        if (tenth != said) {
            said = tenth;
            kprintf("net: kernel %d%% (%lu of %lu KiB)\n", tenth * 10, got / 1024, size / 1024);
        }
    }
    /* a PC that went away in the middle (no FIN reached us): after 10 s of
     * silence the transfer is given up, so the next one is not turned away */
    if ((st == HEADER || st == DATA) && timer_ticks() - last_rx > 10000000u) {
        kprintf("\x1b[91mnet: transfer stalled (%lu of %lu bytes), given up\x1b[0m\n", got, size);
        drop_peer();
        reset();
    }
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
        } else if (op == 'C') {
            char *list = malloc(CONFIG_REPLY);
            const int r = list ? config_merge((const char *)buf, size, list, CONFIG_REPLY) : -2;
            if (r == -2) {
                reply("SZ");
            } else if (r < 0) {
                reply("KV");
                kprintf("\x1b[91mnet: settings refused (not key=value lines), nothing changed\x1b[0m\n");
            } else {
                reply_text("OK", list, (uint32_t)strlen(list));
                kprintf("\x1b[92mnet: settings from the PC: %d changed in bm/config.txt\x1b[0m\n", r);
            }
            free(list);
            reset();
        } else if (op == 'K' && !kernel_fits(buf, size)) {
            reply("KA");
            kprintf("\x1b[91mnet: not a kernel for this console, nothing written (the Pi Zero 2 W "
                    "takes kernel7.img, the RGB30 kernel8.img, the other boards kernel.img)\x1b[0m\n");
            reset();
        } else {
            const char *where = op == 'K' ? KERNEL_FILE : path;
            int (*was)(void) = fat_write_tick;
            st = WRITING;
            fat_write_tick = keep_net;
            int r = save(where, buf, size);
            fat_write_tick = was;
            reply(r == 0 ? "OK" : "WE");
            if (r == 0) {
                saves++;
                kprintf("\x1b[92mnet: saved %s on the SD card (%lu bytes)\x1b[0m\n", where, size);
                if (op == 'K') {
                    /* not at once: 3 s, counted on the screen (the user's
                     * request, 2026-10-04) */
                    reboot_after = 1;
                    restart_at = timer_ticks() + 3000000u;
                    said = -1;
                }
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
    }
    if (reboot_after) {
        int secs;
        netxfer_kernel_state(NULL, NULL, &secs);
        if (secs != said && secs) {
            said = secs;
            kprintf("net: restarting with the new kernel in %d s\n", secs);
        }
        if (!secs && st == IDLE) {
            kprintf("net: restarting with the new kernel...\n");
            timer_delay_ms(200);
            wifi_leave();               /* the AP told: it does not hold the old association */
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
