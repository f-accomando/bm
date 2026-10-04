/*
 * Updates from GitHub (M19, step 4): Settings > System, "Check for updates"
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

#include "drivers/fb.h"

/* On the text console: the check, then the install (which restarts). */
void update_check(framebuffer_t *fb);
void update_install(framebuffer_t *fb);
/* The monitor's 'u': the check, then a question on the console. */
void update_monitor(void);

/* For the System panel: what the last check found ("not checked", "up to
 * date", "v0.3.0: 4 files, 6.1 MiB"), and the release to install, or NULL. */
const char *update_state(void);
const char *update_ready(void);

#endif
