/*
 * psx_bus.c: the memory map of the PlayStation and what hangs on it: the BIOS ROM, the interrupt controller, the three
 * timers, the DMA controller (channels 0..6, instant), the pad port, and the frame loop (263 lines of 2154 cycles; the
 * vertical blank at line 240). It also loads a PS-EXE (at the BIOS shell, or at once when there is no BIOS) and answers
 * the few BIOS calls a program makes when there is no BIOS (putchar, puts, printf). Original code.
 */
#include "psx_int.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t psx_bios[512 * 1024];
uint32_t I_STAT, I_MASK;
int psx_hooks;

static int has_bios;
static uint8_t *exe_buf;                 /* a PS-EXE waiting for the shell */
static size_t exe_len;
static int exe_pending;
static uint32_t (*clock_us)(void);
static uint32_t frames, unhandled, last_unhandled, dma_runs;
static uint32_t us_frame, us_cpu, us_gpu;
static char tty[2048];
static uint32_t tty_len;
static uint32_t mc[16];                  /* the memory control registers 1F801000.. */
static uint32_t ram_size_reg;

void psx_set_clock(uint32_t (*f)(void)) { clock_us = f; }
void psx_irq_raise(int n) { I_STAT |= 1u << n; }
void psx_dbg_unhandled(uint32_t a) { unhandled++; last_unhandled = a; }
const char *psx_tty(void) { return tty; }
const uint16_t *psx_vram(void) { return GP.vram; }
uint32_t psx_reg(int n) { return n < 32 ? C.r[n] : n == 32 ? C.pc : n == 33 ? C.hi : C.lo; }
uint32_t psx_peek32(uint32_t a) { return *(const uint32_t *)(psx_ram + (a & 0x1FFFFCu)); }

/* ------------------------------------------------------------------ timers */
typedef struct { uint32_t val, mode, target; uint32_t frac; int paused; } timer_t_;
static timer_t_ T[3];
static uint64_t t_last;                   /* C.cycles at the last sync */
static int hblank_in;
static int line_no;

static void timer_irq(int i)
{
    psx_irq_raise(IRQ_T0 + i);
}

static void timer_add(int i, uint32_t ticks)
{
    timer_t_ *t = &T[i];
    const uint32_t tgt = t->target;
    uint32_t v = t->val;
    while (ticks) {
        const uint32_t limit = ((t->mode & 8u) && v <= tgt) ? tgt + 1 : 0x10000u;   /* the value that wraps to 0 */
        const uint32_t room = limit - v;
        const uint32_t step = ticks < room ? ticks : room;
        const uint32_t nv = v + step;
        int hit = (v < tgt && nv >= tgt);
        if (limit == tgt + 1 && nv == limit && tgt == 0) hit = 1;
        ticks -= step;
        v = nv;
        if (nv == limit) {
            v = 0;
            if (limit == 0x10000u) {
                t->mode |= 0x1000u;
                if (t->mode & 0x20u) { t->mode &= ~0x400u; timer_irq(i); }
            }
        }
        if (hit) {
            t->mode |= 0x800u;
            if (t->mode & 0x10u) { t->mode &= ~0x400u; timer_irq(i); }
        }
    }
    t->val = v & 0xFFFFu;
}

static void timers_sync(void)
{
    const uint32_t cyc = (uint32_t)(C.cycles - t_last);
    t_last = C.cycles;
    if (!cyc) return;
    for (int i = 0; i < 3; i++) {
        timer_t_ *t = &T[i];
        const uint32_t src = (t->mode >> 8) & 3;
        uint32_t ticks = 0;
        if (i == 0 && (src & 1)) {                                  /* dot clock: 11/7 of the system clock, divided by the dot width */
            static const int div[5] = { 10, 8, 7, 5, 4 };           /* 256 320 368 512 640 */
            const int h = psx_gpu_hres();
            const int d = h == 256 ? div[0] : h == 320 ? div[1] : h == 368 ? div[2] : h == 512 ? div[3] : div[4];
            t->frac += cyc * 11u;
            ticks = t->frac / (7u * (uint32_t)d);
            t->frac -= ticks * (7u * (uint32_t)d);
        } else if (i == 1 && (src & 1)) {
            continue;                                              /* counts hblanks: see timers_hblank */
        } else if (i == 2 && (src & 2)) {
            t->frac += cyc;
            ticks = t->frac >> 3;
            t->frac &= 7u;
        } else if (i != 2 || (t->mode & 1) == 0 || ((t->mode >> 1) & 3) == 1 || ((t->mode >> 1) & 3) == 2) {
            ticks = cyc;
        }
        timer_add(i, ticks);
    }
}

