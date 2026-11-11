/* The tests of the C core of bm SNES (src/bm/snes_*.c): the 65C816 on small programs, the bus, and the SPC700
 * with its boot protocol. Run: make test-snes. */
#include "snes_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)

static cpu_regs_t R;
static void prog(const uint8_t *code, size_t n)
{
    static uint8_t rom[0x8000];
    memset(rom, 0, sizeof rom);
    memcpy(rom, code, n);
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    snes_load(rom, sizeof rom, NULL);
    for (int i = 0; i < 10000 && !cpu_stopped; i++) cpu_step();
    cpu_get(&R);
}
#define PROG(...) do { static const uint8_t c_[] = { __VA_ARGS__ }; prog(c_, sizeof c_); } while (0)
#define FC (R.p & 1)
#define FZ ((R.p >> 1) & 1)
#define FN ((R.p >> 7) & 1)
#define FV ((R.p >> 6) & 1)
#define FX ((R.p >> 4) & 1)

static void cpu_tests(void)
{
    PROG(0x18,0xFB, 0xC2,0x30, 0xA9,0x34,0x12, 0x18, 0x69,0x01,0x00, 0xDB);
    CHECK("adc16", R.a == 0x1235 && !FC && !FZ && !FN && !R.e);
    PROG(0x18, 0xA9,0xFF, 0x69,0x01, 0xDB);
    CHECK("adc8 carry", (R.a & 0xFF) == 0 && FC && FZ);
    PROG(0x18, 0xA9,0x50, 0x69,0x50, 0xDB);
    CHECK("adc ov", (R.a & 0xFF) == 0xA0 && FV && FN && !FC);
    PROG(0x38, 0xA9,0x50, 0xE9,0xF0, 0xDB);
    CHECK("sbc", (R.a & 0xFF) == 0x60 && !FC && !FV);
    PROG(0xF8, 0x18, 0xA9,0x19, 0x69,0x28, 0xDB);
    CHECK("bcd add", (R.a & 0xFF) == 0x47 && !FC);
    PROG(0xF8, 0x18, 0xA9,0x99, 0x69,0x01, 0xDB);
    CHECK("bcd carry", (R.a & 0xFF) == 0x00 && FC);
    PROG(0xF8, 0x38, 0xA9,0x47, 0xE9,0x28, 0xDB);
    CHECK("bcd sub", (R.a & 0xFF) == 0x19 && FC);
    PROG(0x18,0xFB, 0xC2,0x10, 0xA2,0x05,0x00, 0xCA, 0xD0,0xFD, 0xDB);
    CHECK("dex loop", R.x == 0 && FZ);
    PROG(0x20,0x08,0x80, 0xA9,0x11, 0xDB, 0xEA, 0xEA, 0xA9,0x22, 0x60);
    CHECK("jsr rts", (R.a & 0xFF) == 0x11 && R.s == 0x1FF);
    PROG(0xA9,0x12, 0x48, 0xA9,0x00, 0x68, 0xAA, 0xEB, 0xDB);
    CHECK("pha pla", R.x == 0x12 && (R.a & 0xFF) == 0x00 && ((R.a >> 8) & 0xFF) == 0x12);
    PROG(0xA9,0x77, 0x85,0x10, 0xA2,0x02, 0xA9,0x55, 0x95,0x10, 0xA9,0x20, 0x85,0x20, 0xA9,0x00, 0x85,0x21,
         0xA0,0x01, 0xB1,0x20, 0xDB);
    CHECK("wram", S.wram[0x10] == 0x77 && S.wram[0x12] == 0x55);
    CHECK("(dp),y", (R.a & 0xFF) == 0x00);
    PROG(0xA9,0x5A, 0x8F,0x34,0x12,0x7E, 0xA9,0x00, 0xAF,0x34,0x12,0x7E, 0x8F,0x01,0x00,0x7F, 0xDB);
    CHECK("long", S.wram[0x1234] == 0x5A && S.wram[0x10001] == 0x5A && (R.a & 0xFF) == 0x5A);
    PROG(0x18,0xFB, 0xC2,0x30, 0xA9,0x01,0x02, 0x8D,0x00,0x01, 0xA9,0x03,0x04, 0x8D,0x02,0x01,
         0xA2,0x00,0x01, 0xA0,0x00,0x02, 0xA9,0x03,0x00, 0x54,0x7E,0x7E, 0xDB);
    CHECK("mvn", S.wram[0x200] == 1 && S.wram[0x201] == 2 && S.wram[0x202] == 3 && S.wram[0x203] == 4 && R.a == 0xFFFF);
    PROG(0x18, 0xA9,0x81, 0x0A, 0x6A, 0xDB);
    CHECK("asl ror", (R.a & 0xFF) == 0x81 && !FC && FN);
    PROG(0xA9,0x10, 0xC9,0x10, 0xF0,0x02, 0xA9,0xFF, 0xDB);
    CHECK("cmp beq", (R.a & 0xFF) == 0x10 && FC && FZ);
    PROG(0x18,0xFB, 0xC2,0x10, 0xA2,0x34,0x12, 0xE2,0x10, 0xDB);
    CHECK("sep x", R.x == 0x34 && FX);
    PROG(0xA9,0x0F, 0x85,0x30, 0xA9,0xF0, 0x04,0x30, 0xDB);
    CHECK("tsb", S.wram[0x30] == 0xFF && FZ);
    PROG(0x22,0x10,0x80,0x00, 0xA9,0x33, 0xDB, 0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA, 0xA9,0x44, 0x6B);
    CHECK("jsl rtl", (R.a & 0xFF) == 0x33);
    PROG(0xA9,0x07, 0x8D,0x02,0x42, 0xA9,0x09, 0x8D,0x03,0x42, 0xAD,0x16,0x42, 0xDB);
    CHECK("mul", (R.a & 0xFF) == 63);
    PROG(0xA9,0xE8, 0x8D,0x04,0x42, 0xA9,0x03, 0x8D,0x05,0x42, 0xA9,0x07, 0x8D,0x06,0x42, 0xAD,0x14,0x42, 0xDB);
    CHECK("div", (R.a & 0xFF) == 142);
    /* DMA: copy 4 bytes of WRAM to VRAM through $2118/$2119 (mode 1) */
    PROG(0x18,0xFB, 0xE2,0x20, 0xA9,0x11, 0x85,0x00, 0xA9,0x22, 0x85,0x01, 0xA9,0x33, 0x85,0x02, 0xA9,0x44, 0x85,0x03,
         0xA9,0x80, 0x8D,0x15,0x21,                         /* VMAIN: step 1 after the high byte */
         0xA9,0x01, 0x8D,0x00,0x43, 0xA9,0x18, 0x8D,0x01,0x43,
         0xA9,0x00, 0x8D,0x02,0x43, 0x8D,0x03,0x43, 0x8D,0x04,0x43,
         0xA9,0x04, 0x8D,0x05,0x43, 0xA9,0x00, 0x8D,0x06,0x43,
         0x8D,0x16,0x21, 0x8D,0x17,0x21,
         0xA9,0x01, 0x8D,0x0B,0x42, 0xDB);
    CHECK("dma to vram", S.vram[0] == 0x11 && S.vram[1] == 0x22 && S.vram[2] == 0x33 && S.vram[3] == 0x44);
}

