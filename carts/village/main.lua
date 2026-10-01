-- Studio Village: a little village whose 3D models were made with bm
-- Studio (sdk/studio) and a villager animated with bm Animator
-- (sdk/animator). They live in the cartridge itself (models.bm, packed by
-- make): model("house") gives a mesh, animate() moves the villager.
-- Left / right: turn around the village. Up / down: closer, farther.
-- A: tour on/off. B: day / night. Y: hide the numbers.

local W, H = SCREEN_W, SCREEN_H
local m = {}                              -- the models, by name
local things = {                          -- what stands where: model, x, z, turn, size
  { "house", -3.5, 5.5, 0, 1 },
  { "well", 3, -3, 0.3, 1 },
  { "crates", 4.4, 0.2, 0, 0.8 },
  { "fence", -3.2, -5.4, 0, 1 },
  { "fence", 5.4, 3.4, 1.5708, 1 },
  { "tree", -4.5, -3.5, 0, 1.6 }, { "tree", -2.2, -1.2, 0.7, 1.3 }, { "tree", 4.6, 5.2, 0.3, 1.7 },
  { "tree", 5.2, -5.3, 1.1, 1.5 }, { "tree", 0.2, 5.6, 0.4, 1.4 },
  { "bush", -1.6, -4.4, 0, 1 }, { "bush", 2.3, 4.2, 0.8, 1.2 }, { "bush", 5.3, -1.2, 0.3, 1 },
  { "bush", -5.3, 0.2, 0.5, 1.1 },
}

-- the villager's walk pre-rendered into sprites by bm Animator (the
-- numbers printed by mkmodels.js): 8 frames of 24x32, 4 directions
local WALK = { x = 0, y = 64, w = 24, h = 32, frames = 8, dirs = 4, ax = 12, ay = 29, fps = 10 }

local ang, dist, tour, night, hud = 0.4, 12, true, false, true
local t = 0
-- the villager walks along the path, waves at each end and turns back
local man = { x = -4, dir = 1, wave = 0 }
local LEFT, RIGHT, SPEED = -4.5, 5, 1.6

function _init()
  for _, name in ipairs(models()) do m[name] = model(name) end
end

function _update()
  t = t + 1 / 60
  if btnp(4) then tour = not tour end
  if btnp(5) then night = not night end
  if btnp(7) then hud = not hud end
  if btn(0) then ang = ang - 0.02; tour = false end
  if btn(1) then ang = ang + 0.02; tour = false end
  if btn(2) then dist = math.max(5, dist - 0.1) end
  if btn(3) then dist = math.min(20, dist + 0.1) end
  if tour then ang = ang + 0.004 end
  if man.wave > 0 then
    man.wave = man.wave - 1 / 60
    if man.wave <= 0 then man.dir = -man.dir end
  else
    man.x = man.x + man.dir * SPEED / 60
    if (man.dir > 0 and man.x >= RIGHT) or (man.dir < 0 and man.x <= LEFT) then man.wave = 2.4 end
  end
end

local function draw_villager()
  local v = m.villager
  if not v then return end
  if man.wave > 0 then
    -- from the walk into the wave and back: a mix of the two animations
    local k = math.min(1, (2.4 - man.wave) * 3, man.wave * 3)
    animate(v, "walk", t, "wave", t, k)
  else
    animate(v, "walk", t)
  end
  local ry = man.dir > 0 and -1.5708 or 1.5708                      -- its front (-z) along the path
  draw3d(v, man.x, 0, 2, 0, ry, 0, 1)
  if night then                                                     -- a lantern in its hand
    local hx, hy, hz = bone3d(v, "arm.L")
    local c, s = math.cos(ry), math.sin(ry)
    lamp3d(3, man.x + c * hx + s * hz, hy - 0.4, 2 - s * hx + c * hz, 3, 1.2)
  end
end

function _draw()
  cls(night and 0x0A0C1C or 0x8CC8F0)
  zclear()
  local cx, cz = -math.sin(ang) * dist, -math.cos(ang) * dist
  local cy = 2 + dist * 0.35
  local pitch = -math.atan(cy - 1, dist)
  camera3d(cx, cy, cz, ang, pitch, 64)
  if night then
    light3d(-0.3, 0.8, -0.5, 0.18)
    lamp3d(1, 3, 1.5, -3, 5, 1.2)                -- a lantern by the well
    lamp3d(2, -3.5, 2, 3, 4, 0.9)                -- the windows of the house
    fog3d(0x0A0C1C, 10, 26)
  else
    light3d(-0.4, 0.8, -0.5, 0.45)
    lamp3d()
    fog3d(0x8CC8F0, 16, 34)
  end
  draw3d(m.ground, 0, 0, 0, 0, 0, 0, 1, 1)       -- flat: drawn first, without the z-buffer
  for _, it in ipairs(things) do
    local mesh = m[it[1]]
    if mesh then draw3d(mesh, it[2], 0, it[3], 0, it[4], 0, it[5]) end
  end
  draw_villager()
  if hud then
    print("Studio Village", 4, 4, 0xFFFFFF)
    print(stat(2) .. " fps  " .. stat(4) .. " tri", 4, 20, night and 0x8890A8 or 0x203050)
    print("models: bm Studio  villager: bm Animator", 4, H - 18, night and 0x8890A8 or 0x203050)
    -- the same villager as pre-rendered sprites (2D), facing the way he walks
    local s = WALK
    local dir = man.wave > 0 and 0 or (man.dir > 0 and 1 or 3)
    local f = man.wave > 0 and 0 or math.floor(t * s.fps) % s.frames
    rectfill(W - 34, 4, 30, 38, night and 0x1C2030 or 0xDCEAF4)
    sspr(s.x + f * s.w, s.y + dir * s.h, s.w, s.h, W - 19 - s.ax, 39 - s.ay)
  end
end
