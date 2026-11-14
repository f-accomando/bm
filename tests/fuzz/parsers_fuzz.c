/*
 * parsers_fuzz.c: a mutation fuzz of the parsers that read files from the SD card: SNES and NES ROMs (load and two frames), PNG, JPEG and .bm
 * cartridges. Built with AddressSanitizer and UndefinedBehaviorSanitizer (make test-fuzz): a read or a write out of bounds, or undefined
 * behaviour, stops the run with a report. The seeds are made here (a valid header and random content); a .bm file named on the command line
 * is one more seed. Deterministic: the same count gives the same inputs.
 *
 *   parsers_fuzz [iterations] [seed.bm ...]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm.h"
#include "jpeg.h"
#include "nes.h"
#include "png.h"
#include "snes.h"
#include "lib/crc32.h"

static uint32_t fake_us;
static uint32_t now_us(void) { return fake_us += 10; }

static uint32_t rs = 0x12345678u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* a few random changes: flipped bits, random bytes, a cut at the end */
static size_t mutate(uint8_t *d, size_t len, size_t cap, size_t hot)
{
    const int n = 1 + (int)(rnd() % 8);
    for (int i = 0; i < n && len; i++) {
        const size_t at = (rnd() & 1) && hot ? rnd() % (hot < len ? hot : len) : rnd() % len;
        switch (rnd() % 4) {
        case 0: d[at] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: d[at] = (uint8_t)rnd(); break;
        case 2: d[at] = (uint8_t)((rnd() & 1) ? 0xFF : 0); break;
        default: if (rnd() % 8 == 0) len = 1 + rnd() % len; break;
        }
    }
    (void)cap;
    return len;
}

/* ---------------------------------------------------------------- seeds */
static size_t snes_seed(uint8_t *d)
{
    const size_t len = 65536;
    for (size_t i = 0; i < len; i++) d[i] = (uint8_t)rnd();
    uint8_t *h = d + 0x7FC0;                            /* LoROM header */
    memcpy(h, "FUZZ TEST ROM        ", 21);
    h[0x15] = 0x20; h[0x16] = 0; h[0x17] = 9; h[0x18] = 0;
    h[0x1C] = 0xFF; h[0x1D] = 0xFF; h[0x1E] = 0; h[0x1F] = 0;
    d[0x7FFC] = 0x00; d[0x7FFD] = 0x80;                 /* reset vector */
    return len;
}

static size_t nes_seed(uint8_t *d)
{
    const size_t len = 16 + 32768 + 8192;
    for (size_t i = 0; i < len; i++) d[i] = (uint8_t)rnd();
    memcpy(d, "NES\x1A", 4);
    d[4] = 2; d[5] = 1; d[6] = (uint8_t)(rnd() & 0xF1); d[7] = 0;
    memset(d + 8, 0, 8);
    d[16 + 32768 - 4] = 0x00; d[16 + 32768 - 3] = 0x80; /* reset vector */
    return len;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static size_t chunk(uint8_t *d, const char *type, const uint8_t *data, uint32_t n)
{
    be32(d, n);
    memcpy(d + 4, type, 4);
    if (n) memcpy(d + 8, data, n);
    be32(d + 8 + n, crc32(d + 4, n + 4));
    return 12 + n;
}

/* a small RGBA PNG with stored deflate blocks */
static size_t png_seed(uint8_t *d)
{
    const int w = 8 + (int)(rnd() % 8), hgt = 8 + (int)(rnd() % 8);
    static uint8_t raw[4096], z[8192];
    size_t rn = 0;
    for (int y = 0; y < hgt; y++) {
        raw[rn++] = (uint8_t)(rnd() % 5);              /* the filter of the row */
        for (int x = 0; x < w * 4; x++) raw[rn++] = (uint8_t)rnd();
    }
    size_t zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    z[zn++] = 1; z[zn++] = (uint8_t)rn; z[zn++] = (uint8_t)(rn >> 8); z[zn++] = (uint8_t)~rn; z[zn++] = (uint8_t)(~rn >> 8);
    memcpy(z + zn, raw, rn); zn += rn;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rn; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + zn, b << 16 | a); zn += 4;
    size_t n = 0;
    memcpy(d, "\x89PNG\r\n\x1a\n", 8); n = 8;
    uint8_t ih[13];
    be32(ih, (uint32_t)w); be32(ih + 4, (uint32_t)hgt);
    ih[8] = 8; ih[9] = 6; ih[10] = 0; ih[11] = 0; ih[12] = 0;
    n += chunk(d + n, "IHDR", ih, 13);
    n += chunk(d + n, "IDAT", z, (uint32_t)zn);
    n += chunk(d + n, "IEND", NULL, 0);
    return n;
}

