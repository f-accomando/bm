-- Frost, damage (the kit of Mei). Cryo Blaster: a short stream of frost
-- that slows (primary) or an icicle that flies far and hits hard, double on
-- the head (secondary), one magazine for both; Cryo-Freeze (ability 1: an
-- ice block around her, she heals and nothing hurts her); Ice Wall (ability
-- 2: five pillars of ice across the aim, they stop everyone); Blizzard
-- (ultimate: Pip the drone flies off and makes a storm that slows, hurts
-- and at last freezes the enemies inside).

local F = {
  id = "frost", name = "Frost", short = "FRS", role = "damage", rgb = 0x9FE6FF,
  ult_cost = 1700,
  desc = "A climate scientist with a cryo blaster",
}
H.frost = F
HERO_ORDER[#HERO_ORDER + 1] = "frost"

local FORM = {
  name = "frost", tp = "frost", fp = "frost_fp",
  hp = 275, armor = 0, speed = 5.5, radius = 0.36, height = 1.62, eye = 1.48, crouch_eye = 1.05,
  jump = 6.2, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
F.forms = { frost = FORM }

local STREAM = { tick = 0.1, dmg = 5.5, range = 10, slow = 0.3, cost = 2, ammo = 150, reload = 1.5 }
local ICICLE = { rate = 0.8, cost = 10, speed = 115, dmg = 75 }
local CRYO = { time = 4.0, heal = 37.5, cd = 12 }
local WALL = { cd = 12, range = 20, n = 5, gap = 1.22, hp = 250, life = 4.5 }
local STORM = { speed = 20, range = 30, time = 4.25, radius = 10, slow = 0.5, dps = 20, freeze_after = 1.5, freeze = 2.0 }

F.numbers = { stream = STREAM, icicle = ICICLE, cryo = CRYO, wall = WALL, storm = STORM }

F.hud = {
  { key = "ab1", name = "Cryo-Freeze", icon = 11, state = function(a)
      return a.st.cryo_cd, CRYO.cd, a.st.cryo_t > 0, true end },
  { key = "ab2", name = "Ice Wall", icon = 9, state = function(a)
      return a.st.wall_cd, WALL.cd, false, true end },
  { key = "fire", name = "Cryo Blaster", ammo = function(a)
      return a.st.ammo, STREAM.ammo, a.st.reload_t > 0 end },
}

F.ult_name = function(a) return "BLIZZARD" end

function F.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ammo, s.reload_t, s.ice_cd, s.cryo_cd, s.cryo_t, s.wall_cd = 0, STREAM.ammo, 0, 0, 0, 0, 0
  s.streaming = false
  a.override = nil
end

-- ---------------------------------------------------------------- the blaster

local function nozzle(a)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return ex + cy * 0.18 + fx * 0.6, ey - 0.2 + fy * 0.6, ez - sy * 0.18 + fz * 0.6
end

local function stream_tick(a)
  local s = a.st
  s.fire_cd = STREAM.tick
  s.ammo = s.ammo - STREAM.cost
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local t, who, crit = Actors.shoot(a, ex, ey, ez, fx, fy, fz, STREAM.range)
  if who then
    Actors.damage(who, STREAM.dmg, a, false, "beam")
    if not who.is_barrier then who.fx.slow_t, who.fx.slow = 0.4, max(who.fx.slow or 0, STREAM.slow) end
    -- frost on the target
    Fx.burst(ex + fx * t, ey + fy * t, ez + fz * t, 2, 0, 1.0, 0.3, 0.06, 0xDDF6FF, -0.3)
  end
end

local function stream_fx(a)
  if random() > Fx.density then return end
  local nx, ny, nz = nozzle(a)
  local fx, fy, fz = Actors.aim_dir(a)
  for _ = 1, 2 do
    local sp = 14 + random() * 6
    Fx.spawn(nx, ny, nz, fx * sp + (random() - 0.5) * 1.5, fy * sp + (random() - 0.5) * 1.5, fz * sp + (random() - 0.5) * 1.5,
      0.5, 0.05 + random() * 0.04, random() < 0.5 and 0x9FE6FF or 0xEFFBFF, 2.5, 0)
  end
end

local function fire_icicle(a)
  local s = a.st
  s.ice_cd = ICICLE.rate
  s.ammo = s.ammo - ICICLE.cost
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local nx, ny, nz = nozzle(a)
  Proj.spawn({ x = ex + fx * 0.4, y = ey + fy * 0.4 - 0.05, z = ez + fz * 0.4, vx = fx * ICICLE.speed, vy = fy * ICICLE.speed,
               vz = fz * ICICLE.speed, owner = a, dmg = ICICLE.dmg, headshot = true, life = 1.2, size = 0.06, kind = "icicle",
               draw = function(p)
                 local l = len3(p.vx, p.vy, p.vz)
                 line3d(p.x, p.y, p.z, p.x - p.vx / l * 0.5, p.y - p.vy / l * 0.5, p.z - p.vz / l * 0.5, 0xDDF6FF, 2)
                 point3d(p.x, p.y, p.z, 0.05, 0xFFFFFF)
               end,
               on_hit = function(p, x, y, z) Fx.burst(x, y, z, 6, 0, 2, 0.3, 0.06, 0xCFF2FF, 1) end })
  Fx.burst(nx, ny, nz, 3, 2, 0.8, 0.12, 0.08, 0xDDF6FF, 0, fx, fy, fz)
  Actors.layer(a, "icicle", true)
  s.icicle_t = 0.5
  if a == G.local_actor then Snd.play("shatter", 0.5) end
end

-- ---------------------------------------------------------------- Ice Wall

local function pillar_draw(p, y)
  local k = (p.flash and p.flash > 0) and 1.03 or 1
  draw3d(Props.ice_mesh(), p.x, y, p.z, 0, p.spin, 0, k * p.r / 0.62, 0)
end

local function pillar_break(p)
  Fx.burst(p.x, p.y + 1.2, p.z, 14, 0, 3, 0.6, 0.12, 0xCFF2FF, 1.5)
  if p.hp <= 0 then Snd.play("shatter", 0.6) end
end

local function ice_wall(a)
  local s = a.st
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local t, nx, ny, nz = World.ray(ex, ey, ez, fx, fy, fz, WALL.range)
  local px, pz
  if t then
    px, pz = ex + fx * t, ez + fz * t
    if ny < 0.6 then px, pz = px + nx * 0.7, pz + nz * 0.7 end   -- against a wall: just in front of it
  else
    px, pz = ex + fx * WALL.range, ez + fz * WALL.range
  end
  s.wall_cd = WALL.cd
  -- across the aim: along the right of the aim
  local rx, rz = cos(a.yaw), -sin(a.yaw)
  for i = 1, WALL.n do
    local o = (i - (WALL.n + 1) / 2) * WALL.gap
    local x, z = px + rx * o, pz + rz * o
    local y = World.floor_at(x, z, ey + 1, 0.2) or a.y
    Props.add({ x = x, y = y, z = z, r = 0.62, h = 3.2, hp = WALL.hp, life = WALL.life, owner = a, rise = 0.25 + i * 0.03,
                spin = random() * pi, draw = pillar_draw, on_break = pillar_break })
  end
  Actors.layer(a, "wall", true)
  s.wall_anim = 0.6
  log("overbit ice wall " .. a.name)
  if a == G.local_actor then Snd.play("barrier", 0.7) end
end

-- ---------------------------------------------------------------- Blizzard

local function storm_tick(a)
  local s = a.st
  local w = s.storm
  if not w then return end
  w.t = w.t - DT
  if w.t <= 0 then s.storm = nil return end
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local d = len3(o.x - w.x, (o.y - w.y) * 0.5, o.z - w.z)
      if d < STORM.radius and World.clear(w.x, w.y + 2, w.z, o.x, o.y + o.height * 0.5, o.z) then
        o.fx.slow_t, o.fx.slow = 0.25, max(o.fx.slow or 0, STORM.slow)
        Actors.damage(o, STORM.dps * DT, a, false, "ult")
        o.fx.chill = (o.fx.chill or 0) + DT
        if o.fx.chill >= STORM.freeze_after and not Actors.frozen(o) then
          o.fx.frozen_t, o.fx.chill = STORM.freeze, -STORM.freeze
          log("overbit frozen " .. o.name)
        end
      elseif o.fx.chill then
        o.fx.chill = max(0, o.fx.chill - DT)
      end
    end
  end
  -- snow
  local n = floor(6 * Fx.density + random())
  for _ = 1, n do
    local an, r = random() * 2 * pi, sqrt(random()) * STORM.radius
    Fx.spawn(w.x + cos(an) * r, w.y + 2.5 + random() * 2, w.z + sin(an) * r, sin(G.t * 2 + r) * 3, -4, cos(G.t * 2 + r) * 3,
      0.9, 0.06 + random() * 0.05, 0xF4FBFF, 0.5, 0)
  end
end

local function deploy(a, x, y, z)
  local fy = World.floor_at(x, z, y + 0.5, 0.2) or y
  a.st.storm = { x = x, y = fy, z = z, t = STORM.time }
  Fx.light(x, fy + 2, z, 12, 1.2, 0x9FE6FF, 0.5)
  log("overbit blizzard " .. a.name)
  Snd.play("limit", 0.7)
end

local function blizzard(a)
  a.ult = 0
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  Proj.spawn({ x = ex + fx * 0.5, y = ey + 0.3, z = ez + fz * 0.5, vx = fx * STORM.speed, vy = fy * STORM.speed,
               vz = fz * STORM.speed, owner = a, life = STORM.range / STORM.speed, size = 0.15, kind = "drone", fuse = true,
               draw = function(p)
                 point3d(p.x, p.y, p.z, 0.16, 0xF7F9FB)
                 point3d(p.x, p.y + 0.01, p.z, 0.05, 0x9FE6FF)
               end,
               on_hit = function(p, x, y, z) deploy(a, x, y, z) end })
  Actors.layer(a, "blizzard", true)
  a.st.blizzard_anim = 0.8
  if a == G.local_actor then Snd.play("visor") end
end

-- ---------------------------------------------------------------- the update

local TIMERS = { "fire_cd", "ice_cd", "cryo_cd", "wall_cd", "icicle_t", "wall_anim", "blizzard_anim" }  -- once, not every frame
local LAYERS = { { "reload", "reload_t" }, { "icicle", "icicle_t" }, { "wall", "wall_anim" }, { "blizzard", "blizzard_anim" } }  -- once, not every frame
function F.update(a, c)
  local s = a.st
  storm_tick(a)
  if not a.alive then return end
  for _, k in ipairs(TIMERS) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = STREAM.ammo end
  end
  -- Cryo-Freeze: in the ice, healing; pressed again (after a moment) it ends
  if s.cryo_t > 0 then
    s.cryo_t = s.cryo_t - DT
    Actors.heal(a, CRYO.heal * DT)
    a.fx.invuln_t = 0.05
    if (c.ab1_p or c.fire_p) and s.cryo_t < CRYO.time - 0.5 then s.cryo_t = 0 end
    if s.cryo_t <= 0 then
      s.cryo_t, s.cryo_cd = 0, CRYO.cd
      a.override = nil
      a.fx.invuln_t = 0
      Fx.burst(a.x, a.y + 1, a.z, 12, 0, 3, 0.5, 0.1, 0xCFF2FF, 1.5)
      Snd.play("shatter", 0.6)
    end
    a.vx, a.vz = 0, 0
    Actors.physics(a)
    return
  end
  if c.ab1_p and s.cryo_cd <= 0 then
    s.cryo_t = CRYO.time
    a.override = "freeze"
    a.anim.base, a.anim.bt = "freeze", 0
    s.streaming = false
    Actors.layer_off(a)
    log("overbit cryo-freeze " .. a.name)
    if a == G.local_actor then Snd.play("barrier", 0.6) end
    return
  end
  if c.ult_p and a.ult >= 100 then blizzard(a) end
  if c.ab2_p and s.wall_cd <= 0 then ice_wall(a) end
  -- the blaster: the stream while the primary is held, an icicle on the secondary
  local can = s.reload_t <= 0
  if can and c.fire2_p and s.ice_cd <= 0 and s.ammo >= ICICLE.cost then fire_icicle(a) end
  local streaming = can and c.fire and s.ammo >= STREAM.cost and (s.icicle_t or 0) <= 0
  if streaming then
    if s.fire_cd <= 0 then stream_tick(a) end
    stream_fx(a)
    Actors.layer(a, "fire")
    if s.ammo < STREAM.cost then s.reload_t = STREAM.reload Actors.layer(a, "reload", true) end
  elseif a.anim.layer == "fire" then
    Actors.layer(a, "aim")
  end
  if s.streaming ~= streaming and a == G.local_actor and streaming then Snd.play("field", 0.7) end
  s.streaming = streaming
  if (c.reload_p and s.ammo < STREAM.ammo and can) or (can and s.ammo < STREAM.cost and (c.fire or c.fire2_p)) then
    s.reload_t = STREAM.reload
    Actors.layer(a, "reload", true)
  end
  for _, lay in ipairs(LAYERS) do
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

local function draw_storm(a)
  local w = a.st.storm
  if not w then return end
  local bob = sin(G.t * 3) * 0.15
  point3d(w.x, w.y + 3.2 + bob, w.z, 0.2, 0xF7F9FB)
  point3d(w.x, w.y + 3.2 + bob, w.z, 0.07, 0x9FE6FF)
  local n, r = 24, STORM.radius
  for i = 0, n - 1 do
    local a0, a1 = 2 * pi * i / n + G.t * 0.4, 2 * pi * (i + 1) / n + G.t * 0.4
    line3d(w.x + cos(a0) * r, w.y + 0.06, w.z + sin(a0) * r, w.x + cos(a1) * r, w.y + 0.06, w.z + sin(a1) * r, 0xCFF2FF, 1)
  end
end

function F.draw_extra(a)
  draw_storm(a)
  if a.alive and a.st.cryo_t > 0 then
    draw3d(Props.ice_mesh(true), a.x, a.y - 0.05, a.z, 0, a.yaw, 0, 0.6, 0)
  end
end

function F.draw_fp_extra(a)
  draw_storm(a)
end

local fp_mesh

function F.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("frost_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.cryo_t > 0 then layer, lt = "freeze", CRYO.time - s.cryo_t
  elseif s.reload_t > 0 then layer, lt = "reload", STREAM.reload - s.reload_t
  elseif (s.icicle_t or 0) > 0 then layer, lt = "icicle", 0.5 - s.icicle_t
  elseif (s.wall_anim or 0) > 0 then layer, lt = "wall", 0.6 - s.wall_anim
  elseif (s.blizzard_anim or 0) > 0 then layer, lt = "blizzard", 0.8 - s.blizzard_anim
  elseif s.streaming then layer, lt = "fire", G.t end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
end

-- in the ice: a frosted frame on the screen
function F.draw_hud(a)
  local s = a.st
  if s.cryo_t <= 0 then return end
  local c1, c2 = 0xCFF2FF, 0x9FE6FF
  for i = 0, 7 do
    rect(i, i, SW - 2 * i, SH - 2 * i, i % 2 == 0 and c1 or c2)
  end
  for i = 0, 11 do
    local x = (i * 47) % (SW - 20) + 10
    tri(x, 0, x + 14, 0, x + 7, 18 + (i * 5) % 14, c1)
    tri(x, SH, x + 14, SH, x + 7, SH - 18 - (i * 7) % 14, c1)
  end
  local str = string.format("CRYO-FREEZE %.1f", s.cryo_t)
  print(str, SW // 2 - #str * 3, 46, 0xFFFFFF)
end
