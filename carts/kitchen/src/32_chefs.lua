-- The chefs: movement, dash, the A/X/Y actions, throwing, falling in the
-- water, and the state the animations read. A chef's input comes from a
-- controller (Chef.read_pad) or from a computer chef (Bot).

local RADIUS = 0.28
local GRAV = 22

function Chef.new(run, n, pad, chef_i)
  local sp = run.spawns[n]
  local def = Data.CHEFS[chef_i]
  local c = {
    n = n, pad = pad, def = def, ci = chef_i,
    x = sp[1], z = sp[2], y = 0, vx = 0, vz = 0, yaw = 0,
    hold = nil, dash_t = 0, dash_cd = 0, stun = 0, fall = nil, slip = 0,
    inp = { mx = 0, mz = 0 },
    walk = 0, speed = 0, act = nil, act_t = 0, blink = random() * 4,
    work = nil, target = nil, say = nil, say_t = 0, spray = false,
    idle_t = 0, face_t = 0,
  }
  return c
end

function Chef.read_pad(c)
  local p = c.pad
  local x, y = stick(p)
  local inp = c.inp
  inp.mx, inp.mz = x, -y
  inp.a = btnp(BA, p)
  inp.b = btnp(BB, p)
  inp.x = btnp(BX, p)
  inp.xh = btn(BX, p)
  inp.y = btnp(BY, p)
end

function Chef.say(c, text, t)
  c.say, c.say_t = text, t or 1.2
end

local function set_act(c, act, t)
  c.act, c.act_t = act, t or 0.3
end
Chef.set_act = set_act

---------------------------------------------------------------- targets

local probes = { { 0, 0.72 }, { 0.55, 0.68 }, { -0.55, 0.68 }, { 0.95, 0.6 }, { -0.95, 0.6 } }

-- The station in front of the chef (what A and X act on), or nil.
function Chef.find_target(run, c)
  local yaw = c.yaw
  for i = 1, #probes do
    local a, d = yaw + probes[i][1], probes[i][2]
    local st = Kit.station_at(run, c.x + sin(a) * d, c.z + cos(a) * d)
    if st then return st end
  end
  return nil
end

---------------------------------------------------------------- movement

