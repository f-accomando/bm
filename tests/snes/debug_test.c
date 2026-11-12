/* The tests of the debug API (snes_peek8, snes_poke8, snes_dbg_regs, snes_dbg_step, snes_dbg_break, snes_frame_break; docs/api/snes.md, "Debug API"):
 * a small original LoROM program. Run: make test-snes-debug. */
#include "snes.h"
#include "snes_int.h"

#include <stdio.h>
#include <string.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)
#define CHECKV(name, got, want) do { count++; if ((unsigned)(got) != (unsigned)(want)) { fails++; printf("FAIL %s: got %x want %x\n", name, (unsigned)(got), (unsigned)(want)); } } while (0)

static uint8_t rom[0x8000];

static const uint8_t PROG[] = {
    0x18,                   /* 8000 CLC */
    0xFB,                   /* 8001 XCE: native mode */
    0xC2, 0x30,             /* 8002 REP #$30 */
    0xA9, 0x34, 0x12,       /* 8004 LDA #$1234 */
    0xA2, 0x78, 0x56,       /* 8007 LDX #$5678 */
    0x8D, 0x00, 0x03,       /* 800A STA $0300 */
    0xE8,                   /* 800D INX */
    0x80, 0xFE,             /* 800E BRA 800E */
};

static void boot(void)
{
    memset(rom, 0, sizeof rom);
    memcpy(rom, PROG, sizeof PROG);
    memset(rom + 0x7FC0, ' ', 21);
    memcpy(rom + 0x7FC0, "DEBUG TEST", 10);
    rom[0x7FD5] = 0x20; rom[0x7FD7] = 0x08;
    rom[0x7FDC] = 0xFF; rom[0x7FDD] = 0xFF;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    const char *why = NULL;
    if (snes_load(rom, sizeof rom, &why)) printf("load failed: %s\n", why ? why : "?");
    snes_dbg_break(-1);
}

int main(void)
{
    boot();
    snes_dbg_regs_t r;

    /* step: the expected PC and registers after each instruction */
    snes_dbg_regs(&r);
    CHECKV("reset pc", r.pc, 0x8000); CHECK("reset emulation", r.e);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("clc pc", r.pc, 0x8001);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("xce pc", r.pc, 0x8002); CHECK("xce native", !r.e);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("rep pc", r.pc, 0x8004); CHECKV("rep p", r.p, 0x05)   /* I and the carry that XCE took from the emulation bit */;
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("lda pc", r.pc, 0x8007); CHECKV("lda a", r.a, 0x1234);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("ldx pc", r.pc, 0x800A); CHECKV("ldx x", r.x, 0x5678);
    const uint64_t c0 = r.cycle;
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("sta pc", r.pc, 0x800D);
    CHECK("sta moved the clock", r.cycle > c0);
    CHECKV("sta wram lo", snes_peek8(0x000300), 0x34); CHECKV("sta wram hi", snes_peek8(0x000301), 0x12);
    CHECKV("sta mirror", snes_peek8(0x7E0300), 0x34);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("inx pc", r.pc, 0x800E); CHECKV("inx x", r.x, 0x5679);
    CHECKV("pb", r.pb, 0);

    /* peek and poke */
    CHECKV("peek rom", snes_peek8(0x008000), 0x18);
    CHECKV("peek rom mirror", snes_peek8(0x808000), 0x18);
    snes_poke8(0x7E1000, 0xAB); CHECKV("poke wram", snes_peek8(0x7E1000), 0xAB);
    snes_poke8(0x008000, 0x00); CHECKV("poke rom does nothing", snes_peek8(0x008000), 0x18);
    /* no side effect: $4210 keeps its flag, $2180 keeps the address, $4211 keeps its flag, the OAM address stays */
    S.nmiflag = 1; CHECKV("peek 4210", snes_peek8(0x004210), 0x82); CHECKV("4210 flag kept", S.nmiflag, 1);
    S.timeup = 1; CHECKV("peek 4211", snes_peek8(0x004211), 0x80); CHECKV("4211 flag kept", S.timeup, 1);
    S.wram[0x10] = 0x55; S.wmadd = 0x10; CHECKV("peek 2180", snes_peek8(0x002180), 0x55); CHECKV("2180 address kept", S.wmadd, 0x10);
    S.oam[3] = 0x66; S.oamaddr = 3; CHECKV("peek 2138", snes_peek8(0x002138), 0x66); CHECKV("2138 address kept", S.oamaddr, 3);
    S.vmpre = 0x7799; CHECKV("peek 2139", snes_peek8(0x002139), 0x99); CHECKV("peek 213A", snes_peek8(0x00213A), 0x77);
    { const int clk = snes_clk; snes_peek8(0x004210); snes_poke8(0x7E1001, 1); CHECKV("no clock", snes_clk, clk); }
    CHECKV("a read still clears", (snes_read(0x004210), S.nmiflag), 0);

    /* a breakpoint stops the frame; the next call goes on with the same frame */
    boot();
    CHECKV("break count", snes_dbg_break(0x00800D), 1);
    CHECKV("break twice", snes_dbg_break(0x00800D), 1);
    const uint32_t f0 = S.frames;
    CHECKV("hit pc", snes_frame_break(0, 0, 0, 1), 0x00800D);
    snes_dbg_regs(&r); CHECKV("hit x", r.x, 0x5678); CHECKV("hit a", r.a, 0x1234); CHECKV("hit pc reg", r.pc, 0x800D);
    CHECKV("frame not done", S.frames, f0);
    CHECKV("frame done after the hit", snes_frame_break(0, 0, 0, 1), -1);
    snes_dbg_regs(&r); CHECKV("went on x", r.x, 0x5679); CHECKV("frame counted", S.frames, f0 + 1);
    /* a breakpoint in the loop is hit again and again; clearing it lets the frame end */
    snes_dbg_break(0x00800E);
    CHECKV("loop hit", snes_frame_break(0, 0, 0, 1), 0x00800E);
    CHECKV("loop hit again", snes_frame_break(0, 0, 0, 1), 0x00800E);
    CHECKV("break full", (snes_dbg_break(-1), snes_dbg_break(0x9000)), 1);
    snes_dbg_break(-1);
    CHECKV("plain frame finishes it", (snes_frame(0, 0, 0), S.frames), f0 + 2);
    /* a plain frame while a breakpoint is armed does not stop */
    snes_dbg_break(0x00800E);
    snes_frame(0, 0, 0); CHECKV("plain frame ignores break", S.frames, f0 + 3);
    /* the table: 8 breakpoints, the ninth is refused */
    snes_dbg_break(-1);
    for (int i = 0; i < CPU_BP_MAX; i++) CHECKV("fill", snes_dbg_break(0x9000 + i), i + 1);
    CHECKV("full", snes_dbg_break(0x9100), -1);
    snes_dbg_break(-1);

    /* step then breakpoint: a step from a breakpoint does not hit it again */
    boot();
    snes_dbg_break(0x008002);
    CHECKV("run to 8002", snes_frame_break(0, 0, 0, 1), 0x008002);
    snes_dbg_step(); snes_dbg_regs(&r); CHECKV("step from bp", r.pc, 0x8004);
    snes_dbg_break(-1);

    printf("%d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
