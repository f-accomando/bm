/*
 * SD card: the SDHOST driver (sdhost.c) by default, the Arasan EMMC one
 * (sd_emmc.c) if SDHOST cannot bring the card up or later fails a transfer
 * twice. SDHOST is what Linux uses; it leaves the Arasan controller to the
 * WiFi chip.
 */
#include "sd.h"
#include "sd_backend.h"
#include "lib/printf.h"

static int use_emmc;
static int inited;

static int init_emmc(void)
{
    use_emmc = 1;
    return emmc_sd_init();
}

int sd_init(void)
{
    inited = 1;
    use_emmc = 0;
    if (sdhost_init() == 0)
        return 0;
    return init_emmc();
}

/* A transfer SDHOST could not do (after its own retry): the card goes to
 * the Arasan controller for good, and the transfer is tried there. */
static int fall_back(void)
{
    return !use_emmc && inited && init_emmc() == 0;
}

/* SDHOST gave a transfer up. The card moves to the Arasan controller for
 * good, and the WiFi chip needs that controller. So SDHOST starts again first
 * (one try); the caller repeats the transfer when this returns 1. The line
 * says why it failed: the text of the error and the sites that moved (the
 * SD file log cannot be written when the SD fails, so it goes to the RAM log). */
static int restart_sdhost(const char *what)
{
    if (use_emmc || !inited)
        return 0;
    char sites[96];
    sdhost_stats_line(sites, sizeof sites);
    kprintf("sd: sdhost %s failed (%s) %s\n", what, sdhost_error(), sites);
    if (sdhost_init() != 0) {
        kprintf("sd: sdhost does not start again (%s): the Arasan controller\n", sdhost_error());
        return 0;
    }
    kprintf("sd: sdhost started again\n");
    return 1;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (use_emmc)
        return emmc_sd_read(lba, count, buf);
    if (sdhost_read(lba, count, buf) == 0)
        return 0;
    if (restart_sdhost("read") && sdhost_read(lba, count, buf) == 0)
        return 0;
    return fall_back() ? emmc_sd_read(lba, count, buf) : -1;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (use_emmc)
        return emmc_sd_write(lba, count, buf);
    if (sdhost_write(lba, count, buf) == 0)
        return 0;
    /* several blocks in one command (fat.c's runs, 2026-10-11): a failure
     * does not move the card to the Arasan controller (the WiFi's) by
     * itself; fat.c writes them again one at a time, which still may */
    if (count > 1)
        return -1;
    if (restart_sdhost("write") && sdhost_write(lba, count, buf) == 0)
        return 0;
    return fall_back() ? emmc_sd_write(lba, count, buf) : -1;
}

uint32_t sd_blocks(void) { return use_emmc ? emmc_sd_blocks() : sdhost_blocks(); }
int sd_is_hc(void) { return use_emmc ? emmc_sd_is_hc() : sdhost_is_hc(); }
const char *sd_error(void) { return use_emmc ? emmc_sd_error() : sdhost_error(); }
const char *sd_controller(void) { return use_emmc ? "emmc" : "sdhost"; }
void sd_counters(uint32_t *retries, uint32_t *timeouts) { sdhost_counters(retries, timeouts); }
int sd_stats_line(char *buf, size_t n) { return use_emmc ? 0 : sdhost_stats_line(buf, n); }
