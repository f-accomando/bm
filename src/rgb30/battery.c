/*
 * The RGB30's battery (2026-10-10): one place for the bar of the menu, the
 * low-battery LED and the icon over the games (bm_set_battery).
 *
 * The bolt used to come up to 10 s after the cable went in (the user,
 * 2026-10-10): the whole battery was read every 10 s, and "charging" came
 * from the charger's state alone, which starts a moment after the plug.
 * Now the RK817's plug bit (SYS_STS, one I2C byte) is looked at 4 times a
 * second and decides the bolt; a change reads the rest at once and the
 * voltage again 2 s later (on and off the charger it jumps, then settles).
 * The voltage alone every 10 s, as before.
 */
#include "battery.h"
#include "plat.h"
#include "drivers/timer.h"
#include "kernel/ledstate.h"
#include "kernel/config.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include <string.h>
#include <strings.h>

#define FULL_US     10000000u       /* the voltage and the charger's state */
#define PLUG_US     250000u         /* the plug */
#define SETTLE_US   2000000u        /* after the plug came or went */
#define LOW_ON      20              /* the icon over the games from 20%... */
#define LOW_OFF     23              /* ...until 23% (the voltage sags under load) */

#define LED_REST_MV 3500            /* the LED: about 4% left, 40 minutes before the cliff */
#define DEAD_READS  3               /* empty this many reads (30 s) in a row: switch off */

static int known, pct, charging, low, plug = -2, dead;
static uint32_t at_full, at_plug, next_full = FULL_US;

static void read_all(void)
{
    int mv, charge, ma = 0;
    int64_t uah;
    known = plat_battery(&mv, &charge) == 0 && mv > 0;
    if (!known)
        return;
    const int gauge = plat_battery_gauge(&ma, &uah) == 0;
    if (!gauge)
        ma = 0;
    const int rest = battery_rest_mv(mv, ma);
    const int in = plat_power_in();
    plug = in;
    /* on the charger: the plug when the PMIC says it, its state if not */
    const int on = in >= 0 ? in : charge >= 1;
    ledstate_set(LED_POWER, rest < LED_REST_MV && !on);
    dead = gauge && !on && rest < BAT_EMPTY_MV ? dead + 1 : 0;
    pct = charge == 2 ? 100 : battery_curve_pct(rest);     /* full: four bars */
    charging = on && charge != 2;
    if (on)
        low = 0;
    else if (pct <= LOW_ON)
        low = 1;
    else if (pct >= LOW_OFF)
        low = 0;
}

/*
 * The battery log (read only, for the calibration in
 * docs/design/rgb30-battery-calibration.md): with battery_log=1 in
 * bm/config.txt, one line a minute appended to bm/battery.csv:
 * seconds, mV, mA (negative = discharge), the gauge's charge in
 * mAh, the plug, the charger's state. The file is written whole each time
 * (fat_write_file keeps the old one if the power goes), so a run-down leaves
 * everything up to the last minute. A write is skipped while another is under
 * way; after 3 failed writes the log stops.
 */
#define LOG_US      60000000u
#define LOG_MAX     32768

static char log_buf[LOG_MAX];
static const char log_name[] = "battery.csv";
static int log_len, log_on = -1, log_fail;
static uint32_t log_at;
static uint64_t log_sec;

static void log_start(void)
{
    const char *v = config_get("battery_log");
    log_on = v && *v == '1';
    if (!log_on)
        return;

    log_len = 0;
    fat_entry_t e;
    uint8_t *old_data = NULL;
    size_t old_len = 0;
    if (fat_find("/bm/battery.csv", &e) == 0 && !e.is_dir && e.size > 0) {
        if (fat_load(&e, &old_data, &old_len) == 0 && old_data) {
            if (old_len < sizeof log_buf - 1024) {
                memcpy(log_buf, old_data, old_len);
                log_len = (int)old_len;
            } else {
                const char *hdr = "sec,mV,mA,mAh,plug,charge\n";
                size_t hlen = strlen(hdr);
                memcpy(log_buf, hdr, hlen);
                size_t keep = sizeof log_buf - 1024 - hlen;
                const char *start = (const char *)old_data + old_len - keep;
                const char *nl = memchr(start, '\n', keep);
                if (nl)
                    start = nl + 1;
                size_t tail_len = (size_t)((const char *)old_data + old_len - start);
                memcpy(log_buf + hlen, start, tail_len);
                log_len = (int)(hlen + tail_len);
            }
            free(old_data);
        }
    }
    if (log_len == 0)
        log_len = ksnprintf(log_buf, sizeof log_buf, "sec,mV,mA,mAh,plug,charge\n");
}

static void log_step(uint32_t now)
{
    if (log_on == 0 || log_fail >= 3)
        return;
    if (!log_at) {
        log_at = now | 1;
        return;
    }
    if ((int32_t)(now - log_at) < (int32_t)LOG_US)      /* signed: log_at is now|1, a hair ahead of now */
        return;
    if (log_on < 0)
        log_start();
    if (log_on <= 0)
        return;
    if (fat_writing() || log_len + 64 > LOG_MAX)
        return;
    log_sec += (now - log_at) / 1000000u;
    log_at = now | 1;
    int mv, charge, ma;
    int64_t uah;
    if (plat_battery(&mv, &charge) != 0 || plat_battery_gauge(&ma, &uah) != 0)
        return;
    log_len += ksnprintf(log_buf + log_len, sizeof log_buf - (size_t)log_len, "%lu,%d,%d,%ld,%d,%d\n",
                         (unsigned long)log_sec, mv, ma, (long)(uah / 1000), plat_power_in(), charge);
    if (fat_mkdirs("/bm") != 0 || fat_write_file("/bm", log_name, log_buf, (size_t)log_len) != 0)
        log_fail++;
}

int battery_state(int *p, int *c, int *l)
{
    const uint32_t now = timer_ticks();
    log_step(now);
    if (!at_full || now - at_full >= next_full) {
        read_all();
        at_full = at_plug = now | 1;
        next_full = FULL_US;
    } else if (known && now - at_plug >= PLUG_US) {
        at_plug = now;
        const int in = plat_power_in();
        if (in >= 0 && in != plug) {
            read_all();                     /* the cable: the bolt now */
            at_full = now | 1;
            next_full = SETTLE_US;          /* and the voltage once it settles */
        }
    }
    /* empty (3.3 V under 700 mA): switch off while the card is quiet, not at
     * 2.9 V in the middle of a write (the run-down of 2026-10-27 went on 36
     * minutes after the LED, down to 2.88 V) */
    if (dead >= DEAD_READS && !fat_writing()) {
        kprintf("battery empty: power off\n");
        plat_poweroff();
    }
    if (p) *p = pct;
    if (c) *c = charging;
    if (l) *l = low;
    return known;
}
