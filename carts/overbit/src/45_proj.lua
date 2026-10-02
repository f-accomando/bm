-- Projectiles: rockets, grenades, icicles, darts... Moved in small steps,
-- they hit the world and the actors (a capsule around each: cheaper than
-- the bones) and can be stopped by a defence field (Rally's Null Field).

Proj = { list = {}, fields = {} }

local list = Proj.list

-- p: x y z vx vy vz owner dmg [splash radius grav life homing bounce size rgb trail kind on_hit]
function Proj.spawn(p)
  p.age = 0
  p.life = p.life or 3
  p.size = p.size or 0.12
  list[#list + 1] = p
  return p
end

-- a defence field this frame: from (x, y, z) along unit d, length L,
-- radius at the far end R (a cone), owner team
function Proj.field(owner, x, y, z, dx, dy, dz, L, R)
  local f = Proj.fields
  f[#f + 1] = { owner = owner, x = x, y = y, z = z, dx = dx, dy = dy, dz = dz, L = L, R = R }
end

local function in_field(p)
  for _, f in ipairs(Proj.fields) do
    if f.owner.team ~= p.owner.team and f.owner.alive then
      local px, py, pz = p.x - f.x, p.y - f.y, p.z - f.z
      local along = px * f.dx + py * f.dy + pz * f.dz
      if along > 0.3 and along < f.L then
        local ox, oy, oz = px - f.dx * along, py - f.dy * along, pz - f.dz * along
        local r = 0.6 + (f.R - 0.6) * along / f.L
        if ox * ox + oy * oy + oz * oz < r * r then return f end
      end
    end
  end
  return nil
end

local function hit_actor(p, x, y, z)
  for _, a in ipairs(Actors.list) do
    if a.alive and a.team ~= p.owner.team then
      local dy = y - clamp(y, a.y, a.y + a.height)
      local dx, dz = x - a.x, z - a.z
      local r = a.radius + p.size
      if dx * dx + dz * dz + dy * dy < r * r then return a end
    end
  end
  return nil
end

local function explode(p, x, y, z, direct)
  if direct and p.dmg then Actors.damage(direct, p.dmg, p.owner, false, p.kind) end
  if p.splash and p.radius then
    Actors.splash(p.owner, x, y, z, p.radius, p.splash, p.self_k, p.min_k, p.kind)
  end
  if p.on_hit then p.on_hit(p, x, y, z, direct) end
  if p.boom then Fx.explode(x, y, z, p.boom, p.boom_rgb) end
end

function Proj.update()
  local i = 1
  while i <= #list do
    local p = list[i]
    p.age = p.age + DT
    local dead = p.age >= p.life
    if dead and p.fuse then explode(p, p.x, p.y, p.z, nil) end
    if not dead and p.homing and p.target and p.target.alive then
      local t = p.target
      local tx, ty, tz = t.x - p.x, t.y + t.height * 0.5 - p.y, t.z - p.z
      local d = len3(tx, ty, tz)
      local sp = len3(p.vx, p.vy, p.vz)
      local k = min(1, p.homing * DT)
      p.vx = p.vx + (tx / d * sp - p.vx) * k
      p.vy = p.vy + (ty / d * sp - p.vy) * k
      p.vz = p.vz + (tz / d * sp - p.vz) * k
    end
    if not dead then
      if p.grav then p.vy = p.vy - p.grav * DT end
      -- two half steps: fast rockets do not jump over thin actors
      for _ = 1, 2 do
        local nx, ny, nz = p.x + p.vx * DT * 0.5, p.y + p.vy * DT * 0.5, p.z + p.vz * DT * 0.5
        local f = in_field(p)
        if f then
          Fx.burst(p.x, p.y, p.z, 4, 0, 2, 0.25, 0.08, 0xFFB060)
          if f.owner.hero.on_eat then f.owner.hero.on_eat(f.owner, p) end
          dead = true
          break
        end
        local who = hit_actor(p, nx, ny, nz)
        if who and not p.bounce_actors then
          explode(p, nx, ny, nz, who)
          dead = true
          break
        end
        local d = len3(nx - p.x, ny - p.y, nz - p.z)
        if d > 1e-5 then
          local t, wx, wy, wz = World.ray(p.x, p.y, p.z, (nx - p.x) / d, (ny - p.y) / d, (nz - p.z) / d, d + p.size)
          if t then
            if p.bounce and p.bounces ~= 0 then
              -- reflect off the surface, losing speed
              local vn = p.vx * wx + p.vy * wy + p.vz * wz
              p.vx, p.vy, p.vz = (p.vx - 2 * vn * wx) * p.bounce, (p.vy - 2 * vn * wy) * p.bounce,
                (p.vz - 2 * vn * wz) * p.bounce
              p.bounces = (p.bounces or -1) - 1
              nx, ny, nz = p.x, p.y, p.z
              if p.on_bounce then p.on_bounce(p) end
            elseif p.stick then
              p.vx, p.vy, p.vz, p.grav = 0, 0, 0, nil
              p.stuck, p.nx, p.ny, p.nz = true, wx, wy, wz
              nx, ny, nz = p.x + p.vx, p.y, p.z
              if p.on_stick then p.on_stick(p) end
            else
              explode(p, p.x + (nx - p.x) * t / d, p.y + (ny - p.y) * t / d, p.z + (nz - p.z) * t / d, nil)
              dead = true
              break
            end
          end
        end
        p.x, p.y, p.z = nx, ny, nz
      end
      if p.trail and not dead and random() < p.trail * Fx.density then
        Fx.spawn(p.x, p.y, p.z, random() - 0.5, random() * 0.5, random() - 0.5, 0.35, p.size * 1.2,
          p.trail_rgb or 0x908888, 1, -0.2)
      end
    end
    if dead or p.remove then
      table.remove(list, i)
    else
      i = i + 1
    end
  end
  Proj.fields = {}
end

function Proj.draw()
  for _, p in ipairs(list) do
    if p.draw then
      p.draw(p)
    else
      point3d(p.x, p.y, p.z, p.size, p.rgb or 0xFFE0A0)
      if p.glow then point3d(p.x - p.vx * 0.012, p.y - p.vy * 0.012, p.z - p.vz * 0.012, p.size * 1.8, p.glow, 1) end
    end
  end
end

function Proj.clear()
  for k in pairs(list) do list[k] = nil end
  Proj.fields = {}
end
