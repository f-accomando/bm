/*
 * Host test of the picture-to-model flow (src/net/img3d.c and the .glb
 * reader src/bm/glb.c) against the fake service of run_img3d_test.py:
 * the job is started with the picture as data, polled until done, the
 * .glb downloaded and turned into a model; a bad key, a failed job and a
 * URL picture are tried too.
 *
 *   test_img3d http://127.0.0.1:PORT PICTURE.png
 */
#include "net/img3d.h"
#include "net/http.h"
#include "bm/glb.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *p_open(const char *host, uint16_t port, int tls, char *err, size_t err_len)
{
    if (tls) {
        snprintf(err, err_len, "no tls here");
        return NULL;
    }
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &ai)) {
        snprintf(err, err_len, "%s: unknown name", host);
        return NULL;
    }
    int fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        snprintf(err, err_len, "connection refused");
        close(fd);
        freeaddrinfo(ai);
        return NULL;
    }
    freeaddrinfo(ai);
    int *c = malloc(sizeof *c);
    *c = fd;
    return c;
}

static int p_write(void *c, const void *d, size_t n)
{
    return write(*(int *)c, d, n) == (ssize_t)n ? 0 : -1;
}

static int p_read(void *c, void *b, size_t n, uint32_t ms)
{
    struct pollfd pf = { *(int *)c, POLLIN, 0 };
    if (poll(&pf, 1, (int)ms) <= 0)
        return -1;
    return (int)read(*(int *)c, b, n);
}

static void p_close(void *c)
{
    close(*(int *)c);
    free(c);
}

static const http_transport_t posix = { p_open, p_write, p_read, p_close };
const http_transport_t *http_transport = &posix;

static int checks, fails;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_img3d http://127.0.0.1:PORT PICTURE\n");
        return 2;
    }
    const img3d_provider_t *p = img3d_provider("meshy");
    check(p != NULL && strcmp(img3d_key_name(p), "meshy_key") == 0, "the meshy provider and its key's name");
    check(img3d_provider("nobody") == NULL && strcmp(img3d_provider_name(0), "meshy") == 0 && !img3d_provider_name(9),
          "the providers' names");
    img3d_set_base(p, argv[1]);
    FILE *f = fopen(argv[2], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *pic = malloc((size_t)n);
    if (fread(pic, 1, (size_t)n, f) != (size_t)n)
        n = 0;
    fclose(f);
    char task[64], err[128], url[256];
    int progress;

    /* the whole flow: start, poll, download, convert */
    check(img3d_start(p, "good-key", pic, (size_t)n, NULL, 1500, task, sizeof task, err, sizeof err) == 0, err);
    check(strcmp(task, "job-1") == 0, "the job id from the service");
    int r, polls = 0;
    while ((r = img3d_status(p, "good-key", task, &progress, url, sizeof url, err, sizeof err)) == 0 && polls < 10)
        polls++;
    check(r == 1 && polls == 2 && progress == 100, "polled until done (two looks on the way)");
    check(strstr(url, "/model.glb") != NULL, "the .glb's address");
    uint8_t *glb = NULL;
    size_t glen = 0;
    check(img3d_download(url, 16 << 20, &glb, &glen, err, sizeof err) == 0 && glen > 100, "the .glb downloaded");
    glb_model_t m;
    glb_opts_t o = { 2.0f, 1200, 256 };
    check(glb_to_model(glb, glen, "pic", &o, &m, err, sizeof err) == 0, err);
    check(m.nf == 18 && m.textured && m.texture != NULL, "the model: 18 triangles, textured");
    glb_model_free(&m);
    free(glb);

    /* a picture at a URL: the service fetches it */
    check(img3d_start(p, "good-key", NULL, 0, "https://example.org/hero.png", 0, task, sizeof task, err, sizeof err) == 0 &&
          strcmp(task, "job-url") == 0, "a job on a picture at a URL");

    /* a bad key */
    check(img3d_start(p, "bad-key", pic, (size_t)n, NULL, 0, task, sizeof task, err, sizeof err) < 0 &&
          strstr(err, "refuses the key") != NULL, "a refused key is said");
    /* a failed job */
    check(img3d_status(p, "good-key", "job-fail", &progress, url, sizeof url, err, sizeof err) < 0 &&
          strstr(err, "failed") != NULL && strstr(err, "no face") != NULL, "a failed job says why");
    /* not a picture */
    check(img3d_start(p, "good-key", (const uint8_t *)"hello", 5, NULL, 0, task, sizeof task, err, sizeof err) < 0,
          "bytes that are not a picture are refused");
    /* the service down */
    img3d_set_base(p, "http://127.0.0.1:1");
    check(img3d_start(p, "good-key", pic, (size_t)n, NULL, 0, task, sizeof task, err, sizeof err) < 0 &&
          strstr(err, "meshy:") != NULL, "an unreachable service is said");
    free(pic);
    printf("img3d: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
