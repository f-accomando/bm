#ifndef RK_BT_H
#define RK_BT_H

#include <stdint.h>

/* RTL8821CS on: H5 link, firmware, HCI reset. *baud_out is set to the
 * UART speed in use when it changed. 0, or -1 (reason printed). */
int rk_bt_bringup(uint32_t *baud_out);

#endif
