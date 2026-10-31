-- Dark Night: the Hunter in the Clinic (carts/darknight).
-- Keyboard (rawkeys): WASD move; SPACE dodge (no direction: backstep, forward: roll, left or right: side jump);
-- SHIFT opens and closes the saw (during a combo: the transform attack); E the pistol; Q lock on; U the saw attack (press
-- again in the chain window: the next hit of the combo, four hits), I and O pick the second and third hit directly;
-- P the charged attack (hold); ESC the options menu (controls to remap, volume, quit); the arrows turn the camera. A pad has the same moves (see PAD below).
-- The map and the Hunter come from resource files merged into this cartridge: art/maps/clinic.bmk (the map kit) and
-- art/models/dnhunter.bmm (the Hunter with the saw in the right hand and the pistol in the left, in two forms).
-- The enemies come from art/models/dnenemy.bmm: villagers (the clinic), huntsmen and rabid dogs (the courtyard and the alley).
-- The sounds and the two songs are the cartridge's bank (carts/darknight/art/sounds.py): the SFX table names them.
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
  { -- closed: short and quick; the fourth hit a rising cut that ends the chain
    { clip = "saw_c1", frames = 16, rate = 1.35, hit = { 6, 9 }, reach = 1.9, dmg = 14, step = 0.55, chain = 9, sta = 16, sfx = "sawswing" },
    { clip = "saw_c2", frames = 15, rate = 1.35, hit = { 5, 8 }, reach = 1.9, dmg = 16, step = 0.55, chain = 8, sta = 16, sfx = "sawswing" },
    { clip = "saw_c3", frames = 20, rate = 1.3, hit = { 7, 10 }, reach = 2.0, dmg = 22, step = 0.7, chain = 12, sta = 20, sfx = "sawswing", stop = 4 },
    { clip = "saw_c4", frames = 18, rate = 1.3, hit = { 6, 9 }, reach = 2.0, dmg = 26, step = 0.8, chain = 99, sta = 24, sfx = "sawswing", stop = 5, up = 1 },
    charge = { clip = "saw_c_charge", frames = 26, rate = 1.3, hold = 9, resume = 11, hit = { 15, 19 }, reach = 2.2, dmg = 38, step = 1.0, sta = 30, stop = 7 },
  },
  { -- open: long and heavy; the fourth hit a full turn with the blade out
    { clip = "saw_o1", frames = 19, rate = 1.2, hit = { 7, 12 }, reach = 2.6, dmg = 20, step = 0.6, chain = 12, sta = 22, sfx = "bigswing" },
    { clip = "saw_o2", frames = 18, rate = 1.2, hit = { 6, 11 }, reach = 2.6, dmg = 22, step = 0.6, chain = 11, sta = 22, sfx = "bigswing" },
    { clip = "saw_o3", frames = 24, rate = 1.15, hit = { 8, 13 }, reach = 2.8, dmg = 30, step = 0.8, chain = 15, sta = 26, sfx = "bigswing", stop = 5 },
    { clip = "saw_o4", frames = 26, rate = 1.15, hit = { 8, 17 }, reach = 2.8, dmg = 34, step = 0.6, chain = 99, sta = 32, sfx = "bigswing", stop = 6, spin = true },
    charge = { clip = "saw_o_charge", frames = 33, rate = 1.15, hold = 12, resume = 16, hit = { 21, 26 }, reach = 3.0, dmg = 52, step = 1.2, sta = 36, stop = 8 },
  },
}
-- the transform attack: the saw changes form in the middle of a wide sweep (SHIFT in the chain window of a hit)
local TRANSFORM = { clip = "transform", frames = 16, rate = 1.25, swap = 6, hit = { 6, 10 }, dmg = { 24, 18 }, step = 0.6, chain = 11, sta = 20, stop = 5 }
local STAMINA = { max = 100, regen = 30, wait = 0.45, dodge = 18 }
-- the blade along the axis of hand.R, from its head: closed, the folded blade reaches back past the pommel; open, it is long
local SAW_SPAN = { { -0.38, 0.36 }, { 0.15, 1.18 } }
-- the sound effects of the bank, by name (the order of SFX in carts/darknight/art/sounds.py)
local SFX = { sawswing = 0, sawhit = 1, cleave = 2, sawopen = 3, sawclose = 4, pistol = 5, step = 6, hurt = 7, dodge = 8, lantern = 9,
              growl = 10, bite = 11, eswing = 12, edie = 13, door = 14, died = 15, menu = 16, charge = 17, bigswing = 18, headshot = 19 }
-- F9: the places of the walk, in order: the hall door, the atrium, the courtyard, the way to the gate
local SKIPS = { { 14.5, 8.5 }, { 37.5, 10.5 }, { 50.5, 12.5 }, { 66.5, 10.5 } }
local DODGE = {
  roll = { clip = "roll", frames = 16, rate = 1.6, dist = 3.0, inv = { 0.08, 0.6 } },
  back = { clip = "backstep", frames = 16, rate = 1.6, dist = 2.6, inv = { 0.04, 0.4 } },
  left = { clip = "dodge_l", frames = 12, rate = 1.5, dist = 2.2, inv = { 0.04, 0.45 } },
  right = { clip = "dodge_r", frames = 12, rate = 1.5, dist = 2.2, inv = { 0.04, 0.45 } },
}
-- the pistol has no bullets: each shot costs stamina and hurts little (it staggers more than it kills)
local GUN = { clip = "gun", frames = 14, rate = 1.4, shot = 5, dmg = 8, range = 16, sta = 22 }
-- the enemies: health, speed, the clip that moves them, how high and how wide they are (the body that is hit), the weapon (a bone, how
-- far along its axis, how thick: the hitbox that hurts the Hunter), how far they see, and their
-- attacks: the clip, when in it the blow lands (seconds), how long it lasts, the damage, the reach, how often it is chosen (percent).
-- The dog also has a leap from afar: it jumps at where the Hunter stood and bites on the landing.
local TYPES = {
  villager = { hp = 60, speed = 1.6, move = "walk", rate = 1.4, aggro = 7, height = 2.0, radius = 0.45, reach = 1.5, weapon = { "hand.R", 0.85, 0.30 }, attacks = {
    { clip = "attack1", strike = 0.58, total = 1.35, dmg = 14, reach = 1.5, pct = 60 },       -- a chop
    { clip = "attack2", strike = 0.70, total = 1.50, dmg = 16, reach = 1.9, pct = 40 } } },   -- a sweep, wider and slower
  huntsman = { hp = 90, speed = 1.3, move = "walk", rate = 1.3, aggro = 8, height = 2.4, radius = 0.5, reach = 2.0, weapon = { "hand.R", 1.05, 0.32 }, attacks = {
    { clip = "attack1", strike = 0.85, total = 1.75, dmg = 22, reach = 2.1, pct = 60 },      -- the axe, a long swing
    { clip = "attack2", strike = 0.92, total = 1.85, dmg = 30, reach = 2.3, pct = 40 } } },  -- an overhead chop
  dog = { hp = 40, speed = 3.5, move = "run", rate = 1.3, aggro = 10, height = 1.3, radius = 0.5, reach = 1.5, weapon = { "head", 0.5, 0.28 }, attacks = {
    { clip = "bite", strike = 0.42, total = 1.0, dmg = 10, reach = 1.5, pct = 100 } },
    leap = { clip = "leap", takeoff = 0.33, land = 1.0, fly0 = 0.42, fly1 = 1.08, total = 1.35, dmg = 14, reach = 1.7, far = 5.5, cool = 3.5 } },
}

