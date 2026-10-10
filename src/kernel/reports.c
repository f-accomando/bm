/*
 * The reports (reports.h): saved as bm/reports/RPTnnnnn.TXT (8.3 names on
 * the card; the long name is in the header), sent with github_put
 * (src/net/github.c), deleted from the card once on GitHub.
 */
#include "reports.h"
#include "config.h"
#include "fiber.h"
#include "drivers/timer.h"
#include "version.h"
#include "drivers/rng.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "net/github.h"
#include "net/net.h"
#include "net/netcon.h"
#include "net/report.h"
#ifdef BM_RGB30
#include "rgb30/plat.h"
#else
#include "drivers/board.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define DIR         "/bm/reports"
#define CAPTURE     (256 * 1024)        /* the most a test prints into its report */

static char *capture;
static char cap_kind[24];
static char last[96] = "none yet";

static const char *cfg(const char *key, const char *def)
{
    const char *v = config_get(key);
    return v && v[0] ? v : def;
}

static const char *board_name(void)
{
#ifdef BM_RGB30
    return PLAT_NAME;
#else
    return board()->name;
#endif
}

/* the number of the last report on the card (0: none), and how many */
static int scan(int *count)
{
    fat_dir_t d;
    fat_entry_t e;
    int best = 0, n = 0;
    if (fat_opendir(&d, DIR) != 0) {
        if (count)
            *count = 0;
        return 0;
    }
    while (fat_readdir(&d, &e) == 1) {
        const char *s = e.name;
        if (e.is_dir || strncasecmp(s, "RPT", 3) || s[3] < '0' || s[3] > '9')
            continue;
        n++;
        int k = atoi(s + 3);
        if (k > best)
            best = k;
    }
    if (count)
        *count = n;
    return best;
}

int reports_pending(void)
{
    int n;
    scan(&n);
    return n;
}

const char *reports_last(void)
{
    return last;
}

static void progress(const char *step)
{
    kprintf("report: %s\n", step);
}

/* reports_auto_due: the address it last saw, a try to make, the last failed one */
#define RETRY_US (15u * 60u * 1000000u)
static uint32_t auto_ip, auto_at;
static int auto_check, auto_failed, put_tried;

/* a report on the card to GitHub; 0 sent (and gone from the card) */
static int send_one(const char *path, int quiet);

static int send(const char *path, int quiet)
{
    const int rc = send_one(path, quiet);
    if (rc == 0)
        return 0;
    if (!put_tried || fiber_cancelled()) {
        auto_check = 1;                 /* no network or token yet, or stopped: at the next chance */
    } else {
        auto_failed = 1;                /* GitHub said no: again in a while */
        auto_at = timer_ticks();
    }
    return rc;
}

static int send_one(const char *path, int quiet)
{
    put_tried = 0;
    const char *token = config_get("github_token");
    if (!token || !token[0]) {
        ksnprintf(last, sizeof last, "on the SD card: no github_token in bm/config.txt");
        return -1;
    }
    if (!net_ip()) {
        ksnprintf(last, sizeof last, "on the SD card: no network");
        return -1;
    }
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    report_info_t r;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
        ksnprintf(last, sizeof last, "%s cannot be read", path);
        return -1;
    }
    if (report_parse((const char *)data, len, &r, NULL) != 0) {
        free(data);
        ksnprintf(last, sizeof last, "%s is not a report", path);
        return -1;
    }
    char repo_path[256], msg[160], url[256], err[160];
    report_repo_path(repo_path, sizeof repo_path, &r);
    ksnprintf(msg, sizeof msg, "report %s from %s (%s, %s)", r.kind, r.board, r.kernel, r.branch);
    gh_put_t p = {
        .api = cfg("github_api", "https://api.github.com"), .token = token,
        .repo = cfg("report_repo", "f-accomando/bm"), .branch = cfg("report_branch", "reports"),
        .path = repo_path, .data = data, .len = len, .message = msg, .progress = quiet ? NULL : progress,
        .replace = !strcmp(r.kind, "session"),
    };
    put_tried = 1;
    int rc = github_put(&p, url, sizeof url, err, sizeof err);
    free(data);
    if (rc != 0) {
        ksnprintf(last, sizeof last, "on the SD card: %s", err);
        kprintf("report: not sent (%s): it waits in bm/reports\n", err);
        return -1;
    }
    fat_delete(path);
    ksnprintf(last, sizeof last, "sent: %s", repo_path);
    kprintf("report: sent to %s, branch %s: %s\n", p.repo, p.branch, repo_path);
    return 0;
}

/* A secret masked wherever it is in the text (as a whole: not inside a
 * longer word or number, so that a code of digits does not eat a figure). */
static void mask(char *text, size_t len, const char *secret)
{
    size_t n = secret ? strlen(secret) : 0;
    if (n < 4)
        return;
    for (size_t i = 0; i + n <= len; i++) {
        if (memcmp(text + i, secret, n))
            continue;
        int before = i > 0 && isalnum((unsigned char)text[i - 1]);
        int after = i + n < len && isalnum((unsigned char)text[i + n]);
        if (before || after)
            continue;
        memset(text + i, '*', n);
        i += n - 1;
    }
}

/* The reports may go to a public repository: the secrets the console knows
 * never leave it (the network console's password is in the log). */
