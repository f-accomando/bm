/*
 * Shell SDK: the music block (see shell_sdk_music.h). Original code.
 */
#include "shell_sdk_music.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_edit.h"
#include "shell_sdk_lists.h"

static const char *const channel_words[3] = { "PULSE", "TRIANGLE", "NOISE" };
static const char *const duty_words[4] = { "DUTY12", "DUTY25", "DUTY50", "DUTY75" };
static const char *const track_words[4] = { "PU1", "PU2", "TRI", "NOI" };
static const char *const fx_words[3] = { "UP", "DOWN", "VIB" };

/* ---- small things ---- */

static void copy_name(char *out, const char *s)
{
    snprintf(out, SDK_MUS_NAME, "%.*s", SDK_MUS_NAME - 1, s);
}

void sdk_music_clear(sdk_music_t *m) { memset(m, 0, sizeof *m); }

int sdk_music_empty(const sdk_music_t *m)
{
    for (int i = 0; i < SDK_MUS_PATTERNS; i++)
        if (m->pat[i].used)
            return 0;
    return !m->instruments && !m->sfxs && !m->songs && !m->waves;
}

void sdk_music_note_name(int note, char *out)
{
    static const char *const names[12] = { "C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B" };
    if (note == SDK_MUS_OFF)
        strcpy(out, "OFF");
    else if (note < SDK_MUS_LOW || note > SDK_MUS_HIGH)
        strcpy(out, "__");
    else
        snprintf(out, 4, "%s%d", names[note % 12], note / 12 - 1);
}

/* "C4", "cs4" -> the number; -1 when the word is not a note. */
static int note_of(const char *w)
{
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };       /* A B C D E F G */
    const int l = sdk_list_up((unsigned char)w[0]);
    if (l < 'A' || l > 'G')
        return -1;
    int semi = base[l - 'A'], i = 1;
    if (sdk_list_up((unsigned char)w[1]) == 'S') {
        if (l == 'B' || l == 'E')
            return -1;
        semi++;
        i = 2;
    }
    if (w[i] < '0' || w[i] > '9' || w[i + 1])
        return -1;
    const int n = (w[i] - '0' + 1) * 12 + semi;
    return n >= SDK_MUS_LOW && n <= SDK_MUS_HIGH ? n : -1;
}

/* "N0".."N15" -> 0..15, else -1 */
static int noise_of(const char *w)
{
    if (sdk_list_up((unsigned char)w[0]) != 'N' || w[1] < '0' || w[1] > '9')
        return -1;
    int n = w[1] - '0', i = 2;
    if (w[2] >= '0' && w[2] <= '9') {
        n = n * 10 + w[2] - '0';
        i = 3;
    }
    return !w[i] && n <= 15 ? n : -1;
}

int sdk_music_name_ok(const char *name)
{
    static const char *const taken[] = { "PULSE", "TRIANGLE", "NOISE", "DUTY12", "DUTY25", "DUTY50", "DUTY75",
        "SHORT", "LOOP", "RELEASE", "END", "PU1", "PU2", "TRI", "NOI", "UP", "DOWN", "VIB", "OFF" };
    const size_t n = strlen(name);
    if (!n || n >= SDK_MUS_NAME || !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')))
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!sdk_list_word_char((unsigned char)name[i]))
            return 0;
    for (size_t i = 0; i < sizeof taken / sizeof taken[0]; i++)
        if (sdk_list_same(name, taken[i]))
            return 0;
    if (note_of(name) >= 0 || noise_of(name) >= 0)
        return 0;
    char head[8];
    snprintf(head, sizeof head, "%.7s", name);
    if (sdk_list_same(head, "pattern"))
        return 0;
    snprintf(head, sizeof head, "%.4s", name);
    if (sdk_list_same(head, "sfx_"))
        return 0;
    snprintf(head, sizeof head, "%.5s", name);
    return !sdk_list_same(head, "song_") && !sdk_list_same(head, "wave_");
}

static const char *const snes_waves[SDK_MUS_WAVES + 1] = { "SQUARE12", "SQUARE25", "SQUARE50", "TRIANGLE", "SAW",
                                                          "SINE", "NOISE" };

int sdk_music_wave_name_ok(const char *name)
{
    for (int i = 0; i <= SDK_MUS_WAVES; i++)
        if (sdk_list_same(name, snes_waves[i]))
            return 0;
    return sdk_music_name_ok(name);
}

int sdk_music_wave_point(const sdk_music_t *m, int wave, int i)
{
    static const int8_t sine[32] = { 0, 1, 3, 4, 5, 6, 6, 7, 7, 7, 6, 6, 5, 4, 3, 1,
                                     0, -1, -3, -4, -5, -6, -6, -7, -7, -7, -6, -6, -5, -4, -3, -1 };
    if (wave > SDK_MUS_WAVES && wave - SDK_MUS_WAVES - 1 < m->waves)
        return m->drawn[wave - SDK_MUS_WAVES - 1].point[i];
    switch (wave) {
    case 0: return i < 4 ? 15 : 1;                          /* squares of 12.5, 25 and 50% */
    case 1: return i < 8 ? 15 : 1;
    case 2: return i < 16 ? 15 : 1;
    case 3: return 8 + (i < 8 ? i : i < 24 ? 15 - i : i - 31);      /* triangle */
    case 4: return i / 2;                                   /* saw */
    default: return 8 + sine[i];
    }
}

int sdk_music_wave_users(const sdk_music_t *m, int wave)
{
    int n = 0;
    for (int i = 0; i < m->instruments; i++)
        n += m->ins[i].wave == wave;
    for (int i = 0; i < m->sfxs; i++)
        n += m->sfx[i].wave == wave;
    return n;
}

int sdk_music_add_wave(sdk_music_t *m, int from)
{
    if (m->waves == SDK_MUS_DRAWN)
        return -1;
    sdk_mus_wave_t *w = &m->drawn[m->waves];
    memset(w, 0, sizeof *w);
    for (int i = 0; i < SDK_MUS_POINTS; i++)
        w->point[i] = (uint8_t)sdk_music_wave_point(m, from >= 0 && from < m->waves ? SDK_MUS_WAVES + 1 + from : 5, i);
    for (int n = 1;; n++) {
        if (n == 1)
            snprintf(w->name, sizeof w->name, "custom");
        else
            snprintf(w->name, sizeof w->name, "custom%d", n % 100);
        int taken = 0;
        for (int k = 0; k < m->waves; k++)
            taken |= sdk_list_same(m->drawn[k].name, w->name);
        if (!taken)
            return m->waves++;
    }
}

