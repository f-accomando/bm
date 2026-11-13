/* learn_run.c: run one example of a Learn tab lesson in the SNES core and check what it did (make test-snes-learn).
 *   snes_learn_run rom frames pad_hex [check ...]
 * A check is  <kind><hex address><op><hex value>  with op = or >:
 *   w = work RAM, v = video RAM (byte address), o = OAM, cg = colour memory (16 bit colour n)
 * or  pic=1  (the picture has a pixel that is not the colour of the corner).
 * The program also fails when the CPU ends outside the ROM area ($8000-$FFFF of a bank) or the ROM does not load. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 4) { puts("usage: snes_learn_run rom frames pad [check ...]"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("cannot open the ROM"); return 2; }
    static uint8_t rom[1 << 23];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    const char *why = NULL;
    if (snes_load(rom, n, &why)) { printf("load: %s\n", why ? why : "?"); return 1; }
    int frames = atoi(argv[2]);
    unsigned pad = (unsigned)strtoul(argv[3], NULL, 16), fails = 0;
    for (int i = 0; i < frames; i++) snes_frame((uint16_t)pad, 0, 1);
    uint32_t pc = 0;
    int fl = 0;
    cpu_info(&pc, &fl);
    if ((pc & 0xFFFF) < 0x8000) { fails++; printf("FAIL the CPU is at %06X, outside the ROM\n", pc); }
    int ln;
    const uint16_t *pic = snes_picture(&ln);
    for (int a = 4; a < argc; a++) {
        const char *c = argv[a];
        if (!strcmp(c, "pic=1")) {
            int seen = 0;
            if (pic && ln > 0) {
                uint16_t bg = pic[256 * (ln - 1) + 255];
                for (int i = 0; i < 256 * ln && !seen; i++) if (pic[i] != bg) seen = 1;
            }
            if (!seen) { fails++; printf("FAIL %s: the picture shows only the backdrop\n", c); }
            continue;
        }
        char kind = c[0];
        int cg = (c[0] == 'c' && c[1] == 'g');
        const char *p = c + (cg ? 2 : 1);
        char *end;
        unsigned long addr = strtoul(p, &end, 16);
        char op = *end;
        unsigned long want = strtoul(end + 1, NULL, 16), got = 0;
        if (op != '=' && op != '>') { fails++; printf("FAIL bad check %s\n", c); continue; }
        if (cg) got = S.cgram[(addr * 2) & 511] | (S.cgram[(addr * 2 + 1) & 511] << 8);
        else if (kind == 'w') got = S.wram[addr & 0x1FFFF];
        else if (kind == 'v') got = S.vram[addr & 0xFFFF];
        else if (kind == 'o') got = S.oam[addr % 544];
        else { fails++; printf("FAIL bad check %s\n", c); continue; }
        if ((op == '=' && got != want) || (op == '>' && got <= want)) {
            fails++;
            printf("FAIL %s: the value is %lX\n", c, got);
        }
    }
    printf("learn_run: %d frames, pc %06X, %s\n", frames, pc, fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
