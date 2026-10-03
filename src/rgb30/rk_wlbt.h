#ifndef RK_WLBT_H
#define RK_WLBT_H

/* the RTL8821CS module powered, its 32 kHz clock on, WiFi out of reset */
void wlbt_power_on(void);
/* WiFi side through reset again (WL_REG_ON low, high, 200 ms) */
void wlbt_wifi_reset(void);

#endif