void sdk_music_remove_wave(sdk_music_t *m, int k)
{
    if (k < 0 || k >= m->waves)
        return;
    memmove(&m->drawn[k], &m->drawn[k + 1], (size_t)(m->waves - k - 1) * sizeof m->drawn[0]);
    m->waves--;
    const int gone = SDK_MUS_WAVES + 1 + k;
    for (int i = 0; i < m->instruments + m->sfxs; i++) {
        uint8_t *w = i < m->instruments ? &m->ins[i].wave : &m->sfx[i - m->instruments].wave;
        if (*w == gone)
            *w = 2;                                         /* the square of 50% */
        else if (*w > gone)
            (*w)--;
    }
}

/* ---- reading: what the lists say ---- */

static const char *shown(const sdk_value_t *v, char *buf, size_t n)
{
    if (v->word[0])
        snprintf(buf, n, "%s", v->word);
    else
        snprintf(buf, n, "%d", v->num);
    return buf;
}

static int number_in(sdk_reader_t *r, const sdk_value_t *v, int lo, int hi, const char *what, int *out)
{
    char b[24];
    if (v->word[0] || v->num < lo || v->num > hi)
        return sdk_list_wrong(r, v->line, "%s is %d to %d, not %s", what, lo, hi, shown(v, b, sizeof b));
    *out = v->num;
    return 0;
}

static int is(const sdk_value_t *v, const char *w) { return v->word[0] && sdk_list_same(v->word, w) && !v->num; }

static int one_of(const sdk_value_t *v, const char *const *list, int n)
{
    for (int i = 0; i < n; i++)
        if (is(v, list[i]))
            return i;
    return -1;
}

static int read_instrument(sdk_reader_t *r, const sdk_item_t *it, sdk_mus_ins_t *s)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1;                        /* without END */
    char b[24];
    if (n < 4)
        return sdk_list_wrong(r, it->line, "instrument %s needs channel, wave, vibrato, arpeggio", it->name);
    const int ch = one_of(&v[0], channel_words, 3);
    if (ch < 0)
        return sdk_list_wrong(r, v[0].line, "the channel of an instrument is PULSE, TRIANGLE or NOISE, not %s",
                     shown(&v[0], b, sizeof b));
    s->channel = (uint8_t)ch;
    int x = 0;
    if (ch == SDK_MUS_PULSE) {
        x = one_of(&v[1], duty_words, 4);
        if (x < 0 && number_in(r, &v[1], 0, 3, "the wave of a pulse (DUTY12 to DUTY75)", &x) != 0)
            return -1;
        s->wave = (uint8_t)x;
    } else {
        int shortn = v[1].word[0] && sdk_list_same(v[1].word, "SHORT");
        if ((v[1].word[0] && !shortn) || v[1].num > 15)
            return sdk_list_wrong(r, v[1].line, "the wave of the noise is 0 to 15, with +SHORT for the short noise");
        s->wave = (uint8_t)(ch == SDK_MUS_NOISE ? v[1].num | (shortn ? SDK_MUS_SHORT : 0) : 0);
    }
    if (number_in(r, &v[2], 0, 15, "vibrato", &x) != 0)
        return -1;
    s->vibrato = (uint8_t)x;
    if (number_in(r, &v[3], 0, 255, "arpeggio", &x) != 0)
        return -1;
    s->arp = (uint8_t)x;
    s->loop = s->release = SDK_MUS_NO;
    for (int i = 4; i < n; i++) {
        if (is(&v[i], "LOOP")) {
            if (s->loop != SDK_MUS_NO || s->release != SDK_MUS_NO)
                return sdk_list_wrong(r, v[i].line, "one LOOP, before RELEASE");
            s->loop = s->len;
        } else if (is(&v[i], "RELEASE")) {
            if (s->release != SDK_MUS_NO)
                return sdk_list_wrong(r, v[i].line, "one RELEASE in an instrument");
            s->release = s->len;
        } else {
            if (number_in(r, &v[i], 0, 15, "a volume", &x) != 0)
                return -1;
            if (s->len == SDK_MUS_ENV)
                return sdk_list_wrong(r, v[i].line, "an instrument has %d volumes at most", SDK_MUS_ENV);
            s->volume[s->len++] = (uint8_t)x;
        }
    }
    if ((s->loop != SDK_MUS_NO && s->loop >= s->len) || (s->release != SDK_MUS_NO && s->release >= s->len) ||
        (s->loop != SDK_MUS_NO && s->loop == s->release))
        return sdk_list_wrong(r, it->line, "LOOP and RELEASE of %s need a volume after them", it->name);
    return 0;
}

static int find_ins(const sdk_music_t *m, const char *word)
{
    for (int i = 0; i < m->instruments; i++)
        if (sdk_list_same(m->ins[i].name, word))
            return i;
    return -1;
}

static int ins_value(sdk_reader_t *r, const sdk_music_t *m, const sdk_value_t *v, int channel, int track, uint8_t *out)
{
    char b[24];
    if (is(v, "__") || (!v->word[0] && !v->num)) {
        *out = 0;
        return 0;
    }
    const int i = v->word[0] && !v->num ? find_ins(m, v->word) : -1;
    if (i < 0)
        return sdk_list_wrong(r, v->line, "there is no instrument '%s'", shown(v, b, sizeof b));
    if (m->ins[i].channel != channel)
        return sdk_list_wrong(r, v->line, "%s is a %s instrument: track %d wants %s", m->ins[i].name,
                     channel_words[m->ins[i].channel], track + 1, channel_words[channel]);
    *out = (uint8_t)(i + 1);
    return 0;
}

static int note_value(sdk_reader_t *r, const sdk_value_t *v, int noise, uint8_t *out)
{
    char b[24];
    if (is(v, "__") || (!v->word[0] && !v->num))
        *out = SDK_MUS_NONE;
    else if (is(v, "OFF"))
        *out = SDK_MUS_OFF;
    else {
        int n = -1;
        if (v->word[0] && !v->num) {
            if (noise) {
                n = noise_of(v->word);
                n = n < 0 ? -1 : SDK_MUS_LOW + n;
            } else
                n = note_of(v->word);
        }
        if (n < 0)
            return sdk_list_wrong(r, v->line, noise ? "'%s' is not a noise pitch (N0 to N15, __ or OFF)"
                                           : "'%s' is not a note (C1 to B7, CS4, __ or OFF)", shown(v, b, sizeof b));
        *out = (uint8_t)n;
    }
    return 0;
}

