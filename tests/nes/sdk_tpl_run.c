/* sdk_tpl_run.c: runs the NES start file of the Shell SDK (core/src/script/sdk_tpl_nes.s, built by
 * tests/snes/sdk_tpl_build.lua) in the NES core: the screen is blue, the NMI counts the frames, the pad reaches
 * pad, pad_old and pad_new, sprites are off.   nes_sdk_tpl_run rom.nes      (make test-sdk-templates) */
#include "nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) { puts("usage: nes_sdk_tpl_run rom"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("cannot open the ROM"); return 2; }
    static uint8_t rom[1 << 20];
    const size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    const char *why = NULL;
    if (nes_load(rom, n, &why)) { printf("load: %s\n", why ? why : "?"); return 1; }
    nes_reset();
    for (int i = 0; i < 20; i++) nes_frame(0, 0, 1);
    const uint8_t *ram = nes_ram();
    const uint16_t *pic = nes_picture();
    const int frames = ram[3];
    CHECK("the backdrop colour is $11 (blue)", nes_palette()[0] == 0x11);
    CHECK("the NMI counts the frames", frames >= 17 && frames <= 20);
    CHECK("the picture is one colour", pic[256 * 100 + 10] == pic[256 * 200 + 240] && pic[256 * 100 + 10] == pic[0]);
    const unsigned c = pic[256 * 100 + 10];                          /* RGB565 */
    CHECK("the colour is blue", (c & 0x1F) > (c >> 11) && (c & 0x1F) > ((c >> 5) & 0x3F) / 2);
    CHECK("no button: pad is 0", ram[0] == 0);
    CHECK("the sprites are off the screen", ram[0x200] == 0xFF && ram[0x2FC] == 0xFF);
    for (int i = 0; i < 3; i++) nes_frame(NES_PAD_A | NES_PAD_START, 0, 1);
    CHECK("A and Start: pad = $90", ram[0] == 0x90);
    CHECK("held: pad_old = pad", ram[1] == 0x90);
    CHECK("held: pad_new = 0", ram[2] == 0);
    nes_frame(NES_PAD_A | NES_PAD_START | NES_PAD_UP, 0, 1);
    nes_frame(NES_PAD_A | NES_PAD_START | NES_PAD_UP, 0, 1);
    nes_frame(NES_PAD_A | NES_PAD_START | NES_PAD_RIGHT, 0, 1);
    CHECK("Right alone is new: pad_new = $01", ram[2] == 0x01 && ram[0] == 0x91);
    printf("sdk_tpl_run (NES): %d frames counted, %s\n", frames, fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
