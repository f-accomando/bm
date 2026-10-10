/*
 * The games' loading screen (src/bm/loading.c, 2026-10-04) on the PC: the
 * retro intro on a console page (32 bits a pixel), then on the game's page
 * (RGB565): the logo falls and lands at its place, the jingle's notes come
 * at the landing and on time, no title (the system's splash), a circle of
 * dots turning under the logo, it lasts at least the intro, and nothing is
 * drawn without a begin or with game_intro=0. The frames as PPM in the
 * folder given (to look at).
 */
#include "bm/loading.h"
#include "bm/runtime.h"
#include "audio/synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* ---- what loading.c uses */
void uart_putc(char c) { (void)c; }           /* printf.c's console */
static uint32_t now_us;
uint32_t timer_ticks(void) { return now_us; }
static const char *intro = NULL;
const char *config_get(const char *key) { return strcmp(key, "game_intro") == 0 ? intro : NULL; }
static int suspended;
void console_suspend(int s) { suspended = s; }
int console_suspended(void) { return suspended; }
static int notes, landing_note_at = -1, wave_seen[SYNTH_WAVES], slides, retro;
static uint8_t vregs[SYNTH_VOICES][SYNTH_VOICE_BYTES];
void audio_note(unsigned ch, float freq, uint32_t ms, int wave, int vol)
{
    (void)freq; (void)ms; (void)vol;
    if (landing_note_at < 0)
        landing_note_at = (int)(now_us / 1000);
    notes++;
    if (wave < 0)
        wave = vregs[ch % SYNTH_VOICES][SYNTH_WAVEFORM];   /* the voice's sound */
    if (wave >= 0 && wave < SYNTH_WAVES)
        wave_seen[wave]++;
}
void audio_envelope(unsigned ch, int a, int d, int s, int r) { (void)ch; (void)a; (void)d; (void)s; (void)r; }
void audio_duty(unsigned ch, int duty) { (void)ch; (void)duty; }
void audio_slide(unsigned ch, float hz, uint32_t ms) { (void)ch; (void)hz; (void)ms; slides++; }
int audio_retro_on(void) { return retro; }
void audio_tone(unsigned ch, const uint8_t *r) { memcpy(vregs[ch % SYNTH_VOICES], r, SYNTH_VOICE_BYTES); }
void audio_tone_get(unsigned ch, uint8_t *r) { memcpy(r, vregs[ch % SYNTH_VOICES], SYNTH_VOICE_BYTES); }
void au_voice_default(volatile uint8_t *r)
{
    for (int i = 0; i < SYNTH_VOICE_BYTES; i++)
        r[i] = 0;
    r[SYNTH_VOLUME] = 128;
}
void audio_idle(void) { now_us += 1000; }       /* loading_end's wait: time goes on */
static int presents;
uint32_t bm_video_present(framebuffer_t *fb, g16_t *g) { (void)fb; (void)g; presents++; return 0; }

/* ---- the screens */
#define CW 640
#define CH 360
static uint32_t con[CW * CH];
static uint16_t page_px[480 * 270];

static void save_con(const char *dir, const char *name)
{
    if (!dir)
        return;
    char path[256];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", CW, CH);
    for (int i = 0; i < CW * CH; i++) {
        uint8_t rgb[3] = { (uint8_t)(con[i] >> 16), (uint8_t)(con[i] >> 8), (uint8_t)con[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* the console's pixels of the logo's colours (blue to purple) in a band of rows */
static int logo_pixels(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < CW; x++) {
            const uint32_t c = con[y * CW + x];
            const int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
            n += b > 180 && b > r + 40 && b > g;
        }
    return n;
}

static int light_pixels(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < CW; x++) {
            const uint32_t c = con[y * CW + x];
            n += (c >> 16 & 255) > 200 && (c >> 8 & 255) > 200 && (c & 255) > 200;
        }
    return n;
}

/* the same outside a box (x0..x1, y0..y1) */
static int light_outside(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1)
                continue;
            const uint32_t c = con[y * CW + x];
            n += (c >> 16 & 255) > 200 && (c >> 8 & 255) > 200 && (c & 255) > 200;
        }
    return n;
}

