/*
 * The RGB30's screen: VOP2 (0xfe040000) video port 1 -> MIPI DSI0 -> the
 * 720x720 ST7703 panel, the image in window Esmart0 (centred, or scaled),
 * the backlight on PWM4. Order and values follow Linux (rockchip_drm_vop2.c,
 * rockchip_vop2_reg.c on RK3566): power domain VO, clocks (pixel clock
 * VPLL 292.5 MHz / 8 = 36.5625 MHz, 59.97 Hz), the video port scanning,
 * then the DSI link and the panel (rk_dsi.c), then the window, then the
 * backlight.
 *
 * Diagnostics without a serial cable: the border around a small image is
 * the VP background colour, the same as the menu's. Uniform dark blue-grey:
 * panel fine, window not; black: panel or DSI link not working; nothing at
 * all: backlight. The report of each step is in plat_display_info().
 */
#ifdef PLAT_RK3566
#include "plat.h"
#include "rk_display.h"
#include "rk_gpio.h"
#include "rk_hdmi.h"
#include "io.h"
#include "kernel/config.h"
#include "drivers/timer.h"
#include "lib/printf.h"
#include <string.h>

#define VOP         0xfe040000u
#define VP0         (VOP + 0xc00)
#define VP1         (VOP + 0xd00)
#define ESMART0     (VOP + 0x1800)
#define PMU         0xfdd90000u
#define CRU         0xfdd20000u
#define PMUGRF      0xfdc20000u
#define PWM4        0xfe6e0000u

#define PANEL_W     720
#define PANEL_H     720

#define CFG_DONE_VP1 0x00028002u        /* GLB_CFG_DONE_EN | VP1 | VP1 write enable */
#define CFG_DONE_VP0 0x00018001u        /* the same for VP0 (the external screen) */
#define CFG_DONE    (d.ext ? CFG_DONE_VP0 : CFG_DONE_VP1)
#define BG_COLOR    ((0x40u << 20) | (0x50u << 10) | 0x70u)     /* the menu's 0x10141c, 10-bit */

static struct {
    int up, failed, problem;
    int ext;                                /* 1: the HDMI screen is the one lit, the panel is off */
    int noretry;                            /* HDMI failed to start with this cable plugged in */
    int streak;                             /* polls in a row that disagree with the screen in use */
    uint32_t last_poll;
    uint32_t ow, oh, xoff, yoff;            /* the screen in use: size, and where the image area starts */
    plat_mode_t mode;
    uintptr_t addr;
    uint32_t w, h, depth, out_w, out_h;     /* out: on the screen in use */
    char info[384];
    int pos;
} d;

#define LOG(...) do { if (d.pos < (int)sizeof d.info) \
    d.pos += ksnprintf(d.info + d.pos, sizeof d.info - d.pos, __VA_ARGS__); } while (0)

static int poll_bits(uintptr_t reg, uint32_t mask, uint32_t want, uint32_t us)
{
    uint32_t t0 = timer_ticks();
    while ((readl(reg) & mask) != want)
        if (timer_ticks() - t0 > us)
            return -1;
    return 0;
}

/* --- power domain VO --- */

static int pd_vo_on(void)
{
    writel(CRU + 0x350, HIWORD(0x0047, 0));         /* aclk_vo, hclk_vo, pclk_vo, aclk_vop_pre */
    if (readl(PMU + 0x98) & (1u << 7)) {            /* off */
        writel(PMU + 0xa0, HIWORD(1u << 7, 0));
        if (poll_bits(PMU + 0x98, 1u << 7, 0, 10000))
            return -1;
    }
    if ((readl(PMU + 0x68) | readl(PMU + 0x60)) & (1u << 4)) {
        writel(PMU + 0x50, HIWORD(1u << 4, 0));     /* bus out of idle */
        if (poll_bits(PMU + 0x60, 1u << 4, 0, 10000) || poll_bits(PMU + 0x68, 1u << 4, 0, 10000))
            return -2;
    }
    return 0;
}

/* --- clocks --- */

