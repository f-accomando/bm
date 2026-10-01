/*
 * The RGB30's Bluetooth controller, the Realtek RTL8821CS on UART1, brought
 * up as Linux does it (hci_h5.c + btrtl.c): power pins, H5 link at
 * 115200 8E1, version and ROM version, then the speed from the config file
 * (vendor command 0xFC17, 1.5 Mbaud with RTS/CTS), the firmware patch from
 * bm/rtl8821cs_fw.bin + bm/rtl8821cs_config.bin on the SD card, and an HCI
 * reset. After this bt.c carries on as on the Pi.
 */
#ifdef PLAT_RK3566
#include "rk_bt.h"
#include "rk_gpio.h"
#include "rk_pmic.h"
#include "bt/btuart.h"
#include "bt/h5.h"
#include "bt/hci.h"
#include "bt/rtlbt.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/printf.h"

#include <stdlib.h>

#define BT_ENABLE   rk_pin(4, 'A', 3)
#define BT_WAKE     rk_pin(4, 'A', 4)
#define WL_RESET    rk_pin(4, 'A', 2)       /* WiFi side out of reset (as Linux leaves it) */
#define VCC_WIFI    rk_pin(0, 'A', 0)       /* the module's 3.3 V switch */

static void power_cycle(void)
{
    rk_gpio_output(BT_ENABLE, 0);
    rk_gpio_output(BT_WAKE, 0);
    timer_delay_ms(100);
    rk_gpio_set(BT_ENABLE, 1);
    rk_gpio_set(BT_WAKE, 1);
    timer_delay_ms(500);
}

static int load(const char *name, uint8_t **data, size_t *len)
{
    fat_entry_t e;
    *data = NULL;
    if (config_find_file(name, &e) != 0 || fat_load(&e, data, len) != 0)
        return -1;
    return 0;
}

int rk_bt_bringup(uint32_t *baud_out)
{
    static int module_on;
    if (!module_on) {
        rk_gpio_output(VCC_WIFI, 1);
        rk817_clk32k_wifi(1);
        rk_gpio_output(WL_RESET, 1);
        timer_delay_ms(200);
        module_on = 1;
    }
    hci_set_transport(&h5_transport);
    int r = -1;
    for (int attempt = 0; attempt < 2 && r != 0; attempt++) {
        kprintf("bt: powering the RTL8821CS (GPIO4_A3)%s\n", attempt ? ", second try" : "");
        power_cycle();
        btuart_init(115200);
        r = h5_open(3000);
    }
    if (r != 0) {
        kprintf("\x1b[91mbt: no H5 link (%s)\x1b[0m\n",
                r == -2 ? "SYNC answered, CONFIG not" : "no answer at 115200 8E1");
        return -1;
    }
    uint8_t v[8] = { 0 };
    if (hci_cmd(0x1001, NULL, 0, v, sizeof v, 1000000) != 0) {
        kprintf("\x1b[91mbt: no answer to Read Local Version\x1b[0m\n");
        return -1;
    }
    unsigned lmp = v[6] | v[7] << 8, rev = v[1] | v[2] << 8;
    kprintf("bt: H5 link up (config %02x), HCI %u rev %04x LMP %04x\n",
            h5_peer_config() & 0xff, v[0], rev, lmp);
    if (lmp != 0x8821)
        kprintf("bt: not the expected RTL8821CS (LMP 8821): trying anyway\n");

    uint8_t rom[1] = { 0 };
    int st = hci_cmd(0xfc6d, NULL, 0, rom, 1, 1000000);
    uint8_t *fw = NULL, *cfg = NULL, *img = NULL;
    size_t fwlen = 0, cfglen = 0, imglen = 0;
    if (st != 0) {
        kprintf("bt: no ROM version (%d): running the ROM firmware\n", st);
    } else if (load("rtl8821cs_fw.bin", &fw, &fwlen) || load("rtl8821cs_config.bin", &cfg, &cfglen)) {
        kprintf("bt: bm/rtl8821cs_fw.bin or rtl8821cs_config.bin not on the SD card\n"
                "    (make TARGET=rgb30 firmware image): running the ROM firmware\n");
    } else {
        uint32_t word = 0, baud = 115200;
        int flow = 0;
        const char *err = "";
        if (rtlbt_uart_config(cfg, cfglen, &word, &baud, &flow) == 0 && baud != 115200) {
            uint8_t w[4] = { (uint8_t)word, (uint8_t)(word >> 8), (uint8_t)(word >> 16), (uint8_t)(word >> 24) };
            if (hci_cmd(0xfc17, w, 4, NULL, 0, 1000000) == 0) {
                timer_delay_ms(15);
                btuart_set_baud(baud);
                btuart_set_flow(flow);
                *baud_out = baud;
            } else {
                kprintf("bt: the chip refused %lu baud\n", baud);
            }
        }
        if (rtlbt_patch(fw, fwlen, cfg, cfglen, rom[0], &img, &imglen, &err) != 0) {
            kprintf("\x1b[91mbt: firmware: %s (ROM version %u)\x1b[0m\n", err, rom[0]);
        } else {
            unsigned frags = 0;
            int d = rtlbt_download(img, imglen, &frags);
            if (d)
                kprintf("\x1b[91mbt: firmware download stopped at piece %u (%d)\x1b[0m\n", frags, d);
            else
                kprintf("bt: firmware patch loaded (ROM %u, %u bytes in %u pieces)\n",
                        rom[0], (unsigned)imglen, frags);
        }
    }
    free(fw);
    free(cfg);
    free(img);
    if (hci_cmd(0x1001, NULL, 0, v, sizeof v, 1000000) == 0)
        kprintf("bt: firmware version %04x%04x\n", v[1] | v[2] << 8, v[6] | v[7] << 8);
    timer_delay_ms(15);
    hci_flush();
    if (hci_cmd(0x0c03, NULL, 0, NULL, 0, 2000000) != 0) {
        kprintf("\x1b[91mbt: no answer to HCI reset\x1b[0m\n");
        return -1;
    }
    unsigned re, crc, bad;
    h5_stats(&re, &crc, &bad);
    if (re || crc || bad || btuart_overruns())
        kprintf("bt: H5 %u retransmissions, %u CRC errors, %u bad frames, %u bytes lost\n",
                re, crc, bad, btuart_overruns());
    return 0;
}
#endif
