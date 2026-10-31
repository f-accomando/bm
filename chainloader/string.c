/* Minimal freestanding helpers the compiler may emit calls to. */
#include <stddef.h>

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/* The chainloader has no printf: the drivers it shares with the kernel (mbox.c) may call kprintf for a diagnostic
 * line, which the loader drops. */
#include "lib/printf.h"
int kprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
