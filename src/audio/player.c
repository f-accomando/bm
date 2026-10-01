#include "player.h"

#include <math.h>
#include <string.h>

#define TWO_PI      6.2831853f
#define FX_RATE_HZ  6.0f            /* vibrato and tremolo of the effects */
#define CHORD_HZ    60.0f           /* notes per second of AU_FX_CHORD */
#define MIN_STEP    16.0f           /* samples: a step is never shorter */

const int8_t au_chord[AU_CHORDS][4] = {
    { 0, 12 },          /* octave */
    { 0, 4, 7 },        /* major */
    { 0, 3, 7 },        /* minor */
    { 0, 2, 7 },        /* sus2 */
    { 0, 5, 7 },        /* sus4 */
    { 0, 4, 7, 11 },    /* major 7th */
    { 0, 3, 7, 10 },    /* minor 7th */
    { 0, 4, 7, 10 },    /* dominant 7th */
    { 0, 3, 6 },        /* diminished */
    { 0, 4, 8 },        /* augmented */
    { 0, 7 },           /* power chord */
    { 0, 7, 12 },       /* power + octave */
    { 0, 4, 7, 12 },    /* major + octave */
    { 0, 3, 7, 12 },    /* minor + octave */
    { 0, -12 },         /* octave down */
    { 0, 12, 24 },      /* two octaves */
};
const uint8_t au_chord_len[AU_CHORDS] = { 2, 3, 3, 3, 3, 4, 4, 4, 3, 3, 2, 3, 4, 4, 2, 3 };

/* the sound of a note whose sound is not in the bank */
static const au_sound_t default_sound = {
    "", SYNTH_SQUARE, 128, 200, 1, 0, 255, 10, 0, 0, 0, 0, 0
};

/* ---------------------------------------------------------------- parse */

static int fail(char *err, size_t n, const char *msg)
{
    if (err && n) {
        strncpy(err, msg, n - 1);
        err[n - 1] = 0;
    }
    return -1;
}

static void get_name(char *dst, const uint8_t *src)
{
    int i;
    for (i = 0; i < 8 && src[i]; i++)
        dst[i] = src[i] >= 32 && src[i] < 127 ? (char)src[i] : '?';
    dst[i] = 0;
}

static void get_step(au_step_t *s, const uint8_t *p)
{
    s->note = p[0];
    s->sound = p[1];
    s->vol = p[2];
    s->fx = p[3];
}

