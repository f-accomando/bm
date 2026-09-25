#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>
#include <stddef.h>

typedef void (*putc_fn)(char c, void *ctx);

/* Supports %d %i %u %x %X %p %s %c %%, flags '-' '0', field width,
 * and the 'l' length modifier (no-op on this 32-bit target). */
int kvprintf(putc_fn out, void *ctx, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Kernel log to the serial console ('\n' becomes "\r\n"). */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvlog(const char *fmt, va_list ap);

#endif