/* the SPC700 through the ports, as a game does it */
static void upload(uint16_t addr, const uint8_t *b, int n)
{
    snes_load((const uint8_t[0x8000]){ 0 }, 0x8000, NULL);   /* a reset of every part */
    spc_reset();
    CHECK("greeting", spc_port_read(0) == 0xAA && spc_port_read(1) == 0xBB);
    spc_port_write(2, (uint8_t)addr); spc_port_write(3, (uint8_t)(addr >> 8));
    spc_port_write(1, 1); spc_port_write(0, 0xCC);
    CHECK("kick echo", spc_port_read(0) == 0xCC);
    for (int i = 0; i < n; i++) {
        spc_port_write(1, b[i]); spc_port_write(0, (uint8_t)i);
        CHECK("byte echo", spc_port_read(0) == (uint8_t)i);
    }
    spc_port_write(2, (uint8_t)addr); spc_port_write(3, (uint8_t)(addr >> 8));
    spc_port_write(1, 0); spc_port_write(0, (uint8_t)(n + 1));
    CHECK("jump echo", spc_port_read(0) == (uint8_t)(n + 1));   /* the game waits for it before it goes on */
}

static void spc_tests(void)
{
    static const uint8_t p1[] = { 0xE8,0x07, 0x8D,0x06, 0xCF, 0xC4,0xF4, 0x8D,0x00, 0xE8,0x64, 0xCD,0x07, 0x9E,
                                  0xC4,0xF5, 0xCB,0xF6, 0x2F,0xFE };
    upload(0x200, p1, sizeof p1);
    spc_run(1000);
    CHECK("mul", spc_port_read(0) == 0x2A);
    CHECK("div A", spc_port_read(1) == 14);
    CHECK("div Y", spc_port_read(2) == 2);
    static const uint8_t p2[] = { 0xE8,0x42, 0xC4,0xF4, 0xE4,0xF5, 0xF0,0xFC, 0x60, 0x88,0x01, 0xC4,0xF6, 0x2F,0xFE };
    upload(0x300, p2, sizeof p2);
    spc_run(500);
    CHECK("hello", spc_port_read(0) == 0x42);
    spc_port_write(1, 5);
    spc_run(500);
    CHECK("answer", spc_port_read(2) == 6);
    static const uint8_t p3[] = { 0x8F,0x01,0xFA, 0x8F,0x01,0xF1, 0xE4,0xFD, 0xF0,0xFC, 0xC4,0xF7, 0x2F,0xFE };
    upload(0x400, p3, sizeof p3);
    for (int i = 0; i < 20; i++) spc_run(200);
    CHECK("timer", spc_port_read(3) != 0);
    static const uint8_t p4[] = { 0x8F,0x80,0xF1, 0x5F,0xC0,0xFF };
    upload(0x500, p4, sizeof p4);
    spc_run(200);
    CHECK("boot ROM again", spc_port_read(0) == 0xAA && spc_port_read(1) == 0xBB);
}