/* rate of a "pll_rk3328" PLL at CRU + con (MODE_CON0 bit mode_shift) */
static uint32_t pll_khz(uint32_t con, unsigned mode_shift)
{
    if (!((readl(CRU + 0xc0) >> mode_shift) & 1))
        return 24000;                               /* slow mode: the crystal */
    uint32_t c0 = readl(CRU + con), c1 = readl(CRU + con + 4), c2 = readl(CRU + con + 8);
    uint64_t fb = c0 & 0xfff, post1 = (c0 >> 12) & 7, ref = c1 & 0x3f, post2 = (c1 >> 6) & 7;
    if (!ref || !post1 || !post2)
        return 0;
    uint64_t khz = 24000ull * fb;
    if (!(c1 & (1u << 12)))                         /* fractional */
        khz += 24000ull * (c2 & 0xffffff) >> 24;
    return (uint32_t)(khz / ref / post1 / post2);
}

static int vpll_set(uint32_t ref, uint32_t fb, uint32_t post1, uint32_t post2)
{
    writel(CRU + 0xc0, HIWORD(1u << 12, 0));        /* slow mode */
    writel(CRU + 0xa4, HIWORD(1u << 13, 1u << 13)); /* power down */
    writel(CRU + 0xa0, HIWORD(0x7fff, post1 << 12 | fb));
    writel(CRU + 0xa4, HIWORD(0x11ff, 1u << 12 | post2 << 6 | ref));
    writel(CRU + 0xa8, readl(CRU + 0xa8) & ~0x00ffffffu);
    writel(CRU + 0xa4, HIWORD(1u << 13, 0));        /* power up */
    int locked = poll_bits(CRU + 0xa4, 1u << 10, 1u << 10, 2000) == 0;
    writel(CRU + 0xc0, HIWORD(1u << 12, 1u << 12)); /* normal mode */
    return locked ? 0 : -1;
}

static void clocks_on(void)
{
    uint32_t v = pll_khz(0xa0, 12);
    if (v > 291000 && v < 294000) {
        LOG("VPLL %lu kHz (from U-Boot)", v);
    } else if (vpll_set(2, 195, 4, 2) == 0) {       /* VCO 2340 MHz */
        LOG("VPLL set");
    } else {
        int r = vpll_set(1, 195, 4, 4);             /* Linux's table entry */
        LOG("VPLL %s", r ? "NO LOCK" : "set (2nd try)");
    }
    writel(CRU + 0x350, 0x0b470000u);               /* ungate VO and VOP clocks, dclk_vop1 */
    /* aclk_vop_pre = CPLL / n, at most 500 MHz (CPLL is 1000 MHz with
     * mainline U-Boot: n = 2) */
    uint32_t cpll = pll_khz(0x60, 4), div = cpll ? (cpll + 499999) / 500000 : 2;
    if (div < 1) div = 1;
    if (div > 32) div = 32;
    writel(CRU + 0x198, 0x00df0000u | (div - 1));
    writel(CRU + 0x1a0, 0x0cff0407u);               /* dclk_vop1 = VPLL / 8 */
    LOG(", dclk %lu kHz", pll_khz(0xa0, 12) / 8);
}

/* The pixel clock of the external screen: dclk_vop0 = VPLL (1188 MHz) / 16 =
 * 74.25 MHz. VPLL is the panel's clock too (292.5 MHz), so the panel is off
 * before this runs and clocks_on() sets it back. */
static int hdmi_clocks(void)
{
    uint32_t khz = pll_khz(0xa0, 12);
    if ((khz < 1187000 || khz > 1189000) && vpll_set(1, 99, 2, 1))
        return -1;
    writel(CRU + 0x19c, 0x0cff0000u | (1u << 10) | 15);  /* mux VPLL, divide by 16 */
    writel(CRU + 0x350, HIWORD(1u << 10, 0));            /* ungate dclk_vop0 */
    return 0;
}

/* --- VOP2 --- */

/* Windows off, the layers of every window on one port: VP1 for the panel,
 * VP0 for the external screen (OVL_PORT_SEL: the six windows' port in the
 * top half, the layer count of each port below). */
