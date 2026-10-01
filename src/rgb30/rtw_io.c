/*
 * RTL8821CS register and FIFO access over SDIO function 1 (rtw88 sdio.c):
 * MAC registers at bus address 0x10000 | offset (device id 8), SDIO local
 * registers at the offset itself (device id 0), TX FIFOs at 0x8000..0xe000
 * + length in dwords, RX FIFO at 0xe000 | (sequence & 3). 8 and 16-bit
 * accesses are CMD52s; 32-bit aligned ones a 4-byte CMD53 once the chip is
 * powered (before that, 4 CMD52s), as rtw88 does on a non-UHS bus.
 */
#ifdef PLAT_RK3566
#include "rtw.h"
#include "rk_sdio.h"
#include "drivers/timer.h"

#include <string.h>

#define WLAN_IOREG  0x10000u

static int poweron, errors;
static uint8_t rx_seq;

void rtw_set_poweron(int on)    { poweron = on; }
int rtw_io_errors(void)         { return errors; }

static uint8_t b_read(uint32_t bus)
{
    int v = sdio_read8(bus);
    if (v < 0) {
        errors++;
        return 0xff;
    }
    return (uint8_t)v;
}

static void b_write(uint32_t bus, uint8_t v)
{
    if (sdio_write8(bus, v) < 0)
        errors++;
}

static uint32_t bus_addr(uint32_t addr)
{
    return WLAN_IOREG | (addr & 0xffff);
}

uint8_t rtw_r8(uint32_t addr)            { return b_read(bus_addr(addr)); }
void rtw_w8(uint32_t addr, uint8_t v)    { b_write(bus_addr(addr), v); }

uint16_t rtw_r16(uint32_t addr)
{
    uint32_t a = bus_addr(addr);
    return (uint16_t)(b_read(a) | b_read(a + 1) << 8);
}

void rtw_w16(uint32_t addr, uint16_t v)
{
    uint32_t a = bus_addr(addr);
    b_write(a, (uint8_t)v);
    b_write(a + 1, (uint8_t)(v >> 8));
}

static uint32_t r32_bus(uint32_t a)
{
    if (poweron && !(a & 3)) {
        uint32_t v;
        if (sdio_cmd53(0, a, &v, 4) == 0)
            return v;
        errors++;
        return 0xffffffffu;
    }
    return b_read(a) | b_read(a + 1) << 8 | b_read(a + 2) << 16 | (uint32_t)b_read(a + 3) << 24;
}

static void w32_bus(uint32_t a, uint32_t v)
{
    if (poweron && !(a & 3)) {
        if (sdio_cmd53(1, a, &v, 4) != 0)
            errors++;
        return;
    }
    for (int i = 0; i < 4; i++)
        b_write(a + i, (uint8_t)(v >> (8 * i)));
}

uint32_t rtw_r32(uint32_t addr)          { return r32_bus(bus_addr(addr)); }
void rtw_w32(uint32_t addr, uint32_t v)  { w32_bus(bus_addr(addr), v); }

void rtw_w32_mask(uint32_t addr, uint32_t mask, uint32_t v)
{
    unsigned shift = (unsigned)__builtin_ctz(mask);
    rtw_w32(addr, (rtw_r32(addr) & ~mask) | ((v << shift) & mask));
}

uint8_t rtw_local_r8(uint32_t off)              { return b_read(off & 0xfff); }
void rtw_local_w8(uint32_t off, uint8_t v)      { b_write(off & 0xfff, v); }
uint32_t rtw_local_r32(uint32_t off)            { return r32_bus(off & 0xfff); }
void rtw_local_w32(uint32_t off, uint32_t v)    { w32_bus(off & 0xfff, v); }

int rtw_poll32(uint32_t addr, uint32_t mask, uint32_t want)
{
    for (int i = 0; i < 1000; i++) {
        if ((rtw_r32(addr) & mask) == want)
            return 1;
        timer_delay_us(10);
    }
    return 0;
}

