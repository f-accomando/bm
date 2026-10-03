-- Overbit: a hero shooter for bm (M38). Several files joined by build.py;
-- this one stays at the top level: the shared locals, then every other file
-- runs inside its own do ... end.
--
-- World: metres, y up. An actor with yaw a looks along (sin a, 0, cos a);
-- its right is (cos a, 0, -sin a). Pitch > 0 looks up.

local sin, cos, sqrt, abs, floor, max, min, atan, pi = math.sin, math.cos, math.sqrt, math.abs,
  math.floor, math.max, math.min, math.atan, math.pi
local random = math.random      -- for the looks (particles, shakes)

-- for the game: the same numbers on every console of a match on the network
-- (lockstep, 83_net), seeded with the match: xorshift32, as math.random:
-- grandom() in [0, 1), grandom(n) in 1..n, grandom(a, b) in a..b
local rng = 0x2545F491
local function grandom(a, b)
  local x = rng
  x = x ~ ((x << 13) & 0xFFFFFFFF)
  x = x ~ (x >> 17)
  x = x ~ ((x << 5) & 0xFFFFFFFF)
  rng = x
  local u = x / 4294967296.0
  if not a then return u end
  if not b then a, b = 1, a end
  return a + math.floor(u * (b - a + 1))
end
local function grandom_seed(s) rng = (s % 4294967295) + 1 end
local DT = 1 / 60

-- The screen: the cartridge's resolution (480x270 from its header; the
-- RESOLUTION of the menu changes it with screen(), 320x180 to 1920x1080).
--  SW, SH    its pixels
--  ZOOM      how many of them make one of the 320x180 the game was first
--            drawn at: the things of the world drawn in 2D (sun, flashes)
--  UI        how many make a pixel of the HUD and the menus: 1 up to 640x360,
--            2 at 960x540 and 1280x720, 4 at 1920x1080 (the same size on the
--            TV as at 480x270), drawn on a screen of LW x LH with the
--            functions below (urectfill, uprint... in its coordinates)
local SW, SH, ZOOM, UI, LW, LH
local urectfill, urect, upset, uline, ucirc, ucircfill, utri, uprint, uprompt

local function screen_size()
  SW, SH = SCREEN_W or 480, SCREEN_H or 270
  ZOOM = SW / 320
  UI = math.max(1, SH // 270)
  LW, LH = SW // UI, SH // UI
  if UI == 1 then
    urectfill, urect, upset, uline, ucirc, ucircfill, utri, uprint = rectfill, rect, pset, line, circ, circfill, tri,
      print
    uprompt = prompt
    return
  end
  local S, H = UI, UI // 2
  urectfill = function(x, y, w, h, c) rectfill(x * S, y * S, w * S, h * S, c) end
  urect = function(x, y, w, h, c)                 -- edges S pixels thick
    rectfill(x * S, y * S, w * S, S, c)
    rectfill(x * S, (y + h - 1) * S, w * S, S, c)
    rectfill(x * S, y * S, S, h * S, c)
    rectfill((x + w - 1) * S, y * S, S, h * S, c)
  end
  upset = function(x, y, c) rectfill(x * S, y * S, S, S, c) end
  uline = function(x0, y0, x1, y1, c)             -- S lines side by side
    local dx, dy = abs(x1 - x0), abs(y1 - y0)
    for k = 0, S - 1 do
      if dx >= dy then line(x0 * S + H, y0 * S + k, x1 * S + H, y1 * S + k, c)
      else line(x0 * S + k, y0 * S + H, x1 * S + k, y1 * S + H, c) end
    end
  end
  ucirc = function(x, y, r, c)
    for k = 0, S - 1 do circ(x * S + H, y * S + H, r * S - H + k, c) end
  end
  ucircfill = function(x, y, r, c) circfill(x * S + H, y * S + H, r * S + H, c) end
  utri = function(x0, y0, x1, y1, x2, y2, c) tri(x0 * S, y0 * S, x1 * S, y1 * S, x2 * S, y2 * S, c) end
  uprint = function(s, x, y, c, k) return print(s, x * S, y * S, c, (k or 1) * S) // S end
  -- prompt(name, x, y, small) drawn; prompt(name [, small]) measures (in
  -- the HUD's pixels, as before)
  uprompt = function(n, x, y, small)
    if type(x) ~= "number" then return prompt(n, x) end
    return prompt(n, x * S, y * S, small, S) // S
  end
end
screen_size()

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function lerp(a, b, t) return a + (b - a) * t end
local function approach(v, target, step)
  if v < target then return min(v + step, target) end
  return max(v - step, target)
end
local function len3(x, y, z) return sqrt(x * x + y * y + z * z) end
local function wrap_angle(a)
  a = a % (2 * pi)
  if a > pi then a = a - 2 * pi end
  return a
end
local function smooth01(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end

-- the game's state, shared by every file
local G = {
  t = 0,                -- seconds of game (60 steps a second)
  frame = 0,
  actors = {},          -- every hero in the match (players and bots)
  local_actor = nil,    -- the one the screen follows
  mode = "range",       -- "range", "reel", "menu"
  quality = 3,          -- 0 lowest .. 4 highest (see 85_quality)
  qauto = true,
  dev = false,          -- the dev overlay
  paused = false,
}

local H = {}            -- hero definitions, by id (50_*.lua)
local HERO_ORDER = {}   -- their ids in roster order

-- forward declarations filled in by later files
local World, Fx, Actors, Proj, Props, Hud, Cam, Snd, Input, Quality, Modes, Dev

-- colours of the teams: 1 = the player's side (blue), 2 = the other (red)
local TEAM_RGB = { 0x46B4FF, 0xFF4646 }
