-- Astro Wing: a polygon space fighter in the style of the SNES classics.
-- Arrows steer, A fires twin lasers, B boosts. Fly through the gold rings
-- to repair the shield; at the end of the run the mothership waits.

local W, H = SCREEN_W, SCREEN_H
local SKY, HAZE, GROUND = 0x1A2A6A, 0x8098C8, 0x2E6A3A
local TEXT, ACCENT, DIM, RED = 0xF0F0F0, 0xFFD050, 0x8090B0, 0xFF5050
local FAR = 150                          -- objects appear this far away

----------------------------------------------------------------- meshes

-- A mesh is built from convex pieces; every triangle is turned to face away
-- from the centre of its piece, so the vertex order never matters.
local function builder()
  local b = { v = {}, f = {} }
  function b.piece(pts, tris, color)
    local base = #b.v // 3
    local cx, cy, cz = 0, 0, 0
    for _, p in ipairs(pts) do
      b.v[#b.v + 1], b.v[#b.v + 2], b.v[#b.v + 3] = p[1], p[2], p[3]
      cx, cy, cz = cx + p[1], cy + p[2], cz + p[3]
    end
    cx, cy, cz = cx / #pts, cy / #pts, cz / #pts
    for _, t in ipairs(tris) do
      local a, c, d = pts[t[1]], pts[t[2]], pts[t[3]]
      local ux, uy, uz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
      local vx, vy, vz = d[1] - a[1], d[2] - a[2], d[3] - a[3]
      local nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
      local mx, my, mz = (a[1] + c[1] + d[1]) / 3 - cx, (a[2] + c[2] + d[2]) / 3 - cy, (a[3] + c[3] + d[3]) / 3 - cz
      local i, j, k = base + t[1], base + t[2], base + t[3]
      if nx * mx + ny * my + nz * mz < 0 then j, k = k, j end
      local col = t[4] or color
      b.f[#b.f + 1], b.f[#b.f + 2], b.f[#b.f + 3], b.f[#b.f + 4] = i, j, k, col
    end
  end
  function b.tetra(p1, p2, p3, p4, color)
    b.piece({ p1, p2, p3, p4 }, { { 1, 2, 3 }, { 1, 2, 4 }, { 1, 3, 4 }, { 2, 3, 4 } }, color)
  end
  function b.box(x0, y0, z0, x1, y1, z1, color, top)
    b.piece({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
              { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } },
            { { 1, 2, 3 }, { 1, 3, 4 }, { 5, 6, 7 }, { 5, 7, 8 }, { 1, 2, 6 }, { 1, 6, 5 },
              { 4, 3, 7, top }, { 4, 7, 8, top }, { 1, 4, 8 }, { 1, 8, 5 }, { 2, 3, 7 }, { 2, 7, 6 } }, color)
  end
  function b.build() return mesh(b.v, b.f) end
  return b
end

local M = {}

local function make_meshes()
  -- the fighter: a pointed fuselage, swept wings, fins at the wing tips
  local b = builder()
  local N, T, L, R = { 0, 0, 2.2 }, { 0, 0.45, -0.5 }, { -0.5, 0, -0.8 }, { 0.5, 0, -0.8 }
  local B, E = { 0, -0.3, -0.5 }, { 0, 0.05, -1.2 }
  b.piece({ N, T, L, R, B, E }, {
    { 1, 2, 3 }, { 1, 4, 2 }, { 1, 3, 5, 0xB8C0D0 }, { 1, 5, 4, 0xB8C0D0 },
    { 6, 3, 2, 0x5070E0 }, { 6, 2, 4, 0x5070E0 }, { 6, 5, 3, 0x404860 }, { 6, 4, 5, 0x404860 },
  }, 0xF0F0F8)
  b.tetra({ 0, 0.2, 0.6 }, { -0.25, 0.42, -0.4 }, { 0.25, 0.42, -0.4 }, { 0, 0.62, -0.3 }, 0x40C8FF)  -- canopy
  for s = -1, 1, 2 do
    b.tetra({ 0.3 * s, 0, 0.4 }, { 2.3 * s, -0.25, -1.0 }, { 0.45 * s, 0, -1.0 }, { 0.7 * s, 0.14, -0.5 }, 0x3060D0)
    b.tetra({ 2.1 * s, -0.2, -0.3 }, { 2.3 * s, -0.25, -1.1 }, { 1.9 * s, -0.2, -1.1 }, { 2.2 * s, 0.7, -1.0 }, 0xE0E0E8)
  end
  b.tetra({ -0.2, -0.1, -1.15 }, { 0.2, -0.1, -1.15 }, { 0, 0.2, -1.15 }, { 0, 0.02, -1.6 }, 0xFF9020)  -- engine
  M.ship = b.build()

  -- enemy dart
  b = builder()
  b.piece({ { 0, 0, -1.6 }, { -0.9, 0.35, 0.6 }, { 0.9, 0.35, 0.6 }, { 0.9, -0.35, 0.6 }, { -0.9, -0.35, 0.6 } },
          { { 1, 2, 3 }, { 1, 3, 4, 0x802020 }, { 1, 4, 5, 0x601818 }, { 1, 5, 2, 0x802020 }, { 2, 3, 4, 0xFFB030 }, { 2, 4, 5, 0xFFB030 } },
          0xE04040)
  for s = -1, 1, 2 do
    b.tetra({ 0.6 * s, 0, 0 }, { 2.0 * s, 0.3, 0.9 }, { 0.8 * s, 0, 0.7 }, { 0.9 * s, -0.25, 0.4 }, 0xB02828)
  end
  M.dart = b.build()

  -- tower with a red roof, and a gate made of three blocks
  b = builder()
  b.box(-1, 0, -1, 1, 9, 1, 0x9098A8, 0xE04030)
  M.tower = b.build()
  b = builder()
  b.box(-6, 0, -0.8, -4.4, 8, 0.8, 0xA0A8B8, 0xA0A8B8)
  b.box(4.4, 0, -0.8, 6, 8, 0.8, 0xA0A8B8, 0xA0A8B8)
  b.box(-6, 8, -0.8, 6, 9.6, 0.8, 0xC0C8D8, 0x40C8FF)
  M.gate = b.build()

  -- gold ring: 10 wedge-shaped segments
  b = builder()
  local n, r0, r1, d = 10, 3.2, 3.9, 0.35
  for i = 0, n - 1 do
    local a0, a1 = i / n * 6.2832, (i + 1) / n * 6.2832
    local p = {}
    for _, a in ipairs({ a0, a1 }) do
      for _, r in ipairs({ r0, r1 }) do
        p[#p + 1] = { math.cos(a) * r, math.sin(a) * r, -d }
        p[#p + 1] = { math.cos(a) * r, math.sin(a) * r, d }
      end
    end
    -- p: 1,2 inner a0; 3,4 outer a0; 5,6 inner a1; 7,8 outer a1
    b.piece(p, { { 1, 3, 7 }, { 1, 7, 5 }, { 2, 4, 8 }, { 2, 8, 6 }, { 1, 2, 6 }, { 1, 6, 5 },
                 { 3, 4, 8 }, { 3, 8, 7 }, { 1, 2, 4 }, { 1, 4, 3 }, { 5, 6, 8 }, { 5, 8, 7 } },
            i % 2 == 0 and 0xFFD040 or 0xE0A020)
  end
  M.ring = b.build()

  -- lasers, bolts, debris, ground marks
  b = builder()
  b.box(-0.08, -0.08, -0.9, 0.08, 0.08, 0.9, 0x60FF80, 0xC0FFD0)
  M.laser = b.build()
  M.bolt = mesh_sphere(3, 6, 0xFF6040, 0xFFC040)
  M.debris = mesh_cube(0xFFA040)
  M.debris2 = mesh_cube(0xE04040)
  b = builder()
  b.box(-1.6, -0.05, -3, 1.6, 0, 3, 0x285E34, 0x3C8048)
  M.mark = b.build()

  -- mothership: a core and four turrets
  M.core = mesh_sphere(6, 12, 0x8040C0, 0x502880)
  M.core_hot = mesh_sphere(6, 12, 0xFF6060, 0xC03030)
  b = builder()
  b.box(-1.2, -1.2, -1.2, 1.2, 1.2, 1.2, 0x808890, 0xB0B8C0)
  b.box(-0.3, -0.3, -3, 0.3, 0.3, -1.2, 0x505860, 0x505860)
  M.turret = b.build()
end

----------------------------------------------------------------- sound

local tune, tune_i, tune_t
local function jingle(notes) tune, tune_i, tune_t = notes, 1, 0 end
local function update_jingle()
  if not tune then return end
  tune_t = tune_t - 1
  if tune_t > 0 then return end
  local n = tune[tune_i]
  if not n then tune = nil; return end
  if n[1] > 0 then note(3, n[1], n[2] * 14, SQUARE, 110) end
  tune_t, tune_i = n[2], tune_i + 1
end

-- a driving bass line on voice 4 while flying
local BASS = { 110, 110, 220, 110, 131, 131, 262, 131, 98, 98, 196, 98, 123, 147, 165, 196 }
local bass_step, bass_t, bass_on = 1, 0, false
local function update_bass()
  if not bass_on then return end
  bass_t = bass_t - 1
  if bass_t > 0 then return end
  bass_t = 8
  note(4, BASS[bass_step], 90, TRIANGLE, 120)
  bass_step = bass_step % #BASS + 1
end

local laser_slide = 0

----------------------------------------------------------------- game state

local state = "title"                    -- title, play, boss, clear, over
local ship, lasers, bolts, things, debris, marks
local score, best, shield, frame, speed, boost, shake, loop, run_t
local boss

local function reset_marks()
  marks = {}
  for i = 0, 35 do
    marks[#marks + 1] = { x = (i % 6) * 14 - 35 + (i // 6 % 2) * 7, z = (i // 6) * 26 }
  end
end

local function new_game()
  math.randomseed(stat(3))
  ship = { x = 0, y = 3, vx = 0, vy = 0, bank = 0, cool = 0 }
  lasers, bolts, things, debris = {}, {}, {}, {}
  score, shield, frame, speed, boost, shake, loop, run_t = 0, 100, 0, 1, 100, 0, 1, 0
  boss = nil
  reset_marks()
  state = "play"
  bass_on, bass_step, bass_t = true, 1, 0
  jingle({ { 392, 6 }, { 523, 6 }, { 659, 6 }, { 784, 16 } })
end

function _init()
  local data = saved()
  if data and data.best then best = data.best else best = 0 end
  make_meshes()
  envelope(2, 0, 50, 0, 30)             -- explosions die away
  envelope(4, 1, 30, 120, 12)           -- plucked bass
  reset_marks()
  ship = { x = 0, y = 3, vx = 0, vy = 0, bank = 0, cool = 0 }
  lasers, bolts, things, debris = {}, {}, {}, {}
  score, shield, frame, speed, boost, shake, loop, run_t = 0, 100, 0, 1, 100, 0, 1, 0
end

local function explode(x, y, z, big)
  for i = 1, big and 16 or 8 do
    debris[#debris + 1] = {
      x = x, y = y, z = z,
      vx = (math.random() - 0.5) * 0.8, vy = math.random() * 0.6, vz = (math.random() - 0.3) * 0.8,
      r = math.random() * 6, life = 25 + math.random(0, 15), s = big and 0.45 or 0.3,
      m = i % 2 == 0 and M.debris or M.debris2,
    }
  end
  note(2, big and 700 or 1800, big and 500 or 220, NOISE, big and 170 or 130)
end

local function spawn(kind, x, y, extra)
  local t = { kind = kind, x = x, y = y, z = FAR, hp = 1, t = 0, r = 1.4 }
  if kind == "dart" then t.hp, t.phase, t.r = 1, math.random() * 6, 1.6 end
  if kind == "ring" then t.r = 3.5 end
  for k, v in pairs(extra or {}) do t[k] = v end
  things[#things + 1] = t
end

-- the run: every 90 frames a new group, the mothership after 40 groups
local function level_script()
  run_t = run_t + 1
  if run_t % 90 ~= 0 then return end
  local g = run_t // 90
  if g >= 40 then
    if not boss then
      boss = { x = 0, y = 7, z = FAR, hp = 30 + loop * 10, turrets = {}, spin = 0, t = 0 }
      for i = 0, 3 do boss.turrets[i + 1] = { a = i * 1.5708, hp = 6 + loop * 2 } end
      state = "boss"
      bass_on = false
      jingle({ { 220, 10 }, { 0, 4 }, { 220, 10 }, { 0, 4 }, { 220, 10 }, { 0, 4 }, { 175, 30 } })
    end
    return
  end
  local r = g % 5
  if r == 0 then
    for i = 1, 3 + loop do spawn("tower", math.random(-14, 14), 0) end
  elseif r == 1 then
    local cx = math.random(-6, 6)
    for i = -2, 2 do spawn("dart", cx + i * 3, 4 + math.abs(i) * 0.8, { z = FAR + math.abs(i) * 6 }) end
  elseif r == 2 then
    spawn("gate", math.random(-4, 4), 0)
    spawn("ring", math.random(-5, 5), math.random(2, 6) + 0.0, { z = FAR + 40 })
  elseif r == 3 then
    for i = 1, 2 + loop do spawn("dart", math.random(-10, 10), math.random(2, 8) + 0.0, { z = FAR + i * 12 }) end
    spawn("tower", math.random(-14, 14), 0)
  else
    for i = 0, 3 do spawn("tower", i % 2 == 0 and -9 or 9, 0, { z = FAR + i * 18 }) end
    spawn("ring", 0, 4, { z = FAR + 30 })
  end
end

local function hurt(n)
  if state ~= "play" and state ~= "boss" then return end
  shield = shield - n
  shake = 12
  note(1, 90, 300, SAW, 140)
  note(2, 300, 300, NOISE, 150)
  if shield <= 0 then
    shield = 0
    explode(ship.x, ship.y, 0, true)
    state = "over"
    frame = 0
    bass_on = false
    if score > best then best = score; save({ best = best }) end
    jingle({ { 0, 30 }, { 392, 12 }, { 311, 12 }, { 262, 12 }, { 196, 40 } })
  end
end

local function fire()
  lasers[#lasers + 1] = { x = ship.x - 1.1, y = ship.y - 0.1, z = 1 }
  lasers[#lasers + 1] = { x = ship.x + 1.1, y = ship.y - 0.1, z = 1 }
  duty(0, 48)
  note(0, 1500, 110, SQUARE, 70)
  laser_slide = 7
end

local function near(ax, ay, az, bx, by, bz, r)
  local dx, dy, dz = ax - bx, ay - by, az - bz
  return dx * dx + dy * dy + dz * dz < r * r
end

----------------------------------------------------------------- update

local function update_ship()
  local ax, ay = 0, 0
  if btn(0) then ax = -0.09 end
  if btn(1) then ax = 0.09 end
  if btn(2) then ay = 0.07 end
  if btn(3) then ay = -0.07 end
  ship.vx = (ship.vx + ax) * 0.88
  ship.vy = (ship.vy + ay) * 0.88
  ship.x = math.max(-12, math.min(12, ship.x + ship.vx))
  ship.y = math.max(1, math.min(10, ship.y + ship.vy))
  ship.bank = ship.bank + (-ship.vx * 0.9 - ship.bank) * 0.15
  ship.cool = ship.cool - 1
  if btn(4) and ship.cool <= 0 then fire(); ship.cool = 7 end
  if btn(5) and boost > 0 then
    speed = math.min(2.0, speed + 0.08)
    boost = boost - 1.2
    if frame % 4 == 0 then note(5, 60 + speed * 40, 80, NOISE, 60) end
  else
    speed = speed + (1 - speed) * 0.08
    boost = math.min(100, boost + 0.3)
  end
end

local function update_world()
  for _, m in ipairs(marks) do
    m.z = m.z - speed
    if m.z < -12 then m.z = m.z + 36 * 26 / 6 end
  end

  for i = #lasers, 1, -1 do
    local l = lasers[i]
    l.z = l.z + 3.5
    if l.z > FAR + 20 then table.remove(lasers, i) end
  end

  for i = #bolts, 1, -1 do
    local b = bolts[i]
    b.x, b.y, b.z = b.x + b.vx, b.y + b.vy, b.z + b.vz - speed * 0.3
    if near(b.x, b.y, b.z, ship.x, ship.y, 0, 1.2) then
      table.remove(bolts, i); hurt(10)
    elseif b.z < -10 then
      table.remove(bolts, i)
    end
  end

  for i = #things, 1, -1 do
    local t = things[i]
    t.t = t.t + 1
    t.z = t.z - speed
    local dead = false
    if t.kind == "dart" then
      t.z = t.z - 0.4
      t.x = t.x + math.sin(t.t * 0.05 + t.phase) * 0.12
      t.y = math.max(1.5, t.y + math.cos(t.t * 0.04 + t.phase) * 0.05)
      if t.z > 30 and t.z < 110 and math.random() < 0.008 + 0.003 * loop then
        local dx, dy, dz = ship.x - t.x, ship.y - t.y, -t.z
        local d = math.sqrt(dx * dx + dy * dy + dz * dz)
        local v = 1.3
        bolts[#bolts + 1] = { x = t.x, y = t.y, z = t.z, vx = dx / d * v, vy = dy / d * v, vz = dz / d * v }
      end
    end
    -- lasers against ships
    if t.kind == "dart" then
      for j = #lasers, 1, -1 do
        local l = lasers[j]
        if near(l.x, l.y, l.z, t.x, t.y, t.z, t.r + 0.6) then
          table.remove(lasers, j)
          t.hp = t.hp - 1
          if t.hp <= 0 then dead = true; score = score + 100; explode(t.x, t.y, t.z, false) end
          break
        end
      end
    end
    -- the player against everything, when it passes by
    if not dead and math.abs(t.z) < 1.5 then
      if t.kind == "tower" then
        if math.abs(ship.x - t.x) < 1.9 and ship.y < 9.5 then hurt(20); dead = true; explode(t.x, 4, 0, true) end
      elseif t.kind == "gate" then
        local dx = math.abs(ship.x - t.x)
        if (dx > 3.6 and dx < 6.8) or (dx < 6.8 and ship.y > 7.2) then hurt(20); dead = true end
      elseif t.kind == "ring" then
        if near(ship.x, ship.y, 0, t.x, t.y, 0, 3.0) then
          shield = math.min(100, shield + 20)
          score = score + 50
          jingle({ { 1047, 3 }, { 1319, 3 }, { 1568, 3 }, { 2093, 8 } })
          dead = true
        end
      elseif t.kind == "dart" then
        if near(ship.x, ship.y, 0, t.x, t.y, t.z, 1.8) then hurt(15); dead = true; explode(t.x, t.y, t.z, false) end
      end
    end
    if dead or t.z < -15 then table.remove(things, i) end
  end

  for i = #debris, 1, -1 do
    local d = debris[i]
    d.x, d.y, d.z = d.x + d.vx, d.y + d.vy, d.z + d.vz - speed * 0.5
    d.vy = d.vy - 0.03
    d.r = d.r + 0.2
    d.life = d.life - 1
    if d.life <= 0 then table.remove(debris, i) end
  end
end

local function turret_pos(tu)
  local a = tu.a + boss.spin
  return boss.x + math.cos(a) * 6, boss.y + math.sin(a) * 6, boss.z - 1
end

local function update_boss()
  if not boss then return end
  boss.t = boss.t + 1
  if boss.z > 55 then boss.z = boss.z - speed * 0.8 end
  boss.x = math.sin(boss.t * 0.012) * 8
  boss.y = 7 + math.sin(boss.t * 0.021) * 2
  boss.spin = boss.spin + 0.01 + loop * 0.004
  local alive = 0
  for _, tu in ipairs(boss.turrets) do
    if tu.hp > 0 then
      alive = alive + 1
      local x, y, z = turret_pos(tu)
      if boss.z <= 56 and math.random() < 0.012 + loop * 0.004 then
        local dx, dy, dz = ship.x - x, ship.y - y, -z
        local d = math.sqrt(dx * dx + dy * dy + dz * dz)
        bolts[#bolts + 1] = { x = x, y = y, z = z, vx = dx / d * 1.1, vy = dy / d * 1.1, vz = dz / d * 1.1 }
        note(1, 330, 60, SAW, 70)
      end
      for j = #lasers, 1, -1 do
        local l = lasers[j]
        if near(l.x, l.y, l.z, x, y, z, 2.2) then
          table.remove(lasers, j)
          tu.hp = tu.hp - 1
          tu.hit = 4
          note(1, 220, 40, SQUARE, 90)
          if tu.hp <= 0 then score = score + 500; explode(x, y, z, true) end
          break
        end
      end
    end
  end
  boss.open = alive == 0
  if boss.open then
    for j = #lasers, 1, -1 do
      local l = lasers[j]
      if near(l.x, l.y, l.z, boss.x, boss.y, boss.z, 4.6) then
        table.remove(lasers, j)
        boss.hp = boss.hp - 1
        boss.hit = 4
        note(1, 160, 50, SQUARE, 100)
        if boss.hp <= 0 then
          score = score + 3000 * loop
          for k = 1, 4 do explode(boss.x + math.random(-3, 3), boss.y + math.random(-3, 3), boss.z, true) end
          boss = nil
          state = "clear"
          frame = 0
          jingle({ { 523, 8 }, { 523, 8 }, { 523, 8 }, { 659, 24 }, { 587, 8 }, { 659, 8 }, { 784, 40 } })
          return
        end
        break
      end
    end
  end
end

function _update()
  update_jingle()
  update_bass()
  if laser_slide > 0 then
    laser_slide = laser_slide - 1
    freq(0, 500 + laser_slide * 140)
  end
  frame = frame + 1
  if shake > 0 then shake = shake - 1 end

  if state == "title" then
    for _, m in ipairs(marks) do
      m.z = m.z - 0.6
      if m.z < -12 then m.z = m.z + 36 * 26 / 6 end
    end
    if btnp(4) then new_game() end
    return
  end
  if state == "over" then
    update_world()
    if frame > 60 and btnp(4) then state = "title" end
    return
  end
  if state == "clear" then
    update_ship()
    update_world()
    if frame > 240 then
      loop = loop + 1
      run_t = 0
      shield = math.min(100, shield + 50)
      state = "play"
      bass_on = true
    end
    return
  end
  update_ship()
  level_script()
  update_world()
  update_boss()
end

----------------------------------------------------------------- draw

local cam = { x = 0, y = 4, z = -12, roll = 0 }

local function center(s, y, c)
  local x = (W - #s * 8) // 16 * 8
  rectfill(x - 8, y - 4, #s * 8 + 16, 24, 0x000000)
  print(s, x, y, c)
end

-- sky, ground and a band of haze at the horizon, rolled with the camera
local function draw_backdrop()
  cls(SKY)
  local lx, ly = project3d(cam.x - 4000, 0, cam.z + 3000)
  local rx, ry = project3d(cam.x + 4000, 0, cam.z + 3000)
  if not lx or not rx then return end
  local k = (ry - ly) / (rx - lx)
  local x0, x1 = -20, W + 20
  local y0, y1 = ly + (x0 - lx) * k, ly + (x1 - lx) * k
  local low = H + 400
  tri(x0, y0 - 14, x1, y1 - 14, x0, y0, HAZE)
  tri(x1, y1 - 14, x1, y1, x0, y0, HAZE)
  tri(x0, y0, x1, y1, x0, low, GROUND)
  tri(x1, y1, x1, low, x0, low, GROUND)
end

local function draw_hud()
  print(string.format("SCORE %d", score), 12, 8, TEXT)
  print(string.format("RECORD %d", math.max(best, score)), 12, 28, score > best and ACCENT or DIM)
  print("SHIELD", W - 236, 8, TEXT)
  rect(W - 180, 8, 164, 14, TEXT)
  rectfill(W - 178, 10, math.floor(shield * 1.6), 10, shield > 30 and 0x40E080 or RED)
  print("BOOST", W - 228, 28, DIM)
  rect(W - 180, 30, 164, 8, DIM)
  rectfill(W - 178, 32, math.floor(boost * 1.6), 4, 0x40A0FF)
  if boss then
    local total = boss.hp
    for _, tu in ipairs(boss.turrets) do total = total + math.max(0, tu.hp) end
    print("MOTHERSHIP", W // 2 - 40, H - 28, RED)
    rectfill(W // 2 - 120, H - 10, math.min(240, total * 2), 4, RED)
  end
  if state == "play" and run_t < 120 and loop > 1 then center("LOOP " .. loop, 120, ACCENT) end
end

function _draw()
  -- the camera follows the ship loosely and leans into the turns
  local sx = (shake > 0) and (math.random() - 0.5) * shake * 0.05 or 0
  local sy = (shake > 0) and (math.random() - 0.5) * shake * 0.05 or 0
  cam.x = cam.x + (ship.x * 0.7 - cam.x) * 0.2
  cam.y = cam.y + (ship.y * 0.6 + 2.2 - cam.y) * 0.2
  cam.roll = cam.roll + (ship.bank * 0.2 - cam.roll) * 0.1
  camera3d(cam.x + sx, cam.y + sy, cam.z, 0, -0.06, 64, cam.roll)
  light3d(-0.3, 0.8, -0.5, 0.45)
  fog3d(HAZE, 70, FAR)

  draw_backdrop()
  zclear()
  for _, m in ipairs(marks) do
    if m.z < FAR then draw3d(M.mark, m.x, 0, m.z) end
  end

  if state == "title" then
    local t = frame * 0.03
    draw3d(M.ship, 0, 4, 2, 0.2 * math.sin(t), math.pi + t, 0.3 * math.sin(t * 1.3), 1.4)
    center("A S T R O   W I N G", 64, ACCENT)
    center("arrows steer   A fires   B boosts", 256, TEXT)
    center("fly through the gold rings to repair the shield", 280, DIM)
    center("press A to start", 312, ACCENT)
    if best > 0 then center("record " .. best, 96, TEXT) end
    return
  end

  for _, t in ipairs(things) do
    if t.kind == "tower" then draw3d(M.tower, t.x, 0, t.z)
    elseif t.kind == "gate" then draw3d(M.gate, t.x, 0, t.z)
    elseif t.kind == "ring" then draw3d(M.ring, t.x, t.y, t.z, 0, 0, t.t * 0.04)
    elseif t.kind == "dart" then draw3d(M.dart, t.x, t.y, t.z, 0, 0, math.sin(t.t * 0.05 + t.phase) * 0.6)
    end
  end
  if boss then
    local m = (boss.hit and boss.hit > 0) and M.core_hot or M.core
    if boss.hit and boss.hit > 0 then boss.hit = boss.hit - 1 end
    draw3d(boss.open and m or M.core, boss.x, boss.y, boss.z, 0, boss.t * 0.02, 0, 4, 4)  -- flag 4: smooth (Gouraud)
    for _, tu in ipairs(boss.turrets) do
      if tu.hp > 0 then
        local x, y, z = turret_pos(tu)
        draw3d(M.turret, x, y, z, 0, 0, boss.spin)
      end
    end
  end
  for _, l in ipairs(lasers) do draw3d(M.laser, l.x, l.y, l.z) end
  for _, b in ipairs(bolts) do draw3d(M.bolt, b.x, b.y, b.z, 0, frame * 0.3, 0, 0.5) end
  for _, d in ipairs(debris) do draw3d(d.m, d.x, d.y, d.z, d.r, d.r * 0.7, 0, d.s * d.life / 40) end
  if state ~= "over" and (shake == 0 or frame % 4 < 2) then
    draw3d(M.ship, ship.x, ship.y, 0, -ship.vy * 1.5, 0, ship.bank, 0.75)
  end

  draw_hud()
  if state == "clear" then
    center("MISSION COMPLETE", 130, ACCENT)
    center("bonus " .. 3000 * (loop), 160, TEXT)
  elseif state == "over" then
    center("GAME OVER", 130, RED)
    center("score " .. score .. "   record " .. best, 160, TEXT)
    if frame > 60 then center("press A", 190, TEXT) end
  end
end

---------------------------------------------------------------- pause

-- START while playing: the game stops, with RESUME, VOLUME (left/right;
-- the console keeps it for every game) and QUIT to the title.
local paused, psel = false, 1
local PAUSE_ROWS = { "RESUME", "VOLUME", "QUIT" }
local PLAYING = { play = true, boss = true, clear = true }
local game_update, game_draw = _update, _draw

function _update()
  if not paused then
    if PLAYING[state] and btnp(8) then paused, psel = true, 1 return end
    return game_update()
  end
  if btnp(8) or btnp(5) then paused = false return end
  if btnp(2) then psel = (psel + #PAUSE_ROWS - 2) % #PAUSE_ROWS + 1 end
  if btnp(3) then psel = psel % #PAUSE_ROWS + 1 end
  local row = PAUSE_ROWS[psel]
  if row == "VOLUME" and (btnp(0) or btnp(1) or btnp(4)) then
    local v = volume() + (btnp(0) and -1 or 1)
    if btnp(4) and v > 10 then v = 0 end
    volume(v)
    note(1, 660, 60, SQUARE, 110)               -- how loud it is now
  elseif btnp(4) then
    paused = false
    if row == "QUIT" then state = "title" end
  end
end

function _draw()
  game_draw()
  if not paused then return end
  local s = W >= 640 and 2 or 1
  local bw, bh = s == 2 and 208 or 140, 76 * s
  local x, y = (W - bw) // 2, (H - bh) // 2
  rectfill(x - 2, y - 2, bw + 4, bh + 4, 0xFFD050)
  rectfill(x, y, bw, bh, 0x101418)
  print("PAUSED", x + (bw - 48 * s) // 2, y + 4 * s, 0xFFD050, s)
  for i, r in ipairs(PAUSE_ROWS) do
    local ry = y + (14 + i * 14) * s
    local sel = i == psel
    if sel then rectfill(x + 4 * s, ry - s, bw - 8 * s, 10 * s, 0x2A3442) end
    print((sel and "> " or "  ") .. r, x + 6 * s, ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
    if r == "VOLUME" then
      print(tostring(volume()), x + (bw - 18 * s), ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
      for k = 1, 10 do
        rectfill(x + bw + (k * 3 - 54) * s, ry + (6 - math.min(k, 8) // 2) * s, 2 * s, (math.min(k, 8) // 2 + 2) * s,
                 k <= volume() and 0xFFD050 or 0x303844)
      end
    end
  end
end
