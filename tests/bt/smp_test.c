/* Host test of src/bt/smp_crypto.c against the sample data of the Bluetooth
 * Core spec (Vol 3 Part H, Appendix D) and RFC 4493. Values are written as
 * in the spec (most significant byte first) and reversed for the calls. */
#include "bt/smp_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;

static void hex(uint8_t *out, const char *s, int n, int reversed)
{
    uint8_t t[80];
    for (int i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        t[i] = (uint8_t)v;
    }
    for (int i = 0; i < n; i++)
        out[i] = reversed ? t[n - 1 - i] : t[i];
}

static void check(const char *what, const uint8_t *got, const char *want_msb, int n)
{
    uint8_t w[80];
    hex(w, want_msb, n, 1);
    if (memcmp(got, w, (size_t)n) != 0) {
        printf("FAIL %s: got ", what);
        for (int i = n - 1; i >= 0; i--) printf("%02x", got[i]);
        printf(", want %s\n", want_msb);
        failed = 1;
    } else {
        printf("ok   %s\n", what);
    }
}

/* the entropy hook the kernel's TLS provides (unused here) */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    for (size_t i = 0; i < len; i++) out[i] = (unsigned char)rand();
    *olen = len;
    return 0;
}

static int host_rng(void *ctx, unsigned char *b, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)rand();
    return 0;
}

int main(void)
{
    uint8_t k[16] = { 0 }, r[16], preq[7], pres[7], ia[6], ra[6], out[16];
    /* c1 (D.1 of the legacy sample data) */
    hex(r, "5783D52156AD6F0E6388274EC6702EE0", 16, 1);
    hex(preq, "07071000000101", 7, 1);
    hex(pres, "05000800000302", 7, 1);
    hex(ia, "A1A2A3A4A5A6", 6, 1);
    hex(ra, "B1B2B3B4B5B6", 6, 1);
    smp_c1(k, r, preq, pres, 1, ia, 0, ra, out);
    check("c1", out, "1e1e3fef878988ead2a74dc5bef13b86", 16);
    /* s1 */
    uint8_t r1[16], r2[16];
    hex(r1, "000F0E0D0C0B0A091122334455667788", 16, 1);
    hex(r2, "010203040506070899AABBCCDDEEFF00", 16, 1);
    smp_s1(k, r1, r2, out);
    check("s1", out, "9a1fe1f0e8b0f49b5b4216ae796da062", 16);
    /* ah */
    uint8_t irk[16], prand[3], h[3];
    hex(irk, "ec0234a357c8ad05341010a60a397d9b", 16, 1);
    hex(prand, "708194", 3, 1);
    smp_ah(irk, prand, h);
    check("ah", h, "0dfbaa", 3);
    uint8_t rpa[6];
    memcpy(rpa, h, 3);
    memcpy(rpa + 3, prand, 3);
    if (!smp_resolves(irk, rpa)) { printf("FAIL resolves\n"); failed = 1; } else printf("ok   resolves\n");
    /* AES-CMAC, RFC 4493 examples 1 and 2 (in protocol order: reversed) */
    uint8_t ck[16], m[16];
    hex(ck, "2b7e151628aed2a6abf7158809cf4f3c", 16, 1);
    smp_cmac(ck, m, 0, out);
    check("cmac empty", out, "bb1d6929e95937287fa37d129b756746", 16);
    hex(m, "6bc1bee22e409f96e93d7e117393172a", 16, 1);
    smp_cmac(ck, m, 16, out);
    check("cmac 16", out, "070a16b46b4d4144f79bdd9dd04a287c", 16);
    /* f4 (D.2) */
    uint8_t u[32], v[32], x[16];
    hex(u, "20b003d2f297be2c5e2c83a7e9f9a5b9eff49111acf4fddbcc0301480e359de6", 32, 1);
    hex(v, "55188b3d32f6bb9a900afcfbeed4e72a59cb9ac2f19d7cfb6b4fdd49f47fc5fd", 32, 1);
    hex(x, "d5cb8454d177733effffb2ec712baeab", 16, 1);
    smp_f4(u, v, x, 0, out);
    check("f4", out, "f2c916f107a9bd1cf1eda1bea974872d", 16);
    /* f5 (D.3) */
    uint8_t w[32], n1[16], n2[16], a1[7], a2[7], mk[16], ltk[16];
    hex(w, "ec0234a357c8ad05341010a60a397d9b99796b13b4f866f1868d34f373bfa698", 32, 1);
    hex(n1, "d5cb8454d177733effffb2ec712baeab", 16, 1);
    hex(n2, "a6e8e7cc25a75f6e216583f7ff3dc4cf", 16, 1);
    hex(a1, "0056123737bfce", 7, 1);
    hex(a2, "00a713702dcfc1", 7, 1);
    smp_f5(w, n1, n2, a1, a2, mk, ltk);
    check("f5 mackey", mk, "2965f176a1084a02fd3f6a20ce636e20", 16);
    check("f5 ltk", ltk, "6986791169d7cd23980522b594750a38", 16);
    /* f6 (D.4) */
    uint8_t rr[16], io[3];
    hex(rr, "12a3343bb453bb5408da42d20c2d0fc8", 16, 1);
    hex(io, "010102", 3, 1);
    smp_f6(mk, n1, n2, rr, io, a1, a2, out);
    check("f6", out, "e3c473989cd0e8c5d26c0b09da958f61", 16);
    /* P-256: two key pairs agree on the DHKey */
    uint8_t da[32], pa[64], db[32], pb[64], za[32], zb[32];
    if (smp_p256_keypair(da, pa, host_rng, NULL) || smp_p256_keypair(db, pb, host_rng, NULL) ||
        smp_p256_dhkey(da, pb, za, host_rng, NULL) || smp_p256_dhkey(db, pa, zb, host_rng, NULL) ||
        memcmp(za, zb, 32) != 0) {
        printf("FAIL p256\n");
        failed = 1;
    } else {
        printf("ok   p256\n");
    }
    pb[40] ^= 1;                                /* off the curve: refused */
    if (smp_p256_dhkey(da, pb, za, host_rng, NULL) == 0) { printf("FAIL bad key accepted\n"); failed = 1; }
    else printf("ok   bad key refused\n");
    printf(failed ? "smp: FAILED\n" : "smp: ok\n");
    return failed;
}
