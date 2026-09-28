-- Hunter's Night: a gothic top-down hunt through a town of beasts.
-- Arrows: move (8 directions). A: saw cleaver. B: pistol (staggers; a
-- staggered foe takes a visceral blow). X: dodge (backstep, or a quickstep
-- towards the arrows held). Y: blood vial. Light the hunter's lamps to rest;
-- the great beast waits in the cathedral to the north.

local W, H = SCREEN_W, SCREEN_H          -- 320 x 180
local MAPW = 256                         -- cells of 8 px: 2048 x 2048 pixels
local TEXT, RED, DIM, GOLD = 0xE8E0D0, 0xC02020, 0x807060, 0xE0B050

-- tiles (see mkassets.py)
local FLOOR = 1
local T_WINDOW, T_LAMP, T_CANDELABRA, T_CANDLES, T_CANDLES2, T_HLAMP = 33, 40, 50, 64, 67, 68
local M_PLAYER, M_VILL, M_BEAST, M_BOSS = 244, 245, 246, 247
local S_HUNTER, S_VILL, S_BEAST, S_BOSS1, S_BOSS2 = 128, 160, 192, 140, 204
local S_FLASH, S_SPLAT, S_VIAL, S_BULLETS, S_ORB = 225, 226, 227, 228, 229

local function solid_tile(v) return v >= 32 and v < 64 end

----------------------------------------------------------------- sound

local tune, tune_i, tune_t
local function jingle(notes) tune, tune_i, tune_t = notes, 1, 0 end
local function update_jingle()
  if not tune then return end
  tune_t = tune_t - 1
  if tune_t > 0 then return end
  local n = tune[tune_i]
  if not n then tune = nil; return end
  if n[1] > 0 then note(3, n[1], n[2] * 15, n[3] or TRIANGLE, n[4] or 110) end
  tune_t, tune_i = n[2], tune_i + 1
end

-- a slow minor organ on voices 4-6, a bell on voice 7
local CHORDS = {
  { 220, 262, 330 }, { 175, 220, 262 }, { 147, 175, 220 }, { 165, 208, 247 },
}
local chord_i, chord_t, music_fast = 0, 0, false
local function update_music()
  chord_t = chord_t - 1
  if chord_t > 0 then return end
  chord_i = chord_i % #CHORDS + 1
  chord_t = music_fast and 90 or 220
  local c = CHORDS[chord_i]
  for v = 1, 3 do note(3 + v, c[v] / (v == 1 and 2 or 1), chord_t * 14, TRIANGLE, music_fast and 70 or 45) end
  if chord_i == 1 and not music_fast then note(7, 523, 1200, TRIANGLE, 55) end
end

local function sfx(kind)
  if kind == "slash" then note(0, 2600, 90, NOISE, 70)
  elseif kind == "gun" then note(0, 900, 160, NOISE, 170); note(2, 70, 120, SQUARE, 110)
  elseif kind == "hit" then note(2, 380, 110, NOISE, 130)
  elseif kind == "visceral" then note(2, 180, 500, NOISE, 180); note(1, 60, 400, SAW, 130)
  elseif kind == "hurt" then note(1, 90, 260, SAW, 140); note(2, 500, 160, NOISE, 120)
  elseif kind == "dodge" then note(0, 1400, 70, NOISE, 45)
  elseif kind == "heal" then jingle({ { 523, 4 }, { 659, 4 }, { 784, 8 } })
  elseif kind == "growl" then note(2, 70, 400, SAW, 90)
  elseif kind == "roar" then note(2, 55, 900, SAW, 170); note(1, 300, 700, NOISE, 110)
  elseif kind == "lamp" then jingle({ { 392, 8 }, { 494, 8 }, { 587, 8 }, { 784, 24 } })
  elseif kind == "pick" then note(1, 1320, 60, TRIANGLE, 90)
  end
end

----------------------------------------------------------------- the town

local lights = {}        -- buckets of 16x16 cells: { x, y, r, colour, intensity, flicker }
local spawns = {}        -- { kind, x, y }
local hlamps = {}        -- hunter's lamps: { x, y, lit }
local start_x, start_y = 160, 1840

