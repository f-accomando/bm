-- Sarge, damage (the kit of Soldier: 76). Heavy Pulse Rifle (primary:
-- automatic, a magazine of 30, the spread grows while firing), Helix
-- Rockets (secondary: three rockets twisted together, splash), Sprint
-- (ability 1, held), Biotic Field (ability 2: a field on the ground that
-- heals the team around it), Tactical Visor (ultimate: 6 seconds of shots
-- that find the enemy nearest to the crosshair).

local S = {
  id = "sarge", name = "Sarge", short = "SRG", role = "damage", rgb = 0x4FD8FF,
  ult_cost = 1650,
  desc = "A veteran with a pulse rifle",
}
H.sarge = S
HERO_ORDER[#HERO_ORDER + 1] = "sarge"

local FORM = {
  name = "soldier", tp = "sarge", fp = "sarge_fp",
  hp = 250, armor = 0, speed = 5.5, radius = 0.36, height = 1.86, eye = 1.68, crouch_eye = 1.2,
  jump = 6.4, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
S.forms = { soldier = FORM }

local RIFLE = { rate = 1 / 9, dmg = 19, ammo = 30, reload = 1.5, near = 30, far = 50, low = 0.5,
                spread0 = 0.0, spread1 = 0.042, bloom = 0.006, free = 2 }
local HELIX = { cd = 6.0, speed = 50, dmg = 40, splash = 80, radius = 2.5 }
local SPRINT = { k = 1.5 }
local FIELD = { cd = 15, time = 5, radius = 5, heal = 35 }
local VISOR = { time = 6.0, cone = 0.9, range = 60 }

S.numbers = { rifle = RIFLE, helix = HELIX, sprint = SPRINT, field = FIELD, visor = VISOR }

S.hud = {
  { key = "ab1", name = "Sprint", icon = 7, state = function(a)
      return 0, 1, a.st.sprint, true end },
  { key = "ab2", name = "Biotic Field", icon = 6, state = function(a)
      return a.st.field_cd, FIELD.cd, a.st.field ~= nil, true end },
  { key = "fire2", name = "Helix Rockets", icon = 2, state = function(a)
      return a.st.helix_cd, HELIX.cd, false, true end },
  { key = "fire", name = "Heavy Pulse Rifle", ammo = function(a)
      return a.st.ammo, RIFLE.ammo, a.st.reload_t > 0 end },
}

S.ult_name = function(a) return "TACTICAL VISOR" end

function S.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ammo, s.reload_t, s.helix_cd, s.field_cd, s.visor_t = 0, RIFLE.ammo, 0, 0, 0, 0
  s.burst, s.sprint, s.field = 0, false, nil
  a.override = nil
end

local function gun_tip(a)
  local m = a.mesh
  local _, _, _, tx, ty, tz = bone3d(m, "gun")
  if not tx then
    local ex, ey, ez = Actors.eye(a)
    return ex, ey - 0.2, ez
  end
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return a.x + tx * cy + tz * sy, a.y + ty, a.z - tx * sy + tz * cy
end

-- Tactical Visor: the enemy nearest to the crosshair, in sight
local function visor_target(a)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local best, who = VISOR.cone, nil
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local tx, ty, tz = o.x - ex, o.y + o.height * 0.6 - ey, o.z - ez
      local d = len3(tx, ty, tz)
      if d < VISOR.range and d > 0.1 then
        local off = 1 - (tx * fx + ty * fy + tz * fz) / d
        if off < best and World.clear(ex, ey, ez, o.x, o.y + o.height * 0.6, o.z) then best, who = off, o end
      end
    end
  end
  return who
end

local function fire_rifle(a)
  local s = a.st
  s.fire_cd = RIFLE.rate
  if s.visor_t <= 0 then s.ammo = s.ammo - 1 end
  s.burst = s.burst + 1
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local target = s.visor_t > 0 and visor_target(a)
  local dx, dy, dz
  if target then
    dx, dy, dz = target.x - ex, target.y + target.height * 0.6 - ey, target.z - ez
    local l = len3(dx, dy, dz)
    dx, dy, dz = dx / l, dy / l, dz / l
  else
    local sp = s.burst <= RIFLE.free and RIFLE.spread0 or min(RIFLE.spread1, (s.burst - RIFLE.free) * RIFLE.bloom)
    local ang, rad = random() * 2 * pi, sqrt(random()) * sp
    local rx, rz = cos(a.yaw), -sin(a.yaw)
    local ox, oy = cos(ang) * rad, sin(ang) * rad
    dx, dy, dz = fx + rx * ox, fy + oy, fz + rz * ox
    local l = len3(dx, dy, dz)
    dx, dy, dz = dx / l, dy / l, dz / l
  end
  local t, who, crit, nx, ny, nz = Actors.shoot(a, ex, ey, ez, dx, dy, dz, 80)
  local hx, hy, hz = ex + dx * (t or 80), ey + dy * (t or 80), ez + dz * (t or 80)
  if who then
    Actors.damage(who, Actors.falloff(RIFLE.dmg, t, RIFLE.near, RIFLE.far, RIFLE.low) * (crit and 2 or 1), a, crit,
      "hitscan")
    Fx.burst(hx, hy, hz, 2, 0, 1.5, 0.15, 0.05, 0x9FE8FF)
  elseif t then
    Fx.burst(hx, hy, hz, 2, 1, 1.4, 0.2, 0.04, 0xD8F4FF, 2, nx, ny, nz)
  end
  local mx, my, mz = gun_tip(a)
  if a == G.local_actor then
    local sy, cy = sin(a.yaw), cos(a.yaw)
    mx, my, mz = ex + cy * 0.16 + dx * 0.7, ey - 0.18, ez - sy * 0.16 + dz * 0.7
  end
  Fx.tracer(mx, my, mz, hx, hy, hz, 0x7FE4FF, 1, 0.05)
  a.muzzle_t = 0.05
  if a == G.local_actor then Snd.play("rifle", 0.8) end
end

local function fire_helix(a)
  local s = a.st
  s.helix_cd = HELIX.cd
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  local px, py, pz = ex + cy * 0.15 + fx * 0.6, ey - 0.2 + fy * 0.6, ez - sy * 0.15 + fz * 0.6
  Proj.spawn({ x = px, y = py, z = pz, vx = fx * HELIX.speed, vy = fy * HELIX.speed, vz = fz * HELIX.speed, owner = a,
               dmg = HELIX.dmg, splash = HELIX.splash, radius = HELIX.radius, life = 2, size = 0.12, kind = "rocket",
               self_k = 0, boom = 0.5, boom_rgb = 0x60D8FF, trail = 1, trail_rgb = 0xB0C8D0,
               draw = function(p)
                 -- three rockets turning around the path
                 local l = len3(p.vx, p.vy, p.vz)
                 local ux, uy, uz = p.vx / l, p.vy / l, p.vz / l
                 local rx, rz = uz, -ux
                 local rl = sqrt(rx * rx + rz * rz) + 1e-6
                 rx, rz = rx / rl, rz / rl
                 local vx, vy, vz = uy * rz, uz * rx - ux * rz, -uy * rx
                 for i = 0, 2 do
                   local an = p.age * 30 + i * 2 * pi / 3
                   local c, s2 = cos(an) * 0.18, sin(an) * 0.18
                   point3d(p.x + rx * c + vx * s2, p.y + vy * s2, p.z + rz * c + vz * s2, 0.07, 0xFFFFFF)
                 end
                 point3d(p.x, p.y, p.z, 0.2, 0x60D8FF, 1)
               end })
  Actors.layer(a, "rockets", true)
  s.rockets_t = 0.36
  if a == G.local_actor then Snd.play("helix") end
end

-- Biotic Field: a little device on the ground that heals the team around it
local field_mesh
local function make_field()
  local v, f = {}, {}
  local N = 8
  for i = 0, N - 1 do
    local an = 2 * pi * i / N
    v[#v + 1] = cos(an) * 0.18 v[#v + 1] = 0 v[#v + 1] = sin(an) * 0.18
    v[#v + 1] = cos(an) * 0.14 v[#v + 1] = 0.12 v[#v + 1] = sin(an) * 0.14
  end
  v[#v + 1] = 0 v[#v + 1] = 0.16 v[#v + 1] = 0
  local top = #v // 3
  local body, glow = 0xDDE2E6 | 0x20000000, 0xFFD24A | 0x40000000
  for i = 0, N - 1 do
    local a0, a1 = 2 * i + 1, 2 * ((i + 1) % N) + 1
    f[#f + 1] = a0 f[#f + 1] = a1 + 1 f[#f + 1] = a1 f[#f + 1] = body
    f[#f + 1] = a0 f[#f + 1] = a0 + 1 f[#f + 1] = a1 + 1 f[#f + 1] = body
    f[#f + 1] = a0 + 1 f[#f + 1] = top f[#f + 1] = a1 + 1 f[#f + 1] = glow
  end
  field_mesh = mesh(v, f)
end

local function drop_field(a)
  local s = a.st
  s.field_cd = FIELD.cd
  local fx, fz = sin(a.yaw), cos(a.yaw)
  local x, z = a.x + fx * 0.8, a.z + fz * 0.8
  local y = World.floor_at(x, z, a.y + 1, 0.1) or a.y
  s.field = { x = x, y = y, z = z, t = FIELD.time }
  Actors.layer(a, "field", true)
  s.field_anim = 0.6
  log("overbit biotic field " .. a.name)
  if a == G.local_actor then Snd.play("heal") end
end

local function tick_field(a)
  local s = a.st
  local f = s.field
  if not f then return end
  f.t = f.t - DT
  if f.t <= 0 then s.field = nil return end
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team == a.team and len3(o.x - f.x, (o.y - f.y) * 0.5, o.z - f.z) < FIELD.radius then
      Actors.heal(o, FIELD.heal * DT, a)
    end
  end
  if random() < 0.5 * Fx.density then
    local an, r = random() * 2 * pi, sqrt(random()) * FIELD.radius
    Fx.spawn(f.x + cos(an) * r, f.y + 0.1, f.z + sin(an) * r, 0, 1.2, 0, 0.8, 0.08, 0xFFE070, 0.5, 0)
  end
end

-- ---------------------------------------------------------------- the update

function S.update(a, c)
  local s = a.st
  tick_field(a)
  if not a.alive then return end
  for _, k in ipairs({ "fire_cd", "helix_cd", "field_cd", "visor_t", "rockets_t", "field_anim" }) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = RIFLE.ammo end
  end
  -- Tactical Visor
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    s.visor_t = VISOR.time
    s.ammo, s.reload_t = RIFLE.ammo, 0
    log("overbit tactical visor " .. a.name)
    if a == G.local_actor then Snd.play("visor") end
  end
  -- Sprint: held, forward; shooting stops it
  local moving_fwd = c.mz > 0.3
  local want_sprint = c.ab1 and moving_fwd and a.crouch < 0.5
  if (c.fire or c.fire2_p or s.reload_t > 0) then want_sprint = false end
  if want_sprint and not s.sprint and a == G.local_actor then Snd.play("sprint") end
  s.sprint = want_sprint
  a.override = (s.sprint and a.on_ground) and "sprint" or nil
  -- Biotic Field
  if c.ab2_p and s.field_cd <= 0 then drop_field(a) end
  -- Helix Rockets
  if c.fire2_p and s.helix_cd <= 0 then fire_helix(a) end
  -- the rifle
  local can = not s.sprint and s.reload_t <= 0
  if c.fire and can then
    if s.ammo <= 0 then
      s.reload_t = RIFLE.reload
      Actors.layer(a, "reload", true)
    elseif s.fire_cd <= 0 then
      fire_rifle(a)
      if s.ammo <= 0 then
        s.reload_t = RIFLE.reload
        Actors.layer(a, "reload", true)
      end
    end
    if s.reload_t <= 0 and (s.rockets_t or 0) <= 0 then Actors.layer(a, "fire") end
  else
    if s.fire_cd <= 0 then s.burst = 0 end
    if a.anim.layer == "fire" then Actors.layer(a, "aim") end
  end
  if c.reload_p and s.ammo < RIFLE.ammo and s.reload_t <= 0 then
    s.reload_t = RIFLE.reload
    Actors.layer(a, "reload", true)
  end
  if a.anim.layer == "reload" and s.reload_t <= 0 then Actors.layer(a, "aim") end
  if a.anim.layer == "field" and (s.field_anim or 0) <= 0 then Actors.layer(a, "aim") end
  if a.anim.layer == "rockets" and (s.rockets_t or 0) <= 0 then Actors.layer(a, "aim") end
  Actors.move(a, c, s.sprint and SPRINT.k or 1)
  if c.jump_p and a.on_ground then
    a.vy = a.form.jump
    a.on_ground = false
    a.anim.base, a.anim.bt = "jump", 0
  end
  a.crouch = approach(a.crouch, c.crouch and 1 or 0, DT * 8)
  Actors.physics(a)
end

-- ---------------------------------------------------------------- drawing

function S.draw_extra(a)
  local f = a.st.field
  if f then
    if not field_mesh then make_field() end
    draw3d(field_mesh, f.x, f.y, f.z, 0, G.t, 0, 1, 0)
    -- the ring of the field on the ground, pulsing
    local n = 16
    local r = FIELD.radius * (0.97 + 0.03 * sin(G.t * 6))
    for i = 0, n - 1 do
      local a0, a1 = 2 * pi * i / n, 2 * pi * (i + 1) / n
      line3d(f.x + cos(a0) * r, f.y + 0.05, f.z + sin(a0) * r, f.x + cos(a1) * r, f.y + 0.05, f.z + sin(a1) * r,
        0xFFD24A, 1)
    end
  end
end

function S.draw_fp_extra(a)
  S.draw_extra(a)
end

local fp_mesh

function S.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("sarge_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if s.sprint and speed > 1 then base, t = "sprint", G.t
  elseif speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.reload_t > 0 then layer, lt = "reload", RIFLE.reload - s.reload_t
  elseif (s.rockets_t or 0) > 0 then layer, lt = "rockets", 0.36 - s.rockets_t
  elseif s.fire_cd > 0 then layer, lt = "fire", RIFLE.rate - s.fire_cd end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
  if a.muzzle_t and a.muzzle_t > 0 then
    a.muzzle_t = a.muzzle_t - DT
    local _, _, _, tx, ty, tz = bone3d(m, "gun")
    if tx then
      local cp, sp, cyw, syw = cos(pitch), sin(pitch), cos(yaw), sin(yaw)
      local y2, z2 = ty * cp + tz * sp, -ty * sp + tz * cp
      local px, py, pz = cx + tx * cyw + z2 * syw, cy + y2, cz - tx * syw + z2 * cyw
      local sx, sy = project3d(px, py, pz)
      if sx then
        circfill(floor(sx), floor(sy), 4, 0x9FE8FF)
        circfill(floor(sx), floor(sy), 2, 0xFFFFFF)
      end
    end
  end
end

-- the visor's overlay: brackets on the enemies it sees, an amber frame
function S.draw_hud(a)
  local s = a.st
  if s.visor_t <= 0 then return end
  local c = 0xFFA22E
  rect(2, 2, 316, 176, c)
  local target = visor_target(a)
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local sx, sy = project3d(o.x, o.y + o.height * 0.6, o.z)
      if sx then
        local x, y = floor(sx), floor(sy)
        local r = o == target and 9 or 6
        local col = o == target and 0xFFFFFF or c
        line(x - r, y - r, x - r + 3, y - r, col) line(x - r, y - r, x - r, y - r + 3, col)
        line(x + r, y - r, x + r - 3, y - r, col) line(x + r, y - r, x + r, y - r + 3, col)
        line(x - r, y + r, x - r + 3, y + r, col) line(x - r, y + r, x - r, y + r - 3, col)
        line(x + r, y + r, x + r - 3, y + r, col) line(x + r, y + r, x + r, y + r - 3, col)
      end
    end
  end
  local str = string.format("VISOR %.1f", s.visor_t)
  print(str, 160 - #str * 3, 30, c)
end
