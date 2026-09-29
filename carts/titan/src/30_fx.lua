-- Effects: hit sparks, the guns' bullets, muzzle flashes, the sword's
-- trail, booster flames, dust, smoke, armour flying off and explosions.
-- Everything lives in arena coordinates (x along the arena, h the height
-- over the floor) and is drawn where the camera says. Two lists: behind the
-- robots (flames, dust) and in front of them (the rest).

local back, front = {}, {}

-- the frames of an animated sprite, by name
local SEQ = {}
local function seq(name, n)
  if not SEQ[name] then
    local t = {}
    for k = 0, n - 1 do t[#t + 1] = name .. k end
    SEQ[name] = t
  end
  return SEQ[name]
end

local function add(list, p)
  list[#list + 1] = p
  return p
end

function Fx.clear()
  for i = #back, 1, -1 do back[i] = nil end
  for i = #front, 1, -1 do front[i] = nil end
end

function Fx.count() return #back + #front end

-- an animated sprite: `names` played `tick` frames each
local function anim(list, names, tick, x, h, flip, vx, vh, delay)
  return add(list, { kind = "anim", names = names, tick = tick, t = -(delay or 0), x = x, h = h,
                     flip = flip, vx = vx or 0, vh = vh or 0 })
end

---------------------------------------------------------------- hits

-- kind: "spark" (a hit), "bspark" (a guard, blue), "bigspark" (a heavy hit)
function Fx.spark(x, h, kind)
  anim(front, seq(kind, 4), kind == "bigspark" and 3 or 2, x, h, random(2) == 1)
  -- a few hot bits fly off
  for _ = 1, kind == "bigspark" and 8 or 4 do
    add(front, { kind = "bit", x = x, h = h, vx = (random() - 0.5) * 9, vh = random() * 6,
                 c = kind == "bspark" and 0xBFEAFF or 0xFFE070, t = 0, life = 10 + random(10) })
  end
end

-- an explosion, after `delay` frames
function Fx.boom(x, h, delay)
  anim(front, seq("boom", 5), 3, x, h, random(2) == 1, 0, 0.3, delay)
end

-- the robot is beaten: explosions over its body for a second
function Fx.ko(f)
  for i = 0, 6 do
    Fx.boom(f.x + random(-50, 50), f.y + random(20, 170), i * 8)
  end
end

---------------------------------------------------------------- the guns

function Fx.bullet(f, x, h)
  add(front, { kind = "bullet", f = f, x = x, h = h, vx = f.face * Data.BULLET.speed, t = 0 })
end

function Fx.muzzle(x, h, face)
  anim(front, seq("muzzle", 2), 2, x, h, face < 0)
end

-- a bullet flies until it hits the other robot or leaves the screen
local function bullet(p, fighters)
  p.x = p.x + p.vx
  if p.x < G.camx - 40 or p.x > G.camx + W + 40 then return false end
  local o = fighters[1] == p.f and fighters[2] or fighters[1]
  if not o then return true end
  local hx, hy = Fighter.hits(o, p.x - 9, p.h - 3, p.x + 9, p.h + 3)
  if not hx or (o.state == "air" and o.juggle >= 4) then return true end
  -- the hit stops the one hit, not the one shooting
  local stop = p.f.hitstop
  Fighter.take_hit(o, p.f, Data.BULLET, hx, hy)
  p.f.hitstop = stop
  return false
end

---------------------------------------------------------------- the sword

-- the cut of the sword: a crescent of light around the shoulder that
-- grows as the blade comes down (k = 0, 1), then fades
function Fx.trail(f, k)
  for _, p in ipairs(front) do
    if p.kind == "trail" and p.f == f then
      p.a1, p.t = k == 0 and 15 or -45, 0
      return
    end
  end
  add(front, { kind = "trail", f = f, a0 = 105, a1 = k == 0 and 15 or -45, t = 0, life = 14 })
end

-- bands of the crescent, from the outer edge in: white, pale, blue, dark
local TRAIL_BANDS = { { 0, 0.14, 0xFFFFFF }, { 0.14, 0.4, 0xB0F4FF }, { 0.4, 0.82, 0x48B8F0 },
                      { 0.82, 1, 0x1A5A8A } }

local function draw_trail(p)
  local f = p.f
  local fade = max(0, p.t - 4) / (p.life - 4)
  local a0 = p.a0 + (p.a1 - p.a0) * fade      -- the tail catches up with the blade
  local a1 = p.a1
  local cx = floor(f.x - G.camx + f.face * -2)
  local cy = floor(GROUND - f.y - 150 + G.sy)
  local R, T = 156, 64 * (1 - fade * 0.7)
  local n = 12
  local sgn = f.face
  -- the points of each band's edges, then two triangles per piece
  for _, b in ipairs(TRAIL_BANDS) do
    local px0, py0, px1, py1
    for i = 0, n do
      local u = i / n
      local a = (a0 + (a1 - a0) * u) * pi / 180
      local w = T * u ^ 0.7                  -- thin at the tail, thick where the blade is now
      local r0, r1 = R - w * b[1], R - w * b[2]
      local ca, sa = cos(a), sin(a)
      local x0, y0 = cx + sgn * r0 * ca, cy - r0 * sa
      local x1, y1 = cx + sgn * r1 * ca, cy - r1 * sa
      if px0 then
        tri(px0, py0, x0, y0, px1, py1, b[3])
        tri(x0, y0, x1, y1, px1, py1, b[3])
      end
      px0, py0, px1, py1 = x0, y0, x1, y1
    end
  end
end

---------------------------------------------------------------- boosters, dust, smoke

-- the two nozzles of the booster pack, on the robot's back
function Fx.flames(f)
  local flip = f.face < 0
  for _, dx in ipairs({ -10, -26 }) do
    anim(back, seq("flame", 3), 2, f.x + f.face * dx, f.y + 104 + (dx == -10 and 0 or 3), flip, -f.vx * 0.2, -1)
  end
  add(back, { kind = "smoke", x = f.x - f.face * 20, h = f.y + 84, vx = -f.face * 0.8, r = 3, t = 0, life = 22,
              c = 0x6A6A72 })
end

function Fx.dust(x)
  anim(back, seq("dust", 4), 4, x - 24, 0, false, -1.2)
  anim(back, seq("dust", 4), 4, x + 24, 0, true, 1.2)
end
SEQ.dust = { "dust1", "dust2", "dust3", "dust3" }

function Fx.smoke(x, h)
  add(front, { kind = "smoke", x = x + random(-6, 6), h = h, vx = 0.3, r = 3, t = 0, life = 40, c = 0x8C8C94 })
end

---------------------------------------------------------------- armour

-- the shoulder armour cracks: bits of it fly
function Fx.burst(f, col)
  local x, h = f.x - f.face * 13, f.y + 150
  anim(front, seq("bspark", 4), 2, x, h, false)
  for _ = 1, 12 do
    add(front, { kind = "bit", x = x, h = h, vx = (random() - 0.5) * 8, vh = 2 + random() * 5,
                 c = random(2) == 1 and col or 0xE8F0FF, t = 0, life = 20 + random(15) })
  end
end

-- the shoulder armour is gone: the plate flies off, bounces on the street
-- and lies there for a while
function Fx.shoulder_off(f)
  local fr = Fighter.art(f)
  local list = f.cfg.armor == "heavy" and fr.shd or fr.sd
  if list then
    local low = -9999
    for _, r in ipairs(list) do low = max(low, r[6] + r[4]) end
    add(front, { kind = "plate", list = list, x = f.x, h = f.y, low = -low, vx = -f.face * 3.5, vh = 7,
                 flip = f.face < 0, t = 0, life = 300 })
  end
  Fx.boom(f.x - f.face * 13, f.y + 150, 0)
  Fx.burst(f, 0xFFB43C)
  G.shake = max(G.shake, 6)
end

---------------------------------------------------------------- update and draw

local function step(p, fighters)
  p.t = p.t + 1
  local k = p.kind
  if k == "anim" then
    if p.t < 0 then return true end
    p.x, p.h = p.x + p.vx, p.h + p.vh
    return p.t < #p.names * p.tick
  elseif k == "bullet" then
    return bullet(p, fighters)
  elseif k == "bit" then
    p.x, p.h = p.x + p.vx, p.h + p.vh
    p.vh = p.vh - 0.45
    if p.h < 0 then p.h, p.vh, p.vx = 0, -p.vh * 0.3, p.vx * 0.6 end
    return p.t < p.life
  elseif k == "smoke" then
    p.x, p.h = p.x + p.vx, p.h + 0.9
    p.r = p.r + 0.2
    return p.t < p.life
  elseif k == "trail" then
    return p.t < p.life
  elseif k == "plate" then
    if p.vh ~= 0 or p.h + p.low > 0 then
      p.x, p.h = p.x + p.vx, p.h + p.vh
      p.vh = p.vh - GRAV
      if p.h + p.low <= 0 then
        p.h = -p.low
        if p.vh < -2.5 then
          p.vh, p.vx = -p.vh * 0.35, p.vx * 0.6
          Fx.dust(p.x)
        else
          p.vh, p.vx = 0, 0
        end
      end
    end
    return p.t < p.life
  end
  return false
end

local function update_list(list, fighters)
  local j = 0
  for i = 1, #list do
    local p = list[i]
    if step(p, fighters) then
      j = j + 1
      list[j] = p
    end
  end
  for i = #list, j + 1, -1 do list[i] = nil end
end

function Fx.update(fighters)
  update_list(back, fighters)
  update_list(front, fighters)
end

local function draw_one(p)
  local k = p.kind
  if k == "trail" then return draw_trail(p) end
  local x, y = floor(p.x - G.camx), floor(GROUND - p.h + G.sy)
  if k == "anim" then
    if p.t >= 0 then sprite(p.names[p.t // p.tick + 1], x, y, p.flip) end
  elseif k == "bullet" then
    local s = sign(p.vx)
    local tail = x - s * 22
    rectfill(min(x, tail), y - 1, 22, 3, 0xEE641A)
    rectfill(min(x, x - s * 12), y - 1, 12, 2, 0xFFB43C)
    rectfill(min(x, x - s * 5), y - 1, 5, 2, 0xFFF0A8)
  elseif k == "bit" then
    rectfill(x, y, 2, 2, p.c)
  elseif k == "smoke" then
    local r = floor(p.r)
    local c = p.t > p.life * 0.6 and 0x4C4C54 or p.c
    circfill(x, y, r, c)
    if r > 3 then circfill(x - 1, y - 1, r - 2, p.t > p.life * 0.6 and 0x5A5A62 or 0x8C8C94) end
  elseif k == "plate" then
    -- it blinks before it goes
    if p.t < p.life - 60 or p.t % 8 < 4 then pieces(p.list, x, y, p.flip) end
  end
end

function Fx.draw_back()
  for i = 1, #back do draw_one(back[i]) end
end

function Fx.draw_front()
  for i = 1, #front do draw_one(front[i]) end
end
