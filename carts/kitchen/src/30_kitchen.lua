-- The kitchen being played: a grid of cells (x right, z away from the
-- camera; cell (cx, cz) covers [cx, cx+1) x [cz, cz+1)), the stations on
-- it, moving platforms, and the items lying on the floor or flying.

local WALL = { kind = "wall" }

local function idx(run, cx, cz) return cz * run.w + cx + 1 end

function Kit.cell(run, cx, cz)
  if cx < 0 or cz < 0 or cx >= run.w or cz >= run.h then return WALL end
  return run.cells[idx(run, cx, cz)]
end

-- A new station of `kind` in cell (cx, cz); `plat`: the platform it rides.
function Kit.add_station(run, kind, cx, cz, plat)
  local st = {
    kind = kind, def = Data.ST[kind], cx = cx, cz = cz, plat = plat,
    x = cx + 0.5, z = cz + 0.5, bx = cx + 0.5, bz = cz + 0.5,
    item = nil, prog = 0, fire = 0, id = #run.stations + 1, bump = 0,
  }
  if st.def.box then st.box = { items = {}, t = 0, done = false, burnt = false, warn = 0 } end
  run.stations[#run.stations + 1] = st
  return st
end

local function parse_layout(run, stage)
  local lay = stage.layout
  run.h, run.w = #lay, #lay[1]
  run.cells, run.stations, run.spawns = {}, {}, {}
  for r = 1, run.h do
    local row = lay[r]
    assert(#row == run.w, fmt("stage %s: row %d is %d wide, not %d", stage.id, r, #row, run.w))
    local cz = run.h - r
    for cx = 0, run.w - 1 do
      local ch = sub(row, cx + 1, cx + 1)
      local cell = { kind = "floor", ch = ch }
      local kind = Data.CHAR_ST[ch]
      if kind == "wall" then
        cell.kind = "wall"
      elseif kind then
        cell.st = Kit.add_station(run, kind, cx, cz)
        if ch == "E" then cell.st.item = { key = "ext" } end
        if Data.BELT_DIR[ch] then cell.st.dir = Data.BELT_DIR[ch] end
      elseif Data.CRATE_CHAR[ch] then
        cell.st = Kit.add_station(run, "crate", cx, cz)
        cell.st.ing = Data.CRATE_CHAR[ch]
      else
        cell.kind = Data.FLOOR_CHAR[ch] or "floor"
        local n = tonumber(ch)
        if n then run.spawns[n] = { cx + 0.5, cz + 0.5 } end
      end
      run.cells[idx(run, cx, cz)] = cell
    end
  end
  for n = 1, 4 do
    run.spawns[n] = run.spawns[n] or run.spawns[1] or { run.w / 2, run.h / 2 }
  end
end

-- Platforms: a rectangle of the layout that moves by (dx, dy) cells and
-- back. Its cells leave the grid (water below) and ride with it.
local function make_platforms(run, stage)
  run.plats = {}
  for _, pd in ipairs(stage.plats or {}) do
    local p = { def = pd, w = pd.w, h = pd.h, cells = {}, stations = {},
                x0 = pd.x, z0 = run.h - pd.y - pd.h, ox = 0, oz = 0, vx = 0, vz = 0,
                t = pd.phase or 0, rot = 0 }
    for lz = 0, p.h - 1 do
      for lx = 0, p.w - 1 do
        local cx, cz = p.x0 + lx, p.z0 + lz
        local cell = Kit.cell(run, cx, cz)
        local pc = { kind = cell.kind, st = cell.st, ch = cell.ch }
        if pc.st then
          pc.st.plat = p
          pc.st.lx, pc.st.lz = lx, lz
          pc.st.lx0, pc.st.lz0 = lx, lz
          p.stations[#p.stations + 1] = pc.st
        end
        p.cells[lz * p.w + lx + 1] = pc
        run.cells[idx(run, cx, cz)] = { kind = "void", ch = "_" }
      end
    end
    run.plats[#run.plats + 1] = p
  end
  -- the cells a platform's floor passes over (the computer chefs wait there
  -- for the platform instead of giving up)
  run.plat_cover = {}
  for _, p in ipairs(run.plats) do
    local d = p.def
    local steps = max(abs(d.dx or 0), abs(d.dy or 0))
    for k = 0, steps do
      local ox = steps > 0 and floor((d.dx or 0) * k / steps + 0.5) or 0
      local oz = steps > 0 and -floor((d.dy or 0) * k / steps + 0.5) or 0
      for lz = 0, p.h - 1 do
        for lx = 0, p.w - 1 do
          local c = p.cells[lz * p.w + lx + 1]
          if c.kind ~= "void" and not c.st then
            run.plat_cover[(p.z0 + lz + oz) * run.w + p.x0 + lx + ox] = true
          end
        end
      end
    end
  end
end

function Kit.new(stage)
  local run = {
    stage = stage, world = Data.WORLDS[stage.world],
    loose = {},                 -- items on the floor or in the air
    returns = {},               -- plates on their way back { t, dirty }
    fx = {},
  }
  parse_layout(run, stage)
  make_platforms(run, stage)
  -- clean plates on the plate stations
  local stacks = Kit.find(run, "plates")
  local n = stage.plates or 4
  for i = 1, n do
    local st = stacks[(i - 1) % max(1, #stacks) + 1]
    if st then st.n = (st.n or 0) + 1 end
  end
  for _, st in ipairs(stacks) do st.n = st.n or 0 end
  for _, st in ipairs(Kit.find(run, "sink")) do st.dirty, st.clean = 0, 0 end
  for _, st in ipairs(Kit.find(run, "ret")) do st.n = 0 end
  return run
end

function Kit.find(run, kind)
  local out = {}
  for _, st in ipairs(run.stations) do
    if st.kind == kind then out[#out + 1] = st end
  end
  return out
end

---------------------------------------------------------------- geometry

-- The cell at a world point: a platform's if one covers it, else the grid's.
-- Returns cell, platform.
function Kit.cell_at(run, x, z)
  local plats = run.plats
  for i = 1, #plats do
    local p = plats[i]
    local lx, lz = floor(x - p.x0 - p.ox), floor(z - p.z0 - p.oz)
    if lx >= 0 and lz >= 0 and lx < p.w and lz < p.h then
      local c = p.cells[lz * p.w + lx + 1]
      if c.kind ~= "void" then return c, p end
    end
  end
  return Kit.cell(run, floor(x), floor(z)), nil
end

local function blocked(cell)
  local k = cell.kind
  return cell.st ~= nil or k == "wall" or k == "void" or (k == "door" and not cell.open) or cell.debris
end
Kit.blocked = blocked

function Kit.solid_at(run, x, z)
  local c = Kit.cell_at(run, x, z)
  return blocked(c) or (#run.carts > 0 and Haz.solid_at(run, x, z))
end

-- the station whose cell contains the point
function Kit.station_at(run, x, z)
  local c = Kit.cell_at(run, x, z)
  return c.st
end

-- world position of a station (it may ride a platform, maybe turning)
function Kit.update_station_pos(run)
  for _, p in ipairs(run.plats) do
    if p.def.rotate then
      local cx, cz = p.x0 + p.w / 2, p.z0 + p.h / 2
      local ca, sa = cos(p.rot), sin(p.rot)
      for _, st in ipairs(p.stations) do
        local dx, dz = st.lx0 + 0.5 - p.w / 2, st.lz0 + 0.5 - p.h / 2
        st.x, st.z = cx + dx * ca + dz * sa, cz - dx * sa + dz * ca
      end
    else
      for _, st in ipairs(p.stations) do
        st.x = p.x0 + p.ox + st.lx + 0.5
        st.z = p.z0 + p.oz + st.lz + 0.5
      end
    end
  end
end

-- a quarter turn of a square platform: every cell moves round the centre
-- (the same way draw3d turns its meshes with a positive angle)
function Kit.rotate_platform(run, p)
  local cells = {}
  local w, h = p.w, p.h
  for lz = 0, h - 1 do
    for lx = 0, w - 1 do
      local c = p.cells[lz * w + lx + 1]
      local dx, dz = lx + 0.5 - w / 2, lz + 0.5 - h / 2
      local nx, nz = floor(dz + w / 2), floor(-dx + h / 2)
      cells[nz * w + nx + 1] = c
      if c.st then c.st.lx, c.st.lz = nx, nz end
    end
  end
  p.cells = cells
  -- loose items on it turn too
  local cx, cz = p.x0 + w / 2, p.z0 + h / 2
  for _, l in ipairs(run.loose) do
    local _, pp = Kit.cell_at(run, l.x, l.z)
    if pp == p and not l.fly then
      local dx, dz = l.x - cx, l.z - cz
      l.x, l.z = cx + dz, cz - dx
    end
  end
end

-- Cells a chef can stand on next to a station, for the computer chefs.
function Kit.walkable_cell(run, cx, cz)
  if cx < 0 or cz < 0 or cx >= run.w or cz >= run.h then return false end
  return not Kit.solid_at(run, cx + 0.5, cz + 0.5)
end

---------------------------------------------------------------- loose items

-- An item dropped at (x, z), or thrown with a velocity.
function Kit.drop(run, item, x, z, y, vx, vy, vz, from)
  local it = { item = item, x = x, z = z, y = y or 0, vx = vx or 0, vy = vy or 0, vz = vz or 0,
               fly = vx ~= nil, from = from, t = 0, bounce = 0 }
  run.loose[#run.loose + 1] = it
  return it
end

function Kit.remove_loose(run, it)
  for i, o in ipairs(run.loose) do
    if o == it then remove(run.loose, i) return end
  end
end

-- the loose item nearest to (x, z) within r, on the floor (not flying)
function Kit.loose_near(run, x, z, r, fx, fz)
  local best, bd = nil, r * r
  for _, it in ipairs(run.loose) do
    if not it.fly and not it.taken then
      local dx, dz = it.x - x, it.z - z
      local d = dx * dx + dz * dz
      if d < bd and (not fx or dx * fx + dz * fz > -0.1) then best, bd = it, d end
    end
  end
  return best
end

---------------------------------------------------------------- update

-- plates coming back from the serving window
function Kit.plate_back(run, delay)
  local wash = run.stage.wash or run.wash
  run.returns[#run.returns + 1] = { t = delay or 6, dirty = wash }
end

local function update_returns(run, dt)
  for i = #run.returns, 1, -1 do
    local r = run.returns[i]
    r.t = r.t - dt
    if r.t <= 0 then
      remove(run.returns, i)
      local target
      if r.dirty then
        target = Kit.find(run, "ret")[1]
      end
      if target then
        target.n = target.n + 1
        target.bump = 0.3
      else
        local stacks = Kit.find(run, "plates")
        local st = stacks[1]
        for _, s in ipairs(stacks) do if s.n < st.n then st = s end end
        if st then st.n = st.n + 1; st.bump = 0.3 end
      end
    end
  end
end

-- items on conveyor belts move to the next cell every `period` seconds
local function update_belts(run, dt)
  run.belt_t = (run.belt_t or 0) + dt
  local period = run.belt_period or 1.2
  if run.belt_t < period then return end
  run.belt_t = run.belt_t - period
  local moves = {}
  for _, st in ipairs(run.stations) do
    if st.kind == "belt" and st.item then
      local nx, nz = st.cx + st.dir[1], st.cz + st.dir[2]
      local nxt = Kit.cell(run, nx, nz).st
      if nxt and nxt.def.hold and not nxt.item and nxt.kind ~= "crate" then
        moves[#moves + 1] = { st, nxt }
      elseif nxt and nxt.kind == "serve" and st.item.plate then
        moves[#moves + 1] = { st, nxt }
      elseif nxt and nxt.kind == "trash" then
        moves[#moves + 1] = { st, nxt }
      end
    end
  end
  for _, m in ipairs(moves) do
    local a, b = m[1], m[2]
    if b.kind == "serve" then
      local it = a.item
      a.item = nil
      Ord.serve(G.run, it, b, nil)
    elseif b.kind == "trash" then
      a.item = Food.trash(a.item)
    elseif not b.item then
      b.item, a.item = a.item, nil
      b.slide = 1
    end
  end
end

function Kit.update(run, dt)
  update_returns(run, dt)
  update_belts(run, dt)
  for _, st in ipairs(run.stations) do
    if st.bump > 0 then st.bump = max(0, st.bump - dt) end
    if st.slide then st.slide = st.slide - dt * 4; if st.slide <= 0 then st.slide = nil end end
  end
end
