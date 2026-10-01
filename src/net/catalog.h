/*
 * The catalog of the bm Market (M25): index.txt, made and signed by
 * scripts/mkmarket.py in the market's repository (f-accomando/bm-market)
 * and served by its GitHub Pages. Signed with the market's key (ECDSA
 * P-256, SHA-256; keys/market-pub.pem, built into the kernel, not the
 * release key); every file it lists is checked with its size and SHA-256
 * before it is used, wherever it came from.
 */
#ifndef CATALOG_H
#define CATALOG_H

#include <stddef.h>
#include <stdint.h>

#define CATALOG_MAX     256             /* games */
#define CATALOG_MAX_LEN (256 * 1024)    /* bytes of index.txt */
#define CATALOG_MAX_FILE (8u * 1024 * 1024)   /* a cartridge (scripts/mkmarket.py) */

typedef struct {
    char path[96];              /* under the catalog's address: games/snake/snake.bm */
    uint32_t size;              /* 0: no such file (a game without a cover) */
    uint8_t sha256[32];
} catalog_file_t;

typedef struct {
    char id[24];                /* a-z, 0-9, '-' */
    char title[49];             /* as in the cartridge's header (CP437) */
    char author[33];
    char version[16];
    char license[41];
    char about[121];
    catalog_file_t file;        /* the .bm */
    catalog_file_t cover;       /* a 128x80 PNG */
} catalog_game_t;

typedef struct {
    char serial[16];            /* only grows: 20261001120000 */
    int n;
    catalog_game_t *games;      /* malloc'd; catalog_free */
} catalog_t;

/* The public key, PEM. The kernel has its own; the tests set theirs.
 * Returns 0, or -1 if it is not an EC P-256 public key (the placeholder). */
int catalog_set_key(const char *pem);
/* 1 if this kernel can check the catalog. */
int catalog_has_key(void);

/* 0 if sig (DER) is the key's signature of index's bytes. */
int catalog_verify(const uint8_t *index, size_t len, const uint8_t *sig, size_t sig_len,
                   char *err, size_t err_len);
/* Reads index.txt (checked first with catalog_verify). 0, or -1 with err. */
int catalog_parse(const uint8_t *index, size_t len, catalog_t *c, char *err, size_t err_len);
void catalog_free(catalog_t *c);
/* 0 if data is the file the catalog lists (size and SHA-256). */
int catalog_check_file(const catalog_file_t *f, const uint8_t *data, size_t len);
/* 1 if serial a is newer than b ("" is the oldest). */
int catalog_newer(const char *a, const char *b);

#endif
