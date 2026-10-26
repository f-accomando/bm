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

#define FULL_US     10000000u       /* the voltage and the charger's state */
#define PLUG_US     250000u         /* the plug */
#define SETTLE_US   2000000u        /* after the plug came or went */
#define LOW_ON      20              /* the icon over the games from 20%... */
#define LOW_OFF     23              /* ...until 23% (the voltage sags under load) */

static int known, pct, charging, low, plug = -2;
static uint32_t at_full, at_plug, next_full = FULL_US;

static void read_all(void)
{
    int mv, charge;
    known = plat_battery(&mv, &charge) == 0 && mv > 0;
    if (!known)
        return;
    const int in = plat_power_in();
    plug = in;
    /* on the charger: the plug when the PMIC says it, its state if not */
    const int on = in >= 0 ? in : charge >= 1;
    ledstate_set(LED_POWER, mv < 3450 && !on);
    pct = charge == 2 ? 100 : battery_percent(mv);         /* full: four bars */
    charging = on && charge != 2;
    if (on)
        low = 0;
    else if (pct <= LOW_ON)
        low = 1;
    else if (pct >= LOW_OFF)
        low = 0;
}

/*
 * The battery log (2026-10-26, read only, for the calibration in
 * docs/design/rgb30-battery-calibration.md): with battery_log=1 in
 * bm/config.txt, one line a minute in bm/bat00.csv (the first free name up
 * to bat99): seconds, mV, mA (negative = discharge), the gauge's charge in
 * mAh, the plug, the charger's state. The file is written whole each time
 * (fat_write_file keeps the old one if the power goes), so a run-down leaves
 * everything up to the last minute. A write is skipped while another is under
 * way; after 3 failed writes the log stops.
 */
#define LOG_US      60000000u
#define LOG_MAX     32768

static char log_buf[LOG_MAX];
static char log_name[] = "bat00.csv";
static int log_len, log_on = -1, log_fail;
static uint32_t log_at;
static uint64_t log_sec;

static void log_start(void)
{
    const char *v = config_get("battery_log");
    log_on = v && *v == '1';
    if (!log_on)
        return;
    for (int i = 0; i < 100; i++) {
        log_name[3] = (char)('0' + i / 10);
        log_name[4] = (char)('0' + i % 10);
        char path[24];
        fat_entry_t e;
        ksnprintf(path, sizeof path, "/bm/%s", log_name);
        if (fat_find(path, &e) != 0)
            break;
    }
    log_len = ksnprintf(log_buf, sizeof log_buf, "sec,mV,mA,mAh,plug,charge\n");
}

static void log_step(uint32_t now)
{
    if (log_on < 0)
        log_start();
    if (log_on <= 0 || log_fail >= 3)
        return;
    if (!log_at)
        log_at = now | 1;
    if (now - log_at < LOG_US)
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
    if (p) *p = pct;
    if (c) *c = charging;
    if (l) *l = low;
    return known;
}
