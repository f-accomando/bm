/*
 * The sound of a voice from Lua (tone(), play()): a preset's name, a
 * sound of the bank, or a table in plain units ({wave = "saw", cutoff =
 * 800, res = 0.6, attack = 5, ...}, presets.h). Shared by the console's
 * runtime, bmhost and bmplay.
 */
#ifndef LUA_TONE_H
#define LUA_TONE_H

#include "lua.h"
#include "player.h"

/* The value at idx into a voice's 32 register bytes (they start as the
 * voice is): a preset name sets the whole sound, a table its keys (with
 * `preset` first if it has one). Raises a Lua error on a bad name or key. */
void au_lua_tone(lua_State *L, int idx, uint8_t *regs);

/* The value at idx as a whole sound (for play()): a preset name, or a
 * table (from the square of note(), or `preset`) with the tone's keys and
 * the player's: pitch (semitones), ptime (ms), vib (cents), vibhz,
 * detune (cents). */
void au_lua_sound(lua_State *L, int idx, au_sound_t *s);

/* instruments([kind]): a list of {name =, kind =, about =} */
int au_lua_instruments(lua_State *L);

/* instrument(name): the whole preset as bm Sound keeps a sound: {name,
 * kind, about, wave, duty, vol, a, d, s, r, pitch, ptime, vdepth, vrate,
 * detune, tone = {21 register bytes}}; nil if there is none */
int au_lua_instrument(lua_State *L);

#endif
