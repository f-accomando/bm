/*
 * RGB30 panel link: MIPI DSI0 (Synopsys DesignWare host at 0xfe060000),
 * the Innosilicon D-PHY at 0xfe850000 and the panel, a Sitronix ST7703
 * 720x720 ("powkiddy,rgb30-panel"). Values follow Linux (dw-mipi-dsi.c,
 * dw-mipi-dsi-rockchip.c, phy-rockchip-inno-dsidphy.c,
 * panel-sitronix-st7703.c) with U-Boot's order for the PHY: powered before
 * PHY_RSTZ is released, so the PLL lock bit means something.
 *
 * Link: 4 lanes, RGB888, burst video, no EoT packet, commands in LP mode.
 * PHY: 24 MHz reference, prediv 2, fbdiv 45 -> 270 Mbit/s per lane. The
 * host counts with 277 (Linux's figure), as the panel driver ships.
 */
#ifdef PLAT_RK3566
#include "rk_display.h"
#include "rk_gpio.h"
#include "io.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#define DSI         0xfe060000u
#define DPHY        0xfe850000u
#define CRU         0xfdd20000u
#define PMUCRU      0xfdd00000u
#define GRF         0xfdc60000u

#define DSI_VERSION         0x00
#define DSI_PWR_UP          0x04
#define DSI_CLKMGR_CFG      0x08
#define DSI_DPI_VCID        0x0c
#define DSI_DPI_COLOR_CODING 0x10
#define DSI_DPI_CFG_POL     0x14
#define DSI_DPI_LP_CMD_TIM  0x18
#define DSI_PCKHDL_CFG      0x2c
#define DSI_MODE_CFG        0x34
#define DSI_VID_MODE_CFG    0x38
#define DSI_VID_PKT_SIZE    0x3c
#define DSI_VID_HSA_TIME    0x48
#define DSI_VID_HBP_TIME    0x4c
#define DSI_VID_HLINE_TIME  0x50
#define DSI_VID_VSA_LINES   0x54
#define DSI_VID_VBP_LINES   0x58
#define DSI_VID_VFP_LINES   0x5c
#define DSI_VID_VACTIVE_LINES 0x60
#define DSI_CMD_MODE_CFG    0x68
#define DSI_GEN_HDR         0x6c
#define DSI_GEN_PLD_DATA    0x70
#define DSI_CMD_PKT_STATUS  0x74
#define DSI_TO_CNT_CFG      0x78
#define DSI_BTA_TO_CNT      0x8c
#define DSI_LPCLK_CTRL      0x94
#define DSI_PHY_TMR_LPCLK_CFG 0x98
#define DSI_PHY_TMR_CFG     0x9c
#define DSI_PHY_RSTZ        0xa0
#define DSI_PHY_IF_CFG      0xa4
#define DSI_PHY_STATUS      0xb0
#define DSI_PHY_TST_CTRL0   0xb4
#define DSI_INT_ST0         0xbc
#define DSI_INT_ST1         0xc0
#define DSI_INT_MSK0        0xc4
#define DSI_INT_MSK1        0xc8
#define DSI_PHY_TMR_RD_CFG  0xf4

#define VID_MODE_LP_ALL     (0x3fu << 8)
#define VID_MODE_BURST      2u
#define VID_MODE_LP_CMD_EN  (1u << 15)
#define CMD_MODE_ALL_LP     0x010f7f00u

#define ST_GEN_CMD_EMPTY    (1u << 0)
#define ST_GEN_CMD_FULL     (1u << 1)
#define ST_GEN_PLD_W_EMPTY  (1u << 2)
#define ST_GEN_PLD_W_FULL   (1u << 3)
#define ST_GEN_PLD_R_EMPTY  (1u << 4)
#define ST_GEN_RD_CMD_BUSY  (1u << 6)

#define PANEL_RESET         rk_pin(4, 'A', 0)
#define PANEL_POWER         rk_pin(0, 'C', 2)

