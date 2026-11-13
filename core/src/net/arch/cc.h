/* lwIP port for bm (ARM1176, newlib, little endian). */
#ifndef ARCH_CC_H
#define ARCH_CC_H

#include <stdint.h>
#include <stdlib.h>

#include "lib/printf.h"

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x)   do { kprintf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { kprintf("lwip assert: %s (%s:%d)\n", x, __FILE__, __LINE__); } while (0)
#define LWIP_RAND()             ((uint32_t)rand())
#define LWIP_NO_UNISTD_H        1

#ifdef BM_RGB30     /* picolibc has ssize_t (long) but no SSIZE_MAX: lwIP would make its own */
#include <limits.h>
#include <sys/types.h>
#define SSIZE_MAX               LONG_MAX
#endif

#endif
