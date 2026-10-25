/*
 * api_sound.c: sound (the audio bindings and the bank).
 * Moved from runtime.c (docs/NEXT.md, item 0b step 5). It shares the run
 * state through rt_internal.h.
 */
#ifdef BM_RGB30
#include "display.h"            /* fb_init_game */
#endif
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "world3d.h"
#include "map3d.h"
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "platform.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "vid.h"
#include "rec.h"
#include "lib/crc32.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "audio/presets.h"
#include "audio/lua_tone.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "n8lua.h"
#include "shot.h"
#include "webfile.h"
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"
#include "profile.h"
#include "project.h"
#include "rt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* ---------------------------------------------------------------- sound */

static unsigned voice_arg(lua_State *L)
{
    lua_Integer ch = luaL_checkinteger(L, 1);
    luaL_argcheck(L, ch >= 0 && ch < 8, 1, "voice 0..7");
    return (unsigned)ch;
}

/* "C4", "c#4", "Db3", "A-1", "F#-1" -> MIDI note (C4 = 60). 1 if valid. */
static int note_name(const char *s, float *midi)
{
    static const int8_t pc[7] = { 9, 11, 0, 2, 4, 5, 7 };      /* A B C D E F G */
    char c = (char)(*s >= 'a' ? *s - 32 : *s);
    if (c < 'A' || c > 'G')
        return 0;
    int n = pc[c - 'A'];
    s++;
    if (*s == '#' || *s == 's') { n++; s++; }
    else if (*s == 'b') { n--; s++; }
    if (*s == '-' && s[1] >= '0' && s[1] <= '9' && s[2])
        s++;                                    /* tracker style: "C-4" */
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (*s < '0' || *s > '9')
        return 0;
    int oct = 0;
    while (*s >= '0' && *s <= '9')
        oct = oct * 10 + (*s++ - '0');
    if (*s || oct > 9)
        return 0;
    *midi = (float)(12 * ((neg ? -oct : oct) + 1) + n);
    return 1;
}

/* a pitch: Hz, or a note name ("A4", "C#5", "Bb3") */
static float hz_arg(lua_State *L, int i)
{
    if (lua_type(L, i) == LUA_TSTRING) {
        float m = 0;
        if (!note_name(lua_tostring(L, i), &m))
            luaL_argerror(L, i, "a note name like \"C4\", \"F#3\" or \"Bb2\"");
        return au_note_hz(m);
    }
    lua_Number f = luaL_checknumber(L, i);
    return f <= 0 ? 0.0f : f >= 65535 ? 65535.0f : (float)f;
}

/* hz(note): a MIDI note number (60 = C4, 69 = A4) or a name -> Hz */
int l_hz(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING) {
        lua_pushnumber(L, hz_arg(L, 1));
        return 1;
    }
    lua_pushnumber(L, au_note_hz((float)luaL_checknumber(L, 1)));
    return 1;
}

static int wave_arg(lua_State *L, int i)
{
    lua_Integer w = luaL_optinteger(L, i, -1);
    return w >= 0 && w < SYNTH_WAVES ? (int)w : -1;
}

/* note(ch, freq, [ms], [wave], [vol]): restarts the envelope; ms > 0
 * releases the note by itself, else it holds until noteoff(ch). freq in
 * Hz or a note name. */
int l_note(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 0);
    audio_note(ch, hz, ms > 0 ? (uint32_t)ms : 0, wave_arg(L, 4), (int)luaL_optinteger(L, 5, -1));
    return 0;
}

int l_noteoff(lua_State *L)
{
    audio_note_off(voice_arg(L));
    return 0;
}

/* freq(ch, hz): changes the pitch without restarting (slides, vibrato) */
int l_freq(lua_State *L)
{
    audio_freq(voice_arg(L), hz_arg(L, 2));
    return 0;
}

