/* lwIP port for bm33 (ARM1176, newlib, little endian). */
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

#endif
