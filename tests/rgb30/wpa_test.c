/*
 * WPA2-PSK on the PC (src/rgb30/wpa.c): the published vectors of PBKDF2
 * (IEEE 802.11 annex J), the PRF and AES key unwrap (RFC 3394), then a
 * whole 4-way handshake and a group rekey from an AP computed apart from
 * wpa.c (tests/rgb30/wpa_vectors.py): our messages 2 and 4 and group 2 must
 * be those bytes; a wrong MIC, a replay and a changed ANonce are refused.
 */
#include "rgb30/wpa.h"
#include "wpa_vectors.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void hex(const char *s, uint8_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void test_primitives(void)
{
    uint8_t pmk[32], want[64];
    clock_t t0 = clock();
    wpa_pmk("password", (const uint8_t *)"IEEE", 4, pmk);
    double ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
    hex("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", want, 32);
    CHECK(memcmp(pmk, want, 32) == 0);
    wpa_pmk("ThisIsAPassword", (const uint8_t *)"ThisIsASSID", 11, pmk);
    hex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", want, 32);
    CHECK(memcmp(pmk, want, 32) == 0);

    uint8_t key[20], out[64];
    memset(key, 0x0b, 20);
    wpa_prf(key, 20, "prefix", (const uint8_t *)"Hi There", 8, out, 64);
    hex("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee"
        "75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a", want, 64);
    CHECK(memcmp(out, want, 64) == 0);

    /* RFC 2202 HMAC-SHA1 test 2 */
    wpa_hmac_sha1((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, out);
    hex("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", want, 20);
    CHECK(memcmp(out, want, 20) == 0);

    /* RFC 3394 4.1 */
    uint8_t kek[16], c[24], p[16];
    hex("000102030405060708090a0b0c0d0e0f", kek, 16);
    hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", c, 24);
    CHECK(wpa_unwrap(kek, c, 24, p) == 0);
    hex("00112233445566778899aabbccddeeff", want, 16);
    CHECK(memcmp(p, want, 16) == 0);
    c[5] ^= 1;
    CHECK(wpa_unwrap(kek, c, 24, p) == -1);
    printf("wpa_test: PMK in %.1f ms on this PC\n", ms);
}

static void start(wpa_t *w)
{
    memset(w, 0, sizeof *w);
    wpa_pmk(V_PASS, (const uint8_t *)V_SSID, (unsigned)strlen(V_SSID), w->pmk);
    memcpy(w->aa, v_aa, 6);
    memcpy(w->spa, v_spa, 6);
    memcpy(w->snonce, v_snonce, 32);
    w->ie = v_rsn_ie;
    w->ie_len = sizeof v_rsn_ie;
}

static void test_handshake(void)
{
    wpa_t w;
    uint8_t out[256], frame[512];
    unsigned n;
    start(&w);
    CHECK(memcmp(w.pmk, v_pmk, 32) == 0);

    /* message 1 -> 2 */
    CHECK(wpa_rx(&w, v_m1, sizeof v_m1, out, &n) == WPA_SEND);
    CHECK(memcmp(w.ptk, v_ptk, 48) == 0);
    CHECK(n == sizeof v_m2 && memcmp(out, v_m2, n) == 0);
    /* the AP repeats message 1 (our 2 lost): the same answer */
    CHECK(wpa_rx(&w, v_m1, sizeof v_m1, out, &n) == WPA_SEND && memcmp(out, v_m2, n) == 0);

    /* a message 3 with a bad MIC (another password), then a changed ANonce */
    CHECK(wpa_rx(&w, v_m3_bad, sizeof v_m3_bad, out, &n) == WPA_ERR_MIC && n == 0);
    wpa_t other = w;
    other.anonce[0] ^= 1;
    CHECK(wpa_rx(&other, v_m3, sizeof v_m3, out, &n) == WPA_ERR_NONCE);

    /* message 3 -> 4, the keys; Ethernet padding after the frame is ignored */
    memcpy(frame, v_m3, sizeof v_m3);
    memset(frame + sizeof v_m3, 0, 20);
    CHECK(wpa_rx(&w, frame, sizeof v_m3 + 20, out, &n) == (WPA_SEND | WPA_SET_PTK | WPA_SET_GTK));
    CHECK(n == sizeof v_m4 && memcmp(out, v_m4, n) == 0);
    CHECK(w.gtk_len == 16 && w.gtk_id == 1 && memcmp(w.gtk, v_gtk, 16) == 0);
    CHECK(memcmp(w.ptk + 32, v_ptk + 32, 16) == 0);
    /* message 3 again (replayed): refused */
    CHECK(wpa_rx(&w, v_m3, sizeof v_m3, out, &n) == WPA_ERR_REPLAY);

    /* group rekey */
    CHECK(wpa_rx(&w, v_g1, sizeof v_g1, out, &n) == (WPA_SEND | WPA_SET_GTK));
    CHECK(n == sizeof v_g2 && memcmp(out, v_g2, n) == 0);
    CHECK(w.gtk_id == 2 && memcmp(w.gtk, v_gtk2, 16) == 0);
    CHECK(wpa_rx(&w, v_g1, sizeof v_g1, out, &n) == WPA_ERR_REPLAY);

    /* truncated, not a key frame */
    CHECK(wpa_rx(&w, v_m1, 60, out, &n) == WPA_ERR_FORMAT);
    memcpy(frame, v_m1, sizeof v_m1);
    frame[1] = 0;
    CHECK(wpa_rx(&w, frame, sizeof v_m1, out, &n) == WPA_ERR_FORMAT);
    /* descriptor version 1 (TKIP's HMAC-MD5) */
    frame[1] = 3;
    frame[6] = (uint8_t)((frame[6] & ~7) | 1);
    CHECK(wpa_rx(&w, frame, sizeof v_m1, out, &n) == WPA_ERR_VERSION);
    /* a group message before the 4-way handshake */
    start(&w);
    CHECK(wpa_rx(&w, v_g1, sizeof v_g1, out, &n) == WPA_ERR_FORMAT);
    /* message 3 before message 1 */
    CHECK(wpa_rx(&w, v_m3, sizeof v_m3, out, &n) == WPA_ERR_FORMAT);
}

int main(void)
{
    test_primitives();
    test_handshake();
    if (fails) {
        printf("wpa_test: %d failures\n", fails);
        return 1;
    }
    printf("wpa_test: ok\n");
    return 0;
}
