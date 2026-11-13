/*
 * RTL8821CS bring-up (rtw88 mac.c, fw.c, efuse.c, rtw8821c.c): power on,
 * firmware download (reserved page + internal DMA, three sections with
 * their checksums), efuse (MAC address, RFE type, crystal), and the
 * hardware feature report the firmware gives back.
 */
#ifdef PLAT_RK3566
#include "rtw.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

rtw_chip_t rtw;

/* --- power on --- */

/* card_enable_flow_8821c for SDIO (rtw8821c.c: trans_carddis_to_cardemu,
 * trans_cardemu_to_act): {sdio local?, offset, op, mask, value}
 * op: 'w' write, 'p' poll, 'd' delay in ms */
static const struct { uint8_t local; uint16_t off; char op; uint8_t mask, val; } pwr_on[] = {
    { 1, 0x86, 'w', 0x01, 0x00 },
    { 1, 0x86, 'p', 0x02, 0x02 },
    { 0, 0x05, 'w', 0x98, 0x00 },
    { 0, 0x20, 'w', 0x01, 0x01 },
    { 0, 0x00, 'd', 1, 0 },
    { 0, 0x00, 'w', 0x20, 0x00 },
    { 0, 0x05, 'w', 0x1c, 0x00 },
    { 0, 0x06, 'p', 0x02, 0x02 },
    { 0, 0x06, 'w', 0x01, 0x01 },
    { 0, 0x05, 'w', 0x80, 0x00 },
    { 0, 0x05, 'w', 0x18, 0x00 },
    { 0, 0x05, 'w', 0x01, 0x01 },
    { 0, 0x05, 'p', 0x01, 0x00 },
    { 0, 0x20, 'w', 0x08, 0x08 },
    { 0, 0x7c, 'w', 0x02, 0x00 },
};

static int run_pwr_seq(char *err, unsigned errlen)
{
    for (unsigned i = 0; i < sizeof pwr_on / sizeof pwr_on[0]; i++) {
        uint8_t v;
        switch (pwr_on[i].op) {
        case 'w':
            v = pwr_on[i].local ? rtw_local_r8(pwr_on[i].off) : rtw_r8(pwr_on[i].off);
            v = (uint8_t)((v & ~pwr_on[i].mask) | (pwr_on[i].val & pwr_on[i].mask));
            if (pwr_on[i].local)
                rtw_local_w8(pwr_on[i].off, v);
            else
                rtw_w8(pwr_on[i].off, v);
            break;
        case 'p': {
            int ok = 0;
            for (int n = 0; n < 20000 && !ok; n++) {
                v = pwr_on[i].local ? rtw_local_r8(pwr_on[i].off) : rtw_r8(pwr_on[i].off);
                ok = (v & pwr_on[i].mask) == (pwr_on[i].val & pwr_on[i].mask);
                if (!ok)
                    timer_delay_us(50);
            }
            if (!ok) {
                ksnprintf(err, errlen, "power sequence: step %u (reg %02x) does not settle", i,
                          pwr_on[i].off);
                return -1;
            }
            break;
        }
        case 'd':
            timer_delay_ms(pwr_on[i].mask);
            break;
        }
    }
    return 0;
}

