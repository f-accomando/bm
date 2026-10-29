/*
 * ai_pair: bm Agent pairs the console with tools/bm_agent_relay.py on the user's PC without a PC tool.
 * The relay prints a code of 20 characters (Crockford base32, groups of 4):
 *   - the first 12: the fingerprint of its CA, the first 60 bits of SHA-256 of the certificate (DER);
 *   - the last 8: the relay's token (ai_key).
 * The kernel fetches the CA over plain http on the LAN (web_start_lan), checks it against the first 12
 * characters, asks the player, then keeps it in bm/ca-user.pem (updates rewrite bm/ca.pem, never this file).
 * Another PC on the LAN cannot put its own CA in: it would need one whose fingerprint is the code's.
 */
#ifndef AIPAIR_H
#define AIPAIR_H

#include <stddef.h>

#define AI_PAIR_CODE_LEN 20
#define AI_PAIR_FP_LEN   12
#define AI_USER_CA       "ca-user.pem"      /* in /bm: the roots the user added, read after ca.pem */

/* The code as typed (any case, "-" and spaces ignored, O read as 0, I and L as 1) into out: 20 characters
 * of the alphabet 0-9 A-Z without I L O U. 0, or -1 and the reason in err. */
int ai_pair_code(const char *typed, char out[AI_PAIR_CODE_LEN + 1], char *err, size_t en);

/* The fingerprint of a DER certificate: 12 characters of the same alphabet. */
void ai_pair_fingerprint(const unsigned char *der, size_t len, char out[AI_PAIR_FP_LEN + 1]);

/* 0 if pem holds exactly one certificate, a CA, whose fingerprint is the first 12 characters of code
 * (normalized by ai_pair_code). Else -1 and the reason in err. */
int ai_pair_check(const char *pem, size_t len, const char *code, char *err, size_t en);

/* Kernel only: adds the certificate (PEM) to bm/ca-user.pem if it is not there, and reloads the roots.
 * 0, or -1 and the reason in err. */
int ai_pair_install(const char *pem, size_t len, char *err, size_t en);

#endif
