#include "version.h"
#include "bm/bm.h"

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

/* "bmSPEC=" and the revision of the specification (bm.h BM_SPEC_REV): a tool on the PC reads it in kernel.img
 * as it reads the version */
#define BM_STR2(x) #x
#define BM_STR(x) BM_STR2(x)
const char bm_spec_tag[] = "bmSPEC=" BM_STR(BM_SPEC_REV);