static void vop_common(int ext)
{
    /* IOMMU of the VOP in bypass */
    for (uintptr_t mmu = VOP + 0x3e00; mmu <= VOP + 0x3f00; mmu += 0x100)
        if (readl(mmu + 0x04) & 1)
            writel(mmu + 0x08, 1);
    uint32_t ver = readl(VOP + 0x004);
    LOG(", VOP %08lx", ver);
    if (ver != 0x40158023u)
        LOG(" (unexpected)");
    writel(VOP + 0x050, 1);                         /* OTP_WIN_EN (RK3566) */
    writel(VOP + 0x000, 0x00008000u);               /* GLB_CFG_DONE_EN */
    writel(VOP + 0x008, readl(VOP + 0x008) & ~(1u << 31));     /* no auto gating */
    /* every window off */
    writel(VOP + 0x1000, 0); writel(VOP + 0x1100, 0);
    writel(VOP + 0x1200, 0); writel(VOP + 0x1300, 0);
    for (uintptr_t w = 0x1800; w <= 0x1e00; w += 0x200)
        for (uintptr_t r = 0x10; r <= 0xa0; r += 0x30)
            writel(VOP + w + r, 0);
    /* overlay: every window on the port, Esmart0 the bottom layer */
    writel(VOP + 0x600, 0x10000000u);
    writel(VOP + 0x608, ext ? 0x00000885u : 0x55050858u);
    writel(VOP + 0x604, 0x00763102u);
    writel(VOP + 0x6f0, 0);
    writel(VOP + 0x6f8, 0x00000014u);
    writel(VOP + 0x6e0 + (ext ? 0 : 4), 0x28000000u);
}

static int vop_init(void)
{
    vop_common(0);
    /* VP1 -> MIPI0, polarities as the panel (none positive), interface changes at once;
     * HDMI off, VP0 in standby */
    uint32_t v = readl(VOP + 0x028);
    v = (v & ~((3u << 16) | (1u << 1))) | (1u << 4) | (1u << 16);
    writel(VOP + 0x028, v);
    v = readl(VOP + 0x030);
    v = (v & ~(0xfu << 16)) | (1u << 28);
    writel(VOP + 0x030, v);
    writel(VP0 + 0x00, 1u << 31);
    /* VP1 timing: 720x720, htotal 814, vtotal 749 */
    writel(VP1 + 0x04, 0);
    writel(VP1 + 0x08, 0);
    writel(VP1 + 0x48, 0x032e0004u);
    writel(VP1 + 0x4c, 0x00310301u);
    writel(VP1 + 0x54, 0x000e02deu);
    writel(VOP + 0x074, 0x02de02deu);               /* line flag at the end of the active area */
    writel(VP1 + 0x50, 0x02ed0003u);
    writel(VP1 + 0x30, 0x018f0004u);
    writel(VP1 + 0x34, 0x00310301u);
    writel(VP1 + 0x38, 0x000e02deu);
    writel(VP1 + 0x3c, 0x10001000u);
    writel(VP1 + 0x40, 0);
    writel(VP1 + 0x2c, BG_COLOR);
    writel(VOP + 0x000, CFG_DONE_VP1 | CFG_DONE_VP0);
    writel(VP1 + 0x00, 0x00010000u);                /* RGB888 out, out of standby: scanning */
    return 0;
}

/* VP0 -> HDMI, 1280x720p60 (rk_hdmi.h), both syncs positive; VP1 in standby.
 * The timing registers are the panel's, with the numbers of CEA mode 4. */
static void vop_init_ext(void)
{
    vop_common(1);
    uint32_t v = readl(VOP + 0x028);
    v = (v & ~((3u << 10) | (1u << 4) | (3u << 16))) | (1u << 1);  /* HDMI on VP0, MIPI0 off */
    writel(VOP + 0x028, v);
    v = readl(VOP + 0x030);
    writel(VOP + 0x030, (v & ~0xf0u) | 0x30u);      /* HDMI pins: hsync and vsync positive */
    uint32_t hs = HDMI_HTOTAL - (HDMI_W + HDMI_HFRONT), he = hs + HDMI_W;
    uint32_t vs = HDMI_VTOTAL - (HDMI_H + HDMI_VFRONT), ve = vs + HDMI_H;
    writel(VP0 + 0x04, 0);
    writel(VP0 + 0x08, 0);
    writel(VP0 + 0x48, HDMI_HTOTAL << 16 | HDMI_HSYNC);
    writel(VP0 + 0x4c, hs << 16 | he);
    writel(VP0 + 0x54, vs << 16 | ve);
    writel(VOP + 0x070, ve << 16 | ve);
    writel(VP0 + 0x50, HDMI_VTOTAL << 16 | HDMI_VSYNC);
    writel(VP0 + 0x30, (40 + HDMI_W / 2 - 1) << 16 | HDMI_HSYNC);   /* pre-scan: bg delay 40 + half a line */
    writel(VP0 + 0x34, hs << 16 | he);
    writel(VP0 + 0x38, vs << 16 | ve);
    writel(VP0 + 0x3c, 0x10001000u);
    writel(VP0 + 0x40, 0);
    writel(VP0 + 0x2c, BG_COLOR);
    writel(VP1 + 0x00, 1u << 31);                   /* the panel's port in standby */
    writel(VOP + 0x000, CFG_DONE_VP1 | CFG_DONE_VP0);
    const char *sw = config_get("hdmi_swap");       /* red and green swapped on the screen: 1 */
    writel(VP0 + 0x00, sw && sw[0] == '1' ? 0x00010000u : 0);   /* RGB888 out, scanning */
    d.ow = HDMI_W;
    d.oh = HDMI_H;
    const char *org = config_get("hdmi_origin");    /* 1: window positions count from the sync */
    d.xoff = org && org[0] == '1' ? hs : 0;
    d.yoff = org && org[0] == '1' ? vs : 0;
}

