/*
 * picolibc glue for the AArch64 build (the Pi build uses newlib, see
 * src/lib/syscalls.c): stdin/stdout/stderr on the kernel log, the heap for
 * malloc (from the end of the kernel to heap_init's limit), clock and time,
 * and failing stubs for files (bm reads the SD through fs/fat.c).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

#include "lib/heap.h"
#include "lib/printf.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "kernel/exceptions.h"
#include "plat.h"

extern char __kernel_end[];

/* --- stdio --- */

static int k_putc(char c, FILE *f)
{
    (void)f;
    klog_putc(c);
    return (unsigned char)c;
}

static int k_getc(FILE *f)
{
    (void)f;
    return (unsigned char)uart_getc();
}

static FILE kstdio = FDEV_SETUP_STREAM(k_putc, k_getc, NULL, _FDEV_SETUP_RW);
FILE *const stdin = &kstdio;
FILE *const stdout = &kstdio;
FILE *const stderr = &kstdio;

/* --- heap --- */

static char *heap_ptr;
static char *heap_limit;

void heap_init(uintptr_t limit)
{
    heap_ptr = (char *)(((uintptr_t)__kernel_end + 15) & ~(uintptr_t)15);
    heap_limit = (char *)limit;
}

uintptr_t heap_start(void)
{
    return ((uintptr_t)__kernel_end + 15) & ~(uintptr_t)15;
}

uintptr_t heap_end(void)    { return (uintptr_t)heap_limit; }
uintptr_t heap_brk(void)    { return (uintptr_t)heap_ptr; }

void *sbrk(ptrdiff_t incr)
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

/* --- time --- */

extern unsigned long net_time(void) __attribute__((weak));
uint64_t timer_us64(void);

int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
    (void)tz;
    uint64_t us = timer_us64();
    unsigned long wall = net_time ? net_time() : 0;
    tv->tv_sec = wall ? (time_t)wall : (time_t)(us / 1000000);
    tv->tv_usec = (suseconds_t)(us % 1000000);
    return 0;
}

clock_t times(struct tms *t)
{
    clock_t ticks = (clock_t)(timer_us64() / (1000000 / CLOCKS_PER_SEC));
    t->tms_utime = ticks;
    t->tms_stime = t->tms_cutime = t->tms_cstime = 0;
    return ticks;
}

/* --- processes and files: not here --- */

void _exit(int status)
{
    panic("exit(%d) called", status);
}

int getpid(void)            { return 1; }

int kill(pid_t pid, int sig)
{
    (void)pid;
    panic("signal %d raised (abort?)", sig);
}

int open(const char *path, int flags, ...)
{
    (void)path; (void)flags;
    errno = ENOSYS;
    return -1;
}

int close(int fd)               { (void)fd; errno = EBADF; return -1; }
ssize_t read(int fd, void *b, size_t n)        { (void)fd; (void)b; (void)n; errno = EBADF; return -1; }
ssize_t write(int fd, const void *b, size_t n) { (void)fd; (void)b; (void)n; errno = EBADF; return -1; }
off_t lseek(int fd, off_t off, int whence)     { (void)fd; (void)off; (void)whence; errno = ESPIPE; return -1; }
int unlink(const char *path)    { (void)path; errno = ENOSYS; return -1; }
int fstat(int fd, struct stat *st) { (void)fd; (void)st; errno = EBADF; return -1; }
int isatty(int fd)              { return fd >= 0 && fd <= 2; }
int rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
