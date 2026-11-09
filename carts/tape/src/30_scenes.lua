-- bm Tape scenes (design 4.3). A scene is { name, params = { {label, min, max, step, value, format} x4 },
-- draw = function(t, v) }; v[1..4] are the four values. Units are 320 x 160 (see 00_core.lua).
-- format is a string.format pattern or a function(value).
local function P(label, lo, hi, step, value, format)
  local fine = (step >= 1 and floor(step) == step) and step or step / 10
  return { label = label, min = lo, max = hi, step = step, fine = fine, value = value, format = format }
end

local function mix_hz(v) return v < 0.05 and v > -0.05 and "OFF" or string.format(v < 0 and "LP %.1fk" or "HP %.1fk", 0.4 + 7 * abs(v) ^ 2) end

local scenes = {}

-- 1. SYNTH FM: the moon (modulator) orbits a planet (carrier)
scenes[1] = { name = "SYNTH FM", params = {
  P("RATIO", 0.5, 4, 0.05, 2.0, "%.2f"), P("DEPTH", 0, 8, 0.1, 3.4, "%.1f"),
  P("FEEDBK", 0, 1, 0.05, 0.2, "%.2f"), P("DECAY", 20, 1000, 10, 180, "%dms") },
  draw = function(t, v)
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
  end }

-- 2. SYNTH STRING: a string between two pylons
scenes[2] = { name = "SYNTH STRING", params = {
  P("BRIGHT", 0, 1, 0.02, 0.62, "%.2f"), P("RING", 0, 1, 0.02, 0.8, "%.2f"),
  P("CUTOFF", 0.2, 8, 0.1, 2.4, "%.1fk"), P("DRIVE", 0, 1, 0.01, 0.15, "%.2f") },
  draw = function(t, v)
    sun(160, 100 - 52 * sqrt(v[3] / 8), 24, 0.1, C.e3)
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
  end }

-- 3. ENVELOPE: the ridge is the ADSR curve
scenes[3] = { name = "ENVELOPE", params = {
  P("ATTACK", 1, 400, 1, 40, "%dms"), P("DECAY", 10, 1000, 5, 220, "%dms"),
  P("SUSTAIN", 0, 1, 0.01, 0.55, "%.2f"), P("RELEASE", 10, 2000, 10, 600, "%dms") },
  draw = function(t, v)
    local aw = clamp(6 + 28 * sqrt(v[1] / 40), 6, 70) + 2 * sin(t * 0.7)
    local dw = clamp(46 * sqrt(v[2] / 220), 8, 80)
    local sy = HZ - 62 * v[3]
    local rw = clamp(60 * sqrt(v[4] / 600), 6, 100)
    sun(214, sy, 30, 0, C.hot)
    ground(t)
    local p = { { 40, HZ }, { 40 + aw, 30 }, { 40 + aw + dw, sy }, { 214, sy }, { 214 + rw, HZ } }
    poly(p, nil, C.ink)
    for i = 1, 4 do L(p[i][1], p[i][2], p[i + 1][1], p[i + 1][2], ECOL[i], 1.6) end
    for i = 2, 4 do dash(p[i][1], p[i][2], p[i][1], HZ, C.grid, 1, 3) end
  end }

