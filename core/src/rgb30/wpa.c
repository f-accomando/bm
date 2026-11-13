/*
 * WPA2-PSK, the supplicant's side (wpa.h): PBKDF2, the PRF, the EAPOL-Key
 * MIC (HMAC-SHA1) and AES key unwrap over mbedTLS's SHA-1 and AES, and the
 * 4-way and group key handshakes. Descriptor version 2 only (HMAC-SHA1-128
 * + AES key wrap): the AKM we ask for is PSK (00-0F-AC:2).
 */
#include "wpa.h"

#include "mbedtls/aes.h"
#include "mbedtls/sha1.h"

#include <string.h>

/* --- HMAC-SHA1 with the padded key's state kept (PBKDF2 calls it 8192 times) --- */

typedef struct {
    mbedtls_sha1_context in, out;
} hmac_t;

static void hmac_key(hmac_t *h, const uint8_t *key, unsigned klen)
{
    uint8_t k[64], pad[64];
    memset(k, 0, sizeof k);
    if (klen > 64) {
        mbedtls_sha1(key, klen, k);
    } else {
        memcpy(k, key, klen);
    }
    for (int i = 0; i < 64; i++)
        pad[i] = k[i] ^ 0x36;
    mbedtls_sha1_init(&h->in);
    mbedtls_sha1_starts(&h->in);
    mbedtls_sha1_update(&h->in, pad, 64);
    for (int i = 0; i < 64; i++)
        pad[i] = k[i] ^ 0x5c;
    mbedtls_sha1_init(&h->out);
    mbedtls_sha1_starts(&h->out);
    mbedtls_sha1_update(&h->out, pad, 64);
}

static void hmac_run(const hmac_t *h, const uint8_t *m1, unsigned l1, const uint8_t *m2,
                     unsigned l2, uint8_t out[20])
{
    mbedtls_sha1_context c;
    uint8_t inner[20];
    mbedtls_sha1_init(&c);
    mbedtls_sha1_clone(&c, &h->in);
    mbedtls_sha1_update(&c, m1, l1);
    if (l2)
        mbedtls_sha1_update(&c, m2, l2);
    mbedtls_sha1_finish(&c, inner);
    mbedtls_sha1_clone(&c, &h->out);
    mbedtls_sha1_update(&c, inner, 20);
    mbedtls_sha1_finish(&c, out);
    mbedtls_sha1_free(&c);
}

void wpa_hmac_sha1(const uint8_t *key, unsigned klen, const uint8_t *msg, unsigned mlen,
                   uint8_t out[20])
{
    hmac_t h;
    hmac_key(&h, key, klen);
    hmac_run(&h, msg, mlen, NULL, 0, out);
}

void wpa_pmk(const char *pass, const uint8_t *ssid, unsigned ssid_len, uint8_t pmk[32])
{
    hmac_t h;
    hmac_key(&h, (const uint8_t *)pass, (unsigned)strlen(pass));
    for (unsigned block = 1; block <= 2; block++) {
        uint8_t ctr[4] = { 0, 0, 0, (uint8_t)block }, u[20], t[20];
        hmac_run(&h, ssid, ssid_len, ctr, 4, u);
        memcpy(t, u, 20);
        for (int i = 1; i < 4096; i++) {
            hmac_run(&h, u, 20, NULL, 0, u);
            for (int j = 0; j < 20; j++)
                t[j] ^= u[j];
        }
        memcpy(pmk + (block - 1) * 20, t, block == 1 ? 20 : 12);
    }
}

void wpa_prf(const uint8_t *key, unsigned klen, const char *label, const uint8_t *data,
             unsigned dlen, uint8_t *out, unsigned olen)
{
    uint8_t msg[128], r[20];
    unsigned ll = (unsigned)strlen(label);
    if (ll + 1 + dlen + 1 > sizeof msg)
        return;
    memcpy(msg, label, ll);
    msg[ll] = 0;
    memcpy(msg + ll + 1, data, dlen);
    unsigned n = ll + 1 + dlen;
    hmac_t h;
    hmac_key(&h, key, klen);
    for (uint8_t i = 0; olen; i++) {
        msg[n] = i;
        hmac_run(&h, msg, n + 1, NULL, 0, r);
        unsigned k = olen < 20 ? olen : 20;
        memcpy(out, r, k);
        out += k;
        olen -= k;
    }
}

