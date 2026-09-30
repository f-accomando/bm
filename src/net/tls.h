/*
 * TLS client connections (M19.2) with mbedTLS: a stream (stream.c) with
 * TLS 1.2 on top, the server's certificate checked against the root
 * certificates (bm/ca.pem) and its name.
 */
#ifndef TLS_H
#define TLS_H

#include <stddef.h>
#include <stdint.h>

typedef struct tls tls_t;

/* The root certificates, PEM text (several). Returns how many were read,
 * -1 on error. Without them tls_open refuses to connect. */
int tls_set_roots(const char *pem, size_t len);
/* Reads bm/ca.pem from the SD card (kernel only). */
int tls_load_roots(void);

tls_t *tls_open(const char *host, uint16_t port, char *err, size_t err_len);
int  tls_write(tls_t *t, const void *data, size_t len);
int  tls_read(tls_t *t, void *buf, size_t len, uint32_t timeout_ms);
void tls_close(tls_t *t);

#endif