-- 4. DRUM PAD: waveform of the pad with start and end gates
scenes[4] = { name = "DRUM PAD 7", params = {
  P("START", 0, 1, 0.01, 0.18, "%.2f"), P("END", 0, 1, 0.01, 0.76, "%.2f"),
  P("PITCH", -12, 12, 1, 0, "%+d st"), P("LEVEL", 0, 1, 0.01, 0.9, "%.2f") },
  draw = function(t, v)
    ground(t)
    local s0 = 20 + 280 * v[1] + 6 * sin(t * 0.8)
    local s1 = 20 + 280 * v[2]
    local f, g = 1.7 * 2 ^ (v[3] / 12), v[4] / 0.9
    for x = 20, 298, 2 do
      local u = (x - 20) / 280
      local env = exp(-u * 3.2) * (0.55 + 0.45 * sin(x * f) * sin(x * 0.31))
      local h = (3 + 30 * abs(env)) * g
      L(x, 58 - h, x, 58 + h, (x >= s0 and x <= s1) and C.e4 or C.grid)
    end
    L(s0, 22, s0, HZ, C.e1); poly({ { s0, 22 }, { s0 + 7, 25 }, { s0, 28 } }, C.e1, C.e1)
    L(s1, 22, s1, HZ, C.e2); poly({ { s1, 22 }, { s1 - 7, 25 }, { s1, 28 } }, C.e2, C.e2)
    local hit = floor(t * 2) % 12
    for i = 0, 11 do R(268 + (i % 3) * 10, 100 + (i // 3) * 8, 8, 6, i == hit and C.hot or C.grid, i == hit) end
    L(20, 100, 20, 124, C.dim)
    R(18, 124 - 18 * v[4] - 1.5, 5, 3, C.e3, true)
  end }

-- 5. SEQ GRID: a road with 16 gates; a lit gate is a step of the euclidean pattern
local seq_key, seq_pat = nil, {}
scenes[5] = { name = "SEQ GRID", params = {
  P("STEP", 1, 16, 1, 6, "%02d"), P("PULSES", 0, 16, 1, 6, "%d"),
  P("ROTATE", 0, 15, 1, 0, "%d"), P("GATE", 10, 100, 5, 50, "%d%%") },
  draw = function(t, v)
    local key = v[2] * 100 + v[3]
    if key ~= seq_key then
      seq_key = key
      seq_pat = engine.euclid(floor(v[2]), 16, floor(v[3])) or {}
    end
    local pat = seq_pat
    local cur, cursor = tr_step() % 16, floor(v[1]) - 1
    local gk = 0.5 + v[4] / 100
    sun(160, 46, 20, 0.15, C.e3)
    L(0, 52, 320, 52, C.grid); L(160, 52, 20, BOT, C.grid); L(160, 52, 300, BOT, C.grid)
    for k = 15, 0, -1 do
      local i = (cur + k) % 16
      local on = pat[i + 1] == 1
      local p = (1 - k / 16) ^ 2.4
      local y, hw, ph = 52 + (BOT - 52) * p, 140 * p, (4 + 26 * p) * gk
      local c = k == 0 and C.hot or (on and C.e2 or C.dim)
      local w = k == 0 and 2 or 1
      L(160 - hw, y, 160 - hw, y - ph, c, w); L(160 + hw, y, 160 + hw, y - ph, c, w)
      if on or k == 0 then L(160 - hw, y - ph, 160 + hw, y - ph, c, w) else L(160 - hw, y, 160 + hw, y, C.dim) end
    end
    for i = 0, 15 do
      local on = pat[i + 1] == 1
      R(96 + i * 8, 18, 6, 5, i == cur and C.hot or (i == cursor and C.e1 or (on and C.e2 or C.dim)), i == cur or on)
    end
  end }

-- 6. TAPE: two reels above four track lanes; reels and play head follow the transport
scenes[6] = { name = "TAPE", params = {
  P("SCROLL", 0, 4, 0.25, 0, "bar %.2f"), P("LOOP IN", 1, 8, 0.25, 3, "%.2f"),
  P("LOOP OUT", 1, 8, 0.25, 7, "%.2f"), P("TRACK", 1, 4, 1, 3, "%d") },
  draw = function(t, v)
    local a0 = tr.pos * 2
    for _, x in ipairs({ 112, 208 }) do
      O(x, 44, 22, 22, C.e4); O(x, 44, 6, 6, C.e4)
      for k = 0, 2 do
        local a = a0 + k * 2.094
        L(x + cos(a) * 7, 44 + sin(a) * 7, x + cos(a) * 20, 44 + sin(a) * 20, C.grid)
      end
    end
    O(112, 44, 15, 15, C.grid); O(208, 44, 11, 11, C.grid)
    L(112, 66, 208, 66, C.e4); R(152, 62, 16, 7, C.hot)
    local shift_x, trk = -v[1] * 32, floor(v[4] + 0.5)
    local clips = { { { 0, 60 }, { 64, 60 }, { 130, 90 } }, { { 0, 124 }, { 130, 60 } }, { { 64, 60 }, { 192, 60 } }, { { 130, 124 } } }
    for i, lane in ipairs(clips) do
      local y, c = 78 + (i - 1) * 13, i == trk and C.e4 or C.grid
      T("T" .. i, 8, y + 7, i == trk and C.e4 or C.mute)
      clipu(24, 72, 294, 62)
      for _, cl in ipairs(lane) do
        R(26 + cl[1] + shift_x, y, cl[2] - 3, 9, c)
        if i == 2 and cl[1] == 130 then
          for x = 28 + cl[1], 26 + cl[1] + cl[2] - 6, 3 do L(x + shift_x, y + 2, x + shift_x, y + 7, C.grid) end
        end
      end
      clip()
    end
    local lx, rx = 26 + (v[2] - 1) * 32 + shift_x, 26 + (max(v[3], v[2] + 0.25) - 1) * 32 + shift_x
    clipu(24, 72, 294, 62)
    L(lx, 74, lx, 131, C.e2); L(rx, 74, rx, 131, C.e3); L(lx, 74, rx, 74, C.dim)
    local ph = lx + (tr.pos / 2 * 32) % (rx - lx)
    L(ph, 72, ph, 132, C.hot, 1.6)
    clip()
  end }

-- 7. MIX: four towers with a level cap and a meter that falls (engine.meter_fall)
local mx = { lv = nil, pk = nil, t = 0 }
scenes[7] = { name = "MIX", params = {
  P("DRUMS", 0, 1, 0.02, 0.8, "%.2f"), P("BASS", 0, 1, 0.02, 0.62, "%.2f"),
  P("CHORDS", 0, 1, 0.02, 0.5, "%.2f"), P("LEAD", 0, 1, 0.02, 0.7, "%.2f") },
  draw = function(t, v)
    if not mx.lv and engine.mod then mx.lv, mx.pk = engine.buf(4), engine.buf(8) end
    local lev, peak = { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
    if mx.lv then
      -- the test signal pushes each level up; the engine lets it fall
      local dt = clamp(floor((t - mx.t) * 1000), 0, 100)
      for i = 1, 4 do
        local src = floor(1000 * (0.45 + 0.5 * abs(sin(t * (2 + i) + i))))
        if sin(t * (2 + i) + i) > 0.5 then vm_buf_fill(mx.lv, { max(src, vm_buf_get(mx.lv, i)) }, i) end
      end
      if engine.meter_fall(mx.lv, mx.pk, 4, dt) then
        for i = 1, 4 do lev[i], peak[i] = vm_buf_get(mx.lv, i), vm_buf_get(mx.pk, i) end
      end
    end
    mx.t = t
    local avg = (lev[1] + lev[2] + lev[3] + lev[4]) / 4000
    sun(276, 54, 14 + 10 * avg, 0.2, C.hot)
    ground(t, 276)
    for i = 1, 4 do
      local x, h = 34 + (i - 1) * 52, 70 * v[i]
      local m = h * lev[i] / 1000
      R(x, HZ - 70, 30, 70, C.dim); R(x, HZ - h, 30, h, ECOL[i])
      for y = HZ - 2, HZ - m, -3 do L(x + 4, y, x + 26, y, ECOL[i]) end
      if peak[i] > 0 then L(x + 4, HZ - h * peak[i] / 1000, x + 26, HZ - h * peak[i] / 1000, C.e4) end
      L(x - 3, HZ - h, x + 33, HZ - h, ECOL[i], 2)
      T("T" .. i, x + 15, HZ + 10, C.mute, "center")
    end
  end }

-- 8. FX ECHO: one sun for each repeat, going back to the horizon
scenes[8] = { name = "FX ECHO", params = {
  P("TIME", 50, 1000, 5, 375, "%dms"), P("FEEDBK", 0, 0.95, 0.05, 0.55, "%.2f"),
  P("WET", 0, 1, 0.05, 0.35, "%.2f"), P("SEND T3", 0, 1, 0.05, 0.4, "%.2f") },
  draw = function(t, v)
    ground(t, 250)
    local n, ph = floor(2 + v[2] * 7.3), (t * 225 / v[1]) % 1
    for k = n - 1, 0, -1 do
      local u = (k + ph) / n
      local x, y, r = 70 + 180 * (1 - (1 - u) ^ 1.6), 62 - 14 * u, 30 * (1 - u * 0.82)
      local al = max(0, 1 - u * 1.05) * clamp(0.4 + v[3] * 1.7, 0, 1)
      O(x, y, r, r, C.ink, true)
      O(x, y, r, r, fade(k == 0 and ph < 0.25 and C.e4 or C.e2, al))
    end
    local len = v[1] * 0.144
    L(70, 104, 70 + len, 104, C.e1); L(70, 101, 70, 107, C.e1); L(70 + len, 101, 70 + len, 107, C.e1)
    T(string.format("%dms", v[1]), 70 + len / 2, 113, C.e1, "center")
    R(292, 30, 6, 60, C.dim)
    R(292, 90 - 60 * v[4], 6, 60 * v[4], C.e3, true)
  end }

-- 9. DJ: two discs with a phase mark, two wave strips, a crossfader
scenes[9] = { name = "DJ", params = {
  P("PITCH A", -8, 8, 0.1, 0, "%+.1f%%"), P("PITCH B", -8, 8, 0.1, 4, "%+.1f%%"),
  P("XFADE", -1, 1, 0.05, 0, function(x) return x < -0.05 and "A" or (x > 0.05 and "B" or "A=B") end),
  P("FILTER", -1, 1, 0.05, -0.3, mix_hz) },
  draw = function(t, v)
    for d = 0, 1 do
      local x, c, sp = d == 0 and 84 or 236, d == 0 and C.e1 or C.e2, 1 + v[d + 1] / 100
      O(x, 54, 32, 32, C.grid); O(x, 54, 24, 24, C.dim); O(x, 54, 16, 16, C.dim); O(x, 54, 5, 5, c)
      local a = t * 3.3 * sp
      L(x + cos(a) * 7, 54 + sin(a) * 7, x + cos(a) * 32, 54 + sin(a) * 32, c, 1.6)
      for i = 0, 59 do
        local off = i + floor(t * 14 * sp)
        local h = 2 + 6 * abs(sin(off * 0.9) * sin(off * 0.23)) + (off % 8 == 0 and 4 or 0)
        L(x - 60 + i * 2, 104 - h, x - 60 + i * 2, 104 + h, i == 30 and C.e4 or (off % 8 == 0 and c or C.grid))
      end
      T(string.format("%s  %.1f", d == 0 and "A" or "B", BPM * sp), x, 20, c, "center")
    end
    for i = 0, 3 do
      R(146 + i * 8, 40, 5, 5, floor(t * 2) % 4 == i and C.e1 or C.dim, true)
      R(146 + i * 8, 50, 5, 5, floor(t * 2.08) % 4 == i and C.e2 or C.dim, true)
    end
    L(110, 126, 210, 126, C.grid)
    R(160 + 34 * v[3] - 5, 121, 10, 10, C.e3, true)
    O(160, 74, 9, 9, C.e4)
    local ka = v[4] * 2.2 - 1.57
    L(160, 74, 160 + 6 * cos(ka), 74 + 6 * sin(ka), C.e4)
  end }
