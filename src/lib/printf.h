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

/* Kernel log: serial console ('\n' becomes "\r\n") plus an optional
 * second sink such as the framebuffer console. */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvlog(const char *fmt, va_list ap);
void kprintf_set_sink(void (*sink)(char c));
/* A third output, such as the network console (NULL: none). */
void kprintf_set_tap(void (*tap)(char c));
void klog_putc(char c);   /* one byte to all log outputs */

/* Everything printed since boot (first 64 KiB), colour escapes removed. */
const char *klog_text(void);
/* What is printed from now on is also kept in buf (size bytes, NUL ended,
 * without the colours) until klog_capture(NULL, 0); klog_captured() says
 * how much. For the reports (src/kernel/reports.c). */
void klog_capture(char *buf, size_t size);
size_t klog_captured(void);
/* Each byte of the log, without colours and '\r', also to ring (NULL:
 * none): the last lines kept for after a crash (src/kernel/crumbs.c). */
void klog_set_ring(void (*ring)(char c));

#endif
