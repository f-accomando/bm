/*
 * Shell SDK: the sound of the SNES projects. It builds the image of the sound memory (the driver for the SPC700,
 * shell_sdk_spc.asm, then the tables of the music block and the samples) and the 65C816 routines that send it and
 * give the orders. Original code.
 */
#include "shell_sdk_music.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_lists.h"
#include "shell_sdk_spc_bin.h"

#define DATA 0x0A00                     /* the head of the data in the sound memory (the driver reads it) */
#define HEAD 16
#define WAVE_BYTES 18                   /* a sample: one wave of 32 values, 2 blocks of 9 bytes */
#define BRR_SHIFT 11                    /* a value of the sample: n * 1024, n -8..7 */

/* ---- the image ---- */

typedef struct { uint8_t *b; int len, max, full; } image_t;     /* len counts from SDK_SPC_START */

static int here(const image_t *i) { return SDK_SPC_START + i->len; }

static void put(image_t *i, int v)
{
    if (i->len >= i->max)
        i->full = 1;
    else
        i->b[i->len++] = (uint8_t)v;
}

static void put16(image_t *i, int v)
{
    put(i, v & 0xFF);
    put(i, v >> 8 & 0xFF);
}

static void set16(image_t *i, int at, int v)           /* at: an address of the sound memory */
{
    const int k = at - SDK_SPC_START;
    if (k + 1 < i->max) {
        i->b[k] = (uint8_t)v;
        i->b[k + 1] = (uint8_t)(v >> 8);
    }
}

/* The value i (0..31) of the wave w, -8..7. */
static int wave_value(int w, int i)
{
    static const int8_t sine[32] = { 0, 1, 3, 4, 5, 6, 6, 7, 7, 7, 6, 6, 5, 4, 3, 1,
                                     0, -1, -3, -4, -5, -6, -6, -7, -7, -7, -6, -6, -5, -4, -3, -1 };
    switch (w) {
    case 0: return i < 4 ? 7 : -7;                      /* squares of 12.5, 25 and 50% */
    case 1: return i < 8 ? 7 : -7;
    case 2: return i < 16 ? 7 : -7;
    case 3: return i < 8 ? i : i < 24 ? 15 - i : i - 31;    /* triangle */
    case 4: return i / 2 - 8;                           /* saw */
    default: return sine[i];
    }
}

