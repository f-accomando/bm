#include "bt.h"
#include "btuart.h"
#include "hci.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define BT_ON_GPIO   45             /* BT_REG_ON of the BCM43438 on the Zero W */
#define LPO_GPIO     43             /* GPCLK2: 32.768 kHz sleep clock */

#define CM_GP2CTL    (PERIPHERAL_BASE + 0x101080)
#define CM_GP2DIV    (PERIPHERAL_BASE + 0x101084)
#define CM_PASSWD    0x5A000000u
#define CM_ENAB      (1u << 4)
#define CM_BUSY      (1u << 7)

static int started;

/* The chip needs a 32 kHz clock; the firmware usually provides it. */
static const char *lpo_clock(void)
{
    gpio_set_function(LPO_GPIO, GPIO_ALT0);
    if (mmio_read(CM_GP2CTL) & CM_ENAB)
        return "already on";
    mmio_write(CM_GP2CTL, CM_PASSWD | (mmio_read(CM_GP2CTL) & ~CM_ENAB & 0xFFFFFFu));
    uint32_t t0 = timer_ticks();
    while ((mmio_read(CM_GP2CTL) & CM_BUSY) && timer_ticks() - t0 < 10000)
        ;
    /* 19.2 MHz oscillator / 585.9375 = 32768 Hz (MASH 1) */
    mmio_write(CM_GP2DIV, CM_PASSWD | 585u << 12 | 3840u);
    mmio_write(CM_GP2CTL, CM_PASSWD | 1u << 9 | 1u);
    mmio_write(CM_GP2CTL, CM_PASSWD | 1u << 9 | 1u | CM_ENAB);
    return "started";
}

static void power_cycle(void)
{
    gpio_set_function(BT_ON_GPIO, GPIO_OUTPUT);
    gpio_write(BT_ON_GPIO, 0);
    timer_delay_ms(20);
    gpio_write(BT_ON_GPIO, 1);
    timer_delay_ms(250);
}

static int reset(void)
{
    btuart_drain();
    return hci_cmd(HCI_RESET, NULL, 0, NULL, 0, 1000000);
}

/* The .hcd file is a list of HCI commands: opcode (2), length (1), data. */
static int load_patch(void)
{
    static const char *const paths[] = { "/bm33/BCM43430A1.hcd", "/BCM43430A1.hcd" };
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    for (unsigned i = 0; i < 2 && !data; i++)
        if (fat_find(paths[i], &e) == 0)
            fat_load(&e, &data, &len);
    if (!data) {
        kprintf("bt: BCM43430A1.hcd not on the SD card (make firmware; make sdcard puts it\n"
                "    in bm33/): the chip runs its ROM firmware\n");
        return -1;
    }
    if (hci_cmd(HCI_BCM_DOWNLOAD_MINI, NULL, 0, NULL, 0, 1000000) != 0) {
        kprintf("bt: the chip refused the firmware download\n");
        free(data);
        return -1;
    }
    timer_delay_ms(50);
    unsigned records = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint16_t op = (uint16_t)(data[i] | data[i + 1] << 8);
        uint8_t n = data[i + 2];
        if (i + 3 + n > len)
            break;
        if (hci_cmd(op, data + i + 3, n, NULL, 0, 1000000) != 0) {
            kprintf("bt: firmware record %u (opcode %04x) failed\n", records, op);
            free(data);
            return -1;
        }
        records++;
        i += 3u + n;
    }
    free(data);
    timer_delay_ms(250);                        /* the chip restarts, at 115200 */
    btuart_set_baud(115200);
    kprintf("bt: firmware patch loaded (%u records, %u bytes)\n", records, (unsigned)len);
    return 0;
}

int bt_start(void)
{
    if (started)
        return 0;
    kprintf("bt: the serial console moves to the mini UART (same pins, same speed)\n");
    uart_use_mini();
    kprintf("bt: 32 kHz clock %s\n", lpo_clock());
    btuart_init(115200);

    int r = reset();
    if (r != 0) {
        kprintf("bt: no answer to HCI reset, power-cycling the chip (GPIO%d)\n", BT_ON_GPIO);
        power_cycle();
        btuart_init(115200);
        r = reset();
    }
    if (r != 0) {
        kprintf("\x1b[91mbt: the chip does not answer (%d)\x1b[0m\n", r);
        return -1;
    }
    if (load_patch() == 0 && reset() != 0) {
        kprintf("\x1b[91mbt: no answer after the firmware patch\x1b[0m\n");
        return -1;
    }

    uint8_t v[8] = { 0 }, a[6] = { 0 };
    hci_cmd(HCI_READ_LOCAL_VERSION, NULL, 0, v, sizeof v, 500000);
    hci_cmd(HCI_READ_BD_ADDR, NULL, 0, a, sizeof a, 500000);
    kprintf("bt: ready, address %02x:%02x:%02x:%02x:%02x:%02x, HCI %u, LMP subversion %04x\n",
            a[5], a[4], a[3], a[2], a[1], a[0], v[0], v[6] | v[7] << 8);
    started = 1;
    return 0;
}

static const char *major_class(uint32_t cod)
{
    switch ((cod >> 8) & 0x1F) {
    case 1: return "computer";
    case 2: return "phone";
    case 4: return "audio";
    case 5: return (cod & 0xC0) == 0x40 ? "keyboard" : (cod & 0x0C) == 0x08 ? "gamepad" : "input";
    default: return "other";
    }
}

void bt_scan(unsigned seconds)
{
    if (!started && bt_start() != 0)
        return;
    uint8_t p[5] = { 0x33, 0x8B, 0x9E, (uint8_t)((seconds * 100 + 127) / 128), 0 };  /* GIAC */
    if (hci_cmd(HCI_INQUIRY, p, sizeof p, NULL, 0, 1000000) != 0) {
        kprintf("bt: inquiry refused\n");
        return;
    }
    kprintf("bt: looking for devices for %u s (DS4: hold Share + PS until the light flashes)\n",
            seconds);
    static uint8_t ev[260];
    uint8_t seen[16][6];
    int nseen = 0;
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < (seconds + 3) * 1000000u) {
        int n = hci_event(ev, sizeof ev, 500000);
        if (n < 0)
            continue;
        if (ev[0] == 0x01)                      /* Inquiry Complete */
            break;
        /* Inquiry Result (0x02), with RSSI (0x22), extended (0x2F) */
        if (ev[0] != 0x02 && ev[0] != 0x22 && ev[0] != 0x2F)
            continue;
        int count = ev[2], stride = ev[0] == 0x02 ? 14 : ev[0] == 0x22 ? 14 : 255;
        for (int i = 0; i < count && i < 8; i++) {
            const uint8_t *r = ev[0] == 0x2F ? ev + 3 : ev + 3 + i * stride;
            const uint8_t *addr = r;
            uint32_t cod = ev[0] == 0x02 ? (uint32_t)(r[9] | r[10] << 8 | r[11] << 16)
                                         : (uint32_t)(r[8] | r[9] << 8 | r[10] << 16);
            int dup = 0;
            for (int k = 0; k < nseen; k++)
                dup |= memcmp(seen[k], addr, 6) == 0;
            if (dup)
                continue;
            if (nseen < 16)
                memcpy(seen[nseen++], addr, 6);
            kprintf("bt: found %02x:%02x:%02x:%02x:%02x:%02x class %06lx (%s)\n",
                    addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], cod, major_class(cod));
        }
    }
    kprintf("bt: %d device%s found\n", nseen, nseen == 1 ? "" : "s");
}
