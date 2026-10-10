/*
 * The saved WiFi network joined by itself (2026-10-10).
 *
 * Before, the boot tried once and nothing tried again: a scan that missed
 * the network, a join that met the access point still holding the
 * console's old association (a restart after an update or a kernel from
 * the network said no goodbye), a chip that did not start, or a link lost
 * later left the console offline until Settings > Connect.
 *
 * Now the boot tries twice (the chip powered again if it did not start),
 * and the menu tries again in a fiber while the link is down: after 5 s,
 * 15 s, 30 s, 1 min, then every 2 min; the scan's and the join's waits
 * give the CPU back (fiber_slice in the drivers), so the menu goes on. A
 * chip that does not start three times running is left alone (no chip, no
 * firmware on the card) until a join by hand works. Pi and RGB30 alike.
 */
#include "wifi_auto.h"
#include "net.h"
#include "wifi/wifi.h"
#include "kernel/config.h"
#include "kernel/fiber.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#if defined(BM_RGB30) || defined(BM_HOST_TEST)
static int wireless(void) { return 1; }
#else
#include "drivers/board.h"
static int wireless(void) { return board()->wireless; }
#endif

#define BOOT_TRIES      2
#define MAX_START_FAILS 3
#define JOB_STACK       (64 * 1024)

static fiber_job_t job;
static int fails;               /* tries in a row that did not join */
static int start_fails;         /* wifi_start in a row that failed */
static int due;                 /* a try is set for `at` */
static uint32_t at;

uint32_t wifi_auto_delay(int n)
{
    static const uint16_t secs[] = { 5, 15, 30, 60, 120 };
    const int last = (int)(sizeof secs / sizeof *secs) - 1;
    return secs[n < 0 ? 0 : n > last ? last : n];
}

/* the saved network, wifi_boot not 0, a chip that may still start */
static int enabled(void)
{
    const char *ssid = config_get("wifi_ssid"), *on = config_get("wifi_boot");
    return wireless() && ssid && ssid[0] && !(on && strcmp(on, "0") == 0) &&
           start_fails < MAX_START_FAILS;
}

int wifi_auto_try(void)
{
    if (!wifi_up()) {
        if (wifi_start() != 0) {
            if (++start_fails >= MAX_START_FAILS)
                kprintf("\x1b[91mwifi: the chip did not start %d times: no more tries "
                        "(Settings > Connect tries again)\x1b[0m\n", start_fails);
            return -1;
        }
        start_fails = 0;
    }
    if (fiber_cancelled() || wifi_connect_saved() != 0)
        return -1;
    return net_start(&net_wifi);
}

int wifi_auto_boot(void)
{
    if (!enabled())
        return -1;
    kprintf("wifi: joining \"%s\" (wifi_boot=0 in bm/config.txt: off)\n", config_get("wifi_ssid"));
    for (int t = 1; t <= BOOT_TRIES; t++) {
        if (wifi_auto_try() == 0) {
            fails = 0;
            return 0;
        }
        if (start_fails >= MAX_START_FAILS)
            break;
        if (t < BOOT_TRIES) {
            kprintf("wifi: trying again (%d of %d)\n", t + 1, BOOT_TRIES);
            timer_delay_ms(1000);
        }
    }
    kprintf("wifi: not joined; the menu tries again by itself\n");
    return -1;
}

static void job_main(void *arg)
{
    (void)arg;
    kprintf("wifi: joining \"%s\" again (try %d)\n", config_get("wifi_ssid"), fails + 1);
    const int r = wifi_auto_try();
    if (fiber_cancelled())
        return;                         /* stopped, not failed: again later */
    fails = r == 0 ? 0 : fails + 1;
}

void wifi_auto_idle(uint32_t until)
{
    if (job.busy) {
        fiber_job_run(&job, until);
        return;
    }
    if (wifi_linked()) {
        fails = start_fails = due = 0;
        return;
    }
    if (!enabled() || net_link_kind() == NET_LINK_ETHERNET)
        return;
    const uint32_t now = timer_ticks();
    if (!due) {
        due = 1;
        at = now + wifi_auto_delay(fails) * 1000000u;
        return;
    }
    if ((int32_t)(now - at) < 0)
        return;
    due = 0;
    if (fiber_job_start(&job, JOB_STACK, job_main, NULL) == 0)
        fiber_job_run(&job, until);
}

void wifi_auto_stop(void)
{
    fiber_job_stop(&job);
}
