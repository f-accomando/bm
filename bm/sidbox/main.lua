-- SIDBOX: a 4-track SID-style synthesizer and step sequencer for bm.
-- Every track is a monophonic voice (or a drum kit) with its own SID-like instrument:
-- waveforms (pulse, saw, triangle, noise, combined), ring mod, hard sync, ADSR in SID
-- steps, PWM, resonant filter, vibrato, arpeggiator, portamento. Each track has a row of
-- 16 pads that loops. The keyboard plays live (tracker layout). Original code.

local floor, max, min, abs, sin, cos, pi, random = math.floor, math.max, math.min, math.abs, math.sin, math.cos, math.pi, math.random
local fmt = string.format

local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end
local function round(v) return floor(v + 0.5) end

local W, H = 640, 360

-- ---------------------------------------------------------------- colours (C64, Colodore)
local C = {
  blk = 0x000000, wht = 0xFFFFFF, red = 0x813338, cyan = 0x75CEC8, pur = 0x8E3C97, grn = 0x56AC4D,
  blu = 0x2E2C9B, yel = 0xEDF171, org = 0x8E5029, brn = 0x553800, lred = 0xC46C71, dgr = 0x4A4A4A,
  mgr = 0x7B7B7B, lgrn = 0xA9FF9F, lblu = 0x706DEB, lgr = 0xB2B2B2,
}
local TCOL = { C.lgrn, C.yel, C.cyan, C.lred }

