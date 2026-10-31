/*
 * The HDMI transmitter of the RK3566: Synopsys DW-HDMI (registers 8 bits wide,
 * each at a 32-bit word: address << 2) and its Gen2 PHY, programmed through
 * the controller's own I2C master. One mode only, 1280x720p60 (rk_hdmi.h),
 * RGB 8 bit in and out, no audio, no EDID: the picture is always the same,
 * and a sink that accepts 720p60 shows it.
 *
 * The order follows the controller's data book: frame composer, PHY (twice),
 * video path, packetizer, colour space converter off, AVI infoframe. The PHY
 * values for 74.25 MHz (MPLL, symbol, termination, level) are the ones the
 * RK3568 needs. The VOP has to be sending pixels before the PHY can lock.
 */
#ifdef PLAT_RK3566
#include "rk_hdmi.h"
#include "io.h"
#include "drivers/timer.h"

#define HDMI        0xfe0a0000u
#define CRU         0xfdd20000u
#define GRF         0xfdc60000u

/* the controller */
#define IH_I2CMPHY_STAT0    0x0108
#define TX_INVID0           0x0200
#define TX_INSTUFFING       0x0201
#define TX_GYDATA0          0x0202
#define VP_PR_CD            0x0801
#define VP_STUFF            0x0802
#define VP_REMAP            0x0803
#define VP_CONF             0x0804
#define FC_INVIDCONF        0x1000
#define FC_INHACTV0         0x1001
#define FC_INHBLANK0        0x1003
#define FC_INVACTV0         0x1005
#define FC_INVBLANK         0x1007
#define FC_HSYNCINDELAY0    0x1008
#define FC_HSYNCINWIDTH0    0x100a
#define FC_VSYNCINDELAY     0x100c
#define FC_VSYNCINWIDTH     0x100d
#define FC_CTRLDUR          0x1011
#define FC_EXCTRLDUR        0x1012
#define FC_EXCTRLSPAC       0x1013
#define FC_CH0PREAM         0x1014
#define FC_CH1PREAM         0x1015
#define FC_CH2PREAM         0x1016
#define FC_AVICONF3         0x1017
#define FC_AVICONF0         0x1019
#define FC_AVICONF1         0x101a
#define FC_AVICONF2         0x101b
#define FC_AVIVID           0x101c
#define FC_AVIBARS          0x101d      /* ETB0 .. SRB1: eight bytes, all 0 */
#define FC_PRCONF           0x10e0
#define FC_SCRAMBLER_CTRL   0x10e1
#define PHY_CONF0           0x3000
#define PHY_TST0            0x3001
#define PHY_STAT0           0x3004
#define PHY_I2CM_SLAVE      0x3020
#define PHY_I2CM_ADDRESS    0x3021
#define PHY_I2CM_DATAO_1    0x3022
#define PHY_I2CM_DATAO_0    0x3023
#define PHY_I2CM_OPERATION  0x3026
#define MC_CLKDIS           0x4001
#define MC_FLOWCTRL         0x4004
#define MC_PHYRSTZ          0x4005
#define MC_HEACPHY_RST      0x4007
#define CSC_CFG             0x4100
#define CSC_SCALE           0x4101

#define PHY_CONF0_PDDQ      0x10
#define PHY_CONF0_TXPWRON   0x08
#define PHY_CONF0_ENHPD     0x04
#define PHY_CONF0_DATAENPOL 0x02
#define PHY_CONF0_SELDIPIF  0x01
#define PHY_STAT0_LOCK      0x01
#define PHY_STAT0_HPD       0x02

/* the PHY's registers, through its I2C slave (address 0x69) */
#define PHY_CKCALCTRL       0x05
#define PHY_CPCE_CTRL       0x06
#define PHY_CKSYMTXCTRL     0x09
#define PHY_VLEVCTRL        0x0e
#define PHY_CURRCTRL        0x10
#define PHY_PLLPHBYCTRL     0x13
#define PHY_GMPCTRL         0x15
#define PHY_MSM_CTRL        0x17
#define PHY_TXTERM          0x19

