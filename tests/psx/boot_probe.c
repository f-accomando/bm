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
extern uint32_t psx_watch_lo, psx_watch_hi;

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
    if (getenv("PSX_WATCH")) { psx_watch_lo = (uint32_t)strtoul(getenv("PSX_WATCH"), NULL, 16); psx_watch_hi = psx_watch_lo + (getenv("PSX_WATCH_LEN") ? (uint32_t)strtoul(getenv("PSX_WATCH_LEN"), NULL, 16) : 4); }
    const char *why = "";
    if (psx_load_bios(bios, sizeof bios, &why) != 0) { fprintf(stderr, "%s\n", why); return 2; }
    psx_disc_t d = { disc_read, NULL, (uint32_t)(size / 2352), 1 };
    psx_set_disc(&d);
    psx_reset();
    psx_set_disc(&d);
    psx_log_bios_tty(1);
    for (int i = 0; i < frames; i++) {
        psx_frame(0, 0, 0);
        if (i % 60 == 59) {
            psx_debug_t s;
            psx_debug(&s);
            printf("t=%3ds pc=%08x gpu=%u cd cmds %u last %02X sectors %u lba %u istat %x imask %x\n", (i + 1) / 60, s.pc, s.gpu_cmds,
                   s.cd_cmds, s.cd_last_cmd, s.cd_sectors, s.cd_lba, s.istat, s.imask);
        }
    }
    if (getenv("PSX_BREAK")) {                         /* run until the PC reaches an address, then print the argument registers */
        const uint32_t target = (uint32_t)strtoul(getenv("PSX_BREAK"), NULL, 16);
        for (long i = 0; i < 40000000 && psx_cpu.pc != target; i++) psx_cpu_run(2);
        printf("break at %08x: a0 %08x a1 %08x a2 %08x a3 %08x sp %08x\n", psx_cpu.pc, psx_cpu.r[4], psx_cpu.r[5], psx_cpu.r[6], psx_cpu.r[7], psx_cpu.r[29]);
        for (int k = 4; k < 8; k++) {
            const uint32_t v = psx_cpu.r[k] & 0x1FFFFFu;
            if (psx_cpu.r[k] >= 0x80000000u && psx_cpu.r[k] < 0x80200000u) printf("  a%d: \"%.60s\"\n", k - 4, (const char *)psx_ram + v);
        }
        printf("  ra %08x%c", psx_cpu.r[31], 10);
        printf("  s0 %08x s1 %08x s2 %08x s3 %08x s4 %08x s5 %08x gp %08x\n", psx_cpu.r[16], psx_cpu.r[17], psx_cpu.r[18], psx_cpu.r[19], psx_cpu.r[20], psx_cpu.r[21], psx_cpu.r[28]);
        if (getenv("PSX_DUMP")) {
            const uint32_t da = (uint32_t)strtoul(getenv("PSX_DUMP"), NULL, 16);
            for (int k = 0; k < 16; k++) printf("  [%08x] = %08x\n", da + 4u * (uint32_t)k, psx_peek32(da + 4u * (uint32_t)k));
        }
        if (getenv("PSX_STACK"))
            for (uint32_t o = 0; o < 0x400; o += 4) {
                const uint32_t w = psx_peek32(psx_cpu.r[29] + o);
                if (w >= 0x80010000u && w < 0x80070000u && !(w & 3)) printf("  sp+%03x: %08x\n", o, w);
            }
        const uint32_t sv = psx_peek32(psx_cpu.r[29] + 0x10);
        if (sv >= 0x80000000u && sv < 0x80200000u) printf("  stack0: \"%.60s\"\n", (const char *)psx_ram + (sv & 0x1FFFFF));
    }
    if (getenv("PSX_PCHIST")) {                       /* the addresses the CPU runs in, for the next 0.5 s */
        static uint32_t hist[0x80000 / 4];
        for (int i = 0; i < 280000; i++) { hist[(psx_cpu.pc & 0x1FFFFFu) / 4]++; psx_cpu_run(2); }
        for (uint32_t a = 0; a < 0x80000 / 4; a++) if (hist[a] > 40) printf("pc %08x x%u\n", 0x80000000u + a * 4, hist[a]);
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
    {
        FILE *r = fopen("build/psx_ram.bin", "wb");
        if (r) { fwrite(psx_ram, 1, sizeof psx_ram, r); fclose(r); }
    }
    psx_debug_t s;
    psx_debug(&s);
    printf("tty: %s\n", psx_tty());
    printf("trace:");
    for (int i = 0; i < 12; i++) printf(" %02X.%06X", s.cd_trace[i] & 255, s.cd_trace[i] >> 8);
    printf("\n");
    return 0;
}
