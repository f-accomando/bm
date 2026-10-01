#include "printf.h"
#include "drivers/uart.h"

#include <stdint.h>

static int emit_padded(putc_fn out, void *ctx, const char *s, int len,
                       int width, int left, char pad)
{
    int n = 0;
    if (!left && pad == '0' && len > 0 && s[0] == '-' && width > len) {
        out('-', ctx); n++;
        s++; len--; width--;
    }
    if (!left)
        for (; width > len; width--, n++)
            out(pad, ctx);
    for (int i = 0; i < len; i++, n++)
        out(s[i], ctx);
    if (left)
        for (; width > len; width--, n++)
            out(' ', ctx);
    return n;
}

static int fmt_uint(char *buf, uint32_t v, unsigned base, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    return n;
}

int kvprintf(putc_fn out, void *ctx, const char *fmt, va_list ap)
{
    int total = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt, ctx);
            total++;
            continue;
        }

        int left = 0, width = 0;
        char pad = ' ';
        for (fmt++; *fmt == '-' || *fmt == '0'; fmt++) {
            if (*fmt == '-') left = 1;
            else pad = '0';
        }
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        while (*fmt == 'l')
            fmt++;
        if (left)
            pad = ' ';

        char buf[16];
        int len = 0;
        const char *s = buf;

        switch (*fmt) {
        case 'd': case 'i': {
            int32_t v = va_arg(ap, int32_t);
            uint32_t u = (uint32_t)v;
            if (v < 0) { buf[len++] = '-'; u = -u; }
            len += fmt_uint(buf + len, u, 10, 0);
            break;
        }
        case 'u':
            len = fmt_uint(buf, va_arg(ap, uint32_t), 10, 0);
            break;
        case 'x': case 'X':
            len = fmt_uint(buf, va_arg(ap, uint32_t), 16, *fmt == 'X');
            break;
        case 'p':
            buf[0] = '0'; buf[1] = 'x';
            len = 2 + fmt_uint(buf + 2, (uint32_t)(uintptr_t)va_arg(ap, void *), 16, 0);
            break;
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (s[len]) len++;
            break;
        case '%':
            buf[0] = '%';
            len = 1;
            break;
        case '\0':
            return total;
        default:            /* unknown: print verbatim */
            buf[0] = '%'; buf[1] = *fmt;
            len = 2;
            break;
        }
        total += emit_padded(out, ctx, s, len, width, left, pad);
    }
    return total;
}

struct sbuf { char *p; size_t left; };

static void sbuf_putc(char c, void *ctx)
{
    struct sbuf *b = ctx;
    if (b->left > 1) {
        *b->p++ = c;
        b->left--;
    }
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    struct sbuf b = { buf, size };
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(sbuf_putc, &b, fmt, ap);
    va_end(ap);
    if (size)
        *b.p = '\0';
    return n;
}

static void (*log_sink)(char c);
static void (*log_tap)(char c);

void kprintf_set_sink(void (*sink)(char c))
{
    log_sink = sink;
}

void kprintf_set_tap(void (*tap)(char c))
{
    log_tap = tap;
}

/* Everything printed since boot, without the colour escapes, for the
 * monitor's 'o' (the first 64 KiB). */
#define BOOTLOG_SIZE (64 * 1024)
static char bootlog[BOOTLOG_SIZE];
static unsigned bootlog_len;
static int bootlog_esc;

static void bootlog_putc(char c)
{
    if (bootlog_esc) {                          /* ESC [ ... letter */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            bootlog_esc = 0;
        return;
    }
    if (c == 0x1B) {
        bootlog_esc = 1;
        return;
    }
    if (c == '\r' || bootlog_len + 1 >= BOOTLOG_SIZE)
        return;
    bootlog[bootlog_len++] = c;
    bootlog[bootlog_len] = 0;
}

const char *klog_text(void)
{
    return bootlog;
}

void klog_putc(char c)
{
    bootlog_putc(c);
    if (c == '\n')
        uart_putc('\r');
    uart_putc(c);
    if (log_sink)
        log_sink(c);
    if (log_tap)
        log_tap(c);
}

static void log_putc(char c, void *ctx)
{
    (void)ctx;
    klog_putc(c);
}

int kvlog(const char *fmt, va_list ap)
{
    return kvprintf(log_putc, 0, fmt, ap);
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvlog(fmt, ap);
    va_end(ap);
    return n;
}
