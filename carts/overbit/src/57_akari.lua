-- Akari, support (the kit of Kiriko). Healing Ofuda (primary: charms that
-- find the friend nearest the crosshair and heal), Kunai (secondary: a
-- thrown blade, three times on the head), Swift Step (ability 1: she
-- appears next to a friend in her sight), Protection Suzu (ability 2: a
-- bell that makes the friends around it untouchable for a moment, heals
-- them and takes their troubles away), Kitsune Rush (ultimate: her spirit
-- fox runs ahead and leaves a road of gates: friends on it move faster,
-- shoot faster and get their abilities back sooner).

local K = {
  id = "akari", name = "Akari", short = "AKR", role = "support", rgb = 0x5FE8FF,
  ult_cost = 2000,
  desc = "A shrine guardian with paper charms",
}
H.akari = K
HERO_ORDER[#HERO_ORDER + 1] = "akari"

local FORM = {
  name = "akari", tp = "akari", fp = "akari_fp",
  hp = 225, armor = 0, speed = 5.5, radius = 0.34, height = 1.68, eye = 1.54, crouch_eye = 1.1,
  jump = 6.4, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
K.forms = { akari = FORM }

local OFUDA = { ammo = 12, volley = 4, rate = 0.45, heal = 13, speed = 26, cone = 0.45, range = 30, reload = 1.0 }
local KUNAI = { ammo = 15, rate = 0.5, dmg = 45, crit = 3, speed = 80 }
local STEP = { cd = 7, range = 35, cone = 0.4 }
local SUZU = { cd = 15, speed = 30, radius = 4, heal = 80, invuln = 0.65 }
local RUSH = { len = 30, run = 1.5, time = 10, width = 4.5, haste = 0.3, gate = 6 }

K.numbers = { ofuda = OFUDA, kunai = KUNAI, step = STEP, suzu = SUZU, rush = RUSH }

K.hud = {
  { key = "ab1", name = "Swift Step", icon = 10, state = function(a)
      return a.st.step_cd, STEP.cd, false, true end },
  { key = "ab2", name = "Protection Suzu", icon = 11, state = function(a)
      return a.st.suzu_cd, SUZU.cd, false, true end },
  { key = "fire", name = "Healing Ofuda", ammo = function(a)
      return a.st.ofuda, OFUDA.ammo, a.st.reload_t > 0 end },
}

K.ult_name = function(a) return "KITSUNE RUSH" end

function K.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ofuda, s.kunai, s.reload_t, s.step_cd, s.suzu_cd = 0, OFUDA.ammo, KUNAI.ammo, 0, 0, 0
  s.kunai_cd, s.volley, s.volley_t = 0, 0, 0
  s.rush = nil
  a.override = nil
end

local function hand(a, side)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return ex + cy * 0.2 * side + fx * 0.4, ey - 0.2 + fy * 0.4, ez - sy * 0.2 * side + fz * 0.4
end

-- the friend nearest to the crosshair (in a cone, in range); see: the wall must not hide them
local function friend_in_sight(a, cone, range, see)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local best, who = cone, nil
  for _, o in ipairs(Actors.list) do
    if o ~= a and o.alive and o.team == a.team then
      local tx, ty, tz = o.x - ex, o.y + o.height * 0.6 - ey, o.z - ez
      local d = len3(tx, ty, tz)
      if d < range and d > 0.5 then
        local off = math.acos(clamp((tx * fx + ty * fy + tz * fz) / d, -1, 1))
        if off < best and (not see or World.clear(ex, ey, ez, o.x, o.y + o.height * 0.6, o.z)) then best, who = off, o end
      end
    end
  end
  return who
end

-- ---------------------------------------------------------------- charms and kunai

local function throw_ofuda(a)
  local s = a.st
  s.ofuda = s.ofuda - 1
  local target = friend_in_sight(a, OFUDA.cone, OFUDA.range, true)
  local x, y, z = hand(a, 1)
  local fx, fy, fz = Actors.aim_dir(a)
  local j = (random() - 0.5) * 0.3
  Proj.spawn({ x = x, y = y, z = z, vx = (fx + j * cos(a.yaw)) * OFUDA.speed, vy = (fy + 0.1) * OFUDA.speed,
               vz = (fz - j * sin(a.yaw)) * OFUDA.speed, owner = a, target = target, homing = target and 8 or nil,
               any_team = true, life = 1.6, size = 0.08, kind = "ofuda",
               draw = function(p)
                 local l = len3(p.vx, p.vy, p.vz)
                 line3d(p.x, p.y, p.z, p.x - p.vx / l * 0.15, p.y - p.vy / l * 0.15, p.z - p.vz / l * 0.15, 0xFFF8E8, 2)
                 point3d(p.x, p.y, p.z, 0.03, 0xD8323A)
               end,
               on_hit = function(p, hx, hy, hz, direct)
                 if direct and not direct.is_barrier and direct.team == a.team then
                   local d = Actors.heal(direct, OFUDA.heal, a)
                   if G.dmg_numbers and d > 0 then Fx.number(hx, hy + 0.3, hz, floor(d + 0.5), false, true) end
                   Fx.burst(hx, hy, hz, 4, 0, 1.2, 0.3, 0.06, 0x8FFFC0, -0.5)
                 end
               end })
  if a == G.local_actor then Snd.play("ui", 0.4) end
end

local function throw_kunai(a)
  local s = a.st
  s.kunai = s.kunai - 1
  s.kunai_cd = KUNAI.rate
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  Proj.spawn({ x = ex + fx * 0.4, y = ey + fy * 0.4 - 0.04, z = ez + fz * 0.4, vx = fx * KUNAI.speed, vy = fy * KUNAI.speed,
               vz = fz * KUNAI.speed, owner = a, dmg = KUNAI.dmg, headshot = true, crit_k = KUNAI.crit, life = 1.0,
               size = 0.04, grav = 3, kind = "kunai",
               draw = function(p)
                 local l = len3(p.vx, p.vy, p.vz)
                 line3d(p.x, p.y, p.z, p.x - p.vx / l * 0.25, p.y - p.vy / l * 0.25, p.z - p.vz / l * 0.25, 0xB8C0C8, 1)
                 point3d(p.x - p.vx / l * 0.25, p.y - p.vy / l * 0.25, p.z - p.vz / l * 0.25, 0.025, 0xD8323A)
               end,
               on_hit = function(p, x, y, z) Fx.burst(x, y, z, 3, 0, 1.5, 0.2, 0.04, 0xE8ECF0, 1) end })
  Actors.layer(a, "kunai", true)
  s.kunai_anim = 0.5
  if a == G.local_actor then Snd.play("saber", 0.5) end
end

-- ---------------------------------------------------------------- Swift Step, Protection Suzu

local function leaves(x, y, z)
  for _ = 1, floor(14 * Fx.density) do
    Fx.spawn(x + (random() - 0.5) * 0.6, y + random() * 1.6, z + (random() - 0.5) * 0.6, (random() - 0.5) * 2, random() * 2,
      (random() - 0.5) * 2, 0.6, 0.06, random() < 0.5 and 0x5FE8FF or 0xFFF8E8, 1, 0.1)
  end
end

local function swift_step(a)
  local o = friend_in_sight(a, STEP.cone, STEP.range, false)
  if not o then return false end
  local s = a.st
  s.step_cd = STEP.cd
  leaves(a.x, a.y, a.z)
  -- next to them, on the side she came from
  local dx, dz = a.x - o.x, a.z - o.z
  local d = sqrt(dx * dx + dz * dz)
  if d < 0.01 then dx, dz, d = 1, 0, 1 end
  a.x, a.y, a.z = o.x + dx / d * (o.radius + a.radius + 0.3), o.y + 0.05, o.z + dz / d * (o.radius + a.radius + 0.3)
  a.vx, a.vy, a.vz = 0, 0, 0
  leaves(a.x, a.y, a.z)
  Actors.layer(a, "step", true)
  s.step_anim = 0.4
  log("overbit swift step " .. a.name .. " > " .. o.name)
  if a == G.local_actor then Snd.play("dash", 0.7) end
  return true
end

local function suzu_ring(a, x, y, z)
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team == a.team and len3(o.x - x, (o.y + o.height * 0.5 - y) * 0.7, o.z - z) < SUZU.radius then
      o.fx.invuln_t = SUZU.invuln
      o.fx.frozen_t, o.fx.slow_t, o.fx.rooted_t, o.fx.antiheal_t, o.fx.chill = 0, 0, 0, 0, 0
      local d = Actors.heal(o, SUZU.heal, a)
      if G.dmg_numbers and d > 0 then Fx.number(o.x, o.y + o.height + 0.3, o.z, floor(d + 0.5), false, true) end
    end
  end
  Fx.ring(x, y + 0.2, z, 0.5, 24, 0x8FFFC0, 7, 0.5)
  Fx.light(x, y + 1, z, 6, 1.2, 0x8FFFC0, 0.3)
  log("overbit suzu " .. a.name)
  Snd.play("heal")
end

local function throw_suzu(a)
  local s = a.st
  s.suzu_cd = SUZU.cd
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  -- thrown at a friend under the crosshair if there is one, else along the aim
  local o = friend_in_sight(a, 0.25, 30, true)
  if o and len3(o.x - a.x, o.y - a.y, o.z - a.z) < 1.5 then
    suzu_ring(a, a.x, a.y + 0.5, a.z)
  else
    Proj.spawn({ x = ex + fx * 0.4, y = ey + fy * 0.4, z = ez + fz * 0.4, vx = fx * SUZU.speed, vy = fy * SUZU.speed + 2,
                 vz = fz * SUZU.speed, owner = a, target = o, any_team = true, grav = 10, life = 1.5, fuse = true,
                 size = 0.12, kind = "suzu",
                 draw = function(p)
                   point3d(p.x, p.y, p.z, 0.1, 0xF2C230)
                   point3d(p.x, p.y + 0.08, p.z, 0.04, 0xD8323A)
                 end,
                 on_hit = function(p, x, y, z) suzu_ring(a, x, y, z) end })
  end
  Actors.layer(a, "suzu", true)
  s.suzu_anim = 0.5
  if a == G.local_actor then Snd.play("ui") end
end

-- ---------------------------------------------------------------- Kitsune Rush

local function tick_rush(a)
  local s = a.st
  local r = s.rush
  if not r then return end
  r.t = r.t + DT
  if r.t >= RUSH.time then s.rush = nil return end
  r.len = min(RUSH.len, RUSH.len * r.t / RUSH.run)
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team == a.team then
      local dx, dz = o.x - r.x, o.z - r.z
      local along = dx * r.dx + dz * r.dz
      local side = abs(dx * r.dz - dz * r.dx)
      if along > -1 and along < r.len + 1 and side < RUSH.width then
        o.fx.rush_t = 0.25
        o.fx.haste_t, o.fx.haste = 0.25, max(o.fx.haste_t and o.fx.haste_t > 0 and o.fx.haste or 0, RUSH.haste)
      end
    end
  end
end

local fox_m, gate_m
local function make_gate()
  -- a gate of the shrine: two red posts, two beams (the top one longer)
  local v, f = {}, {}
  local function box_(x0, y0, z0, x1, y1, z1, col)
    local b = #v // 3
    for _, p in ipairs({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
                         { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } }) do
      v[#v + 1] = p[1] v[#v + 1] = p[2] v[#v + 1] = p[3]
    end
    for _, q in ipairs({ { 1, 4, 3, 2 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 }, { 4, 8, 7, 3 }, { 1, 5, 8, 4 }, { 2, 3, 7, 6 } }) do
      f[#f + 1] = b + q[1] f[#f + 1] = b + q[2] f[#f + 1] = b + q[3] f[#f + 1] = col
      f[#f + 1] = b + q[1] f[#f + 1] = b + q[3] f[#f + 1] = b + q[4] f[#f + 1] = col
    end
  end
  local red, dark = 0xE8323A | 0x40000000, 0x2A2A30
  box_(-2.0, 0, -0.15, -1.7, 3.6, 0.15, red)
  box_(1.7, 0, -0.15, 2.0, 3.6, 0.15, red)
  box_(-2.4, 3.6, -0.2, 2.4, 3.9, 0.2, dark)
  box_(-2.1, 3.0, -0.12, 2.1, 3.2, 0.12, red)
  gate_m = mesh(v, f)
end

local function draw_rush(a)
  local r = a.st.rush
  if not r then return end
  if not gate_m then make_gate() end
  local yaw = atan(r.dx, r.dz)
  local n = floor(r.len / RUSH.gate)
  for i = 1, n do
    local d = i * RUSH.gate
    local rise = min(1, (r.t - RUSH.run * d / RUSH.len) * 4)
    if rise > 0 then
      draw3d(gate_m, r.x + r.dx * d, r.y - 3.9 * (1 - rise), r.z + r.dz * d, 0, yaw, 0, 1, 0)
    end
  end
  -- the edges of the road, glowing
  local rx, rz = r.dz, -r.dx
  for s2 = -1, 1, 2 do
    line3d(r.x + rx * RUSH.width * s2, r.y + 0.05, r.z + rz * RUSH.width * s2,
      r.x + r.dx * r.len + rx * RUSH.width * s2, r.y + 0.05, r.z + r.dz * r.len + rz * RUSH.width * s2, 0x5FE8FF, 1)
  end
  if r.t < RUSH.run + 0.6 then
    if not fox_m then fox_m = model("akari_fox") end
    animate(fox_m, "run", r.t)
    local d = min(r.len + 1, RUSH.len * r.t / RUSH.run)
    draw3d(fox_m, r.x + r.dx * d, r.y, r.z + r.dz * d, 0, yaw, 0, 1.4, 2)
  end
end

-- ---------------------------------------------------------------- the update

function K.update(a, c)
  local s = a.st
  tick_rush(a)
  if not a.alive then return end
  for _, k in ipairs({ "fire_cd", "kunai_cd", "step_cd", "suzu_cd", "kunai_anim", "suzu_anim", "step_anim", "rush_anim" }) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ofuda, s.kunai = OFUDA.ammo, KUNAI.ammo end
  end
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    local fx, fz = sin(a.yaw), cos(a.yaw)
    s.rush = { x = a.x, y = a.y, z = a.z, dx = fx, dz = fz, t = 0, len = 0 }
    Actors.layer(a, "rush", true)
    s.rush_anim = 1.2
    log("overbit kitsune rush " .. a.name)
    if a == G.local_actor then Snd.play("call") end
  end
  if c.ab1_p and s.step_cd <= 0 then swift_step(a) end
  if c.ab2_p and s.suzu_cd <= 0 then throw_suzu(a) end
  -- the charms: a volley of four, one by one
  local can = s.reload_t <= 0
  if s.volley > 0 then
    s.volley_t = s.volley_t - DT
    if s.volley_t <= 0 and s.ofuda > 0 then
      throw_ofuda(a)
      s.volley, s.volley_t = s.volley - 1, 0.06
    elseif s.ofuda <= 0 then s.volley = 0 end
  elseif can and c.fire and s.fire_cd <= 0 then
    if s.ofuda > 0 then
      s.volley, s.volley_t, s.fire_cd = OFUDA.volley, 0, OFUDA.rate
      Actors.layer(a, "ofuda")
    end
  elseif not c.fire and a.anim.layer == "ofuda" and s.volley <= 0 then
    Actors.layer(a, "aim")
  end
  if can and c.fire2_p and s.kunai_cd <= 0 and s.kunai > 0 then throw_kunai(a) end
  if can and ((s.ofuda <= 0 and c.fire) or (s.kunai <= 0 and c.fire2_p) or
              (c.reload_p and (s.ofuda < OFUDA.ammo or s.kunai < KUNAI.ammo))) then
    s.reload_t = OFUDA.reload
    Actors.layer(a, "reload", true)
  end
  for _, lay in ipairs({ { "reload", "reload_t" }, { "kunai", "kunai_anim" }, { "suzu", "suzu_anim" }, { "step", "step_anim" },
                         { "rush", "rush_anim" } }) do
    if a.anim.layer == lay[1] and (s[lay[2]] or 0) <= 0 then Actors.layer(a, "aim") end
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

-- ---------------------------------------------------------------- drawing

function K.draw_extra(a)
  draw_rush(a)
end

function K.draw_fp_extra(a)
  draw_rush(a)
end

local fp_mesh

function K.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("akari_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.reload_t > 0 then layer, lt = "reload", OFUDA.reload - s.reload_t
  elseif (s.kunai_anim or 0) > 0 then layer, lt = "kunai", 0.5 - s.kunai_anim
  elseif (s.suzu_anim or 0) > 0 then layer, lt = "suzu", 0.5 - s.suzu_anim
  elseif s.volley > 0 or s.fire_cd > OFUDA.rate - 0.25 then layer, lt = "ofuda", G.t end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
end

-- the friend Swift Step would take her to; the kunai left
function K.draw_hud(a)
  local s = a.st
  if s.step_cd <= 0 then
    local o = friend_in_sight(a, STEP.cone, STEP.range, false)
    if o then
      local sx, sy = project3d(o.x, o.y + o.height + 0.3, o.z)
      if sx then
        local x, y = floor(sx), floor(sy)
        tri(x - 4, y - 6, x + 4, y - 6, x, y, 0x5FE8FF)
      end
    end
  end
  local str = tostring(s.kunai)
  print(str, 300 - #str * 6, 118, 0xB8C0C8)
  line(304, 118, 310, 126, 0xB8C0C8)
end
