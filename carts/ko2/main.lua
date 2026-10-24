-- KO-II: a keyboard groovebox in the spirit of a pocket sampler/sequencer.
-- 12 pads x 4 groups, a 4-bar step recorder, 12 effects, an LCD, a fader and a
-- volume knob. Original code: the layout is inspired by the EP-133 K.O. II.

local floor, max, min, abs, sin, cos, pi = math.floor, math.max, math.min, math.abs, math.sin, math.cos, math.pi
local fmt = string.format
local insert, remove = table.insert, table.remove

local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end
local function round(v) return floor(v + 0.5) end

-- ---------------------------------------------------------------- keys
-- USB HID usages. A group button is the first key of each row (6, Y, H, N),
-- the pads are the three keys that follow.
local K = {
  A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09, G = 0x0A, H = 0x0B, I = 0x0C,
  J = 0x0D, K = 0x0E, L = 0x0F, M = 0x10, N = 0x11, O = 0x12, Q = 0x14, R = 0x15, S = 0x16,
  T = 0x17, U = 0x18, W = 0x1A, X = 0x1B, Y = 0x1C, Z = 0x1D,
  D1 = 0x1E, D2 = 0x1F, D3 = 0x20, D4 = 0x21, D5 = 0x22, D6 = 0x23, D7 = 0x24, D8 = 0x25, D9 = 0x26,
  V = 0x19, MINUS = 0x2D, EQUAL = 0x2E, SPACE = 0x2C, COMMA = 0x36, PERIOD = 0x37, SLASH = 0x38, LB = 0x2F, RB = 0x30, F1 = 0x3A,
  RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52, SHL = 0xE1, SHR = 0xE5,
}