-- ---------------------------------------------------------------- the state
local world, objs, doors, lanterns, enemies = nil, {}, {}, {}, {}
local M3 = {}                         -- the meshes: hunter_c, hunter_o, and the kit's by name (and name_f)
local P = {}                          -- the player
-- the camera: behind and a little above the Hunter, looking where he looks (a low pitch), a shoulder's width to the right
local Cam = { yaw = PI / 2, pitch = -0.36, dist = 5.4, side = 0.55, x = 0, y = 0, z = 0, shake = 0 }
local O = { res = 1, textures = true, bright = 2, music = true, sfx = 8, perf = false } -- the options (sfx: the effects volume
                                                                                        -- 0-10; perf: the dev kit's overlay)
local G = { frame = 0, state = "play", msg = "", msgt = 0, flash = 0, died = 0, menu = 1, spawn = { 0, 0 }, stop = 0, song = -1, calm = 0 }
local held, prev, pressed = {}, {}, {}
local fx = {}                         -- short effects: sparks and tracers
local SKY = 0x07080E                  -- the night: the fog and the clear colour

local function snd(name, semis, vol)
  local n = SFX[name]
  if n and O.sfx > 0 then sfx(n, nil, semis or 0, (vol or 1) * O.sfx / 10) end
end

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
  world = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25, wall_tile = 1.0 })
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
                                    flash = 0, anim = "idle", at = 0, idx = #enemies + 1, leapcd = 1.5 }
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
  P.hp, P.maxhp = 100, 100
  P.sta, P.stawait = STAMINA.max, 0
  P.form = 1                            -- 1 closed, 2 open
  P.mode, P.t = "free", 0
  P.anim, P.at, P.fade_from, P.fade_t = "idle", 0, nil, 1
  P.combo, P.queued, P.inv = 0, nil, 0
  P.lock = nil
  P.swing = {}
  P.charge = 0
  P.flash = 0
  P.stepd = 0
end

-- stamina: an action spends it; it comes back after a short wait. An action starts with any stamina left (it may go under 0)
local function spend(n)
  P.sta = P.sta - n
  P.stawait = STAMINA.wait
end

local function can_act()
  return P.sta > 0
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
  P.queued = nil
  spend(STAMINA.dodge)
  ev("dodge " .. kind)
  snd("dodge", kind == "roll" and -2 or 0)
  set_anim(d.clip, 0)
  P.at = 0
end

local function face_lock()
  if P.lock then P.yaw = yaw_of(P.lock.x - P.x, P.lock.z - P.z) end
end

local function start_attack(n)
  local set = ATK[P.form]
  local a = set[n]
  P.mode, P.t, P.atk, P.combo = "attack", 0, a, n
  ev("attack " .. (P.form == 1 and "closed " or "open ") .. n)
  P.swing = {}
  P.queued = nil
  P.swung = false
  spend(a.sta)
  set_anim(a.clip, 0)
  P.at = 0
  face_lock()
end

-- SHIFT: in a combo, the transform attack (the blade changes form inside the sweep and hurts); standing, only the change
local function start_transform(attacking)
  P.mode, P.t, P.atk = "transform", 0, TRANSFORM
  P.swing, P.swapped, P.swung, P.queued = {}, false, false, nil
  P.tattack = attacking
  if attacking then spend(TRANSFORM.sta); ev("transform attack") end
  set_anim(TRANSFORM.clip, 0)
  P.at = 0
  face_lock()
end

local function start_charge()
  local a = ATK[P.form].charge
  P.mode, P.t, P.atk, P.charge, P.released = "charge", 0, a, 0, false
  ev("charge " .. (P.form == 1 and "closed" or "open"))
  P.swing = {}
  P.swung = false
  spend(a.sta)
  snd("charge")
  set_anim(a.clip, 0)
  P.at = 0
end

local function start_gun()
  if not can_act() then return end
  spend(GUN.sta)
  P.mode, P.t, P.shot = "gun", 0, false
  P.queued = nil
  set_anim(GUN.clip, 0)
  P.at = 0
  face_lock()
end

local function hurt(dmg, from_x, from_z)
  if P.inv > 0 or P.hp <= 0 then return end
  P.hp = max(0, P.hp - dmg)
  P.flash = 0.35
  P.inv = 0.5
  Cam.shake = max(Cam.shake, 0.12)
  local dx, dz = P.x - from_x, P.z - from_z
  local l = sqrt(dx * dx + dz * dz) + 1e-6
  try_move(P, dx / l * 0.5, dz / l * 0.5, 0.32)
  ev("hurt hp " .. P.hp)
  snd("hurt", math.random(-2, 2))
  if P.hp <= 0 then
    G.state, G.died = "dead", 0
    P.mode = "dead"
    set_anim("death", 0); P.at = 0
    ev("died")
    snd("died")
    if O.music then music(-1, 400); G.song = -1 end
  elseif P.mode == "free" or P.mode == "toggle" then
    P.mode, P.t = "hurt", 0                        -- a flinch; an attack or a dodge goes on (the blow is taken in the move)
    set_anim("hurt", 0); P.at = 0
  end
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
  spark(e.x, e.y + 1.2, e.z, kind == "gun" and 0xFFE080 or 0xB02020, kind == "gun" and 8 or 12)
  if e.hp <= 0 then e.st, e.t = "dead", 0; ev("enemy dead " .. kind); snd("edie", e.kind == "dog" and 7 or 0); if P.lock == e then P.lock = nil end
  else
    local busy = (e.st == "attack" and e.t >= e.atk.strike) or (e.st == "leap" and e.t >= e.ty.leap.fly0)
    if not busy then e.st, e.t, e.struck = "stagger", 0, false end
  end
end

-- A weapon is a hitbox on a bone: points along the axis of the bone (from its head, for `len`) in the pose the model has now, taken
-- to the world with the turn the model is drawn with (draw3d turns by yaw + PI about y; the model looks toward -z).
local function to_world(ox, oy, oz, yaw, x, y, z)
  local c, s = cos(yaw), sin(yaw)
  return ox - x * c - z * s, oy + y, oz + x * s - z * c
end

