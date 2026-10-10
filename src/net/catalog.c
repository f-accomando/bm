/*
 * The Market's catalog (M25): the signature (ECDSA P-256 over SHA-256,
 * the market's key built into the kernel), the records, the files.
 */
#include "catalog.h"

#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BM_HOST_TEST
extern const char bm_market_key[];      /* keys/market-pub.pem (embed.S) */
#endif

/* the key built into the kernel, and one from the SD card (bm/market.pem:
 * a market of one's own, the tests) */
static mbedtls_pk_context key, extra;
static int key_state, extra_state;      /* 0: not read yet, 1: usable, -1: none */

static int read_key(mbedtls_pk_context *k, int *state, const char *pem)
{
    if (*state)
        mbedtls_pk_free(k);
    mbedtls_pk_init(k);
    /* the PEM parser wants the terminating NUL counted */
    int r = mbedtls_pk_parse_public_key(k, (const unsigned char *)pem, strlen(pem) + 1);
    if (r == 0 && (!mbedtls_pk_can_do(k, MBEDTLS_PK_ECDSA) || mbedtls_pk_get_bitlen(k) != 256))
        r = -1;
    *state = r == 0 ? 1 : -1;
    return r == 0 ? 0 : -1;
}

int catalog_set_key(const char *pem)
{
    return read_key(&key, &key_state, pem);
}

int catalog_add_key(const char *pem)
{
    return read_key(&extra, &extra_state, pem);
}

int catalog_has_key(void)
{
#ifndef BM_HOST_TEST
    if (!key_state)
        catalog_set_key(bm_market_key);
#endif
    return key_state == 1 || extra_state == 1;
}

int catalog_verify(const uint8_t *index, size_t len, const uint8_t *sig, size_t sig_len,
                   char *err, size_t err_len)
{
    if (!catalog_has_key()) {
        snprintf(err, err_len, "this kernel has no Market key (keys/market-pub.pem)");
        return -1;
    }
    unsigned char hash[32];
    if (mbedtls_sha256(index, len, hash, 0) == 0 &&
        ((key_state == 1 && mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof hash, sig, sig_len) == 0) ||
         (extra_state == 1 && mbedtls_pk_verify(&extra, MBEDTLS_MD_SHA256, hash, sizeof hash, sig, sig_len) == 0)))
        return 0;
    snprintf(err, err_len, "the catalog is not signed with the Market key");
    return -1;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int id_ok(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof ((catalog_game_t *)0)->id || *s == '-')
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-'))
            return 0;
    return 1;
}

/* Paths go into download addresses, under the catalog's: relative, no
 * "..", no hidden names. */
static int path_ok(const char *p)
{
    if (!*p || *p == '/' || strstr(p, "..") || strstr(p, "//") || strstr(p, "/.") || *p == '.')
        return 0;
    for (; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '_' || *p == '-' || *p == '/'))
            return 0;
    return 1;
}

static int parse_file(const char *s, catalog_file_t *f, uint32_t max)
{
    char path[sizeof f->path + 1], sha[80];
    unsigned long size;
    char extra;
    if (sscanf(s, "%96s %lu %79s %c", path, &size, sha, &extra) != 3)
        return -1;
    if (strlen(path) >= sizeof f->path || !path_ok(path) || !size || size > max || strlen(sha) != 64)
        return -1;
    for (int i = 0; i < 32; i++) {
        int hi = hexval(sha[2 * i]), lo = hexval(sha[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        f->sha256[i] = (uint8_t)(hi << 4 | lo);
    }
    strcpy(f->path, path);
    f->size = (uint32_t)size;
    return 0;
}

/* A text field: one line of printable CP437 that fits. */
static int text(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap)
        return -1;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == 127)
            return -1;
    memcpy(dst, s, n + 1);
    return 0;
}

/* a .b16 by its path: the handhelds' cartridge, the rest are .bm */
static int is_b16_path(const char *p)
{
    size_t n = strlen(p);
    return n > 4 && p[n - 4] == '.' && (p[n - 3] | 32) == 'b' && p[n - 2] == '1' && p[n - 1] == '6';
}

/* g is the last game of c (already added): the same id is allowed once per
 * kind, a .bm for the Pi and a .b16 for the handhelds (Overbit has both) */
static const char *finish(const catalog_t *c, const catalog_game_t *g)
{
    if (!g->title[0]) return "without a title";
    if (!g->version[0]) return "without a version";
    if (!g->license[0]) return "without a license";
    if (!g->file.size) return "without its file";
    for (int i = 0; i < c->n - 1; i++)
        if (strcmp(c->games[i].id, g->id) == 0 && is_b16_path(c->games[i].file.path) == is_b16_path(g->file.path))
            return "twice (the same kind)";
    return NULL;
}

