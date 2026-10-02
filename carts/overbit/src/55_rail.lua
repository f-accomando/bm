-- Rail, damage (the kit of Sojourn). Railgun: fast bolts that charge energy
-- on every hit (primary), the charged rail shot, hitscan, stronger with the
-- energy, which it spends (secondary); Power Slide (ability 1: a slide on
-- the ground; a jump from it goes high), Disruptor Shot (ability 2: an orb
-- that opens a field which hurts and slows), Overclock (ultimate: the
-- energy charges by itself and the rail shots go through everyone).

local R = {
  id = "rail", name = "Rail", short = "RAL", role = "damage", rgb = 0x4FE8FF,
  ult_cost = 1800,
  desc = "A commander with a rail rifle",
}
H.rail = R
HERO_ORDER[#HERO_ORDER + 1] = "rail"

local FORM = {
  name = "rail", tp = "rail", fp = "rail_fp",
  hp = 250, armor = 0, speed = 5.5, radius = 0.36, height = 1.82, eye = 1.66, crouch_eye = 1.2,
  jump = 6.4, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
R.forms = { rail = FORM }

local BOLT = { rate = 1 / 14, dmg = 9, ammo = 45, reload = 1.4, speed = 140, spread = 0.012, energy = 5 }
local SHOT = { base = 30, per = 1.0, need = 10, crit = 1.5, range = 120 }
local SLIDE = { time = 0.8, speed = 11, cd = 7, jump = 11 }
local DISRUPT = { cd = 12, speed = 22, radius = 4, time = 4, dps = 52, slow = 0.15 }
local OVER = { time = 9, charge = 40 }

R.numbers = { bolt = BOLT, shot = SHOT, slide = SLIDE, disruptor = DISRUPT, overclock = OVER }

R.hud = {
  { key = "ab1", name = "Power Slide", icon = 7, state = function(a)
      return a.st.slide_cd, SLIDE.cd, a.st.slide_t > 0, true end },
  { key = "ab2", name = "Disruptor Shot", icon = 11, state = function(a)
      return a.st.dis_cd, DISRUPT.cd, a.st.field ~= nil, true end },
  { key = "fire", name = "Railgun", ammo = function(a)
      return a.st.ammo, BOLT.ammo, a.st.reload_t > 0 end },
}

R.ult_name = function(a) return "OVERCLOCK" end

function R.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ammo, s.reload_t, s.energy, s.slide_cd, s.slide_t, s.dis_cd, s.over_t = 0, BOLT.ammo, 0, 0, 0, 0, 0, 0
  s.field = nil
  a.override = nil
end

local function muzzle(a)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return ex + cy * 0.17 + fx * 0.9, ey - 0.16 + fy * 0.9, ez - sy * 0.17 + fz * 0.9
end

-- ---------------------------------------------------------------- the railgun

local function fire_bolt(a)
  local s = a.st
  s.fire_cd = BOLT.rate
  s.ammo = s.ammo - 1
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sp = BOLT.spread
  local dx, dy, dz = fx + (random() - 0.5) * sp, fy + (random() - 0.5) * sp, fz + (random() - 0.5) * sp
  local mx, my, mz = muzzle(a)
  Proj.spawn({ x = ex + dx * 0.5, y = ey + dy * 0.5 - 0.05, z = ez + dz * 0.5, vx = dx * BOLT.speed, vy = dy * BOLT.speed,
               vz = dz * BOLT.speed, owner = a, dmg = BOLT.dmg, headshot = true, life = 0.6, size = 0.04, kind = "bolt",
               on_direct = function(p, o) a.st.energy = min(100, a.st.energy + BOLT.energy) end,
               draw = function(p)
                 line3d(p.x, p.y, p.z, p.x - p.vx * 0.006, p.y - p.vy * 0.006, p.z - p.vz * 0.006, 0x8FF4FF, 1)
               end,
               on_hit = function(p, x, y, z) Fx.burst(x, y, z, 2, 0, 1.2, 0.15, 0.04, 0x8FF4FF) end })
  a.muzzle_t = 0.04
  Fx.burst(mx, my, mz, 1, 2, 0.5, 0.06, 0.06, 0xCFFAFF, 0, fx, fy, fz)
  if a == G.local_actor then Snd.play("repeater", 0.6) end
end

local function fire_rail(a)
  local s = a.st
  local e = s.energy
  s.energy = s.over_t > 0 and s.energy * 0.5 or 0
  local dmg = SHOT.base + e * SHOT.per
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local wt = World.ray(ex, ey, ez, fx, fy, fz, SHOT.range) or SHOT.range
  local hit_end = wt
  if s.over_t > 0 then
    -- Overclock: through every enemy on the line, up to the wall
    for _, o in ipairs(Actors.list) do
      if o.alive and o.team ~= a.team then
        local t, bone = hit3d(o.mesh, o.x, o.y, o.z, o.yaw, o.form.scale or 1, ex, ey, ez, fx, fy, fz, wt)
        if t then
          local crit = bone == o.form.head
          Actors.damage(o, dmg * (crit and SHOT.crit or 1), a, crit, "hitscan")
        end
      end
    end
  else
    local t, who, crit = Actors.shoot(a, ex, ey, ez, fx, fy, fz, SHOT.range)
    if who then
      Actors.damage(who, dmg * (crit and SHOT.crit or 1), a, crit, "hitscan")
      hit_end = t
    end
  end
  local mx, my, mz = muzzle(a)
  local hx, hy, hz = ex + fx * hit_end, ey + fy * hit_end, ez + fz * hit_end
  Fx.tracer(mx, my, mz, hx, hy, hz, 0x4FE8FF, 3, 0.25)
  Fx.tracer(mx, my, mz, hx, hy, hz, 0xFFFFFF, 1, 0.12)
  Fx.burst(hx, hy, hz, 8, 0, 3, 0.3, 0.08, 0x8FF4FF, 0.5)
  Fx.light(mx, my, mz, 6, 1.2, 0x4FE8FF, 0.2)
  Actors.layer(a, "rail", true)
  s.rail_anim = 0.5
  s.fire_cd = 0.4
  log(string.format("overbit rail %s %d", a.name, floor(dmg)))
  if a == G.local_actor then Snd.play("limit", 0.6) end
end

-- ---------------------------------------------------------------- Disruptor Shot

local function tick_field(a)
  local s = a.st
  local f = s.field
  if not f then return end
  f.t = f.t - DT
  if f.t <= 0 then s.field = nil return end
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team and len3(o.x - f.x, (o.y + o.height * 0.5 - f.y) * 0.8, o.z - f.z) < DISRUPT.radius then
      Actors.damage(o, DISRUPT.dps * DT, a, false, "field")
      o.fx.slow_t, o.fx.slow = 0.2, max(o.fx.slow or 0, DISRUPT.slow)
    end
  end
  if random() < 0.7 * Fx.density then
    local an, r = random() * 2 * pi, DISRUPT.radius * (0.3 + 0.7 * random())
    Fx.spawn(f.x + cos(an) * r, f.y + (random() - 0.5) * 2, f.z + sin(an) * r, -cos(an) * 2, 0, -sin(an) * 2, 0.4, 0.08,
      0x8FF4FF, 0, 0)
  end
end

local function fire_disruptor(a)
  local s = a.st
  s.dis_cd = DISRUPT.cd
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  Proj.spawn({ x = ex + fx * 0.6, y = ey - 0.1 + fy * 0.6, z = ez + fz * 0.6, vx = fx * DISRUPT.speed, vy = fy * DISRUPT.speed,
               vz = fz * DISRUPT.speed, owner = a, life = 2.5, fuse = true, size = 0.15, kind = "disruptor", grav = 2,
               draw = function(p)
                 point3d(p.x, p.y, p.z, 0.16, 0x4FE8FF)
                 point3d(p.x, p.y, p.z, 0.08, 0xFFFFFF)
               end,
               on_hit = function(p, x, y, z)
                 a.st.field = { x = x, y = y, z = z, t = DISRUPT.time }
                 Fx.light(x, y, z, 6, 1.0, 0x4FE8FF, 0.4)
                 log("overbit disruptor " .. a.name)
               end })
  Actors.layer(a, "disruptor", true)
  s.dis_anim = 0.5
  if a == G.local_actor then Snd.play("helix") end
end

-- ---------------------------------------------------------------- the update

function R.update(a, c)
  local s = a.st
  tick_field(a)
  if not a.alive then return end
  for _, k in ipairs({ "fire_cd", "slide_cd", "dis_cd", "rail_anim", "dis_anim", "over_anim" }) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = BOLT.ammo end
  end
  if s.over_t > 0 then
    s.over_t = s.over_t - DT
    s.energy = min(100, s.energy + OVER.charge * DT)
  end
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    s.over_t, s.energy = OVER.time, 100
    Actors.layer(a, "overclock", true)
    s.over_anim = 0.8
    log("overbit overclock " .. a.name)
    if a == G.local_actor then Snd.play("visor") end
  end
  -- Power Slide
  if c.ab1_p and s.slide_cd <= 0 and a.on_ground and s.slide_t <= 0 then
    s.slide_t, s.slide_cd = SLIDE.time, SLIDE.cd
    local mx, mz = c.mx, c.mz
    if mx * mx + mz * mz < 0.1 then mx, mz = 0, 1 end
    local l = sqrt(mx * mx + mz * mz)
    local sy, cy = sin(a.yaw), cos(a.yaw)
    s.slide_x, s.slide_z = (mz * sy + mx * cy) / l, (mz * cy - mx * sy) / l
    a.override = "slide"
    if a == G.local_actor then Snd.play("sprint") end
  end
  local sliding = s.slide_t > 0
  if sliding then
    s.slide_t = s.slide_t - DT
    a.vx, a.vz = s.slide_x * SLIDE.speed, s.slide_z * SLIDE.speed
    if random() < 0.6 * Fx.density then
      Fx.spawn(a.x, a.y + 0.05, a.z, -a.vx * 0.1, 0.8, -a.vz * 0.1, 0.4, 0.12, 0x9A9488, 1, 0)
    end
    if c.jump_p then
      -- the jump out of the slide: high
      s.slide_t = 0
      a.vy, a.on_ground = SLIDE.jump, false
      a.anim.base, a.anim.bt = "jump", 0
    end
    if s.slide_t <= 0 then a.override = nil end
  end
  if c.ab2_p and s.dis_cd <= 0 then fire_disruptor(a) end
  -- the railgun
  local can = s.reload_t <= 0
  if can and c.fire2_p and s.energy >= SHOT.need and s.fire_cd <= 0 then fire_rail(a)
  elseif can and c.fire and s.fire_cd <= 0 then
    if s.ammo > 0 then
      fire_bolt(a)
      Actors.layer(a, "fire")
    end
    if s.ammo <= 0 then s.reload_t = BOLT.reload Actors.layer(a, "reload", true) end
  elseif not c.fire and a.anim.layer == "fire" then
    Actors.layer(a, "aim")
  end
  if c.reload_p and s.ammo < BOLT.ammo and s.reload_t <= 0 then
    s.reload_t = BOLT.reload
    Actors.layer(a, "reload", true)
  end
  for _, lay in ipairs({ { "reload", "reload_t" }, { "rail", "rail_anim" }, { "disruptor", "dis_anim" }, { "overclock", "over_anim" } }) do
    if a.anim.layer == lay[1] and (s[lay[2]] or 0) <= 0 then Actors.layer(a, "aim") end
  end
  if not sliding then
    Actors.move(a, c)
    if c.jump_p and a.on_ground then
      a.vy = a.form.jump
      a.on_ground = false
      a.anim.base, a.anim.bt = "jump", 0
    end
  end
  a.crouch = approach(a.crouch, (c.crouch or sliding) and 1 or 0, DT * 10)
  Actors.physics(a)
end

-- ---------------------------------------------------------------- drawing

local function draw_field(a)
  local f = a.st.field
  if not f then return end
  local r = DISRUPT.radius * (0.92 + 0.08 * sin(G.t * 8))
  point3d(f.x, f.y, f.z, 0.3, 0x4FE8FF)
  local n = 20
  for i = 0, n - 1 do
    local a0, a1 = 2 * pi * i / n + G.t, 2 * pi * (i + 1) / n + G.t
    line3d(f.x + cos(a0) * r, f.y, f.z + sin(a0) * r, f.x + cos(a1) * r, f.y, f.z + sin(a1) * r, 0x4FE8FF, 1)
    local c0, c1 = cos(G.t * 1.3), sin(G.t * 1.3)            -- a second ring, upright and turning
    line3d(f.x + cos(a0) * r * c0, f.y + sin(a0) * r, f.z + cos(a0) * r * c1,
      f.x + cos(a1) * r * c0, f.y + sin(a1) * r, f.z + cos(a1) * r * c1, 0x8FF4FF, 1)
  end
end

function R.draw_extra(a)
  draw_field(a)
end

function R.draw_fp_extra(a)
  draw_field(a)
end

local fp_mesh

function R.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("rail_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if s.slide_t > 0 then base = "slide"
  elseif speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.reload_t > 0 then layer, lt = "reload", BOLT.reload - s.reload_t
  elseif (s.rail_anim or 0) > 0 then layer, lt = "rail", 0.5 - s.rail_anim
  elseif (s.dis_anim or 0) > 0 then layer, lt = "disruptor", 0.5 - s.dis_anim
  elseif s.fire_cd > 0 then layer, lt = "fire", BOLT.rate - s.fire_cd end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
end

-- the energy: a ring round the crosshair that fills up; full: it shines
function R.draw_hud(a)
  local s = a.st
  local e = s.energy
  local cx, cy = 160, 90
  local n = floor(e / 100 * 20)
  local col = e >= 100 and 0xFFFFFF or 0x4FE8FF
  for i = 0, n - 1 do
    local an = -pi / 2 + i * 2 * pi / 20
    pset(cx + floor(cos(an) * 16), cy + floor(sin(an) * 16), col)
    pset(cx + floor(cos(an) * 17), cy + floor(sin(an) * 17), col)
  end
  if s.over_t > 0 then
    local str = string.format("OVERCLOCK %.1f", s.over_t)
    print(str, 160 - #str * 3, 30, 0x4FE8FF)
  end
end
