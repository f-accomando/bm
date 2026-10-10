#include "lua_tone.h"
#include "presets.h"
#include "lauxlib.h"

#include <string.h>

const au_bank_t *(*au_lua_bank)(void);

static void preset_regs(lua_State *L, const char *name, uint8_t *regs)
{
    int p = au_preset_find(name);
    if (p < 0)
        luaL_error(L, "no instrument called \"%s\" (instruments() lists them)", name);
    au_sound_regs(&au_presets[p].s, regs);
}

/* the keys of the table at idx into regs; the player's own keys (pitch,
 * vibrato) are skipped here: au_lua_sound reads them */
static void table_regs(lua_State *L, int idx, uint8_t *regs)
{
    idx = lua_absindex(L, idx);
    if (lua_getfield(L, idx, "preset") == LUA_TSTRING)
        preset_regs(L, lua_tostring(L, -1), regs);
    lua_pop(L, 1);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 1);
            continue;
        }
        const char *k = lua_tostring(L, -2);
        int ok = 0;
        if (!strcmp(k, "preset") || !strcmp(k, "s") || !strcmp(k, "pitch") || !strcmp(k, "ptime") || !strcmp(k, "vib") ||
            !strcmp(k, "vibhz") || !strcmp(k, "detune") || !strcmp(k, "name")) {
            ok = 1;
        } else if (!strcmp(k, "bars") && lua_istable(L, -1)) {
            static const char *const bars[4] = { "bar1", "bar2", "bar3", "bar4" };
            for (int i = 0; i < 4; i++) {
                lua_geti(L, -1, i + 1);
                au_tone_num(regs, bars[i], lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            ok = 1;
        } else if (lua_type(L, -1) == LUA_TSTRING && !lua_isnumber(L, -1)) {
            const au_bank_t *b = au_lua_bank ? au_lua_bank() : 0;
            int smp = !strcmp(k, "sample") && b ? au_sample_find(b, lua_tostring(L, -1)) : -1;
            if (smp >= 0) {
                regs[SYNTH_MOD1] = (uint8_t)smp;        /* a sample of the cartridge's bank */
                ok = 1;
            } else {
                ok = au_tone_str(regs, k, lua_tostring(L, -1)) == 0;
            }
        } else if (lua_isboolean(L, -1)) {
            ok = au_tone_num(regs, k, lua_toboolean(L, -1)) == 0;
        } else if (lua_isnumber(L, -1)) {
            ok = au_tone_num(regs, k, lua_tonumber(L, -1)) == 0;
        }
        if (!ok)
            luaL_error(L, "tone: bad key or value \"%s\"", k);
        lua_pop(L, 1);
    }
}

void au_lua_tone(lua_State *L, int idx, uint8_t *regs)
{
    if (lua_type(L, idx) == LUA_TSTRING)
        preset_regs(L, lua_tostring(L, idx), regs);
    else if (lua_istable(L, idx))
        table_regs(L, idx, regs);
    else
        luaL_argerror(L, idx, "an instrument's name or a table");
}