static int power_on(char *err, unsigned errlen)
{
    /* pre_system_cfg (WCPU 3081, SDIO) */
    rtw_w8(REG_RSV_CTRL, 0);
    rtw_local_w8(SDIO_HSUS_CTRL, rtw_local_r8(SDIO_HSUS_CTRL) & ~1u);
    int ok = 0;
    for (int n = 0; n < 20000 && !ok; n++) {
        ok = rtw_local_r8(SDIO_HSUS_CTRL) & 2;
        if (!ok)
            timer_delay_us(20);
    }
    if (!ok) {
        ksnprintf(err, errlen, "the SDIO interface does not resume (local 0x86 = %02x)",
                  rtw_local_r8(SDIO_HSUS_CTRL));
        return -1;
    }
    rtw_clr8(REG_HCI_OPT_CTRL + 2, 1u << 2);        /* no SDIO 3.0 pads */
    rtw_set32(REG_PAD_CTRL1, (1u << 29) | (1u << 28));
    rtw_clr32(REG_LED_CFG, (1u << 25) | (1u << 26));
    rtw_set32(REG_GPIO_MUXCFG, 1u << 2);
    rtw_clr8(REG_SYS_FUNC_EN, 3);                   /* BB off */
    rtw_clr8(REG_RF_CTRL, 7);                       /* RF off */
    rtw_clr32(REG_WLRF1, 7u << 24);

    /* power switch: firmware still running? then wake it; 0xea in CR = off */
    if (rtw_r16(REG_MCUFW_CTRL) == 0xc078) {
        uint8_t rpwm = rtw_local_r8(SDIO_HRPWM1);
        rtw_local_w8(SDIO_HRPWM1, (uint8_t)((rpwm ^ 0x80) & 0x80));
    }
    uint8_t cr = rtw_r8(REG_CR);
    if (cr != 0xea)
        kprintf("wifi: the chip was already on (CR %02x): carrying on\n", cr);
    uint32_t himr = rtw_local_r32(SDIO_HIMR);
    rtw_local_w32(SDIO_HIMR, 0);
    if (run_pwr_seq(err, errlen))
        return -1;
    rtw_local_w32(SDIO_HIMR, himr);
    rtw_set_poweron(1);

    /* init_system_cfg (3081) */
    rtw_set32(REG_CPU_DMEM_CON, (1u << 16) | (1u << 8));    /* platform reset off, DDMA on */
    rtw_set8(REG_SYS_FUNC_EN + 1, 0xd8);
    rtw_w8(REG_CR_EXT + 3, (uint8_t)((rtw_r8(REG_CR_EXT + 3) & 0xf0) | 0x0c));
    uint32_t fw_ctrl = rtw_r32(REG_MCUFW_CTRL);
    if (fw_ctrl & (1u << 20)) {                     /* boot from flash: off */
        rtw_w32(REG_MCUFW_CTRL, fw_ctrl & ~(1u << 20));
        rtw_clr32(REG_GPIO_MUXCFG, 1u << 19);
    }
    rtw.powered = 1;
    return 0;
}

/* --- firmware --- */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

#define FW_HDR_SIZE     64
#define FW_CHKSUM_SIZE  8

static uint8_t page_buf[TX_DESC_SIZE + 0x1000 + 512] __attribute__((aligned(64)));

/* one chunk into the reserved page at page 0, through the beacon queue */
static int write_rsvd_page(const uint8_t *data, uint32_t size)
{
    uint8_t bcn_ctrl = rtw_r8(REG_BCN_CTRL);
    rtw_w16(REG_FIFOPAGE_CTRL_2, 0 | (1u << 15));
    uint8_t cr1 = rtw_r8(REG_CR + 1);
    rtw_w8(REG_CR + 1, cr1 | 1);                    /* ENSWBCN */
    rtw_w8(REG_BCN_CTRL, (uint8_t)((bcn_ctrl & ~(1u << 3)) | (1u << 4)));

    rtw_txinfo_t t = { 0 };
    t.pkt_size = size;
    t.offset = TX_DESC_SIZE;
    t.qsel = QSEL_BEACON;
    t.rate = 0;                                     /* 1M */
    t.rate_id = 8;                                  /* B_20M */
    t.use_rate = 1;
    t.dis_rate_fb = 1;
    t.dis_qselseq = 1;
    t.en_hwseq = 1;
    t.bmc = size >= 10 && (data[4] & 1);            /* rtw88 reads an "addr1" there */
    memcpy(page_buf + TX_DESC_SIZE, data, size);
    rtw_fill_txdesc(page_buf, &t);
    int r = rtw_write_port(FIFO_HIGH, page_buf, TX_DESC_SIZE + size);
    if (r == 0 && !rtw_poll32(REG_FIFOPAGE_CTRL_2, 1u << 15, 1u << 15))
        r = -2;
    rtw_w16(REG_FIFOPAGE_CTRL_2, 0 | (1u << 15));
    rtw_w8(REG_BCN_CTRL, bcn_ctrl);
    rtw_w8(REG_CR + 1, cr1);
    return r;
}