static inline uint32_t rd(uint32_t a)
{
    return readl(HDMI + (a << 2)) & 0xff;
}

static inline void wr(uint32_t a, uint32_t v)
{
    writel(HDMI + (a << 2), v & 0xff);
}

static void mod(uint32_t a, uint32_t mask, uint32_t val)
{
    wr(a, (rd(a) & ~mask) | (val & mask));
}

void rk_hdmi_clocks(void)
{
    /* pclk_hdmi_host, clk_hdmi_sfr, clk_hdmi_cec: bits 3..5 of CLKGATE_CON21 (0 = on) */
    writel(CRU + 0x354, HIWORD(0x38, 0));
    /* the DDC lines from the pins, not from I2C5 (GRF_VO_CON1 bits 15 and 14) */
    writel(GRF + 0x364, HIWORD(0xc000, 0xc000));
}

int rk_hdmi_hpd(void)
{
    mod(PHY_CONF0, PHY_CONF0_ENHPD, PHY_CONF0_ENHPD);
    return (rd(PHY_STAT0) & PHY_STAT0_HPD) != 0;
}

static int phy_i2c_write(uint32_t reg, uint32_t val)
{
    wr(IH_I2CMPHY_STAT0, 0xff);
    wr(PHY_I2CM_ADDRESS, reg);
    wr(PHY_I2CM_DATAO_1, val >> 8);
    wr(PHY_I2CM_DATAO_0, val);
    wr(PHY_I2CM_OPERATION, 0x10);                   /* write */
    for (int ms = 0; ms < 100; ms++) {
        uint32_t st = rd(IH_I2CMPHY_STAT0) & 3;     /* done or error */
        if (st) {
            wr(IH_I2CMPHY_STAT0, st);
            return st & 2 ? -1 : 0;
        }
        timer_delay_ms(1);
    }
    return -1;
}

static void phy_power(int on)
{
    if (!on) {
        mod(PHY_CONF0, PHY_CONF0_TXPWRON, 0);
        for (int i = 0; i < 5 && (rd(PHY_STAT0) & PHY_STAT0_LOCK); i++)
            timer_delay_ms(1);
        mod(PHY_CONF0, PHY_CONF0_PDDQ, PHY_CONF0_PDDQ);
        return;
    }
    mod(PHY_CONF0, PHY_CONF0_TXPWRON, PHY_CONF0_TXPWRON);
    mod(PHY_CONF0, PHY_CONF0_PDDQ, 0);
}

/* one pass of the PHY set-up for 74.25 MHz; the data book asks for two */
static int phy_pass(void)
{
    phy_power(0);
    mod(PHY_CONF0, PHY_CONF0_DATAENPOL | PHY_CONF0_SELDIPIF, PHY_CONF0_DATAENPOL);
    wr(FC_SCRAMBLER_CTRL, 0);                       /* clock ratio 1/10, no scrambling */
    wr(MC_PHYRSTZ, 1);                              /* PHY reset (active high) */
    wr(MC_PHYRSTZ, 0);
    wr(MC_HEACPHY_RST, 1);
    mod(PHY_TST0, 0x20, 0x20);                      /* test clear, then the slave address */
    wr(PHY_I2CM_SLAVE, 0x69);
    mod(PHY_TST0, 0x20, 0);
    int r = 0;
    r |= phy_i2c_write(PHY_CPCE_CTRL, 0x0072);      /* MPLL for 74.25 MHz */
    r |= phy_i2c_write(PHY_GMPCTRL, 0x0001);
    r |= phy_i2c_write(PHY_CURRCTRL, 0x0000);
    r |= phy_i2c_write(PHY_PLLPHBYCTRL, 0x0000);
    r |= phy_i2c_write(PHY_MSM_CTRL, 3 << 1);       /* clock out: feedback clock */
    r |= phy_i2c_write(PHY_TXTERM, 0x0004);
    r |= phy_i2c_write(PHY_CKSYMTXCTRL, 0x8009);
    r |= phy_i2c_write(PHY_VLEVCTRL, 0x0272);
    r |= phy_i2c_write(PHY_CKCALCTRL, 0x8000);      /* override */
    phy_power(1);
    for (int i = 0; i < 50; i++) {
        if (rd(PHY_STAT0) & PHY_STAT0_LOCK)
            return r ? -2 : 0;
        timer_delay_ms(1);
    }
    return -1;
}