static void timers_hblank(void)
{
    if (((T[1].mode >> 8) & 1) == 1) timer_add(1, 1);
}

static uint32_t timer_read(uint32_t reg)
{
    timers_sync();
    const int i = (int)(reg >> 4) & 3;
    if (i > 2) return 0;
    switch (reg & 15) {
    case 0: return T[i].val;
    case 4: { const uint32_t m = T[i].mode; T[i].mode &= ~0x1800u; return m; }
    case 8: return T[i].target;
    default: return 0;
    }
}

static void timer_write(uint32_t reg, uint32_t v)
{
    timers_sync();
    const int i = (int)(reg >> 4) & 3;
    if (i > 2) return;
    switch (reg & 15) {
    case 0: T[i].val = v & 0xFFFFu; break;
    case 4: T[i].mode = (v & 0x3FFu) | 0x400u; T[i].val = 0; T[i].frac = 0; break;
    case 8: T[i].target = v & 0xFFFFu; break;
    default: break;
    }
}

/* ------------------------------------------------------------------ DMA */
static uint32_t dma_madr[7], dma_bcr[7], dma_chcr[7], dpcr = 0x07654321u, dicr;

static void dicr_update(uint32_t flags)
{
    const uint32_t before = (dicr >> 31) & 1;
    dicr |= flags;
    const int master = ((dicr >> 15) & 1) || (((dicr >> 23) & 1) && (((dicr >> 16) & 0x7F) & ((dicr >> 24) & 0x7F)));
    dicr = (dicr & 0x7FFFFFFFu) | (master ? 0x80000000u : 0);
    if (master && !before) psx_irq_raise(IRQ_DMA);
}

static void dma_run(int ch)
{
    uint32_t madr = dma_madr[ch] & 0x1FFFFCu;
    const uint32_t chcr = dma_chcr[ch];
    const int mode = (int)((chcr >> 9) & 3), from_ram = (int)(chcr & 1), step = (chcr & 2) ? -4 : 4;
    uint32_t words = 0;
    dma_runs++;
    if (mode == 2) {                                           /* linked list: only the GPU */
        int guard = 0x40000;
        uint32_t addr = madr;
        while (guard--) {
            const uint32_t hdr = *(const uint32_t *)(psx_ram + (addr & 0x1FFFFCu));
            const int n = (int)(hdr >> 24);
            for (int k = 1; k <= n; k++) psx_gp0_write(*(const uint32_t *)(psx_ram + ((addr + (uint32_t)k * 4) & 0x1FFFFCu)));
            if (hdr & 0x800000u) break;
            addr = hdr & 0x1FFFFCu;
        }
        madr = 0xFFFFFFu;
    } else {
        uint32_t bs = dma_bcr[ch] & 0xFFFFu, bc = dma_bcr[ch] >> 16;
        if (mode == 0) words = bs ? bs : 0x10000u;
        else words = (bs ? bs : 0x10000u) * (bc ? bc : 1u);
        if (ch == 6) {                                         /* OTC: a list of pointers back to front */
            uint32_t a = madr;
            for (uint32_t i = 0; i < words; i++) {
                *(uint32_t *)(psx_ram + (a & 0x1FFFFCu)) = (i == words - 1) ? 0x00FFFFFFu : ((a - 4) & 0x1FFFFCu);
                a -= 4;
            }
            madr = a;
        } else if (ch == 2) {
            if (from_ram) {
                for (uint32_t i = 0; i < words; i++) { psx_gp0_write(*(const uint32_t *)(psx_ram + (madr & 0x1FFFFCu))); madr += (uint32_t)step; }
            } else {
                for (uint32_t i = 0; i < words; i++) { *(uint32_t *)(psx_ram + (madr & 0x1FFFFCu)) = psx_gpu_read(); madr += (uint32_t)step; }
            }
        } else if (ch == 3) {
            uint8_t *dst = psx_ram + (madr & 0x1FFFFCu);
            const uint32_t n = words * 4 > 0x200000u - (madr & 0x1FFFFCu) ? 0x200000u - (madr & 0x1FFFFCu) : words * 4;
            psx_cd_dma_read(dst, (int)n);
            madr += n;
        } else if (ch == 4) {
            const uint32_t n = words * 4 > 0x200000u - (madr & 0x1FFFFCu) ? 0x200000u - (madr & 0x1FFFFCu) : words * 4;
            if (from_ram) psx_spu_dma_write(psx_ram + (madr & 0x1FFFFCu), (int)n);
            else psx_spu_dma_read(psx_ram + (madr & 0x1FFFFCu), (int)n);
            madr += n;
        } else {                                               /* the MDEC (0, 1) and the extension (5) are not done */
            unhandled++;
            last_unhandled = 0x1F801080u + (uint32_t)ch * 16;
            if (ch == 1) {
                for (uint32_t i = 0; i < words && ((madr + i * 4) & 0x1FFFFCu) < 0x1FFFFCu; i++) *(uint32_t *)(psx_ram + ((madr + i * 4) & 0x1FFFFCu)) = 0;
            }
            madr += words * 4;
        }
    }
    dma_madr[ch] = madr;
    dma_bcr[ch] = mode == 0 ? dma_bcr[ch] : dma_bcr[ch] & 0xFFFFu;
    dma_chcr[ch] &= ~0x11000000u;
    if ((dicr >> (16 + ch)) & 1) dicr_update(1u << (24 + ch));
    else dicr_update(0);
}

