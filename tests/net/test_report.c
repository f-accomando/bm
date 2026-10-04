/*
 * The reports' names and header (src/net/report.c): the parts made safe for
 * a path, the date, the file's name with and without the network's time,
 * the header written and read back, the place in the repository.
 */
#include "net/report.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void check(int ok, const char *what, const char *got)
{
    printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, ok ? "" : ": ", ok ? "" : got);
    fails += !ok;
}

int main(void)
{
    char b[256];
    report_slug(b, sizeof b, "Pi Zero W");
    check(!strcmp(b, "pi-zero-w"), "slug: spaces", b);
    report_slug(b, sizeof b, "claude/bare-metal-mvp");
    check(!strcmp(b, "claude-bare-metal-mvp"), "slug: a branch with a slash", b);
    report_slug(b, sizeof b, "v0.1.0-45-gc1dfebb-dirty");
    check(!strcmp(b, "v0.1.0-45-gc1dfebb-dirty"), "slug: git describe kept", b);
    report_slug(b, sizeof b, "  ../..//x  ");
    check(!strcmp(b, "..-..-x"), "slug: no slashes, no dashes at the ends", b);
    report_slug(b, sizeof b, "///");
    check(!strcmp(b, "x"), "slug: never empty", b);
    report_slug(b, 6, "abcdefgh");
    check(!strcmp(b, "abcde"), "slug: cut to the buffer", b);

    report_date(b, sizeof b, 0);
    check(!strcmp(b, "1970-01-01 00:00:00"), "date: 0", b);
    report_date(b, sizeof b, 1791127812ul);
    check(!strcmp(b, "2026-10-04 15:30:12"), "date: 2026-10-04 15:30:12", b);
    report_date(b, sizeof b, 951782400ul);
    check(!strcmp(b, "2000-02-29 00:00:00"), "date: a leap day", b);

    report_info_t r;
    memset(&r, 0, sizeof r);
    strcpy(r.kind, "bench3d");
    strcpy(r.kernel, "v0.1.0-45-gc1dfebb");
    strcpy(r.branch, "bm-core");
    strcpy(r.board, "Pi Zero W");
    strcpy(r.date, "2026-10-04 15:30:12");
    report_file_name(&r, "a1b2c3");
    check(!strcmp(r.file, "20261004-153012_bench3d_pi-zero-w_v0.1.0-45-gc1dfebb.txt"), "name with the date", r.file);
    report_repo_path(b, sizeof b, &r);
    check(!strcmp(b, "reports/bm-core/20261004-153012_bench3d_pi-zero-w_v0.1.0-45-gc1dfebb.txt"), "path", b);

    char text[1024];
    size_t n = report_header(text, sizeof text, &r);
    strcpy(text + n, "R,spheres,ARM,1\n");
    report_info_t back;
    size_t body;
    int ok = report_parse(text, strlen(text), &back, &body) == 0 && !strcmp(back.kind, "bench3d") &&
             !strcmp(back.kernel, r.kernel) && !strcmp(back.branch, "bm-core") && !strcmp(back.board, "Pi Zero W") &&
             !strcmp(back.date, r.date) && !strcmp(back.file, r.file) && !strcmp(text + body, "R,spheres,ARM,1\n");
    check(ok, "header written and read back", text);

    r.date[0] = 0;
    strcpy(r.branch, "claude/x");
    strcpy(r.board, "RGB30");
    strcpy(r.kind, "log");
    report_file_name(&r, "00f0aa");
    check(!strcmp(r.file, "nodate-00f0aa_log_rgb30_v0.1.0-45-gc1dfebb.txt"), "name without the date", r.file);
    n = report_header(text, sizeof text, &r);
    check(strstr(text, "date: unknown (no network time)\n") != NULL, "no date: said so", text);
    ok = report_parse(text, n, &back, &body) == 0 && back.date[0] == 0 && body == n;
    check(ok, "no date: read back empty", text);
    report_repo_path(b, sizeof b, &back);
    check(!strcmp(b, "reports/claude-x/nodate-00f0aa_log_rgb30_v0.1.0-45-gc1dfebb.txt"), "path: the branch's slash", b);
    check(report_parse("hello\n", 6, &back, &body) != 0, "not a report", "");

    printf(fails ? "report: %d FAILED\n" : "report: all passed\n", fails);
    return fails != 0;
}
