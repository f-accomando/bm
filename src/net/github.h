/*
 * Publishing a game to the Market from the console (M25, step 6): a pull
 * request to the market's repository (f-accomando/bm-market) through
 * GitHub's REST API with the user's personal token (github_token in
 * bm/config.txt). The owner of the repository gets a branch in it; anyone
 * else a fork first. The pull request adds games/<id>/ with the .bm and
 * info.txt (or updates them, keeping the .bm's name); the market's
 * workflow checks it and, once merged, publishes it.
 */
#ifndef GITHUB_H
#define GITHUB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *api;            /* "https://api.github.com" (the tests: a local server) */
    const char *token;
    const char *repo;           /* the market: "f-accomando/bm-market" */
    const char *base;           /* its main branch: "main" */
    const char *branch;         /* the new branch: "snake-20261001120000" */
    const char *id;             /* the game's folder: games/<id>/ */
    const char *name;           /* the .bm's name for a new game ("snake.bm") */
    const uint8_t *cart;
    size_t cart_len;
    const char *info;           /* info.txt: version, license, about */
    const char *title, *version;    /* for the messages and the pull request */
    void (*progress)(const char *step);     /* a line for the screen, or NULL */
    void (*pause)(unsigned ms);             /* while the fork is made, or NULL */
} gh_publish_t;

/* 0 and the pull request's address in url, or -1 with err. */
int github_publish(const gh_publish_t *p, char *url, size_t url_len, char *err, size_t err_len);

/* A Market id from a file name ("/carts/My Game.bm" -> "my-game"). */
int github_id_from_name(const char *file, char *id, size_t n);

#endif
