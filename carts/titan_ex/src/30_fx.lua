-- Titan Clash EX: Visual Effects System
-- Hit sparks, plasma projectiles, muzzle bursts, thruster flames,
-- debris, explosion pillars, and Hyper Super cinematic cut-ins.

local back, front = {}, {}
local alerts = {}
local super_fx = nil

local SEQ = {}
local function seq(name, n)
  if not SEQ[name] then
    local t = {}
    for k = 0, n - 1 do t[#t + 1] = name .. k end
    SEQ[name] = t
  end
  return SEQ[name]
end

local function add(list, p)
  list[#list + 1] = p
  return p
end

function Fx.clear()
  for i = #back, 1, -1 do back[i] = nil end
  for i = #front, 1, -1 do front[i] = nil end
  for i = #alerts, 1, -1 do alerts[i] = nil end
  super_fx = nil
end

function Fx.count() return #back + #front + #alerts end

local function anim(list, names, tick, x, h, flip, vx, vh, delay)
  return add(list, { kind = "anim", names = names, tick = tick, t = -(delay or 0), x = x, h = h,
                     flip = flip, vx = vx or 0, vh = vh or 0 })
end

---------------------------------------------------------------- hit sparks and explosions

function Fx.spark(x, h, kind)
  anim(front, seq(kind, 4), kind == "bigspark" and 3 or 2, x, h, random(2) == 1)
  local num_bits = kind == "bigspark" and 8 or 4
  for _ = 1, num_bits do
    add(front, { kind = "bit", x = x, h = h, vx = (random() - 0.5) * 9, vh = random() * 6,
                 c = kind == "bspark" and 0x78D8FF or (kind == "bigspark" and 0xFF7A1E or 0xFFE070),
                 t = 0, life = 12 + random(8) })
  end
end

function Fx.boom(x, h, delay)
  anim(front, seq("boom", 5), 3, x, h, random(2) == 1, 0, 0.35, delay)
end

function Fx.ko(f)
  for i = 0, 7 do
    Fx.boom(f.x + random(-45, 45), f.y + random(20, 160), i * 7)
  end
  for _ = 1, 16 do
    add(front, { kind = "bit", x = f.x, h = f.y + 80, vx = (random() - 0.5) * 12, vh = random() * 9,
                 c = choose({ 0xFF4A28, 0xFFB43C, 0xFFFFFF }), t = 0, life = 30 })
  end
end

---------------------------------------------------------------- text banners & combat alerts

function Fx.text_alert(txt, x, y, col, scale)
  alerts[#alerts + 1] = {
    txt = txt, x = x, y = y, col = col or 0xFFD040,
    scale = scale or 1, t = 0, max_t = 45, vy = -0.6
  }
end

function Fx.burst(f, c)
  for _ = 1, 10 do
    add(front, { kind = "bit", x = f.x, h = f.y + 110, vx = (random() - 0.5) * 8, vh = random() * 6,
                 c = c or 0x80E0FF, t = 0, life = 16 })
  end
end

function Fx.shoulder_off(f)
  Fx.boom(f.x - f.face * 20, f.y + 130)
  add(front, { kind = "armor_chunk", x = f.x - f.face * 20, h = f.y + 130,
               vx = -f.face * (3 + random() * 3), vh = 5 + random() * 4,
               heavy = f.cfg.armor == "heavy", p = f.team_num, t = 0 })
end

---------------------------------------------------------------- projectiles

function Fx.bullet(f, x, h)
  add(front, { kind = "bullet", f = f, x = x, h = h, vx = f.face * Data.BULLET.speed, t = 0 })
end

function Fx.muzzle(x, h, face)
  anim(front, seq("muzzle", 2), 2, x, h, face < 0)
end

function Fx.cannon_fire(f)
  local bx = f.x + f.face * 45
  local by = f.y + 110
  Fx.muzzle(bx, by, f.face)
  add(front, { kind = "cannon_ball", f = f, x = bx, h = by, vx = f.face * Data.CANNON_SHELL.speed, t = 0 })
end

---------------------------------------------------------------- boosters, dust, trails

function Fx.dust(x)
  anim(back, seq("dust", 4), 3, x - 18, 0, false)
  anim(back, seq("dust", 4), 3, x + 18, 0, true)
end

function Fx.flames(f)
  local flip = f.face < 0
  local bx = f.x - f.face * 18
  local by = f.y + 105
  anim(back, seq("flame", 3), 2, bx, by, flip)
  if f.C.id == "valkyrie" then
    -- wing booster jets
    anim(back, seq("flame", 3), 2, bx - f.face * 10, by + 25, flip)
  end
end

function Fx.super_jump_blast(f)
  Fx.dust(f.x)
  for i = 0, 2 do
    anim(back, seq("flame", 3), 3, f.x - 14, 0, false, 0, -1, i * 2)
    anim(back, seq("flame", 3), 3, f.x + 14, 0, true, 0, -1, i * 2)
  end
end

function Fx.smoke(x, h)
  add(back, { kind = "smoke", x = x, h = h, vx = (random() - 0.5) * 0.8, vh = 1.0 + random() * 0.6,
              r = 4, t = 0, max_t = 30 })
end

function Fx.trail(f, k)
  for _, p in ipairs(front) do
    if p.kind == "trail" and p.f == f then
      p.a1, p.t = k == 0 and 15 or -45, 0
      return
    end
  end
  add(front, { kind = "trail", f = f, a0 = 105, a1 = k == 0 and 15 or -45, t = 0, life = 14 })
end

---------------------------------------------------------------- cinematic super cut-in

function Fx.super_flash(f)
  super_fx = {
    f = f, t = 0, max_t = 28,
    name = f.C.super_name,
    pilot_id = f.C.pilot_id or 1
  }
end

function Fx.get_super_fx()
  return super_fx
end

---------------------------------------------------------------- update & draw

local function update_list(list, fighters)
  local j = 0
  for i = 1, #list do
    local p = list[i]
    p.t = p.t + 1
    local alive = true

    if p.kind == "anim" then
      if p.t >= #p.names * p.tick then alive = false end
      p.x = p.x + p.vx
      p.h = p.h + p.vh
    elseif p.kind == "bit" then
      p.x = p.x + p.vx
      p.h = p.h + p.vh
      p.vh = p.vh - 0.35
      if p.t >= p.life or p.h <= 0 then alive = false end
    elseif p.kind == "smoke" then
      p.x = p.x + p.vx
      p.h = p.h + p.vh
      p.r = p.r + 0.18
      if p.t >= p.max_t then alive = false end
    elseif p.kind == "bullet" then
      p.x = p.x + p.vx
      if p.x < G.camx - 40 or p.x > G.camx + W + 40 then alive = false end
      local target = fighters[1] == p.f and fighters[2] or fighters[1]
      if target and target.state ~= "tag_out" then
        local hx, hy = Fighter.hits(target, p.x - 9, p.h - 4, p.x + 9, p.h + 4)
        if hx then
          local stop = p.f.hitstop
          Fighter.take_hit(target, p.f, Data.BULLET, hx, hy)
          p.f.hitstop = stop
          alive = false
        end
      end
    elseif p.kind == "cannon_ball" then
      p.x = p.x + p.vx
      if p.x < G.camx - 40 or p.x > G.camx + W + 40 then alive = false end
      local target = fighters[1] == p.f and fighters[2] or fighters[1]
      if target and target.state ~= "tag_out" then
        local hx, hy = Fighter.hits(target, p.x - 16, p.h - 16, p.x + 16, p.h + 16)
        if hx then
          Fx.boom(hx, hy)
          Fighter.take_hit(target, p.f, Data.CANNON_SHELL, hx, hy)
          alive = false
        end
      end
    elseif p.kind == "armor_chunk" then
      p.x = p.x + p.vx
      p.h = p.h + p.vh
      p.vh = p.vh - 0.45
      if p.h <= 0 then
        p.h, p.vh = 0, -p.vh * 0.4
        p.vx = p.vx * 0.6
        if abs(p.vh) < 0.8 then p.vh = 0 end
      end
      if p.t > 120 then alive = false end
    elseif p.kind == "trail" then
      if p.t >= p.life then alive = false end
    end

    if alive then
      j = j + 1
      list[j] = p
    end
  end
  for i = #list, j + 1, -1 do list[i] = nil end
end

function Fx.update(fighters)
  update_list(back, fighters)
  update_list(front, fighters)

  -- update alerts
  local ja = 0
  for i = 1, #alerts do
    local a = alerts[i]
    a.t = a.t + 1
    a.y = a.y + a.vy
    if a.t < a.max_t then
      ja = ja + 1
      alerts[ja] = a
    end
  end
  for i = #alerts, ja + 1, -1 do alerts[i] = nil end

  if super_fx then
    super_fx.t = super_fx.t + 1
    if super_fx.t >= super_fx.max_t then super_fx = nil end
  end
end

---------------------------------------------------------------- drawing

local function draw_particle(p)
  local sx = p.x and floor(p.x - G.camx) or 0
  local sy = p.h and floor(GROUND - p.h + G.sy) or 0
  if p.kind == "anim" then
    if p.t >= 0 then
      local idx = clamp(p.t // p.tick + 1, 1, #p.names)
      sprite(p.names[idx], sx, sy, p.flip)
    end
  elseif p.kind == "bit" then
    rectfill(sx - 1, sy - 1, 2, 2, p.c)
  elseif p.kind == "smoke" then
    circfill(sx, sy, floor(p.r), p.t < 15 and 0x585864 or 0x303038)
  elseif p.kind == "bullet" then
    rectfill(sx - 6, sy - 1, 12, 3, 0xFFE070)
    rectfill(sx - 4, sy, 8, 1, 0xFFFFFF)
  elseif p.kind == "cannon_ball" then
    circfill(sx, sy, 7, 0xFF5A20)
    circfill(sx, sy, 5, 0xFFB43C)
    circfill(sx, sy, 3, 0xFFFFFF)
  elseif p.kind == "armor_chunk" then
    local name = p.heavy and "shd" or "sd"
    local fr = (p.p == 2 and FR2 or FR)[1]
    if fr and fr[name] then pieces(fr[name], sx, sy, p.vx < 0) end
  elseif p.kind == "trail" then
    local tf = p.f
    local fx, fy = floor(tf.x - G.camx), floor(GROUND - tf.y + G.sy)
    local ang0, ang1 = p.a0, p.a1
    for a = ang0, ang1, -10 do
      local rad = a * pi / 180
      local rx = fx + tf.face * cos(rad) * 60
      local ry = fy - sin(rad) * 60
      circfill(floor(rx), floor(ry), 4, 0x78F0FF)
    end
  end
end

function Fx.draw_back()
  for _, p in ipairs(back) do draw_particle(p) end
end

function Fx.draw_front()
  for _, p in ipairs(front) do draw_particle(p) end

  -- Draw combat floating alerts
  for _, a in ipairs(alerts) do
    local sx = floor(a.x - G.camx)
    local sy = floor(GROUND - a.y + G.sy)
    text_c(a.txt, sx, sy, a.col, a.scale)
  end

  -- Draw cinematic super cut-in banner
  if super_fx then
    local t = super_fx.t
    -- dark cinematic bars
    rectfill(0, 110, W, 70, 0x080C14)
    line(0, 110, W, 110, 0x58B4FF)
    line(0, 180, W, 180, 0x58B4FF)

    -- speedlines
    for l = 1, 6 do
      local ly = 115 + l * 9
      local lx = (t * 24 + l * 60) % W
      line(lx, ly, lx + 70, ly, 0x2A4468)
    end

    -- pilot portrait zoomed cut-in
    local px = clamp(t * 12 - 20, 20, 36)
    draw_pilot_face(super_fx.pilot_id, px, 125, false)

    -- super move name title
    text("HYPER MECHA OVERDRIVE!", 72, 122, 0xFFD040, 1)
    text(super_fx.name, 72, 138, 0xFFFFFF, 2)
  end
end