static int read_pattern(sdk_reader_t *r, const sdk_music_t *m, const sdk_item_t *it, sdk_mus_pattern_t *p)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1;
    if (!n || n % 7 || n / 7 > SDK_MUS_STEPS)
        return sdk_list_wrong(r, it->line, "%s has %d values: a step is 7 (3 notes with instruments, 1 noise), 1 to %d "
                     "steps", it->name, n, SDK_MUS_STEPS);
    p->used = 1;
    p->steps = (uint8_t)(n / 7);
    p->line = it->line;
    for (int s = 0; s < p->steps; s++, v += 7) {
        p->row_line[s] = (uint16_t)v->line;
        for (int t = 0; t < 3; t++) {
            if (note_value(r, &v[2 * t], 0, &p->cell[s][t].note) != 0 ||
                ins_value(r, m, &v[2 * t + 1], t < 2 ? SDK_MUS_PULSE : SDK_MUS_TRIANGLE, t, &p->cell[s][t].ins) != 0)
                return -1;
        }
        if (is(&v[6], "OFF"))
            p->cell[s][3].note = SDK_MUS_OFF;
        else if (ins_value(r, m, &v[6], SDK_MUS_NOISE, 3, &p->cell[s][3].ins) != 0)
            return -1;
    }
    return 0;
}

/* The three values of a step of an effect: note, volume, effect. */
static int read_step(sdk_reader_t *r, const sdk_value_t *v, int noise, sdk_mus_step_t *s)
{
    int x;
    if (note_value(r, &v[0], noise, &s->note) != 0 || number_in(r, &v[1], 0, 15, "a volume", &x) != 0)
        return -1;
    s->volume = (uint8_t)x;
    int fx = 0;
    for (int f = 0; f < 3; f++)
        if (v[2].word[0] && sdk_list_same(v[2].word, fx_words[f]))
            fx = f + 1;
    if (fx && v[2].num >= 1 && v[2].num <= 15)
        s->fx = (uint8_t)(fx << 4 | v[2].num);
    else if (is(&v[2], "__") || (!v[2].word[0] && !v[2].num))
        s->fx = 0;
    else
        return sdk_list_wrong(r, v[2].line, "an effect is __, UP+n, DOWN+n or VIB+n with n 1 to 15");
    return 0;
}

static int read_sfx(sdk_reader_t *r, const sdk_item_t *it, sdk_mus_sfx_t *s)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1;
    char b[24];
    int x;
    if (n < 3 || (n - 3) % 3 || (n - 3) / 3 > SDK_MUS_SFX_STEPS)
        return sdk_list_wrong(r, it->line, "%s needs speed, channel, wave, then note, volume, effect a step (%d at most)",
                     it->name, SDK_MUS_SFX_STEPS);
    if (number_in(r, &v[0], 1, 60, "the speed (frames a step)", &x) != 0)
        return -1;
    s->speed = (uint8_t)x;
    const int ch = one_of(&v[1], track_words, 4);
    if (ch < 0)
        return sdk_list_wrong(r, v[1].line, "the channel of an effect is PU1, PU2, TRI or NOI, not %s",
                     shown(&v[1], b, sizeof b));
    s->channel = (uint8_t)ch;
    if (ch < 2) {
        x = one_of(&v[2], duty_words, 4);
        if (x < 0 && number_in(r, &v[2], 0, 3, "the wave of a pulse (DUTY12 to DUTY75)", &x) != 0)
            return -1;
    } else if (is(&v[2], "SHORT"))
        x = SDK_MUS_SHORT;
    else if (number_in(r, &v[2], 0, 0, "the wave here (0, or SHORT for the noise)", &x) != 0)
        return -1;
    s->wave = (uint8_t)x;
    s->steps = (uint8_t)((n - 3) / 3);
    v += 3;
    for (int i = 0; i < s->steps; i++, v += 3)
        if (read_step(r, v, ch == 3, &s->step[i]) != 0)
            return -1;
    return 0;
}

static int read_song(sdk_reader_t *r, const sdk_item_t *it, sdk_mus_song_t *s)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1;
    int x;
    if (n < 3 || n - 2 > SDK_MUS_ORDER)
        return sdk_list_wrong(r, it->line, "%s needs speed, loop place, then 1 to %d pattern numbers", it->name,
                     SDK_MUS_ORDER);
    if (number_in(r, &v[0], 1, 60, "the speed (frames a step)", &x) != 0)
        return -1;
    s->speed = (uint8_t)x;
    s->len = (uint8_t)(n - 2);
    if (is(&v[1], "END"))
        s->loop = SDK_MUS_NO;
    else {
        if (number_in(r, &v[1], 0, s->len - 1, "the place to loop to (or END)", &x) != 0)
            return -1;
        s->loop = (uint8_t)x;
    }
    for (int i = 0; i < s->len; i++) {
        if (number_in(r, &v[2 + i], 0, SDK_MUS_PATTERNS - 1, "a pattern number", &x) != 0)
            return -1;
        s->order[i] = (uint8_t)x;
    }
    return 0;
}

static const char *const voice_words[SDK_MUS_VOICES] = { "V1", "V2", "V3", "V4", "V5", "V6", "V7", "V8" };

/* The number of a wave from its word: one of the driver, the noise, or a drawn one. -1: none. */
static int wave_of(const sdk_music_t *m, const sdk_value_t *v)
{
    const int w = one_of(v, snes_waves, SDK_MUS_WAVES + 1);
    for (int k = 0; w < 0 && k < m->waves; k++)
        if (is(v, m->drawn[k].name))
            return SDK_MUS_WAVES + 1 + k;
    return w;
}

static int read_wave(sdk_reader_t *r, const sdk_item_t *it, sdk_mus_wave_t *w)
{
    const sdk_value_t *v = r->value + it->first;
    if (it->count != SDK_MUS_POINTS + 1)
        return sdk_list_wrong(r, it->line, "%s has %d values: a wave is %d points 0 to 15, then END", it->name,
                              it->count - 1, SDK_MUS_POINTS);
    for (int i = 0; i < SDK_MUS_POINTS; i++) {
        int x;
        if (number_in(r, &v[i], 0, 15, "a point of a wave", &x) != 0)
            return -1;
        w->point[i] = (uint8_t)x;
    }
    return 0;
}

