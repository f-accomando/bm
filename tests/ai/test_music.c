/* The music assistant on the PC (make test-music): the melody network in C
 * against the Python reference bit for bit (build/ai/music_check.txt, by
 * trainmusic.py --check), every recipe with several seeds (instruments
 * that exist, notes in the key, lengths, determinism), the words of the
 * requests; with an argument, a WAV of some of them made by the console's
 * synthesizer (for listening):
 *   test_music [OUTDIR] */
#include "ai/music.h"
#include "audio/presets.h"
#include "audio/player.h"
#include "audio/synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_network(void)
{
    FILE *f = fopen("build/ai/music_check.txt", "r");
    CHECK(f != NULL);
    if (!f)
        return;
    static char line[4096];
    int n = 0, same = 0;
    while (fgets(line, sizeof line, f)) {
        uint8_t feat[MUS_NFEAT];
        for (int i = 0; i < MUS_NFEAT; i++)
            feat[i] = (uint8_t)(line[i] == '1');
        int32_t want[MUS_NOUT], got[MUS_NOUT];
        char *p = line + MUS_NFEAT;
        for (int i = 0; i < MUS_NOUT; i++)
            want[i] = (int32_t)strtol(p, &p, 10);
        CHECK(mus_net_run(feat, got) == MUS_NOUT);
        same += !memcmp(want, got, sizeof got);
        n++;
    }
    fclose(f);
    printf("music: the network on %d moments of the tunes: %d the same as Python\n", n, same);
    CHECK(n > 1000 && same == n);
}

/* the features in C and in Python agree too: the first moments of a tune
 * from melodies.txt, Ode to Joy (3 3 4 5 | 5 4 3 2 over I | V) */
static void test_features(void)
{
    mus_ev_t h[3] = { { 2, 4 }, { 2, 4 }, { 3, 4 } };
    uint8_t f[MUS_NFEAT];
    mus_features(h, 3, 12, 16, 0, 0, 0, 0, 0, f);
    CHECK(f[0 * 17 + 8] && f[1 * 17 + 7] && f[2 * 17 + 16]);     /* +1, 0, nothing before */
    CHECK(f[51 + 3] && f[60 + 3]);                              /* quarters */
    CHECK(f[69 + 3] && f[76 + 1] && f[80 + 12] && f[96] && f[99] && f[106] && f[109] && !f[112] && !f[115]);
}

static int in_scale(int note, int key, int minor)
{
    static const int MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 }, MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };
    int pc = ((note - key) % 12 + 12) % 12;
    for (int i = 0; i < 7; i++)
        if ((minor ? MIN : MAJ)[i] == pc)
            return 1;
    return 0;
}

static mus_out_t out, again;

