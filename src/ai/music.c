#include "music.h"
#include "nn.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- random */

typedef struct { uint32_t s; } rng_t;

static uint32_t rnd(rng_t *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->s = x;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int pick(rng_t *r, int n) { return n > 0 ? (int)(rnd(r) % (uint32_t)n) : 0; }
static float unit(rng_t *r) { return (float)(rnd(r) >> 8) * (1.0f / 16777216.0f); }
static int chance(rng_t *r, float p) { return unit(r) < p; }

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* Python's // and % (the network's features are written in Python) */
static int fdiv(int a, int b) { int q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }
static int fmod_(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

/* ---------------------------------------------------------------- theory */

static const int8_t SCALE[2][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 } };
static const char *const NOTE_NAME[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

/* semitones above the tonic of a step of the scale (any octave) */
static int semis(int deg, int minor)
{
    int o = fdiv(deg, 7);
    return 12 * o + SCALE[minor][deg - 7 * o];
}

typedef struct {
    int root, quality;          /* the root's step 0..6; 0 major, 1 minor, 2 diminished */
    int tone[4], n;             /* semitones above the tonic: root, third, fifth (seventh) */
} chord_t;

/* roman numerals as trainmusic.py reads them */
static const struct { const char *rn; int minor, root, quality; } NUMERALS[] = {
    { "I", 0, 0, 0 }, { "ii", 0, 1, 1 }, { "iii", 0, 2, 1 }, { "IV", 0, 3, 0 }, { "V", 0, 4, 0 },
    { "vi", 0, 5, 1 }, { "vii", 0, 6, 2 }, { "bVII", 0, 6, 0 },
    { "i", 1, 0, 1 }, { "ii", 1, 1, 2 }, { "III", 1, 2, 0 }, { "iv", 1, 3, 1 }, { "v", 1, 4, 1 },
    { "V", 1, 4, 0 }, { "VI", 1, 5, 0 }, { "VII", 1, 6, 0 }, { "IV", 1, 3, 0 },
};

static int chord_of(const char *rn, int minor, int seventh, chord_t *c)
{
    for (unsigned i = 0; i < sizeof NUMERALS / sizeof NUMERALS[0]; i++) {
        if (NUMERALS[i].minor != minor || strcmp(NUMERALS[i].rn, rn))
            continue;
        c->root = NUMERALS[i].root;
        c->quality = NUMERALS[i].quality;
        int r = semis(c->root, minor);
        if (!strcmp(rn, "bVII"))
            r = 10;
        c->tone[0] = r;
        c->tone[1] = r + (c->quality == 0 ? 4 : 3);
        c->tone[2] = r + (c->quality == 2 ? 6 : 7);
        c->n = 3;
        if (seventh) {
            /* the seventh of the scale above the root: major on I and IV of
             * a major key, minor elsewhere */
            int maj7 = !minor && (c->root == 0 || c->root == 3);
            c->tone[3] = r + (maj7 ? 11 : 10);
            c->n = 4;
        }
        return 0;
    }
    return -1;
}

static int deg_in_chord(int deg, const chord_t *c)
{
    return fmod_(deg - c->root, 7) == 0 || fmod_(deg - c->root, 7) == 2 || fmod_(deg - c->root, 7) == 4;
}

/* ---------------------------------------------------------------- the network */

/* The features of trainmusic.py's features(), in the same order. */
void mus_features(const mus_ev_t *hist, int n, int pos, int meter, int root, int quality, int bar,
                  int last_bar, int minor, uint8_t *f)
{
    static const int DURS[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    memset(f, 0, MUS_NFEAT);
    for (int k = 0; k < 3; k++) {
        int i = n - 1 - k, c;
        if (i < 0)
            c = 16;
        else if (hist[i].deg == MUS_REST)
            c = 15;
        else {
            int j = i - 1;
            while (j >= 0 && hist[j].deg == MUS_REST)
                j--;
            if (j < 0)
                c = 16;
            else {
                int d = hist[i].deg - hist[j].deg;
                c = (d < -7 ? -7 : d > 7 ? 7 : d) + 7;
            }
        }
        f[k * 17 + c] = 1;
    }
    for (int k = 0; k < 2; k++) {
        int i = n - 1 - k, c = 8;
        if (i >= 0) {
            c = 0;
            for (int j = 1; j < 8; j++) {
                int dj = DURS[j] - hist[i].dur, dc = DURS[c] - hist[i].dur;
                if ((dj < 0 ? -dj : dj) < (dc < 0 ? -dc : dc))
                    c = j;
            }
        }
        f[51 + k * 9 + c] = 1;
    }
    int last = n - 1;
    while (last >= 0 && hist[last].deg == MUS_REST)
        last--;
    if (last >= 0) {
        int d = hist[last].deg;
        f[69 + fmod_(d, 7)] = 1;
        int o = fdiv(d, 7);
        f[76 + (o < -1 ? -1 : o > 2 ? 2 : o) + 1] = 1;
        int t = fmod_(d - root, 7);
        f[112] = t == 0 || t == 2 || t == 4;
    }
    f[80 + (pos > 15 ? 15 : pos)] = 1;
    f[96 + (meter == 16 ? 0 : meter == 12 ? 1 : 2)] = 1;
    f[99 + root] = 1;
    f[106 + quality] = 1;
    int b = bar % 4;
    if (b < 3)
        f[109 + b] = 1;
    else
        f[113] = 1;
    f[114] = last_bar != 0;
    f[115] = minor != 0;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

float mus_net_scale(void)
{
    float s;
    memcpy(&s, mus_net + 12, 4);
    return s;
}

int mus_net_run(const uint8_t *feat, int32_t *out)
{
    const uint8_t *b = mus_net;
    if (mus_net_len < 16 || memcmp(b, "BMNN", 4))
        return -1;
    int nl = b[5], width = (int)rd16(b + 6);
    float in_scale;
    memcpy(&in_scale, b + 8, 4);
    static int8_t x[256] __attribute__((aligned(4)));
    static int32_t acc[256];
    memset(x, 0, sizeof x);
    int8_t one = (int8_t)(in_scale + 0.5f > 127 ? 127 : (int)(in_scale + 0.5f));
    for (int i = 0; i < width; i++)
        x[i] = feat[i] ? one : 0;
    unsigned off = 16;
    for (int l = 0; l < nl; l++) {
        int nout = (int)rd16(b + off), relu = b[off + 2], shift = b[off + 3];
        int32_t mult;
        memcpy(&mult, b + off + 4, 4);
        off += 8;
        int n4 = (width + 3) & ~3;
        const int32_t *bias = (const int32_t *)(const void *)(b + off);
        off += 4u * (unsigned)nout;
        const int8_t *w = (const int8_t *)(b + off);
        off += (unsigned)(nout * n4);
        nn_dense(w, bias, x, n4, nout, acc);
        if (relu) {
            nn_relu_q(acc, nout, mult, shift, x);
            for (int i = nout; i < ((nout + 3) & ~3); i++)
                x[i] = 0;
        } else {
            for (int i = 0; i < nout; i++)
                out[i] = acc[i];
            return nout;
        }
        width = nout;
    }
    return -1;
}

/* ---------------------------------------------------------------- the piece being written */

typedef struct {
    mus_out_t *o;
    rng_t r;
    int key, minor, meter, bars, seventh;
    chord_t chord[16];
} ctx_t;

static int inst(ctx_t *c, const char *name)
{
    mus_out_t *o = c->o;
    for (int i = 0; i < o->ninst; i++)
        if (!strcmp(o->inst[i], name))
            return i;
    if (o->ninst >= MUS_INST)
        return 0;
    strncpy(o->inst[o->ninst], name, 8);
    o->inst[o->ninst][8] = 0;
    return o->ninst++;
}

/* a step at an absolute 16th of the piece (4 bars to a pattern) */
static mus_step_t *at(ctx_t *c, int track, int step)
{
    int per = 4 * c->meter;
    int p = step / per;
    if (p < 0 || p >= MUS_PATS || track < 0 || track >= MUS_TRACKS)
        return NULL;
    mus_out_t *o = c->o;
    if (p >= o->npat) {
        for (int i = o->npat; i <= p; i++)
            o->pat[i].len = per;
        o->npat = p + 1;
    }
    return &o->pat[p].step[track][step % per];
}

static void put(ctx_t *c, int track, int step, int note, int in, int vol, int fx)
{
    mus_step_t *s = at(c, track, step);
    if (!s || note < 1 || note > 127)
        return;
    s->note = (uint8_t)note;
    s->inst = (uint8_t)in;
    s->vol = (uint8_t)(vol < 1 ? 1 : vol > 255 ? 255 : vol);
    s->fx = (uint8_t)fx;
    c->o->pat[step / (4 * c->meter)].used |= (uint8_t)(1u << track);
}

/* the note is released here, unless a new one starts */
static void off(ctx_t *c, int track, int step)
{
    if (step >= c->bars * c->meter)
        return;
    mus_step_t *s = at(c, track, step);
    if (s && !s->note)
        s->note = 128;
}

static int total(const ctx_t *c) { return c->bars * c->meter; }

/* ---------------------------------------------------------------- progressions */

typedef struct { const char *mood; int minor; const char *bars; } prog_t;

/* eight bars each (four repeated); the first word is the mood */
static const prog_t PROGS[] = {
    { "pop", 0, "I V vi IV I V vi IV" },
    { "pop", 0, "vi IV I V vi IV I V" },
    { "happy", 0, "I IV V I I IV V I" },
    { "happy", 0, "I vi IV V I vi IV V" },
    { "heroic", 0, "I IV V I vi IV V I" },
    { "heroic", 0, "I V IV V I V IV I" },
    { "calm", 0, "I iii IV I IV I V I" },
    { "calm", 0, "I IV I V I IV V I" },
    { "jazz", 0, "ii V I vi ii V I I" },
    { "chip", 0, "I vi IV V I vi IV V" },
    { "rock", 0, "I bVII IV I I bVII IV V" },
    { "reggae", 0, "I IV I V I IV V I" },
    { "blues", 0, "I IV I I IV IV I V" },
    { "sad", 1, "i iv VII III VI iv V i" },
    { "sad", 1, "i VI III VII i VI V i" },
    { "epic", 1, "i VI III VII i VI VII i" },
    { "epic", 1, "i VII VI VII i VII VI V" },
    { "mystery", 1, "i VI iv V i VI iv V" },
    { "dark", 1, "i i VI V i i VI V" },
    { "dark", 1, "i iv i V i iv VI V" },
    { "funk", 1, "i IV i IV i IV v IV" },
    { "synth", 1, "i VI III VII i VI III VII" },
    { "lofi", 0, "ii V I vi ii V I I" },
    { "ambient", 0, "I IV vi IV I IV vi V" },
    { "techno", 1, "i i VI VI i i VII VII" },
};

static void progression(ctx_t *c, const char *mood)
{
    int found[16], n = 0;
    for (unsigned i = 0; i < sizeof PROGS / sizeof PROGS[0] && n < 16; i++)
        if (!strcmp(PROGS[i].mood, mood) && PROGS[i].minor == c->minor)
            found[n++] = (int)i;
    if (!n)                     /* the mood's mode was changed by the words */
        for (unsigned i = 0; i < sizeof PROGS / sizeof PROGS[0] && n < 16; i++)
            if (PROGS[i].minor == c->minor && (n == 0 || !strcmp(PROGS[i].mood, c->minor ? "epic" : "pop")))
                found[n++] = (int)i;
    const char *bars = PROGS[found[pick(&c->r, n)]].bars;
    char rn[8][6];
    int k = 0;
    for (const char *p = bars; *p && k < 8;) {
        while (*p == ' ')
            p++;
        int j = 0;
        while (*p && *p != ' ' && j < 5)
            rn[k][j++] = *p++;
        rn[k++][j] = 0;
    }
    for (int b = 0; b < c->bars && b < 16; b++) {
        chord_t *ch = &c->chord[b];
        if (chord_of(rn[b % k], c->minor, c->seventh, ch) != 0)
            chord_of(c->minor ? "i" : "I", c->minor, c->seventh, ch);
        int root = (c->key + ch->tone[0]) % 12;
        mus_out_t *o = c->o;
        {
            const char *suf = ch->quality == 1 ? "m" : ch->quality == 2 ? "dim" : "";
            size_t ln = strlen(NOTE_NAME[root]);
            memcpy(o->chords[b], NOTE_NAME[root], ln);
            strncpy(o->chords[b] + ln, suf, sizeof o->chords[b] - ln - 1);
            o->chords[b][sizeof o->chords[b] - 1] = 0;
        }
        o->nchords = b + 1;
    }
}

static const chord_t *chord_at(const ctx_t *c, int step)
{
    int b = step / c->meter;
    return &c->chord[b < 0 ? 0 : b >= c->bars ? c->bars - 1 : b];
}

/* a MIDI note near `centre` with these semitones above the tonic */
static int near(const ctx_t *c, int semi, int centre)
{
    int n = 12 * 5 + c->key + semi;     /* C4 = 60 */
    while (n - centre > 6)
        n -= 12;
    while (centre - n > 6)
        n += 12;
    return n;
}

/* ---------------------------------------------------------------- drums */

typedef struct {
    const char *style;
    int meter, bpm, swing;
    const char *k, *s, *h, *o, *x;      /* kick, snare, closed hat, open hat, extra */
    const char *sn, *xn;                /* the snare's and the extra's instruments */
} groove_t;

/* 'x' accent, 'o' a hit, 'g' a ghost, '.' nothing */
static const groove_t GROOVES[] = {
    { "rock", 16, 120, 0, "x.......x.x.....", "....x.......x...", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "pop", 16, 110, 0, "x.....o...x.....", "....x.......x...", "xoooxoooxoooxooo", "", "", "snare", "" },
    { "funk", 16, 100, 10, "x..o..o...x..o..", "....x..g.g..x..g", "xoxoxoxoxoxoxoxo", "..............x.", "", "snare", "" },
    { "disco", 16, 118, 0, "x...x...x...x...", "....x.......x...", "x.o.x.o.x.o.x.o.", "..x...x...x...x.", "", "clap", "" },
    { "house", 16, 124, 0, "x...x...x...x...", "....x.......x...", "", "..x...x...x...x.", "oooooooooooooooo", "clap", "shaker" },
    { "techno", 16, 130, 0, "x...x...x...x...", "....x.......x...", "", "..x...x...x...x.", "...o..o....o..o.", "clap", "rim" },
    { "hiphop", 16, 90, 20, "x......o..x.....", "....x.......x...", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "trap", 16, 140, 0, "x.......o.....x.", "........x.......", "xoxoxoxoxoooxoxo", "", "", "clap", "" },
    { "dnb", 16, 170, 0, "x.........x.....", "....x.......x..g", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "reggae", 16, 80, 15, "........x.......", "........x.......", "x.o.x.o.x.o.x.o.", "", "", "rim", "" },
    { "bossa", 16, 120, 0, "x..o....x..o....", "x..o..o...o..o..", "oooooooooooooooo", "", "", "rim", "" },
    { "waltz", 12, 150, 0, "x...........", "....o...o...", "x.o.x.o.x.o.", "", "", "snare", "" },
    { "march", 16, 112, 0, "x.......x.......", "x..o.oxox..o.oxo", "", "", "", "snare", "" },
    { "shuffle", 16, 110, 60, "x.....o.x.......", "....x.......x...", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "metal", 16, 150, 0, "xoxoxoxoxoxoxoxo", "....x.......x...", "x...x...x...x...", "", "", "snare", "" },
    { "chip", 16, 128, 0, "x.......x.x.....", "....x.......x...", "x.o.x.o.x.o.x.o.", "", "", "chipsnr", "" },
    { "lofi", 16, 80, 25, "x......o..x.....", "....x.......x...", "x.ogx.ogx.ogx.og", "", "", "snare", "" },
    { "halftime", 16, 140, 0, "x.......o.......", "........x.......", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "synth", 16, 108, 0, "x...x...x...x...", "....x.......x...", "x.o.x.o.x.o.x.o.", "", "", "snare", "" },
    { "ambient", 16, 76, 0, "x...............", "........o.......", "..o...o...o...o.", "", "", "rim", "" },
};

static const groove_t *groove_find(const char *style)
{
    for (unsigned i = 0; i < sizeof GROOVES / sizeof GROOVES[0]; i++)
        if (!strcmp(GROOVES[i].style, style))
            return &GROOVES[i];
    return &GROOVES[0];
}

static int hit_vol(char h, int pos)
{
    if (h == 'x') return 230;
    if (h == 'g') return 90;
    return pos % 4 == 0 ? 190 : 160;
}

static void drums(ctx_t *c, const groove_t *g)
{
    int chip = !strcmp(g->style, "chip");
    int ik = inst(c, chip ? "chipkick" : !strcmp(g->style, "metal") || !strcmp(g->style, "rock") ? "punch" : "kick");
    int is = inst(c, g->sn);
    int ih = *g->h ? inst(c, chip ? "chiphat" : "hat") : 0;
    int io = *g->o ? inst(c, "openhat") : ih;
    int ix = *g->x ? inst(c, g->xn) : 0;
    int crash = !chip && strcmp(g->style, "ambient") ? inst(c, "crash") : -1;
    int m = g->meter;
    for (int bar = 0; bar < c->bars; bar++) {
        int fill = bar % 4 == 3 && strcmp(g->style, "ambient");
        for (int p = 0; p < m; p++) {
            int s = bar * m + p;
            char k = g->k[p], sn = g->s[p], h = *g->h ? g->h[p] : '.', oh = *g->o ? g->o[p] : '.';
            char x = *g->x ? g->x[p] : '.';
            /* a fill at the end of each fourth bar: the snare's last beat in 16ths */
            if (fill && p >= m - 4) {
                sn = p == m - 4 ? 'x' : (chance(&c->r, 0.75f) ? 'o' : '.');
                k = '.';
            }
            /* variants: a ghost note, a kick moved, a hat left out */
            if (k == '.' && p % 2 == 1 && chance(&c->r, 0.06f))
                k = 'o';
            if (h == 'o' && chance(&c->r, 0.08f))
                h = '.';
            if (k != '.')
                put(c, 0, s, 36, ik, hit_vol(k, p), 0);
            if (sn != '.')
                put(c, 1, s, 50, is, hit_vol(sn, p) - (fill && p > m - 4 ? 40 - 10 * (p - m + 4) : 0), 0);
            if (oh != '.')
                put(c, 2, s, 72, io, hit_vol(oh, p), 0);
            else if (h != '.')
                put(c, 2, s, 72, ih, hit_vol(h, p) - 30, 0);
            if (x != '.')
                put(c, 7, s, 60, ix, hit_vol(x, p) - 40, 0);
            if (crash >= 0 && bar % 4 == 0 && p == 0 && bar > 0)
                put(c, 7, s, 60, crash, 200, 0);
        }
    }
}

/* ---------------------------------------------------------------- bass */

static void bass(ctx_t *c, const char *style, const char *name)
{
    int in = inst(c, name);
    int m = c->meter, base = 36;                /* around C2 */
    for (int bar = 0; bar < c->bars; bar++) {
        const chord_t *ch = &c->chord[bar];
        const chord_t *next = &c->chord[bar + 1 < c->bars ? bar + 1 : 0];
        int root = near(c, ch->tone[0], base + 4), fifth = near(c, ch->tone[2], root + 5);
        int s0 = bar * m;
        if (same(style, "walking") || same(style, "jazz") || same(style, "lofi")) {
            /* four quarters: the root, the chord, then a step to the next root */
            int nroot = near(c, next->tone[0], root);
            int third = near(c, ch->tone[1], root + 4);
            int notes[4] = { root, chance(&c->r, 0.5f) ? third : fifth, fifth, nroot + (nroot > root ? -1 : 1) };
            for (int q = 0; q < m / 4; q++)
                put(c, 3, s0 + q * 4, notes[q % 4], in, q == 0 ? 220 : 180, 0);
        } else if (same(style, "octave") || same(style, "disco")) {
            for (int e = 0; e < m / 2; e++)
                put(c, 3, s0 + e * 2, e % 2 ? root + 12 : root, in, e % 2 ? 170 : 220, 0);
        } else if (same(style, "acid") || same(style, "techno")) {
            for (int p = 0; p < m; p++) {
                if (p % 4 && !chance(&c->r, 0.55f))
                    continue;
                int n = chance(&c->r, 0.25f) ? root + 12 : chance(&c->r, 0.2f) ? fifth : root;
                put(c, 3, s0 + p, n, in, p % 4 == 0 ? 230 : 170, chance(&c->r, 0.15f) ? 1 << 4 | 1 : 0);
                if (chance(&c->r, 0.3f))
                    off(c, 3, s0 + p + 1);
            }
        } else if (same(style, "funk")) {
            static const int8_t pos[] = { 0, 3, 6, 7, 10, 12, 14 };
            for (unsigned i = 0; i < sizeof pos; i++) {
                if (pos[i] >= m || (i && !chance(&c->r, 0.8f)))
                    continue;
                int n = pos[i] == 7 || pos[i] == 14 ? root + 12 : pos[i] == 10 ? near(c, ch->tone[0] + 10, root + 8) : root;
                put(c, 3, s0 + pos[i], n, in, i == 0 ? 230 : 180, 0);
                off(c, 3, s0 + pos[i] + 1);
            }
        } else if (same(style, "reggae")) {
            put(c, 3, s0 + 2, root, in, 220, 0);
            put(c, 3, s0 + 6, fifth, in, 180, 0);
            put(c, 3, s0 + 8, root, in, 200, 0);
            put(c, 3, s0 + 11, root + 12, in, 160, 0);
            off(c, 3, s0 + 13);
        } else if (same(style, "rock") || same(style, "synth") || same(style, "chip") || same(style, "metal")) {
            for (int e = 0; e < m / 2; e++)        /* steady eighths on the root */
                put(c, 3, s0 + e * 2, same(style, "chip") && e % 2 ? fifth : root, in, e % 4 == 0 ? 220 : 175, 0);
        } else if (same(style, "waltz")) {
            put(c, 3, s0, root, in, 220, 0);
            off(c, 3, s0 + 3);
        } else if (same(style, "ambient")) {
            put(c, 3, s0, root, in, 180, 0);
        } else {                                    /* root: the beat's notes */
            put(c, 3, s0, root, in, 225, 0);
            put(c, 3, s0 + m / 2 - 2, root, in, 160, 0);
            put(c, 3, s0 + m / 2, chance(&c->r, 0.5f) ? fifth : root, in, 190, 0);
            off(c, 3, s0 + m - 2);
        }
    }
}

/* ---------------------------------------------------------------- chords */

/* the chord's tones as two or three voices near C4, each moving as little as it can */
static void voicing(ctx_t *c, int bar, int *v, int nv)
{
    static int prev[3] = { 64, 67, 71 };
    const chord_t *ch = &c->chord[bar];
    if (bar == 0) {
        prev[0] = 64; prev[1] = 67; prev[2] = 71;
    }
    /* the voices take the third, the fifth and (with a seventh) the seventh,
     * else the root: the bass has the root */
    int want[3] = { ch->tone[1], ch->tone[2], ch->n > 3 ? ch->tone[3] : ch->tone[0] };
    for (int i = 0; i < nv; i++) {
        v[i] = near(c, want[i], prev[i]);
        prev[i] = v[i];
    }
}

static void chords(ctx_t *c, const char *style, const char *name, int t0, int nv)
{
    int in = inst(c, name);
    int m = c->meter;
    for (int bar = 0; bar < c->bars; bar++) {
        int v[3];
        voicing(c, bar, v, nv);
        int s0 = bar * m;
        for (int i = 0; i < nv; i++) {
            int t = t0 + i;
            if (same(style, "pad") || same(style, "ambient") || same(style, "synth")) {
                put(c, t, s0, v[i], in, 200, 0);
            } else if (same(style, "skank") || same(style, "reggae")) {
                put(c, t, s0 + 4, v[i], in, 200, 0);
                off(c, t, s0 + 5);
                put(c, t, s0 + 12, v[i], in, 190, 0);
                off(c, t, s0 + 13);
            } else if (same(style, "stabs") || same(style, "house") || same(style, "funk")) {
                static const int8_t pos[] = { 2, 6, 10, 14 };
                for (int k = 0; k < 4; k++) {
                    if (k && !chance(&c->r, 0.7f))
                        continue;
                    put(c, t, s0 + pos[k], v[i], in, 190, 0);
                    off(c, t, s0 + pos[k] + 1);
                }
            } else if (same(style, "waltz")) {
                put(c, t, s0 + 4, v[i], in, 170, 0);
                off(c, t, s0 + 6);
                put(c, t, s0 + 8, v[i], in, 170, 0);
                off(c, t, s0 + 10);
            } else if (same(style, "rock")) {
                for (int e = 0; e < m / 2; e++)
                    put(c, t, s0 + e * 2, v[i], in, e % 4 == 0 ? 210 : 170, 0);
            } else if (same(style, "jazz") || same(style, "lofi")) {
                put(c, t, s0, v[i], in, 190, 0);
                if (chance(&c->r, 0.6f)) {
                    off(c, t, s0 + 5);
                    put(c, t, s0 + 6 + (chance(&c->r, 0.5f) ? 0 : 4), v[i], in, 160, 0);
                }
            } else {                                /* halves */
                put(c, t, s0, v[i], in, 200, 0);
                put(c, t, s0 + m / 2, v[i], in, 170, 0);
            }
        }
    }
}

/* ---------------------------------------------------------------- arpeggios */

static void arp(ctx_t *c, const char *style, const char *name, int track, int rate)
{
    int in = inst(c, name);
    int m = c->meter;
    static const int8_t UP[] = { 0, 1, 2, 3 }, DOWN[] = { 3, 2, 1, 0 }, UPDOWN[] = { 0, 1, 2, 3, 2, 1 },
                        BROKEN[] = { 0, 2, 1, 3 }, ALBERTI[] = { 0, 2, 1, 2 }, TRANCE[] = { 0, 1, 2, 4, 2, 1, 0, 3 },
                        HARP[] = { 0, 1, 2, 3, 4, 5, 4, 3 };
    const int8_t *seq = UP;
    int n = 4;
    if (same(style, "down")) { seq = DOWN; n = 4; }
    else if (same(style, "updown")) { seq = UPDOWN; n = 6; }
    else if (same(style, "broken")) { seq = BROKEN; n = 4; }
    else if (same(style, "alberti")) { seq = ALBERTI; n = 4; }
    else if (same(style, "trance")) { seq = TRANCE; n = 8; }
    else if (same(style, "harp")) { seq = HARP; n = 8; }
    int i = 0;
    for (int s = 0; s < total(c); s += rate) {
        const chord_t *ch = chord_at(c, s);
        int lo = near(c, ch->tone[0], 64);
        /* the chord's tones from its root up two octaves */
        int tones[6];
        for (int k = 0; k < 6; k++) {
            int t = ch->tone[k % 3] + 12 * (k / 3);
            int nt = lo + t - ch->tone[0];
            tones[k] = nt;
        }
        if (s % m == 0)
            i = 0;
        int note = tones[seq[i % n] % 6];
        put(c, track, s, note, in, s % 4 == 0 ? 200 : 160, 0);
        i++;
    }
}

/* ---------------------------------------------------------------- melodies */

static const int DURS[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };

typedef struct {
    mus_ev_t ev[256];
    int n;
} line_t;

/* one bar of the melody from the network: sampled note by note with
 * the temperature, the chord's tones preferred on the strong beats */
static void sample_bar(ctx_t *c, line_t *L, int bar, float temp, int cadence)
{
    int m = c->meter, t = bar * m, end = (bar + 1) * m;
    const chord_t *ch = &c->chord[bar];
    float scale = mus_net_scale();
    while (t < end && L->n < 250) {
        int last = L->n - 1;
        while (last >= 0 && L->ev[last].deg == MUS_REST)
            last--;
        int from = last >= 0 ? L->ev[last].deg : 0;
        int pos = t - bar * m;
        /* the last bar: the tonic, held to the end */
        if (cadence && pos == 0) {
            int best = from, bd = 99;
            for (int d = from - 7; d <= from + 7; d++)
                if (fmod_(d, 7) == 0 && d >= -2 && d <= 11 && (d - from < 0 ? from - d : d - from) < bd) {
                    bd = d - from < 0 ? from - d : d - from;
                    best = d;
                }
            L->ev[L->n++] = (mus_ev_t){ best, m * 3 / 4 };
            L->ev[L->n++] = (mus_ev_t){ MUS_REST, m - m * 3 / 4 };
            return;
        }
        uint8_t f[MUS_NFEAT];
        mus_features(L->ev, L->n, pos, m, ch->root, ch->quality, bar % 4, bar == c->bars - 1, c->minor, f);
        int32_t acc[MUS_NOUT];
        mus_net_run(f, acc);
        float lg[16], ld[8];
        for (int k = 0; k < 16; k++)
            lg[k] = (float)acc[k] * scale;
        for (int k = 0; k < 8; k++)
            ld[k] = (float)acc[16 + k] * scale;
        /* the step: in range, chord tones on the beats, no rest after a rest
         * nor on the first beat */
        for (int k = 0; k < 15; k++) {
            int d = from + k - 7;
            if (d < -2 || d > 11) {
                lg[k] = -1e9f;
                continue;
            }
            if (pos == 0 || pos * 2 == m)
                lg[k] += deg_in_chord(d, ch) ? 1.2f : -1.2f;
        }
        if (pos == 0 || (L->n && L->ev[L->n - 1].deg == MUS_REST) || L->n == 0)
            lg[15] = -1e9f;
        /* the length: inside the bar */
        for (int k = 0; k < 8; k++)
            if (t + DURS[k] > end)
                ld[k] = -1e9f;
        int choice[2];
        float *v[2] = { lg, ld };
        int nv[2] = { 16, 8 };
        for (int h = 0; h < 2; h++) {
            float mx = -1e30f;
            for (int k = 0; k < nv[h]; k++)
                if (v[h][k] > mx) mx = v[h][k];
            float p[16], sum = 0;
            for (int k = 0; k < nv[h]; k++) {
                p[k] = v[h][k] < -1e8f ? 0 : expf((v[h][k] - mx) / temp);
                sum += p[k];
            }
            float u = unit(&c->r) * sum;
            int k = 0;
            for (; k < nv[h] - 1; k++) {
                if (u < p[k])
                    break;
                u -= p[k];
            }
            while (p[k] == 0 && k > 0)
                k--;
            choice[h] = k;
        }
        int dur = DURS[choice[1]];
        if (t + dur > end)
            dur = end - t;
        int deg = choice[0] == 15 ? MUS_REST : from + choice[0] - 7;
        L->ev[L->n++] = (mus_ev_t){ deg, dur };
        t += dur;
    }
}

/* bar `from`'s notes again in bar `to`, the strong beats moved onto the new chord */
static void copy_bar(ctx_t *c, line_t *L, int from, int to)
{
    int m = c->meter, t = 0, i = 0;
    while (i < L->n && t < from * m) {
        t += L->ev[i].dur;
        i++;
    }
    const chord_t *ch = &c->chord[to];
    int pos = 0;
    while (i < L->n && pos < m && L->n < 250) {
        mus_ev_t e = L->ev[i++];
        if (e.deg != MUS_REST && (pos == 0 || pos * 2 == m) && !deg_in_chord(e.deg, ch)) {
            if (deg_in_chord(e.deg + 1, ch))
                e.deg++;
            else if (deg_in_chord(e.deg - 1, ch))
                e.deg--;
        }
        if (pos + e.dur > m)
            e.dur = m - pos;
        L->ev[L->n++] = e;
        pos += e.dur;
    }
    if (pos < m)
        L->ev[L->n++] = (mus_ev_t){ MUS_REST, m - pos };
}

static void melody(ctx_t *c, const char *name, int track, float temp)
{
    static line_t L;
    L.n = 0;
    /* the form: two bars, the first again on the third, an answer; the
     * second half the same, ending on the tonic */
    for (int bar = 0; bar < c->bars; bar++) {
        int ph = bar % 8;
        if ((ph == 2 || ph == 6) && bar >= 2)
            copy_bar(c, &L, bar - ph + 0, bar);
        else if (ph == 4 && bar >= 4 && chance(&c->r, 0.5f))
            copy_bar(c, &L, bar - 4, bar);
        else
            sample_bar(c, &L, bar, temp, bar == c->bars - 1);
    }
    int in = inst(c, name);
    int t = 0, tonic = c->key <= 6 ? 60 + c->key : 48 + c->key;      /* G3..F#4: the tune between A3 and C6 */
    for (int i = 0; i < L.n; i++) {
        mus_ev_t e = L.ev[i];
        if (e.deg == MUS_REST)
            off(c, track, t);
        else
            put(c, track, t, tonic + semis(e.deg, c->minor), in, t % c->meter == 0 ? 225 : 190, 0);
        t += e.dur;
        if (e.deg != MUS_REST && i + 1 < L.n && L.ev[i + 1].deg == MUS_REST)
            off(c, track, t);
    }
}

/* ---------------------------------------------------------------- sound effects */

static void sfx_step(ctx_t *c, int note, const char *name, int vol, int fx)
{
    mus_out_t *o = c->o;
    if (o->sfx_len >= MUS_SFX_STEPS)
        return;
    mus_step_t *s = &o->sfx[o->sfx_len++];
    s->note = (uint8_t)note;
    s->inst = note && note < 128 ? (uint8_t)inst(c, name) : 0;
    s->vol = (uint8_t)vol;
    s->fx = (uint8_t)fx;
}

enum { FX_GLIDE = 1, FX_UP, FX_DOWN, FX_VIB, FX_TREM, FX_CHORD, FX_ARP, FX_FADEOUT, FX_FADEIN, FX_RETRIG,
       FX_DELAY, FX_CUT };
#define FX(t, n) ((t) << 4 | (n))

static void sfx(ctx_t *c, const char *what)
{
    mus_out_t *o = c->o;
    rng_t *r = &c->r;
    int tr = pick(r, 5) - 2;            /* a variant: a little higher or lower */
    o->sfx_ms = 50;
    if (!strcmp(what, "coin")) {
        o->sfx_ms = 45 + pick(r, 20);
        sfx_step(c, 83 + tr, "blip", 230, 0);
        sfx_step(c, 88 + tr, "blip", 230, 0);
        sfx_step(c, 0, "", 0, 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 2));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "jump")) {
        o->sfx_ms = 35 + pick(r, 20);
        sfx_step(c, 60 + tr, "blip", 220, FX(FX_UP, 9 + pick(r, 4)));
        sfx_step(c, 0, "", 0, FX(FX_UP, 3));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 1));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "laser") || !strcmp(what, "shoot")) {
        o->sfx_ms = 30 + pick(r, 15);
        int low = !strcmp(what, "shoot");
        sfx_step(c, (low ? 72 : 84) + tr, "laser", 230, 0);
        sfx_step(c, 0, "", 0, FX(FX_DOWN, 7 + pick(r, 5)));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 1));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "explosion") || !strcmp(what, "thunder")) {
        int big = !strcmp(what, "thunder");
        o->sfx_ms = big ? 90 : 60;
        sfx_step(c, (big ? 36 : 45) + tr, "boom", 255, 0);
        for (int i = 0; i < (big ? 8 : 5); i++)
            sfx_step(c, 0, "", 0, i == 2 ? FX(FX_FADEOUT, big ? 6 : 3) : 0);
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "powerup") || !strcmp(what, "levelup")) {
        int lv = !strcmp(what, "levelup");
        o->sfx_ms = lv ? 70 : 45;
        static const int8_t up[] = { 0, 4, 7, 12, 16, 19, 24 };
        for (int i = 0; i < (lv ? 7 : 6); i++)
            sfx_step(c, 72 + tr + up[i], lv ? "bell" : "chip", 210, 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 3));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "hurt") || !strcmp(what, "hit")) {
        o->sfx_ms = 40;
        sfx_step(c, (!strcmp(what, "hit") ? 50 : 57) + tr, "punch", 240, 0);
        sfx_step(c, 64 + tr, "laser", 200, FX(FX_DOWN, 6));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 1));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "select") || !strcmp(what, "back")) {
        o->sfx_ms = 35;
        int back = !strcmp(what, "back");
        sfx_step(c, (back ? 79 : 84) + tr, "blip", 200, 0);
        sfx_step(c, (back ? 72 : 91) + tr, "blip", 200, 0);
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "alarm")) {
        o->sfx_ms = 120;
        for (int i = 0; i < 8; i++)
            sfx_step(c, (i % 2 ? 76 : 81) + tr, "lead", 210, 0);
        sfx_step(c, 128, "", 0, 0);
        o->sfx_loop0 = 0;
        o->sfx_loop1 = 8;
    } else if (!strcmp(what, "pickup")) {
        o->sfx_ms = 40;
        sfx_step(c, 79 + tr, "glock", 220, 0);
        sfx_step(c, 91 + tr, "glock", 220, 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 3));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "door")) {
        o->sfx_ms = 70;
        sfx_step(c, 40 + tr, "boom", 180, FX(FX_CUT, 6));
        sfx_step(c, 0, "", 0, 0);
        sfx_step(c, 55 + tr, "rim", 220, 0);
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "step")) {
        o->sfx_ms = 40;
        sfx_step(c, 43 + tr, "tom", 150, FX(FX_CUT, 4));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "death") || !strcmp(what, "lose")) {
        int lose = !strcmp(what, "lose");
        o->sfx_ms = lose ? 160 : 90;
        static const int8_t down[] = { 7, 6, 5, 4, 3, 2, 1, 0 };
        for (int i = 0; i < (lose ? 4 : 8); i++)
            sfx_step(c, 64 + tr + (lose ? (3 - i) * 2 - (i == 3) : down[i]), lose ? "chiptri" : "chip", 210,
                     i == (lose ? 3 : 7) ? FX(FX_VIB, 6) : 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 3));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "win")) {
        o->sfx_ms = 110;
        static const int8_t fan[] = { 0, 4, 7, 12, 12, 12, 7, 12 };
        for (int i = 0; i < 8; i++)
            sfx_step(c, i < 4 || i == 6 || i == 7 ? 72 + tr + fan[i] : 0, "brass", 220, i == 7 ? FX(FX_VIB, 4) : 0);
        sfx_step(c, 0, "", 0, 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 2));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "teleport")) {
        o->sfx_ms = 50;
        sfx_step(c, 60 + tr, "glass", 220, FX(FX_CHORD, 15));
        sfx_step(c, 0, "", 0, FX(FX_UP, 12));
        sfx_step(c, 0, "", 0, FX(FX_UP, 12));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 2));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "splash")) {
        o->sfx_ms = 60;
        sfx_step(c, 60 + tr, "clap", 230, 0);
        sfx_step(c, 72 + tr, "shaker", 200, FX(FX_RETRIG, 3));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 3));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "dash")) {
        o->sfx_ms = 35;
        sfx_step(c, 60 + tr, "openhat", 200, FX(FX_DOWN, 5));
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 2));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "bounce")) {
        o->sfx_ms = 40;
        sfx_step(c, 55 + tr, "tom", 220, FX(FX_UP, 12));
        sfx_step(c, 0, "", 0, 0);
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "charge")) {
        o->sfx_ms = 80;
        for (int i = 0; i < 8; i++)
            sfx_step(c, i ? 0 : 48 + tr, "lead", 200, FX(FX_UP, 3));
        sfx_step(c, 0, "", 0, FX(FX_RETRIG, 7));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "heal")) {
        o->sfx_ms = 70;
        static const int8_t up[] = { 0, 4, 7, 11, 14 };
        for (int i = 0; i < 5; i++)
            sfx_step(c, 72 + tr + up[i], "harp", 200, 0);
        sfx_step(c, 0, "", 0, FX(FX_FADEOUT, 4));
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "typing")) {
        o->sfx_ms = 25;
        sfx_step(c, 84 + tr * 2, "rim", 140, 0);
        sfx_step(c, 128, "", 0, 0);
    } else if (!strcmp(what, "bell")) {
        o->sfx_ms = 100;
        sfx_step(c, 84 + tr, "bell", 230, 0);
        for (int i = 0; i < 6; i++)
            sfx_step(c, 0, "", 0, 0);
        sfx_step(c, 128, "", 0, 0);
    } else {                                    /* blip */
        o->sfx_ms = 40;
        sfx_step(c, 84 + tr, "blip", 200, 0);
        sfx_step(c, 128, "", 0, 0);
    }
    if (!o->sfx_loop1)
        o->sfx_loop0 = o->sfx_loop1 = 0;
}

