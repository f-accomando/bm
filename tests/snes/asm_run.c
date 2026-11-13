/* asm_run.c: run a ROM built by core/src/script/snes_asm.lua in the SNES core for a few frames (make test-snes-asm).
 *   snes_asm_run rom.sfc frames
 * The ROM is the one of tests/snes/testrom.s: it sets the backdrop colour, turns the screen on and counts the NMI
 * in the work RAM at $0010. The program exits 0 when the machine did what the program says. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) { puts("usage: snes_asm_run rom frames"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("cannot open the ROM"); return 2; }
    static uint8_t rom[1 << 23];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    const char *why = NULL;
    if (snes_load(rom, n, &why)) { printf("load: %s\n", why ? why : "?"); return 1; }
    int frames = atoi(argv[2]), fails = 0;
    for (int i = 0; i < frames; i++) snes_frame(0, 0, 1);
    int ln;
    const uint16_t *pic = snes_picture(&ln);
    int cg0 = S.cgram[0] | (S.cgram[1] << 8);
    int count = S.wram[0x10];
#define CHECK(name, cond) do { if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)
    CHECK("the NMI runs every frame (counter at $0010)", count >= frames - 3 && count <= frames);
    CHECK("the DMA wrote the backdrop colour", cg0 == (2 | (4 << 5) | (12 << 10)));
    CHECK("the picture is not empty", pic && ln > 100 && pic[256 * 100 + 2] != 0);
    printf("asm_run: %d frames, NMI count %d, backdrop %04X, %s\n", frames, count, cg0, fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