int sdk_spc_image(const sdk_music_t *m, uint8_t *out, int max)
{
    /* the pitches of C7..B7 for a wave of 32 values (it sounds at 1000 Hz with $1000) */
    static const uint16_t top[12] = { 8573, 9083, 9623, 10195, 10801, 11443, 12124, 12845, 13609, 14418, 15275,
                                      16183 };
    image_t im = { out, 0, max, 0 };
    for (int i = 0; i < (int)sizeof sdk_spc_driver; i++)
        put(&im, sdk_spc_driver[i]);
    while (here(&im) < DATA)
        put(&im, 0);
    int patterns = 0;
    for (int i = 0; i < SDK_MUS_PATTERNS; i++)
        if (m->pat[i].used)
            patterns = i + 1;
    for (int i = 0; i < m->songs; i++)
        for (int k = 0; k < m->song[i].len; k++)
            if (m->song[i].order[k] >= patterns)
                patterns = m->song[i].order[k] + 1;
    for (int i = 0; i < HEAD; i++)
        put(&im, 0);
    const int instruments = here(&im);
    for (int i = 0; i < m->instruments; i++) {
        const sdk_mus_ins_t *s = &m->ins[i];
        const int noise = s->wave == SDK_MUS_WAVES;
        put(&im, noise ? 0 : s->wave);
        put(&im, noise ? s->noise : 0xFF);
        put(&im, 0x80 | (s->decay & 7) << 4 | (s->attack & 15));
        put(&im, (s->sustain & 7) << 5 | (s->rate & 31));
        put(&im, s->level * (s->pan <= 8 ? 8 : 16 - s->pan) / 8);       /* left, right */
        put(&im, s->level * (s->pan >= 8 ? 8 : s->pan) / 8);
        put(&im, s->vibrato);
        put(&im, s->arp);
    }
    const int pat_table = here(&im);
    for (int i = 0; i < patterns; i++)
        put16(&im, 0);
    const int song_table = here(&im);
    for (int i = 0; i < m->songs; i++)
        put16(&im, 0);
    const int sfx_table = here(&im);
    for (int i = 0; i < m->sfxs; i++)
        put16(&im, 0);
    const int pitches = here(&im);
    for (int note = SDK_MUS_LOW; note <= SDK_MUS_HIGH; note++) {
        const int down = 7 - (note / 12 - 1), v = top[note % 12];
        put16(&im, down > 0 ? (v + (1 << (down - 1))) >> down : v);
    }
    for (int i = 0; i < patterns; i++) {                /* steps, then 16 bytes a step */
        const sdk_mus_pattern_t *p = &m->pat[i];
        if (!p->used)
            continue;
        set16(&im, pat_table + 2 * i, here(&im));
        put(&im, p->steps);
        for (int s = 0; s < p->steps; s++)
            for (int v = 0; v < SDK_MUS_VOICES; v++) {
                put(&im, p->cell[s][v].note);
                put(&im, p->cell[s][v].ins);
            }
    }
    for (int i = 0; i < m->songs; i++) {                /* speed, loop, length, patterns */
        const sdk_mus_song_t *s = &m->song[i];
        set16(&im, song_table + 2 * i, here(&im));
        put(&im, s->speed);
        put(&im, s->loop);
        put(&im, s->len);
        for (int k = 0; k < s->len; k++)
            put(&im, s->order[k]);
    }
    for (int i = 0; i < m->sfxs; i++) {                 /* speed, voice, wave, steps, then 3 bytes a step */
        const sdk_mus_sfx_t *s = &m->sfx[i];
        set16(&im, sfx_table + 2 * i, here(&im));
        put(&im, s->speed);
        put(&im, s->channel);
        put(&im, s->wave == SDK_MUS_WAVES ? 0x80 : s->wave);
        put(&im, s->steps);
        for (int k = 0; k < s->steps; k++) {
            put(&im, s->step[k].note);
            put(&im, s->step[k].volume);
            put(&im, s->step[k].fx);
        }
    }
    while (here(&im) & 0xFF)                            /* the list of the samples starts a page */
        put(&im, 0);
    const int directory = here(&im), first = directory + 4 * SDK_MUS_WAVES;
    for (int w = 0; w < SDK_MUS_WAVES; w++) {
        put16(&im, first + WAVE_BYTES * w);             /* the start and the place of the loop */
        put16(&im, first + WAVE_BYTES * w);
    }
    for (int w = 0; w < SDK_MUS_WAVES; w++)
        for (int block = 0; block < 2; block++) {
            put(&im, BRR_SHIFT << 4 | 2 | block);       /* no filter; loop; the second block ends */
            for (int i = 0; i < 8; i++) {
                const int a = wave_value(w, block * 16 + 2 * i), b = wave_value(w, block * 16 + 2 * i + 1);
                put(&im, (a & 15) << 4 | (b & 15));
            }
        }
    if (im.full)
        return -1;
    const int k = DATA - SDK_SPC_START;
    out[k] = (uint8_t)m->songs;
    out[k + 1] = (uint8_t)m->sfxs;
    set16(&im, DATA + 2, instruments);
    set16(&im, DATA + 4, pat_table);
    set16(&im, DATA + 6, song_table);
    set16(&im, DATA + 8, sfx_table);
    set16(&im, DATA + 10, pitches);
    out[k + 12] = (uint8_t)(directory >> 8);
    out[k + 13] = (uint8_t)patterns;
    out[k + 14] = (uint8_t)m->instruments;
    return im.len;
}

/* ---- the program's side ---- */

