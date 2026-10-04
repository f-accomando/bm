#include "version.h"

#ifndef BM_VERSION
#define BM_VERSION "dev"
#endif

/* "bmVER=" before the version, so that a tool on the PC finds it in
 * kernel.img (easy_install.sh shows the kernel a card had and has) */
const char bm_version_tag[] = "bmVER=" BM_VERSION;