/* ---------------------------------------------------------------- recipes */

typedef struct {
    const char *id, *name, *kind, *style, *mood;
    int bars;
} recipe_t;

static const recipe_t RECIPES[] = {
    /* rhythms: drums only */
    { "beat.rock", "rock beat", "beat", "rock", "rock", 4 },
    { "beat.pop", "pop beat", "beat", "pop", "pop", 4 },
    { "beat.funk", "funk groove", "beat", "funk", "funk", 4 },
    { "beat.disco", "disco beat", "beat", "disco", "pop", 4 },
    { "beat.house", "house beat", "beat", "house", "pop", 4 },
    { "beat.techno", "techno beat", "beat", "techno", "techno", 4 },
    { "beat.hiphop", "hip-hop beat", "beat", "hiphop", "lofi", 4 },
    { "beat.trap", "trap beat", "beat", "trap", "dark", 4 },
    { "beat.dnb", "drum and bass", "beat", "dnb", "dark", 4 },
    { "beat.reggae", "reggae one drop", "beat", "reggae", "reggae", 4 },
    { "beat.bossa", "bossa nova", "beat", "bossa", "jazz", 4 },
    { "beat.waltz", "waltz", "beat", "waltz", "calm", 4 },
    { "beat.march", "march", "beat", "march", "heroic", 4 },
    { "beat.shuffle", "shuffle", "beat", "shuffle", "blues", 4 },
    { "beat.metal", "metal double kick", "beat", "metal", "dark", 4 },
    { "beat.chip", "8-bit beat", "beat", "chip", "chip", 4 },
    { "beat.lofi", "lo-fi beat", "beat", "lofi", "lofi", 4 },
    { "beat.halftime", "half-time beat", "beat", "halftime", "epic", 4 },
    /* backing tracks: drums, bass, chords */
    { "base.pop", "pop backing", "base", "pop", "pop", 8 },
    { "base.rock", "rock backing", "base", "rock", "rock", 8 },
    { "base.lofi", "lo-fi backing", "base", "lofi", "lofi", 8 },
    { "base.synthwave", "synthwave backing", "base", "synth", "synth", 8 },
    { "base.chiptune", "chiptune backing", "base", "chip", "chip", 8 },
    { "base.ambient", "ambient backing", "base", "ambient", "ambient", 8 },
    { "base.epic", "epic backing", "base", "halftime", "epic", 8 },
    { "base.jazz", "jazz backing", "base", "bossa", "jazz", 8 },
    { "base.reggae", "reggae backing", "base", "reggae", "reggae", 8 },
    { "base.funk", "funk backing", "base", "funk", "funk", 8 },
    { "base.boss", "boss fight backing", "base", "metal", "dark", 8 },
    { "base.town", "town backing", "base", "waltz", "happy", 8 },
    { "base.dungeon", "dungeon backing", "base", "ambient", "mystery", 8 },
    { "base.space", "space backing", "base", "synth", "ambient", 8 },
    { "base.sad", "sad backing", "base", "pop", "sad", 8 },
    { "base.happy", "happy backing", "base", "pop", "happy", 8 },
    { "base.blues", "blues backing", "base", "shuffle", "blues", 8 },
    { "base.techno", "techno backing", "base", "techno", "techno", 8 },
    /* bass lines */
    { "bass.root", "bass on the beat", "bass", "root", "pop", 4 },
    { "bass.octave", "octave bass", "bass", "octave", "pop", 4 },
    { "bass.walking", "walking bass", "bass", "walking", "jazz", 4 },
    { "bass.acid", "acid bass line", "bass", "acid", "techno", 4 },
    { "bass.funk", "funk bass", "bass", "funk", "funk", 4 },
    /* arpeggios over the chords */
    { "arp.up", "arpeggio up", "arp", "up", "pop", 4 },
    { "arp.down", "arpeggio down", "arp", "down", "sad", 4 },
    { "arp.updown", "arpeggio up and down", "arp", "updown", "calm", 4 },
    { "arp.broken", "broken chords", "arp", "broken", "pop", 4 },
    { "arp.alberti", "Alberti bass", "arp", "alberti", "calm", 4 },
    { "arp.trance", "trance arpeggio", "arp", "trance", "synth", 4 },
    { "arp.chip", "8-bit arpeggio", "arp", "up", "chip", 4 },
    { "arp.harp", "harp sweep", "arp", "harp", "ambient", 4 },
    /* melodies (the network) */
    { "melody.happy", "happy melody", "melody", "lead", "happy", 8 },
    { "melody.sad", "sad melody", "melody", "lead", "sad", 8 },
    { "melody.epic", "epic melody", "melody", "lead", "epic", 8 },
    { "melody.mysterious", "mysterious melody", "melody", "lead", "mystery", 8 },
    { "melody.calm", "calm melody", "melody", "lead", "calm", 8 },
    { "melody.heroic", "heroic melody", "melody", "lead", "heroic", 8 },
    { "melody.chip", "8-bit melody", "melody", "lead", "chip", 8 },
    { "melody.jazzy", "jazzy melody", "melody", "lead", "jazz", 8 },
    { "melody.spooky", "spooky melody", "melody", "lead", "dark", 8 },
    { "melody.adventure", "adventure melody", "melody", "lead", "pop", 8 },
    /* classic sound effects */
    { "sfx.coin", "coin", "sfx", "coin", "", 0 },
    { "sfx.jump", "jump", "sfx", "jump", "", 0 },
    { "sfx.laser", "laser", "sfx", "laser", "", 0 },
    { "sfx.shoot", "shot", "sfx", "shoot", "", 0 },
    { "sfx.explosion", "explosion", "sfx", "explosion", "", 0 },
    { "sfx.powerup", "power-up", "sfx", "powerup", "", 0 },
    { "sfx.levelup", "level up", "sfx", "levelup", "", 0 },
    { "sfx.hurt", "hurt", "sfx", "hurt", "", 0 },
    { "sfx.hit", "hit", "sfx", "hit", "", 0 },
    { "sfx.select", "menu select", "sfx", "select", "", 0 },
    { "sfx.back", "menu back", "sfx", "back", "", 0 },
    { "sfx.alarm", "alarm", "sfx", "alarm", "", 0 },
    { "sfx.pickup", "pick up", "sfx", "pickup", "", 0 },
    { "sfx.door", "door", "sfx", "door", "", 0 },
    { "sfx.step", "footstep", "sfx", "step", "", 0 },
    { "sfx.death", "death", "sfx", "death", "", 0 },
    { "sfx.win", "victory", "sfx", "win", "", 0 },
    { "sfx.lose", "game over", "sfx", "lose", "", 0 },
    { "sfx.teleport", "teleport", "sfx", "teleport", "", 0 },
    { "sfx.splash", "splash", "sfx", "splash", "", 0 },
    { "sfx.thunder", "thunder", "sfx", "thunder", "", 0 },
    { "sfx.dash", "dash", "sfx", "dash", "", 0 },
    { "sfx.bounce", "bounce", "sfx", "bounce", "", 0 },
    { "sfx.charge", "charge up", "sfx", "charge", "", 0 },
    { "sfx.heal", "heal", "sfx", "heal", "", 0 },
    { "sfx.typing", "typing", "sfx", "typing", "", 0 },
    { "sfx.bell", "bell", "sfx", "bell", "", 0 },
    { "sfx.blip", "blip", "sfx", "blip", "", 0 },
};
#define NRECIPES ((int)(sizeof RECIPES / sizeof RECIPES[0]))

