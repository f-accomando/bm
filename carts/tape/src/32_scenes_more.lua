-- bm Tape scenes of the DRUM, SEQ and TEMPO modules (design 4.3).

-- DRUM PAD: the waveform of the last pad hit with start and decay gates, and the 12 pad lights.
-- A kit pad draws the 32 peaks of its sample (audio_samples("kit")); a synth one-shot draws a shape of its number.
local drum_page = { name = "DRUM PAD", params = {
  P("START", 0, 1, 0.01, 0, "%.2f"), P("DECAY", 10, 2000, 10, 300, "%dms", 1),
  P("PITCH", -12, 12, 1, 0, "%+d st"), P("LEVEL", 0, 1, 0.01, 0.9, "%.2f") } }
function drum_page.sub() return string.format("PAD %d %s", S.pad, PADS[S.pad].name) end
function drum_page.pull(sc)
  local p = PADS[S.pad]
  sc.params[1].value, sc.params[2].value, sc.params[3].value, sc.params[4].value = p.begin, p.rel, p.pitch, p.level
end
function drum_page.push(sc, i)
  local p, v = PADS[S.pad], sc.params[i].value
  if i == 1 then p.begin = v elseif i == 2 then p.rel = v elseif i == 3 then p.pitch = v else p.level = v end
  p.tone = pad_tone(p)
end
local function pad_wave(p, u, n)
  if p.meter then
    local x = clamp(u, 0, 1) * 31
    local i = floor(x)
    return p.meter[i + 1] * (1 - (x - i)) + p.meter[min(i + 2, 32)] * (x - i)
  end
  return exp(-u * 3.2) * (0.55 + 0.45 * sin(u * (60 + 25 * n))) * (0.7 + 0.3 * sin(u * 23))