static int read_snes_instrument(sdk_reader_t *r, const sdk_music_t *m, const sdk_item_t *it, sdk_mus_ins_t *s)
{
    const sdk_value_t *v = r->value + it->first;
    static const char *const what[9] = { "the noise rate", "attack", "decay", "sustain", "release", "volume",
                                         "pan", "vibrato", "arpeggio" };
    static const int most[9] = { 31, 15, 7, 7, 31, 127, 16, 15, 255 };
    char b[24];
    int x[9];
    if (it->count != 11)
        return sdk_list_wrong(r, it->line, "instrument %s is: wave, noise rate, attack, decay, sustain, release, "
                              "volume, pan, vibrato, arpeggio, END", it->name);
    const int w = wave_of(m, &v[0]);
    if (w < 0)
        return sdk_list_wrong(r, v[0].line, "the wave is SQUARE12, SQUARE25, SQUARE50, TRIANGLE, SAW, SINE, NOISE "
                              "or the name of a wave_ list, not %s", shown(&v[0], b, sizeof b));
    for (int i = 0; i < 9; i++)
        if (number_in(r, &v[1 + i], 0, most[i], what[i], &x[i]) != 0)
            return -1;
    s->wave = (uint8_t)w;
    s->noise = (uint8_t)x[0];
    s->attack = (uint8_t)x[1];
    s->decay = (uint8_t)x[2];
    s->sustain = (uint8_t)x[3];
    s->rate = (uint8_t)x[4];
    s->level = (uint8_t)x[5];
    s->pan = (uint8_t)x[6];
    s->vibrato = (uint8_t)x[7];
    s->arp = (uint8_t)x[8];
    s->loop = s->release = SDK_MUS_NO;
    return 0;
}

static int read_snes_pattern(sdk_reader_t *r, const sdk_music_t *m, const sdk_item_t *it, sdk_mus_pattern_t *p)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1, row = 2 * SDK_MUS_VOICES;
    if (!n || n % row || n / row > SDK_MUS_STEPS)
        return sdk_list_wrong(r, it->line, "%s has %d values: a step is %d (a note and an instrument for 8 voices), "
                              "1 to %d steps", it->name, n, row, SDK_MUS_STEPS);
    p->used = 1;
    p->steps = (uint8_t)(n / row);
    p->line = it->line;
    for (int s = 0; s < p->steps; s++, v += row) {
        p->row_line[s] = (uint16_t)v->line;
        for (int t = 0; t < SDK_MUS_VOICES; t++) {
            const sdk_value_t *i = &v[2 * t + 1];
            char b[24];
            if (note_value(r, &v[2 * t], 0, &p->cell[s][t].note) != 0)
                return -1;
            if (is(i, "__") || (!i->word[0] && !i->num))
                continue;
            const int at = i->word[0] && !i->num ? find_ins(m, i->word) : -1;
            if (at < 0)
                return sdk_list_wrong(r, i->line, "there is no instrument '%s'", shown(i, b, sizeof b));
            p->cell[s][t].ins = (uint8_t)(at + 1);
        }
    }
    return 0;
}

static int read_snes_sfx(sdk_reader_t *r, const sdk_music_t *m, const sdk_item_t *it, sdk_mus_sfx_t *s)
{
    const sdk_value_t *v = r->value + it->first;
    const int n = it->count - 1;
    char b[24];
    int x;
    if (n < 3 || (n - 3) % 3 || (n - 3) / 3 > SDK_MUS_SFX_STEPS)
        return sdk_list_wrong(r, it->line, "%s needs speed, voice, wave, then note, volume, effect a step (%d at "
                              "most)", it->name, SDK_MUS_SFX_STEPS);
    if (number_in(r, &v[0], 1, 60, "the speed (frames a step)", &x) != 0)
        return -1;
    s->speed = (uint8_t)x;
    const int voice = one_of(&v[1], voice_words, SDK_MUS_VOICES);
    if (voice < 0)
        return sdk_list_wrong(r, v[1].line, "the voice of an effect is V1 to V8, not %s", shown(&v[1], b, sizeof b));
    s->channel = (uint8_t)voice;
    const int w = wave_of(m, &v[2]);
    if (w < 0)
        return sdk_list_wrong(r, v[2].line, "'%s' is not a wave", shown(&v[2], b, sizeof b));
    s->wave = (uint8_t)w;
    s->steps = (uint8_t)((n - 3) / 3);
    v += 3;
    for (int i = 0; i < s->steps; i++, v += 3)
        if (read_step(r, v, w == SDK_MUS_WAVES, &s->step[i]) != 0)
            return -1;
    return 0;
}

/* The kind of a list from its name: 0 instrument, 1 pattern (its number in *num), 2 sfx_, 3 song_, 4 wave_,
 * -1 wrong. */
static int kind_of(const char *name, int *num)
{
    if (!strncmp(name, "pattern", 7)) {
        const char *d = name + 7;
        int n = 0, digits = 0;
        while (*d >= '0' && *d <= '9' && digits < 3)
            n = n * 10 + *d++ - '0', digits++;
        *num = n;
        return digits && !*d && n < SDK_MUS_PATTERNS ? 1 : -1;
    }
    if (!strncmp(name, "sfx_", 4))
        return sdk_music_name_ok(name + 4) ? 2 : -1;
    if (!strncmp(name, "song_", 5))
        return sdk_music_name_ok(name + 5) ? 3 : -1;
    if (!strncmp(name, "wave_", 5))
        return sdk_music_wave_name_ok(name + 5) ? 4 : -1;
    return sdk_music_name_ok(name) ? 0 : -1;
}

static int interpret(sdk_reader_t *r, sdk_music_t *m)
{
    for (int pass = 0; pass < 3; pass++)    /* the waves, then the instruments that name them, then the rest */
        for (int i = 0; i < r->items; i++) {
            const sdk_item_t *it = &r->item[i];
            int num = 0;
            const int kind = kind_of(it->name, &num);
            if (kind < 0 || (kind == 4 && !m->snes))
                return sdk_list_wrong(r, it->line, "'%s' cannot be a name here", it->name);
            if (pass != (kind == 4 ? 0 : kind == 0 ? 1 : 2))
                continue;
            if (!it->count || !is(&r->value[it->first + it->count - 1], "END"))
                return sdk_list_wrong(r, it->line, "'%s' ends with END", it->name);
            if (kind == 4) {
                if (m->waves == SDK_MUS_DRAWN)
                    return sdk_list_wrong(r, it->line, "%d waves at most", SDK_MUS_DRAWN);
                sdk_mus_wave_t *w = &m->drawn[m->waves];
                copy_name(w->name, it->name + 5);
                w->line = it->line;
                if (read_wave(r, it, w) != 0)
                    return -1;
                m->waves++;
            } else if (kind == 0) {
                if (m->instruments == SDK_MUS_INS)
                    return sdk_list_wrong(r, it->line, "%d instruments at most", SDK_MUS_INS);
                sdk_mus_ins_t *s = &m->ins[m->instruments];
                copy_name(s->name, it->name);
                s->line = it->line;
                if ((m->snes ? read_snes_instrument(r, m, it, s) : read_instrument(r, it, s)) != 0)
                    return -1;
                m->instruments++;
            } else if (kind == 1) {
                if (m->pat[num].used)
                    return sdk_list_wrong(r, it->line, "pattern %d is there twice", num);
                if ((m->snes ? read_snes_pattern(r, m, it, &m->pat[num])
                             : read_pattern(r, m, it, &m->pat[num])) != 0)
                    return -1;
            } else if (kind == 2) {
                if (m->sfxs == SDK_MUS_SFX)
                    return sdk_list_wrong(r, it->line, "%d effects at most", SDK_MUS_SFX);
                sdk_mus_sfx_t *s = &m->sfx[m->sfxs];
                copy_name(s->name, it->name + 4);
                s->line = it->line;
                if ((m->snes ? read_snes_sfx(r, m, it, s) : read_sfx(r, it, s)) != 0)
                    return -1;
                m->sfxs++;
            } else {
                if (m->songs == SDK_MUS_SONGS)
                    return sdk_list_wrong(r, it->line, "%d songs at most", SDK_MUS_SONGS);
                sdk_mus_song_t *s = &m->song[m->songs];
                copy_name(s->name, it->name + 5);
                s->line = it->line;
                if (read_song(r, it, s) != 0)
                    return -1;
                m->songs++;
            }
        }
    return 0;
}