static uint32_t dma_read(uint32_t reg)
{
    if (reg < 0x70) {
        const int ch = (int)(reg >> 4);
        switch (reg & 15) {
        case 0: return dma_madr[ch];
        case 4: return dma_bcr[ch];
        case 8: return dma_chcr[ch];
        default: return 0;
        }
    }
    if (reg == 0x70) return dpcr;
    if (reg == 0x74) return dicr;
    return 0;
}

static void dma_write(uint32_t reg, uint32_t v)
{
    if (reg < 0x70) {
        const int ch = (int)(reg >> 4);
        switch (reg & 15) {
        case 0: dma_madr[ch] = v & 0xFFFFFFu; break;
        case 4: dma_bcr[ch] = v; break;
        case 8:
            if (ch == 6) v = (v & 0x51000000u) | 2;            /* OTC: the other bits are fixed */
            dma_chcr[ch] = v & 0x7F777707u;
            if ((v & 0x01000000u) && ((dpcr >> (ch * 4 + 3)) & 1)) {
                const int mode = (int)((v >> 9) & 3);
                if (mode != 0 || (v & 0x10000000u) || ch == 6) dma_run(ch);
            }
            break;
        default: break;
        }
        return;
    }
    if (reg == 0x70) dpcr = v;
    if (reg == 0x74) {
        dicr = (dicr & 0xFF000000u & ~(v & 0x7F000000u)) | (v & 0x00FF803Fu) | (dicr & 0x80000000u);
        dicr_update(0);
    }
}

/* ------------------------------------------------------------------ pad port */
static uint16_t pad_bits[2];              /* 1 = pressed, as PSX_PAD_* */
static uint32_t joy_ctrl, joy_mode, joy_baud;
static uint8_t joy_rx;
static int joy_rx_full, joy_state, joy_irq_flag, joy_ack_delay;

static void joy_tx(uint8_t b)
{
    const int port = (joy_ctrl >> 13) & 1;
    uint8_t rx = 0xFF;
    int ack = 0;
    const uint16_t keys = (uint16_t)~pad_bits[port];
    switch (joy_state) {
    case 0:
        if (b == 0x01 && port == 0) { joy_state = 1; ack = 1; }          /* a pad on port 1 only */
        break;
    case 1:
        if (b == 0x42) { rx = 0x41; joy_state = 2; ack = 1; } else joy_state = 0;
        break;
    case 2: rx = 0x5A; joy_state = 3; ack = 1; break;
    case 3: rx = (uint8_t)keys; joy_state = 4; ack = 1; break;
    case 4: rx = (uint8_t)(keys >> 8); joy_state = 0; break;
    default: joy_state = 0; break;
    }
    joy_rx = rx;
    joy_rx_full = 1;
    if (ack) joy_ack_delay = 360;
}