static int ddma(uint32_t src, uint32_t dst, uint32_t len, int first)
{
    if (!rtw_poll32(REG_DDMA_CH0CTRL, 1u << 31, 0))
        return -1;
    uint32_t ctrl = (1u << 29) | (1u << 31) | (len & 0x3ffff) | (first ? 0 : 1u << 24);
    rtw_w32(REG_DDMA_CH0SA, src);
    rtw_w32(REG_DDMA_CH0DA, dst);
    rtw_w32(REG_DDMA_CH0CTRL, ctrl);
    return rtw_poll32(REG_DDMA_CH0CTRL, 1u << 31, 0) ? 0 : -1;
}

static int to_mem(const uint8_t *data, uint32_t dst, uint32_t size, char *err, unsigned errlen)
{
    rtw_set32(REG_DDMA_CH0CTRL, 1u << 25);          /* reset checksum status */
    uint32_t off = 0;
    int first = 1;
    while (off < size) {
        uint32_t n = size - off > 0x1000 ? 0x1000 : size - off;
        int r = write_rsvd_page(data + off, n);
        if (r) {
            ksnprintf(err, errlen, "reserved page write failed (%d) at %08lx+%lx", r, dst, off);
            return -1;
        }
        if (ddma(0x18780000u + TX_DESC_SIZE, dst + off, n, first)) {
            ksnprintf(err, errlen, "internal DMA stuck at %08lx+%lx", dst, off);
            return -1;
        }
        first = 0;
        off += n;
    }
    uint8_t fw_ctrl = rtw_r8(REG_MCUFW_CTRL);
    int dmem = dst >= 0x200000;
    if (rtw_r32(REG_DDMA_CH0CTRL) & (1u << 27)) {
        rtw_w8(REG_MCUFW_CTRL, (uint8_t)(fw_ctrl | (dmem ? 0x20 : 0x08)));
        ksnprintf(err, errlen, "firmware checksum wrong (section at %08lx)", dst);
        return -1;
    }
    rtw_w8(REG_MCUFW_CTRL, (uint8_t)(fw_ctrl | (dmem ? 0x60 : 0x18)));
    return 0;
}

static int ltecoex_read(uint32_t off, uint32_t *v)
{
    if (!rtw_poll32(0x1700, 1u << 29, 1u << 29))
        return -1;
    rtw_w32(0x1700, 0x800f0000u | off);
    *v = rtw_r32(0x1708);
    return 0;
}

static int ltecoex_write(uint32_t off, uint32_t v)
{
    if (!rtw_poll32(0x1700, 1u << 29, 1u << 29))
        return -1;
    rtw_w32(0x1704, v);
    rtw_w32(0x1700, 0xc00f0000u | off);
    return 0;
}

