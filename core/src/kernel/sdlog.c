#include "kernel/sdlog.h"

#include "drivers/sd.h"
#include "fs/fat.h"
#include "kernel/crumbs.h"
#include "kernel/reports.h"
#include "lib/printf.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void sdlog_prev(void)
{
    if (!sd_blocks())
        return;
    fat_entry_t e;
    uint8_t *old = NULL;
    size_t n = 0;
    if (fat_find("/bm/bootlog.txt", &e) == 0 && fat_load(&e, &old, &n) != 0) {
        old = NULL;
        n = 0;
    }
    static char tail[4096 + 1];
    size_t tn = crumbs_ring_copy(tail, sizeof tail);
    char head[200];
    int hn = ksnprintf(head, sizeof head, "\n--- the run before %s; the last lines it printed (RAM) ---\n",
                       crumbs_prev_state());
    if (hn < 0 || hn >= (int)sizeof head)
        hn = (int)strlen(head);
    char *buf = malloc(n + (size_t)hn + tn + 64);
    if (!buf) {
        free(old);
        return;
    }
    size_t len = 0;
    if (old) {
        memcpy(buf, old, n);
        len = n;
    } else {
        len = (size_t)ksnprintf(buf, 64, "(no bm/bootlog.txt)\n");
    }
    free(old);
    memcpy(buf + len, head, (size_t)hn);
    len += (size_t)hn;
    if (tn) {
        memcpy(buf + len, tail, tn);
        len += tn;
    } else {
        len += (size_t)ksnprintf(buf + len, 32, "(nothing in RAM)\n");
    }
    reports_mask_secrets(buf, len);
    if (fat_mkdirs("/bm") != 0 || fat_write_file("/bm", "bootprev.txt", buf, len) != 0)
        kprintf("boot log: cannot write bm/bootprev.txt (%s)\n", fat_error());
    free(buf);
}

void sdlog_boot(int say)
{
    if (!sd_blocks())
        return;
    const char *log = klog_text();
    size_t n = strlen(log);
    char *copy = malloc(n + 1);
    if (!copy) {
        kprintf("boot log: no memory\n");
        return;
    }
    memcpy(copy, log, n + 1);
    reports_mask_secrets(copy, n);
    if (fat_mkdirs("/bm") == 0 && fat_write_file("/bm", "bootlog.txt", copy, n) == 0) {
        if (say)
            kprintf("boot log saved to bm/bootlog.txt\n");
    } else {
        kprintf("boot log: cannot write bm/bootlog.txt (%s)\n", fat_error());
    }
    free(copy);
}

void sdlog_lastrun(void)
{
    if (!sd_blocks())
        return;
    kprintf("restarting: this run's log in bm/lastrun.txt\n");
    const char *log = klog_text();
    size_t n = strlen(log);
    char *buf = malloc(n + 1);              /* the log keeps the last lines (printf.c bootlog_trim) */
    if (buf)
        memcpy(buf, log, n + 1);
    if (!buf) {
        kprintf("cannot write bm/lastrun.txt (no memory)\n");
        return;
    }
    reports_mask_secrets(buf, n);
    if (fat_mkdirs("/bm") != 0 || fat_write_file("/bm", "lastrun.txt", buf, n) != 0)
        kprintf("cannot write bm/lastrun.txt (%s)\n", fat_error());
    free(buf);
}