local function weapon_points(m, bone, len, ox, oy, oz, yaw, from)
  local hx, hy, hz, cx, cy, cz = bone3d(m, bone)
  local pts = {}
  if not hx then return pts end
  local dx, dy, dz = cx - hx, cy - hy, cz - hz
  local l = sqrt(dx * dx + dy * dy + dz * dz) + 1e-6
  dx, dy, dz = dx / l, dy / l, dz / l
  from = from or 0
  for i = 0, 6 do
    local s = from + (len - from) * i / 6
    local wx, wy, wz = to_world(ox, oy, oz, yaw, hx + dx * s, hy + dy * s, hz + dz * s)
    pts[#pts + 1] = { wx, wy, wz }
  end
  return pts
end

-- does a point of the weapon touch a body (an upright cylinder at x, z from y0 to y1)?
local function touches(pts, x, z, y0, y1, r)
  for _, p in ipairs(pts) do
    local dx, dz = p[1] - x, p[3] - z
    if dx * dx + dz * dz <= r * r and p[2] >= y0 and p[2] <= y1 then return true end
  end
  return false
end

local function melee_hits(a)
  local m = P.form == 1 and M3.hunter_c or M3.hunter_o
  animate(m, P.anim, P.at)
  local span = SAW_SPAN[P.form]
  local pts = weapon_points(m, "hand.R", span[2], P.x, P.y, P.z, P.yaw, span[1])
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" and not P.swing[e] and touches(pts, e.x, e.z, e.y - 0.1, e.y + e.ty.height + 0.1, e.ty.radius + 0.45) then
      P.swing[e] = true
      ev("hit " .. e.kind .. " saw")
      damage_enemy(e, a.dmg, "saw")
      -- the hit stops time for a few frames (the blow is felt) and shakes the camera
      G.stop = max(G.stop, a.stop or 3)
      Cam.shake = max(Cam.shake, (a.stop or 3) * 0.018)
      snd(a.cleave and "cleave" or "sawhit", math.random(-3, 2))
    end
  end
end

-- does the weapon of an enemy touch the Hunter (a cylinder 0.35 wide and 1.9 high)?
local function enemy_weapon_hits_player(e)
  local w = e.ty.weapon
  local m = M3[e.kind]
  animate(m, e.anim, e.at)
  local pts = weapon_points(m, w[1], w[2], e.x, e.y, e.z, e.yaw)
  return touches(pts, P.x, P.z, P.y, P.y + 1.9, 0.35 + w[3])
end

-- Where a bullet hurts: the bones of the enemy in the pose it has now (hit3d: each bone is a capsule from its head to its tail,
-- as wide as its vertices). The head counts double, a limb a little more than half.
local LIMB = { "hand", "fore", "upper", "fing", "thumb", "thigh", "shin", "foot", "paw", "tail", "fpaw", "ffore", "fupper", "hthigh", "hshin", "hfoot" }
local function zone_of(bone)
  bone = tostring(bone or "")
  if bone:find("^head") or bone:find("^jaw") then return "head", 2.0 end
  for _, n in ipairs(LIMB) do if bone:find("^" .. n) then return "limb", 0.6 end end
  return "body", 1.0
end

local function fire_bullet()
  local fxx, fzz = dir(P.yaw)
  local sx, sy, sz = P.x + fxx * 0.6, P.y + 1.35, P.z + fzz * 0.6
  -- the enemies the bullet may meet: the one locked on, or those in front of the Hunter (a narrow cone: the aim is a little forgiving)
  local cand = {}
  if P.lock and P.lock.st ~= "dead" then cand[1] = P.lock
  else
    for _, e in ipairs(enemies) do
      if e.st ~= "dead" then
        local dx, dz = e.x - P.x, e.z - P.z
        local d = sqrt(dx * dx + dz * dz)
        if d > 0.2 and d < GUN.range and (dx * fxx + dz * fzz) / d > 0.96 then cand[#cand + 1] = e end
      end
    end
  end
  local best, bt, bdir, bbone
  for _, e in ipairs(cand) do
    local m = M3[e.kind]
    animate(m, e.anim, e.at)                                  -- the pose the enemy has now (the mesh is shared by its kind)
    local ax, ay, az = e.x, e.y + e.ty.height * 0.55, e.z
    local dx, dy, dz = ax - sx, ay - sy, az - sz
    local l = sqrt(dx * dx + dy * dy + dz * dz) + 1e-6
    dx, dy, dz = dx / l, dy / l, dz / l
    local t, bone = hit3d(m, e.x, e.y, e.z, e.yaw + PI, 1, sx, sy, sz, dx, dy, dz, GUN.range)
    if t and (not bt or t < bt) then best, bt, bdir, bbone = e, t, { dx, dy, dz }, bone end
  end
  local ddx, ddy, ddz = fxx, 0, fzz
  if bdir then ddx, ddy, ddz = bdir[1], bdir[2], bdir[3] end
  -- the bullet stops at a wall
  local wall = GUN.range
  for t = 0.6, GUN.range, 0.4 do
    if map3d_blocked(world, sx + ddx * t, sy + ddy * t, sz + ddz * t, 0.08) then wall = t; break end
  end
  local hit = best and bt < wall
  local tl = hit and bt or wall
  local zone, mult = "body", 1.0
  if hit then zone, mult = zone_of(bbone) end
  fx[#fx + 1] = { kind = "tracer", x = sx, y = sy, z = sz, x2 = sx + ddx * tl, y2 = sy + ddy * tl, z2 = sz + ddz * tl, t = 0.08 }
  fx[#fx + 1] = { kind = "flash", x = sx, y = sy, z = sz, t = 0.08 }
  snd("pistol")
  Cam.shake = max(Cam.shake, 0.08)
  if hit then
    damage_enemy(best, GUN.dmg * mult, "gun")
    if zone == "head" then say("headshot", 1.0); snd("headshot") end
  else
    spark(sx + ddx * tl, sy + ddy * tl, sz + ddz * tl, 0xC0C0C0, 4)
  end
  ev("shot " .. (hit and ("hit " .. zone .. " " .. tostring(bbone)) or "miss"))
end

-- can the Hunter see the enemy? A wall between them (at chest height, above the steps of a stair) says no
local function in_sight(e)
  local y = max(P.y, e.y) + 1.4
  local dx, dz = e.x - P.x, e.z - P.z
  local d = sqrt(dx * dx + dz * dz)
  if d < 0.6 then return true end
  for s = 0.5, d - 0.4, 0.5 do
    if map3d_blocked(world, P.x + dx / d * s, y, P.z + dz / d * s, 0.1) then return false end
  end
  return true
end

local function pick_lock()
  local best, bd = nil, 15
  local fxx, fzz = dir(Cam.yaw)
  for _, e in ipairs(enemies) do
    if e.st ~= "dead" then
      local dx, dz = e.x - P.x, e.z - P.z
      local d = sqrt(dx * dx + dz * dz)
      local front = (dx * fxx + dz * fzz) / (d + 1e-6)
      if d < bd and front > -0.2 and in_sight(e) then best, bd = e, d end
    end
  end
  return best
end

-- the next move, pressed during a hit: it starts in the chain window (an input buffer)
local function buffer_input()
  if pressed.atk1 then P.queued = "next" end
  if pressed.atk2 then P.queued = 2 end
  if pressed.atk3 then P.queued = 3 end
  if pressed.saw then P.queued = "transform" end
  if pressed.dodge then P.queued = "dodge" end
end

local function run_queued(mx, my, wx, wz)
  local q = P.queued
  if not q then return false end
  if q == "dodge" then
    if can_act() then start_dodge(mx, my, wx, wz) else P.queued = nil end
    return true
  end
  if not can_act() then P.queued = nil; say("out of breath", 0.8); return false end
  if q == "transform" then start_transform(true)
  elseif q == "next" then start_attack(min(4, P.combo + 1))
  else start_attack(q) end
  return true
end

local function update_player(mx, my)
  local dt = 1 / 60
  P.t = P.t + dt
  P.inv = max(0, P.inv - dt)
  P.flash = max(0, P.flash - dt)
  -- stamina comes back after a short wait, slower while the Hunter runs
  P.stawait = max(0, P.stawait - dt)
  if P.stawait <= 0 and P.sta < STAMINA.max then P.sta = min(STAMINA.max, P.sta + STAMINA.regen * dt * (P.mode == "free" and 1 or 0.4)) end
  -- the camera's forward gives the meaning of W
  local cfx, cfz = dir(Cam.yaw)
  local crx, crz = cfz, -cfx                       -- the camera's right (the Hunter's right hand when he looks away from it)
  local wx, wz = cfx * my + crx * mx, cfz * my + crz * mx
  local moving = (wx ~= 0 or wz ~= 0)

  if pressed.lock then
    if P.lock then P.lock = nil; ev("lock off") else P.lock = pick_lock(); if P.lock then ev("lock on") else say("nothing to lock on", 1.2) end end
  end
  if P.lock then
    -- the lock goes when the enemy dies, is far, or has been out of sight for a second and a half
    P.unseen = in_sight(P.lock) and 0 or (P.unseen or 0) + dt
    if P.lock.st == "dead" or ((P.lock.x - P.x) ^ 2 + (P.lock.z - P.z) ^ 2) > 22 * 22 or P.unseen > 1.5 then P.lock = nil; P.unseen = 0; ev("lock lost") end
  end

  if P.mode == "free" then
    local speed, clip, rate = 0, "idle", 1
    if moving then
      if P.lock then speed, clip, rate = 1.7, "walk", 1.8 else speed, clip, rate = 3.4, "run", 1.25 end
      local l = sqrt(wx * wx + wz * wz)
      local ox, oz = P.x, P.z
      try_move(P, wx / l * speed * dt * min(1, l), wz / l * speed * dt * min(1, l), 0.32)
      if not P.lock then P.yaw = turn_to(P.yaw, yaw_of(wx, wz), 12 * dt) end
      -- a step every stride: a boot on the stone
      P.stepd = P.stepd + sqrt((P.x - ox) ^ 2 + (P.z - oz) ^ 2)
      if P.stepd > (P.lock and 0.75 or 1.05) then P.stepd = 0; snd("step", math.random(-3, 3), 0.8) end
    else
      P.stepd = 0.6
    end
    if P.lock then P.yaw = turn_to(P.yaw, yaw_of(P.lock.x - P.x, P.lock.z - P.z), 14 * dt) end
    set_anim(clip)
    P.at = P.at + dt * rate
    P.combo = 0
    if pressed.dodge and can_act() then start_dodge(mx, my, wx, wz)
    elseif pressed.saw then start_transform(false)
    elseif pressed.gun then start_gun()
    elseif pressed.charge and can_act() then start_charge()
    elseif pressed.atk1 and can_act() then start_attack(1)
    elseif pressed.atk2 and can_act() then start_attack(2)
    elseif pressed.atk3 and can_act() then start_attack(3)
    elseif (pressed.dodge or pressed.charge or pressed.atk1 or pressed.atk2 or pressed.atk3) then say("out of breath", 0.8) end

  elseif P.mode == "hurt" then
    P.at = P.at + dt * 1.4
    buffer_input()
    if P.t > 0.32 then P.mode = "free"; if not run_queued(mx, my, wx, wz) then P.queued = nil end end

  elseif P.mode == "transform" then
    local a = P.atk
    P.at = P.at + dt * (P.tattack and a.rate or 1.7)
    local f = P.at * FPS
    if f >= a.swap and not P.swapped then
      P.swapped = true
      P.form = 3 - P.form
      ev("saw " .. (P.form == 1 and "closed" or "open"))
      snd(P.form == 1 and "sawclose" or "sawopen")
      G.swapt = G.frame                            -- the HUD icon flashes
      spark(P.x + dir(P.yaw) * 0.5, P.y + 1.3, P.z, 0xC8C8D0, 8)
    end
    if P.tattack then
      if f >= a.hit[1] and not P.swung then P.swung = true; snd("bigswing", 3) end
      if f >= a.hit[1] and f <= a.hit[2] then
        melee_hits({ dmg = a.dmg[P.form], stop = a.stop })
        local fxx, fzz = dir(P.yaw)
        try_move(P, fxx * a.step * dt * 3, fzz * a.step * dt * 3, 0.32)
      end
      buffer_input()
      if P.queued and f >= a.chain and run_queued(mx, my, wx, wz) then P.combo = 0
      elseif f >= a.frames - 1 then P.mode, P.combo = "free", 0 end
    else
      if f >= 9 then P.mode = "free" end
    end

  elseif P.mode == "attack" then
    local a = P.atk
    local rate = a.rate
    P.at = P.at + dt * rate
    local f = P.at * FPS
    if f >= a.hit[1] - 1.5 and not P.swung then P.swung = true; snd(a.sfx, a.up and 4 or math.random(-2, 2)) end
    if f >= a.hit[1] and f <= a.hit[2] then
      melee_hits(a)
      local fxx, fzz = dir(P.yaw)
      local s = a.step * dt * rate * FPS / max(1, (a.hit[2] - a.hit[1] + 1))
      try_move(P, fxx * s, fzz * s, 0.32)
    end
    -- the next move, pressed during the cut or the recovery, starts in the chain window; a dodge may cut the recovery
    buffer_input()
    if P.queued == "dodge" and f >= a.hit[2] then run_queued(mx, my, wx, wz)
    elseif P.queued and P.queued ~= "dodge" and f >= a.chain then run_queued(mx, my, wx, wz)
    elseif f >= a.frames - 1 then P.mode, P.combo, P.queued = "free", 0, nil end

  elseif P.mode == "charge" then
    local a = P.atk
    local f = P.at * FPS
    if not P.released then
      P.charge = P.charge + dt
      -- wind up to the hold pose, then wait there while P is held
      if f < a.hold then P.at = P.at + dt * a.rate else P.at = (a.hold + 0.4 * math.sin(P.charge * 14)) / FPS end
      if not held.charge then P.released = true; P.at = max(P.at, a.hold / FPS); P.at = max(P.at, a.resume / FPS - 0.01); snd("bigswing", -4) end
    else
      P.at = P.at + dt * a.rate
      local ff = P.at * FPS
      if ff >= a.hit[1] and ff <= a.hit[2] then
        local boost = min(1.8, 1 + P.charge * 0.5)
        local aa = { reach = a.reach, dmg = a.dmg * boost, stop = a.stop, cleave = true }
        melee_hits(aa)
        local fxx, fzz = dir(P.yaw)
        try_move(P, fxx * a.step * dt, fzz * a.step * dt, 0.32)
      end
      buffer_input()
      if P.queued == "dodge" and ff >= a.hit[2] then run_queued(mx, my, wx, wz)
      elseif ff >= a.frames - 1 then P.mode, P.queued = "free", nil end
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
    buffer_input()
    if P.queued == "dodge" then P.queued = nil end
    if u >= 0.78 and P.queued and run_queued(mx, my, wx, wz) then
    elseif u >= 1 then P.mode = "free"; P.queued = nil end

  elseif P.mode == "dead" then
    P.at = min(P.at + dt, 21 / FPS)
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
    e.leapcd = max(0, e.leapcd - dt)
    local dx, dz = P.x - e.x, P.z - e.z
    local d = sqrt(dx * dx + dz * dz)
    local clip, rate = "idle", 1.0
    if e.st == "idle" then
      if d < ty.aggro and P.hp > 0 then e.st, e.t = "chase", 0; if e.kind == "dog" then snd("growl", math.random(-2, 2)) end end
    elseif e.st == "chase" then
      e.yaw = turn_to(e.yaw, yaw_of(dx, dz), 8 * dt)
      clip, rate = ty.move, ty.rate
      local lp = ty.leap
      if lp and e.leapcd <= 0 and d > 2.6 and d < 6.5 and P.hp > 0 then
        e.st, e.t, e.struck, e.leaping = "leap", 0, false, nil
        ev("enemy " .. e.kind .. " leap")
        snd("growl", 4)
      elseif d > ty.reach * 0.7 then
        try_move(e, dx / d * ty.speed * dt, dz / d * ty.speed * dt, 0.35)
      else
        -- the attack by its weight; the choice follows the frame, so a run of the game is the same every time
        local roll, pick = (G.frame * 7 + e.idx * 13) % 100, ty.attacks[1]
        local acc = 0
        for _, a in ipairs(ty.attacks) do acc = acc + a.pct; if roll < acc then pick = a; break end end
        e.st, e.t, e.struck, e.atk, e.swung = "attack", 0, false, pick, false
        ev("enemy " .. e.kind .. " " .. pick.clip)
      end
      if d > ty.aggro * 2.2 then e.st = "idle" end
    elseif e.st == "attack" then
      clip, rate = e.atk.clip, 1.0
      if e.t < e.atk.strike then e.yaw = turn_to(e.yaw, yaw_of(dx, dz), 6 * dt) end
      if e.t >= e.atk.strike - 0.18 and not e.swung then e.swung = true; snd(e.kind == "dog" and "bite" or "eswing", e.kind == "huntsman" and -3 or 0) end
      -- the blow lands when its weapon touches the Hunter in the window round the hit time of the clip; a Hunter who has moved is missed
      if e.t >= e.atk.strike - 0.08 and e.t <= e.atk.strike + 0.22 and not e.struck and enemy_weapon_hits_player(e) then
        e.struck = true
        hurt(e.atk.dmg, e.x, e.z)
      end
      if e.t > e.atk.total then e.st, e.t = "chase", 0 end
    elseif e.st == "leap" then
      local lp = ty.leap
      clip, rate = lp.clip, 1.0
      if e.t < lp.takeoff then
        e.yaw = turn_to(e.yaw, yaw_of(dx, dz), 10 * dt)
      elseif not e.leaping then                        -- the take off: where the Hunter stands now, at most `far` away
        local dist = min(d, lp.far)
        e.leaping = { sx = e.x, sz = e.z, tx = e.x + dx / (d + 1e-6) * dist, tz = e.z + dz / (d + 1e-6) * dist }
      end
      if e.leaping and e.t >= lp.fly0 and e.t <= lp.fly1 then
        local u = (e.t - lp.fly0) / (lp.fly1 - lp.fly0)
        local s = u * u * (3 - 2 * u)
        local L = e.leaping
        try_move(e, lerp(L.sx, L.tx, s) - e.x, lerp(L.sz, L.tz, s) - e.z, 0.35)
      end
      if e.t >= lp.land and not e.struck then
        e.struck = true
        snd("bite")
        if d < lp.reach then hurt(lp.dmg, e.x, e.z) end
      end
      if e.t > lp.total then e.st, e.t, e.leapcd = "chase", 0, lp.cool end
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
        if d.open == 0 then ev("door opens"); snd("door") end
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
      P.hp = P.maxhp
      say("the lantern is lit: health restored", 3)
      ev("lantern")
      snd("lantern")
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
  -- the music: the clinic's theme, and the hunt while an enemy is after the Hunter (back to the theme after a calm)
  local fight = false
  for _, e in ipairs(enemies) do
    if (e.st == "chase" or e.st == "attack" or e.st == "leap" or e.st == "stagger") and (e.x - P.x) ^ 2 + (e.z - P.z) ^ 2 < 14 * 14 then fight = true end
  end
  if fight then G.calm = 0 else G.calm = G.calm + dt end
  local want = (fight or G.calm < 5) and 1 or 0
  if not O.music then want = -1 end
  if want ~= G.song and P.hp > 0 then
    G.song = want
    if want < 0 then music(-1, 600) else music(want, want == 1 and 300 or 1500) end
  end
end

local function respawn()
  reset_player(G.spawn[1], G.spawn[2])
  Cam.yaw = P.yaw
  for _, e in ipairs(enemies) do
    e.x, e.z, e.hp, e.st, e.t, e.flash, e.anim, e.at = e.hx, e.hz, e.maxhp, "idle", 0, 0, "idle", 0
    e.leapcd, e.leaping = 1.5, nil
  end
  G.state = "play"
  G.song, G.calm = -1, 10
  ev("respawn")
end

-- ---------------------------------------------------------------- the camera
-- the walls and the door leaves (open or shut: an open leaf stands in the doorway's side) stop the camera
local function cam_blocked(x, y, z)
  if map3d_blocked(world, x, y, z, 0.18) then return true end
  for i = 1, #objs do
    local o = objs[i]
    if o.door and (x - o.x) ^ 2 + (z - o.z) ^ 2 < (o.r + 0.3) ^ 2 then return true end
  end
  return false
end

local function update_camera()
  local dt = 1 / 60
  if held.camL then Cam.yaw = Cam.yaw - 1.8 * dt end
  if held.camR then Cam.yaw = Cam.yaw + 1.8 * dt end
  if held.camU then Cam.dist = max(3.2, Cam.dist - 4 * dt) end
  if held.camD then Cam.dist = min(9, Cam.dist + 4 * dt) end
  if P.lock then
    -- behind the player, looking at the target
    local want = yaw_of(P.lock.x - P.x, P.lock.z - P.z)
    Cam.yaw = Cam.yaw + wrap(want - Cam.yaw) * 0.08
  elseif P.mode == "free" and (held.fwd or held.back or held.left or held.right) then
    -- the camera drifts round behind the Hunter as he runs
    Cam.yaw = Cam.yaw + wrap(P.yaw - Cam.yaw) * 0.012
  end
  local fxx, fzz = dir(Cam.yaw)
  local crx, crz = fzz, -fxx
  -- the look point: the Hunter's chest, a shoulder's width to the right of him (an over-the-shoulder view); when locked on,
  -- a little toward the target
  -- next to a wall the shoulder offset shrinks, so the look point stays out of the wall
  local side = Cam.side
  while side > 0 and map3d_blocked(world, P.x + crx * side, P.y + 1.45, P.z + crz * side, 0.25) do side = side - 0.15 end
  side = max(0, side)
  Cam.so = lerp(Cam.so or side, side, 0.15)
  local tx, ty, tz = P.x + crx * Cam.so, P.y + 1.45, P.z + crz * Cam.so
  if P.lock then tx, ty, tz = lerp(tx, P.lock.x, 0.18), lerp(ty, P.lock.y + 1.0, 0.18), lerp(tz, P.lock.z, 0.18) end
  -- a wall in the way: first look down more steeply (the camera goes up, not in); only then come closer
  local function clear(pitch, k)
    local hd, vd = Cam.dist * cos(pitch), Cam.dist * sin(-pitch)
    for i = 1, 14 do
      local u = i / 14 * k
      if cam_blocked(tx - fxx * hd * u, ty + vd * u, tz - fzz * hd * u) then return false, (i - 1) / 14 end
    end
    return true, 1
  end
  local pitch, best = Cam.pitch, 1
  for _, extra in ipairs({ 0, -0.12, -0.25 }) do
    local ok, k = clear(Cam.pitch + extra, 1)
    pitch = Cam.pitch + extra
    if ok then best = 1; break end
    best = k
  end
  Cam.pe = lerp(Cam.pe or Cam.pitch, pitch, 0.1)
  best = max(0.35, best)
  local hd, vd = Cam.dist * cos(Cam.pe), Cam.dist * sin(-Cam.pe)
  local gx, gy, gz = tx - fxx * hd * best, ty + vd * best, tz - fzz * hd * best
  -- the ground: the camera never goes under the floor it stands over
  local fh = map_height(gx, gz)
  if fh and gy < fh + 0.5 then gy = fh + 0.5 end
  Cam.x, Cam.y, Cam.z = lerp(Cam.x, gx, 0.25), lerp(Cam.y, gy, 0.25), lerp(Cam.z, gz, 0.25)
  -- a hit shakes the view
  Cam.shake = max(0, Cam.shake - dt * 0.6)
  local sh = Cam.shake
  Cam.tx, Cam.ty, Cam.tz = tx + (math.random() - 0.5) * sh, ty + (math.random() - 0.5) * sh, tz + (math.random() - 0.5) * sh
end

local function look(x, y, z, tx, ty, tz)
  local dx, dy, dz = tx - x, ty - y, tz - z
  return atan(dx, dz), atan(dy, sqrt(dx * dx + dz * dz))
end

-- ---------------------------------------------------------------- the drawing
-- The night: a pale moon high up (the shadows fall straight under the figures, all the same way), a cold sky, a black
-- ground, a thick fog close by. The lamps and lanterns are the warm light.
local function apply_environment()
  local amb = ({ 0.05, 0.09, 0.15 })[O.bright]
  sky3d(0x6E7290, 0x23283E, 0x08080C)
  light3d(-0.16, 1.0, -0.12, amb)
  fog3d(SKY, 5, 22)
  shadow3d(0)
end

local function place_lamps()
  -- the three lamps or lanterns nearest to the Hunter light the street; the fourth lamp is the pistol's flash
  local src = {}
  for _, o in ipairs(objs) do if o.name == "lamp" or o.name == "lantern" then src[#src + 1] = o end end
  table.sort(src, function(a, b)
    return (a.x - P.x) ^ 2 + (a.z - P.z) ^ 2 < (b.x - P.x) ^ 2 + (b.z - P.z) ^ 2
  end)
  lamp3d()
  for i = 1, min(3, #src) do
    local o = src[i]
    if (o.x - P.x) ^ 2 + (o.z - P.z) ^ 2 < 20 * 20 then
      local flick = 0.86 + 0.14 * sin(G.frame * 0.37 + o.x * 3.1) * sin(G.frame * 0.11 + o.z)
      lamp3d(i, o.x, o.y + (o.name == "lamp" and 2.7 or 1.5), o.z, o.name == "lamp" and 5.5 or 4.2, 0.8 * flick,
             o.name == "lamp" and 0xFFB850 or 0xFF8A30)
    end
  end
  for _, f in ipairs(fx) do if f.kind == "flash" then lamp3d(4, f.x, f.y, f.z, 6, 1.6, 0xFFE0A0) end end
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
  if P.inv > 0 and (G.frame // 3) % 2 == 0 and P.mode ~= "dodge" and P.mode ~= "dead" then return end
  draw3d(m, P.x, P.y, P.z, 0, ry, 0, 1, 0)
  draw3d(m, P.x, map_height(P.x, P.z) or P.y, P.z, 0, ry, 0, 1, 8)        -- the shadow, on the floor under him
end

local function draw_enemy(e)
  if e.st == "dead" and e.t > 6 then return end
  local m = M3[e.kind]
  if not m or not visible3d(e.x, e.y + 1, e.z, 2.5) then return end
  animate(m, e.anim, e.at)
  local ry = e.yaw + PI                                    -- the models face -z
  draw3d(m, e.x, e.y, e.z, 0, ry, 0, 1, e.flash > 0 and 2 or 0)
  if e.st ~= "dead" or e.t < 1.2 then draw3d(m, e.x, e.y, e.z, 0, ry, 0, 1, 8) end
end

local function draw_objects()
  for _, o in ipairs(objs) do
    if not (o.door and o.open > 0.95) and visible3d(o.x, o.y + 1, o.z, 2.5) then
      local m = object_mesh(o.name)
      if m then
        local lift = 0
        if o.door then lift = o.open * 2.8 end
        draw3d(m, o.x, o.y + lift, o.z, 0, 0, 0, 1, 0)
        if o.name == "lamp" or o.name == "pillar" or o.name == "lantern" or o.name == "barrel" or o.name == "crate" then
          draw3d(m, o.x, o.y, o.z, 0, 0, 0, 1, 8)
        end
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

-- the saw in the HUD: a silhouette of its form (1 closed: the blade folded back over the handle; 2 open: the blade beyond
-- the hinge), a pixel of the picture is `k` pixels of the screen
local SAW_ICON = {
  { "....#.#.#.#.#.#.#.#.#.....",
    "...##################.....",
    "..####################....",
    "..######################..",
    "####.................###..",
    "###...................#...", },
  { "................#.#.#.#.#.#.#.#.#.#.#.#.",
    "...............########################.",
    "..............#########################.",
    "..####################################..",
    "####..........###.......................",
    "###.....................................", },
}
local function saw_icon(x, y, k, form, lit)
  for pass = 1, 2 do
    local o, c = pass == 1 and k // 2 or 0, pass == 1 and 0x050508 or (lit and 0xFFF0C8 or 0xC8C4BC)
    for j, row in ipairs(SAW_ICON[form]) do
      for i = 1, #row do
        if row:byte(i) == 35 then rectfill(x + (i - 1) * k + o, y + (j - 1) * k + o, k, k, c) end
      end
    end
  end
end

local function draw_hud()
  local s = S()
  bar(8 * s, 8 * s, 120 * s, 7 * s, P.hp / P.maxhp, 0xA82828)
  bar(8 * s, 17 * s, 100 * s, 5 * s, max(0, P.sta) / STAMINA.max, P.sta > 0 and 0x5A9A48 or 0x806030)
  saw_icon(8 * s, 28 * s, 2 * s, P.form, G.frame - (G.swapt or -99) < 18)
  if P.mode == "charge" and not P.released then bar(8 * s, 46 * s, 80 * s, 5 * s, min(1, P.charge / 1.2), 0xE0A040) end
  if P.mode == "attack" and P.combo > 0 then text(string.rep("|", P.combo), 8 * s, 46 * s, 0xE0C890) end
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
    text(G.msg, (SCREEN_W - w) // 2, SCREEN_H // 4, 0xF0E8D8)
  end
  if P.flash > 0 then
    local a = P.flash / 0.35
    for i = 0, 3 do rect(i * s, i * s, SCREEN_W - 2 * i * s, SCREEN_H - 2 * i * s, 0x802020) end
  end
  if G.state == "dead" and G.died > 0.9 then
    rectfill(0, SCREEN_H // 2 - 30 * s, SCREEN_W, 60 * s, 0x180808)
    local t = "YOU DIED"
    text(t, (SCREEN_W - #t * 8 * s * 3) // 2, SCREEN_H // 2 - 16 * s, 0xC03030, s * 3)
  end
end

-- ---------------------------------------------------------------- the options menu (ESC)
-- The menu reads its own fixed keys (arrows or W S, ENTER or SPACE, ESC), so a remapped key never locks the player out.
local MK = { up = { { 0x52, 0x1A }, "up" }, down = { { 0x51, 0x16 }, "down" }, left = { { 0x50, 0x04 }, "left" },
             right = { { 0x4F, 0x07 }, "right" }, ok = { { 0x28, 0x2C }, "ok" }, back = { { 0x29 }, "back" } }
local mheld, mp = {}, {}
local function menu_keys()
  for name, k in pairs(MK) do
    local d = btn(k[2])
    for _, u in ipairs(k[1]) do if keydown(u) then d = true end end
    mp[name] = d and not mheld[name]
    mheld[name] = d
  end
end

-- the actions the player can move to another key, in the order of the Controls page
local REMAP = { { "fwd", "Move forward" }, { "back", "Move back" }, { "left", "Move left" }, { "right", "Move right" },
  { "dodge", "Dodge" }, { "atk1", "Attack (again: the combo)" }, { "atk2", "Second hit" }, { "atk3", "Third hit" },
  { "charge", "Charged attack (hold)" }, { "saw", "Saw: open / close" }, { "gun", "Pistol" }, { "lock", "Lock on" },
  { "camL", "Camera left" }, { "camR", "Camera right" }, { "camU", "Camera closer" }, { "camD", "Camera farther" } }
local DEFAULT = {}
for _, r in ipairs(REMAP) do DEFAULT[r[1]] = { table.unpack(BIND[r[1]]) } end
local FIXED = { [0x29] = true, [0x28] = true, [0x42] = true }          -- ESC, ENTER and F9 keep their jobs

local KNAME = { [0x2C] = "space", [0x28] = "enter", [0x2A] = "backspace", [0x2B] = "tab", [0x2D] = "-", [0x2E] = "=",
  [0x2F] = "[", [0x30] = "]", [0x31] = "\\", [0x33] = ";", [0x34] = "'", [0x35] = "`", [0x36] = ",", [0x37] = ".",
  [0x38] = "/", [0x4F] = "right", [0x50] = "left", [0x51] = "down", [0x52] = "up", [0xE0] = "ctrl", [0xE4] = "r ctrl",
  [0xE1] = "shift", [0xE5] = "r shift", [0xE2] = "alt", [0xE6] = "r alt", [0x27] = "0" }
for i = 0, 25 do KNAME[0x04 + i] = string.char(65 + i) end
for i = 1, 9 do KNAME[0x1D + i] = tostring(i) end
for i = 1, 12 do KNAME[0x39 + i] = "F" .. i end
local function keyname(u) return KNAME[u] or string.format("key %02X", u) end

local function save_options()
  local keys = {}
  for _, r in ipairs(REMAP) do
    local a = r[1]
    if BIND[a][1] ~= DEFAULT[a][1] or #BIND[a] ~= #DEFAULT[a] then keys[a] = BIND[a] end
  end
  pcall(save, { keys = keys, music = O.music, sfx = O.sfx })
end

local function load_options()
  local ok, t = pcall(saved)
  if not ok or type(t) ~= "table" then return end
  if type(t.keys) == "table" then
    for a, list in pairs(t.keys) do
      if DEFAULT[a] and type(list) == "table" and type(list[1]) == "number" then BIND[a] = { list[1] } end
    end
  end
  if t.music ~= nil then O.music = t.music and true or false end
  if type(t.sfx) == "number" then O.sfx = max(0, min(10, floor(t.sfx))) end
end

-- give action `a` the key `u`; an action that had `u` takes the old key of `a` (a swap: no action is left without a key)
local function bind_key(a, u)
  local old = BIND[a][1]
  for _, r in ipairs(REMAP) do
    local b = r[1]
    if b ~= a then
      for i, k in ipairs(BIND[b]) do if k == u then BIND[b][i] = old end end
    end
  end
  BIND[a] = { u }
  save_options()
end

local MENU = { "Resume", "Controls", "Volume", "Effects", "Music", "Resolution", "Textures", "Brightness",
               "Performance", "Restart at the lantern", "Quit the game" }
local function menu_value(i)
  if i == 3 then return tostring(volume() or 0) .. " / 10" end
  if i == 4 then return O.sfx .. " / 10" end
  if i == 5 then return O.music and "on" or "off" end
  if i == 6 then return O.res == 1 and "1280x720" or "640x360" end
  if i == 7 then return O.textures and "on" or "off" end
  if i == 8 then return ({ "dim", "normal", "bright" })[O.bright] end
  if i == 9 then return O.perf and "fps, ms, triangles" or "off" end
  if i == 11 and G.confirm == 11 then return "ENTER again to quit" end
  return ""
end

local function apply_resolution()
  if O.res == 1 then screen(1280, 720) else screen(640, 360) end
end

local function draw_rows(title, rows, cur, x, y, step, s)
  text(title, x, y - 34 * s, 0xE0C890, s * 2)
  for i, r in ipairs(rows) do
    local c = i == cur and 0xFFE0A0 or 0xB8B0A8
    text((i == cur and "> " or "  ") .. r[1], x, y + (i - 1) * step, c)
    if r[2] ~= "" then text(r[2], x + 230 * s, y + (i - 1) * step, c) end
  end
end

local function draw_menu()
  local s = S()
  rectfill(0, 0, SCREEN_W, SCREEN_H, 0x0C0C12)
  local x = SCREEN_W // 2 - 170 * s
  if G.page == "keys" then
    local rows = {}
    for i, r in ipairs(REMAP) do
      local v = G.wait == r[1] and "press a key (ESC: cancel)" or keyname(BIND[r[1]][1])
      rows[i] = { r[2], v }
    end
    rows[#rows + 1] = { "Reset to default", G.confirm == "reset" and "ENTER again to reset" or "" }
    rows[#rows + 1] = { "Back", "" }
    draw_rows("CONTROLS", rows, G.krow, x, 46 * s, 15 * s, s)
    text("ENTER change   ESC back", x, SCREEN_H - 16 * s, 0x807870)
  else
    local rows = {}
    for i, label in ipairs(MENU) do rows[i] = { label, menu_value(i) } end
    draw_rows("DARK NIGHT", rows, G.menu, x, SCREEN_H // 2 - 80 * s, 18 * s, s)
  end
end

local function keys_update()
  if G.wait then
    local k = keys() or {}
    if G.waitup then
      if #k == 0 then G.waitup = false end
      return
    end
    local u = k[1]
    if u == 0x29 then G.wait = nil; snd("menu", -5)
    elseif u and not FIXED[u] then bind_key(G.wait, u); G.wait = nil; snd("menu") end
    return
  end
  local n = #REMAP + 2
  if mp.back then G.page, G.confirm = "main", nil; snd("menu", -3); return end
  if mp.up then G.krow = (G.krow - 2) % n + 1; G.confirm = nil; snd("menu", -3) end
  if mp.down then G.krow = G.krow % n + 1; G.confirm = nil; snd("menu", -3) end
  if not mp.ok then return end
  snd("menu")
  if G.krow <= #REMAP then G.wait, G.waitup = REMAP[G.krow][1], true
  elseif G.krow == #REMAP + 1 then
    if G.confirm == "reset" then
      for a, list in pairs(DEFAULT) do BIND[a] = { table.unpack(list) } end
      G.confirm = nil
      save_options()
    else G.confirm = "reset" end
  else G.page, G.confirm = "main", nil end
end

local function menu_update()
  if G.page == "keys" then keys_update(); return end
  if mp.back then G.state, G.confirm = "play", nil; snd("menu"); return end
  if mp.up then G.menu = (G.menu - 2) % #MENU + 1; G.confirm = nil; snd("menu", -3) end
  if mp.down then G.menu = G.menu % #MENU + 1; G.confirm = nil; snd("menu", -3) end
  local step = (mp.right and 1 or 0) - (mp.left and 1 or 0)
  if not mp.ok and step == 0 then return end
  if step == 0 then step = 1 end
  local i = G.menu
  if i == 1 and mp.ok then G.state = "play"
  elseif i == 2 and mp.ok then G.page, G.krow = "keys", 1
  elseif i == 3 then volume(max(0, min(10, (volume() or 0) + step)))
  elseif i == 4 then O.sfx = max(0, min(10, O.sfx + step)); save_options()
  elseif i == 5 then O.music = not O.music; save_options()
  elseif i == 6 then O.res = 3 - O.res; apply_resolution()
  elseif i == 7 then O.textures = not O.textures
  elseif i == 8 then O.bright = (O.bright + step - 1) % 3 + 1
  elseif i == 9 then
    O.perf = not O.perf
    devkit(O.perf and 1 or 0)                      -- the dev kit's overlay at the top right (fps, ms, triangles, CPU)
  elseif i == 10 and mp.ok then respawn(); G.state = "play"
  elseif i == 11 and mp.ok then
    if G.confirm == 11 then quit() else G.confirm = 11 end
  end
  snd("menu")
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
  load_options()
  load_world()
  reset_player(G.spawn[1], G.spawn[2])
  Cam.x, Cam.y, Cam.z = P.x - 4, P.y + 2.5, P.z
  reverb(0.7, 0.45, 0.8)                           -- the clinic's halls
  say("The Clinic", 4)
  local n = { villager = 0, huntsman = 0, dog = 0 }
  for _, e in ipairs(enemies) do n[e.kind] = n[e.kind] + 1 end
  log("darknight: " .. #objs .. " objects, " .. #enemies .. " enemies (villager " .. n.villager .. ", huntsman " .. n.huntsman .. ", dog " .. n.dog .. ")")
end

function _update()
  G.frame = G.frame + 1
  local mx, my = read_input()
  menu_keys()
  -- hit stop: a landed blow holds the picture for a few frames (the keys pressed meanwhile are kept)
  if G.stop > 0 and G.state == "play" then
    G.stop = G.stop - 1
    buffer_input()
    return
  end
  if G.state == "play" then
    if pressed.menu then G.state, G.page, G.menu = "menu", "main", 1; snd("menu"); return end
    if pressed.skip then
      G.skipn = (G.skipn or 0) % #SKIPS + 1
      local s = SKIPS[G.skipn]
      P.x, P.z = s[1], s[2]
      P.y = map_height(P.x, P.z) or P.y
      P.lock = nil
      Cam.yaw = P.yaw                                  -- the view goes behind the Hunter at once
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
    update_player(0, 0)
    update_enemies()
    update_camera()
    if G.died > 3.6 then respawn() end
  end
end

function _draw()
  cls(SKY)
  zclear()
  apply_environment()
  place_lamps()
  local yaw, pitch = look(Cam.x, Cam.y, Cam.z, Cam.tx or P.x, Cam.ty or P.y + 1.3, Cam.tz or P.z)
  camera3d(Cam.x, Cam.y, Cam.z, yaw, pitch, 56)
  textures3d(O.textures)
  map3d_draw(world, 0)
  draw_objects()
  for _, e in ipairs(enemies) do draw_enemy(e) end
  draw_player()
  draw_effects()
  if G.state == "menu" then draw_menu() else draw_hud() end
end
