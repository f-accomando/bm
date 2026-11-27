/*
 * Shell SDK: the Sounds tab (see shell_sdk_sounds.h). Original code.
 */
#include "shell_sdk_sounds.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"
#include "shell_sdk_build.h"
#include "shell_sdk_code.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_keys.h"
#include "shell_sdk_lists.h"
#include "shell_sdk_music.h"
#include "shell_sdk_pane.h"
#include "shell_sdk_play.h"
#include "usb/hid.h"

enum { PAGE_INS, PAGE_SFX, PAGE_PATTERN, PAGE_SONG, PAGE_WAVE, PAGES };     /* the last one: SNES only */
static const char *const page_names[PAGES] = { "Instruments", "SFX", "Pattern", "Song", "Waves" };
static const char *const track_names[SDK_MUS_TRACKS] = { "Pulse 1", "Pulse 2", "Triangle", "Noise" };
static const int track_channel[SDK_MUS_TRACKS] = { SDK_MUS_PULSE, SDK_MUS_PULSE, SDK_MUS_TRIANGLE, SDK_MUS_NOISE };
static const char *const track_colour[SDK_MUS_VOICES] = { "103", "43", "102", "106", "104", "105", "101", "46" };
static const char *const wave_names[SDK_MUS_WAVES + 1] = { "square 12%", "square 25%", "square 50%", "triangle",
                                                          "saw", "sine", "noise" };
static const char *const channel_names[3] = { "pulse", "triangle", "noise" };
static const char *const duty_names[4] = { "12%", "25%", "50%", "75%" };
static const char *const sfx_channels[4] = { "Pulse 1", "Pulse 2", "Triangle", "Noise" };

#define ENV_ROWS 5                      /* the volumes: two levels a row */
/* The piano in the order of the notes: q to p are the white keys, from C to the E of the octave above; the
 * numbers over them are the black ones. */
#define PIANO "q2w3er5t6y7ui9o0p"

static sdk_music_t mus, reading;
static int supported, target, is_c;     /* a NES or SNES project; its language */
static int synced;
static unsigned code_rev;
static char broken[128];                /* why the block cannot be read ("": it can) */
static int silent_program;              /* assembly with no call to the driver */

static int page, octave, preview_speed;
static int pat, track, step, on_header, field;
static int last_ins[3], last_note[3];
static int ins_sel, ins_row, env_at;
static int sfx_sel, sfx_grid, sfx_field, sfx_step;
static int song_sel, song_order, song_field, place;
static int wave_sel, wave_at, shape_next;       /* the drawn wave, its point, the shape that s gives */
static int typing, armed;               /* a name is typed; Del was pressed once */
static char typed[SDK_MUS_NAME];
static sdk_mus_pattern_t clip;
static int screen_cols = 80, screen_rows = 21;

void sdk_sounds_reset(void)
{
    sdk_note[0] = 0;
    sdk_play_stop();
    sdk_music_clear(&mus);
    synced = 0;
    broken[0] = 0;
    page = PAGE_PATTERN;
    octave = 4;
    preview_speed = 6;
    pat = track = step = on_header = field = 0;
    memset(last_ins, 0, sizeof last_ins);
    last_note[0] = 60;
    last_note[1] = 48;
    last_note[2] = 36;
    ins_sel = ins_row = env_at = 0;
    sfx_sel = sfx_grid = sfx_field = sfx_step = 0;
    song_sel = song_order = song_field = place = 0;
    wave_sel = wave_at = shape_next = 0;
    typing = armed = 0;
    memset(&clip, 0, sizeof clip);
}

int sdk_sounds_typing(void) { return typing; }

/* What differs between the machines. The NES has four tracks, each with its channel; the noise track takes only
 * hits. The SNES has eight voices that take every instrument. */
static int tracks(void) { return mus.snes ? SDK_MUS_VOICES : SDK_MUS_TRACKS; }
static int channel_of(int t) { return mus.snes ? 0 : track_channel[t]; }
static int hit_track(int t) { return !mus.snes && t == 3; }
static int noise_sfx(const sdk_mus_sfx_t *s) { return mus.snes ? s->wave == SDK_MUS_WAVES : s->channel == 3; }
static int pages(void) { return target == SDK_SNES ? PAGES : PAGE_WAVE; }
/* The name of a wave of the SNES on the screen. */
static const char *wave_label(int wave)
{
    if (wave > SDK_MUS_WAVES && wave - SDK_MUS_WAVES - 1 < mus.waves)
        return mus.drawn[wave - SDK_MUS_WAVES - 1].name;
    return wave_names[wave <= SDK_MUS_WAVES ? wave : 2];
}

int sdk_sounds_keys(void)
{
    return !supported || broken[0] ? SDK_KEYS_SOUND_NONE :
           page == PAGE_INS ? (mus.snes ? SDK_KEYS_SOUND_SNES_INS : SDK_KEYS_SOUND_INSTRUMENTS) :
           page == PAGE_SFX ? SDK_KEYS_SOUND_SFX : page == PAGE_SONG ? SDK_KEYS_SOUND_SONG :
           page == PAGE_WAVE ? SDK_KEYS_SOUND_WAVES : SDK_KEYS_SOUND_PATTERN;
}

