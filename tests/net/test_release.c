/*
 * Host test of the release manifests (src/net/release.c) with the files
 * made by scripts/mkrelease.py: tests/net/run_release_test.py signs a
 * release with a test key, and passes the directory and the keys.
 *   test_release DIR PUB OTHER_PUB
 */
#include "net/release.h"

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

/* release_parse of a manifest written here: 0 if accepted */
static int parse_text(const char *text)
{
    release_t r;
    char err[96];
    return release_parse((const uint8_t *)text, strlen(text), &r, err, sizeof err);
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    const char *dir = argv[1];
    size_t ml, sl, pl, n;
    char *man = slurp(dir, "manifest.txt", &ml);
    char *sig = slurp(dir, "manifest.sig", &sl);
    char *pub = slurp("", argv[2], &pl);
    char *other = slurp("", argv[3], &pl);
    char err[160] = "";
    if (!man || !sig || !pub || !other) {
        printf("FAIL missing input\n");
        return 1;
    }

    /* the key built into the kernel: the placeholder, or a real P-256 key */
    char *repo_key = slurp("", "keys/release-pub.pem", &n);
    int real = repo_key && strstr(repo_key, "BEGIN PUBLIC KEY") != NULL;
    check(repo_key && release_set_key(repo_key) == (real ? 0 : -1),
          real ? "keys/release-pub.pem: a P-256 public key" : "keys/release-pub.pem: placeholder, no key");
    free(repo_key);

    check(release_set_key("# no key\n") == -1 && !release_has_key(), "placeholder: no key");
    check(release_verify((uint8_t *)man, ml, (uint8_t *)sig, sl, err, sizeof err) == -1 &&
          strstr(err, "no release key"), "without a key nothing is verified");

    check(release_set_key(pub) == 0 && release_has_key(), "test public key read");
    check(release_verify((uint8_t *)man, ml, (uint8_t *)sig, sl, err, sizeof err) == 0,
          "signature of the manifest: good");

    man[ml / 2] ^= 1;
    check(release_verify((uint8_t *)man, ml, (uint8_t *)sig, sl, err, sizeof err) == -1,
          "one bit changed in the manifest: refused");
    man[ml / 2] ^= 1;
    check(release_verify((uint8_t *)man, ml, (uint8_t *)sig, sl - 3, err, sizeof err) == -1,
          "cut signature: refused");
    check(release_set_key(other) == 0 &&
          release_verify((uint8_t *)man, ml, (uint8_t *)sig, sl, err, sizeof err) == -1 &&
          strstr(err, "not signed"), "signature by another key: refused");
    release_set_key(pub);

    release_t r;
    check(release_parse((uint8_t *)man, ml, &r, err, sizeof err) == 0, "manifest read");
    check(strcmp(r.version, "v9.9.9") == 0 && strcmp(r.commit, "0123abc") == 0, "version and commit");
    check(r.nfiles == 3 && strcmp(r.files[0].asset, "kernel.img") == 0 &&
          strcmp(r.files[0].path, "/kernel.img") == 0 && strcmp(r.files[1].path, "/carts/pong.bm") == 0 &&
          strcmp(r.files[2].asset, "ca.pem") == 0 && strcmp(r.files[2].path, "/bm/ca.pem") == 0,
          "files: names and paths on the SD card");
    int good = 1;
    for (int i = 0; i < r.nfiles; i++) {
        char *d = slurp(dir, r.files[i].asset, &n);
        good &= d && release_check_file(&r.files[i], (uint8_t *)d, n) == 0;
        if (d && i == 0) {
            d[n - 1] ^= 0x80;
            check(release_check_file(&r.files[0], (uint8_t *)d, n) == -1, "kernel with one byte changed: refused");
            check(release_check_file(&r.files[0], (uint8_t *)d, n - 1) == -1, "kernel one byte short: refused");
        }
        free(d);
    }
    check(good, "each file: size and SHA-256 as in the manifest");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    char buf[512];
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a.bm /carts/a.bm 0 %s\n", sha);
    check(parse_text(buf) == 0, "a small manifest");
    snprintf(buf, sizeof buf, "bm release\r\nversion v1\r\nfuture line\r\nfile a.bm /carts/a.bm 0 %s\r\n", sha);
    check(parse_text(buf) == 0, "\\r\\n line ends and unknown lines");
    snprintf(buf, sizeof buf, "bm33 release\nversion v1\nfile a.bm /carts/a.bm 0 %s\n", sha);
    check(parse_text(buf) == -1, "wrong first line: refused");
    snprintf(buf, sizeof buf, "bm release\nfile a.bm /carts/a.bm 0 %s\n", sha);
    check(parse_text(buf) == -1, "no version: refused");
    check(parse_text("bm release\nversion v1\n") == -1, "no files: refused");
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a.bm /carts/../kernel.img 0 %s\n", sha);
    check(parse_text(buf) == -1, "path with ..: refused");
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a/b.bm /carts/a.bm 0 %s\n", sha);
    check(parse_text(buf) == -1, "asset name with /: refused");
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a.bm carts/a.bm 0 %s\n", sha);
    check(parse_text(buf) == -1, "relative path: refused");
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a.bm /carts/a.bm 0 %.63s\n", sha);
    check(parse_text(buf) == -1, "short SHA-256: refused");
    snprintf(buf, sizeof buf, "bm release\nversion v1\nfile a.bm /carts/a.bm 0 %s extra\n", sha);
    check(parse_text(buf) == -1, "extra field: refused");

    free(man);
    free(sig);
    free(pub);
    free(other);
    printf(fails ? "release: %d FAILED\n" : "release: all passed\n", fails);
    return fails != 0;
}
