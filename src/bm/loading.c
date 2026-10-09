#include "loading.h"
#include "runtime.h"
#include "audio/audio.h"
#include "audio/presets.h"
#include "audio/synth.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "kernel/config.h"
#include "lib/printf.h"

#include <math.h>
#include <string.h>

#define CW 160                  /* the canvas: pixels as big as the screen allows */
#define CH 90
#define LAND_MS 500             /* the logo falls this long, then lands: the jingle */
#define BOUNCE_MS 150
#define MIN_MS 1800             /* at least this much: the intro, the jingle, a moment */
#define LOGO_Y 22               /* where it rests on the canvas */
#define SPIN_R 7                /* the spinner under it: its radius */
#define J_FIRST 5               /* the voices the jingle takes */
#define J_VOICES 3

/* the colours: the splash's background, the spinner's dots */
#define BG      0x0042u         /* RGB565 of #000810 */
#define C_DIM   0x5A6Eu
#define C_SHADOW 0x0000u

static uint16_t cv[CW * CH];
static g16_t cg;
static struct {
    int on;
    uint32_t t0, last, every;   /* timer_ticks at the start, of the last frame; us between frames */
    framebuffer_t *fb;          /* the screen */
    g16_t *page;                /* the game's page, or NULL: the console's page (32 bits) */
    int borders;                /* pages whose border around the canvas is still to fill */
    unsigned note;              /* the jingle's next note */
    int retro;                  /* the chip's jingle */
    uint8_t used[J_VOICES];     /* the voices it took, */
    uint8_t regs[J_VOICES][SYNTH_VOICE_BYTES]; /* as it left them */
} ld;

/* The jingle (ours: C major up, then the high C over the low one), from the
 * landing, with bm's instruments (presets.c, 2026-10-06: the user found the
 * square one too 8-bit): a soft thud, a harp going up, an electric piano on
 * the last note over a warm pad; quieter than the chip's, with almost
 * nothing over 4 kHz. The last voices, which a game rarely starts with. */
static const struct {
    uint16_t at, hz, ms, to;    /* to: the pitch slides there (Hz), or 0 */
    uint8_t ch, vol;
    const char *inst;
} jingle[] = {
    { 0, 150, 140, 55, 5, 50, "kick" },
    { 0, 523, 420, 0, 6, 70, "harp" },
    { 90, 659, 420, 0, 7, 70, "harp" },
    { 180, 784, 420, 0, 6, 70, "harp" },
    { 270, 1047, 900, 0, 7, 75, "epiano" },
    { 270, 262, 900, 0, 5, 70, "warm" },
};

/* With the 8-bit sound (Settings > Sound style, a game's retro()) the
 * chip's version, the first one: a noise thud, four square notes, a
 * triangle under the last one. */
static const struct {
    uint16_t at, hz, ms;
    uint8_t ch, wave, vol;
} chip[] = {
    { 0, 140, 60, 5, SYNTH_NOISE, 60 },
    { 0, 1047, 80, 6, SYNTH_SQUARE, 80 },
    { 75, 1319, 80, 6, SYNTH_SQUARE, 80 },
    { 150, 1568, 80, 6, SYNTH_SQUARE, 80 },
    { 225, 2093, 560, 6, SYNTH_SQUARE, 90 },
    { 225, 523, 560, 7, SYNTH_TRIANGLE, 120 },
};

static uint32_t now_ms(void)
{
    return (timer_ticks() - ld.t0) / 1000;
}

static uint16_t lighter(uint16_t c)             /* half way to white */
{
    return (uint16_t)(((c >> 1) & 0x7BEFu) + 0x7BEFu);
}

static uint16_t darker(uint16_t c)              /* three quarters: the screen's lines */
{
    return (uint16_t)(((c >> 1) & 0x7BEFu) + ((c >> 2) & 0x39E7u));
}

static void play_chip(uint32_t t)
{
    while (ld.note < sizeof chip / sizeof chip[0] && t >= (uint32_t)(LAND_MS + chip[ld.note].at)) {
        const unsigned i = ld.note++;
        const unsigned ch = chip[i].ch;
        if (chip[i].wave == SYNTH_SQUARE) {
            audio_envelope(ch, 1, 40, 150, 50);
            audio_duty(ch, 64);                 /* 25%: the old consoles' pulse */
        } else if (chip[i].wave == SYNTH_NOISE) {
            audio_envelope(ch, 0, 12, 0, 8);
        } else {
            audio_envelope(ch, 1, 80, 170, 60);
        }
        audio_note(ch, chip[i].hz, chip[i].ms, chip[i].wave, chip[i].vol);
    }
}