static int parse(const char *src, size_t len, int c, int snes, sdk_music_t *m, char *err, size_t en)
{
    sdk_music_clear(m);
    m->snes = snes;
    sdk_reader_t *r = NULL;
    const int found = sdk_list_read(src, len, c, "music", &r, err, en);
    if (found <= 0)
        return found;
    const int rc = interpret(r, m);
    if (rc != 0) {
        sdk_music_clear(m);
        m->snes = snes;
    }
    sdk_list_free(r);
    return rc;
}

int sdk_music_parse(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en)
{
    return parse(src, len, c, 0, m, err, en);
}

int sdk_music_parse_snes(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en)
{
    return parse(src, len, c, 1, m, err, en);
}

static void wave_word(int channel, int wave, char *out, size_t n)
{
    if (channel == SDK_MUS_PULSE)
        snprintf(out, n, "%s", duty_words[wave & 3]);
    else
        snprintf(out, n, "%d%s", wave & 15, wave & SDK_MUS_SHORT ? "+SHORT" : "");
}

static void ins_word(const sdk_music_t *m, int ins, const char *none, char *out, size_t n)
{
    if (ins < 1 || ins > m->instruments) {
        snprintf(out, n, "%s", none);
        return;
    }
    const char *s = m->ins[ins - 1].name;
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++)
        out[i] = (char)sdk_list_up((unsigned char)s[i]);
    out[i] = 0;
}

static void write_instrument(sdk_text_t *t, int c, const sdk_mus_ins_t *s)
{
    sdk_list_t l;
    char line[160], wave[16];
    sdk_list_list_open(&l, t, c, s->name, "instrument");
    wave_word(s->channel, s->wave, wave, sizeof wave);
    snprintf(line, sizeof line, "%s, %s, %d, $%02X", channel_words[s->channel], s->channel == SDK_MUS_TRIANGLE ?
             "0" : wave, s->vibrato, s->arp);
    sdk_list_list_line(&l, line, "channel,wave,vibrato,arpeggio");
    size_t at = 0;
    int on = 0, first = 1;
    for (int i = 0; i < s->len; i++) {
        if (i == s->loop)
            at = sdk_list_put(line, at, sizeof line, "%sLOOP", at ? ", " : "");
        if (i == s->release)
            at = sdk_list_put(line, at, sizeof line, "%sRELEASE", at ? ", " : "");
        at = sdk_list_put(line, at, sizeof line, "%s%d", at ? ", " : "", s->volume[i]);
        if (++on == 10) {
            sdk_list_list_line(&l, line, first ? "volume" : "");
            at = 0;
            on = 0;
            first = 0;
        }
    }
    sdk_list_put(line, at, sizeof line, "%sEND", at ? ", " : "");
    sdk_list_list_line(&l, line, first ? "volume" : "");
    sdk_list_list_flush(&l, 1);
}

static void write_pattern(sdk_text_t *t, int c, const sdk_music_t *m, int num)
{
    const sdk_mus_pattern_t *p = &m->pat[num];
    sdk_list_t l;
    char name[16], line[160];
    snprintf(name, sizeof name, "pattern%02d", num);
    sdk_list_list_open(&l, t, c, name, "a step: pulse 1, 2, triangle, noise");
    for (int s = 0; s < p->steps; s++) {
        size_t at = 0;
        for (int k = 0; k < 3; k++) {
            char note[4], ins[SDK_MUS_NAME], cell[32];
            sdk_music_note_name(p->cell[s][k].note, note);
            ins_word(m, p->cell[s][k].ins, "0", ins, sizeof ins);
            snprintf(cell, sizeof cell, "%s,%s,", note, ins);
            at = sdk_list_put(line, at, sizeof line, "%-11s ", cell);
        }
        char noise[SDK_MUS_NAME], num2[8];
        if (p->cell[s][3].note == SDK_MUS_OFF)
            strcpy(noise, "OFF");
        else
            ins_word(m, p->cell[s][3].ins, "__", noise, sizeof noise);
        sdk_list_put(line, at, sizeof line, "%-5s", noise);
        snprintf(num2, sizeof num2, "%02d", s);
        sdk_list_list_line(&l, line, num2);
    }
    sdk_list_list_line(&l, "END", "");
    sdk_list_list_flush(&l, 1);
}

/* A wave of a SNES list as the word that names it. */
static void snes_wave_word(const sdk_music_t *m, int wave, char *out, size_t n)
{
    const int k = wave - SDK_MUS_WAVES - 1;
    if (k < 0 || k >= m->waves) {
        snprintf(out, n, "%s", snes_waves[wave <= SDK_MUS_WAVES ? wave : 2]);
        return;
    }
    size_t i = 0;
    for (; m->drawn[k].name[i] && i + 1 < n; i++)
        out[i] = (char)sdk_list_up((unsigned char)m->drawn[k].name[i]);
    out[i] = 0;
}

static void write_wave(sdk_text_t *t, int c, const sdk_mus_wave_t *w)
{
    sdk_list_t l;
    char name[20], line[160], note[24];
    snprintf(name, sizeof name, "wave_%s", w->name);
    sdk_list_list_open(&l, t, c, name, "a wave: 32 points 0..15");
    for (int i = 0; i < SDK_MUS_POINTS; i += 8) {
        size_t at = 0;
        for (int k = i; k < i + 8; k++)
            at = sdk_list_put(line, at, sizeof line, "%s%2d", k > i ? ", " : "", w->point[k]);
        snprintf(note, sizeof note, "points %02d-%02d", i, i + 7);
        sdk_list_list_line(&l, line, note);
    }
    sdk_list_list_line(&l, "END", "");
    sdk_list_list_flush(&l, 1);
}

