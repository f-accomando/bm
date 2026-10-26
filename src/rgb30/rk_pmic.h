/* RK817 PMIC (rk_pmic.c). Register reads return -1 on an I2C error. */
#ifndef RK_PMIC_H
#define RK_PMIC_H

#include <stdint.h>

int  rk817_read(uint8_t reg);
int  rk817_write(uint8_t reg, uint8_t val);
int  rk817_present(void);
void rk817_power_off(void);
void rk817_clk32k_wifi(int on);
int  rk817_battery_mv(void);
int  rk817_battery_ma(void);                /* signed: negative = discharge; INT32_MIN on error */
int64_t rk817_charge_uah(void);             /* the gauge's charge counter; -1 on error */
/* 0 off, 1 dead, 2 trickle, 3 charging, 4 full, 5-7 errors */
int  rk817_charge_state(void);
int  rk817_plugged(void);

#endif
