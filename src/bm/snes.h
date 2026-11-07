/*
 * snes.h: the Super Nintendo core of bm SNES (2026-11-07). Original code. It runs a .sfc or .smc ROM:
 * the 65C816 (snes_cpu.c), the bus, DMA and the timing (snes_bus.c), the picture (snes_ppu.c) and the sound
 * CPU, the SPC700 (snes_spc.c). The DSP makes no sound yet. The Lua cartridge carts/snes is the user interface;
 * it calls the functions of api_snes.c, which call this.
 *
 * One machine only (a global), as the cartridge runs alone.
 */
#ifndef SNES_H
#define SNES_H

#include <stddef.h>
#include <stdint.h>

#define SNES_W 256
#define SNES_H_MAX 239

/* joypad bits as the machine reads them in $4218/$4219 */
#define SNES_PAD_B      0x8000
#define SNES_PAD_Y      0x4000
#define SNES_PAD_SELECT 0x2000
#define SNES_PAD_START  0x1000
#define SNES_PAD_UP     0x0800
#define SNES_PAD_DOWN   0x0400
#define SNES_PAD_LEFT   0x0200
#define SNES_PAD_RIGHT  0x0100
#define SNES_PAD_A      0x0080
#define SNES_PAD_X      0x0040
#define SNES_PAD_L      0x0020
#define SNES_PAD_R      0x0010

typedef struct {
    char title[24];
    int hirom;
    uint32_t rom_size;
    uint32_t sram_size;
} snes_info_t;

/* loads a ROM (a copier header of 512 bytes is skipped); the bytes are copied. 0, or -1 and *why */
int snes_load(const uint8_t *data, size_t len, const char **why);
void snes_reset(void);
/* one frame with the two pads; the picture is ready after it */
void snes_frame(uint16_t pad1, uint16_t pad2, int render);   /* render 0: the picture is not drawn (a frame that is skipped) */
/* the picture: SNES_W x lines of RGB565 (lines is 224 or 239) */
const uint16_t *snes_picture(int *lines);
const snes_info_t *snes_info(void);
/* the battery RAM: its bytes and whether it changed since snes_sram_clean */
uint8_t *snes_sram(uint32_t *size);
int snes_sram_dirty(void);
void snes_sram_clean(void);
/* what a stuck game waits for: the main CPU's PC, flags (1 WAI, 2 STP), the last I/O reads, the sound CPU */
typedef struct {
    uint32_t pc;
    int flags;
    uint16_t io[6];
    int spc_running, spc_halted;
    uint16_t spc_pc;
    uint8_t inidisp, tm, nmitimen;
    uint32_t frames;
    uint8_t cpu_code[12];               /* the bytes at the CPU PC */
    uint8_t spc_code[12];               /* the bytes at the sound PC */
    uint8_t port_in[4], port_out[4];    /* the four ports: written by the CPU, written by the sound CPU */
    uint8_t timer_out[3];
    uint16_t trace[12], trace_s[12];
    uint32_t us_bg, us_obj, us_win, us_comp, us_mix;     /* since the last read: layers, sprites, windows, mixing (microseconds) */
    uint32_t us_frame, us_ppu, us_spc, us_draw; /* the time of a frame and its parts, averaged, in microseconds */
    uint32_t spc_writes;                /* port writes by the CPU since the reset */
    uint8_t spc_ctl, spc_tgt[3], spc_en;   /* $F1, the timer targets, which timers run */
} snes_debug_t;
void snes_debug(snes_debug_t *d);
/* a clock in microseconds, to time the parts of a frame (the host or the kernel gives it; NULL: no timing) */
void snes_set_clock(uint32_t (*now_us)(void));

/* the sound CPU (snes_spc.c) */
void spc_reset(void);
void spc_port_write(int n, uint8_t v);      /* n = 0..3, the main CPU writes */
uint8_t spc_port_read(int n);
void spc_run(int cycles);
void spc_trace_s(uint16_t *out12);         /* the sound CPU's writes */
void spc_trace(uint16_t *out12);           /* the last 12 port events, oldest first */
void spc_info(int *running, int *halted, uint16_t *pc);
void spc_peek(uint16_t addr, uint8_t *out, int n, uint8_t *pin, uint8_t *pout, uint8_t *tout, uint8_t *regs);

#endif