local function add_light(cx, cy, r, c, k, fl)
  local key = (cy // 16) * 16 + cx // 16
  local b = lights[key]
  if not b then b = {}; lights[key] = b end
  b[#b + 1] = { x = cx * 8 + 4, y = cy * 8 + 3, r = r, c = c, k = k, fl = fl, seed = (cx * 7 + cy * 13) % 17 }
end

local function scan_map()
  for y = 0, MAPW - 1 do
    for x = 0, MAPW - 1 do
      local v = mget(x, y)
      if v >= 244 then
        if v == M_PLAYER then start_x, start_y = x * 8 + 4, y * 8 + 4
        elseif v == M_VILL then spawns[#spawns + 1] = { kind = "vill", x = x * 8 + 4, y = y * 8 + 4 }
        elseif v == M_BEAST then spawns[#spawns + 1] = { kind = "beast", x = x * 8 + 4, y = y * 8 + 4 }
        elseif v == M_BOSS then spawns[#spawns + 1] = { kind = "boss", x = x * 8 + 4, y = y * 8 + 4 }
        end
        mset(x, y, FLOOR)
      elseif v == T_LAMP then add_light(x, y, 50, 0xFFB060, 1.05, 0.03)
      elseif v == T_WINDOW then add_light(x, y + 1, 24, 0xFFA048, 0.7, 0.02)
      elseif v == T_CANDELABRA then add_light(x, y, 44, 0xFF9038, 1.2, 0.12)
      elseif v == T_CANDLES or v == T_CANDLES2 then add_light(x, y, 28, 0xFF8030, 1.1, 0.18)
      elseif v == T_HLAMP then
        add_light(x, y, 50, 0xFFD890, 1.3, 0.05)
        hlamps[#hlamps + 1] = { x = x * 8 + 4, y = y * 8 + 4 }
      end
    end
  end
end

local function solid_at(px, py)
  local cx, cy = px // 8, py // 8
  if cx < 0 or cy < 0 or cx >= MAPW or cy >= MAPW then return true end
  return solid_tile(mget(cx, cy))
end

-- moves a body (feet box 8x6) and slides along walls
local function move(o, dx, dy)
  local nx = o.x + dx
  if not (solid_at(nx - 4, o.y - 3) or solid_at(nx + 3, o.y - 3) or solid_at(nx - 4, o.y + 2) or solid_at(nx + 3, o.y + 2)) then
    o.x = nx
  end
  local ny = o.y + dy
  if not (solid_at(o.x - 4, ny - 3) or solid_at(o.x + 3, ny - 3) or solid_at(o.x - 4, ny + 2) or solid_at(o.x + 3, ny + 2)) then
    o.y = ny
  end
end

----------------------------------------------------------------- state

local state = "title"      -- title, play, dead, victory
local p                    -- the hunter
local foes, pickups, decals, parts, tracers
local blood, best, lost_orb = 0, 0, nil
local frame, shake, msg, msg_t = 0, 0, nil, 0
local cam_x, cam_y = 0, 0
local boss                 -- the great beast, once awake
local boss_dead = false
local respawn = nil

local function say(s, t) msg, msg_t = s, t or 120 end

local function new_hunter(x, y)
  return { x = x, y = y, fx = 0, fy = 1, hp = 100, rally = 0, st = 100, st_wait = 0,
           vials = 10, bullets = 20, atk = 0, dodge = 0, ddx = 0, ddy = 0, inv = 0,
           heal = 0, gun_cd = 0, walk = 0, flash = 0 }
end

local function spawn_foes()
  foes = {}
  for _, s in ipairs(spawns) do
    if s.kind ~= "boss" or not boss_dead then
      local f = { kind = s.kind, x = s.x, y = s.y, hx = s.x, hy = s.y, t = math.random(0, 60),
                  fx = 0, fy = 1, wind = 0, act = 0, cd = 0, stag = 0, hurt = 0, dx = 0, dy = 0 }
      if s.kind == "vill" then f.hp, f.max = 55, 55
      elseif s.kind == "beast" then f.hp, f.max = 120, 120
      else f.hp, f.max, f.awake = 800, 800, false; boss = f end
      foes[#foes + 1] = f
    end
  end
end

local function start_game()
  math.randomseed(stat(3))
  p = new_hunter(start_x, start_y)
  respawn = { x = start_x, y = start_y }
  pickups, decals, parts, tracers = {}, {}, {}, {}
  blood, lost_orb, boss, boss_dead = 0, nil, nil, false
  for _, l in ipairs(hlamps) do l.lit = false end
  spawn_foes()
  state = "play"
  say("THE NIGHT OF THE HUNT", 150)
  jingle({ { 220, 20 }, { 262, 20 }, { 247, 20 }, { 196, 40 } })
end

function _init()
  local data = saved()
  if data and data.best then best = data.best end
  scan_map()
  envelope(0, 0, 40, 0, 20)             -- weapons: sharp, short
  envelope(2, 0, 50, 0, 30)             -- impacts die away
  for v = 4, 6 do envelope(v, 70, 0, 255, 120) end   -- organ swells
  envelope(7, 0, 110, 0, 110)           -- bell
  p = new_hunter(1024, 900)
  pickups, decals, parts, tracers, foes = {}, {}, {}, {}, {}
end

----------------------------------------------------------------- combat

local function bleed(x, y, n, big)
  for i = 1, n do
    local a, s = math.random() * 6.283, 0.5 + math.random() * (big and 2.5 or 1.5)
    parts[#parts + 1] = { x = x, y = y, vx = math.cos(a) * s, vy = math.sin(a) * s - 0.5, life = 18 + math.random(0, 14),
                          c = math.random() < 0.5 and 0xA01010 or 0x600808 }
  end
  if #decals > 140 then table.remove(decals, 1) end
  decals[#decals + 1] = { x = x - 4 + math.random(-3, 3), y = y - 2 + math.random(-3, 3) }
end

local function hurt_player(dmg, from_x, from_y)
  if p.inv > 0 or state ~= "play" then return end
  p.hp = p.hp - dmg
  p.rally = dmg
  p.inv, shake = 40, 10
  local dx, dy = p.x - from_x, p.y - from_y
  local d = math.max(1, math.sqrt(dx * dx + dy * dy))
  move(p, dx / d * 6, dy / d * 6)
  bleed(p.x, p.y - 6, 10, false)
  sfx("hurt")
  if p.hp <= 0 then
    p.hp = 0
    state = "dead"
    frame = 0
    lost_orb = blood > 0 and { x = p.x, y = p.y, amount = blood } or nil
    blood = 0
    say("YOU DIED", 999)
    note(3, 110, 1500, SAW, 150)
  end
end

local function kill(f)
  local gain = f.kind == "vill" and 60 or f.kind == "beast" and 150 or 5000
  blood = blood + gain
  bleed(f.x, f.y - 6, f.kind == "boss" and 60 or 20, true)
  local r = math.random()
  if f.kind ~= "boss" then
    if r < (f.kind == "beast" and 0.35 or 0.2) then pickups[#pickups + 1] = { x = f.x, y = f.y, s = S_VIAL }
    elseif r < 0.55 then pickups[#pickups + 1] = { x = f.x, y = f.y, s = S_BULLETS } end
  else
    boss_dead, boss = true, nil
    state = "victory"
    frame = 0
    say("BEAST SLAIN", 999)
    music_fast = false
    jingle({ { 262, 12 }, { 330, 12 }, { 392, 12 }, { 523, 40 }, { 494, 12 }, { 523, 60 } })
    if blood > best then best = blood end
    save({ best = best })
  end
end

local function damage(f, dmg, visceral)
  f.hp = f.hp - dmg
  f.hurt = 8
  bleed(f.x, f.y - 6, visceral and 30 or 6, visceral)
  -- rally: striking back regains the health just lost
  if p.rally > 0 then
    local g = math.min(p.rally, dmg * 0.5)
    p.hp, p.rally = math.min(100, p.hp + g), p.rally - g
  end
  if f.hp <= 0 then kill(f); return true end
  if f.kind ~= "boss" and not visceral then f.wind = 0; f.act = 0; f.cd = math.max(f.cd, 14) end
  return false
end

local function slash_hits()
  local reach = 24
  for i = #foes, 1, -1 do
    local f = foes[i]
    local dx, dy = f.x - p.x, (f.y - 4) - (p.y - 4)
    local big = f.kind == "boss" and 14 or 0
    local d = math.sqrt(dx * dx + dy * dy)
    if d < reach + big and (d < 8 + big or (dx * p.fx + dy * p.fy) / math.max(d, 1) > 0.2) then
      if f.stag > 0 then
        sfx("visceral"); shake = 14
        say("VISCERAL ATTACK", 50)
        f.stag = 0
        if damage(f, f.kind == "boss" and 160 or 200, true) then table.remove(foes, i) end
      else
        sfx("hit")
        if damage(f, 32, false) then table.remove(foes, i) end
      end
    end
  end
end

-- the pistol: the first foe along the line of fire, walls stop the shot
local function shoot()
  local best_f, best_d = nil, 170
  for _, f in ipairs(foes) do
    local dx, dy = f.x - p.x, (f.y - 6) - (p.y - 6)
    local along = dx * p.fx + dy * p.fy
    local side = math.abs(dx * p.fy - dy * p.fx)
    if along > 0 and along < best_d and side < (f.kind == "boss" and 16 or 9) then best_f, best_d = f, along end
  end
  local ex, ey = p.x + p.fx * best_d, p.y - 6 + p.fy * best_d
  for s = 6, best_d, 4 do                          -- walls
    local x, y = p.x + p.fx * s, p.y - 4 + p.fy * s
    if solid_at(x, y) then ex, ey, best_f = x, y, nil; break end
  end
  tracers[#tracers + 1] = { x0 = p.x + p.fx * 8, y0 = p.y - 6 + p.fy * 8, x1 = ex, y1 = ey, t = 5 }
  p.flash = 5
  sfx("gun")
  if best_f then
    local f = best_f
    -- a shot during a wind-up topples the foe: open to a visceral attack
    if f.wind > 0 then
      f.stag, f.wind = f.kind == "boss" and 110 or 90, 0
      say("STAGGERED", 40)
    else
      f.stag = math.max(f.stag, f.kind == "boss" and 0 or 16)
    end
    if damage(f, 6, false) then
      for i, g in ipairs(foes) do if g == f then table.remove(foes, i) break end end
    end
  end
end

----------------------------------------------------------------- update

local function update_hunter()
  local ix, iy = 0, 0
  if btn(0) then ix = -1 end
  if btn(1) then ix = 1 end
  if btn(2) then iy = -1 end
  if btn(3) then iy = 1 end
  local n = (ix ~= 0 and iy ~= 0) and 0.7071 or 1
  local busy = p.atk > 0 or p.dodge > 0 or p.heal > 0

  if p.inv > 0 then p.inv = p.inv - 1 end
  if p.gun_cd > 0 then p.gun_cd = p.gun_cd - 1 end
  if p.flash > 0 then p.flash = p.flash - 1 end
  if p.rally > 0 then p.rally = math.max(0, p.rally - 0.2) end

  if not busy then
    if ix ~= 0 or iy ~= 0 then p.fx, p.fy = ix * n, iy * n end
    if btnp(4) and p.st >= 12 then
      p.atk, p.st, p.st_wait = 18, p.st - 18, 30
      sfx("slash")
    elseif btnp(5) and p.gun_cd == 0 then
      if p.bullets > 0 then p.bullets = p.bullets - 1; p.gun_cd = 22; shoot()
      else say("NO BULLETS", 40) end
    elseif btnp(6) and p.st >= 12 then
      local dx, dy = -p.fx, -p.fy                -- backstep
      if ix ~= 0 or iy ~= 0 then dx, dy = ix * n, iy * n end   -- quickstep
      p.dodge, p.ddx, p.ddy, p.inv = 13, dx, dy, math.max(p.inv, 10)
      p.st, p.st_wait = p.st - 20, 30
      sfx("dodge")
    elseif btnp(7) then
      if p.vials > 0 and p.hp < 100 then p.heal = 34; p.vials = p.vials - 1
      elseif p.vials == 0 then say("NO BLOOD VIALS", 40) end
    else
      local sp = 1.45
      move(p, ix * n * sp, iy * n * sp)
      if ix ~= 0 or iy ~= 0 then p.walk = p.walk + 1 end
    end
  end

  if p.atk > 0 then
    p.atk = p.atk - 1
    if p.atk == 12 then slash_hits() end
    if p.atk > 8 then move(p, p.fx * 0.8, p.fy * 0.8) end
  end
  if p.dodge > 0 then
    local s = p.dodge * 0.32
    move(p, p.ddx * s, p.ddy * s)
    p.dodge = p.dodge - 1
  end
  if p.heal > 0 then
    p.heal = p.heal - 1
    if p.heal == 17 then p.hp = math.min(100, p.hp + 45); p.rally = 0; sfx("heal") end
    move(p, ix * n * 0.5, iy * n * 0.5)
  end
  if p.st_wait > 0 then p.st_wait = p.st_wait - 1
  elseif p.st < 100 then p.st = math.min(100, p.st + 1.3) end

  -- hunter's lamps
  for _, l in ipairs(hlamps) do
    if not l.lit and math.abs(p.x - l.x) < 14 and math.abs(p.y - l.y) < 14 then
      l.lit = true
      respawn = { x = l.x, y = l.y + 10 }
      p.hp, p.vials, p.bullets = 100, math.max(p.vials, 10), 20
      say("LAMP LIT", 90)
      sfx("lamp")
    end
  end
  -- pickups and the lost blood
  for i = #pickups, 1, -1 do
    local k = pickups[i]
    if math.abs(p.x - k.x) < 9 and math.abs(p.y - k.y) < 9 then
      if k.s == S_VIAL then p.vials = math.min(20, p.vials + 1) else p.bullets = math.min(20, p.bullets + 5) end
      table.remove(pickups, i)
      sfx("pick")
    end
  end
  if lost_orb and math.abs(p.x - lost_orb.x) < 10 and math.abs(p.y - lost_orb.y) < 10 then
    blood = blood + lost_orb.amount
    say("BLOOD REGAINED", 60)
    lost_orb = nil
    sfx("pick")
  end
end

local function face(f, dx, dy, d)
  f.fx, f.fy = dx / d, dy / d
end

local function update_foe(f)
  local dx, dy = p.x - f.x, p.y - f.y
  local d = math.sqrt(dx * dx + dy * dy)
  if d > 300 and f.kind ~= "boss" then return end
  f.t = f.t + 1
  if f.hurt > 0 then f.hurt = f.hurt - 1 end
  if f.stag > 0 then f.stag = f.stag - 1; return end
  if f.cd > 0 then f.cd = f.cd - 1 end
  local dead = state ~= "play"

  if f.kind == "boss" then
    if not f.awake then
      if d < 150 then
        f.awake = true
        music_fast = true
        chord_t = 0
        sfx("roar")
        say("THE GREAT BEAST", 120)
        shake = 20
      end
      return
    end
    local fast = f.hp < f.max / 2 and 0.7 or 1
    if f.act > 0 then                          -- leap in the air
      f.act = f.act - 1
      f.x, f.y = f.x + f.dx, f.y + f.dy
      if f.act == 0 then
        shake = 16
        sfx("roar")
        if d < 40 then hurt_player(34, f.x, f.y) end
        parts[#parts + 1] = { ring = true, x = f.x, y = f.y, life = 16 }
        f.cd = 40
      end
      return
    end
    if f.wind > 0 then
      f.wind = f.wind - 1
      if f.wind == 0 then
        if f.move == "swipe" then
          if d < 42 then hurt_player(28, f.x, f.y) end
          shake = 6
          sfx("hit")
          f.cd = math.floor(34 * fast)
        elseif f.move == "leap" then
          f.act = 30
          f.dx, f.dy = dx / 30, dy / 30
        end
      end
      return
    end
    if d > 1 then face(f, dx, dy, d) end
    if f.cd == 0 then
      if d < 40 then f.move, f.wind = "swipe", math.floor(30 * fast)
      elseif math.random() < 0.02 then f.move, f.wind = "leap", math.floor(26 * fast) end
    end
    if d > 26 and f.wind == 0 then move(f, f.fx * 0.7 / fast, f.fy * 0.7 / fast) end
    return
  end

  local aggro = f.kind == "beast" and 150 or 115
  if dead or d > aggro then                 -- wander near home
    if f.t % 90 == 0 then
      local a = math.random() * 6.283
      f.dx, f.dy = math.cos(a) * 0.3, math.sin(a) * 0.3
      if math.abs(f.x - f.hx) + math.abs(f.y - f.hy) > 60 then
        local hx, hy = f.hx - f.x, f.hy - f.y
        local hd = math.max(1, math.sqrt(hx * hx + hy * hy))
        f.dx, f.dy = hx / hd * 0.4, hy / hd * 0.4
      end
    end
    if f.t % 90 < 50 then move(f, f.dx, f.dy) end
    return
  end
  if f.t % 240 == 0 and f.kind == "beast" then sfx("growl") end

  if f.act > 0 then                         -- the beast's lunge
    f.act = f.act - 1
    move(f, f.dx, f.dy)
    if d < 14 and not f.hit then f.hit = true; hurt_player(22, f.x, f.y) end
    return
  end
  if f.wind > 0 then
    f.wind = f.wind - 1
    if f.wind == 0 then
      if f.kind == "vill" then
        if d < 24 then hurt_player(15, f.x, f.y) end
        f.cd = 40
        sfx("hit")
      else
        f.act, f.hit = 12, false
        f.dx, f.dy = f.fx * 3.2, f.fy * 3.2
        f.cd = 50
      end
    end
    return
  end
  if d > 1 then face(f, dx, dy, d) end
  local reach = f.kind == "vill" and 20 or 44
  if d < reach and f.cd == 0 then
    f.wind = f.kind == "vill" and 26 or 20
  elseif d > 14 then
    local sp = f.kind == "vill" and 0.6 or 1.0
    move(f, f.fx * sp, f.fy * sp)
  end
end

function _update()
  frame = frame + 1
  update_jingle()
  if state ~= "title" then update_music() end
  if shake > 0 then shake = shake - 1 end
  if msg_t > 0 then msg_t = msg_t - 1 end

  if state == "title" then
    if btnp(4) then start_game() end
    return
  end
  if state == "dead" then
    for _, f in ipairs(foes) do update_foe(f) end
    if frame > 150 and btnp(4) then
      -- wake at the last lamp; the beasts are back
      p = new_hunter(respawn.x, respawn.y)
      p.vials = 10
      boss = nil
      spawn_foes()
      music_fast = false
      state = "play"
      msg_t = 0
    end
  elseif state == "victory" then
    if frame > 240 and btnp(4) then state = "title"; msg_t = 0 end
  else
    update_hunter()
    for _, f in ipairs(foes) do update_foe(f) end
  end
  for i = #parts, 1, -1 do
    local q = parts[i]
    q.life = q.life - 1
    if not q.ring then q.x, q.y, q.vx, q.vy = q.x + q.vx, q.y + q.vy, q.vx * 0.9, q.vy * 0.9 + 0.08 end
    if q.life <= 0 then table.remove(parts, i) end
  end
  for i = #tracers, 1, -1 do
    tracers[i].t = tracers[i].t - 1
    if tracers[i].t <= 0 then table.remove(tracers, i) end
  end
end

----------------------------------------------------------------- draw

local function center(s, y, c)
  local x = (W - #s * 8) // 16 * 8
  rectfill(x - 8, y - 4, #s * 8 + 16, 24, 0x000000)
  print(s, x, y, c)
end

local function draw_hunter()
  local moving = (btn(0) or btn(1) or btn(2) or btn(3)) and p.atk == 0
  local fr = moving and (p.walk // 8) % 2 or 0
  local s, flip
  if math.abs(p.fx) > 0.3 then s, flip = S_HUNTER + 8 + fr * 2, p.fx < 0
  elseif p.fy < 0 then s, flip = S_HUNTER + 4 + fr * 2, false
  else s, flip = S_HUNTER + fr * 2, false end
  if p.inv > 0 and p.dodge == 0 and (p.inv // 3) % 2 == 0 then return end
  spr(s, math.floor(p.x) - 8, math.floor(p.y) - 14, 2, 2, flip)
  -- the saw cleaver's arc
  if p.atk > 6 and p.atk <= 14 then
    local a0 = math.atan(p.fy, p.fx)
    local prog = (14 - p.atk) / 8
    for k = 0, 6 do
      local a = a0 - 1.1 + (k / 6) * 2.2 * prog
      local c = k > 4 and 0xFFFFFF or k > 2 and 0xD0C8C8 or 0x902020
      line(p.x + math.cos(a) * 8, p.y - 6 + math.sin(a) * 8, p.x + math.cos(a) * 22, p.y - 6 + math.sin(a) * 22, c)
    end
  end
  if p.flash > 2 then spr(S_FLASH, math.floor(p.x + p.fx * 10) - 4, math.floor(p.y - 6 + p.fy * 10) - 4) end
end

local function draw_foe(f)
  local x, y = math.floor(f.x), math.floor(f.y)
  if f.kind == "boss" then
    local s = (f.t // 20) % 2 == 0 and S_BOSS1 or S_BOSS2
    local lift = f.act > 0 and math.floor(math.sin(f.act / 30 * 3.14) * 18) or 0
    if lift > 0 then circfill(x, y, 10, 0x000000) end
    if f.hurt % 2 == 0 then spr(s, x - 16, y - 28 - lift, 4, 4, f.fx < 0) end
  else
    local base = f.kind == "vill" and S_VILL or S_BEAST
    local fr = (f.t // 10) % 2
    local s, flip
    if math.abs(f.fx) > 0.5 then s, flip = base + 8 + fr * 2, f.fx < 0
    elseif f.fy < 0 then s, flip = base + 4 + fr * 2, false
    else s, flip = base + fr * 2, false end
    if f.hurt % 2 == 0 then spr(s, x - 8, y - 14, 2, 2, flip) end
  end
  -- wind-up: a glint over the head; staggered: circling sparks
  if f.wind > 0 and (f.wind // 3) % 2 == 0 then
    local hy = f.kind == "boss" and y - 34 or y - 18
    line(x - 3, hy, x + 3, hy, 0xFFFFFF); line(x, hy - 3, x, hy + 3, 0xFFFFFF)
  end
  if f.stag > 0 then
    for k = 0, 2 do
      local a = frame * 0.2 + k * 2.1
      pset(x + math.cos(a) * 7, (f.kind == "boss" and y - 34 or y - 18) + math.sin(a) * 3, 0xFFE060)
    end
  end
end

local function lamp_lights(t)
  local cx0, cy0 = (cam_x - 70) // 128, (cam_y - 70) // 128
  local cx1, cy1 = (cam_x + W + 70) // 128, (cam_y + H + 70) // 128
  for by = cy0, cy1 do
    for bx = cx0, cx1 do
      local b = lights[by * 16 + bx]
      if b then
        for _, l in ipairs(b) do
          if l.x > cam_x - l.r and l.x < cam_x + W + l.r and l.y > cam_y - l.r and l.y < cam_y + H + l.r then
            local fl = 1 + math.sin(t * 7 + l.seed) * l.fl + (math.random() - 0.5) * l.fl
            light(l.x, l.y, l.r * fl, l.c, l.k)
          end
        end
      end
    end
  end
end

local function draw_world(t)
  cls(0)
  camera(cam_x + ((shake > 0) and math.random(-2, 2) or 0), cam_y + ((shake > 0) and math.random(-2, 2) or 0))
  local mx, my = cam_x // 8, cam_y // 8
  map(mx, my, mx * 8, my * 8, 42, 25)
  for _, d in ipairs(decals) do spr(S_SPLAT, d.x, d.y) end
  for _, k in ipairs(pickups) do spr(k.s, k.x - 4, k.y - 6 + math.floor(math.sin(t * 4) * 1.5)) end
  if lost_orb then spr(S_ORB, lost_orb.x - 4, lost_orb.y - 6) end

  -- bodies sorted by their feet
  local list = {}
  for _, f in ipairs(foes) do
    if f.x > cam_x - 40 and f.x < cam_x + W + 40 and f.y > cam_y - 40 and f.y < cam_y + H + 50 then list[#list + 1] = f end
  end
  if state ~= "title" and (state ~= "dead" or frame < 20) then list[#list + 1] = p end
  table.sort(list, function(a, b) return a.y < b.y end)
  for _, o in ipairs(list) do
    if o == p then draw_hunter() else draw_foe(o) end
  end
  for _, q in ipairs(parts) do
    if q.ring then circ(q.x, q.y, (16 - q.life) * 3, 0xC0B090) else pset(q.x, q.y, q.c) end
  end
  for _, r in ipairs(tracers) do line(r.x0, r.y0, r.x1, r.y1, r.t > 2 and 0xFFF0C0 or 0xC08040) end

  -- the only light: lamps, candles, windows, torches, the hunter's lantern
  light_begin(0x0A0A16)
  lamp_lights(t)
  if state ~= "title" then
    light(p.x + p.fx * 4, p.y - 6, 46, 0xFFC888, 0.95)
    if p.flash > 0 then light(p.x + p.fx * 12, p.y - 6 + p.fy * 12, 90, 0xFFE0A0, 1.6) end
  end
  for _, f in ipairs(list) do
    if f ~= p and f.kind == "vill" then
      light(f.x + (f.fx >= 0 and 6 or -6), f.y - 14, 36 * (0.9 + math.random() * 0.2), 0xFF8028, 1.0)
    elseif f ~= p and f.kind == "boss" then
      light(f.x, f.y - 16, 30, 0xFFE060, 0.4)
    end
  end
  for _, r in ipairs(tracers) do if r.t > 3 then light(r.x1, r.y1, 24, 0xFFD080, 1.2) end end
  if lost_orb then light(lost_orb.x, lost_orb.y - 4, 26, 0xFF2020, 1.2) end
  light_end()
  camera(0, 0)
end

local function bar(x, y, w, h, v, max, c, back)
  rectfill(x - 1, y - 1, w + 2, h + 2, 0x000000)
  rectfill(x, y, w, h, back or 0x201010)
  rectfill(x, y, math.floor(w * v / max), h, c)
end

local function draw_hud()
  bar(6, 6, 100, 5, math.min(100, p.hp + p.rally), 100, 0xC87020)
  rectfill(6, 6, math.floor(p.hp), 5, 0xB01818)
  bar(6, 14, 80, 3, p.st, 100, 0x40A060, 0x102010)
  spr(S_VIAL, 4, H - 12)
  print(tostring(p.vials), 14, H - 16, TEXT)
  spr(S_BULLETS, 34, H - 12)
  print(tostring(p.bullets), 44, H - 16, TEXT)
  local s = "blood " .. blood
  print(s, W - 6 - #s * 8, H - 16, GOLD)
  if boss and boss.awake then
    print("THE GREAT BEAST", W // 2 - 60, H - 34, TEXT)
    bar(W // 2 - 80, H - 16, 160, 4, boss.hp, boss.max, 0xB01818)
  end
end

function _draw()
  local t = time()
  if state == "title" then
    -- the plaza by candlelight
    cam_x, cam_y = 864 + math.floor(math.sin(t * 0.1) * 60), 860
    draw_world(t)
    center("HUNTER'S NIGHT", 32, 0xC02020)
    center("a hunt among beasts", 64, DIM)
    center("A saw cleaver  B pistol", 96, TEXT)
    center("X dodge  Y blood vial", 112, TEXT)
    center("press A", 144, GOLD)
    if best > 0 then print("record " .. best, 6, 4, GOLD) end
    return
  end
  -- the camera leads a little where the hunter looks
  local tx = math.max(0, math.min(MAPW * 8 - W, math.floor(p.x + p.fx * 24 - W / 2)))
  local ty = math.max(0, math.min(MAPW * 8 - H, math.floor(p.y + p.fy * 16 - H / 2 - 8)))
  cam_x, cam_y = cam_x + (tx - cam_x) // 6, cam_y + (ty - cam_y) // 6
  if frame < 3 then cam_x, cam_y = tx, ty end
  draw_world(t)
  draw_hud()
  if msg_t > 0 then
    local c = (msg == "YOU DIED") and RED or (msg == "BEAST SLAIN" and GOLD or TEXT)
    center(msg, 64, c)
  end
  if state == "dead" and frame > 150 then center("press A", 112, DIM) end
  if state == "victory" and frame > 240 then center("record " .. best .. "   press A", 112, GOLD) end
end