int mus_recipes(void) { return NRECIPES; }
const char *mus_recipe_id(int i) { return i >= 0 && i < NRECIPES ? RECIPES[i].id : NULL; }
const char *mus_recipe_name(int i) { return i >= 0 && i < NRECIPES ? RECIPES[i].name : NULL; }

int mus_find(const char *gen)
{
    for (int i = 0; i < NRECIPES; i++)
        if (!strcmp(RECIPES[i].id, gen))
            return i;
    return -1;
}

/* the minor moods */
static int mood_minor(const char *m)
{
    static const char *const MINOR[] = { "sad", "epic", "mystery", "dark", "funk", "synth", "techno" };
    for (unsigned i = 0; i < sizeof MINOR / sizeof MINOR[0]; i++)
        if (!strcmp(m, MINOR[i]))
            return 1;
    return 0;
}

/* the instruments of a style: bass, chords, arpeggio, melody */
static void style_inst(const char *style, const char *mood, const char **b, const char **ch, const char **ar,
                       const char **me)
{
    *b = "bass"; *ch = "epiano"; *ar = "pluck"; *me = "lead";
    if (!strcmp(style, "chip") || !strcmp(mood, "chip")) { *b = "chiptri"; *ch = "chip"; *ar = "chip"; *me = "chip"; }
    else if (!strcmp(style, "synth")) { *b = "acid"; *ch = "pad"; *ar = "pluck"; *me = "sawlead"; }
    else if (!strcmp(style, "rock") || !strcmp(style, "metal")) { *b = "pickbass"; *ch = "sawlead"; *ar = "guitar"; *me = "sawlead"; }
    else if (!strcmp(style, "lofi") || !strcmp(mood, "jazz")) { *b = "sub"; *ch = "epiano"; *ar = "epiano"; *me = "flute"; }
    else if (!strcmp(style, "ambient")) { *b = "sub"; *ch = "pad"; *ar = "harp"; *me = "glass"; }
    else if (!strcmp(style, "techno") || !strcmp(style, "house")) { *b = "acid"; *ch = "organ"; *ar = "pluck"; *me = "lead"; }
    else if (!strcmp(style, "reggae")) { *b = "sub"; *ch = "organ"; *ar = "guitar"; *me = "lead"; }
    else if (!strcmp(style, "waltz")) { *b = "pickbass"; *ch = "strings"; *ar = "harp"; *me = "flute"; }
    else if (!strcmp(style, "funk")) { *b = "fmbass"; *ch = "epiano"; *ar = "guitar"; *me = "brass"; }
    else if (!strcmp(style, "halftime")) { *b = "sub"; *ch = "strings"; *ar = "harp"; *me = "brass"; }
    else if (!strcmp(style, "shuffle")) { *b = "pickbass"; *ch = "organ"; *ar = "guitar"; *me = "lead"; }
    if (!strcmp(mood, "sad")) *me = "strings";
    if (!strcmp(mood, "mystery") || !strcmp(mood, "dark")) *me = !strcmp(style, "chip") ? "chip" : "glass";
    if (!strcmp(mood, "heroic")) *me = "brass";
    if (!strcmp(mood, "calm")) *me = "flute";
    if (!strcmp(mood, "happy")) *me = !strcmp(style, "chip") ? "chip" : "marimba";
}