static void write_sfx(sdk_text_t *t, int c, const sdk_music_t *m, const sdk_mus_sfx_t *s)
{
    const int snes = m->snes;
    sdk_list_t l;
    char name[20], line[160], wave[16];
    snprintf(name, sizeof name, "sfx_%s", s->name);
    sdk_list_list_open(&l, t, c, name, "sound effect");
    if (!snes && s->channel < 2)
        snprintf(wave, sizeof wave, "%s", duty_words[s->wave & 3]);
    else if (!snes)
        snprintf(wave, sizeof wave, "%s", s->wave & SDK_MUS_SHORT ? "SHORT" : "0");
    else
        snes_wave_word(m, s->wave, wave, sizeof wave);
    snprintf(line, sizeof line, "%d, %s, %s", s->speed,
             snes ? voice_words[s->channel & 7] : track_words[s->channel & 3], wave);
    sdk_list_list_line(&l, line, snes ? "frames a step, voice, wave" : "frames a step, channel, wave");
    size_t at = 0;
    for (int i = 0; i < s->steps; i++) {
        const sdk_mus_step_t *e = &s->step[i];
        char note[4], fx[12];
        if ((snes ? s->wave == SDK_MUS_WAVES : s->channel == 3) && e->note >= SDK_MUS_LOW)
            snprintf(note, sizeof note, "N%d", (e->note - SDK_MUS_LOW) & 15);
        else
            sdk_music_note_name(e->note, note);
        if (e->fx >> 4 >= 1 && e->fx >> 4 <= 3)
            snprintf(fx, sizeof fx, "%s+%d", fx_words[(e->fx >> 4) - 1], e->fx & 15);
        else
            strcpy(fx, "__");
        at = sdk_list_put(line, at, sizeof line, "%s%s,%d,%s", at ? ",  " : "", note, e->volume, fx);
        if (i % 4 == 3 || i == s->steps - 1) {
            sdk_list_list_line(&l, line, "");
            at = 0;
        }
    }
    sdk_list_list_line(&l, "END", "");
    sdk_list_list_flush(&l, 1);
}

static void write_song(sdk_text_t *t, int c, const sdk_mus_song_t *s)
{
    sdk_list_t l;
    char name[20], line[160];
    snprintf(name, sizeof name, "song_%s", s->name);
    sdk_list_list_open(&l, t, c, name, "song");
    if (s->loop == SDK_MUS_NO)
        snprintf(line, sizeof line, "%d, END", s->speed);
    else
        snprintf(line, sizeof line, "%d, %d", s->speed, s->loop);
    sdk_list_list_line(&l, line, "frames a step, place to loop to");
    size_t at = 0;
    for (int i = 0; i < s->len; i++) {
        at = sdk_list_put(line, at, sizeof line, "%s%d", at ? ", " : "", s->order[i]);
        if (i % 16 == 15 || i == s->len - 1) {
            sdk_list_list_line(&l, line, i < 16 ? "patterns" : "");
            at = 0;
        }
    }
    sdk_list_list_line(&l, "END", "");
    sdk_list_list_flush(&l, 1);
}

static void write_snes_instrument(sdk_text_t *t, int c, const sdk_music_t *m, const sdk_mus_ins_t *s)
{
    sdk_list_t l;
    char line[160], wave[16];
    sdk_list_list_open(&l, t, c, s->name, "instrument");
    snes_wave_word(m, s->wave, wave, sizeof wave);
    snprintf(line, sizeof line, "%s, %d", wave, s->noise);
    sdk_list_list_line(&l, line, "wave, noise rate");
    snprintf(line, sizeof line, "%d, %d, %d, %d", s->attack, s->decay, s->sustain, s->rate);
    sdk_list_list_line(&l, line, "attack,decay,sustain,release");
    snprintf(line, sizeof line, "%d, %d, %d, $%02X, END", s->level, s->pan, s->vibrato, s->arp);
    sdk_list_list_line(&l, line, "volume,pan,vibrato,arpeggio");
    sdk_list_list_flush(&l, 1);
}

static void write_snes_pattern(sdk_text_t *t, int c, const sdk_music_t *m, int num)
{
    const sdk_mus_pattern_t *p = &m->pat[num];
    sdk_list_t l;
    char name[16], line[160];
    snprintf(name, sizeof name, "pattern%02d", num);
    sdk_list_list_open(&l, t, c, name, "two rows a step: voices 1-4, 5-8");
    for (int s = 0; s < p->steps; s++)
        for (int half = 0; half < 2; half++) {
            size_t at = 0;
            for (int k = 4 * half; k < 4 * half + 4; k++) {
                char note[4], ins[SDK_MUS_NAME], cell[32];
                sdk_music_note_name(p->cell[s][k].note, note);
                ins_word(m, p->cell[s][k].ins, "0", ins, sizeof ins);
                snprintf(cell, sizeof cell, "%s,%s,", note, ins);
                at = sdk_list_put(line, at, sizeof line, k % 4 < 3 ? "%-11s " : "%s", cell);
            }
            line[strlen(line) - 1] = 0;             /* the last ',' comes with the next line */
            char comment[16];
            snprintf(comment, sizeof comment, half ? "" : "%02d", s);
            sdk_list_list_line(&l, line, comment);
        }
    sdk_list_list_line(&l, "END", "");
    sdk_list_list_flush(&l, 1);
}

static void write_block(sdk_text_t *t, const sdk_music_t *m, int c)
{
    sdk_list_add(t, "%s@music  from the Sounds tab: C1..B7 (CS4 sharp), __ nothing, OFF release\n", c ? "//" : ";");
    for (int i = 0; m->snes && i < m->waves; i++)
        write_wave(t, c, &m->drawn[i]);
    for (int i = 0; i < m->instruments; i++)
        if (m->snes)
            write_snes_instrument(t, c, m, &m->ins[i]);
        else
            write_instrument(t, c, &m->ins[i]);
    for (int i = 0; i < SDK_MUS_PATTERNS; i++)
        if (m->pat[i].used) {
            if (m->snes)
                write_snes_pattern(t, c, m, i);
            else
                write_pattern(t, c, m, i);
        }
    for (int i = 0; i < m->sfxs; i++)
        write_sfx(t, c, m, &m->sfx[i]);
    for (int i = 0; i < m->songs; i++)
        write_song(t, c, &m->song[i]);
    sdk_list_add(t, "%s@end\n", c ? "//" : ";");
}

long sdk_music_text(const sdk_music_t *m, int c, char *out, size_t max)
{
    sdk_text_t t = { out, max, 0, 0, 0 };
    if (max)
        out[0] = 0;
    write_block(&t, m, c);
    return t.full ? -1 : (long)t.len;
}

