-- Chaos Kitchen: a cooperative cooking game for 1-4 players on one screen.
-- Orders come in, the chefs fetch, chop, cook and plate, and serve before
-- the customers lose patience; the kitchens get stranger as the campaign
-- goes on. Endless mode grows the kitchen while you play.
--
-- The source is split in carts/kitchen/src/*.lua, joined by build.py: this
-- first file holds the shared locals, every other file is a `do ... end`
-- block that fills the module tables below.

local W, H = SCREEN_W, SCREEN_H
local DT = 1 / 60
local floor, ceil, sqrt, abs = math.floor, math.ceil, math.sqrt, math.abs
local min, max, sin, cos, atan = math.min, math.max, math.sin, math.cos, math.atan
local random, pi = math.random, math.pi
local TAU = pi * 2
local fmt, sub, byte = string.format, string.sub, string.byte
local insert, remove, concat, sort = table.insert, table.remove, table.concat, table.sort

-- modules, filled by the other files
local Data = {}     -- ingredients, recipes, stations, chefs, stages, upgrades, disasters
local Mesh = {}     -- 3D models
local Snd = {}      -- sound effects and music
local Kit = {}      -- the kitchen: grid, stations, floor items
local Food = {}     -- items, keys, recipes matching, cooking
local Chef = {}     -- the chefs: movement, actions, animation
local Ord = {}      -- orders, patience, combo, score
local Haz = {}      -- hazards: platforms, conveyors, ice, doors, vents
local Dis = {}      -- disasters: rats, ducks, ghosts, tornado...
local End = {}      -- endless mode: money, register, upgrades
local Bot = {}      -- computer chefs (title screen, tests)
local Ren = {}      -- camera and 3D drawing
local Hud = {}      -- icons, order cards, texts
local Fx = {}       -- particles, floating texts, shake
local Scr = {}      -- screens: title, menus, lobby, results
local Save = {}     -- progress on the SD card
local Pad = {}      -- input per player

-- The game state shared by all modules (see Scr.start_stage).
local G = {
  t = 0,            -- seconds in the current screen
  frame = 0,
  screen = "boot",
  players = {},     -- joined players: { pad = 1..4, chef = 1..4 }
  run = nil,        -- the kitchen being played
  debug = false,
}

-- the colours of players 1-4 (the same as the DS4 lights)
local PCOL = { 0x3C8CFF, 0xFF5040, 0x40D060, 0xFF60C0 }
local PCOL_DARK = { 0x1A3E78, 0x782418, 0x1C6A2C, 0x782E5C }

---------------------------------------------------------------- utilities

local function clamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
local function lerp(a, b, t) return a + (b - a) * t end
local function approach(v, target, step)
  if v < target then return min(v + step, target) end
  return max(v - step, target)
end
local function sign(v) return v > 0 and 1 or v < 0 and -1 or 0 end
local function round(v) return floor(v + 0.5) end
local function dist2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz return dx * dx + dz * dz end
local function rnd(a, b) return a + random() * (b - a) end
local function choose(t) return t[random(#t)] end
local function shuffle(t)
  for i = #t, 2, -1 do local j = random(i); t[i], t[j] = t[j], t[i] end
  return t
end
-- shortest signed difference between two angles
local function angdiff(a, b)
  local d = (b - a) % TAU
  if d > pi then d = d - TAU end
  return d
end
local function ease(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end
local function mix_rgb(c1, c2, t)
  local r1, g1, b1 = c1 >> 16 & 255, c1 >> 8 & 255, c1 & 255
  local r2, g2, b2 = c2 >> 16 & 255, c2 >> 8 & 255, c2 & 255
  return floor(r1 + (r2 - r1) * t) << 16 | floor(g1 + (g2 - g1) * t) << 8 | floor(b1 + (b2 - b1) * t)
end
local function shade(c, k)
  local r, g, b = (c >> 16 & 255) * k, (c >> 8 & 255) * k, (c & 255) * k
  return min(255, floor(r)) << 16 | min(255, floor(g)) << 8 | min(255, floor(b))
end
local function fmt_time(s)
  s = max(0, floor(s))
  return fmt("%d:%02d", s // 60, s % 60)
end
local function copy(t)
  local c = {}
  for k, v in pairs(t) do c[k] = v end
  return c
end

-- text helpers (the font is 8x16; scale multiplies it)
local function text_w(s, scale) return #s * 8 * (scale or 1) end
local function text_c(s, cx, y, c, scale)
  print(s, floor(cx - text_w(s, scale) / 2), y, c, scale)
end
local function text_shadow(s, x, y, c, scale, sc)
  scale = scale or 1
  print(s, x + scale, y + scale, sc or 0x000000, scale)
  print(s, x, y, c, scale)
end
local function text_cs(s, cx, y, c, scale, sc)
  text_shadow(s, floor(cx - text_w(s, scale) / 2), y, c, scale, sc)
end

-- a rounded panel
local function panel(x, y, w, h, c, border)
  rectfill(x + 2, y, w - 4, h, c)
  rectfill(x, y + 2, w, h - 4, c)
  rectfill(x + 1, y + 1, w - 2, h - 2, c)
  if border then
    line(x + 2, y, x + w - 3, y, border)
    line(x + 2, y + h - 1, x + w - 3, y + h - 1, border)
    line(x, y + 2, x, y + h - 3, border)
    line(x + w - 1, y + 2, x + w - 1, y + h - 3, border)
    pset(x + 1, y + 1, border); pset(x + w - 2, y + 1, border)
    pset(x + 1, y + h - 2, border); pset(x + w - 2, y + h - 2, border)
  end
end

local function bar(x, y, w, h, t, fg, bg)
  rectfill(x, y, w, h, bg)
  local f = floor(w * clamp(t, 0, 1))
  if f > 0 then rectfill(x, y, f, h, fg) end
end

---------------------------------------------------------------- input

-- Buttons: A interact, B dash, X chop/use, Y throw, Start pause.
local BA, BB, BX, BY, BSTART = 4, 5, 6, 7, 8

-- Pad.held(p, b) / Pad.hit(p, b): player p's controller (1-4); p = nil any.
function Pad.held(p, b) return btn(b, p) end
function Pad.hit(p, b) return btnp(b, p) end
-- menu navigation with auto repeat: -1, 0, 1 for up/down or left/right
local rep = {}
function Pad.nav(p, axis)
  local key = (p or 0) * 2 + (axis == "x" and 1 or 0)
  local neg, pos = axis == "x" and 0 or 2, axis == "x" and 1 or 3
  local d = btn(neg, p) and -1 or btn(pos, p) and 1 or 0
  local r = rep[key]
  if d == 0 then rep[key] = nil return 0 end
  if not r or r.d ~= d then rep[key] = { d = d, t = 0.35 } return d end
  r.t = r.t - DT
  if r.t <= 0 then r.t = 0.09 return d end
  return 0
end
function Pad.stick(p)
  local x, y = stick(p)
  return x, y
end
