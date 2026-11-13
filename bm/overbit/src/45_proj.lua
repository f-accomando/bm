-- Projectiles: rockets, grenades, icicles, darts... Moved in small steps,
-- they hit the world and the actors (a capsule around each: cheaper than
-- the bones) and can be stopped by a defence field (Rally's Null Field) or
-- a barrier (Kaiju's Power Barrier: a flat panel with its own health).

Proj = { list = {}, fields = {}, nbarriers = 0 }

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

-- a barrier: a vertical panel centred at (x, y, z), facing (nx, 0, nz),
-- half width w, half height h, kept by its owner (a.barrier) while it is up
-- (set again every frame as it moves). It stops the shots of the other team
-- (from both sides, as Overwatch's); their damage goes to hit(b, amount, src).
function Proj.barrier(owner, x, y, z, nx, nz, w, h, hit)
  local b = owner.barrier
  if not b then
    b = { is_barrier = true, owner = owner, alive = true }
    owner.barrier = b
  end
  b.team, b.x, b.y, b.z, b.nx, b.nz, b.w, b.h, b.hit = owner.team, x, y, z, nx, nz, w, h, hit
  return b
end

function Proj.barrier_off(owner)
  owner.barrier = nil
end

-- the first barrier of an enemy of `team` along a ray: distance, barrier
function Proj.ray_barrier(ox, oy, oz, dx, dy, dz, maxd, team)
  local best, who = maxd, nil
  for _, a in ipairs(Actors.list) do
    local b = a.barrier
    if b and a.alive and b.team ~= team then
      local den = dx * b.nx + dz * b.nz
      if den > 1e-4 or den < -1e-4 then
        local t = ((b.x - ox) * b.nx + (b.z - oz) * b.nz) / den
        if t > 0 and t < best then
          local px, py, pz = ox + dx * t - b.x, oy + dy * t - b.y, oz + dz * t - b.z
          if abs(px * b.nz - pz * b.nx) <= b.w and abs(py) <= b.h then best, who = t, b end
        end
      end
    end
  end
  return who and best, who
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
  if p.any_team then                    -- a torpedo for a friend or a foe: only its target
    local a = p.target
    if a and a.alive then
      local dy = y - clamp(y, a.y, a.y + a.height)
      local dx, dz = x - a.x, z - a.z
      local r = a.radius + p.size
      if dx * dx + dz * dz + dy * dy < r * r then return a end
    end
    return nil
  end
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
  if direct and p.dmg then
    -- a projectile that can find the head (icicles, kunai): the top fifth of the actor
    local crit = p.headshot and not direct.is_barrier and y > direct.y + direct.height * 0.8
    Actors.damage(direct, p.dmg * (crit and (p.crit_k or 2) or 1), p.owner, crit, p.kind)
    if p.on_direct and not direct.is_barrier then p.on_direct(p, direct) end
  end
  if p.splash and p.radius then
    Actors.splash(p.owner, x, y, z, p.radius, p.splash, p.self_k, p.min_k, p.kind)
  end
  if p.on_hit then p.on_hit(p, x, y, z, direct) end
  if p.boom then Fx.explode(x, y, z, p.boom, p.boom_rgb) end
end

function Proj.update()
  Props.update()
  local nb = 0
  for _, a in ipairs(Actors.list) do if a.barrier then nb = nb + 1 end end
  Proj.nbarriers = nb
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
        if Proj.nbarriers > 0 then
          local d = len3(nx - p.x, ny - p.y, nz - p.z)
          if d > 1e-5 then
            local t, b = Proj.ray_barrier(p.x, p.y, p.z, (nx - p.x) / d, (ny - p.y) / d, (nz - p.z) / d, d + p.size,
              p.owner.team)
            if b then
              local k = t / d
              explode(p, p.x + (nx - p.x) * k, p.y + (ny - p.y) * k, p.z + (nz - p.z) * k, b)
              dead = true
              break
            end
          end
        end
        local who = hit_actor(p, nx, ny, nz)
        if who and not p.bounce_actors then
          explode(p, nx, ny, nz, who)
          dead = true
          break
        end
        local d = len3(nx - p.x, ny - p.y, nz - p.z)
        if d > 1e-5 then
          local t, wx, wy, wz, prop = World.ray(p.x, p.y, p.z, (nx - p.x) / d, (ny - p.y) / d, (nz - p.z) / d, d + p.size)
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
              explode(p, p.x + (nx - p.x) * t / d, p.y + (ny - p.y) * t / d, p.z + (nz - p.z) * t / d, prop)
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
  Props.clear()
  Proj.fields = {}
end
