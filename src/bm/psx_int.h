/* psx_int.h: the state of the PlayStation core shared by the files psx_*.c. Original code. */
#ifndef PSX_INT_H
#define PSX_INT_H

#include "psx.h"
#include <stdio.h>
#include <string.h>

/* set by the host probe (tests/psx/boot_probe.c); the kernel build has none */
extern int psx_probe_trace __attribute__((weak));
#define TRACE(...) do { if (&psx_probe_trace && psx_probe_trace) printf(__VA_ARGS__); } while (0)

/* the short names of the three big states and of the interrupt registers: the symbols are psx_*, so they never meet
 * the SNES core's in the kernel */
#define C psx_cpu
#define G psx_gte
#define GP psx_gpu
#define I_STAT psx_i_stat
#define I_MASK psx_i_mask

#define PSX_CPI 2                       /* cycles an instruction costs */
#define PSX_CLOCK      33868800u
#define PSX_FRAME_CYC  564480u           /* NTSC, 60 Hz */
#define PSX_LINE_CYC   2154u
#define PSX_LINES      263
#define PSX_VBLANK_LN  240

/* interrupt bits of I_STAT */
enum { IRQ_VBLANK = 0, IRQ_GPU, IRQ_CD, IRQ_DMA, IRQ_T0, IRQ_T1, IRQ_T2, IRQ_PAD, IRQ_SIO, IRQ_SPU, IRQ_PIO };

typedef struct {
    uint32_t r[32], hi, lo;
    uint32_t pc, npc, cur_pc;
    uint32_t cop0[32];
    uint32_t ld_r, ld_v;                 /* the load in flight: it lands after the next instruction */
    int in_bd, next_bd;                  /* the instruction is in a branch delay slot */
    int halted;
    int jit_exc;                         /* set by a slow load or store of the dynamic recompiler that raised an exception */
    uint64_t cycles;                     /* since the reset */
} cpu_t;

typedef struct {
    int32_t d[32];                       /* data registers */
    int32_t c[32];                       /* control registers */
} gte_t;

typedef struct {
    uint16_t vram[512 * 1024];
    uint32_t texpage;                    /* the GP0 E1 state, as GPUSTAT bits 0..10 */
    uint32_t stat;                       /* GPUSTAT bits 11..31 that are state (mask, display) */
    int tw_mx, tw_my, tw_ox, tw_oy;      /* texture window: mask and offset, in 8 pixel units */
    int da_x1, da_y1, da_x2, da_y2;      /* drawing area */
    int off_x, off_y;                    /* drawing offset */
    int disp_x, disp_y;                  /* start of the display in VRAM */
    int hr_x1, hr_x2, vr_y1, vr_y2;      /* horizontal and vertical display range */
    uint32_t dispmode;                   /* GP1 08 */
    int disp_off;
    int dma_dir;
    int irq;
    uint32_t cmd[16];
    int cmd_n, cmd_need;
    int poly_line;                       /* in a polyline */
    uint32_t poly_op;                    /* its first word */
    int xfer;                            /* 0 none, 1 cpu to vram, 2 vram to cpu */
    int xfer_x, xfer_y, xfer_w, xfer_h, xfer_cx, xfer_cy;
    uint32_t read_latch;
    uint32_t line_colors[2];
    int32_t line_xy[2];
    uint32_t cmds, pixels;
} gpu_t;

extern cpu_t psx_cpu;
extern gte_t psx_gte;
extern gpu_t psx_gpu;
extern uint8_t psx_ram[2 * 1024 * 1024];
extern uint8_t psx_bios[512 * 1024];
extern uint8_t psx_scratch[1024];
extern uint32_t psx_i_stat, psx_i_mask;

/* psx_cpu.c */
void psx_cpu_reset(void);
void psx_cpu_run(uint32_t cycles);
void psx_cpu_exception(uint32_t code);
uint32_t psx_bus_read(uint32_t addr, int size);          /* physical address; size 1, 2, 4 */
void psx_bus_write(uint32_t addr, uint32_t v, int size);
/* the helpers of the dynamic recompiler (psx_jit.c): psx_cpu.c gives them what the interpreter knows */
int psx_jit_exec(uint32_t instr, uint32_t pc, int bd);                       /* one instruction by the interpreter; 1 if the PC left pc + 4 */
uint32_t psx_jit_read(uint32_t addr, uint32_t kind_bd, uint32_t pc);          /* kind: 0 lw, 1 lh, 2 lhu, 3 lb, 4 lbu; bit 4: in a delay slot */
int psx_jit_write(uint32_t addr, uint32_t val, uint32_t kind_bd, uint32_t pc); /* kind: 0 sw, 1 sh, 2 sb; 1 if the block must stop */
void psx_jit_reset(void);
int psx_jit_take_stale(void);                             /* 1 once if a store changed the code of the block that is running */
void psx_jit_ram_written(uint32_t off, uint32_t len);     /* a DMA or a store put new bytes into RAM: drop the code compiled from them */
void psx_jit_sr_changed(void);                            /* the cache isolation bit may have changed */
int psx_jit_run_block(void);                              /* runs one block at the PC (cycles used), or 0 if there is none */
extern uint8_t psx_jit_pageflags[];
/* what the CPU did in the frame: blocks run, instructions in blocks, instructions by the interpreter, helper calls (the compiler leaves an instruction to the interpreter; a load or store takes the slow way) */
extern uint32_t psx_stat_exec_hist[128], psx_stat_slow_hist[6];
extern uint32_t psx_stat_blocks, psx_stat_jit_instr, psx_stat_interp, psx_stat_exec, psx_stat_rslow, psx_stat_wslow;
/* psx_gte.c */
void psx_gte_reset(void);
void psx_gte_command(uint32_t op);
uint32_t psx_gte_get_data(int n);
void psx_gte_set_data(int n, uint32_t v);
uint32_t psx_gte_get_ctl(int n);
void psx_gte_set_ctl(int n, uint32_t v);
/* psx_gpu.c */
void psx_gpu_reset(void);
void psx_gp0_write(uint32_t v);
void psx_gp1_write(uint32_t v);
uint32_t psx_gpu_read(void);
uint32_t psx_gpu_stat(void);
void psx_gpu_dma_to(const uint32_t *words, int n);           /* ram to gpu */
int psx_gpu_dma_from(uint32_t *words, int n);                /* gpu to ram */
const uint16_t *psx_gpu_picture(int *w, int *h);
extern int psx_gpu_hres(void);
extern int psx_gpu_line, psx_gpu_field;
extern int psx_irq_poll;
uint32_t psx_now_us(void);                 /* the host clock in microseconds (0 without one) */
extern uint32_t psx_gpu_us;                /* microseconds spent drawing since the last frame (psx_gpu.c, read by psx_bus.c) */
/* psx_bus.c */
void psx_irq_raise(int n);
void psx_dbg_unhandled(uint32_t addr);
/* psx_spu.c */
void psx_spu_reset(void);
uint16_t psx_spu_read(uint32_t reg);
void psx_spu_write(uint32_t reg, uint16_t v);
void psx_spu_run(uint32_t cycles);
void psx_spu_dma_write(const uint8_t *src, int n);
void psx_spu_dma_read(uint8_t *dst, int n);
/* psx_cd.c */
void psx_cd_reset(void);
uint32_t psx_cd_read(uint32_t reg);
void psx_cd_write(uint32_t reg, uint32_t v);
void psx_cd_run(uint32_t cycles);
void psx_cd_set_disc(const psx_disc_t *d);
int psx_cd_dma_read(uint8_t *dst, int n);
void psx_cd_debug(psx_debug_t *d);

#endif