static uint16_t scale_factor(uint32_t src, uint32_t dst)
{
    if (src == dst)
        return 0;
    uint32_t shift = src > dst ? 12 : 16;
    return (uint16_t)((((src - 1) << shift) + dst - 2) / (dst - 1) - 1);
}

/* Esmart0: the image scaled by the mode (up: nearest neighbour or smooth;
 * down when bigger than the screen), centred (rockchip_drm_vop2.c
 * vop2_setup_scale). "As big as fits" is worked out here, against the
 * screen in use, so a cable plugged in later enlarges it. */
static void window(const plat_mode_t *m, uintptr_t addr)
{
    uint32_t w = m->w, h = m->h, dw = m->out_w, dh = m->out_h;
    if (m->scale == 0) {
        if ((uint64_t)w * d.oh <= (uint64_t)h * d.ow) {
            dh = d.oh;
            dw = w * d.oh / h;
        } else {
            dw = d.ow;
            dh = h * d.ow / w;
        }
    }
    if (dw > d.ow)
        dw = d.ow;
    if (dh > d.oh)
        dh = d.oh;
    if (w > dw && (dw & 1))
        dw--;                                       /* even when scaling down */
    uint32_t x = (d.ow - dw) / 2 + d.xoff, y = (d.oh - dh) / 2 + d.yoff;
    /* a vertical shrink by 2 or 4 first drops lines (GT2, GT4) */
    uint32_t sh = h, gt = 0;
    if (h >= 4 * dh) {
        gt = 2;
        sh = h >> 2;
    } else if (h >= 2 * dh) {
        gt = 1;
        sh = h >> 1;
    }
    /* modes: 0 none, 1 up, 2 down; filters up: 0 nearest, 2 bicubic (hor)
     * or 1 bilinear (ver); down: 1 bilinear */
    uint32_t hm = w < dw ? 1 : w > dw ? 2 : 0, vm = sh < dh ? 1 : sh > dh ? 2 : 0;
    uint32_t hf = hm == 1 ? (m->smooth ? 2 : 0) : hm == 2 ? 1 : 0;
    uint32_t vf = vm == 1 ? (m->smooth ? 1 : 0) : vm == 2 ? 1 : 0;
    writel(ESMART0 + 0x00, 0);
    writel(ESMART0 + 0x04, readl(ESMART0 + 0x04) & ~(1u << 31));
    writel(ESMART0 + 0x1c, m->pitch / 4);           /* stride in words */
    writel(ESMART0 + 0x14, (uint32_t)addr);
    writel(ESMART0 + 0x34, (uint32_t)scale_factor(sh, dh) << 16 | scale_factor(w, dw));
    writel(ESMART0 + 0x30, hm | hf << 2 | vm << 4 | vf << 6);
    writel(ESMART0 + 0xd0, 0);
    writel(ESMART0 + 0x20, (h - 1) << 16 | (w - 1));
    writel(ESMART0 + 0x24, (dh - 1) << 16 | (dw - 1));
    writel(ESMART0 + 0x28, y << 16 | x);
    writel(ESMART0 + 0x10, (m->depth == 16 ? 0x00001005u : 0x00000001u) | gt << 8);
    writel(VOP + 0x000, CFG_DONE);
    d.out_w = dw;
    d.out_h = dh;
}

