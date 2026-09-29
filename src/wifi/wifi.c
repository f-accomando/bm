#include "wifi.h"
#include "sdio.h"
#include "bt/bt.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

/* Function 1 registers of the Broadcom SDIO core (brcmfmac sdio.h) */
#define SB_ADDR_LOW     0x1000A         /* backplane window, bits 15..8 */
#define SB_ADDR_MID     0x1000B         /* bits 23..16 */
#define SB_ADDR_HIGH    0x1000C         /* bits 31..24 */
#define CHIP_CLK_CSR    0x1000E
#define CLK_FORCE_ALP   0x01
#define CLK_ALP_REQ     0x08
#define CLK_HW_OFF      0x20
#define CLK_ALP_AVAIL   0x40

#define CHIPCOMMON      0x18000000u     /* chip id register at offset 0 */
#define SB_WINDOW       0x8000u

static void say(const char *step, int ok, uint32_t value)
{
    char v[16] = "";
    if (value)
        ksnprintf(v, sizeof v, " %08lx", value);
    if (ok)
        kprintf("wifi: %s%s\n", step, v);
    else
        kprintf("\x1b[91mwifi: %s%s\x1b[0m\n", step, v);
}

/* Window of the backplane seen through function 1 at 0x8000..0xFFFF. */
static int set_window(uint32_t addr)
{
    return sdio_write8(1, SB_ADDR_LOW, (uint8_t)(addr >> 8 & 0x80)) ||
           sdio_write8(1, SB_ADDR_MID, (uint8_t)(addr >> 16)) ||
           sdio_write8(1, SB_ADDR_HIGH, (uint8_t)(addr >> 24));
}

static int bp_read32(uint32_t addr, uint32_t *v)
{
    if (set_window(addr))
        return -1;
    uint8_t b[4];
    for (int i = 0; i < 4; i++)
        if (sdio_read8(1, (addr & (SB_WINDOW - 1)) + (uint32_t)i, &b[i]))
            return -1;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

int wifi_probe(void)
{
    if (strcmp(sd_controller(), "emmc") == 0) {
        kprintf("\x1b[91mwifi: the SD card is on the Arasan controller (SDHOST failed): "
                "no controller left for WiFi\x1b[0m\n");
        return -1;
    }
    kprintf("wifi: 32 kHz clock %s\n", bcm43438_lpo_clock());
    if (sdio_init(say) != 0) {
        kprintf("\x1b[91mwifi: stopped (%s)\x1b[0m\n", sdio_error());
        return -1;
    }
    /* function 1 (backplane) on */
    if (sdio_write8(0, 0x02, 0x02)) {
        say("enable function 1 failed", 0, 0);
        return -1;
    }
    uint8_t rdy = 0;
    uint32_t t0 = timer_ticks();
    while (sdio_read8(0, 0x03, &rdy) == 0 && !(rdy & 0x02))
        if (timer_ticks() - t0 > 500000)
            break;
    if (!(rdy & 0x02)) {
        say("function 1 not ready", 0, rdy);
        return -1;
    }
    say("function 1 ready", 1, 0);

    /* ALP clock of the backplane */
    uint8_t csr = 0;
    sdio_write8(1, CHIP_CLK_CSR, CLK_HW_OFF | CLK_ALP_REQ);
    t0 = timer_ticks();
    while (sdio_read8(1, CHIP_CLK_CSR, &csr) == 0 && !(csr & CLK_ALP_AVAIL))
        if (timer_ticks() - t0 > 500000)
            break;
    if (!(csr & CLK_ALP_AVAIL)) {
        say("ALP clock not available, CSR", 0, csr);
        return -1;
    }
    sdio_write8(1, CHIP_CLK_CSR, CLK_HW_OFF | CLK_FORCE_ALP);
    say("backplane clock (ALP) on", 1, 0);

    uint32_t id = 0;
    if (bp_read32(CHIPCOMMON, &id)) {
        say("chip id read failed", 0, 0);
        return -1;
    }
    uint32_t chip = id & 0xFFFF, rev = id >> 16 & 0xF;
    kprintf("wifi: chip %lu (%04lx) rev %lu%s\n", chip, chip, rev,
            chip == 43430 ? ": BCM43430/43438, ok" : "");
    return chip == 43430 ? 0 : -1;
}