static uint32_t joy_read(uint32_t reg, int size)
{
    (void)size;
    switch (reg) {
    case 0x40: { const uint32_t v = joy_rx; joy_rx_full = 0; return v; }
    case 0x44: return 0x05u | (joy_rx_full ? 2u : 0u) | (joy_irq_flag ? 0x200u : 0u);
    case 0x48: return joy_mode;
    case 0x4A: return joy_ctrl;
    case 0x4E: return joy_baud;
    default: return 0;
    }
}

static void joy_write(uint32_t reg, uint32_t v)
{
    switch (reg) {
    case 0x40: joy_tx((uint8_t)v); break;
    case 0x48: joy_mode = v; break;
    case 0x4A:
        joy_ctrl = v & 0xFFFFu;
        if (v & 0x10u) joy_irq_flag = 0;
        if (v & 0x40u) { joy_state = 0; joy_rx_full = 0; joy_irq_flag = 0; joy_ctrl &= ~0x40u; }
        if (!(v & 2u)) joy_state = 0;
        break;
    case 0x4E: joy_baud = v; break;
    default: break;
    }
}

static void joy_run(uint32_t cycles)
{
    if (joy_ack_delay > 0) {
        joy_ack_delay -= (int)cycles;
        if (joy_ack_delay <= 0) {
            joy_ack_delay = 0;
            if (joy_ctrl & 0x1000u) { joy_irq_flag = 1; psx_irq_raise(IRQ_PAD); }
        }
    }
}

/* ------------------------------------------------------------------ the bus */
static uint32_t io_read32(uint32_t reg)
{
    if (reg < 0x40) return reg < 0x24 ? mc[reg >> 2] : 0;
    if (reg < 0x50) return joy_read(reg, 4);
    if (reg < 0x60) return reg == 0x54 ? 0x05u : 0;                  /* SIO: ready, nothing there */
    if (reg == 0x60) return ram_size_reg;
    if (reg == 0x70) return I_STAT;
    if (reg == 0x74) return I_MASK;
    if (reg >= 0x80 && reg < 0x100) return dma_read(reg - 0x80);
    if (reg >= 0x100 && reg < 0x130) return timer_read(reg - 0x100);
    if (reg == 0x810) return psx_gpu_read();
    if (reg == 0x814) return psx_gpu_stat();
    if (reg == 0x820) return 0;
    if (reg == 0x824) return 0x04000000u;                            /* MDEC: idle */
    return 0;
}

