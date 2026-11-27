/*
 * Shell SDK: listening in the Sounds tab (see shell_sdk_play.h). Original code.
 */
#include "shell_sdk_play.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "bm/nes.h"
#include "drivers/timer.h"
#include "shell_sdk_build.h"
#include "shell_sdk_proj.h"

#define FRAME_US 16639              /* a frame of the NTSC machine */

/* The program: the driver, and orders from the SDK at $07F0 (what), $07F1 (the number or the note), $07F2 (the
 * channel) and $07F3 (the instrument + 1), taken by the vertical blank. */
static const char stub[] =
    ".org $8000\n"
    "reset:\n"
    "    sei\n"
    "    cld\n"
    "    ldx #$FF\n"
    "    txs\n"
    "    inx\n"
    "    stx $2000\n"
    "    stx $2001\n"
    "    stx $4010\n"
    "    bit $2002\n"
    "@a: bit $2002\n"
    "    bpl @a\n"
    "@b: bit $2002\n"
    "    bpl @b\n"
    "    lda #0\n"
    "    sta $07F0\n"
    "    jsr snd_init\n"
    "    lda #$80\n"
    "    sta $2000\n"
    "idle:\n"
    "    jmp idle\n"
    "nmi:\n"
    "    jsr order\n"
    "    jsr snd_frame\n"
    "irq:\n"
    "    rti\n"
    "order:\n"
    "    lda $07F0\n"
    "    beq @none\n"
    "    cmp #1\n"
    "    bne @sfx\n"
    "    lda $07F1\n"
    "    jsr play_song\n"
    "    jmp @done\n"
    "@sfx:\n"
    "    cmp #2\n"
    "    bne @note\n"
    "    lda $07F1\n"
    "    jsr play_sfx\n"
    "    jmp @done\n"
    "@note:\n"
    "    ldx $07F2\n"
    "    lda $07F1\n"
    "    sta __t_note\n"
    "    lda $07F3\n"
    "    sta __t_ins\n"
    "    jsr __s_cell\n"
    "@done:\n"
    "    lda #0\n"
    "    sta $07F0\n"
    "@none:\n"
    "    rts\n";

/* places of the driver's RAM (shell_sdk_snd.c) */
enum { M_ON = 0x700, X_ON = 0x730, M_AT_ORDER = 0x77E, M_AT_ROW = 0x77F };

static sdk_music_t scratch;             /* the music with the song that plays one pattern */
static int built;                       /* a program is in the core */
static uint32_t built_hash, built_ins;
static int preview_song;                /* its number in the program */
static int what;
static uint32_t next_frame;
static int frames, quiet;
static int at_order, at_row;
static int note_channel;
static int silent;

static uint32_t hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

int sdk_play_silent(void) { return silent; }
int sdk_play_what(void) { return what; }

void sdk_play_position(int *order, int *row)
{
    *order = at_order;
    *row = at_row;
}

void sdk_play_stop(void)
{
    if (what != SDK_PLAY_NOTHING)
        audio_set_ext(NULL);
    what = SDK_PLAY_NOTHING;
    built = 0;                          /* the next start begins from silence */
}

/* Builds the program with the music m and loads it in the core. */
static int build(const sdk_music_t *m, int pattern, int speed, char *err, size_t en)
{
    scratch = *m;
    preview_song = scratch.songs < SDK_MUS_SONGS ? scratch.songs : SDK_MUS_SONGS - 1;
    if (pattern >= 0) {                 /* one more song, round the pattern */
        sdk_mus_song_t *s = &scratch.song[preview_song];
        memset(s, 0, sizeof *s);
        strcpy(s->name, "preview_");
        s->speed = (uint8_t)speed;
        s->len = 1;
        s->order[0] = (uint8_t)pattern;
        if (scratch.songs <= preview_song)
            scratch.songs = preview_song + 1;
    }
    char *driver = sdk_music_asm(&scratch);
    static const char chr[] = ".chr\n    .res 8192,0\n.endchr\n";
    const size_t n = driver ? sizeof stub + strlen(driver) + sizeof chr : 0;
    char *src = driver ? malloc(n) : NULL;
    if (!src) {
        free(driver);
        snprintf(err, en, "not enough memory to play");
        return -1;
    }
    const int len = snprintf(src, n, "%s%s%s", stub, driver, chr);
    free(driver);
    sdk_proj_t p;
    sdk_proj_default(&p, SDK_NES);
    uint8_t *rom = NULL;
    size_t rom_len = 0;
    const int rc = sdk_build_plain(&p, "preview", src, (size_t)len, &rom, &rom_len, err, en);
    free(src);
    if (rc != 0)
        return -1;
    const char *why = NULL;
    if (nes_load(rom, rom_len, &why) != 0) {
        free(rom);
        snprintf(err, en, "cannot play: %s", why ? why : "?");
        return -1;
    }
    free(rom);
    nes_reset();
    for (int i = 0; i < 5; i++)
        nes_frame(0, 0, 0);
    return 0;
}

