-- bm Tape: palette, drawing helpers in units of a 320 x 160 display, key polling.
-- The display is drawn at S pixels per unit (S = 2 on the Pi: 640 x 320, a 40-pixel strip below).
local floor, sin, cos, abs, max, min, sqrt, exp = math.floor, math.sin, math.cos, math.abs, math.max, math.min, math.sqrt, math.exp
local PI = math.pi
local S = 2
local HZ, BOT = 92, 134          -- horizon and bottom of the ground, in units

-- the seven colours of the design (4.2) and one soft grey for small text
local C = {
  ink = rgb(0x0c, 0x08, 0x20), grid = rgb(0x4b, 0x3d, 0x96), e1 = rgb(0x3f, 0xe8, 0xf5),
  e2 = rgb(0xff, 0x4f, 0xae), e3 = rgb(0xff, 0xc5, 0x3d), e4 = rgb(0xf4, 0xf0, 0xff),
  hot = rgb(0xff, 0x6a, 0x3d), dim = rgb(0x2c, 0x23, 0x60), mute = rgb(0xa6, 0x9f, 0xce),
}
local ECOL = { C.e1, C.e2, C.e3, C.e4 }

local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end
local function px(v) return floor(v * S + 0.5) end

-- c blended toward the background: a = 1 keeps c, a = 0 gives ink
local function fade(c, a)
  a = clamp(a, 0, 1)
  local k = C.ink
  local r = floor(((c >> 16) & 255) * a + ((k >> 16) & 255) * (1 - a))
  local g = floor(((c >> 8) & 255) * a + ((k >> 8) & 255) * (1 - a))
  local b = floor((c & 255) * a + (k & 255) * (1 - a))
  return rgb(r, g, b)
end

-- line of width w units (1 unit = S pixels): parallel pixel lines across the minor axis
local function L(x1, y1, x2, y2, c, w)
  local a, b, d, e = px(x1), px(y1), px(x2), px(y2)
  local th = max(1, floor((w or 1) * S + 0.5))
  if th == 1 then line(a, b, d, e, c) return end
  local first = -floor((th - 1) / 2)
  local horiz = abs(d - a) >= abs(e - b)
  for k = first, first + th - 1 do
    if horiz then line(a, b + k, d, e + k, c) else line(a + k, b, d + k, e, c) end
  end
end

-- polyline through {{x, y}, ...}; closed joins the last point to the first
local function PL(p, c, w, closed)
  for i = 1, #p - 1 do L(p[i][1], p[i][2], p[i + 1][1], p[i + 1][2], c, w) end
  if closed and #p > 2 then L(p[#p][1], p[#p][2], p[1][1], p[1][2], c, w) end
end

-- dashed line: on and off lengths in units
local function dash(x1, y1, x2, y2, c, on, off, w)
  local len = sqrt((x2 - x1) ^ 2 + (y2 - y1) ^ 2)
  if len < 0.01 then return end
  local ux, uy = (x2 - x1) / len, (y2 - y1) / len
  local s = 0
  while s < len do
    local e = min(s + on, len)
    L(x1 + ux * s, y1 + uy * s, x1 + ux * e, y1 + uy * e, c, w)
    s = s + on + off
  end
end

-- rectangle: outline of 1 unit, or filled
local function R(x, y, w, h, c, fill)
  local X, Y, W, H = px(x), px(y), max(1, px(w)), max(1, px(h))
  if fill then rectfill(X, Y, W, H, c) return end
  for k = 0, S - 1 do
    if W - 2 * k > 0 and H - 2 * k > 0 then rect(X + k, Y + k, W - 2 * k, H - 2 * k, c) end
  end
end

-- ellipse centred at (x, y); rot turns it (radians) and is built from segments
local function O(x, y, rx, ry, c, fill, rot)
  if not rot or rot == 0 then
    local X, Y, RX, RY = px(x), px(y), max(1, px(rx)), max(1, px(ry))
    if fill then ellipsefill(X, Y, RX, RY, c) return end
    for k = 0, S - 1 do
      if RX - k > 0 and RY - k > 0 then ellipse(X, Y, RX - k, RY - k, c) end
    end
    return
  end
  local pts, flat = {}, {}
  local cr, sr = cos(rot), sin(rot)
  for i = 0, 35 do
    local a = i * PI / 18
    local ex, ey = cos(a) * rx, sin(a) * ry
    local X, Y = x + ex * cr - ey * sr, y + ex * sr + ey * cr
    pts[#pts + 1] = { X, Y }
    flat[#flat + 1] = px(X); flat[#flat + 1] = px(Y)
  end
  if fill then polyfill(flat, c) else PL(pts, c, 1, true) end
end

-- polygon {{x, y}, ...}: filled with fillc if given, outline in c if given
local function poly(p, c, fillc)
  if fillc then
    local flat = {}
    for i = 1, #p do flat[#flat + 1] = px(p[i][1]); flat[#flat + 1] = px(p[i][2]) end
    polyfill(flat, fillc)
  end
  if c then PL(p, c, 1, fillc ~= nil) end
end

-- text with the 8 x 16 font; y is the baseline in units; align "center" or "left"
local function T(s, x, y, c, align)
  local X = px(x)
  if align == "center" then X = X - #s * 4 end
  print(s, X, px(y) - 12, c)
end

local function clipu(x, y, w, h) clip(px(x), px(y), px(w), px(h)) end

-- horizon at HZ with a perspective grid below it; vx is the vanishing column
local function ground(t, vx)
  vx = vx or 160
  L(0, HZ, 320, HZ, C.grid)
  for i = 0, 6 do
    local u = (i + (t * 0.5) % 1) / 7
    local y = HZ + (BOT - HZ) * u * u
    L(0, y, 320, y, C.dim)
  end
  for i = -8, 8 do L(vx + i * 6, HZ, vx + i * 60, BOT, C.dim) end
end

-- the sun: a disc over the horizon, cut by bands that start at cut * r below the centre
local function sun(x, y, r, cut, c)
  clipu(0, 0, 320, HZ)
  O(x, y, r, r, c, true)
  for i = 0, 4 do
    local yy = y + r * (cut + i * 0.2)
    rectfill(px(x - r), px(yy), px(r * 2), max(1, px(1 + i * 0.7)), C.ink)
  end
  clip()
end

-- keys: USB HID usages polled each frame (rawkeys), as in carts/ko2
local K = {
  Q = 0x14, A = 0x04, W = 0x1A, S = 0x16, E = 0x08, D = 0x07, R = 0x15, F = 0x09,
  SPACE = 0x2C, ENTER = 0x28, RIGHT = 0x4F, LEFT = 0x50, SHL = 0xE1, SHR = 0xE5,
  F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D, F5 = 0x3E, F6 = 0x3F, F7 = 0x40, F8 = 0x41,
}
local tracked, held, was = {}, {}, {}
for _, u in pairs(K) do tracked[#tracked + 1] = u end

local function poll()
  for i = 1, #tracked do
    local u = tracked[i]
    was[u] = held[u]
    held[u] = keydown(u)
  end
end
local function pressed(u) return held[u] and not was[u] end
local function shift() return held[K.SHL] or held[K.SHR] end