int au_parse(const uint8_t *d, size_t len, au_bank_t *b, char *err, size_t errlen)
{
    memset(b, 0, sizeof *b);
    if (len < 16 || memcmp(d, AU_MAGIC, 4) != 0)
        return fail(err, errlen, "not a sound bank");
    if (d[4] != AU_VERSION)
        return fail(err, errlen, "unsupported sound bank version");
    if (d[5] > AU_SOUNDS || d[6] > AU_SFX || d[7] > AU_PATTERNS || d[8] > AU_SONGS)
        return fail(err, errlen, "too many items in the sound bank");
    b->nsounds = d[5];
    b->nsfx = d[6];
    b->npatterns = d[7];
    b->nsongs = d[8];
    size_t off = 16;

    for (int i = 0; i < b->nsounds; i++, off += 24) {
        if (off + 24 > len)
            return fail(err, errlen, "sound bank truncated (sounds)");
        const uint8_t *p = d + off;
        au_sound_t *s = &b->sound[i];
        get_name(s->name, p);
        s->wave = p[8] < SYNTH_WAVES ? p[8] : SYNTH_SQUARE;
        s->duty = p[9];
        s->vol = p[10];
        s->attack = p[11];
        s->decay = p[12];
        s->sustain = p[13];
        s->release = p[14];
        s->pitch = (int8_t)p[15];
        s->pitch_time = p[16];
        s->vib_depth = p[17];
        s->vib_rate = p[18];
        s->detune = (int8_t)p[19];
    }
    for (int i = 0; i < b->nsfx; i++) {
        if (off + 16 > len)
            return fail(err, errlen, "sound bank truncated (sfx)");
        const uint8_t *p = d + off;
        au_sfx_t *s = &b->sfx[i];
        get_name(s->name, p);
        s->ms = (uint16_t)(p[8] | p[9] << 8);
        s->len = p[10];
        s->loop_start = p[11];
        s->loop_end = p[12];
        if (!s->ms || s->len < 1 || s->len > AU_SFX_STEPS)
            return fail(err, errlen, "bad sound effect in the bank");
        if (s->loop_end > s->len || s->loop_start >= s->loop_end)
            s->loop_start = s->loop_end = 0;
        off += 16;
        if (off + 4u * s->len > len)
            return fail(err, errlen, "sound bank truncated (sfx steps)");
        for (int k = 0; k < s->len; k++)
            get_step(&s->step[k], d + off + 4 * k);
        off += 4u * s->len;
    }
    for (int i = 0; i < b->npatterns; i++) {
        if (off + 4 > len)
            return fail(err, errlen, "sound bank truncated (patterns)");
        au_pattern_t *pt = &b->pat[i];
        pt->len = d[off];
        uint8_t mask = d[off + 1];
        if (pt->len < 1 || pt->len > AU_PAT_STEPS)
            return fail(err, errlen, "bad pattern in the bank");
        off += 4;
        for (int t = 0; t < AU_TRACKS; t++) {
            if (!(mask >> t & 1))
                continue;
            if (off + 4u * pt->len > len)
                return fail(err, errlen, "sound bank truncated (pattern steps)");
            for (int k = 0; k < pt->len; k++)
                get_step(&pt->step[t][k], d + off + 4 * k);
            off += 4u * pt->len;
        }
    }
    for (int i = 0; i < b->nsongs; i++) {
        if (off + 16 > len)
            return fail(err, errlen, "sound bank truncated (songs)");
        const uint8_t *p = d + off;
        au_song_t *s = &b->song[i];
        get_name(s->name, p);
        s->bpm = p[8];
        s->swing = p[9] > 100 ? 100 : p[9];
        s->len = p[10];
        s->loop = p[11];
        if (s->len < 1 || s->len > AU_SONG_LEN)
            return fail(err, errlen, "bad song in the bank");
        if (s->loop >= s->len)
            s->loop = AU_NO_LOOP;
        off += 16;
        if (off + s->len > len)
            return fail(err, errlen, "sound bank truncated (song)");
        memcpy(s->order, d + off, s->len);
        off += s->len;
        /* the tracks the song plays: the voices it leaves to the effects */
        for (int k = 0; k < s->len; k++) {
            if (s->order[k] >= b->npatterns)
                continue;
            const au_pattern_t *pt = &b->pat[s->order[k]];
            for (int t = 0; t < AU_TRACKS; t++)
                for (int j = 0; j < pt->len && !(s->tracks >> t & 1); j++)
                    if (pt->step[t][j].note)
                        s->tracks |= (uint8_t)(1u << t);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- pitch */

float au_note_hz(float note)
{
    return 440.0f * exp2f((note - 69.0f) * (1.0f / 12.0f));
}

float au_hz_note(float hz)
{
    return hz > 0.0f ? 69.0f + 12.0f * log2f(hz * (1.0f / 440.0f)) : 0.0f;
}

static volatile uint8_t *vr(player_t *p, int ch)
{
    return p->regs + ch * SYNTH_VOICE_BYTES;
}

static void write_hz(volatile uint8_t *r, float hz)
{
    if (hz < 0.0f)
        hz = 0.0f;
    uint32_t f = hz >= 65535.0f ? 0xFFFFFFu : (uint32_t)(hz * 256.0f + 0.5f);
    if (f > 0xFFFFFFu)
        f = 0xFFFFFFu;
    r[SYNTH_FREQ_LO] = (uint8_t)(f >> 8);
    r[SYNTH_FREQ_HI] = (uint8_t)(f >> 16);
    r[SYNTH_FREQ_FRAC] = (uint8_t)f;
}

static const au_sound_t *sound_of(const player_t *p, int s)
{
    if (p->bank && s >= 0 && s < p->bank->nsounds)
        return &p->bank->sound[s];
    return &default_sound;
}

static int idle(const player_t *p, int ch)
{
    return p->synth->v[ch].stage == SYNTH_IDLE;
}

static void gate_off(player_t *p, int ch)
{
    vr(p, ch)[SYNTH_CONTROL] &= (uint8_t)~SYNTH_GATE;
}

/* ---------------------------------------------------------------- notes */

static void clear_fx(au_voice_t *v)
{
    v->fx = v->fxn = 0;
    v->glide = 0;
    v->glide_len = v->cut_at = v->retrig_every = v->retrig_t = 0;
    v->arp_i = 0;
}

/* the pitch offset of the effect, in semitones */
static float fx_pitch(const player_t *p, const au_voice_t *v)
{
    float off = 0.0f, t = (float)v->fx_t, len = v->step_len ? (float)v->step_len : 1.0f;
    if (v->glide_len && v->fx_t < v->glide_len)
        off += v->glide * (1.0f - t / (float)v->glide_len);
    switch (v->fx) {
    case AU_FX_BEND_UP:
        off += v->fxn * (t < len ? t / len : 1.0f);
        break;
    case AU_FX_BEND_DOWN:
        off -= v->fxn * (t < len ? t / len : 1.0f);
        break;
    case AU_FX_VIBRATO:
        off += v->fxn * 0.125f * sinf(TWO_PI * FX_RATE_HZ * t / (float)p->rate);
        break;
    case AU_FX_CHORD: {
        unsigned k = (unsigned)(t * CHORD_HZ / (float)p->rate) % au_chord_len[v->fxn];
        off += au_chord[v->fxn][k];
        break;
    }
    case AU_FX_ARP:
        off += au_chord[v->fxn][v->arp_i % au_chord_len[v->fxn]];
        break;
    }
    return off;
}

/* the volume factor of the effect, 0..1 */
static float fx_level(const player_t *p, const au_voice_t *v)
{
    float t = (float)v->fx_t, len = (float)(v->step_len ? v->step_len : 1) * (v->fxn + 1);
    switch (v->fx) {
    case AU_FX_FADE_OUT:
        return t < len ? 1.0f - t / len : 0.0f;
    case AU_FX_FADE_IN:
        return t < len ? t / len : 1.0f;
    case AU_FX_TREMOLO:
        return 1.0f - v->fxn / 15.0f * (0.5f - 0.5f * cosf(TWO_PI * FX_RATE_HZ * t / (float)p->rate));
    }
    return 1.0f;
}

/* the pitch and volume of a voice the sequencer drives, into its registers */
static void update_voice(player_t *p, int ch)
{
    au_voice_t *v = &p->v[ch];
    const au_sound_t *s = sound_of(p, v->sound);
    volatile uint8_t *r = vr(p, ch);
    float semi = v->note + s->detune * 0.01f + fx_pitch(p, v);
    float t = (float)v->t;
    if (s->pitch && s->pitch_time) {
        float len = s->pitch_time * 0.01f * (float)p->rate;
        if (t < len) {
            float k = 1.0f - t / len;
            semi += s->pitch * k * k;
        }
    }
    if (s->vib_depth && s->vib_rate)
        semi += s->vib_depth * 0.01f * sinf(TWO_PI * s->vib_rate * 0.1f * t / (float)p->rate);
    write_hz(r, au_note_hz(semi));
    float vol = s->vol * v->vol * fx_level(p, v);
    if (v->owner == AU_OWN_MUSIC)
        vol *= p->m.level;
    r[SYNTH_VOLUME] = (uint8_t)(vol < 0.0f ? 0 : vol > 255.0f ? 255 : vol + 0.5f);
}

/* an effect starts on a note (with the note, or later on an empty step) */
static void set_fx(au_voice_t *v, uint8_t fx, uint32_t step_len, float prev_note, int glide_ok)
{
    /* what a bend or a fade has done so far stays */
    float t = (float)v->fx_t, len = v->step_len ? (float)v->step_len : 1.0f;
    if (v->fx == AU_FX_BEND_UP || v->fx == AU_FX_BEND_DOWN)
        v->note += (v->fx == AU_FX_BEND_UP ? 1 : -1) * v->fxn * (t < len ? t / len : 1.0f);
    if (v->fx == AU_FX_FADE_OUT || v->fx == AU_FX_FADE_IN) {
        float flen = len * (v->fxn + 1), k = t < flen ? t / flen : 1.0f;
        v->vol *= v->fx == AU_FX_FADE_OUT ? 1.0f - k : k;
    }
    clear_fx(v);
    v->fx = fx >> 4;
    v->fxn = fx & 15;
    v->fx_t = 0;
    v->step_len = step_len;
    switch (v->fx) {
    case AU_FX_GLIDE:
        if (glide_ok) {
            v->glide = prev_note - v->note;
            v->glide_len = step_len * (v->fxn + 1u) / 4u;
        }
        break;
    case AU_FX_CUT:
        v->cut_at = step_len * (v->fxn + 1u) / 16u;
        break;
    case AU_FX_RETRIG:
        if (v->fxn)
            v->retrig_every = step_len / (v->fxn + 1u);
        break;
    case AU_FX_CHORD:
    case AU_FX_ARP:
        if (v->fxn >= AU_CHORDS)
            v->fx = 0;
        break;
    case AU_FX_FADE_IN:
        break;
    }
    if (v->fx >= AU_FX_COUNT)
        v->fx = 0;
}

static void start_note(player_t *p, int ch, const au_step_t *e, float vol_scale, uint32_t step_len,
                       uint8_t owner)
{
    au_voice_t *v = &p->v[ch];
    volatile uint8_t *r = vr(p, ch);
    int sounding = v->owner == owner && !idle(p, ch) && v->sound >= 0;
    float prev = v->note;
    v->owner = owner;
    v->sound = (int8_t)(e->sound < AU_SOUNDS ? e->sound : 0);
    v->note = e->note;
    v->vol = e->vol * (1.0f / 255.0f) * vol_scale;
    v->t = 0;
    v->fx = 0;
    v->gate_left = 0;
    v->delay_left = 0;
    v->mods = 0;
    set_fx(v, e->fx, step_len, prev, sounding);
    const au_sound_t *s = sound_of(p, v->sound);
    r[SYNTH_WAVEFORM] = s->wave;
    r[SYNTH_DUTY] = s->duty;
    r[SYNTH_ATTACK] = s->attack;
    r[SYNTH_DECAY] = s->decay;
    r[SYNTH_SUSTAIN] = s->sustain;
    r[SYNTH_RELEASE] = s->release;
    update_voice(p, ch);
    r[SYNTH_CONTROL] |= SYNTH_GATE;
    synth_retrigger(p->synth, (unsigned)ch);
}

/* One step of a sequence on voice ch. */
static void do_event(player_t *p, int ch, const au_step_t *e, int transpose, float vol_scale,
                     uint32_t step_len, uint8_t owner)
{
    au_voice_t *v = &p->v[ch];
    if (e->note == 0) {
        if (v->owner != owner)
            return;
        if (e->fx >> 4)
            set_fx(v, e->fx, step_len, v->note, 0);
        else if (v->fx == AU_FX_ARP)
            v->arp_i++;
        return;
    }
    if (e->note >= AU_NOTE_OFF) {
        if (v->owner == owner) {
            gate_off(p, ch);
            v->delay_left = 0;
        }
        return;
    }
    au_step_t s = *e;
    int n = e->note + transpose;
    s.note = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n);
    if ((e->fx >> 4) == AU_FX_DELAY && (e->fx & 15)) {
        v->owner = owner;
        v->pending = s;
        v->pending.fx = 0;
        v->vol = vol_scale;             /* kept for the note */
        v->delay_left = step_len * (e->fx & 15u) / 16u;
        v->step_len = step_len;
        return;
    }
    start_note(p, ch, &s, vol_scale, step_len, owner);
}

/* ---------------------------------------------------------------- music */

static int music_on(const player_t *p)
{
    return p->bank && p->m.song != -1;
}

static void music_release(player_t *p)
{
    for (int ch = 0; ch < AU_TRACKS; ch++)
        if (p->v[ch].owner == AU_OWN_MUSIC) {
            gate_off(p, ch);
            p->v[ch].delay_left = 0;
        }
}

static void music_end(player_t *p)
{
    music_release(p);
    p->m.song = -1;
    p->m.fade = 0;
}

static const au_pattern_t *music_pattern_now(const player_t *p, int *pidx)
{
    int i;
    if (p->m.song == -2)
        i = p->m.pat;
    else
        i = p->bank->song[p->m.song].order[p->m.order];
    if (pidx)
        *pidx = i;
    return i < p->bank->npatterns ? &p->bank->pat[i] : NULL;
}

static float music_step_len(const player_t *p, int step)
{
    int bpm, swing;
    if (p->m.song == -2) {
        bpm = p->m.bpm;
        swing = p->m.swing;
    } else {
        bpm = p->bank->song[p->m.song].bpm;
        swing = p->bank->song[p->m.song].swing;
    }
    if (bpm < 20)
        bpm = 20;
    float tempo = p->m.tempo > 0.05f ? p->m.tempo : 0.05f;
    float base = (float)p->rate * 60.0f / ((float)bpm * 4.0f) / tempo;
    float sw = swing * (1.0f / 200.0f);
    float len = base * (step & 1 ? 1.0f - sw : 1.0f + sw);
    return len < MIN_STEP ? MIN_STEP : len;
}

static void music_do_step(player_t *p)
{
    if (p->m.song >= 0 && p->m.order >= p->bank->song[p->m.song].len) {
        music_end(p);                   /* the last step of a song that stops */
        return;
    }
    int pidx;
    const au_pattern_t *pt = music_pattern_now(p, &pidx);
    int plen = pt ? pt->len : 16;
    if (p->m.step >= plen)
        p->m.step = 0;
    p->m.cur_order = p->m.order;
    p->m.cur_step = p->m.step;
    p->m.cur_pat = (uint8_t)pidx;
    float len = music_step_len(p, p->m.step);
    if (pt)
        for (int t = 0; t < AU_TRACKS; t++) {
            uint8_t own = p->v[t].owner;
            if ((p->m.mute >> t & 1) || own == AU_OWN_SFX || own == AU_OWN_LUA)
                continue;
            do_event(p, t, &pt->step[t][p->m.step], 0, 1.0f, (uint32_t)len, AU_OWN_MUSIC);
        }
    p->m.left += len;
    if (++p->m.step >= plen) {
        p->m.step = 0;
        if (p->m.song >= 0) {
            const au_song_t *s = &p->bank->song[p->m.song];
            if (++p->m.order >= s->len && s->loop != AU_NO_LOOP)
                p->m.order = s->loop;
        }
    }
}

/* ---------------------------------------------------------------- effects */

static void sfx_do_step(player_t *p, int ch)
{
    au_sfxch_t *c = &p->sfx[ch];
    if (!p->bank || c->n >= p->bank->nsfx) {
        c->n = -1;
        return;
    }
    const au_sfx_t *s = &p->bank->sfx[c->n];
    if (s->loop_end > s->loop_start && c->step >= s->loop_end)
        c->step = s->loop_start;
    if (c->step >= s->len) {
        if (p->v[ch].owner == AU_OWN_SFX)
            gate_off(p, ch);
        c->n = -1;
        return;
    }
    float len = (float)s->ms * (float)p->rate * 0.001f;
    if (len < MIN_STEP)
        len = MIN_STEP;
    do_event(p, ch, &s->step[c->step], c->transpose, c->vol, (uint32_t)len, AU_OWN_SFX);
    c->step++;
    c->left += len;
}

/* ---------------------------------------------------------------- voices */

static float lua_pitch(au_voice_t *v, uint32_t rate)
{
    float semi = v->note;
    if (v->mods & 1) {
        if (v->mod_t < v->mod_glide_len)
            semi += v->mod_glide * (1.0f - (float)v->mod_t / (float)v->mod_glide_len);
        else
            v->mods &= (uint8_t)~1;
    }
    if (v->mods & 2)
        semi += v->mod_vib_depth * sinf(TWO_PI * v->mod_vib_rate * (float)v->t / (float)rate);
    if ((v->mods & 4) && v->mod_arp_n)
        semi += v->mod_arp[(v->t / (v->mod_arp_len ? v->mod_arp_len : 1)) % v->mod_arp_n];
    return semi;
}

static void voice_tick(player_t *p, int ch, unsigned n)
{
    au_voice_t *v = &p->v[ch];
    volatile uint8_t *r = vr(p, ch);
    if (v->owner == AU_OWN_NONE)
        return;
    if (v->gate_left) {
        if (v->gate_left <= n) {
            v->gate_left = 0;
            gate_off(p, ch);
        } else {
            v->gate_left -= n;
        }
    }
    if (v->owner == AU_OWN_LUA) {
        if (v->mods) {
            write_hz(r, au_note_hz(lua_pitch(v, p->rate)));
            v->mod_t += n;
        }
        v->t += n;
        if (!(r[SYNTH_CONTROL] & SYNTH_GATE) && idle(p, ch) && !v->gate_left) {
            v->owner = AU_OWN_NONE;
            v->mods = 0;
        }
        return;
    }
    if (v->delay_left) {
        if (v->delay_left <= n) {
            v->delay_left = 0;
            au_step_t e = v->pending;
            start_note(p, ch, &e, v->vol, v->step_len, v->owner);
        } else {
            v->delay_left -= n;
            return;
        }
    }
    if (v->cut_at && v->fx_t >= v->cut_at) {
        v->cut_at = 0;
        gate_off(p, ch);
    }
    if (v->retrig_every) {
        v->retrig_t += n;
        if (v->retrig_t >= v->retrig_every && v->fx_t < v->step_len) {
            v->retrig_t -= v->retrig_every;
            r[SYNTH_CONTROL] |= SYNTH_GATE;
            synth_retrigger(p->synth, (unsigned)ch);
        }
    }
    if (v->sound >= 0)
        update_voice(p, ch);
    v->t += n;
    v->fx_t += n;
    int gated = r[SYNTH_CONTROL] & SYNTH_GATE;
    if (!gated && idle(p, ch) && !v->delay_left) {
        if ((v->owner == AU_OWN_SFX && p->sfx[ch].n < 0) || (v->owner == AU_OWN_MUSIC && !music_on(p))) {
            v->owner = AU_OWN_NONE;
            clear_fx(v);
        }
    }
}

/* ---------------------------------------------------------------- player */

void player_init(player_t *p, uint32_t rate, volatile uint8_t *regs, synth_t *synth)
{
    memset(p, 0, sizeof *p);
    p->rate = rate;
    p->regs = regs;
    p->synth = synth;
    for (int ch = 0; ch < AU_TRACKS; ch++) {
        p->sfx[ch].n = -1;
        p->v[ch].sound = -1;
    }
    p->m.song = -1;
    p->m.tempo = 1.0f;
    p->m.level = 1.0f;
}

void player_set_bank(player_t *p, au_bank_t *b)
{
    if (!b) {
        player_sfx_stop(p, -1);
        if (p->bank)
            music_end(p);
        p->bank = NULL;
        p->m.song = -1;
        return;
    }
    p->bank = b;
    for (int ch = 0; ch < AU_TRACKS; ch++)
        if (p->sfx[ch].n >= b->nsfx)
            player_sfx_stop(p, ch);
    if (p->m.song >= 0 && p->m.song >= b->nsongs)
        music_end(p);
    else if (p->m.song >= 0 && p->m.order >= b->song[p->m.song].len)
        p->m.order = 0;
}

void player_stop_all(player_t *p)
{
    for (int ch = 0; ch < AU_TRACKS; ch++) {
        p->sfx[ch].n = -1;
        au_voice_t *v = &p->v[ch];
        v->owner = AU_OWN_NONE;
        v->sound = -1;
        v->gate_left = v->delay_left = 0;
        v->mods = 0;
        clear_fx(v);
    }
    p->m.song = -1;
    p->m.paused = 0;
    p->m.mute = 0;
    p->m.tempo = 1.0f;
    p->m.level = 1.0f;
    p->m.fade = 0;
}

void player_advance(player_t *p, unsigned n)
{
    for (int ch = 0; ch < AU_TRACKS; ch++) {
        au_sfxch_t *c = &p->sfx[ch];
        if (c->n < 0)
            continue;
        while (c->n >= 0 && c->left <= 0.0f)
            sfx_do_step(p, ch);
        c->left -= (float)n;
    }
    if (music_on(p) && !p->m.paused) {
        while (music_on(p) && p->m.left <= 0.0f)
            music_do_step(p);
        p->m.left -= (float)n;
        if (p->m.fade != 0.0f) {
            p->m.level += p->m.fade * (float)n;
            if (p->m.level >= 1.0f) {
                p->m.level = 1.0f;
                p->m.fade = 0;
            } else if (p->m.level <= 0.0f) {
                p->m.level = 0.0f;
                music_end(p);
            }
        }
    }
    for (int ch = 0; ch < AU_TRACKS; ch++)
        voice_tick(p, ch, n);
}

/* ---------------------------------------------------------------- API */

static int pick_voice(const player_t *p)
{
    uint8_t song = 0;
    if (music_on(p)) {
        if (p->m.song >= 0) {
            song = p->bank->song[p->m.song].tracks;
        } else if (p->m.pat < p->bank->npatterns) {
            const au_pattern_t *pt = &p->bank->pat[p->m.pat];
            for (int t = 0; t < AU_TRACKS; t++)
                for (int j = 0; j < pt->len; j++)
                    if (pt->step[t][j].note) { song |= (uint8_t)(1u << t); break; }
        }
        song &= (uint8_t)~p->m.mute;
    }
    for (int ch = AU_TRACKS - 1; ch >= 0; ch--)          /* silent, not the song's */
        if (p->v[ch].owner == AU_OWN_NONE && idle(p, ch) && !(song >> ch & 1))
            return ch;
    for (int ch = AU_TRACKS - 1; ch >= 0; ch--)          /* a fading tail, not the song's */
        if (p->v[ch].owner == AU_OWN_NONE && !(song >> ch & 1))
            return ch;
    int best = -1;
    for (int ch = 0; ch < AU_TRACKS; ch++)               /* the oldest effect */
        if (p->v[ch].owner == AU_OWN_SFX && (best < 0 || p->sfx[ch].serial < p->sfx[best].serial))
            best = ch;
    if (best >= 0)
        return best;
    for (int ch = AU_TRACKS - 1; ch >= 0; ch--)          /* a voice the song is not using now */
        if (p->v[ch].owner == AU_OWN_NONE)
            return ch;
    for (int ch = AU_TRACKS - 1; ch >= 0; ch--)          /* the song's highest track */
        if (p->v[ch].owner == AU_OWN_MUSIC)
            return ch;
    return -1;
}

int player_sfx(player_t *p, int n, int voice, int transpose, float vol)
{
    if (!p->bank || n < 0 || n >= p->bank->nsfx)
        return -1;
    if (voice < 0)
        voice = pick_voice(p);
    if (voice < 0 || voice >= AU_TRACKS)
        return -1;
    au_sfxch_t *c = &p->sfx[voice];
    c->n = (int16_t)n;
    c->step = 0;
    c->transpose = (int8_t)(transpose < -96 ? -96 : transpose > 96 ? 96 : transpose);
    c->vol = vol < 0.0f ? 0.0f : vol > 1.0f ? 1.0f : vol;
    c->left = 0;
    c->serial = ++p->serial;
    au_voice_t *v = &p->v[voice];
    if (v->owner != AU_OWN_SFX) {
        v->owner = AU_OWN_SFX;
        v->delay_left = 0;
    }
    v->gate_left = 0;
    v->mods = 0;
    return voice;
}

void player_sfx_stop(player_t *p, int voice)
{
    for (int ch = 0; ch < AU_TRACKS; ch++) {
        if (voice >= 0 && ch != voice)
            continue;
        if (p->sfx[ch].n >= 0 || p->v[ch].owner == AU_OWN_SFX) {
            p->sfx[ch].n = -1;
            if (p->v[ch].owner == AU_OWN_SFX) {
                gate_off(p, ch);
                p->v[ch].delay_left = 0;
            }
        }
    }
}

int player_sfx_pos(const player_t *p, int voice, int *step)
{
    if (voice < 0 || voice >= AU_TRACKS || p->sfx[voice].n < 0)
        return -1;
    if (step)
        *step = p->sfx[voice].step ? p->sfx[voice].step - 1 : 0;
    return p->sfx[voice].n;
}

static void music_start(player_t *p, int fade_ms)
{
    p->m.step = 0;
    p->m.cur_order = p->m.order;
    p->m.cur_step = 0;
    p->m.cur_pat = 0;
    p->m.left = 0;
    p->m.paused = 0;
    if (fade_ms > 0) {
        p->m.level = 0;
        p->m.fade = 1000.0f / ((float)fade_ms * (float)p->rate);
    } else {
        p->m.level = 1.0f;
        p->m.fade = 0;
    }
}

void player_music(player_t *p, int song, int order, int fade_ms)
{
    music_release(p);
    if (!p->bank || song < 0 || song >= p->bank->nsongs) {
        p->m.song = -1;
        return;
    }
    p->m.song = (int8_t)song;
    p->m.order = (uint8_t)(order >= 0 && order < p->bank->song[song].len ? order : 0);
    music_start(p, fade_ms);
}

void player_music_pattern(player_t *p, int pat, int bpm, int swing, int step)
{
    music_release(p);
    if (!p->bank || pat < 0 || pat >= p->bank->npatterns) {
        p->m.song = -1;
        return;
    }
    p->m.song = -2;
    p->m.pat = (uint8_t)pat;
    p->m.order = 0;
    p->m.bpm = (uint8_t)(bpm < 20 ? 20 : bpm > 255 ? 255 : bpm);
    p->m.swing = (uint8_t)(swing < 0 ? 0 : swing > 100 ? 100 : swing);
    music_start(p, 0);
    if (step > 0 && step < p->bank->pat[pat].len)
        p->m.step = (uint8_t)step;
}

void player_music_stop(player_t *p, int fade_ms)
{
    if (!music_on(p))
        return;
    if (fade_ms > 0 && !p->m.paused) {
        p->m.fade = -1000.0f / ((float)fade_ms * (float)p->rate);
        return;
    }
    music_end(p);
}

int player_music_pos(const player_t *p, int *song, int *order, int *step, int *pat)
{
    if (!music_on(p))
        return 0;
    if (song) *song = p->m.song;
    if (order) *order = p->m.cur_order;
    if (step) *step = p->m.cur_step;
    if (pat) *pat = p->m.cur_pat;
    return 1;
}

void player_music_pause(player_t *p, int on)
{
    p->m.paused = (uint8_t)(on != 0);
    if (on)
        music_release(p);
}

void player_tempo(player_t *p, float scale)
{
    p->m.tempo = scale < 0.25f ? 0.25f : scale > 4.0f ? 4.0f : scale;
}

void player_mute(player_t *p, int track, int on)
{
    if (track < 0 || track >= AU_TRACKS)
        return;
    if (on) {
        p->m.mute |= (uint8_t)(1u << track);
        if (p->v[track].owner == AU_OWN_MUSIC)
            gate_off(p, track);
    } else {
        p->m.mute &= (uint8_t)~(1u << track);
    }
}

void player_play(player_t *p, int voice, int sound, int note, int vol, int fx, uint32_t ms)
{
    if (voice < 0 || voice >= AU_TRACKS || note < 1 || note > 127)
        return;
    p->sfx[voice].n = -1;
    au_step_t e = { (uint8_t)note, (uint8_t)(sound < 0 ? 0 : sound), (uint8_t)(vol < 0 ? 0 : vol > 255 ? 255 : vol),
                    (uint8_t)fx };
    uint32_t len = ms ? ms * p->rate / 1000u : p->rate / 8u;
    start_note(p, voice, &e, 1.0f, len, AU_OWN_SFX);
    p->v[voice].gate_left = ms ? ms * p->rate / 1000u + 64u : 0;
}

void player_release(player_t *p, int voice)
{
    if (voice >= 0 && voice < AU_TRACKS) {
        gate_off(p, voice);
        p->v[voice].gate_left = 0;
    }
}

void player_lua_note(player_t *p, int voice, float hz, uint32_t ms)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    au_voice_t *v = &p->v[voice];
    volatile uint8_t *r = vr(p, voice);
    p->sfx[voice].n = -1;
    v->owner = AU_OWN_LUA;
    v->sound = -1;
    clear_fx(v);
    v->delay_left = 0;
    v->mods = 0;
    v->t = 0;
    v->note = au_hz_note(hz);
    write_hz(r, hz);
    /* at least one block, or the gate would drop before the synth saw it */
    v->gate_left = ms ? ms * p->rate / 1000u + 64u : 0;
    r[SYNTH_CONTROL] |= SYNTH_GATE;
    synth_retrigger(p->synth, (unsigned)voice);
}

void player_lua_off(player_t *p, int voice)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    if (p->v[voice].owner == AU_OWN_SFX)
        p->sfx[voice].n = -1;
    gate_off(p, voice);
    p->v[voice].gate_left = 0;
    p->v[voice].delay_left = 0;
}