static void order(int cmd, int value, int channel, int ins)
{
    nes_poke_ram(0x7F1, (uint8_t)value);
    nes_poke_ram(0x7F2, (uint8_t)channel);
    nes_poke_ram(0x7F3, (uint8_t)ins);
    nes_poke_ram(0x7F0, (uint8_t)cmd);
}

int sdk_play_start(const sdk_music_t *m, int kind, int index, int note, int speed, char *err, size_t en)
{
    /* a note needs the instruments only: writing a pattern does not build again at each key */
    const uint32_t ins = hash(2166136261u ^ (uint32_t)m->instruments, m->ins, sizeof m->ins);
    const uint32_t all = hash(hash(ins, m, sizeof *m), &speed, sizeof speed) ^
                         (uint32_t)(kind == SDK_PLAY_PATTERN ? index + 1 : 0);
    const int stale = !built || (kind == SDK_PLAY_NOTE ? ins != built_ins : all != built_hash);
    if (kind != SDK_PLAY_NOTE || stale) {
        sdk_play_stop();
        if (build(m, kind == SDK_PLAY_PATTERN ? index : -1, speed, err, en) != 0)
            return -1;
        built = 1;
        built_hash = all;
        built_ins = ins;
    }
    if (kind == SDK_PLAY_PATTERN)
        order(1, preview_song, 0, 0);
    else if (kind == SDK_PLAY_SONG)
        order(1, index, 0, 0);
    else if (kind == SDK_PLAY_SFX)
        order(2, index, 0, 0);
    else {
        note_channel = m->ins[index].channel == SDK_MUS_PULSE ? 0 :
                       m->ins[index].channel == SDK_MUS_TRIANGLE ? 2 : 3;
        order(3, note, note_channel, index + 1);
    }
    if (!audio_ready())
        audio_init();
    silent = !audio_ready();
    audio_set_ext(nes_audio_mix);
    what = kind;
    frames = quiet = 0;
    at_order = at_row = 0;
    next_frame = timer_ticks();
    return 0;
}

int sdk_play_tick(void)
{
    if (what == SDK_PLAY_NOTHING)
        return 0;
    const uint32_t now = timer_ticks();
    if ((int32_t)(now - next_frame) < 0)
        return 0;
    if (now - next_frame > 100000)          /* far behind (a screen was drawn): do not run to catch up */
        next_frame = now - FRAME_US;
    int changed = 0;
    for (int i = 0; i < 3 && (int32_t)(now - next_frame) >= 0; i++, next_frame += FRAME_US) {
        nes_frame(0, 0, 0);
        frames++;
        const uint8_t *ram = nes_ram();
        if (ram[M_AT_ROW] != at_row || ram[M_AT_ORDER] != at_order) {
            at_row = ram[M_AT_ROW];
            at_order = ram[M_AT_ORDER];
            changed = 1;
        }
        int done = 0;
        if (what == SDK_PLAY_NOTE) {
            if (frames == 24)
                order(3, SDK_MUS_OFF, note_channel, 0);
            done = frames > 100;
        } else {
            const int on = what == SDK_PLAY_SFX ? ram[X_ON] | ram[X_ON + 1] | ram[X_ON + 2] | ram[X_ON + 3]
                                                : ram[M_ON];
            quiet = on ? 0 : quiet + 1;
            done = quiet > 30;
        }
        if (done) {
            audio_set_ext(NULL);
            what = SDK_PLAY_NOTHING;
            return 1;
        }
    }
    return changed;
}
