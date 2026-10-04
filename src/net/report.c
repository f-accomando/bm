/*
 * The reports' names and header (report.h). Portable.
 */
#include "report.h"

#include <stdio.h>
#include <string.h>

void report_slug(char *out, size_t n, const char *s)
{
    size_t o = 0;
    int dash = 1;                               /* none at the start */
    for (; s && *s && o + 1 < n; s++) {
        char c = *s >= 'A' && *s <= 'Z' ? (char)(*s + 32) : *s;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.') {
            out[o++] = c;
            dash = 0;
        } else if (!dash) {
            out[o++] = '-';
            dash = 1;
        }
    }
    while (o && out[o - 1] == '-')
        o--;
    if (!o && n > 1)
        out[o++] = 'x';
    if (n)
        out[o] = 0;
}

void report_date(char *out, size_t n, unsigned long secs)
{
    /* days to the civil date (Howard Hinnant's algorithm) */
    long z = (long)(secs / 86400) + 719468;
    long era = z / 146097, doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    unsigned long t = secs % 86400;
    snprintf(out, n, "%04ld-%02ld-%02ld %02lu:%02lu:%02lu", y + (m <= 2), m, d, t / 3600, t / 60 % 60,
             t % 60);
}

void report_file_name(report_info_t *r, const char *tag)
{
    char date[24], kind[24], board[40], kernel[48];
    if (r->date[0]) {                           /* "2026-10-04 15:30:12" -> "20261004-153012" */
        size_t o = 0;
        for (const char *p = r->date; *p && o + 1 < sizeof date; p++)
            if (*p >= '0' && *p <= '9')
                date[o++] = *p;
            else if (*p == ' ')
                date[o++] = '-';
        date[o] = 0;
    } else {
        char t[12];
        report_slug(t, sizeof t, tag && tag[0] ? tag : "0");
        snprintf(date, sizeof date, "nodate-%s", t);
    }
    report_slug(kind, sizeof kind, r->kind);
    report_slug(board, sizeof board, r->board);
    report_slug(kernel, sizeof kernel, r->kernel);
    snprintf(r->file, sizeof r->file, "%s_%s_%s_%s.txt", date, kind, board, kernel);
}

size_t report_header(char *out, size_t n, const report_info_t *r)
{
    int k = snprintf(out, n, "bm report\nkind: %s\nkernel: %s\nbranch: %s\nboard: %s\ndate: %s\nfile: %s\n\n",
                     r->kind, r->kernel, r->branch, r->board,
                     r->date[0] ? r->date : "unknown (no network time)", r->file);
    return k < 0 ? 0 : (size_t)k < n ? (size_t)k : n ? n - 1 : 0;
}

/* the value of "key: value" on a line of the header */
static void field(const char *line, size_t len, const char *key, char *out, size_t n)
{
    size_t k = strlen(key);
    if (len > k + 1 && !memcmp(line, key, k) && line[k] == ':') {
        size_t i = k + 1;
        while (i < len && line[i] == ' ')
            i++;
        size_t m = len - i < n - 1 ? len - i : n - 1;
        memcpy(out, line + i, m);
        out[m] = 0;
    }
}

int report_parse(const char *text, size_t len, report_info_t *r, size_t *body)
{
    memset(r, 0, sizeof *r);
    if (len < 10 || memcmp(text, "bm report\n", 10))
        return -1;
    size_t at = 10;
    while (at < len) {
        const char *nl = memchr(text + at, '\n', len - at);
        size_t l = nl ? (size_t)(nl - (text + at)) : len - at;
        if (l == 0) {                           /* the empty line: the body follows */
            at++;
            break;
        }
        const char *line = text + at;
        field(line, l, "kind", r->kind, sizeof r->kind);
        field(line, l, "kernel", r->kernel, sizeof r->kernel);
        field(line, l, "branch", r->branch, sizeof r->branch);
        field(line, l, "board", r->board, sizeof r->board);
        field(line, l, "date", r->date, sizeof r->date);
        field(line, l, "file", r->file, sizeof r->file);
        at += l + 1;
    }
    if (!strncmp(r->date, "unknown", 7))
        r->date[0] = 0;
    if (body)
        *body = at < len ? at : len;
    return r->kind[0] && r->file[0] ? 0 : -1;
}

void report_repo_path(char *out, size_t n, const report_info_t *r)
{
    char branch[64];
    report_slug(branch, sizeof branch, r->branch[0] ? r->branch : "unknown");
    snprintf(out, n, "reports/%s/%s", branch, r->file);
}
