/*
 * SD card: the SDHOST driver (sdhost.c) by default, the Arasan EMMC one
 * (sd_emmc.c) if SDHOST cannot bring the card up or later fails a transfer
 * twice. SDHOST is what Linux uses; it leaves the Arasan controller to the
 * WiFi chip.
 */
#include "sd.h"
#include "sd_backend.h"

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

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (use_emmc)
        return emmc_sd_read(lba, count, buf);
    if (sdhost_read(lba, count, buf) == 0)
        return 0;
    return fall_back() ? emmc_sd_read(lba, count, buf) : -1;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (use_emmc)
        return emmc_sd_write(lba, count, buf);
    if (sdhost_write(lba, count, buf) == 0)
        return 0;
    return fall_back() ? emmc_sd_write(lba, count, buf) : -1;
}

uint32_t sd_blocks(void) { return use_emmc ? emmc_sd_blocks() : sdhost_blocks(); }
int sd_is_hc(void) { return use_emmc ? emmc_sd_is_hc() : sdhost_is_hc(); }
const char *sd_error(void) { return use_emmc ? emmc_sd_error() : sdhost_error(); }
const char *sd_controller(void) { return use_emmc ? "emmc" : "sdhost"; }