static void play_jingle(uint32_t t)
{
    if (ld.note == 0 && t >= LAND_MS)
        ld.retro = audio_retro_on();
    if (ld.retro) {
        play_chip(t);
        return;
    }
    while (ld.note < sizeof jingle / sizeof jingle[0] && t >= (uint32_t)(LAND_MS + jingle[ld.note].at)) {
        const unsigned i = ld.note++;
        const unsigned ch = jingle[i].ch;
        const int p = au_preset_find(jingle[i].inst);
        uint8_t *r = ld.regs[ch - J_FIRST];
        audio_tone_get(ch, r);
        if (p >= 0)
            au_sound_regs(&au_presets[p].s, r);
        r[SYNTH_VOLUME] = jingle[i].vol;
        audio_tone(ch, r);
        ld.used[ch - J_FIRST] = 1;
        audio_note(ch, jingle[i].hz, jingle[i].ms, -1, -1);
        if (jingle[i].to)
            audio_slide(ch, jingle[i].to, jingle[i].ms);
    }
}

/* The voices it took back as a game finds them (audio_reset), unless the
 * game gave them a sound of its own meanwhile. */
static void free_voices(void)
{
    for (unsigned k = 0; k < J_VOICES; k++) {
        if (!ld.used[k])
            continue;
        uint8_t now[SYNTH_VOICE_BYTES], def[SYNTH_VOICE_BYTES];
        audio_tone_get(J_FIRST + k, now);
        if (memcmp(now + SYNTH_WAVEFORM, ld.regs[k] + SYNTH_WAVEFORM, SYNTH_RELEASE + 1 - SYNTH_WAVEFORM) ||
            memcmp(now + SYNTH_CUTOFF, ld.regs[k] + SYNTH_CUTOFF, SYNTH_VOICE_BYTES - SYNTH_CUTOFF))
            continue;
        au_voice_default(def);
        audio_tone(J_FIRST + k, def);
        ld.used[k] = 0;
    }
}

/* one frame of the animation at t ms, on the canvas */
static void draw(uint32_t t)
{
    g16_rectfill(&cg, 0, 0, CW, CH, BG);
    const int lw = loading_logo_w, lh = loading_logo_h, x0 = (CW - lw) / 2;
    int y0;
    if (t < LAND_MS) {
        const float k = (float)t / LAND_MS;     /* falling faster and faster */
        y0 = -lh + (int)((LOGO_Y + lh) * k * k);
    } else if (t < LAND_MS + BOUNCE_MS) {
        y0 = LOGO_Y - (int)(4.0f * sinf(3.14159f * (float)(t - LAND_MS) / BOUNCE_MS));
    } else {
        y0 = LOGO_Y;
    }
    /* a shine across the letters after the landing */
    const int shine = t > LAND_MS + 120 && t < LAND_MS + 520 ? (int)(t - LAND_MS - 120) / 5 - 10 : -100;
    for (int pass = 0; pass < 2; pass++)        /* its shadow, then the logo */
        for (int y = 0; y < lh; y++) {
            const int sy = y0 + y + (pass ? 0 : 2);
            if (sy < 0 || sy >= CH)
                continue;
            for (int x = 0; x < lw; x++) {
                uint16_t c = loading_logo[y * lw + x];
                if (c == LOADING_KEY)
                    continue;
                if (!pass)
                    c = C_SHADOW;
                else if ((unsigned)(x - y - shine) < 4u)
                    c = lighter(c);
                cv[sy * CW + x0 + x + (pass ? 0 : 2)] = c;
            }
        }
    /* dust where it lands */
    if (t >= LAND_MS && t < LAND_MS + 300) {
        const int d = (int)(t - LAND_MS) / 30, fy = LOGO_Y + lh - 1 - d / 3;
        for (int i = 0; i < 3; i++) {
            g16_pset(&cg, x0 - 2 - d - i * 3, fy - i, C_DIM);
            g16_pset(&cg, x0 + lw + 1 + d + i * 3, fy - i, C_DIM);
        }
    }
    /* a circle of dots turning while it loads (the same for every game and
     * tool: the system's, no title) */
    if (t > LAND_MS + 300) {
        static const int8_t dx[8] = { 0, 5, 7, 5, 0, -5, -7, -5 }, dy[8] = { -7, -5, 0, 5, 7, 5, 0, -5 };
        const int cx = CW / 2, cy = LOGO_Y + lh + 4 + SPIN_R + 2, head = (int)(t / 85) % 8;
        for (int i = 0; i < 8; i++) {
            const int behind = (head - i + 8) % 8;     /* 0: the head, then its tail */
            const uint16_t c = behind == 0 ? 0xFFFFu : behind == 1 ? 0xA53Fu : behind == 2 ? 0x6C1Fu
                             : behind == 3 ? 0x4AF5u : 0x2128u;
            g16_rectfill(&cg, cx + dx[i] - 1, cy + dy[i] - 1, 2, 2, c);
        }
    }
}

