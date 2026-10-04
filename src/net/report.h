/*
 * The console's reports (src/kernel/reports.c): a text file with a few
 * lines that say where it comes from, then what a test printed. They go to
 * a branch of a git repository (the reports branch of f-accomando/bm by
 * default), where whoever asked for them reads them:
 *   reports/<branch>/<date>_<kind>_<board>_<kernel>.txt
 * Portable (tests/net/test_report.c).
 */
#ifndef REPORT_H
#define REPORT_H

#include <stddef.h>

typedef struct {
    char kind[24];              /* "bench3d", "gpu", "log" */
    char kernel[48];            /* git describe of the kernel */
    char branch[64];            /* the git branch it was built from */
    char board[40];             /* "Pi Zero W", "RGB30" */
    char date[24];              /* "2026-10-04 15:30:12", or "" without the network's time */
    char file[160];             /* the name in the repository (report_file_name) */
} report_info_t;

/* s for a name: lower case letters, digits, '.' and '-' (the rest '-',
 * no two in a row), at most n-1 characters, never empty ("x") */
void report_slug(char *out, size_t n, const char *s);

/* "2026-10-04 15:30:12" from seconds since 1970 (UTC) */
void report_date(char *out, size_t n, unsigned long secs);

/* r->file from the rest: <date>_<kind>_<board>_<kernel>.txt; the date
 * "20261004-153012", or "nodate-<tag>" without one (tag: a few random hex
 * digits, so that two reports never take the same name) */
void report_file_name(report_info_t *r, const char *tag);

/* the lines before the body (and an empty line); their length */
size_t report_header(char *out, size_t n, const report_info_t *r);

/* the header of a saved report read back: 0 and the body's offset, or -1 */
int report_parse(const char *text, size_t len, report_info_t *r, size_t *body);

/* where it goes in the repository: reports/<branch slug>/<file> */
void report_repo_path(char *out, size_t n, const report_info_t *r);

#endif
