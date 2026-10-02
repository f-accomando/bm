-- Actors: the heroes in the match. Health, armour and shields as in
-- Overwatch (shields first, then armour, which takes 30% less from each hit,
-- then health); moving, jumping and crouching with the world's collisions;
-- the third-person model with its animation layers; shots that hit the bones.

Actors = { list = G.actors, feed = {} }

local ARMOR_CUT = 0.30          -- armour takes this share off every hit
local GRAV = 18.0               -- m/s^2 (Overwatch's gravity is about 2x real)

local next_id = 1

-- a hero of team `team` at (x, y, z) facing yaw
function Actors.spawn(hero_id, team, x, y, z, yaw, opts)
  local def = H[hero_id]
  local a = {
    id = next_id, hero = def, team = team, name = (opts and opts.name) or def.name,
    x = x, y = y, z = z, vx = 0, vy = 0, vz = 0, yaw = yaw or 0, pitch = 0,
    on_ground = true, crouch = 0, alive = true, dead_t = 0, respawn = (opts and opts.respawn) or 3,
    home = { x = x, y = y, z = z, yaw = yaw or 0 },
    cmd = Input.blank(), st = {}, fx = {}, ult = 0, kills = 0, deaths = 0,
    bot = opts and opts.bot, dummy = opts and opts.dummy,
    anim = { base = "idle", bt = 0, layer = nil, lt = 0, lk = 0, hips = 0 },
    hit_t = 0, dmg_t = 0, last_hit_by = nil, step_t = 0,
  }
  next_id = next_id + 1
  Actors.list[#Actors.list + 1] = a
  def.spawn(a)
  return a
end

-- the actor takes a form (Rally: mech or pilot): model, pools, sizes
function Actors.set_form(a, form, keep_pools)
  a.form = form
  a.radius, a.height, a.eye = form.radius, form.height, form.eye
  if not a.meshes then a.meshes = {} end
  if not a.meshes[form.tp] then a.meshes[form.tp] = model(form.tp) end
  a.mesh = a.meshes[form.tp]
  if not keep_pools then
    a.hp, a.hpmax = form.hp, form.hp
    a.armor, a.armormax = form.armor or 0, form.armor or 0
    a.shield, a.shieldmax = form.shield or 0, form.shield or 0
    a.over = 0
  end
  a.anim.base, a.anim.bt, a.anim.layer = "idle", 0, nil
end

function Actors.total(a) return a.hp + a.armor + a.shield + a.over end
function Actors.total_max(a) return a.hpmax + a.armormax + a.shieldmax end

-- damage `amount` from `src` (an actor or nil); returns what was taken
function Actors.damage(a, amount, src, crit, kind)
  if a.is_barrier then return a.hit(a, amount, src) end     -- a barrier in the way (45_proj)
  if not a.alive or amount <= 0 then return 0 end
  if a.fx.invuln_t and a.fx.invuln_t > 0 then return 0 end
  if src and src ~= a and src.team == a.team then return 0 end
  if a.fx.amp_t and a.fx.amp_t > 0 then amount = amount * (1 + a.fx.amp) end
  if src and src.fx.dmg_boost_t and src.fx.dmg_boost_t > 0 then amount = amount * (1 + src.fx.dmg_boost) end
  local left, taken = amount, 0
  local function take(field, cut)
    if left <= 0 or a[field] <= 0 then return end
    local need = left * (1 - (cut or 0))
    local d = min(a[field], need)
    a[field] = a[field] - d
    taken = taken + d
    left = cut and (need - d) / (1 - cut) or left - d
  end
  take("over")
  take("shield")
  take("armor", ARMOR_CUT)
  take("hp")
  a.dmg_t = G.t
  a.last_hit_by = src
  a.hit_t = 0.25
  if src and src ~= a then
    src.ult = min(100, src.ult + taken / src.hero.ult_cost * 100)
    src.hit_marker = crit and 2 or 1
    src.hit_marker_t = 0.18
    if src == G.local_actor and Snd then Snd.hit(crit) end
  end
  if a.hero.on_damage then a.hero.on_damage(a, taken, src, crit, kind) end
  if taken > 0 and Fx and G.dmg_numbers then
    Fx.number(a.x, a.y + a.height + 0.2, a.z, floor(taken + 0.5), crit)
  end
  if a.alive and a.hp <= 0.01 then
    a.hp = 0
    if not (a.hero.on_lethal and a.hero.on_lethal(a, src)) then Actors.kill(a, src) end
  end
  return taken
end

function Actors.heal(a, amount, src)
  if not a.alive or amount <= 0 then return 0 end
  a.heal_t = 0.3
  if a.fx.antiheal_t and a.fx.antiheal_t > 0 then return 0 end
  local d = min(amount, a.hpmax - a.hp)
  a.hp = a.hp + d
  local rest = amount - d
  if rest > 0 then
    local da = min(rest, a.armormax - a.armor)
    a.armor = a.armor + da
    d = d + da
    rest = rest - da
    local ds = min(rest, a.shieldmax - a.shield)
    a.shield = a.shield + ds
    d = d + ds
  end
  if src and src ~= a and d > 0 then src.ult = min(100, src.ult + d / src.hero.ult_cost * 100) end
  return d
end

function Actors.kill(a, src)
  a.alive = false
  a.dead_t = 0
  a.deaths = a.deaths + 1
  a.anim.base, a.anim.bt, a.anim.layer = "death", 0, nil
  if src and src ~= a then
    src.kills = src.kills + 1
    if src == G.local_actor and Snd then Snd.kill() end
  end
  local feed = Actors.feed
  feed[#feed + 1] = { killer = src and src.name or "", kt = src and src.team or 0, victim = a.name,
                      vt = a.team, t = G.t, hero = src and src.hero.short or "" }
  if #feed > 5 then table.remove(feed, 1) end
  log(string.format("overbit kill %s > %s", src and src.name or "-", a.name))
  if a.hero.on_death then a.hero.on_death(a, src) end
end

function Actors.respawn(a)
  local h = a.home
  a.x, a.y, a.z, a.yaw, a.pitch = h.x, h.y, h.z, h.yaw, 0
  a.vx, a.vy, a.vz = 0, 0, 0
  a.alive, a.fx, a.st, a.ult = true, {}, {}, a.ult
  a.hero.spawn(a)
end

function Actors.push(a, vx, vy, vz)
  a.vx, a.vy, a.vz = a.vx + vx, a.vy + vy, a.vz + vz
  if vy > 0 then a.on_ground = false end
  a.pushed_t = 0.3
end

-- walking: speed from the form and its slows; air control is weak
function Actors.move(a, c, speed_k)
  local f = a.form
  local slow = (a.fx.slow_t and a.fx.slow_t > 0) and (1 - a.fx.slow) or 1
  if a.fx.haste_t and a.fx.haste_t > 0 then slow = slow * (1 + a.fx.haste) end        -- Hyper Ring, Kitsune Rush
  local speed = f.speed * (speed_k or 1) * slow * (a.crouch > 0.5 and 0.5 or 1)
  -- wanted velocity from the stick, in the world
  local s, co = sin(a.yaw), cos(a.yaw)
  local wx = (c.mz * s + c.mx * co) * speed
  local wz = (c.mz * co - c.mx * s) * speed
  local accel = a.on_ground and 60 or 12
  if a.pushed_t and a.pushed_t > 0 then accel = a.on_ground and 10 or 1 end
  a.vx = approach(a.vx, wx, accel * DT)
  a.vz = approach(a.vz, wz, accel * DT)
end

-- one step of physics: gravity, collisions, landing
function Actors.physics(a, grav_k)
  if not a.on_ground or a.vy > 0 then a.vy = a.vy - GRAV * (grav_k or 1) * DT end
  local was = a.on_ground
  local og, wall = World.move(a, a.vx * DT, a.vy * DT, a.vz * DT)
  a.on_ground = og
  a.hit_wall = wall
  -- enemies do not walk through each other: out of their circle (the walls still hold)
  for _, o in ipairs(Actors.list) do
    if o ~= a and o.alive and o.team ~= a.team and not o.hidden then
      local dx, dz = a.x - o.x, a.z - o.z
      local r = a.radius + o.radius
      local d2 = dx * dx + dz * dz
      if d2 < r * r and a.y < o.y + o.height and o.y < a.y + a.height then
        local d = sqrt(d2)
        if d < 1e-3 then dx, dz, d = sin(a.yaw), cos(a.yaw), 1 end
        local push = (r - d) * 0.5 + 0.01
        World.move(a, dx / d * push, 0, dz / d * push)
      end
    end
  end
  if #Props.list > 0 then Props.push(a) end
  if og then
    if not was and a.vy < -4 then
      a.land_t = 0.35
      if a.hero.on_land then a.hero.on_land(a, -a.vy) end
    end
    if a.vy < 0 then a.vy = 0 end
  end
  if a.pushed_t then a.pushed_t = a.pushed_t - DT end
end

-- the status effects on a command, before the hero reads it: frozen (no
-- control at all), rooted (no moving or jumping)
function Actors.status(a, c)
  local fx = a.fx
  if fx.frozen_t and fx.frozen_t > 0 then
    Input.blank(c)
  elseif fx.rooted_t and fx.rooted_t > 0 then
    c.mx, c.mz, c.jump, c.jump_p = 0, 0, false, false
  end
end

function Actors.frozen(a)
  return a.fx.frozen_t and a.fx.frozen_t > 0
end

-- the timers of the status effects: the fields whose name ends in "_t"
-- (the answer kept per name: no new strings every frame)
local is_timer = setmetatable({}, { __index = function(t, k)
  local v = type(k) == "string" and k:sub(-2) == "_t"
  t[k] = v
  return v
end })
local is_cd = setmetatable({}, { __index = function(t, k)
  local v = type(k) == "string" and k:sub(-3) == "_cd"
  t[k] = v
  return v
end })

function Actors.tick_fx(a)
  local fx = a.fx
  for k, v in pairs(fx) do
    if is_timer[k] and v > 0 then fx[k] = v - DT end
  end
  -- Kitsune Rush: the cooldowns run twice as fast, the weapon half again
  if a.fx.rush_t and a.fx.rush_t > 0 then
    for k, v in pairs(a.st) do
      if type(v) == "number" and v > 0 then
        if k == "fire_cd" then a.st[k] = max(0, v - DT * 0.5)
        elseif is_cd[k] then a.st[k] = max(0, v - DT) end
      end
    end
  end
  if a.hit_t > 0 then a.hit_t = a.hit_t - DT end
  if a.hit_marker_t then a.hit_marker_t = a.hit_marker_t - DT end
end

-- the eye of an actor (where its shots start)
function Actors.eye(a)
  return a.x, a.y + a.eye - a.crouch * (a.eye - (a.form.crouch_eye or a.eye)), a.z
end

function Actors.aim_dir(a)
  local cp = cos(a.pitch)
  return sin(a.yaw) * cp, sin(a.pitch), cos(a.yaw) * cp
end

-- the nearest actor hit by a ray (other than `skip`, enemies of `team` if
-- given): distance, actor, critical (head bone)
function Actors.ray(ox, oy, oz, dx, dy, dz, maxd, skip, team)
  local best, who, crit = maxd, nil, false
  for _, a in ipairs(Actors.list) do
    if a.alive and a ~= skip and (not team or a.team ~= team) then
      -- a quick test against the sphere around the actor first
      local cx, cy, cz = a.x - ox, a.y + a.height * 0.5 - oy, a.z - oz
      local along = cx * dx + cy * dy + cz * dz
      local r = a.height * 0.6 + a.radius
      if along > -r and along - r < best then
        local px, py, pz = cx - dx * along, cy - dy * along, cz - dz * along
        if px * px + py * py + pz * pz < r * r then
          local t, bone = hit3d(a.mesh, a.x, a.y, a.z, a.yaw, a.form.scale or 1, ox, oy, oz, dx, dy, dz, best)
          if t and t < best then best, who, crit = t, a, bone == a.form.head end
        end
      end
    end
  end
  return who and best, who, crit
end

-- a hitscan shot: world first, then the enemy barriers and the actors in
-- front of the wall (a barrier comes back as `who`: Actors.damage knows it)
function Actors.shoot(src, ox, oy, oz, dx, dy, dz, range)
  local wt, nx, ny, nz, prop = World.ray(ox, oy, oz, dx, dy, dz, range)
  local bt, bar = nil, nil
  if Proj.nbarriers > 0 then bt, bar = Proj.ray_barrier(ox, oy, oz, dx, dy, dz, wt or range, src.team) end
  local at, who, crit = Actors.ray(ox, oy, oz, dx, dy, dz, bt or wt or range, src, src.team)
  if who then return at, who, crit end
  if bar then return bt, bar, false, -bar.nx, 0, -bar.nz end
  return wt, prop, false, nx, ny, nz          -- a prop (an ice pillar) can be shot too
end

-- the enemies in front of `a` within reach (a cone of half angle `half`,
-- radians, around its aim, with the world clear between): a list
function Actors.cone(a, reach, half, height)
  local out = {}
  local fx, fz = sin(a.yaw), cos(a.yaw)
  local cs = cos(half)
  local ey = a.y + a.height * 0.5
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local dx, dz = o.x - a.x, o.z - a.z
      local d = sqrt(dx * dx + dz * dz)
      local dy = (o.y + o.height * 0.5) - ey
      if d - o.radius < reach and abs(dy) < (height or 2.5) + o.height * 0.5 then
        local inside = d < a.radius + o.radius + 0.2 or (dx * fx + dz * fz) / d > cs
        if inside and World.clear(a.x, ey, a.z, o.x, o.y + o.height * 0.5, o.z) then out[#out + 1] = o end
      end
    end
  end
  return out
end

-- damage with distance falloff (full until near, `low` share at far)
function Actors.falloff(dmg, d, near, far, low)
  if d <= near then return dmg end
  if d >= far then return dmg * low end
  return dmg * (1 - (1 - low) * (d - near) / (far - near))
end

-- splash damage around (x, y, z), less towards the edge; walls protect
function Actors.splash(src, x, y, z, radius, dmg, self_k, min_k, kind)
  for _, a in ipairs(Actors.list) do
    if a.alive and (a.team ~= src.team or a == src) then
      local cx, cy, cz = a.x, a.y + a.height * 0.5, a.z
      local d = len3(cx - x, cy - y, cz - z) - a.radius
      if d < radius and World.clear(x, y + 0.1, z, cx, cy, cz) then
        local k = 1 - (1 - (min_k or 0.3)) * clamp(d / radius, 0, 1)
        if a == src then k = k * (self_k or 0) end
        if k > 0 then Actors.damage(a, dmg * k, src, false, kind or "splash") end
      end
    end
  end
end

-- ---------------------------------------------------------------- animation

-- picks the base clip (legs) and the layer (upper body) of an actor, and
-- moves their clocks
function Actors.animate(a)
  local an, f = a.anim, a.form
  if a.alive and Actors.frozen(a) then             -- frozen: the pose stays as it is
    if an.layer then animate(a.mesh, an.base, an.bt, an.layer, an.lt, an.lk, f.layer_bone)
    else animate(a.mesh, an.base, an.bt) end
    return
  end
  local clips = f.clips
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if a.alive then
    local want, rate = clips.idle, 1
    if a.override then
      want, rate = a.override, 1
    elseif not a.on_ground then
      want = (a.land_t and a.land_t > 0) and clips.air or clips.air
      if an.base == clips.jump and an.bt < (f.jump_len or 0.4) then want = clips.jump end
    elseif a.land_t and a.land_t > 0 then
      want = clips.land
    elseif a.crouch > 0.5 and clips.crouch then
      want = clips.crouch
    elseif speed > 0.4 then
      if speed > f.speed * 0.75 and clips.run then
        want, rate = clips.run, speed / (f.run_speed or f.speed)
      else
        want, rate = clips.walk or clips.run, speed / (f.walk_speed or f.speed * 0.6)
      end
      -- walking backwards: the clip runs backwards
      local fx, fz = sin(a.yaw), cos(a.yaw)
      if a.vx * fx + a.vz * fz < -0.3 * speed then rate = -rate end
    end
    if want ~= an.base then an.base, an.bt = want, 0 end
    an.bt = an.bt + DT * rate
  else
    an.bt = an.bt + DT
  end
  if a.land_t then a.land_t = a.land_t - DT end
  if an.layer then
    an.lt = an.lt + DT
    an.lk = approach(an.lk, an.ltarget or 1, DT * 8)
    if an.lk <= 0 and (an.ltarget or 1) <= 0 then an.layer = nil end
  end
  -- the legs turn towards where the actor goes; the body keeps the aim
  local hips = 0
  if a.alive and speed > 0.5 and a.on_ground then
    local fx, fz = sin(a.yaw), cos(a.yaw)
    local rx, rz = cos(a.yaw), -sin(a.yaw)
    local along, side = a.vx * fx + a.vz * fz, a.vx * rx + a.vz * rz
    if along < 0 then along, side = -along, -side end
    hips = clamp(atan(side, along), -1.1, 1.1)
  end
  an.hips = approach(an.hips, hips, DT * 6)
  local m = a.mesh
  if f.hips_bone then
    bone_turn(m, f.hips_bone, 0, an.hips, 0)        -- ry > 0 turns towards the right (+x)
    bone_turn(m, f.body_bone, 0, -an.hips, 0)
  end
  if f.aim_bones then
    for _, b in ipairs(f.aim_bones) do bone_turn(m, b, -a.pitch * (f.aim_k or 1), 0, 0) end
  end
  if an.layer then
    animate(m, an.base, an.bt, an.layer, an.lt, an.lk, f.layer_bone)
  else
    animate(m, an.base, an.bt)
  end
end

-- a layer clip on the upper body (shooting, abilities); target weight 0 fades it out
function Actors.layer(a, clip, restart)
  local an = a.anim
  if an.layer ~= clip or restart then
    if an.layer ~= clip then an.lk = 0 end
    an.layer, an.lt = clip, 0
  end
  an.ltarget = 1
end

function Actors.layer_off(a)
  a.anim.ltarget = 0
end

-- ---------------------------------------------------------------- drawing

-- detail level from the distance and the quality
function Actors.detail(d)
  local q = G.quality
  local k = ({ 0.45, 0.65, 1, 1.3, 1.7 })[q + 1]
  if d < 7 * k then return q >= 2 and 3 or 2 end
  if d < 20 * k then return q >= 1 and 2 or 1 end
  if d < 38 * k then return 1 end
  return 0
end

function Actors.draw(cx, cy, cz, skip)
  local q = G.quality
  for _, a in ipairs(Actors.list) do
    if a ~= skip and (a.alive or a.dead_t < (a.form.corpse or 2.5)) and not a.hidden then
      local d = len3(a.x - cx, a.y - cy, a.z - cz)
      local det = Actors.detail(d)
      local flags = (3 - det) * 16
      if q >= 2 then flags = flags + 4 end           -- Gouraud
      local sc = a.form.scale or 1
      if q >= 3 and d < 30 then                      -- the shadow on the ground: a coarse model is enough
        draw3d(a.mesh, a.x, a.y, a.z, 0, a.yaw, 0, sc, (3 - min(det, 1)) * 16 + 8)
      end
      draw3d(a.mesh, a.x, a.y, a.z, 0, a.yaw, 0, sc, flags)
      if a.hero.draw_extra then a.hero.draw_extra(a, d) end
      if a.alive and Actors.frozen(a) then
        local k = a.height / 3.2 * 1.12
        local ice = Props.ice_mesh(true)
        if a.radius > 0.6 then                   -- a mech: three blocks round it
          for i = 0, 2 do
            local an = a.yaw + i * 2 * pi / 3
            draw3d(ice, a.x + sin(an) * a.radius * 0.55, a.y - 0.05, a.z + cos(an) * a.radius * 0.55, 0, an, 0, k, 0)
          end
        else
          draw3d(ice, a.x, a.y - 0.05, a.z, 0, a.yaw, 0, max(k, (a.radius + 0.1) / 0.62), 0)
        end
      end
    end
  end
end