end
function drum_page.draw(t, v)
  ground(t)
  local p = PADS[S.pad]
  local s0 = 20 + 280 * v[1] + 6 * sin(t * 0.8)
  local s1 = clamp(s0 + (298 - s0) * lg(v[2], 10, 2000), s0 + 4, 298)
  local g = v[4] / 0.9
  for x = 20, 298, 2 do
    local h = (3 + 30 * abs(pad_wave(p, (x - 20) / 280, S.pad))) * g
    L(x, 58 - h, x, 58 + h, (x >= s0 and x <= s1) and C.e4 or C.grid)
  end
  L(s0, 22, s0, HZ, C.e1); poly({ { s0, 22 }, { s0 + 7, 25 }, { s0, 28 } }, C.e1, C.e1)
  L(s1, 22, s1, HZ, C.e2); poly({ { s1, 22 }, { s1 - 7, 25 }, { s1, 28 } }, C.e2, C.e2)
  for i = 0, 11 do R(268 + (i % 3) * 10, 100 + (i // 3) * 8, 8, 6, i + 1 == S.pad and C.hot or C.grid, i + 1 == S.pad) end
  L(20, 100, 20, 124, C.dim)
  R(18, 124 - 18 * v[4] - 1.5, 5, 3, C.e3, true)
  L(40, 112, 220, 112, C.dim)                           -- the pitch: a pink mark that moves along a scale
  O(130 + 90 * v[3] / 12, 112, 4, 4, C.e2, true)
  T(string.format("%+d st", v[3]), 130, 128, C.e2, "center")
end

-- SEQ GRID: a road with 16 gates ahead of the play head; a lit gate is a step of the track.
local NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }
local function note_name(n) return NAMES[n % 12 + 1] .. (n // 12 - 1) end
local grid_page = { name = "SEQ GRID", params = {
  P("STEP", 1, 16, 1, 1, "%02d"), P("NOTE", 1, 127, 1, 36, function(n) return S.trk == 1 and ("PAD " .. n) or note_name(n) end),
  P("VELOCITY", 1, 127, 1, 100, "%d"), P("GATE", 1, 16, 1, 1, "%d st") } }
function grid_page.sub() return string.format("T%d  %d STEPS", S.trk, S.len) end
function grid_page.pull(sc)
  S.cur = clamp(S.cur, 0, S.len - 1)
  local pr = sc.params
  pr[1].max, pr[2].max = S.len, S.trk == 1 and 12 or 127
  pr[1].value, pr[2].value, pr[3].value, pr[4].value = S.cur + 1, clamp(S.bn, 1, pr[2].max), S.bv, S.bg
end
function grid_page.push(sc, i)
  local v = sc.params[i].value
  if i == 1 then
    S.cur = v - 1
    local n, vel, gate = step_info(S.trk, S.cur)            -- the brush takes the values of the step
    if n > 0 then
      S.bn, S.bv, S.bg = aget(Q.notes, sidx(S.trk, S.cur, 0)), vel, clamp(gate // TPS, 1, 16)
    end
  elseif i == 2 then S.bn = v
  elseif i == 3 then S.bv = v
  else S.bg = v end
end
function grid_page.draw(t, v)
  local len, tk = S.len, S.trk
  local head = (now_tick() // TPS) % len
  local base = tr.run and head or S.cur
  sun(160, 46, 12 + 12 * v[3] / 127, 0.15, C.e3)
  L(0, 52, 320, 52, C.grid); L(160, 52, 20, BOT, C.grid); L(160, 52, 300, BOT, C.grid)
  for k = 15, 0, -1 do
    local i = (base + k) % len
    local n, vel = step_info(tk, i)
    local p = (1 - k / 16) ^ 2.4
    local y, hw, ph = 52 + (BOT - 52) * p, 140 * p, (4 + 26 * p) * (n > 0 and 0.4 + 0.6 * vel / 127 or 0.5)
    local c = (k == 0 and tr.run) and C.hot or (i == S.cur and C.e1 or (n > 0 and C.e2 or C.dim))
    local w = (k == 0 and tr.run or i == S.cur) and 2 or 1
    L(160 - hw, y, 160 - hw, y - ph, c, w); L(160 + hw, y, 160 + hw, y - ph, c, w)
    if n > 0 or c ~= C.dim then L(160 - hw, y - ph, 160 + hw, y - ph, c, w) else L(160 - hw, y, 160 + hw, y, C.dim) end
  end
  local page = S.cur // 16
  for i = 0, 15 do
    local st = page * 16 + i
    if st < len then
      local n = step_info(tk, st)
      local c = (tr.run and st == head) and C.hot or (st == S.cur and C.e1 or (n > 0 and C.e2 or C.dim))
      R(96 + i * 8, 18, 6, 5, c, n > 0 or st == head and tr.run or st == S.cur)
    end
  end
  R(96 + (S.cur % 16) * 8, 25, min(v[4], 16 - S.cur % 16) * 8 - 2, 2, C.e4, true)
  if len > 16 then T(string.format("%d/%d", page + 1, len // 16), 78, 24, C.mute, "center") end
  local top, nmax = 30, tk == 1 and 12 or 127                -- the note ladder: the pink mark is the brush note
  L(12, top, 12, 126, C.dim)
  R(9, 126 - 96 * (v[2] - 1) / (nmax - 1) - 1.5, 6, 3, C.e2, true)
end

-- TEMPO: a pendulum under the sun. It swings once for each beat (the transport's clock when it plays).
local tempo_page = { name = "TEMPO", params = {
  P("BPM", 60, 200, 1, 120, "%.1f", 0.1), P("SWING", 0, 3, 1, 0, "%d tk"),
  P("METRO", 0, 1, 0.05, 0, "%.2f"), P("LENGTH", 16, 64, 16, 16, "%d st") } }
function tempo_page.pull(sc)
  local pr = sc.params
  pr[1].value, pr[2].value, pr[3].value, pr[4].value = S.bpm, S.swing, S.metro, S.len
end
function tempo_page.push(sc, i)
  local v = sc.params[i].value
  if i == 1 then S.bpm = v elseif i == 2 then S.swing = v elseif i == 3 then S.metro = v else S.len = v end
end
function tempo_page.draw(t, v)
  local beats = tr.run and tick_at(audio_time()) / TPB or t * v[1] / 60
  local flash = exp(-(beats % 1) * 6)
  sun(160, 74, 14 + 14 * v[3] + 6 * v[3] * flash, 0.2, C.e3)
  ground(t)
  local a = 0.6 * sin(PI * beats)
  local bx, by = 160 + 70 * sin(a), 16 + 70 * cos(a)
  L(160, 16, bx, by, C.e1, 1.6)
  O(160, 16, 3, 3, C.e4, true)
  O(bx, by, 7, 7, C.ink, true); O(bx, by, 7, 7, C.e1)
  for i = 0, 7 do                                          -- eight 16th steps; the odd ones are late by the swing
    local x = 96 + i * 16 + (i % 2) * v[2] / TPS * 16
    R(x, 104, 6, 6, C.e2, i % 2 == 1)
  end
  for i = 0, v[4] // 16 - 1 do                             -- the length: one block for each 16 steps
    local playing = tr.run and (now_tick() // (16 * TPS)) % (v[4] // 16) == i
    R(100 + i * 30, 118, 26, 6, playing and C.hot or C.e4, true)
  end
end
