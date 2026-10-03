-- Fuse, damage (the kit of Junkrat). Frag Launcher (primary: bouncing
-- grenades that blow up on an enemy or after a while), Concussion Mine
-- (ability 1: thrown, it sticks; the secondary or ability 1 again blows it
-- up and throws everyone around, Fuse too: the mine jump), Steel Trap
-- (ability 2: jaws on the floor that bite and hold an enemy), Boom Wheel
-- (ultimate: a spiked bomb wheel driven from afar, blown up at will), Total
-- Mayhem (passive: bombs spill out when he falls).

local U = {
  id = "fuse", name = "Fuse", short = "FUS", role = "damage", rgb = 0xF28A1E,
  ult_cost = 1650,
  desc = "A scrapyard demolitions punk",
}
H.fuse = U
HERO_ORDER[#HERO_ORDER + 1] = "fuse"

local FORM = {
  name = "fuse", tp = "fuse", fp = "fuse_fp",
  hp = 250, armor = 0, speed = 5.5, radius = 0.36, height = 1.85, eye = 1.7, crouch_eye = 1.2,
  jump = 6.4, head = "head", corpse = 3,
  clips = { idle = "idle", walk = "walk", run = "run", air = "air", jump = "jump", land = "land",
            crouch = "crouch", death = "death" },
  walk_speed = 2.6, run_speed = 5.5, jump_len = 0.35,
  hips_bone = "hips", body_bone = "spine", aim_bones = { "chest" }, aim_k = 0.8, layer_bone = "chest",
}
U.forms = { fuse = FORM }

local FRAG = { rate = 0.66, ammo = 5, reload = 1.5, speed = 22, grav = 14, bounce = 0.55, bounces = 3, fuse = 1.5,
               dmg = 40, splash = 80, radius = 2.0, self_k = 0.5 }
local MINE = { charges = 2, recharge = 8, speed = 16, dmg = 100, radius = 4, push = 12, self_push = 13 }
local TRAP = { cd = 10, speed = 9, arm = 0.5, reach = 0.9, dmg = 100, root = 1.5 }
local WHEEL = { time = 10, speed = 11, jump = 7, dmg = 500, radius = 7, low = 0.3 }
local MAYHEM = { n = 5, splash = 40 }

U.numbers = { frag = FRAG, mine = MINE, trap = TRAP, wheel = WHEEL, mayhem = MAYHEM }

U.hud = {
  { key = "ab1", name = "Concussion Mine", icon = 8, state = function(a)
      local s = a.st
      local left = s.mines >= 1 and 0 or (1 - s.mines) * MINE.recharge
      return left, MINE.recharge, s.mine ~= nil, true, floor(s.mines) end },
  { key = "ab2", name = "Steel Trap", icon = 4, state = function(a)
      return a.st.trap_cd, TRAP.cd, a.st.trap ~= nil, true end },
  { key = "fire", name = "Frag Launcher", ammo = function(a)
      return a.st.ammo, FRAG.ammo, a.st.reload_t > 0 end },
}

U.ult_name = function(a) return "BOOM WHEEL" end

function U.spawn(a)
  Actors.set_form(a, FORM)
  local s = a.st
  s.fire_cd, s.ammo, s.reload_t, s.mines, s.trap_cd = 0, FRAG.ammo, 0, MINE.charges, 0
  s.mine, s.wheel = nil, nil
  a.override = nil
end

local mine_m, trap_m

-- ---------------------------------------------------------------- the launcher

local function frag(a, x, y, z, vx, vy, vz, dmg, splash, life)
  local p = Proj.spawn({ x = x, y = y, z = z, vx = vx, vy = vy, vz = vz, owner = a, dmg = dmg, splash = splash,
                         radius = FRAG.radius, grav = FRAG.grav, bounce = FRAG.bounce, bounces = FRAG.bounces,
                         life = life, fuse = true, size = 0.09, kind = "grenade", self_k = FRAG.self_k,
                         boom = 0.7, boom_rgb = 0xFFA040,
                         on_bounce = function(p) if a == G.local_actor then Snd.play("step", 0.4) end end,
                         draw = function(p)
                           point3d(p.x, p.y, p.z, 0.09, 0x2E2E30)
                           if floor(p.age * 10) % 2 == 0 then point3d(p.x, p.y + 0.07, p.z, 0.035, 0xFF3A2A) end
                         end })
  p.trail, p.trail_rgb = 0.6, 0xFFB050
  return p
end

local function fire_frag(a)
  local s = a.st
  s.fire_cd = FRAG.rate
  s.ammo = s.ammo - 1
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  local sy, cy = sin(a.yaw), cos(a.yaw)
  local x, y, z = ex + cy * 0.2 + fx * 0.6, ey - 0.15 + fy * 0.6, ez - sy * 0.2 + fz * 0.6
  frag(a, x, y, z, fx * FRAG.speed, fy * FRAG.speed + 2.5, fz * FRAG.speed, FRAG.dmg, FRAG.splash, FRAG.fuse)
  Fx.burst(x, y, z, 4, 2, 1, 0.2, 0.1, 0xB0A8A0, -0.5, fx, fy, fz)
  Actors.layer(a, "fire", true)
  s.fire_anim = 0.66
  if a == G.local_actor then Snd.play("bump", 0.8) end
end

-- ---------------------------------------------------------------- Concussion Mine

local function mine_boom(a)
  local s = a.st
  local p = s.mine
  if not p then return end
  s.mine = nil
  p.remove = true
  local x, y, z = p.x, p.y, p.z
  for _, o in ipairs(Actors.list) do
    if o.alive then
      local cx, cy, cz = o.x - x, o.y + o.height * 0.5 - y, o.z - z
      local d = len3(cx, cy, cz)
      if d < MINE.radius and (o == a or o.team ~= a.team) and World.clear(x, y + 0.1, z, o.x, o.y + o.height * 0.5, o.z) then
        local k = 1 - 0.5 * d / MINE.radius
        if d < 0.01 then cx, cy, cz, d = 0, 1, 0, 1 end
        local push = o == a and MINE.self_push or MINE.push
        Actors.push(o, cx / d * push * k, 4 + 5 * k, cz / d * push * k)
        if o ~= a then Actors.damage(o, MINE.dmg, a, false, "splash") end
      end
    end
  end
  Fx.explode(x, y, z, 1.4, 0xFFC040)
  log("overbit mine " .. a.name)
  Snd.play("boom")
end

local function throw_mine(a)
  local s = a.st
  if s.mine then mine_boom(a) return end
  s.mines = s.mines - 1
  local ex, ey, ez = Actors.eye(a)
  local fx, fy, fz = Actors.aim_dir(a)
  s.mine = Proj.spawn({ x = ex + fx * 0.5, y = ey - 0.2, z = ez + fz * 0.5, vx = fx * MINE.speed, vy = fy * MINE.speed + 3,
                        vz = fz * MINE.speed, owner = a, grav = 14, life = 30, size = 0.12, stick = true,
                        bounce_actors = true, kind = "mine",
                        draw = function(p)
                          if not mine_m then mine_m = model("fuse_mine") end
                          local up = p.stuck and 0 or p.age * 12
                          draw3d(mine_m, p.x, p.y, p.z, up, p.age * 3, 0, 1, 0)
                        end })
  Actors.layer(a, "mine", true)
  s.mine_anim = 0.45
  if a == G.local_actor then Snd.play("ui_back") end
end

-- ---------------------------------------------------------------- Steel Trap

local function throw_trap(a)
  local s = a.st
  s.trap_cd = TRAP.cd
  local ex, ey, ez = Actors.eye(a)
  local fx, _, fz = Actors.aim_dir(a)
  local trap = { owner = a }
  s.trap = trap
  trap.p = Proj.spawn({ x = ex + fx * 0.5, y = ey - 0.4, z = ez + fz * 0.5, vx = fx * TRAP.speed, vy = 4, vz = fz * TRAP.speed,
                        owner = a, grav = 14, life = 3, size = 0.15, stick = true, bounce_actors = true, kind = "trap",
                        draw = function(p) end,
                        on_stick = function(p)
                          trap.x, trap.z = p.x, p.z
                          trap.y = World.floor_at(p.x, p.z, p.y + 0.3, 0.1) or p.y
                          trap.t = 0
                          trap.mesh = model("fuse_trap")
                          animate(trap.mesh, "open", 0)
                          p.remove = true
                        end })
  Actors.layer(a, "trap", true)
  s.trap_anim = 0.6
end

local function tick_trap(a)
  local s = a.st
  local tr = s.trap
  if not tr then return end
  if not tr.t then
    if tr.p.age >= tr.p.life - DT or tr.p.remove and not tr.x then s.trap = nil end   -- lost (fell off the world)
    return
  end
  tr.t = tr.t + DT
  if tr.shut then
    animate(tr.mesh, "shut", tr.t - tr.shut)
    local o = tr.victim
    if tr.t - tr.shut > TRAP.root + 0.5 or not (o and o.alive) then s.trap = nil end
    return
  end
  if tr.t < TRAP.arm then return end
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local dx, dz = o.x - tr.x, o.z - tr.z
      if dx * dx + dz * dz < (TRAP.reach + o.radius * 0.5) ^ 2 and abs(o.y - tr.y) < 0.6 then
        tr.shut, tr.victim = tr.t, o
        Actors.damage(o, TRAP.dmg, a, false, "trap")
        o.fx.rooted_t = TRAP.root
        o.x, o.z = tr.x, tr.z
        o.vx, o.vz = 0, 0
        Fx.burst(tr.x, tr.y + 0.2, tr.z, 8, 0, 2, 0.3, 0.06, 0xDDE2E6, 1)
        log("overbit trap " .. o.name)
        Snd.play("strike", 0.8)
        break
      end
    end
  end
end

-- ---------------------------------------------------------------- Boom Wheel

local wheel_m

local function wheel_boom(a)
  local s = a.st
  local w = s.wheel
  if not w then return end
  s.wheel = nil
  a.override = nil
  for _, o in ipairs(Actors.list) do
    if o.alive and o.team ~= a.team then
      local d = len3(o.x - w.x, (o.y - w.y) * 0.7, o.z - w.z)
      if d < WHEEL.radius and World.clear(w.x, w.y + 0.6, w.z, o.x, o.y + o.height * 0.5, o.z) then
        Actors.damage(o, WHEEL.dmg * (1 - (1 - WHEEL.low) * d / WHEEL.radius), a, false, "ult")
      end
    end
  end
  Fx.explode(w.x, w.y + 0.6, w.z, 4, 0xFFA040)
  Fx.flash, Fx.flash_rgb = 0.5, 0xFFD080
  log("overbit boom wheel " .. a.name)
  Snd.play("boom_big")
end

local function wheel_update(a, c)
  local w = a.st.wheel
  w.t = w.t - DT
  w.yaw = wrap_angle(w.yaw + c.look_x)
  local fx, fz = sin(w.yaw), cos(w.yaw)
  local steer = c.mx * 0.4
  w.vx = approach(w.vx, (fx + cos(w.yaw) * steer) * WHEEL.speed, 30 * DT)
  w.vz = approach(w.vz, (fz - sin(w.yaw) * steer) * WHEEL.speed, 30 * DT)
  if not w.on_ground or w.vy > 0 then w.vy = w.vy - 18 * DT end
  if c.jump_p and w.on_ground then w.vy, w.on_ground = WHEEL.jump, false end
  local og = World.move(w, w.vx * DT, w.vy * DT, w.vz * DT)
  w.on_ground = og
  if og and w.vy < 0 then w.vy = 0 end
  w.spin = w.spin + len3(w.vx, 0, w.vz) * DT / 0.5
  if random() < 0.6 * Fx.density then
    Fx.spawn(w.x - fx * 0.4, w.y + 0.1, w.z - fz * 0.4, -w.vx * 0.1, 1, -w.vz * 0.1, 0.5, 0.15, 0x8A7A68, 1, 0)
  end
  if c.fire_p or c.ult_p or w.t <= 0 or w.y < -20 then wheel_boom(a) end
end

-- the camera of the wheel's driver: behind it
function U.camera(a)
  local w = a.st.wheel
  a.cam3p = w ~= nil and a.alive
  if not a.cam3p then return false end
  Cam.orbit(w.x, w.y + 0.9, w.z, w.yaw, -0.22, 3.4, 80)
  return true
end

-- ---------------------------------------------------------------- the update

local TIMERS = { "fire_cd", "trap_cd", "fire_anim", "mine_anim", "trap_anim" }  -- once, not every frame
local LAYERS = { { "reload", "reload_t" }, { "fire", "fire_anim" }, { "mine", "mine_anim" }, { "trap", "trap_anim" } }  -- once, not every frame
function U.update(a, c)
  local s = a.st
  tick_trap(a)
  if not a.alive then
    if s.wheel then s.wheel = nil end
    return
  end
  for _, k in ipairs(TIMERS) do
    if s[k] and s[k] > 0 then s[k] = max(0, s[k] - DT) end
  end
  if s.mines < MINE.charges then s.mines = min(MINE.charges, s.mines + DT / MINE.recharge) end
  if s.reload_t > 0 then
    s.reload_t = s.reload_t - DT
    if s.reload_t <= 0 then s.ammo = FRAG.ammo end
  end
  if s.mine and s.mine.age >= s.mine.life - DT then s.mine = nil end
  -- driving the wheel: Fuse kneels, the stick and the look drive it
  if s.wheel then
    wheel_update(a, c)
    a.vx, a.vz = 0, 0
    Actors.physics(a)
    return
  end
  if c.ult_p and a.ult >= 100 then
    a.ult = 0
    local fx, fz = sin(a.yaw), cos(a.yaw)
    s.wheel = { x = a.x + fx * 1.2, y = a.y + 0.2, z = a.z + fz * 1.2, vx = 0, vy = 0, vz = 0, yaw = a.yaw, spin = 0,
                radius = 0.45, height = 1.0, on_ground = false, t = WHEEL.time }
    a.override = "wheel"
    a.anim.base, a.anim.bt = "wheel", 0
    Actors.layer_off(a)
    if a == G.local_actor then Snd.play("boost") end
    return
  end
  -- mines and the trap
  if c.ab1_p and (s.mine or s.mines >= 1) then throw_mine(a)
  elseif c.fire2_p and s.mine then mine_boom(a) end
  if c.ab2_p and s.trap_cd <= 0 then throw_trap(a) end
  -- the launcher
  if s.reload_t <= 0 and c.fire and s.fire_cd <= 0 then
    if s.ammo > 0 then fire_frag(a) end
    if s.ammo <= 0 then s.reload_t = FRAG.reload Actors.layer(a, "reload", true) end
  end
  if c.reload_p and s.ammo < FRAG.ammo and s.reload_t <= 0 then
    s.reload_t = FRAG.reload
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

-- Total Mayhem: a handful of bombs spill out
function U.on_death(a)
  a.override = nil
  a.st.wheel = nil
  for i = 1, MAYHEM.n do
    local an = 2 * pi * i / MAYHEM.n + grandom()
    frag(a, a.x, a.y + 1.0, a.z, cos(an) * 3, 4 + grandom() * 2, sin(an) * 3, 0, MAYHEM.splash, 1.0 + grandom() * 0.4)
  end
end

-- ---------------------------------------------------------------- drawing

function U.draw_extra(a)
  local s = a.st
  local tr = s.trap
  if tr and tr.mesh then draw3d(tr.mesh, tr.x, tr.y, tr.z, 0, 0, 0, 1, 0) end
  local w = s.wheel
  if w then
    if not wheel_m then wheel_m = model("fuse_wheel") end
    draw3d(wheel_m, w.x, w.y + 0.5, w.z, w.spin, w.yaw, 0, 1, 4)
    if floor(G.t * 6) % 2 == 0 then point3d(w.x, w.y + 1.05, w.z, 0.06, 0xFF3A2A) end
  end
end

function U.draw_fp_extra(a)
  U.draw_extra(a)
end

local fp_mesh

function U.draw_fp(a, cx, cy, cz, yaw, pitch, roll)
  if not fp_mesh then fp_mesh = model("fuse_fp") end
  local m = fp_mesh
  local s = a.st
  local base, t = "idle", G.t
  local speed = sqrt(a.vx * a.vx + a.vz * a.vz)
  if speed > 1 and a.on_ground then base, t = "run", G.t * speed / 5.5 end
  local layer, lt = nil, 0
  if s.reload_t > 0 then layer, lt = "reload", FRAG.reload - s.reload_t
  elseif (s.fire_anim or 0) > 0 then layer, lt = "fire", FRAG.rate - s.fire_anim
  elseif (s.mine_anim or 0) > 0 then layer, lt = "throw", 0.45 - s.mine_anim
  elseif (s.trap_anim or 0) > 0 then layer, lt = "throw", (0.6 - s.trap_anim) * 0.75 end
  if layer then animate(m, base, t, layer, lt, 1) else animate(m, base, t) end
  local q = G.quality
  draw3d(m, cx, cy, cz, -pitch, yaw, roll or 0, 1, 64 + (q >= 1 and 4 or 0))
end

function U.draw_hud(a)
  local w = a.st.wheel
  if not w then return end
  local str = string.format("BOOM WHEEL %.1f", max(0, w.t))
  print(str, 160 - #str * 3, 30, 0xFFC040)
  local k = Hud.key("fire")
  print("detonate", 172, 44, 0xFFFFFF)
  prompt(k, 150, 42, true)
end