static inline void dsi_w(uint32_t off, uint32_t v)  { writel(DSI + off, v); }
static inline uint32_t dsi_r(uint32_t off)          { return readl(DSI + off); }

/* --- D-PHY: 8-bit registers, one per word, part p register n at (p << 7) + n * 4 --- */

enum { P_ANALOG, P_DIGITAL, P_CLK, P_D0, P_D1, P_D2, P_D3, P_LVDS };

static void phy_rmw(unsigned part, unsigned reg, uint32_t mask, uint32_t val)
{
    uintptr_t a = DPHY + ((part << 5 | reg) << 2);
    writel(a, (readl(a) & ~mask) | (val & mask));
}

static void dphy_power_on(void)
{
    phy_rmw(P_ANALOG, 0x00, 0x80, 0x00);        /* bandgap on */
    phy_rmw(P_ANALOG, 0x00, 0x03, 0x01);        /* POWER_WORK_ENABLE */
    phy_rmw(P_LVDS, 0x03, 0x07, 0x01);          /* MIPI mode */
    phy_rmw(P_ANALOG, 0x03, 0x1f, 2);           /* prediv */
    phy_rmw(P_ANALOG, 0x03, 0x20, 0);           /* fbdiv[8] */
    phy_rmw(P_ANALOG, 0x04, 0xff, 45);          /* fbdiv[7:0] */
    phy_rmw(P_ANALOG, 0x08, 0x20, 0x20);        /* PLL post divider */
    phy_rmw(P_ANALOG, 0x0b, 0x0f, 0x0f);        /* clock lane VOD */
    phy_rmw(P_ANALOG, 0x01, 0x03, 0);           /* LDO and PLL on */
    phy_rmw(P_ANALOG, 0x01, 0x04, 0x04);        /* analog reset */
    timer_delay_us(2);
    phy_rmw(P_ANALOG, 0x01, 0x04, 0);
    phy_rmw(P_DIGITAL, 0x00, 0x01, 0);          /* digital reset */
    timer_delay_us(2);
    phy_rmw(P_DIGITAL, 0x00, 0x01, 1);
    for (unsigned p = P_CLK; p <= P_D3; p++) {
        unsigned hs_zero = p == P_CLK ? 0x18 : 0x06;
        phy_rmw(p, 0x05, 0x3f, 0x02);           /* T_LPX */
        phy_rmw(p, 0x06, 0x7f, 0x7f);           /* T_HS_PREPARE */
        phy_rmw(p, 0x06, 0x80, 0);              /* T_HS_ZERO[6] */
        phy_rmw(p, 0x07, 0x3f, hs_zero);
        phy_rmw(p, 0x08, 0x7f, 0x04);           /* T_HS_TRAIL */
        phy_rmw(p, 0x11, 0x40, 0);              /* T_HS_EXIT[5] */
        phy_rmw(p, 0x09, 0x1f, 4);
        phy_rmw(p, 0x10, 0xc0, 0);              /* T_CLK_POST[5:4] */
        phy_rmw(p, 0x0a, 0x0f, 9);
        phy_rmw(p, 0x0e, 0x0f, 1);              /* T_CLK_PRE */
        phy_rmw(p, 0x0c, 0x03, 0x3);            /* T_WAKEUP = 0x3ff */
        phy_rmw(p, 0x0d, 0xff, 0xff);
        phy_rmw(p, 0x10, 0x3f, 4);              /* T_TA_GO */
        phy_rmw(p, 0x11, 0x3f, 1);              /* T_TA_SURE */
        phy_rmw(p, 0x12, 0x3f, 5);              /* T_TA_WAIT */
    }
    phy_rmw(P_ANALOG, 0x00, 0x7c, 0x7c);        /* clock lane + 4 data lanes */
}

/* --- commands (LP, generic packet interface) --- */

