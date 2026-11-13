-- Props: things the heroes put in the world for a while (Frost's ice
-- pillars, ...). Upright round blocks with their own health: they stop
-- everyone (both teams) and every shot, and can be stood on. World.ray and
-- World.clear see them; Actors.physics pushes the actors out of them.

Props = { list = {} }

local list = Props.list

-- p: x y z (base) r h hp life [owner draw on_break]
function Props.add(p)
  p.is_barrier = true
  p.alive = true
  p.age = 0
  p.rise = p.rise or 0.25
  p.hpmax = p.hp
  p.hit = Props.hit
  list[#list + 1] = p
  return p
end

function Props.hit(p, amount, src)
  if not p.alive then return 0 end
  local d = min(p.hp, amount)
  p.hp = p.hp - amount
  p.flash = 0.08
  if src and src.team ~= (p.owner and p.owner.team) then src.ult = min(100, src.ult + d / src.hero.ult_cost * 100) end
  if p.hp <= 0 then Props.remove(p) end
  return d
end

function Props.remove(p)
  if not p.alive then return end
  p.alive = false
  if p.on_break then p.on_break(p) end
end

function Props.update()
  local i = 1
  while i <= #list do
    local p = list[i]
    p.age = p.age + DT
    if p.flash and p.flash > 0 then p.flash = p.flash - DT end
    if p.age >= p.life then Props.remove(p) end
    if not p.alive then table.remove(list, i) else i = i + 1 end
  end
end

-- the height of a prop now (it rises out of the ground)
local function top(p)
  return p.y + p.h * smooth01(p.age / p.rise)
end

-- a ray against the props: distance, normal, prop (or nil)
function Props.ray(ox, oy, oz, dx, dy, dz, maxd)
  local best, bp, bnx, bny, bnz = maxd, nil, 0, 0, 0
  for _, p in ipairs(list) do
    local ty = top(p)
    -- the side: |(o + d t - c)_xz| = r
    local fx, fz = ox - p.x, oz - p.z
    local a = dx * dx + dz * dz
    if a > 1e-8 then
      local b = fx * dx + fz * dz
      local c = fx * fx + fz * fz - p.r * p.r
      local disc = b * b - a * c
      if disc >= 0 then
        local t = (-b - sqrt(disc)) / a
        if t > 0 and t < best then
          local y = oy + dy * t
          if y >= p.y and y <= ty then
            best, bp = t, p
            bnx, bny, bnz = (fx + dx * t) / p.r, 0, (fz + dz * t) / p.r
          end
        end
      end
    end
    -- the top
    if dy < -1e-6 and oy > ty then
      local t = (ty - oy) / dy
      if t > 0 and t < best then
        local x, z = ox + dx * t - p.x, oz + dz * t - p.z
        if x * x + z * z <= p.r * p.r then best, bp, bnx, bny, bnz = t, p, 0, 1, 0 end
      end
    end
  end
  if bp then return best, bnx, bny, bnz, bp end
  return nil
end

-- an actor out of the props (sideways), or standing on one
function Props.push(a)
  for _, p in ipairs(list) do
    local dx, dz = a.x - p.x, a.z - p.z
    local r = a.radius + p.r
    local d2 = dx * dx + dz * dz
    if d2 < r * r then
      local ty = top(p)
      if a.y >= ty - 0.5 and a.vy <= 0 and d2 < (p.r + a.radius * 0.5) ^ 2 then
        if a.y < ty then a.y = ty end
        if a.y - ty < 0.05 then a.on_ground, a.vy = true, 0 end
      elseif a.y < ty and a.y + a.height > p.y then
        local d = sqrt(d2)
        if d < 1e-3 then dx, dz, d = 1, 0, 1 end
        local push = r - d + 0.01
        World.move(a, dx / d * push, 0, dz / d * push)
      end
    end
  end
end

function Props.draw()
  for _, p in ipairs(list) do
    if p.draw then p.draw(p, top(p) - p.h) end
  end
end

function Props.clear()
  for k in pairs(list) do list[k] = nil end
end

-- the ice: an upright hexagonal block, 0.62 m round and 3.2 m high, with a
-- pointed top (pillars, frozen heroes, Cryo-Freeze); glossy pale blue,
-- the edges lighter; see = true: see-through (a hero inside)
local ice = {}
function Props.ice_mesh(see)
  local key = see and 2 or 1
  if ice[key] then return ice[key] end
  local v, f = {}, {}
  local function vert(x, y, z)
    v[#v + 1] = x v[#v + 1] = y v[#v + 1] = z
    return #v // 3
  end
  -- half the sides glow a little by themselves: ice reads as ice in any light
  local extra = see and 0x10000000 or 0
  local body = 0x7CC0E0 | 0x40000000 | extra
  local light = 0xDDF6FF | 0x20000000 | 0x08000000 | extra
  local tipc = 0xC8F0FF | 0x40000000 | extra
  local R, Hh = 0.62, 3.2
  local bot, mid = {}, {}
  for i = 0, 5 do
    local an = pi / 3 * i + 0.3
    local wob = 1 + 0.08 * sin(i * 2.7)
    bot[i] = vert(cos(an) * R * wob, 0, sin(an) * R * wob)
    mid[i] = vert(cos(an) * R * 0.94 * wob, Hh * (0.78 + 0.06 * sin(i * 1.9)), sin(an) * R * 0.94 * wob)
  end
  local tip = vert(0.05, Hh, -0.04)
  for i = 0, 5 do
    local j = (i + 1) % 6
    local c = (i % 2 == 0) and body or light
    f[#f + 1] = bot[i] f[#f + 1] = mid[j] f[#f + 1] = bot[j] f[#f + 1] = c
    f[#f + 1] = bot[i] f[#f + 1] = mid[i] f[#f + 1] = mid[j] f[#f + 1] = c
    f[#f + 1] = mid[i] f[#f + 1] = tip f[#f + 1] = mid[j] f[#f + 1] = (i % 2 == 0) and tipc or light
  end
  ice[key] = mesh(v, f)
  return ice[key]
end
