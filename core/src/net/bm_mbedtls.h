/*
 * mbedTLS for bm (M19): a TLS 1.2 client, enough for GitHub and most of
 * the web. ECDHE key exchange (P-256, P-384, X25519), AES-GCM and
 * ChaCha20-Poly1305, RSA and ECDSA certificates, SHA-256/384/512 (SHA-1
 * for old chains). Entropy from the BCM2835 hardware generator
 * (mbedtls_hardware_poll in tls.c); no file system, no threads.
 */
#ifndef BM_MBEDTLS_CONFIG_H
#define BM_MBEDTLS_CONFIG_H

/* platform */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_DEPRECATED_REMOVED
#define MBEDTLS_PLATFORM_MS_TIME_ALT     /* mbedtls_ms_time in tls.c */

/* random numbers */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* hashes and ciphers */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C

/* public keys */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_ERROR_C                  /* mbedtls_strerror: readable failures */

/* certificates */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* TLS 1.2 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384

/* IP addresses in certificates: mbedTLS's own parser. Bare metal has no
 * inet_pton, but a C library's headers may still define AF_INET6 (the
 * RGB30's picolibc does with some toolchains), and mbedTLS would call it. */
#define MBEDTLS_TEST_SW_INET_PTON

#endif
