/*
 * The kernel log (src/lib/printf.c, klog_text): 64 KB in all. When it is full the oldest lines after the
 * boot go and the end stays: a report of a long run shows how the run ended (26 Oct 2026, the Pi froze after
 * 3 hours and the log had stopped in the first minutes).
 */
#include "lib/printf.h"

#include <stdio.h>
#include <string.h>

void uart_putc(char c) { (void)c; }

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

int main(void)
{
    kprintf("boot line one\n");
    for (int i = 0; i < 100; i++)
        kprintf("early %03d\n", i);
    const char *t = klog_text();
    check(strstr(t, "boot line one") != NULL, "the first line is there");
    check(strstr(t, "older lines left out") == NULL, "nothing is cut while the log has room");

    for (int i = 0; i < 6000; i++)                  /* about 150 KB: it fills more than twice */
        kprintf("line %05d of the run\n", i);
    t = klog_text();
    size_t n = strlen(t);
    check(n < 64 * 1024, "the log stays under 64 KB");
    check(strstr(t, "boot line one") != NULL, "the head stays: the boot line is still there");
    check(strstr(t, "early 099") != NULL, "the head holds the early lines");
    const char *cut = strstr(t, "--- (older lines left out) ---\n");
    check(cut != NULL, "a marker says that lines were left out");
    check(cut && strstr(cut + 1, "--- (older lines left out) ---") == NULL, "only one marker");
    check(strstr(t, "line 05999 of the run\n") != NULL, "the last line is there");
    check(strstr(t, "line 01000 of the run\n") == NULL, "the lines after the head, the oldest, are gone");
    check(n > 40 * 1024, "most of the log is kept (not emptied)");
    /* the line after the marker is whole */
    check(cut && strncmp(cut + 31, "line ", 5) == 0, "the lines after the marker start whole");
    printf("klog: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
