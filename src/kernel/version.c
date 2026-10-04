#include "version.h"

#ifndef BM_VERSION
#define BM_VERSION "dev"
#endif
#ifndef BM_BRANCH
#define BM_BRANCH "unknown"
#endif

/* "bmVER=" before the version, so that a tool on the PC finds it in
 * kernel.img (easy_install.sh shows the kernel a card had and has) */
const char bm_version_tag[] = "bmVER=" BM_VERSION;

/* the git branch of the build (the reports' names: src/kernel/reports.c) */
const char bm_branch[] = BM_BRANCH;
