-- Particles and floating texts, in world coordinates, drawn in 2D over the
-- 3D picture (projected every frame). A fixed pool: no garbage per frame.

local MAXP = 90
local parts = {}
for i = 1, MAXP do parts[i] = { life = 0 } end
local texts = {}
local next_p = 1

local function spawn(x, y, z, vx, vy, vz, life, color, size, grav, kind)
  local p = parts[next_p]
  next_p = next_p % MAXP + 1
  p.x, p.y, p.z, p.vx, p.vy, p.vz = x, y, z, vx, vy, vz
  p.life, p.t, p.color, p.size, p.grav, p.kind = life, life, color, size, grav or 0, kind
  return p
end

function Fx.clear()
  for i = 1, MAXP do parts[i].life = 0 end
  texts = {}
end

function Fx.puff(x, y, z, color, n)
  for _ = 1, n or 4 do
    spawn(x + rnd(-0.15, 0.15), y, z + rnd(-0.15, 0.15), rnd(-0.3, 0.3), rnd(0.6, 1.3), rnd(-0.3, 0.3),
          rnd(0.5, 0.9), color, rnd(3, 6), -0.4, "puff")
  end
end

function Fx.steam(x, y, z)
  spawn(x + rnd(-0.12, 0.12), y, z + rnd(-0.12, 0.12), rnd(-0.1, 0.1), rnd(0.5, 0.9), 0,
        rnd(0.6, 1.0), 0xF0F0F0, rnd(2, 4), -0.2, "puff")
end

function Fx.bits(x, y, z, color)
  for _ = 1, 6 do
    spawn(x, y, z, rnd(-1.2, 1.2), rnd(1.5, 2.8), rnd(-1.2, 1.2), rnd(0.35, 0.6), color, 2, 9, "bit")
  end
end

function Fx.sizzle(x, z)
  spawn(x + rnd(-0.2, 0.2), Kit.TOP + 0.1, z + rnd(-0.2, 0.2), rnd(-0.6, 0.6), rnd(1.2, 2.0), rnd(-0.6, 0.6),
        rnd(0.2, 0.35), random() < 0.5 and 0xFFF0B0 or 0xFFFFFF, 2, 9, "bit")
end

function Fx.dust(x, z)
  spawn(x + rnd(-0.1, 0.1), 0.05, z + rnd(-0.1, 0.1), rnd(-0.4, 0.4), rnd(0.2, 0.5), rnd(-0.4, 0.4),
        rnd(0.25, 0.45), 0xE8E0D0, rnd(2, 4), 0, "puff")
end

function Fx.splash(x, z)
  for _ = 1, 10 do
    spawn(x, 0, z, rnd(-1.5, 1.5), rnd(2, 4), rnd(-1.5, 1.5), rnd(0.4, 0.7), 0xC8E8FF, rnd(2, 3), 10, "bit")
  end
end

function Fx.flame(x, z)
  if random() < 0.5 then
    spawn(x + rnd(-0.25, 0.25), Kit.TOP + 0.2, z + rnd(-0.25, 0.25), 0, rnd(0.8, 1.6), 0,
          rnd(0.3, 0.5), random() < 0.5 and 0xFF8020 or 0xFFD040, rnd(3, 5), -0.5, "puff")
  end
  if random() < 0.15 then Fx.puff(x, Kit.TOP + 0.6, z, 0x404040, 1) end
end

function Fx.spray(x, z, fx, fz)
  spawn(x, 0.6, z, fx * rnd(3, 4.5) + rnd(-0.4, 0.4), rnd(-0.2, 0.4), fz * rnd(3, 4.5) + rnd(-0.4, 0.4),
        rnd(0.3, 0.45), 0xF0F8FF, rnd(3, 5), 1, "puff")
end

function Fx.sparkle(x, y, z, color)
  for _ = 1, 8 do
    local a = random() * TAU
    spawn(x, y, z, cos(a) * 1.5, rnd(1, 2.5), sin(a) * 1.5, rnd(0.4, 0.7), color or 0xFFE060, 2, 3, "star")
  end
end

-- a text that rises from a point in the kitchen
function Fx.text(x, z, s, color, big)
  if #texts > 12 then remove(texts, 1) end
  texts[#texts + 1] = { x = x, z = z, s = s, c = color or 0xFFFFFF, t = 0, life = big and 1.6 or 1.1, big = big }
end

function Fx.update(dt)
  for i = 1, MAXP do
    local p = parts[i]
    if p.life > 0 then
      p.life = p.life - dt
      p.x, p.y, p.z = p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt
      p.vy = p.vy - p.grav * dt
      if p.y < 0 and p.kind == "bit" then p.y, p.vy = 0, -p.vy * 0.3 end
    end
  end
  for i = #texts, 1, -1 do
    local t = texts[i]
    t.t = t.t + dt
    if t.t > t.life then remove(texts, i) end
  end
end

function Fx.draw3d(run) end

function Fx.draw2d()
  for i = 1, MAXP do
    local p = parts[i]
    if p.life > 0 then
      local sx, sy = project3d(p.x, p.y, p.z)
      if sx then
        local k = p.life / p.t
        if p.kind == "puff" then
          circfill(sx, sy, max(1, floor(p.size * (1.4 - k * 0.6))), p.color)
        elseif p.kind == "star" then
          pset(sx, sy, p.color); pset(sx - 1, sy, p.color); pset(sx + 1, sy, p.color)
          pset(sx, sy - 1, p.color); pset(sx, sy + 1, p.color)
        else
          rectfill(sx - 1, sy - 1, p.size, p.size, p.color)
        end
      end
    end
  end
  for _, t in ipairs(texts) do
    local sx, sy = project3d(t.x, 1.2, t.z)
    if sx then
      local rise = t.t * 26
      local scale = t.big and 2 or 1
      if t.t < 0.12 and t.big then scale = 3 end
      text_cs(t.s, sx, floor(sy - rise - 8 * scale), t.c, scale)
    end
  end
end
