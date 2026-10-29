/* ai_pair: the code, the fingerprint and the user's roots; see aipair.h. */
#include "aipair.h"

#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char ALPHA[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";      /* Crockford base32 */

static int say(char *err, size_t en, const char *what)
{
    if (en)
        snprintf(err, en, "%s", what);
    return -1;
}

int ai_pair_code(const char *typed, char out[AI_PAIR_CODE_LEN + 1], char *err, size_t en)
{
    int n = 0;
    for (const char *p = typed ? typed : ""; *p; p++) {
        char c = *p;
        if (c == '-' || c == ' ')
            continue;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        c = c == 'O' ? '0' : (c == 'I' || c == 'L') ? '1' : c;
        if (!strchr(ALPHA, c) || !c)
            return say(err, en, "the code has only letters and digits (no U)");
        if (n == AI_PAIR_CODE_LEN)
            return say(err, en, "the code has 20 characters");
        out[n++] = c;
    }
    out[n] = 0;
    return n == AI_PAIR_CODE_LEN ? 0 : say(err, en, "the code has 20 characters");
}

void ai_pair_fingerprint(const unsigned char *der, size_t len, char out[AI_PAIR_FP_LEN + 1])
{
    unsigned char h[32];
    mbedtls_sha256(der, len, h, 0);
    for (int i = 0; i < AI_PAIR_FP_LEN; i++) {         /* 5 bits a character, from the first byte */
        const int bit = i * 5;
        const unsigned v = ((unsigned)h[bit / 8] << 8 | h[bit / 8 + 1]) >> (11 - bit % 8);
        out[i] = ALPHA[v & 31];
    }
    out[AI_PAIR_FP_LEN] = 0;
}

static int has(const char *s, size_t n, const char *what)
{
    const size_t wl = strlen(what);
    for (size_t i = 0; i + wl <= n; i++)
        if (!memcmp(s + i, what, wl))
            return 1;
    return 0;
}

int ai_pair_check(const char *pem, size_t len, const char *code, char *err, size_t en)
{
    if (!pem || !len || len > 16384 || !has(pem, len, "-----BEGIN CERTIFICATE-----"))
        return say(err, en, "the relay sent no certificate");
    char *copy = malloc(len + 1);                       /* the PEM parser wants the NUL counted */
    if (!copy)
        return say(err, en, "not enough memory");
    memcpy(copy, pem, len);
    copy[len] = 0;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    const int rc = mbedtls_x509_crt_parse(&crt, (const unsigned char *)copy, len + 1);
    free(copy);
    int r = 0;
    char fp[AI_PAIR_FP_LEN + 1];
    if (rc != 0 || !crt.raw.len)
        r = say(err, en, "the relay's certificate cannot be read");
    else if (crt.next && crt.next->raw.len)
        r = say(err, en, "the relay sent more than one certificate");
    else if (!crt.MBEDTLS_PRIVATE(ca_istrue))
        r = say(err, en, "the relay's certificate is not a CA");
    else {
        ai_pair_fingerprint(crt.raw.p, crt.raw.len, fp);
        if (strncmp(fp, code, AI_PAIR_FP_LEN))
            r = say(err, en, "the certificate does not match the code: check the code, or another PC answered");
    }
    mbedtls_x509_crt_free(&crt);
    return r;
}

#ifndef BM_HOST_TEST
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "kernel/config.h"
#include "tls.h"

int ai_pair_install(const char *pem, size_t len, char *err, size_t en)
{
    fat_entry_t e;
    uint8_t *old = NULL;
    size_t ol = 0;
    if (config_find_file(AI_USER_CA, &e) == 0 && fat_load(&e, &old, &ol))
        return say(err, en, "bm/ca-user.pem cannot be read");
    int there = 0;
    if (old && ol >= len)
        for (size_t i = 0; i + len <= ol && !there; i++)
            there = memcmp(old + i, pem, len) == 0;
    if (!there) {
        char *all = malloc(ol + len + 2);
        if (!all) {
            free(old);
            return say(err, en, "not enough memory");
        }
        size_t n = 0;
        if (ol) {
            memcpy(all, old, ol);
            n = ol;
            if (all[n - 1] != '\n')
                all[n++] = '\n';
        }
        memcpy(all + n, pem, len);
        n += len;
        const int w = sd_write_file("ai_pair", "/bm", AI_USER_CA, all, n);
        free(all);
        if (w) {
            free(old);
            return say(err, en, "bm/ca-user.pem cannot be written");
        }
    }
    free(old);
    tls_load_roots();
    return 0;
}
#endif