local PADKEY = { K.D7, K.D8, K.D9, K.U, K.I, K.O, K.J, K.K, K.L, K.M, K.COMMA, K.PERIOD }
local PADLAB = { "7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "ENT" }
local PADHINT = { "7", "8", "9", "U", "I", "O", "J", "K", "L", "M", ",", "." }
local PADNUM = { 7, 8, 9, 4, 5, 6, 1, 2, 3, 10, 11, 12 }       -- pattern / fx chosen by the pad
local GRPKEY = { K.D6, K.Y, K.H, K.N }
local GRPHINT = { "6", "Y", "H", "N" }
local GRPNAME = { "A", "B", "C", "D" }
local PARAMKEY = { K.D1, K.D2, K.D3, K.D4, K.D5 }

local tracked, held, was, hold = {}, {}, {}, {}
for _, u in pairs(K) do tracked[#tracked + 1] = u end

local function poll()
  for i = 1, #tracked do
    local u = tracked[i]
    was[u] = held[u]
    held[u] = keydown(u)
  end
end
local function down(u) return held[u] end
local function pressed(u) return held[u] and not was[u] end
local function released(u) return was[u] and not held[u] end
local function shift() return held[K.SHL] or held[K.SHR] end

-- ---------------------------------------------------------------- sounds
-- name, instrument (tone table), root note; one = a one-shot (a pad held does not hold it)
local function drum(name, sample) return { name = name, tone = { preset = "kit", sample = sample }, one = true, ms = 700, kit = sample } end
local function oneshot(name, preset, note, ms) return { name = name, tone = { preset = preset }, note = note or 60, one = true, ms = ms or 600 } end
local function inst(name, preset, note) return { name = name, tone = { preset = preset }, note = note or 60 } end

-- in the order of the pads on the grid (7 8 9 / 4 5 6 / 1 2 3 / . 0 ENT)
local SOUNDS = {
  { drum("TOM", "tom"), drum("COWBL", "cb"), oneshot("CRASH", "crash", 60, 1600),
    drum("OHAT", "oh"), drum("CLAP", "cp"), drum("RIM", "rim"),
    drum("KICK", "bd"), drum("SNARE", "sd"), drum("CHAT", "hh"),
    oneshot("PUNCH", "punch", 36), oneshot("SNARE2", "snare", 60), oneshot("SHAKER", "shaker", 60) },
  { inst("SUB C", "sub", 36), inst("SUB F", "sub", 41), inst("SUB G", "sub", 43),
    inst("BASS C", "bass", 36), inst("BASS D#", "bass", 39), inst("BASS G", "bass", 43),
    inst("ACID C", "acid", 36), inst("ACID D#", "acid", 39), inst("ACID G", "acid", 43),
    inst("FM BAS", "fmbass", 36), inst("PICK", "pickbass", 36), inst("8BIT B", "bitbass", 36) },
  { inst("EPIANO", "epiano", 60), inst("RHODES", "rhodes", 60), inst("ORGAN", "organ", 60),
    inst("MARIMB", "marimba", 60), inst("BELL", "bell", 72), inst("GLOCK", "glock", 72),
    inst("HARP", "harp", 60), inst("PLUCK", "pluck", 60), inst("GUITAR", "guitar", 55),
    inst("STRNGS", "strings", 60), inst("PAD", "pad", 60), inst("CHOIR", "choir", 60) },
  { inst("LEAD", "lead", 60), inst("SAWLD", "sawlead", 60), inst("FLUTE", "flute", 72),
    inst("BRASS", "brass", 60), inst("CHIP", "chip", 72), inst("SOLO", "solo", 60),
    oneshot("LASER", "laser", 60, 500), oneshot("BLIP", "blip", 72, 200), oneshot("BOOM", "boom", 36, 900),
    inst("WIND", "wind", 60), inst("VINYL", "vinyl", 60), inst("GLASS", "glass", 60) },
}
local ALL = {}
for g = 1, 4 do for p = 1, 12 do ALL[#ALL + 1] = SOUNDS[g][p] end end

-- the kit's drums play at their own root note
do
  local kit = audio_samples and audio_samples("kit") or {}
  local order = { "bd", "sd", "hh", "oh", "cp", "rim", "tom", "cb" }
  for i, nm in ipairs(order) do
    local r = kit[i] and kit[i].root or 60
    for _, sd in ipairs(ALL) do if sd.kit == nm then sd.note = r end end
  end
end

-- ---------------------------------------------------------------- state
local TPB, TPBAR = 24, 96                       -- ticks per beat, per bar
local QN = { [0] = 0, 24, 12, 6, 3 }            -- quantize grid, in ticks
local QNAME = { [0] = "OFF", "1/4", "1/8", "1/16", "1/32" }
local MODES = { "main", "sound", "pattern", "timing", "fx", "keys" }
local MODENAME = { main = "MAIN", sound = "SOUND", pattern = "PATTERN", timing = "TIMING", fx = "FX", keys = "KEYS" }

local S = {
  bpm = 120, quant = 3, swing = 0, metro = false, master = 0.8,
  glvl = { 1, 1, 1, 1 }, fx = 1, fxamt = 0.5,
  pat = 1, group = 1, sel = { 1, 7 }, oct = 0,
  pads = {}, patterns = {},
}
for g = 1, 4 do
  S.pads[g] = {}
  for p = 1, 12 do S.pads[g][p] = { snd = (g - 1) * 12 + p, lvl = 1, pit = 0, flt = 1, hpf = 0, dec = nil, atk = nil, rel = nil,
                     send = 1, pan = 0, tune = 0, vel = 1, mod = 0 } end
end
S.sel = { 1, 7 }

local mode, psel, dirty = "main", { main = 1, sound = 1, pattern = 1, timing = 1, fx = 1, keys = 1 }, false
local showhelp = false
local recarm = false
local meter = { l = 0, r = 0 }
local flash, gflash = {}, {}                    -- pad / group lit for a few frames
local fq = {}                                   -- pads the sequencer plays: {time, group, pad}
local padhold = {}                              -- pad -> { voice =, ev =, t0 = }
local taps = {}
local frame_n, idle_save = 0, 0

local function PT()
  local pt = S.patterns[S.pat]
  if not pt then pt = { bars = 1, ev = {} }; S.patterns[S.pat] = pt end
  return pt
end
local function plen() return PT().bars * TPBAR end

-- ---------------------------------------------------------------- effects
-- each effect changes the tone of the notes that start after it; amt is 0..1
local FXS = {
  { name = "OFF" },
  { name = "DELAY", echo = function(bpm) return 60000 / bpm * 0.75, 0.45 end, apply = function(t, a) t.echo = a end },
  { name = "PING", echo = function(bpm) return 60000 / bpm * 0.5, 0.6 end, apply = function(t, a) t.echo = a end },
  { name = "ROOM", room = { 0.25, 0.6 }, apply = function(t, a) t.reverb = a * 0.8 end },
  { name = "HALL", room = { 0.9, 0.35 }, apply = function(t, a) t.reverb = a end },
  { name = "CHORUS", apply = function(t, a) t.chorus = a end },
  { name = "DRIVE", apply = function(t, a) t.drive = a; t.curve = "hard" end },
  { name = "CRUSH", apply = function(t, a) t.crush = clamp(round(16 - a * 12), 4, 15); t.coarse = 1 + floor(a * 3) end },
  { name = "LOWPASS", apply = function(t, a)
      local c = floor(19000 * 2 ^ (-a * 5.5)); t.filter = "lp"
      t.cutoff = (t.cutoff and t.cutoff > 0) and min(t.cutoff, c) or c end },
  { name = "HIPASS", apply = function(t, a) t.filter = "hp"; t.cutoff = floor(30 * 2 ^ (a * 7.5)) end },
  { name = "WAH", apply = function(t, a) t.lfo = 1 + a * 6; t.wah = a * 2; t.res = 0.5; if not t.cutoff then t.cutoff = 900 end end },
  { name = "VOCAL", apply = function(t, a) t.vowel = a > 0.5 and "a" or "o" end },
}

local function fx_globals()
  local fx = FXS[S.fx]
  if fx.room then reverb(fx.room[1], fx.room[2], 1) else reverb(0.3, 0.5, 1) end
  if fx.echo then
    local ms, fb = fx.echo(S.bpm)
    echo(min(680, ms), fb, 1)
  end
end

-- ---------------------------------------------------------------- sound
local function note_text(m)
  local names = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }
  return names[m % 12 + 1] .. (m // 12 - 1)
end

local function build_tone(sd, pr)
  local t = {}
  for k, v in pairs(sd.tone) do t[k] = v end
  t.pan = pr.pan
  if pr.flt < 0.995 then t.filter = "lp"; t.cutoff = floor(80 * 2 ^ (pr.flt * 7.8))
  elseif pr.hpf > 0.01 then t.filter = "hp"; t.cutoff = floor(30 * 2 ^ (pr.hpf * 7.5)) end
  if pr.dec then t.decay = 30 + pr.dec * pr.dec * 1800 end
  if pr.atk then t.attack = pr.atk * pr.atk * 800 end
  if pr.rel then t.release = 20 + pr.rel * pr.rel * 2000 end
  if pr.tune ~= 0 then t.detune = pr.tune end
  if pr.mod > 0.01 then t.vib = pr.mod * 40; t.vibhz = 5.5 end
  local fx = FXS[S.fx]
  local amt = S.fxamt * pr.send
  if fx.apply and amt > 0.01 then fx.apply(t, amt) end
  return t
end

-- g, p: the sound's pad; sem: semitones above its root (keys mode); when: audio time (sequencer)
local function hit(g, p, sem, vel, when, ms)
  local pr = S.pads[g][p]
  local sd = ALL[pr.snd]
  local note = clamp(floor((sd.note or 60) + pr.pit + (sem or 0)), 1, 127)
  local vol = clamp(S.master * S.glvl[g] * pr.lvl * pr.vel * (vel or 1), 0, 1)
  local tone = build_tone(sd, pr)
  local m = vol * (g == 1 and 1 or 0.8)
  meter.l = max(meter.l, m * (1 - max(0, pr.pan) * 0.6))
  meter.r = max(meter.r, m * (1 + min(0, pr.pan) * 0.6))
  if when then
    return play_at(when, tone, note, sd.one and sd.ms or max(40, ms or 250), vol, 1)
  end
  return play(nil, tone, note, sd.one and sd.ms or 0, vol)
end

local function click(when, accent)
  play_at(when, { preset = "blip" }, accent and 84 or 76, 40, 0.3, 2)
end

-- ---------------------------------------------------------------- sequencer
local LOOK = 0.14
local pl = { on = false, tick = 0, t = 0, iter = 0, q = {}, ht = 0, hk = 0 }

local function tickdur() return 60 / S.bpm / TPB end

local function process_tick(tick, when)
  if tick < 0 then
    local k = tick + TPBAR
    if k % TPB == 0 then click(when, k == 0) end
    return
  end
  if S.metro and tick % TPB == 0 then click(when, tick % TPBAR == 0) end
  local td = tickdur()
  local sw = (S.swing > 0 and tick % 12 == 6) and S.swing * 3 * td or 0
  for _, e in ipairs(PT().ev) do
    if e.t == tick then
      local skip = false
      if e.ls then
        if e.ls == pl.iter then skip = true; e.ls = nil
        elseif e.ls < pl.iter then e.ls = nil end
      end
      if not skip then
        hit(e.g, e.p, e.n, 1, when + sw, e.l * td * 1000)
        fq[#fq + 1] = { when + sw, e.g, e.p }
      end
    end
  end
end

local function sched_run(now)
  if not pl.on then return end
  local td = tickdur()
  while pl.t < now + LOOK do
    process_tick(pl.tick, pl.t)
    pl.q[#pl.q + 1] = { pl.t, pl.tick }
    pl.t = pl.t + td
    pl.tick = pl.tick + 1
    if pl.tick >= plen() then pl.tick = 0; pl.iter = pl.iter + 1 end
  end
  while pl.q[1] and pl.q[1][1] <= now do
    pl.ht, pl.hk = pl.q[1][1], pl.q[1][2]
    remove(pl.q, 1)
  end
end

local function play_start()
  pl.on = true
  pl.iter = 0
  pl.q = {}
  pl.t = audio_time() + 0.05
  pl.tick = recarm and -TPBAR or 0
  pl.ht, pl.hk = pl.t, pl.tick
end

local save_now
local function play_stop()
  pl.on = false
  pl.q = {}
  pl.hk = 0
  recarm = false
  play_cancel()
  fq = {}
  if dirty then save_now() end
end

local function pos_now(now)
  if not pl.on or pl.hk < 0 then return nil end
  return pl.hk + clamp((now - pl.ht) / tickdur(), 0, 1.5)
end

-- the position as the LCD shows it: bar, beat, step (1-based)
local function pos_text()
  if not pl.on then return "01.1" end
  local k = pl.hk
  if k < 0 then return fmt("CNT %d", 4 - (k + TPBAR) // TPB) end
  return fmt("%02d.%d", k // TPBAR + 1, k % TPBAR // TPB + 1)
end

-- a pad hit goes into the pattern, rounded to the grid
local function rec_event(g, p, sem, now)
  local pos = pos_now(now)
  if not pos then return nil end
  local len = plen()
  local q = QN[S.quant]
  local t = q > 0 and round(pos / q) * q or round(pos)
  local wrapped = false
  if t >= len then t = t - len; wrapped = true end
  local ev = PT().ev
  for i = #ev, 1, -1 do
    local e = ev[i]
    if e.t == t and e.g == g and e.p == p and e.n == sem then remove(ev, i) end
  end
  local e = { t = t, g = g, p = p, n = sem, l = max(3, q > 0 and q or 3) }
  if t >= pl.tick then e.ls = pl.iter
  elseif wrapped and pl.tick > len // 2 then e.ls = pl.iter + 1 end
  insert(ev, e)
  dirty = true
  return e
end

local function erase_pad(g, p)
  local ev = PT().ev
  for i = #ev, 1, -1 do if ev[i].g == g and ev[i].p == p then remove(ev, i); dirty = true end end
end
local function erase_group(g)
  local ev = PT().ev
  for i = #ev, 1, -1 do if ev[i].g == g then remove(ev, i); dirty = true end end
end

local function copy_pattern(from, to)
  local a = S.patterns[from]
  local b = { bars = a and a.bars or 1, ev = {} }
  if a then for _, e in ipairs(a.ev) do b.ev[#b.ev + 1] = { t = e.t, g = e.g, p = e.p, n = e.n, l = e.l } end end
  S.patterns[to] = b
  dirty = true
end

-- ---------------------------------------------------------------- save / load
local function snapshot()
  local pads, pats = {}, {}
  for g = 1, 4 do
    for p = 1, 12 do
      local r = S.pads[g][p]
      pads[#pads + 1] = { r.snd, r.lvl, r.pit, r.flt, r.dec or -1, r.pan, r.hpf, r.atk or -1, r.rel or -1, r.send, r.tune, r.vel, r.mod }
    end
  end
  for n, pt in pairs(S.patterns) do
    if #pt.ev > 0 or pt.bars > 1 then
      local f = {}
      for _, e in ipairs(pt.ev) do
        f[#f + 1] = e.t; f[#f + 1] = e.g; f[#f + 1] = e.p; f[#f + 1] = e.n or 255; f[#f + 1] = e.l
      end
      pats[#pats + 1] = { n = n, bars = pt.bars, f = f }
    end
  end
  return { v = 1, bpm = S.bpm, quant = S.quant, swing = S.swing, metro = S.metro, master = S.master,
           glvl = S.glvl, fx = S.fx, fxamt = S.fxamt, pat = S.pat, group = S.group, pads = pads, pats = pats }
end

function save_now()
  save(snapshot())
  dirty = false
end

local function restore(d)
  if type(d) ~= "table" or d.v ~= 1 then return false end
  S.bpm, S.quant, S.swing, S.metro = d.bpm or 120, d.quant or 3, d.swing or 0, d.metro and true or false
  S.master, S.fx, S.fxamt = d.master or 0.8, d.fx or 1, d.fxamt or 0.5
  S.pat, S.group = d.pat or 1, d.group or 1
  for i = 1, 4 do S.glvl[i] = d.glvl and d.glvl[i] or 1 end
  local i = 0
  for g = 1, 4 do
    for p = 1, 12 do
      i = i + 1
      local r, s = S.pads[g][p], d.pads and d.pads[i]
      if s then
        r.snd = clamp(s[1], 1, #ALL); r.lvl, r.pit, r.flt = s[2], s[3], s[4]
        r.dec = s[5] >= 0 and s[5] or nil
        r.pan = s[6]
        if s[7] then
          r.hpf, r.atk, r.rel = s[7], s[8] >= 0 and s[8] or nil, s[9] >= 0 and s[9] or nil
          r.send, r.tune, r.vel, r.mod = s[10], s[11], s[12], s[13]
        end
      end
    end
  end
  S.patterns = {}
  for _, rec in ipairs(d.pats or {}) do
    local pt = { bars = rec.bars, ev = {} }
    for j = 1, #rec.f, 5 do
      local n = rec.f[j + 3]
      pt.ev[#pt.ev + 1] = { t = rec.f[j], g = rec.f[j + 1], p = rec.f[j + 2], n = n ~= 255 and n or nil, l = rec.f[j + 4] }
    end
    S.patterns[rec.n] = pt
  end
  return true
end

-- a first beat to play with, when nothing is saved
local function demo()
  local ev = {}
  local function add(t, g, p, n) ev[#ev + 1] = { t = t, g = g, p = p, n = n, l = 6 } end
  for beat = 0, 3 do
    add(beat * 24, 1, 7)                       -- kick
    add(beat * 24 + 12, 1, 9)                  -- hat
    if beat % 2 == 1 then add(beat * 24, 1, 8) end   -- snare
  end
  add(66, 1, 7); add(90, 1, 9)
  S.patterns[1] = { bars = 1, ev = ev }
  local b = {}
  local line = { 0, 0, 3, 0, 5, 3, 0, 7 }
  for i, n in ipairs(line) do b[#b + 1] = { t = (i - 1) * 12, g = 2, p = 1, n = n, l = 10 } end
  S.patterns[2] = { bars = 1, ev = b }
  copy_pattern(1, 3)
  for _, e in ipairs(b) do insert(S.patterns[3].ev, { t = e.t, g = e.g, p = e.p, n = e.n, l = e.l }) end
end

-- ---------------------------------------------------------------- parameters
-- the fader edits the chosen parameter of the mode. In SOUND the twelve parameters sit on
-- the pads (Shift + pad, as the labels above them say); 1-5 choose the first five anywhere.
local function P(name, lo, hi, kind, get, set, show, step)
  return { name = name, lo = lo, hi = hi, kind = kind, get = get, set = set, show = show, step = step or 1 }
end
local function pct(v) return fmt("%d", round(v * 100)) end
local PLBL = { "LEVEL", "PITCH", "TIME", "LPF", "HPF", ">FX", "ATK", "REL", "PAN", "TUNE", "VEL", "MOD" }

local function params()
  if mode == "sound" or mode == "keys" then
    local pr = S.pads[S.sel[1]][S.sel[2]]
    local function F(i, key, lo, hi, kind, show, dflt)
      local p = P(PLBL[i], lo, hi, kind, function() local v = pr[key]; if v == nil then v = dflt end; return v end,
        function(v) pr[key] = v end, show)
      return p
    end
    local function hz(v, k) return fmt("%d", k * 2 ^ (v * (k == 80 and 7.8 or 7.5))) end
    return {
      F(1, "lvl", 0, 1, "f", pct),
      F(2, "pit", -12, 12, "i", function(v) return fmt("%+d", v) end),
      F(3, "dec", 0, 1, "f", function(v) return pr.dec and pct(v) or "DEF" end, 0.5),
      F(4, "flt", 0, 1, "f", function(v) return v >= 0.995 and "OFF" or hz(v, 80) end),
      F(5, "hpf", 0, 1, "f", function(v) return v < 0.01 and "OFF" or hz(v, 30) end),
      F(6, "send", 0, 1, "f", pct),
      F(7, "atk", 0, 1, "f", function(v) return pr.atk and pct(v) or "DEF" end, 0),
      F(8, "rel", 0, 1, "f", function(v) return pr.rel and pct(v) or "DEF" end, 0.5),
      F(9, "pan", -1, 1, "f", function(v)
        return abs(v) < 0.03 and "C" or (v < 0 and fmt("L%d", round(-v * 100)) or fmt("R%d", round(v * 100))) end),
      F(10, "tune", -50, 50, "i", function(v) return fmt("%+d", v) end),
      F(11, "vel", 0, 1, "f", pct),
      F(12, "mod", 0, 1, "f", pct),
    }
  elseif mode == "timing" then
    return {
      P("BPM", 60, 200, "i", function() return S.bpm end, function(v) S.bpm = v; fx_globals() end, function(v) return fmt("%d", v) end),
      P("QNT", 0, 4, "i", function() return S.quant end, function(v) S.quant = v end, function(v) return QNAME[v] end),
      P("SWG", 0, 1, "f", function() return S.swing end, function(v) S.swing = v end, pct),
      P("BAR", 1, 4, "i", function() return PT().bars end, function(v) PT().bars = v; dirty = true end, function(v) return fmt("%d", v) end),
      P("MET", 0, 1, "i", function() return S.metro and 1 or 0 end, function(v) S.metro = v == 1 end, function(v) return v == 1 and "ON" or "OFF" end),
    }
  elseif mode == "fx" then
    return { P("AMT", 0, 1, "f", function() return S.fxamt end, function(v) S.fxamt = v end, pct) }
  end
  local list = {}
  for g = 1, 4 do
    list[g] = P("LV" .. GRPNAME[g], 0, 1, "f", function() return S.glvl[g] end, function(v) S.glvl[g] = v end, pct)
  end
  list[5] = P("MST", 0, 1, "f", function() return S.master end, function(v) S.master = v end, pct)
  return list
end

local function cur_param()
  local list = params()
  return list[psel[mode]] or list[1], list
end

local fine = false
local fh, fdir = 0, 0
local function fader_update(dir)
  if dir ~= fdir then fh = 0; fdir = dir end
  if dir == 0 then return end
  fh = fh + 1
  local p = cur_param()
  if not p then return end
  if p.kind == "i" then
    if fh == 1 or (fh > 18 and (fh - 18) % (fh > 70 and 2 or 4) == 0) then
      p.set(clamp(p.get() + dir * p.step, p.lo, p.hi))
      dirty = true
    end
  else
    local rate = (p.hi - p.lo) / 150
    if shift() or fine then rate = rate / 8 end
    p.set(clamp(p.get() + dir * rate, p.lo, p.hi))
    dirty = true
  end
end

local vh, vdir = 0, 0
local function volume_update(dir)
  if dir ~= vdir then vh = 0; vdir = dir end
  if dir == 0 then return end
  vh = vh + 1
  local rate = 1 / 120
  if shift() or fine then rate = rate / 8 end
  S.master = clamp(S.master + dir * rate, 0, 1)
  dirty = true
end

-- true on the press and, after a short wait, on every few frames
local function rep(u)
  if not held[u] then hold[u] = 0; return false end
  hold[u] = (hold[u] or 0) + 1
  local h = hold[u]
  return h == 1 or (h > 18 and (h - 18) % 3 == 0)
end

-- ---------------------------------------------------------------- actions
local function set_mode(m, p)
  if mode == m and not p and m ~= "main" then m = "main" end
  mode = m
  if m == "main" then psel.main = S.group end
  if p then psel[m] = p end
  if dirty and not pl.on then save_now() end
end

local function select_sound(step)
  local pr = S.pads[S.sel[1]][S.sel[2]]
  pr.snd = (pr.snd - 1 + step) % #ALL + 1
  dirty = true
end

local function tap_tempo(now)
  if #taps > 0 and now - taps[#taps] > 2 then taps = {} end
  taps[#taps + 1] = now
  while #taps > 5 do remove(taps, 1) end
  if #taps >= 2 then
    local avg = (taps[#taps] - taps[1]) / (#taps - 1)
    S.bpm = clamp(round(60 / avg), 60, 200)
    fx_globals()
    dirty = true
  end
end

local function pad_semitone(p)
  local row, col = (p - 1) // 3, (p - 1) % 3
  return (3 - row) * 3 + col + S.oct * 12
end

local function set_pattern(n)
  S.pat = clamp(n, 1, 99)
  if pl.on and pl.tick >= plen() then pl.tick = 0 end
end

local function pad_down(p, now)
  local g = S.group
  if mode == "pattern" then
    local n = PADNUM[p] + (S.page or 0) * 12
    if n > 99 then return end
    if shift() then copy_pattern(S.pat, n) end
    set_pattern(n)
    flash[p] = 8
    return
  elseif mode == "fx" then
    S.fx = PADNUM[p]
    fx_globals()
    dirty = true
    flash[p] = 8
    return
  elseif mode == "sound" and shift() then
    psel.sound = p
    flash[p] = 8
    return
  elseif held[K.X] then
    erase_pad(g, p)
    flash[p] = 8
    return
  end
  local sg, sp, sem = g, p, nil
  if mode == "keys" then
    sg, sp = S.sel[1], S.sel[2]
    sem = pad_semitone(p)
  else
    S.sel = { g, p }
  end
  local v = hit(sg, sp, sem, 1)
  local h = { voice = v, gate = not ALL[S.pads[sg][sp].snd].one, t0 = now }
  if recarm and pl.on then h.ev = rec_event(sg, sp, sem, now) end
  padhold[p] = h
  flash[p] = 6
end

local function pad_up(p, now)
  local h = padhold[p]
  padhold[p] = nil
  if not h then return end
  if h.gate and h.voice then noteoff(h.voice) end
  if h.ev then
    local len = round((now - h.t0) / tickdur())
    h.ev.l = clamp(len, 3, plen())
  end
end

local function handle_keys(now)
  local sh = shift()
  -- the fader: W and S, up and down arrows; the volume knob: A and D, left and right
  local fd = ((held[K.W] or held[K.UP]) and 1 or 0) - ((held[K.S] or held[K.DOWN]) and 1 or 0)
  local vd = ((held[K.D] or held[K.RIGHT]) and 1 or 0) - ((held[K.A] or held[K.LEFT]) and 1 or 0)
  fader_update(fd)
  volume_update(vd)

  if pressed(K.F1) or pressed(K.SLASH) then showhelp = not showhelp end

  -- modes
  if pressed(K.Q) then set_mode("sound") end
  if pressed(K.B) then set_mode("main") end
  if pressed(K.T) then
    if sh then tap_tempo(now) else set_mode("timing", 1) end
  end
  if pressed(K.C) then set_mode("timing", 2) end
  if pressed(K.E) then set_mode("pattern") end
  if pressed(K.F) then set_mode("fx") end
  if pressed(K.G) then set_mode("keys") end
  if pressed(K.V) then fine = not fine end

  -- transport
  if pressed(K.SPACE) then
    if pl.on then play_stop() else play_start() end
  end
  if pressed(K.R) then recarm = not recarm end
  if pressed(K.Z) then S.metro = not S.metro; dirty = true end
  if pressed(K.X) and sh then
    PT().ev = {}
    dirty = true
  end

  -- the tempo knob
  if rep(K.MINUS) then S.bpm = clamp(S.bpm - 1, 60, 200); fx_globals(); dirty = true end
  if rep(K.EQUAL) then S.bpm = clamp(S.bpm + 1, 60, 200); fx_globals(); dirty = true end

  -- parameters
  for i = 1, 5 do
    if pressed(PARAMKEY[i]) then
      local _, list = cur_param()
      if list[i] then psel[mode] = i end
    end
  end
  -- the minus and plus buttons
  local d = (rep(K.RB) and 1 or 0) - (rep(K.LB) and 1 or 0)
  if d ~= 0 then
    if mode == "sound" then select_sound(d)
    elseif mode == "keys" then S.oct = clamp(S.oct + d, -3, 3)
    elseif mode == "pattern" then S.page = clamp((S.page or 0) + d, 0, 8)
    else set_pattern(S.pat + d); dirty = true end
  end

  -- groups
  for g = 1, 4 do
    if pressed(GRPKEY[g]) then
      if held[K.X] then
        erase_group(g)
      else
        S.group = g
        if mode == "main" then psel.main = g end
        if mode == "sound" then S.sel = { g, S.sel[2] } end
      end
      gflash[g] = 8
    end
  end

  -- pads
  for p = 1, 12 do
    local u = PADKEY[p]
    if pressed(u) then pad_down(p, now) end
    if released(u) then pad_up(p, now) end
  end
end

-- ---------------------------------------------------------------- drawing
local C = {
  body = 0xD2D3D0, bodyhi = 0xE6E7E4, bodylo = 0xA4A6A4, black = 0x1A1B1E, pad = 0x26272B, padtop = 0x34353A,
  padlo = 0x0C0C0E, orange = 0xFF5A1F, red = 0xFF2A1A, key = 0xEAEAE6, keylo = 0xB6B7B3, txt = 0x2A2C2F,
  dim = 0x6E7270, lcd = 0x0C0D0F, lcdfg = 0xF2F5F2, lcdghost = 0x1C1E21, lcddim = 0x4A5250,
  lcdhi = 0xFF7A1E, green = 0x30D060, led = 0xFF3A20, ledoff = 0x7A7C7A,
}

local function rrect(x, y, w, h, r, c)
  rectfill(x + r, y, w - 2 * r, h, c)
  rectfill(x, y + r, w, h - 2 * r, c)
  circfill(x + r, y + r, r, c); circfill(x + w - r - 1, y + r, r, c)
  circfill(x + r, y + h - r - 1, r, c); circfill(x + w - r - 1, y + h - r - 1, r, c)
end

local function small(txt, x, y, c)
  font("6x12")
  print(txt, x, y, c)
  font("8x16")
end
local function small_c(txt, cx, y, c) small(txt, cx - #txt * 3, y, c) end
local function small_r(txt, rx, y, c) small(txt, rx - #txt * 6, y, c) end
local function big_c(txt, cx, y, c, scale) print(txt, cx - #txt * 4 * scale, y, c, scale) end

local function dot(x, y, on, col)
  circfill(x, y, 2, on and (col or C.led) or C.ledoff)
end

-- seven segments: the LCD's digits
local SEG = { ["0"] = "abcdef", ["1"] = "bc", ["2"] = "abdeg", ["3"] = "abcdg", ["4"] = "bcfg", ["5"] = "acdfg",
              ["6"] = "acdefg", ["7"] = "abc", ["8"] = "abcdefg", ["9"] = "abcdfg", ["-"] = "g" }
local function digit(x, y, w, h, t, ch, on, off)
  local mid = y + (h - t) // 2
  local R = {
    a = { x + t, y, w - 2 * t, t }, d = { x + t, y + h - t, w - 2 * t, t }, g = { x + t, mid, w - 2 * t, t },
    f = { x, y + t, t, mid - y }, b = { x + w - t, y + t, t, mid - y },
    e = { x, mid, t, y + h - t - mid }, c = { x + w - t, mid, t, y + h - t - mid },
  }
  local lit = SEG[ch] or ""
  for k, r in pairs(R) do
    rectfill(r[1], r[2], r[3], r[4], lit:find(k, 1, true) and on or off)
  end
end
local function seg_width(str, w)
  local n = 0
  for ch in str:gmatch(".") do n = n + (ch == "." and 12 or w + 6) end
  return n
end
local function seg_text(str, cx, y, w, h, t, on, off)
  local x = cx - seg_width(str, w) // 2
  for ch in str:gmatch(".") do
    if ch == "." then
      rectfill(x + 2, y + h - t, t, t, on)
      x = x + 12
    else
      digit(x, y, w, h, t, ch, on, off)
      x = x + w + 6
    end
  end
end

local function knob(cx, cy, r, v, cap, ring)
  for i = 0, 10 do
    local a = (-135 + 270 * i / 10) * pi / 180
    circfill(cx + floor(sin(a) * (r + 6)), cy - floor(cos(a) * (r + 6)), 1, C.dim)
  end
  circfill(cx + 1, cy + 3, r + 1, C.bodylo)
  circfill(cx, cy, r + 1, ring or 0x8C8E8C)
  circfill(cx, cy, r - 1, cap)
  circfill(cx, cy, r - 6, cap == 0xFFFFFF and 0xF4F4F4 or cap + 0x0A0A0A)
  local a = (-135 + 270 * v) * pi / 180
  local x1, y1 = cx + sin(a) * (r * 0.3), cy - cos(a) * (r * 0.3)
  local x2, y2 = cx + sin(a) * (r - 4), cy - cos(a) * (r - 4)
  local ic = (cap == 0xFFFFFF) and 0x303030 or 0xFFFFFF
  line(floor(x1), floor(y1), floor(x2), floor(y2), ic)
  line(floor(x1) + 1, floor(y1), floor(x2) + 1, floor(y2), ic)
end

local function fader(cx, y1, y2, v)
  rectfill(cx - 2, y1, 4, y2 - y1, C.black)
  for i = 0, 8 do
    local y = y2 - floor((y2 - y1) * i / 8)
    line(cx - 12, y, cx - 7, y, C.dim)
  end
  local ky = y2 - floor((y2 - y1) * v) - 8
  rrect(cx - 11, ky, 22, 16, 8, 0x58595B)
  circfill(cx, ky + 8, 5, 0x8A8B8D)
end

-- a two-tone button: a dark label above, a light one below
local function button2(x, y, w, h, top, sub, lit, topcol, subcol, ledcol)
  rrect(x, y + 2, w, h, 4, C.bodylo)
  local th = h * 3 // 5
  rrect(x, y, w, th + 3, 4, topcol or C.black)
  rrect(x, y + th - 2, w, h - th + 2, 4, subcol or C.key)
  rectfill(x + 1, y + th - 2, w - 2, 5, topcol or C.black)
  local tc = (topcol == C.red or topcol == C.orange or (topcol or C.black) == C.black) and 0xF4F4F0 or C.txt
  print(top, x + 5, y + (th - 16) // 2 + 1, tc)
  small(sub, x + 6, y + th + 1, (subcol == C.orange) and 0xFFFFFF or C.dim)
  if lit then dot(x + w - 7, y + 7, true, ledcol or C.orange) end
end

local function draw_lcd(now)
  local x0, y0, W, H = 8, 6, 624, 84
  rrect(x0, y0, W, H, 6, C.lcd)

  -- the modes
  local tags = { "SOUND", "MAIN", "PATTERN", "TEMPO", "TIMING", "FX", "KEYS" }
  local tx, ty = x0 + 12, y0 + 6
  for i, t in ipairs(tags) do
    local act
    if t == "TEMPO" then act = mode == "timing" and psel.timing == 1
    elseif t == "TIMING" then act = mode == "timing" and psel.timing ~= 1
    else act = mode == t:lower() end
    local w = #t * 6 + 8
    if tx + w > x0 + 180 then tx = x0 + 12; ty = ty + 17 end
    if act then rectfill(tx, ty, w, 14, C.lcdfg); small(t, tx + 4, ty + 1, C.lcd)
    else rect(tx, ty, w, 14, C.lcddim); small(t, tx + 4, ty + 1, C.lcddim) end
    tx = tx + w + 5
  end
  local sg = S.sel
  small(fmt("PTN %02d   GROUP %s", S.pat, GRPNAME[S.group]), x0 + 12, y0 + 44, C.lcdfg)
  small(fmt("%s-%s  %s", GRPNAME[sg[1]], PADLAB[sg[2]], ALL[S.pads[sg[1]][sg[2]].snd].name), x0 + 12, y0 + 58, C.lcdhi)
  small(fmt("%d BPM  QNT %s", S.bpm, QNAME[S.quant]), x0 + 12, y0 + 70, C.lcddim)

  -- the big value: the position, or the chosen parameter
  local p = cur_param()
  local txt, cap
  if mode == "main" then txt = pos_text(); cap = "POSITION"
  elseif mode == "pattern" then txt = fmt("%02d", S.pat); cap = "PATTERN"
  elseif mode == "keys" then
    local pr = S.pads[sg[1]][sg[2]]
    txt = note_text(clamp((ALL[pr.snd].note or 60) + pr.pit + S.oct * 12, 0, 127)); cap = fmt("KEYS  OCTAVE %+d", S.oct)
  elseif mode == "fx" then txt = pct(S.fxamt); cap = FXS[S.fx].name
  else txt = p.show(p.get()); cap = p.name end
  local cx = x0 + 330
  if txt:match("^[%d%.%-%+ ]+$") then
    txt = txt:gsub("%+", "")
    seg_text((txt:gsub("%d", "8")), cx, y0 + 10, 26, 44, 5, C.lcdghost, C.lcdghost)
    seg_text(txt, cx, y0 + 10, 26, 44, 5, C.lcdfg, C.lcdghost)
  else
    big_c(txt, cx, y0 + 16, C.lcdfg, 3)
  end
  small_c(cap, cx, y0 + 60, C.lcddim)

  -- the icons
  local ix = x0 + 470
  rect(ix, y0 + 8, 30, 14, pl.on and C.red or C.lcddim)
  small("BAR", ix + 6, y0 + 9, pl.on and C.lcdfg or C.lcddim)
  local blink = not pl.on or (frame_n // 15) % 2 == 0
  circfill(ix + 46, y0 + 15, 5, recarm and blink and C.red or C.lcddim)
  if pl.on then tri(ix + 60, y0 + 9, ix + 60, y0 + 21, ix + 71, y0 + 15, C.lcdfg)
  else tri(ix + 60, y0 + 9, ix + 60, y0 + 21, ix + 71, y0 + 15, C.lcddim) end
  small("MET", ix + 82, y0 + 9, S.metro and C.lcdfg or C.lcddim)
  small("FX", ix, y0 + 32, S.fx > 1 and C.lcdhi or C.lcddim)
  small(S.fx > 1 and FXS[S.fx].name or "-", ix + 20, y0 + 32, S.fx > 1 and C.lcdfg or C.lcddim)
  small("STEREO", ix + 82, y0 + 32, C.lcddim)
  for i, v in ipairs({ meter.l, meter.r }) do
    local h = floor(clamp(v, 0, 1) * 44)
    local mx = x0 + W - 28 + (i - 1) * 10
    rect(mx, y0 + 28, 7, 46, C.lcddim)
    rectfill(mx + 1, y0 + 73 - h, 5, h, h > 38 and C.led or C.lcdhi)
  end

  -- the 16 steps of the bar
  local bar = (pl.on and pl.hk >= 0) and pl.hk // TPBAR or 0
  local head = (pl.on and pl.hk >= 0) and (pl.hk % TPBAR) // 6 or -1
  local lit, others = {}, {}
  for _, e in ipairs(PT().ev) do
    if e.t // TPBAR == bar then
      local s = (e.t % TPBAR) // 6 + 1
      if e.g == S.group then lit[s] = true else others[s] = true end
    end
  end
  for s = 1, 16 do
    local sx = x0 + 218 + (s - 1) * 16
    local col = lit[s] and C.lcdhi or (others[s] and C.lcddim or C.lcdghost)
    if s - 1 == head then col = C.lcdfg end
    rectfill(sx, y0 + 74, 12, 5, col)
  end
end

local BTNS = {
  { "SOUND", "Q", 372, 94, 80, 34, function() return mode == "sound" end },
  { "MAIN", "B", 458, 94, 80, 34, function() return mode == "main" end, C.orange },
  { "TEMPO", "T", 544, 94, 80, 34, function() return mode == "timing" and psel.timing == 1 end },
  { "PATTERN", "E", 372, 200, 122, 34, function() return mode == "pattern" end },
  { "TIMING", "C", 502, 200, 122, 34, function() return mode == "timing" and psel.timing ~= 1 end },
  { "FX", "F", 372, 238, 122, 34, function() return mode == "fx" end },
  { "ERASE", "X (Shift: clear)", 502, 238, 122, 34, function() return held[K.X] end },
  { "-", "[", 372, 276, 122, 34, function() return held[K.LB] end, C.key, C.dim },
  { "+", "]", 502, 276, 122, 34, function() return held[K.RB] end, C.key, C.dim },
  { "RECORD", "R", 372, 314, 122, 34, function() return recarm end, C.red },
  { "PLAY", "Space", 502, 314, 122, 34, function() return pl.on end, 0x58595B },
  { "KEYS", "G", 10, 158, 62, 34, function() return mode == "keys" end },
  { "FADER", "V (fine)", 10, 196, 62, 34, function() return fine end },
}

local function draw_buttons()
  for _, b in ipairs(BTNS) do
    local lit = b[7]()
    local topcol = b[8]
    if b[1] == "-" or b[1] == "+" then
      rrect(b[3], b[4] + 2, b[5], b[6], 4, C.bodylo)
      rrect(b[3], b[4], b[5], b[6], 4, lit and 0xFFFFFF or C.key)
      big_c(b[1], b[3] + b[5] // 2, b[4] + 8, C.txt, 2)
      small(b[2], b[3] + 6, b[4] + 22, C.dim)
    else
      button2(b[3], b[4], b[5], b[6], b[1], b[2], lit, topcol, (b[1] == "MAIN") and C.orange or nil)
    end
  end
  -- shift
  rrect(10, 316, 62, 28, 4, C.bodylo)
  rrect(10, 314, 62, 28, 4, shift() and 0xFFFFFF or C.key)
  small_c("SHIFT", 41, 322, C.dim)
  -- the knobs: tempo, metronome
  small_c("BPM", 415, 133, C.txt)
  knob(415, 164, 19, (S.bpm - 60) / 140, C.orange, C.red)
  small_c("- =", 415, 190, C.dim)
  small_c("METRONOME", 583, 133, C.txt)
  knob(583, 164, 19, S.metro and 0.8 or 0.1, 0x202124, 0x6A6C6E)
  small_c("Z", 583, 190, C.dim)
end

local function draw_pads(now)
  local rows = {}
  for g = 1, 4 do
    local y = 94 + (g - 1) * 62 + 13
    local on = S.group == g
    rrect(96, y + 3, 44, 46, 5, C.bodylo)
    rrect(96, y, 44, 46, 5, on and 0xFFFFFF or C.key)
    big_c(GRPNAME[g], 118, y + 6, C.txt, 2)
    small_c(GRPHINT[g], 118, y + 32, C.dim)
    dot(133, y + 6, on or (gflash[g] or 0) > 0)
  end
  for p = 1, 12 do
    local r, c = (p - 1) // 3, (p - 1) % 3
    local x, ly = 150 + c * 70, 94 + r * 62
    local y = ly + 13
    local pr = S.pads[S.group][p]
    local sd = ALL[pr.snd]
    local lit = (flash[p] or 0) > 0 or padhold[p]
    local sel = false
    if mode == "sound" or mode == "keys" then sel = S.sel[1] == S.group and S.sel[2] == p
    elseif mode == "pattern" then sel = (PADNUM[p] + (S.page or 0) * 12) == S.pat
    elseif mode == "fx" then sel = PADNUM[p] == S.fx end
    -- the parameter label and its light
    small_c(PLBL[p], x + 36, ly, C.dim)
    dot(x + 36 - #PLBL[p] * 3 - 7, ly + 6, mode == "sound" and psel.sound == p, C.led)
    rrect(x, y + 3, 62, 46, 6, C.padlo)
    rrect(x, y, 62, 46, 6, lit and C.orange or C.pad)
    rrect(x + 2, y + 2, 58, 40, 5, lit and 0xFF7A40 or C.padtop)
    if sel then rect(x - 1, y - 1, 64, 48, C.orange) end
    print(PADLAB[p], x + 8, y + 3, 0xF6F6F2, 2)
    small_r(PADHINT[p], x + 57, y + 3, lit and 0xFFE0C8 or 0x8A8D90)
    local label = sd.name
    if mode == "pattern" then label = "PTN " .. (PADNUM[p] + (S.page or 0) * 12)
    elseif mode == "fx" then label = FXS[PADNUM[p]] and FXS[PADNUM[p]].name or "-" end
    small(label, x + 6, y + 31, lit and 0xFFFFFF or 0x9A9DA0)
  end
end

local function draw_help()
  rectfill(0, 0, 640, 360, 0x101418)
  print("KO-II  KEYS", 20, 8, C.lcdhi, 2)
  local L = {
    { "PADS", "7 8 9 / U I O / J K L / M , .   = pads 7 8 9 / 4 5 6 / 1 2 3 / . 0 ENT" },
    { "GROUPS", "6=A  Y=B  H=C  N=D   (A drums, B bass, C keys, D leads and fx)" },
    { "FADER", "W S or Up Down edits the chosen parameter (V or Shift: fine)" },
    { "VOLUME", "A D or Left Right: master volume" },
    { "TEMPO", "- and = move the BPM; T tempo page, Shift+T taps it; C timing page" },
    { "PARAMS", "1-5 choose a parameter. In SOUND: Shift + pad (LEVEL PITCH TIME LPF...)" },
    { "PLAY / REC", "Space plays and stops. R arms recording (one bar of count-in)" },
    { "SOUND  Q", "[ ] change the sound of the last pad" },
    { "MAIN  B", "[ ] change the pattern; the fader sets the group levels" },
    { "PATTERN  E", "pads pick pattern 1-12, [ ] page, Shift+pad copies it" },
    { "FX  F", "pads pick one of 12 effects, the fader sets the amount" },
    { "KEYS  G", "the pads play a chromatic scale, [ ] octave" },
    { "ERASE  X", "X + pad erases it from the pattern, X + group the group, Shift+X all" },
    { "METRO  Z", "metronome on and off" },
  }
  for i, l in ipairs(L) do
    small(l[1], 20, 40 + (i - 1) * 20, C.lcdhi)
    small(l[2], 120, 40 + (i - 1) * 20, C.lcdfg)
  end
  small("F1 or / closes this page", 20, 332, C.lcddim)
end

function _draw()
  cls(0x8A8E8D)
  if showhelp then draw_help() return end
  local now = audio_time()
  rrect(2, 2, 636, 356, 12, C.bodylo)
  rrect(2, 2, 636, 354, 12, C.body)
  draw_lcd(now)

  small_c("VOLUME", 41, 94, C.txt)
  knob(41, 124, 19, S.master, 0xFFFFFF, 0x8C8E8C)
  small_c("A D", 41, 146, C.dim)

  local p = cur_param()
  fader(41, 246, 304, p and (p.get() - p.lo) / (p.hi - p.lo) or 0)
  small_c("W S", 41, 232, C.dim)

  draw_buttons()
  draw_pads(now)
end

-- ---------------------------------------------------------------- main
function _init()
  rawkeys(true)
  local d = saved()
  if not (d and restore(d)) then demo() end
  fx_globals()
  psel.main = S.group
end

function _update()
  frame_n = frame_n + 1
  poll()
  local now = audio_time()
  handle_keys(now)
  sched_run(now)
  while fq[1] and fq[1][1] <= now do
    local f = remove(fq, 1)
    if f[2] == S.group then flash[f[3]] = 5 end
  end
  for p in pairs(flash) do flash[p] = flash[p] - 1; if flash[p] <= 0 then flash[p] = nil end end
  for g in pairs(gflash) do gflash[g] = gflash[g] - 1; if gflash[g] <= 0 then gflash[g] = nil end end
  meter.l = meter.l * 0.93
  meter.r = meter.r * 0.93
  if dirty and not pl.on then
    idle_save = idle_save + 1
    if idle_save > 300 then save_now(); idle_save = 0 end
  else
    idle_save = 0
  end
end

function _exit()
  if pl.on then play_stop() end
  if dirty then save_now() end
end
