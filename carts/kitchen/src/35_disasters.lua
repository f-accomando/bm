-- Disasters: absurd, never deadly, always announced and always fixable.
--   rats         steal food and hide in crates; A near them or a dash scares
--                them (they drop what they carry)
--   ducks        waddle through in a line and eat chopped food; a dash
--                scatters them
--   leak         a pipe bursts, the floor gets slippery; hold X at the valve
--   ghosts       the lights go out (the chefs carry the light), crates pop
--                open, chefs get a fright; the extinguisher blows them away
--   possessed    a knife haunts a board, a pan rattles and will spill; A calms
--   poltergeist  food floats from one counter to another
--   runaway      raw ingredients jump off the counters and roll away
--   tornado      a tiny twister: spins chefs, scatters loose food
-- stage.dis = { { kind = "rats", at = 30, every = 45 }, ... }

local TOP = Kit.TOP

local NAMES = {
  rats = "RATS!", ducks = "DUCKS!", leak = "PIPE BURST!", ghosts = "GHOSTS!",
  possessed = "HAUNTED UTENSILS!", poltergeist = "POLTERGEIST!", runaway = "RUNAWAY FOOD!",
  tornado = "TORNADO!",
}
local ICON = { rats = "rat", ducks = "duck", leak = "drop", ghosts = "ghost", possessed = "knife2",
               poltergeist = "ghost", runaway = "arrow", tornado = "tornado" }