extern uint8_t *spc_ram_ptr(void);

/* the DSP: a looping square-wave sample plays on voice 0 and comes out of the ring as sound that changes sign */
static void dsp_tests(void)
{
    static const uint8_t prog[] = {
        0x8F,0x5D,0xF2, 0x8F,0x02,0xF3,                 /* DIR = $02 (the directory at $0200) */
        0x8F,0x04,0xF2, 0x8F,0x00,0xF3,                 /* voice 0: SRCN 0 */
        0x8F,0x05,0xF2, 0x8F,0x00,0xF3,                 /* ADSR1 0: GAIN is used */
        0x8F,0x07,0xF2, 0x8F,0x7F,0xF3,                 /* GAIN: direct, full */
        0x8F,0x00,0xF2, 0x8F,0x7F,0xF3,                 /* VOL L */
        0x8F,0x01,0xF2, 0x8F,0x7F,0xF3,                 /* VOL R */
        0x8F,0x02,0xF2, 0x8F,0x00,0xF3,                 /* PITCH low */
        0x8F,0x03,0xF2, 0x8F,0x10,0xF3,                 /* PITCH high: $1000, one sample a step */
        0x8F,0x0C,0xF2, 0x8F,0x7F,0xF3,                 /* master volume L */
        0x8F,0x1C,0xF2, 0x8F,0x7F,0xF3,                 /* master volume R */
        0x8F,0x6C,0xF2, 0x8F,0x00,0xF3,                 /* FLG: not muted */
        0x8F,0x4C,0xF2, 0x8F,0x01,0xF3,                 /* KON voice 0 */
        0x2F,0xFE,
    };
    upload(0x400, prog, sizeof prog);
    uint8_t *ram = spc_ram_ptr();
    ram[0x200] = 0x00; ram[0x201] = 0x03; ram[0x202] = 0x00; ram[0x203] = 0x03;    /* start and loop: $0300 */
    ram[0x300] = 0xC3;                                                              /* range 12, no filter, loop and end */
    for (int i = 0; i < 4; i++) { ram[0x301 + i] = 0x77; ram[0x305 + i] = 0x88; }  /* 8 samples of +7, 8 of -8 */
    for (int i = 0; i < 200; i++) spc_run(65);
    float out[2 * 128];
    memset(out, 0, sizeof out);
    snes_audio_mix(out, 128, 1.0f);
    float hi = 0, lo = 0;
    for (int i = 0; i < 128; i++) { if (out[2 * i] > hi) hi = out[2 * i]; if (out[2 * i] < lo) lo = out[2 * i]; }
    CHECK("dsp makes sound", hi > 0.05f && lo < -0.05f);
    CHECK("dsp left and right", out[100] == out[101]);
}