/* the chords of the bars that already have notes: of the key's chords
 * (triads on its steps), the one with most of the bar's notes, its root
 * counting twice and the lowest note's chord first */
static void context_chords(ctx_t *c, const mus_req_t *req)
{
    for (int b = 0; b < c->bars && b < 16; b++) {
        int pcs[12] = { 0 }, any = 0, low = 128;
        for (int i = 0; i < req->nctx; i++)
            if (req->ctx_bar[i] == b && req->ctx[i] >= 1 && req->ctx[i] <= 127) {
                pcs[(req->ctx[i] - c->key + 120) % 12]++;
                any = 1;
                if (req->ctx[i] < low)
                    low = req->ctx[i];
            }
        if (!any)
            continue;
        static const char *const MAJ[] = { "I", "ii", "iii", "IV", "V", "vi" };
        static const char *const MIN[] = { "i", "III", "iv", "V", "VI", "VII" };
        int best = -1;
        float bs = 0;
        chord_t ch;
        for (int k = 0; k < 6; k++) {
            if (chord_of(c->minor ? MIN[k] : MAJ[k], c->minor, c->seventh, &ch) != 0)
                continue;
            float sc = 0;
            for (int t = 0; t < 3; t++)
                sc += (float)pcs[fmod_(ch.tone[t], 12)] * (t == 0 ? 2.0f : 1.0f);
            if (fmod_(low - c->key - ch.tone[0], 12) == 0)
                sc += 1.5f;
            if (sc > bs) {
                bs = sc;
                best = k;
            }
        }
        if (best >= 0) {
            chord_of(c->minor ? MIN[best] : MAJ[best], c->minor, c->seventh, &c->chord[b]);
            int root = (c->key + c->chord[b].tone[0]) % 12;
            const char *suf = c->chord[b].quality == 1 ? "m" : c->chord[b].quality == 2 ? "dim" : "";
            size_t ln = strlen(NOTE_NAME[root]);
            memcpy(c->o->chords[b], NOTE_NAME[root], ln);
            strncpy(c->o->chords[b] + ln, suf, sizeof c->o->chords[b] - ln - 1);
        }
    }
}

