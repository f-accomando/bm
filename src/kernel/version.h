#ifndef VERSION_H
#define VERSION_H

/* git describe of the build ("25f5dbc", "25f5dbc-dirty", "v0.1.0"); version.c
 * is rebuilt whenever it changes, so every part of the kernel agrees. In
 * kernel.img it follows "bmVER=" (bm_version_tag). */
extern const char bm_version_tag[];
#define bm_version (bm_version_tag + 6)
/* the git branch it was built from ("bm-core"; in CI the pushed branch) */
extern const char bm_branch[];

#endif
