/*
 * WPA2-PSK for the RGB30's WiFi: the station's side of the 4-way and group
 * key handshakes (IEEE 802.11i), in software, because the RTL8821C's
 * firmware has no supplicant. The keys go to the chip's CAM, which does
 * CCMP. Host-tested (tests/rgb30/wpa_test.c).
 */
#ifndef WPA_H
#define WPA_H

#include <stdint.h>

void wpa_hmac_sha1(const uint8_t *key, unsigned klen, const uint8_t *msg, unsigned mlen,
                   uint8_t out[20]);
/* PMK = PBKDF2-HMAC-SHA1(passphrase, SSID, 4096, 32) */
void wpa_pmk(const char *pass, const uint8_t *ssid, unsigned ssid_len, uint8_t pmk[32]);
/* PRF-n: HMAC-SHA1(key, label 0 data i) for i = 0, 1, ..., out olen bytes */
void wpa_prf(const uint8_t *key, unsigned klen, const char *label, const uint8_t *data,
             unsigned dlen, uint8_t *out, unsigned olen);
/* AES key unwrap (RFC 3394): in len bytes (n + 1 blocks of 8) to out (len
 * - 8 bytes); 0, or -1 if the integrity check fails */
int wpa_unwrap(const uint8_t kek[16], const uint8_t *in, unsigned len, uint8_t *out);

typedef struct {
    uint8_t pmk[32];
    uint8_t aa[6], spa[6];          /* the AP (authenticator), us (supplicant) */
    uint8_t snonce[32];             /* random, set by the caller before M1 */
    uint8_t anonce[32];
    uint8_t ptk[48];                /* KCK 0..15, KEK 16..31, TK 32..47 */
    uint8_t replay[8];              /* last replay counter with a good MIC */
    uint8_t have_replay, have_anonce, done;
    const uint8_t *ie;              /* our RSN IE, as in the association request */
    unsigned ie_len;
    uint8_t gtk[32], gtk_len, gtk_id;
} wpa_t;

/* what wpa_rx asks the caller to do (bits), or an error (negative) */
#define WPA_SEND        1           /* send out[0..olen) (EAPOL, ethertype 888E) */
#define WPA_SET_PTK     2           /* then install ptk + 32 (pairwise, CCMP) */
#define WPA_SET_GTK     4           /* then install gtk (group, at gtk_id) */
#define WPA_ERR_FORMAT  (-1)
#define WPA_ERR_MIC     (-2)
#define WPA_ERR_REPLAY  (-3)
#define WPA_ERR_VERSION (-4)        /* not HMAC-SHA1 + AES key wrap */
#define WPA_ERR_UNWRAP  (-5)
#define WPA_ERR_NONCE   (-6)        /* M3 with another ANonce than M1 */

/* An EAPOL frame from the AP (from its 4-byte EAPOL header on); out gets
 * the answer (up to 128 + ie_len bytes). */
int wpa_rx(wpa_t *w, const uint8_t *eapol, unsigned len, uint8_t *out, unsigned *olen);

#endif
