-- bm Tape sound library (design 8, layers 1 and 2): eight engines, 44 sounds (20 live synth sounds, 24 PCM sounds), the drum kit.
-- An engine is a base tone() table plus four macros. A macro maps one encoder value (in its real unit) to
-- tone() keys. A macro with an alias lives in the filter (cutoff, res, rate): the FILTER page edits the same value.

local CURVES = { "soft", "hard", "fold", "sine", "asym", "cubic" }

-- the filter values of a synth track, in the order of the FILTER page
local FDEF = {
  { key = "cutoff", label = "CUTOFF", min = 0.1, max = 12, step = 0.1, def = 6, fmt = "%.1fk" },
  { key = "res", label = "RESO", min = 0, max = 1, step = 0.02, def = 0.2, fmt = "%.2f" },
  { key = "rate", label = "LFO RATE", min = 0, max = 20, step = 0.1, def = 0, fmt = "%.1fHz" },
  { key = "depth", label = "LFO DEPTH", min = 0, max = 3, step = 0.05, def = 0, fmt = "%.2f" },
}
local FIDX = { cutoff = 1, res = 2, rate = 3, depth = 4 }

-- the engines; the helpers of the table are local to this block (a chunk holds 200 locals at most)
local ENGINES
do
local VOWELS = { "a", "e", "i", "o", "u" }
-- a macro: label, range, step, default, format, and the tone() key it sets (or a function(tone, v, i))
local function M(label, lo, hi, step, def, fmt, to)
  return { label = label, min = lo, max = hi, step = step, def = def, fmt = fmt, to = to }
end
-- a macro that lives in the filter
local function A(alias)
  local f = FDEF[FIDX[alias]]
  return { label = f.label, min = f.min, max = f.max, step = f.step, def = f.def, fmt = f.fmt, alias = alias }
end
local function key(k) return function(tn, v) tn[k] = v end end
local function bar(tn, v, i)
  tn.bars = tn.bars or { 0, 0, 0, 0 }
  tn.bars[i] = floor(v + 0.5)
end

ENGINES = {
  { name = "FM", base = { wave = "fm" }, macros = {
    M("RATIO", 0.5, 4, 0.05, 2.0, "%.2f", key("ratio")), M("DEPTH", 0, 8, 0.1, 3.4, "%.1f", key("depth")),
    M("FEEDBK", 0, 1, 0.05, 0.2, "%.2f", key("feedback")), M("DECAY", 20, 1000, 10, 180, "%dms", key("mdecay")) } },
  { name = "STRING", base = { wave = "pluck" }, macros = {
    M("BRIGHT", 0, 1, 0.02, 0.62, "%.2f", key("bright")), M("RING", 0, 1, 0.02, 0.8, "%.2f", key("ring")),
    A("cutoff"), M("DRIVE", 0, 1, 0.05, 0.15, "%.2f", key("drive")) } },
  { name = "PULSE", base = { wave = "square" }, macros = {
    M("DUTY", 0.05, 0.95, 0.01, 0.5, "%.2f", key("duty")), M("PWM", 0, 1, 0.02, 0.4, "%.2f", key("pwm")),
    M("NOISE", 0, 1, 0.02, 0.1, "%.2f", key("noise")), A("cutoff") } },
  { name = "CLUSTER", base = { wave = "supersaw" }, macros = {
    M("SPREAD", 0, 1, 0.02, 0.4, "%.2f", key("spread")), A("cutoff"), A("res"),
    M("DRIVE", 0, 1, 0.01, 0.2, "%.2f", key("drive")) } },
  { name = "ORGAN", base = { wave = "organ" }, macros = {
    M("BAR 1", 0, 15, 1, 8, "%d", bar), M("BAR 2", 0, 15, 1, 6, "%d", bar),
    M("BAR 3", 0, 15, 1, 3, "%d", bar), M("BAR 4", 0, 15, 1, 2, "%d", bar) } },
  { name = "VOX", base = { wave = "saw" }, macros = {
    M("VOWEL", 0, 4, 1, 0, function(v) return VOWELS[floor(v + 0.5) + 1]:upper() end,
      function(tn, v) tn.vowel = VOWELS[floor(v + 0.5) + 1] end),
    A("cutoff"), A("rate"), M("TREM", 0, 1, 0.02, 0.2, "%.2f", key("trem")) } },
  { name = "DIGITAL", base = { wave = "saw" }, macros = {
    M("BITS", 2, 16, 1, 8, function(v) return v >= 16 and "OFF" or string.format("%d", v) end,
      function(tn, v) tn.crush = v >= 16 and 0 or floor(v + 0.5) end),
    M("COARSE", 1, 16, 1, 2, "%d", key("coarse")),
    M("CURVE", 0, 5, 1, 1, function(v) return CURVES[floor(v + 0.5) + 1]:upper() end,
      function(tn, v) tn.curve = CURVES[floor(v + 0.5) + 1] end),
    M("DRIVE", 0, 1, 0.01, 0.3, "%.2f", key("drive")) } },
  -- PCM: a sample of the cartridge's bank, rendered on the PC from Felucca-Engines (MIT, tools/felrender).
  -- The macros change the bm voice (start, filter, drive), not the Felucca engine: the timbre is fixed.
  { name = "PCM", pcm = true, base = { wave = "sample" }, macros = {
    M("START", 0, 0.9, 0.01, 0, "%.2f", key("begin")), A("cutoff"), A("res"),
    M("DRIVE", 0, 1, 0.01, 0, "%.2f", key("drive")) } },
}
end
local ENGIDX = {}
for i, e in ipairs(ENGINES) do ENGIDX[e.name] = i end

