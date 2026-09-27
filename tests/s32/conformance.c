/*
 * Host-side conformance runner for the s32 core: replays every vector in
 * spec/s32/conformance and compares each tick with the reference (lua32).
 *
 *   conformance <dir-with-.cart-and-.vec> [name ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "s32/s32.h"
#include "lib/crc32.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static uint32_t frame_crc(const uint32_t *px)
{
    static uint8_t rgb[S32_SCREEN_W * S32_SCREEN_H * 3];
    for (int i = 0; i < S32_SCREEN_W * S32_SCREEN_H; i++) {
        rgb[i * 3] = (uint8_t)(px[i] >> 16);
        rgb[i * 3 + 1] = (uint8_t)(px[i] >> 8);
        rgb[i * 3 + 2] = (uint8_t)px[i];
    }
    return crc32(rgb, sizeof rgb);
}

static int run(const char *dir, const char *name, uint8_t *mem)
{
    char path[512], line[256], cart_name[128] = "";
    snprintf(path, sizeof path, "%s/%s.vec", dir, name);
    FILE *vf = fopen(path, "r");
    if (!vf) { printf("FAIL %s: cannot open %s\n", name, path); return 1; }

    s32_cart_t cart;
    s32_machine_t m;
    uint8_t *data = NULL;
    size_t len = 0;
    static uint32_t frame[S32_SCREEN_W * S32_SCREEN_H];
    int ticks = 0, fails = 0;

    while (fgets(line, sizeof line, vf)) {
        if (line[0] == '#') {
            char *c = strstr(line, "cart=");
            if (c) sscanf(c + 5, "%127s", cart_name);
            continue;
        }
        if (!data) {
            char err[64];
            snprintf(path, sizeof path, "%s/%s", dir, cart_name);
            data = read_file(path, &len);
            if (!data || s32_cart_parse(data, len, &cart, err, sizeof err)) {
                printf("FAIL %s: cart %s: %s\n", name, path, data ? err : "unreadable");
                fclose(vf);
                return 1;
            }
            s32_init(&m, mem);
            s32_install(&m, &cart);
        }

        int tick;
        unsigned input;
        char exp_wram[16], exp_hw[16], exp_frame[16];
        unsigned ea, ex, ey, ef, esp;
        int n = sscanf(line, "%d %x %15s %15s %15s %x %x %x %x %x",
                       &tick, &input, exp_wram, exp_hw, exp_frame, &ea, &ex, &ey, &ef, &esp);
        uint8_t in[8] = { (uint8_t)input };
        enum s32_status st = s32_tick(&m, in);
        ticks++;

        if (n == 3 && strcmp(exp_wram, "crash") == 0) {
            if (st == S32_OK) { printf("FAIL %s tick %d: expected crash, got ok\n", name, tick); fails++; }
            break;
        }
        if (st != S32_OK) {
            printf("FAIL %s tick %d: unexpected crash (%s at pc %06lx)\n", name, tick,
                   s32_status_str(st), (unsigned long)m.crash_pc);
            fails++;
            break;
        }
        s32_render(&m, frame, S32_SCREEN_W);
        /* the player renders in bands of 8 rows: must be the same picture */
        static uint32_t band[S32_SCREEN_W * 8];
        s32_render_begin(&m);
        for (int y0 = 0; y0 < S32_SCREEN_H; y0 += 8) {
            int y1 = y0 + 8 < S32_SCREEN_H ? y0 + 8 : S32_SCREEN_H;
            s32_render_rows(&m, band, S32_SCREEN_W, y0, y1);
            if (memcmp(band, frame + y0 * S32_SCREEN_W, (size_t)(y1 - y0) * S32_SCREEN_W * 4) != 0) {
                printf("FAIL %-16s tick %d: band at row %d differs from the full render\n",
                       name, tick, y0);
                fails++;
                break;
            }
        }
        char got[160];
        snprintf(got, sizeof got, "%08lx %08lx %08lx %04x %04x %04x %02x %06lx",
                 (unsigned long)crc32(m.mem, S32_WRAM_END),
                 (unsigned long)crc32(m.mem + S32_VRAM_BASE, S32_APU_END - S32_VRAM_BASE),
                 (unsigned long)frame_crc(frame), m.a, m.x, m.y, m.flags, (unsigned long)m.sp);
        char want[160];
        snprintf(want, sizeof want, "%s %s %s %04x %04x %04x %02x %06x",
                 exp_wram, exp_hw, exp_frame, ea, ex, ey, ef, esp);
        if (strcmp(got, want) != 0) {
            printf("FAIL %s tick %d (input %02x)\n  want %s\n  got  %s\n", name, tick, input, want, got);
            if (++fails >= 3) break;
        }
    }
    fclose(vf);
    free(data);
    printf("%s %-16s %d ticks\n", fails ? "FAIL" : "PASS", name, ticks);
    return fails != 0;
}

int main(int argc, char **argv)
{
    static const char *all[] = { "demo", "cpu_ops", "ppu", "crash_opcode", "crash_underflow",
                                 "crash_overflow", "crash_limit", "stack_smash" };
    const char *dir = argc > 1 ? argv[1] : "spec/s32/conformance";
    uint8_t *mem = malloc(S32_MEM_SIZE);
    int failed = 0, n = 0;

    if (argc > 2) {
        for (int i = 2; i < argc; i++, n++)
            failed += run(dir, argv[i], mem);
    } else {
        for (size_t i = 0; i < sizeof all / sizeof *all; i++, n++)
            failed += run(dir, all[i], mem);
    }
    printf("\n%d/%d vectors passed\n", n - failed, n);
    return failed != 0;
}
