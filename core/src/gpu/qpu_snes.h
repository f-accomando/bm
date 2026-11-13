/*
 * The SNES PPU on the QPUs of the V3D (Pi Zero W only; docs/progress/snes-gpu.md, items G2 to G8).
 *
 * The ARM keeps the registers, the video RAM and the timing. For a run of lines it gives the QPUs a copy of the
 * video RAM, the colour table and two words for each line; the QPUs write the 256 x 224 RGB565 lines. Every call has
 * an ARM version that makes the same lines, and it runs when the QPUs are off or a run fails (G8). Original code.
 *
 * qpu_snes_m7: the picture of a frame with only Mode 7 BG1 on, equal byte for byte to ppu_line (tests/gpu/test_snes_m7.c).
 * qpu_snes_bench: the micro-benchmark of the V3D that decides what is worth moving (one line of numbers).
 */
#ifndef QPU_SNES_H
#define QPU_SNES_H

#include <stddef.h>
#include <stdint.h>

#define QPU_SNES_LINES 224

/* Starts the V3D, loads the programs and checks Mode 7 against the ARM version on a synthetic state.
 * 0 if the QPUs may draw Mode 7; -1 else (the ARM version still works). Once. */
int qpu_snes_init(void);
const char *qpu_snes_status(void);
int qpu_snes_m7_ok(void);

/* Memory the caller fills before qpu_snes_m7: a copy of the 64 KB of video RAM, the 256 colours as RGB565 (the
 * brightness applied) and two words for each line of the run (X and Y at the start of the line, as draw_mode7 makes
 * them). NULL until qpu_snes_init has run. */
typedef struct {
    uint8_t *vram;
    uint16_t *lut;
    int32_t *tab;
} qpu_snes_buf_t;
const qpu_snes_buf_t *qpu_snes_bufs(void);

/* `lines` lines (1 .. QPU_SNES_LINES) of Mode 7 into dst (256 pixels a line) from the buffers, with the matrix words
 * a and c and the register $211A. 1 if the QPUs made them, 0 if the ARM did. */
int qpu_snes_m7(uint16_t *dst, int lines, int a, int c, int sel);
/* the ARM version (also the reference of the probe) */
void qpu_snes_m7_arm(uint16_t *dst, int lines, int a, int c, int sel, const uint8_t *vram, const uint16_t *lut, const int32_t *tab);

typedef struct {
    uint32_t runs, on_qpu, on_arm, failures;
} qpu_snes_stats_t;
extern qpu_snes_stats_t qpu_snes_stats;

/* The micro-benchmark. iters scales the gathers (the kernel uses 0 for the default), big is the size of the SDRAM
 * region of the slow tests in bytes (a power of two; 0: 8 MB). Writes one line (about 300 characters):
 *   snes gpu bench: job N us, jobs12 N us, copy64k N us, gather64k N ns/word, gather8m N ns/word, stream N MB/s,
 *   arm-slowdown N% (chunks A/B)
 * 0 if every part ran; -1 if the QPUs are not usable (the line says why). us: a microsecond clock. */
int qpu_snes_bench(uint32_t (*us)(void), uint32_t iters, uint32_t big, char *line, size_t n);

#endif