/* the markers of a baseline JPEG with random tables and data: the parser's paths, not a picture */
static size_t jpeg_seed(uint8_t *d)
{
    static const uint8_t head[] = { 0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x43, 0x00 };
    size_t n = 0;
    memcpy(d, head, sizeof head); n = sizeof head;
    for (int i = 0; i < 64; i++) d[n++] = (uint8_t)(1 + rnd() % 50);
    static const uint8_t sof[] = { 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00 };
    memcpy(d + n, sof, sizeof sof); n += sizeof sof;
    static const uint8_t dht[] = { 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    memcpy(d + n, dht, sizeof dht); n += sizeof dht;
    static const uint8_t sos[] = { 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x3F, 0x00 };
    memcpy(d + n, sos, sizeof sos); n += sizeof sos;
    for (int i = 0; i < 512; i++) { uint8_t v = (uint8_t)rnd(); d[n++] = v; if (v == 0xFF) d[n++] = 0; }
    d[n++] = 0xFF; d[n++] = 0xD9;
    return n;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = n > 0 ? malloc((size_t)n) : NULL;
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f);
    *len = p ? (size_t)n : 0;
    return p;
}

int main(int argc, char **argv)
{
    const int iters = argc > 1 ? atoi(argv[1]) : 200;
    static uint8_t buf[1 << 20];
    snes_set_clock(now_us);
    unsigned ok[5] = { 0 };
    for (int i = 0; i < iters; i++) {
        const char *why = NULL;
        /* SNES: load and two frames (the CPU runs the random program) */
        size_t n = snes_seed(buf);
        n = mutate(buf, n, sizeof buf, 0x200);
        for (size_t k = 0; k < 8; k++) buf[0x7FC0 + rnd() % 64] ^= (uint8_t)(i & 1 ? 0 : rnd());
        if (snes_load(buf, n, &why) == 0) { ok[0]++; snes_frame(0, 0, 1); snes_frame((uint16_t)rnd(), 0, 1); }
        /* NES */
        n = mutate(buf, nes_seed(buf), sizeof buf, 16);
        if (nes_load(buf, n, &why) == 0) { ok[1]++; nes_frame(0, 0, 1); nes_frame((uint8_t)rnd(), 0, 1); }
        /* PNG */
        n = mutate(buf, png_seed(buf), sizeof buf, 64);
        uint8_t *rgba = NULL;
        int w = 0, h = 0;
        if (png_rgba(buf, n, &rgba, &w, &h) == 0) ok[2]++;
        free(rgba);
        /* JPEG */
        n = mutate(buf, jpeg_seed(buf), sizeof buf, 120);
        uint8_t *rgb = NULL;
        char err[64];
        if (jpeg_decode(buf, n, &rgb, &w, &h, err, sizeof err) == 0) ok[3]++;
        free(rgb);
    }
    /* .bm cartridges: the seeds named on the command line */
    for (int a = 2; a < argc; a++) {
        size_t len;
        uint8_t *seed = read_file(argv[a], &len);
        if (!seed || len > sizeof buf) { free(seed); continue; }
        for (int i = 0; i < iters; i++) {
            memcpy(buf, seed, len);
            const size_t n = mutate(buf, len, sizeof buf, 256);
            bm_cart_t c;
            char err[128];
            memset(&c, 0, sizeof c);
            if (bm_parse(buf, n, &c, err, sizeof err) == 0) ok[4]++;
        }
        free(seed);
    }
    printf("parsers fuzz: %d iterations, accepted snes %u nes %u png %u jpeg %u bm %u: ok\n", iters, ok[0], ok[1], ok[2], ok[3], ok[4]);
    return 0;
}
