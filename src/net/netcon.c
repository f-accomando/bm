/*
 * The network console: a TCP server on lwIP's raw API. The output is a
 * ring filled by the log tap (kprintf) and sent from netcon_poll, so
 * printing never enters lwIP; the input is a ring filled by the receive
 * callback and read by input_key.
 */
#include "netcon.h"
#include "kernel/config.h"
#include "kernel/version.h"
#include "lib/printf.h"
#include "drivers/timer.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_SIZE (32 * 1024)            /* powers of two */
#define IN_SIZE  1024
#define MAX_TRIES 3

static struct tcp_pcb *listener, *client;
static enum { NONE, AUTH, OPEN } state;
static char out[OUT_SIZE], in[IN_SIZE];
static unsigned out_head, out_tail, in_head, in_tail;
static char line[72];
static int line_len, tries, last_cr, closing;
static char password[40];

static void out_put(char c)
{
    if (out_head - out_tail >= OUT_SIZE)
        return;                         /* full: the client is too slow, dropped */
    out[out_head++ % OUT_SIZE] = c;
}

static void out_str(const char *s)
{
    while (*s)
        out_put(*s++);
}

/* kprintf's third output: only once logged in, "\n" as "\r\n" */
static void tap(char c)
{
    if (state != OPEN)
        return;
    if (c == '\n')
        out_put('\r');
    out_put(c);
}

/* 1 if the pcb had to be aborted (a callback must then return ERR_ABRT) */
static int drop_client(int abort_it)
{
    int aborted = 0;
    if (!client)
        return 0;
    tcp_arg(client, NULL);
    tcp_recv(client, NULL);
    tcp_err(client, NULL);
    if (abort_it || tcp_close(client) != ERR_OK) {
        tcp_abort(client);
        aborted = 1;
    }
    client = NULL;
    state = NONE;
    out_head = out_tail = 0;
    in_head = in_tail = 0;
    return aborted;
}

static void got_byte(char c)
{
    if (state == AUTH) {
        if (c == '\r' || c == '\n') {
            if (line_len == 0)
                return;
            line[line_len] = 0;
            line_len = 0;
            if (strcmp(line, password) == 0) {
                kprintf("\nnet: console opened from %s\n> ", ipaddr_ntoa(&client->remote_ip));
                state = OPEN;                   /* from here kprintf goes to the client too */
                out_str("\r\nok - bm33 monitor, 'h' for help, Ctrl-] to leave\r\n> ");
            } else if (++tries >= MAX_TRIES) {
                out_str("\r\nwrong password, bye\r\n");
                kprintf("\n\x1b[91mnet: console: wrong password from %s\x1b[0m\n",
                        ipaddr_ntoa(&client->remote_ip));
                closing = 1;
            } else {
                out_str("\r\nwrong password\r\npassword: ");
            }
        } else if ((c == 0x7F || c == 0x08) && line_len > 0) {
            line_len--;
        } else if ((unsigned char)c >= 32 && line_len < (int)sizeof line - 1) {
            line[line_len++] = c;
        }
        return;
    }
    if (c == '\n' && last_cr) {         /* "\r\n" is one Enter */
        last_cr = 0;
        return;
    }
    last_cr = c == '\r';
    if (in_head - in_tail < IN_SIZE)
        in[in_head++ % IN_SIZE] = c;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p) {                           /* the client closed */
        if (state == OPEN)
            kprintf("\nnet: console closed\n");
        return drop_client(0) ? ERR_ABRT : ERR_OK;
    }
    if (err == ERR_OK && pcb == client)
        for (struct pbuf *q = p; q; q = q->next)
            for (u16_t i = 0; i < q->len; i++)
                got_byte(((const char *)q->payload)[i]);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    (void)arg; (void)err;
    client = NULL;                      /* lwIP already freed the pcb */
    if (state == OPEN)
        kprintf("\nnet: console connection lost\n");
    state = NONE;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !pcb)
        return ERR_VAL;
    if (client) {                       /* one at a time */
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    client = pcb;
    state = AUTH;
    line_len = tries = last_cr = closing = 0;
    out_head = out_tail = in_head = in_tail = 0;
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    out_str("bm33 ");
    out_str(bm33_version);
    out_str(" network console\r\npassword: ");
    return ERR_OK;
}

int netcon_start(void)
{
    if (listener)
        return 0;
    const char *pw = config_get("net_password");
    if (pw && pw[0]) {
        snprintf(password, sizeof password, "%s", pw);
    } else {
        srand(timer_ticks());
        snprintf(password, sizeof password, "%06d", rand() % 1000000);
        config_set("net_password", password);
        config_save();
    }
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb || tcp_bind(pcb, IP4_ADDR_ANY, NETCON_PORT) != ERR_OK) {
        kprintf("\x1b[91mnet: console: port %d not available\x1b[0m\n", NETCON_PORT);
        if (pcb)
            tcp_close(pcb);
        return -1;
    }
    listener = tcp_listen(pcb);
    if (!listener)
        return -1;
    tcp_accept(listener, on_accept);
    kprintf_set_tap(tap);
    return 0;
}

void netcon_poll(void)
{
    if (!client)
        return;
    while (out_head != out_tail) {
        u16_t room = tcp_sndbuf(client);
        if (room == 0)
            break;
        unsigned from = out_tail % OUT_SIZE;
        unsigned n = out_head - out_tail;
        if (n > OUT_SIZE - from)
            n = OUT_SIZE - from;        /* up to the end of the ring */
        if (n > room)
            n = room;
        if (tcp_write(client, out + from, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK)
            break;
        out_tail += n;
    }
    tcp_output(client);
    if (closing && out_head == out_tail)
        drop_client(0);
}

int netcon_getc(void)
{
    if (in_tail == in_head)
        return -1;
    return (unsigned char)in[in_tail++ % IN_SIZE];
}

const char *netcon_password(void)
{
    return password;
}

int netcon_active(void)
{
    return state == OPEN;
}