int sdk_music_commit(const sdk_music_t *m, int c, int run, char *err, size_t en)
{
    sdk_text_t t = { NULL, 0, 0, 0, 1 };
    write_block(&t, m, c);
    int rc = -1;
    if (t.full)
        snprintf(err, en, "not enough memory");
    else
        rc = sdk_list_commit("music", c, t.s, t.len, run, err, en);
    free(t.s);
    return rc;
}

int sdk_music_find(const char *src, size_t len, int c, size_t *from, size_t *to)
{
    return sdk_list_find(src, len, c, "music", from, to);
}

/* ---- for the builder ---- */

int sdk_music_called(const char *src, size_t len, int c)
{
    static const char *const calls[] = { "snd_init", "snd_frame", "play_song", "play_sfx", "stop_music" };
    for (size_t i = 0; i < len;) {
        if ((!c && src[i] == ';') || (c && src[i] == '/' && i + 1 < len && src[i + 1] == '/')) {
            while (i < len && src[i] != '\n')
                i++;
        } else if (c && src[i] == '/' && i + 1 < len && src[i + 1] == '*') {
            for (i += 2; i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'); i++)
                ;
            i += 2;
        } else if (src[i] == '"') {
            for (i++; i < len && src[i] != '"' && src[i] != '\n'; i++)
                ;
            i++;
        } else if (sdk_list_word_char((unsigned char)src[i])) {
            size_t e = i;
            while (e < len && sdk_list_word_char((unsigned char)src[e]))
                e++;
            for (size_t k = 0; k < sizeof calls / sizeof calls[0]; k++)
                if (strlen(calls[k]) == e - i && !memcmp(calls[k], src + i, e - i))
                    return 1;
            i = e;
        } else
            i++;
    }
    return 0;
}

/* A table of the low or high bytes of the addresses of lists. have: which entries there are. */
static void table(sdk_text_t *t, const char *name, char kind, int n, const sdk_music_t *m)
{
    for (int high = 0; high < 2; high++) {
        sdk_list_add(t, "%s_%s:\n", name, high ? "hi" : "lo");
        if (!n)
            sdk_list_add(t, "    .byte 0\n");
        for (int i = 0; i < n; i++)
            if (kind == 'p' && !m->pat[i].used)
                sdk_list_add(t, "    .byte 0\n");
            else
                sdk_list_add(t, "    .byte %c__snd_%c%d\n", high ? '>' : '<', kind, i);
    }
}

static void bytes(sdk_text_t *t, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i += 16) {
        char line[96];
        size_t at = 0;
        for (int k = i; k < n && k < i + 16; k++)
            at = sdk_list_put(line, at, sizeof line, "%s%d", k > i ? "," : "", b[k]);
        sdk_list_add(t, "    .byte %s\n", line);
    }
}

char *sdk_music_asm(const sdk_music_t *m)
{
    sdk_text_t t = { NULL, 0, 0, 0, 1 };
    sdk_list_add(&t, "; ==== the sound: the driver and the data of the Sounds tab =====================\n");
    sdk_list_add(&t, "__snd_songs = %d\n__snd_sfxs  = %d\n", m->songs, m->sfxs);
    for (int i = 0; i < m->songs; i++)
        sdk_list_add(&t, "song_%s = %d\n", m->song[i].name, i);
    for (int i = 0; i < m->sfxs; i++)
        sdk_list_add(&t, "sfx_%s = %d\n", m->sfx[i].name, i);
    sdk_list_add_str(&t, sdk_snd_driver());

    int patterns = 0;                       /* up to the last one that is there or that a song names */
    for (int i = 0; i < SDK_MUS_PATTERNS; i++)
        if (m->pat[i].used)
            patterns = i + 1;
    for (int i = 0; i < m->songs; i++)
        for (int k = 0; k < m->song[i].len; k++)
            if (m->song[i].order[k] >= patterns)
                patterns = m->song[i].order[k] + 1;
    table(&t, "__snd_ins", 'i', m->instruments, m);
    table(&t, "__snd_pat", 'p', patterns, m);
    table(&t, "__snd_song", 's', m->songs, m);
    table(&t, "__snd_sfx", 'x', m->sfxs, m);
    sdk_list_add(&t, "__snd_sfx_ch:\n");
    if (!m->sfxs)
        sdk_list_add(&t, "    .byte 0\n");
    for (int i = 0; i < m->sfxs; i++)
        sdk_list_add(&t, "    .byte %d\n", m->sfx[i].channel);

    uint8_t b[8 + SDK_MUS_STEPS * 7];
    for (int i = 0; i < m->instruments; i++) {      /* channel, wave, vibrato, arpeggio, len, loop, release */
        const sdk_mus_ins_t *s = &m->ins[i];
        const uint8_t head[7] = { s->channel, s->wave, s->vibrato, s->arp, s->len, s->loop, s->release };
        sdk_list_add(&t, "__snd_i%d:                      ; %s\n", i, s->name);
        bytes(&t, head, 7);
        bytes(&t, s->volume, s->len);
    }
    for (int i = 0; i < patterns; i++) {            /* steps, then 7 bytes a step */
        const sdk_mus_pattern_t *p = &m->pat[i];
        if (!p->used)
            continue;
        sdk_list_add(&t, "__snd_p%d:\n    .byte %d\n", i, p->steps);
        for (int s = 0; s < p->steps; s++) {
            const sdk_mus_cell_t *c = p->cell[s];
            const uint8_t row[7] = { c[0].note, c[0].ins, c[1].note, c[1].ins, c[2].note, c[2].ins,
                                     (uint8_t)(c[3].note == SDK_MUS_OFF ? 1 : c[3].ins ? c[3].ins + 1 : 0) };
            bytes(&t, row, 7);
        }
    }
    for (int i = 0; i < m->songs; i++) {            /* speed, loop, length, patterns */
        const sdk_mus_song_t *s = &m->song[i];
        const uint8_t head[3] = { s->speed, s->loop, s->len };
        sdk_list_add(&t, "__snd_s%d:                      ; song_%s\n", i, s->name);
        bytes(&t, head, 3);
        bytes(&t, s->order, s->len);
    }
    for (int i = 0; i < m->sfxs; i++) {             /* speed, channel, wave, steps, then 3 bytes a step */
        const sdk_mus_sfx_t *s = &m->sfx[i];
        const uint8_t head[4] = { s->speed, s->channel, s->wave, s->steps };
        sdk_list_add(&t, "__snd_x%d:                      ; sfx_%s\n", i, s->name);
        bytes(&t, head, 4);
        for (int k = 0; k < s->steps; k++) {
            b[3 * k] = s->step[k].note;
            b[3 * k + 1] = s->step[k].volume;
            b[3 * k + 2] = s->step[k].fx;
        }
        bytes(&t, b, 3 * s->steps);
    }
    if (t.full) {
        free(t.s);
        return NULL;
    }
    return t.s;
}