static int within(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The selections stay inside what there is. */
static void settle(void)
{
    ins_sel = within(ins_sel, 0, mus.instruments ? mus.instruments - 1 : 0);
    sfx_sel = within(sfx_sel, 0, mus.sfxs ? mus.sfxs - 1 : 0);
    song_sel = within(song_sel, 0, mus.songs ? mus.songs - 1 : 0);
    wave_sel = within(wave_sel, 0, mus.waves ? mus.waves - 1 : 0);
    const int steps = mus.pat[pat].used ? mus.pat[pat].steps : 16;
    step = within(step, 0, steps - 1);
    if (mus.instruments) {
        const int len = mus.ins[ins_sel].len;
        env_at = within(env_at, 0, len ? len - 1 : 0);
    }
    if (mus.sfxs)
        sfx_step = within(sfx_step, 0, within(mus.sfx[sfx_sel].steps, 0, SDK_MUS_SFX_STEPS - 1));
    else
        sfx_step = sfx_grid = 0;
    if (mus.songs)
        place = within(place, 0, within(mus.song[song_sel].len, 0, SDK_MUS_ORDER - 1));
    else
        place = song_order = 0;
}

static void read_code(void)
{
    code_rev = sdk_edit_revision();
    synced = 1;
    broken[0] = 0;
    const int snes = target == SDK_SNES;
    if ((snes ? sdk_music_parse_snes : sdk_music_parse)(sdk_edit_text(), sdk_edit_length(), is_c, &reading, broken,
                                                       sizeof broken) == 0) {
        mus = reading;
        for (int i = 0; i < SDK_MUS_PATTERNS; i++)
            if (!mus.pat[i].used)
                mus.pat[i].steps = 16;
    }
    silent_program = 0;
    if (!is_c) {                        /* assembly calls the driver itself */
        const char *s = sdk_edit_text();
        size_t from, to;
        const size_t len = sdk_edit_length();
        if (sdk_music_find(s, len, 0, &from, &to) <= 0)
            from = to = len;
        silent_program = !sdk_music_called(s, from, 0) && !sdk_music_called(s + to, len - to, 0);
    }
    settle();
}

void sdk_sounds_sync(const sdk_proj_t *p)
{
    const int c = sdk_build_is_c(p), other = p->target != target;
    target = p->target;
    supported = p->target != SDK_BM;
    if (page >= pages())
        page = PAGE_PATTERN;
    if (!supported) {
        sdk_play_stop();
        return;
    }
    if (!synced || c != is_c || other || sdk_edit_revision() != code_rev) {
        is_c = c;
        read_code();
    }
}

/* Writes the music into the code. run > 0: changes in a row with the same run are one step of the undo. */
static void commit(int run)
{
    char err[96];
    if (sdk_music_commit(&mus, is_c, run, err, sizeof err) != 0) {
        snprintf(sdk_note, sizeof sdk_note, "%.63s", err);
        read_code();
        return;
    }
    read_code();                        /* the lines of the code, for the strip */
}

static void play_of(const sdk_music_t *m, int what, int index, int note)
{
    char err[96];
    if (sdk_play_start(m, what, index, note, preview_speed, err, sizeof err) != 0)
        snprintf(sdk_note, sizeof sdk_note, "%.63s", err);
    else if (sdk_play_silent() && !sdk_note[0])
        snprintf(sdk_note, sizeof sdk_note, "This console has no sound output");
}

static void play(int what, int index, int note) { play_of(&mus, what, index, note); }

/* The drawn wave on the screen as a note: with an instrument made for this, that the code does not get. */
static void hear_wave(int note)
{
    static sdk_music_t one;
    one = mus;
    const int i = one.instruments < SDK_MUS_INS ? one.instruments++ : SDK_MUS_INS - 1;
    sdk_mus_ins_t *s = &one.ins[i];
    memset(s, 0, sizeof *s);
    s->wave = (uint8_t)(SDK_MUS_WAVES + 1 + wave_sel);
    s->attack = 15;
    s->decay = s->sustain = 7;
    s->level = 100;
    s->pan = 8;
    s->loop = s->release = SDK_MUS_NO;
    play_of(&one, SDK_PLAY_NOTE, i, note);
}

/* A change of the wave is heard, when nothing else plays. */
static void try_wave(void)
{
    if (sdk_play_what() == SDK_PLAY_NOTHING || sdk_play_what() == SDK_PLAY_NOTE)
        hear_wave(last_note[0]);
}

/* A note just written is heard, when nothing else plays. */
static void try_note(int ins, int note)
{
    if (ins >= 0 && ins < mus.instruments && (sdk_play_what() == SDK_PLAY_NOTHING ||
                                              sdk_play_what() == SDK_PLAY_NOTE))
        play(SDK_PLAY_NOTE, ins, note);
}

/* ---- drawing ---- */

static void text(int row, int col, const char *colour, const char *s)
{
    sdk_at(row, col);
    console_write("\x1b[0m");
    if (colour[0]) {
        char b[24];
        snprintf(b, sizeof b, "\x1b[%sm", colour);
        console_write(b);
    }
    console_write(s);
    console_write("\x1b[0m");
}

/* "Label value", the value in the swapped colours when it is the one chosen. */
static int labelled(int row, int col, const char *label, const char *value, int chosen)
{
    char b[48];
    text(row, col, "90", label);
    col += (int)strlen(label) + 1;
    snprintf(b, sizeof b, " %s ", value);
    text(row, col, chosen ? "30;107" : "97", b);
    return col + (int)strlen(b) + 1;
}

/* The picture of the piano, two rows and 40 columns: the black keys over the white ones, each with its note. */
static void keyboard(int row, int col)
{
    static const char white[] = "qwertyuiop", black[] = "23 567 90";
    static const char *const sharp = "CD FGA CD";
    static const char names[] = "CDEFGABCDE";
    for (int i = 0; i < 10; i++) {
        char b[8];
        snprintf(b, sizeof b, "%c %c", white[i], names[i]);
        text(row + 1, col + 4 * i, "30;47", b);
    }
    for (int i = 0; i < 9; i++) {
        if (black[i] == ' ')
            continue;
        char b[8];
        snprintf(b, sizeof b, "%c%c#", black[i], sharp[i]);
        text(row, col + 4 * i + 2, "97;100", b);
    }
}

/* The lines of the code round `line` (from 1), from the row top to the row bottom. */
static void code_strip(int top, int bottom, int line)
{
    char b[96];
    snprintf(b, sizeof b, " %s  %s@music", is_c ? "main.c" : "main.s", is_c ? "//" : ";");
    sdk_title_bar(top, 1, screen_cols, b, 0);
    const int room = bottom - top;
    if (line <= 0) {
        size_t from, to;
        if (sdk_music_find(sdk_edit_text(), sdk_edit_length(), is_c, &from, &to) <= 0) {
            text(top + 1, 3, "90", "no music in the code yet: what you write here goes there");
            return;
        }
        line = 1;
        for (size_t i = 0; i < from; i++)
            line += sdk_edit_text()[i] == '\n';
    }
    int first = line - room / 3;
    if (first < 1)
        first = 1;
    for (int i = 0; i < room && first + i <= sdk_edit_line_count(); i++) {
        const char *s;
        const int n = sdk_edit_line(first - 1 + i, &s);
        const int here = first + i == line;
        console_write(here ? "\x1b[0;30;47m" : "\x1b[0m");
        sdk_put_row(top + 1 + i, 2, screen_cols - 2, s ? s : "", n);
        console_write("\x1b[0m");
    }
}

static void tabs_line(void)
{
    int col = 1;
    for (int i = 0; i < pages(); i++) {
        char b[24];
        snprintf(b, sizeof b, " %s ", page_names[i]);
        text(1, col, i == page ? "30;103" : "37;100", b);
        col += (int)strlen(b) + 1;
    }
    static const char *const sounding[] = { "stopped", "pattern", "song", "effect", "note" };
    char b[40];
    snprintf(b, sizeof b, "octave %d  %s", octave, sounding[sdk_play_what()]);
    text(1, screen_cols - 20, "96", b);
}

/* The answer to a key, a message of the SDK or a warning, on the last row. */
static void message(const char *msg)
{
    const char *s = sdk_note[0] ? sdk_note : msg && msg[0] ? msg : silent_program && !broken[0] ?
                    (mus.snes ? "main.s calls no sound: add jsr snd_init before the NMI is on"
                              : "main.s calls no driver: add jsr snd_init, and jsr snd_frame in nmi") : NULL;
    if (!s)
        return;
    console_write("\x1b[93m");
    sdk_put_row(screen_rows, 1, screen_cols - 1, s, (int)strlen(s));
    console_write("\x1b[0m");
}

static int cell_col(int s) { return 11 + (s % 16) * 4 + (s % 16) / 4; }

/* The row over the grid: the numbers of the steps and the one that sounds. */
static void pattern_playhead(void)
{
    const int steps = mus.pat[pat].used ? mus.pat[pat].steps : 16;
    const int from = step / 16 * 16;
    int order = 0, row = -1;
    if (sdk_play_what() == SDK_PLAY_PATTERN)
        sdk_play_position(&order, &row);
    else if (sdk_play_what() == SDK_PLAY_SONG && mus.songs) {
        sdk_play_position(&order, &row);
        if (order >= mus.song[song_sel].len || mus.song[song_sel].order[order] != pat)
            row = -1;
    }
    for (int i = from; i < from + 16; i++) {
        char b[16];
        if (i >= steps)
            snprintf(b, sizeof b, "   ");
        else
            snprintf(b, sizeof b, " %02d", i);
        text(3, cell_col(i), i == row ? "30;107" : i % 4 ? "90" : "37", b);
    }
}

static void cell_text(const sdk_mus_cell_t *c, int t, char out[4])
{
    if (c->note == SDK_MUS_OFF)
        strcpy(out, "OFF");
    else if (hit_track(t)) {
        if (c->ins >= 1 && c->ins <= mus.instruments) {
            for (int i = 0; i < 3; i++) {
                const char ch = mus.ins[c->ins - 1].name[i];
                out[i] = !ch ? ' ' : ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch;
                if (!ch) {
                    out[i + 1] = 0;
                    break;
                }
            }
            out[3] = 0;
        } else
            out[0] = 0;
    } else if (c->note)
        sdk_music_note_name(c->note, out);
    else
        out[0] = 0;
}

static void draw_pattern(const char *msg)
{
    const sdk_mus_pattern_t *p = &mus.pat[pat];
    const int steps = p->used ? p->steps : 16;
    char v[24];
    snprintf(v, sizeof v, "%02d%s", pat, p->used ? "" : " new");
    int col = labelled(2, 1, "Pattern", v, on_header && field == 0);
    snprintf(v, sizeof v, "%d", steps);
    col = labelled(2, col, "Steps", v, on_header && field == 1);
    snprintf(v, sizeof v, "%d frames", preview_speed);
    col = labelled(2, col, "Play speed", v, on_header && field == 2);
    snprintf(v, sizeof v, "%d BPM", 900 / preview_speed);
    text(2, col, "90", v);
    pattern_playhead();
    const int from = step / 16 * 16;
    for (int t = 0; t < tracks(); t++) {
        char b[24];
        if (mus.snes)
            snprintf(b, sizeof b, "Voice %d  ", t + 1);
        else
            snprintf(b, sizeof b, "%-9s", track_names[t]);
        text(4 + t, 1, !on_header && t == track ? "97" : "37", b);
        for (int s = from; s < from + 16 && s < steps; s++) {
            char c[4], cell[8], colour[16];
            cell_text(&p->cell[s][t], t, c);
            const int here = !on_header && t == track && s == step;
            if (c[0])
                snprintf(cell, sizeof cell, "%-3s", c);
            else
                snprintf(cell, sizeof cell, " \xfa ");
            if (here)
                snprintf(colour, sizeof colour, "30;107");
            else if (p->cell[s][t].note == SDK_MUS_OFF)
                snprintf(colour, sizeof colour, "97;41");
            else if (c[0])
                snprintf(colour, sizeof colour, "30;%s", track_colour[t]);
            else
                snprintf(colour, sizeof colour, "%s", s % 4 ? "90" : "97;100");
            text(4 + t, cell_col(s), colour, cell);
        }
    }
    const sdk_mus_cell_t *c = &p->cell[step][track];
    char about[80] = "";
    if (!on_header && c->ins >= 1 && c->ins <= mus.instruments)
        snprintf(about, sizeof about, "step %02d: %s", step, mus.ins[c->ins - 1].name);
    else if (!on_header && last_ins[channel_of(track)] < mus.instruments &&
             mus.ins[last_ins[channel_of(track)]].channel == channel_of(track))
        snprintf(about, sizeof about, "step %02d   next note: %s", step,
                 mus.ins[last_ins[channel_of(track)]].name);
    const int under = 4 + tracks();
    text(under, 11, "93", about);
    keyboard(under + 1, 11);
    code_strip(under + 3, screen_rows - 1, p->used ? (on_header ? p->line : p->row_line[step]) : 0);
    message(msg);
}

/* The settings of a SNES instrument: the row, its value, how far it goes and the larger step. */
enum { SI_NAME, SI_WAVE, SI_NOISE, SI_ATTACK, SI_DECAY, SI_SUSTAIN, SI_RELEASE, SI_VOLUME, SI_PAN, SI_VIBRATO,
       SI_ARP, SI_ROWS };
static const char *const si_names[SI_ROWS] = { "Name", "Wave", "Noise", "Attack", "Decay", "Sustain", "Release",
                                               "Volume", "Pan", "Vibrato", "Arpeggio" };

static uint8_t *snes_value(sdk_mus_ins_t *s, int row, int *max, int *big)
{
    *big = 4;
    switch (row) {
    case SI_WAVE: *max = SDK_MUS_WAVES + mus.waves; *big = 1; return &s->wave;
    case SI_NOISE: *max = 31; return &s->noise;
    case SI_ATTACK: *max = 15; return &s->attack;
    case SI_DECAY: *max = 7; *big = 2; return &s->decay;
    case SI_SUSTAIN: *max = 7; *big = 2; return &s->sustain;
    case SI_RELEASE: *max = 31; return &s->rate;
    case SI_VOLUME: *max = 127; *big = 16; return &s->level;
    case SI_PAN: *max = 16; return &s->pan;
    case SI_VIBRATO: *max = 15; return &s->vibrato;
    default: *max = 255; *big = 16; return &s->arp;
    }
}

/* The shape of the envelope, 36 columns of 10 half rows: a sketch of what attack, decay, sustain and release do. */
static void snes_envelope(const sdk_mus_ins_t *s, int row, int col)
{
    const int attack = 1 + (15 - s->attack) / 2, decay = 2 + (7 - s->decay), hold = (s->sustain + 1) * 10 / 8;
    const int fade = s->rate ? 2 + (31 - s->rate) : 0;
    for (int x = 0; x < 36; x++) {
        int high;
        if (x < attack)
            high = 10 * (x + 1) / attack;
        else if (x < attack + decay)
            high = 10 - (10 - hold) * (x - attack + 1) / decay;
        else
            high = fade ? within(hold - hold * (x - attack - decay) / fade, 0, hold) : hold;
        const char *colour = x < attack ? "93" : x < attack + decay ? "33" : "35";
        for (int r = 0; r < 5; r++) {
            const int low = 2 * (4 - r);
            text(row + r, col + x, colour, high >= low + 2 ? "\xdb" : high == low + 1 ? "\xdc" : r == 4 ? "_" : " ");
        }
    }
}

static void draw_snes_instrument(void)
{
    sdk_mus_ins_t *s = &mus.ins[ins_sel];
    for (int row = 0; row < SI_ROWS; row++) {
        char v[32], label[16];
        int max, big;
        if (row == SI_NAME)
            snprintf(v, sizeof v, "%s", typing ? typed : s->name);
        else if (row == SI_WAVE)
            snprintf(v, sizeof v, "%s", wave_label(s->wave));
        else if (row == SI_NOISE && s->wave != SDK_MUS_WAVES)
            snprintf(v, sizeof v, "-");
        else if (row == SI_PAN)
            snprintf(v, sizeof v, s->pan == 8 ? "center" : s->pan < 8 ? "left %d" : "right %d",
                     s->pan < 8 ? 8 - s->pan : s->pan - 8);
        else if (row == SI_ARP && s->arp)
            snprintf(v, sizeof v, "$%02X +%d +%d", s->arp, s->arp >> 4, s->arp & 15);
        else if (row == SI_ARP)
            snprintf(v, sizeof v, "off");
        else
            snprintf(v, sizeof v, "%d", *snes_value(s, row, &max, &big));
        snprintf(label, sizeof label, "%-8s", si_names[row]);
        labelled(2 + row, 18, label, v, (row == SI_NAME && typing) || ins_row == row);
    }
    text(2, 42, "90", "envelope");
    snes_envelope(s, 3, 42);
}

static void draw_instruments(const char *msg)
{
    if (!mus.instruments) {
        text(3, 3, "97", "No instrument yet");
        code_strip(9, screen_rows - 1, 0);
        message(msg);
        return;
    }
    const sdk_mus_ins_t *s = &mus.ins[ins_sel];
    const int top = ins_sel > 7 ? ins_sel - 7 : 0;
    for (int i = 0; i < 8 && top + i < mus.instruments; i++) {
        char b[24];
        snprintf(b, sizeof b, " %02d %-10s", top + i, mus.ins[top + i].name);
        text(2 + i, 1, top + i == ins_sel ? "30;103" : "37", b);
    }
    if (mus.snes) {
        draw_snes_instrument();
        keyboard(9, 40);
        code_strip(14, screen_rows - 1, s->line);
        message(msg);
        return;
    }
    char v[48];
    labelled(2, 18, "Name    ", typing ? typed : s->name, typing || ins_row == 0);
    labelled(3, 18, "Channel ", channel_names[s->channel], ins_row == 1);
    if (s->channel == SDK_MUS_PULSE)
        snprintf(v, sizeof v, "%s", duty_names[s->wave & 3]);
    else if (s->channel == SDK_MUS_NOISE)
        snprintf(v, sizeof v, "pitch %d%s", s->wave & 15, s->wave & SDK_MUS_SHORT ? " short" : "");
    else
        snprintf(v, sizeof v, "-");
    labelled(4, 18, "Wave    ", v, ins_row == 2);
    snprintf(v, sizeof v, "%d", s->vibrato);
    labelled(5, 18, "Vibrato ", v, ins_row == 3);
    if (s->arp)
        snprintf(v, sizeof v, "$%02X +%d +%d", s->arp, s->arp >> 4, s->arp & 15);
    else
        snprintf(v, sizeof v, "off");
    labelled(6, 18, "Arpeggio", v, ins_row == 4);
    snprintf(v, sizeof v, "frame %d of %d: %d", s->len ? env_at + 1 : 0, s->len, s->len ? s->volume[env_at] : 15);
    text(7, 18, ins_row == 5 ? "97" : "90", "Volume, one a frame");
    text(7, 40, "93", v);
    const int from = env_at / 60 * 60;
    for (int i = from; i < s->len && i < from + 60; i++) {
        const int high = (s->volume[i] * 2 * ENV_ROWS + 14) / 15;      /* in half rows */
        for (int r = 0; r < ENV_ROWS; r++) {
            const int low = 2 * (ENV_ROWS - 1 - r);
            const char *ch = high >= low + 2 ? "\xdb" : high == low + 1 ? "\xdc" :
                             r == ENV_ROWS - 1 ? "_" : " ";
            text(8 + r, 18 + i - from, ins_row == 5 && i == env_at ? "97" :
                 s->release != SDK_MUS_NO && i >= s->release ? "35" : "33", ch);
        }
        const char *mark = i == s->loop ? "L" : i == s->release ? "R" : ins_row == 5 && i == env_at ? "^" : " ";
        text(8 + ENV_ROWS, 18 + i - from, i == env_at && ins_row == 5 ? "30;107" : "96", mark);
    }
    keyboard(5, 40);
    code_strip(14, screen_rows - 1, s->line);
    message(msg);
}

/* The drawn wave: 32 points two columns wide, 16 levels in 8 rows of half blocks, filled from the middle. */
#define WAVE_COL 6
static void draw_waves(const char *msg)
{
    if (!mus.waves) {
        text(3, 3, "97", "No wave of your own yet: a draws one.");
        text(4, 3, "90", "The six of the driver stay: square 12%, 25% and 50%, triangle, saw, sine.");
        code_strip(9, screen_rows - 1, 0);
        message(msg);
        return;
    }
    const sdk_mus_wave_t *w = &mus.drawn[wave_sel];
    char v[80];
    snprintf(v, sizeof v, "%d/%d", wave_sel + 1, mus.waves);
    int col = labelled(2, 1, "Wave", v, 0);
    col = labelled(2, col, "Name", typing ? typed : w->name, typing);
    const int users = sdk_music_wave_users(&mus, SDK_MUS_WAVES + 1 + wave_sel);
    if (users)
        snprintf(v, sizeof v, "used by %d instrument%s or effect%s", users, users > 1 ? "s" : "", users > 1 ? "s" : "");
    else
        snprintf(v, sizeof v, "not used: choose it in Instruments or SFX");
    text(2, col + 1, "90", v);
    for (int r = 0; r < 8; r++) {
        const int low = 2 * (7 - r);                    /* the lower level of the row */
        for (int i = 0; i < SDK_MUS_POINTS; i++) {
            const int p = w->point[i];
            const int under = p >= 8 ? low >= 8 && low <= p : low >= p && low <= 7;
            const int over = p >= 8 ? low + 1 >= 8 && low + 1 <= p : low + 1 >= p && low + 1 <= 7;
            const char *ch = under && over ? "\xdb\xdb" : over ? "\xdf\xdf" : under ? "\xdc\xdc" :
                             i % 4 ? "  " : "\xfa ";
            const int here = i == wave_at;
            text(3 + r, WAVE_COL + 2 * i, here ? (under || over ? "93;100" : "37;100") :
                 under || over ? "96" : "90", ch);
        }
    }
    text(3, 2, "90", "15");
    text(6, 3, "90", "8");
    text(10, 3, "90", "0");
    for (int i = 0; i < SDK_MUS_POINTS; i++) {
        if (i != wave_at && i % 4)
            continue;
        snprintf(v, sizeof v, "%02d", i);
        text(11, WAVE_COL + 2 * i, i == wave_at ? "30;107" : "90", v);
    }
    snprintf(v, sizeof v, "point %02d  value %d", wave_at, w->point[wave_at]);
    text(12, WAVE_COL, "93", v);
    keyboard(13, 9);
    code_strip(15, screen_rows - 1, w->line);
    message(msg);
}

static void draw_sfx(const char *msg)
{
    if (!mus.sfxs) {
        text(3, 3, "97", "No effect yet");
        code_strip(9, screen_rows - 1, 0);
        message(msg);
        return;
    }
    const sdk_mus_sfx_t *s = &mus.sfx[sfx_sel];
    char v[24];
    snprintf(v, sizeof v, "%d/%d", sfx_sel + 1, mus.sfxs);
    int col = labelled(2, 1, "SFX", v, 0);
    col = labelled(2, col, "Name", typing ? typed : s->name, typing || (!sfx_grid && sfx_field == 0));
    snprintf(v, sizeof v, "%d", s->speed);
    col = labelled(2, col, "Speed", v, !sfx_grid && sfx_field == 1);
    if (mus.snes) {
        snprintf(v, sizeof v, "V%d", s->channel + 1);
        col = labelled(2, col, "Voice", v, !sfx_grid && sfx_field == 2);
    } else
        col = labelled(2, col, "Channel", sfx_channels[s->channel], !sfx_grid && sfx_field == 2);
    if (mus.snes)
        snprintf(v, sizeof v, "%s", wave_label(s->wave));
    else if (s->channel < 2)
        snprintf(v, sizeof v, "%s", duty_names[s->wave & 3]);
    else
        snprintf(v, sizeof v, "%s", s->channel == 3 ? (s->wave & SDK_MUS_SHORT ? "short" : "long") : "-");
    labelled(2, col, "Wave", v, !sfx_grid && sfx_field == 3);
    for (int i = 0; i < SDK_MUS_SFX_STEPS; i++) {
        const int c = 9 + 2 * i;
        const sdk_mus_step_t *e = &s->step[i];
        const int here = sfx_grid && i == sfx_step, used = i < s->steps;
        int high = 0;                       /* the height of the bar, 1 to 12 half rows */
        if (used && e->note >= SDK_MUS_LOW)
            high = noise_sfx(s) ? 1 + ((e->note - SDK_MUS_LOW) & 15) * 11 / 15
                                   : 1 + within(e->note - 36, 0, 60) * 11 / 60;
        for (int r = 0; r < 6; r++) {
            const int low = 2 * (5 - r);
            const char *ch = high >= low + 2 ? "\xdb\xdb" : high == low + 1 ? "\xdc\xdc" : i % 4 ? "  " : "\xfa ";
            text(4 + r, c, here ? "97;100" : high > low ? "93" : "90", ch);
        }
        char b[4] = "  ";
        if (used && e->note == SDK_MUS_OFF)
            strcpy(b, "--");
        else if (used)
            snprintf(b, sizeof b, "%-2X", e->volume);
        text(10, c, here ? "30;107" : "37", b);
        b[0] = used && e->fx >> 4 ? "?UDV"[(e->fx >> 4) & 3] : ' ';
        b[1] = 0;
        text(11, c, "96", b);
        if (i % 4 == 0) {
            snprintf(b, sizeof b, "%02d", i + 1);
            text(3, c, "90", b);
        }
    }
    text(4, 1, "90", "note");
    text(10, 1, "90", "volume");
    text(11, 1, "90", "effect");
    char about[80] = "";
    if (sfx_grid && sfx_step < s->steps) {
        const sdk_mus_step_t *e = &s->step[sfx_step];
        char note[4], fx[20] = "none";
        if (noise_sfx(s) && e->note >= SDK_MUS_LOW)
            snprintf(note, sizeof note, "N%d", (e->note - SDK_MUS_LOW) & 15);
        else
            sdk_music_note_name(e->note, note);
        if (e->fx >> 4 >= 1 && e->fx >> 4 <= 3)
            snprintf(fx, sizeof fx, "%s %d", (e->fx >> 4) == 1 ? "slide up" : (e->fx >> 4) == 2 ? "slide down"
                     : "vibrato", e->fx & 15);
        snprintf(about, sizeof about, "step %02d  note %s  volume %d  effect %s", sfx_step + 1, note, e->volume, fx);
    } else if (sfx_grid)
        snprintf(about, sizeof about, "step %02d: a note adds it", sfx_step + 1);
    text(12, 1, "93", about);
    keyboard(13, 9);
    code_strip(15, screen_rows - 1, s->line);
    message(msg);
}

static void song_playhead(void)
{
    if (!mus.songs)
        return;
    const sdk_mus_song_t *s = &mus.song[song_sel];
    int order = -1, row = 0;
    if (sdk_play_what() == SDK_PLAY_SONG)
        sdk_play_position(&order, &row);
    const int from = place / 10 * 10;
    for (int i = from; i < from + 10; i++) {
        char b[16];
        if (i <= s->len)
            snprintf(b, sizeof b, " %-3d", i + 1);
        else
            snprintf(b, sizeof b, "    ");
        text(7, 18 + (i - from) * 6, i == order ? "30;107" : "90", b);
    }
}

static void draw_song(const char *msg)
{
    if (!mus.songs) {
        text(3, 3, "97", "No song yet");
        code_strip(9, screen_rows - 1, 0);
        message(msg);
        return;
    }
    const sdk_mus_song_t *s = &mus.song[song_sel];
    for (int i = 0; i < mus.songs; i++) {
        char b[24];
        snprintf(b, sizeof b, " %02d %-10s", i, mus.song[i].name);
        text(2 + i, 1, i == song_sel ? "30;103" : "37", b);
    }
    char v[24];
    labelled(2, 18, "Name   ", typing ? typed : s->name, typing || (!song_order && song_field == 0));
    snprintf(v, sizeof v, "%d frames (%d BPM)", s->speed, 900 / s->speed);
    labelled(3, 18, "Speed  ", v, !song_order && song_field == 1);
    if (s->loop == SDK_MUS_NO)
        snprintf(v, sizeof v, "no");
    else
        snprintf(v, sizeof v, "to place %d", s->loop + 1);
    labelled(4, 18, "Loop   ", v, !song_order && song_field == 2);
    text(6, 18, "90", "Pattern order");
    song_playhead();
    const int from = place / 10 * 10;
    for (int i = from; i < from + 10; i++) {
        const int c = 18 + (i - from) * 6;
        const int here = song_order && i == place;
        if (i < s->len) {
            snprintf(v, sizeof v, " %02d ", s->order[i]);
            char colour[16];
            snprintf(colour, sizeof colour, "30;%s", here ? "107" : track_colour[s->order[i] & 3]);
            text(8, c, colour, v);
            text(9, c, "96", i == s->loop ? "loop" : "    ");
        } else if (i == s->len)
            text(8, c, here ? "30;107" : "37;100", " +  ");
    }
    if (song_order && place < s->len) {
        const sdk_mus_pattern_t *p = &mus.pat[s->order[place]];
        snprintf(v, sizeof v, "pattern %02d", s->order[place]);
        text(10, 18, "93", v);
        text(10, 30, "90", p->used ? "" : "(empty: 16 steps of silence)");
    }
    code_strip(11, screen_rows - 1, s->line);
    message(msg);
}

void sdk_sounds_draw(int cols, int rows, const char *msg)
{
    screen_cols = cols;
    screen_rows = rows;
    console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    if (!supported) {
        text(rows / 2 - 1, 3, "97", "Sounds: for NES and SNES. A bm project takes its sounds from bm Sound.");
        return;
    }
    tabs_line();
    if (broken[0]) {
        text(3, 3, "91", "The music block of the code cannot be read:");
        text(4, 3, "93", broken);
        text(5, 3, "90", "Put it right in the code. Nothing is written until then.");
        int line = 0;
        sscanf(broken, "line %d", &line);
        code_strip(7, rows - 1, line);
        message(msg);
        return;
    }
    if (page == PAGE_PATTERN)
        draw_pattern(msg);
    else if (page == PAGE_INS)
        draw_instruments(msg);
    else if (page == PAGE_SFX)
        draw_sfx(msg);
    else if (page == PAGE_WAVE)
        draw_waves(msg);
    else
        draw_song(msg);
}

void sdk_sounds_playhead(void)
{
    if (!supported || broken[0])
        return;
    if (page == PAGE_PATTERN)
        pattern_playhead();
    else if (page == PAGE_SONG)
        song_playhead();
}

/* ---- keys ---- */

/* A key of the piano as a note: from the C of the octave, a half tone a key (PIANO). -1: not one. */
static int piano(int key)
{
    const char *p = key > 0 && key < 127 ? strchr(PIANO, key) : NULL;
    return p ? within((octave + 1) * 12 + (int)(p - PIANO), SDK_MUS_LOW, SDK_MUS_HIGH) : -1;
}

/* The instrument for a new note of a channel: the last one used, else the first, else a new one. -1: no room. */
static int instrument_for(int channel)
{
    int i = last_ins[channel];
    if (i < mus.instruments && mus.ins[i].channel == channel)
        return i;
    i = sdk_music_next_ins(&mus, -1, 1, channel);
    if (i < 0) {
        i = sdk_music_add_ins(&mus, channel);
        if (i >= 0)
            snprintf(sdk_note, sizeof sdk_note, "New instrument %s (page Instruments)", mus.ins[i].name);
        else
            snprintf(sdk_note, sizeof sdk_note, "No room: %d instruments at most", SDK_MUS_INS);
    }
    if (i >= 0)
        last_ins[channel] = i;
    return i;
}

static void use_pattern(void)
{
    sdk_mus_pattern_t *p = &mus.pat[pat];
    if (!p->used) {
        memset(p, 0, sizeof *p);
        p->used = 1;
        p->steps = 16;
    }
}

/* A note (or for the noise a hit) at the cursor. */
static void put_note(int note, int advance)
{
    const int ch = channel_of(track);
    const int i = instrument_for(ch);
    if (i < 0)
        return;
    use_pattern();
    sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
    c->ins = (uint8_t)(i + 1);
    c->note = (uint8_t)(hit_track(track) ? SDK_MUS_NONE : note);
    if (!hit_track(track))
        last_note[ch] = note;
    if (advance)
        step = (step + 1) % mus.pat[pat].steps;
    commit(0);
    try_note(i, hit_track(track) ? 60 : note);
}

static void change_note(int by)
{
    if (!mus.pat[pat].used)
        return;
    sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
    if (hit_track(track)) {                     /* the noise: the next noise instrument */
        if (!c->ins)
            return;
        const int i = sdk_music_next_ins(&mus, c->ins - 1, by > 0 ? 1 : -1, SDK_MUS_NOISE);
        if (i < 0)
            return;
        c->ins = (uint8_t)(i + 1);
        last_ins[SDK_MUS_NOISE] = i;
    } else {
        if (c->note < SDK_MUS_LOW)
            return;
        c->note = (uint8_t)within(c->note + by, SDK_MUS_LOW, SDK_MUS_HIGH);
        last_note[channel_of(track)] = c->note;
    }
    const int i = c->ins - 1, note = hit_track(track) ? 60 : c->note;
    commit(100000 + pat * 600 + step * 8 + track);       /* a range of its own: the other pages count below */
    try_note(i >= 0 ? i : last_ins[channel_of(track)], note);
}

static int pattern_key(int key, int mods)
{
    const int ctrl = (mods & SDK_MOD_CTRL) != 0;
    const int steps = mus.pat[pat].used ? mus.pat[pat].steps : 16;
    if (key == HID_KEY_PGUP || key == HID_KEY_PGDN) {
        pat = within(pat + (key == HID_KEY_PGDN ? 1 : -1), 0, SDK_MUS_PATTERNS - 1);
        settle();
    } else if (key == 0x03) {                   /* Ctrl+C */
        clip = mus.pat[pat];
        snprintf(sdk_note, sizeof sdk_note, clip.used ? "Copied pattern %02d" : "Pattern %02d is empty", pat);
    } else if (key == 0x16) {                   /* Ctrl+V */
        if (!clip.used)
            snprintf(sdk_note, sizeof sdk_note, "No pattern copied (Ctrl+C)");
        else {
            mus.pat[pat] = clip;
            settle();
            commit(0);
            snprintf(sdk_note, sizeof sdk_note, "Pasted into pattern %02d", pat);
        }
    } else if (on_header) {
        const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
        if (key == HID_KEY_DOWN) {
            on_header = 0;
            track = 0;
        } else if (key == HID_KEY_LEFT || key == HID_KEY_RIGHT)
            field = within(field + (key == HID_KEY_RIGHT ? 1 : -1), 0, 2);
        else if (d && field == 0) {
            pat = within(pat + d, 0, SDK_MUS_PATTERNS - 1);
            settle();
        } else if (d && field == 1) {
            use_pattern();
            const int big = key == '+' || key == '_' ? 4 : 1;
            mus.pat[pat].steps = (uint8_t)within(mus.pat[pat].steps + d * big, 1, SDK_MUS_STEPS);
            settle();
            commit(900 + pat);
        } else if (d && field == 2)
            preview_speed = within(preview_speed + d, 1, 60);
        else
            return 0;
    } else if (key == HID_KEY_UP) {
        if (track)
            track--;
        else
            on_header = 1;
    } else if (key == HID_KEY_DOWN)
        track = within(track + 1, 0, tracks() - 1);
    else if (key == HID_KEY_LEFT && !ctrl)
        step = (step + steps - 1) % steps;
    else if (key == HID_KEY_RIGHT && !ctrl)
        step = (step + 1) % steps;
    else if (key == HID_KEY_HOME)
        step = 0;
    else if (key == HID_KEY_END)
        step = steps - 1;
    else if (key == '\r' || key == '\n') {
        sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
        if (mus.pat[pat].used && (c->note || c->ins)) {
            c->note = c->ins = 0;
            commit(0);
        } else
            put_note(last_note[channel_of(track)], 0);
    } else if (piano(key) >= 0)
        put_note(piano(key), 1);
    else if (key == '1') {
        use_pattern();
        sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
        c->note = SDK_MUS_OFF;
        c->ins = 0;
        step = (step + 1) % mus.pat[pat].steps;
        commit(0);
    } else if (key == 0x08 || key == 0x7F || key == HID_KEY_DEL) {
        sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
        if (mus.pat[pat].used && (c->note || c->ins)) {
            c->note = c->ins = 0;
            commit(0);
        }
    } else if (key == '-' || key == '=')
        change_note(key == '=' ? 1 : -1);
    else if (key == '_' || key == '+')
        change_note(key == '+' ? 12 : -12);
    else if (key == '[' || key == ']') {
        const int ch = channel_of(track);
        sdk_mus_cell_t *c = &mus.pat[pat].cell[step][track];
        const int has = mus.pat[pat].used && c->ins;
        const int i = sdk_music_next_ins(&mus, has ? c->ins - 1 : last_ins[ch] < mus.instruments &&
                                         mus.ins[last_ins[ch]].channel == ch ? last_ins[ch] : -1,
                                         key == ']' ? 1 : -1, ch);
        if (i < 0)
            snprintf(sdk_note, sizeof sdk_note, "No %s%sinstrument yet", mus.snes ? "" : channel_names[ch],
                     mus.snes ? "" : " ");
        else {
            last_ins[ch] = i;
            if (has) {
                c->ins = (uint8_t)(i + 1);
                const int note = hit_track(track) ? 60 : c->note;
                commit(0);
                try_note(i, note >= SDK_MUS_LOW ? note : last_note[ch]);
            } else
                try_note(i, last_note[ch]);
        }
    } else
        return 0;
    return 1;
}

static void start_name(const char *now)
{
    typing = 1;
    snprintf(typed, sizeof typed, "%s", now);
}

/* The name typed goes to what the page shows. */
static void take_name(void)
{
    char *name = NULL;
    int taken = 0;
    if (page == PAGE_INS) {
        name = mus.ins[ins_sel].name;
        for (int i = 0; i < mus.instruments; i++)
            taken |= i != ins_sel && !strcmp(mus.ins[i].name, typed);
    } else if (page == PAGE_SFX) {
        name = mus.sfx[sfx_sel].name;
        for (int i = 0; i < mus.sfxs; i++)
            taken |= i != sfx_sel && !strcmp(mus.sfx[i].name, typed);
    } else if (page == PAGE_WAVE) {
        name = mus.drawn[wave_sel].name;
        for (int i = 0; i < mus.waves; i++)
            taken |= i != wave_sel && sdk_list_same(mus.drawn[i].name, typed);
    } else {
        name = mus.song[song_sel].name;
        for (int i = 0; i < mus.songs; i++)
            taken |= i != song_sel && !strcmp(mus.song[i].name, typed);
    }
    if (!(page == PAGE_WAVE ? sdk_music_wave_name_ok(typed) : sdk_music_name_ok(typed)) || taken) {
        snprintf(sdk_note, sizeof sdk_note, taken ? "'%s' is taken" : "'%s' cannot be a name", typed);
        return;
    }
    typing = 0;
    snprintf(name, SDK_MUS_NAME, "%s", typed);
    commit(0);
    if (page == PAGE_SFX || page == PAGE_SONG)
        snprintf(sdk_note, sizeof sdk_note, "In the program: %s_%s", page == PAGE_SFX ? "sfx" : "song", typed);
}

static int name_key(int key)
{
    const size_t n = strlen(typed);
    if (key == 0x1B)
        typing = 0;
    else if (key == '\r' || key == '\n')
        take_name();
    else if ((key == 0x08 || key == 0x7F) && n)
        typed[n - 1] = 0;
    else if (n + 1 < sizeof typed && key > 0 && key < 127 &&
             (key == '_' || (key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z'))) {
        typed[n] = (char)key;
        typed[n + 1] = 0;
    }
    return 1;
}

/* Del: the first time asks, the second deletes. */
static int confirmed(const char *what)
{
    if (armed) {
        armed = 0;
        return 1;
    }
    armed = 1;
    snprintf(sdk_note, sizeof sdk_note, "Del again deletes the %s (Ctrl+Z brings it back)", what);
    return 0;
}

/* A change of the channel: the patterns drop the instrument from the tracks that cannot play it. */
static void change_channel(int by)
{
    sdk_mus_ins_t *s = &mus.ins[ins_sel];
    s->channel = (uint8_t)((s->channel + 3 + by) % 3);
    s->wave = s->channel == SDK_MUS_TRIANGLE ? 0 : 2;
    int lost = 0;
    for (int p = 0; p < SDK_MUS_PATTERNS; p++)
        for (int i = 0; i < SDK_MUS_STEPS; i++)
            for (int t = 0; t < SDK_MUS_TRACKS; t++)
                if (mus.pat[p].cell[i][t].ins == ins_sel + 1 && track_channel[t] != s->channel) {
                    mus.pat[p].cell[i][t].ins = 0;
                    if (t == 3)
                        mus.pat[p].cell[i][t].note = 0;
                    lost++;
                }
    if (lost)
        snprintf(sdk_note, sizeof sdk_note, "%d steps lost this instrument (Ctrl+Z)", lost);
}

static void envelope_insert(sdk_mus_ins_t *s)
{
    if (s->len == SDK_MUS_ENV) {
        snprintf(sdk_note, sizeof sdk_note, "%d frames at most", SDK_MUS_ENV);
        return;
    }
    const int at = s->len ? env_at + 1 : 0;
    const uint8_t v = s->len ? s->volume[env_at] : 15;
    memmove(&s->volume[at + 1], &s->volume[at], (size_t)(s->len - at));
    s->volume[at] = v;
    s->len++;
    if (s->loop != SDK_MUS_NO && s->loop >= at)
        s->loop++;
    if (s->release != SDK_MUS_NO && s->release >= at)
        s->release++;
    env_at = at;
}

static void envelope_delete(sdk_mus_ins_t *s)
{
    if (!s->len)
        return;
    memmove(&s->volume[env_at], &s->volume[env_at + 1], (size_t)(s->len - env_at - 1));
    s->len--;
    if (s->loop != SDK_MUS_NO && (s->loop > env_at || s->loop >= s->len))
        s->loop = s->loop ? (uint8_t)(s->loop - 1) : SDK_MUS_NO;
    if (s->release != SDK_MUS_NO && (s->release > env_at || s->release >= s->len))
        s->release = s->release ? (uint8_t)(s->release - 1) : SDK_MUS_NO;
    if (!s->len)
        s->loop = s->release = SDK_MUS_NO;
    else if (s->loop == s->release)
        s->loop = SDK_MUS_NO;
    if (env_at >= s->len && env_at)
        env_at--;
}

static int snes_instrument_key(int key)
{
    sdk_mus_ins_t *s = &mus.ins[ins_sel];
    const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
    const int note = piano(key);
    if (key == HID_KEY_UP)
        ins_row = within(ins_row - 1, 0, SI_ROWS - 1);
    else if (key == HID_KEY_DOWN)
        ins_row = within(ins_row + 1, 0, SI_ROWS - 1);
    else if (note >= 0) {
        last_note[0] = note;
        play(SDK_PLAY_NOTE, ins_sel, note);
    } else if (ins_row == SI_NAME && (key == '\r' || key == '\n'))
        start_name(s->name);
    else if (key == '\r' || key == '\n')
        play(SDK_PLAY_NOTE, ins_sel, last_note[0]);
    else if (d && ins_row != SI_NAME) {
        int max, big;
        uint8_t *v = snes_value(s, ins_row, &max, &big);
        *v = (uint8_t)within(*v + d * (key == '+' || key == '_' ? big : 1), 0, max);
        commit(2000 + ins_sel * 16 + ins_row);
        try_note(ins_sel, last_note[0]);
    } else
        return 0;
    return 1;
}

/* What a new instrument starts from. NES: the channel of the one on the screen. SNES: lead, bass or drum, the first
 * the music has not got; with all three, the kind of the one on the screen. */
static int new_kind(void)
{
    if (!mus.snes)
        return mus.instruments ? mus.ins[ins_sel].channel : SDK_MUS_PULSE;
    static const char *const kinds[3] = { "lead", "bass", "drum" };
    for (int k = 0; k < 3; k++) {
        int i = 0;
        while (i < mus.instruments && strcmp(mus.ins[i].name, kinds[k]))
            i++;
        if (i == mus.instruments)
            return k;
    }
    const int wave = mus.ins[ins_sel].wave;
    return wave == SDK_MUS_WAVES ? 2 : wave == 3 ? 1 : 0;       /* 3: the triangle */
}

static int instruments_key(int key)
{
    if (key == 'a') {
        const int i = sdk_music_add_ins(&mus, new_kind());
        if (i < 0)
            snprintf(sdk_note, sizeof sdk_note, "%d instruments at most", SDK_MUS_INS);
        else {
            ins_sel = i;
            env_at = 0;
            commit(0);
        }
        return 1;
    }
    if (!mus.instruments)
        return 0;
    sdk_mus_ins_t *s = &mus.ins[ins_sel];
    const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
    const int big = key == '+' || key == '_';
    const int note = piano(key);
    if (key == HID_KEY_PGUP || key == HID_KEY_PGDN) {
        ins_sel = within(ins_sel + (key == HID_KEY_PGDN ? 1 : -1), 0, mus.instruments - 1);
        env_at = 0;
    } else if (key == HID_KEY_DEL) {
        if (confirmed("instrument")) {
            sdk_music_remove_ins(&mus, ins_sel);
            settle();
            commit(0);
        }
    } else if (mus.snes)
        return snes_instrument_key(key);
    else if (key == HID_KEY_UP)
        ins_row = within(ins_row - 1, 0, 5);
    else if (key == HID_KEY_DOWN)
        ins_row = within(ins_row + 1, 0, 5);
    else if (note >= 0) {
        last_note[s->channel] = note;
        play(SDK_PLAY_NOTE, ins_sel, note);
    } else if (ins_row == 0 && (key == '\r' || key == '\n'))
        start_name(s->name);
    else if (key == '\r' || key == '\n')
        play(SDK_PLAY_NOTE, ins_sel, last_note[s->channel]);
    else if (d && ins_row >= 1 && ins_row <= 4) {
        if (ins_row == 1)
            change_channel(d);
        else if (ins_row == 2) {
            if (s->channel == SDK_MUS_PULSE)
                s->wave = (uint8_t)((s->wave + 4 + d) % 4);
            else if (s->channel == SDK_MUS_NOISE)
                s->wave = (uint8_t)(big ? s->wave ^ SDK_MUS_SHORT
                                        : (s->wave & SDK_MUS_SHORT) | ((s->wave + 16 + d) & 15));
        } else if (ins_row == 3)
            s->vibrato = (uint8_t)within(s->vibrato + d, 0, 15);
        else
            s->arp = (uint8_t)within(s->arp + d * (big ? 16 : 1), 0, 255);
        commit(2000 + ins_sel * 8 + ins_row);
        try_note(ins_sel, last_note[mus.ins[ins_sel].channel]);
    } else if (ins_row == 5 && key == HID_KEY_LEFT)
        env_at = within(env_at - 1, 0, s->len ? s->len - 1 : 0);
    else if (ins_row == 5 && key == HID_KEY_RIGHT)
        env_at = within(env_at + 1, 0, s->len ? s->len - 1 : 0);
    else if (ins_row == 5 && d && s->len) {
        s->volume[env_at] = (uint8_t)within(s->volume[env_at] + d * (big ? 4 : 1), 0, 15);
        commit(2500 + ins_sel * 70 + env_at);
    } else if (ins_row == 5 && key == 'd') {
        envelope_insert(s);
        commit(0);
    } else if (ins_row == 5 && (key == 0x08 || key == 0x7F)) {
        envelope_delete(s);
        commit(0);
    } else if (ins_row == 5 && (key == '[' || key == ']') && s->len) {
        uint8_t *m = key == '[' ? &s->loop : &s->release;
        const uint8_t other = key == '[' ? s->release : s->loop;
        if (*m == env_at)
            *m = SDK_MUS_NO;
        else if (other != SDK_MUS_NO && (key == '[' ? env_at >= other : env_at <= other))
            snprintf(sdk_note, sizeof sdk_note, "The loop comes before the release");
        else
            *m = (uint8_t)env_at;
        commit(0);
    } else
        return 0;
    return 1;
}

/* A new effect or song takes a free name. */
static void free_name(char *out, const char *base, int songs)
{
    for (int n = 1;; n++) {
        if (n == 1)
            snprintf(out, SDK_MUS_NAME, "%s", base);
        else
            snprintf(out, SDK_MUS_NAME, "%s%d", base, n);
        int taken = 0;
        for (int i = 0; i < (songs ? mus.songs : mus.sfxs); i++)
            taken |= !strcmp(songs ? mus.song[i].name : mus.sfx[i].name, out);
        if (!taken)
            return;
    }
}

static void sfx_put(int note)
{
    sdk_mus_sfx_t *s = &mus.sfx[sfx_sel];
    if (sfx_step >= SDK_MUS_SFX_STEPS)
        return;
    while (s->steps <= sfx_step) {
        memset(&s->step[s->steps], 0, sizeof s->step[0]);
        s->step[s->steps++].volume = 12;
    }
    s->step[sfx_step].note = (uint8_t)note;
    if (note > SDK_MUS_OFF && !s->step[sfx_step].volume)
        s->step[sfx_step].volume = 12;
}

/* The noise counts its pitches from N0: the notes of an effect that becomes, or stops being, noise. */
static void noise_notes(sdk_mus_sfx_t *s, int was)
{
    const int now = noise_sfx(s);
    for (int i = 0; i < s->steps; i++) {
        uint8_t *n = &s->step[i].note;
        if (*n < SDK_MUS_LOW || now == was)
            continue;
        *n = (uint8_t)(now ? SDK_MUS_LOW + (*n - SDK_MUS_LOW) % 16 : 60 + (*n - SDK_MUS_LOW));
    }
}

static int sfx_key(int key)
{
    if (key == 'a') {
        if (mus.sfxs == SDK_MUS_SFX) {
            snprintf(sdk_note, sizeof sdk_note, "%d effects at most", SDK_MUS_SFX);
            return 1;
        }
        sdk_mus_sfx_t *s = &mus.sfx[mus.sfxs];
        memset(s, 0, sizeof *s);
        free_name(s->name, "sound", 0);
        s->speed = 2;
        s->wave = 2;
        s->channel = mus.snes ? SDK_MUS_VOICES - 1 : 0;
        s->steps = 1;
        s->step[0].note = 72;
        s->step[0].volume = 15;
        sfx_sel = mus.sfxs++;
        sfx_step = 0;
        sfx_grid = 1;
        commit(0);
        snprintf(sdk_note, sizeof sdk_note, "In the program: sfx_%s", mus.sfx[sfx_sel].name);
        return 1;
    }
    if (!mus.sfxs)
        return 0;
    sdk_mus_sfx_t *s = &mus.sfx[sfx_sel];
    const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
    const int run = 3000 + sfx_sel * 200 + sfx_step * 4;
    if (key == HID_KEY_PGUP || key == HID_KEY_PGDN) {
        sfx_sel = within(sfx_sel + (key == HID_KEY_PGDN ? 1 : -1), 0, mus.sfxs - 1);
        sfx_step = 0;
    } else if (!sfx_grid) {
        if (key == HID_KEY_DOWN)
            sfx_grid = 1;
        else if (key == HID_KEY_LEFT || key == HID_KEY_RIGHT)
            sfx_field = within(sfx_field + (key == HID_KEY_RIGHT ? 1 : -1), 0, 3);
        else if (key == HID_KEY_DEL) {
            if (confirmed("effect")) {
                memmove(s, s + 1, (size_t)(mus.sfxs - sfx_sel - 1) * sizeof *s);
                mus.sfxs--;
                settle();
                commit(0);
            }
        } else if ((key == '\r' || key == '\n') && sfx_field == 0)
            start_name(s->name);
        else if (d && sfx_field == 1) {
            s->speed = (uint8_t)within(s->speed + d, 1, 60);
            commit(run + 1);
        } else if (d && sfx_field == 2 && mus.snes) {
            s->channel = (uint8_t)((s->channel + SDK_MUS_VOICES + d) % SDK_MUS_VOICES);
            commit(run + 2);
        } else if (d && sfx_field == 2) {
            const int was = noise_sfx(s);
            s->channel = (uint8_t)((s->channel + 4 + d) % 4);
            noise_notes(s, was);
            s->wave = s->channel < 2 ? 2 : 0;
            commit(0);
        } else if (d && sfx_field == 3 && mus.snes) {
            const int was = noise_sfx(s);
            const int count = SDK_MUS_WAVES + 1 + mus.waves;
            s->wave = (uint8_t)((s->wave + count + d) % count);
            noise_notes(s, was);
            commit(0);
        } else if (d && sfx_field == 3) {
            if (s->channel < 2)
                s->wave = (uint8_t)((s->wave + 4 + d) % 4);
            else if (s->channel == 3)
                s->wave ^= SDK_MUS_SHORT;
            commit(run + 3);
        } else
            return 0;
    } else {
        const int in = sfx_step < s->steps;
        sdk_mus_step_t *e = &s->step[sfx_step];
        const int note = piano(key);
        if (key == HID_KEY_UP)
            sfx_grid = 0;
        else if (key == HID_KEY_LEFT)
            sfx_step = within(sfx_step - 1, 0, SDK_MUS_SFX_STEPS - 1);
        else if (key == HID_KEY_RIGHT)
            sfx_step = within(sfx_step + 1, 0, within(s->steps, 0, SDK_MUS_SFX_STEPS - 1));
        else if (key == HID_KEY_HOME)
            sfx_step = 0;
        else if (key == HID_KEY_END)
            sfx_step = s->steps ? s->steps - 1 : 0;
        else if (key == '\r' || key == '\n') {
            if (in && e->note)
                e->note = 0;
            else
                sfx_put(noise_sfx(s) ? SDK_MUS_LOW + 8 : 72);
            commit(0);
        } else if (note >= 0) {
            sfx_put(noise_sfx(s) ? SDK_MUS_LOW + note % 16 : note);
            sfx_step = within(sfx_step + 1, 0, SDK_MUS_SFX_STEPS - 1);
            commit(0);
        } else if (key == '1') {
            sfx_put(SDK_MUS_OFF);
            commit(0);
        } else if (d && in && e->note >= SDK_MUS_LOW) {
            const int by = key == '+' || key == '_' ? 12 : 1;
            e->note = (uint8_t)within(e->note + d * by, SDK_MUS_LOW,
                                      noise_sfx(s) ? SDK_MUS_LOW + 15 : SDK_MUS_HIGH);
            commit(run);
        } else if ((key == ';' || key == '\'') && in) {
            e->volume = (uint8_t)within(e->volume + (key == '\'' ? 1 : -1), 0, 15);
            commit(run + 1);
        } else if ((key == 'k' || key == 'l') && in) {
            const int kind = ((e->fx >> 4) + (key == 'l' ? 1 : 3)) % 4;
            e->fx = (uint8_t)(kind ? kind << 4 | (e->fx & 15 ? e->fx & 15 : 2) : 0);
            commit(run + 2);
        } else if ((key == 'n' || key == 'm') && in && e->fx) {
            e->fx = (uint8_t)((e->fx & 0xF0) | within((e->fx & 15) + (key == 'm' ? 1 : -1), 1, 15));
            commit(run + 3);
        } else if ((key == 0x08 || key == 0x7F || key == HID_KEY_DEL)) {
            if (in) {
                memmove(e, e + 1, (size_t)(s->steps - sfx_step - 1) * sizeof *e);
                s->steps--;
                commit(0);
            } else if (sfx_step)
                sfx_step--;
        } else
            return 0;
    }
    return 1;
}

static int wave_key(int key, int mods)
{
    if (key == 'a') {
        const int k = sdk_music_add_wave(&mus, mus.waves ? wave_sel : -1);
        if (k < 0)
            snprintf(sdk_note, sizeof sdk_note, "%d waves at most", SDK_MUS_DRAWN);
        else {
            wave_sel = k;
            commit(0);
            snprintf(sdk_note, sizeof sdk_note, "New wave: choose it in Instruments or SFX");
        }
        return 1;
    }
    if (!mus.waves)
        return 0;
    sdk_mus_wave_t *w = &mus.drawn[wave_sel];
    const int d = key == HID_KEY_UP || key == '=' || key == '+' ? 1 :
                  key == HID_KEY_DOWN || key == '-' || key == '_' ? -1 : 0;
    const int run = 200000 + wave_sel * 64;
    const int note = piano(key);
    if (key == HID_KEY_PGUP || key == HID_KEY_PGDN)
        wave_sel = within(wave_sel + (key == HID_KEY_PGDN ? 1 : -1), 0, mus.waves - 1);
    else if (key == HID_KEY_DEL) {
        if (confirmed("wave")) {
            sdk_music_remove_wave(&mus, wave_sel);
            settle();
            commit(0);
        }
    } else if (note >= 0) {
        last_note[0] = note;
        hear_wave(note);
    } else if (key == '\r' || key == '\n')
        start_name(w->name);
    else if (key == HID_KEY_LEFT || key == HID_KEY_RIGHT) {
        const int to = within(wave_at + (key == HID_KEY_RIGHT ? 1 : -1), 0, SDK_MUS_POINTS - 1);
        const int drawing = (mods & SDK_MOD_SHIFT) && to != wave_at;    /* the point takes the value along */
        if (drawing)
            w->point[to] = w->point[wave_at];
        wave_at = to;
        if (drawing) {
            commit(run + 32);                   /* a stroke is one step of the undo */
            try_wave();
        }
    } else if (key == HID_KEY_HOME || key == HID_KEY_END)
        wave_at = key == HID_KEY_HOME ? 0 : SDK_MUS_POINTS - 1;
    else if (d) {
        const int by = key == '+' || key == '_' ? 4 : 1;
        w->point[wave_at] = (uint8_t)within(w->point[wave_at] + d * by, 0, 15);
        commit(run + wave_at);
        try_wave();
    } else if (key == 's' || key == 'm' || key == 'f') {
        if (key == 's') {                       /* a wave of the driver as the start */
            for (int i = 0; i < SDK_MUS_POINTS; i++)
                w->point[i] = (uint8_t)sdk_music_wave_point(&mus, shape_next, i);
            snprintf(sdk_note, sizeof sdk_note, "Shape: %s", wave_names[shape_next]);
            shape_next = (shape_next + 1) % SDK_MUS_WAVES;
        } else if (key == 'm') {
            for (int i = 0; i < SDK_MUS_POINTS / 2; i++) {
                const uint8_t a = w->point[i];
                w->point[i] = w->point[SDK_MUS_POINTS - 1 - i];
                w->point[SDK_MUS_POINTS - 1 - i] = a;
            }
        } else
            for (int i = 0; i < SDK_MUS_POINTS; i++)
                w->point[i] = (uint8_t)(15 - w->point[i]);
        commit(0);
        try_wave();
    } else
        return 0;
    return 1;
}

static int song_key(int key)
{
    if (key == 'a') {
        if (mus.songs == SDK_MUS_SONGS) {
            snprintf(sdk_note, sizeof sdk_note, "%d songs at most", SDK_MUS_SONGS);
            return 1;
        }
        sdk_mus_song_t *s = &mus.song[mus.songs];
        memset(s, 0, sizeof *s);
        free_name(s->name, "title", 1);
        s->speed = (uint8_t)preview_speed;
        s->loop = 0;
        s->len = 1;
        s->order[0] = (uint8_t)pat;
        use_pattern();
        song_sel = mus.songs++;
        place = 0;
        song_order = 1;
        commit(0);
        snprintf(sdk_note, sizeof sdk_note, "In the program: song_%s", mus.song[song_sel].name);
        return 1;
    }
    if (!mus.songs)
        return 0;
    sdk_mus_song_t *s = &mus.song[song_sel];
    const int d = key == '=' || key == '+' ? 1 : key == '-' || key == '_' ? -1 : 0;
    const int run = 5000 + song_sel * 100;
    if (key == HID_KEY_PGUP || key == HID_KEY_PGDN) {
        song_sel = within(song_sel + (key == HID_KEY_PGDN ? 1 : -1), 0, mus.songs - 1);
        place = 0;
    } else if (!song_order) {
        if (key == HID_KEY_UP)
            song_field = within(song_field - 1, 0, 2);
        else if (key == HID_KEY_DOWN) {
            if (song_field == 2)
                song_order = 1;
            else
                song_field++;
        } else if (key == HID_KEY_LEFT || key == HID_KEY_RIGHT)
            song_order = 1;
        else if (key == HID_KEY_DEL) {
            if (confirmed("song")) {
                memmove(s, s + 1, (size_t)(mus.songs - song_sel - 1) * sizeof *s);
                mus.songs--;
                settle();
                commit(0);
            }
        } else if ((key == '\r' || key == '\n') && song_field == 0)
            start_name(s->name);
        else if (d && song_field == 1) {
            s->speed = (uint8_t)within(s->speed + d, 1, 60);
            commit(run + 1);
        } else if (d && song_field == 2) {
            int v = (s->loop == SDK_MUS_NO ? -1 : s->loop) + d;
            v = within(v, -1, s->len - 1);
            s->loop = (uint8_t)(v < 0 ? SDK_MUS_NO : v);
            commit(run + 2);
        } else
            return 0;
    } else {
        const int in = place < s->len;
        if (key == HID_KEY_UP)
            song_order = 0;
        else if (key == HID_KEY_LEFT)
            place = within(place - 1, 0, s->len);
        else if (key == HID_KEY_RIGHT)
            place = within(place + 1, 0, within(s->len, 0, SDK_MUS_ORDER - 1));
        else if (key == '\r' || key == '\n') {
            if (in) {
                pat = s->order[place];
                page = PAGE_PATTERN;
                step = 0;
                settle();
            } else if (s->len < SDK_MUS_ORDER) {
                s->order[s->len] = s->order[s->len - 1];
                s->len++;
                commit(0);
            }
        } else if (d && in) {
            const int by = key == '+' || key == '_' ? 4 : 1;
            s->order[place] = (uint8_t)within(s->order[place] + d * by, 0, SDK_MUS_PATTERNS - 1);
            commit(run + 10 + place);
        } else if (key == 'i' && in && s->len < SDK_MUS_ORDER) {
            memmove(&s->order[place + 1], &s->order[place], (size_t)(s->len - place));
            s->len++;
            if (s->loop != SDK_MUS_NO && s->loop > place)
                s->loop++;
            place++;
            commit(0);
        } else if ((key == 0x08 || key == 0x7F || key == HID_KEY_DEL) && in && s->len > 1) {
            memmove(&s->order[place], &s->order[place + 1], (size_t)(s->len - place - 1));
            s->len--;
            if (s->loop != SDK_MUS_NO && (s->loop > place || s->loop >= s->len))
                s->loop = s->loop ? (uint8_t)(s->loop - 1) : 0;
            commit(0);
        } else if (key == '[' && in) {
            s->loop = (uint8_t)(s->loop == place ? SDK_MUS_NO : place);
            commit(0);
        } else
            return 0;
    }
    return 1;
}

int sdk_sounds_key(int key, int mods)
{
    if (!supported)
        return 0;
    sdk_note[0] = 0;
    if (typing)
        return name_key(key);
    if (key != HID_KEY_DEL)
        armed = 0;
    if (key == 0x1A || key == 0x19) {                  /* Ctrl+Z, Ctrl+Y: the history of the code */
        const int did = key == 0x1A && !(mods & SDK_MOD_SHIFT) ? sdk_edit_undo() : sdk_edit_redo();
        if (did)
            read_code();
        snprintf(sdk_note, sizeof sdk_note, did ? "Done" : "Nothing to undo or redo");
        return 1;
    }
    if (key == '\t') {
        page = (page + 1) % pages();
        return 1;
    }
    if (broken[0])
        return 0;
    if (key == 'z' || key == 'x') {
        octave = within(octave + (key == 'x' ? 1 : -1), 1, 6);
        return 1;
    }
    if (key == ' ') {
        if (sdk_play_what() != SDK_PLAY_NOTHING && sdk_play_what() != SDK_PLAY_NOTE)
            sdk_play_stop();
        else if (page == PAGE_PATTERN)
            play(SDK_PLAY_PATTERN, pat, 0);
        else if (page == PAGE_SONG && mus.songs)
            play(SDK_PLAY_SONG, song_sel, 0);
        else if (page == PAGE_SFX && mus.sfxs)
            play(SDK_PLAY_SFX, sfx_sel, 0);
        else if (page == PAGE_INS && mus.instruments)
            play(SDK_PLAY_NOTE, ins_sel, last_note[mus.ins[ins_sel].channel]);
        else if (page == PAGE_WAVE && mus.waves)
            hear_wave(last_note[0]);
        return 1;
    }
    if (page == PAGE_PATTERN)
        return pattern_key(key, mods);
    if (page == PAGE_INS)
        return instruments_key(key);
    if (page == PAGE_SFX)
        return sfx_key(key);
    if (page == PAGE_WAVE)
        return wave_key(key, mods);
    return song_key(key);
}