int wpa_unwrap(const uint8_t kek[16], const uint8_t *in, unsigned len, uint8_t *out)
{
    if (len < 24 || len % 8)
        return -1;
    unsigned n = len / 8 - 1;
    uint8_t a[8], b[16];
    memcpy(a, in, 8);
    memmove(out, in + 8, len - 8);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, kek, 128);
    for (int j = 5; j >= 0; j--) {
        for (unsigned i = n; i >= 1; i--) {
            uint64_t t = (uint64_t)n * (unsigned)j + i;
            memcpy(b, a, 8);
            for (int k = 0; k < 8; k++)
                b[7 - k] ^= (uint8_t)(t >> (8 * k));
            memcpy(b + 8, out + (i - 1) * 8, 8);
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, b, b);
            memcpy(a, b, 8);
            memcpy(out + (i - 1) * 8, b + 8, 8);
        }
    }
    mbedtls_aes_free(&aes);
    for (int i = 0; i < 8; i++)
        if (a[i] != 0xa6)
            return -1;
    return 0;
}

/* --- EAPOL-Key --- */

/* offsets from the EAPOL header */
#define K_TYPE      1
#define K_BODYLEN   2
#define K_DESC      4
#define K_INFO      5
#define K_KEYLEN    7
#define K_REPLAY    9
#define K_NONCE     17
#define K_IV        49
#define K_RSC       65
#define K_MIC       81
#define K_DATALEN   97
#define K_DATA      99

#define KI_VERSION  0x0007
#define KI_PAIRWISE 0x0008
#define KI_INSTALL  0x0040
#define KI_ACK      0x0080
#define KI_MIC      0x0100
#define KI_SECURE   0x0200
#define KI_ERROR    0x0400
#define KI_REQUEST  0x0800
#define KI_ENCDATA  0x1000

static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }

static void mic(const uint8_t kck[16], const uint8_t *frame, unsigned len, uint8_t out[16])
{
    static uint8_t tmp[512];
    uint8_t h[20];
    if (len > sizeof tmp)
        len = sizeof tmp;
    memcpy(tmp, frame, len);
    memset(tmp + K_MIC, 0, 16);
    wpa_hmac_sha1(kck, 16, tmp, len, h);
    memcpy(out, h, 16);
}

/* min/max of two strings of n bytes, as memcmp orders them */
static void order(const uint8_t *a, const uint8_t *b, unsigned n, uint8_t *out)
{
    int lo = memcmp(a, b, n) < 0;
    memcpy(out, lo ? a : b, n);
    memcpy(out + n, lo ? b : a, n);
}

static void derive_ptk(wpa_t *w)
{
    uint8_t data[6 * 2 + 32 * 2];
    order(w->aa, w->spa, 6, data);
    order(w->anonce, w->snonce, 32, data + 12);
    wpa_prf(w->pmk, 32, "Pairwise key expansion", data, sizeof data, w->ptk, 48);
}

/* an EAPOL-Key frame from us: version as the AP's, RSN descriptor */
static unsigned reply(const wpa_t *w, uint8_t version, unsigned info, const uint8_t *replay,
                      const uint8_t *nonce, const uint8_t *data, unsigned dlen, uint8_t *out)
{
    unsigned len = K_DATA + dlen;
    memset(out, 0, K_DATA);
    out[0] = version;
    out[K_TYPE] = 3;
    out[K_BODYLEN] = (uint8_t)((len - 4) >> 8);
    out[K_BODYLEN + 1] = (uint8_t)(len - 4);
    out[K_DESC] = 2;
    out[K_INFO] = (uint8_t)(info >> 8);
    out[K_INFO + 1] = (uint8_t)info;
    memcpy(out + K_REPLAY, replay, 8);
    if (nonce)
        memcpy(out + K_NONCE, nonce, 32);
    out[K_DATALEN] = (uint8_t)(dlen >> 8);
    out[K_DATALEN + 1] = (uint8_t)dlen;
    if (dlen)
        memcpy(out + K_DATA, data, dlen);
    mic(w->ptk, out, len, out + K_MIC);
    return len;
}

