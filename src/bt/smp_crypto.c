/*
 * LE Security Manager cryptography on top of mbedTLS (AES, P-256). The
 * spec's functions take their numbers most significant byte first; the
 * protocol sends them least significant first: every value here is kept
 * in protocol order and reversed around the AES calls.
 */
#include "smp_crypto.h"

#include "mbedtls/aes.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#include <string.h>

static void reverse(uint8_t *dst, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
        dst[i] = src[n - 1 - i];
}

/* AES-128 in the spec's byte order (most significant first) */
static void aes_msb(const uint8_t k[16], const uint8_t in[16], uint8_t out[16])
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, k, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&aes);
}

void smp_e(const uint8_t k[16], const uint8_t r[16], uint8_t out[16])
{
    uint8_t kk[16], rr[16], o[16];
    reverse(kk, k, 16);
    reverse(rr, r, 16);
    aes_msb(kk, rr, o);
    reverse(out, o, 16);
}

void smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
            uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16])
{
    /* p1 = pres || preq || rat' || iat', p2 = padding || ia || ra */
    uint8_t p1[16], p2[16], t[16];
    p1[0] = iat;
    p1[1] = rat;
    memcpy(p1 + 2, preq, 7);
    memcpy(p1 + 9, pres, 7);
    memcpy(p2, ra, 6);
    memcpy(p2 + 6, ia, 6);
    memset(p2 + 12, 0, 4);
    for (int i = 0; i < 16; i++)
        t[i] = r[i] ^ p1[i];
    smp_e(k, t, t);
    for (int i = 0; i < 16; i++)
        t[i] ^= p2[i];
    smp_e(k, t, out);
}

void smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16])
{
    /* r' = r1' || r2': the low halves, r2's in the least significant bytes */
    uint8_t r[16];
    memcpy(r, r2, 8);
    memcpy(r + 8, r1, 8);
    smp_e(k, r, out);
}

void smp_ah(const uint8_t irk[16], const uint8_t prand[3], uint8_t out[3])
{
    uint8_t r[16], o[16];
    memset(r, 0, sizeof r);
    memcpy(r, prand, 3);
    smp_e(irk, r, o);
    memcpy(out, o, 3);
}

int smp_resolves(const uint8_t irk[16], const uint8_t addr[6])
{
    uint8_t h[3];
    if ((addr[5] & 0xC0) != 0x40)               /* not a resolvable private address */
        return 0;
    smp_ah(irk, addr + 3, h);
    return memcmp(h, addr, 3) == 0;
}

/* ---------------------------------------------------------------- AES-CMAC */

static void shift1(uint8_t *out, const uint8_t *in)      /* 16 bytes, MSB first */
{
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t b = in[i];
        out[i] = (uint8_t)(b << 1 | carry);
        carry = b >> 7;
    }
}

/* RFC 4493 on MSB-first data */
static void cmac_msb(const uint8_t k[16], const uint8_t *m, size_t len, uint8_t out[16])
{
    uint8_t zero[16] = { 0 }, l[16], k1[16], k2[16], x[16], last[16];
    aes_msb(k, zero, l);
    shift1(k1, l);
    if (l[0] & 0x80) k1[15] ^= 0x87;
    shift1(k2, k1);
    if (k1[0] & 0x80) k2[15] ^= 0x87;
    size_t n = (len + 15) / 16;
    int complete;
    if (n == 0) {
        n = 1;
        complete = 0;
    } else {
        complete = len % 16 == 0;
    }
    const uint8_t *tail = m + (n - 1) * 16;
    if (complete) {
        for (int i = 0; i < 16; i++) last[i] = tail[i] ^ k1[i];
    } else {
        size_t r = len - (n - 1) * 16;
        memset(last, 0, 16);
        memcpy(last, tail, r);
        last[r] = 0x80;
        for (int i = 0; i < 16; i++) last[i] ^= k2[i];
    }
    memset(x, 0, 16);
    for (size_t b = 0; b + 1 < n; b++) {
        for (int i = 0; i < 16; i++) x[i] ^= m[b * 16 + i];
        aes_msb(k, x, x);
    }
    for (int i = 0; i < 16; i++) x[i] ^= last[i];
    aes_msb(k, x, out);
}

void smp_cmac(const uint8_t k[16], const uint8_t *m, size_t len, uint8_t out[16])
{
    uint8_t kk[16], mm[80], o[16];
    if (len > sizeof mm)
        return;
    reverse(kk, k, 16);
    reverse(mm, m, len);
    cmac_msb(kk, mm, len, o);
    reverse(out, o, 16);
}