static void run_until(uint32_t ms)
{
    while (now_us / 1000 < ms) {
        now_us += 5000;
        loading_tick();
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    framebuffer_t fb = { .width = CW, .height = CH, .pitch = CW * 4, .base = (uint8_t *)con, .depth = 32 };

    /* nothing without a begin */
    loading_tick();
    loading_end();
    check(!loading_active() && con[0] == 0 && notes == 0, "no begin: nothing drawn, no sound");

    /* game_intro=0: off */
    intro = "0";
    loading_begin(&fb);
    check(!loading_active() && con[0] == 0, "game_intro=0: no loading screen");
    intro = NULL;

    now_us = 0;                                 /* the times below: from the begin */
    loading_begin(&fb);
    check(loading_active() && suspended == 1, "begun: the console suspended (the log unseen)");
    check(con[0] != 0 && (con[0] & 0xFFFFFF) == 0x000810, "the splash's background over the whole screen");

    /* the logo falls: at 200 ms high, after the landing at its place */
    run_until(250);
    save_con(dir, "fall");
    check(logo_pixels(60, 200) < 2000, "falling: not at its place yet");
    run_until(700);
    const int at_rest = logo_pixels(60, 200);
    save_con(dir, "landed");
    check(at_rest > 2000, "landed: the logo in the middle of the screen");
    check(landing_note_at >= 500 && landing_note_at <= 560, "the jingle starts when it lands (500 ms)");
    run_until(800);                             /* its last notes at 770 ms */
    check(notes == 6 && wave_seen[SYNTH_SINE] == 1 && slides == 1 && wave_seen[SYNTH_PLUCK] == 3 &&
          wave_seen[SYNTH_FM] == 1 && wave_seen[SYNTH_TRIANGLE] == 1 && wave_seen[SYNTH_SQUARE] == 0,
          "the jingle: a soft thud, a harp going up, a piano over a warm pad");

    /* no title (the system's splash), a circle of dots turning under the logo */
    run_until(1200);
    save_con(dir, "spinner");
    /* the canvas x4: the spinner around (320, 244), 28 pixels of radius */
    check(light_outside(280, 204, 360, 284) == 0, "no title: nothing bright but the spinner");
    const int spin1 = light_pixels(204, 284);
    run_until(1300);
    check(spin1 > 8 && light_pixels(204, 284) > 8, "the spinner's bright dot");
    uint32_t snap[64];
    for (int i = 0; i < 64; i++)
        snap[i] = con[(260 + i / 8 * 4) * CW + 296 + i % 8 * 6];
    run_until(1500);
    int moved = 0;
    for (int i = 0; i < 64; i++)
        moved += snap[i] != con[(260 + i / 8 * 4) * CW + 296 + i % 8 * 6];
    check(moved > 0, "  turning");

    /* the game's page: the frames go there and are presented */
    g16_t page;
    g16_target(&page, page_px, 480, 480, 270, NULL);
    loading_page(&fb, &page);
    const int before = presents;
    run_until(1800);
    check(presents > before + 5, "on the game's page: presented frame after frame");
    int blue = 0;
    for (int i = 0; i < 480 * 270; i++)
        blue += (page_px[i] & 31) > 22 && (page_px[i] >> 11) < 20;
    check(blue > 1000, "the logo on the game's page (RGB565)");

    /* loaded already: it waits for the intro to end (1.8 s), then stops */
    loading_stop();
    now_us = 0;
    loading_begin(&fb);
    now_us = 100000;                            /* a fast game: loaded at 100 ms */
    notes = 0;
    landing_note_at = -1;
    loading_end();
    check(!loading_active() && now_us / 1000 >= 1800, "a fast game: the whole intro, then the game");
    check(notes == 6, "  with its jingle");
    int back = 1;
    for (int ch = 5; ch < 8; ch++)
        back &= vregs[ch][SYNTH_WAVEFORM] == 0 && vregs[ch][SYNTH_VOLUME] == 128;
    check(back, "  its voices back as a game finds them");

    /* a game's sound on one of its voices meanwhile: it stays */
    now_us = 0;
    loading_begin(&fb);
    run_until(1500);
    vregs[6][SYNTH_WAVEFORM] = SYNTH_SAW;
    loading_end();
    check(vregs[6][SYNTH_WAVEFORM] == SYNTH_SAW && vregs[5][SYNTH_WAVEFORM] == 0, "  but not one the game changed");

    /* the 8-bit sound (Settings): the chip's jingle */
    retro = 1;
    memset(wave_seen, 0, sizeof wave_seen);
    notes = 0;
    now_us = 0;
    loading_begin(&fb);
    run_until(800);
    check(notes == 6 && wave_seen[SYNTH_SQUARE] == 4 && wave_seen[SYNTH_NOISE] == 1 &&
          wave_seen[SYNTH_TRIANGLE] == 1, "8-bit sound: the chip's jingle, four square notes");
    loading_stop();
    retro = 0;

    /* a long load: it goes on until the end */
    now_us = 0;
    loading_begin(&fb);
    run_until(6000);
    check(loading_active(), "a long load: still on after 6 s");
    loading_end();
    check(!loading_active() && now_us / 1000 < 6100, "  and over at once when it is loaded");

    printf(fails ? "\n%d of %d FAILED\n" : "loading: %d/%d checks passed\n", fails ? fails : checks - fails, checks);
    return fails != 0;
}