-- ---------------------------------------------------------------- keys (USB HID usages)
local K = {
  A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09, G = 0x0A, H = 0x0B, I = 0x0C,
  J = 0x0D, K = 0x0E, L = 0x0F, M = 0x10, N = 0x11, O = 0x12, P = 0x13, Q = 0x14, R = 0x15,
  S = 0x16, T = 0x17, U = 0x18, V = 0x19, W = 0x1A, X = 0x1B, Y = 0x1C, Z = 0x1D,
  D1 = 0x1E, D2 = 0x1F, D3 = 0x20, D4 = 0x21, D5 = 0x22, D6 = 0x23, D7 = 0x24, D8 = 0x25, D9 = 0x26, D0 = 0x27,
  ENTER = 0x28, BKSP = 0x2A, TAB = 0x2B, SPACE = 0x2C, MINUS = 0x2D, EQUAL = 0x2E, LB = 0x2F, RB = 0x30,
  SEMI = 0x33, COMMA = 0x36, PERIOD = 0x37, SLASH = 0x38,
  F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D, F5 = 0x3E, F6 = 0x3F, F7 = 0x40, F8 = 0x41, F9 = 0x42, F10 = 0x43,
  HOME = 0x4A, PGUP = 0x4B, DELETE = 0x4C, END = 0x4D, PGDN = 0x4E,
  RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52,
  LCTRL = 0xE0, LSHIFT = 0xE1, RCTRL = 0xE4, RSHIFT = 0xE5,
}
local tracked, held, was, hcount = {}, {}, {}, {}
for _, u in pairs(K) do tracked[#tracked + 1] = u end

local function poll()
  for i = 1, #tracked do
    local u = tracked[i]
    was[u] = held[u]
    held[u] = keydown(u) and true or false
    hcount[u] = held[u] and ((hcount[u] or 0) + 1) or 0
  end
end
local function pressed(u) return held[u] and not was[u] end
local function released(u) return was[u] and not held[u] end
local function rep(u)                       -- pressed, then auto repeat
  if not held[u] then return false end
  local n = hcount[u]
  return n == 1 or (n > 18 and (n - 18) % 3 == 0)
end
local function shift() return held[K.LSHIFT] or held[K.RSHIFT] end
local function ctrl() return held[K.LCTRL] or held[K.RCTRL] end

-- the piano: lower row and upper row (tracker layout). off = semitones above the C of the octave
local PK = {}
do
  local lower = { [K.Z] = 0, [K.S] = 1, [K.X] = 2, [K.D] = 3, [K.C] = 4, [K.V] = 5, [K.G] = 6, [K.B] = 7, [K.H] = 8,
                  [K.N] = 9, [K.J] = 10, [K.M] = 11, [K.COMMA] = 12, [K.L] = 13, [K.PERIOD] = 14, [K.SEMI] = 15, [K.SLASH] = 16 }
  local upper = { [K.Q] = 12, [K.D2] = 13, [K.W] = 14, [K.D3] = 15, [K.E] = 16, [K.R] = 17, [K.D5] = 18, [K.T] = 19,
                  [K.D6] = 20, [K.Y] = 21, [K.D7] = 22, [K.U] = 23, [K.I] = 24, [K.D9] = 25, [K.O] = 26, [K.D0] = 27, [K.P] = 28 }
  for u, o in pairs(lower) do PK[u] = { off = o, rel = o } end
  for u, o in pairs(upper) do PK[u] = { off = o, rel = o - 12, up = true } end
end
local PIANOKEYS = {}
for u in pairs(PK) do PIANOKEYS[#PIANOKEYS + 1] = u end
local DRUMREL = { [0] = 1, [2] = 2, [4] = 3, [5] = 4, [7] = 5, [9] = 6, [11] = 7, [12] = 8 }   -- white keys = the 8 slots
local LOWERLAB = { "Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M", ",", "L", ".", ";", "/" }
local UPPERLAB = { [12] = "Q", [13] = "2", [14] = "W", [15] = "3", [16] = "E", [17] = "R", [18] = "5", [19] = "T",
                   [20] = "6", [21] = "Y", [22] = "7", [23] = "U", [24] = "I", [25] = "9", [26] = "O", [27] = "0", [28] = "P" }

-- ---------------------------------------------------------------- SID tables
local AT = { 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000 }              -- the 6581's attack times (ms)
local DT = { 6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000 }     -- decay and release (ms)
local WAVES = { "PULSE", "SAW", "TRI", "NOISE", "P+T", "P+S", "S+T", "P+S+T" }
local FILTS = { [0] = "OFF", "LP", "BP", "HP", "NOTCH" }
local FILTK = { "lp", "bp", "hp", "notch" }
local ARPS = { [0] = "OFF", "MAJ", "MIN", "DOM7", "MIN7", "MAJ7", "SUS4", "SUS2", "DIM", "AUG", "5TH", "OCT" }
local CHORDS = { { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 }, { 0, 5, 7 }, { 0, 2, 7 },
                 { 0, 3, 6 }, { 0, 4, 8 }, { 0, 7, 12 }, { 0, 12 } }
local ARPDIRS = { "UP", "DOWN", "U+D", "RND" }
local ARPSPDS = { 2, 3, 4, 6, 8 }                -- notes in one step
local ETIMES = { 0.5, 0.75, 1, 0.25 }            -- echo time in beats
local ENAMES = { "1/8", "1/8.", "1/4", "1/16" }
local NN = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" }
local function nname(m) return NN[m % 12 + 1] .. (m // 12 - 1) end

local function msf(ms) return ms >= 1000 and fmt("%.1fS", ms / 1000) or fmt("%dMS", ms) end
local function cuthz(c) return 30 * 2 ^ (c / 255 * 9.4) end

-- ---------------------------------------------------------------- instruments
local DEF = {
  wave = 1, duty = 50, pwm = 0, lfo = 40, ring = 0, sync = 0, osc2 = 7, sweep = 0, s6581 = 0, oct = 0,
  a = 0, d = 5, s = 10, r = 4, filt = 0, cut = 190, res = 3, fenv = 0, fdec = 250, wah = 0, key = 0, analog = 1,
  vib = 0, vibhz = 60, trem = 0, porta = 0, arp = 0, arpdir = 1, arpspd = 3, gate = 75, tune = 0, drive = 0, crush = 0,
  vol = 12, pan = 0, rev = 15, echo = 0, cho = 0,
}
-- the melodic instruments: SID ones (every parameter is mine) and the console's ready-made ones
local MEL = {}
local function sid(name, o) MEL[#MEL + 1] = { name = name, kind = "sid", o = o } end
local function sim(name, preset) MEL[#MEL + 1] = { name = name, kind = "sim", base = preset } end

sid("HEAVY ARP", { wave = 1, duty = 48, pwm = 60, lfo = 35, a = 0, d = 7, s = 8, r = 5, arp = 2, arpdir = 3, arpspd = 3, gate = 85, filt = 1, cut = 215, res = 5, rev = 12, echo = 20 })
sid("PULSE LEAD", { wave = 1, duty = 40, pwm = 45, lfo = 55, a = 0, d = 6, s = 11, r = 5, vib = 14, vibhz = 55, filt = 1, cut = 225, res = 4, echo = 25, rev = 20 })
sid("SAW LEAD", { wave = 2, a = 0, d = 6, s = 12, r = 5, vib = 10, filt = 1, cut = 205, res = 6, fenv = 20, fdec = 300, echo = 25, rev = 20 })
sid("SYNC SWEEP", { wave = 2, sync = 1, osc2 = 14, sweep = 380, a = 0, d = 7, s = 10, r = 4, filt = 1, cut = 225, res = 3, echo = 20 })
sid("RING BELL", { wave = 3, ring = 1, osc2 = 17, a = 0, d = 9, s = 0, r = 9, rev = 45 })
sid("FAT BASS", { wave = 1, duty = 30, pwm = 25, lfo = 25, oct = -1, a = 0, d = 6, s = 9, r = 3, filt = 1, cut = 120, res = 8, fenv = 30, fdec = 150, drive = 20, gate = 60, rev = 8 })
sid("ACID BASS", { wave = 2, oct = -1, a = 0, d = 6, s = 7, r = 3, filt = 1, cut = 95, res = 13, fenv = 50, fdec = 140, drive = 30, porta = 40, gate = 65, rev = 8 })
sid("SUB BASS", { wave = 3, oct = -1, a = 0, d = 8, s = 13, r = 4, gate = 80, rev = 5 })
sid("COMBO 6581", { wave = 6, s6581 = 1, duty = 50, a = 0, d = 7, s = 10, r = 5, filt = 1, cut = 210, res = 5 })
sid("SOFT PAD", { wave = 5, duty = 50, pwm = 30, lfo = 20, a = 9, d = 9, s = 13, r = 9, filt = 1, cut = 180, res = 2, cho = 50, rev = 45, gate = 100 })
sid("STRING PAD", { wave = 2, a = 8, d = 9, s = 14, r = 9, filt = 1, cut = 170, res = 2, cho = 60, rev = 50, vib = 8, vibhz = 50, gate = 100 })
sid("PWM STRINGS", { wave = 1, duty = 50, pwm = 70, lfo = 18, a = 7, d = 9, s = 13, r = 8, cho = 50, rev = 40, gate = 100 })
sid("CHIP ARP", { wave = 1, duty = 25, a = 0, d = 4, s = 8, r = 3, arp = 1, arpdir = 1, arpspd = 3, gate = 95, rev = 10, echo = 25 })
sid("ARP MIN7", { wave = 1, duty = 30, pwm = 30, lfo = 40, a = 0, d = 5, s = 9, r = 4, arp = 4, arpdir = 3, arpspd = 4, gate = 90, filt = 1, cut = 220, res = 4, echo = 30 })
sid("ARP DOWN", { wave = 2, a = 0, d = 4, s = 7, r = 3, arp = 1, arpdir = 2, arpspd = 3, gate = 85, filt = 1, cut = 215, res = 6, rev = 20 })
sid("WAH PULSE", { wave = 1, duty = 50, pwm = 20, lfo = 30, a = 0, d = 8, s = 12, r = 5, filt = 2, cut = 170, res = 10, wah = 20 })
sid("PLUCK", { wave = 1, duty = 35, a = 0, d = 4, s = 0, r = 4, filt = 1, cut = 120, res = 3, fenv = 40, fdec = 120, rev = 25, echo = 20 })
sid("BLIP", { wave = 1, duty = 25, a = 0, d = 2, s = 0, r = 1, rev = 8 })
sid("RINGMOD LEAD", { wave = 3, ring = 1, osc2 = 7, a = 0, d = 7, s = 10, r = 5, filt = 1, cut = 220, res = 3, echo = 20 })
sid("TRI FLUTE", { wave = 3, a = 5, d = 7, s = 12, r = 5, vib = 28, vibhz = 52, rev = 35 })
sid("NOISE HIT", { wave = 4, oct = 3, a = 0, d = 5, s = 0, r = 4, filt = 3, cut = 150, res = 2, drive = 40 })
sid("WIND", { wave = 4, a = 11, d = 11, s = 13, r = 11, filt = 1, cut = 160, res = 8, wah = 25, lfo = 15, rev = 50, gate = 100 })
sid("LASER", { wave = 2, sync = 1, osc2 = 31, sweep = 250, a = 0, d = 5, s = 0, r = 3, rev = 15 })
-- the console's ready-made sounds (simulations of other instruments)
sim("SIM SID LEAD", "sidlead"); sim("SIM SID SYNC", "sidsync"); sim("SIM SID BASS", "sidbass")
sim("SIM SID RING", "sidring"); sim("SIM SID PAD", "sidpad")
sim("BASS", "bass"); sim("ACID", "acid"); sim("SUB", "sub"); sim("FM BASS", "fmbass"); sim("PICK BASS", "pickbass"); sim("BIT BASS", "bitbass")
sim("E-PIANO", "epiano"); sim("RHODES", "rhodes"); sim("ORGAN", "organ"); sim("BELL", "bell"); sim("MARIMBA", "marimba"); sim("GLOCKENSPIEL", "glock")
sim("PAD", "pad"); sim("STRINGS", "strings"); sim("WARM PAD", "warm"); sim("GLASS PAD", "glass"); sim("LUSH PAD", "lush"); sim("CHOIR", "choir")
sim("PLUCK STRING", "pluck"); sim("GUITAR", "guitar"); sim("HARP", "harp")
sim("LEAD", "lead"); sim("SAW LEAD 2", "sawlead"); sim("FLUTE", "flute"); sim("BRASS", "brass"); sim("TRIANGLE", "triangle")
sim("CHIP LEAD", "chip"); sim("CHIP TRI", "chiptri"); sim("SOLO", "solo")
sim("FX LASER", "laser"); sim("FX BOOM", "boom"); sim("FX WIND", "wind"); sim("FX BLIP", "blip")

-- the drum kits: 8 slots each, a step holds any of them. ch = letter on the pad, ms = how long it is held
local function dr(ch, name, tone, note, ms) return { ch = ch, name = name, tone = tone, note = note or 60, ms = ms or 250 } end
local KITS = {
  { name = "SAMPLE KIT", slots = {
      dr("K", "KICK", { preset = "kit", sample = "bd" }, 60, 400), dr("S", "SNARE", { preset = "kit", sample = "sd" }, 60, 300),
      dr("H", "C-HAT", { preset = "kit", sample = "hh" }, 60, 120), dr("O", "O-HAT", { preset = "kit", sample = "oh" }, 60, 500),
      dr("C", "CLAP", { preset = "kit", sample = "cp" }, 60, 300), dr("R", "RIM", { preset = "kit", sample = "rim" }, 60, 150),
      dr("T", "TOM", { preset = "kit", sample = "tom" }, 60, 400), dr("B", "COWBL", { preset = "kit", sample = "cb" }, 60, 400) } },
  { name = "SYNTH KIT", slots = {
      dr("K", "KICK", { preset = "kick" }, 36, 400), dr("S", "SNARE", { preset = "snare" }, 60, 300),
      dr("H", "C-HAT", { preset = "hat", drive = 0.6 }, 127, 120), dr("O", "O-HAT", { preset = "openhat", drive = 0.5 }, 127, 500),
      dr("C", "CLAP", { preset = "clap" }, 108, 300), dr("R", "RIM", { preset = "rim" }, 96, 150),
      dr("T", "TOM", { preset = "tom" }, 48, 400), dr("B", "CRASH", { preset = "crash", drive = 0.5 }, 127, 1200) } },
  { name = "CHIP KIT", slots = {
      dr("K", "KICK", { preset = "chipkick" }, 36, 300), dr("S", "SNARE", { preset = "chipsnr" }, 60, 250),
      dr("H", "C-HAT", { preset = "chiphat" }, 127, 100), dr("O", "O-HAT", { preset = "chiphat", decay = 220, release = 120 }, 127, 400),
      dr("C", "BLIP", { preset = "blip" }, 84, 100), dr("R", "BOOM", { preset = "boom" }, 36, 600),
      dr("T", "LASER", { preset = "laser" }, 72, 400), dr("B", "PUNCH", { preset = "punch" }, 36, 400) } },
  { name = "SID KIT", slots = {
      dr("K", "KICK", { wave = "triangle", attack = 0, decay = 110, sustain = 0, release = 30, pitch = 24, ptime = 70, drive = 0.3 }, 33, 300),
      dr("S", "SNARE", { wave = "noise", attack = 0, decay = 130, sustain = 0, release = 50, filter = "bp", cutoff = 2400, res = 0.3, drive = 0.3 }, 127, 260),
      dr("H", "C-HAT", { wave = "noise", attack = 0, decay = 35, sustain = 0, release = 20, filter = "hp", cutoff = 4000, drive = 0.6 }, 127, 90),
      dr("O", "O-HAT", { wave = "noise", attack = 0, decay = 260, sustain = 0, release = 120, filter = "hp", cutoff = 3500, drive = 0.5 }, 127, 400),
      dr("C", "CLAP", { wave = "noise", attack = 0, decay = 90, sustain = 0, release = 60, filter = "bp", cutoff = 1500, res = 0.2, echo = 0.2 }, 120, 260),
      dr("R", "RIM", { wave = "square", duty = 0.5, attack = 0, decay = 25, sustain = 0, release = 10 }, 84, 80),
      dr("T", "TOM", { wave = "triangle", attack = 0, decay = 170, sustain = 0, release = 60, pitch = 12, ptime = 90 }, 45, 320),
      dr("B", "RING", { preset = "sidring" }, 64, 500) } },
}

-- ---------------------------------------------------------------- parameters
local PDL = {}
local function pd(key, name, lo, hi, def, f, o)
  local d = { key = key, name = name, lo = lo, hi = hi, def = def, fmt = f, step = 1, big = 4 }
  if o then for k, v in pairs(o) do d[k] = v end end
  PDL[key] = d
end
local function onoff(v) return v == 1 and "ON" or "OFF" end
local function pct(v) return fmt("%d%%", v) end
-- s = SID instruments only, m = melodic only, g = global
pd("type", "TYPE", 0, 1, 0, function(v) return v == 0 and "MELODIC" or "DRUMS" end, { special = true })
pd("inst", "INST", 1, 99, 1, nil, { special = true })
pd("wave", "WAVE", 1, 8, 1, function(v) return WAVES[v] end, { s = true })
pd("duty", "WIDTH", 5, 95, 50, pct, { s = true, step = 1, big = 5 })
pd("pwm", "PWM", 0, 100, 0, function(v) return v == 0 and "OFF" or pct(v) end, { s = true, big = 10 })
pd("lfo", "RATE", 1, 200, 40, function(v) return fmt("%.1fHZ", v / 10) end, { big = 10 })
pd("ring", "RINGM", 0, 1, 0, onoff, { s = true })
pd("sync", "HSYNC", 0, 1, 0, onoff, { s = true })
pd("osc2", "OSC2", 0, 31, 7, function(v) return fmt("+%dST", v) end, { s = true, big = 5 })
pd("sweep", "SWEEP", 0, 1500, 0, function(v) return v == 0 and "OFF" or msf(v) end, { s = true, step = 25, big = 100 })
pd("s6581", "6581", 0, 1, 0, onoff, { s = true })
pd("oct", "OCTAVE", -3, 3, 0, function(v) return fmt("%+d", v) end, {})
pd("a", "ATTACK", 0, 15, 0, function(v) return fmt("%X %s", v, msf(AT[v + 1])) end, {})
pd("d", "DECAY", 0, 15, 5, function(v) return fmt("%X %s", v, msf(DT[v + 1])) end, {})
pd("s", "SUSTAIN", 0, 15, 10, function(v) return fmt("%X", v) end, {})
pd("r", "RELEASE", 0, 15, 4, function(v) return fmt("%X %s", v, msf(DT[v + 1])) end, {})
pd("filt", "FILTER", 0, 4, 0, function(v) return FILTS[v] end, {})
pd("cut", "CUTOFF", 0, 255, 190, function(v) local h = cuthz(v); return fmt(h >= 1000 and "%d %.1fK" or "%d %dHZ", v, h >= 1000 and h / 1000 or floor(h)) end, { big = 10 })
pd("res", "RESO", 0, 15, 3, function(v) return fmt("%X", v) end, {})
pd("fenv", "FLT ENV", -80, 80, 0, function(v) return fmt("%+.1fOCT", v / 10) end, { step = 2, big = 10 })
pd("fdec", "FLT DEC", 20, 1500, 250, msf, { step = 10, big = 50 })
pd("wah", "WAH", 0, 40, 0, function(v) return v == 0 and "OFF" or fmt("%.1fOCT", v / 10) end, { step = 1, big = 5 })
pd("key", "KEYTRK", 0, 1, 0, onoff, { m = true })
pd("analog", "ANALOG", 0, 1, 1, onoff, {})
pd("vib", "VIBRATO", 0, 100, 0, function(v) return v == 0 and "OFF" or fmt("%dCT", v) end, { m = true, big = 10 })
pd("vibhz", "VIB RT", 10, 120, 60, function(v) return fmt("%.1fHZ", v / 10) end, { m = true, big = 10 })
pd("trem", "TREMOLO", 0, 100, 0, function(v) return v == 0 and "OFF" or pct(v) end, { big = 10 })
pd("porta", "PORTA", 0, 500, 0, function(v) return v == 0 and "OFF" or msf(v) end, { m = true, step = 10, big = 50 })
pd("arp", "ARP", 0, 11, 0, function(v) return ARPS[v] end, { m = true })
pd("arpdir", "ARP DIR", 1, 4, 1, function(v) return ARPDIRS[v] end, { m = true })
pd("arpspd", "ARP SPD", 1, 5, 3, function(v) return fmt("%d/STEP", ARPSPDS[v]) end, { m = true })
pd("gate", "GATE", 10, 100, 75, pct, { m = true, step = 5, big = 10 })
pd("tune", "TUNE", -100, 100, 0, function(v) return fmt("%+dCT", v) end, { step = 5, big = 25 })
pd("drive", "DRIVE", 0, 100, 0, function(v) return v == 0 and "OFF" or pct(v) end, { big = 10 })
pd("crush", "CRUSH", 0, 12, 0, function(v) return v == 0 and "OFF" or fmt("%dBIT", v) end, { step = 1 })
pd("len", "LENGTH", 1, 16, 16, function(v) return fmt("%d", v) end, { special = true })
pd("vol", "VOLUME", 0, 15, 12, function(v) return fmt("%d", v) end, {})
pd("pan", "PAN", -10, 10, 0, function(v) return v == 0 and "CENTER" or (v < 0 and fmt("L%d", -v * 10) or fmt("R%d", v * 10)) end, {})
pd("rev", "REVERB", 0, 100, 15, pct, { step = 5, big = 20 })
pd("echo", "ECHO", 0, 100, 0, function(v) return v == 0 and "OFF" or pct(v) end, { step = 5, big = 20 })
pd("cho", "CHORUS", 0, 100, 0, function(v) return v == 0 and "OFF" or pct(v) end, { step = 5, big = 20 })

local S = {                                  -- the state
  bpm = 120, swing = 0, master = 8, room = 40, etime = 2, efb = 35,
  bank = 1, track = 1, step = 1, page = 1, oct = 3, rec = false, dsel = 1, prow = 1,
}
pd("bpm", "TEMPO", 40, 240, 120, function(v) return fmt("%d BPM", v) end, { g = true, big = 10 })
pd("swing", "SWING", 0, 60, 0, pct, { g = true, big = 10 })
pd("master", "MASTER", 0, 10, 8, function(v) return fmt("%d", v) end, { g = true })
pd("room", "ROOM", 0, 100, 40, pct, { g = true, step = 5, big = 20 })
pd("etime", "DELAY", 1, 4, 2, function(v) return ENAMES[v] end, { g = true })
pd("efb", "FEEDBK", 0, 90, 35, pct, { g = true, step = 5, big = 20 })

local COLS = {
  { "type", "inst", "wave", "duty", "pwm", "lfo", "ring", "sync", "osc2", "sweep", "s6581", "oct" },
  { "a", "d", "s", "r", "filt", "cut", "res", "fenv", "fdec", "wah", "key", "analog" },
  { "vib", "vibhz", "trem", "porta", "arp", "arpdir", "arpspd", "gate", "tune", "drive", "crush", "len" },
  { "vol", "pan", "rev", "echo", "cho", "bpm", "swing", "master", "room", "etime", "efb" },
}
local ROWS = {}                              -- linear order of the rows (column by column)
for c = 1, #COLS do for r = 1, #COLS[c] do ROWS[#ROWS + 1] = PDL[COLS[c][r]] end end

-- ---------------------------------------------------------------- tracks
local tracks = {}
local function copy(t) local r = {}; for k, v in pairs(t) do r[k] = v end; return r end

local function kind_of(tr)
  if tr.mode == "drum" then return "drum" end
  return MEL[tr.inst].kind
end

local function load_inst(tr, mode, idx)
  tr.mode, tr.inst, tr.tones = mode, idx, {}
  if mode == "drum" then
    tr.p = { vol = 13, oct = 0 }
    tr.kind = "drum"
  else
    local m = MEL[idx]
    tr.kind = m.kind
    if m.kind == "sid" then
      tr.p = copy(DEF)
      for k, v in pairs(m.o) do tr.p[k] = v end
    else
      tr.p = { vol = 9 }
    end
  end
end

local function new_track(mode, idx)
  local tr = { len = 16, mute = false, solo = false, pats = {}, lvl = 0, tones = {} }
  for b = 1, 4 do tr.pats[b] = {}; for s = 1, 16 do tr.pats[b][s] = 0 end end
  load_inst(tr, mode, idx)
  return tr
end

local function cur_pat(tr) return tr.pats[S.bank] end

-- a step is an integer: 0 = empty. Melodic: (note + 1) + 256 (slide) + 512 (accent). Drums: slot mask (bits 0-7) + 512 (accent).
local function st_note(v) return (v & 255) - 1 end
local function st_slide(v) return v & 256 ~= 0 end
local function st_acc(v) return v & 512 ~= 0 end

-- ---------------------------------------------------------------- tone building
local function getp(p, k) local v = p[k]; if v == nil then return PDL[k].def end; return v end

local SHAPE1 = { [1] = 2, [2] = 1, [3] = 0, [5] = 4, [6] = 5, [7] = 3, [8] = 6 }
local WAVENAMES = { "square", "saw", "triangle", "noise" }
local function sid_wave(t, p)
  local w = p.wave or 1
  local ring, sync = (p.ring or 0) == 1, (p.sync or 0) == 1
  if (ring or sync) and w ~= 4 then
    local couple = (sync and ring) and 3 or (ring and 2 or 1)
    local sh2 = (ring and not sync) and 0 or 2
    t.wave = "sid+"
    t.mod1 = SHAPE1[w] | (couple << 3) | (sh2 << 5)
    t.mod2 = clamp(floor((p.osc2 or 7) * 8), 0, 255)
    t.mdecay = p.sweep or 0
    t.feedback = 0
  elseif w >= 5 then
    t.wave = "sid+"
    t.mod1 = SHAPE1[w] | (2 << 5)
    t.mod2 = 0
    t.feedback = 0.004
  else
    t.wave = WAVENAMES[w]
  end
  t.duty = (p.duty or 50) / 100
  if (p.s6581 or 0) == 1 then t.s6581 = true end
end

-- the overrides every instrument kind shares (a missing value leaves the preset as it is)
local function common(t, p)
  if p.a then t.attack = min(2000, AT[p.a + 1]) end
  if p.d then t.decay = min(2000, DT[p.d + 1]) end
  if p.s then t.sustain = p.s / 15 end
  if p.r then t.release = min(2000, DT[p.r + 1]) end
  if p.filt then
    if p.filt == 0 then t.cutoff = 0
    else
      t.filter = FILTK[p.filt]
      t.cutoff = cuthz(p.cut or 190)
      t.res = (p.res or 3) / 15
      if p.fenv and p.fenv ~= 0 then t.fenv = p.fenv / 10; t.fdecay = p.fdec or 250 end
      if p.wah and p.wah > 0 then t.wah = p.wah / 10 end
      if (p.key or 0) == 1 then t.keytrack = true end
    end
  end
  if p.lfo and ((p.pwm or 0) > 0 or (p.wah or 0) > 0) then t.lfo = p.lfo / 10 end
  if p.pwm and p.pwm > 0 then t.pwm = p.pwm / 100 end
  if p.analog == 1 and p.filt and p.filt > 0 then t.analog = true end
  if p.trem and p.trem > 0 then t.trem = p.trem / 100; if not t.lfo then t.lfo = 5 end end
  if p.drive and p.drive > 0 then t.drive = p.drive / 100 end
  if p.crush and p.crush > 0 then t.crush = 13 - p.crush end
  if p.pan then t.pan = p.pan / 10 end
  if p.rev then t.reverb = p.rev / 100 end
  if p.echo then t.echo = p.echo / 100 end
  if p.cho then t.chorus = p.cho / 100 end
  if p.tune and p.tune ~= 0 then t.detune = p.tune end
end

local function build_tone(tr, slot)
  local key = slot or 0
  local t = tr.tones[key]
  if t then return t end
  local p = tr.p
  if tr.kind == "drum" then
    t = copy(KITS[tr.inst].slots[slot].tone)
  elseif tr.kind == "sim" then
    t = { preset = MEL[tr.inst].base }
  else
    t = {}
    sid_wave(t, p)
  end
  common(t, p)
  if tr.kind ~= "drum" then
    if p.vib and p.vib > 0 then t.vib = p.vib; t.vibhz = (p.vibhz or 60) / 10 end
  end
  tr.tones[key] = t
  return t
end

-- ---------------------------------------------------------------- sound engine
local pl = { on = false, t = 0, i = 0, cur = {}, curt = {}, vq = {}, kfl = {} }
local live = {}                              -- key -> { v = voice, m = note }
local arp_c = { 0, 0, 0, 0, 0 }
local lastlive = nil
local msgtxt, msgt = "", 0
local function msg(s) msgtxt, msgt = s, 150 end

local function tickdur() return 60 / S.bpm / 4 end
local function trvol(tr) return clamp((tr.p.vol or 12) / 15 * S.master / 10, 0, 1) end
local function audible(i)
  local any = false
  for _, t in ipairs(tracks) do if t.solo then any = true end end
  local tr = tracks[i]
  if tr.mute then return false end
  return (not any) or tr.solo
end

local function apply_fx()
  reverb(S.room / 100, 0.5, 1)
  local ms = clamp(60000 / S.bpm * ETIMES[S.etime], 30, 680)
  echo(ms, S.efb / 100, 1)
  chorus(0.8, 2.5, 1)
end

local function arp_index(dir, n, c)
  if n == 1 then return 1 end
  if dir == 1 then return c % n + 1
  elseif dir == 2 then return n - c % n
  elseif dir == 3 then
    local m = 2 * n - 2
    local k = c % m
    return k < n and k + 1 or m - k + 1
  end
  return random(n)
end

local function flash_key(m, col) pl.kfl[m] = { col, 14 } end

-- one melodic note at audio time 'when'; returns nothing. last = the note before (for slides)
local function fire_note(i, tr, note, v, when, dur, vol)
  local p = tr.p
  local tone = build_tone(tr)
  local base = note + 12 * (p.oct or 0)
  local gate = (p.gate or 75) / 100
  local sl = st_slide(v)
  local ms = max(25, dur * 1000 * (sl and 1.04 or gate))
  local v0 = vol * (st_acc(v) and 1 or 0.78)
  local arp = p.arp or 0
  local glide = (p.porta or 0) > 0 and (p.porta) or (sl and max(40, dur * 500) or 0)
  local t = tone
  if tr.last and glide > 0 and arp == 0 then
    t = copy(tone)
    t.pitch = clamp(tr.last - base, -24, 24)
    t.ptime = glide
  end
  if arp > 0 then
    local ch = CHORDS[arp]
    local n = ARPSPDS[p.arpspd or 3]
    local sub = dur / n
    for k = 0, n - 1 do
      local idx = arp_index(p.arpdir or 1, #ch, arp_c[i])
      arp_c[i] = arp_c[i] + 1
      play_at(when + k * sub, tone, base + ch[idx], max(20, sub * 1000 * gate), v0, i)
    end
  else
    play_at(when, t, base, ms, v0, i)
  end
  tr.last = base
  pl.vq[#pl.vq + 1] = { when, i, base, 1 }
end

local function fire_drums(i, tr, mask, v, when, dur, vol)
  local kit = KITS[tr.inst]
  local p = tr.p
  local v0 = vol * (st_acc(v) and 1 or 0.8)
  for s = 1, 8 do
    if mask & (1 << (s - 1)) ~= 0 then
      local sl = kit.slots[s]
      play_at(when, build_tone(tr, s), sl.note + 12 * (p.oct or 0), sl.ms, v0, i)
    end
  end
  pl.vq[#pl.vq + 1] = { when, i, 60, 1 }
end

local function fire_step(i, idx, when, dur)
  local tr = tracks[i]
  local v = cur_pat(tr)[idx]
  pl.vq[#pl.vq + 1] = { when, i, nil, nil, idx }
  if v == 0 or not audible(i) then
    if v == 0 then tr.last = tr.last end
    return
  end
  local vol = trvol(tr)
  if tr.kind == "drum" then fire_drums(i, tr, v & 255, v, when, dur, vol)
  else fire_note(i, tr, st_note(v), v, when, dur, vol) end
end

local function seq_start(now)
  pl.on, pl.t, pl.i = true, now + 0.08, 0
  pl.vq = {}
  for k = 1, 4 do arp_c[k] = 0; tracks[k].last = nil end
end

local function seq_stop()
  pl.on = false
  play_cancel()
  pl.cur, pl.curt, pl.vq = {}, {}, {}
  for _, tr in ipairs(tracks) do tr.last = nil end
end

local LOOK = 0.13
local function seq_run(now)
  if not pl.on then return end
  local dur = tickdur()
  while pl.t < now + LOOK do
    local when = pl.t + ((pl.i % 2 == 1) and S.swing / 100 * dur or 0)
    for i = 1, 4 do
      local tr = tracks[i]
      fire_step(i, pl.i % tr.len + 1, when, dur)
    end
    pl.t = pl.t + dur
    pl.i = pl.i + 1
  end
end

local function seq_visual(now)
  local vq = pl.vq
  while vq[1] and vq[1][1] <= now do
    local e = table.remove(vq, 1)
    if e[5] then pl.cur[e[2]], pl.curt[e[2]] = e[5], e[1]
    else
      tracks[e[2]].lvl = 1
      if e[3] then flash_key(e[3] - 12 * (S.oct + 1), TCOL[e[2]]) end
    end
  end
end

-- ---------------------------------------------------------------- live play
local function note_of(u)
  local k = PK[u]
  return 12 * (S.oct + 1) + k.off
end

local function live_arp_run(now)
  -- every held key joins the chord of the live arpeggiator
  local tr = tracks[S.track]
  if tr.kind == "drum" or (tr.p.arp or 0) == 0 then return end
  local notes = {}
  for u, l in pairs(live) do if l.arp then notes[#notes + 1] = l.m end end
  if #notes == 0 then lastlive = nil; return end
  table.sort(notes)
  lastlive = lastlive or (now + 0.01)
  local p = tr.p
  local n = ARPSPDS[p.arpspd or 3]
  local sub = tickdur() / n
  local ch = CHORDS[p.arp]
  local tone = build_tone(tr)
  while lastlive < now + LOOK do
    local idx = arp_index(p.arpdir or 1, #ch, arp_c[5])
    arp_c[5] = arp_c[5] + 1
    local m = notes[1] + ch[idx] + 12 * (p.oct or 0)
    play_at(lastlive, tone, m, max(20, sub * 1000 * (p.gate or 75) / 100), trvol(tr), 5)
    lastlive = lastlive + sub
  end
end

local function rec_write(tr, slot_or_note, now)
  -- write into the cursor step (stopped) or into the step that is sounding (playing)
  local pat = cur_pat(tr)
  local idx = S.step
  if pl.on and pl.cur[S.track] then
    idx = pl.cur[S.track]
    if now - (pl.curt[S.track] or now) > tickdur() * 0.5 then idx = idx % tr.len + 1 end
  end
  if tr.kind == "drum" then
    local v = pat[idx]
    local mask = (v & 255) ~ (1 << (slot_or_note - 1))
    pat[idx] = mask == 0 and 0 or (mask | (v & 512))
  else
    pat[idx] = (slot_or_note + 1) | ((pat[idx] & 256) ~= 0 and 256 or 0) | ((pat[idx] & 512) ~= 0 and 512 or 0)
    tr.lastn = slot_or_note
  end
  if not pl.on then S.step = idx % tr.len + 1 end
  S.dirty = true
end

local function key_down(u, now)
  local tr = tracks[S.track]
  local k = PK[u]
  local m = note_of(u)
  if tr.kind == "drum" then
    local slot = DRUMREL[k.rel]
    if not slot then return end
    local sl = KITS[tr.inst].slots[slot]
    play(nil, build_tone(tr, slot), sl.note + 12 * (tr.p.oct or 0), sl.ms, trvol(tr))
    tr.lvl = 1
    S.dsel = slot
    if S.rec then rec_write(tr, slot, now) end
    return
  end
  local p = tr.p
  tr.lvl = 1
  flash_key(k.off, TCOL[S.track])
  if (p.arp or 0) > 0 then
    live[u] = { arp = true, m = m }
    lastlive = lastlive or nil
  else
    local tone = build_tone(tr)
    local note = m + 12 * (p.oct or 0)
    local t = tone
    if (p.porta or 0) > 0 and lastlive == nil and tr.llast then
      t = copy(tone); t.pitch = clamp(tr.llast - note, -24, 24); t.ptime = p.porta
    end
    tr.llast = note
    live[u] = { v = play(nil, t, note, 0, trvol(tr)), m = m }
  end
  if S.rec then rec_write(tr, m, now) end
end

local function key_up(u)
  local l = live[u]
  if not l then return end
  if l.v then noteoff(l.v) end
  live[u] = nil
end

local function release_all()
  for u in pairs(live) do key_up(u) end
end

-- ---------------------------------------------------------------- editing
local function set_step_default(tr, idx)
  local pat = cur_pat(tr)
  if tr.kind == "drum" then pat[idx] = 1 << (S.dsel - 1)
  else pat[idx] = (tr.lastn or 12 * (S.oct + 1)) + 1 end
end

local function toggle_step(tr)
  local pat = cur_pat(tr)
  local idx = S.step
  if tr.kind == "drum" then
    local v = pat[idx]
    local mask = (v & 255) ~ (1 << (S.dsel - 1))
    pat[idx] = mask == 0 and 0 or (mask | (v & 512))
  elseif pat[idx] == 0 then set_step_default(tr, idx)
  else pat[idx] = 0 end
  S.dirty = true
end

local function step_note(tr, d)
  local pat = cur_pat(tr)
  local v = pat[S.step]
  if v == 0 or tr.kind == "drum" then return end
  local n = clamp(st_note(v) + d, 0, 127)
  pat[S.step] = (n + 1) | (v & 768)
  tr.lastn = n
  S.dirty = true
end

local function randomize(tr)
  local pat = cur_pat(tr)
  for s = 1, 16 do pat[s] = 0 end
  if tr.kind == "drum" then
    for s = 1, 16 do
      local m = 0
      if (s - 1) % 4 == 0 and random() < 0.9 then m = m | 1 end          -- kick on the beats
      if random() < 0.12 then m = m | 1 end
      if (s - 1) % 8 == 4 then m = m | 2 end                               -- snare on 2 and 4
      if (s - 1) % 2 == 0 and random() < 0.85 then m = m | 4 end          -- hats on the eighths
      if random() < 0.06 then m = m | 8 end
      if random() < 0.05 then m = m | 32 end
      if m ~= 0 then pat[s] = m | (random() < 0.2 and 512 or 0) end
    end
  else
    local scale = { 0, 3, 5, 7, 10, 12 }              -- minor pentatonic
    local root = 12 * (S.oct + 1) + 3 * 0
    for s = 1, 16 do
      if random() < 0.55 then
        local n = clamp(root + scale[random(#scale)], 0, 127)
        pat[s] = (n + 1) | (random() < 0.15 and 256 or 0) | (random() < 0.25 and 512 or 0)
      end
    end
  end
  S.dirty = true
end

local function change_param(tr, row, dir, big)
  if row.g then
    local v = clamp(S[row.key] + dir * (big and row.big or row.step), row.lo, row.hi)
    S[row.key] = v
    if row.key == "bpm" or row.key == "room" or row.key == "etime" or row.key == "efb" then apply_fx() end
    S.dirty = true
    return
  end
  local k = tr.kind
  if row.special then
    if row.key == "type" then
      release_all()
      if tr.mode == "drum" then load_inst(tr, "mel", 1) else load_inst(tr, "drum", 1) end
    elseif row.key == "inst" then
      local n = (tr.mode == "drum") and #KITS or #MEL
      local idx = tr.inst + dir * (big and 5 or 1)
      idx = ((idx - 1) % n) + 1
      load_inst(tr, tr.mode, idx)
    elseif row.key == "len" then
      tr.len = clamp(tr.len + dir, 1, 16)
      if S.step > tr.len then S.step = tr.len end
    end
    S.dirty = true
    return
  end
  if row.s and k ~= "sid" then return end
  if row.m and k == "drum" then return end
  local p = tr.p
  local cur = p[row.key]
  if cur == nil then cur = row.def end
  local nv = clamp(cur + dir * (big and row.big or row.step), row.lo, row.hi)
  p[row.key] = nv
  -- editing a filter value on a ready-made sound switches the filter on
  if k ~= "sid" and p.filt == nil and (row.key == "cut" or row.key == "res" or row.key == "fenv" or row.key == "fdec" or row.key == "wah" or row.key == "key") then
    p.filt = 1; p.cut = p.cut or 190
  end
  if row.key == "filt" and nv > 0 then p.cut = p.cut or 190 end
  tr.tones = {}
  S.dirty = true
end

local function reset_param(tr, row)
  if row.special or row.g then return end
  if tr.kind == "sid" then tr.p[row.key] = row.def
  else tr.p[row.key] = nil end
  tr.tones = {}
  S.dirty = true
end

-- ---------------------------------------------------------------- save and load
local function snapshot()
  local d = { v = 1, bpm = S.bpm, swing = S.swing, master = S.master, room = S.room, etime = S.etime, efb = S.efb,
              bank = S.bank, track = S.track, oct = S.oct, tracks = {} }
  for i, tr in ipairs(tracks) do
    d.tracks[i] = { mode = tr.mode, inst = tr.inst, p = tr.p, len = tr.len, mute = tr.mute, solo = tr.solo, pats = tr.pats }
  end
  return d
end

local function restore(d)
  if type(d) ~= "table" or d.v ~= 1 or type(d.tracks) ~= "table" then return false end
  for i = 1, 4 do
    local t = d.tracks[i]
    if type(t) ~= "table" then return false end
    local mode = t.mode == "drum" and "drum" or "mel"
    local n = mode == "drum" and #KITS or #MEL
    local tr = new_track(mode, clamp(t.inst or 1, 1, n))
    if type(t.p) == "table" then for k, v in pairs(t.p) do if PDL[k] and type(v) == "number" then tr.p[k] = v end end end
    tr.len = clamp(t.len or 16, 1, 16)
    tr.mute, tr.solo = t.mute and true or false, t.solo and true or false
    if type(t.pats) == "table" then
      for b = 1, 4 do
        for s = 1, 16 do
          local v = t.pats[b] and t.pats[b][s]
          tr.pats[b][s] = type(v) == "number" and floor(v) or 0
        end
      end
    end
    tracks[i] = tr
  end
  for _, k in ipairs({ "bpm", "swing", "master", "room", "etime", "efb" }) do
    if type(d[k]) == "number" then S[k] = clamp(d[k], PDL[k].lo, PDL[k].hi) end
  end
  S.bank = clamp(d.bank or 1, 1, 4)
  S.track = clamp(d.track or 1, 1, 4)
  S.oct = clamp(d.oct or 3, 1, 6)
  return true
end

local function save_now()
  save(snapshot())
  S.dirty = false
end

-- the first start: a groove to play with
local function demo()
  tracks[1] = new_track("mel", 1)           -- HEAVY ARP
  tracks[2] = new_track("mel", 6)           -- FAT BASS
  tracks[3] = new_track("mel", 2)           -- PULSE LEAD
  tracks[4] = new_track("drum", 4)          -- SID KIT
  local function put(tr, notes) for s, n in pairs(notes) do tr.pats[1][s] = n end end
  local C3, D3, E3, G3, A3 = 48, 50, 52, 55, 57
  put(tracks[1], { [1] = (C3 + 1) | 512, [9] = (A3 - 3 + 1) | 512 })            -- C minor arp, then Ab
  put(tracks[2], { [1] = (C3 - 12 + 1) | 512, [4] = (C3 - 12 + 1), [7] = (C3 - 12 + 1) | 256, [9] = (G3 - 12 + 1) | 512,
                   [12] = (G3 - 12 + 1), [15] = (A3 - 12 - 2 + 1) | 256 })
  put(tracks[3], { [3] = (G3 + 12 + 1), [5] = (G3 + 14 + 1) | 512, [8] = (G3 + 15 + 1), [11] = (G3 + 10 + 1) | 256,
                   [13] = (G3 + 12 + 1) | 512, [16] = (G3 + 7 + 1) })
  tracks[3].p.vol = 10
  for s = 1, 16 do
    local m = 0
    if s == 1 or s == 5 or s == 9 or s == 12 or s == 13 then m = m | 1 end
    if s == 5 or s == 13 then m = m | 2 end
    if s % 2 == 1 then m = m | 4 end
    if s == 16 then m = m | 8 end
    tracks[4].pats[1][s] = m
  end
  for _, s in ipairs({ 1, 5, 9, 13 }) do tracks[4].pats[1][s] = tracks[4].pats[1][s] | 512 end
  tracks[4].pats[1][1] = tracks[4].pats[1][1] | 512
  S.bpm = 118
end

-- ---------------------------------------------------------------- drawing
local function T(s, x, y, c, sc) return print(s, x, y, c or C.wht, sc) end
local function INV(s, x, y, fg, bg) rectfill(x, y, #s * 8, 16, bg); print(s, x, y, fg) end

local function dim(c, f)
  local r, g, b = (c >> 16) & 255, (c >> 8) & 255, c & 255
  return (floor(r * f) << 16) | (floor(g * f) << 8) | floor(b * f)
end

local function step_label(tr, v)
  if tr.kind == "drum" then
    local kit = KITS[tr.inst]
    local s, n = "", 0
    for k = 1, 8 do
      if v & (1 << (k - 1)) ~= 0 then n = n + 1; if n <= 2 then s = s .. kit.slots[k].ch end end
    end
    if n > 2 then s = s:sub(1, 1) .. "+" end
    return s
  end
  return nname(st_note(v))
end

local function draw_pads(i, y, sel, now)
  local tr = tracks[i]
  local col = TCOL[i]
  local pat = cur_pat(tr)
  for s = 1, 16 do
    local x = 32 + (s - 1) * 36
    if s <= tr.len then
      local v = pat[s]
      local base = ((s - 1) // 4) % 2 == 0 and 0x3C3C3C or 0x2A2A2A
      local fill = v ~= 0 and col or base
      if v ~= 0 and (tr.mute) then fill = dim(col, 0.4) end
      local playing = pl.on and pl.cur[i] == s
      if playing then fill = v ~= 0 and C.wht or 0x707070 end
      rectfill(x, y, 34, 34, fill)
      if v ~= 0 then
        local txt = step_label(tr, v)
        T(txt, x + (34 - #txt * 8) // 2, y + 9, playing and C.blk or C.blk)
        if st_acc(v) then rectfill(x + 26, y + 2, 6, 6, C.red) end
        if tr.kind ~= "drum" and st_slide(v) then
          rectfill(x + 2, y + 28, 30, 3, C.wht)
        end
      end
      if sel and S.step == s and S.page == 1 then
        local blink = (floor(now * 4) % 2 == 0)
        rect(x - 1, y - 1, 36, 36, blink and C.yel or C.wht)
        rect(x, y, 34, 34, blink and C.yel or C.wht)
      end
    else
      rectfill(x + 14, y + 16, 4, 2, 0x303030)
    end
  end
end

local function draw_status(now)
  rectfill(0, 0, W, 18, C.blu)
  INV(" SIDBOX ", 4, 1, C.blk, C.lblu)
  local x = 84
  T(fmt("BPM:%d", S.bpm), x, 1, C.wht); x = x + 8 * 8
  T(fmt("BANK:%s", string.char(64 + S.bank)), x, 1, C.cyan); x = x + 8 * 8
  T(fmt("OCT:%d", S.oct), x, 1, C.yel); x = x + 7 * 8
  T(fmt("SWG:%d", S.swing), x, 1, C.lgr); x = x + 8 * 8
  if S.rec then INV(" REC ", x, 1, C.wht, floor(now * 3) % 2 == 0 and C.red or dim(C.red, 0.6)) end
  x = 470
  if pl.on then INV(" PLAY ", x, 1, C.blk, C.lgrn) else INV(" STOP ", x, 1, C.wht, C.red) end
  local cs = pl.on and (pl.cur[S.track] or 1) or S.step
  T(fmt("%02d/%02d", cs, tracks[S.track].len), 552, 1, C.wht)
  local pages = { "SEQ", "SND", "VIS" }
  for k = 1, 3 do
    if S.page == k then INV(pages[k], 604, 1, C.blu, C.wht) end
  end
end

-- the keyboard: 29 semitones (C to E two octaves up) as the keys of the two rows
local WHITE = { [0] = 0, [2] = 1, [4] = 2, [5] = 3, [7] = 4, [9] = 5, [11] = 6 }
local BLACKS = { 1, 3, 6, 8, 10 }
local function draw_keys(x, y, h)
  local ww = 16
  local tr = tracks[S.track]
  local function wx(m) return x + ((m // 12) * 7 + WHITE[m % 12]) * ww end
  local isw = function(m) return WHITE[m % 12] ~= nil end
  for m = 0, 28 do
    if isw(m) then
      local fl = pl.kfl[m]
      local c = C.lgr
      if fl then c = fl[1] end
      for u, l in pairs(live) do if l.m and l.m - 12 * (S.oct + 1) == m then c = C.wht end end
      rectfill(wx(m), y, ww - 1, h, c)
      local lab = m <= 16 and LOWERLAB[m + 1] or UPPERLAB[m]
      if lab then T(lab, wx(m) + 4, y + h - 17, C.blk) end
    end
  end
  for m = 0, 28 do
    if not isw(m) then
      local fl = pl.kfl[m]
      local c = fl and fl[1] or C.blk
      for u, l in pairs(live) do if l.m and l.m - 12 * (S.oct + 1) == m then c = C.wht end end
      local bx = wx(m - 1) + ww - 5
      rectfill(bx, y, 10, h * 0.6, c)
      rect(bx, y, 10, h * 0.6, C.dgr)
      local lab = m <= 16 and LOWERLAB[m + 1] or UPPERLAB[m]
      if lab then T(lab, bx + 1, y + h * 0.6 - 17, c == C.blk and C.lgr or C.blk) end
    end
  end
end

local function draw_seq(now)
  for i = 1, 4 do
    local y = 22 + (i - 1) * 54
    local tr = tracks[i]
    local sel = S.track == i
    local col = TCOL[i]
    local nm = tr.mode == "drum" and KITS[tr.inst].name or MEL[tr.inst].name
    T(tostring(i), 10, y + 14, sel and col or dim(col, 0.55), 2)
    local hc = sel and col or dim(col, 0.6)
    T((sel and ">" or " ") .. nm, 28, y, hc)
    local x = 28 + (#nm + 3) * 8
    if tr.kind ~= "drum" then
      local p = tr.p
      local info = {}
      if tr.kind == "sid" then
        info[#info + 1] = WAVES[p.wave or 1]
        if (p.ring or 0) == 1 then info[#info + 1] = "RING" end
        if (p.sync or 0) == 1 then info[#info + 1] = "SYNC" end
        if (p.pwm or 0) > 0 then info[#info + 1] = "PWM" end
      end
      if (p.filt or 0) > 0 then info[#info + 1] = FILTS[p.filt] end
      if (p.arp or 0) > 0 then info[#info + 1] = "ARP:" .. ARPS[p.arp] end
      T(table.concat(info, " "), x, y, C.mgr)
    else
      T("DRUMS  ARMED:" .. KITS[tr.inst].slots[S.dsel].name, x, y, C.mgr)
    end
    local fx = 520
    T(fmt("LEN%02d", tr.len), fx, y, C.mgr)
    if tr.mute then INV("M", fx + 56, y, C.wht, C.red) end
    if tr.solo then INV("S", fx + 66, y, C.blk, C.yel) end
    draw_pads(i, y + 16, sel, now)
    -- level meter
    local lv = clamp(tr.lvl, 0, 1)
    rectfill(612, y + 16, 8, 34, 0x202020)
    if lv > 0.02 then rectfill(612, y + 16 + 34 - floor(34 * lv), 8, floor(34 * lv), col) end
  end
  -- keyboard and help
  draw_keys(8, 244, 70)
  local tr = tracks[S.track]
  local hx = 296
  font("6x12")
  local help = {
    "SPACE PLAY/STOP  F1-F4 TRACK  F5 SEQ F6 SOUND F7 VIS",
    "ENTER STEP  BKSP CLEAR  A ACCENT  F SLIDE  [ ] LENGTH",
    "SHIFT+UP/DN NOTE  CTRL+UP/DN OCTAVE OF STEP",
    "PGUP/PGDN KEYBOARD OCTAVE  HOME/END TEMPO  -/= SWING",
    "TAB MUTE  SH+TAB SOLO  F8 REC  F9 RANDOM  F10 CLEAR",
    "SH+F1-F4 PATTERN A-D   Z..M AND Q..P PLAY LIVE",
  }
  for k = 1, #help do print(help[k], hx, 246 + (k - 1) * 13, k == 6 and C.yel or C.lgr) end
  font("8x16")
  if msgt > 0 then T(msgtxt, 8, 328, C.yel) end
end

-- the filter's response (a 2-pole state variable), for the curve and the visualizer
local function filter_mag(p, f)
  local mode = p.filt or 0
  if mode == 0 then return 1 end
  local fc = cuthz(p.cut or 190)
  local q = 0.55 + (p.res or 3) / 15 * 6
  local r = f / fc
  local d = (1 - r * r) ^ 2 + (r / q) ^ 2
  if mode == 1 then return 1 / math.sqrt(d)
  elseif mode == 2 then return (r / q) / math.sqrt(d)
  elseif mode == 3 then return r * r / math.sqrt(d)
  else return abs(1 - r * r) / math.sqrt(d) end
end

local function draw_env(x, y, w, h, tr)
  local p = tr.p
  rect(x, y, w, h, C.dgr)
  local a = tr.kind == "sid" and min(2000, AT[(p.a or 0) + 1]) or (p.a and min(2000, AT[p.a + 1]) or 5)
  local d = tr.kind == "sid" and min(2000, DT[(p.d or 5) + 1]) or (p.d and min(2000, DT[p.d + 1]) or 200)
  local s = (p.s or 10) / 15
  local r = tr.kind == "sid" and min(2000, DT[(p.r or 4) + 1]) or (p.r and min(2000, DT[p.r + 1]) or 120)
  local function sx(ms) return math.sqrt(ms / 2000) end
  local sa, sd, sr = sx(a), sx(d), sx(r)
  local tot = sa + sd + 0.35 + sr
  local scale = (w - 4) / tot
  local col = TCOL[S.track]
  local x0, yb = x + 2, y + h - 3
  local ytop = y + 4
  local ys = yb - (yb - ytop) * s
  local x1 = x0 + sa * scale
  local x2 = x1 + sd * scale
  local x3 = x2 + 0.35 * scale
  local x4 = x3 + sr * scale
  line(x0, yb, x1, ytop, col); line(x1, ytop, x2, ys, col)
  line(x2, ys, x3, ys, col); line(x3, ys, x4, yb, col)
end

local function draw_curve(x, y, w, h, tr)
  rect(x, y, w, h, C.dgr)
  local col = TCOL[S.track]
  local py
  for i = 0, w - 3 do
    local f = 30 * 2 ^ (i / (w - 3) * 9.4)
    local m = filter_mag(tr.p, f)
    local v = clamp(m, 0.001, 4)
    local yy = y + h - 4 - clamp((math.log(v, 10) + 1.4) / 2.2, 0, 1) * (h - 8)
    if py then line(x + 1 + i - 1, py, x + 1 + i, yy, col) end
    py = yy
  end
end

local function draw_snd(now)
  local tr = tracks[S.track]
  -- track tabs
  for i = 1, 4 do
    local nm = tracks[i].mode == "drum" and KITS[tracks[i].inst].name or MEL[tracks[i].inst].name
    local x = 8 + (i - 1) * 158
    if i == S.track then INV(fmt("%d:%s", i, nm:sub(1, 16)), x, 22, C.blk, TCOL[i])
    else T(fmt("%d:%s", i, nm:sub(1, 16)), x, 22, dim(TCOL[i], 0.6)) end
  end
  -- the parameter grid
  local k = tr.kind
  local idx = 0
  for c = 1, #COLS do
    local x = 8 + (c - 1) * 158
    for r = 1, #COLS[c] do
      idx = idx + 1
      local row = ROWS[idx]
      local y = 46 + (r - 1) * 16
      local ok = true
      if row.s and k ~= "sid" then ok = false end
      if row.m and k == "drum" then ok = false end
      local selrow = (idx == S.prow)
      local label = row.name
      local val
      if row.key == "type" then val = row.fmt(tr.mode == "drum" and 1 or 0)
      elseif row.key == "inst" then
        val = tr.mode == "drum" and KITS[tr.inst].name or MEL[tr.inst].name
        val = fmt("%02d %s", tr.inst, val)
      elseif row.key == "len" then val = tostring(tr.len)
      elseif row.g then val = row.fmt(S[row.key])
      else
        local v = tr.p[row.key]
        if v == nil then val = (k == "sid") and row.fmt(row.def) or "PRESET"
        else val = row.fmt(v) end
      end
      local lc = ok and TCOL[S.track] or C.dgr
      local vc = ok and C.wht or C.dgr
      if val == "PRESET" then vc = C.mgr end
      if row.g then lc = C.lblu end
      if selrow then rectfill(x - 2, y, 156, 16, C.blu) end
      if row.key == "inst" then
        T(label, x, y, lc); T(val:sub(1, 11), x + 56, y, vc)
      else
        T(label, x, y, lc); T(val:sub(1, 12), x + 64, y, vc)
      end
    end
  end
  -- graphs
  draw_env(8, 246, 140, 78, tr)
  T("ENVELOPE", 8, 326, C.mgr)
  draw_curve(160, 246, 140, 78, tr)
  T("FILTER", 160, 326, C.mgr)
  draw_keys(312, 252, 64)
  font("6x12")
  print("UP/DN ROW  LT/RT VALUE  SHIFT=BIG  ENTER COLUMN  DEL RESET", 312, 324, C.lgr)
  font("8x16")
  if msgt > 0 then T(msgtxt, 8, 344, C.yel) end
end

-- the visualizer: copper bars, a wavy title and a scope and spectrum for each track
local function wave_at(tr, ph)
  ph = ph - floor(ph)
  local k = tr.kind
  if k == "sid" then
    local w = tr.p.wave or 1
    if w == 1 then return ph < (tr.p.duty or 50) / 100 and 1 or -1
    elseif w == 2 then return 2 * ph - 1
    elseif w == 3 then return ph < 0.5 and (4 * ph - 1) or (3 - 4 * ph)
    elseif w == 4 then return random() * 2 - 1
    else return 0.5 * ((ph < 0.5 and (4 * ph - 1) or (3 - 4 * ph)) + (ph < (tr.p.duty or 50) / 100 and 1 or -1)) end
  elseif k == "drum" then return random() * 2 - 1 end
  return sin(2 * pi * ph) + 0.4 * sin(4 * pi * ph + 1) + 0.2 * sin(6 * pi * ph)
end

local COPPER = { C.blu, C.lblu, C.cyan, C.wht, C.cyan, C.lblu, C.blu }
local function draw_vis(now)
  local lvlsum = 0
  for i = 1, 4 do lvlsum = lvlsum + tracks[i].lvl end
  -- copper bars
  for b = 0, 6 do
    local yc = 78 + sin(now * 1.4 + b * 0.5) * (34 + lvlsum * 12)
    for k = 1, #COPPER do rectfill(0, floor(yc) + (k - 1) * 4, W, 4, dim(COPPER[k], 0.6 + 0.4 * b / 6)) end
  end
  -- the title
  local title = "SIDBOX   4 VOICES   SID STYLE"
  for k = 1, #title do
    local ch = title:sub(k, k)
    local x = 24 + (k - 1) * 20
    local y = 26 + sin(now * 3 + k * 0.5) * 8
    T(ch, x + 2, y + 2, C.blk, 2)
    local cc = COPPER[(k + floor(now * 6)) % #COPPER + 1]
    T(ch, x, y, cc == C.blu and C.lblu or cc, 2)
  end
  -- the four channels
  for i = 1, 4 do
    local tr = tracks[i]
    local x = 6 + (i - 1) * 158
    local y = 150
    rectfill(x, y + 8, 152, 190, C.blk)
    rect(x, y + 8, 152, 190, TCOL[i])
    local nm = tr.mode == "drum" and KITS[tr.inst].name or MEL[tr.inst].name
    INV(fmt("%d %s", i, nm:sub(1, 15)), x + 2, y + 10, C.blk, TCOL[i])
    local lv = clamp(tr.lvl, 0, 1)
    -- scope
    local mid = y + 56
    local note = tr.last or 60
    local cyc = clamp(2 ^ ((note - 48) / 12) * 2, 1, 10)
    local px, py
    for xi = 0, 140 do
      local v = wave_at(tr, xi / 140 * cyc) * lv
      local yy = mid - v * 24
      if px then line(px, py, x + 6 + xi, yy, TCOL[i]) end
      px, py = x + 6 + xi, yy
    end
    -- spectrum
    local base = y + 188
    local fund = 440 * 2 ^ ((note - 69) / 12)
    for b = 1, 16 do
      local f = 40 * 2 ^ (b * 0.5)
      local amp
      local k = tr.kind == "sid" and (tr.p.wave or 1) or 0
      if tr.kind == "drum" or k == 4 then amp = 0.6 + random() * 0.4
      elseif k == 3 then amp = (f <= fund * 1.2 and 1 or (fund / f) ^ 2)
      else amp = (f <= fund * 1.2 and 1 or (fund / f) ^ 0.8) end
      amp = amp * filter_mag(tr.p, f) * (b == 1 and 0.6 or 1)
      local hgt = clamp(amp * lv * 75, 0, 75)
      local c = hgt > 55 and C.yel or (hgt > 30 and C.lgrn or C.grn)
      for seg = 0, floor(hgt / 5) - 1 do rectfill(x + 6 + (b - 1) * 8 + 0, base - 5 - seg * 5, 7, 4, c) end
    end
  end
  T("F5 SEQ   F6 SOUND   SPACE PLAY", 8, 344, C.lgr)
end

function _draw()
  local now = audio_time()
  cls(C.blk)
  if S.page == 3 then
    draw_vis(now)
    draw_status(now)
  else
    draw_status(now)
    if S.page == 1 then draw_seq(now) else draw_snd(now) end
  end
end

-- ---------------------------------------------------------------- input
local function handle_keys(now)
  local tr = tracks[S.track]
  if pressed(K.SPACE) then
    if pl.on then seq_stop() else seq_start(now) end
  end
  if pressed(K.F5) then S.page = 1 end
  if pressed(K.F6) then S.page = 2 end
  if pressed(K.F7) then S.page = 3 end
  if pressed(K.F8) then S.rec = not S.rec; msg(S.rec and "RECORD ON: KEYS WRITE STEPS" or "RECORD OFF") end
  for i = 1, 4 do
    if pressed(K.F1 + i - 1) then
      if shift() then S.bank = i; msg("BANK " .. string.char(64 + i)); S.dirty = true
      else S.track = i; if S.step > tracks[i].len then S.step = tracks[i].len end end
    end
  end
  tr = tracks[S.track]
  if rep(K.PGUP) then S.oct = clamp(S.oct + 1, 1, 6); release_all() end
  if rep(K.PGDN) then S.oct = clamp(S.oct - 1, 1, 6); release_all() end
  if rep(K.HOME) then S.bpm = clamp(S.bpm - (shift() and 5 or 1), 40, 240); apply_fx(); S.dirty = true end
  if rep(K.END) then S.bpm = clamp(S.bpm + (shift() and 5 or 1), 40, 240); apply_fx(); S.dirty = true end
  if rep(K.MINUS) then S.swing = clamp(S.swing - 5, 0, 60); S.dirty = true end
  if rep(K.EQUAL) then S.swing = clamp(S.swing + 5, 0, 60); S.dirty = true end
  if pressed(K.TAB) then
    if shift() then tr.solo = not tr.solo; msg(tr.solo and "SOLO" or "SOLO OFF")
    else tr.mute = not tr.mute; msg(tr.mute and "MUTE" or "UNMUTE") end
    S.dirty = true
  end
  if pressed(K.F9) then randomize(tr); msg("RANDOM PATTERN") end
  if pressed(K.F10) then
    local pat = cur_pat(tr)
    for s = 1, 16 do pat[s] = 0 end
    S.dirty = true; msg("PATTERN CLEARED")
  end

  -- the piano
  for k = 1, #PIANOKEYS do
    local u = PIANOKEYS[k]
    if pressed(u) then key_down(u, now) end
    if released(u) then key_up(u) end
  end

  if S.page == 1 then
    if rep(K.RIGHT) then S.step = S.step % tr.len + 1 end
    if rep(K.LEFT) then S.step = (S.step - 2) % tr.len + 1 end
    if rep(K.UP) then
      if shift() then step_note(tr, 1)
      elseif ctrl() then step_note(tr, 12)
      else S.track = (S.track - 2) % 4 + 1; S.step = min(S.step, tracks[S.track].len) end
    end
    if rep(K.DOWN) then
      if shift() then step_note(tr, -1)
      elseif ctrl() then step_note(tr, -12)
      else S.track = S.track % 4 + 1; S.step = min(S.step, tracks[S.track].len) end
    end
    if pressed(K.ENTER) then toggle_step(tr) end
    if pressed(K.BKSP) then cur_pat(tr)[S.step] = 0; S.dirty = true end
    if pressed(K.A) then
      local pat = cur_pat(tr)
      if pat[S.step] ~= 0 then pat[S.step] = pat[S.step] ~ 512; S.dirty = true end
    end
    if pressed(K.F) and tr.kind ~= "drum" then
      local pat = cur_pat(tr)
      if pat[S.step] ~= 0 then pat[S.step] = pat[S.step] ~ 256; S.dirty = true end
    end
    if rep(K.LB) then tr.len = clamp(tr.len - 1, 1, 16); S.step = min(S.step, tr.len); S.dirty = true end
    if rep(K.RB) then tr.len = clamp(tr.len + 1, 1, 16); S.dirty = true end
    if tr.kind == "drum" and shift() then
      if rep(K.LEFT) then end
    end
  elseif S.page == 2 then
    if rep(K.DOWN) then S.prow = S.prow % #ROWS + 1 end
    if rep(K.UP) then S.prow = (S.prow - 2) % #ROWS + 1 end
    if pressed(K.TAB) and false then end
    local row = ROWS[S.prow]
    if rep(K.RIGHT) then change_param(tr, row, 1, shift()) end
    if rep(K.LEFT) then change_param(tr, row, -1, shift()) end
    if pressed(K.DELETE) then reset_param(tr, row) end
    if pressed(K.ENTER) then
      -- jump to the next column
      local c, acc = 1, 0
      for cc = 1, #COLS do
        if S.prow <= acc + #COLS[cc] then c = cc; break end
        acc = acc + #COLS[cc]
      end
      local n = c % #COLS + 1
      local start = 0
      for cc = 1, n - 1 do start = start + #COLS[cc] end
      S.prow = start + 1
    end
  end
end

-- ---------------------------------------------------------------- main
function _init()
  screen(W, H)
  rawkeys(true)
  font("8x16")
  local d = saved()
  if not (d and restore(d)) then
    for i = 1, 4 do tracks[i] = new_track("mel", 1) end
    demo()
  end
  apply_fx()
  S.idle = 0
end

function _update()
  poll()
  local now = audio_time()
  handle_keys(now)
  seq_run(now)
  seq_visual(now)
  live_arp_run(now)
  for i = 1, 4 do tracks[i].lvl = tracks[i].lvl * 0.92 end
  for m, f in pairs(pl.kfl) do f[2] = f[2] - 1; if f[2] <= 0 then pl.kfl[m] = nil end end
  if msgt > 0 then msgt = msgt - 1 end
  if S.dirty then
    S.idle = S.idle + 1
    if S.idle > 240 then save_now(); S.idle = 0 end
  else
    S.idle = 0
  end
end

function _exit()
  if S.dirty then save_now() end
  return true
end