/* ---------------------------------------------------------------- f4 f5 f6 */

void smp_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z, uint8_t out[16])
{
    /* U || V || Z, most significant first: in protocol order Z, V, U */
    uint8_t m[65];
    m[0] = z;
    memcpy(m + 1, v, 32);
    memcpy(m + 33, u, 32);
    smp_cmac(x, m, sizeof m, out);
}

void smp_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
            const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16])
{
    static const uint8_t salt[16] = { 0xBE, 0x83, 0x60, 0x5A, 0xDB, 0x0B, 0x37, 0x60,
                                      0x38, 0xA5, 0xF5, 0xAA, 0x91, 0x83, 0x88, 0x6C };
    static const uint8_t btle[4] = { 0x65, 0x6C, 0x74, 0x62 };           /* "btle" */
    uint8_t t[16], m[53];
    smp_cmac(salt, w, 32, t);
    /* Counter || keyID || N1 || N2 || A1 || A2 || Length, reversed */
    m[0] = 0x00;                                /* Length = 256 */
    m[1] = 0x01;
    memcpy(m + 2, a2, 7);
    memcpy(m + 9, a1, 7);
    memcpy(m + 16, n2, 16);
    memcpy(m + 32, n1, 16);
    memcpy(m + 48, btle, 4);
    m[52] = 0;                                  /* Counter */
    smp_cmac(t, m, sizeof m, mackey);
    m[52] = 1;
    smp_cmac(t, m, sizeof m, ltk);
}

void smp_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
            const uint8_t iocap[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16])
{
    uint8_t m[65];
    memcpy(m, a2, 7);
    memcpy(m + 7, a1, 7);
    memcpy(m + 14, iocap, 3);
    memcpy(m + 17, r, 16);
    memcpy(m + 33, n2, 16);
    memcpy(m + 49, n1, 16);
    smp_cmac(w, m, sizeof m, out);
}

/* ---------------------------------------------------------------- P-256 */

int smp_p256_keypair(uint8_t priv[32], uint8_t pub[64], smp_rng_t f_rng, void *ctx)
{
    mbedtls_ecp_group g;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    uint8_t b[32];
    int r;
    mbedtls_ecp_group_init(&g);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&q);
    r = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1);
    if (!r) r = mbedtls_ecp_gen_keypair(&g, &d, &q, f_rng, ctx);
    if (!r) r = mbedtls_mpi_write_binary(&d, b, 32);
    if (!r) reverse(priv, b, 32);
    if (!r) r = mbedtls_mpi_write_binary(&q.MBEDTLS_PRIVATE(X), b, 32);
    if (!r) reverse(pub, b, 32);
    if (!r) r = mbedtls_mpi_write_binary(&q.MBEDTLS_PRIVATE(Y), b, 32);
    if (!r) reverse(pub + 32, b, 32);
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&g);
    return r ? -1 : 0;
}

int smp_p256_dhkey(const uint8_t priv[32], const uint8_t peer[64], uint8_t dhkey[32],
                   smp_rng_t f_rng, void *ctx)
{
    mbedtls_ecp_group g;
    mbedtls_mpi d, z;
    mbedtls_ecp_point q;
    uint8_t b[32];
    int r;
    mbedtls_ecp_group_init(&g);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&q);
    r = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1);
    reverse(b, priv, 32);
    if (!r) r = mbedtls_mpi_read_binary(&d, b, 32);
    reverse(b, peer, 32);
    if (!r) r = mbedtls_mpi_read_binary(&q.MBEDTLS_PRIVATE(X), b, 32);
    reverse(b, peer + 32, 32);
    if (!r) r = mbedtls_mpi_read_binary(&q.MBEDTLS_PRIVATE(Y), b, 32);
    if (!r) r = mbedtls_mpi_lset(&q.MBEDTLS_PRIVATE(Z), 1);
    if (!r) r = mbedtls_ecp_check_pubkey(&g, &q);          /* on the curve, or no key */
    if (!r) r = mbedtls_ecdh_compute_shared(&g, &z, &q, &d, f_rng, ctx);
    if (!r) r = mbedtls_mpi_write_binary(&z, b, 32);
    if (!r) reverse(dhkey, b, 32);
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&g);
    return r ? -1 : 0;
}
