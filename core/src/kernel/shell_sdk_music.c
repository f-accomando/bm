/*
 * Shell SDK: the music block (see shell_sdk_music.h). Original code.
 */
#include "shell_sdk_music.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_edit.h"

static const char *const channel_words[3] = { "PULSE", "TRIANGLE", "NOISE" };
static const char *const duty_words[4] = { "DUTY12", "DUTY25", "DUTY50", "DUTY75" };
static const char *const track_words[4] = { "PU1", "PU2", "TRI", "NOI" };
static const char *const fx_words[3] = { "UP", "DOWN", "VIB" };

/* ---- small things ---- */

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int same(const char *a, const char *b)          /* with no regard to case */
{
    while (*a && up((unsigned char)*a) == up((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

static int word_char(int c)
{
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

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
    return !m->instruments && !m->sfxs && !m->songs;
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
    const int l = up((unsigned char)w[0]);
    if (l < 'A' || l > 'G')
        return -1;
    int semi = base[l - 'A'], i = 1;
    if (up((unsigned char)w[1]) == 'S') {
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
    if (up((unsigned char)w[0]) != 'N' || w[1] < '0' || w[1] > '9')
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
        if (!word_char((unsigned char)name[i]))
            return 0;
    for (size_t i = 0; i < sizeof taken / sizeof taken[0]; i++)
        if (same(name, taken[i]))
            return 0;
    if (note_of(name) >= 0 || noise_of(name) >= 0)
        return 0;
    char head[8];
    snprintf(head, sizeof head, "%.7s", name);
    if (same(head, "pattern"))
        return 0;
    snprintf(head, sizeof head, "%.4s", name);
    if (same(head, "sfx_"))
        return 0;
    snprintf(head, sizeof head, "%.5s", name);
    return !same(head, "song_");
}

/* ---- finding the block ---- */

static size_t next_line(const char *s, size_t len, size_t p)
{
    while (p < len && s[p] != '\n')
        p++;
    return p < len ? p + 1 : len;
}

/* The first line from p that starts (after spaces) with the marker, followed by no letter or '-'. len: none. */
static size_t find_marker(const char *s, size_t len, size_t p, const char *marker)
{
    const size_t n = strlen(marker);
    for (; p < len; p = next_line(s, len, p)) {
        size_t q = p;
        while (q < len && (s[q] == ' ' || s[q] == '\t'))
            q++;
        if (len - q >= n && !memcmp(s + q, marker, n)) {
            const char c = q + n < len ? s[q + n] : ' ';
            if (c != '-' && !(c >= 'a' && c <= 'z'))
                return p;
        }
    }
    return len;
}

int sdk_music_find(const char *src, size_t len, int c, size_t *from, size_t *to)
{
    *from = find_marker(src, len, 0, c ? "//@music" : ";@music");
    *to = *from;
    if (*from == len)
        return 0;
    const size_t end = find_marker(src, len, next_line(src, len, *from), c ? "//@end" : ";@end");
    if (end == len)
        return -1;
    *to = next_line(src, len, end);
    return 1;
}

static int line_number(const char *s, size_t p)
{
    int n = 1;
    for (size_t i = 0; i < p; i++)
        n += s[i] == '\n';
    return n;
}

/* ---- reading: the lists of values ---- */

typedef struct { char word[SDK_MUS_NAME]; int num; int line; } value_t;
typedef struct { char name[24]; int line, first, count; } item_t;

typedef struct {
    const char *p, *end;
    int line, c;
    value_t *value;
    int values, values_max;
    item_t item[SDK_MUS_INS + SDK_MUS_PATTERNS + SDK_MUS_SFX + SDK_MUS_SONGS + 1];
    int items;
    char *err;
    size_t en;
} reader_t;

static int wrong(reader_t *r, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static int wrong(reader_t *r, int line, const char *fmt, ...)
{
    char msg[128];
    va_list a;
    va_start(a, fmt);
    vsnprintf(msg, sizeof msg, fmt, a);
    va_end(a);
    snprintf(r->err, r->en, "line %d: %s", line, msg);
    return -1;
}

/* Spaces, comments and (bm C) ends of line. In assembly the end of a line matters: it stays. */
static void blank(reader_t *r)
{
    for (;;) {
        if (r->p >= r->end)
            return;
        const char c = *r->p;
        if (c == ' ' || c == '\t' || c == '\r' || (c == '\n' && r->c)) {
            r->line += c == '\n';
            r->p++;
        } else if ((!r->c && c == ';') || (r->c && c == '/' && r->p + 1 < r->end && r->p[1] == '/')) {
            while (r->p < r->end && *r->p != '\n')
                r->p++;
        } else if (r->c && c == '/' && r->p + 1 < r->end && r->p[1] == '*') {
            r->p += 2;
            while (r->p < r->end && !(*r->p == '*' && r->p + 1 < r->end && r->p[1] == '/'))
                r->line += *r->p++ == '\n';
            r->p = r->p + 2 < r->end ? r->p + 2 : r->end;
        } else
            return;
    }
}

static int mark(reader_t *r, char c)
{
    blank(r);
    if (r->p < r->end && *r->p == c) {
        r->p++;
        return 1;
    }
    return 0;
}

static int read_word(reader_t *r, char *out, size_t max)
{
    blank(r);
    size_t n = 0;
    if (r->p >= r->end || !word_char((unsigned char)*r->p) || (*r->p >= '0' && *r->p <= '9'))
        return 0;
    while (r->p < r->end && word_char((unsigned char)*r->p)) {
        if (n + 1 < max)
            out[n++] = *r->p;
        r->p++;
    }
    out[n] = 0;
    return 1;
}

static int read_number(reader_t *r, int *out)
{
    blank(r);
    const char *s = r->p;
    int base = 10;
    if (s < r->end && *s == '$') {
        base = 16;
        s++;
    } else if (s < r->end && *s == '%') {
        base = 2;
        s++;
    } else if (r->end - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    long v = 0;
    int digits = 0;
    for (; s < r->end; s++, digits++) {
        const int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                      *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base)
            break;
        v = v * base + d;
        if (v > 0xFFFF)
            return 0;
    }
    if (!digits)
        return 0;
    r->p = s;
    *out = (int)v;
    return 1;
}

/* One value: words and numbers joined by +, one word at most. Into the last item. */
static int read_value(reader_t *r)
{
    value_t v = { "", 0, 0 };
    blank(r);
    v.line = r->line;
    do {
        char w[24];
        int n;
        if (read_word(r, w, sizeof w)) {
            if (v.word[0])
                return wrong(r, v.line, "'%s+%s': one name in a value", v.word, w);
            if (strlen(w) >= SDK_MUS_NAME)
                return wrong(r, v.line, "'%s' is too long: %d letters at most", w, SDK_MUS_NAME - 1);
            copy_name(v.word, w);
        } else if (read_number(r, &n))
            v.num += n;
        else
            return wrong(r, v.line, "a value is expected here, a number or a name");
    } while (mark(r, '+'));
    if (r->values == r->values_max) {
        const int max = r->values_max ? r->values_max * 2 : 512;
        value_t *p = max > 20000 ? NULL : realloc(r->value, (size_t)max * sizeof *p);
        if (!p)
            return wrong(r, v.line, "too much music");
        r->value = p;
        r->values_max = max;
    }
    r->value[r->values++] = v;
    r->item[r->items - 1].count++;
    return 0;
}

static int new_item(reader_t *r, const char *name)
{
    if (r->items == (int)(sizeof r->item / sizeof r->item[0]))
        return wrong(r, r->line, "too many lists");
    for (int i = 0; i < r->items; i++)
        if (same(r->item[i].name, name))
            return wrong(r, r->line, "'%s' is already there, at line %d", name, r->item[i].line);
    item_t *it = &r->item[r->items++];
    snprintf(it->name, sizeof it->name, "%s", name);
    it->line = r->line;
    it->first = r->values;
    it->count = 0;
    return 0;
}

/* label:  then .byte lines */
static int read_asm(reader_t *r)
{
    while (r->p < r->end) {
        char w[24];
        blank(r);
        if (r->p < r->end && *r->p == '\n') {
            r->p++;
            r->line++;
            continue;
        }
        if (r->p >= r->end)
            break;
        if (read_word(r, w, sizeof w)) {
            if (!mark(r, ':'))
                return wrong(r, r->line, "the music block has labels and .byte lines only: '%s' needs a ':'", w);
            if (new_item(r, w) != 0)
                return -1;
            blank(r);
            if (r->p >= r->end || *r->p == '\n')
                continue;
        }
        if (!mark(r, '.') || !read_word(r, w, sizeof w) || (!same(w, "byte") && !same(w, "db")))
            return wrong(r, r->line, "the music block has labels and .byte lines only");
        if (!r->items)
            return wrong(r, r->line, "these values need a label before them");
        do {
            if (read_value(r) != 0)
                return -1;
        } while (mark(r, ','));
        blank(r);
        if (r->p < r->end && *r->p != '\n')
            return wrong(r, r->line, "a ',' or the end of the line is expected");
    }
    return 0;
}

/* const byte name[] = { values }; */
static int read_c(reader_t *r)
{
    for (;;) {
        char w[24];
        blank(r);
        if (r->p >= r->end)
            return 0;
        if (!read_word(r, w, sizeof w) || strcmp(w, "const"))
            return wrong(r, r->line, "the music block has const byte name[] = { } only");
        if (read_word(r, w, sizeof w) && !strcmp(w, "unsigned"))
            read_word(r, w, sizeof w);
        if (strcmp(w, "byte") && strcmp(w, "char"))
            return wrong(r, r->line, "the music is made of bytes: const byte name[]");
        if (!read_word(r, w, sizeof w))
            return wrong(r, r->line, "a name is expected after const byte");
        if (new_item(r, w) != 0)
            return -1;
        if (!mark(r, '['))
            return wrong(r, r->line, "'%s' is a list: %s[]", w, w);
        int size;
        read_number(r, &size);
        if (!mark(r, ']') || !mark(r, '=') || !mark(r, '{'))
            return wrong(r, r->line, "expected %s[] = {", w);
        while (!mark(r, '}')) {
            if (read_value(r) != 0)
                return -1;
            if (!mark(r, ',')) {
                if (!mark(r, '}'))
                    return wrong(r, r->line, "a ',' or '}' is expected");
                break;
            }
        }
        mark(r, ';');
    }
}

/* ---- reading: what the lists say ---- */

static const char *shown(const value_t *v, char *buf, size_t n)
{
    if (v->word[0])
        snprintf(buf, n, "%s", v->word);
    else
        snprintf(buf, n, "%d", v->num);
    return buf;
}

static int number_in(reader_t *r, const value_t *v, int lo, int hi, const char *what, int *out)
{
    char b[24];
    if (v->word[0] || v->num < lo || v->num > hi)
        return wrong(r, v->line, "%s is %d to %d, not %s", what, lo, hi, shown(v, b, sizeof b));
    *out = v->num;
    return 0;
}

static int is(const value_t *v, const char *w) { return v->word[0] && same(v->word, w) && !v->num; }

static int one_of(const value_t *v, const char *const *list, int n)
{
    for (int i = 0; i < n; i++)
        if (is(v, list[i]))
            return i;
    return -1;
}

static int read_instrument(reader_t *r, const item_t *it, sdk_mus_ins_t *s)
{
    const value_t *v = r->value + it->first;
    const int n = it->count - 1;                        /* without END */
    char b[24];
    if (n < 4)
        return wrong(r, it->line, "instrument %s needs channel, wave, vibrato, arpeggio", it->name);
    const int ch = one_of(&v[0], channel_words, 3);
    if (ch < 0)
        return wrong(r, v[0].line, "the channel of an instrument is PULSE, TRIANGLE or NOISE, not %s",
                     shown(&v[0], b, sizeof b));
    s->channel = (uint8_t)ch;
    int x = 0;
    if (ch == SDK_MUS_PULSE) {
        x = one_of(&v[1], duty_words, 4);
        if (x < 0 && number_in(r, &v[1], 0, 3, "the wave of a pulse (DUTY12 to DUTY75)", &x) != 0)
            return -1;
        s->wave = (uint8_t)x;
    } else {
        int shortn = v[1].word[0] && same(v[1].word, "SHORT");
        if ((v[1].word[0] && !shortn) || v[1].num > 15)
            return wrong(r, v[1].line, "the wave of the noise is 0 to 15, with +SHORT for the short noise");
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
                return wrong(r, v[i].line, "one LOOP, before RELEASE");
            s->loop = s->len;
        } else if (is(&v[i], "RELEASE")) {
            if (s->release != SDK_MUS_NO)
                return wrong(r, v[i].line, "one RELEASE in an instrument");
            s->release = s->len;
        } else {
            if (number_in(r, &v[i], 0, 15, "a volume", &x) != 0)
                return -1;
            if (s->len == SDK_MUS_ENV)
                return wrong(r, v[i].line, "an instrument has %d volumes at most", SDK_MUS_ENV);
            s->volume[s->len++] = (uint8_t)x;
        }
    }
    if ((s->loop != SDK_MUS_NO && s->loop >= s->len) || (s->release != SDK_MUS_NO && s->release >= s->len) ||
        (s->loop != SDK_MUS_NO && s->loop == s->release))
        return wrong(r, it->line, "LOOP and RELEASE of %s need a volume after them", it->name);
    return 0;
}

static int find_ins(const sdk_music_t *m, const char *word)
{
    for (int i = 0; i < m->instruments; i++)
        if (same(m->ins[i].name, word))
            return i;
    return -1;
}

static int ins_value(reader_t *r, const sdk_music_t *m, const value_t *v, int channel, int track, uint8_t *out)
{
    char b[24];
    if (is(v, "__") || (!v->word[0] && !v->num)) {
        *out = 0;
        return 0;
    }
    const int i = v->word[0] && !v->num ? find_ins(m, v->word) : -1;
    if (i < 0)
        return wrong(r, v->line, "there is no instrument '%s'", shown(v, b, sizeof b));
    if (m->ins[i].channel != channel)
        return wrong(r, v->line, "%s is a %s instrument: track %d wants %s", m->ins[i].name,
                     channel_words[m->ins[i].channel], track + 1, channel_words[channel]);
    *out = (uint8_t)(i + 1);
    return 0;
}

static int note_value(reader_t *r, const value_t *v, int noise, uint8_t *out)
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
            return wrong(r, v->line, noise ? "'%s' is not a noise pitch (N0 to N15, __ or OFF)"
                                           : "'%s' is not a note (C1 to B7, CS4, __ or OFF)", shown(v, b, sizeof b));
        *out = (uint8_t)n;
    }
    return 0;
}

static int read_pattern(reader_t *r, const sdk_music_t *m, const item_t *it, sdk_mus_pattern_t *p)
{
    const value_t *v = r->value + it->first;
    const int n = it->count - 1;
    if (!n || n % 7 || n / 7 > SDK_MUS_STEPS)
        return wrong(r, it->line, "%s has %d values: a step is 7 (3 notes with instruments, 1 noise), 1 to %d "
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

static int read_sfx(reader_t *r, const item_t *it, sdk_mus_sfx_t *s)
{
    const value_t *v = r->value + it->first;
    const int n = it->count - 1;
    char b[24];
    int x;
    if (n < 3 || (n - 3) % 3 || (n - 3) / 3 > SDK_MUS_SFX_STEPS)
        return wrong(r, it->line, "%s needs speed, channel, wave, then note, volume, effect a step (%d at most)",
                     it->name, SDK_MUS_SFX_STEPS);
    if (number_in(r, &v[0], 1, 60, "the speed (frames a step)", &x) != 0)
        return -1;
    s->speed = (uint8_t)x;
    const int ch = one_of(&v[1], track_words, 4);
    if (ch < 0)
        return wrong(r, v[1].line, "the channel of an effect is PU1, PU2, TRI or NOI, not %s",
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
    for (int i = 0; i < s->steps; i++, v += 3) {
        if (note_value(r, &v[0], ch == 3, &s->step[i].note) != 0 ||
            number_in(r, &v[1], 0, 15, "a volume", &x) != 0)
            return -1;
        s->step[i].volume = (uint8_t)x;
        int fx = 0;
        for (int f = 0; f < 3; f++)
            if (v[2].word[0] && same(v[2].word, fx_words[f]))
                fx = f + 1;
        if (fx && v[2].num >= 1 && v[2].num <= 15)
            s->step[i].fx = (uint8_t)(fx << 4 | v[2].num);
        else if (is(&v[2], "__") || (!v[2].word[0] && !v[2].num))
            s->step[i].fx = 0;
        else
            return wrong(r, v[2].line, "an effect is __, UP+n, DOWN+n or VIB+n with n 1 to 15");
    }
    return 0;
}

static int read_song(reader_t *r, const item_t *it, sdk_mus_song_t *s)
{
    const value_t *v = r->value + it->first;
    const int n = it->count - 1;
    int x;
    if (n < 3 || n - 2 > SDK_MUS_ORDER)
        return wrong(r, it->line, "%s needs speed, loop place, then 1 to %d pattern numbers", it->name,
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

/* The kind of a list from its name: 0 instrument, 1 pattern (its number in *num), 2 sfx_, 3 song_, -1 wrong. */
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
    return sdk_music_name_ok(name) ? 0 : -1;
}

static int interpret(reader_t *r, sdk_music_t *m)
{
    for (int pass = 0; pass < 2; pass++)            /* the instruments first: the patterns name them */
        for (int i = 0; i < r->items; i++) {
            const item_t *it = &r->item[i];
            int num = 0;
            const int kind = kind_of(it->name, &num);
            if (kind < 0)
                return wrong(r, it->line, "'%s' cannot be a name here", it->name);
            if (pass != (kind != 0))
                continue;
            if (!it->count || !is(&r->value[it->first + it->count - 1], "END"))
                return wrong(r, it->line, "'%s' ends with END", it->name);
            if (kind == 0) {
                if (m->instruments == SDK_MUS_INS)
                    return wrong(r, it->line, "%d instruments at most", SDK_MUS_INS);
                sdk_mus_ins_t *s = &m->ins[m->instruments];
                copy_name(s->name, it->name);
                s->line = it->line;
                if (read_instrument(r, it, s) != 0)
                    return -1;
                m->instruments++;
            } else if (kind == 1) {
                if (m->pat[num].used)
                    return wrong(r, it->line, "pattern %d is there twice", num);
                if (read_pattern(r, m, it, &m->pat[num]) != 0)
                    return -1;
            } else if (kind == 2) {
                if (m->sfxs == SDK_MUS_SFX)
                    return wrong(r, it->line, "%d effects at most", SDK_MUS_SFX);
                sdk_mus_sfx_t *s = &m->sfx[m->sfxs];
                copy_name(s->name, it->name + 4);
                s->line = it->line;
                if (read_sfx(r, it, s) != 0)
                    return -1;
                m->sfxs++;
            } else {
                if (m->songs == SDK_MUS_SONGS)
                    return wrong(r, it->line, "%d songs at most", SDK_MUS_SONGS);
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

int sdk_music_parse(const char *src, size_t len, int c, sdk_music_t *m, char *err, size_t en)
{
    sdk_music_clear(m);
    size_t from, to;
    const int found = sdk_music_find(src, len, c, &from, &to);
    if (found < 0) {
        snprintf(err, en, "line %d: %s@music has no %s@end", line_number(src, from), c ? "//" : ";", c ? "//" : ";");
        return -1;
    }
    if (!found)
        return 0;
    reader_t *r = calloc(1, sizeof *r);
    if (!r) {
        snprintf(err, en, "line 1: not enough memory for the music");
        return -1;
    }
    r->p = src + next_line(src, len, from);
    size_t end = to;                            /* back to the start of the line of @end */
    while (end > from && src[end - 1] == '\n')
        end--;
    while (end > from && src[end - 1] != '\n')
        end--;
    r->end = src + end;
    if (r->p > r->end)
        r->p = r->end;
    r->line = line_number(src, (size_t)(r->p - src));
    r->c = c;
    r->err = err;
    r->en = en;
    int rc = c ? read_c(r) : read_asm(r);
    if (rc == 0)
        rc = interpret(r, m);
    if (rc != 0)
        sdk_music_clear(m);
    free(r->value);
    free(r);
    return rc;
}

/* ---- writing ---- */

typedef struct { char *s; size_t max, len; int full, grow; } text_t;

static void add(text_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void add(text_t *t, const char *fmt, ...)
{
    char b[256];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (n >= (int)sizeof b)
        n = sizeof b - 1;
    if (t->len + (size_t)n + 1 > t->max) {
        if (!t->grow) {
            t->full = 1;
            return;
        }
        const size_t max = (t->max ? t->max * 2 : 8192) + (size_t)n;
        char *s = realloc(t->s, max);
        if (!s) {
            t->full = 1;
            return;
        }
        t->s = s;
        t->max = max;
    }
    memcpy(t->s + t->len, b, (size_t)n + 1);
    t->len += (size_t)n;
}

static void add_str(text_t *t, const char *s)           /* of any length */
{
    for (size_t n = strlen(s), at = 0; at < n; at += 200)
        add(t, "%.200s", s + at);
}

/* A list being written: its lines of values, with a comment each. */
typedef struct {
    text_t *t;
    int c;
    char line[160];             /* the values waiting */
    char note[64];
    int has;
} list_t;

static void list_flush(list_t *l, int last)
{
    if (!l->has)
        return;
    char v[200];
    snprintf(v, sizeof v, l->c ? "    %s%s" : "    .byte %s", l->line, !l->c ? "" : last ? " };" : ",");
    if (l->note[0])
        add(l->t, "%-44s %s %s\n", v, l->c ? "//" : ";", l->note);
    else
        add(l->t, "%s\n", v);
    l->has = 0;
}

/* One line of values; the one before goes out. */
static void list_line(list_t *l, const char *values, const char *note)
{
    list_flush(l, 0);
    snprintf(l->line, sizeof l->line, "%s", values);
    snprintf(l->note, sizeof l->note, "%s", note);
    l->has = 1;
}

static void list_open(list_t *l, text_t *t, int c, const char *name, const char *note)
{
    l->t = t;
    l->c = c;
    l->has = 0;
    if (c)
        add(t, note[0] ? "const byte %s[] = {          // %s\n" : "const byte %s[] = {%s\n", name, note);
    else
        add(t, note[0] ? "%s:                          ; %s\n" : "%s:%s\n", name, note);
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
        out[i] = (char)up((unsigned char)s[i]);
    out[i] = 0;
}

static size_t put(char *line, size_t at, size_t max, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static size_t put(char *line, size_t at, size_t max, const char *fmt, ...)
{
    if (at >= max - 1)
        return at;
    va_list a;
    va_start(a, fmt);
    const int n = vsnprintf(line + at, max - at, fmt, a);
    va_end(a);
    return at + (size_t)n < max ? at + (size_t)n : max - 1;
}

static void write_instrument(text_t *t, int c, const sdk_mus_ins_t *s)
{
    list_t l;
    char line[160], wave[16];
    list_open(&l, t, c, s->name, "instrument");
    wave_word(s->channel, s->wave, wave, sizeof wave);
    snprintf(line, sizeof line, "%s, %s, %d, $%02X", channel_words[s->channel], s->channel == SDK_MUS_TRIANGLE ?
             "0" : wave, s->vibrato, s->arp);
    list_line(&l, line, "channel,wave,vibrato,arpeggio");
    size_t at = 0;
    int on = 0, first = 1;
    for (int i = 0; i < s->len; i++) {
        if (i == s->loop)
            at = put(line, at, sizeof line, "%sLOOP", at ? ", " : "");
        if (i == s->release)
            at = put(line, at, sizeof line, "%sRELEASE", at ? ", " : "");
        at = put(line, at, sizeof line, "%s%d", at ? ", " : "", s->volume[i]);
        if (++on == 10) {
            list_line(&l, line, first ? "volume" : "");
            at = 0;
            on = 0;
            first = 0;
        }
    }
    put(line, at, sizeof line, "%sEND", at ? ", " : "");
    list_line(&l, line, first ? "volume" : "");
    list_flush(&l, 1);
}

static void write_pattern(text_t *t, int c, const sdk_music_t *m, int num)
{
    const sdk_mus_pattern_t *p = &m->pat[num];
    list_t l;
    char name[16], line[160];
    snprintf(name, sizeof name, "pattern%02d", num);
    list_open(&l, t, c, name, "a step: pulse 1, 2, triangle, noise");
    for (int s = 0; s < p->steps; s++) {
        size_t at = 0;
        for (int k = 0; k < 3; k++) {
            char note[4], ins[SDK_MUS_NAME], cell[32];
            sdk_music_note_name(p->cell[s][k].note, note);
            ins_word(m, p->cell[s][k].ins, "0", ins, sizeof ins);
            snprintf(cell, sizeof cell, "%s,%s,", note, ins);
            at = put(line, at, sizeof line, "%-11s ", cell);
        }
        char noise[SDK_MUS_NAME], num2[8];
        if (p->cell[s][3].note == SDK_MUS_OFF)
            strcpy(noise, "OFF");
        else
            ins_word(m, p->cell[s][3].ins, "__", noise, sizeof noise);
        put(line, at, sizeof line, "%-5s", noise);
        snprintf(num2, sizeof num2, "%02d", s);
        list_line(&l, line, num2);
    }
    list_line(&l, "END", "");
    list_flush(&l, 1);
}

static void write_sfx(text_t *t, int c, const sdk_mus_sfx_t *s)
{
    list_t l;
    char name[20], line[160], wave[16];
    snprintf(name, sizeof name, "sfx_%s", s->name);
    list_open(&l, t, c, name, "sound effect");
    if (s->channel < 2)
        snprintf(wave, sizeof wave, "%s", duty_words[s->wave & 3]);
    else
        snprintf(wave, sizeof wave, "%s", s->wave & SDK_MUS_SHORT ? "SHORT" : "0");
    snprintf(line, sizeof line, "%d, %s, %s", s->speed, track_words[s->channel & 3], wave);
    list_line(&l, line, "frames a step, channel, wave");
    size_t at = 0;
    for (int i = 0; i < s->steps; i++) {
        const sdk_mus_step_t *e = &s->step[i];
        char note[4], fx[12];
        if (s->channel == 3 && e->note >= SDK_MUS_LOW)
            snprintf(note, sizeof note, "N%d", (e->note - SDK_MUS_LOW) & 15);
        else
            sdk_music_note_name(e->note, note);
        if (e->fx >> 4 >= 1 && e->fx >> 4 <= 3)
            snprintf(fx, sizeof fx, "%s+%d", fx_words[(e->fx >> 4) - 1], e->fx & 15);
        else
            strcpy(fx, "__");
        at = put(line, at, sizeof line, "%s%s,%d,%s", at ? ",  " : "", note, e->volume, fx);
        if (i % 4 == 3 || i == s->steps - 1) {
            list_line(&l, line, "");
            at = 0;
        }
    }
    list_line(&l, "END", "");
    list_flush(&l, 1);
}

static void write_song(text_t *t, int c, const sdk_mus_song_t *s)
{
    list_t l;
    char name[20], line[160];
    snprintf(name, sizeof name, "song_%s", s->name);
    list_open(&l, t, c, name, "song");
    if (s->loop == SDK_MUS_NO)
        snprintf(line, sizeof line, "%d, END", s->speed);
    else
        snprintf(line, sizeof line, "%d, %d", s->speed, s->loop);
    list_line(&l, line, "frames a step, place to loop to");
    size_t at = 0;
    for (int i = 0; i < s->len; i++) {
        at = put(line, at, sizeof line, "%s%d", at ? ", " : "", s->order[i]);
        if (i % 16 == 15 || i == s->len - 1) {
            list_line(&l, line, i < 16 ? "patterns" : "");
            at = 0;
        }
    }
    list_line(&l, "END", "");
    list_flush(&l, 1);
}

static void write_block(text_t *t, const sdk_music_t *m, int c)
{
    add(t, "%s@music  from the Sounds tab: C1..B7 (CS4 sharp), __ nothing, OFF release\n", c ? "//" : ";");
    for (int i = 0; i < m->instruments; i++)
        write_instrument(t, c, &m->ins[i]);
    for (int i = 0; i < SDK_MUS_PATTERNS; i++)
        if (m->pat[i].used)
            write_pattern(t, c, m, i);
    for (int i = 0; i < m->sfxs; i++)
        write_sfx(t, c, &m->sfx[i]);
    for (int i = 0; i < m->songs; i++)
        write_song(t, c, &m->song[i]);
    add(t, "%s@end\n", c ? "//" : ";");
}

long sdk_music_text(const sdk_music_t *m, int c, char *out, size_t max)
{
    text_t t = { out, max, 0, 0, 0 };
    if (max)
        out[0] = 0;
    write_block(&t, m, c);
    return t.full ? -1 : (long)t.len;
}

int sdk_music_commit(const sdk_music_t *m, int c, int run, char *err, size_t en)
{
    const char *src = sdk_edit_text();
    const size_t len = sdk_edit_length();
    size_t from, to;
    const int found = sdk_music_find(src, len, c, &from, &to);
    if (found < 0) {
        snprintf(err, en, "line %d: %s@music has no %s@end", line_number(src, from), c ? "//" : ";", c ? "//" : ";");
        return -1;
    }
    text_t t = { NULL, 0, 0, 0, 1 };
    if (!found) {                                       /* before the map, or at the end */
        from = to = find_marker(src, len, 0, c ? "//@map" : ";@map");
        if (from == len && len && src[len - 1] != '\n')
            add(&t, "\n");
    }
    write_block(&t, m, c);
    int rc = 0;
    if (!found && to < len)
        add(&t, "\n");
    if (t.full) {
        snprintf(err, en, "not enough memory");
        rc = -1;
    } else if (sdk_edit_replace(from, to, t.s, t.len, run) != 0) {
        snprintf(err, en, "the code is full: the music is not written");
        rc = -1;
    }
    free(t.s);
    return rc;
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
        } else if (word_char((unsigned char)src[i])) {
            size_t e = i;
            while (e < len && word_char((unsigned char)src[e]))
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
static void table(text_t *t, const char *name, char kind, int n, const sdk_music_t *m)
{
    for (int high = 0; high < 2; high++) {
        add(t, "%s_%s:\n", name, high ? "hi" : "lo");
        if (!n)
            add(t, "    .byte 0\n");
        for (int i = 0; i < n; i++)
            if (kind == 'p' && !m->pat[i].used)
                add(t, "    .byte 0\n");
            else
                add(t, "    .byte %c__snd_%c%d\n", high ? '>' : '<', kind, i);
    }
}

static void bytes(text_t *t, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i += 16) {
        char line[96];
        size_t at = 0;
        for (int k = i; k < n && k < i + 16; k++)
            at = put(line, at, sizeof line, "%s%d", k > i ? "," : "", b[k]);
        add(t, "    .byte %s\n", line);
    }
}

char *sdk_music_asm(const sdk_music_t *m)
{
    text_t t = { NULL, 0, 0, 0, 1 };
    add(&t, "; ==== the sound: the driver and the data of the Sounds tab =====================\n");
    add(&t, "__snd_songs = %d\n__snd_sfxs  = %d\n", m->songs, m->sfxs);
    for (int i = 0; i < m->songs; i++)
        add(&t, "song_%s = %d\n", m->song[i].name, i);
    for (int i = 0; i < m->sfxs; i++)
        add(&t, "sfx_%s = %d\n", m->sfx[i].name, i);
    add_str(&t, sdk_snd_driver());

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
    add(&t, "__snd_sfx_ch:\n");
    if (!m->sfxs)
        add(&t, "    .byte 0\n");
    for (int i = 0; i < m->sfxs; i++)
        add(&t, "    .byte %d\n", m->sfx[i].channel);

    uint8_t b[8 + SDK_MUS_STEPS * 7];
    for (int i = 0; i < m->instruments; i++) {      /* channel, wave, vibrato, arpeggio, len, loop, release */
        const sdk_mus_ins_t *s = &m->ins[i];
        const uint8_t head[7] = { s->channel, s->wave, s->vibrato, s->arp, s->len, s->loop, s->release };
        add(&t, "__snd_i%d:                      ; %s\n", i, s->name);
        bytes(&t, head, 7);
        bytes(&t, s->volume, s->len);
    }
    for (int i = 0; i < patterns; i++) {            /* steps, then 7 bytes a step */
        const sdk_mus_pattern_t *p = &m->pat[i];
        if (!p->used)
            continue;
        add(&t, "__snd_p%d:\n    .byte %d\n", i, p->steps);
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
        add(&t, "__snd_s%d:                      ; song_%s\n", i, s->name);
        bytes(&t, head, 3);
        bytes(&t, s->order, s->len);
    }
    for (int i = 0; i < m->sfxs; i++) {             /* speed, channel, wave, steps, then 3 bytes a step */
        const sdk_mus_sfx_t *s = &m->sfx[i];
        const uint8_t head[4] = { s->speed, s->channel, s->wave, s->steps };
        add(&t, "__snd_x%d:                      ; sfx_%s\n", i, s->name);
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

int sdk_music_expand(const char *src, size_t len, sdk_music_src_t *s, char *err, size_t en)
{
    memset(s, 0, sizeof *s);
    sdk_music_t *m = malloc(sizeof *m);
    if (!m) {
        snprintf(err, en, "main.s:1: not enough memory for the music");
        return -1;
    }
    char why[128];
    int rc = -1;
    if (sdk_music_parse(src, len, 0, m, why, sizeof why) != 0) {
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
    char *driver = sdk_music_asm(m);
    const size_t n = driver ? strlen(driver) : 0;
    s->text = driver ? malloc(len + n + 2) : NULL;
    if (!s->text) {
        free(driver);
        snprintf(err, en, "main.s:1: not enough memory for the music");
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
            for (int t = 0; t < SDK_MUS_TRACKS; t++) {
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
    sdk_mus_ins_t *s = &m->ins[m->instruments];
    memset(s, 0, sizeof *s);
    for (int n = 1;; n++) {
        if (n == 1)
            snprintf(s->name, sizeof s->name, "%s", usual[channel].name);
        else
            snprintf(s->name, sizeof s->name, "%s%d", usual[channel].name, n);
        if (find_ins(m, s->name) < 0)
            break;
    }
    s->channel = (uint8_t)channel;
    s->wave = usual[channel].wave;
    s->len = usual[channel].len;
    s->loop = usual[channel].loop;
    s->release = usual[channel].release;
    memcpy(s->volume, usual[channel].volume, usual[channel].len);
    return m->instruments++;
}