/* The scroll registers take two writes through one latch: the low byte first, then the high byte. The value must be what the game wrote, for
 * every low byte: the second write takes bits 0 to 2 of the first from the first value (a mask to 10 bits too early lost bit 2). */
static void scroll_tests(void)
{
    int bad = 0;
    for (int lo = 0; lo < 256; lo++)
        for (int hi = 0; hi < 4; hi++) {
            snes_ppu_write(0x210D, (uint8_t)lo); snes_ppu_write(0x210D, (uint8_t)hi);
            if ((S.hofs[0] & 0x3FF) != (hi << 8 | lo)) bad++;
            snes_ppu_write(0x2110, (uint8_t)lo); snes_ppu_write(0x2110, (uint8_t)hi);
            if ((S.vofs[1] & 0x3FF) != (hi << 8 | lo)) bad++;
        }
    CHECK("scroll registers: all 1024 values", bad == 0);
}

/* C1: the time of a frame in its parts. A fake clock that follows the emulated time (a master clock is a quarter of a microsecond): the CPU part is nearly all of the frame, and one frame in 8 is timed. */
extern int snes_clk;
static uint32_t fake_clock(void) { return (uint32_t)(((S.frames * 262 + S.line) * 1364 + snes_clk) / 4); }   /* the emulated time: a read costs nothing */
static uint32_t tick_clock(void) { static uint32_t t; return t += 3; }     /* a clock that only costs 3 microseconds a read: the cost of the reads must come off */
static void timing_tests(void)
{
    static const uint8_t loop[] = { 0x80, 0xFE };           /* BRA -2: the CPU spins */
    prog(loop, sizeof loop);
    snes_set_clock(fake_clock);
    for (int i = 0; i < 24; i++) snes_frame(0, 0, 1);
    snes_debug_t d;
    snes_debug(&d);
    snes_set_clock(NULL);
    CHECK("timing: instructions a frame", d.instr > 5000);
    CHECK("timing: cpu time a frame", d.us_cpu > 200);
    CHECK("timing: no DMA, no HDMA", d.us_dma == 0 && d.us_hdma == 0);
    CHECK("timing: the parts fit in the frame", d.us_cpu + d.us_ppu + d.us_spc <= d.us_frame + 2);
    /* the parts of the PPU: averages of a drawn frame, with the reads taken off (a clock that does nothing but cost reads gives no time to any part) */
    snes_set_clock(tick_clock);
    for (int i = 0; i < 24; i++) snes_frame(0, 0, 1);
    snes_debug(&d);
    snes_set_clock(NULL);
    CHECK("timing: the parts of the PPU are drawn-frame averages without the cost of the reads",
          d.us_bg < 40 && d.us_obj < 40 && d.us_win < 40 && d.us_comp < 40 && d.us_mix < 40);
    snes_debug(&d);
    CHECK("timing: a second read gives the same parts (no reset by the read)", d.us_bg < 40);
}

int main(void)
{
    scroll_tests();
    timing_tests();
    cpu_tests();
    spc_tests();
    dsp_tests();
    printf("%d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