static int download_firmware(const uint8_t *fw, size_t len, char *err, unsigned errlen)
{
    if (len < FW_HDR_SIZE || (fw[0] | fw[1] << 8) != 0x8821) {
        ksnprintf(err, errlen, "rtw8821c_fw.bin: not the 8821C firmware");
        return -1;
    }
    uint32_t dmem = le32(fw + 0x24) + FW_CHKSUM_SIZE, imem = le32(fw + 0x30) + FW_CHKSUM_SIZE;
    uint32_t emem = (fw[0x18] & 0x10) ? le32(fw + 0x34) + FW_CHKSUM_SIZE : 0;   /* mem_usage */
    if (FW_HDR_SIZE + dmem + imem + emem != len) {
        ksnprintf(err, errlen, "rtw8821c_fw.bin: sizes do not add up (%lu bytes)", (uint32_t)len);
        return -1;
    }
    rtw.fw_major = fw[4] | fw[5] << 8;
    rtw.fw_minor = fw[6];
    rtw.fw_patch = fw[7];

    uint32_t lte = 0;
    int lte_ok = ltecoex_read(0x38, &lte) == 0;
    /* CPU off */
    rtw_clr8(REG_SYS_FUNC_EN + 1, 1u << 2);
    rtw_clr8(REG_RSV_CTRL + 1, 1u << 0);
    /* register backup, set-up for the download */
    uint8_t b_pq = rtw_r8(REG_TXDMA_PQ_MAP + 1), b_cr = rtw_r8(REG_CR);
    uint16_t b_info = rtw_r16(REG_FIFOPAGE_INFO_1);
    uint32_t b_rqpn = rtw_r32(REG_RQPN_CTRL_2) | (1u << 31);
    uint8_t b_bcn = rtw_r8(REG_BCN_CTRL);
    rtw_w8(REG_TXDMA_PQ_MAP + 1, 3u << 6);
    rtw_w8(REG_CR, 0x05);
    rtw_w32(REG_H2CQ_CSR, 1u << 31);
    rtw_w16(REG_FIFOPAGE_INFO_1, 0x200);
    rtw_w32(REG_RQPN_CTRL_2, b_rqpn);
    (void)rtw_local_r32(SDIO_FREE_TXPG);
    rtw_w8(REG_BCN_CTRL, (uint8_t)((b_bcn & ~(1u << 3)) | (1u << 4)));
    /* reset the WLAN CPU platform */
    rtw_clr8(REG_CPU_DMEM_CON + 2, 1u << 0);
    rtw_clr8(REG_SYS_CLK_CTRL + 1, 1u << 6);
    rtw_set8(REG_CPU_DMEM_CON + 2, 1u << 0);
    rtw_set8(REG_SYS_CLK_CTRL + 1, 1u << 6);
    /* the three sections */
    rtw_w16(REG_MCUFW_CTRL, (uint16_t)((rtw_r16(REG_MCUFW_CTRL) & 0x3800) | 1));
    const uint8_t *p = fw + FW_HDR_SIZE;
    int r = to_mem(p, le32(fw + 0x20) & ~(1u << 31), dmem, err, errlen);
    p += dmem;
    if (!r)
        r = to_mem(p, le32(fw + 0x3c) & ~(1u << 31), imem, err, errlen);
    p += imem;
    if (!r && emem)
        r = to_mem(p, le32(fw + 0x38) & ~(1u << 31), emem, err, errlen);
    if (r) {
        rtw_clr8(REG_MCUFW_CTRL, 1);
        rtw_set8(REG_SYS_FUNC_EN + 1, 1u << 2);
        return -1;
    }
    /* restore, end of download, CPU on */
    rtw_w8(REG_TXDMA_PQ_MAP + 1, b_pq);
    rtw_w8(REG_CR, b_cr);
    rtw_w32(REG_H2CQ_CSR, 1u << 31);
    rtw_w16(REG_FIFOPAGE_INFO_1, b_info);
    rtw_w32(REG_RQPN_CTRL_2, b_rqpn);
    rtw_w8(REG_BCN_CTRL, b_bcn);
    rtw_w32(REG_TXDMA_STATUS, 1u << 2);
    uint16_t fw_ctrl = rtw_r16(REG_MCUFW_CTRL);
    if ((fw_ctrl & 0x50) == 0x50)
        rtw_w16(REG_MCUFW_CTRL, (uint16_t)((fw_ctrl | (1u << 14)) & ~1u));
    rtw_set8(REG_RSV_CTRL + 1, 1u << 0);
    rtw_set8(REG_SYS_FUNC_EN + 1, 1u << 2);
    if (lte_ok)
        ltecoex_write(0x38, lte);
    /* the firmware says it is ready */
    for (int i = 0; i < 1000; i++) {
        if ((rtw_r32(REG_MCUFW_CTRL) & 0xcfff) == 0xc078) {
            rtw.fw_running = 1;
            return 0;
        }
        timer_delay_us(100);
    }
    uint32_t key = rtw_r32(REG_FW_DBG7) & 0xffffff00u;
    ksnprintf(err, errlen, "the firmware does not start (MCUFW_CTRL %08lx%s)", rtw_r32(REG_MCUFW_CTRL),
              key == 0xfaaaaa00u ? ", invalid key" : "");
    return -1;
}

/* --- efuse --- */