static int lines_in(const char *s, size_t n)
{
    int lines = 0;
    for (size_t i = 0; i < n; i++)
        lines += s[i] == '\n';
    return lines;
}

static int expand(const char *src, size_t len, int snes, sdk_music_src_t *s, char *err, size_t en)
{
    memset(s, 0, sizeof *s);
    sdk_music_t *m = malloc(sizeof *m);
    if (!m) {
        snprintf(err, en, "main.s:1: not enough memory for the music");
        return -1;
    }
    char why[128];
    int rc = -1;
    if (parse(src, len, 0, snes, m, why, sizeof why) != 0) {
        int line = 1, used = 0;
        if (sscanf(why, "line %d: %n", &line, &used) != 1)
            used = 0;
        snprintf(err, en, "main.s:%d: music: %s", line, why + used);
        goto done;
    }
    if (sdk_music_empty(m) && !sdk_music_called(src, len, 0)) {
        rc = 0;                                 /* no sound: the source as it is */
        goto done;
    }
    size_t from, to;
    if (sdk_music_find(src, len, 0, &from, &to) <= 0)
        from = to = len;
    char *driver = snes ? sdk_music_snes_asm(m, 0x0F) : sdk_music_asm(m);
    const size_t n = driver ? strlen(driver) : 0;
    s->text = driver ? malloc(len + n + 2) : NULL;
    if (!s->text) {
        free(driver);
        snprintf(err, en, snes ? "main.s:1: the music does not fit in the ROM bank (or no memory)"
                               : "main.s:1: not enough memory for the music");
        goto done;
    }
    const size_t gap = from == len && len && src[len - 1] != '\n';
    memcpy(s->text, src, from);
    if (gap)
        s->text[from] = '\n';
    memcpy(s->text + from + gap, driver, n);
    memcpy(s->text + from + gap + n, src + to, len - to);
    s->len = from + gap + n + (len - to);
    s->text[s->len] = 0;
    s->first = lines_in(s->text, from + gap) + 1;
    s->lines = lines_in(driver, n);
    s->block = lines_in(src + from, to - from);
    free(driver);
    rc = 0;
done:
    free(m);
    return rc;
}

int sdk_music_expand(const char *src, size_t len, sdk_music_src_t *s, char *err, size_t en)
{
    return expand(src, len, 0, s, err, en);
}

int sdk_music_expand_snes(const char *src, size_t len, sdk_music_src_t *s, char *err, size_t en)
{
    return expand(src, len, 1, s, err, en);
}

/* The assembler's message for the source with the driver in it: the line of the source. */
void sdk_music_error_line(const sdk_music_src_t *s, char *err, size_t en)
{
    const char *colon = strchr(err, ':');
    const int at = colon ? atoi(colon + 1) : 0;
    const char *why = colon ? strchr(colon + 1, ':') : NULL;
    if (at < s->first || !why)
        return;
    char msg[160];
    if (at < s->first + s->lines)
        snprintf(msg, sizeof msg, "main.s:%d: in the sound driver:%s", s->first, why + 1);
    else
        snprintf(msg, sizeof msg, "main.s:%d:%s", at - s->lines + s->block, why + 1);
    snprintf(err, en, "%s", msg);
}

/* ---- editing ---- */

int sdk_music_next_ins(const sdk_music_t *m, int from, int step, int channel)
{
    const int n = m->instruments;
    for (int k = 1; k <= n; k++) {
        const int i = from < 0 ? (step < 0 ? n - k : k - 1) : ((from + step * k) % n + n) % n;
        if (m->ins[i].channel == channel)
            return i;
    }
    return -1;
}

void sdk_music_remove_ins(sdk_music_t *m, int i)
{
    if (i < 0 || i >= m->instruments)
        return;
    memmove(&m->ins[i], &m->ins[i + 1], (size_t)(m->instruments - i - 1) * sizeof m->ins[0]);
    m->instruments--;
    for (int p = 0; p < SDK_MUS_PATTERNS; p++)
        for (int s = 0; s < SDK_MUS_STEPS; s++)
            for (int t = 0; t < SDK_MUS_VOICES; t++) {
                uint8_t *ins = &m->pat[p].cell[s][t].ins;
                if (*ins == i + 1)
                    *ins = 0;
                else if (*ins > i + 1)
                    (*ins)--;
            }
}

int sdk_music_add_ins(sdk_music_t *m, int channel)
{
    static const struct {
        const char *name;
        uint8_t wave, len, loop, release, volume[12];
    } usual[3] = {
        { "lead", 2, 11, 5, 6, { 15, 13, 12, 11, 10, 10, 8, 6, 4, 2, 0 } },
        { "bass", 0, 1, 0, SDK_MUS_NO, { 15 } },
        { "hat", 2, 6, SDK_MUS_NO, SDK_MUS_NO, { 12, 8, 5, 3, 1, 0 } } };
    if (m->instruments == SDK_MUS_INS || channel < 0 || channel > 2)
        return -1;
    static const struct {
        const char *name;
        uint8_t wave, noise, attack, decay, sustain, rate, level;
    } snes[3] = { { "lead", 2, 0, 15, 7, 7, 0, 100 }, { "bass", 3, 0, 15, 7, 7, 0, 110 },
                  { "drum", SDK_MUS_WAVES, 24, 15, 7, 2, 26, 90 } };
    sdk_mus_ins_t *s = &m->ins[m->instruments];
    memset(s, 0, sizeof *s);
    const char *base = m->snes ? snes[channel].name : usual[channel].name;
    for (int n = 1;; n++) {
        if (n == 1)
            snprintf(s->name, sizeof s->name, "%s", base);
        else
            snprintf(s->name, sizeof s->name, "%s%d", base, n);
        if (find_ins(m, s->name) < 0)
            break;
    }
    if (m->snes) {                              /* every voice takes every instrument: no channel */
        s->wave = snes[channel].wave;
        s->noise = snes[channel].noise;
        s->attack = snes[channel].attack;
        s->decay = snes[channel].decay;
        s->sustain = snes[channel].sustain;
        s->rate = snes[channel].rate;
        s->level = snes[channel].level;
        s->pan = 8;
        s->loop = s->release = SDK_MUS_NO;
        return m->instruments++;
    }
    s->channel = (uint8_t)channel;
    s->wave = usual[channel].wave;
    s->len = usual[channel].len;
    s->loop = usual[channel].loop;
    s->release = usual[channel].release;
    memcpy(s->volume, usual[channel].volume, usual[channel].len);
    return m->instruments++;
}