static void test_recipes(void)
{
    int bad_inst = 0, bad_note = 0, odd_melody = 0, total_notes = 0;
    for (int i = 0; i < mus_recipes(); i++) {
        const char *id = mus_recipe_id(i);
        for (uint32_t seed = 1; seed <= 6; seed++) {
            mus_req_t r;
            mus_req_init(&r, id);
            r.seed = seed;
            CHECK(mus_make(&r, &out) == 0);
            CHECK(mus_make(&r, &again) == 0 && !memcmp(&out, &again, sizeof out));    /* the same seed, the same piece */
            for (int k = 0; k < out.ninst; k++)
                if (au_preset_find(out.inst[k]) < 0) {
                    bad_inst++;
                    printf("  %s: no instrument %s\n", id, out.inst[k]);
                }
            if (!strcmp(out.kind, "sfx")) {
                CHECK(out.sfx_len >= 2 && out.sfx_len <= MUS_SFX_STEPS && out.sfx_ms >= 10);
                CHECK(out.sfx[out.sfx_len - 1].note == 128 || (out.sfx_loop1 > out.sfx_loop0));
                continue;
            }
            CHECK(out.npat >= 1 && out.npat <= MUS_PATS && out.bars >= 1 && out.nchords == out.bars);
            CHECK(out.bpm >= 40 && out.bpm <= 250);
            int notes = 0;
            for (int p = 0; p < out.npat; p++) {
                CHECK(out.pat[p].len == 4 * out.meter);
                for (int t = 0; t < MUS_TRACKS; t++)
                    for (int s = 0; s < out.pat[p].len; s++) {
                        mus_step_t st = out.pat[p].step[t][s];
                        if (!st.note || st.note == 128)
                            continue;
                        notes++;
                        if (st.note > 127 || st.inst >= out.ninst || !st.vol)
                            bad_note++;
                        /* the melody keeps to the key's scale */
                        if (!strcmp(out.kind, "melody") && t == 6 && !in_scale(st.note, out.key, out.minor))
                            odd_melody++;
                    }
            }
            total_notes += notes;
            CHECK(notes >= (!strcmp(out.kind, "melody") ? 8 : 4));
            /* rhythms: kick and snare on their tracks */
            if (!strcmp(out.kind, "beat") || !strcmp(out.kind, "base"))
                CHECK((out.pat[0].used & 1) && (out.pat[0].used & 2));
            if (!strcmp(out.kind, "melody"))
                CHECK(out.pat[0].used & 1 << 6);
        }
    }
    printf("music: %d recipes x 6 seeds, %d notes\n", mus_recipes(), total_notes);
    CHECK(bad_inst == 0 && bad_note == 0 && odd_melody == 0);
    CHECK(mus_find("melody.happy") >= 0 && mus_find("nothing") < 0);
    mus_req_t r;
    mus_req_init(&r, "nothing");
    CHECK(mus_make(&r, &out) == -1);

    /* different seeds, different melodies */
    mus_req_init(&r, "melody.happy");
    r.seed = 1;
    mus_make(&r, &out);
    r.seed = 2;
    mus_make(&r, &again);
    CHECK(memcmp(out.pat, again.pat, sizeof out.pat) != 0);

    /* a melody over the chords already there: a bar of A minor, a bar of
     * F, a bar of C, a bar of G (the notes of a backing track) */
    static const uint8_t notes[] = { 45, 57, 60, 64, 41, 53, 57, 60, 48, 55, 60, 64, 43, 55, 59, 62 };
    static const uint8_t bars[] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
    mus_req_init(&r, "melody.calm");
    r.ctx = notes;
    r.ctx_bar = bars;
    r.nctx = 16;
    r.bars = 4;
    mus_make(&r, &out);
    printf("music: over Am F C G it heard %s %s, %s %s %s %s\n", out.minor ? "minor" : "major",
           (const char *[]){ "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }[out.key],
           out.chords[0], out.chords[1], out.chords[2], out.chords[3]);
    CHECK((out.key == 0 && !out.minor) || (out.key == 9 && out.minor));
    CHECK(!strcmp(out.chords[0], "Am") && !strcmp(out.chords[1], "F") && !strcmp(out.chords[2], "C")
          && !strcmp(out.chords[3], "G"));
    /* a backing track goes into patterns of its own: the notes there do
     * not change its key ("base lofi in re" stays in D major) */
    mus_req_init(&r, "base.lofi");
    mus_parse("base lofi in re", &r);
    r.ctx = notes;
    r.ctx_bar = bars;
    r.nctx = 16;
    mus_make(&r, &out);
    CHECK(out.key == 2 && !out.minor);
}

static void test_words(void)
{
    mus_req_t r;
    mus_req_init(&r, "melody.happy");
    mus_parse("una melodia in la minore, 90 bpm, 8 battute, con il piano", &r);
    CHECK(r.key == 9 && r.minor == 1 && r.bpm == 90 && r.bars == 8 && !strcmp(r.inst, "epiano"));
    mus_req_init(&r, "base.pop");
    mus_parse("pop base in C major", &r);
    CHECK(r.key == 0 && r.minor == 0);
    mus_req_init(&r, "base.pop");
    mus_parse("fa diesis minore", &r);
    CHECK(r.key == 6 && r.minor == 1);
    mus_req_init(&r, "melody.happy");
    mus_parse("I am happy, la melodia del menu", &r);         /* "am" and "la" are words here */
    CHECK(r.key == -1 && r.minor == 0);
    mus_req_init(&r, "melody.sad");
    mus_parse("melodia triste e lenta con la chitarra", &r);
    CHECK(r.minor == 1 && r.bpm == -2 && !strcmp(r.inst, "guitar"));
    mus_req_init(&r, "beat.rock");
    mus_parse("ritmo rock veloce 8 bit", &r);
    CHECK(r.bpm == -1 && !strcmp(r.inst, "chip"));
    /* the words alone pick a recipe (the RGB30, where the network is not) */
    CHECK(!strcmp(mus_guess("fammi un ritmo rock"), "beat.rock"));
    CHECK(!strcmp(mus_guess("effetto della moneta"), "sfx.coin"));
    CHECK(!strcmp(mus_guess("una melodia triste"), "melody.sad"));
    CHECK(!strcmp(mus_guess("base chiptune 8 bit"), "base.chiptune"));
    CHECK(!strcmp(mus_guess("arpeggio trance"), "arp.trance"));
    CHECK(!strcmp(mus_guess("walking bass"), "bass.walking"));
    CHECK(!strcmp(mus_guess("the sound of a jump"), "sfx.jump"));
    CHECK(!strcmp(mus_guess("qualcosa"), "beat.pop"));
    mus_req_init(&r, "beat.rock");
    mus_parse("ritmo rock", &r);
    mus_make(&r, &out);
    int rock = out.bpm;
    mus_parse("ritmo rock veloce", &r);
    mus_make(&r, &out);
    CHECK(out.bpm > rock);
}

/* ---------------------------------------------------------------- listening */

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

/* the piece as a bank (the instruments its sounds) played by the console's player */
static void render(const mus_out_t *o, const char *path, int seconds)
{
    static au_bank_t b;
    memset(&b, 0, sizeof b);
    b.nsounds = (uint8_t)o->ninst;
    for (int i = 0; i < o->ninst; i++)
        b.sound[i] = au_presets[au_preset_find(o->inst[i])].s;
    b.npatterns = (uint8_t)o->npat;
    for (int p = 0; p < o->npat; p++) {
        b.pat[p].len = (uint8_t)o->pat[p].len;
        for (int t = 0; t < MUS_TRACKS; t++)
            for (int s = 0; s < o->pat[p].len; s++) {
                mus_step_t st = o->pat[p].step[t][s];
                b.pat[p].step[t][s] = (au_step_t){ st.note, st.inst, st.vol, st.fx };
            }
    }
    b.nsongs = 1;
    b.song[0].bpm = (uint8_t)o->bpm;
    b.song[0].swing = (uint8_t)o->swing;
    b.song[0].len = (uint8_t)o->npat;
    b.song[0].loop = 0;
    b.song[0].echo = (uint8_t)o->echo;
    b.song[0].room = (uint8_t)o->room;
    for (int p = 0; p < o->npat; p++)
        b.song[0].order[p] = (uint8_t)p;
    if (!strcmp(o->kind, "sfx")) {
        b.nsfx = 1;
        b.sfx[0].ms = (uint16_t)o->sfx_ms;
        b.sfx[0].len = (uint8_t)o->sfx_len;
        for (int s = 0; s < o->sfx_len; s++)
            b.sfx[0].step[s] = (au_step_t){ o->sfx[s].note, o->sfx[s].inst, o->sfx[s].vol, o->sfx[s].fx };
    }
    static synth_t syn;
    static player_t pl;
    static uint8_t regs[SYNTH_REG_BYTES];
    synth_init(&syn, 48000);
    player_init(&pl, 48000, regs, &syn);
    for (int ch = 0; ch < SYNTH_VOICES; ch++)
        au_voice_default(regs + ch * SYNTH_VOICE_BYTES);
    player_set_bank(&pl, &b);
    if (b.nsfx)
        player_sfx(&pl, 0, 0, 0, 1.0f);
    else
        player_music(&pl, 0, 0, 0);
    uint32_t n = (uint32_t)seconds * 48000;
    int16_t *pcm = malloc(n * 4 + 512);
    for (uint32_t k = 0; k < n; k += 64) {
        player_advance(&pl, 64);
        synth_render(&syn, regs, pcm + 2 * k, 64);
    }
    FILE *f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 48000); put32(f, 48000 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, n * 4); fwrite(pcm, 4, n, f);
    fclose(f);
    free(pcm);
}

int main(int argc, char **argv)
{
    test_features();
    test_network();
    test_recipes();
    test_words();
    if (argc > 1) {
        static const char *const show[] = { "base.pop", "base.synthwave", "base.lofi", "base.chiptune", "base.epic",
                                            "base.reggae", "beat.funk", "arp.trance", "melody.happy", "melody.sad",
                                            "melody.epic", "melody.chip", "sfx.coin", "sfx.jump", "sfx.powerup",
                                            "sfx.explosion", "sfx.laser", "sfx.win", NULL };
        for (int i = 0; show[i]; i++) {
            mus_req_t r;
            mus_req_init(&r, show[i]);
            r.seed = 1;
            mus_make(&r, &out);
            char path[512];
            snprintf(path, sizeof path, "%s/%s.wav", argv[1], show[i]);
            int secs = !strcmp(out.kind, "sfx") ? 2 : out.bars * out.meter * 15 / out.bpm + 3;
            render(&out, path, secs);
            printf("  %s: %s, %d BPM, key %d %s, chords", path, out.name, out.bpm, out.key, out.minor ? "minor" : "major");
            for (int b = 0; b < out.nchords; b++)
                printf(" %s", out.chords[b]);
            printf("\n");
        }
    }
    printf("music: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
