/*
 * nes.h: the Famicom / NES core of bm NES (2026-11-11). Original code. It runs an iNES (.nes) ROM:
 * the 2A03 (a 6502 with no decimal mode, all the official opcodes and the usual unofficial ones),
 * the 2C02 picture unit drawn a line at a time, the five sound channels, and the mappers 0, 1, 2, 3, 4, 7, 11, 34, 66, 71 and 79.
 * It has no dependence on the kernel, so the host tests build it alone. The Lua cartridge bm/nes is the user
 * interface; it calls the functions of api_nes.c, which call this.
 *
 * One machine only (a global), as the cartridge runs alone.
 */
#ifndef NES_H
#define NES_H

#include <stddef.h>
#include <stdint.h>

#define NES_W 256
#define NES_H_PIX 240

/* joypad bits as the machine shifts them out of $4016/$4017 (A first) */
#define NES_PAD_A      0x01
#define NES_PAD_B      0x02
#define NES_PAD_SELECT 0x04
#define NES_PAD_START  0x08
#define NES_PAD_UP     0x10
#define NES_PAD_DOWN   0x20
#define NES_PAD_LEFT   0x40
#define NES_PAD_RIGHT  0x80

typedef struct {
    int mapper;
    uint32_t prg_size;      /* bytes of program ROM */
    uint32_t chr_size;      /* bytes of picture ROM (0: the cartridge has picture RAM) */
    int battery;            /* the cartridge keeps its RAM with a battery */
    int mirroring;          /* 0 horizontal, 1 vertical, 2 one screen (low), 3 one screen (high), 4 four screens */
    uint32_t sram_size;     /* bytes of battery RAM (8192 or 0) */
} nes_info_t;

/* loads a ROM (the bytes are copied). 0, or -1 and *why */
int nes_load(const uint8_t *data, size_t len, const char **why);
void nes_reset(void);
/* one frame with the two pads; render 0: the picture is not drawn (a frame that is skipped) */
void nes_frame(uint8_t pad1, uint8_t pad2, int render);
/* the picture: NES_W x NES_H_PIX of RGB565 */
const uint16_t *nes_picture(void);
const nes_info_t *nes_info(void);
/* the battery RAM: its bytes and whether it changed since nes_sram_clean */
uint8_t *nes_sram(uint32_t *size);
int nes_sram_dirty(void);
void nes_sram_clean(void);
/* the output's side: mono samples at 48 kHz added into a stereo float buffer; the output calls it (audio_set_ext) */
int nes_audio_mix(float *out, unsigned n, float gain);
void nes_set_clock(uint32_t (*us_fn)(void));

/* what a stuck game waits for, and what the tests read */
typedef struct {
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint8_t ctrl, mask, status;
    uint16_t v, t;
    uint32_t frames, cycles;
    int scanline;
    int jammed;
    uint32_t audio_under, audio_over;
    uint32_t us_frame, us_ppu, us_cpu;         /* the time of a frame and its parts, averaged, in microseconds */
} nes_debug_t;
void nes_debug(nes_debug_t *d);
uint8_t nes_peek(uint16_t addr);                    /* a CPU read with no side effect (the registers read 0) */
const uint8_t *nes_ram(void);                       /* the 2 KB of work RAM */
const uint8_t *nes_oam(void);                       /* the 256 bytes of sprite memory */
const uint8_t *nes_palette(void);                   /* the 32 bytes of palette */
/* tests only: one instruction at a time, and a start state (the nestest.nes log starts at $C000, P $24, S $FD, 7 cycles) */
int nes_test_step(void);                            /* the cycles it took */
void nes_test_set_cpu(uint16_t pc, uint8_t p, uint8_t s, uint32_t cycles);

#endif
