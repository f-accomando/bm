/*
 * psx.h: the PlayStation 1 core of bm PSX. Original code (clean room: the hardware facts come from public
 * documentation, no code of pcsx-rearmed or any other emulator is used). It runs a BIOS image and a PS-EXE
 * or a disc image: the R3000A (psx_cpu.c), the GTE (psx_gte.c), the bus with the interrupts, timers, DMA, pads and the
 * CD-ROM controller (psx_bus.c), the GPU (psx_gpu.c) and the SPU (psx_spu.c). The Lua cartridge carts/psx is the
 * user interface; it calls the functions of api_psx.c, which call this.
 *
 * One machine only (a global), as the cartridge runs alone.
 */
#ifndef PSX_H
#define PSX_H

#include <stddef.h>
#include <stdint.h>

#define PSX_W_MAX 640
#define PSX_H_MAX 480

/* pad bits (1 = pressed) */
#define PSX_PAD_SELECT   0x0001
#define PSX_PAD_L3       0x0002
#define PSX_PAD_R3       0x0004
#define PSX_PAD_START    0x0008
#define PSX_PAD_UP       0x0010
#define PSX_PAD_RIGHT    0x0020
#define PSX_PAD_DOWN     0x0040
#define PSX_PAD_LEFT     0x0080
#define PSX_PAD_L2       0x0100
#define PSX_PAD_R2       0x0200
#define PSX_PAD_L1       0x0400
#define PSX_PAD_R1       0x0800
#define PSX_PAD_TRIANGLE 0x1000
#define PSX_PAD_CIRCLE   0x2000
#define PSX_PAD_CROSS    0x4000
#define PSX_PAD_SQUARE   0x8000

/* the BIOS (512 KB). 0, or -1 and *why. Without it the machine runs a PS-EXE alone and answers the few BIOS calls
 * a program makes (putchar, puts, printf) itself. */
int psx_load_bios(const uint8_t *data, size_t len, const char **why);
/* a PS-EXE: with a BIOS it starts when the BIOS reaches its shell, without one at once. 0, or -1 and *why. */
int psx_load_exe(const uint8_t *data, size_t len, const char **why);
/* a disc: sectors come from read(); raw is 1 for 2352-byte sectors (.bin), 0 for 2048-byte sectors (.iso).
 * The callback returns 0 or -1. */
typedef struct {
    int (*read)(void *user, uint32_t lba, uint8_t *out);   /* out: 2352 bytes if raw, else 2048 */
    void *user;
    uint32_t sectors;
    int raw;
} psx_disc_t;
void psx_set_disc(const psx_disc_t *d);           /* NULL: no disc (the shell is open) */
void psx_reset(void);
/* one frame (1/60 s) with the two pads */
void psx_frame(uint16_t pad1, uint16_t pad2, int render);
/* the picture: w x h of RGB565 (w 256..640, h 240 or 480) */
const uint16_t *psx_picture(int *w, int *h);
/* the sound: 44.1 kHz stereo ring; this is what the output calls (it resamples to AUDIO_RATE). Interrupt safe. */
int psx_audio_mix(float *out, unsigned n, float gain);
void psx_audio_stats(uint32_t *underruns, uint32_t *overruns, uint32_t *voices);

typedef struct {
    uint32_t pc, ra, sp, sr, cause, istat, imask;
    uint32_t frames, cycles_frame;
    uint32_t gpu_cmds, gpu_pixels;
    uint32_t dma_runs;
    uint32_t cd_cmds, cd_sectors, cd_last_cmd, cd_irq_flag, cd_irq_en, cd_events, cd_read_fail;
    uint32_t cd_trace[12];     /* the last commands, newest last: cmd | param0 << 8 | param1 << 16 | param2 << 24 */
    uint32_t cd_lba, cd_loc;
    int bios, disc, exe;
    uint32_t us_frame, us_cpu, us_gpu;
    uint32_t tty_len;
    uint32_t unhandled;        /* bus accesses to an address nothing answers, or a GPU / GTE command not done */
    uint32_t last_unhandled;
} psx_debug_t;
void psx_debug(psx_debug_t *d);
/* the text a program printed with the BIOS calls (kept without a BIOS; the last 2 KB) */
const char *psx_tty(void);
/* with a BIOS: log what a program prints through the BIOS calls into psx_tty (the BIOS still runs the call). Costs a check per instruction. */
void psx_log_bios_tty(int on);
void psx_set_clock(uint32_t (*now_us)(void));
/* a debug read of the machine: a word of RAM (address in KUSEG, 0..2 MB) */
uint32_t psx_peek32(uint32_t addr);
/* the VRAM (1024 x 512 of RGB555 + mask) and the registers, for the tests */
const uint16_t *psx_vram(void);
uint32_t psx_reg(int n);

#endif