/* --- backlight: PWM4 on GPIO0_C3, 25 us period from the 24 MHz crystal --- */

static void backlight(unsigned percent)
{
    writel(CRU + 0x220, 0x03000100u);               /* clk_pwm1 = xin24m */
    writel(CRU + 0x37c, 0x0c000000u);               /* pclk_pwm1, clk_pwm1 on */
    writel(PMUGRF + 0x10, 0xf0001000u);             /* GPIO0_C3 = pwm4 */
    writel(PWM4 + 0x0c, 0);
    writel(PWM4 + 0x04, 600);
    writel(PWM4 + 0x08, 600 * percent / 100);
    writel(PWM4 + 0x0c, 0x0b);                      /* enable, continuous, duty high */
}

/* The backlight's pin back to a plain GPIO, low, and the panel off (display
 * off and sleep in if its link is up, then reset, supply off, 300 ms). The
 * next start finds it as after power-on. */
static void panel_off(int link_up)
{
    if (link_up)
        writel(PWM4 + 0x0c, 0);                     /* (its clock is on) */
    writel(PMUGRF + 0x10, 0xf0000000u);             /* GPIO0_C3 = gpio */
    rk_gpio_output(rk_pin(0, 'C', 3), 0);
    rk_dsi_off(link_up);
}

/* the DSI link and the panel, the VOP scanning for it; 0 or the rk_dsi_init error */
static int panel_on(void)
{
    d.ow = PANEL_W;
    d.oh = PANEL_H;
    d.xoff = d.yoff = 0;
    d.ext = 0;
    vop_init();
    timer_delay_ms(20);
    LOG("; ");
    char dsi[200];
    rgb30_display_stage("DSI link and panel starting");
    int r = rk_dsi_init(dsi, sizeof dsi);
    LOG("%s", dsi);
    rgb30_display_stage(r ? "panel failed" : "panel on");
    return r;
}

/* The external screen instead of the panel: panel off, 74.25 MHz pixel clock,
 * VP0 scanning, then the transmitter (the PHY locks only with pixels coming).
 * 0, or -1 with everything left off. */
static int ext_on(void)
{
    kprintf("hdmi: ext_on starting\n");
    rk_hdmi_clocks();
    panel_off(d.up && !d.ext);
    int r = hdmi_clocks();
    kprintf("hdmi: hdmi_clocks returned %d\n", r);
    if (r == 0) {
        vop_init_ext();
        r = rk_hdmi_start();
        kprintf("hdmi: rk_hdmi_start returned %d\n", r);
    }
    if (r) {
        rk_hdmi_stop();
        writel(VP0 + 0x00, 1u << 31);
        writel(VOP + 0x000, CFG_DONE_VP0);
        return -1;
    }
    d.ext = 1;
    return 0;
}

int plat_display_init(const plat_mode_t *m, uintptr_t addr)
{
    if (d.failed)
        return -1;
    d.mode = *m;
    d.addr = addr;
    if (!d.up) {
        d.pos = 0;
        /* the LEDs say which step a hang is in (docs/RGB30.md): both
         * on in the clocks, green alone in the VOP, none in the DSI
         * and panel; red alone before or after */
        int r = pd_vo_on();
        if (r) {
            LOG("power domain VO stuck (%d)", r);
            d.failed = 1;
            return -1;
        }
        plat_led(1, 1);
        rk_hdmi_clocks();
        const char *off = config_get("hdmi");
        int ext = 0;
        int hpd = rk_hdmi_hpd();
        kprintf("hdmi: boot check hpd=%d (rd=%02x, config hdmi=%s)\n", hpd, rk_hdmi_phy_stat(), off ? off : "unset");
        if ((off && !strcmp(off, "force")) || (!(off && off[0] == '0') && hpd)) {
            /* a cable that stays for 30 ms is a cable (the pin can glitch) */
            int ok = 1;
            if (!(off && !strcmp(off, "force"))) {
                for (int i = 0; i < 4 && ok; i++) {
                    ok = rk_hdmi_hpd();
                    timer_delay_ms(10);
                }
            }
            if (ok) {
                rgb30_display_stage("HDMI starting");
                ext = ext_on() == 0;
                LOG("HDMI %s", ext ? "1280x720" : "FAILED");
                d.noretry = !ext;
                rgb30_display_stage(ext ? "HDMI on" : "HDMI failed");
            }
        }
        if (ext) {
            plat_led(0, 1);
            d.up = 1;
            window(m, addr);
            d.problem = 0;
            return 0;
        }
        clocks_on();
        plat_led(1, 0);
        r = panel_on();
        plat_led(0, 1);
        d.up = 1;
        window(m, addr);
        timer_delay_ms(20);
        backlight(80);
        uint32_t st = readl(VOP + 0x0bc);
        if (st & (1u << 4))
            LOG("; underflow");
        /* even when the DSI link reports errors the panel may show the
         * picture: carry on, the report says what went wrong */
        d.problem = r != 0;
        return 0;
    }
    window(m, addr);
    return 0;
}

