/* Renders a song or a sound effect of a sound bank to a WAV file on the
 * PC, with the console's own synthesizer and player:
 *   bmrender BANK.bmau OUT.wav [song|sfx N] [seconds]
 * `make wav BANK=carts/sound/demo.json SONG=0` does the conversion first. */
#include "audio/synth.h"
#include "audio/player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s BANK.bmau OUT.wav [song|sfx N] [seconds]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static uint8_t data[1 << 20];
    size_t len = fread(data, 1, sizeof data, f);
    fclose(f);
    static au_bank_t bank;
    char err[64];
    if (au_parse(data, len, &bank, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 1;
    }
    int is_sfx = argc > 3 && !strcmp(argv[3], "sfx");
    int n = argc > 4 ? atoi(argv[4]) : 0;
    double secs = argc > 5 ? atof(argv[5]) : is_sfx ? 2 : 20;
    static uint8_t regs[SYNTH_REG_BYTES];
    static synth_t syn;
    static player_t pl;
    synth_init(&syn, RATE);
    player_init(&pl, RATE, regs, &syn);
    player_set_bank(&pl, &bank);
    if (is_sfx)
        player_sfx(&pl, n, 0, 0, 1.0f);
    else
        player_music(&pl, n, 0, 0);
    uint32_t total = (uint32_t)(secs * RATE);
    int16_t *pcm = malloc(total * 4 + 256);     /* stereo */
    int peak = 0, clipped = 0;
    for (uint32_t k = 0; k < total; k += 64) {
        player_advance(&pl, 64);
        synth_render(&syn, regs, pcm + 2 * k, 64);
    }
    for (uint32_t i = 0; i < total * 2; i++) {
        int a = abs(pcm[i]);
        if (a > peak) peak = a;
        clipped += a > 27800;            /* above the limiter's knee */
    }
    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    fwrite("RIFF", 1, 4, o); put32(o, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, o);
    put32(o, 16); put16(o, 1); put16(o, 2); put32(o, RATE); put32(o, RATE * 4); put16(o, 4); put16(o, 16);
    fwrite("data", 1, 4, o); put32(o, total * 4);
    fwrite(pcm, 4, total, o);
    fclose(o);
    printf("%s: %.1f s, peak %d%%, %.2f%% of samples in the limiter\n", argv[2], secs, peak * 100 / 32767,
           clipped * 100.0 / (total * 2));
    free(pcm);
    return 0;
}