/* the key that fits the notes already there (Krumhansl's profiles) */
static int detect_key(const uint8_t *notes, int n, int *minor)
{
    static const float MAJ[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float MIN[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float h[12] = { 0 };
    for (int i = 0; i < n; i++)
        if (notes[i] >= 1 && notes[i] <= 127)
            h[notes[i] % 12] += 1;
    float best = -1e9f;
    int key = 0;
    *minor = 0;
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 12; k++) {
            float s = 0;
            for (int i = 0; i < 12; i++)
                s += h[(k + i) % 12] * (m ? MIN[i] : MAJ[i]);
            if (s > best) {
                best = s;
                key = k;
                *minor = m;
            }
        }
    return key;
}

void mus_req_init(mus_req_t *r, const char *gen)
{
    memset(r, 0, sizeof *r);
    r->gen = gen;
    r->seed = 1;
    r->key = -1;
    r->minor = -1;
}

int mus_make(const mus_req_t *req, mus_out_t *o)
{
    int ri = req->gen ? mus_find(req->gen) : -1;
    if (ri < 0)
        return -1;
    const recipe_t *rc = &RECIPES[ri];
    memset(o, 0, sizeof *o);
    strncpy(o->gen, rc->id, sizeof o->gen - 1);
    strncpy(o->name, rc->name, sizeof o->name - 1);
    strncpy(o->kind, rc->kind, sizeof o->kind - 1);
    static ctx_t c;
    memset(&c, 0, sizeof c);
    c.o = o;
    c.r.s = (req->seed ? req->seed : 1) * 2654435761u ^ hash(rc->id);
    if (!c.r.s)
        c.r.s = 1;
    for (int i = 0; i < 4; i++)
        rnd(&c.r);
    if (!strcmp(rc->kind, "sfx")) {
        sfx(&c, rc->style);
        return 0;
    }

    /* the key and the mode: the words, or the notes already there (for the
     * parts that go over them: not a rhythm or a backing track, which get
     * patterns of their own), or the recipe's */
    int over = !strcmp(rc->kind, "melody") || !strcmp(rc->kind, "arp") || !strcmp(rc->kind, "bass");
    int ctx_minor = 0, ctx_key = over && req->nctx >= 4 ? detect_key(req->ctx, req->nctx, &ctx_minor) : -1;
    c.minor = req->minor >= 0 ? req->minor : ctx_key >= 0 ? ctx_minor : mood_minor(rc->mood);
    static const int8_t MAJ_KEYS[] = { 0, 2, 5, 7, 9, 0, 7 }, MIN_KEYS[] = { 9, 2, 4, 0, 9, 7 };
    c.key = req->key >= 0 ? req->key % 12 : ctx_key >= 0 ? ctx_key
            : c.minor ? MIN_KEYS[pick(&c.r, (int)sizeof MIN_KEYS)] : MAJ_KEYS[pick(&c.r, (int)sizeof MAJ_KEYS)];
    const char *style = rc->style;
    const groove_t *g = groove_find(!strcmp(rc->kind, "beat") || !strcmp(rc->kind, "base") ? style
                                    : !strcmp(rc->mood, "chip") ? "chip" : !strcmp(rc->mood, "jazz") ? "bossa"
                                    : !strcmp(rc->mood, "calm") ? "waltz" : "pop");
    c.meter = g->meter;
    c.bars = req->bars > 0 ? req->bars : rc->bars;
    int maxbars = MUS_PATS * 4;
    if (c.bars > maxbars) c.bars = maxbars;
    if (c.bars < 1) c.bars = 1;
    if (c.bars > 16) c.bars = 16;
    c.seventh = !strcmp(rc->mood, "jazz") || !strcmp(rc->mood, "lofi");
    o->bpm = g->bpm;
    if (!strcmp(rc->kind, "melody") || !strcmp(rc->kind, "arp"))
        o->bpm = !strcmp(rc->mood, "calm") || !strcmp(rc->mood, "sad") ? 84 : !strcmp(rc->mood, "epic") ? 100 : 116;
    if (req->bpm > 0)
        o->bpm = req->bpm;
    else if (req->bpm == -1)            /* "veloce" */
        o->bpm = o->bpm * 5 / 4;
    else if (req->bpm == -2)            /* "lento" */
        o->bpm = o->bpm * 4 / 5;
    o->swing = g->swing;
    o->key = c.key;
    o->minor = c.minor;
    o->meter = c.meter;
    o->bars = c.bars;
    progression(&c, rc->mood);
    if (req->ctx_bar && ctx_key >= 0)
        context_chords(&c, req);

    const char *ib, *ich, *iar, *ime;
    style_inst(style, rc->mood, &ib, &ich, &iar, &ime);
    if (req->inst[0]) {                 /* the instrument the words asked for */
        if (!strcmp(rc->kind, "bass")) ib = req->inst;
        else if (!strcmp(rc->kind, "arp")) iar = req->inst;
        else if (!strcmp(rc->kind, "melody")) ime = req->inst;
        else ich = req->inst;
    }
    if (!strcmp(rc->kind, "beat")) {
        drums(&c, g);
        o->room = 70;
    } else if (!strcmp(rc->kind, "base")) {
        drums(&c, g);
        bass(&c, !strcmp(style, "halftime") ? "root" : !strcmp(style, "bossa") || !strcmp(style, "shuffle") ? "walking"
                 : !strcmp(style, "house") || !strcmp(style, "disco") ? "octave" : style, ib);
        const char *cs = !strcmp(style, "ambient") || !strcmp(style, "synth") || !strcmp(style, "halftime") ? "pad"
                         : !strcmp(style, "reggae") ? "skank" : !strcmp(style, "house") || !strcmp(style, "techno") || !strcmp(style, "funk") ? "stabs"
                         : !strcmp(style, "waltz") ? "waltz" : !strcmp(style, "rock") || !strcmp(style, "metal") ? "rock"
                         : !strcmp(style, "lofi") || !strcmp(style, "bossa") ? "jazz" : "halves";
        chords(&c, cs, ich, 4, c.seventh ? 3 : 2);
        if (!strcmp(style, "synth") || !strcmp(style, "ambient") || !strcmp(style, "chip"))
            arp(&c, !strcmp(style, "ambient") ? "harp" : !strcmp(style, "synth") ? "trance" : "up", iar, 6,
                !strcmp(style, "ambient") ? 4 : 2);
        o->echo = !strcmp(style, "synth") || !strcmp(style, "ambient") ? 3 : 0;
        o->room = !strcmp(style, "ambient") ? 200 : 110;
    } else if (!strcmp(rc->kind, "bass")) {
        bass(&c, style, ib);
    } else if (!strcmp(rc->kind, "arp")) {
        arp(&c, style, !strcmp(rc->id, "arp.chip") ? "chip" : !strcmp(rc->id, "arp.harp") ? "harp" : iar, 5,
            !strcmp(style, "trance") || !strcmp(rc->id, "arp.chip") ? 1 : 2);
        o->echo = !strcmp(style, "trance") ? 3 : 0;
    } else if (!strcmp(rc->kind, "melody")) {
        float temp = 0.75f + 0.1f * (float)pick(&c.r, 4);
        melody(&c, ime, 6, temp);
        o->room = 100;
    }
    return 0;
}

/* ---------------------------------------------------------------- the words */

static int is_word(const char *t, const char *w)
{
    size_t n = strlen(w);
    return !strncmp(t, w, n) && !(t[n] >= 'a' && t[n] <= 'z');
}

static int starts(const char *t, const char *w) { return !strncmp(t, w, strlen(w)); }

/* the words of a request, lower case and plain letters, split */
#define MAXW 24
typedef struct { char w[MAXW][16]; int n; } words_t;

static void split(const char *s, words_t *ws)
{
    ws->n = 0;
    while (*s && ws->n < MAXW) {
        while (*s && !((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '#'
                       || (uint8_t)*s >= 0x80))
            s++;
        if (!*s)
            break;
        int j = 0;
        while (*s && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '#'
                      || (uint8_t)*s >= 0x80)) {
            char ch = *s++;
            if ((uint8_t)ch >= 0x80) {          /* accented letters (UTF-8): the vowel for the word */
                continue;
            }
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch + 32);
            if (j < 15)
                ws->w[ws->n][j++] = ch;
        }
        ws->w[ws->n][j] = 0;
        if (j)
            ws->n++;
    }
}

