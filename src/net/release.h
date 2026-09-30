/*
 * Releases of bm (M19): the manifest that GitHub Actions attaches to each
 * release (scripts/mkrelease.py) says which files there are, where they go
 * on the SD card, their size and SHA-256. It is signed with the project's
 * key (ECDSA P-256, SHA-256); the public half is built into the kernel
 * (keys/release-pub.pem). Nothing is installed unless both checks pass.
 */
#ifndef RELEASE_H
#define RELEASE_H

#include <stddef.h>
#include <stdint.h>

#define RELEASE_MAX_FILES 32

typedef struct {
    char asset[32];             /* its name in the GitHub release */
    char path[48];              /* where it goes on the SD card: /kernel.img */
    uint32_t size;
    uint8_t sha256[32];
} release_file_t;

typedef struct {
    char version[32];           /* the tag: v0.1.0 */
    char commit[41];            /* "" if not given */
    int nfiles;
    release_file_t files[RELEASE_MAX_FILES];
} release_t;

/* The public key, PEM. The kernel has its own; the tests set theirs.
 * Returns 0, or -1 if it is not an EC public key (the placeholder file). */
int release_set_key(const char *pem);
/* 1 if this kernel can check releases. */
int release_has_key(void);

/* 0 if sig (DER) is the key's signature of the manifest's bytes. */
int release_verify(const uint8_t *manifest, size_t len, const uint8_t *sig, size_t sig_len,
                   char *err, size_t err_len);
/* Reads a manifest (checked first with release_verify). 0, or -1 with err. */
int release_parse(const uint8_t *manifest, size_t len, release_t *r, char *err, size_t err_len);
/* 0 if data is the file the manifest describes (size and SHA-256). */
int release_check_file(const release_file_t *f, const uint8_t *data, size_t len);

#endif
