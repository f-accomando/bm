-- Orbit, support (the kit of Juno). Mediblaster (primary: bursts of four
-- shots that heal the team and hurt the enemies), Pulsar Torpedoes
-- (secondary held: locks up to four heroes in front, released they fly to
-- them: healing or damage), Martian Mobility (passive: a second jump, and
-- hovering while the jump is held in the air), Glide Boost (ability 1:
-- faster, floating), Hyper Ring (ability 2: a ring that speeds up the team
-- going through it), Orbital Ray (ultimate: a beam from orbit that moves
-- forward, healing and strengthening the team inside).

local O = {
  id = "orbit", name = "Orbit", short = "ORB", role = "support", rgb = 0x3FF2D8,
  ult_cost = 1900,
  desc = "A young explorer from a space station",
}
H.orbit = O
HERO_ORDER[#HERO_ORDER + 1] = "orbit"

local FORM = {
  name = "orbit", tp = "orbit", fp = "orbit_fp",
  hp = 225, armor = 0, speed = 5.5, radius = 0.35, height = 1.66, eye = 1.52, crouch_eye = 1.1,
  jump = 6.2, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
O.forms = { orbit = FORM }

local BLAST = { burst = 4, gap = 0.05, rate = 0.45, dmg = 7.5, heal = 7.5, ammo = 120, reload = 1.5, range = 50 }
local TORP = { cd = 10, lock = 1.2, max = 4, cone = 0.35, range = 40, amount = 85, speed = 18 }
local HOVER = { fall = -1.5, time = 3.0, jump2 = 5.5 }
local GLIDE = { cd = 12, time = 4, k = 1.5 }
local RING = { cd = 14, time = 5, radius = 1.6, dist = 3, boost = 0.3, boost_t = 4 }
local RAY = { time = 4.5, radius = 4.5, speed = 1.6, heal = 85, amp = 0.35, dist = 6 }

O.numbers = { blaster = BLAST, torpedoes = TORP, hover = HOVER, glide = GLIDE, ring = RING, ray = RAY }

O.hud = {
  { key = "ab1", name = "Glide Boost", icon = 7, state = function(a)
      return a.st.glide_cd, GLIDE.cd, a.st.glide_t > 0, true end },
  { key = "ab2", name = "Hyper Ring", icon = 10, state = function(a)
      return a.st.ring_cd, RING.cd, a.st.ring ~= nil, true end },
  { key = "fire2", name = "Pulsar Torpedoes", icon = 2, state = function(a)
      return a.st.torp_cd, TORP.cd, a.st.locking ~= nil, true end },
  { key = "fire", name = "Mediblaster", ammo = function(a)
      return a.st.ammo, BLAST.ammo, a.st.reload_t > 0 end },
}

O.ult_name = function(a) return "ORBITAL RAY" end

function O.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ammo, s.reload_t, s.burst, s.burst_t = 0, BLAST.ammo, 0, 0, 0
  s.torp_cd, s.glide_cd, s.glide_t, s.ring_cd, s.hover_t, s.jumps = 0, 0, 0, 0, HOVER.time, 0
  s.locking, s.ring, s.ray = nil, nil, nil
  a.override = nil
end

local function muzzle(a)
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  return ex + cy * 0.18 + fx * 0.6, ey - 0.15 + fy * 0.6, ez - sy * 0.18 + fz * 0.6
end

-- ---------------------------------------------------------------- the Mediblaster

local function blast(a)
  local s = a.st
  s.ammo = s.ammo - 1
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sp = 0.01
  local dx, dy, dz = fx + (grandom() - 0.5) * sp, fy + (grandom() - 0.5) * sp, fz + (grandom() - 0.5) * sp
  local wt, nx, ny, nz, prop = World.ray(ex, ey, ez, dx, dy, dz, BLAST.range)
  local t, who, crit = Actors.ray(ex, ey, ez, dx, dy, dz, wt or BLAST.range, a, nil)   -- friends and foes
  local hx, hy, hz
  if who then
    hx, hy, hz = ex + dx * t, ey + dy * t, ez + dz * t
    if who.team == a.team then
      local d = Actors.heal(who, BLAST.heal, a)
      if d > 0 and G.dmg_numbers then Fx.number(hx, hy + 0.3, hz, floor(d + 0.5), false, true) end
    else
      Actors.damage(who, BLAST.dmg * (crit and 2 or 1), a, crit, "hitscan")
    end
    Fx.burst(hx, hy, hz, 2, 0, 1.2, 0.15, 0.05, 0x7FFFE8)
  else
    local d = wt or BLAST.range
    hx, hy, hz = ex + dx * d, ey + dy * d, ez + dz * d
    if prop then Actors.damage(prop, BLAST.dmg, a, false, "hitscan") end
  end
  local mx, my, mz = muzzle(a)
  Fx.tracer(mx, my, mz, hx, hy, hz, 0x7FFFE8, 1, 0.04)
  a.muzzle_t = 0.04
  if a == G.local_actor then Snd.play("pistol", 0.5) end
end

-- ---------------------------------------------------------------- Pulsar Torpedoes

local function lock_targets(a)
  local s = a.st
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local cs = cos(TORP.cone)
  for _, o in ipairs(Actors.list) do
    if o ~= a and o.alive and not s.locked[o] and #s.locks < TORP.max then
      local tx, ty, tz = o.x - ex, o.y + o.height * 0.6 - ey, o.z - ez
      local d = len3(tx, ty, tz)
      if d < TORP.range and (tx * fx + ty * fy + tz * fz) / d > cs and World.clear(ex, ey, ez, o.x, o.y + o.height * 0.6, o.z) then
        s.locked[o] = true
        s.locks[#s.locks + 1] = o
        if a == G.local_actor then Snd.play("ui") end
      end
    end
  end
end

local function launch_torpedoes(a)
  local s = a.st
  s.torp_cd = TORP.cd
  local mx, my, mz = muzzle(a)
  for i, o in ipairs(s.locks) do
    local an = i * 2 * pi / max(1, #s.locks)
    Proj.spawn({ x = mx, y = my + 0.3, z = mz, vx = cos(an) * 5, vy = 9 + sin(an) * 2, vz = sin(an) * 5, owner = a,
                 target = o, homing = 6, life = 3, size = 0.12, kind = "torpedo", any_team = true,
                 draw = function(p)
                   local c = p.target.team == a.team and 0x7FFFE8 or 0xFF7FD8
                   point3d(p.x, p.y, p.z, 0.12, c)
                   point3d(p.x, p.y, p.z, 0.05, 0xFFFFFF)
                 end,
                 on_hit = function(p, x, y, z, direct)
                   local o2 = p.target
                   if o2.alive and len3(o2.x - x, o2.y + o2.height * 0.5 - y, o2.z - z) < 1.5 then
                     if o2.team == a.team then
                       local d = Actors.heal(o2, TORP.amount, a)
                       if G.dmg_numbers and d > 0 then Fx.number(x, y + 0.3, z, floor(d + 0.5), false, true) end
                     else
                       Actors.damage(o2, TORP.amount, a, false, "torpedo")
                     end
                   end
                   Fx.burst(x, y, z, 8, 0, 2.5, 0.35, 0.08, 0x7FFFE8, 0.5)
                 end })
  end
  log(string.format("overbit torpedoes %s %d", a.name, #s.locks))
  s.locks, s.locked, s.locking = {}, {}, nil
  if a == G.local_actor then Snd.play("rocket") end
end

-- ---------------------------------------------------------------- Hyper Ring and Orbital Ray

local function tick_ring(a)
  local s = a.st
  local r = s.ring
  if not r then return end
  r.t = r.t - DT
  if r.t <= 0 then s.ring = nil return end
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team == a.team and not r.done[o] then
      -- through the ring: near its plane, inside its circle
      local dx, dy, dz = o.x - r.x, o.y + o.height * 0.5 - r.y, o.z - r.z
      local along = dx * r.nx + dz * r.nz
      if abs(along) < 0.6 and len3(dx - r.nx * along, dy, dz - r.nz * along) < RING.radius + 0.3 then
        r.done[o] = true
        o.fx.haste_t, o.fx.haste = RING.boost_t, RING.boost
        if o == G.local_actor then Snd.play("dash", 0.6) end
      end
    end
  end
end

local function tick_ray(a)
  local s = a.st
  local r = s.ray
  if not r then return end
  r.t = r.t - DT
  if r.t <= 0 then s.ray = nil return end
  r.x, r.z = r.x + r.dx * RAY.speed * DT, r.z + r.dz * RAY.speed * DT
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team == a.team then
      local dx, dz = o.x - r.x, o.z - r.z
      if dx * dx + dz * dz < RAY.radius * RAY.radius then
        Actors.heal(o, RAY.heal * DT, a)
        o.fx.dmg_boost_t, o.fx.dmg_boost = 0.2, RAY.amp
      end
    end
  end
  if random() < 0.8 * Fx.density then
    local an, rr = random() * 2 * pi, sqrt(random()) * RAY.radius
    Fx.spawn(r.x + cos(an) * rr, r.y + 0.1, r.z + sin(an) * rr, 0, 3, 0, 0.6, 0.1, 0xFFD0F0, 0.5, 0)
  end
end

-- ---------------------------------------------------------------- the update

local TIMERS = { "fire_cd", "torp_cd", "glide_cd", "ring_cd", "ring_anim", "ray_anim" }  -- once, not every frame
local LAYERS = { { "reload", "reload_t" }, { "ring", "ring_anim" }, { "orbital", "ray_anim" } }  -- once, not every frame
function O.update(a, c)
  local s = a.st
  tick_ring(a)
  tick_ray(a)
  if not a.alive then return end
  for _, k in ipairs(TIMERS) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = BLAST.ammo end
  end
  -- Orbital Ray
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    local fx, fz = sin(a.yaw), cos(a.yaw)
    local x, z = a.x + fx * RAY.dist, a.z + fz * RAY.dist
    s.ray = { x = x, y = World.floor_at(x, z, a.y + 2, 0.2) or a.y, z = z, dx = fx, dz = fz, t = RAY.time }
    Actors.layer(a, "orbital", true)
    s.ray_anim = 1.0
    log("overbit orbital ray " .. a.name)
    if a == G.local_actor then Snd.play("call") end
  end
  -- Glide Boost
  if c.ab1_p and s.glide_cd <= 0 then
    s.glide_t, s.glide_cd = GLIDE.time, GLIDE.cd
    if a == G.local_actor then Snd.play("boost", 0.6) end
  end
  if s.glide_t > 0 then s.glide_t = s.glide_t - DT end
  -- Hyper Ring
  if c.ab2_p and s.ring_cd <= 0 then
    s.ring_cd = RING.cd
    local fx, fz = sin(a.yaw), cos(a.yaw)
    s.ring = { x = a.x + fx * RING.dist, y = a.y + 1.2, z = a.z + fz * RING.dist, nx = fx, nz = fz, yaw = a.yaw,
               t = RING.time, done = {} }
    Actors.layer(a, "ring", true)
    s.ring_anim = 0.6
    log("overbit hyper ring " .. a.name)
    if a == G.local_actor then Snd.play("barrier", 0.6) end
  end
  -- Pulsar Torpedoes: held to lock, released to fire
  if c.fire2 and s.torp_cd <= 0 then
    if not s.locking then s.locking, s.locks, s.locked = 0, {}, {} end
    s.locking = s.locking + DT
    lock_targets(a)
    Actors.layer(a, "torpedo")
    if s.locking >= TORP.lock + 0.6 then launch_torpedoes(a) end
  elseif s.locking then
    if #s.locks > 0 then launch_torpedoes(a) else s.locking = nil end
    Actors.layer(a, "aim")
  end
  -- the Mediblaster: bursts of four
  if s.burst > 0 then
    s.burst_t = s.burst_t - DT
    if s.burst_t <= 0 then
      blast(a)
      s.burst, s.burst_t = s.burst - 1, BLAST.gap
    end
  elseif c.fire and s.fire_cd <= 0 and s.reload_t <= 0 and not s.locking then
    if s.ammo >= BLAST.burst then
      s.burst, s.burst_t, s.fire_cd = BLAST.burst, 0, BLAST.rate
      Actors.layer(a, "fire")
    else
      s.reload_t = BLAST.reload
      Actors.layer(a, "reload", true)
    end
  elseif not c.fire and a.anim.layer == "fire" then
    Actors.layer(a, "aim")
  end
  if c.reload_p and s.ammo < BLAST.ammo and s.reload_t <= 0 then
    s.reload_t = BLAST.reload
    Actors.layer(a, "reload", true)
  end
  for _, lay in ipairs(LAYERS) do
    if a.anim.layer == lay[1] and (s[lay[2]] or 0) <= 0 then Actors.layer(a, "aim") end
  end
  -- moving: Martian Mobility (double jump, hover), Glide Boost
  Actors.move(a, c, s.glide_t > 0 and GLIDE.k or 1)
  if a.on_ground then
    s.jumps, s.hover_t = 0, min(HOVER.time, s.hover_t + DT * 2)
  end
  if c.jump_p then
    if a.on_ground then
      a.vy, a.on_ground, s.jumps = a.form.jump, false, 1
      a.anim.base, a.anim.bt = "jump", 0
    elseif s.jumps < 2 then
      a.vy, s.jumps = HOVER.jump2, 2
      a.anim.base, a.anim.bt = "jump", 0
      Fx.burst(a.x, a.y, a.z, 6, 2, 1, 0.3, 0.08, 0x3FF2D8, 0, 0, -1, 0)
    end
  end
  local hovering = not a.on_ground and a.vy < 0 and ((c.jump and s.hover_t > 0) or s.glide_t > 0)
  if hovering then
    a.vy = max(a.vy, HOVER.fall)
    if s.glide_t <= 0 then s.hover_t = s.hover_t - DT end
    a.override = "hover"
    if random() < 0.5 * Fx.density then
      Fx.spawn(a.x + (random() - 0.5) * 0.2, a.y, a.z + (random() - 0.5) * 0.2, 0, -2, 0, 0.3, 0.07, 0x3FF2D8, 1, 0)
    end
  elseif a.override == "hover" then
    a.override = nil
  end
  a.crouch = approach(a.crouch, c.crouch and 1 or 0, DT * 8)
  Actors.physics(a)
end

-- ---------------------------------------------------------------- drawing

local function draw_ring(a)
  local r = a.st.ring
  if not r then return end
  local n = 18
  local rx, rz = r.nz, -r.nx                         -- the ring's own right
  local k = min(1, (RING.time - r.t) * 4)
  local rad = RING.radius * k
  -- each corner once (the end of a segment is the start of the next)
  local c0, s0 = cos(G.t), sin(G.t)
  local x0, y0, z0 = r.x + rx * c0 * rad, r.y + s0 * rad, r.z + rz * c0 * rad
  for i = 0, n - 1 do
    local a1 = 2 * pi * (i + 1) / n + G.t
    local c1, s1 = cos(a1), sin(a1)
    local x1, y1, z1 = r.x + rx * c1 * rad, r.y + s1 * rad, r.z + rz * c1 * rad
    line3d(x0, y0, z0, x1, y1, z1, i % 2 == 0 and 0x3FF2D8 or 0xF28A2E, 2)
    x0, y0, z0 = x1, y1, z1
  end
end

local function draw_ray(a)
  local r = a.st.ray
  if not r then return end
  -- the beam from the sky and its circle on the ground
  for i = -1, 1 do
    line3d(r.x + i * 0.4, r.y, r.z, r.x + i * 0.4, r.y + 40, r.z, i == 0 and 0xFFFFFF or 0xFFA0E0, i == 0 and 3 or 2)
  end
  local n = 20
  for i = 0, n - 1 do
    local a0, a1 = 2 * pi * i / n, 2 * pi * (i + 1) / n
    line3d(r.x + cos(a0) * RAY.radius, r.y + 0.06, r.z + sin(a0) * RAY.radius, r.x + cos(a1) * RAY.radius, r.y + 0.06,
      r.z + sin(a1) * RAY.radius, 0xFFA0E0, 1)
  end
end

function O.draw_extra(a)
  draw_ring(a)
  draw_ray(a)
end

function O.draw_fp_extra(a)
  draw_ring(a)
  draw_ray(a)
end

local fp_mesh

function O.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("orbit_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.reload_t > 0 then layer, lt = "reload", BLAST.reload - s.reload_t
  elseif s.locking then layer, lt = "torpedo", s.locking
  elseif (s.ring_anim or 0) > 0 then layer, lt = "ring", 0.6 - s.ring_anim
  elseif s.burst > 0 or s.fire_cd > BLAST.rate - 0.2 then layer, lt = "fire", G.t end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
end

-- the locks: a ring on each hero locked; the hover fuel
function O.draw_hud(a)
  local s = a.st
  if s.locking then
    for _, o in ipairs(s.locks) do
      local sx, sy = project3d(o.x, o.y + o.height * 0.6, o.z)
      if sx then
        local c = o.team == a.team and 0x3FF2D8 or 0xFF7FD8
        local r = floor(7 * ZOOM / UI)
        ucirc(floor(sx / UI), floor(sy / UI), r, c)
        ucirc(floor(sx / UI), floor(sy / UI), r + 1, c)
      end
    end
    ucirc(LW // 2, LH // 2, floor(TORP.cone * LW / 2 / 0.9), 0x7FFFE8)
  end
  if s.hover_t < HOVER.time and not a.on_ground then
    urectfill(LW // 2 - 20, LH // 2 + 22, floor(40 * s.hover_t / HOVER.time), 2, 0x3FF2D8)
  end
end
