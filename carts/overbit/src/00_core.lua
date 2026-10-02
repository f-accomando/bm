-- Overbit: a hero shooter for bm (M31). Several files joined by build.py;
-- this one stays at the top level: the shared locals, then every other file
-- runs inside its own do ... end.
--
-- World: metres, y up. An actor with yaw a looks along (sin a, 0, cos a);
-- its right is (cos a, 0, -sin a). Pitch > 0 looks up.

local sin, cos, sqrt, abs, floor, max, min, atan, pi = math.sin, math.cos, math.sqrt, math.abs,
  math.floor, math.max, math.min, math.atan, math.pi
local random = math.random
local DT = 1 / 60

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