-- the 20 live synth sounds. macros: the four macro values (false: the macro lives in the filter, see below);
-- env: attack ms, decay ms, sustain 0..1, release ms; filter: cutoff kHz, resonance, LFO rate Hz, LFO depth (octaves)
local PCM_FIRST = "PHBRASS"
local SOUNDS
do
local function snd(name, engine, macros, env, filter)
  return { name = name, engine = engine, macros = macros, env = env, filter = filter }
end
SOUNDS = {
  snd("DX BASS", "FM", { 1.0, 2.2, 0.15, 200 }, { 2, 260, 0.35, 110 }, { 6, 0.15, 0, 0 }),
  snd("GLASS BELL", "FM", { 3.5, 1.6, 0, 900 }, { 1, 1200, 0, 1400 }, { 12, 0.1, 0, 0 }),
  snd("FM KEYS", "FM", { 2.0, 1.4, 0.1, 350 }, { 2, 700, 0.25, 300 }, { 9, 0.1, 0, 0 }),
  snd("PICK BASS", "STRING", { 0.5, 0.6, false, 0.2 }, { 1, 300, 0.5, 150 }, { 3.2, 0.2, 0, 0 }),
  snd("ARP PLUCK", "STRING", { 0.8, 0.4, false, 0.05 }, { 1, 250, 0.2, 200 }, { 6, 0.1, 0, 0 }),
  snd("HARP", "STRING", { 0.65, 0.9, false, 0 }, { 1, 900, 0.2, 600 }, { 8, 0.05, 0, 0 }),
  snd("PWM STRINGS", "PULSE", { 0.5, 0.6, 0.02, false }, { 250, 500, 0.8, 500 }, { 4.5, 0.15, 0.7, 0 }),
  snd("SQUARE BASS", "PULSE", { 0.5, 0, 0, false }, { 2, 180, 0.6, 90 }, { 1.4, 0.3, 0, 0 }),
  snd("PULSE LEAD", "PULSE", { 0.25, 0.3, 0, false }, { 4, 200, 0.7, 160 }, { 5, 0.2, 5, 0.1 }),
  snd("JUNO PAD", "CLUSTER", { 0.35, false, false, 0.1 }, { 400, 600, 0.7, 800 }, { 3.2, 0.2, 0.4, 0.3 }),
  snd("BRASS STAB", "CLUSTER", { 0.25, false, false, 0.25 }, { 30, 260, 0.45, 140 }, { 2.6, 0.35, 0, 0 }),
  snd("SYNC LEAD", "CLUSTER", { 0.15, false, false, 0.4 }, { 5, 250, 0.75, 200 }, { 6, 0.45, 5.5, 0.15 }),
  snd("SUPER BASS", "CLUSTER", { 0.3, false, false, 0.3 }, { 3, 200, 0.5, 100 }, { 1.1, 0.4, 0, 0 }),
  snd("DRAWBARS", "ORGAN", { 8, 6, 4, 2 }, { 3, 50, 0.9, 80 }, { 9, 0.05, 0, 0 }),
  snd("ORGAN BASS", "ORGAN", { 8, 3, 0, 0 }, { 3, 40, 0.95, 60 }, { 1.6, 0.2, 0, 0 }),
  snd("CHURCH", "ORGAN", { 8, 8, 6, 4 }, { 40, 100, 0.9, 400 }, { 6, 0.05, 0.3, 0 }),
  snd("VOX PAD", "VOX", { 0, false, false, 0.2 }, { 500, 500, 0.8, 700 }, { 3.5, 0.2, 4.5, 0 }),
  snd("OOH CHOIR", "VOX", { 3, false, false, 0.15 }, { 300, 400, 0.8, 600 }, { 2.8, 0.2, 5, 0 }),
  snd("BIT LEAD", "DIGITAL", { 5, 3, 1, 0.3 }, { 2, 200, 0.7, 120 }, { 9, 0.1, 0, 0 }),
  snd("CRUSH BASS", "DIGITAL", { 6, 2, 0, 0.4 }, { 2, 220, 0.5, 100 }, { 2.2, 0.3, 0, 0 }),
}