void player_lua_freq(player_t *p, int voice, float hz)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    au_voice_t *v = &p->v[voice];
    v->note = au_hz_note(hz);
    v->mods &= (uint8_t)~1;             /* a new pitch ends a slide */
    if (!v->mods)
        write_hz(vr(p, voice), hz);
}

static float current_semi(player_t *p, int voice)
{
    au_voice_t *v = &p->v[voice];
    return v->owner == AU_OWN_LUA ? lua_pitch(v, p->rate) : v->note;
}

void player_slide(player_t *p, int voice, float hz, uint32_t ms)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    au_voice_t *v = &p->v[voice];
    float from = current_semi(p, voice), to = au_hz_note(hz);
    v->note = to;
    if (v->owner != AU_OWN_LUA || !ms) {
        if (v->owner == AU_OWN_LUA || v->owner == AU_OWN_NONE)
            write_hz(vr(p, voice), hz);
        v->mods &= (uint8_t)~1;
        return;
    }
    v->mod_glide = from - to;
    v->mod_glide_len = ms * p->rate / 1000u;
    v->mod_t = 0;
    v->mods |= 1;
}

void player_vibrato(player_t *p, int voice, float semitones, float rate_hz)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    au_voice_t *v = &p->v[voice];
    v->mod_vib_depth = semitones;
    v->mod_vib_rate = rate_hz;
    if (semitones != 0.0f && rate_hz > 0.0f) {
        v->mods |= 2;
    } else if (v->mods & 2) {
        v->mods &= (uint8_t)~2;
        if (!v->mods && v->owner == AU_OWN_LUA)
            write_hz(vr(p, voice), au_note_hz(v->note));
    }
}

void player_arp(player_t *p, int voice, const int8_t *semis, int n, uint32_t ms)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return;
    au_voice_t *v = &p->v[voice];
    if (n > 8)
        n = 8;
    if (n <= 0 || !ms) {
        if (v->mods & 4) {
            v->mods &= (uint8_t)~4;
            if (!v->mods && v->owner == AU_OWN_LUA)
                write_hz(vr(p, voice), au_note_hz(v->note));
        }
        return;
    }
    memcpy(v->mod_arp, semis, (size_t)n);
    v->mod_arp_n = (uint8_t)n;
    v->mod_arp_len = ms * p->rate / 1000u;
    v->mods |= 4;
}

int player_busy(const player_t *p, int voice)
{
    if (voice < 0 || voice >= AU_TRACKS)
        return 0;
    return !idle(p, voice) || (p->regs[voice * SYNTH_VOICE_BYTES + SYNTH_CONTROL] & SYNTH_GATE) ||
           p->sfx[voice].n >= 0 || p->v[voice].delay_left;
}
