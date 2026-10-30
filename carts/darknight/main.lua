-- Dark Night: the Hunter in the Clinic (carts/darknight).
-- Keyboard (rawkeys): WASD move; SPACE dodge (no direction: backstep, forward: roll, left or right: side jump);
-- SHIFT opens and closes the saw; E the pistol; Q lock on; U I O the three saw attacks; P the charged attack (hold);
-- ESC the options menu; the arrows turn the camera. A pad has the same moves (see PAD below).
-- The map and the Hunter come from resource files merged into this cartridge: art/maps/clinic.bmk (the map kit) and
-- art/models/dnhunter.bmm (the Hunter with the saw in the right hand and the pistol in the left, in two forms).
-- The enemies come from art/models/dnenemy.bmm: villagers (the clinic), huntsmen and rabid dogs (the courtyard and the alley).
rawkeys(true)

local PI = math.pi
local sin, cos, sqrt, abs, floor, max, min, atan = math.sin, math.cos, math.sqrt, math.abs, math.floor, math.max, math.min, math.atan

-- ---------------------------------------------------------------- the keys (USB HID usages) and the pad
local KEY = { w = 0x1A, a = 0x04, s = 0x16, d = 0x07, q = 0x14, e = 0x08, u = 0x18, i = 0x0C, o = 0x12, p = 0x13,
              space = 0x2C, shift = 0xE1, esc = 0x29, enter = 0x28, up = 0x52, down = 0x51, left = 0x50, right = 0x4F, f9 = 0x42 }
-- action -> keys and pad buttons
local BIND = {
  fwd = { KEY.w }, back = { KEY.s }, left = { KEY.a }, right = { KEY.d },
  dodge = { KEY.space }, saw = { KEY.shift }, gun = { KEY.e }, lock = { KEY.q },
  atk1 = { KEY.u }, atk2 = { KEY.i }, atk3 = { KEY.o }, charge = { KEY.p },
  menu = { KEY.esc }, ok = { KEY.enter, KEY.space },
  camL = { KEY.left }, camR = { KEY.right }, camU = { KEY.up }, camD = { KEY.down },
  skip = { KEY.f9 },                         -- a developer key: the next place of the walk (SKIPS)
}
local PAD = { dodge = "b", saw = "y", gun = "x", lock = "l1", atk1 = "a", atk2 = "r1", atk3 = "l2", charge = "r2", menu = "start" }

-- ---------------------------------------------------------------- the ids of the clinic kit (art/maps/clinic/build_clinic.py)
local ID = { bed = 1040, bed_ns = 1042, table = 1044, cabinet = 1046, lamp = 1048, lantern = 1050, door_ew = 1052,
             door_ns = 1054, door_ew_l = 1056, door_ns_l = 1058, gate_ns = 1060, crate = 1062, barrel = 1064,
             pillar = 1066, bench = 1068, bench_ns = 1070, cart = 1072, barricade = 1074, enemy = 1076, start = 1024 }
local RADIUS = { bed = 0.7, bed_ns = 0.7, table = 0.6, cabinet = 0.5, lamp = 0.15, lantern = 0.3, door_ew = 0.5, door_ns = 0.5,
                 door_ew_l = 0.5, door_ns_l = 0.5, gate_ns = 1.5, crate = 0.5, barrel = 0.4, pillar = 0.4, bench = 0.8,
                 bench_ns = 0.8, cart = 0.9, barricade = 1.5 }
local NAME_OF = {}
for n, id in pairs(ID) do NAME_OF[id] = n end

-- ---------------------------------------------------------------- the moves of the saw
-- clip, frames, rate (the clips run at 12 frames a second), the frames where it hurts, the reach, the damage,
-- a step forward during the cut, the frame from which the next attack may follow
local FPS = 12
local ATK = {
  { -- closed: short and quick
    { clip = "saw_c1", frames = 16, rate = 1.3, hit = { 6, 9 }, reach = 1.9, dmg = 14, step = 0.5, chain = 10 },
    { clip = "saw_c2", frames = 15, rate = 1.3, hit = { 5, 8 }, reach = 1.9, dmg = 16, step = 0.5, chain = 9 },
    { clip = "saw_c3", frames = 20, rate = 1.3, hit = { 7, 10 }, reach = 2.0, dmg = 22, step = 0.7, chain = 13 },
    charge = { clip = "saw_c_charge", frames = 26, rate = 1.3, hold = 9, resume = 11, hit = { 15, 19 }, reach = 2.2, dmg = 38, step = 1.0 },
  },
  { -- open: long and heavy
    { clip = "saw_o1", frames = 19, rate = 1.15, hit = { 7, 12 }, reach = 2.6, dmg = 20, step = 0.6, chain = 12 },
    { clip = "saw_o2", frames = 18, rate = 1.15, hit = { 6, 11 }, reach = 2.6, dmg = 22, step = 0.6, chain = 11 },
    { clip = "saw_o3", frames = 24, rate = 1.15, hit = { 8, 13 }, reach = 2.8, dmg = 30, step = 0.8, chain = 16 },
    charge = { clip = "saw_o_charge", frames = 33, rate = 1.15, hold = 12, resume = 16, hit = { 21, 26 }, reach = 3.0, dmg = 52, step = 1.2 },
  },
}
-- F9: the places of the walk, in order: the hall door, the atrium, the courtyard, the way to the gate
local SKIPS = { { 14.5, 8.5 }, { 37.5, 10.5 }, { 50.5, 12.5 }, { 66.5, 10.5 } }
local DODGE = {
  roll = { clip = "roll", frames = 16, rate = 1.6, dist = 3.0, inv = { 0.08, 0.6 } },
  back = { clip = "backstep", frames = 16, rate = 1.6, dist = 2.6, inv = { 0.04, 0.4 } },
  left = { clip = "dodge_l", frames = 12, rate = 1.5, dist = 2.2, inv = { 0.04, 0.45 } },
  right = { clip = "dodge_r", frames = 12, rate = 1.5, dist = 2.2, inv = { 0.04, 0.45 } },
}
local GUN = { clip = "gun", frames = 14, rate = 1.4, shot = 5, dmg = 30, range = 16 }
-- the enemies: health, speed, the clip that moves them, when in the attack clip it hurts (seconds), how long it lasts, the damage,
-- the reach, how far they see, how high they are (for the bar over the head)
local TYPES = {
  villager = { hp = 60, speed = 1.6, move = "walk", rate = 1.4, atk = "attack1", strike = 0.58, total = 1.35, dmg = 14, reach = 1.5, aggro = 7, height = 2.0 },
  huntsman = { hp = 90, speed = 1.3, move = "walk", rate = 1.3, atk = "attack1", strike = 0.85, total = 1.75, dmg = 22, reach = 2.1, aggro = 8, height = 2.4 },
  dog = { hp = 40, speed = 3.5, move = "run", rate = 1.3, atk = "bite", strike = 0.42, total = 1.0, dmg = 10, reach = 1.5, aggro = 10, height = 1.3 },
}