/* %02X: the byte of the direct page that keeps the last order. */
static const char routines[] =
    "__snd_last = $%02X          ; the last order given (0: the sound is not started)\n"
    "    .a8\n"
    "    .i16\n"
    "; snd_init: sends the driver and the music to the sound chip and starts it\n"
    "snd_init:\n"
    "    php\n"
    "    rep #$30\n"
    "    .a16\n"
    "    pha\n"
    "    phx\n"
    "    phy\n"
    "@boot:\n"
    "    lda #$BBAA             ; the chip's boot ROM listens\n"
    "    cmp $2140\n"
    "    bne @boot\n"
    "    ldx #$0200             ; where the image goes\n"
    "    stx $2142\n"
    "    sep #$20\n"
    "    .a8\n"
    "    lda #$01\n"
    "    sta $2141\n"
    "    lda #$CC\n"
    "    sta $2140\n"
    "@go:\n"
    "    cmp $2140\n"
    "    bne @go\n"
    "    ldx #0\n"
    "@byte:\n"
    "    lda __snd_image,x\n"
    "    sta $2141\n"
    "    txa                    ; the count: the byte of the place\n"
    "    sta $2140\n"
    "@echo:\n"
    "    cmp $2140\n"
    "    bne @echo\n"
    "    inx\n"
    "    cpx #__snd_image_end-__snd_image\n"
    "    bne @byte\n"
    "    ldy #$0200             ; start the driver\n"
    "    sty $2142\n"
    "    stz $2141\n"
    "    txa\n"
    "    inc a\n"
    "    bne @nonzero\n"
    "    inc a\n"
    "    inc a\n"
    "@nonzero:\n"
    "    cmp #$EE\n"
    "    bne @jump\n"
    "    inc a\n"
    "@jump:\n"
    "    sta $2140\n"
    "@taken:\n"
    "    cmp $2140\n"
    "    bne @taken\n"
    "    lda #$EE               ; the driver listens\n"
    "@driver:\n"
    "    cmp $2140\n"
    "    bne @driver\n"
    "    sta __snd_last\n"
    "    rep #$30\n"
    "    .a16\n"
    "    ply\n"
    "    plx\n"
    "    pla\n"
    "    plp\n"
    "    .a8\n"
    "    rts\n"
    "; play_song, play_sfx: A the number. stop_music. They keep the registers.\n"
    "play_song:\n"
    "    php\n"
    "    sep #$20\n"
    "    .a8\n"
    "    pha\n"
    "    jsr __snd_wait\n"
    "    beq @off\n"
    "    lda 1,s\n"
    "    sta $2141\n"
    "    lda #1\n"
    "    jsr __snd_order\n"
    "@off:\n"
    "    pla\n"
    "    plp\n"
    "    rts\n"
    "play_sfx:\n"
    "    php\n"
    "    sep #$20\n"
    "    .a8\n"
    "    pha\n"
    "    jsr __snd_wait\n"
    "    beq @off\n"
    "    lda 1,s\n"
    "    sta $2141\n"
    "    lda #2\n"
    "    jsr __snd_order\n"
    "@off:\n"
    "    pla\n"
    "    plp\n"
    "    rts\n"
    "stop_music:\n"
    "    php\n"
    "    sep #$20\n"
    "    .a8\n"
    "    pha\n"
    "    jsr __snd_wait\n"
    "    beq @off\n"
    "    lda #3\n"
    "    jsr __snd_order\n"
    "@off:\n"
    "    pla\n"
    "    plp\n"
    "    rts\n"
    "; waits until the last order is taken; Z set when the sound is not started\n"
    "__snd_wait:\n"
    "    lda __snd_last\n"
    "    beq @none\n"
    "@taken:\n"
    "    cmp $2140\n"
    "    bne @taken\n"
    "    ora #0\n"
    "@none:\n"
    "    rts\n"
    "; A the order: it goes with the 4 high bits changed, so the driver sees it\n"
    "__snd_order:\n"
    "    pha\n"
    "    lda __snd_last\n"
    "    clc\n"
    "    adc #$10\n"
    "    and #$F0\n"
    "    ora 1,s\n"
    "    sta __snd_last\n"
    "    sta $2140\n"
    "    pla\n"
    "    rts\n"
    "; the image of the sound memory from $0200\n"
    "__snd_image:\n";

char *sdk_music_snes_asm(const sdk_music_t *m, int last)
{
    enum { MOST = 0x6000 };                     /* of the 32 KB of the first bank */
    uint8_t *image = malloc(MOST);
    const int n = image ? sdk_spc_image(m, image, MOST) : -1;
    if (n < 0) {
        free(image);
        return NULL;
    }
    sdk_text_t t = { NULL, 0, 0, 0, 1 };
    sdk_list_add(&t, "; ==== the sound: the SNES orders and the image of the Sounds data ==============\n");
    for (int i = 0; i < m->songs; i++)
        sdk_list_add(&t, "song_%s = %d\n", m->song[i].name, i);
    for (int i = 0; i < m->sfxs; i++)
        sdk_list_add(&t, "sfx_%s = %d\n", m->sfx[i].name, i);
    {   /* the routines, in pieces: sdk_list_add takes 255 bytes at a time */
        char head[96];
        const char *rest = strchr(routines, '\n') + 1;
        snprintf(head, sizeof head, "__snd_last = $%02X          ; the last order (0: not started)\n", last & 0xFF);
        sdk_list_add_str(&t, head);
        sdk_list_add_str(&t, rest);
    }
    for (int i = 0; i < n; i += 16) {
        char line[96];
        size_t at = 0;
        for (int k = i; k < n && k < i + 16; k++)
            at = sdk_list_put(line, at, sizeof line, "%s%d", k > i ? "," : "", image[k]);
        sdk_list_add(&t, "    .db %s\n", line);
    }
    sdk_list_add(&t, "__snd_image_end:\n");
    free(image);
    if (t.full) {
        free(t.s);
        return NULL;
    }
    return t.s;
}