function Dis.init(run)
  run.dis = {}
  for _, d in ipairs(run.stage.dis or {}) do
    run.dis[#run.dis + 1] = { kind = d.kind, t = d.at or 30, every = d.every }
  end
  run.rats, run.ducks, run.ghosts, run.tornado = {}, {}, {}, nil
  run.dark, run.banner, run.leak = 0, nil, nil
end

---------------------------------------------------------------- helpers

local function floor_cells(run, test)
  local out = {}
  for cz = 0, run.h - 1 do
    for cx = 0, run.w - 1 do
      local c = Kit.cell(run, cx, cz)
      if not Kit.blocked(c) and c.kind ~= "void" and (not test or test(cx, cz, c)) then
        out[#out + 1] = { cx + 0.5, cz + 0.5 }
      end
    end
  end
  return out
end

-- a floor cell next to a wall (holes for rats, doors for ducks)
local function edge_cells(run)
  return floor_cells(run, function(cx, cz)
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      if Kit.cell(run, cx + d[1], cz + d[2]).kind == "wall" then return true end
    end
    return false
  end)
end

local function food_stations(run, test)
  local out = {}
  for _, st in ipairs(run.stations) do
    local it = st.item
    if it and Food.is_food(it) and (st.def.hold or st.kind == "crate") and (not test or test(st, it)) then
      out[#out + 1] = st
    end
  end
  return out
end

local function move_to(run, e, tx, tz, speed, dt, radius)
  local dx, dz = tx - e.x, tz - e.z
  local d = sqrt(dx * dx + dz * dz)
  if d < 0.05 then return true end
  local step = min(d, speed * dt)
  local nx, nz = e.x + dx / d * step, e.z + dz / d * step
  if radius then
    if not Kit.solid_at(run, nx, e.z) then e.x = nx end
    if not Kit.solid_at(run, e.x, nz) then e.z = nz end
  else
    e.x, e.z = nx, nz
  end
  e.yaw = atan(dx, dz)
  return d < 0.15
end

local function banner(run, kind)
  run.banner = { text = NAMES[kind], icon = Hud.IC[ICON[kind]] or Hud.IC.skull, t = 2.2 }
  Snd.alarm()
  Ren.shake(0.2)
end

---------------------------------------------------------------- starting

local start = {}

function start.rats(run)
  local holes = edge_cells(run)
  if #holes == 0 then return end
  local n = clamp(#run.chefs, 1, 3) + 1
  for _ = 1, n do
    local h = choose(holes)
    run.rats[#run.rats + 1] = { x = h[1], z = h[2], hx = h[1], hz = h[2], yaw = 0, state = "seek",
                                t = 22 + random() * 6, carry = nil, target = nil, run = 0 }
  end
end

function start.ducks(run)
  if #run.ducks > 0 then return end             -- one flock at a time
  local edges = edge_cells(run)
  if #edges == 0 then return end
  local e = choose(edges)
  local path = {}
  local cells = floor_cells(run)
  for _ = 1, 4 do path[#path + 1] = choose(cells) end
  path[#path + 1] = e
  local n = clamp(#run.chefs + 2, 3, 5)
  for i = 1, n do
    run.ducks[#run.ducks + 1] = { x = e[1], z = e[2], yaw = 0, i = i, path = path, p = 1, delay = (i - 1) * 0.6,
                                  size = 1, flee = 0, waddle = random() * 6, life = 40, stuck = 0 }
  end
end

function start.leak(run)
  if run.leak then return end
  -- a wall cell of the back wall above a floor cell
  local cands = {}
  for cx = 1, run.w - 2 do
    for cz = run.h - 1, 1, -1 do
      local c = Kit.cell(run, cx, cz)
      local below = Kit.cell(run, cx, cz - 1)
      if c.kind == "wall" and not c.st and not Kit.blocked(below) then cands[#cands + 1] = { cx, cz } break end
    end
  end
  if #cands == 0 then return end
  local p = choose(cands)
  local c = Kit.cell(run, p[1], p[2])
  local st = Kit.add_station(run, "valve", p[1], p[2])
  st.leak = true
  st.prog = 0
  c.st = st
  run.leak = { st = st, cell = c, cx = p[1], cz = p[2] - 1, t = 0, wet = {}, fixed = false }
end

function start.ghosts(run)
  local cells = floor_cells(run)
  for _ = 1, clamp(#run.chefs, 1, 2) do
    local p = choose(cells)
    run.ghosts[#run.ghosts + 1] = { x = p[1], z = p[2], y = 1.4, tx = p[1], tz = p[2], t = 20,
                                    act = 3 + random() * 2, yaw = 0, fade = 0 }
  end
  run.dark = 11
end

function start.possessed(run)
  local boards, pans = {}, {}
  for _, st in ipairs(run.stations) do
    if st.kind == "board" and not st.item and not st.possessed then boards[#boards + 1] = st end
    if st.kind == "pan" and not st.possessed then pans[#pans + 1] = st end
  end
  if #boards > 0 then choose(boards).possessed = { kind = "knife", t = 0 } end
  if #pans > 0 then
    local st = choose(pans)
    st.possessed = { kind = "pan", t = 0, spill = 9 }
  end
end

function start.poltergeist(run)
  local src = food_stations(run)
  local dst = {}
  for _, st in ipairs(run.stations) do
    if st.kind == "counter" and not st.item then dst[#dst + 1] = st end
  end
  shuffle(src)
  shuffle(dst)
  for i = 1, min(3, #src, #dst) do
    local a, b = src[i], dst[i]
    local it = a.item
    a.item = nil
    -- a slow float to the other counter (the throw code lands it there)
    local tf = 1.1
    local dx, dz = b.x - a.x, b.z - a.z
    -- 0.3 above the counter now, back on a counter after tf seconds (gravity 22)
    local l = Kit.drop(run, it, a.x, a.z, TOP + 0.3, dx / tf, (-0.3 + 11 * tf * tf) / tf, dz / tf, nil)
    l.ghost = true
    Fx.puff(a.x, TOP + 0.4, a.z, 0xC8B8FF, 5)
  end
  Snd.boo()
end

function start.runaway(run)
  local list = food_stations(run, function(st, it)
    local k = Food.parse(it.key)
    return k.ing and not k.chopped and st.kind ~= "board"
  end)
  -- crates cough one up too
  for _, st in ipairs(run.stations) do
    if st.kind == "crate" and random() < 0.25 then list[#list + 1] = st end
  end
  shuffle(list)
  for i = 1, min(3, #list) do
    local st = list[i]
    local it = st.item
    if st.kind == "crate" and not it then it = Food.new_ing(st.ing) else st.item = nil end
    if it then
      local a = random() * TAU
      local l = Kit.drop(run, it, st.x, st.z, TOP + 0.1, 0, 0, 0)
      -- off the counter, then rolling on the floor
      local d = 0.8
      l.x, l.z = st.x + sin(a) * d, st.z + cos(a) * d
      if Kit.solid_at(run, l.x, l.z) then l.x, l.z = st.x - sin(a) * d, st.z - cos(a) * d end
      l.fly, l.vx, l.vy, l.vz = true, sin(a) * 1.5, 3, cos(a) * 1.5
      l.roll = 3
      l.eyes = true
      Fx.puff(st.x, TOP + 0.2, st.z, 0xFFFFFF, 3)
    end
  end
  Snd.boing()
end

function start.tornado(run)
  local cells = floor_cells(run)
  if #cells == 0 then return end
  local p = choose(cells)
  run.tornado = { x = p[1], z = p[2], tx = p[1], tz = p[2], t = 14, spin = 0, snd = 0 }
end

-- starts a disaster now (stages use their timetable; endless calls it)
function Dis.trigger(run, kind)
  if not start[kind] then return end
  start[kind](run)
  banner(run, kind)
  G.stats_add("disasters", 1)
end

---------------------------------------------------------------- updating

local function scare_rat(run, r)
  if r.state == "flee" then return end
  if r.carry then
    Kit.drop(run, r.carry, r.x, r.z, 0.3, 0, 1, 0)
    local l = run.loose[#run.loose]
    l.fly = true
    r.carry = nil
  end
  if r.crate then r.crate.rat = nil r.crate = nil end
  r.state = "flee"
  Snd.squeak()
  Fx.text(r.x, r.z, "SQUEAK!", 0xE0E0E0)
end

local function update_rats(run, dt)
  for i = #run.rats, 1, -1 do
    local r = run.rats[i]
    r.t = r.t - dt
    r.run = r.run + dt * 12
    -- chefs frighten them: very close, or dashing near
    for _, c in ipairs(run.chefs) do
      local d2 = dist2(c.x, c.z, r.x, r.z)
      if d2 < 0.3 or (c.dash_t > 0 and d2 < 1.5) then scare_rat(run, r) end
    end
    if r.t <= 0 and r.state ~= "flee" then scare_rat(run, r) end
    if r.state == "seek" then
      -- the nearest food on the floor, on a counter, or a crate to hide in
      if not r.target or r.retarget_t == nil or r.retarget_t <= 0 then
        r.retarget_t = 1.5
        local best, bd = nil, 1e9
        for _, l in ipairs(run.loose) do
          if not l.fly and Food.is_food(l.item) then
            local d = dist2(l.x, l.z, r.x, r.z)
            if d < bd then best, bd = { loose = l, x = l.x, z = l.z }, d end
          end
        end
        for _, st in ipairs(run.stations) do
          local ok = (st.item and Food.is_food(st.item) and st.def.hold) or
                     (st.kind == "crate" and not st.rat and r.lazy)
          if ok and not st.plat then
            local d = dist2(st.x, st.z, r.x, r.z)
            if d < bd then best, bd = { st = st, x = st.x, z = st.z }, d end
          end
        end
        -- nothing to steal: scurry about (and now and then hide in a crate)
        if not best then
          local cells = floor_cells(run)
          local p = choose(cells)
          best = { x = p[1], z = p[2] }
          r.lazy = random() < 0.25
        end
        r.target = best
      end
      r.retarget_t = r.retarget_t - dt
      local tg = r.target
      if tg then
        local arrived = move_to(run, r, tg.x, tg.z, 2.6, dt, true)
        if not arrived and tg.st and dist2(r.x, r.z, tg.x, tg.z) < 0.55 then arrived = true end
        if arrived then
          if tg.loose and not tg.loose.taken then
            Kit.remove_loose(run, tg.loose)
            r.carry = tg.loose.item
            r.state = "home"
          elseif tg.st and tg.st.item and Food.is_food(tg.st.item) and tg.st.def.hold then
            r.carry = tg.st.item
            tg.st.item = nil
            r.state = "home"
          elseif tg.st and tg.st.kind == "crate" and not tg.st.rat then
            tg.st.rat = r
            r.crate = tg.st
            r.state = "hide"
            r.hide_t = 10
          end
          if r.carry then Snd.squeak() end
          r.target = nil
        end
      end
    elseif r.state == "hide" then
      r.hide_t = r.hide_t - dt
      if r.hide_t <= 0 then r.crate.rat = nil r.crate = nil r.state = "seek" end
    elseif r.state == "home" or r.state == "flee" then
      if move_to(run, r, r.hx, r.hz, r.state == "flee" and 4 or 2.4, dt, true) then
        if r.carry then G.stats_add("stolen", 1) Fx.text(r.x, r.z, "STOLEN!", 0xFF8080) end
        if r.crate then r.crate.rat = nil end
        remove(run.rats, i)
      end
    end
  end
end

local function update_ducks(run, dt)
  for i = #run.ducks, 1, -1 do
    local d = run.ducks[i]
    d.waddle = d.waddle + dt * 10
    if d.delay > 0 then d.delay = d.delay - dt goto continue end
    -- they never stay for long, even if a path is blocked
    d.life = d.life - dt
    if d.life <= 0 then
      Fx.puff(d.x, 0.2, d.z, 0xFFFFFF, 4)
      remove(run.ducks, i)
      goto continue
    end
    -- a dash scatters the whole flock
    for _, c in ipairs(run.chefs) do
      if c.dash_t > 0 and dist2(c.x, c.z, d.x, d.z) < 1.4 and d.flee <= 0 then
        for _, o in ipairs(run.ducks) do o.flee = 3; o.p = #o.path end
        Snd.quack()
        Fx.text(d.x, d.z, "QUACK!", 0xFFF0A0)
      end
    end
    do
      local tgt = d.path[d.p]
      local speed = d.flee > 0 and 3.5 or 1.3
      if d.flee > 0 then d.flee = d.flee - dt end
      local ox, oz = d.x, d.z
      local there = move_to(run, d, tgt[1] + (d.i - 1) * 0.12, tgt[2], speed, dt, true)
      -- against a counter: give up on this point after a moment
      if abs(d.x - ox) + abs(d.z - oz) < speed * dt * 0.2 then d.stuck = d.stuck + dt else d.stuck = 0 end
      if there or d.stuck > 1.5 then
        d.p, d.stuck = d.p + 1, 0
        if d.p > #d.path then remove(run.ducks, i) goto continue end
      end
      -- a chopped piece on a counter nearby: gulp
      if random() < dt * 1.5 then
        for _, st in ipairs(run.stations) do
          local it = st.item
          if it and it.key and st.def.hold and dist2(st.x, st.z, d.x, d.z) < 1.1 then
            local k = Food.parse(it.key)
            if k.ing and k.chopped then
              st.item = nil
              d.size = min(1.6, d.size + 0.2)
              Snd.quack()
              Fx.text(d.x, d.z, "GULP", 0xFFF0A0)
              G.stats_add("eaten", 1)
              break
            end
          end
        end
      end
      if random() < dt * 0.4 then Snd.quack() end
      -- they get in the way: chefs walk around them
      for _, c in ipairs(run.chefs) do
        local dx, dz = c.x - d.x, c.z - d.z
        local d2 = dx * dx + dz * dz
        if d2 < 0.2 and d2 > 1e-4 then
          local k = (0.45 - sqrt(d2)) * 0.5
          local l = sqrt(d2)
          if Chef.free(run, c.x + dx / l * k, c.z + dz / l * k) then c.x, c.z = c.x + dx / l * k, c.z + dz / l * k end
        end
      end
    end
    ::continue::
  end
end

local function update_leak(run, dt)
  local L = run.leak
  if not L then return end
  L.t = L.t + dt
  if not L.fixed then
    if random() < 0.6 then Fx.splash(L.cx + 0.5, L.cz + 0.8) end
    -- the puddle grows one cell at a time
    if L.t > 1.2 and #L.wet < 14 then
      L.t = 0
      local cands = {}
      local start_c = Kit.cell(run, L.cx, L.cz)
      if not start_c.wet then cands[1] = { L.cx, L.cz } end
      for _, w in ipairs(L.wet) do
        for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          local c = Kit.cell(run, w[1] + d[1], w[2] + d[2])
          if not Kit.blocked(c) and not c.wet and c.kind ~= "void" then cands[#cands + 1] = { w[1] + d[1], w[2] + d[2] } end
        end
      end
      if #cands > 0 then
        local p = choose(cands)
        Kit.cell(run, p[1], p[2]).wet = true
        L.wet[#L.wet + 1] = p
      end
    end
  else
    -- dries, then the valve goes away
    if L.t > 2.5 and #L.wet > 0 then
      L.t = 0
      local p = remove(L.wet, 1)
      Kit.cell(run, p[1], p[2]).wet = nil
    end
    if #L.wet == 0 then
      for i, st in ipairs(run.stations) do if st == L.st then remove(run.stations, i) break end end
      L.cell.st = nil
      run.leak = nil
    end
  end
end

function Dis.fix_valve(run, c, st, dt)
  local L = run.leak
  if not L or L.st ~= st or L.fixed then return false end
  st.prog = st.prog + dt / 2
  if random() < 0.3 then Snd.clang() end
  if st.prog >= 1 then
    L.fixed, L.t = true, 0
    st.leak = false
    Snd.ui_ok()
    Fx.text(st.x, st.z - 1, "FIXED!", 0x80E0FF, true)
    return false
  end
  return true
end

local function update_ghosts(run, dt)
  if run.dark > 0 then run.dark = run.dark - dt end
  local cells
  for i = #run.ghosts, 1, -1 do
    local g = run.ghosts[i]
    g.t = g.t - dt
    g.fade = min(1, g.fade + dt)
    if g.t <= 0 then
      g.fade = g.fade - dt * 3
      if g.t < -0.5 then remove(run.ghosts, i) goto continue end
    end
    if move_to(run, g, g.tx, g.tz, 1.4, dt) then
      cells = cells or floor_cells(run)
      local p = choose(cells)
      g.tx, g.tz = p[1], p[2]
    end
    g.y = 1.1 + sin(G.t * 2 + i) * 0.15
    g.act = g.act - dt
    if g.act <= 0 then
      g.act = 3 + random() * 3
      local r = random()
      if r < 0.5 then
        -- a crate flies open and its ingredient pops out
        local crates = {}
        for _, st in ipairs(run.stations) do if st.kind == "crate" then crates[#crates + 1] = st end end
        if #crates > 0 then
          local st = choose(crates)
          st.bump = 0.3
          local l = Kit.drop(run, Food.new_ing(st.ing), st.x, st.z, TOP + 0.2, (random() - 0.5) * 2, 4, -2)
          l.fly = true
          Snd.boo()
        end
      else
        -- pushes a floating item to another counter
        start.poltergeist(run)
      end
    end
    -- chefs it floats through get a fright
    for _, c in ipairs(run.chefs) do
      if dist2(c.x, c.z, g.x, g.z) < 0.35 and c.stun <= 0 and (c.scared or 0) <= 0 then
        c.stun, c.scared = 0.5, 3
        Chef.say(c, "EEK!", 0.8)
        Snd.boo()
      end
      if c.scared and c.scared > 0 then c.scared = c.scared - dt end
    end
    ::continue::
  end
end

-- the extinguisher blows ghosts and the tornado away
function Dis.spray(run, c, fx, fz, dt)
  for _, g in ipairs(run.ghosts) do
    if dist2(c.x + fx * 1.2, c.z + fz * 1.2, g.x, g.z) < 1.2 then
      g.t = min(g.t, 0)
      Fx.puff(g.x, g.y, g.z, 0xFFFFFF, 3)
    end
  end
end

local function update_possessed(run, dt)
  for _, st in ipairs(run.stations) do
    local p = st.possessed
    if p then
      p.t = p.t + dt
      if random() < dt * 3 then Fx.sparkle(st.x, TOP + 0.4, st.z, 0xC080FF) end
      if p.kind == "pan" then
        p.spill = p.spill - dt
        if random() < dt * 4 then Snd.clang() end
        if p.spill <= 0 then
          -- throws its food out on the floor
          local b = st.box
          for _, key in ipairs(b.items) do
            local a = random() * TAU
            local l = Kit.drop(run, { key = b.done and b.out or key }, st.x, st.z, TOP + 0.3, sin(a) * 2, 4, cos(a) * 2)
            l.fly = true
            if b.done then break end
          end
          Food.box_take(st)
          st.possessed = nil
          Fx.text(st.x, st.z, "SPLAT!", 0xC080FF)
          Snd.boo()
        end
      end
    end
  end
end

local function update_loose_rolling(run, dt)
  for _, l in ipairs(run.loose) do
    if l.roll and not l.fly then
      l.roll = l.roll - dt
      if l.roll > 0 then
        if l.rvx == nil then
          local a = random() * TAU
          l.rvx, l.rvz = sin(a) * 2.2, cos(a) * 2.2
        end
        local nx, nz = l.x + l.rvx * dt, l.z + l.rvz * dt
        if Kit.solid_at(run, nx, l.z) then l.rvx = -l.rvx else l.x = nx end
        if Kit.solid_at(run, l.x, nz) then l.rvz = -l.rvz else l.z = nz end
        l.spin = (l.spin or 0) + dt * 8
      else
        l.roll, l.rvx, l.rvz, l.eyes = nil, nil, nil, nil
      end
    end
  end
end

local function update_tornado(run, dt)
  local T = run.tornado
  if not T then return end
  T.t = T.t - dt
  T.spin = T.spin + dt * 14
  T.snd = T.snd - dt
  if T.snd <= 0 then T.snd = 0.25 Snd.wind() end
  if T.t <= 0 then run.tornado = nil Fx.puff(T.x, 0.5, T.z, 0xD0D0D8, 8) return end
  if move_to(run, T, T.tx, T.tz, 1.7, dt, true) then
    local cells = floor_cells(run)
    local p = choose(cells)
    T.tx, T.tz = p[1], p[2]
  end
  if random() < 0.5 then Fx.dust(T.x + rnd(-0.3, 0.3), T.z + rnd(-0.3, 0.3)) end
  for _, c in ipairs(run.chefs) do
    local dx, dz = c.x - T.x, c.z - T.z
    if dx * dx + dz * dz < 0.5 and c.stun <= 0 then
      c.stun = 1.0
      local d = sqrt(dx * dx + dz * dz) + 0.01
      c.vx, c.vz = dx / d * 5, dz / d * 5
      Chef.say(c, "WHOOA!", 0.9)
    end
  end
  for _, l in ipairs(run.loose) do
    if not l.fly and dist2(l.x, l.z, T.x, T.z) < 1.2 then
      local a = random() * TAU
      l.fly, l.vx, l.vy, l.vz = true, sin(a) * 3.5, 5, cos(a) * 3.5
    end
  end
  -- knocks food off counters it brushes past
  if random() < dt * 1.2 then
    for _, st in ipairs(run.stations) do
      if st.item and Food.is_food(st.item) and st.def.hold and dist2(st.x, st.z, T.x, T.z) < 1.0 then
        local it = st.item
        st.item = nil
        local a = atan(st.x - T.x, st.z - T.z) + pi
        local l = Kit.drop(run, it, st.x, st.z, TOP + 0.2, sin(a) * 2.5, 4, cos(a) * 2.5)
        l.fly = true
        break
      end
    end
  end
end

function Dis.update(run, dt)
  for _, d in ipairs(run.dis) do
    d.t = d.t - dt
    if d.t <= 0 then
      Dis.trigger(run, d.kind)
      d.t = d.every or 1e9
    end
  end
  if run.banner then
    run.banner.t = run.banner.t - dt
    if run.banner.t <= 0 then run.banner = nil end
  end
  update_rats(run, dt)
  update_ducks(run, dt)
  update_leak(run, dt)
  update_ghosts(run, dt)
  update_possessed(run, dt)
  update_loose_rolling(run, dt)
  update_tornado(run, dt)
end

-- A near a rat, or on a haunted station: true if it did something
function Dis.chef_press(run, c)
  for _, r in ipairs(run.rats) do
    if dist2(c.x + sin(c.yaw) * 0.5, c.z + cos(c.yaw) * 0.5, r.x, r.z) < 0.7 then
      scare_rat(run, r)
      return true
    end
  end
  local st = c.target
  if st and st.rat then
    scare_rat(run, st.rat)
    return true
  end
  if st and st.possessed then
    st.possessed = nil
    Fx.sparkle(st.x, TOP + 0.5, st.z, 0xFFFFFF)
    Fx.text(st.x, st.z, "CALM!", 0xC080FF)
    Snd.ui_ok()
    return true
  end
  return false
end

---------------------------------------------------------------- drawing

local M

local function meshes()
  if M then return end
  M = {}
  local b = Mesh.builder()
  b.ball(0, 0.09, 0, 0.1, 0.08, 0.16, 0x8A8078, 5, 3)
  b.ball(0, 0.12, 0.15, 0.06, 0.05, 0.06, 0x9A9088, 5, 2)
  b.box(-0.07, 0.15, 0.12, -0.03, 0.2, 0.15, 0xE0A0A0)
  b.box(0.03, 0.15, 0.12, 0.07, 0.2, 0.15, 0xE0A0A0)
  b.box(-0.01, 0.06, -0.38, 0.01, 0.08, -0.14, 0xE0A0A0)
  M.rat = b.build()
  b = Mesh.builder()
  -- (up to five at once: kept light, about 50 triangles)
  b.ball(0, 0.18, 0, 0.16, 0.13, 0.2, 0xF8F4E0, 6, 3)
  b.ball(0, 0.36, 0.13, 0.09, 0.09, 0.09, 0xF8F4E0, 5, 2)
  b.boxf(-0.04, 0.33, 0.2, 0.04, 0.36, 0.3, 0xF09020, "tnwe")
  b.decal(-0.06, 0.38, -0.03, 0.41, 0.221, 0x101010)
  b.decal(0.03, 0.38, 0.06, 0.41, 0.221, 0x101010)
  b.boxf(-0.09, 0, -0.02, 0.09, 0.05, 0.06, 0xF09020, "tns")
  M.duck = b.build()
  b = Mesh.builder()
  b.ball(0, 0.2, 0, 0.28, 0.3, 0.26, 0xECECFF, 6, 3)
  b.prism(0, 0, -0.25, 0.0, 0.2, 0.28, 6, 0xDCDCF0, false)
  b.decal(-0.12, 0.26, -0.04, 0.36, 0.25, 0x202040)
  b.decal(0.04, 0.26, 0.12, 0.36, 0.25, 0x202040)
  b.decal(-0.05, 0.1, 0.05, 0.16, 0.26, 0x202040)
  M.ghost = b.build()
  b = Mesh.builder()
  for i = 0, 4 do
    local r = 0.12 + i * 0.1
    b.prism(0, 0, i * 0.3, i * 0.3 + 0.26, r, r + 0.08, 6, i % 2 == 0 and 0xB8C0D0 or 0x98A0B0, false)
  end
  M.tornado = b.build()
  b = Mesh.builder()
  b.box(-0.02, 0, -0.18, 0.02, 0.05, 0.12, 0xD8E0E8)
  b.box(-0.03, -0.01, 0.12, 0.03, 0.06, 0.26, 0x6A3A2A)
  M.knife = b.build()
  b = Mesh.builder()
  b.disc(0, 0.01, 0, 0.5, 8, 0x5A9AE0)
  M.puddle = b.build()
  b = Mesh.builder()
  b.ball(-0.05, 0.05, 0.06, 0.035, 0.035, 0.02, 0xFFFFFF, 4, 2)
  b.ball(0.05, 0.05, 0.06, 0.035, 0.035, 0.02, 0xFFFFFF, 4, 2)
  M.eyes = b.build()
  b = Mesh.builder()
  b.boxf(-0.06, -0.5, -0.06, 0.06, 0.4, 0.06, 0x8898B8, "tsew")
  b.prism(0, -0.12, 0.1, 0.16, 0.2, 0.2, 8, 0xD03030, 0xE04040)
  M.valve = b.build()
end

function Dis.draw(run)
  meshes()
  -- puddles lie flat on the floor
  if run.leak then
    for _, p in ipairs(run.leak.wet) do
      draw3d(M.puddle, p[1] + 0.5, 0.004, p[2] + 0.5, 0, p[1] + p[2], 0, 1, 1)
    end
  end
  if run.leak then
    local st = run.leak.st
    draw3d(M.valve, st.x, 0.6, st.cz - 0.02, -pi / 2, 0, st.prog * 6, 1)
  end
  for _, r in ipairs(run.rats) do
    if r.state ~= "hide" then
      draw3d(M.rat, r.x, abs(sin(r.run)) * 0.04, r.z, 0, r.yaw, 0, 1.7)
      if r.carry then Ren.draw_item(r.carry, r.x + sin(r.yaw) * 0.25, 0.1, r.z + cos(r.yaw) * 0.25, r.yaw, 0.6) end
    end
  end
  for _, d in ipairs(run.ducks) do
    if d.delay <= 0 then
      draw3d(M.duck, d.x, 0, d.z, 0, d.yaw, sin(d.waddle) * 0.18, 1.3 * d.size)
    end
  end
  for _, g in ipairs(run.ghosts) do
    if g.fade > 0.05 then
      draw3d(M.ghost, g.x, g.y, g.z, 0, pi + sin(G.t * 1.5) * 0.4, sin(G.t * 3) * 0.1, g.fade * 1.7, 2)
    end
  end
  if run.tornado then
    local T = run.tornado
    draw3d(M.tornado, T.x, 0, T.z, 0, T.spin, sin(T.spin * 0.3) * 0.08, 1.2, 2)
  end
  for _, st in ipairs(run.stations) do
    local p = st.possessed
    if p and p.kind == "knife" then
      draw3d(M.knife, st.x, TOP + 0.35 + abs(sin(G.t * 6)) * 0.15, st.z, 0, G.t * 7, sin(G.t * 9) * 0.5, 1.3)
    elseif p and p.kind == "pan" then
      st.bump = 0.15
    end
  end
  -- runaway food has little eyes
  for _, l in ipairs(run.loose) do
    if l.eyes then draw3d(M.eyes, l.x, l.y + 0.12, l.z, 0, atan(l.rvx or 0, l.rvz or 1), 0, 1.5, 2) end
  end
end

function Dis.draw2d(run)
  -- a rat in a crate: its tail sticks out, with a warning
  for _, r in ipairs(run.rats) do
    if r.state == "hide" and r.crate and (G.frame // 10) % 2 == 0 then
      local sx, sy = project3d(r.crate.x, TOP + 0.5, r.crate.z)
      if sx then Hud.icon(Hud.IC.rat, floor(sx - 8), floor(sy - 8)) end
    end
  end
  if run.leak and not run.leak.fixed then
    local st = run.leak.st
    local sx, sy = project3d(st.x, 1.2, st.z)
    if sx then
      Hud.icon(Hud.IC.wrench, floor(sx - 8), floor(sy - 18 + sin(G.t * 6) * 2))
      if st.prog > 0 then bar(floor(sx - 14), floor(sy), 28, 5, st.prog, 0x80E0FF, 0x203040) end
    end
  end
  local b = run.banner
  if b then
    local k = min(1, (2.2 - b.t) * 5)
    local y = floor(H / 2 - 60 + (1 - k) * -40)
    local w = text_w(b.text, 3) + 60
    panel(floor(W / 2 - w / 2), y, w, 56, 0x401818, 0xFFD040)
    Hud.icon(b.icon, floor(W / 2 - w / 2) + 12, y + 20)
    text_cs(b.text, W / 2 + 12, y + 4, 0xFFE060, 3, 0x000000)
  end
end