local samples = {}
for i = 0, 7 do samples[#samples + 1] = { sin(i * TAU / 8) * RADIUS, cos(i * TAU / 8) * RADIUS } end

local function free(run, x, z)
  for i = 1, #samples do
    local s = samples[i]
    if Kit.solid_at(run, x + s[1], z + s[2]) then return false end
  end
  return true
end
Chef.free = free

local function move(run, c, dx, dz)
  if dx ~= 0 then
    if free(run, c.x + dx, c.z) then c.x = c.x + dx else c.vx = 0 end
  end
  if dz ~= 0 then
    if free(run, c.x, c.z + dz) then c.z = c.z + dz else c.vz = 0 end
  end
end

-- chefs push each other apart
local function separate(chefs)
  for i = 1, #chefs do
    local a = chefs[i]
    for j = i + 1, #chefs do
      local b = chefs[j]
      if not a.fall and not b.fall then
        local dx, dz = b.x - a.x, b.z - a.z
        local d2 = dx * dx + dz * dz
        local r = RADIUS * 2
        if d2 < r * r and d2 > 1e-6 then
          local d = sqrt(d2)
          local push = (r - d) * 0.5
          dx, dz = dx / d * push, dz / d * push
          local run = G.run
          if free(run, a.x - dx, a.z - dz) then a.x, a.z = a.x - dx, a.z - dz end
          if free(run, b.x + dx, b.z + dz) then b.x, b.z = b.x + dx, b.z + dz end
        end
      end
    end
  end
end
Chef.separate = separate

---------------------------------------------------------------- actions

local function can_throw(it)
  return it and not it.dirty and it.key ~= "ext"
end

local function throw(run, c)
  local it = c.hold
  if not c.def.throw then
    set_act(c, "shrug", 0.5)
    Chef.say(c, "I can't throw!", 0.9)
    Snd.nope()
    return
  end
  if not can_throw(it) then Snd.nope() return end
  c.hold = nil
  local range = Data.THROW_RANGE * Food.speed(run, "throw")
  local fx, fz = sin(c.yaw), cos(c.yaw)
  local tflight = 0.55
  local v = range / tflight
  local l = Kit.drop(run, it, c.x + fx * 0.3, c.z + fz * 0.3, 1.0,
                     fx * v + c.vx * 0.3, GRAV * tflight * 0.5 - 0.3 / tflight, fz * v + c.vz * 0.3, c)
  l.spin = random() * 6
  set_act(c, "throw", 0.3)
  Snd.throw()
  G.stats_add("thrown", 1)
end

local function drop_front(run, c)
  local fx, fz = sin(c.yaw), cos(c.yaw)
  local x, z = c.x + fx * 0.45, c.z + fz * 0.45
  if Kit.solid_at(run, x, z) then x, z = c.x, c.z end
  Kit.drop(run, c.hold, x, z, 0.6, 0, 0, 0)
  c.hold = nil
  local l = run.loose[#run.loose]
  l.fly, l.vx, l.vy, l.vz = true, 0, 0, 0
  Snd.drop()
end

local function press_a(run, c)
  if Dis.chef_press(run, c) then set_act(c, "pick", 0.22) return end
  local st = c.target
  if st then
    local ok, why = Food.interact(run, c, st)
    if ok then
      set_act(c, "pick", 0.22)
      st.bump = max(st.bump, 0.12)
      if not c.last_sound or c.last_sound < G.t then Snd.pick() end
    else
      Snd.nope()
      if why then Chef.say(c, why, 0.9) end
    end
    return
  end
  -- nothing in front: the floor
  local fx, fz = sin(c.yaw), cos(c.yaw)
  local it = Kit.loose_near(run, c.x + fx * 0.3, c.z + fz * 0.3, 0.75)
  if it and not c.hold then
    c.hold = it.item
    it.taken = true
    Kit.remove_loose(run, it)
    set_act(c, "pick", 0.22)
    Snd.pick()
  elseif it and c.hold and c.hold.plate and Food.is_food(it.item) and Food.plate_ok(run, c.hold.parts, it.item.key) then
    c.hold.parts[#c.hold.parts + 1] = it.item.key
    Kit.remove_loose(run, it)
    Snd.pick()
  elseif c.hold then
    drop_front(run, c)
    set_act(c, "pick", 0.2)
  end
end

local function press_x(run, c)
  if c.hold and c.hold.key == "ext" then return end    -- spraying: held, see update
  local st = c.target
  if not st then return end
  local kind = Food.work_kind(run, c, st)
  if kind then
    c.work = { st = st, kind = kind }
  else
    Snd.nope()
  end
end

---------------------------------------------------------------- update

local function fall_in(run, c)
  c.fall = 0
  if c.hold then
    Fx.splash(c.x, c.z)
    c.hold = nil
  end
  c.work = nil
  Snd.splash()
  Chef.say(c, "SPLASH!", 1.0)
  G.stats_add("falls", 1)
end

function Chef.update(run, c, dt)
  local inp = c.inp
  c.act_t = c.act_t - dt
  if c.act_t <= 0 then c.act = nil end
  c.say_t = c.say_t - dt
  c.blink = c.blink - dt
  if c.blink < 0 then c.blink = 2.5 + random() * 3 end
  if c.panel then return Chef.idle_anim(c, dt) end        -- at the register (endless)

  -- in the water: sink, then come back at the spawn point
  if c.fall then
    c.fall = c.fall + dt
    c.y = -c.fall * 1.2
    if c.fall > 2.2 then
      local sp = run.spawns[c.n]
      c.x, c.z, c.y, c.vx, c.vz, c.fall = sp[1], sp[2], 0, 0, 0, nil
      Fx.puff(c.x, 0.5, c.z, 0xFFFFFF, 5)
    end
    return
  end

  -- stunned (tornado, steam): spin in place
  if c.stun > 0 then
    c.stun = c.stun - dt
    c.yaw = c.yaw + dt * 14
    c.vx, c.vz = c.vx * 0.9, c.vz * 0.9
    move(run, c, c.vx * dt, c.vz * dt)
    return
  end

  local cell, plat = Kit.cell_at(run, c.x, c.z)
  local ice = cell.kind == "ice" or cell.wet
  -- ride the platform under the chef
  if plat then move(run, c, plat.vx * dt, plat.vz * dt) end

  -- steering
  local mx, mz = inp.mx or 0, inp.mz or 0
  local m = sqrt(mx * mx + mz * mz)
  if m > 1 then mx, mz, m = mx / m, mz / m, 1 end
  local spd = Data.BASE_SPEED * c.def.speed * Food.speed(run, "move")
  if c.work then spd = 0 end
  local tvx, tvz = mx * spd, mz * spd

  if c.dash_cd > 0 then c.dash_cd = c.dash_cd - dt end
  if c.dash_t > 0 then
    c.dash_t = c.dash_t - dt
    local ds = Data.DASH_SPEED * (c.def.speed > 1 and 1.05 or 1)
    tvx, tvz = sin(c.yaw) * ds, cos(c.yaw) * ds
    if random() < 0.5 then Fx.dust(c.x, c.z) end
  elseif inp.b then
    if c.def.dash then
      c.dash_t, c.dash_cd = Data.DASH_TIME, Data.DASH_COOL
      c.work = nil
      set_act(c, "dash", 0.25)
      Snd.dash()
    else
      -- Bun cannot dash: a little belly hop instead
      if c.act ~= "hop" then set_act(c, "hop", 0.35) Snd.boing() end
    end
  end

  local acc = ice and 7 or (c.dash_t > 0 and 90 or 42)
  c.vx = approach(c.vx, tvx, acc * dt)
  c.vz = approach(c.vz, tvz, acc * dt)
  if ice and m < 0.1 and c.dash_t <= 0 then
    c.vx, c.vz = c.vx * (1 - dt * 0.6), c.vz * (1 - dt * 0.6)
  end
  move(run, c, c.vx * dt, c.vz * dt)
  c.speed = sqrt(c.vx * c.vx + c.vz * c.vz)

  if m > 0.2 then
    local target = atan(mx, mz)
    local d = angdiff(c.yaw, target)
    local turn = 18 * dt
    c.yaw = c.yaw + clamp(d, -turn, turn)
    c.work = nil
    c.idle_t = 0
  else
    c.idle_t = c.idle_t + dt
  end
  c.walk = c.walk + c.speed * dt * (2.2 / max(0.6, c.def.height))

  -- off the edge: into the water
  local under = Kit.cell_at(run, c.x, c.z)
  if under.kind == "void" then return fall_in(run, c) end

  c.target = Chef.find_target(run, c)

  -- actions
  if inp.a then c.work = nil; press_a(run, c) end
  if inp.x then press_x(run, c) end
  if inp.y and c.hold then throw(run, c) end
  if inp.y and not c.hold and not c.def.throw then
    set_act(c, "shrug", 0.4)
  end

  -- the extinguisher sprays while X is held
  c.spray = c.hold and c.hold.key == "ext" and inp.xh
  if c.spray then Chef.spray(run, c, dt) end

  -- chopping / washing continues until done or the chef walks away
  if c.work then
    local w = c.work
    if c.target ~= w.st or not Food.work(run, c, w.st, w.kind, dt) then
      c.work = nil
    else
      set_act(c, w.kind, 0.1)
      c.chop_snd = (c.chop_snd or 0) - dt
      if c.chop_snd <= 0 then
        c.chop_snd = w.kind == "chop" and 0.22 or 0.35
        if w.kind == "chop" then Snd.chop() else Snd.scrub() end
      end
    end
  end
end

function Chef.idle_anim(c, dt)
  c.speed = 0
  c.vx, c.vz = 0, 0
end

-- the extinguisher: puts out fires in front of the chef
function Chef.spray(run, c, dt)
  local fx, fz = sin(c.yaw), cos(c.yaw)
  for d = 0.6, 2.2, 0.8 do
    local st = Kit.station_at(run, c.x + fx * d, c.z + fz * d)
    if st and st.fire > 0 then
      st.fire = st.fire - dt * 0.9
      if st.fire <= 0 then
        st.fire = 0
        st.spread = nil
        Snd.whoosh()
        Fx.puff(st.x, Kit.TOP + 0.3, st.z, 0xFFFFFF, 6)
        Fx.text(st.x, st.z, "OUT!", 0x80E0FF)
      end
    end
  end
  Dis.spray(run, c, fx, fz, dt)
  if random() < 0.6 then Fx.spray(c.x + fx * 0.5, c.z + fz * 0.5, fx, fz) end
  c.spray_snd = (c.spray_snd or 0) - dt
  if c.spray_snd <= 0 then c.spray_snd = 0.15; Snd.spray() end
end

---------------------------------------------------------------- flying items

function Chef.update_loose(run, dt)
  local chefs = run.chefs
  for i = #run.loose, 1, -1 do
    local l = run.loose[i]
    l.t = l.t + dt
    if l.fly then
      l.x = l.x + l.vx * dt
      l.z = l.z + l.vz * dt
      l.y = l.y + l.vy * dt
      l.vy = l.vy - GRAV * dt
      l.spin = (l.spin or 0) + dt * 12
      local done = false
      -- a chef with free hands catches it
      for _, c in ipairs(chefs) do
        if not c.hold and not c.fall and (c ~= l.from or l.t > 0.35) and l.y > 0.3 and l.y < 1.8 then
          if dist2(c.x, c.z, l.x, l.z) < 0.36 then
            c.hold = l.item
            set_act(c, "catch", 0.3)
            Snd.catch()
            Fx.text(c.x, c.z, "CATCH!", PCOL[c.pad] or 0xFFFFFF)
            G.stats_add("caught", 1)
            remove(run.loose, i)
            done = true
            break
          end
        end
      end
      if not done then
        local st = Kit.station_at(run, l.x, l.z)
        local cell = Kit.cell_at(run, l.x, l.z)
        if st then
          if l.y <= Kit.TOP + 0.15 then
            if Food.catch(run, st, l.item) then
              remove(run.loose, i)
              Fx.puff(st.x, Kit.TOP + 0.2, st.z, 0xFFFFFF, 2)
              Snd.land()
            else
              -- bounces back off the station
              l.x, l.z = l.x - l.vx * dt * 2, l.z - l.vz * dt * 2
              l.vx, l.vz = -l.vx * 0.25, -l.vz * 0.25
              l.vy = max(l.vy, 1.5)
              Snd.bump()
            end
          end
        elseif cell.kind == "wall" then
          l.x, l.z = l.x - l.vx * dt * 2, l.z - l.vz * dt * 2
          l.vx, l.vz = -l.vx * 0.3, -l.vz * 0.3
          Snd.bump()
        elseif l.y <= 0 then
          if cell.kind == "void" then
            Fx.splash(l.x, l.z)
            Snd.splash()
            remove(run.loose, i)
          elseif abs(l.vy) > 2 then
            l.y = 0
            l.vy = -l.vy * 0.3
            l.vx, l.vz = l.vx * 0.5, l.vz * 0.5
            Snd.land()
          else
            l.y, l.vx, l.vy, l.vz, l.fly = 0, 0, 0, 0, false
          end
        end
      end
    else
      -- on the floor: ride platforms, fall in the water if one leaves
      local cell, plat = Kit.cell_at(run, l.x, l.z)
      if plat then l.x, l.z = l.x + plat.vx * dt, l.z + plat.vz * dt end
      if cell.kind == "void" then
        Fx.splash(l.x, l.z)
        remove(run.loose, i)
      end
    end
  end
end
