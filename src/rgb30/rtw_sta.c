/*
 * wifi/wifi.h on the RGB30: the RTL8821CS (rtw_*.c) behind the same calls
 * the Pi's Broadcom driver offers. Step 1 of M31's WiFi: the module powered,
 * the SDIO card set up, the chip on, its firmware running, the MAC address
 * read from the efuse. Scanning and joining come next (they need the MAC,
 * BB and RF tables and the 802.11 station logic).
 */
#ifdef PLAT_RK3566
#include "wifi/wifi.h"
#include "rtw.h"
#include "rk_sdio.h"
#include "rk_wlbt.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/printf.h"

#include <stdarg.h>
#include <stdlib.h>

static int started;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kprintf("wifi: ");
    kvlog(fmt, ap);
    kprintf("\n");
    va_end(ap);
}

static void fail(const char *what)
{
    kprintf("\x1b[91mwifi: %s\x1b[0m\n", what);
}

int wifi_probe(void)
{
    say("powering the RTL8821CS (GPIO0_A0, 32 kHz from the RK817, WL_REG_ON GPIO4_A2)");
    wlbt_wifi_reset();
    if (sdio_init() != 0) {
        char m[120];
        ksnprintf(m, sizeof m, "SDIO (sdmmc2): %s", sdio_error());
        fail(m);
        return -1;
    }
    say("SDIO card ready (sdmmc2, 4-bit, function 1, 512-byte blocks)");
    return 0;
}

int wifi_start(void)
{
    if (started)
        return 0;
    if (wifi_probe() != 0)
        return -1;
    fat_entry_t e;
    uint8_t *fw = NULL;
    size_t len = 0;
    if (config_find_file("rtw8821c_fw.bin", &e) != 0 || fat_load(&e, &fw, &len) != 0) {
        fail("bm/rtw8821c_fw.bin not on the SD card (make TARGET=rgb30 firmware image)");
        return -1;
    }
    char err[120];
    int r = rtw_chip_start(fw, len, err, sizeof err);
    free(fw);
    if (r) {
        fail(err);
        if (rtw_io_errors())
            say("%d SDIO errors on the way", rtw_io_errors());
        return -1;
    }
    say("chip cut %u, firmware %u.%u.%u running, RFE %u, package %u", rtw.cut, rtw.fw_major,
        rtw.fw_minor, rtw.fw_patch, rtw.rfe, rtw.pkg);
    say("MAC address %02x:%02x:%02x:%02x:%02x:%02x", rtw.mac[0], rtw.mac[1], rtw.mac[2],
        rtw.mac[3], rtw.mac[4], rtw.mac[5]);
    if (rtw.rfe != 0 && rtw.rfe != 2 && rtw.rfe != 4 && rtw.rfe != 6)
        say("RFE type %u is not one rtw88 knows", rtw.rfe);
    started = 1;
    return 0;
}

int wifi_scan(void)
{
    if (!started)
        return -1;
    say("scanning is the next step of the RGB30 port (radio tables not in yet)");
    return 0;
}

int wifi_connect(void)
{
    say("joining a network is not in the RGB30 port yet");
    return -1;
}

int wifi_connect_saved(void)                { return -1; }
int wifi_linked(void)                       { return 0; }
const unsigned char *wifi_mac(void)         { return rtw.mac; }
void wifi_poll(void)                        { }
int wifi_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int wifi_send(const void *eth, int len)     { (void)eth; (void)len; return -1; }
#endif