static lua_Number opt_field(lua_State *L, int idx, const char *k, lua_Number def)
{
    lua_getfield(L, idx, k);
    lua_Number v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static int clampi(lua_Number v, int lo, int hi)
{
    int x = (int)(v + (v < 0 ? -0.5 : 0.5));
    return x < lo ? lo : x > hi ? hi : x;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}

/* the sound called (or numbered) as the value at idx: the bank's, else a
 * preset; raises an error if there is none */
static const au_sound_t *named_sound(lua_State *L, int idx)
{
    const au_bank_t *b = au_lua_bank ? au_lua_bank() : 0;
    if (lua_type(L, idx) == LUA_TNUMBER) {
        lua_Integer n = lua_tointeger(L, idx);
        if (!b || n < 0 || n >= b->nsounds)
            luaL_error(L, "no sound %d in the bank", (int)n);
        return &b->sound[n];
    }
    const char *name = lua_tostring(L, idx);
    if (!name)
        luaL_error(L, "a sound's name or number");
    for (int i = 0; b && i < b->nsounds; i++)
        if (same_name(b->sound[i].name, name))
            return &b->sound[i];
    int p = au_preset_find(name);
    if (p < 0)
        luaL_error(L, "no instrument called \"%s\" (instruments() lists them)", name);
    return &au_presets[p].s;
}

void au_lua_sound(lua_State *L, int idx, au_sound_t *s)
{
    idx = lua_absindex(L, idx);
    uint8_t regs[SYNTH_VOICE_BYTES];
    memset(s, 0, sizeof *s);
    au_voice_default(regs);
    if (lua_type(L, idx) == LUA_TSTRING || lua_type(L, idx) == LUA_TNUMBER) {
        *s = *named_sound(L, idx);
        return;
    }
    luaL_checktype(L, idx, LUA_TTABLE);
    if (lua_getfield(L, idx, "s") != LUA_TNIL) {
        *s = *named_sound(L, -1);
        au_sound_regs(s, regs);
    }
    lua_pop(L, 1);
    if (lua_getfield(L, idx, "preset") == LUA_TSTRING) {
        int p = au_preset_find(lua_tostring(L, -1));
        if (p < 0)
            luaL_error(L, "no instrument called \"%s\"", lua_tostring(L, -1));
        *s = au_presets[p].s;
        au_sound_regs(s, regs);
    }
    lua_pop(L, 1);
    table_regs(L, idx, regs);
    s->wave = regs[SYNTH_WAVEFORM];
    s->duty = regs[SYNTH_DUTY];
    s->vol = regs[SYNTH_VOLUME];
    s->attack = regs[SYNTH_ATTACK];
    s->decay = regs[SYNTH_DECAY];
    s->sustain = regs[SYNTH_SUSTAIN];
    s->release = regs[SYNTH_RELEASE];
    memcpy(s->tone, regs + SYNTH_CUTOFF, AU_TONE);
    s->pitch = (int8_t)clampi(opt_field(L, idx, "pitch", s->pitch), -96, 96);
    s->pitch_time = (uint8_t)clampi(opt_field(L, idx, "ptime", s->pitch_time * 10.0) / 10.0, 0, 255);
    s->vib_depth = (uint8_t)clampi(opt_field(L, idx, "vib", s->vib_depth), 0, 255);
    s->vib_rate = (uint8_t)clampi(opt_field(L, idx, "vibhz", s->vib_rate / 10.0) * 10.0, 0, 255);
    s->detune = (int8_t)clampi(opt_field(L, idx, "detune", s->detune), -100, 100);
}

int au_lua_instrument(lua_State *L)
{
    int i = au_preset_find(luaL_checkstring(L, 1));
    if (i < 0) {
        lua_pushnil(L);
        return 1;
    }
    const au_preset_t *p = &au_presets[i];
    const au_sound_t *s = &p->s;
    lua_createtable(L, 0, 16);
    lua_pushstring(L, s->name); lua_setfield(L, -2, "name");
    lua_pushstring(L, p->kind); lua_setfield(L, -2, "kind");
    lua_pushstring(L, p->about); lua_setfield(L, -2, "about");
    static const char *const keys[] = { "wave", "duty", "vol", "a", "d", "s", "r", "ptime", "vdepth", "vrate" };
    const int vals[] = { s->wave, s->duty, s->vol, s->attack, s->decay, s->sustain, s->release, s->pitch_time,
                         s->vib_depth, s->vib_rate };
    for (int k = 0; k < 10; k++) {
        lua_pushinteger(L, vals[k]);
        lua_setfield(L, -2, keys[k]);
    }
    lua_pushinteger(L, s->pitch); lua_setfield(L, -2, "pitch");
    lua_pushinteger(L, s->detune); lua_setfield(L, -2, "detune");
    lua_createtable(L, AU_TONE, 0);
    for (int k = 0; k < AU_TONE; k++) {
        lua_pushinteger(L, s->tone[k]);
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "tone");
    return 1;
}

int au_lua_instruments(lua_State *L)
{
    const char *kind = luaL_optstring(L, 1, NULL);
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < au_preset_count; i++) {
        const au_preset_t *p = &au_presets[i];
        if (kind && strcmp(kind, p->kind))
            continue;
        lua_createtable(L, 0, 3);
        lua_pushstring(L, p->s.name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, p->kind);
        lua_setfield(L, -2, "kind");
        lua_pushstring(L, p->about);
        lua_setfield(L, -2, "about");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
