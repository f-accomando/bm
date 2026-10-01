/*
 * Host test of the Market's catalog (src/net/catalog.c) with the files made
 * by scripts/mkmarket.py: tests/net/run_catalog_test.py builds a catalog
 * signed with a test key, and passes the directory, the keys and the
 * covers it expects (RGBA, as in the cartridges).
 *   test_catalog DIR PUB OTHER_PUB [ID COVER.rgba]...
 */
#include "net/catalog.h"
#include "bm/n8cart.h"

#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* what the kernel takes from the hardware generator and the timer */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    (void)out;
    *olen = len;
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (mbedtls_ms_time_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static char *slurp(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s%s%s", dir, dir[0] ? "/" : "", name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    rewind(f);
    char *b = malloc(*len + 1);
    *len = fread(b, 1, *len, f);
    b[*len] = 0;
    fclose(f);
    return b;
}

/* catalog_parse of a catalog written here: 0 if accepted */
static char last_err[160];
static int parse_text(const char *text)
{
    catalog_t c;
    int r = catalog_parse((const uint8_t *)text, strlen(text), &c, last_err, sizeof last_err);
    catalog_free(&c);
    return r;
}

static const catalog_game_t *find(const catalog_t *c, const char *id)
{
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->games[i].id, id) == 0)
            return &c->games[i];
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    const char *dir = argv[1];
    size_t il, sl, pl, n;
    char *idx = slurp(dir, "index.txt", &il);
    char *sig = slurp(dir, "index.sig", &sl);
    char *pub = slurp("", argv[2], &pl);
    char *other = slurp("", argv[3], &pl);
    char err[160] = "";
    if (!idx || !sig || !pub || !other) {
        printf("FAIL missing input\n");
        return 1;
    }

    /* the key built into the kernel: the placeholder, or a real P-256 key */
    char *repo_key = slurp("", "keys/market-pub.pem", &n);
    int real = repo_key && strstr(repo_key, "BEGIN PUBLIC KEY") != NULL;
    check(repo_key && catalog_set_key(repo_key) == (real ? 0 : -1),
          real ? "keys/market-pub.pem: a P-256 public key" : "keys/market-pub.pem: placeholder, no key");
    free(repo_key);

    check(catalog_set_key("# no key\n") == -1 && !catalog_has_key(), "placeholder: no key");
    check(catalog_verify((uint8_t *)idx, il, (uint8_t *)sig, sl, err, sizeof err) == -1 &&
          strstr(err, "no Market key"), "without a key nothing is verified");

    check(catalog_set_key(pub) == 0 && catalog_has_key(), "test public key read");
    check(catalog_verify((uint8_t *)idx, il, (uint8_t *)sig, sl, err, sizeof err) == 0,
          "signature of the catalog: good");
    idx[il / 2] ^= 1;
    check(catalog_verify((uint8_t *)idx, il, (uint8_t *)sig, sl, err, sizeof err) == -1,
          "one bit changed in the catalog: refused");
    idx[il / 2] ^= 1;
    check(catalog_verify((uint8_t *)idx, il, (uint8_t *)sig, sl - 3, err, sizeof err) == -1,
          "cut signature: refused");
    check(catalog_set_key(other) == 0 &&
          catalog_verify((uint8_t *)idx, il, (uint8_t *)sig, sl, err, sizeof err) == -1 &&
          strstr(err, "not signed"), "signature by another key: refused");
    catalog_set_key(pub);

    catalog_t c;
    check(catalog_parse((uint8_t *)idx, il, &c, err, sizeof err) == 0, "catalog read");
    if (fails) {
        printf("     %s\n", err);
        return 1;
    }
    check(strcmp(c.serial, "20261001120000") == 0, "serial");
    check(c.n == 3, "three games");
    const catalog_game_t *snake = find(&c, "snake"), *pong = find(&c, "pong-2"), *plain = find(&c, "plain");
    check(snake && strcmp(snake->title, "Snake") == 0 && strcmp(snake->author, "bm") == 0 &&
          strcmp(snake->version, "1.2") == 0 && strcmp(snake->license, "MIT") == 0,
          "snake: title and author from the header, version and license from info.txt");
    check(snake && strcmp(snake->about, "Eat the apples, do not bite your tail. \x8a la \x85!") == 0,
          "snake: about in CP437 (UTF-8 accents converted)");
    check(snake && strcmp(snake->file.path, "games/snake/snake.bm") == 0 &&
          strcmp(snake->cover.path, "games/snake/cover.png") == 0, "snake: file and cover");
    check(pong && strcmp(pong->author, "") == 0 && strcmp(pong->about, "") == 0 && pong->cover.size,
          "pong: no author, no about");
    check(plain && plain->cover.size == 0 && plain->file.size, "plain: no cover");

    /* each file: size and SHA-256; the covers decode with the kernel's PNG reader */
    int good = 1;
    for (int i = 0; i < c.n; i++) {
        const catalog_game_t *g = &c.games[i];
        char *d = slurp(dir, g->file.path, &n);
        good &= d && catalog_check_file(&g->file, (uint8_t *)d, n) == 0;
        if (d && g == snake) {
            d[n - 1] ^= 0x80;
            check(catalog_check_file(&g->file, (uint8_t *)d, n) == -1, "cartridge with one byte changed: refused");
            check(catalog_check_file(&g->file, (uint8_t *)d, n - 1) == -1, "cartridge one byte short: refused");
        }
        free(d);
        if (g->cover.size) {
            d = slurp(dir, g->cover.path, &n);
            good &= d && catalog_check_file(&g->cover, (uint8_t *)d, n) == 0;
            free(d);
        }
    }
    check(good, "each file: size and SHA-256 as in the catalog");
    check(catalog_check_file(&plain->cover, (const uint8_t *)"", 0) == -1, "a missing cover never matches");

    for (int a = 4; a + 1 < argc; a += 2) {
        const catalog_game_t *g = find(&c, argv[a]);
        size_t pn, rn;
        char *png = g ? slurp(dir, g->cover.path, &pn) : NULL;
        char *want = slurp("", argv[a + 1], &rn);
        uint8_t *rgba = NULL;
        int w = 0, h = 0;
        int ok = png && want && n8_png_rgba((uint8_t *)png, pn, &rgba, &w, &h) == 0 && w == 128 && h == 80 &&
                 rn == 128 * 80 * 4 && memcmp(rgba, want, rn) == 0;
        char what[96];
        snprintf(what, sizeof what, "%s: cover.png decodes to the cartridge's cover", argv[a]);
        check(ok, what);
        free(rgba);
        free(png);
        free(want);
    }
    catalog_free(&c);

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    char buf[1024];
#define GAME(id, extra) "game " id "\ntitle T\nversion 1\nlicense MIT\nfile games/a/a.bm 10 %s\n" extra
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("a", ""), sha);
    check(parse_text(buf) == 0, "a small catalog");
    snprintf(buf, sizeof buf, "bm market\r\nserial 1\r\nfuture line\r\ngame a\r\ntitle T\r\nversion 1\r\n"
             "license MIT\r\nfile games/a/a.bm 10 %s\r\nfuture 2\r\n", sha);
    check(parse_text(buf) == 0, "\\r\\n line ends and unknown lines");
    snprintf(buf, sizeof buf, "bm release\nserial 1\n" GAME("a", ""), sha);
    check(parse_text(buf) == -1 && strstr(last_err, "not a bm Market"), "wrong first line: refused");
    snprintf(buf, sizeof buf, "bm market\n" GAME("a", ""), sha);
    check(parse_text(buf) == -1 && strstr(last_err, "serial"), "no serial: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 12a\n" GAME("a", ""), sha);
    check(parse_text(buf) == -1, "serial not a number: refused");
    check(parse_text("bm market\nserial 1\n") == 0, "no games: an empty market");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n");
    check(parse_text(buf) == -1 && strstr(last_err, "without its file"), "a game without its file: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nfile games/a/a.bm 10 %s\n", sha);
    check(parse_text(buf) == -1 && strstr(last_err, "license"), "a game without a license: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("a", "") GAME("a", ""), sha, sha);
    check(parse_text(buf) == -1 && strstr(last_err, "twice"), "the same id twice: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("A", ""), sha);
    check(parse_text(buf) == -1, "id with capitals: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("../x", ""), sha);
    check(parse_text(buf) == -1, "id with ..: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n"
             "file games/../../kernel.img 10 %s\n", sha);
    check(parse_text(buf) == -1, "path with ..: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n"
             "file /kernel.img 10 %s\n", sha);
    check(parse_text(buf) == -1, "absolute path: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n"
             "file https://x.org/a.bm 10 %s\n", sha);
    check(parse_text(buf) == -1, "another server's address: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n"
             "file games/a/a.bm 99999999 %s\n", sha);
    check(parse_text(buf) == -1, "a cartridge over 8 MiB: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ngame a\ntitle T\nversion 1\nlicense MIT\n"
             "file games/a/a.bm 10 %.63s\n", sha);
    check(parse_text(buf) == -1, "short SHA-256: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("a", "cover games/a/c.png 10 %s x\n"), sha, sha);
    check(parse_text(buf) == -1, "extra field: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("a", "title %s\n"), sha,
             "a title much longer than the forty-eight bytes of the header");
    check(parse_text(buf) == -1, "title too long: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\n" GAME("a", "about a\tb\n"), sha);
    check(parse_text(buf) == -1, "control character in a text: refused");
    snprintf(buf, sizeof buf, "bm market\nserial 1\ntitle T\n" GAME("a", ""), sha);
    check(parse_text(buf) == 0, "fields before the first game: ignored");

    check(catalog_newer("20261001120001", "20261001120000") && !catalog_newer("20261001120000", "20261001120000") &&
          catalog_newer("10", "9") && !catalog_newer("9", "10") && catalog_newer("1", "") &&
          !catalog_newer("", "1") && !catalog_newer("009", "10"), "serials compared as numbers");

    free(idx);
    free(sig);
    free(pub);
    free(other);
    printf(fails ? "catalog: %d FAILED\n" : "catalog: all passed\n", fails);
    return fails != 0;
}
