#include "notice.h"
#include "update.h"
#include "net/netxfer.h"
#include "drivers/timer.h"
#include "lib/printf.h"

static char flash_title[NOTICE_LEN], flash_detail[NOTICE_LEN];
static int flash_progress;
static uint32_t flash_until;            /* timer_ticks(); 0: none */

void notice_flash(const char *title, const char *detail, int progress, uint32_t ms)
{
    ksnprintf(flash_title, sizeof flash_title, "%s", title);
    ksnprintf(flash_detail, sizeof flash_detail, "%s", detail ? detail : "");
    flash_progress = progress;
    flash_until = (timer_ticks() + ms * 1000u) | 1;
}

void notice_fail(const char *reason)
{
    notice_flash("Not done", reason, -1, 4000);
}

int notice_now(char *title, char *detail, int *progress)
{
    uint32_t got = 0, size = 0;
    int secs = 0;
    if (netxfer_kernel_state(&got, &size, &secs) == NETXFER_K_RESTART) {
        if (secs)
            ksnprintf(title, NOTICE_LEN, "Restarting in %d", secs);
        else
            ksnprintf(title, NOTICE_LEN, "Restarting...");
        ksnprintf(detail, NOTICE_LEN, "The new kernel is on the SD card");
        *progress = -1;
        return 1;
    }
    /* an update from GitHub being installed (update.c), then its restart */
    if (update_notice(title, detail, progress))
        return 1;
    if (flash_until && (int32_t)(flash_until - timer_ticks()) > 0) {
        ksnprintf(title, NOTICE_LEN, "%s", flash_title);
        ksnprintf(detail, NOTICE_LEN, "%s", flash_detail);
        *progress = flash_progress;
        return 1;
    }
    flash_until = 0;
    return 0;
}
