/* A probe for the bm PSX core on the PC: boots a BIOS image with a disc image and prints what the CD-ROM controller
 * sees. The BIOS and the disc are given on the command line (they are never part of the repository).
 *
 *   build/host/psx_boot_probe BIOS.bin DISC.bin [frames] [trace]      (DISC is a raw 2352 byte per sector .bin)
 *
 * With trace, every CD command and every interrupt is printed. At the end: the CD counters, the PC and a picture of
 * the display area as build/psx_probe.ppm. */
#include "psx_int.h"

#include <stdio.h>
#include <stdlib.h>

static FILE *disc_f;
static int disc_read(void *u, uint32_t lba, uint8_t *out)
{
    (void)u;
    if (fseek(disc_f, (long)lba * 2352, SEEK_SET) != 0) return -1;
    return fread(out, 1, 2352, disc_f) == 2352 ? 0 : -1;
}

int psx_probe_trace;

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s BIOS DISC [frames] [trace]\n", argv[0]); return 2; }
    static uint8_t bios[512 * 1024];
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(bios, 1, sizeof bios, f) != sizeof bios) { fprintf(stderr, "bad BIOS\n"); return 2; }
    fclose(f);
    disc_f = fopen(argv[2], "rb");
    if (!disc_f) { fprintf(stderr, "bad disc\n"); return 2; }
    fseek(disc_f, 0, SEEK_END);
    const long size = ftell(disc_f);
    const int frames = argc > 3 ? atoi(argv[3]) : 600;
    psx_probe_trace = argc > 4;
    const char *why = "";
    if (psx_load_bios(bios, sizeof bios, &why) != 0) { fprintf(stderr, "%s\n", why); return 2; }
    psx_disc_t d = { disc_read, NULL, (uint32_t)(size / 2352), 1 };
    psx_set_disc(&d);
    psx_reset();
    psx_set_disc(&d);
    for (int i = 0; i < frames; i++) {
        psx_frame(0, 0, 0);
        if (i % 60 == 59) {
            psx_debug_t s;
            psx_debug(&s);
            printf("t=%3ds pc=%08x gpu=%u cd cmds %u last %02X sectors %u lba %u istat %x imask %x\n", (i + 1) / 60, s.pc, s.gpu_cmds,
                   s.cd_cmds, s.cd_last_cmd, s.cd_sectors, s.cd_lba, s.istat, s.imask);
        }
    }
    int w, h;
    const uint16_t *p = psx_picture(&w, &h);
    FILE *o = fopen("build/psx_probe.ppm", "wb");
    if (o) {
        fprintf(o, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            const uint8_t px[3] = { (uint8_t)((p[i] >> 11) << 3), (uint8_t)(((p[i] >> 5) & 63) << 2), (uint8_t)((p[i] & 31) << 3) };
            fwrite(px, 1, 3, o);
        }
        fclose(o);
    }
    psx_debug_t s;
    psx_debug(&s);
    printf("tty: %s\n", psx_tty());
    printf("trace:");
    for (int i = 0; i < 12; i++) printf(" %02X.%06X", s.cd_trace[i] & 255, s.cd_trace[i] >> 8);
    printf("\n");
    return 0;
}