-- ---------------------------------------------------------------- the state
local world, objs, doors, lanterns, enemies = nil, {}, {}, {}, {}
local M3 = {}                         -- the meshes: hunter_c, hunter_o, and the kit's by name (and name_f)
local P = {}                          -- the player
local Cam = { yaw = PI / 2, pitch = -0.95, dist = 7.5, x = 0, y = 0, z = 0 }
local O = { res = 1, textures = true, bright = 2, help = false }      -- the options
local G = { frame = 0, state = "play", msg = "", msgt = 0, flash = 0, died = 0, menu = 1, spawn = { 0, 0 } }
local held, prev, pressed = {}, {}, {}
local fx = {}                         -- short effects: sparks and tracers
local SKY = 0x2C3346

local function dir(yaw) return sin(yaw), cos(yaw) end                    -- a facing direction from an angle
local function yaw_of(dx, dz) return atan(dx, dz) end
local function wrap(a) while a > PI do a = a - 2 * PI end while a < -PI do a = a + 2 * PI end return a end
local function turn_to(a, b, rate) local d = wrap(b - a); return a + max(-rate, min(rate, d)) end
local function lerp(a, b, t) return a + (b - a) * t end
local function say(text, secs) G.msg, G.msgt = text, secs or 2.5 end
local function ev(s) log("darknight: " .. s) end            -- the events, for tests/darknight (a line each)

-- ---------------------------------------------------------------- input
local function read_input()
  for a, keys in pairs(BIND) do
    local d = false
    for _, u in ipairs(keys) do if keydown(u) then d = true end end
    local pb = PAD[a]
    if pb and btn(pb) then d = true end
    prev[a] = held[a] or false
    held[a] = d
    pressed[a] = d and not prev[a]
  end
  local sx, sy = stick()
  local mx = (held.right and 1 or 0) - (held.left and 1 or 0) + sx
  local my = (held.fwd and 1 or 0) - (held.back and 1 or 0) - sy
  local l = sqrt(mx * mx + my * my)
  if l > 1 then mx, my = mx / l, my / l end
  if l < 0.2 then mx, my = 0, 0 end
  return mx, my
end

-- ---------------------------------------------------------------- the map and the objects
local function map_height(x, z) return map3d_height(world, x, z) end

-- which enemy stands where: villagers in the clinic, then huntsmen and dogs in turn in the courtyard, dogs in the alley
local function enemy_kind(x, z, n)
  if x < 43 then return "villager" end
  if z >= 31 then return "dog" end
  return n % 2 == 0 and "huntsman" or "dog"
end