/* the GTK KDE (DD len 00-0F-AC:1, key id, reserved, key) in unwrapped key data */
static int find_gtk(wpa_t *w, const uint8_t *d, unsigned len)
{
    unsigned i = 0;
    while (i + 2 <= len) {
        unsigned id = d[i], n = d[i + 1];
        if (id == 0xdd && n == 0)                   /* padding */
            break;
        if (i + 2 + n > len)
            return -1;
        if (id == 0xdd && n >= 6 + 16 && d[i + 2] == 0x00 && d[i + 3] == 0x0f &&
            d[i + 4] == 0xac && d[i + 5] == 0x01) {
            unsigned klen = n - 6;
            if (klen > sizeof w->gtk)
                return -1;
            w->gtk_id = d[i + 6] & 3;
            w->gtk_len = (uint8_t)klen;
            memcpy(w->gtk, d + i + 8, klen);
            return 0;
        }
        i += 2 + n;
    }
    return -1;
}

static int check_mic_replay(wpa_t *w, const uint8_t *e, unsigned len)
{
    uint8_t m[16];
    mic(w->ptk, e, len, m);
    if (memcmp(m, e + K_MIC, 16) != 0)
        return WPA_ERR_MIC;
    if (w->have_replay && memcmp(e + K_REPLAY, w->replay, 8) <= 0)
        return WPA_ERR_REPLAY;
    return 0;
}

static int unwrap_gtk(wpa_t *w, const uint8_t *e, unsigned info)
{
    static uint8_t plain[256];
    unsigned dlen = be16(e + K_DATALEN);
    if (!(info & KI_ENCDATA) || dlen < 24 || dlen - 8 > sizeof plain)
        return WPA_ERR_UNWRAP;
    if (wpa_unwrap(w->ptk + 16, e + K_DATA, dlen, plain) != 0)
        return WPA_ERR_UNWRAP;
    return find_gtk(w, plain, dlen - 8) == 0 ? 0 : WPA_ERR_FORMAT;
}

int wpa_rx(wpa_t *w, const uint8_t *e, unsigned len, uint8_t *out, unsigned *olen)
{
    *olen = 0;
    if (len < K_DATA || e[K_TYPE] != 3)
        return WPA_ERR_FORMAT;
    unsigned body = be16(e + K_BODYLEN);
    if (4 + body > len || 4 + body < K_DATA)
        return WPA_ERR_FORMAT;
    len = 4 + body;                                 /* without the frame's padding */
    if (e[K_DESC] != 2 || K_DATA + be16(e + K_DATALEN) > len)
        return WPA_ERR_FORMAT;
    unsigned info = be16(e + K_INFO);
    if ((info & KI_VERSION) != 2)
        return WPA_ERR_VERSION;
    if (!(info & KI_ACK) || (info & (KI_REQUEST | KI_ERROR)))
        return WPA_ERR_FORMAT;

    if ((info & KI_PAIRWISE) && !(info & KI_MIC)) {
        /* message 1: ANonce; our SNonce and IE, MIC with the new PTK */
        memcpy(w->anonce, e + K_NONCE, 32);
        w->have_anonce = 1;
        derive_ptk(w);
        *olen = reply(w, e[0], 2 | KI_PAIRWISE | KI_MIC, e + K_REPLAY, w->snonce, w->ie,
                      w->ie_len, out);
        return WPA_SEND;
    }
    if (info & KI_PAIRWISE) {
        /* message 3: MIC, same ANonce, the GTK; answer 4, keys in */
        if (!w->have_anonce)
            return WPA_ERR_FORMAT;
        int r = check_mic_replay(w, e, len);
        if (r)
            return r;
        if (memcmp(e + K_NONCE, w->anonce, 32) != 0)
            return WPA_ERR_NONCE;
        if (!(info & KI_INSTALL))
            return WPA_ERR_FORMAT;
        if ((r = unwrap_gtk(w, e, info)) != 0)
            return r;
        memcpy(w->replay, e + K_REPLAY, 8);
        w->have_replay = 1;
        w->done = 1;
        *olen = reply(w, e[0], 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, e + K_REPLAY, NULL, NULL,
                      0, out);
        return WPA_SEND | WPA_SET_PTK | WPA_SET_GTK;
    }
    /* group key message 1 (a new GTK); answer message 2 */
    if (!w->done || !(info & KI_MIC))
        return WPA_ERR_FORMAT;
    int r = check_mic_replay(w, e, len);
    if (r)
        return r;
    if ((r = unwrap_gtk(w, e, info)) != 0)
        return r;
    memcpy(w->replay, e + K_REPLAY, 8);
    *olen = reply(w, e[0], 2 | KI_MIC | KI_SECURE, e + K_REPLAY, NULL, NULL, 0, out);
    return WPA_SEND | WPA_SET_GTK;
}