-- the 24 PCM sounds (design 8, Felucca-Engines): the name is the engine prefix and the preset, the sample is in the
-- cartridge's bank (carts/tape/sounds/felucca.json, scripts/felpack.py), the kind sets the release of the bm voice.
-- The sample holds its own envelope and plays once: a held key stops at the end of the sample.
local PCM_REL = { bass = 80, keys = 150, pad = 300, lead = 120, bell = 400, fx = 200 }
local function pcm(name, sample, kind)
  local s = snd(name, "PCM", { 0, false, false, 0 }, { 1, 10, 1, PCM_REL[kind] }, { 12, 0, 0, 0 })
  s.sample, s.kind = sample, kind
  return s
end
local PCM_SOUNDS = {
  pcm("PH BRASS", "PHBRASS", "lead"),
  pcm("PH ORGAN", "PHORGAN", "pad"),
  pcm("PH STRING", "PHSTRING", "pad"),
  pcm("PH RESO", "PHRESO", "lead"),
  pcm("PH BELL", "PHBELL", "bell"),
  pcm("PH WIRE", "PHWIRE", "lead"),
  pcm("TR FAT BASS", "TRFATBAS", "bass"),
  pcm("TR ARP LEAD", "TRARPLD", "lead"),
  pcm("TR SYNC LEAD", "TRSYNCLD", "lead"),
  pcm("TR RING BELL", "TRRINGBL", "bell"),
  pcm("TR CHIP CHOIR", "TRCHIPCH", "pad"),
  pcm("WH FULL ORGAN", "WHFULL", "pad"),
  pcm("WH JAZZ PERC", "WHJAZZ", "keys"),
  pcm("WH GOSPEL", "WHGOSPEL", "pad"),
  pcm("WH SOFT FLUTE", "WHFLUTE", "pad"),
  pcm("WH ROCK DRIVE", "WHROCK", "pad"),
  pcm("AN RAVE", "ANRAVE", "lead"),
  pcm("AN PWM STR", "ANPWMSTR", "pad"),
  pcm("AN SUB BASS", "ANSUBBAS", "bass"),
  pcm("AN BRASS", "ANBRASS", "lead"),
  pcm("LO PULSE LD", "LOPULSE", "lead"),
  pcm("LO WAVE BASS", "LOWAVBAS", "bass"),
  pcm("LO WAVE LEAD", "LOWAVLD", "lead"),
  pcm("NO METAL", "NOMETAL", "fx"),
}
for _, s in ipairs(PCM_SOUNDS) do SOUNDS[#SOUNDS + 1] = s end
end
-- the bank's samples by name: frames, rate and the 32 peaks (meter) of each, for the PCM scene
local PCMINFO = {}
do
  for _, x in ipairs(audio_samples and audio_samples() or {}) do PCMINFO[x.name] = x end
end

-- a synth track: its own copy of a sound (the encoders edit this copy)
local function macro_get(t, i)
  local m = ENGINES[t.eng].macros[i]
  if m.alias then return t.flt[FIDX[m.alias]] end
  return t.mac[i]
end
local function macro_set(t, i, v)
  local m = ENGINES[t.eng].macros[i]
  if m.alias then t.flt[FIDX[m.alias]] = v else t.mac[i] = v end
end

-- the tone() table of a track: base, macros, envelope, filter
local function retone(t)
  local e = ENGINES[t.eng]
  local tn = {}
  for k, v in pairs(e.base) do tn[k] = v end
  for i, m in ipairs(e.macros) do
    if m.to then m.to(tn, t.mac[i], i) end
  end
  if e.pcm then tn.sample = t.sample or PCM_FIRST end
  tn.attack, tn.decay, tn.sustain, tn.release = t.env[1], t.env[2], t.env[3], t.env[4]
  tn.filter, tn.cutoff, tn.res = "lp", t.flt[1] * 1000, t.flt[2]
  tn.lfo, tn.wah = t.flt[3], t.flt[4]
  t.tone = tn
end

local function load_sound(t, n)
  local s = SOUNDS[n]
  t.snd, t.eng, t.mod, t.sample = n, ENGIDX[s.engine], false, s.sample
  t.env, t.flt, t.mac = { table.unpack(s.env) }, { table.unpack(s.filter) }, {}
  for i, m in ipairs(ENGINES[t.eng].macros) do t.mac[i] = s.macros[i] or m.def end
  retone(t)
end

-- a new engine on a track: macros at their defaults, the filter and the envelope stay
local function set_engine(t, e)
  t.eng, t.snd, t.mod, t.mac, t.sample = e, 0, false, {}, nil
  for i, m in ipairs(ENGINES[e].macros) do t.mac[i] = m.def end
  retone(t)
end

-- the drum kit: pads 1..12 are the KO-II block 7 8 9 / U I O / J K L / M , .
-- kit pads play the kit's sample at its root note; the others are synth one-shots of the presets.
-- The API has no sample end control: DECAY is the release (ms) after a short gate.
local PADS
do
local function mkpad(name, sample, note, rel)
  local src = sample and { preset = "kit", sample = sample } or nil
  return { name = name, sample = sample, src = src, note = note or 60, rel = rel, rel0 = rel, begin = 0, pitch = 0, level = 0.9 }
end
local function oneshot(name, preset, note, rel)
  local p = mkpad(name, nil, note, rel)
  p.src = { preset = preset }
  return p
end
local KIT_ORDER = { "bd", "sd", "hh", "oh", "cp", "rim", "tom", "cb" }
PADS = {
  mkpad("KICK", "bd", 60, 450), mkpad("SNARE", "sd", 60, 300), mkpad("CHAT", "hh", 60, 90), mkpad("OHAT", "oh", 60, 450),
  mkpad("CLAP", "cp", 60, 300), mkpad("RIM", "rim", 60, 120), mkpad("TOM", "tom", 60, 450), mkpad("COWBL", "cb", 60, 300),
  oneshot("PUNCH", "punch", 36, 400), oneshot("CSNARE", "chipsnr", 60, 300),
  oneshot("SHAKER", "shaker", 60, 150), oneshot("CRASH", "crash", 60, 1200),
}
-- the kit's samples: root note of each, and the 32 peaks of its picture (audio_samples("kit"))
do
  local kit = audio_samples and audio_samples("kit") or {}
  for i, nm in ipairs(KIT_ORDER) do
    for _, p in ipairs(PADS) do
      if p.sample == nm then
        if kit[i] and kit[i].root then p.note = kit[i].root end
        p.meter = kit[i] and kit[i].meter
      end
    end
  end
end

end

-- the tone of a pad: its source with the pad's start and release
local function pad_tone(p)
  local tn = {}
  for k, v in pairs(p.src) do tn[k] = v end
  tn.begin, tn.release = p.begin, p.rel
  return tn
end