local function load_world()
  world = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
  objs, doors, lanterns, enemies = {}, {}, {}, {}
  local mw, mh = msize()
  for cz = 0, mh - 1, 2 do
    for cx = 0, mw - 1, 2 do
      local id = mget(cx, cz, "objects")
      if id and id >= 1024 then
        local x, z = cx / 2 + 0.5, cz / 2 + 0.5
        local name = NAME_OF[id]
        local y = map_height(x, z) or 0
        if name == "start" then
          G.spawn = { x, z }
        elseif name == "enemy" then
          local kind = enemy_kind(x, z, #enemies)
          local ty = TYPES[kind]
          enemies[#enemies + 1] = { kind = kind, ty = ty, x = x, z = z, hx = x, hz = z, y = y, hp = ty.hp, maxhp = ty.hp, st = "idle", t = 0, yaw = 0,
                                    flash = 0, anim = "idle", at = 0 }
        elseif name then
          local o = { name = name, x = x, y = y, z = z, r = RADIUS[name] or 0 }
          objs[#objs + 1] = o
          if name:find("^door") then o.door = true; o.locked = name:find("_l$") ~= nil; o.open = 0; doors[#doors + 1] = o end
          if name == "lantern" then lanterns[#lanterns + 1] = o end
        end
      end
    end
  end
end

local function object_mesh(name)
  return O.textures and M3[name] or M3[name .. "_f"] or M3[name]
end

-- a circle of the solid objects (the beds and the barrels are circles to the player)
local function hits_object(x, z, r)
  for i = 1, #objs do
    local o = objs[i]
    if o.r > 0 and not (o.door and o.open > 0.5) then
      local dx, dz = x - o.x, z - o.z
      local rr = o.r + r
      if dx * dx + dz * dz < rr * rr then return o end
    end
  end
end

local function blocked(x, z, y, r)
  return map3d_blocked(world, x, y + 0.9, z, r) or hits_object(x, z, r) ~= nil
end

-- a move with sliding along the walls
local function try_move(e, dx, dz, r)
  local y = e.y
  if not blocked(e.x + dx, e.z + dz, y, r) then e.x, e.z = e.x + dx, e.z + dz; return true end
  if dx ~= 0 and not blocked(e.x + dx, e.z, y, r) then e.x = e.x + dx; return true end
  if dz ~= 0 and not blocked(e.x, e.z + dz, y, r) then e.z = e.z + dz; return true end
  return false
end

-- ---------------------------------------------------------------- the player
local function reset_player(x, z)
  P.x, P.z = x, z
  P.y = map_height(x, z) or 0
  P.yaw = PI / 2
  P.hp, P.maxhp, P.ammo = 100, 100, 12
  P.form = 1                            -- 1 closed, 2 open
  P.mode, P.t = "free", 0
  P.anim, P.at, P.fade_from, P.fade_t = "idle", 0, nil, 1
  P.combo, P.queued, P.inv = 0, nil, 0
  P.lock = nil
  P.swing = {}
  P.charge = 0
  P.flash = 0
end

-- play a clip: a short cross fade from the one before
local function set_anim(name, t0)
  if P.anim == name then return end
  P.fade_from, P.fade_at = P.anim, P.at
  P.fade_t = 0
  P.anim, P.at = name, t0 or 0
end

-- The dodge follows the keys, as the player holds them (mx right, my forward, in the camera's frame; locked on, forward
-- is toward the target): nothing held: a backstep from where the Hunter looks; forward: a roll that way; left or right:
-- a side jump, the Hunter facing forward; back: a backstep, facing forward.
local function start_dodge(mx, my, wx, wz)
  local cfx, cfz = dir(Cam.yaw)
  if P.lock then
    cfx, cfz = P.lock.x - P.x, P.lock.z - P.z
    local l = sqrt(cfx * cfx + cfz * cfz) + 1e-6
    cfx, cfz = cfx / l, cfz / l
  end
  local kind, dx, dz
  if mx == 0 and my == 0 then
    local px, pz = dir(P.yaw)
    kind, dx, dz = "back", -px, -pz
  elseif my > 0 and my >= abs(mx) then
    kind, dx, dz = "roll", wx, wz
    P.yaw = yaw_of(wx, wz)
  else
    P.yaw = yaw_of(cfx, cfz)
    if my < 0 and -my > abs(mx) then kind, dx, dz = "back", wx, wz
    else kind, dx, dz = mx > 0 and "right" or "left", wx, wz end
  end
  local d = DODGE[kind]
  P.mode, P.t, P.dodge = "dodge", 0, { kind = kind, dx = dx, dz = dz, done = 0, def = d }
  ev("dodge " .. kind)
  set_anim(d.clip, 0)
  P.at = 0
end

local function start_attack(n)
  local set = ATK[P.form]
  local a = set[n]
  P.mode, P.t, P.atk, P.combo = "attack", 0, a, n
  ev("attack " .. (P.form == 1 and "closed " or "open ") .. n)
  P.swing = {}
  P.queued = nil
  set_anim(a.clip, 0)
  P.at = 0
  if P.lock then P.yaw = yaw_of(P.lock.x - P.x, P.lock.z - P.z) end
end

local function start_charge()
  local a = ATK[P.form].charge
  P.mode, P.t, P.atk, P.charge, P.released = "charge", 0, a, 0, false
  ev("charge " .. (P.form == 1 and "closed" or "open"))
  P.swing = {}
  set_anim(a.clip, 0)
  P.at = 0
end

local function start_gun()
  if P.ammo <= 0 then say("no quicksilver bullets: light a lantern", 2); return end
  P.mode, P.t, P.shot = "gun", 0, false
  set_anim(GUN.clip, 0)
  P.at = 0
  if P.lock then P.yaw = yaw_of(P.lock.x - P.x, P.lock.z - P.z) end
end

local function hurt(dmg, from_x, from_z)
  if P.inv > 0 or P.hp <= 0 then return end
  P.hp = max(0, P.hp - dmg)
  P.flash = 0.35
  P.inv = 0.5
  local dx, dz = P.x - from_x, P.z - from_z
  local l = sqrt(dx * dx + dz * dz) + 1e-6
  try_move(P, dx / l * 0.5, dz / l * 0.5, 0.32)
  ev("hurt hp " .. P.hp)
  if P.hp <= 0 then G.state, G.died = "dead", 0; ev("died") end
end

local function spark(x, y, z, col, n)
  for i = 1, n or 6 do
    fx[#fx + 1] = { kind = "spark", x = x, y = y, z = z, vx = (math.random() - 0.5) * 3, vy = math.random() * 2.5,
                    vz = (math.random() - 0.5) * 3, t = 0.35 + math.random() * 0.25, col = col or 0xFFD060 }
  end
end

local function damage_enemy(e, dmg, kind)
  if e.st == "dead" then return end
  e.hp = e.hp - dmg
  e.flash = 0.2
  spark(e.x, e.y + 1.2, e.z, kind == "gun" and 0xFFE080 or 0xB02020, 8)
  if e.hp <= 0 then e.st, e.t = "dead", 0; ev("enemy dead " .. kind); if P.lock == e then P.lock = nil end
  elseif e.st ~= "attack" or e.t < e.ty.strike then e.st, e.t, e.struck = "stagger", 0, false end
end

local function melee_hits(a)
  local fxx, fzz = dir(P.yaw)
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" and not P.swing[e] then
      local dx, dz = e.x - P.x, e.z - P.z
      local d = sqrt(dx * dx + dz * dz)
      if d < a.reach + 0.4 and d > 0.001 and (dx * fxx + dz * fzz) / d > 0.3 then
        P.swing[e] = true
        damage_enemy(e, a.dmg, "saw")
      end
    end
  end
end

local function fire_bullet()
  local fxx, fzz = dir(P.yaw)
  local sx, sy, sz = P.x + fxx * 0.6, P.y + 1.35, P.z + fzz * 0.6
  local best, bt = nil, GUN.range
  -- the ray stops at a wall
  local wall = GUN.range
  for t = 0.6, GUN.range, 0.4 do
    if map3d_blocked(world, sx + fxx * t, sy, sz + fzz * t, 0.08) then wall = t; break end
  end
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" then
      local dx, dz = e.x - sx, e.z - sz
      local along = dx * fxx + dz * fzz
      local side = abs(dx * fzz - dz * fxx)
      if along > 0 and along < min(wall, bt) and side < 0.6 then best, bt = e, along end
    end
  end
  local tl = best and bt or min(wall, GUN.range)
  fx[#fx + 1] = { kind = "tracer", x = sx, y = sy, z = sz, x2 = sx + fxx * tl, y2 = sy, z2 = sz + fzz * tl, t = 0.08 }
  fx[#fx + 1] = { kind = "flash", x = sx, y = sy, z = sz, t = 0.08 }
  if best then damage_enemy(best, GUN.dmg, "gun") else spark(sx + fxx * tl, sy, sz + fzz * tl, 0xC0C0C0, 4) end
  P.ammo = P.ammo - 1
  ev("shot " .. (best and "hit" or "miss") .. " bullets " .. P.ammo)
end

local function pick_lock()
  local best, bd = nil, 15
  local fxx, fzz = dir(Cam.yaw)
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" then
      local dx, dz = e.x - P.x, e.z - P.z
      local d = sqrt(dx * dx + dz * dz)
      local front = (dx * fxx + dz * fzz) / (d + 1e-6)
      if d < bd and front > -0.2 then best, bd = e, d end
    end
  end
  return best
end

local function update_player(mx, my)
  local dt = 1 / 60
  P.t = P.t + dt
  P.inv = max(0, P.inv - dt)
  P.flash = max(0, P.flash - dt)
  -- the camera's forward gives the meaning of W
  local cfx, cfz = dir(Cam.yaw)
  local crx, crz = -cfz, cfx
  local wx, wz = cfx * my + crx * mx, cfz * my + crz * mx
  local moving = (wx ~= 0 or wz ~= 0)

  if pressed.lock then
    if P.lock then P.lock = nil; ev("lock off") else P.lock = pick_lock(); if P.lock then ev("lock on") else say("nothing to lock on", 1.2) end end
  end
  if P.lock and (P.lock.st == "dead" or ((P.lock.x - P.x) ^ 2 + (P.lock.z - P.z) ^ 2) > 22 * 22) then P.lock = nil end

  if P.mode == "free" then
    local speed, clip, rate = 0, "idle", 1
    if moving then
      if P.lock then speed, clip, rate = 1.7, "walk", 1.8 else speed, clip, rate = 3.4, "run", 1.25 end
      local l = sqrt(wx * wx + wz * wz)
      try_move(P, wx / l * speed * dt * min(1, l), wz / l * speed * dt * min(1, l), 0.32)
      if not P.lock then P.yaw = turn_to(P.yaw, yaw_of(wx, wz), 12 * dt) end
    end
    if P.lock then P.yaw = turn_to(P.yaw, yaw_of(P.lock.x - P.x, P.lock.z - P.z), 14 * dt) end
    set_anim(clip)
    P.at = P.at + dt * rate
    if pressed.dodge then start_dodge(mx, my, wx, wz)
    elseif pressed.saw then P.mode, P.t = "toggle", 0
    elseif pressed.gun then start_gun()
    elseif pressed.charge then start_charge()
    elseif pressed.atk1 then start_attack(1)
    elseif pressed.atk2 then start_attack(2)
    elseif pressed.atk3 then start_attack(3) end

  elseif P.mode == "toggle" then
    P.at = P.at + dt
    if P.t > 0.12 and not P.toggled then P.form = 3 - P.form; P.toggled = true; ev("saw " .. (P.form == 1 and "closed" or "open")); spark(P.x, P.y + 1.3, P.z, 0xC8C8D0, 10)
      say(P.form == 1 and "saw closed" or "saw open", 1.2) end
    if P.t > 0.35 then P.mode, P.toggled = "free", false end

  elseif P.mode == "attack" then
    local a = P.atk
    local rate = a.rate
    P.at = P.at + dt * rate
    local f = P.at * FPS
    if f >= a.hit[1] and f <= a.hit[2] then
      melee_hits(a)
      local fxx, fzz = dir(P.yaw)
      local s = a.step * dt * rate * FPS / max(1, (a.hit[2] - a.hit[1] + 1))
      try_move(P, fxx * s, fzz * s, 0.32)
    end
    -- the next attack: pressed during the cut or the recovery
    for n, act in ipairs({ "atk1", "atk2", "atk3" }) do if pressed[act] then P.queued = n end end
    if pressed.dodge and f >= a.hit[2] then start_dodge(mx, my, wx, wz)
    elseif P.queued and f >= a.chain then start_attack(P.queued)
    elseif f >= a.frames - 1 then P.mode, P.combo = "free", 0 end

  elseif P.mode == "charge" then
    local a = P.atk
    local f = P.at * FPS
    if not P.released then
      P.charge = P.charge + dt
      -- wind up to the hold pose, then wait there while P is held
      if f < a.hold then P.at = P.at + dt * a.rate else P.at = (a.hold + 0.4 * math.sin(P.charge * 14)) / FPS end
      if not held.charge then P.released = true; P.at = max(P.at, a.hold / FPS); P.at = max(P.at, a.resume / FPS - 0.01) end
    else
      P.at = P.at + dt * a.rate
      local ff = P.at * FPS
      if ff >= a.hit[1] and ff <= a.hit[2] then
        local boost = min(1.8, 1 + P.charge * 0.5)
        local aa = { reach = a.reach, dmg = a.dmg * boost }
        melee_hits(aa)
        local fxx, fzz = dir(P.yaw)
        try_move(P, fxx * a.step * dt, fzz * a.step * dt, 0.32)
      end
      if ff >= a.frames - 1 then P.mode = "free" end
    end

  elseif P.mode == "gun" then
    P.at = P.at + dt * GUN.rate
    local f = P.at * FPS
    if f >= GUN.shot and not P.shot then P.shot = true; fire_bullet() end
    if f >= GUN.frames - 1 then P.mode = "free" end

  elseif P.mode == "dodge" then
    local D = P.dodge
    local d = D.def
    P.at = P.at + dt * d.rate
    local u = min(1, P.at * FPS / (d.frames - 1))
    -- the displacement follows a smooth curve
    local s = u * u * (3 - 2 * u)
    local want = d.dist * s
    local step = want - D.done
    D.done = want
    local l = sqrt(D.dx * D.dx + D.dz * D.dz)
    if l > 0 then try_move(P, D.dx / l * step, D.dz / l * step, 0.32) end
    local tsec = P.at
    if tsec >= d.inv[1] and tsec <= d.inv[2] then P.inv = max(P.inv, 0.05) end
    if u >= 1 then P.mode = "free" end
  end

  -- the floor under the feet (stairs: follow, not jump)
  local h = map_height(P.x, P.z)
  if h then P.y = lerp(P.y, h, 0.4) end
end

-- ---------------------------------------------------------------- the enemies (placeholders)
local function update_enemies()
  local dt = 1 / 60
  for _, e in ipairs(enemies) do
    local ty = e.ty
    e.t = e.t + dt
    e.flash = max(0, e.flash - dt)
    local dx, dz = P.x - e.x, P.z - e.z
    local d = sqrt(dx * dx + dz * dz)
    local clip, rate = "idle", 1.0
    if e.st == "idle" then
      if d < ty.aggro and P.hp > 0 then e.st, e.t = "chase", 0 end
    elseif e.st == "chase" then
      e.yaw = turn_to(e.yaw, yaw_of(dx, dz), 8 * dt)
      clip, rate = ty.move, ty.rate
      if d > ty.reach * 0.7 then
        try_move(e, dx / d * ty.speed * dt, dz / d * ty.speed * dt, 0.35)
      else e.st, e.t, e.struck = "attack", 0, false end
      if d > ty.aggro * 2.2 then e.st = "idle" end
    elseif e.st == "attack" then
      clip, rate = ty.atk, 1.0
      if e.t < ty.strike then e.yaw = turn_to(e.yaw, yaw_of(dx, dz), 6 * dt) end
      if e.t >= ty.strike and not e.struck then
        e.struck = true
        if d < ty.reach then hurt(ty.dmg, e.x, e.z) end
      end
      if e.t > ty.total then e.st, e.t = "chase", 0 end
    elseif e.st == "stagger" then
      clip = "hit"
      if e.t > 0.45 then e.st, e.t = "chase", 0 end
    elseif e.st == "dead" then
      clip = "death"
    end
    if clip ~= e.anim then e.anim, e.at = clip, 0 end
    e.at = e.at + dt * rate
    -- the enemies do not stand in each other
    for _, o in ipairs(enemies) do
      if o ~= e and o.st ~= "dead" and e.st ~= "dead" then
        local ox, oz = e.x - o.x, e.z - o.z
        local od = sqrt(ox * ox + oz * oz)
        if od < 0.8 and od > 0.001 then try_move(e, ox / od * 0.02, oz / od * 0.02, 0.35) end
      end
    end
    if d < 0.7 and e.st ~= "dead" and d > 0.001 then try_move(P, -dx / d * 0.03, -dz / d * 0.03, 0.32) end
    local h = map_height(e.x, e.z)
    if h then e.y = h end
  end
end

-- ---------------------------------------------------------------- doors, lanterns, effects
local function update_world()
  local dt = 1 / 60
  for _, d in ipairs(doors) do
    local dx, dz = P.x - d.x, P.z - d.z
    local near = dx * dx + dz * dz
    if not d.locked then
      if near < 1.8 * 1.8 then
        if d.open == 0 then ev("door opens") end
        d.open = min(1, d.open + dt * 4)
      elseif near > 3.2 * 3.2 then d.open = max(0, d.open - dt * 2) end
    elseif near < 1.5 * 1.5 and G.frame % 120 == 0 then
      say("locked", 1.2)
    end
  end
  for _, l in ipairs(lanterns) do
    local dx, dz = P.x - l.x, P.z - l.z
    if dx * dx + dz * dz < 1.6 * 1.6 and (G.spawn[3] ~= l) then
      G.spawn = { l.x + 1, l.z, l }
      P.hp, P.ammo = P.maxhp, 12
      say("the lantern is lit: health and bullets restored", 3)
      ev("lantern")
      spark(l.x, l.y + 1.4, l.z, 0xFFA030, 14)
    end
  end
  for i = #fx, 1, -1 do
    local f = fx[i]
    f.t = f.t - dt
    if f.kind == "spark" then f.x, f.y, f.z = f.x + f.vx * dt, f.y + f.vy * dt, f.z + f.vz * dt; f.vy = f.vy - 6 * dt end
    if f.t <= 0 then table.remove(fx, i) end
  end
  G.msgt = max(0, G.msgt - dt)
end

local function respawn()
  reset_player(G.spawn[1], G.spawn[2])
  for _, e in ipairs(enemies) do
    e.x, e.z, e.hp, e.st, e.t, e.flash, e.anim, e.at = e.hx, e.hz, e.maxhp, "idle", 0, 0, "idle", 0
  end
  G.state = "play"
  ev("respawn")
end

-- ---------------------------------------------------------------- the camera
local function update_camera()
  local dt = 1 / 60
  if held.camL then Cam.yaw = Cam.yaw - 1.8 * dt end
  if held.camR then Cam.yaw = Cam.yaw + 1.8 * dt end
  if held.camU then Cam.dist = max(4, Cam.dist - 4 * dt) end
  if held.camD then Cam.dist = min(12, Cam.dist + 4 * dt) end
  if P.lock then
    -- behind the player, looking at the target
    local want = yaw_of(P.lock.x - P.x, P.lock.z - P.z)
    Cam.yaw = Cam.yaw + wrap(want - Cam.yaw) * 0.08
  end
  local tx, ty, tz = P.x, P.y + 1.3, P.z
  local fxx, fzz = dir(Cam.yaw)
  -- a wall in the way: first look down more steeply (the camera goes up, not in); only then come closer
  local function clear(pitch, k)
    local hd, vd = Cam.dist * cos(pitch), Cam.dist * sin(-pitch)
    for i = 1, 14 do
      local u = i / 14 * k
      if map3d_blocked(world, tx - fxx * hd * u, ty + vd * u, tz - fzz * hd * u, 0.25) then return false, (i - 1) / 14 end
    end
    return true, 1
  end
  local pitch, best = Cam.pitch, 1
  for _, extra in ipairs({ 0, -0.15, -0.3, -0.45, -0.6 }) do
    local ok, k = clear(Cam.pitch + extra, 1)
    pitch = Cam.pitch + extra
    if ok then best = 1; break end
    best = k
  end
  Cam.pe = lerp(Cam.pe or Cam.pitch, pitch, 0.1)
  best = max(0.3, best)
  local hd, vd = Cam.dist * cos(Cam.pe), Cam.dist * sin(-Cam.pe)
  local gx, gy, gz = tx - fxx * hd * best, ty + vd * best, tz - fzz * hd * best
  Cam.x, Cam.y, Cam.z = lerp(Cam.x, gx, 0.3), lerp(Cam.y, gy, 0.3), lerp(Cam.z, gz, 0.3)
  Cam.tx, Cam.ty, Cam.tz = tx, ty, tz
end

local function look(x, y, z, tx, ty, tz)
  local dx, dy, dz = tx - x, ty - y, tz - z
  return atan(dx, dz), atan(dy, sqrt(dx * dx + dz * dz))
end

-- ---------------------------------------------------------------- the drawing
local function apply_environment()
  local amb = ({ 0.34, 0.44, 0.58 })[O.bright]
  sky3d(0x98A2C0, 0x6C7494, 0x3A3630)             -- a cold dusk, so the lamps are the warm things
  light3d(-0.45, 0.8, -0.35, amb)
  fog3d(SKY, 18, 60)
end

local function place_lamps()
  -- the four lamps or lanterns nearest to the camera light the street
  local near = {}
  local src = {}
  for _, o in ipairs(objs) do if o.name == "lamp" or o.name == "lantern" then src[#src + 1] = o end end
  table.sort(src, function(a, b)
    return (a.x - P.x) ^ 2 + (a.z - P.z) ^ 2 < (b.x - P.x) ^ 2 + (b.z - P.z) ^ 2
  end)
  lamp3d()
  for i = 1, min(3, #src) do
    local o = src[i]
    if (o.x - P.x) ^ 2 + (o.z - P.z) ^ 2 < 16 * 16 then
      lamp3d(i, o.x, o.y + (o.name == "lamp" and 2.7 or 1.5), o.z, 6.5, 0.55, o.name == "lamp" and 0xFFC060 or 0xFF9A40)
    end
  end
  if #fx > 0 then
    for _, f in ipairs(fx) do if f.kind == "flash" then lamp3d(4, f.x, f.y, f.z, 5, 1.5, 0xFFE0A0) end end
  end
end

local function draw_player()
  local m = P.form == 1 and M3.hunter_c or M3.hunter_o
  local k = 1
  if P.fade_from and P.fade_t < 1 then
    P.fade_t = min(1, P.fade_t + (1 / 60) / 0.18)
    P.fade_at = P.fade_at + (1 / 60)
    animate(m, P.fade_from, P.fade_at, P.anim, P.at, P.fade_t)
  else
    animate(m, P.anim, P.at)
  end
  local ry = P.yaw + PI                           -- the model faces -z
  if P.inv > 0 and (G.frame // 3) % 2 == 0 and P.mode ~= "dodge" then return end
  draw3d(m, P.x, P.y, P.z, 0, ry, 0, 1, 0)
  draw3d(m, P.x, P.y, P.z, 0, ry, 0, 1, 8)        -- the shadow
end

local function draw_enemy(e)
  if e.st == "dead" and e.t > 6 then return end
  local m = M3[e.kind]
  if not m or not visible3d(e.x, e.y + 1, e.z, 2.5) then return end
  animate(m, e.anim, e.at)
  local ry = e.yaw + PI                                    -- the models face -z
  draw3d(m, e.x, e.y, e.z, 0, ry, 0, 1, e.flash > 0 and 2 or 0)
  if e.st ~= "dead" then draw3d(m, e.x, e.y, e.z, 0, ry, 0, 1, 8) end
end

local function draw_objects()
  for _, o in ipairs(objs) do
    if not (o.door and o.open > 0.95) and visible3d(o.x, o.y + 1, o.z, 2.5) then
      local m = object_mesh(o.name)
      if m then
        local lift = 0
        if o.door then lift = o.open * 2.8 end
        draw3d(m, o.x, o.y + lift, o.z, 0, 0, 0, 1, 0)
      end
    end
  end
end

local function draw_effects()
  for _, f in ipairs(fx) do
    if f.kind == "spark" then point3d(f.x, f.y, f.z, 0.05, f.col)
    elseif f.kind == "tracer" then line3d(f.x, f.y, f.z, f.x2, f.y2, f.z2, 0xFFE8A0, 2) end
  end
end

-- ---------------------------------------------------------------- the HUD
local function S() return max(1, SCREEN_H // 360) end

local function bar(x, y, w, h, frac, col, back)
  rectfill(x, y, w, h, back or 0x201820)
  rectfill(x + 1, y + 1, max(0, floor((w - 2) * frac)), h - 2, col)
  rect(x, y, w, h, 0xB0A8A0)
end

local function text(s, x, y, c, k)
  k = k or S()
  print(s, x + k, y + k, 0x101018, k)                -- a shadow, so the text reads on the bright walls
  print(s, x, y, c or 0xF0E8D8, k)
end

local function draw_hud()
  local s = S()
  bar(8 * s, 8 * s, 120 * s, 8 * s, P.hp / P.maxhp, 0xC03030)
  text("BULLETS " .. P.ammo, 8 * s, 20 * s, 0xE0D8A0)
  text(P.form == 1 and "SAW CLOSED" or "SAW OPEN", 8 * s, 36 * s, 0xC8C8D0)
  if P.mode == "charge" and not P.released then bar(8 * s, 52 * s, 80 * s, 5 * s, min(1, P.charge / 1.2), 0xE0A040) end
  -- the enemies' bars and the lock
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" and (e.hp < e.maxhp or e == P.lock) then
      local sx, sy = project3d(e.x, e.y + e.ty.height + 0.2, e.z)
      if sx then bar(sx - 16 * s, sy - 4 * s, 32 * s, 4 * s, e.hp / e.maxhp, 0xC03030) end
    end
  end
  if P.lock then
    local sx, sy = project3d(P.lock.x, P.lock.y + P.lock.ty.height * 0.5, P.lock.z)
    if sx then
      local r = 14 * s
      line(sx - r, sy, sx, sy - r, 0xFFD060); line(sx, sy - r, sx + r, sy, 0xFFD060)
      line(sx + r, sy, sx, sy + r, 0xFFD060); line(sx, sy + r, sx - r, sy, 0xFFD060)
    end
  end
  if G.msgt > 0 then
    local w = #G.msg * 8 * s
    text(G.msg, (SCREEN_W - w) // 2, SCREEN_H - 40 * s, 0xF0E8D8)
  end
  if G.frame < 60 * 9 then
    text("WASD move  SPACE dodge  SHIFT saw  E gun  Q lock", 8 * s, SCREEN_H - 36 * s, 0xA8A098)
    text("U I O attacks  P charge (hold)  ESC menu  arrows: camera", 8 * s, SCREEN_H - 20 * s, 0xA8A098)
  end
  if P.flash > 0 then
    local a = P.flash / 0.35
    for i = 0, 3 do rect(i * s, i * s, SCREEN_W - 2 * i * s, SCREEN_H - 2 * i * s, 0x802020) end
  end
  if G.state == "dead" then
    rectfill(0, SCREEN_H // 2 - 30 * s, SCREEN_W, 60 * s, 0x180808)
    local t = "YOU DIED"
    text(t, (SCREEN_W - #t * 8 * s * 3) // 2, SCREEN_H // 2 - 16 * s, 0xC03030, s * 3)
  end
end

local MENU = { "Resume", "Resolution", "Textures", "Brightness", "Controls", "Restart at the lantern" }
local function menu_value(i)
  if i == 2 then return O.res == 1 and "1280x720" or "640x360" end
  if i == 3 then return O.textures and "on" or "off" end
  if i == 4 then return ({ "dim", "normal", "bright" })[O.bright] end
  if i == 5 then return O.help and "shown" or "hidden" end
  return ""
end

local function apply_resolution()
  if O.res == 1 then screen(1280, 720) else screen(640, 360) end
end

local function draw_menu()
  local s = S()
  rectfill(0, 0, SCREEN_W, SCREEN_H, 0x101018)
  local x, y = SCREEN_W // 2 - 140 * s, SCREEN_H // 2 - 70 * s
  text("DARK NIGHT", x, y - 30 * s, 0xE0C890, s * 2)
  for i, label in ipairs(MENU) do
    local c = i == G.menu and 0xFFE0A0 or 0xB8B0A8
    text((i == G.menu and "> " or "  ") .. label, x, y + (i - 1) * 18 * s, c)
    local v = menu_value(i)
    if v ~= "" then text(v, x + 190 * s, y + (i - 1) * 18 * s, c) end
  end
  if O.help then
    local hx, hy = 8 * s, SCREEN_H - 130 * s
    for k, line_ in ipairs({
      "W A S D    move (camera relative; locked: around the target)",
      "SPACE      dodge: back, or roll forward, or jump left / right",
      "SHIFT      open or close the saw",
      "E          fire the pistol        Q  lock on / off",
      "U I O      the three saw attacks (U then I then O: a combo)",
      "P          the charged attack: hold, then let go",
      "arrows     turn the camera, zoom       ESC  this menu",
    }) do text(line_, hx, hy + (k - 1) * 16 * s, 0xA8A098) end
  end
end

local function menu_update()
  if pressed.menu then G.state = "play"; return end
  if pressed.fwd or pressed.camU then G.menu = (G.menu - 2) % #MENU + 1 end
  if pressed.back or pressed.camD then G.menu = G.menu % #MENU + 1 end
  local act = pressed.ok or pressed.right or pressed.left
  if not act then return end
  local i = G.menu
  if i == 1 and pressed.ok then G.state = "play"
  elseif i == 2 then O.res = 3 - O.res; apply_resolution()
  elseif i == 3 then O.textures = not O.textures
  elseif i == 4 then O.bright = (O.bright + (pressed.left and -1 or 1) - 1) % 3 + 1
  elseif i == 5 then O.help = not O.help
  elseif i == 6 and pressed.ok then respawn(); G.state = "play" end
end

-- ---------------------------------------------------------------- the cartridge
function _init()
  screen(1280, 720)
  M3.hunter_c, M3.hunter_o = model("hunter_c"), model("hunter_o")
  for n in pairs(RADIUS) do
    M3[n] = model(n)
    M3[n .. "_f"] = model(n .. "_f")
  end
  for kind in pairs(TYPES) do M3[kind] = model(kind) end
  load_world()
  reset_player(G.spawn[1], G.spawn[2])
  Cam.x, Cam.y, Cam.z = P.x, P.y + 6, P.z
  say("The Clinic", 4)
  local n = { villager = 0, huntsman = 0, dog = 0 }
  for _, e in ipairs(enemies) do n[e.kind] = n[e.kind] + 1 end
  log("darknight: " .. #objs .. " objects, " .. #enemies .. " enemies (villager " .. n.villager .. ", huntsman " .. n.huntsman .. ", dog " .. n.dog .. ")")
end

function _update()
  G.frame = G.frame + 1
  local mx, my = read_input()
  if G.state == "play" then
    if pressed.menu then G.state = "menu"; return end
    if pressed.skip then
      G.skipn = (G.skipn or 0) % #SKIPS + 1
      local s = SKIPS[G.skipn]
      P.x, P.z = s[1], s[2]
      P.y = map_height(P.x, P.z) or P.y
      P.lock = nil
      ev("skip " .. G.skipn)
    end
    update_player(mx, my)
    update_enemies()
    update_world()
    update_camera()
  elseif G.state == "menu" then
    menu_update()
  elseif G.state == "dead" then
    G.died = G.died + 1 / 60
    update_enemies()
    update_camera()
    if G.died > 2.8 then respawn() end
  end
end

function _draw()
  cls(SKY)
  zclear()
  apply_environment()
  place_lamps()
  local yaw, pitch = look(Cam.x, Cam.y, Cam.z, Cam.tx or P.x, Cam.ty or P.y + 1.3, Cam.tz or P.z)
  camera3d(Cam.x, Cam.y, Cam.z, yaw, pitch, 60)
  textures3d(O.textures)
  map3d_draw(world, 0)
  draw_objects()
  for _, e in ipairs(enemies) do draw_enemy(e) end
  draw_player()
  draw_effects()
  if G.state == "menu" then draw_menu() else draw_hud() end
end
