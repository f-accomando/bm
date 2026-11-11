-- bm Tape: palette, drawing helpers in units of a 320 x 160 display, key polling.
-- The scene is drawn at SCL pixels per unit, from the pixel (OX, OY): 1280 x 720 gives SCL 3 (960 x 360 for the scene,
-- the rest is the screen of 38_frame.lua and the keyboard map of 39_keys.lua); 640 x 360 gives half of that.
local floor, sin, cos, abs, max, min, sqrt, exp = math.floor, math.sin, math.cos, math.abs, math.max, math.min, math.sqrt, math.exp
local PI = math.pi
local SCL, OX, OY = 3, 0, 0
local HZ, BOT = 92, 134          -- horizon and bottom of the ground, in units

-- the colours of the design (4.2), one soft grey for small text, and two for the keyboard map: nav (violet) and pad (mint)
local C = {
  ink = rgb(0x0c, 0x08, 0x20), grid = rgb(0x4b, 0x3d, 0x96), e1 = rgb(0x3f, 0xe8, 0xf5),
  e2 = rgb(0xff, 0x4f, 0xae), e3 = rgb(0xff, 0xc5, 0x3d), e4 = rgb(0xf4, 0xf0, 0xff),
  hot = rgb(0xff, 0x6a, 0x3d), dim = rgb(0x2c, 0x23, 0x60), mute = rgb(0xa6, 0x9f, 0xce),
  nav = rgb(0x9b, 0x7b, 0xff), pad = rgb(0x5c, 0xff, 0xb0), key = rgb(0x18, 0x12, 0x3a),
}
local ECOL = { C.e1, C.e2, C.e3, C.e4 }

local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end
local function px(v) return floor(v * SCL + 0.5) end
local function X(v) return floor(v * SCL + 0.5) + OX end      -- a place on the screen, in pixels
local function Y(v) return floor(v * SCL + 0.5) + OY end

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
  local a, b, d, e = X(x1), Y(y1), X(x2), Y(y2)
  local th = max(1, floor((w or 1) * SCL + 0.5))
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
  local x0, y0, W, H = X(x), Y(y), max(1, px(w)), max(1, px(h))
  if fill then rectfill(x0, y0, W, H, c) return end
  for k = 0, floor(SCL + 0.5) - 1 do
    if W - 2 * k > 0 and H - 2 * k > 0 then rect(x0 + k, y0 + k, W - 2 * k, H - 2 * k, c) end
  end
end

-- ellipse centred at (x, y); rot turns it (radians) and is built from segments
local function O(x, y, rx, ry, c, fill, rot)
  if not rot or rot == 0 then
    local x0, y0, RX, RY = X(x), Y(y), max(1, px(rx)), max(1, px(ry))
    if fill then ellipsefill(x0, y0, RX, RY, c) return end
    for k = 0, floor(SCL + 0.5) - 1 do
      if RX - k > 0 and RY - k > 0 then ellipse(x0, y0, RX - k, RY - k, c) end
    end
    return
  end
  local pts, flat = {}, {}
  local cr, sr = cos(rot), sin(rot)
  for i = 0, 35 do
    local a = i * PI / 18
    local ex, ey = cos(a) * rx, sin(a) * ry
    local qx, qy = x + ex * cr - ey * sr, y + ex * sr + ey * cr
    pts[#pts + 1] = { qx, qy }
    flat[#flat + 1] = X(qx); flat[#flat + 1] = Y(qy)
  end
  if fill then polyfill(flat, c) else PL(pts, c, 1, true) end
end

-- polygon {{x, y}, ...}: filled with fillc if given, outline in c if given
local function poly(p, c, fillc)
  if fillc then
    local flat = {}
    for i = 1, #p do flat[#flat + 1] = X(p[i][1]); flat[#flat + 1] = Y(p[i][2]) end
    polyfill(flat, fillc)
  end
  if c then PL(p, c, 1, fillc ~= nil) end
end

-- text with the 8 x 16 font; y is the baseline in units; align "center" or "left"
local function T(s, x, y, c, align)
  local x0 = X(x)
  if align == "center" then x0 = x0 - #s * 4 end
  print(s, x0, Y(y) - 12, c)
end

local function clipu(x, y, w, h) clip(X(x), Y(y), px(w), px(h)) end
-- a filled rectangle in units (a band over the sun)
local function RF(x, y, w, h, c) rectfill(X(x), Y(y), max(1, px(w)), max(1, px(h)), c) end

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
    RF(x - r, yy, r * 2, 1 + i * 0.7, C.ink)
  end
  clip()
end

-- keys: USB HID usages polled each frame (rawkeys), as in carts/ko2. The map is for an Apple Magic Keyboard (ANSI, no
-- numpad): modules on F1..F8, tracks 1..4, punch-in 5..8, encoders 9 0 - = (up)
-- and O P [ ] (down), piano on Z..M and S D G H J L ;, SET on A, octave on F and K (design 5).
local K = {
  Q = 0x14, A = 0x04, W = 0x1A, S = 0x16, E = 0x08, D = 0x07, R = 0x15, F = 0x09, T = 0x17,
  U = 0x18, I = 0x0C, O = 0x12, J = 0x0D, K = 0x0E, L = 0x0F, M = 0x10, COMMA = 0x36, DOT = 0x37,
  P = 0x13, N = 0x11, Z = 0x1D, X = 0x1B, C = 0x06, V = 0x19, SEMI = 0x33, QUOTE = 0x34,
  D1 = 0x1E, D2 = 0x1F, D3 = 0x20, D4 = 0x21, D5 = 0x22, D6 = 0x23, D7 = 0x24, D8 = 0x25, D9 = 0x26, D0 = 0x27,
  F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D, F5 = 0x3E, F6 = 0x3F, F7 = 0x40, F8 = 0x41,
  Y = 0x1C, B = 0x05, G = 0x0A, H = 0x0B, SLASH = 0x38, BSLASH = 0x31, ALTL = 0xE2, ALTR = 0xE6, GUIL = 0xE3, GUIR = 0xE7,
  SPACE = 0x2C, ENTER = 0x28, BKSP = 0x2A, TAB = 0x2B, MINUS = 0x2D, EQUALS = 0x2E, LB = 0x2F, RB = 0x30,
  RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52, SHL = 0xE1, SHR = 0xE5, CTL = 0xE0, CTR = 0xE4,
}
local ENC = { { K.D9, K.O }, { K.D0, K.P }, { K.MINUS, K.LB }, { K.EQUALS, K.RB } }   -- encoders 1..4: up key, down key
local FKEYS = { K.F1, K.F2, K.F3, K.F4, K.F5, K.F6, K.F7, K.F8 }   -- the eight modules
local TRKEYS = { K.D1, K.D2, K.D3, K.D4 }
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
local function released(u) return was[u] and not held[u] end
local function shift() return held[K.SHL] or held[K.SHR] end
local function ctrl() return held[K.CTL] or held[K.CTR] end
