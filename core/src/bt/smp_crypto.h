/*
 * The cryptographic functions of the Bluetooth LE Security Manager (Core
 * spec Vol 3 Part H, 2.2), for pairing a keyboard. Every value is a byte
 * array in the order it travels over the air: least significant byte
 * first (the spec writes them most significant first).
 */
#ifndef SMP_CRYPTO_H
#define SMP_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* e: AES-128 of plaintext r with key k (security function e). */
void smp_e(const uint8_t k[16], const uint8_t r[16], uint8_t out[16]);

/* LE legacy pairing: the confirm value c1 and the short term key s1.
 * preq/pres: the Pairing Request / Response PDUs (7 bytes, opcode first);
 * iat/rat: address types (0 public, 1 random) of initiator / responder. */
void smp_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
            uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16]);
void smp_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]);

/* ah: the hash of a resolvable private address (prand: its top 3 bytes). */
void smp_ah(const uint8_t irk[16], const uint8_t prand[3], uint8_t out[3]);
/* 1 if addr (6 bytes) is a resolvable private address made with irk. */
int smp_resolves(const uint8_t irk[16], const uint8_t addr[6]);

/* AES-CMAC (RFC 4493), key and message in over-the-air order. */
void smp_cmac(const uint8_t k[16], const uint8_t *m, size_t len, uint8_t out[16]);

/* LE Secure Connections: f4 (confirm), f5 (MacKey and LTK), f6 (DHKey
 * check). u, v, w: 32 bytes; a1, a2: address (6) then type (1). */
void smp_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z, uint8_t out[16]);
void smp_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
            const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16]);
void smp_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
            const uint8_t iocap[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16]);

/* P-256: a new key pair (pub = X then Y, 32 bytes each) and the shared
 * DHKey with the peer's public key. 0, or -1 (no random numbers; a peer
 * key not on the curve). f_rng: fills a buffer, 0 on success. */
typedef int (*smp_rng_t)(void *ctx, unsigned char *buf, size_t len);
int smp_p256_keypair(uint8_t priv[32], uint8_t pub[64], smp_rng_t f_rng, void *ctx);
int smp_p256_dhkey(const uint8_t priv[32], const uint8_t peer[64], uint8_t dhkey[32],
                   smp_rng_t f_rng, void *ctx);

#endif