/* the canvas as big as it fits, centred, every pixel a square with its
 * last row a little darker (a screen's lines) */
static void blit(void)
{
    const int W = ld.page ? ld.page->w : (int)ld.fb->width, H = ld.page ? ld.page->h : (int)ld.fb->height;
    int s = W / CW < H / CH ? W / CW : H / CH;
    s = s < 1 ? 1 : s;
    const int ox = (W - CW * s) / 2, oy = (H - CH * s) / 2;
    if (ld.page) {
        g16_t *g = ld.page;
        if (ld.borders > 0) {
            ld.borders--;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    g->px[(uint32_t)y * g->stride + x] = BG;
        }
        for (int cy = 0; cy < CH; cy++) {
            const uint16_t *src = cv + cy * CW;
            for (int r = 0; r < s; r++) {
                const int y = oy + cy * s + r;
                if (y < 0 || y >= H)
                    continue;
                uint16_t *dst = g->px + (uint32_t)y * g->stride + ox;
                const int dim = s >= 3 && r == s - 1;
                for (int cx = 0; cx < CW; cx++) {
                    const uint16_t c = dim ? darker(src[cx]) : src[cx];
                    for (int k = 0; k < s; k++)
                        *dst++ = c;
                }
            }
        }
        bm_video_present(ld.fb, g);
    } else {
        framebuffer_t *fb = ld.fb;
        if (ld.borders > 0) {
            ld.borders--;
            const uint32_t bg = fb_color(fb, 0, 8, 16);
            for (int y = 0; y < H; y++) {
                uint32_t *row = (uint32_t *)(fb->base + (uint32_t)y * fb->pitch);
                for (int x = 0; x < W; x++)
                    row[x] = bg;
            }
        }
        uint32_t line[CW], dline[CW];
        for (int cy = 0; cy < CH; cy++) {
            for (int cx = 0; cx < CW; cx++) {
                const uint16_t c = cv[cy * CW + cx], d = darker(c);
                line[cx] = fb_color(fb, (uint8_t)((c >> 11) << 3), (uint8_t)((c >> 5 & 63) << 2),
                                    (uint8_t)((c & 31) << 3));
                dline[cx] = fb_color(fb, (uint8_t)((d >> 11) << 3), (uint8_t)((d >> 5 & 63) << 2),
                                     (uint8_t)((d & 31) << 3));
            }
            for (int r = 0; r < s; r++) {
                const int y = oy + cy * s + r;
                if (y < 0 || y >= H)
                    continue;
                uint32_t *dst = (uint32_t *)(fb->base + (uint32_t)y * fb->pitch) + ox;
                const uint32_t *l = s >= 3 && r == s - 1 ? dline : line;
                for (int cx = 0; cx < CW; cx++)
                    for (int k = 0; k < s; k++)
                        *dst++ = l[cx];
            }
        }
    }
}

static void frame(void)
{
    const uint32_t t0 = timer_ticks();
    draw(now_ms());
    blit();
    /* the drawing takes at most a quarter of the time: the loading goes on */
    const uint32_t cost = timer_ticks() - t0;
    ld.every = cost * 4 > 33000u ? cost * 4 : 33000u;
    ld.last = timer_ticks();
}

void loading_begin(framebuffer_t *fb)
{
    const char *v = config_get("game_intro");
    if (!fb || (v && strcmp(v, "0") == 0))
        return;
    memset(&ld, 0, sizeof ld);
    ld.on = 1;
    ld.fb = fb;
    ld.borders = 3;
    ld.t0 = timer_ticks();
    g16_target(&cg, cv, CW, CW, CH, &font_console_6x12);
    console_suspend(1);                 /* the log goes on there, unseen */
    frame();
}

int loading_active(void)
{
    return ld.on;
}

void loading_page(framebuffer_t *fb, g16_t *page)
{
    if (!ld.on)
        return;
    ld.fb = fb;
    ld.page = page;
    ld.borders = 3;                     /* the pages of the game's mode, maybe three */
    frame();
}

void loading_tick(void)
{
    if (!ld.on)
        return;
    play_jingle(now_ms());
    if (timer_ticks() - ld.last >= ld.every)
        frame();
}

void loading_end(void)
{
    if (!ld.on)
        return;
    const uint32_t loaded = now_ms();
    while (now_ms() < MIN_MS) {
        loading_tick();
        audio_idle();
    }
    ld.on = 0;
    free_voices();
    /* the game's first frame comes next: the tests wait for this line */
    kprintf("bm: loaded in %lu ms, the splash over at %lu ms\n", (unsigned long)loaded, (unsigned long)now_ms());
}

void loading_stop(void)
{
    if (ld.on)
        free_voices();
    ld.on = 0;
}