int l_envelope(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_envelope(ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                   (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

int l_duty(lua_State *L)
{
    audio_duty(voice_arg(L), (int)luaL_checkinteger(L, 2));
    return 0;
}

/* playing(ch): true while the voice sounds (release included) or a sound
 * effect or the music holds it */
int l_playing(lua_State *L)
{
    lua_pushboolean(L, audio_busy(voice_arg(L)));
    return 1;
}

/* apu(ch, reg, [value]): raw register byte 0..31, the synthesizer's layout (synth.h) */
int l_apu(lua_State *L)
{
    unsigned ch = voice_arg(L);
    lua_Integer reg = luaL_checkinteger(L, 2);
    luaL_argcheck(L, reg >= 0 && reg < SYNTH_VOICE_BYTES, 2, "register 0..31");
    volatile uint8_t *r = audio_regs() + ch * SYNTH_VOICE_BYTES + reg;
    if (lua_gettop(L) >= 3) {
        *r = (uint8_t)luaL_checkinteger(L, 3);
        return 0;
    }
    lua_pushinteger(L, *r);
    return 1;
}

/* slide(ch, freq, ms): the note of the voice glides to freq */
int l_slide(lua_State *L)
{
    unsigned ch = voice_arg(L);
    float hz = hz_arg(L, 2);
    lua_Integer ms = luaL_optinteger(L, 3, 100);
    audio_slide(ch, hz, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* vibrato(ch, [semitones], [rate_hz]): vibrato(ch) turns it off */
int l_vibrato(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_vibrato(ch, (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 6));
    return 0;
}

static const char *const chord_names[AU_CHORDS + 1] = {
    "octave", "major", "minor", "sus2", "sus4", "maj7", "min7", "7", "dim", "aug",
    "power", "power8", "major8", "minor8", "down", "octaves", NULL
};

/* arp(ch, chord, [ms]): the note runs through a chord, ms per note
 * (default 50): a name ("major", "minor", "maj7", "min7", "7", "sus2",
 * "sus4", "dim", "aug", "power", "octave"...), or a table of semitones
 * ({0, 4, 7, 12}); arp(ch) turns it off */
int l_arp(lua_State *L)
{
    unsigned ch = voice_arg(L);
    int8_t semis[8];
    int n = 0;
    if (lua_istable(L, 2)) {
        n = (int)luaL_len(L, 2);
        luaL_argcheck(L, n >= 1 && n <= 8, 2, "1 to 8 semitones");
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, 2, i + 1);
            lua_Integer v = luaL_checkinteger(L, -1);
            semis[i] = (int8_t)(v < -48 ? -48 : v > 48 ? 48 : v);
            lua_pop(L, 1);
        }
    } else if (!lua_isnoneornil(L, 2)) {
        int c = lua_type(L, 2) == LUA_TNUMBER ? (int)lua_tointeger(L, 2) : luaL_checkoption(L, 2, NULL, chord_names);
        luaL_argcheck(L, c >= 0 && c < AU_CHORDS, 2, "chord 0..15");
        n = au_chord_len[c];
        memcpy(semis, au_chord[c], (size_t)n);
    }
    lua_Integer ms = luaL_optinteger(L, 3, 50);
    audio_arp(ch, semis, n, ms > 0 ? (uint32_t)ms : 0);
    return 0;
}

/* sfx(n, [voice], [transpose], [vol]): sound effect n of the cartridge's
 * bank on a voice (nil: a free one), transposed by semitones, at volume
 * 0..1; returns the voice, or nil. sfx(-1, [voice]) stops the effect of a
 * voice, or all of them. */
int l_sfx(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    int ch = -1;
    if (!lua_isnoneornil(L, 2)) {
        lua_Integer v = luaL_checkinteger(L, 2);
        luaL_argcheck(L, v >= 0 && v < 8, 2, "voice 0..7");
        ch = (int)v;
    }
    if (n < 0) {
        audio_sfx_stop(ch);
        return 0;
    }
    int v = audio_sfx((int)n, ch, (int)luaL_optinteger(L, 3, 0), (float)luaL_optnumber(L, 4, 1.0));
    if (v < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, v);
    return 1;
}

/* sfxpos(voice) -> the effect playing on the voice and its step, or nil */
int l_sfxpos(lua_State *L)
{
    int step = 0, n = audio_sfx_pos((int)voice_arg(L), &step);
    if (n < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, n);
    lua_pushinteger(L, step);
    return 2;
}

/* music(n, [fade_ms], [position]): song n of the bank, from a position
 * (0 = the first pattern), fading in; music(-1, [fade_ms]) stops it;
 * music() -> song, position, step, pattern while it plays, else nil */
int l_music(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        int song, order, step, pat;
        if (!audio_music_pos(&song, &order, &step, &pat)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, song);
        lua_pushinteger(L, order);
        lua_pushinteger(L, step);
        lua_pushinteger(L, pat);
        return 4;
    }
    lua_Integer n = luaL_checkinteger(L, 1);
    lua_Integer fade = luaL_optinteger(L, 2, 0);
    if (n < 0)
        audio_music_stop((int)fade);
    else
        audio_music((int)n, (int)luaL_optinteger(L, 3, 0), (int)fade);
    return 0;
}

/* tempo(scale): the music plays scale times faster (1 = as written) */
int l_tempo(lua_State *L)
{
    audio_tempo((float)luaL_checknumber(L, 1));
    return 0;
}

/* mute(track, [on]): a track of the music goes silent (on, the default)
 * or plays again */
int l_mute(lua_State *L)
{
    lua_Integer t = luaL_checkinteger(L, 1);
    luaL_argcheck(L, t >= 0 && t < 8, 1, "track 0..7");
    audio_mute((int)t, lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2));
    return 0;
}

/* volume([level]) -> the master volume 0..10 (set it with level); the
 * console keeps it in its settings */
int l_volume(lua_State *L)
{
    if (!lua_isnoneornil(L, 1))
        audio_set_volume((int)luaL_checkinteger(L, 1));
    lua_pushinteger(L, audio_volume());
    return 1;
}

/* audio_bank(data): plays this bank (a string, as cart_audio() gives it)
 * from now on; nil: none. true, or nil and a message. For the editor. */
int l_audio_bank(lua_State *L)
{
    size_t len = 0;
    const char *d = lua_isnoneornil(L, 1) ? NULL : luaL_checklstring(L, 1, &len);
    char err[64];
    if (audio_bank((const uint8_t *)d, len, err, sizeof err) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* audio_samples(["kit"]): the samples of the bank playing (entry n + 1 is
 * the one a sound's MOD1 n plays), or the console's drum kit (MOD1 128 +
 * k): each { name, frames, rate (Hz), channels, root (MIDI note), fine
 * (cents), loop (0 off, 1 forward, 2 ping-pong), loop_start, loop_end
 * (frames), peak (0..1 of full scale), meter (AUDIO_METER peaks, 0..1,
 * from its start to its end) }. For bm Sound's meter. */
#define AUDIO_METER 32
int l_audio_samples(lua_State *L)
{
    const int kit = !lua_isnoneornil(L, 1) && !strcmp(luaL_checkstring(L, 1), "kit");
    const au_bank_t *b = kit ? NULL : audio_bank_now();
    const unsigned n = kit ? SYNTH_KIT_SIZE : b ? b->nsamples : 0;
    lua_createtable(L, (int)n, 0);
    for (unsigned i = 0; i < n; i++) {
        const synth_sample_t *x = kit ? synth_kit(i) : &b->sample[i];
        lua_createtable(L, 0, 12);
        lua_pushstring(L, kit ? synth_kit_name(i) : b->sample_name[i]);
        lua_setfield(L, -2, "name");
        static const char *const keys[] = { "frames", "rate", "channels", "root", "fine", "loop", "loop_start",
                                            "loop_end" };
        const lua_Integer v[] = { x->len, x->rate, x->channels, x->root, x->fine, x->loop, x->loop_start,
                                  x->loop_end };
        for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
            lua_pushinteger(L, v[k]);
            lua_setfield(L, -2, keys[k]);
        }
        /* the peaks: the whole sample, and AUDIO_METER parts of it */
        int peak = 0, part[AUDIO_METER] = { 0 };
        const unsigned ch = x->channels ? x->channels : 1;
        for (uint32_t f = 0; x->pcm && f < x->len; f++) {
            const unsigned m = (unsigned)((uint64_t)f * AUDIO_METER / x->len);
            for (unsigned c = 0; c < ch; c++) {
                int a = x->pcm[f * ch + c];
                a = a < 0 ? -a : a;
                if (a > part[m]) part[m] = a;
            }
        }
        lua_createtable(L, AUDIO_METER, 0);
        for (unsigned m = 0; m < AUDIO_METER; m++) {
            if (part[m] > peak) peak = part[m];
            lua_pushnumber(L, part[m] / 32768.0);
            lua_rawseti(L, -2, (lua_Integer)m + 1);
        }
        lua_setfield(L, -2, "meter");
        lua_pushnumber(L, peak / 32768.0);
        lua_setfield(L, -2, "peak");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

/* audio_depth() -> the bits of each sample at the output asked for
 * (sound_depth: 16, 24 or 32; Settings > Bit depth), and what the output
 * asks for now (0: none, as in QEMU). The game cannot change it. */
int l_audio_depth(lua_State *L)
{
    lua_pushinteger(L, audio_depth());
    lua_pushinteger(L, audio_depth_out());
    return 2;
}

/* audio_pattern(p, bpm, swing, [step]): pattern p of the bank, looping;
 * audio_pattern(-1) stops. For the editor. */
int l_audio_pattern(lua_State *L)
{
    lua_Integer p = luaL_checkinteger(L, 1);
    if (p < 0) {
        audio_music_stop(0);
        return 0;
    }
    audio_music_pattern((int)p, (int)luaL_optinteger(L, 2, 120), (int)luaL_optinteger(L, 3, 0),
                        (int)luaL_optinteger(L, 4, 0));
    return 0;
}

/* audio_play(voice, sound, note, [vol], [fx], [ms]): sound of the bank on a
 * voice, as a step plays it (note: MIDI number), released after ms
 * (default 400). For the editor. */
int l_audio_play(lua_State *L)
{
    unsigned ch = voice_arg(L);
    audio_play((int)ch, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
               (int)luaL_optinteger(L, 4, 255), (int)luaL_optinteger(L, 5, 0),
               (uint32_t)luaL_optinteger(L, 6, 400));
    return 0;
}

/* tone(ch, [sound]): the sound of the voice for note(): an instrument's
 * name ("epiano", "pluck", "kick"... instruments() lists them) or a table
 * in plain units ({wave = "saw", cutoff = 800, res = 0.6, attack = 5,
 * release = 300, reverb = 0.3, pan = -0.5, preset = "bass", ...});
 * tone(ch) puts back the plain square of a fresh voice */
int l_tone(lua_State *L)
{
    unsigned ch = voice_arg(L);
    uint8_t regs[SYNTH_VOICE_BYTES];
    if (lua_isnoneornil(L, 2))
        au_voice_default(regs);
    else {
        audio_tone_get(ch, regs);
        au_lua_tone(L, 2, regs);
    }
    audio_tone(ch, regs);
    return 0;
}

/* a note for play(): a MIDI number (60 = C4) or a name ("C4", "F#3") */
static int midi_arg(lua_State *L, int i)
{
    if (lua_type(L, i) == LUA_TSTRING) {
        float m = 0;
        if (!note_name(lua_tostring(L, i), &m))
            luaL_argerror(L, i, "a note name like \"C4\", \"F#3\" or \"Bb2\"");
        return (int)(m + 0.5f);
    }
    return (int)luaL_optinteger(L, i, 60);
}

/* play([voice], sound, [note], [ms], [vol]): an instrument as the music
 * plays it, its pitch envelope and vibrato included: a name (instruments()),
 * a table as tone()'s (with pitch, ptime, vib, vibhz, detune) or a sound of
 * the cartridge's bank (a number). voice nil: a free one; ms 0: held until
 * noteoff(); vol 0..1. Returns the voice, or nil. */
int l_play(lua_State *L)
{
    int ch = lua_isnoneornil(L, 1) ? -1 : (int)(luaL_checkinteger(L, 1) % SYNTH_VOICES);
    int note = midi_arg(L, 3);
    lua_Integer ms = luaL_optinteger(L, 4, 400);
    int vol = (int)(luaL_optnumber(L, 5, 1.0) * 255.0 + 0.5);
    luaL_argcheck(L, note >= 1 && note <= 127, 3, "a note from 1 to 127");
    int v;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        if (ch < 0)
            ch = 7;
        audio_play(ch, (int)lua_tointeger(L, 2), note, vol, 0, ms > 0 ? (uint32_t)ms : 0);
        v = ch;
    } else {
        au_sound_t snd;
        au_lua_sound(L, 2, &snd);
        v = audio_play_sound(ch, &snd, note, vol, ms > 0 ? (uint32_t)ms : 0);
    }
    if (v < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, v);
    return 1;
}

/* audio_time(): the sound's clock, in seconds (the time of play_at) */
int l_audio_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)audio_clock() / AUDIO_RATE);
    return 1;
}

/* play_at(t, sound, [note], [ms], [vol], [tag]): a note at the time t of
 * audio_time(), started by the sound's interrupt within 1.3 ms whatever
 * the frame rate: sound as play()'s (a number is a sound of the bank),
 * held ms (at least 1), vol 0..1, tag 1..255 (a group for play_cancel).
 * The voice is chosen when it starts (play_voices). true, or false when
 * the queue (160 notes) is full. The pattern language (require "riff")
 * plans its notes with it. */
int l_play_at(lua_State *L)
{
    lua_Number t = luaL_checknumber(L, 1);
    int note = midi_arg(L, 3);
    lua_Number ms = luaL_optnumber(L, 4, 250);
    lua_Number vol = luaL_optnumber(L, 5, 1.0);
    lua_Integer tag = luaL_optinteger(L, 6, 1);
    luaL_argcheck(L, note >= 1 && note <= 127, 3, "a note from 1 to 127");
    luaL_argcheck(L, tag >= 1 && tag <= 255, 6, "a tag from 1 to 255");
    au_sound_t snd;
    au_lua_sound(L, 2, &snd);
    lua_Number at = t * AUDIO_RATE;
    uint64_t when = at <= 0 ? 0 : (uint64_t)at;
    lua_Number len = ms * (AUDIO_RATE / 1000.0);
    int v = (int)(vol * 255.0 + 0.5);
    lua_pushboolean(L, audio_at(when, &snd, note, v, len < 1 ? 1u : len > 4e8 ? 400000000u : (uint32_t)len,
                                (int)tag) == 0);
    return 1;
}

/* play_cancel([tag]): the notes of play_at waiting with that tag (none:
 * all) forgotten, and released if they sound */
int l_play_cancel(lua_State *L)
{
    lua_Integer tag = luaL_optinteger(L, 1, 0);
    audio_at_cancel(tag >= 0 && tag <= 255 ? (int)tag : 0);
    return 0;
}

/* play_voices([v, ...]): the voices play_at may take (none: all 8);
 * returns how many notes wait */
int l_play_voices(lua_State *L)
{
    int n = lua_gettop(L);
    if (n > 0) {
        unsigned mask = 0;
        for (int i = 1; i <= n; i++)
            mask |= 1u << (luaL_checkinteger(L, i) & 7);
        audio_at_voices((uint8_t)mask);
    } else {
        audio_at_voices(0);
    }
    lua_pushinteger(L, audio_at_waiting(0));
    return 1;
}

/* reverb([size], [damp], [wet]): the room every voice sends to (0..1
 * each: a small room .. a hall, bright .. dull, how much is heard);
 * returns the three as they are now */
int l_reverb(lua_State *L)
{
    float room[3], echo[3];
    audio_fx_get(room, echo);
    if (!lua_isnoneornil(L, 1))
        audio_room((float)luaL_checknumber(L, 1), (float)luaL_optnumber(L, 2, room[1]),
                   (float)luaL_optnumber(L, 3, room[2]));
    audio_fx_get(room, echo);
    lua_pushnumber(L, room[0]);
    lua_pushnumber(L, room[1]);
    lua_pushnumber(L, room[2]);
    return 3;
}

/* echo([ms], [feedback], [wet]): the echo's time (at most 680 ms), how
 * much of each repeat comes back (0..0.95) and how much is heard */
int l_echo(lua_State *L)
{
    float room[3], echo[3];
    audio_fx_get(room, echo);
    if (!lua_isnoneornil(L, 1))
        audio_echo((float)luaL_checknumber(L, 1), (float)luaL_optnumber(L, 2, echo[1]),
                   (float)luaL_optnumber(L, 3, echo[2]));
    audio_fx_get(room, echo);
    lua_pushnumber(L, echo[0]);
    lua_pushnumber(L, echo[1]);
    lua_pushnumber(L, echo[2]);
    return 3;
}

/* chorus([rate_hz], [depth_ms], [wet]): the chorus every voice sends to
 * (tone{chorus = x}): its LFO's rate (0.05..5 Hz), how far the delay swings
 * (0..8 ms) and how much is heard (0..1); returns the three as they are */
int l_chorus(lua_State *L)
{
    float c[3];
    audio_chorus_get(c);
    if (!lua_isnoneornil(L, 1))
        audio_chorus((float)luaL_checknumber(L, 1), (float)luaL_optnumber(L, 2, c[1]),
                     (float)luaL_optnumber(L, 3, c[2]));
    audio_chorus_get(c);
    lua_pushnumber(L, c[0]);
    lua_pushnumber(L, c[1]);
    lua_pushnumber(L, c[2]);
    return 3;
}

/* retro([on]): every voice the 8-bit chip of the first versions (no
 * room, no echo) while the game runs; returns whether it is (Settings
 * can ask for it too) */
int l_retro(lua_State *L)
{
    if (!lua_isnoneornil(L, 1))
        audio_retro(AUDIO_RETRO_GAME, lua_toboolean(L, 1));
    lua_pushboolean(L, audio_retro_on());
    return 1;
}