static int wait_status(uint32_t mask, uint32_t want, uint32_t ms)
{
    uint32_t t0 = timer_ticks();
    while ((dsi_r(DSI_CMD_PKT_STATUS) & mask) != want)
        if (timer_ticks() - t0 > ms * 1000)
            return -1;
    return 0;
}

static int dsi_send(uint8_t dt, const uint8_t *b, unsigned len, int is_long)
{
    uint32_t hdr;
    if (is_long) {
        for (unsigned i = 0; i < len; i += 4) {
            uint32_t w = 0;
            for (unsigned j = 0; j < 4 && i + j < len; j++)
                w |= (uint32_t)b[i + j] << (8 * j);
            if (wait_status(ST_GEN_PLD_W_FULL, 0, 50))
                return -1;
            dsi_w(DSI_GEN_PLD_DATA, w);
        }
        hdr = dt | (len & 0xff) << 8 | ((len >> 8) & 0xff) << 16;
    } else {
        hdr = dt | (len > 0 ? (uint32_t)b[0] << 8 : 0) | (len > 1 ? (uint32_t)b[1] << 16 : 0);
    }
    if (wait_status(ST_GEN_CMD_FULL, 0, 50))
        return -2;
    dsi_w(DSI_GEN_HDR, hdr);
    if (wait_status(ST_GEN_CMD_EMPTY | ST_GEN_PLD_W_EMPTY, ST_GEN_CMD_EMPTY | ST_GEN_PLD_W_EMPTY, 60))
        return -3;
    return 0;
}

static int dcs_write(const uint8_t *b, unsigned len)
{
    if (len == 1)
        return dsi_send(0x05, b, len, 0);
    if (len == 2)
        return dsi_send(0x15, b, len, 0);
    return dsi_send(0x39, b, len, 1);
}

/* One byte back from the panel (DCS read in LP, the turnaround on, as
 * dw_mipi_dsi_read): 0, or < 0 if nothing came back. */
static int dcs_read1(uint8_t cmd, uint8_t *out)
{
    for (int i = 0; i < 16 && !(dsi_r(DSI_CMD_PKT_STATUS) & ST_GEN_PLD_R_EMPTY); i++)
        (void)dsi_r(DSI_GEN_PLD_DATA);          /* nothing old in the read FIFO */
    static const uint8_t max1[2] = { 1, 0 };
    if (dsi_send(0x37, max1, 2, 0))             /* set maximum return packet size */
        return -1;
    if (dsi_send(0x06, &cmd, 1, 0))             /* DCS read, no parameters */
        return -2;
    if (wait_status(ST_GEN_RD_CMD_BUSY, 0, 50))
        return -3;
    if (wait_status(ST_GEN_PLD_R_EMPTY, 0, 20))
        return -4;
    *out = (uint8_t)dsi_r(DSI_GEN_PLD_DATA);
    return 0;
}

/* panel-sitronix-st7703.c rgb30panel_init_sequence + the st7703_enable
 * tail; len, bytes...; len 0: the next byte is a delay in units of 10 ms */