static int note_word(const char *w)
{
    static const struct { const char *w; int pc; } N[] = {
        { "do", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 }, { "sol", 7 }, { "la", 9 }, { "si", 11 },
        { "c", 0 }, { "d", 2 }, { "e", 4 }, { "f", 5 }, { "g", 7 }, { "a", 9 }, { "b", 11 },
    };
    for (unsigned i = 0; i < sizeof N / sizeof N[0]; i++) {
        size_t n = strlen(N[i].w);
        if (!strncmp(w, N[i].w, n)) {
            const char *rest = w + n;
            if (!*rest) return N[i].pc;
            if (!strcmp(rest, "#") || !strcmp(rest, "s")) return (N[i].pc + 1) % 12;
            if (!strcmp(rest, "b") && n == 1) return (N[i].pc + 11) % 12;
        }
    }
    return -1;
}

static int mode_word(const char *w)
{
    if (starts(w, "maggior") || is_word(w, "major") || is_word(w, "maj")) return 0;
    if (starts(w, "minor") || is_word(w, "min")) return 1;
    return -1;
}

void mus_parse(const char *text, mus_req_t *r)
{
    static words_t ws;
    split(text, &ws);
    int mood_minor_w = -1;
    for (int i = 0; i < ws.n; i++) {
        const char *w = ws.w[i];
        const char *nx = i + 1 < ws.n ? ws.w[i + 1] : "";
        const char *nx2 = i + 2 < ws.n ? ws.w[i + 2] : "";
        /* the key: "in la minore", "la minore", "in A", "C major", "Am", "tonalita' re" */
        int pc = note_word(w);
        if (pc >= 0) {
            int sharp = 0, flat = 0;
            const char *after = nx;
            if (is_word(nx, "diesis") || is_word(nx, "sharp")) { sharp = 1; after = nx2; }
            if (is_word(nx, "bemolle") || is_word(nx, "flat")) { flat = 1; after = nx2; }
            int m = mode_word(after);
            int prev_in = i > 0 && (is_word(ws.w[i - 1], "in") || starts(ws.w[i - 1], "tonalit") || is_word(ws.w[i - 1], "key")
                                    || is_word(ws.w[i - 1], "of"));
            if (m >= 0 || prev_in) {
                r->key = (pc + sharp * 1 + flat * 11) % 12;
                if (m >= 0) r->minor = m;
            }
        }
        int mw = mode_word(w);
        if (mw >= 0 && (i == 0 || note_word(ws.w[i - 1]) < 0))
            r->minor = r->minor < 0 ? mw : r->minor;
        /* moods */
        if (starts(w, "trist") || is_word(w, "sad") || starts(w, "malincon") || starts(w, "cup") || is_word(w, "dark")
            || starts(w, "oscur") || starts(w, "spook") || starts(w, "pauros") || starts(w, "horror") || starts(w, "misterios")
            || starts(w, "myster"))
            mood_minor_w = 1;
        if (starts(w, "allegr") || is_word(w, "happy") || starts(w, "felic") || starts(w, "gioios") || starts(w, "solar")
            || is_word(w, "bright") || starts(w, "cheer"))
            mood_minor_w = 0;
        /* tempo */
        if ((is_word(nx, "bpm") || is_word(nx, "battiti")) && w[0] >= '0' && w[0] <= '9') {
            int b = 0;
            for (const char *p = w; *p >= '0' && *p <= '9'; p++) b = b * 10 + (*p - '0');
            if (b >= 40 && b <= 250) r->bpm = b;
        }
        /* length */
        if ((starts(nx, "battut") || is_word(nx, "bars") || is_word(nx, "bar") || starts(nx, "misur")) && w[0] >= '0' && w[0] <= '9') {
            int b = 0;
            for (const char *p = w; *p >= '0' && *p <= '9'; p++) b = b * 10 + (*p - '0');
            if (b >= 1 && b <= 16) r->bars = b;
        }
        if (starts(w, "lung") || is_word(w, "long")) r->bars = 16;
        if (starts(w, "cort") || is_word(w, "short")) r->bars = 4;
        /* instruments */
        static const struct { const char *w, *inst; } I[] = {
            { "piano", "epiano" }, { "pianoforte", "epiano" }, { "chitarr", "guitar" }, { "guitar", "guitar" },
            { "organ", "organ" }, { "flaut", "flute" }, { "flute", "flute" }, { "arpa", "harp" }, { "harp", "harp" },
            { "campan", "bell" }, { "bell", "bell" }, { "archi", "strings" }, { "string", "strings" },
            { "violin", "strings" }, { "synth", "sawlead" }, { "sintetizz", "sawlead" }, { "chiptune", "chip" },
            { "8bit", "chip" }, { "retro", "chip" }, { "ottoni", "brass" }, { "brass", "brass" }, { "tromb", "brass" },
            { "trumpet", "brass" }, { "fiati", "brass" }, { "marimba", "marimba" }, { "xilofono", "marimba" },
            { "xylophone", "marimba" }, { "glockenspiel", "glock" }, { "carillon", "glock" },
        };
        for (unsigned k = 0; k < sizeof I / sizeof I[0]; k++)
            if (starts(w, I[k].w)) {
                strncpy(r->inst, I[k].inst, 8);
                r->inst[8] = 0;
            }
        if (is_word(w, "8") && (is_word(nx, "bit") || starts(nx, "bit"))) {
            strcpy(r->inst, "chip");
        }
    }
    if (r->minor < 0 && mood_minor_w >= 0)
        r->minor = mood_minor_w;
    /* fast and slow scale the recipe's tempo: the caller applies them after mus_make */
    for (int i = 0; i < ws.n; i++) {
        const char *w = ws.w[i];
        if (!r->bpm && (starts(w, "veloc") || is_word(w, "fast") || starts(w, "rapid") || starts(w, "frenet") || is_word(w, "quick")))
            r->bpm = -1;
        if (!r->bpm && (starts(w, "lent") || is_word(w, "slow") || starts(w, "tranquill")))
            r->bpm = -2;
    }
}

