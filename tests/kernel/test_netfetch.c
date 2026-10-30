/* make test-netfetch: src/kernel/netfetch.c (what "net fetch" downloads and where it goes) on the PC. */
#include "kernel/netfetch.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *src, const char *name, const char *url, const char *path, int github)
{
    fetch_plan_t p;
    char err[160] = "";
    const int r = fetch_plan(src, name, &p, err, sizeof err);
    const int good = r == 0 && !strcmp(p.url, url) && !strcmp(p.path, path) && p.github == github;
    printf("%s %s%s%s -> %s\n", good ? "ok  " : "FAIL", src, name ? " " : "", name ? name : "", r ? err : p.path);
    if (!good)
        printf("     got %s %s %d\n", p.url, p.path, p.github), fails++;
}

static void no(const char *src, const char *name, const char *why)
{
    fetch_plan_t p;
    char err[160] = "";
    const int good = fetch_plan(src, name, &p, err, sizeof err) == -1 && strstr(err, why);
    printf("%s %s refused: %s\n", good ? "ok  " : "FAIL", src ? src : "(none)", err);
    fails += !good;
}

int main(void)
{
    ok("https://example.org/carts/pong.bm", NULL, "https://example.org/carts/pong.bm", "/bm/pong.bm", 0);
    ok("https://example.org/dl?id=7", "Snake.BM", "https://example.org/dl?id=7", "/bm/snake.bm", 0);
    ok("https://example.org/p/yharnam.bme?raw=1", NULL, "https://example.org/p/yharnam.bme?raw=1", "/bme/yharnam.bme", 0);
    ok("gh:f-accomando/bm-market/games/pong/pong.bm", NULL,
       "https://raw.githubusercontent.com/f-accomando/bm-market/HEAD/games/pong/pong.bm", "/bm/pong.bm", 1);
    ok("gh:f-accomando/bm/carts/yharnam/yharnam.bme@bm-core", NULL,
       "https://raw.githubusercontent.com/f-accomando/bm/bm-core/carts/yharnam/yharnam.bme", "/bme/yharnam.bme", 1);
    ok("gh:o/r/a/b/agent.bm@claude/x", "my-ai.bm", "https://raw.githubusercontent.com/o/r/claude/x/a/b/agent.bm",
       "/bm/my-ai.bm", 1);
    ok("gh:f-accomando/bm-market/bmk/island/island.bmk", NULL,
       "https://raw.githubusercontent.com/f-accomando/bm-market/HEAD/bmk/island/island.bmk", "/bmk/island.bmk", 1);
    ok("gh:f-accomando/bm-market/bmm/hunter/hunter.bmm", NULL,
       "https://raw.githubusercontent.com/f-accomando/bm-market/HEAD/bmm/hunter/hunter.bmm", "/bmm/hunter.bmm", 1);
    ok("https://example.org/m/Hunt_Lo.BMM?raw=1", NULL, "https://example.org/m/Hunt_Lo.BMM?raw=1", "/bmm/hunt_lo.bmm", 0);
    ok("https://example.org/dl?id=3", "knight.bmm", "https://example.org/dl?id=3", "/bmm/knight.bmm", 0);
    no(NULL, NULL, "net fetch URL");
    no("http://example.org/pong.bm", NULL, "https");
    no("https://example.org/", NULL, "names no file");
    no("https://example.org/readme.txt", NULL, ".bm cartridge, a .bme project, a .bmk kit or a .bmm model");
    no("https://example.org/hunterlo_long.bmm", NULL, "1 to 8");
    no("https://example.org/model.bmmx", NULL, ".bmm model");
    no("https://example.org/a_very_long_name.bm", NULL, "1 to 8");
    no("https://example.org/pong.bm", "po ng.bm", "a-z, 0-9");
    no("https://example.org/pong.b16", NULL, ".bm cartridge");
    no("gh:onlyowner/pong.bm", NULL, "gh:OWNER/REPO/PATH");
    no("gh:o/r/../x.bm", NULL, "names a file");
    no("gh:o/r/x.bm@", NULL, "after @");
    no("gh:o/r/x.bm@a b", NULL, "branch");
    printf(fails ? "netfetch: %d FAILED\n" : "netfetch: all passed\n", fails);
    return fails != 0;
}
