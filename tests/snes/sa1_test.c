/* The tests of the SA-1 co-processor (core/src/bm/snes_sa1.c): small original programs on a made-up ROM (no commercial ROM). The S-CPU program releases the SA-1
 * CPU; the SA-1 program uses the math unit, the BW-RAM window, the bit processor, the I-RAM and the interrupts between the two CPUs. Run: make test-snes-sa1. */
#include "snes.h"
#include "snes_int.h"
#include "snes_sa1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)
#define CHECKV(name, got, want) do { count++; if ((unsigned)(got) != (unsigned)(want)) { fails++; printf("FAIL %s: got %x want %x\n", name, (unsigned)(got), (unsigned)(want)); } } while (0)

#define ROM_SIZE 0x40000
static uint8_t rom[ROM_SIZE];

static void put(uint32_t off, const uint8_t *c, size_t n) { memcpy(rom + off, c, n); }

/* an SA-1 LoROM image: the S-CPU code at $00:8000, the SA-1 code at $00:9000, its IRQ handler at $00:9400, data at $00:A000 */
static void make_rom(const uint8_t *scpu, size_t ns, const uint8_t *sa1, size_t na)
{
    memset(rom, 0, sizeof rom);
    for (size_t i = 0x2000; i < 0x2010; i++) rom[i] = (uint8_t)(i == 0x2000 ? 0xA5 : i == 0x2001 ? 0x5A : i == 0x2002 ? 0xFF : 0x11);
    put(0, scpu, ns);
    put(0x1000, sa1, na);
    static const uint8_t handler[] = { 0xA9, 0x80, 0x8D, 0x0B, 0x22, 0xE6, 0x01, 0x40 };    /* LDA #$80; STA $220B (clear the IRQ flag); INC $01; RTI */
    put(0x1400, handler, sizeof handler);
    memset(rom + 0x7FC0, ' ', 21);
    memcpy(rom + 0x7FC0, "SA1 TEST", 8);
    rom[0x7FD5] = 0x23; rom[0x7FD6] = 0x34; rom[0x7FD7] = 0x08; rom[0x7FD8] = 0x05;     /* SA-1 map mode, chipset SA-1 + RAM + battery, 256 KB ROM, 32 KB BW-RAM */
    rom[0x7FDC] = 0xFF; rom[0x7FDD] = 0xFF; rom[0x7FDE] = 0; rom[0x7FDF] = 0;
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    const char *why = NULL;
    if (snes_load(rom, sizeof rom, &why)) printf("load failed: %s\n", why ? why : "?");
}
static void frames(int n) { for (int i = 0; i < n; i++) snes_frame(0, 0, 0); }

static const uint8_t S_CPU[] = {
    0x78,                                   /* SEI */
    0xA9, 0x00, 0x8D, 0x03, 0x22,           /* CRV = $9000 */
    0xA9, 0x90, 0x8D, 0x04, 0x22,
    0xA9, 0x00, 0x8D, 0x07, 0x22,           /* CIV = $9400 */
    0xA9, 0x94, 0x8D, 0x08, 0x22,
    0xA9, 0x80, 0x8D, 0x01, 0x22,           /* SIE: the SA-1 may interrupt the S-CPU */
    0xA9, 0xFF, 0x8D, 0x29, 0x22,           /* SIWP: the S-CPU may write all of the I-RAM */
    0xA9, 0x00, 0x8D, 0x00, 0x22,           /* CCNT = 0: the SA-1 CPU runs */
    0xAD, 0x00, 0x30,                       /* wait: LDA $3000 (I-RAM $00) */
    0xC9, 0x55,                             /* CMP #$55 */
    0xD0, 0xF9,                             /* BNE wait */
    0xCB,                                   /* WAI */
    0x80, 0xFD,                             /* BRA WAI */
};
#define WR(addr, v) 0xA9, (v), 0x8D, (uint8_t)(addr), (uint8_t)((addr) >> 8)
#define RD(addr, dst) 0xAD, (uint8_t)(addr), (uint8_t)((addr) >> 8), 0x8D, (uint8_t)(dst), (uint8_t)((dst) >> 8)
static const uint8_t SA1_CODE[] = {
    WR(0x222A, 0xFF), WR(0x2227, 0x80),                                                              /* CIWP: the SA-1 may write all of the I-RAM; CWBE: it may write the BW-RAM */
    WR(0x2250, 0x00), WR(0x2251, 0x34), WR(0x2252, 0x12), WR(0x2253, 0x10), WR(0x2254, 0x00),      /* $1234 * $0010 = $12340 */
    RD(0x2306, 0x6000), RD(0x2307, 0x6001), RD(0x2308, 0x6002),
    WR(0x2250, 0x01), WR(0x2251, 0xE8), WR(0x2252, 0x03), WR(0x2253, 0x07), WR(0x2254, 0x00),      /* 1000 / 7 = 142, remainder 6 */
    RD(0x2306, 0x6003), RD(0x2308, 0x6004),
    WR(0x2250, 0x00), WR(0x2251, 0xFE), WR(0x2252, 0xFF), WR(0x2253, 0x03), WR(0x2254, 0x00),      /* -2 * 3 = -6 */
    RD(0x2306, 0x6006), RD(0x2309, 0x6007),
    WR(0x2250, 0x01), WR(0x2251, 0xF9), WR(0x2252, 0xFF), WR(0x2253, 0x02), WR(0x2254, 0x00),      /* -7 / 2: quotient -4, remainder 1 (not negative) */
    RD(0x2306, 0x6008), RD(0x2307, 0x6009), RD(0x2308, 0x600A),
    WR(0x2250, 0x02), WR(0x2251, 0x00), WR(0x2252, 0x01), WR(0x2253, 0x00), WR(0x2254, 0x01),      /* accumulate $100 * $100 twice */
    WR(0x2253, 0x00), WR(0x2254, 0x01),
    RD(0x2308, 0x600B),
    WR(0x2259, 0x00), WR(0x225A, 0xA0), WR(0x225B, 0x00), WR(0x2258, 0x04),                          /* the bit processor reads $00:A000, 4 bits in */
    RD(0x230C, 0x6005),
    WR(0x2230, 0x84), WR(0x2232, 0x00), WR(0x2233, 0xA0), WR(0x2234, 0x00), WR(0x2238, 0x04), WR(0x2239, 0x00),   /* DMA ROM -> BW-RAM, 4 bytes from $00:A000 */
    WR(0x2235, 0x20), WR(0x2236, 0x00), WR(0x2237, 0x00),
    WR(0x2225, 0x80), WR(0x6100, 0x0B), WR(0x6101, 0x0A), WR(0x2225, 0x00),                         /* two 4 bit pixels through the bitmap window */
    WR(0x2209, 0x81),                                                                                /* a message to the S-CPU and an IRQ for it */
    WR(0x0000, 0x55),                                                                                /* I-RAM $00: the S-CPU waits for it */
    WR(0x220A, 0x80),                                                                                /* CIE: the IRQ of the S-CPU is enabled */
    0x58,                                                                                            /* CLI */
    0xCB,                                                                                            /* WAI */
    0x80, 0xFD,                                                                                      /* BRA WAI */
};