/* ---------------------------------------------------------------- the words alone */

const char *mus_guess(const char *text)
{
    static words_t ws;
    split(text, &ws);
    /* a sound effect, by its word */
    static const struct { const char *w, *gen; } SFX[] = {
        { "monet", "sfx.coin" }, { "coin", "sfx.coin" }, { "sold", "sfx.coin" }, { "salt", "sfx.jump" },
        { "jump", "sfx.jump" }, { "laser", "sfx.laser" }, { "spar", "sfx.shoot" }, { "shoot", "sfx.shoot" },
        { "shot", "sfx.shoot" }, { "esplo", "sfx.explosion" }, { "explo", "sfx.explosion" }, { "bomb", "sfx.explosion" },
        { "power", "sfx.powerup" }, { "potenz", "sfx.powerup" }, { "livell", "sfx.levelup" }, { "level", "sfx.levelup" },
        { "dann", "sfx.hurt" }, { "hurt", "sfx.hurt" }, { "ferit", "sfx.hurt" }, { "pugn", "sfx.hit" },
        { "colp", "sfx.hit" }, { "hit", "sfx.hit" }, { "selez", "sfx.select" }, { "select", "sfx.select" },
        { "confer", "sfx.select" }, { "indietro", "sfx.back" }, { "back", "sfx.back" }, { "annull", "sfx.back" },
        { "allarm", "sfx.alarm" }, { "alarm", "sfx.alarm" }, { "siren", "sfx.alarm" }, { "raccog", "sfx.pickup" },
        { "pickup", "sfx.pickup" }, { "gemm", "sfx.pickup" }, { "port", "sfx.door" }, { "door", "sfx.door" },
        { "pass", "sfx.step" }, { "footstep", "sfx.step" }, { "mort", "sfx.death" }, { "death", "sfx.death" },
        { "muor", "sfx.death" }, { "vittor", "sfx.win" }, { "win", "sfx.win" }, { "victory", "sfx.win" },
        { "fanfar", "sfx.win" }, { "sconfit", "sfx.lose" }, { "lose", "sfx.lose" }, { "over", "sfx.lose" },
        { "telet", "sfx.teleport" }, { "telep", "sfx.teleport" }, { "portal", "sfx.teleport" }, { "acqua", "sfx.splash" },
        { "splash", "sfx.splash" }, { "tuff", "sfx.splash" }, { "tuon", "sfx.thunder" }, { "thunder", "sfx.thunder" },
        { "fulmin", "sfx.thunder" }, { "scatt", "sfx.dash" }, { "dash", "sfx.dash" }, { "rimbal", "sfx.bounce" },
        { "bounce", "sfx.bounce" }, { "mol", "sfx.bounce" }, { "caric", "sfx.charge" }, { "charge", "sfx.charge" },
        { "cura", "sfx.heal" }, { "heal", "sfx.heal" }, { "guar", "sfx.heal" }, { "testo", "sfx.typing" },
        { "typing", "sfx.typing" }, { "dialog", "sfx.typing" }, { "campan", "sfx.bell" }, { "bell", "sfx.bell" },
        { "blip", "sfx.blip" }, { "bip", "sfx.blip" }, { "beep", "sfx.blip" },
    };
    /* the style or the mood */
    static const struct { const char *w, *style; } STY[] = {
        { "rock", "rock" }, { "pop", "pop" }, { "funk", "funk" }, { "disco", "disco" }, { "house", "house" },
        { "techno", "techno" }, { "hip", "hiphop" }, { "rap", "hiphop" }, { "boom", "hiphop" }, { "trap", "trap" },
        { "dnb", "dnb" }, { "jungle", "dnb" }, { "reggae", "reggae" }, { "bossa", "bossa" }, { "samba", "bossa" },
        { "valzer", "waltz" }, { "waltz", "waltz" }, { "marci", "march" }, { "march", "march" }, { "shuffle", "shuffle" },
        { "blues", "blues" }, { "metal", "metal" }, { "chip", "chip" }, { "retro", "chip" }, { "lofi", "lofi" },
        { "chill", "lofi" }, { "epic", "epic" }, { "trist", "sad" }, { "sad", "sad" }, { "malinc", "sad" },
        { "allegr", "happy" }, { "happy", "happy" }, { "felic", "happy" }, { "mister", "mysterious" },
        { "myster", "mysterious" }, { "calm", "calm" }, { "rilass", "calm" }, { "eroic", "heroic" }, { "hero", "heroic" },
        { "jazz", "jazz" }, { "spook", "spooky" }, { "horror", "spooky" }, { "paur", "spooky" }, { "avventur", "adventure" },
        { "advent", "adventure" }, { "boss", "boss" }, { "villag", "town" }, { "town", "town" }, { "citt", "town" },
        { "dungeon", "dungeon" }, { "grott", "dungeon" }, { "cave", "dungeon" }, { "spazi", "space" }, { "space", "space" },
        { "ambient", "ambient" }, { "atmosf", "ambient" }, { "synth", "synthwave" }, { "trance", "trance" },
        { "acid", "acid" }, { "walking", "walking" }, { "ottav", "octave" }, { "octave", "octave" }, { "arpa", "harp" },
        { "harp", "harp" }, { "albert", "alberti" }, { "scend", "down" }, { "down", "down" }, { "spezz", "broken" },
        { "broken", "broken" },
    };
    const char *style = NULL, *cat = NULL;
    int eight = 0;
    for (int i = 0; i < ws.n; i++) {
        const char *w = ws.w[i];
        if (is_word(w, "8") && i + 1 < ws.n && starts(ws.w[i + 1], "bit"))
            eight = 1;
        if (starts(w, "8bit"))
            eight = 1;
        if (!cat) {
            if (starts(w, "melod") || is_word(w, "tema") || is_word(w, "theme") || is_word(w, "tune")) cat = "melody";
            else if (starts(w, "arpeg") || is_word(w, "arp")) cat = "arp";
            else if (is_word(w, "basso") || is_word(w, "bass") || starts(w, "bassline")) cat = "bass";
            else if (is_word(w, "base") || starts(w, "accompagn") || starts(w, "backing") || starts(w, "musica")
                     || is_word(w, "music") || starts(w, "colonna") || starts(w, "track")) cat = "base";
            else if (starts(w, "ritm") || starts(w, "batter") || is_word(w, "beat") || starts(w, "drum")
                     || starts(w, "groove")) cat = "beat";
            else if (starts(w, "effett") || is_word(w, "sfx") || starts(w, "suon") || is_word(w, "sound")) cat = "sfx";
        }
        for (unsigned k = 0; k < sizeof STY / sizeof STY[0] && !style; k++)
            if (starts(w, STY[k].w))
                style = STY[k].style;
    }
    if (eight && !style)
        style = "chip";
    /* a sound effect's own word wins when no other kind is asked */
    if (!cat || !strcmp(cat, "sfx"))
        for (int i = 0; i < ws.n; i++)
            for (unsigned k = 0; k < sizeof SFX / sizeof SFX[0]; k++)
                if (starts(ws.w[i], SFX[k].w) && strlen(ws.w[i]) >= 3)
                    return SFX[k].gen;
    if (!cat)
        cat = style && (!strcmp(style, "sad") || !strcmp(style, "happy") || !strcmp(style, "heroic")
                        || !strcmp(style, "mysterious") || !strcmp(style, "calm") || !strcmp(style, "spooky")) ? "melody"
              : "beat";
    if (!strcmp(cat, "sfx"))
        return "sfx.blip";
    /* the recipe of this kind with the style, or the kind's first */
    static char id[32];
    const char *alias = style;
    if (style && !strcmp(cat, "melody")) {
        if (!strcmp(style, "chip")) alias = "chip";
        else if (!strcmp(style, "jazz") || !strcmp(style, "lofi")) alias = "jazzy";
        else if (!strcmp(style, "boss") || !strcmp(style, "metal")) alias = "epic";
    }
    if (style && !strcmp(cat, "base") && !strcmp(style, "chip")) alias = "chiptune";
    if (style && !strcmp(cat, "arp") && !strcmp(style, "harp")) alias = "harp";
    if (alias) {
        size_t lc = strlen(cat), la = strlen(alias);
        if (lc + 1 + la < sizeof id) {
            memcpy(id, cat, lc);
            id[lc] = '.';
            memcpy(id + lc + 1, alias, la + 1);
            if (mus_find(id) >= 0)
                return mus_recipe_id(mus_find(id));
        }
    }
    static const struct { const char *cat, *first; } FIRST[] = {
        { "melody", "melody.happy" }, { "arp", "arp.up" }, { "bass", "bass.root" }, { "base", "base.pop" },
        { "beat", "beat.pop" },
    };
    for (unsigned k = 0; k < sizeof FIRST / sizeof FIRST[0]; k++)
        if (!strcmp(cat, FIRST[k].cat))
            return FIRST[k].first;
    return "beat.pop";
}
