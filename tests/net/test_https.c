/*
 * Host test of HTTPS: http.c + tls.c (mbedTLS, the kernel's configuration)
 * over POSIX sockets, against tests/net/run_https_test.py: a test CA and
 * servers with an ECDSA certificate, an RSA one, an expired one, one
 * signed by another CA and one whose chain goes on past the test CA with a
 * cross-certificate from an unknown CA (as example.com's, via Cloudflare).
 */
#include "net/http.h"
#include "net/tls.h"
#include "net/aipair.h"

#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* what the kernel takes from the hardware generator and the timer */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    FILE *f = fopen("/dev/urandom", "rb");
    size_t n = f ? fread(out, 1, len, f) : 0;
    if (f)
        fclose(f);
    *olen = n;
    return n == len ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (mbedtls_ms_time_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    rewind(f);
    char *b = malloc(*len);
    fread(b, 1, *len, f);
    fclose(f);
    return b;
}

static int relay_sink(void *ctx, const uint8_t *data, size_t len)
{
    struct { char *b; size_t n; } *acc = ctx;
    if (acc->n + len < 4095) {
        memcpy(acc->b + acc->n, data, len);
        acc->n += len;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 7)
        return 2;
    const char *ca = argv[1];
    int p_ec = atoi(argv[2]), p_rsa = atoi(argv[3]), p_old = atoi(argv[4]), p_other = atoi(argv[5]);
    int p_cross = atoi(argv[6]);
    char url[160];
    uint8_t *d;
    size_t n;
    http_info_t info;

    snprintf(url, sizeof url, "https://localhost:%d/hello", p_ec);
    check(http_get_buffer(url, NULL, 1 << 20, &d, &n, &info) == -1 && strstr(info.error, "root"),
          "no roots loaded: refused");
    free(d);

    size_t pl;
    char *pem = slurp("boot/ca.pem", &pl);
    int nb = pem ? tls_set_roots(pem, pl) : -1;
    check(nb == 21, "boot/ca.pem: all 21 roots readable by mbedTLS");
    free(pem);

    pem = slurp(ca, &pl);
    check(pem && tls_set_roots(pem, pl) == 1, "test CA loaded");
    free(pem);

    int st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10 && !memcmp(d, "hello, bm\n", 10), "https GET, ECDSA certificate");
    if (st != 200) printf("     %s\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/big", p_rsa);
    st = http_get_buffer(url, NULL, 4 << 20, &d, &n, &info);
    int same = n == 1 << 20;
    for (size_t i = 0; same && i < n; i++)
        same = d[i] == (uint8_t)(i * 7);
    check(st == 200 && same, "https 1 MB, RSA certificate, same bytes");
    if (st != 200) printf("     %s\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/chunked", p_ec);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 3000, "https chunked body");
    free(d);

    snprintf(url, sizeof url, "https://127.0.0.1:%d/hello", p_ec);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == -1 && strstr(info.error, "not trusted") && strstr(info.error, "CN"),
          "wrong host name: refused");
    printf("     (%s)\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/hello", p_old);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == -1 && strstr(info.error, "expired"), "expired certificate: refused");
    printf("     (%s)\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/hello", p_other);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == -1 && strstr(info.error, "not trusted") && strstr(info.error, "CN=some other CA"),
          "certificate from another CA: refused, naming the missing root");
    printf("     (%s)\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/hello", p_cross);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200, "chain with a cross-certificate from an unknown CA: stops at the known root");
    if (st != 200) printf("     %s\n", info.error);
    free(d);

    snprintf(url, sizeof url, "https://localhost:%d/redirect-plain", p_ec);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10 && strstr(info.url, "https://localhost"), "redirect between https servers");
    free(d);

    /* the agent relay: its CA added at the end of boot/ca.pem (as the user does on the SD card), then
     * ai_ask's POST to its address (an IP in the certificate) with the token */
    if (argc >= 10) {
        size_t al;
        char *relay_ca = slurp(argv[7], &al);
        pem = slurp("boot/ca.pem", &pl);
        char *both = malloc(pl + al + 2);
        memcpy(both, pem, pl);
        both[pl] = '\n';
        memcpy(both + pl + 1, relay_ca, al);
        check(tls_set_roots(both, pl + al + 1) == 22, "boot/ca.pem with the relay's CA at the end: 22 roots");
        free(both);
        free(pem);
        free(relay_ca);
        char headers[256], sink_buf[4096];
        snprintf(headers, sizeof headers, "Content-Type: application/json\r\nAuthorization: Bearer %s\r\n", argv[9]);
        static const char body[] = "{\"model\":\"default\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}";
        http_req_t q;
        memset(&q, 0, sizeof q);
        q.method = "POST";
        q.headers = headers;
        q.body = body;
        q.body_len = sizeof body - 1;
        q.any_status = 1;
        struct { char *b; size_t n; } acc = { sink_buf, 0 };
        snprintf(url, sizeof url, "https://127.0.0.1:%s/v1/chat/completions", argv[8]);
        st = http_request(url, &q, relay_sink, &acc, &info);
        sink_buf[acc.n < sizeof sink_buf ? acc.n : sizeof sink_buf - 1] = 0;
        check(st == 200 && strstr(sink_buf, "relay says hi"), "POST to the relay on an IP address over TLS");
        if (st != 200)
            printf("     %d %s %s\n", st, info.error, sink_buf);
    }

    /* ai_pair: the code the relay printed (Python) checked by the kernel's code (C) on the relay's CA */
    if (argc >= 12) {
        char code[AI_PAIR_CODE_LEN + 1], err[160];
        size_t cl, sl;
        char *ca_pem = slurp(argv[7], &cl), *srv_pem = slurp(argv[11], &sl);
        check(ai_pair_code(argv[10], code, err, sizeof err) == 0, "the relay's pairing code reads (20 characters)");
        check(ai_pair_check(ca_pem, cl, code, err, sizeof err) == 0, "the relay's CA matches its code");
        if (ai_pair_check(ca_pem, cl, code, err, sizeof err))
            printf("     %s\n", err);
        char typed[40];
        snprintf(typed, sizeof typed, "%s", argv[10]);
        for (char *p = typed; *p; p++)
            *p = (char)(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
        check(ai_pair_code(typed, code, err, sizeof err) == 0 && ai_pair_check(ca_pem, cl, code, err, sizeof err) == 0,
              "the code typed in lower case works too");
        code[3] = code[3] == '7' ? '8' : '7';
        check(ai_pair_check(ca_pem, cl, code, err, sizeof err) == -1 && strstr(err, "does not match"),
              "one wrong character of the fingerprint: refused");
        ai_pair_code(argv[10], code, err, sizeof err);
        check(ai_pair_check(srv_pem, sl, code, err, sizeof err) == -1 && strstr(err, "not a CA"),
              "the relay's server certificate (not a CA): refused");
        char *two = malloc(cl * 2 + 2);
        memcpy(two, ca_pem, cl);
        memcpy(two + cl, ca_pem, cl);
        check(ai_pair_check(two, cl * 2, code, err, sizeof err) == -1 && strstr(err, "more than one"),
              "two certificates: refused");
        check(ai_pair_code("ABCD-EFGH", code, err, sizeof err) == -1 && ai_pair_code("UUUU-UUUU-UUUU-UUUU-UUUU", code, err, sizeof err) == -1,
              "a short code and the letter U: refused");
        check(ai_pair_check("hello", 5, code, err, sizeof err) == -1, "no certificate: refused");
        free(two);
        free(ca_pem);
        free(srv_pem);
    }

    printf(fails ? "\n%d FAILED\n" : "\nhttps: all passed\n", fails);
    return fails != 0;
}
