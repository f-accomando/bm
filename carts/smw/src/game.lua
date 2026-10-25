-- Super Mario World, first level, for the RGB30 (.b16, 360x360). A personal port: the graphics
-- come from the player's own ROM (scripts/smw_*.py). The movement is in physics.lua.
local VIEW_W, VIEW_H = 256, 224          -- the SNES picture, shown whole inside the 360x360 screen
local VX, VY = (360 - VIEW_W) // 2, (360 - VIEW_H) // 2
local LEVEL_H = 27 * 16
local START_X, START_Y = 16, 352

local level_w
local mario, prev, acc
local camx, camy = 0, 192
local backdrop = 0xFF0000

function _init()
  local mw, mh = msize()
  level_w = mw * 8
  mario = Phys.new(START_X, START_Y)
  prev = { x = mario.x, y = mario.y }
  acc = 0
end

local function solid(px, py)
  if px < 0 or px >= level_w then return true end
  if py < 0 then return false end
  return mflags(px, py, 1, 1, "col") & 1 ~= 0
end

local function inputs()
  return {
    left = btn("left"), right = btn("right"),
    run = btn("y") or btn("x"),
    jump = btn("b") or btn("a"),
  }
end

function _update()
  acc = acc + 50 / 60
  local inp = inputs()
  while acc >= 1 do
    acc = acc - 1
    prev.x, prev.y = mario.x, mario.y
    Phys.step(mario, inp, solid)
    if mario.y > LEVEL_H + 32 then          -- fell out of the level
      mario = Phys.new(START_X, START_Y)
      prev.x, prev.y = mario.x, mario.y
    end
  end
end

local function draw_mario(x, y)
  -- placeholder until the sprites are captured from the ROM: a 12x16 body on the real box
  local f = mario.face
  rectfill(x + 2, y + 16, 12, 16, 0xE03020)
  rectfill(x + 2, y + 16, 12, 5, 0xE03020)
  rectfill(x + 3, y + 21, 10, 5, 0xF8C090)
  rectfill(x + (f > 0 and 9 or 3), y + 22, 3, 2, 0x202020)
  rectfill(x + 2, y + 26, 12, 6, 0x3050D0)
end

function _draw()
  cls(0)
  local a = acc
  local x = prev.x + (mario.x - prev.x) * a
  local y = prev.y + (mario.y - prev.y) * a
  camx = math.max(0, math.min(level_w - VIEW_W, math.floor(x) - 108))
  camy = math.max(0, math.min(LEVEL_H - VIEW_H, math.floor(y) - 160))
  clip(VX, VY, VIEW_W, VIEW_H)
  rectfill(VX, VY, VIEW_W, VIEW_H, backdrop)
  local mx, my = camx // 8, camy // 8
  map(mx, my, VX - (camx % 8), VY - (camy % 8), VIEW_W // 8 + 1, VIEW_H // 8 + 1, "main")
  draw_mario(VX + math.floor(x) - camx, VY + math.floor(y) - camy)
  clip()
  rect(VX - 1, VY - 1, VIEW_W + 2, VIEW_H + 2, 0x303030)
  print(string.format("x %d  vx %.0f  P %d", math.floor(mario.x), mario.vx, math.floor(mario.pm)), 8, 8, 0x808080)
end