uint32_t psx_bus_read(uint32_t a, int size)
{
    if (a >= 0x1FC00000u) {
        const uint8_t *b = psx_bios + (a & 0x7FFFFu);
        if (size == 4) return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        if (size == 2) return (uint32_t)b[0] | (uint32_t)b[1] << 8;
        return b[0];
    }
    if (a >= 0x1F800000u && a < 0x1F800400u) {
        const uint8_t *m = psx_scratch + (a & 0x3FFu);
        if (size == 4) return (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24;
        if (size == 2) return (uint32_t)m[0] | (uint32_t)m[1] << 8;
        return m[0];
    }
    if (a >= 0x1F801000u && a < 0x1F802000u) {
        const uint32_t reg = a & 0xFFFu;
        if (reg >= 0x800 && reg < 0x804) return psx_cd_read(reg & 3);
        if (reg >= 0xC00) return size == 4 ? (uint32_t)psx_spu_read(reg) | (uint32_t)psx_spu_read(reg + 2) << 16 : (uint32_t)psx_spu_read(reg & ~1u) >> ((reg & 1) * 8);
        if (reg >= 0x40 && reg < 0x50) return joy_read(reg & ~1u, size);
        const uint32_t v = io_read32(reg & ~3u);
        return size == 4 ? v : size == 2 ? (v >> ((reg & 2) * 8)) & 0xFFFFu : (v >> ((reg & 3) * 8)) & 0xFFu;
    }
    if (a >= 0x1F802000u && a < 0x1F804000u) return 0;               /* expansion 2: nothing there */
    if (a >= 0x1F000000u && a < 0x1F800000u) return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu;   /* expansion 1 */
    psx_dbg_unhandled(a);
    return 0;
}

void psx_bus_write(uint32_t a, uint32_t v, int size)
{
    if (a >= 0x1F800000u && a < 0x1F800400u) {
        uint8_t *m = psx_scratch + (a & 0x3FFu);
        if (size == 4) { m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); m[2] = (uint8_t)(v >> 16); m[3] = (uint8_t)(v >> 24); }
        else if (size == 2) { m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); }
        else m[0] = (uint8_t)v;
        return;
    }
    if (a >= 0x1F801000u && a < 0x1F802000u) {
        const uint32_t reg = a & 0xFFFu;
        if (reg >= 0x800 && reg < 0x804) { psx_cd_write(reg & 3, v); return; }
        if (reg >= 0xC00) {
            if (size == 4) { psx_spu_write(reg, (uint16_t)v); psx_spu_write(reg + 2, (uint16_t)(v >> 16)); }
            else psx_spu_write(reg & ~1u, (uint16_t)v);
            return;
        }
        if (reg >= 0x40 && reg < 0x50) { joy_write(reg & ~1u, v); return; }
        if (reg == 0x70) { I_STAT &= v | 0xFFFFF800u; return; }       /* writing 0 acknowledges */
        if (reg == 0x74) { I_MASK = v & 0x7FFu; return; }
        if (reg < 0x24) { mc[reg >> 2] = v; return; }
        if (reg == 0x60) { ram_size_reg = v; return; }
        if (reg >= 0x80 && reg < 0x100) { dma_write(reg - 0x80, v); return; }
        if (reg >= 0x100 && reg < 0x130) { timer_write(reg - 0x100, v); return; }
        if (reg == 0x810) { psx_gp0_write(v); return; }
        if (reg == 0x814) { psx_gp1_write(v); return; }
        return;
    }
    if (a >= 0x1F802000u && a < 0x1F804000u) return;                  /* expansion 2 (POST) */
    if (a == 0x1FFE0130u) return;                                     /* cache control */
    if (a >= 0x1F000000u && a < 0x1F800000u) return;
    psx_dbg_unhandled(a);
}

/* ------------------------------------------------------------------ the BIOS calls without a BIOS */
static void tty_put(char c)
{
    if (tty_len >= sizeof tty - 1) {
        memmove(tty, tty + 1024, tty_len - 1024);
        tty_len -= 1024;
    }
    tty[tty_len++] = c;
    tty[tty_len] = 0;
}

static uint32_t ram32(uint32_t a)
{
    const uint32_t p = a & 0x1FFFFFFFu;
    return p < 0x200000u ? psx_peek32(p) : 0;
}

static void hle_printf(void)
{
    uint32_t fmt = C.r[4];
    uint32_t argi = 0;
    const uint32_t sp = C.r[29];
    #define ARG() (argi < 3 ? C.r[5 + argi++] : ram32(sp + 16 + 4 * (argi++)))
    for (int guard = 0; guard < 512; guard++) {
        const char c = (char)(psx_peek32(fmt & 0x1FFFFFFFu) >> (8 * (fmt & 3)));
        fmt++;
        if (!c) break;
        if (c != '%') { tty_put(c); continue; }
        int width = 0, zero = 0;
        char s = (char)(psx_peek32(fmt & 0x1FFFFFFFu) >> (8 * (fmt & 3)));
        if (s == '0') { zero = 1; fmt++; s = (char)(psx_peek32(fmt & 0x1FFFFFFFu) >> (8 * (fmt & 3))); }
        while (s >= '0' && s <= '9') { width = width * 10 + (s - '0'); fmt++; s = (char)(psx_peek32(fmt & 0x1FFFFFFFu) >> (8 * (fmt & 3))); }
        if (s == 'l') { fmt++; s = (char)(psx_peek32(fmt & 0x1FFFFFFFu) >> (8 * (fmt & 3))); }
        fmt++;
        char buf[16];
        int n = 0;
        if (s == 'd' || s == 'i' || s == 'u' || s == 'x' || s == 'X') {
            uint32_t v = ARG();
            int neg = 0;
            if ((s == 'd' || s == 'i') && (int32_t)v < 0) { neg = 1; v = (uint32_t)(-(int32_t)v); }
            const uint32_t base = (s == 'x' || s == 'X') ? 16 : 10;
            do { const uint32_t d = v % base; buf[n++] = (char)(d < 10 ? '0' + d : (s == 'X' ? 'A' : 'a') + d - 10); v /= base; } while (v && n < 15);
            if (neg) buf[n++] = '-';
            for (int pad = n; pad < width; pad++) tty_put(zero ? '0' : ' ');
            while (n) tty_put(buf[--n]);
        } else if (s == 'c') {
            tty_put((char)ARG());
        } else if (s == 's') {
            uint32_t p = ARG();
            for (int g = 0; g < 256; g++, p++) {
                const char ch = (char)(psx_peek32(p & 0x1FFFFFFFu) >> (8 * (p & 3)));
                if (!ch) break;
                tty_put(ch);
            }
        } else if (s == '%') {
            tty_put('%');
        }
    }
    #undef ARG
}

