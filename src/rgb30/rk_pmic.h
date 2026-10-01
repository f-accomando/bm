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
/* 0 off, 1 dead, 2 trickle, 3 charging, 4 full, 5-7 errors */
int  rk817_charge_state(void);
int  rk817_plugged(void);
/* 1 if the power key was pressed since the last call */
int  rk817_power_key(void);

#endif
