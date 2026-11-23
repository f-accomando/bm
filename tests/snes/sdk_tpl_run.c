/* sdk_tpl_run.c: runs the SNES start file of the Shell SDK (core/src/script/sdk_tpl_snes.s, built by
 * tests/snes/sdk_tpl_build.lua) in the SNES core: the screen is blue, the NMI counts the frames, the pad
 * reaches pad, pad_old and pad_new.   snes_sdk_tpl_run rom.sfc      (make test-sdk-templates) */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) { puts("usage: snes_sdk_tpl_run rom"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("cannot open the ROM"); return 2; }
    static uint8_t rom[1 << 23];
    const size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    const char *why = NULL;
    if (snes_load(rom, n, &why)) { printf("load: %s\n", why ? why : "?"); return 1; }
    for (int i = 0; i < 20; i++) snes_frame(0, 0, 1);
    int ln;
    const uint16_t *pic = snes_picture(&ln);
    const int frames = S.wram[0x06];
    CHECK("the backdrop colour is blue ($7C00)", (S.cgram[0] | (S.cgram[1] << 8)) == 0x7C00);
    CHECK("the NMI counts the frames", frames >= 17 && frames <= 20);
    CHECK("the picture is one colour, and not black", pic && ln > 100 && pic[256 * 50 + 10] != 0 &&
          pic[256 * 50 + 10] == pic[256 * 100 + 200]);
    CHECK("the blue is blue", pic && (pic[256 * 50 + 10] & 0x1F) > 20 && (pic[256 * 50 + 10] >> 11) < 8);
    CHECK("no button: pad is 0", S.wram[0x00] == 0 && S.wram[0x01] == 0);
    for (int i = 0; i < 3; i++) snes_frame(SNES_PAD_A | SNES_PAD_START, 0, 1);
    CHECK("A and Start: pad = $1080", S.wram[0x00] == 0x80 && S.wram[0x01] == 0x10);
    CHECK("held: pad_old = pad", S.wram[0x02] == 0x80 && S.wram[0x03] == 0x10);
    CHECK("held: pad_new = 0", S.wram[0x04] == 0 && S.wram[0x05] == 0);
    snes_frame(SNES_PAD_A | SNES_PAD_START | SNES_PAD_UP, 0, 1);
    CHECK("a new button: pad_new = Up ($0800)", S.wram[0x04] == 0 && S.wram[0x05] == 0x08);
    snes_frame(SNES_PAD_A | SNES_PAD_START | SNES_PAD_UP, 0, 1);
    CHECK("then held: pad_new = 0", S.wram[0x04] == 0 && S.wram[0x05] == 0);
    printf("sdk_tpl_run (SNES): %d frames counted, %s\n", frames, fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
