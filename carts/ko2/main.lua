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
  SPACE = 0x2C, COMMA = 0x36, PERIOD = 0x37, SLASH = 0x38, LB = 0x2F, RB = 0x30, F1 = 0x3A,
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
  for p = 1, 12 do S.pads[g][p] = { snd = (g - 1) * 12 + p, lvl = 1, pit = 0, flt = 1, dec = nil, pan = 0 } end
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
  if pr.flt < 0.995 then t.filter = "lp"; t.cutoff = floor(80 * 2 ^ (pr.flt * 7.8)) end
  if pr.dec then t.decay = 30 + pr.dec * pr.dec * 1800; t.release = 20 + pr.dec * pr.dec * 1500 end
  local fx = FXS[S.fx]
  if fx.apply and S.fxamt > 0.01 then fx.apply(t, S.fxamt) end
  return t
end

-- g, p: the sound's pad; sem: semitones above its root (keys mode); when: audio time (sequencer)
local function hit(g, p, sem, vel, when, ms)
  local pr = S.pads[g][p]
  local sd = ALL[pr.snd]
  local note = clamp(floor((sd.note or 60) + pr.pit + (sem or 0)), 1, 127)
  local vol = clamp(S.master * S.glvl[g] * pr.lvl * (vel or 1), 0, 1)
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
  if not pl.on then return "01.1.1" end
  local k = pl.hk
  if k < 0 then return fmt("CNT %d", 4 - (k + TPBAR) // TPB) end
  return fmt("%02d.%d.%d", k // TPBAR + 1, k % TPBAR // TPB + 1, k % TPB // 6 + 1)
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
      pads[#pads + 1] = { r.snd, r.lvl, r.pit, r.flt, r.dec or -1, r.pan }
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
-- the fader edits the chosen parameter of the mode; 1-5 choose it
local function P(name, lo, hi, kind, get, set, show, step)
  return { name = name, lo = lo, hi = hi, kind = kind, get = get, set = set, show = show, step = step or 1 }
end
local function pct(v) return fmt("%d", round(v * 100)) end

local function params()
  if mode == "sound" or mode == "keys" then
    local pr = S.pads[S.sel[1]][S.sel[2]]
    return {
      P("LVL", 0, 1, "f", function() return pr.lvl end, function(v) pr.lvl = v end, pct),
      P("PIT", -12, 12, "i", function() return pr.pit end, function(v) pr.pit = v end, function(v) return fmt("%+d", v) end),
      P("FLT", 0, 1, "f", function() return pr.flt end, function(v) pr.flt = v end,
        function(v) return v >= 0.995 and "OFF" or fmt("%d", 80 * 2 ^ (v * 7.8)) end),
      P("DEC", 0, 1, "f", function() return pr.dec or 0.5 end, function(v) pr.dec = v end,
        function(v) return pr.dec and pct(v) or "DEF" end),
      P("PAN", -1, 1, "f", function() return pr.pan end, function(v) pr.pan = v end,
        function(v) return abs(v) < 0.03 and "C" or (v < 0 and fmt("L%d", round(-v * 100)) or fmt("R%d", round(v * 100))) end),
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
    if shift() then rate = rate / 8 end
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
  if shift() then rate = rate / 8 end
  S.master = clamp(S.master + dir * rate, 0, 1)
  dirty = true
end

-- ---------------------------------------------------------------- actions
local function set_mode(m)
  if mode == m and m ~= "main" then m = "main" end
  mode = m
  if m == "main" then psel.main = S.group end
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

local function pad_down(p, now)
  local g = S.group
  if mode == "pattern" then
    local n = PADNUM[p] + (S.page or 0) * 12
    if n > 99 then return end
    if shift() then copy_pattern(S.pat, n) end
    S.pat = n
    if pl.on and pl.tick >= plen() then pl.tick = 0 end
    flash[p] = 8
    return
  elseif mode == "fx" then
    S.fx = PADNUM[p]
    fx_globals()
    dirty = true
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
  if pressed(K.E) then set_mode("pattern") end
  if pressed(K.T) then
    if sh then tap_tempo(now) else set_mode("timing") end
  end
  if pressed(K.F) then set_mode("fx") end
  if pressed(K.G) then set_mode("keys") end
  if pressed(K.B) then set_mode("main") end

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

  -- parameters
  for i = 1, 5 do
    if pressed(PARAMKEY[i]) then
      local _, list = cur_param()
      if list[i] then psel[mode] = i end
    end
  end
  if pressed(K.LB) or pressed(K.RB) then
    local d = pressed(K.RB) and 1 or -1
    if mode == "sound" then select_sound(d)
    elseif mode == "keys" then S.oct = clamp(S.oct + d, -3, 3)
    elseif mode == "pattern" then S.page = clamp((S.page or 0) + d, 0, 8) end
  end

  -- groups
  for g = 1, 4 do
    if pressed(GRPKEY[g]) then
      if held[K.X] then
        erase_group(g)
        gflash[g] = 8
      else
        S.group = g
        if mode == "main" then psel.main = g end
        gflash[g] = 8
        if mode == "sound" then S.sel = { g, S.sel[2] } end
      end
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
  body = 0xC8CBCA, bodyhi = 0xDCDEDD, bodylo = 0x9EA2A1, black = 0x1C1C1E, dark = 0x34363A,
  orange = 0xFF6A1A, pad = 0xF0F0EC, padlo = 0xB9BAB5, txt = 0x26282B, dim = 0x707472,
  lcd = 0x12161A, lcdfg = 0xE6EDE6, lcddim = 0x56625E, lcdhi = 0xFF8A1E, led = 0xFF4020, ledoff = 0x5A2E26,
  green = 0x30D060,
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

local function led(x, y, on, col)
  rectfill(x, y, 5, 5, on and (col or C.led) or C.ledoff)
  if on then rect(x - 1, y - 1, 7, 7, (col or C.led) | 0) end
end

local function knob(cx, cy, r, v)
  for i = 0, 10 do
    local a = (-135 + 270 * i / 10) * pi / 180
    circfill(cx + floor(sin(a) * (r + 7)), cy - floor(cos(a) * (r + 7)), 1, C.dim)
  end
  circfill(cx, cy + 2, r + 2, C.bodylo)
  circfill(cx, cy, r + 2, C.black)
  circfill(cx, cy, r, C.orange)
  circfill(cx, cy, r - 7, 0xFF8240)
  local a = (-135 + 270 * v) * pi / 180
  local x1, y1 = cx + sin(a) * (r * 0.25), cy - cos(a) * (r * 0.25)
  local x2, y2 = cx + sin(a) * (r - 3), cy - cos(a) * (r - 3)
  line(floor(x1), floor(y1), floor(x2), floor(y2), 0xFFFFFF)
  line(floor(x1) + 1, floor(y1), floor(x2) + 1, floor(y2), 0xFFFFFF)
end

local function fader(cx, y1, y2, v)
  rectfill(cx - 3, y1, 6, y2 - y1, C.black)
  for i = 0, 10 do
    local y = y2 - floor((y2 - y1) * i / 10)
    line(cx - 14, y, cx - 8, y, C.dim)
    line(cx + 8, y, cx + 14, y, C.dim)
  end
  local ky = y2 - floor((y2 - y1) * v) - 9
  rrect(cx - 15, ky, 30, 18, 3, C.black)
  rectfill(cx - 13, ky + 7, 26, 4, C.orange)
  line(cx - 13, ky + 9, cx + 12, ky + 9, 0xFFFFFF)
end

local BTN = {
  { "sound", "SOUND", "Q", 0, 0 }, { "pattern", "PATTERN", "E", 1, 0 }, { "timing", "TIMING", "T", 2, 0 },
  { "fx", "FX", "F", 0, 1 }, { "keys", "KEYS", "G", 1, 1 }, { "main", "MAIN", "B", 2, 1 },
  { "erase", "ERASE", "X", 0, 2 }, { "shift", "SHIFT", "SH", 1, 2 }, { "metro", "METRO", "Z", 2, 2 },
  { "rec", "REC", "R", 0, 3 }, { "play", "PLAY", "SPC", 1, 3 }, { "help", "HELP", "F1", 2, 3 },
}
local BX0, BY0, BW, BH = 22, 140, 62, 36

local function draw_buttons()
  for _, b in ipairs(BTN) do
    local id = b[1]
    local x, y = BX0 + b[4] * (BW + 6), BY0 + b[5] * (BH + 8)
    local lit
    if id == "rec" then lit = recarm
    elseif id == "play" then lit = pl.on
    elseif id == "erase" then lit = held[K.X]
    elseif id == "shift" then lit = shift()
    elseif id == "metro" then lit = S.metro
    elseif id == "help" then lit = showhelp
    else lit = mode == id end
    rrect(x, y + 2, BW, BH, 4, C.bodylo)
    rrect(x, y, BW, BH, 4, lit and 0x4A4C50 or C.dark)
    print(b[2], x + 3, y + 3, 0xF4F4F0)
    small(b[3], x + 5, y + 21, C.dim)
    local col = (id == "rec") and C.led or (id == "play" and C.green or C.orange)
    led(x + BW - 10, y + 23, lit, col)
  end
end

local function draw_pads(now)
  local sound = ALL[S.pads[S.group][1].snd]
  for g = 1, 4 do
    local y = 140 + (g - 1) * 52
    local on = S.group == g
    rrect(234, y + 2, 54, 46, 5, C.bodylo)
    rrect(234, y, 54, 46, 5, on and 0xE8E8E4 or C.dark)
    local fl = (gflash[g] or 0) > 0
    big_c(GRPNAME[g], 261, y + 7, on and C.txt or 0xF0F0F0, 2)
    small_c(GRPHINT[g], 261, y + 33, on and C.dim or C.dim)
    led(278, y + 5, on or fl)
  end
  for p = 1, 12 do
    local r, c = (p - 1) // 3, (p - 1) % 3
    local x, y = 296 + c * 88, 140 + r * 52
    local pr = S.pads[S.group][p]
    local sd = ALL[pr.snd]
    local lit = (flash[p] or 0) > 0 or padhold[p]
    local sel = (mode == "sound" or mode == "keys") and S.sel[1] == S.group and S.sel[2] == p
    if mode == "pattern" then sel = (PADNUM[p] + (S.page or 0) * 12) == S.pat
    elseif mode == "fx" then sel = PADNUM[p] == S.fx end
    rrect(x, y + 3, 80, 46, 6, C.padlo)
    rrect(x, y, 80, 46, 6, lit and C.orange or C.pad)
    if sel then rect(x + 1, y + 1, 78, 44, lit and 0xFFFFFF or C.orange) rect(x + 2, y + 2, 76, 42, lit and 0xFFFFFF or C.orange) end
    print(PADLAB[p], x + 8, y + 5, lit and 0xFFFFFF or C.txt, 2)
    small_r(PADHINT[p], x + 74, y + 4, lit and 0xFFE0C8 or C.dim)
    local label = sd.name
    if mode == "pattern" then label = "PTN " .. (PADNUM[p] + (S.page or 0) * 12)
    elseif mode == "fx" then label = FXS[PADNUM[p]] and FXS[PADNUM[p]].name or "-" end
    small(label, x + 8, y + 32, lit and 0xFFFFFF or C.dim)
  end
end

local function lcd_params(x0, y0, list)
  for i, p in ipairs(list) do
    local x = x0 + (i - 1) * 70
    local on = psel[mode] == i
    if on then rectfill(x, y0 - 1, 68, 13, C.lcdhi) end
    small(i .. " " .. p.name, x + 2, y0, on and C.lcd or C.lcddim)
    small_r(p.show(p.get()), x + 66, y0, on and C.lcd or C.lcdfg)
  end
end

local function draw_lcd(now)
  local x0, y0, W = 184, 20, 360
  rrect(x0 - 8, y0 - 8, W + 16, 116, 8, C.black)
  rectfill(x0, y0, W, 100, C.lcd)

  -- top line
  local tag = MODENAME[mode]
  rectfill(x0 + 4, y0 + 3, #tag * 6 + 6, 13, C.lcdfg)
  small(tag, x0 + 7, y0 + 4, C.lcd)
  small(fmt("PTN %02d", S.pat), x0 + 78, y0 + 4, C.lcdfg)
  for g = 1, 4 do
    local gx = x0 + 132 + (g - 1) * 16
    if S.group == g then rectfill(gx - 2, y0 + 3, 12, 13, C.lcdhi); small(GRPNAME[g], gx + 1, y0 + 4, C.lcd)
    else small(GRPNAME[g], gx + 1, y0 + 4, C.lcddim) end
  end
  small_r(fmt("%d BPM", S.bpm), x0 + W - 34, y0 + 4, C.lcdfg)
  if recarm then
    local blink = pl.on and (frame_n // 15) % 2 == 0 or not pl.on
    if blink then circfill(x0 + W - 22, y0 + 9, 4, C.led) end
  end
  if pl.on then tri(x0 + W - 14, y0 + 4, x0 + W - 14, y0 + 14, x0 + W - 6, y0 + 9, C.green) end

  -- the big area
  local cx = x0 + 170
  if mode == "main" then
    big_c(pos_text(), cx, y0 + 24, C.lcdfg, 3)
    small_c(fmt("BARS %d   QNT %s   %s", PT().bars, QNAME[S.quant], S.metro and "METRO" or ""), cx, y0 + 4 + 62, C.lcddim)
  elseif mode == "sound" then
    local pr = S.pads[S.sel[1]][S.sel[2]]
    big_c(ALL[pr.snd].name, cx, y0 + 24, C.lcdfg, 3)
    small_c(fmt("PAD %s-%s     [ ] SOUND %d/%d", GRPNAME[S.sel[1]], PADLAB[S.sel[2]], pr.snd, #ALL), cx, y0 + 66, C.lcddim)
  elseif mode == "keys" then
    local pr = S.pads[S.sel[1]][S.sel[2]]
    local root = (ALL[pr.snd].note or 60) + pr.pit + S.oct * 12
    big_c(note_text(clamp(root, 0, 127)), cx - 40, y0 + 24, C.lcdfg, 3)
    print(ALL[pr.snd].name, cx + 20, y0 + 40, C.lcdhi, 1)
    small_c(fmt("OCTAVE %+d   [ ] OCTAVE", S.oct), cx, y0 + 66, C.lcddim)
  elseif mode == "pattern" then
    big_c(fmt("PTN %02d", S.pat), cx, y0 + 24, C.lcdfg, 3)
    small_c(fmt("PAGE %d   [ ] PAGE   SHIFT+PAD COPY", (S.page or 0) + 1), cx, y0 + 66, C.lcddim)
  elseif mode == "timing" then
    big_c(fmt("%d", S.bpm), cx - 20, y0 + 24, C.lcdfg, 3)
    print("BPM", cx + 52, y0 + 40, C.lcdhi, 1)
    small_c("SHIFT+T TAP TEMPO", cx, y0 + 66, C.lcddim)
  elseif mode == "fx" then
    big_c(FXS[S.fx].name, cx, y0 + 24, C.lcdfg, 3)
    rect(cx - 70, y0 + 68, 140, 6, C.lcddim)
    rectfill(cx - 70, y0 + 68, floor(140 * S.fxamt), 6, C.lcdhi)
  end

  -- level meter
  for i, v in ipairs({ meter.l, meter.r }) do
    local h = floor(clamp(v, 0, 1) * 52)
    local mx = x0 + W - 24 + (i - 1) * 9
    rect(mx, y0 + 22, 6, 54, C.lcddim)
    rectfill(mx + 1, y0 + 75 - h, 4, h, h > 44 and C.led or C.lcdhi)
  end

  -- parameters and steps
  local _, list = cur_param()
  lcd_params(x0 + 4, y0 + 79, list)

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
    local sx = x0 + 6 + (s - 1) * 22
    local col = lit[s] and C.lcdhi or (others[s] and C.lcddim or 0x252B30)
    if s - 1 == head then col = C.lcdfg end
    rectfill(sx, y0 + 93, 18, 5, col)
    if (s - 1) % 4 == 0 then rectfill(sx, y0 + 90, 18, 1, C.lcddim) end
  end
end

local function draw_help()
  rectfill(0, 0, 640, 360, 0x101418)
  print("KO-II  KEYS", 20, 12, C.lcdhi, 2)
  local L = {
    { "PADS", "7 8 9 / U I O / J K L / M , .   = pads 7 8 9 / 4 5 6 / 1 2 3 / . 0 ENT" },
    { "GROUPS", "6=A  Y=B  H=C  N=D   (A drums, B bass, C keys, D leads and fx)" },
    { "FADER", "W S or Up Down: edits the chosen parameter (hold Shift for fine)" },
    { "VOLUME", "A D or Left Right: master volume" },
    { "PARAMS", "1 2 3 4 5 choose the parameter the fader edits" },
    { "PLAY / REC", "Space plays and stops. R arms recording (a bar of count-in), pads record" },
    { "SOUND  Q", "[ ] change the sound of the last pad; the fader sets LVL PIT FLT DEC PAN" },
    { "PATTERN  E", "pads pick pattern 1-12, [ ] page, Shift+pad copies the pattern" },
    { "TIMING  T", "BPM, quantize, swing, bars, metronome. Shift+T taps the tempo" },
    { "FX  F", "pads pick one of 12 effects, the fader sets the amount" },
    { "KEYS  G", "the pads play a chromatic scale, [ ] octave" },
    { "ERASE  X", "hold X + pad: erase it from the pattern. X + group: erase the group. Shift+X: clear" },
    { "MAIN  B", "group levels A-D on the fader. Z metronome" },
  }
  for i, l in ipairs(L) do
    small(l[1], 20, 44 + (i - 1) * 21, C.lcdhi)
    small(l[2], 120, 44 + (i - 1) * 21, C.lcdfg)
  end
  small("F1 or / closes this page", 20, 332, C.lcddim)
end

function _draw()
  cls(0x8A8E8D)
  if showhelp then draw_help() return end
  local now = audio_time()
  rrect(4, 4, 632, 352, 12, C.bodylo)
  rrect(4, 4, 632, 350, 12, C.body)
  rectfill(16, 5, 608, 2, C.bodyhi)

  knob(92, 66, 28, S.master)
  small_c("VOLUME", 92, 104, C.txt)
  small_c("A D  < >", 92, 116, C.dim)

  draw_lcd(now)

  fader(596, 44, 330, (function() local p = cur_param(); return p and (p.get() - p.lo) / (p.hi - p.lo) or 0 end)())
  small_c("FADER", 596, 22, C.txt)
  small_c("W S ^ v", 596, 336, C.dim)

  draw_buttons()
  draw_pads(now)
  small("K.O. II layout", 22, 326, C.dim)
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