static const uint8_t rgb30_init[] = {
    1, 0x11,
    0, 25,
    4, 0xb9, 0xf1, 0x12, 0x83,
    28, 0xba, 0x33, 0x81, 0x05, 0xf9, 0x0e, 0x0e, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x44, 0x25, 0x00, 0x90, 0x0a, 0x00, 0x00, 0x01, 0x4f, 0x01, 0x00, 0x00, 0x37,
    5, 0xb8, 0x25, 0x22, 0xf0, 0x63,
    4, 0xbf, 0x02, 0x11, 0x00,
    11, 0xb3, 0x10, 0x10, 0x28, 0x28, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00,
    10, 0xc0, 0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x12, 0x70, 0x00,
    2, 0xbc, 0x46,
    2, 0xcc, 0x0b,
    2, 0xb4, 0x80,
    4, 0xb2, 0x3c, 0x12, 0x30,
    15, 0xe3, 0x07, 0x07, 0x0b, 0x0b, 0x03, 0x0b, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xc0, 0x10,
    13, 0xc1, 0x36, 0x00, 0x32, 0x32, 0x77, 0xf1, 0xcc, 0xcc, 0x77, 0x77, 0x33, 0x33,
    3, 0xb5, 0x0a, 0x0a,
    3, 0xb6, 0x88, 0x88,
    64, 0xe9, 0xc8, 0x10, 0x0a, 0x10, 0x0f, 0xa1, 0x80, 0x12, 0x31, 0x23, 0x47, 0x86, 0xa1, 0x80,
        0x47, 0x08, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x48,
        0x02, 0x8b, 0xaf, 0x46, 0x02, 0x88, 0x88, 0x88, 0x88, 0x88, 0x48, 0x13, 0x8b, 0xaf, 0x57,
        0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    62, 0xea, 0x96, 0x12, 0x01, 0x01, 0x01, 0x78, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x31,
        0x8b, 0xa8, 0x31, 0x75, 0x88, 0x88, 0x88, 0x88, 0x88, 0x4f, 0x20, 0x8b, 0xa8, 0x20, 0x64,
        0x88, 0x88, 0x88, 0x88, 0x88, 0x23, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xa1, 0x80, 0x00, 0x00,
        0x00, 0x00,
    35, 0xe0, 0x00, 0x0a, 0x0f, 0x29, 0x3b, 0x3f, 0x42, 0x39, 0x06, 0x0d, 0x10, 0x13, 0x15, 0x14,
        0x15, 0x10, 0x17, 0x00, 0x0a, 0x0f, 0x29, 0x3b, 0x3f, 0x42, 0x39, 0x06, 0x0d, 0x10, 0x13,
        0x15, 0x14, 0x15, 0x10, 0x17,
    1, 0x11,
    0, 12,
    1, 0x29,
};

/* Returns 0, or the 1-based index of the command that failed (with its
 * error in *err). */
static int panel_init(int *err)
{
    int n = 0;
    for (unsigned i = 0; i < sizeof rgb30_init; ) {
        unsigned len = rgb30_init[i++];
        if (len == 0) {
            timer_delay_ms(rgb30_init[i++] * 10u);
            continue;
        }
        n++;
        int r = dcs_write(&rgb30_init[i], len);
        if (r) {
            *err = r;
            return n;
        }
        i += len;
    }
    return 0;
}

/* --- the whole link --- */

static void set_mode(int video)
{
    dsi_w(DSI_PWR_UP, 0);
    if (video) {
        dsi_w(DSI_MODE_CFG, 0);
        dsi_w(DSI_VID_MODE_CFG, VID_MODE_LP_ALL | VID_MODE_BURST);
    } else {
        dsi_w(DSI_MODE_CFG, 1);
    }
    dsi_w(DSI_LPCLK_CTRL, 1);                   /* continuous HS clock */
    dsi_w(DSI_PWR_UP, 1);
}

static int poll_phy(uint32_t bit)
{
    uint32_t t0 = timer_ticks();
    while (!(dsi_r(DSI_PHY_STATUS) & bit))
        if (timer_ticks() - t0 > 10000)
            return -1;
    return 0;
}

/* The panel off for real: reset low, the supply off. After a restart of
 * the chip alone it may still be lit from the run before (2026-10-10: a
 * black screen after a kernel update, fine after a power off and on). */
static void panel_power_off(void)
{
    rk_gpio_output(PANEL_RESET, 0);
    rk_gpio_output(PANEL_POWER, 0);
}

/* On again after at least min_off_ms off: supply, 20 ms, reset released,
 * then the 120 ms the ST7703 wants before sleep out. */
static void panel_power_on(uint32_t off_since, uint32_t min_off_ms)
{
    while (timer_ticks() - off_since < min_off_ms * 1000)
        ;
    rk_gpio_output(PANEL_POWER, 1);
    timer_delay_ms(20);
    rk_gpio_set(PANEL_RESET, 1);
    timer_delay_ms(120);
}