static void composer(void)
{
    const uint32_t hblank = HDMI_HTOTAL - HDMI_W, vblank = HDMI_VTOTAL - HDMI_H;
    wr(FC_INVIDCONF, 0x40 | 0x20 | 0x10 | 0x08);    /* +vsync, +hsync, DE high, HDMI mode */
    wr(FC_INHACTV0, HDMI_W);        wr(FC_INHACTV0 + 1, HDMI_W >> 8);
    wr(FC_INVACTV0, HDMI_H);        wr(FC_INVACTV0 + 1, HDMI_H >> 8);
    wr(FC_INHBLANK0, hblank);       wr(FC_INHBLANK0 + 1, hblank >> 8);
    wr(FC_INVBLANK, vblank);
    wr(FC_HSYNCINDELAY0, HDMI_HFRONT);  wr(FC_HSYNCINDELAY0 + 1, HDMI_HFRONT >> 8);
    wr(FC_VSYNCINDELAY, HDMI_VFRONT);
    wr(FC_HSYNCINWIDTH0, HDMI_HSYNC);   wr(FC_HSYNCINWIDTH0 + 1, HDMI_HSYNC >> 8);
    wr(FC_VSYNCINWIDTH, HDMI_VSYNC);
    wr(FC_PRCONF, 0x10);                            /* pixel repetition 1 */
}

static void video_path(void)
{
    wr(FC_CTRLDUR, 12);
    wr(FC_EXCTRLDUR, 32);
    wr(FC_EXCTRLSPAC, 1);
    wr(FC_CH0PREAM, 0x0b);
    wr(FC_CH1PREAM, 0x16);
    wr(FC_CH2PREAM, 0x21);
    /* HDCP, CSC, audio, pixel repeater and CEC clocks off; pixel and TMDS clocks on */
    wr(MC_CLKDIS, 0x7d);
    wr(MC_CLKDIS, 0x7c);
    wr(MC_FLOWCTRL, 0);                             /* colour space converter bypassed */
    /* packetizer: 8 bit RGB goes straight through */
    wr(VP_PR_CD, 4 << 4);                           /* 24 bits per pixel, no repetition */
    wr(VP_STUFF, 0x05);
    wr(VP_REMAP, 0);
    wr(VP_CONF, 0x40 | 0x03);                       /* bypass */
    wr(CSC_CFG, 0);
    wr(CSC_SCALE, 0x00);                            /* 24 bpp */
    wr(TX_INVID0, 0x01);                            /* RGB888, internal generator off */
    wr(TX_INSTUFFING, 0x07);
    for (uint32_t a = TX_GYDATA0; a < TX_GYDATA0 + 6; a++)
        wr(a, 0);
}

static void avi_infoframe(void)
{
    wr(FC_AVICONF0, 0x40);                          /* RGB, active format present */
    wr(FC_AVICONF1, 0x80 | 0x20 | 0x08);            /* ITU-R 709, 16:9, as coded */
    wr(FC_AVICONF2, 0x08);                          /* full range RGB */
    wr(FC_AVICONF3, 0);
    wr(FC_AVIVID, 4);                               /* CEA mode 4: 720p60 */
    for (uint32_t a = FC_AVIBARS; a < FC_AVIBARS + 8; a++)
        wr(a, 0);
}

int rk_hdmi_start(void)
{
    rk_hdmi_clocks();
    composer();
    int r = phy_pass();
    if (r == 0)
        r = phy_pass();
    video_path();
    avi_infoframe();
    return r == -1 ? -1 : 0;
}

void rk_hdmi_stop(void)
{
    phy_power(0);
    wr(MC_CLKDIS, 0x7f);
}
#endif
