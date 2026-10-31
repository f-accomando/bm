/* RGB30 external screen: the HDMI transmitter of the RK3566 (Synopsys DW-HDMI
 * with its Gen2 PHY at 0xfe0a0000), fixed to 1280x720 at 60 Hz (CEA mode 4,
 * RGB 8 bit, pixel clock 74.25 MHz). rk_display.c drives the VOP side. */
#ifndef RK_HDMI_H
#define RK_HDMI_H

#include <stdint.h>

/* CEA mode 4: 1280x720p60, both syncs positive */
#define HDMI_W          1280u
#define HDMI_H          720u
#define HDMI_HTOTAL     1650u
#define HDMI_HSYNC      40u
#define HDMI_HFRONT     110u
#define HDMI_VTOTAL     750u
#define HDMI_VSYNC      5u
#define HDMI_VFRONT     5u
#define HDMI_PIXEL_HZ   74250000u

/* The clocks of the transmitter on (the VO power domain must be on), and
 * its two SDA/SCL inputs routed. Call before any other function here. */
void rk_hdmi_clocks(void);
/* 1 if a cable with a powered sink is plugged in (the HPD pin) */
int rk_hdmi_hpd(void);
/* The picture path and the PHY. The VOP must already send pixels at
 * 74.25 MHz. 0 on success, -1 if the PHY does not lock. */
int rk_hdmi_start(void);
/* PHY and clocks of the picture path off */
void rk_hdmi_stop(void);

#endif
