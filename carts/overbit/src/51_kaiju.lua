-- Kaiju, tank (the kit of D.Mon, a second mech pilot). In the mech BIG RED:
-- Plasma Saber (primary: wide swings, left and right), Fusion Repeater
-- (secondary: the gun of the right arm, with a magazine), Propulsors
-- (ability 1: short dashes, three charges of fuel), Power Barrier (ability
-- 2, held: a hexagon wall in front; the primary becomes Surging Strike, a
-- lunge with the barrier), Limit Break (ultimate: a full circle of the
-- blade); when the mech breaks the pilot jumps out (passive). On foot: the
-- Mini Repeater and Call Mech (ultimate), as Rally's pilot.

local K = {
  id = "kaiju", name = "Kaiju", short = "KJU", role = "tank", rgb = 0xD8282E,
  ult_cost = 1500,
  desc = "A little monster in a big red mech",
}
H.kaiju = K
HERO_ORDER[#HERO_ORDER + 1] = "kaiju"

local MECH = {
  name = "mech", tp = "kaiju_mech", fp = "kaiju_fp",
  hp = 400, armor = 300, speed = 5.0, radius = 1.15, height = 3.05, eye = 2.5, crouch_eye = 2.1,
  jump = 5.8, head = "dome", corpse = 0.6, cross = "melee",
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 3.0, run_speed = 5.0, jump_len = 0.45,
  hips_bone = "hips", body_bone = "body", aim_bones = { "arm.L", "arm.R" }, aim_k = 0.6, layer_bone = "body",
}
local PILOT = {
  name = "pilot", tp = "kaiju_pilot", fp = "kaiju_pfp",
  hp = 150, armor = 0, speed = 6.0, radius = 0.3, height = 1.2, eye = 1.0, crouch_eye = 0.7,
  jump = 6.6, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 3.0, run_speed = 6.0, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.7, layer_bone = "chest",
}
K.forms = { mech = MECH, pilot = PILOT }

local SABER = { rate = 0.66, dmg = 65, reach = 4.0, half = 0.95, hit_t = 0.24 }
local REPEATER = { rate = 0.128, dmg = 12, ammo = 30, reload = 1.6, near = 15, far = 30, low = 0.5, spread = 0.018,
                   slow = 0.2 }
local DASH = { charges = 3, time = 0.32, speed = 16, recharge = 2.4, hit = 15, push = 8 }
local BARRIER = { hp = 650, regen = 140, delay = 2.0, broken = 5.0, dist = 2.3, w = 1.9, h = 1.45, slow = 0.3 }
local STRIKE = { dmg = 70, reach = 3.8, half = 0.8, cd = 1.2, lunge = 13, time = 0.28, push = 9 }
local LIMIT = { len = 1.2, hit_t = 0.55, dmg = 220, radius = 7.5, push = 11 }
local MINI = { rate = 0.128, dmg = 12, near = 12, far = 25, low = 0.5 }
local CALL = { charge = 6.0, dmg = 250, radius = 4.5 }

K.numbers = { saber = SABER, repeater = REPEATER, dash = DASH, barrier = BARRIER, strike = STRIKE, limit = LIMIT,
              mini = MINI, call = CALL }

K.hud = {
  { key = "ab1", name = "Propulsors", icon = 4, state = function(a)
      local s = a.st
      if a.form ~= MECH then return 0, 1, false, false end
      local left = s.fuel >= 1 and 0 or (1 - s.fuel) * DASH.recharge
      return left, DASH.recharge, (s.dash_t or 0) > 0, true, floor(s.fuel) end },
  { key = "ab2", name = "Power Barrier", icon = 5, state = function(a)
      local s = a.st
      if a.form ~= MECH then return 0, 1, false, false end
      return s.bbroken or 0, BARRIER.broken, s.barrier_on, true end },
  { key = "fire2", name = "Fusion Repeater", ammo = function(a)
      if a.form ~= MECH then return nil end
      return a.st.ammo, REPEATER.ammo, (a.st.reload_t or 0) > 0 end },
  { key = "ab2", name = "Power Barrier", meter = function(a)
      if a.form ~= MECH or (a.st.bhp >= BARRIER.hp and not a.st.barrier_on) then return nil end
      return a.st.bhp / BARRIER.hp, a.st.barrier_on end },
}

K.ult_name = function(a) return a.form == MECH and "LIMIT BREAK" or "CALL MECH" end

function K.spawn(a)
  Actors.set_form(a, MECH)
  local s = a.st
  s.fire_cd, s.fuel, s.dash_t, s.ammo, s.reload_t = 0, DASH.charges, 0, REPEATER.ammo, 0
  s.bhp, s.bwait, s.bbroken, s.barrier_on, s.strike_cd, s.strike_t = BARRIER.hp, 0, 0, false, 0, 0
  s.swing, s.side = false, 1
  a.override = nil
  a.hidden = false
  Proj.barrier_off(a)
end

-- a point of the mech's model in the world (bone tail, or head)
local function bone_world(a, bone, head)
  local hx, hy, hz, tx, ty, tz = bone3d(a.mesh, bone)
  if not hx then return a.x, a.y + 1.5, a.z end
  if not head then hx, hy, hz = tx, ty, tz end
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return a.x + hx * cy + hz * sy, a.y + hy, a.z - hx * sy + hz * cy
end

-- ---------------------------------------------------------------- melee: saber, strike, Limit Break

local function knock(a, o, k, up)
  local dx, dz = o.x - a.x, o.z - a.z
  local d = sqrt(dx * dx + dz * dz)
  if d < 0.01 then dx, dz, d = sin(a.yaw), cos(a.yaw), 1 end
  Actors.push(o, dx / d * k, up or 2.5, dz / d * k)
end

local function saber_hit(a)
  local hits = Actors.cone(a, SABER.reach, SABER.half, 2.0)
  for _, o in ipairs(hits) do
    Actors.damage(o, SABER.dmg, a, false, "melee")
    Fx.burst(o.x, o.y + o.height * 0.6, o.z, 10, 0, 3, 0.3, 0.07, 0x7CFFB0)
  end
  if #hits > 0 then
    if a == G.local_actor then Snd.play("strike", 0.8) end
  end
end

local function strike_hit(a)
  for _, o in ipairs(Actors.cone(a, STRIKE.reach, STRIKE.half, 2.0)) do
    if not a.st.struck[o] then
      a.st.struck[o] = true
      Actors.damage(o, STRIKE.dmg, a, false, "melee")
      knock(a, o, STRIKE.push, 3)
      Fx.burst(o.x, o.y + o.height * 0.5, o.z, 12, 0, 4, 0.3, 0.08, 0x60FF90)
    end
  end
end

local function limit_hit(a)
  local s = a.st
  log("overbit limit break " .. a.name)
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local d = len3(o.x - a.x, (o.y - a.y) * 0.5, o.z - a.z) - o.radius
      if d < LIMIT.radius and World.clear(a.x, a.y + 1.4, a.z, o.x, o.y + o.height * 0.5, o.z) then
        Actors.damage(o, LIMIT.dmg, a, false, "ult")
        knock(a, o, LIMIT.push, 5)
      end
    end
  end
  Fx.ring(a.x, a.y + 1.3, a.z, 1.4, 36, 0x7CFFB0, 16)
  Fx.ring(a.x, a.y + 1.0, a.z, 1.0, 24, 0xFFFFFF, 11)
  Fx.light(a.x, a.y + 1.5, a.z, 10, 1.6, 0x60FFA0, 0.4)
  Fx.shake = max(Fx.shake, a == G.local_actor and 0.8 or 0.3)
  s.limit_hit = true
  Snd.play("boom")
end

-- a little green trail behind the tip of the blade while it swings
local function blade_trail(a)
  if random() > 0.9 * Fx.density then return end
  local x, y, z = bone_world(a, "blade")
  Fx.spawn(x, y, z, 0, 0, 0, 0.16, 0.16, 0x7CFFB0, 3, 0)
  local hx, hy, hz = bone_world(a, "blade", true)
  Fx.spawn((x + hx) * 0.5, (y + hy) * 0.5, (z + hz) * 0.5, 0, 0, 0, 0.12, 0.12, 0xC8FFE0, 3, 0)
end

-- ---------------------------------------------------------------- Power Barrier

local function barrier_hit(b, amount, src)
  local a = b.owner
  local s = a.st
  local d = min(s.bhp, amount)
  s.bhp = s.bhp - amount
  s.bwait, s.bflash = BARRIER.delay, 0.1
  if src and src ~= a then
    src.ult = min(100, src.ult + d / src.hero.ult_cost * 100)
    src.hit_marker, src.hit_marker_t = 1, 0.12
  end
  if s.bhp <= 0 then
    s.bhp, s.bbroken, s.barrier_on = 0, BARRIER.broken, false
    Fx.burst(b.x, b.y, b.z, 24, 0, 4, 0.5, 0.1, 0x60FF90, 1)
    Proj.barrier_off(a)
    Actors.layer_off(a)
    log("overbit barrier broken " .. a.name)
    Snd.play("shatter")
  end
  return d
end

local function place_barrier(a)
  local fx, fz = sin(a.yaw), cos(a.yaw)
  Proj.barrier(a, a.x + fx * BARRIER.dist, a.y + 1.55 - a.crouch * 0.3, a.z + fz * BARRIER.dist, fx, fz,
    BARRIER.w, BARRIER.h, barrier_hit)
end

-- ---------------------------------------------------------------- the repeater

local function fire_repeater(a, mech)
  local s = a.st
  local g = mech and REPEATER or MINI
  s.fire_cd = g.rate
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sp = mech and REPEATER.spread or 0.012
  local dx, dy, dz = fx + (grandom() - 0.5) * sp, fy + (grandom() - 0.5) * sp, fz + (grandom() - 0.5) * sp
  local l = len3(dx, dy, dz)
  dx, dy, dz = dx / l, dy / l, dz / l
  local t, who, crit, nx, ny, nz = Actors.shoot(a, ex, ey, ez, dx, dy, dz, 60)
  local hx, hy, hz = ex + dx * (t or 60), ey + dy * (t or 60), ez + dz * (t or 60)
  if who then
    Actors.damage(who, Actors.falloff(g.dmg, t, g.near, g.far, g.low) * (crit and 2 or 1), a, crit, "hitscan")
    Fx.burst(hx, hy, hz, 2, 0, 1.5, 0.15, 0.05, 0x80FFB0)
  elseif t then
    Fx.burst(hx, hy, hz, 2, 1, 1.2, 0.2, 0.04, 0xC0FFD0, 2, nx, ny, nz)
  end
  local mx, my, mz
  if mech then mx, my, mz = bone_world(a, "fore.R")
  else
    local sy, cy = sin(a.yaw), cos(a.yaw)
    mx, my, mz = ex + cy * 0.12 + dx * 0.4, ey - 0.15, ez - sy * 0.12 + dz * 0.4
  end
  Fx.tracer(mx, my, mz, hx, hy, hz, 0x60FF90, 1, 0.05)
  a.muzzle_t = 0.05
  if a == G.local_actor then Snd.play("repeater", 0.8) end
end

-- ---------------------------------------------------------------- eject and Call Mech

function K.eject(a, destroyed)
  log(string.format("overbit eject %s%s", a.name, destroyed and " (mech destroyed)" or ""))
  local ult = a.ult
  local x, y, z = a.x, a.y + 2.2, a.z
  Proj.barrier_off(a)
  Actors.set_form(a, PILOT)
  a.x, a.y, a.z = x, y, z
  local fx, _, fz = Actors.aim_dir(a)
  a.vx, a.vy, a.vz = -fx * 3, 9.5, -fz * 3
  a.on_ground = false
  a.ult = ult
  a.st.fire_cd = 0
  a.override = "eject"
  a.anim.base, a.anim.bt = "eject", 0
  a.st.eject_t = 0.8
  a.fp_clip, a.fp_t = "raise", 0
  Fx.explode(x, y - 0.8, z, 2.4, 0xFF7050)
  if a == G.local_actor then Snd.play("eject") end
end

local function call_mech(a)
  a.ult = 0
  a.st.call_t = 0.9
  a.override = "call"
  a.anim.base, a.anim.bt = "call", 0
  if a == G.local_actor then Snd.play("call") end
end

local function mech_lands(a)
  log("overbit call mech " .. a.name)
  Actors.set_form(a, MECH)
  local s = a.st
  a.vx, a.vy, a.vz = 0, -2, 0
  a.override = "callmech"
  a.anim.base, a.anim.bt = "callmech", 0
  s.land_t = 0.8
  s.fuel, s.ammo, s.reload_t, s.bhp, s.bbroken = DASH.charges, REPEATER.ammo, 0, BARRIER.hp, 0
  Fx.explode(a.x, a.y + 0.3, a.z, 1.6, 0xFF9070)
  Actors.splash(a, a.x, a.y + 0.5, a.z, CALL.radius, CALL.dmg, 0, 1, "ult")
  a.fp_clip, a.fp_t = "raise", 0
end

-- ---------------------------------------------------------------- the update

local TIMERS = { "fire_cd", "strike_cd", "bbroken", "bflash" }  -- once, not every frame
function K.update(a, c)
  local s = a.st
  if not a.alive then
    Proj.barrier_off(a)
    return
  end
  for _, k in ipairs(TIMERS) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if a.form == MECH then
    K.update_mech(a, c)
  else
    K.update_pilot(a, c)
  end
end

function K.update_mech(a, c)
  local s = a.st
  a.ult = min(100, a.ult + 0.4 * DT)
  -- fuel and the barrier's health come back by themselves
  if s.fuel < DASH.charges then s.fuel = min(DASH.charges, s.fuel + DT / DASH.recharge) end
  if not s.barrier_on then
    if s.bwait > 0 then s.bwait = s.bwait - DT
    elseif s.bbroken <= 0 then s.bhp = min(BARRIER.hp, s.bhp + BARRIER.regen * DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = REPEATER.ammo end
  end
  -- landing after Call Mech, Limit Break: no control
  if s.land_t and s.land_t > 0 then
    s.land_t = s.land_t - DT
    if s.land_t <= 0 then a.override = nil end
    Actors.move(a, Input.blank(), 0)
    Actors.physics(a)
    return
  end
  if s.limit_t then
    s.limit_t = s.limit_t + DT
    if s.limit_t > 0.2 and s.limit_t < 0.95 then blade_trail(a) end
    if not s.limit_hit and s.limit_t >= LIMIT.hit_t then limit_hit(a) end
    if s.limit_t >= LIMIT.len then s.limit_t, a.override = nil, nil end
    Actors.move(a, Input.blank(), 0)
    Actors.physics(a)
    return
  end
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    s.limit_t, s.limit_hit, s.swing, s.barrier_on = 0, false, false, false
    Proj.barrier_off(a)
    Actors.layer_off(a)
    a.override = "limit"
    a.anim.base, a.anim.bt = "limit", 0
    if a == G.local_actor then Snd.play("limit") end
    return
  end
  -- Propulsors: a dash along the stick (or forward), one charge of fuel
  if c.ab1_p and s.fuel >= 1 and s.dash_t <= 0 and not s.barrier_on then
    s.fuel = s.fuel - 1
    s.dash_t, s.dash_hit = DASH.time, {}
    local sy, cy = sin(a.yaw), cos(a.yaw)
    local mx, mz = c.mx, c.mz
    if mx * mx + mz * mz < 0.1 then mx, mz = 0, 1 end
    local l = sqrt(mx * mx + mz * mz)
    s.dash_x, s.dash_z = (mz * sy + mx * cy) / l, (mz * cy - mx * sy) / l
    a.override = "dash"
    if a == G.local_actor then Snd.play("dash") end
  end
  local dashing = s.dash_t > 0
  if dashing then
    s.dash_t = s.dash_t - DT
    a.vx, a.vz = s.dash_x * DASH.speed, s.dash_z * DASH.speed
    if a.vy < 0 then a.vy = a.vy * 0.5 end
    for _, o in ipairs(Actors.list) do
      if o.alive and o.team ~= a.team and not s.dash_hit[o] and
         len3(o.x - a.x, (o.y + o.height * 0.5) - (a.y + a.height * 0.5), o.z - a.z) < a.radius + o.radius + 0.3 then
        s.dash_hit[o] = true
        Actors.damage(o, DASH.hit, a, false, "melee")
        Actors.push(o, s.dash_x * DASH.push, 3, s.dash_z * DASH.push)
        if a == G.local_actor then Snd.play("bump") end
      end
    end
    if random() < 0.9 * Fx.density then
      local sy, cy = sin(a.yaw), cos(a.yaw)
      for k = -1, 1, 2 do
        Fx.spawn(a.x + k * 0.5 * cy - 0.9 * sy, a.y + 1.4, a.z - k * 0.5 * sy - 0.9 * cy,
          -s.dash_x * 4 + random() - 0.5, random() - 0.5, -s.dash_z * 4 + random() - 0.5, 0.25, 0.2, 0xFF9050, 2, 0)
      end
    end
    if s.dash_t <= 0 then a.override = nil end
  end
  -- Power Barrier (held); the primary becomes Surging Strike
  local want_barrier = c.ab2 and s.bbroken <= 0 and s.bhp > 1 and not dashing
  if want_barrier and not s.barrier_on then
    s.barrier_on, s.swing = true, false
    if a == G.local_actor then Snd.play("barrier") end
  elseif not want_barrier and s.barrier_on then
    s.barrier_on = false
    s.bwait = BARRIER.delay
    Proj.barrier_off(a)
    Actors.layer_off(a)
  end
  if s.barrier_on then
    place_barrier(a)
    if s.strike_t <= 0 then Actors.layer(a, "barrier") end
    if c.fire_p and s.strike_cd <= 0 then
      s.strike_cd, s.strike_t, s.struck = STRIKE.cd, STRIKE.time, {}
      Actors.layer(a, "strike", true)
      if a == G.local_actor then Snd.play("strike") end
    end
  end
  if s.strike_t > 0 then
    s.strike_t = s.strike_t - DT
    local fx, fz = sin(a.yaw), cos(a.yaw)
    a.vx, a.vz = fx * STRIKE.lunge, fz * STRIKE.lunge
    strike_hit(a)
  end
  -- Plasma Saber: swings while the primary is held
  local busy = s.barrier_on or dashing
  if s.swing then
    s.swing_t = s.swing_t + DT
    blade_trail(a)
    if not s.swing_hit and s.swing_t >= SABER.hit_t then
      s.swing_hit = true
      saber_hit(a)
    end
    if s.swing_t >= SABER.rate then
      if c.fire and not busy then
        s.swing_t, s.swing_hit, s.side = s.swing_t - SABER.rate, false, 3 - s.side
        if a == G.local_actor then Snd.play("saber") end
      else
        s.swing = false
        Actors.layer_off(a)
      end
    end
  elseif c.fire and not busy and s.fire_cd <= 0 then
    s.swing, s.swing_t, s.swing_hit = true, 0, false
    Actors.layer(a, "saber", true)
    a.anim.lt = s.side == 2 and SABER.rate or 0
    if a == G.local_actor then Snd.play("saber") end
  end
  -- Fusion Repeater: the secondary, with a magazine
  local shooting = c.fire2 and not busy and not s.swing and s.reload_t <= 0
  if shooting then
    if s.ammo <= 0 then
      s.reload_t = REPEATER.reload
    elseif s.fire_cd <= 0 then
      s.ammo = s.ammo - 1
      fire_repeater(a, true)
      if s.ammo <= 0 then s.reload_t = REPEATER.reload end
    end
    Actors.layer(a, "repeater")
  elseif a.anim.layer == "repeater" then
    Actors.layer_off(a)
  end
  if c.reload_p and s.ammo < REPEATER.ammo and s.reload_t <= 0 then s.reload_t = REPEATER.reload end
  s.shooting = shooting
  -- walking, jumping, crouching
  if not dashing and s.strike_t <= 0 then
    local k = 1
    if s.barrier_on then k = 1 - BARRIER.slow elseif shooting then k = 1 - REPEATER.slow end
    Actors.move(a, c, k)
    if c.jump_p and a.on_ground then
      a.vy = a.form.jump
      a.on_ground = false
      a.anim.base, a.anim.bt = "jump", 0
    end
  end
  a.crouch = approach(a.crouch, (c.crouch and not dashing) and 1 or 0, DT * 6)
  Actors.physics(a)
  if s.barrier_on then place_barrier(a) end      -- where the mech is now
end

function K.update_pilot(a, c)
  local s = a.st
  a.ult = min(100, a.ult + CALL.charge * DT)
  if s.eject_t and s.eject_t > 0 then
    s.eject_t = s.eject_t - DT
    if s.eject_t <= 0 then a.override = nil end
  end
  if s.call_t and s.call_t > 0 then
    s.call_t = s.call_t - DT
    if s.call_t <= 0 then mech_lands(a) return end
  elseif c.ult_p and a.ult >= 100 then
    call_mech(a)
  end
  if c.fire and s.fire_cd <= 0 and not (s.call_t and s.call_t > 0) then
    fire_repeater(a, false)
    Actors.layer(a, "fire")
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

function K.on_lethal(a, src)
  if a.form == MECH then
    K.eject(a, true)
    return true
  end
  return false
end

function K.on_death(a)
  a.override = nil
  Proj.barrier_off(a)
end

-- ---------------------------------------------------------------- drawing

-- the barrier: a honeycomb of seven hexagons, glowing and see-through
local barrier_mesh
local function make_barrier()
  local v, f = {}, {}
  local r = 0.62
  local col = 0x5CFF9C | 0x40000000 | 0x10000000
  local rim = 0xB8FFD8 | 0x40000000
  local function vert(x, y)
    v[#v + 1] = x v[#v + 1] = y v[#v + 1] = 0
    return #v // 3
  end
  local function tri2(a, b, c, colour)
    f[#f + 1] = a f[#f + 1] = b f[#f + 1] = c f[#f + 1] = colour
    f[#f + 1] = a f[#f + 1] = c f[#f + 1] = b f[#f + 1] = colour
  end
  local cells = { { 0, 0 } }
  for i = 0, 5 do
    local an = pi / 3 * i
    cells[#cells + 1] = { cos(an) * r * 1.73, sin(an) * r * 1.73 }
  end
  for _, cell in ipairs(cells) do
    local cx, cy = cell[1] * 1.15, cell[2] * 0.95
    local c0 = vert(cx, cy)
    local ring, inner = {}, {}
    for i = 0, 5 do
      local an = pi / 3 * i + pi / 6
      ring[i] = vert(cx + cos(an) * r, cy + sin(an) * r * 0.85)
      inner[i] = vert(cx + cos(an) * r * 0.8, cy + sin(an) * r * 0.68)
    end
    for i = 0, 5 do
      local j = (i + 1) % 6
      tri2(c0, inner[i], inner[j], col)
      tri2(inner[i], ring[i], ring[j], rim)
      tri2(inner[i], ring[j], inner[j], rim)
    end
  end
  barrier_mesh = mesh(v, f)
end

local function draw_barrier(a)
  local b = a.barrier
  if not b then return end
  if not barrier_mesh then make_barrier() end
  draw3d(barrier_mesh, b.x, b.y, b.z, 0, a.yaw, 0, 1, 2)
end

function K.draw_extra(a, d)
  draw_barrier(a)
end

function K.draw_fp_extra(a)
  draw_barrier(a)
end

local fp_mesh = {}

function K.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
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
    if speed > 1 and a.on_ground then base, t = "walk", G.t * speed / 5.0 end
    if s.limit_t then layer, lt = "limit", s.limit_t
    elseif s.strike_t > 0 then layer, lt = "strike", STRIKE.time - s.strike_t
    elseif s.barrier_on then layer, lt = "barrier", G.t
    elseif s.swing then layer, lt = "saber", s.swing_t + (s.side == 2 and SABER.rate or 0)
    elseif s.shooting then layer, lt = "repeater", G.t end
  else
    if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 6 end
    if s.fire_cd > 0 then layer, lt = "fire", MINI.rate - s.fire_cd end
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
  if a.muzzle_t and a.muzzle_t > 0 then
    a.muzzle_t = a.muzzle_t - DT
    local _, _, _, tx, ty, tz = bone3d(m, f == MECH and "muzzle" or "gun")
    if tx then
      local cp, sp, cyw, syw = cos(pitch), sin(pitch), cos(yaw), sin(yaw)
      local y2, z2 = ty * cp + tz * sp, -ty * sp + tz * cp
      local px, py, pz = cx + tx * cyw + z2 * syw, cy + y2, cz - tx * syw + z2 * cyw
      local sx, sy = project3d(px, py, pz)
      if sx then
        circfill(floor(sx), floor(sy), 4, 0x80FFB0)
        circfill(floor(sx), floor(sy), 2, 0xFFFFFF)
      end
    end
  end
end