static void mask_secrets(char *text, size_t len)
{
    static const char *const keys[] = { "github_token", "wifi_psk", "net_password", "meshy_key" };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        mask(text, len, config_get(keys[i]));
    mask(text, len, netcon_password());
}

static int save_report(const char *kind, const char *fixed, const char *text, size_t len)
{
    report_info_t r;
    memset(&r, 0, sizeof r);
    report_slug(r.kind, sizeof r.kind, kind);
    ksnprintf(r.kernel, sizeof r.kernel, "%s", bm_version);
    ksnprintf(r.branch, sizeof r.branch, "%s", bm_branch);
    ksnprintf(r.board, sizeof r.board, "%s", board_name());
    if (net_time())
        report_date(r.date, sizeof r.date, net_time());
    uint32_t rnd = 0;
    char tag[12];
    rng_read(&rnd, sizeof rnd);
    ksnprintf(tag, sizeof tag, "%06lx", (unsigned long)(rnd & 0xFFFFFF));
    report_file_name(&r, tag);
    if (fixed)                          /* the last session's: the same name every time, replaced */
        ksnprintf(r.file, sizeof r.file, "%s.txt", fixed);

    char head[512];
    size_t hl = report_header(head, sizeof head, &r);
    char *all = malloc(hl + len + 1);
    if (!all) {
        ksnprintf(last, sizeof last, "not saved: out of memory");
        return -1;
    }
    memcpy(all, head, hl);
    memcpy(all + hl, text, len);
    if (!len || text[len - 1] != '\n')
        all[hl + len++] = '\n';
    mask_secrets(all + hl, len);
    char name[16], path[40];
    ksnprintf(name, sizeof name, "RPT%05d.TXT", scan(NULL) + 1);
    ksnprintf(path, sizeof path, DIR "/%s", name);
    int rc = fat_mkdirs(DIR) == 0 ? fat_write_file(DIR, name, all, hl + len) : -1;
    free(all);
    if (rc != 0) {
        ksnprintf(last, sizeof last, "not saved: %s", fat_error());
        kprintf("report: %s could not be saved (%s)\n", r.file, fat_error());
        return -1;
    }
    kprintf("report: %s saved as bm/reports/%s\n", r.file, name);
    const char *up = config_get("report_upload");
    if (up && !strcmp(up, "0"))
        ksnprintf(last, sizeof last, "on the SD card (report_upload=0)");
    else if (fixed) {                   /* a game has just ended: the menu sends it, without a wait here */
        ksnprintf(last, sizeof last, "on the SD card: goes from the menu");
        auto_check = 1;
    } else if (send(path, 0) != 0)
        kprintf("report: %s\n", last);
    return 0;
}

int reports_text(const char *kind, const char *text, size_t len)
{
    return save_report(kind, NULL, text, len);
}

int reports_session(const char *game, const char *text, size_t len)
{
    char slug[40], fixed[96];
    report_slug(slug, sizeof slug, game);
    ksnprintf(fixed, sizeof fixed, "session_%s_%s", slug, board_name());
    return save_report("session", fixed, text, len);
}

void reports_begin(const char *kind)
{
    free(capture);
    capture = malloc(CAPTURE);
    ksnprintf(cap_kind, sizeof cap_kind, "%s", kind);
    if (capture)
        klog_capture(capture, CAPTURE);
}

void reports_end(void)
{
    if (!capture)
        return;
    size_t n = klog_captured();
    klog_capture(NULL, 0);
    reports_text(cap_kind, capture, n);
    free(capture);
    capture = NULL;
}

int reports_send_pending(void)
{
    int left = 0;
    for (;;) {
        /* the oldest first; a failure stops (the same reason would stop the others) */
        fat_dir_t d;
        fat_entry_t e;
        int first = 0;
        if (fat_opendir(&d, DIR) != 0)
            break;
        while (fat_readdir(&d, &e) == 1) {
            if (e.is_dir || strncasecmp(e.name, "RPT", 3) || e.name[3] < '0' || e.name[3] > '9')
                continue;
            int k = atoi(e.name + 3);
            if (!first || k < first)
                first = k;
        }
        if (!first)
            break;
        char path[40];
        ksnprintf(path, sizeof path, DIR "/RPT%05d.TXT", first);
        if (send(path, 0) != 0) {
            kprintf("report: %s\n", last);
            break;
        }
    }
    scan(&left);
    if (!left && !strncmp(last, "none", 4))
        ksnprintf(last, sizeof last, "none waiting");
    if (!left)
        auto_failed = 0;
    return left;
}

int reports_auto_due(void)
{
    const char *up = config_get("report_upload"), *token = config_get("github_token");
    if ((up && !strcmp(up, "0")) || !token || !token[0])
        return 0;
    const uint32_t ip = net_ip();
    if (!ip) {
        auto_ip = 0;
        return 0;
    }
    if (ip != auto_ip) {                /* the network came (back) */
        auto_ip = ip;
        auto_check = 1;
    } else if (auto_failed && timer_ticks() - auto_at > RETRY_US) {
        auto_failed = 0;
        auto_check = 1;
    }
    if (!auto_check)
        return 0;
    auto_check = 0;
    return reports_pending() > 0;
}
