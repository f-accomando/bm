-- Rally, tank (the kit of D.Va). In the mech: Fusion Cannons (primary),
-- Null Field (secondary: a field that eats projectiles), Afterburners
-- (ability 1: flight), Swarm Rockets (ability 2), Redline (ultimate: the
-- pilot jumps out and the mech blows up 3 s later); when the mech is
-- destroyed the pilot ejects (passive). On foot: Pit Pistol (primary) and
-- Pit Stop (ultimate: a new mech falls from the sky).

local R = {
  id = "rally", name = "Rally", short = "RLY", role = "tank", rgb = 0xF26A21,
  ult_cost = 1100,           -- points of damage for a full ultimate
  desc = "Racing pilot in a white mech",
}
H.rally = R
HERO_ORDER[#HERO_ORDER + 1] = "rally"

local MECH = {
  name = "mech", tp = "rally_mech", fp = "rally_fp",
  hp = 375, armor = 325, speed = 5.5, radius = 1.05, height = 2.95, eye = 2.25, crouch_eye = 1.85,
  jump = 6.2, head = "canopy", corpse = 0.6,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 3.3, run_speed = 5.5, jump_len = 0.45,
  hips_bone = "hips", body_bone = "body", aim_bones = { "arm.L", "arm.R" }, layer_bone = "body",
}
local PILOT = {
  name = "pilot", tp = "rally_pilot", fp = "rally_pfp",
  hp = 150, armor = 0, speed = 6.0, radius = 0.35, height = 1.65, eye = 1.5, crouch_eye = 1.1,
  jump = 6.6, head = "head", corpse = 3,
  clips = { idle = "idle", run = "run", air = "air", jump = "jump", land = "land", death = "death" },
  run_speed = 6.0, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "upperarm.R" }, layer_bone = "chest",
}
R.forms = { mech = MECH, pilot = PILOT }

