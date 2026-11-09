-- bm Tape preview scenes (design 4.3): TAPE, MIX, FX and DJ. They stay as drawn in phase 1; the sequencer does not use them.
local function mix_hz(v) return v < 0.05 and v > -0.05 and "OFF" or string.format(v < 0 and "LP %.1fk" or "HP %.1fk", 0.4 + 7 * abs(v) ^ 2) end

-- TAPE: two reels above four track lanes; reels and play head follow the transport; the 4th encoder is the selected track
local tape_page = { name = "TAPE", params = {
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
    local shift_x, sel = -v[1] * 32, floor(v[4] + 0.5)
    local clips = { { { 0, 60 }, { 64, 60 }, { 130, 90 } }, { { 0, 124 }, { 130, 60 } }, { { 64, 60 }, { 192, 60 } }, { { 130, 124 } } }
    for i, lane in ipairs(clips) do
      local y, c = 78 + (i - 1) * 13, i == sel and C.e4 or C.grid
      T("T" .. i, 8, y + 7, i == sel and C.e4 or C.mute)
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

function tape_page.pull(sc) sc.params[4].value = S.trk end
function tape_page.push(sc, i)
  if i == 4 then select_track(sc.params[4].value) end
end

-- MIX: four towers with a level cap and a meter that falls (engine.meter_fall)
local mx = { lv = nil, pk = nil, t = 0 }
local mix_page = { name = "MIX", params = {
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

-- FX ECHO: one sun for each repeat, going back to the horizon
local fx_page = { name = "FX ECHO", params = {
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

-- DJ: two discs with a phase mark, two wave strips, a crossfader
local dj_page = { name = "DJ", params = {
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
      T(string.format("%s  %.1f", d == 0 and "A" or "B", S.bpm * sp), x, 20, c, "center")
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
