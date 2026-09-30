/* A simulated LAN9512 Ethernet function for the host tests of
 * src/usb/smsc95xx.c: registers behind vendor requests, the PHY behind
 * MII, frames in both directions. Also the kernel's timer and firmware
 * (MAC address) as the driver sees them. */
#ifndef LAN9512_SIM_H
#define LAN9512_SIM_H

#include "usb/usb.h"

#define SIM_HW_CFG   0x14
#define SIM_PM_CTRL  0x20
#define SIM_TX_CFG   0x10
#define SIM_MAC_CR   0x100
#define SIM_ADDRH    0x104
#define SIM_ADDRL    0x108

extern uint32_t sim_reg[0x140 / 4];
extern uint16_t sim_phy[32];
extern int sim_lrsts, sim_phyrsts, sim_reg_writes;
extern int sim_fails;                   /* protocol errors seen by the chip */
extern usb_dev_t sim_dev;
extern const uint8_t sim_cfg[39];       /* its configuration descriptor */
extern const uint8_t sim_board_mac[6];  /* what the firmware says */
extern uint32_t sim_now_us;             /* timer_ticks() */

/* The cable: link up with this partner ability (MII LPA), or down (a loss
 * is latched in BMSR until read, as on the chip). */
void sim_link(int up, uint16_t lpa);
/* A frame for the driver to receive (error: with the error summary bit). */
void sim_queue_rx(const uint8_t *frame, uint32_t len, int error);
/* Called with every bulk OUT transfer (command header included). */
extern void (*sim_on_out)(const uint8_t *data, uint32_t len);

#endif