static void hle_call(uint32_t vec)
{
    const uint32_t fn = C.r[9] & 0xFF;
    if (vec == 0xA0) {
        if (fn == 0x3C) tty_put((char)C.r[4]);
        else if (fn == 0x3E) { uint32_t p = C.r[4]; for (int g = 0; g < 512; g++, p++) { const char ch = (char)(psx_peek32(p & 0x1FFFFFFFu) >> (8 * (p & 3))); if (!ch) break; tty_put(ch); } tty_put('\n'); }
        else if (fn == 0x3F) hle_printf();
    } else if (vec == 0xB0) {
        if (fn == 0x3D) tty_put((char)C.r[4]);
        else if (fn == 0x3F) { uint32_t p = C.r[4]; for (int g = 0; g < 512; g++, p++) { const char ch = (char)(psx_peek32(p & 0x1FFFFFFFu) >> (8 * (p & 3))); if (!ch) break; tty_put(ch); } tty_put('\n'); }
    }
    C.r[2] = 0;
    C.pc = C.r[31];
    C.npc = C.pc + 4;
    C.next_bd = 0;
}

/* ------------------------------------------------------------------ PS-EXE */
static int exe_apply(const uint8_t *d, size_t len)
{
    if (len < 0x800 || memcmp(d, "PS-X EXE", 8) != 0) return -1;
    #define LE32(o) ((uint32_t)d[o] | (uint32_t)d[(o) + 1] << 8 | (uint32_t)d[(o) + 2] << 16 | (uint32_t)d[(o) + 3] << 24)
    const uint32_t pc0 = LE32(0x10), gp0 = LE32(0x14), taddr = LE32(0x18) & 0x1FFFFFFFu, tsize = LE32(0x1C);
    const uint32_t spb = LE32(0x30), spo = LE32(0x34);
    #undef LE32
    if (tsize > len - 0x800) return -1;
    if (taddr + tsize > 0x200000u) return -1;
    memcpy(psx_ram + taddr, d + 0x800, tsize);
    C.pc = pc0; C.npc = pc0 + 4; C.next_bd = 0; C.ld_r = 0;
    C.r[28] = gp0;
    C.r[29] = spb ? spb + spo : 0x801FFFF0u;
    C.r[30] = C.r[29];
    C.cop0[12] = (C.cop0[12] & ~0x00400000u) | 0x40000000u;   /* the BIOS leaves the GTE on and the vectors in RAM */
    return 0;
}

void psx_hook(void)
{
    const uint32_t pc = C.pc;
    if (exe_pending && has_bios && pc == 0x80030000u) {
        exe_apply(exe_buf, exe_len);
        exe_pending = 0;
        psx_hooks = 0;
        return;
    }
    if (!has_bios) {
        const uint32_t p = pc & 0x1FFFFFFFu;
        if (p == 0xA0 || p == 0xB0 || p == 0xC0) hle_call(p);
    }
}

int psx_load_exe(const uint8_t *data, size_t len, const char **why)
{
    if (len < 0x800 || memcmp(data, "PS-X EXE", 8) != 0) { if (why) *why = "not a PS-EXE"; return -1; }
    uint8_t *nb = (uint8_t *)realloc(exe_buf, len);
    if (!nb) { if (why) *why = "out of memory"; return -1; }
    exe_buf = nb;
    memcpy(exe_buf, data, len);
    exe_len = len;
    psx_reset();
    if (has_bios) {
        exe_pending = 1;
        psx_hooks = 1;
    } else {
        if (exe_apply(exe_buf, exe_len) != 0) { if (why) *why = "bad PS-EXE"; return -1; }
        psx_hooks = 1;
    }
    return 0;
}

