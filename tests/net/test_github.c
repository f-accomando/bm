/*
 * Host test of publishing to the Market (src/net/github.c) over POSIX
 * sockets, against the fake GitHub API of tests/net/run_github_test.py.
 *   test_github PORT TOKEN ID NAME CART TITLE VERSION BRANCH
 * Prints "url <pull request>" or "error <message>"; the script checks the
 * fake repositories afterwards.
 */
#include "net/github.h"
#include "net/http.h"

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
    const char *p = d;
    while (n) {
        ssize_t k = send(*(int *)c, p, n, 0);
        if (k <= 0)
            return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int p_read(void *c, void *b, size_t n, uint32_t ms)
{
    struct pollfd pf = { *(int *)c, POLLIN, 0 };
    if (poll(&pf, 1, (int)ms) <= 0)
        return -1;
    return (int)recv(*(int *)c, b, n, 0);
}

static void p_close(void *c)
{
    close(*(int *)c);
    free(c);
}

static const http_transport_t posix = { p_open, p_write, p_read, p_close };
const http_transport_t *http_transport = &posix;

static void progress(const char *s)
{
    printf("step %s\n", s);
}

static void pause_ms(unsigned ms)
{
    usleep(ms * 50);            /* the fake fork is ready at once: no need to wait long */
}

int main(int argc, char **argv)
{
    if (argc < 9)
        return 2;
    FILE *f = fopen(argv[5], "rb");
    if (!f)
        return 2;
    static uint8_t cart[4 << 20];
    size_t len = fread(cart, 1, sizeof cart, f);
    fclose(f);
    char api[64], info[256], url[256], err[256];
    snprintf(api, sizeof api, "http://127.0.0.1:%s", argv[1]);
    snprintf(info, sizeof info, "version: %s\nlicense: MIT\nabout: Sent by the test, \"quoted\".\n", argv[7]);
    gh_publish_t p = {
        .api = api, .token = argv[2], .repo = "f-accomando/bm-market", .base = "main",
        .branch = argv[8], .id = argv[3], .name = argv[4], .cart = cart, .cart_len = len,
        .info = info, .title = argv[6], .version = argv[7], .progress = progress, .pause = pause_ms,
    };
    if (github_publish(&p, url, sizeof url, err, sizeof err) == 0)
        printf("url %s\n", url);
    else
        printf("error %s\n", err);

    char id[32];
    const char *names[][2] = { { "/carts/My Game.bm", "my-game" }, { "SNAKE.BM", "snake" },
                               { "/carts/__x__.bm", "x" }, { "/carts/.bm", "" },
                               { "/carts/abcdefghijklmnopqrstuvwxyz.bm", "abcdefghijklmnopqrstuvw" } };
    for (int i = 0; i < 5; i++) {
        int r = github_id_from_name(names[i][0], id, sizeof id);
        const char *got = r == 0 ? id : "";
        if (strcmp(got, names[i][1]))
            printf("idfail %s -> %s\n", names[i][0], got);
    }
    return 0;
}
