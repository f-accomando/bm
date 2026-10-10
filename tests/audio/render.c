/* Renders a song or a sound effect of a sound bank to a WAV file on the
 * PC, with the console's own synthesizer and player (and the bank's
 * samples, listed as they are read):
 *   bmrender [--bits 16|24|32|f32] BANK.bmau OUT.wav [song|sfx N] [seconds]
 * `make wav BANK=carts/sound/demo.json SONG=0 [BITS=24]` does the conversion
 * first. 16 bits (the default) is synth_render's, rounded with the dither;
 * 24 and 32 are synth_render32's (the console's sound_depth); f32 is the
 * mix itself (synth_mix), as IEEE floats. */
#include "audio/synth.h"
#include "audio/player.h"
#include "wavout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000

int main(int argc, char **argv)
{
    int bits = 16;
    char *arg[8];
    int na = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bits") && i + 1 < argc) {
            if (!(bits = wav_bits(argv[++i]))) {
                fprintf(stderr, "%s: --bits is 16, 24, 32 or f32\n", argv[0]);
                return 2;
            }
        } else if (na < 8) {
            arg[na++] = argv[i];
        }
    }
    if (na < 2) {
        fprintf(stderr, "usage: %s [--bits 16|24|32|f32] BANK.bmau OUT.wav [song|sfx N] [seconds]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(arg[0], "rb");
    if (!f) { perror(arg[0]); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size > 0 ? (size_t)size : 1);
    size_t len = fread(data, 1, size > 0 ? (size_t)size : 0, f);
    fclose(f);
    static au_bank_t bank;
    char err[64];
    if (au_parse(data, len, &bank, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", arg[0], err);
        return 1;
    }
    /* the samples: converted into a buffer of the size they need */
    int16_t *pool = malloc(bank.pcm_need * sizeof(int16_t) + 2);
    au_parse_pcm(data, len, &bank, pool, bank.pcm_need, err, sizeof err);
    for (int i = 0; i < bank.nsamples; i++)
        printf("sample %d %s: %u frames, %u Hz, %u channels, root %u, loop %u [%u, %u)\n", i, bank.sample_name[i],
               (unsigned)bank.sample[i].len, (unsigned)bank.sample[i].rate, bank.sample[i].channels,
               bank.sample[i].root, bank.sample[i].loop, (unsigned)bank.sample[i].loop_start,
               (unsigned)bank.sample[i].loop_end);
    int is_sfx = na > 2 && !strcmp(arg[2], "sfx");
    int n = na > 3 ? atoi(arg[3]) : 0;
    double secs = na > 4 ? atof(arg[4]) : is_sfx ? 2 : 20;
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
    FILE *o = fopen(arg[1], "wb");
    if (!o) { perror(arg[1]); return 1; }
    wav_header(o, RATE, bits, total);
    /* the level: the peak and how much is above the limiter's knee (27800 of 32767) */
    double peak = 0;
    uint32_t clipped = 0;
    if (bits == 16) {
        int16_t *pcm = malloc(total * 4 + 256);     /* stereo */
        for (uint32_t k = 0; k < total; k += 64) {
            player_advance(&pl, 64);
            synth_render(&syn, regs, pcm + 2 * k, 64);
        }
        for (uint32_t i = 0; i < total * 2; i++) {
            int a = abs(pcm[i]);
            if (a > peak) peak = a;
            clipped += a > 27800;
        }
        peak /= 32767;
        fwrite(pcm, 4, total, o);
        free(pcm);
    } else {
        int32_t w[2 * 64];
        for (uint32_t k = 0; k < total; k += 64) {
            const unsigned m = total - k < 64 ? total - k : 64;     /* the file holds `total` frames */
            player_advance(&pl, 64);
            const float *x = syn.out;
            if (bits == WAV_F32) {
                synth_mix(&syn, regs, syn.out, 64);
                wav_put_floats(o, syn.out, m);
            } else {
                synth_render32(&syn, regs, w, 64, (unsigned)bits);
                wav_put_words(o, w, m, bits);
            }
            for (unsigned i = 0; i < 2 * m; i++) {
                double a = bits == WAV_F32 ? fabs(x[i]) : fabs(w[i] / 2147483648.0);
                if (a > peak) peak = a;
                clipped += a > 27800.0 / 32767;
            }
        }
    }
    fclose(o);
    printf("%s: %.1f s, peak %d%%, %.2f%% of samples in the limiter%s\n", arg[1], secs, (int)(peak * 100 + 1e-9),
           clipped * 100.0 / (total * 2),
           bits == WAV_F32 ? ", 32-bit float" : bits == 32 ? ", 32 bits" : bits == 24 ? ", 24 bits" : "");
    free(pool);
    free(data);
    return 0;
}
