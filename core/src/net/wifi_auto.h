/*
 * The saved WiFi network joined by itself (2026-10-10), on the Pi and the
 * RGB30: at boot up to two tries, then again from the menu while the link
 * is down. wifi_boot=0 in bm/config.txt turns it all off.
 */
#ifndef WIFI_AUTO_H
#define WIFI_AUTO_H

#include <stdint.h>

/* At boot: the radio started and the saved network joined (two tries; the
 * chip powered again if it did not start), DHCP started. 0 if joined. */
int  wifi_auto_boot(void);

/* The menu's free time (menu_view_t.idle): while the saved network is not
 * joined, another try in a fiber after 5 s, 15 s, 30 s, 1 min, then every
 * 2 min. The menu goes on meanwhile. */
void wifi_auto_idle(uint32_t until);

/* Before an application runs, the SD card is written or the WiFi is used
 * by hand (Connect, the monitor's W): a try under way stops. */
void wifi_auto_stop(void);

/* One try now (the boot's, the fiber's, the tests'): the radio if it is
 * not up, the saved network, DHCP. 0 if joined. */
int  wifi_auto_try(void);

/* The wait in seconds before the next try after `fails` failed ones. */
uint32_t wifi_auto_delay(int fails);

#endif