int psx_load_bios(const uint8_t *data, size_t len, const char **why)
{
    if (len != 512 * 1024) { if (why) *why = "a BIOS image is 512 KB"; return -1; }
    memcpy(psx_bios, data, len);
    has_bios = 1;
    psx_reset();
    return 0;
}

/* ------------------------------------------------------------------ the machine */
void psx_reset(void)
{
    memset(psx_ram, 0, sizeof psx_ram);
    memset(psx_scratch, 0, sizeof psx_scratch);
    memset(T, 0, sizeof T);
    memset(dma_madr, 0, sizeof dma_madr); memset(dma_bcr, 0, sizeof dma_bcr); memset(dma_chcr, 0, sizeof dma_chcr);
    dpcr = 0x07654321u; dicr = 0;
    I_STAT = I_MASK = 0;
    joy_ctrl = joy_mode = 0; joy_rx_full = 0; joy_state = 0; joy_irq_flag = 0; joy_ack_delay = 0;
    t_last = 0; line_no = 0; frames = 0; hblank_in = 0;
    tty_len = 0; tty[0] = 0;
    unhandled = last_unhandled = dma_runs = 0;
    exe_pending = 0;
    psx_hooks = has_bios ? 0 : 1;
    psx_cpu_reset();
    psx_gte_reset();
    psx_gpu_reset();
    memset(GP.vram, 0, sizeof GP.vram);
    psx_spu_reset();
    psx_cd_reset();
    if (!has_bios) {
        C.pc = 0x80010000u; C.npc = C.pc + 4;
        C.cop0[12] = 0x40000000u;
    }
}

void psx_frame(uint16_t pad1, uint16_t pad2, int render)
{
    const uint32_t t0 = clock_us ? clock_us() : 0;
    pad_bits[0] = pad1; pad_bits[1] = pad2;
    uint32_t cpu_us = 0;
    for (int ln = 0; ln < PSX_LINES; ln++) {
        line_no = ln;
        const uint32_t a = clock_us ? clock_us() : 0;
        psx_cpu_run(PSX_LINE_CYC);
        if (clock_us) cpu_us += clock_us() - a;
        timers_sync();
        timers_hblank();
        joy_run(PSX_LINE_CYC);
        psx_cd_run(PSX_LINE_CYC);
        psx_spu_run(PSX_LINE_CYC);
        if (ln == PSX_VBLANK_LN) psx_irq_raise(IRQ_VBLANK);
    }
    frames++;
    (void)render;
    if (clock_us) {
        const uint32_t dt = clock_us() - t0;
        us_frame = (us_frame * 7 + dt) / 8;
        us_cpu = (us_cpu * 7 + cpu_us) / 8;
    }
    (void)us_gpu;
}

static int pic_w, pic_h;
static const uint16_t *pic_ptr;

const uint16_t *psx_picture(int *w, int *h)
{
    pic_ptr = psx_gpu_picture(&pic_w, &pic_h);
    *w = pic_w; *h = pic_h;
    return pic_ptr;
}

void psx_set_disc(const psx_disc_t *d) { psx_cd_set_disc(d); }

void psx_debug(psx_debug_t *d)
{
    memset(d, 0, sizeof *d);
    d->pc = C.pc; d->ra = C.r[31]; d->sp = C.r[29]; d->sr = C.cop0[12]; d->cause = C.cop0[13];
    d->istat = I_STAT; d->imask = I_MASK;
    d->frames = frames; d->cycles_frame = PSX_FRAME_CYC;
    d->gpu_cmds = GP.cmds; d->gpu_pixels = GP.pixels; d->dma_runs = dma_runs;
    d->bios = has_bios; d->exe = exe_len != 0;
    d->us_frame = us_frame; d->us_cpu = us_cpu; d->us_gpu = us_gpu;
    d->tty_len = tty_len; d->unhandled = unhandled; d->last_unhandled = last_unhandled;
}
