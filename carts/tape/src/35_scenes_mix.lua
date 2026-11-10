-- bm Tape MIX scenes (design 4.3): four towers. The moving meter of a tower is the notes of its track (MIX.meter, which
-- engine.meter_fall lets fall); the sun is the sum of the four. MIX LEVEL: the cap of a tower is the level of its track.
-- MIX PAN: a marker slides left and right on each tower.
local mixl_page, mixp_page
do
local function pan_text(p)
  if p > -0.025 and p < 0.025 then return "C" end
  return string.format("%s%d", p < 0 and "L" or "R", floor(abs(p) * 100 + 0.5))
end

-- the towers; level = true draws them as MIX LEVEL (coloured), false as MIX PAN (grid, with the pan markers)
local function towers(t, level)
  local lev, peak, sum = {}, {}, 0
  for i = 1, 4 do
    lev[i], peak[i] = MIX.meter(i)
    sum = sum + lev[i]
  end
  sun(276, 54, 14 + 10 * sum / 4000, 0.2, C.hot)
  ground(t, 276)
  for i = 1, 4 do
    local x, h = 34 + (i - 1) * 52, 70 * S.lvl[i]
    local m = h * lev[i] / 1000
    local body = level and ECOL[i] or C.grid
    R(x, HZ - 70, 30, 70, C.dim); R(x, HZ - h, 30, h, body)
    for y = HZ - 2, HZ - m, -3 do L(x + 4, y, x + 26, y, body) end
    if peak[i] > 0 then L(x + 4, HZ - h * peak[i] / 1000, x + 26, HZ - h * peak[i] / 1000, C.e4) end
    if level then L(x - 3, HZ - h, x + 33, HZ - h, ECOL[i], 2) end
    T("T" .. i, x + 15, HZ + 10, C.mute, "center")
    if not level then
      local ym, mx = HZ - 36, x + 15 + 12 * S.pan[i]
      L(x + 3, ym, x + 27, ym, C.mute); L(x + 15, ym - 3, x + 15, ym + 3, C.mute)
      poly({ { mx, ym - 5 }, { mx + 4, ym }, { mx, ym + 5 }, { mx - 4, ym } }, ECOL[i], ECOL[i])
      T(pan_text(S.pan[i]), x + 15, HZ + 20, ECOL[i], "center")
    end
  end
end

-- MIX LEVEL: the level of each track, 0..1
mixl_page = { name = "MIX LEVEL", params = {
  P("DRUMS", 0, 1, 0.02, 0.9, "%.2f"), P("BASS", 0, 1, 0.02, 0.9, "%.2f"),
  P("CHORDS", 0, 1, 0.02, 0.7, "%.2f"), P("LEAD", 0, 1, 0.02, 0.7, "%.2f") } }
function mixl_page.pull(sc) for i = 1, 4 do sc.params[i].value = S.lvl[i] end end
function mixl_page.push(sc, i) S.lvl[i] = sc.params[i].value end
function mixl_page.draw(t, v) towers(t, true) end

-- MIX PAN: the pan of each track, -1 (left) .. 1 (right)
mixp_page = { name = "MIX PAN", params = {
  P("PAN T1", -1, 1, 0.05, 0, pan_text), P("PAN T2", -1, 1, 0.05, 0, pan_text),
  P("PAN T3", -1, 1, 0.05, 0, pan_text), P("PAN T4", -1, 1, 0.05, 0, pan_text) } }
function mixp_page.pull(sc) for i = 1, 4 do sc.params[i].value = S.pan[i] end end
function mixp_page.push(sc, i) S.pan[i] = sc.params[i].value; MIX.touch() end
function mixp_page.draw(t, v) towers(t, false) end
end
