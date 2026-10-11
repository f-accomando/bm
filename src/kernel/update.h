/*
 * Updates from GitHub (M19, step 4): Settings > Updates, "Check for updates"
 * reads the latest release of f-accomando/bm (manifest.txt and its
 * signature, net/release.c), compares its version with this kernel's and
 * finds the files that differ from the SD card's; "Install vX.Y.Z"
 * downloads them, checks each one's size and SHA-256 (and that a kernel is
 * one for its board), and only then writes: the games already on the card,
 * bm/ca.pem, a copy of the kernels in /bm/backup, both kernels (the other
 * board's first, this one's last), then a restart. Nothing is written if
 * anything fails before.
 *
 * The releases come from update_url in bm/config.txt (default
 * https://github.com/f-accomando/bm/releases), or from a folder of the SD
 * card with the release's files (update_url=sd:/release/: the tests). The
 * manifest must be signed with the release key (keys/release-pub.pem, or
 * bm/release.pem on the SD card).
 */
#ifndef UPDATE_H
#define UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/fb.h"

/* Straight through (the monitor; what they print goes to the log): the
 * check, then the install (which restarts). */
void update_check(framebuffer_t *fb);
void update_install(framebuffer_t *fb);
/* The monitor's 'u': the check, then a question on the console. */
void update_monitor(void);

/* In the menu (2026-10-11): the check (0) or the install (1) in a fiber
 * of the menu, run by update_tick in its free time; the menu goes on, the
 * Updates panel shows the check, a box over the menu the install, then
 * the 3 s before the restart (update_notice). Not stopped by
 * background_stop: no application starts while it runs (update_busy says
 * what runs, NULL when nothing). update_start: 0, or -1 (one is running,
 * nothing to install, no memory). */
int  update_start(int install);
void update_tick(uint32_t until);
const char *update_busy(void);
/* The install's box (notice.c): 1 while it runs, its title, line and bar
 * (NOTICE_LEN bytes each, the bar 0..1000 or -1). */
int  update_notice(char *title, char *detail, int *progress);

/* For the System panel: what the last check found ("not checked",
 * "checking...", "up to date", "v0.3.0: 4 files, 6.1 MiB"), the release to
 * install or NULL, and the install's row ("installing...", "failed: ...",
 * else the release). */
const char *update_state(void);
const char *update_ready(void);
const char *update_install_state(void);
/* The i-th file the release would write: its path, "new" or "changed"
 * and its size; 0 past the last one (or no check, or one running). */
int  update_changes(int i, const char **path, const char **what, uint32_t *size);

#endif
