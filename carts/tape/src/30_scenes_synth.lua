-- bm Tape scenes of the SYNTH module (design 4.3). A scene is { name, params = { {label, min, max, step, value, format} x4 },
-- draw = function(t, v), pull, push }. v[1..4] are the four values. Units are 320 x 160 (see 00_core.lua).
-- format is a string.format pattern or a function(value). pull(sc) loads the values from the data each frame;
-- push(sc, i) stores the value i after an encoder turned it.
local function P(label, lo, hi, step, value, format, fine)
  fine = fine or ((step >= 1 and floor(step) == step) and step or step / 10)
  return { label = label, min = lo, max = hi, step = step, fine = fine, value = value, format = format }
end

-- 0..1 position of v between lo and hi on a log scale
local function lg(v, lo, hi) return clamp(math.log(v / lo) / math.log(hi / lo), 0, 1) end

local EDRAW = {}                                   -- one drawing for each engine, in the order of ENGINES

-- FM: the moon (modulator) orbits a planet (carrier)
EDRAW[1] = function(t, v)
  ground(t)
  local rr, tilt, a = 20 + 12 * v[1] + 10 * sin(t * 0.6), -v[3] * 1.25, t * 1.7
  local ct, st = cos(tilt), sin(tilt)
  O(160, 56, rr, rr * 0.32, C.e1, false, tilt)
  L(160 - (rr + 8) * ct, 56 - (rr + 8) * st, 160 + (rr + 8) * ct, 56 + (rr + 8) * st, C.e3)
  local n = clamp(floor(v[4] / 13), 2, 80)
  for i = 1, n - 1 do
    local b = a - i * 0.09
    local x, y = cos(b) * rr, sin(b) * rr * 0.32
    R(160 + x * ct - y * st, 56 + x * st + y * ct, 1, 1, fade(C.e4, 1 - i / n), true)
  end
  local x, y = cos(a) * rr, sin(a) * rr * 0.32
  local mr = 3 + 0.6 * v[2]
  O(160 + x * ct - y * st, 56 + x * st + y * ct, mr, mr, C.e2, true)
  O(160, 56, 11, 11, C.ink, true)
  O(160, 56, 11, 11, C.e4)
end