/* ST7703 power mode (DCS 0x0a): sleep out (bit 4) and display on (bit 2) */
#define PM_ALIVE    0x14u

int rk_dsi_init(char *log, unsigned size)
{
    int pos = 0;
#define LOG(...) do { if (pos < (int)size) pos += ksnprintf(log + pos, size - pos, __VA_ARGS__); } while (0)

    /* the panel off first, whatever the run before left: it drains while
     * the host and the PHY start */
    panel_power_off();
    uint32_t off_since = timer_ticks();

    /* clocks: pclk_dsitx_0 (CLKGATE_CON21 bit 6), pclk_mipidsiphy0
     * (CLKGATE_CON33 bit 14); PHY reference = the 24 MHz crystal
     * (PMU_CLKSEL_CON8 bit 2 = 1, PMU_CLKGATE_CON2 bit 3 ungated) */
    rk_write_mask(CRU + 0x354, 1u << 6, 0);
    rk_write_mask(CRU + 0x384, 1u << 14, 0);
    rk_write_mask(PMUCRU + 0x120, 1u << 2, 1u << 2);
    rk_write_mask(PMUCRU + 0x188, 1u << 3, 0);
    /* DSI host APB reset pulse (SOFTRST_CON17 bit 0) */
    rk_write_mask(CRU + 0x444, 1u << 0, 1u << 0);
    timer_delay_us(20);
    rk_write_mask(CRU + 0x444, 1u << 0, 0);
    timer_delay_us(20);
    /* lanes not forced, turnaround allowed */
    writel(GRF + 0x368, 0xf8f50000u);

    uint32_t version = dsi_r(DSI_VERSION);
    LOG("DSI version %08lx", version);

    /* host (dw_mipi_dsi_mode_set) */
    dsi_w(DSI_PWR_UP, 0);
    dsi_w(DSI_CLKMGR_CFG, 2);                   /* escape clock = byte clock / 2 */
    dsi_w(DSI_DPI_VCID, 0);
    dsi_w(DSI_DPI_COLOR_CODING, 5);             /* 24 bit */
    dsi_w(DSI_DPI_CFG_POL, 0x6);                /* hsync, vsync active low */
    dsi_w(DSI_PCKHDL_CFG, 0x1c);                /* CRC, ECC, BTA; no EoTp */
    dsi_w(DSI_VID_MODE_CFG, VID_MODE_LP_ALL | VID_MODE_BURST);
    dsi_w(DSI_VID_PKT_SIZE, 720);
    dsi_w(DSI_TO_CNT_CFG, 0);
    dsi_w(DSI_BTA_TO_CNT, 0xd00);
    dsi_w(DSI_MODE_CFG, 1);
    dsi_w(DSI_VID_HLINE_TIME, 771);
    dsi_w(DSI_VID_HSA_TIME, 4);
    dsi_w(DSI_VID_HBP_TIME, 43);
    dsi_w(DSI_VID_VACTIVE_LINES, 720);
    dsi_w(DSI_VID_VSA_LINES, 3);
    dsi_w(DSI_VID_VFP_LINES, 15);
    dsi_w(DSI_VID_VBP_LINES, 11);
    dsi_w(DSI_PHY_RSTZ, 0);
    dsi_w(DSI_PHY_TST_CTRL0, 0);
    dsi_w(DSI_PHY_TST_CTRL0, 1);
    dsi_w(DSI_PHY_TST_CTRL0, 0);
    if ((version >> 8) >= 0x313331u) {
        dsi_w(DSI_PHY_TMR_CFG, 0x00120029u);
        dsi_w(DSI_PHY_TMR_RD_CFG, 10000);
    } else {
        dsi_w(DSI_PHY_TMR_CFG, 0x12292710u);
    }
    dsi_w(DSI_PHY_TMR_LPCLK_CFG, 0x001b0032u);
    dsi_w(DSI_PHY_IF_CFG, 0x2003);              /* stop wait 0x20, 4 lanes */
    (void)dsi_r(DSI_INT_ST0);
    (void)dsi_r(DSI_INT_ST1);
    dsi_w(DSI_INT_MSK0, 0);
    dsi_w(DSI_INT_MSK1, 0);

    /* PHY on, then out of reset */
    dphy_power_on();
    dsi_w(DSI_PHY_RSTZ, 0xf);
    int lock = poll_phy(1u << 0), stop = poll_phy(1u << 2);
    LOG(", PHY %s%s", lock ? "NO LOCK" : "locked", stop ? " (clock lane not stopped)" : "");
    timer_delay_ms(34);
    set_mode(0);

    int bad = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        /* panel power and reset: at least 200 ms off (the first time,
         * counted from the top), then on */
        if (attempt) {
            set_mode(0);
            panel_power_off();
            off_since = timer_ticks();
        }
        panel_power_on(off_since, 200);
        rgb30_display_stage(attempt ? "panel power cycled again" : "panel power cycled");

        /* video on, then the commands in the blanking (as Linux) */
        set_mode(1);
        dsi_w(DSI_DPI_LP_CMD_TIM, 0x00100004u);
        dsi_w(DSI_CMD_MODE_CFG, CMD_MODE_ALL_LP);
        dsi_w(DSI_VID_MODE_CFG, dsi_r(DSI_VID_MODE_CFG) | VID_MODE_LP_CMD_EN);
        int err = 0;
        bad = panel_init(&err);
        if (bad) {
            LOG(", panel command %d failed (%d)", bad, err);
            /* second chance: the whole sequence in command mode */
            set_mode(0);
            rk_gpio_set(PANEL_RESET, 0);
            timer_delay_ms(20);
            rk_gpio_set(PANEL_RESET, 1);
            timer_delay_ms(120);
            bad = panel_init(&err);
            LOG(bad ? ", in command mode too (%d)" : ", ok in command mode", bad);
            set_mode(1);
        } else {
            LOG(", panel on");
        }

        /* is it on? its power mode, read in command mode (a read in the
         * video's blanking may not fit); no answer is not a verdict */
        set_mode(0);
        uint8_t pm = 0;
        int rd = dcs_read1(0x0a, &pm);
        set_mode(1);
        dsi_w(DSI_VID_MODE_CFG, dsi_r(DSI_VID_MODE_CFG) | VID_MODE_LP_CMD_EN);
        int dead;
        if (rd) {
            LOG(", readback none (%d)", rd);
            rgb30_display_stage("panel readback: no answer");
            dead = bad;
        } else {
            LOG(", power mode %02x", pm);
            dead = bad || (pm & PM_ALIVE) != PM_ALIVE;
            rgb30_display_stage(dead ? "panel readback: not on" : "panel readback: on");
        }
        if (!dead) {
            bad = 0;
            break;
        }
        bad = bad ? bad : -1;
        if (!attempt)
            LOG(", power cycle again");
    }
    uint32_t st0 = dsi_r(DSI_INT_ST0), st1 = dsi_r(DSI_INT_ST1);
    if (st0 || st1)
        LOG(", errors %lx %lx", st0, st1);
    /* the PHY's lock bit is only a hint (Linux polls it as a debug
     * message, before the PHY is even on): a failed command is the problem */
    return bad ? -1 : 0;
#undef LOG
}

/* Before a restart or power off: with the link up, display off and sleep
 * in first (as st7703_disable/unprepare), then reset, supply off, and the
 * time for the supply to drain, so the next start finds the panel cold. */
void rk_dsi_off(int link_up)
{
    if (link_up) {
        static const uint8_t display_off[] = { 0x28 }, sleep_in[] = { 0x10 };
        dcs_write(display_off, 1);
        timer_delay_ms(20);
        dcs_write(sleep_in, 1);
        timer_delay_ms(120);
    }
    panel_power_off();
    timer_delay_ms(300);
}
#endif
