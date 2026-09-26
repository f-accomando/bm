-- bm33 native demo (.b33): scrolling tile map over a 1280x720 world,
-- 256 bouncing 16x16 sprites, a player, a HUD with timings.
-- It is also the rendering benchmark: full-screen map + 256 sprites + text.

local W, H = SCREEN_W, SCREEN_H
local WORLD_W, WORLD_H = 160 * 8, 90 * 8
local N = 256
local SPRITES = { 32, 34, 36, 38 }      -- ball, star, ship, heart (16x16)

local objs = {}
local player = { x = 312, y = 172, dir = false }
local cam_x, cam_y = 0, 0
local auto = true

function _init()
  math.randomseed(42)
  for i = 1, N do
    objs[i] = {
      x = math.random(0, W - 16), y = math.random(16, H - 32),
      vx = (math.random() - 0.5) * 4, vy = (math.random() - 0.5) * 4,
      s = SPRITES[i % 4 + 1], fx = i % 3 == 0, fy = i % 7 == 0,
    }
  end
end

local function bounce(o)
  o.x, o.y = o.x + o.vx, o.y + o.vy
  if o.x < 0 or o.x > W - 16 then o.vx = -o.vx; o.x = math.max(0, math.min(W - 16, o.x)) end
  if o.y < 16 or o.y > H - 32 then o.vy = -o.vy; o.y = math.max(16, math.min(H - 32, o.y)) end
end

function _update()
  for i = 1, N do bounce(objs[i]) end

  local dx, dy = 0, 0
  if btn(0) then dx = -2 end
  if btn(1) then dx = 2 end
  if btn(2) then dy = -2 end
  if btn(3) then dy = 2 end
  if dx ~= 0 or dy ~= 0 then auto = false end
  if btnp(4) then                       -- A: scatter the sprites
    for i = 1, N do objs[i].vx, objs[i].vy = -objs[i].vx * 1.5, -objs[i].vy * 1.5 end
  end

  local t = time()
  if auto then                          -- attract mode: the camera wanders
    cam_x = (math.sin(t * 0.35) + 1) / 2 * (WORLD_W - W)
    cam_y = (math.cos(t * 0.23) + 1) / 2 * (WORLD_H - H)
    player.dir = math.cos(t * 0.35) < 0
  else
    cam_x = math.max(0, math.min(WORLD_W - W, cam_x + dx * 2))
    cam_y = math.max(0, math.min(WORLD_H - H, cam_y + dy * 2))
    if dx ~= 0 then player.dir = dx < 0 end
  end
end

local function hud()
  rectfill(0, 0, W, 16, 0x000000)
  print(string.format("bm33 native .b33  %2d fps  cpu %4.1f ms  %2d%%",
        stat(2), stat(1), math.floor(stat(1) / 16.667 * 100)), 0, 0, 0xFFFFFF)
  -- colour check: red, green, blue, white (verifies RGB565 channel order)
  local colours = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF }
  for i, c in ipairs(colours) do rectfill(W - 80 + (i - 1) * 20, 2, 16, 12, c) end

  rectfill(0, H - 24, W, 24, 0x000000)
  print(string.format("%d sprites  map %dx%d  Lua %d KiB  %s", N, WORLD_W, WORLD_H, stat(0),
        auto and "attract" or "arrows/wasd, space"), 0, H - 24, 0xA0A0A0)
end

function _draw()
  -- world layer: only the visible cells (81 x 46)
  camera(cam_x, cam_y)
  local mx, my = cam_x // 8, cam_y // 8
  map(mx, my, mx * 8, my * 8, W // 8 + 1, H // 8 + 1)

  -- screen layer
  camera()
  for i = 1, N do
    local o = objs[i]
    spr(o.s, o.x, o.y, 2, 2, o.fx, o.fy)
  end
  spr(36, player.x, player.y, 2, 2, player.dir)
  circ(player.x + 8, player.y + 8, 14, 0xFFFF00)
  hud()
end
