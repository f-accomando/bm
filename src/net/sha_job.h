/* SHA-256 of a whole buffer on a worker core when there is one (kernel/offload.h). Header only: the users link mbedTLS already.
 * sha_offload returns 0 and fills hash when a worker did it; -1: no worker, the caller hashes itself. */
#ifndef SHA_JOB_H
#define SHA_JOB_H

#include "kernel/offload.h"
#include "mbedtls/sha256.h"
#include <stddef.h>
#include <stdint.h>

#define SHA_OFFLOAD_MIN (64u << 10)

typedef struct {
    const uint8_t *data;
    size_t len;
    unsigned char hash[32];
    int rc;
} sha_job_t;                            /* 56 bytes: fits OFFLOAD_BLK */

static void sha_job_run(void *blk)
{
    sha_job_t *j = blk;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int rc = mbedtls_sha256_starts(&ctx, 0);
    if (rc == 0)
        rc = mbedtls_sha256_update(&ctx, j->data, j->len);
    if (rc == 0)
        rc = mbedtls_sha256_finish(&ctx, j->hash);
    mbedtls_sha256_free(&ctx);
    j->rc = rc;
}

static inline int sha_offload(const uint8_t *data, size_t len, unsigned char hash[32], void (*idle)(void))
{
    if (len < SHA_OFFLOAD_MIN)
        return -1;
    sha_job_t j = { data, len, { 0 }, -1 };
    if (offload_run(sha_job_run, &j, sizeof j, idle) != 0 || j.rc != 0)
        return -1;
    for (int i = 0; i < 32; i++)
        hash[i] = j.hash[i];
    return 0;
}

#endif
