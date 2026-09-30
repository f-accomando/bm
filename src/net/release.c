/*
 * Release manifests (M19): the signature (ECDSA P-256 over SHA-256, the
 * key built into the kernel), the lines of the manifest, the files.
 */
#include "release.h"

#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BM_HOST_TEST
extern const char bm_release_key[];     /* keys/release-pub.pem (embed.S) */
#endif

static mbedtls_pk_context key;
static int key_state;                   /* 0: not read yet, 1: usable, -1: none */

int release_set_key(const char *pem)
{
    if (key_state)
        mbedtls_pk_free(&key);
    mbedtls_pk_init(&key);
    /* the PEM parser wants the terminating NUL counted */
    int r = mbedtls_pk_parse_public_key(&key, (const unsigned char *)pem, strlen(pem) + 1);
    if (r == 0 && (!mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) || mbedtls_pk_get_bitlen(&key) != 256))
        r = -1;
    key_state = r == 0 ? 1 : -1;
    return r == 0 ? 0 : -1;
}

int release_has_key(void)
{
#ifndef BM_HOST_TEST
    if (!key_state)
        release_set_key(bm_release_key);
#endif
    return key_state == 1;
}

int release_verify(const uint8_t *manifest, size_t len, const uint8_t *sig, size_t sig_len,
                   char *err, size_t err_len)
{
    if (!release_has_key()) {
        snprintf(err, err_len, "this kernel has no release key (keys/release-pub.pem)");
        return -1;
    }
    unsigned char hash[32];
    if (mbedtls_sha256(manifest, len, hash, 0) != 0 ||
        mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof hash, sig, sig_len) != 0) {
        snprintf(err, err_len, "the manifest is not signed with the release key");
        return -1;
    }
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Asset names go into download URLs, paths onto the SD card. */
static int char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

static int name_ok(const char *s)
{
    if (!*s || *s == '.')
        return 0;
    for (; *s; s++)
        if (!char_ok(*s))
            return 0;
    return 1;
}

static int path_ok(const char *p)
{
    if (p[0] != '/' || p[1] == 0 || strstr(p, "..") || strstr(p, "//") || strstr(p, "/."))
        return 0;
    for (; *p; p++)
        if (*p != '/' && !char_ok(*p))
            return 0;
    return 1;
}

static int parse_file(const char *s, release_file_t *f)
{
    char name[40], path[64], sha[80];
    unsigned long size;
    char extra;
    if (sscanf(s, "%39s %63s %lu %79s %c", name, path, &size, sha, &extra) != 4)
        return -1;
    if (strlen(name) >= sizeof f->asset || !name_ok(name) ||
        strlen(path) >= sizeof f->path || !path_ok(path) || strlen(sha) != 64)
        return -1;
    for (int i = 0; i < 32; i++) {
        int hi = hexval(sha[2 * i]), lo = hexval(sha[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        f->sha256[i] = (uint8_t)(hi << 4 | lo);
    }
    strcpy(f->asset, name);
    strcpy(f->path, path);
    f->size = (uint32_t)size;
    return 0;
}

int release_parse(const uint8_t *m, size_t len, release_t *r, char *err, size_t err_len)
{
    memset(r, 0, sizeof *r);
    char line[192];
    int no = 0;
    for (size_t pos = 0; pos < len; ) {
        size_t n = 0;
        for (; pos < len && m[pos] != '\n'; pos++, n++)
            if (n < sizeof line - 1)
                line[n] = (char)m[pos];
        pos++;                                  /* the '\n' */
        no++;
        if (n >= sizeof line) {
            snprintf(err, err_len, "manifest line %d too long", no);
            return -1;
        }
        line[n] = 0;
        if (n && line[n - 1] == '\r')
            line[--n] = 0;
        if (no == 1) {
            if (strcmp(line, "bm release") != 0) {
                snprintf(err, err_len, "not a bm release manifest");
                return -1;
            }
        } else if (strncmp(line, "version ", 8) == 0) {
            if (n - 8 >= sizeof r->version || !name_ok(line + 8)) {
                snprintf(err, err_len, "manifest: bad version");
                return -1;
            }
            strcpy(r->version, line + 8);
        } else if (strncmp(line, "commit ", 7) == 0) {
            if (n - 7 >= sizeof r->commit || !name_ok(line + 7)) {
                snprintf(err, err_len, "manifest: bad commit");
                return -1;
            }
            strcpy(r->commit, line + 7);
        } else if (strncmp(line, "file ", 5) == 0) {
            if (r->nfiles == RELEASE_MAX_FILES || parse_file(line + 5, &r->files[r->nfiles]) != 0) {
                snprintf(err, err_len, "manifest line %d: bad file", no);
                return -1;
            }
            r->nfiles++;
        }                                       /* other lines: newer manifests */
    }
    if (!r->version[0] || !r->nfiles) {
        snprintf(err, err_len, "manifest without %s", r->version[0] ? "files" : "a version");
        return -1;
    }
    return 0;
}

int release_check_file(const release_file_t *f, const uint8_t *data, size_t len)
{
    unsigned char hash[32];
    if (len != f->size || mbedtls_sha256(data, len, hash, 0) != 0)
        return -1;
    return memcmp(hash, f->sha256, sizeof hash) == 0 ? 0 : -1;
}
