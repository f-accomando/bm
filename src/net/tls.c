/*
 * mbedTLS on a stream: blocking send/receive callbacks, the handshake,
 * certificate checks (chain to a root in bm/ca.pem, host name, dates:
 * the clock comes from SNTP), then plain reads and writes.
 */
#include "tls.h"
#include "stream.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BM_HOST_TEST
#include "drivers/rng.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/printf.h"
#include "net.h"

/* the hardware generator is the entropy source */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    if (rng_read(out, len))
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)(timer_ticks() / 1000);
}
#endif

#define BIO_SEND_FAILED (-0x004E)       /* as BIO_SEND_FAILED */
#define BIO_RECV_FAILED (-0x004C)

struct tls {
    stream_t *s;
    mbedtls_ssl_context ssl;
    uint32_t timeout;
    int top_depth;              /* the top of the chain the server sent */
    char top_issuer[96];        /* who signed it: the root to look for */
};

static mbedtls_x509_crt roots;
static int nroots = -1;
static mbedtls_ssl_config conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static int conf_ready;

int tls_set_roots(const char *pem, size_t len)
{
    if (nroots >= 0)
        mbedtls_x509_crt_free(&roots);
    mbedtls_x509_crt_init(&roots);
    /* the PEM parser wants the terminating NUL counted */
    char *copy = malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, pem, len);
    copy[len] = 0;
    int bad = mbedtls_x509_crt_parse(&roots, (const unsigned char *)copy, len + 1);
    free(copy);
    int n = 0;
    for (mbedtls_x509_crt *c = &roots; c && c->raw.len; c = c->next)
        n++;
    nroots = n;
    (void)bad;                  /* some unreadable ones are skipped */
    return n ? n : -1;
}

#ifdef BM_HOST_TEST
#define tls_load_roots_if_kernel() (-1)
#else
#define tls_load_roots_if_kernel() tls_load_roots()

int tls_load_roots(void)
{
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    if (config_find_file("ca.pem", &e) || fat_load(&e, &data, &len)) {
        kprintf("\x1b[91mtls: bm/ca.pem not on the SD card (make install copies it)\x1b[0m\n");
        return -1;
    }
    int n = tls_set_roots((const char *)data, len);
    free(data);
    kprintf("tls: %d root certificates\n", n);
    return n;
}
#endif

static int setup(char *err, size_t err_len)
{
    if (conf_ready)
        return 0;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    static const unsigned char pers[] = "bm tls";
    int r = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, pers, sizeof pers - 1);
    if (r) {
        snprintf(err, err_len, "no random numbers from the hardware generator (%d)", r);
        mbedtls_ctr_drbg_free(&drbg);       /* the next try starts clean */
        mbedtls_entropy_free(&entropy);
        return -1;
    }
    mbedtls_ssl_config_init(&conf);
    r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
    if (r) {
        snprintf(err, err_len, "tls setup (%d)", r);
        return -1;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    conf_ready = 1;
    return 0;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_t *t = ctx;
    return stream_write(t->s, buf, len) == 0 ? (int)len : BIO_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_t *t = ctx;
    int n = stream_read(t->s, buf, len, t->timeout);
    if (n < 0)
        return BIO_RECV_FAILED;
    return n;                   /* 0: the peer closed */
}

/* Called for each certificate of the chain, the top one first: its
 * issuer is the root that bm/ca.pem needs when the chain is not trusted. */
static int note_chain(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    tls_t *t = ctx;
    (void)flags;
    if (depth > t->top_depth) {
        t->top_depth = depth;
        if (mbedtls_x509_dn_gets(t->top_issuer, sizeof t->top_issuer, &crt->issuer) < 0)
            t->top_issuer[0] = 0;
    }
    return 0;
}

static void describe(int r, const char *what, char *err, size_t err_len)
{
    char m[80];
    mbedtls_strerror(r, m, sizeof m);
    snprintf(err, err_len, "%s: %s", what, m);
}

tls_t *tls_open(const char *host, uint16_t port, char *err, size_t err_len)
{
    if (nroots <= 0 && tls_load_roots_if_kernel() <= 0) {
        snprintf(err, err_len, "no root certificates (bm/ca.pem)");
        return NULL;
    }
    if (setup(err, err_len))
        return NULL;
#ifndef BM_HOST_TEST
    /* certificates have dates: wait a little for the network time */
    for (uint32_t t0 = timer_ticks(); !net_time() && timer_ticks() - t0 < 8000000u; )
        if (net_wait_step() < 0) {
            snprintf(err, err_len, "cancelled");
            return NULL;
        }
    if (!net_time()) {
        snprintf(err, err_len, "the clock is not set yet (no answer from pool.ntp.org)");
        return NULL;
    }
#endif
    tls_t *t = calloc(1, sizeof *t);
    if (!t) {
        snprintf(err, err_len, "out of memory");
        return NULL;
    }
    t->timeout = 15000;
    t->top_depth = -1;
    t->s = stream_open(host, port, 10000, err, err_len);
    if (!t->s) {
        free(t);
        return NULL;
    }
    mbedtls_ssl_init(&t->ssl);
    int r = mbedtls_ssl_setup(&t->ssl, &conf);
    if (!r)
        r = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (r) {
        describe(r, "tls", err, err_len);
        tls_close(t);
        return NULL;
    }
    mbedtls_ssl_conf_ca_chain(&conf, &roots, NULL);
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
    mbedtls_ssl_set_verify(&t->ssl, note_chain, t);
    while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
        if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
            (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) && t->top_issuer[0]) {
            snprintf(err, err_len, "%s: certificate not trusted: no root in bm/ca.pem for %s",
                     host, t->top_issuer);
        } else if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags) {
            char why[120];
            mbedtls_x509_crt_verify_info(why, sizeof why, "", flags);
            why[strcspn(why, "\n")] = 0;
            snprintf(err, err_len, "%s: certificate not trusted: %s", host, why);
        } else {
            describe(r, host, err, err_len);
        }
        tls_close(t);
        return NULL;
    }
    return t;
}

int tls_write(tls_t *t, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        int r = mbedtls_ssl_write(&t->ssl, p, len);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

int tls_read(tls_t *t, void *buf, size_t len, uint32_t timeout_ms)
{
    t->timeout = timeout_ms;
    for (;;) {
        int r = mbedtls_ssl_read(&t->ssl, buf, len);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0)
            return 0;
        return r < 0 ? -1 : r;
    }
}

void tls_close(tls_t *t)
{
    if (!t)
        return;
    if (t->s)
        mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    stream_close(t->s);
    free(t);
}