int catalog_parse(const uint8_t *m, size_t len, catalog_t *c, char *err, size_t err_len)
{
    memset(c, 0, sizeof *c);
    int cap = 0;
    char line[256];
    int no = 0;
    catalog_game_t *g = NULL;
    if (len > CATALOG_MAX_LEN) {
        snprintf(err, err_len, "catalog too big");
        return -1;
    }
    for (size_t pos = 0; pos < len; ) {
        size_t n = 0;
        for (; pos < len && m[pos] != '\n'; pos++, n++)
            if (n < sizeof line - 1)
                line[n] = (char)m[pos];
        pos++;                                  /* the '\n' */
        no++;
        if (n >= sizeof line) {
            snprintf(err, err_len, "catalog line %d too long", no);
            goto bad;
        }
        line[n] = 0;
        if (n && line[n - 1] == '\r')
            line[--n] = 0;
        int bad_line = 0;
        if (no == 1) {
            if (strcmp(line, "bm market") != 0) {
                snprintf(err, err_len, "not a bm Market catalog");
                goto bad;
            }
        } else if (strncmp(line, "serial ", 7) == 0) {
            const char *s = line + 7;
            bad_line = !*s || strlen(s) >= sizeof c->serial || strspn(s, "0123456789") != strlen(s);
            if (!bad_line)
                strcpy(c->serial, s);
        } else if (strncmp(line, "game ", 5) == 0) {
            const char *why = g ? finish(c, g) : NULL;
            if (why) {
                snprintf(err, err_len, "catalog: %s %s", g->id, why);
                goto bad;
            }
            if (c->n == CATALOG_MAX) {
                snprintf(err, err_len, "catalog: more than %d games", CATALOG_MAX);
                goto bad;
            }
            if (!id_ok(line + 5)) {
                snprintf(err, err_len, "catalog line %d: bad id", no);
                goto bad;
            }
            if (c->n == cap) {
                int ncap = cap ? cap * 2 : 16;
                catalog_game_t *ng = realloc(c->games, (size_t)ncap * sizeof *ng);
                if (!ng) {
                    snprintf(err, err_len, "out of memory");
                    goto bad;
                }
                c->games = ng;
                cap = ncap;
            }
            g = &c->games[c->n++];
            memset(g, 0, sizeof *g);
            strcpy(g->id, line + 5);
        } else if (g && strncmp(line, "title ", 6) == 0) {
            bad_line = text(g->title, sizeof g->title, line + 6);
        } else if (g && strncmp(line, "author ", 7) == 0) {
            bad_line = text(g->author, sizeof g->author, line + 7);
        } else if (g && strncmp(line, "version ", 8) == 0) {
            bad_line = text(g->version, sizeof g->version, line + 8) || strchr(line + 8, ' ');
        } else if (g && strncmp(line, "license ", 8) == 0) {
            bad_line = text(g->license, sizeof g->license, line + 8);
        } else if (g && strncmp(line, "about ", 6) == 0) {
            bad_line = text(g->about, sizeof g->about, line + 6);
        } else if (g && strncmp(line, "file ", 5) == 0) {
            bad_line = parse_file(line + 5, &g->file, CATALOG_MAX_FILE);
        } else if (g && strncmp(line, "cover ", 6) == 0) {
            bad_line = parse_file(line + 6, &g->cover, 256 * 1024);
        } else if (g && (!strcmp(line, "title") || !strcmp(line, "author") || !strcmp(line, "about"))) {
            /* an empty field */
        }                                       /* other lines: newer catalogs */
        if (bad_line) {
            char *sp = strchr(line, ' ');
            if (sp)
                *sp = 0;
            snprintf(err, err_len, "catalog line %d: bad %s", no, line);
            goto bad;
        }
    }
    if (no == 0 || !c->serial[0]) {
        snprintf(err, err_len, no ? "catalog without a serial" : "empty catalog");
        goto bad;
    }
    const char *why = g ? finish(c, g) : NULL;
    if (why) {
        snprintf(err, err_len, "catalog: %s %s", g->id, why);
        goto bad;
    }
    return 0;
bad:
    catalog_free(c);
    return -1;
}

void catalog_free(catalog_t *c)
{
    free(c->games);
    memset(c, 0, sizeof *c);
}

int catalog_check_file(const catalog_file_t *f, const uint8_t *data, size_t len)
{
    unsigned char hash[32];
    if (!f->size || len != f->size || mbedtls_sha256(data, len, hash, 0) != 0)
        return -1;
    return memcmp(hash, f->sha256, sizeof hash) == 0 ? 0 : -1;
}

int catalog_newer(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    while (na > 1 && *a == '0') { a++; na--; }
    while (nb > 1 && *b == '0') { b++; nb--; }
    if (na != nb)
        return na > nb;
    return strcmp(a, b) > 0;
}