-- numbers of the kit (Overwatch 2's, more or less)
local CANNON = { rate = 0.15, pellets = 11, dmg = 2.0, spread = 0.062, near = 10, far = 20, low = 0.5,
                 slow = 0.40 }
local FIELD = { max = 3.0, regen = 0.375, delay = 1.0, len = 10, radius = 2.4 }   -- seconds of use
local BOOST = { time = 2.0, speed = 15, cd = 4.0, hit = 10, push = 9 }
local ROCKETS = { n = 18, time = 1.5, speed = 32, dmg = 3, splash = 3, radius = 2.5, cd = 7.0 }
local REDLINE = { fuse = 3.0, dmg = 1000, radius = 20, full = 12 }
local PISTOL = { rate = 1 / 7, dmg = 15, near = 15, far = 30, low = 0.5 }
local PITSTOP = { charge = 6.0, dmg = 250, radius = 4.5 }   -- % per second while on foot

R.numbers = { cannon = CANNON, field = FIELD, boost = BOOST, rockets = ROCKETS, redline = REDLINE,
              pistol = PISTOL, pitstop = PITSTOP }

-- the abilities for the HUD: key, name, state(a) -> left, total, active, ready
R.hud = {
  { key = "ab1", name = "Afterburners", icon = 1, state = function(a)
      local s = a.st
      if a.form ~= MECH then return 0, 1, false, false end
      return s.boost_cd or 0, BOOST.cd, (s.boost_t or 0) > 0, true end },
  { key = "ab2", name = "Swarm Rockets", icon = 2, state = function(a)
      local s = a.st
      if a.form ~= MECH then return 0, 1, false, false end
      return s.rocket_cd or 0, ROCKETS.cd, (s.rockets or 0) > 0, true end },
  { key = "fire2", name = "Null Field", icon = 3, meter = function(a)
      if a.form ~= MECH then return nil end
      return (a.st.field or FIELD.max) / FIELD.max, a.st.field_on end },
}

function R.spawn(a)
  Actors.set_form(a, MECH)
  local s = a.st
  s.field, s.field_on, s.field_wait = FIELD.max, false, 0
  s.fire_cd, s.boost_t, s.boost_cd, s.rocket_cd, s.rockets = 0, 0, 0, 0, 0
  a.override = nil
  a.hidden = false
end

R.ult_name = function(a) return a.form == MECH and "REDLINE" or "PIT STOP" end

-- ---------------------------------------------------------------- the mech's weapons

local function muzzle(a, side)
  -- the tip of a cannon: the tail of bone gun.L / gun.R, placed in the world
  local m = a.mesh
  local _, _, _, tx, ty, tz = bone3d(m, side == 1 and "gun.L" or "gun.R")
  if not tx then return a.x, a.y + 1.8, a.z end
  local s, c = sin(a.yaw), cos(a.yaw)
  return a.x + tx * c + tz * s, a.y + ty, a.z - tx * s + tz * c
end

local function fire_cannons(a)
  local s = a.st
  s.fire_cd = CANNON.rate
  s.side = 3 - (s.side or 1)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  -- the right and up of the aim, for the spread
  local rx, rz = cos(a.yaw), -sin(a.yaw)
  local ux, uy, uz = -sin(a.pitch) * sin(a.yaw), cos(a.pitch), -sin(a.pitch) * cos(a.yaw)
  local mx, my, mz = muzzle(a, s.side)
  local tracers = 0
  for i = 1, CANNON.pellets do
    local ang = grandom() * 2 * pi
    local rad = sqrt(grandom()) * CANNON.spread
    local ox, oy = cos(ang) * rad, sin(ang) * rad
    local dx, dy, dz = fx + rx * ox + ux * oy, fy + uy * oy, fz + rz * ox + uz * oy
    local l = len3(dx, dy, dz)
    dx, dy, dz = dx / l, dy / l, dz / l
    local t, who, crit, nx, ny, nz = Actors.shoot(a, ex, ey, ez, dx, dy, dz, 40)
    local hx, hy, hz = ex + dx * (t or 40), ey + dy * (t or 40), ez + dz * (t or 40)
    if who then
      local dmg = Actors.falloff(CANNON.dmg, t, CANNON.near, CANNON.far, CANNON.low) * (crit and 2 or 1)
      Actors.damage(who, dmg, a, crit, "hitscan")
      if i % 4 == 0 then Fx.burst(hx, hy, hz, 2, 0, 2.5, 0.15, 0.05, 0xFFE0A0, 1) end
    elseif t and i % 3 == 0 then
      Fx.burst(hx, hy, hz, 2, 1, 1.5, 0.2, 0.04, 0xE8D0A0, 2, nx, ny, nz)
    end
    if tracers < 3 and i % 4 == 1 then
      tracers = tracers + 1
      Fx.tracer(mx, my, mz, hx, hy, hz, 0xFFE8B0, 1, 0.05)
    end
  end
  Fx.burst(mx, my, mz, 3, 2, 1.0, 0.08, 0.12, 0xFFF0C0, 0, fx, fy, fz)
  a.muzzle_t, a.muzzle_side = 0.05, s.side
  if a == G.local_actor then Snd.play("cannon") end
end

local function boost_hits(a)
  local s = a.st
  s.boost_hit = s.boost_hit or {}
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team and not s.boost_hit[o] then
      local d = len3(o.x - a.x, (o.y + o.height * 0.5) - (a.y + a.height * 0.5), o.z - a.z)
      if d < a.radius + o.radius + 0.4 then
        s.boost_hit[o] = true
        Actors.damage(o, BOOST.hit, a, false, "melee")
        local fx, _, fz = Actors.aim_dir(a)
        Actors.push(o, fx * BOOST.push, 3, fz * BOOST.push)
        Fx.burst(o.x, o.y + o.height * 0.5, o.z, 8, 2, 3, 0.3, 0.08, 0xFFFFFF)
        if a == G.local_actor then Snd.play("bump") end
      end
    end
  end
end

local function launch_rocket(a)
  local s = a.st
  s.rocket_side = 3 - (s.rocket_side or 1)
  local m = a.mesh
  local hx, hy, hz = bone3d(m, s.rocket_side == 1 and "pod.L" or "pod.R")
  local sy, cy = sin(a.yaw), cos(a.yaw)
  local px, py, pz = a.x + hx * cy + (hz + 0.3) * sy, a.y + hy + 0.1, a.z - hx * sy + (hz + 0.3) * cy
  local fx, fy, fz = Actors.aim_dir(a)
  -- a little scatter, then they straighten towards the aim
  local jx, jy, jz = (grandom() - 0.5) * 0.16, (grandom() - 0.2) * 0.14, (grandom() - 0.5) * 0.16
  Proj.spawn({ x = px, y = py, z = pz, vx = (fx + jx) * ROCKETS.speed, vy = (fy + jy + 0.06) * ROCKETS.speed,
               vz = (fz + jz) * ROCKETS.speed, owner = a, dmg = ROCKETS.dmg, splash = ROCKETS.splash,
               radius = ROCKETS.radius, life = 2.5, size = 0.09, rgb = 0xFFFFFF, glow = 0xFF8A30,
               trail = 0.8, trail_rgb = 0xB0A8A8, boom = 0.35, boom_rgb = 0xFFA040, kind = "rocket", self_k = 0 })
  if a == G.local_actor then Snd.play("rocket") end
end

-- ---------------------------------------------------------------- Redline (ultimate)

local function redline(a)
  local s = a.st
  a.ult = 0
  -- the empty mech flies on along the aim, then shakes and blows up
  local fx, _, fz = Actors.aim_dir(a)
  local m = a.mesh
  local bomb = { x = a.x, y = a.y, z = a.z, vx = fx * 12, vy = 3, vz = fz * 12, yaw = a.yaw, mesh = m,
                 owner = a, age = 0, life = REDLINE.fuse, t = 0 }
  s.bomb = bomb
  Proj.spawn({ x = a.x, y = a.y + 1.5, z = a.z, vx = 0, vy = 0, vz = 0, owner = a, life = REDLINE.fuse,
               size = 0.01, fuse = true, kind = "redline",
               draw = function(p)
                 local b = bomb
                 animate(b.mesh, "selfdestruct", p.age)
                 local blink = (floor(p.age * (4 + p.age * 4)) % 2 == 0)
                 draw3d(b.mesh, b.x, b.y, b.z, 0, b.yaw, 0, 1, 4)
                 if blink then point3d(b.x, b.y + 2.2, b.z, 0.5, 0xFF3020) end
               end,
               on_hit = function(p, x, y, z)
                 local b = bomb
                 Fx.explode(b.x, b.y + 1.5, b.z, 4.5, 0xFFA050)
                 Fx.flash, Fx.flash_rgb = 0.6, 0xFFE0B0
                 for _, o in ipairs(Actors.list) do
                   if o.alive and o.team ~= a.team then
                     local d = len3(o.x - b.x, o.y - b.y, o.z - b.z)
                     if d < REDLINE.radius and World.clear(b.x, b.y + 1.5, b.z, o.x, o.y + o.height * 0.6, o.z) then
                       local k = d < REDLINE.full and 1 or 1 - 0.7 * (d - REDLINE.full) / (REDLINE.radius - REDLINE.full)
                       Actors.damage(o, REDLINE.dmg * k, a, false, "ult")
                     end
                   end
                 end
                 s.bomb = nil
                 log("overbit redline boom")
                 Snd.play("boom_big")
               end })
  -- the bomb moves with the projectile's clock: a small mover
  bomb.update = function()
    local b = bomb
    b.vy = b.vy - 18 * DT
    b.vx, b.vz = b.vx * (1 - 1.5 * DT), b.vz * (1 - 1.5 * DT)
    local probe = { x = b.x, y = b.y, z = b.z, vx = b.vx, vy = b.vy, vz = b.vz, radius = 1.0, height = 2.6, on_ground = false }
    local og = World.move(probe, b.vx * DT, b.vy * DT, b.vz * DT)
    b.x, b.y, b.z = probe.x, probe.y, probe.z
    if og and b.vy < 0 then b.vy = 0 end
  end
  -- the pilot pops out, up and back
  R.eject(a, false)
  if a == G.local_actor then Snd.play("redline") end
end

-- out of the mech: on foot, a jump up and back
function R.eject(a, destroyed)
  log(string.format("overbit eject %s%s", a.name, destroyed and " (mech destroyed)" or " (redline)"))
  local ult = a.ult
  local x, y, z = a.x, a.y + 1.8, a.z
  Actors.set_form(a, PILOT)
  a.x, a.y, a.z = x, y, z
  local fx, _, fz = Actors.aim_dir(a)
  a.vx, a.vy, a.vz = -fx * 4, 9, -fz * 4
  a.on_ground = false
  a.ult = destroyed and ult or 0
  a.st.fire_cd = 0
  a.override = "eject"
  a.anim.base, a.anim.bt = "eject", 0
  a.st.eject_t = 0.8
  a.fp_clip, a.fp_t = "raise", 0
  if destroyed then
    Fx.explode(x, y - 0.6, z, 2.2, 0xFFB050)
    if a == G.local_actor then Snd.play("eject") end
  end
end

-- Pit Stop: a new mech falls from the sky onto the pilot
local function pit_stop(a)
  a.ult = 0
  a.st.call_t = 0.9
  a.override = "call"
  a.anim.base, a.anim.bt = "call", 0
  if a == G.local_actor then Snd.play("call") end
end

local function mech_lands(a)
  log("overbit pit stop " .. a.name)
  Actors.set_form(a, MECH)
  a.vx, a.vy, a.vz = 0, -2, 0
  a.override = "callmech"
  a.anim.base, a.anim.bt = "callmech", 0
  a.st.land_t = 0.8
  a.st.field, a.st.boost_cd, a.st.rocket_cd = FIELD.max, 0, 0
  Fx.explode(a.x, a.y + 0.3, a.z, 1.5, 0xFFD080)
  Actors.splash(a, a.x, a.y + 0.5, a.z, PITSTOP.radius, PITSTOP.dmg, 0, 1, "ult")
  a.fp_clip, a.fp_t = "raise", 0
end

-- ---------------------------------------------------------------- the update of a frame

local TIMERS = { "fire_cd", "boost_cd", "rocket_cd" }  -- once, not every frame
function R.update(a, c)
  local s = a.st
  if s.bomb and s.bomb.update then s.bomb.update() end
  if not a.alive then return end
  for _, k in ipairs(TIMERS) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if a.form == MECH then
    R.update_mech(a, c)
  else
    R.update_pilot(a, c)
  end
end

function R.update_mech(a, c)
  local s = a.st
  a.ult = min(100, a.ult + 0.4 * DT)
  if s.land_t and s.land_t > 0 then
    s.land_t = s.land_t - DT
    if s.land_t <= 0 then a.override = nil end
    Actors.move(a, Input.blank(), 0)
    Actors.physics(a)
    return
  end
  -- Redline
  if c.ult_p and a.ult >= 100 then redline(a) return end
  -- Afterburners: fly along the aim
  if c.ab1_p and s.boost_cd <= 0 and s.boost_t <= 0 then
    s.boost_t, s.boost_hit = BOOST.time, {}
    if a == G.local_actor then Snd.play("boost") end
  end
  local boosting = s.boost_t > 0
  if boosting then
    s.boost_t = s.boost_t - DT
    local fx, fy, fz = Actors.aim_dir(a)
    a.vx, a.vy, a.vz = fx * BOOST.speed, fy * BOOST.speed + (a.on_ground and 1.5 or 0), fz * BOOST.speed
    a.on_ground = false
    a.override = "boost"
    boost_hits(a)
    -- flames from the thrusters
    if random() < 0.8 * Fx.density then
      local sy, cy = sin(a.yaw), cos(a.yaw)
      for k = -1, 1, 2 do
        local px, pz = a.x + k * 0.42 * cy - 1.35 * sy, a.z - k * 0.42 * sy - 1.35 * cy
        Fx.spawn(px, a.y + 2.0, pz, -a.vx * 0.3 + random() - 0.5, -1 + random(), -a.vz * 0.3 + random() - 0.5,
          0.22, 0.22, 0xFFB050, 2, 0)
      end
    end
    if s.boost_t <= 0 or c.ab1_p and s.boost_t < BOOST.time - 0.25 then
      s.boost_t, s.boost_cd = 0, BOOST.cd
      a.override = nil
    end
  end
  -- Null Field: hold the secondary
  local want_field = c.fire2 and s.field > 0.05
  if want_field then
    s.field = s.field - DT
    s.field_on = true
    s.field_wait = FIELD.delay
    local ex, ey, ez = Actors.eye(a)
    local fx, fy, fz = Actors.aim_dir(a)
    Proj.field(a, ex, ey, ez, fx, fy, fz, FIELD.len, FIELD.radius)
    Actors.layer(a, "matrix")
  else
    if s.field_on then Actors.layer_off(a) end
    s.field_on = false
    if s.field_wait > 0 then s.field_wait = s.field_wait - DT
    else s.field = min(FIELD.max, s.field + FIELD.regen * DT) end
  end
  -- Swarm Rockets
  if c.ab2_p and s.rocket_cd <= 0 and s.rockets <= 0 then
    s.rockets, s.rocket_next = ROCKETS.n, 0
    s.rocket_cd = ROCKETS.cd
    Actors.layer(a, "missiles", true)
  end
  if s.rockets > 0 then
    s.rocket_next = s.rocket_next - DT
    while s.rocket_next <= 0 and s.rockets > 0 do
      launch_rocket(a)
      s.rockets = s.rockets - 1
      s.rocket_next = s.rocket_next + ROCKETS.time / ROCKETS.n
    end
  end
  -- Fusion Cannons (not while flying or holding the field)
  local firing = c.fire and not boosting and not s.field_on
  if firing then
    if s.fire_cd <= 0 then fire_cannons(a) end
    if not s.field_on and s.rockets <= 0 then Actors.layer(a, "fire") end
  elseif a.anim.layer == "fire" then
    Actors.layer_off(a)
  end
  s.firing = firing
  -- walking (slower while firing), jumping, crouching
  if not boosting then
    Actors.move(a, c, firing and (1 - CANNON.slow) or 1)
    if c.jump_p and a.on_ground then
      a.vy = a.form.jump
      a.on_ground = false
      a.anim.base, a.anim.bt = "jump", 0
    end
  end
  a.crouch = approach(a.crouch, (c.crouch and not boosting) and 1 or 0, DT * 6)
  Actors.physics(a, boosting and 0 or 1)
  if boosting and a.hit_wall then s.boost_t = math.min(s.boost_t, 0.05) end
end

function R.update_pilot(a, c)
  local s = a.st
  -- Pit Stop charges by itself on foot
  a.ult = min(100, a.ult + PITSTOP.charge * DT)
  if s.eject_t and s.eject_t > 0 then
    s.eject_t = s.eject_t - DT
    if s.eject_t <= 0 then a.override = nil end
  end
  if s.call_t and s.call_t > 0 then
    s.call_t = s.call_t - DT
    if s.call_t <= 0 then mech_lands(a) return end
  elseif c.ult_p and a.ult >= 100 then
    pit_stop(a)
  end
  -- Pit Pistol
  if c.fire and s.fire_cd <= 0 and not (s.call_t and s.call_t > 0) then
    s.fire_cd = PISTOL.rate
    local ex, ey, ez = Actors.eye(a)
    local dx, dy, dz = Actors.aim_dir(a)
    local t, who, crit = Actors.shoot(a, ex, ey, ez, dx, dy, dz, 60)
    local hx, hy, hz = ex + dx * (t or 60), ey + dy * (t or 60), ez + dz * (t or 60)
    if who then
      Actors.damage(who, Actors.falloff(PISTOL.dmg, t, PISTOL.near, PISTOL.far, PISTOL.low) * (crit and 2 or 1),
        a, crit, "hitscan")
    end
    local sy, cy = sin(a.yaw), cos(a.yaw)
    Fx.tracer(ex + cy * 0.15 + dx * 0.5, ey - 0.15, ez - sy * 0.15 + dz * 0.5, hx, hy, hz, 0x80F8FF, 1, 0.05)
    Fx.burst(hx, hy, hz, 2, 0, 1.5, 0.15, 0.04, 0x80F8FF)
    a.muzzle_t = 0.05
    Actors.layer(a, "fire")
    if a == G.local_actor then Snd.play("pistol") end
  elseif not c.fire and a.anim.layer == "fire" then
    Actors.layer(a, "aim")
  end
  Actors.move(a, c)
  if c.jump_p and a.on_ground then
    a.vy = a.form.jump
    a.on_ground = false
    a.anim.base, a.anim.bt = "jump", 0
  end
  a.crouch = approach(a.crouch, c.crouch and 1 or 0, DT * 8)
  Actors.physics(a)
end

-- the mech's health at zero: the pilot ejects instead of dying
function R.on_lethal(a, src)
  if a.form == MECH then
    R.eject(a, true)
    return true
  end
  return false
end

function R.on_eat(a, p)
  -- Null Field eats a projectile: a spark and a little ultimate
  a.ult = min(100, a.ult + 0.5)
end

function R.on_death(a)
  a.override = nil
end

-- ---------------------------------------------------------------- first person

local fp_mesh = {}

function R.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  local f = a.form
  local m = fp_mesh[f.fp]
  if not m then
    m = model(f.fp)
    fp_mesh[f.fp] = m
  end
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if a.fp_clip then
    a.fp_t = (a.fp_t or 0) + DT
    if a.fp_t > 0.5 then a.fp_clip = nil end
  end
  local layer, lt = nil, 0
  if f == MECH then
    if s.boost_t > 0 then base = "boost"
    elseif a.override == "eject" then base = "eject"
    elseif speed > 1 and a.on_ground then base, t = "walk", G.t * speed / 5.5 end
    if s.field_on then layer, lt = "matrix", G.t
    elseif s.firing then layer, lt = "fire", G.t
    elseif s.rockets > 0 then layer, lt = "missiles", ROCKETS.time - s.rockets * ROCKETS.time / ROCKETS.n end
  else
    if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 6 end
    if s.fire_cd > 0 then layer, lt = "fire", PISTOL.rate - s.fire_cd end
    if s.call_t and s.call_t > 0 then layer, lt = "call", 0.9 - s.call_t end
  end
  if a.fp_clip then
    animate(m, a.fp_clip, a.fp_t)
  elseif layer then
    animate(m, base, t, layer, lt, 1)
  else
    animate(m, base, t)
  end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
  -- muzzle flash at the tip of the cannon that fired (over the cannon, in 2D)
  if a.muzzle_t and a.muzzle_t > 0 then
    a.muzzle_t = a.muzzle_t - DT
    local bone = f == MECH and (a.muzzle_side == 1 and "gun.L" or "gun.R") or "hand.R"
    local _, _, _, tx, ty, tz = bone3d(m, bone)
    if tx then
      -- the model's frame is the camera's: its point to the world
      local cp, sp, cyw, syw = cos(pitch), sin(pitch), cos(yaw), sin(yaw)
      local y2, z2 = ty * cp + tz * sp, -ty * sp + tz * cp
      local px, py, pz = cx + tx * cyw + z2 * syw, cy + y2, cz - tx * syw + z2 * cyw
      local sx, sy = project3d(px, py, pz)
      if sx then
        circfill(floor(sx), floor(sy), f == MECH and 8 or 3, 0xFFE8A0)
        circfill(floor(sx), floor(sy), f == MECH and 4 or 1, 0xFFFFFF)
      end
    end
  end
end

-- the Null Field seen from outside (and from inside): an orange cone
local field_mesh
local function make_field()
  local v, f = { 0, 0, 0 }, {}
  local N = 8
  for i = 0, N - 1 do
    local an = 2 * pi * i / N
    v[#v + 1] = cos(an) * FIELD.radius
    v[#v + 1] = sin(an) * FIELD.radius
    v[#v + 1] = FIELD.len
  end
  local col = 0xFF9A3C | 0x40000000 | 0x10000000    -- emissive, screen-door
  for i = 0, N - 1 do
    local a, b = 2 + i, 2 + (i + 1) % N
    f[#f + 1] = 1 f[#f + 1] = a f[#f + 1] = b f[#f + 1] = col
    f[#f + 1] = 1 f[#f + 1] = b f[#f + 1] = a f[#f + 1] = col
  end
  field_mesh = mesh(v, f)
end

function R.draw_extra(a, d)
  if a.form == MECH and a.st.field_on then
    if not field_mesh then make_field() end
    local ex, ey, ez = Actors.eye(a)
    draw3d(field_mesh, ex, ey, ez, -a.pitch, a.yaw, 0, 1, 2)
  end
end

function R.draw_fp_extra(a, cx, cy, cz, yaw, pitch)
  if a.form == MECH and a.st.field_on then
    if not field_mesh then make_field() end
    draw3d(field_mesh, cx, cy - 0.4, cz, -pitch, yaw, 0, 1, 2)
  end
end
