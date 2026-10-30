-- Hero Stress (stage 1 of docs/design/hero-stress-cart-and-cel-shader.md).
-- 64 heroes walk in a plaza; the player sets how many are drawn, their polygon
-- level and the 3D options, and reads the cost. The logic of the 64 heroes runs
-- every frame whatever the count, so only the drawing changes with it.
-- This file declares what the others share. Each other file is a `do ... end`.

local H = {
  n = 20, maxh = 64,            -- heroes drawn, heroes in the array
  lod = 5,                      -- 1..4 the models hero0..hero3, 5 auto
  cam = 1, cur = 1, panel = true,
  frame = 0,
  x = {}, y = {}, z = {}, yaw = {}, ry = {}, spd = {}, wp = {},   -- the heroes
  hero = {}, hull = {},         -- the meshes: level 0..3 at 1..4
  lines = {},                   -- the last 120 printed lines
  anim_ms = 0, map_tris = nil,
  scene = 1, fill = 1, ns = 100,    -- the scene (60_scenes.lua), its Fill face and its load
  o2d = 1, n2d = 100, gopt = 1, top = 1,   -- the 2D over the 3D, its amount, the GPU option picked, the first row shown
}
local SKY = 0x8CC8F0
local PI = math.pi
local CX, CZ = 16, 16           -- the centre of the plaza (32 x 32 cells)
local DT = 1 / 60               -- the logic step: fixed, so two runs draw the same frames
local MAX_SKIP = 4            -- most _update before one _draw (frameskip)
local ROWS                      -- the option rows (30_options.lua)

local function fmt(v, d)
  return string.format("%." .. (d or 1) .. "f", v)
end
