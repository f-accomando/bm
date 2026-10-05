/*
 * The tests' reports going by themselves (src/kernel/reports.c,
 * reports_auto_due, 2026-10-04) on the PC: an SD card in memory, a GitHub
 * that takes or refuses them, the network and the clock set by the test.
 * They go when the network comes (not before, not without a token or with
 * report_upload=0), once; a refusal waits 15 minutes; a send stopped half
 * way (a game, the Market) goes again at the next chance; a report made
 * while the network is up goes at once.
 */
#include "kernel/reports.h"
#include "drivers/board.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "net/github.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* ---- what reports.c uses */
void uart_putc(char c) { (void)c; }
const char bm_version_tag[] = "bmVER=v0.2.1-9-gtest", bm_branch[] = "bm-core";
const char *netcon_password(void) { return "123456"; }
static const board_t the_board = { .name = "Pi Zero W" };
const board_t *board(void) { return &the_board; }
int rng_read(void *p, size_t n) { memset(p, 0x5a, n); return 0; }
static uint32_t now_us;
uint32_t timer_ticks(void) { return now_us; }
static int cancelled;
int fiber_cancelled(void) { return cancelled; }
static uint32_t ip;
uint32_t net_ip(void) { return ip; }
unsigned long net_time(void) { return 1790000000ul; }

static const char *token = "ghp_secret", *upload;
const char *config_get(const char *key)
{
    if (!strcmp(key, "github_token")) return token;
    if (!strcmp(key, "report_upload")) return upload;
    return NULL;
}

static int puts_ok = 1, puts_n;
int github_put(const gh_put_t *p, char *url, size_t url_len, char *err, size_t err_len)
{
    (void)p;
    (void)url;
    (void)url_len;
    puts_n++;
    if (cancelled) {
        snprintf(err, err_len, "stopped");
        return -1;
    }
    if (!puts_ok) {
        snprintf(err, err_len, "HTTP 500");
        return -1;
    }
    return 0;
}

/* the SD card: the files of bm/reports */
#define FILES 16
static struct { char name[16]; char *data; size_t len; } files[FILES];
static int nfiles;
static char err_text[32] = "no";
const char *fat_error(void) { return err_text; }
int fat_mkdirs(const char *path) { (void)path; return 0; }
int fat_opendir(fat_dir_t *d, const char *path) { (void)path; d->index = 0; return 0; }
int fat_readdir(fat_dir_t *d, fat_entry_t *e)
{
    if ((int)d->index >= nfiles)
        return 0;
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", files[d->index].name);
    e->size = (uint32_t)files[d->index].len;
    e->cluster = d->index++;
    return 1;
}
static int find(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (int i = 0; i < nfiles; i++)
        if (!strcasecmp(files[i].name, base))
            return i;
    return -1;
}
int fat_find(const char *path, fat_entry_t *e)
{
    int i = find(path);
    if (i < 0)
        return -1;
    memset(e, 0, sizeof *e);
    e->cluster = (uint32_t)i;
    e->size = (uint32_t)files[i].len;
    return 0;
}
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    *len = files[e->cluster].len;
    *data = malloc(*len + 1);
    memcpy(*data, files[e->cluster].data, *len);
    return 0;
}
int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    (void)dir;
    if (nfiles >= FILES)
        return -1;
    snprintf(files[nfiles].name, sizeof files[nfiles].name, "%s", name);
    files[nfiles].data = malloc(len);
    memcpy(files[nfiles].data, data, len);
    files[nfiles].len = len;
    nfiles++;
    return 0;
}
int fat_delete(const char *path)
{
    int i = find(path);
    if (i < 0)
        return -1;
    free(files[i].data);
    memmove(&files[i], &files[i + 1], sizeof files[0] * (size_t)(nfiles - i - 1));
    nfiles--;
    return 0;
}

static void report(const char *text)
{
    reports_text("bench", text, strlen(text));
}

int main(void)
{
    /* made without the network: they wait on the card, nothing goes */
    report("3D Bench: 60 fps\n");
    report("CPU bench: 1000 MIPS\n");
    check(nfiles == 2 && puts_n == 0, "no network: two reports wait on the card");
    check(!reports_auto_due(), "  and nothing to send without an address");

    /* without a token or with report_upload=0: never by themselves */
    ip = 0x0101a8c0;
    token = "";
    check(!reports_auto_due(), "no github_token: not due");
    token = "ghp_secret";
    upload = "0";
    check(!reports_auto_due(), "report_upload=0: not due");
    upload = NULL;

    /* the network comes: due once, both go, the card is empty */
    ip = 0;
    reports_auto_due();
    ip = 0x0101a8c0;
    check(reports_auto_due(), "the network came: due");
    check(!reports_auto_due(), "  only once for that network");
    check(reports_send_pending() == 0 && puts_n == 2 && nfiles == 0, "  both sent, gone from the card");
    check(!strncmp(reports_last(), "sent: reports/bm-core/", 22), "  reports_last says where");

    /* GitHub refuses: not again at once, again after 15 minutes */
    puts_ok = 0;
    report("Stress test\n");
    check(nfiles == 1 && puts_n == 3, "a report made on the network: tried at once (refused, it waits)");
    check(!reports_auto_due(), "  not again at once");
    now_us += 14u * 60u * 1000000u;
    check(!reports_auto_due(), "  not after 14 minutes");
    now_us += 2u * 60u * 1000000u;
    puts_ok = 1;
    check(reports_auto_due(), "  again after 15 minutes");
    check(reports_send_pending() == 0 && nfiles == 0, "  and it goes");
    check(!reports_auto_due(), "nothing waits: not due");

    /* a send stopped half way (a game starts, the Market's tab) goes again
     * at the next chance, without waiting 15 minutes */
    ip = 0;
    report("GPU test\n");
    ip = 0x0101a8c0;
    check(reports_auto_due(), "a report waits, the network comes: due");
    cancelled = 1;
    check(reports_send_pending() == 1, "  stopped half way: still on the card");
    cancelled = 0;
    check(reports_auto_due(), "  due again at the next chance");
    check(reports_send_pending() == 0 && nfiles == 0, "  and it goes");

    /* another network (another address): due again if something waits */
    ip = 0;
    report("Diagnostics\n");
    ip = 0x0201a8c0;
    check(reports_auto_due() && reports_send_pending() == 0, "a new address: what waits goes");

    printf(fails ? "\n%d of %d FAILED\n" : "reports: %d/%d checks passed\n", fails ? fails : checks - fails, checks);
    return fails != 0;
}