static void basic_tests(void)
{
    make_rom(S_CPU, sizeof S_CPU, SA1_CODE, sizeof SA1_CODE);
    CHECK("header: an SA-1 cartridge", S.sa1 == 1);
    CHECKV("BW-RAM size", S.sram_size, 32768);
    frames(4);
    printf("sa1 running %d, instructions %u\n", sa1_running(), (unsigned)sa1_instr_count());
    CHECKV("the S-CPU saw the I-RAM write of the SA-1", snes_read(0x003000), 0x55);
    CHECKV("multiply low", snes_read(0x400000), 0x40);
    CHECKV("multiply mid", snes_read(0x400001), 0x23);
    CHECKV("multiply high", snes_read(0x400002), 0x01);
    CHECKV("divide: quotient", snes_read(0x400003), 142);
    CHECKV("divide: remainder", snes_read(0x400004), 6);
    CHECKV("bit processor", snes_read(0x400005), 0xAA);
    CHECKV("multiply negative: low", snes_read(0x400006), 0xFA);
    CHECKV("multiply negative: byte 3", snes_read(0x400007), 0xFF);
    CHECKV("divide negative: quotient low", snes_read(0x400008), 0xFC);
    CHECKV("divide negative: quotient high", snes_read(0x400009), 0xFF);
    CHECKV("divide negative: remainder", snes_read(0x40000A), 1);
    CHECKV("accumulate", snes_read(0x40000B), 2);
    CHECKV("DMA ROM to BW-RAM", snes_read(0x400020) | (snes_read(0x400021) << 8) | (snes_read(0x400022) << 16), 0xFF5AA5);
    CHECKV("DMA: the fourth byte", snes_read(0x400023), 0x11);
    CHECKV("BW-RAM bitmap window: two 4 bit pixels", snes_read(0x400080), 0xAB);
    CHECKV("message and flags to the S-CPU", snes_read(0x002300), 0x81 | 0x80);        /* IRQ flag (bit 7) and the message 1 (bits 0-3) */
    CHECK("the SA-1 holds the S-CPU IRQ line", S.irq_pending == 1);
    snes_write(0x002202, 0x80);
    CHECKV("the S-CPU clears the flag", snes_read(0x002300) & 0x80, 0);
    /* the S-CPU interrupts the SA-1: CCNT bit 7; the handler at $9400 counts */
    snes_write(0x002200, 0x80);
    frames(2);
    CHECKV("the SA-1 took the IRQ of the S-CPU", snes_read(0x003001), 1);
    snes_write(0x002200, 0x80);
    frames(2);
    CHECKV("and a second one", snes_read(0x003001), 2);
    /* the S-CPU writes the BW-RAM when SWBE is on */
    snes_write(0x002226, 0x80);
    snes_write(0x400010, 0x77);
    CHECKV("the S-CPU writes the BW-RAM", snes_read(0x400010), 0x77);
    /* the ROM windows: CXB = 1 maps the second MB to $C0; the image is 256 KB, so it mirrors: the byte at $C0:0000 is rom[0] */
    CHECKV("ROM at $C0", snes_read(0xC00000), 0x78);
    snes_write(0x002220, 0x80 | 0x01);
    CHECKV("ROM window mirrors the 256 KB image", snes_read(0xC00000), 0x78);
}

int main(void)
{
    basic_tests();
    printf("sa1: %d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
