/*
 * Reports for whoever is developing bm (2026-10-04): the text of a test
 * (what the kernel printed while it ran, or the test's own report) with a
 * header saying the kernel, the branch it was built from, the board and
 * the date, saved in bm/reports on the SD card and sent to a branch of a
 * git repository through GitHub's API, where it can be read:
 *   reports/<branch>/<date>_<kind>_<board>_<kernel>.txt
 * bm/config.txt: github_token (a personal token that can write the
 * repository's contents), report_repo (f-accomando/bm), report_branch
 * (reports), report_upload=0 to keep them on the SD card until they are
 * sent by hand. Without network or token they wait on the card.
 */
#ifndef REPORTS_H
#define REPORTS_H

#include <stddef.h>

/* What the kernel prints from now on goes into a report of this kind... */
void reports_begin(const char *kind);
/* ...until here: saved on the SD card and, if it can, sent. */
void reports_end(void);

/* A report with its own text (the 3D Bench's, a cartridge's report()):
 * 0 saved (and sent if it could), -1 not even saved. */
int reports_text(const char *kind, const char *text, size_t len);

/* The reports still on the SD card sent now: how many are left. */
int reports_send_pending(void);
/* How many wait on the SD card. */
int reports_pending(void);
/* What became of the last one ("sent: reports/...", "on the SD card: no
 * network"), for the menu. */
const char *reports_last(void);

#endif