static int read_efuse(char *err, unsigned errlen)
{
    static uint8_t phy[512];
    rtw_w32_mask(REG_LDO_EFUSE_CTRL, 3u << 8, 0);   /* bank 0 */
    rtw_clr8(REG_LDO_EFUSE_CTRL + 3, 1u << 7);      /* 2.5 V LDO off */
    uint32_t ctl = rtw_r32(REG_EFUSE_CTRL);
    for (unsigned addr = 0; addr < 512; addr++) {
        ctl &= ~(0xffu | (0x3ffu << 8));
        ctl |= addr << 8;
        rtw_w32(REG_EFUSE_CTRL, ctl & ~(1u << 31));
        int n = 0;
        do {
            ctl = rtw_r32(REG_EFUSE_CTRL);
            if (++n > 10000) {
                ksnprintf(err, errlen, "efuse read stuck at %u", addr);
                return -1;
            }
        } while (!(ctl & (1u << 31)));
        phy[addr] = (uint8_t)ctl;
    }
    memset(rtw.efuse, 0xff, sizeof rtw.efuse);
    for (unsigned i = 0; i < 512 - 96; ) {
        uint8_t h1 = phy[i], h2 = phy[i + 1];
        if (h1 == 0xff || ((h1 & 0x1f) == 0xf && h2 == 0xff))
            break;
        unsigned blk, we;
        if ((h1 & 0x1f) == 0xf) {
            blk = ((h2 & 0xf0) >> 1) | ((h1 >> 5) & 7);
            we = h2 & 0xf;
            i += 2;
        } else {
            blk = h1 >> 4;
            we = h1 & 0xf;
            i += 1;
        }
        for (unsigned w = 0; w < 4; w++) {
            if ((we >> w) & 1)
                continue;
            unsigned log = blk * 8 + w * 2;
            if (i + 1 >= 512 - 96 || log + 1 >= sizeof rtw.efuse)
                break;
            rtw.efuse[log] = phy[i];
            rtw.efuse[log + 1] = phy[i + 1];
            i += 2;
        }
    }
    memcpy(rtw.mac, rtw.efuse + 0x11a, 6);
    rtw.rfe = rtw.efuse[0xca] & 0x1f;
    rtw.pkg = (rtw.efuse[0xca] >> 5) & 1;
    rtw.xtal = rtw.efuse[0xb9];
    rtw.thermal = rtw.efuse[0xba];
    rtw.swing_2g = rtw.efuse[0xc6];
    rtw.channel_plan = rtw.efuse[0xb8];
    int valid = (rtw.mac[0] & 1) == 0 && memcmp(rtw.mac, "\xff\xff\xff\xff\xff\xff", 6) != 0 &&
                memcmp(rtw.mac, "\0\0\0\0\0\0", 6) != 0;
    if (!valid) {                                   /* as Linux: a random local address */
        uint32_t t = (uint32_t)timer_ticks();
        uint8_t m[6] = { 0x02, 0xbb, 0x30, (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
        memcpy(rtw.mac, m, 6);
    }
    if (rtw.xtal == 0xff)                           /* as rtw88 */
        rtw.xtal = 0;
    if (rtw.swing_2g == 0xff)
        rtw.swing_2g = 0;
    return 0;
}

int rtw_chip_start(const uint8_t *fw, size_t len, char *err, unsigned errlen)
{
    memset(&rtw, 0, sizeof rtw);
    rtw_set_poweron(0);
    uint32_t cfg1 = rtw_r32(REG_SYS_CFG1);
    rtw.cut = (cfg1 >> 12) & 0xf;
    if (power_on(err, errlen))
        return -1;
    rtw_w8(REG_C2HEVT, 0xfd);                       /* ask for the hardware feature report */
    if (download_firmware(fw, len, err, errlen))
        return -1;
    if (read_efuse(err, errlen))
        return -1;
    for (int i = 0; i < 100 && rtw_r8(REG_C2HEVT) != 0x19; i++)
        timer_delay_ms(1);
    if (rtw_r8(REG_C2HEVT) == 0x19) {
        for (int i = 0; i < 4; i++)
            rtw.hw_feature[i] = rtw_r32(REG_C2HEVT + 2 + 4 * i);
        rtw_w8(REG_C2HEVT, 0);
    }
    return 0;
}
#endif