-- STRING: a string between two pylons
EDRAW[2] = function(t, v)
  sun(160, 100 - 52 * sqrt(clamp(v[3] / 8, 0, 1)), 24, 0.1, C.e3)
  ground(t)
  L(44, HZ, 44, 34, C.grid); L(276, HZ, 276, 34, C.grid)
  L(36, HZ, 44, 34, C.grid); L(284, HZ, 276, 34, C.grid)
  for _, s in ipairs({ 1, -1 }) do                       -- dashed envelope of the first mode
    for x = 44, 271, 5 do
      local y0 = 40 + s * 14 * sin(PI * (x - 44) / 232)
      local y1 = 40 + s * 14 * sin(PI * (x + 2 - 44) / 232)
      L(x, y0, x + 2, y1, C.e2)
    end
  end
  local nodes = floor(v[1] * 4 + 0.5) + 1
  local amp = exp(-(t % 3) * (4 - 3.4 * v[2]))           -- a pluck every 3 s
  local pts = {}
  for x = 44, 276, 2 do
    pts[#pts + 1] = { x, 40 + 14 * amp * sin(nodes * PI * (x - 44) / 232) * cos(t * 9) }
  end
  PL(pts, C.e1, 1 + v[4] * 4)
end

-- PULSE: a skyline; the width of the towers is the duty, they breathe with the PWM depth,
-- the stars are the noise, the sun is the cutoff
EDRAW[3] = function(t, v)
  sun(160, 104 - 56 * sqrt(clamp(v[4] / 12, 0, 1)), 26, 0.1, C.e4)
  ground(t)
  for i = 1, floor(v[3] * 40) do
    if (i + floor(t * 3)) % 4 ~= 0 then R((i * 73) % 310 + 5, 18 + (i * 37) % 44, 1, 1, C.e3, true) end
  end
  local w = 4 + 26 * v[1]
  for i = 0, 8 do
    local h = (28 + 8 * ((i * 5) % 4)) * (1 + 0.35 * v[2] * sin(t * 2.2 + i * 0.8))
    local x = 16 + i * 34
    R(x, HZ - h, w, h, C.ink, true); R(x, HZ - h, w, h, C.e1)
    for y = HZ - h + 5, HZ - 5, 8 do L(x + 3, y, x + w - 3, y, C.dim) end
    L(x, HZ - h, x + w, HZ - h, C.e2, 1.6)
  end
end

-- CLUSTER: three beams from one point; the angle is the spread, the length the cutoff,
-- the ring the resonance, the thickness of the middle beam the drive
EDRAW[4] = function(t, v)
  ground(t)
  local cx, len, ang = 160, 24 + 50 * sqrt(clamp(v[2] / 12, 0, 1)), v[1] * 0.9 + 0.03 * sin(t * 3)
  for g = 0, 2 do
    local r = 5 + 14 * v[3] + g * 3 + 1.5 * sin(t * 5 + g)
    O(cx, HZ - 2, r, r * 0.5, fade(C.e3, 0.95 - g * 0.3))
  end
  for s = -1, 1, 2 do
    local a = -PI / 2 + s * ang
    local ex, ey = cx + cos(a) * len, HZ - 2 + sin(a) * len
    L(cx, HZ - 2, ex, ey, C.e1)
    O(ex, ey, 3, 3, C.e2, true)
  end
  L(cx, HZ - 2, cx, HZ - 2 - len, C.e4, 1 + 4 * v[4])
  O(cx, HZ - 2 - len, 3, 3, C.e2, true)
end

-- ORGAN: four neon pillars, one for each drawbar
EDRAW[5] = function(t, v)
  sun(160, 60, 26, 0.15, C.hot)
  ground(t)
  for i = 1, 4 do
    local x, h = 40 + (i - 1) * 62, 4 + 70 * v[i] / 15
    R(x, HZ - h, 34, h, C.ink, true)
    for y = HZ - 3, HZ - h, -3 do L(x + 3, y, x + 31, y, fade(ECOL[i], 0.5)) end
    R(x, HZ - h, 34, h, ECOL[i])
    L(x - 2, HZ - h - 2 + sin(t * 3 + i), x + 36, HZ - h - 2 + sin(t * 3 + i), ECOL[i], 2)
  end
end

-- VOX: a vector mouth; the vowel is its shape, the rays are the cutoff, the wave below turns with the LFO rate,
-- the mouth pulses with the tremolo
do
local MOUTH_W, MOUTH_H = { 56, 52, 56, 28, 20 }, { 46, 30, 9, 40, 18 }   -- width and height of the mouth for a e i o u
EDRAW[6] = function(t, v)
  ground(t)
  local i0 = clamp(floor(v[1]), 0, 3)
  local f = v[1] - i0
  local w = MOUTH_W[i0 + 1] * (1 - f) + MOUTH_W[i0 + 2] * f
  local h = MOUTH_H[i0 + 1] * (1 - f) + MOUTH_H[i0 + 2] * f
  local k = 1 + v[4] * 0.3 * sin(t * (2 + v[3]))
  w, h = w * k, h * k
  local n = 3 + floor(v[2] / 12 * 9)
  for j = 1, n do
    local a = -PI / 2 + (j / (n + 1) - 0.5) * 2.6
    local r0 = max(w, h) * 0.5 + 4
    L(160 + cos(a) * r0 * 1.2, 60 + sin(a) * r0, 160 + cos(a) * (r0 + 5 + 6 * v[2] / 12), 60 + sin(a) * (r0 + 5 + 6 * v[2] / 12), C.e2)
  end
  O(160, 60, w * 0.5, h * 0.5, C.ink, true)
  O(160, 60, w * 0.5, h * 0.5, C.e1)
  O(160, 60 + h * 0.2, w * 0.28, h * 0.2, C.grid, true)
  local pts = {}
  for x = 40, 280, 3 do pts[#pts + 1] = { x, 112 + 4 * sin(x * 0.08 + t * v[3] * 2) } end
  PL(pts, C.e3, 1.4)
  R(300, 80 - 30 * v[4], 5, 30 * v[4] + 1, C.e4, true)
end

end

-- DIGITAL: a wave that breaks into blocks; the block height is the bit depth, the width the coarse value,
-- the shape of the fold is the curve, the gain the drive
do
local SHAPE = {
  function(x) local e = exp(2 * x) return (e - 1) / (e + 1) end,           -- soft
  function(x) return clamp(x, -1, 1) end,                                     -- hard
  function(x) return 2 / PI * math.asin(sin(x * 1.5)) end,                   -- fold
  function(x) return sin(x * PI / 2) end,                                     -- sine
  function(x) return clamp(x > 0 and x or x * 0.5, -1, 1) end,               -- asym
  function(x) x = clamp(x, -1.5, 1.5) return (x - x * x * x / 3) / 1.125 end, -- cubic
}
EDRAW[7] = function(t, v)
  ground(t)
  local hq = v[1] >= 16 and 0.5 or max(0.5, 56 / 2 ^ (min(v[1], 7) - 1))
  local bw, g = v[2] * 2, 0.5 + v[4] * 1.5
  local shape = SHAPE[floor(v[3] + 0.5) + 1]
  for x0 = 20, 300 - bw, bw do
    local y = shape(sin((x0 + bw / 2 - 20) * 0.045 + t * 2) * g)
    local yq = floor(y * 36 / hq + 0.5) * hq
    R(x0, 52 - yq - hq / 2, max(1, bw - 0.5), hq, C.e1, true)
  end
  L(10, 52 - hq / 2, 10, 52 + hq / 2, C.e1, 2)
  L(20, 102, 20 + bw, 102, C.e2, 2)
  T(CURVES[floor(v[3] + 0.5) + 1]:upper(), 160, 116, C.e3, "center")
  R(296, 90 - 40 * v[4], 5, 40 * v[4] + 1, C.e4, true)
end

end

-- PCM: the sample as a ridge over the horizon (its 32 peaks from audio_samples()). The start gate (encoder 1) cuts the
-- ridge: the part before it is dim. The sun is the cutoff, the ring around it the resonance, the second line of the
-- ridge (it shakes) the drive. A dot runs along the ridge from the gate: the sample as the voice plays it.
do
local PCM_TOP, PCM_X0, PCM_W = 30, 20, 280
local function ridge_at(m, u)
  if not m then return 0.5 + 0.4 * sin(u * 23) * exp(-u * 2) end
  local x = clamp(u, 0, 1) * 31
  local i = floor(x)
  local f = (1 - cos((x - i) * PI)) / 2                      -- a smooth step between two peaks
  return m[i + 1] * (1 - f) + m[min(i + 2, 32)] * f
end
EDRAW[8] = function(t, v)
  local tk = trk[S.syn]
  local info = PCMINFO[tk.sample or PCM_FIRST]
  local m = info and info.meter
  local sy = 98 - 52 * sqrt(clamp(v[2] / 12, 0, 1))
  sun(160, sy, 24, 0.1, C.e3)
  clipu(0, 0, 320, HZ)
  O(160, sy, 30 + 12 * v[3], 30 + 12 * v[3], fade(C.e2, 0.2 + 0.8 * v[3]))
  clip()
  ground(t)
  local gx = PCM_X0 + PCM_W * v[1]
  local pts, low = {}, {}
  for x = PCM_X0, PCM_X0 + PCM_W, 4 do
    local y = HZ - 6 - 46 * ridge_at(m, (x - PCM_X0) / PCM_W)
    pts[#pts + 1] = { x, y }
    low[#low + 1] = { x, y + 2 * v[4] * 4 * sin(x * 0.7 + t * 31) }
  end
  local body = { { PCM_X0, HZ - 2 } }
  for _, p in ipairs(pts) do body[#body + 1] = p end
  body[#body + 1] = { PCM_X0 + PCM_W, HZ - 2 }
  poly(body, nil, C.ink)
  for i = 1, #pts - 1 do
    local on = pts[i][1] >= gx - 2
    L(pts[i][1], pts[i][2], pts[i + 1][1], pts[i + 1][2], on and C.e1 or C.grid, 1.4)
    if v[4] > 0.02 and on then L(low[i][1], low[i][2], low[i + 1][1], low[i + 1][2], C.e2, 1) end
  end
  L(PCM_X0, HZ - 2, PCM_X0 + PCM_W, HZ - 2, C.grid)
  L(gx, PCM_TOP, gx, HZ, C.e1, 1.2)
  poly({ { gx, PCM_TOP }, { gx + 7, PCM_TOP + 3 }, { gx, PCM_TOP + 6 } }, C.e1, C.e1)
  local span = max(0.05, 1 - v[1])
  local u = v[1] + (t * 0.5 % 1) * span
  local k = clamp(floor((u * PCM_W) / 4) + 1, 1, #pts)
  O(PCM_X0 + u * PCM_W, pts[k][2], 3, 3, C.e4, true)
  local nm = info and info.name or (tk.sample or PCM_FIRST)
  local secs = info and info.frames and info.rate and info.rate > 0 and info.frames / info.rate or 0
  T(string.format("%s %.2fs", nm, secs), 160, 116, C.e3, "center")
end

end

-- the ENGINE page: the engine of the synth track S.syn picks the drawing and the macros
local EPAR = {}
for i, e in ipairs(ENGINES) do
  EPAR[i] = {}
  for j, m in ipairs(e.macros) do EPAR[i][j] = P(m.label, m.min, m.max, m.step, m.def, m.fmt) end
end
local engine_page = { name = "ENGINE", params = EPAR[1] }
function engine_page.pull(sc)
  local t = trk[S.syn]
  sc.params = EPAR[t.eng]
  for j, p in ipairs(sc.params) do p.value = macro_get(t, j) end
end
function engine_page.push(sc, i)
  local t = trk[S.syn]
  macro_set(t, i, sc.params[i].value)
  t.mod = true
  retone(t)
end
function engine_page.draw(t, v) EDRAW[trk[S.syn].eng](t, v) end
-- Up and Down change the engine
function engine_page.arrows(d) set_engine(trk[S.syn], (trk[S.syn].eng - 1 + d) % #ENGINES + 1) end

-- the name of the synth track's sound for the line under the status strip
local function sound_name(sc)
  local t = trk[S.syn]
  return string.format("T%d %s %s%s", S.syn, ENGINES[t.eng].name, t.snd > 0 and SOUNDS[t.snd].name or "CUSTOM", t.mod and "*" or "")
end
engine_page.sub = sound_name

-- ENVELOPE: the ridge is the ADSR curve of the synth track
local env_page = { name = "ENVELOPE", sub = sound_name, params = {
  P("ATTACK", 1, 1000, 5, 40, "%dms", 1), P("DECAY", 10, 2000, 10, 220, "%dms", 1),
  P("SUSTAIN", 0, 1, 0.01, 0.55, "%.2f"), P("RELEASE", 10, 2000, 10, 600, "%dms", 1) } }
function env_page.pull(sc)
  for i, p in ipairs(sc.params) do p.value = trk[S.syn].env[i] end
end
function env_page.push(sc, i)
  local t = trk[S.syn]
  t.env[i], t.mod = sc.params[i].value, true
  retone(t)
end
function env_page.draw(t, v)
  local aw = 6 + 58 * lg(v[1], 1, 1000) + 2 * sin(t * 0.7)
  local dw = 8 + 72 * lg(v[2], 10, 2000)
  local sy = HZ - 62 * v[3]
  local rw = 6 + 94 * lg(v[4], 10, 2000)
  sun(214, sy, 30, 0, C.hot)
  ground(t)
  local p = { { 40, HZ }, { 40 + aw, 30 }, { 40 + aw + dw, sy }, { 214, sy }, { 214 + rw, HZ } }
  poly(p, nil, C.ink)
  for i = 1, 4 do L(p[i][1], p[i][2], p[i + 1][1], p[i + 1][2], ECOL[i], 1.6) end
  for i = 2, 4 do dash(p[i][1], p[i][2], p[i][1], HZ, C.grid, 1, 3) end
end

-- FILTER and LFO: the sun is cut at the cutoff line; the resonance is the bump on the line;
-- the water below wobbles with the LFO rate (speed) and depth (height)
local flt_page = { name = "FILTER", sub = sound_name, params = {} }
for i, f in ipairs(FDEF) do flt_page.params[i] = P(f.label, f.min, f.max, f.step, f.def, f.fmt) end
function flt_page.pull(sc)
  for i, p in ipairs(sc.params) do p.value = trk[S.syn].flt[i] end
end
function flt_page.push(sc, i)
  local t = trk[S.syn]
  t.flt[i], t.mod = sc.params[i].value, true
  retone(t)
end
function flt_page.draw(t, v)
  local yc = 24 + 84 * lg(v[1], 0.1, 12)           -- a higher cutoff lowers the line: more of the sun shows
  clipu(0, 14, 320, yc - 14)
  O(160, 66, 42, 42, C.hot, true)
  for i = 0, 5 do rectfill(px(118), px(66 + 42 * (0.1 + i * 0.16)), px(84), max(1, px(1 + i * 0.6)), C.ink) end
  clip()
  local bump = {}
  for x = 100, 220, 3 do bump[#bump + 1] = { x, yc - 30 * v[2] * exp(-((x - 160) / 22) ^ 2) } end
  PL(bump, C.e2, 1.4)
  L(60, yc, 100, yc, C.e1, 1.6); L(220, yc, 260, yc, C.e1, 1.6)
  for j = 0, 8 do
    local pts, y0 = {}, yc + 6 + j * j * 0.9 + j * 2
    if y0 < BOT then
      for x = 20, 300, 4 do pts[#pts + 1] = { x, y0 + (1.5 + 5 * v[4]) * sin(x * 0.06 + t * v[3] * 2 + j) } end
      PL(pts, j == 0 and C.e3 or fade(C.e3, 0.6), 1)
    end
  end
  R(306, yc + 8, 5, 4 + 12 * v[4], C.e4, true)
end