/* The screen follows the cable. Every 250 ms the HPD pin is read; two reads
 * in a row that disagree with the screen in use change it: HDMI plugged in,
 * panel off and the external screen on; unplugged, the other way round.
 * hdmi=0 in bm/config.txt keeps the panel. A cable that fails to start is
 * not tried again until it has been unplugged. */
static void hotplug(void)
{
    uint32_t now = timer_ticks();
    if (!d.up || now - d.last_poll < 250000u)
        return;
    d.last_poll = now;
    const char *off = config_get("hdmi");
    int hpd = rk_hdmi_hpd();
    int want = ((off && !strcmp(off, "force")) || (!(off && off[0] == '0') && hpd));
    static int last_hpd = -1;
    if (hpd != last_hpd) {
        last_hpd = hpd;
        kprintf("hdmi: hpd changed to %d (rd=%02x, want=%d, ext=%d, noretry=%d)\n",
                hpd, rk_hdmi_phy_stat(), want, d.ext, d.noretry);
    }
    if (!want)
        d.noretry = 0;
    if (want == d.ext || (want && d.noretry)) {
        d.streak = 0;
        return;
    }
    if (++d.streak < 2)
        return;
    d.streak = 0;
    int saved = d.pos;
    d.pos = (int)sizeof d.info;                     /* the boot report stays as it was */
    if (want) {
        kprintf("hdmi: hotplug switching to external 720p...\n");
        d.noretry = ext_on() != 0;
        if (d.noretry) {
            kprintf("hdmi: ext_on failed, restoring panel\n");
            clocks_on();                            /* VPLL back to the panel's */
            panel_on();                             /* the panel again, as it was */
        } else {
            kprintf("hdmi: external 720p active!\n");
        }
    } else {
        kprintf("hdmi: hotplug switching back to panel...\n");
        rk_hdmi_stop();
        writel(VP0 + 0x00, 1u << 31);
        writel(VOP + 0x000, CFG_DONE_VP0);
        d.ext = 0;
        clocks_on();
        panel_on();
    }
    if (!d.ext)
        backlight(80);
    d.pos = saved;
    window(&d.mode, d.addr);
}

void plat_display_show(uintptr_t addr)
{
    if (!d.up)
        return;
    d.addr = addr;
    writel(ESMART0 + 0x14, (uint32_t)addr);
    writel(VOP + 0x000, CFG_DONE);
    hotplug();
}

/* The pending address is taken at the next frame start: the cfg_done bit
 * of the port reads 1 until then. */
int plat_display_wait_vsync(void)
{
    if (!d.up)
        return 0;
    int ok = poll_bits(VOP + 0x000, d.ext ? 1u << 0 : 1u << 1, 0, 40000) == 0;
    hotplug();
    return ok;
}

int plat_display_problem(void)
{
    return d.failed || d.problem;
}

const char *plat_display_info(void)
{
    return d.pos ? d.info : "display not started";
}

/* Before a restart: the backlight's pin back to a plain GPIO, low, and the
 * panel off. A restart of the chip alone (PSCI) leaves the PMU's GPIO as
 * they were, so the panel would stay powered and lit into the next start;
 * off, the next start finds it as after power-on. */
void plat_display_off(void)
{
    if (d.ext) {
        rk_hdmi_stop();
        writel(VP0 + 0x00, 1u << 31);
        writel(VOP + 0x000, CFG_DONE_VP0);
    }
    panel_off(d.up && !d.ext);
    d.up = 0;
    d.ext = 0;
}
#endif
