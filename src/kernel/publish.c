#include "publish.h"
#include "config.h"
#include "market.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "net/github.h"
#include "net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_REPO "f-accomando/bm-market"
#define DEFAULT_API  "https://api.github.com"

enum { P_GAME = 400, P_ID, P_VERSION, P_LICENSE, P_TOKEN, P_REPO, P_SEND };

/* the licenses offered: the common ones for games and their art */
static const char *const licenses[] = { "MIT", "CC-BY-4.0", "CC-BY-SA-4.0", "CC0-1.0",
                                        "BM Community License 1.0" };
#define NLIC ((int)(sizeof licenses / sizeof *licenses))

static struct {
    char path[FAT_NAME_MAX + 10], title[49], author[33];
    char id[24], version[16], license[41], about[121];
    int lic;                    /* licenses[lic], or -1: the catalog's own */
} pub;

static const char *repo(void)
{
    const char *r = config_get("market_repo");
    return r && r[0] ? r : DEFAULT_REPO;
}

static int has_token(void)
{
    const char *t = config_get("github_token");
    return t && t[0];
}

/* today, as a version: 2026.10.01 (or 1 before the network time) */
static void today(char *out, size_t n)
{
    time_t now = (time_t)net_time();
    struct tm tm;
    if (!now || !gmtime_r(&now, &tm)) {
        ksnprintf(out, n, "1");
        return;
    }
    ksnprintf(out, n, "%04d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

void publish_setup(const char *path, const char *title, const char *author)
{
    memset(&pub, 0, sizeof pub);
    ksnprintf(pub.path, sizeof pub.path, "%s", path);
    ksnprintf(pub.title, sizeof pub.title, "%s", title);
    ksnprintf(pub.author, sizeof pub.author, "%s", author);
    char old_version[16] = "";
    pub.lic = 0;
    /* a game of the catalog: the same folder, its license and about */
    if (market_lookup(title, author, pub.id, sizeof pub.id, old_version, sizeof old_version,
                      pub.license, sizeof pub.license, pub.about, sizeof pub.about) == 0) {
        pub.lic = -1;
    } else if (github_id_from_name(path, pub.id, sizeof pub.id) != 0) {
        ksnprintf(pub.id, sizeof pub.id, "game");
    }
    today(pub.version, sizeof pub.version);
    if (strcmp(pub.version, old_version) == 0) {    /* twice in a day */
        size_t n = strlen(pub.version);
        if (n + 2 < sizeof pub.version)
            strcpy(pub.version + n, "-2");
    }
}

static const char *license(void)
{
    return pub.lic < 0 ? pub.license : licenses[pub.lic];
}

void publish_panel(home_panel_t *p)
{
    memset(p, 0, sizeof p[0]);
    ksnprintf(p->title, sizeof p->title, "Publish > %s", pub.title);
    home_row(p, MENU_ROW_INFO, P_ID, "Folder in the Market",
             pub.lic < 0 ? "Already in the Market: this is an update" : "games/<id>/, from the file name",
             "games/%s", pub.id);
    home_row(p, MENU_ROW_INFO, P_VERSION, "Version", "The day it is sent", "%s", pub.version);
    home_row(p, MENU_ROW_CHOICE, P_LICENSE, "License", "What others may do with your game", "%s", license());
    home_row(p, MENU_ROW_INFO, P_TOKEN, "GitHub token",
             has_token() ? "github_token in bm/config.txt" : "Put github_token=... in bm/config.txt",
             "%s", has_token() ? "set" : "missing");
    home_row(p, MENU_ROW_INFO, P_REPO, "Market", "The repository the pull request goes to", "%s", repo());
    home_row(p, MENU_ROW_ACTION, P_SEND, "Send the pull request",
             "The Market checks it; once merged, everyone can get it", NULL);
}

static void step(const char *s)
{
    kprintf("  %s\n", s);
}

static void wait_ms(unsigned ms)
{
    for (uint32_t t0 = timer_ticks(); timer_ticks() - t0 < ms * 1000u; )
        net_wait_step();
}

static void run(framebuffer_t *fb)
{
    (void)fb;
    kprintf("\n\x1b[1;96mbm Market: publishing %s\x1b[0m\n", pub.title);
    kprintf("  games/%s, version %s, license %s, to %s\n", pub.id, pub.version, license(), repo());
    if (!net_ip()) {
        kprintf("\x1b[91mno network: connect in Settings > WiFi and network\x1b[0m\n");
        return;
    }
    fat_entry_t e;
    uint8_t *cart;
    size_t len;
    if (fat_find(pub.path, &e) != 0 || fat_load(&e, &cart, &len) != 0) {
        kprintf("\x1b[91mcannot read %s: %s\x1b[0m\n", pub.path, fat_error());
        return;
    }
    char info[320], branch[48], url[256], err[256], name[64];
    ksnprintf(info, sizeof info, "version: %s\nlicense: %s\n%s%s%s", pub.version, license(),
              pub.about[0] ? "about: " : "", pub.about, pub.about[0] ? "\n" : "");
    unsigned long t = net_time();
    ksnprintf(branch, sizeof branch, "%s-%lu", pub.id, t ? t : (unsigned long)timer_ticks());
    ksnprintf(name, sizeof name, "%s.bm", pub.id);
    const char *api = config_get("github_api");
    gh_publish_t p = {
        .api = api && api[0] ? api : DEFAULT_API, .token = config_get("github_token"), .repo = repo(),
        .base = "main", .branch = branch, .id = pub.id, .name = name, .cart = cart, .cart_len = len,
        .info = info, .title = pub.title, .version = pub.version, .progress = step, .pause = wait_ms,
    };
    uint32_t t0 = timer_ticks();
    int r = github_publish(&p, url, sizeof url, err, sizeof err);
    free(cart);
    if (r == 0) {
        kprintf("\x1b[92mpull request sent in %lu s:\x1b[0m\n  %s\n", (timer_ticks() - t0) / 1000000, url);
        kprintf("  The Market checks it; once it is merged, the game is in the catalog.\n");
    } else {
        kprintf("\x1b[91mnot sent: %s\x1b[0m\n", err);
    }
}

void publish_act(int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    switch (row) {
    case P_LICENSE:                             /* left / right, or A: the next one */
        if (pub.lic < 0)
            pub.lic = how < 0 ? NLIC - 1 : 0;
        else
            pub.lic = (pub.lic + (how < 0 ? NLIC - 1 : 1)) % NLIC;
        break;
    case P_SEND:
        if (!has_token()) {
            ksnprintf(d->note, sizeof d->note, "no github_token in bm/config.txt");
        } else if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Publish %s?", pub.title);
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "Pull request to the Market, license %s", license());
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Publish");
        } else if (how == HOME_YES) {
            d->what = HOME_TEXT;
            d->text = run;
            d->wait = 1;
        }
        break;
    }
}