/* --- TX --- */

static void put_bits(uint8_t *d, unsigned word, unsigned lo, unsigned hi, uint32_t v)
{
    uint32_t w = d[word * 4] | d[word * 4 + 1] << 8 | d[word * 4 + 2] << 16 | (uint32_t)d[word * 4 + 3] << 24;
    uint32_t mask = (hi == 31 ? 0xffffffffu : (1u << (hi + 1)) - 1) & ~((1u << lo) - 1);
    w = (w & ~mask) | ((v << lo) & mask);
    for (int i = 0; i < 4; i++)
        d[word * 4 + i] = (uint8_t)(w >> (8 * i));
}

void rtw_fill_txdesc(uint8_t *d, const rtw_txinfo_t *t)
{
    memset(d, 0, TX_DESC_SIZE);
    put_bits(d, 0, 0, 15, t->pkt_size);
    put_bits(d, 0, 16, 23, t->offset);
    put_bits(d, 0, 24, 24, t->bmc);
    put_bits(d, 0, 26, 26, 1);                      /* LS */
    put_bits(d, 0, 31, 31, t->dis_qselseq);
    put_bits(d, 1, 0, 7, t->macid);
    put_bits(d, 1, 8, 12, t->qsel);
    put_bits(d, 1, 16, 20, t->rate_id);
    put_bits(d, 1, 22, 23, t->sec_type);
    put_bits(d, 1, 29, 29, t->qsel == QSEL_HIGH);   /* MORE_DATA */
    put_bits(d, 3, 6, 7, 0);                        /* HW_SSN_SEL */
    put_bits(d, 3, 8, 8, t->use_rate);
    put_bits(d, 3, 10, 10, t->dis_rate_fb);
    put_bits(d, 4, 0, 6, t->rate);
    put_bits(d, 8, 15, 15, t->en_hwseq);
    put_bits(d, 9, 12, 23, t->seq);
    /* checksum: XOR of the first 16 little-endian 16-bit words, in w7 */
    put_bits(d, 7, 0, 15, 0);
    uint16_t x = 0;
    for (int i = 0; i < 16; i++)
        x ^= (uint16_t)(d[2 * i] | d[2 * i + 1] << 8);
    put_bits(d, 7, 0, 15, x);
}

int rtw_free_pages(uint32_t fifo)
{
    uint32_t f0 = rtw_local_r32(SDIO_FREE_TXPG), f1 = rtw_local_r32(SDIO_FREE_TXPG + 4),
             f2 = rtw_local_r32(SDIO_FREE_TXPG + 8);
    uint32_t pub = (f1 >> 16) & 0xfff, own;
    switch (fifo) {
    case FIFO_HIGH:   own = f0 & 0xfff; break;
    case FIFO_NORMAL: own = (f0 >> 16) & 0xfff; break;
    case FIFO_LOW:    own = f1 & 0xfff; break;
    default:          own = f2 & 0xfff; break;
    }
    return (int)(own + pub);
}

int rtw_write_port(uint32_t fifo, uint8_t *buf, uint32_t len)
{
    uint32_t addr = fifo + ((len + 3) / 4);         /* length in dwords */
    uint32_t xfer = (len + 3) & ~3u;
    if (xfer > 512)
        xfer = (xfer + 511) & ~511u;
    return sdio_cmd53(1, addr, buf, xfer) == 0 ? 0 : -1;
}

/* --- RX --- */

uint32_t rtw_rx_len(void)
{
    return rtw_local_r32(SDIO_RX0_REQ_LEN);
}

int rtw_read_port(uint8_t *buf, uint32_t len)
{
    uint32_t xfer = (len + 3) & ~3u;
    if (xfer > 512)
        xfer = (xfer + 511) & ~511u;
    return sdio_cmd53(0, FIFO_EXTRA | (rx_seq++ & 3), buf, xfer) == 0 ? 0 : -1;
}
#endif
