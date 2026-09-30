/*
 * Host test of HTTPS: http.c + tls.c (mbedTLS, the kernel's configuration)
 * over POSIX sockets, against tests/net/run_https_test.py: a test CA and
 * servers with an ECDSA certificate, an RSA one, an expired one and one
 * signed by another CA.
 */
#include "net/http.h"
#include "net/tls.h"

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

int main(int argc, char **argv)
{
    if (argc < 6)
        return 2;
    const char *ca = argv[1];
    int p_ec = atoi(argv[2]), p_rsa = atoi(argv[3]), p_old = atoi(argv[4]), p_other = atoi(argv[5]);
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
    check(nb == 19, "boot/ca.pem: all 19 roots readable by mbedTLS");
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
    check(st == 200 && same, "https 1 MiB, RSA certificate, same bytes");
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

    snprintf(url, sizeof url, "https://localhost:%d/redirect-plain", p_ec);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10 && strstr(info.url, "https://localhost"), "redirect between https servers");
    free(d);

    printf(fails ? "\n%d FAILED\n" : "\nhttps: all passed\n", fails);
    return fails != 0;
}
