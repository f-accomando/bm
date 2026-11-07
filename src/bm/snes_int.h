/* snes_int.h: the state that the files of the SNES core share (private to src/bm/snes_*.c). */
#ifndef SNES_INT_H
#define SNES_INT_H

#include "snes.h"

typedef struct {
    uint8_t dmap, bbad, a1b, dasb, ntrl, unused;
    uint16_t a1t, das, a2a;
    /* HDMA */
    uint16_t tbl;           /* the table address now */
    uint8_t line;           /* the line counter (bit 7: repeat) */
    uint8_t doit, active;
    uint16_t ind;           /* indirect data address */
} snes_dma_t;

typedef struct {
    /* memory */
    uint8_t wram[131072];
    uint8_t vram[65536];
    uint8_t oam[544];
    uint8_t cgram[512];
    uint16_t pal[256];      /* the colours as RGB565 */
    uint8_t sram[131072];
    uint8_t *rom;
    uint32_t rom_size, rom_mask;
    uint32_t sram_size;
    int hirom, sram_dirty;
    snes_info_t info;

    /* the PPU registers */
    uint8_t inidisp, obsel, bgmode, mosaic, setini;
    uint8_t sc[4];
    uint16_t nba[4];                    /* the tile data of each BG: byte address */
    uint16_t hofs[4], vofs[4];
    uint8_t bgofs_latch, m7_latch;
    uint8_t vmain;
    uint16_t vmaddr, vmpre;
    uint16_t oamaddr, oamaddr_reload;
    uint8_t oam_latch;
    uint8_t cgadd, cgflip, cglatch;
    uint8_t m7sel;
    int16_t m7a, m7b, m7c, m7d, m7x, m7y, m7hofs, m7vofs;
    uint8_t tm, ts, tmw, tsw;
    uint8_t w12sel, w34sel, wobjsel, wh0, wh1, wh2, wh3, wbglog, wobjlog;
    uint8_t cgwsel, cgadsub, fixr, fixg, fixb;
    int32_t mpy;
    uint32_t wmadd;
    uint8_t ophct_flip, opvct_flip, hcount_lo, stat78;
    uint16_t hcount, vcount;

    /* the CPU registers */
    uint8_t nmitimen, wrmpya, wrmpyb, rdmpy_lo, rdmpy_hi, mdmaen, hdmaen, memsel;
    uint16_t wrdiv, rddiv, rdmpy, htime, vtime;
    uint8_t nmiflag, timeup, joybusy;
    uint16_t joy1, joy2;                /* the pads' state from the frontend */
    uint16_t joyauto1, joyauto2;        /* what auto-read made ($4218..$421B) */
    uint16_t joyshift1, joyshift2;
    uint8_t joystrobe;
    snes_dma_t dma[8];

    /* the clocks */
    int line;                           /* the scanline 0..261 */
    int line_clk;                       /* master clocks into the line */
    int irq_pending, nmi_pending;
    int lines;                          /* 224 or 239 */
    int vblank, hblank;
    uint32_t frames;
    uint16_t io_log[6];
    int io_idx;

    uint16_t picture[SNES_W * SNES_H_MAX];
} snes_t;

extern snes_t S;

/* snes_bus.c */
uint8_t snes_read(uint32_t a);
void snes_write(uint32_t a, uint8_t v);
void snes_bus_reset(void);
void snes_ppu_write(uint16_t o, uint8_t v);
uint8_t snes_ppu_read(uint16_t o);
void snes_hdma_init(void);
void snes_hdma_line(void);
extern int snes_clk;                    /* the CPU's clock, in master clocks, counting into the line */

/* snes_cpu.c */
void cpu_reset(void);
void cpu_step(void);
void cpu_irq_nmi_check(void);
void cpu_info(uint32_t *pc, int *flags);
extern int cpu_waiting, cpu_stopped;
typedef struct { uint16_t a, x, y, s, d, pc; uint8_t p; int e; uint8_t pb, db; } cpu_regs_t;
void cpu_get(cpu_regs_t *r);                    /* for the tests */

/* snes_ppu.c */
void ppu_reset(void);
void ppu_line(int y);                   /* draws line y into S.picture */
void ppu_cgram_write(int index);
extern uint32_t (*snes_now_us)(void);
extern uint32_t ppu_us[5];               /* the time of the parts of ppu_line since the last read: layers, sprites, windows, compose, mix */

#endif
