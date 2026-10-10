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
 * sent by hand. Without network or token they wait on the card, and go by
 * themselves once the console is on the network (reports_auto_due).
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

/* The dev kit's report of the last session of a game or tool (2026-10-10):
 * always the same file, reports/<branch>/session_<game>_<board>.txt, which
 * a new run of the same game replaces. 0 saved, -1 not even saved. */
int reports_session(const char *game, const char *text, size_t len);

/* The reports still on the SD card sent now: how many are left. */
int reports_send_pending(void);
/* How many wait on the SD card. */
int reports_pending(void);
/* The reports waiting go by themselves (2026-10-04, the user's request):
 * 1 when it is time to send them now with reports_send_pending (some wait,
 * a github_token, report_upload not 0, an address: the network just came,
 * or 15 minutes after a failed try). Cheap: call it every frame of the
 * menu, where the network is free. */
int reports_auto_due(void);
/* What became of the last one ("sent: reports/...", "on the SD card: no
 * network"), for the menu. */
const char *reports_last(void);

#endif
