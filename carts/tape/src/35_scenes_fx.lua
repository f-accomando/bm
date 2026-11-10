-- bm Tape FX scenes (design 4.3): echo (suns that go back to the horizon), room (a wire room), chorus (two palms that sway
-- out of phase). Encoders 1..3 are the effect (echo(), reverb(), chorus() through FX.apply); encoder 4 is the send of the
-- current track (S.send), so its label names the track.
local fxe_page, fxr_page, fxc_page
do
local function fx_pull(name, si)
  return function(sc)
    local f, pr = S.fx[name], sc.params
    pr[1].value, pr[2].value, pr[3].value = f[1], f[2], f[3]
    pr[4].value, pr[4].label = S.send[S.trk][si], "SEND T" .. S.trk
  end
end
local function fx_push(name, si)
  return function(sc, i)
    local v = sc.params[i].value
    if i < 4 then
      S.fx[name][i] = v
      FX.apply(name)
    else
      S.send[S.trk][si] = v
      MIX.touch()
    end
  end
end
-- the send gauge at the right edge, in the colour of encoder 4
local function send_gauge(v)
  R(292, 30, 6, 60, C.dim)
  R(292, 90 - 60 * v, 6, 60 * v, C.e4, true)
end

-- FX ECHO: one sun for each repeat, going back to the horizon
fxe_page = { name = "FX ECHO", params = {
  P("TIME", 50, 680, 5, 375, "%dms"), P("FEEDBK", 0, 0.95, 0.05, 0.55, "%.2f"),
  P("WET", 0, 1, 0.05, 0.35, "%.2f"), P("SEND T3", 0, 1, 0.05, 0, "%.2f") },
  pull = fx_pull("echo", 2), push = fx_push("echo", 2),
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
    send_gauge(v[4])
  end }

-- FX ROOM: a wire room seen from its front. The back wall goes away as the size grows (e1). The rungs between the
-- walls fade with the damping (e2). The glow in the room and the ring that spreads from it are the wet level (e3).
fxr_page = { name = "FX ROOM", params = {
  P("SIZE", 0, 1, 0.02, 0.5, "%.2f"), P("DAMP", 0, 1, 0.02, 0.5, "%.2f"),
  P("WET", 0, 1, 0.02, 0.3, "%.2f"), P("SEND T3", 0, 1, 0.05, 0.16, "%.2f") },
  pull = fx_pull("room", 1), push = fx_push("room", 1),
  draw = function(t, v)
    local cx, cy, fw, fh = 160, 62, 124, 44
    local k = 1 - 0.86 * v[1]                              -- the size of the back wall against the front wall
    local bw, bh = fw * k, fh * k
    for i = 1, 5 do                                        -- the rungs
      local u = (i / 6) ^ 1.2
      local w, h = fw + (bw - fw) * u, fh + (bh - fh) * u
      R(cx - w, cy - h, w * 2, h * 2, fade(C.e2, (1 - 0.8 * v[2]) ^ i))
    end
    R(cx - fw, cy - fh, fw * 2, fh * 2, C.e1)
    R(cx - bw, cy - bh, bw * 2, bh * 2, C.e1)
    for _, sx in ipairs({ -1, 1 }) do
      for _, sy in ipairs({ -1, 1 }) do L(cx + sx * fw, cy + sy * fh, cx + sx * bw, cy + sy * bh, C.e1) end
    end
    local r = 3 + 12 * v[3]
    O(cx, cy, r, r, C.e3, true)
    local ring = (t * 24) % (10 + 60 * v[1])
    O(cx, cy, 8 + ring, (8 + ring) * 0.7, fade(C.e3, v[3] * (1 - ring / (10 + 60 * v[1]))))
    send_gauge(v[4])
  end }

-- FX CHORUS: two palms that sway out of phase. The left one is the voice (e1: it sways at the rate); the right one is
-- the copy (e3: its brightness is the wet level); the range between the two sways is the depth (e2).
local function palm(x, y0, lean, col)
  local pts = {}
  for i = 0, 10 do
    local u = i / 10
    pts[#pts + 1] = { x + lean * u * u * 22, y0 - 58 * u }
  end
  PL(pts, col, 1.6)
  local tx, ty = pts[#pts][1], pts[#pts][2]
  for a = -2, 2 do
    local dx = a * 9 + lean * 5
    PL({ { tx, ty }, { tx + dx * 0.6, ty - 9 + abs(a) }, { tx + dx * 1.3, ty + 4 + abs(a) * 3 } }, col, 1)
  end
end
fxc_page = { name = "FX CHORUS", params = {
  P("RATE", 0.05, 5, 0.05, 0.8, "%.2fHz"), P("DEPTH", 0, 8, 0.1, 2.5, "%.1fms"),
  P("WET", 0, 1, 0.05, 1, "%.2f"), P("SEND T3", 0, 1, 0.05, 0, "%.2f") },
  pull = fx_pull("chorus", 3), push = fx_push("chorus", 3),
  draw = function(t, v)
    ground(t)
    local amp, ph = 0.25 + 0.75 * v[2] / 8, t * v[1] * 2 * PI
    palm(104, HZ - 2, amp * sin(ph), C.e1)
    palm(216, HZ - 2, -amp * sin(ph), fade(C.e3, 0.15 + 0.85 * v[3]))
    local half = 14 + 26 * v[2] / 8
    L(160 - half, HZ + 8, 160 + half, HZ + 8, C.e2, 1.6)
    L(160 - half, HZ + 5, 160 - half, HZ + 11, C.e2); L(160 + half, HZ + 5, 160 + half, HZ + 11, C.e2)
    O(160 + half * sin(ph), HZ + 8, 3, 3, C.e1, true)          -- it swings at the rate
    send_gauge(v[4])
  end }
end
