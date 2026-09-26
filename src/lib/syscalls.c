/*
 * newlib system call layer for the bm33 kernel.
 *   stdout/stderr -> kernel log (UART + screen console)
 *   stdin         -> UART (blocking)
 *   heap          -> from the end of the kernel image up to heap_init()'s limit
 * No filesystem yet: open() and friends fail with ENOSYS/EBADF.
 */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>

#include "heap.h"
#include "printf.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "kernel/exceptions.h"

#undef errno
extern int errno;

extern char __kernel_end[];

static char *heap_ptr;
static char *heap_limit;

void heap_init(uintptr_t limit)
{
    heap_ptr = (char *)(((uintptr_t)__kernel_end + 7) & ~(uintptr_t)7);
    heap_limit = (char *)limit;
}

uintptr_t heap_start(void)
{
    return ((uintptr_t)__kernel_end + 7) & ~(uintptr_t)7;
}

uintptr_t heap_end(void)
{
    return (uintptr_t)heap_limit;
}

uintptr_t heap_brk(void)
{
    return (uintptr_t)heap_ptr;
}

void *_sbrk(ptrdiff_t incr)
{
    if (!heap_ptr)
        panic("malloc before heap_init");
    if (incr > heap_limit - heap_ptr) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *prev = heap_ptr;
    heap_ptr += incr;
    return prev;
}

int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    for (int i = 0; i < len; i++)
        klog_putc(buf[i]);
    return len;
}

int _read(int fd, char *buf, int len)
{
    if (fd != 0) {
        errno = EBADF;
        return -1;
    }
    if (len <= 0)
        return 0;
    buf[0] = uart_getc();
    return 1;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _open(const char *path, int flags, int mode)
{
    (void)path; (void)flags; (void)mode;
    errno = ENOSYS;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if (fd < 0 || fd > 2) {
        errno = EBADF;
        return -1;
    }
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    return fd >= 0 && fd <= 2;
}

int _lseek(int fd, int off, int whence)
{
    (void)fd; (void)off; (void)whence;
    errno = ESPIPE;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    panic("signal %d raised (abort?)", sig);
}

void _exit(int status)
{
    panic("exit(%d) called", status);
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    uint32_t us = timer_ticks();
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
    return 0;
}

clock_t _times(struct tms *t)
{
    clock_t ticks = (clock_t)(timer_ticks() / (1000000 / CLOCKS_PER_SEC));
    t->tms_utime = ticks;
    t->tms_stime = t->tms_cutime = t->tms_cstime = 0;
    return ticks;
}

int _unlink(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return -1;
}

int _link(const char *a, const char *b)
{
    (void)a; (void)b;
    errno = ENOSYS;
    return -1;
}
