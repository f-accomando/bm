-- bm Tape DJ scene (design 4.3). It stays as drawn in phase 1 (a PREVIEW): the sequencer does not use it.
local function mix_hz(v) return v < 0.05 and v > -0.05 and "OFF" or string.format(v < 0 and "LP %.1fk" or "HP %.1fk", 0.4 + 7 * abs(v) ^ 2) end

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
