-- Computer chefs: they take an order, plan it (fetch, chop, cook, plate,
-- serve), walk the grid with a breadth-first search and press the same
-- buttons a player would. They cook behind the title screen, and the host
-- tests use them to prove that every stage's dishes can be made.

---------------------------------------------------------------- paths

local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

-- cells next to a station a chef can stand on
local function stand_cells(run, st)
  local out = {}
  for _, d in ipairs(DIRS) do
    local cx, cz = st.cx + d[1], st.cz + d[2]
    if Kit.walkable_cell(run, cx, cz) then out[#out + 1] = { cx, cz } end
  end
  return out
end

-- breadth-first search from (sx, sz) to any goal cell; returns the path
local function bfs(run, sx, sz, goals)
  local w = run.w
  local goal = {}
  for _, g in ipairs(goals) do goal[g[2] * w + g[1]] = true end
  if goal[sz * w + sx] then return {} end
  local prev, q, head = { [sz * w + sx] = -1 }, { sz * w + sx }, 1
  while head <= #q do
    local k = q[head]
    head = head + 1
    local cx, cz = k % w, k // w
    for _, d in ipairs(DIRS) do
      local nx, nz = cx + d[1], cz + d[2]
      local nk = nz * w + nx
      if not prev[nk] and Kit.walkable_cell(run, nx, nz) then
        prev[nk] = k
        if goal[nk] then
          local path = {}
          while nk ~= sz * w + sx do
            insert(path, 1, { nk % w + 0.5, nk // w + 0.5 })
            nk = prev[nk]
          end
          return path
        end
        q[#q + 1] = nk
      end
    end
  end
  return nil
end
Bot.bfs = bfs

---------------------------------------------------------------- planning

local function free_station(run, kind, c, test)
  local best, bd = nil, 1e9
  for _, st in ipairs(run.stations) do
    if st.kind == kind and (not st.res or st.res == c) and st.fire <= 0 and (not test or test(st)) then
      local d = dist2(st.x, st.z, c.x, c.z)
      if d < bd and #stand_cells(run, st) > 0 then best, bd = st, d end
    end
  end
  return best
end

local function crate_of(run, ing, c)
  return free_station(run, "crate", c, function(st) return st.ing == ing and not st.item end)
    or (function()
      for _, st in ipairs(run.stations) do
        if st.kind == "crate" and st.ing == ing then return st end
      end
    end)()
end

local BOX = { boil = "pot", fry = "pan", bake = "oven", blend = "blender" }

-- steps that end with the chef holding `key`
local function produce(run, c, key, steps)
  local k = Food.parse(key)
  if k.ing then
    local cr = crate_of(run, k.ing, c)
    if not cr then return false end
    steps[#steps + 1] = { op = "use", st = cr, want = "hold" }
    if k.chopped then
      local b = free_station(run, "board", c, function(st) return not st.item end)
      if not b then return false end
      b.res = c
      steps[#steps + 1] = { op = "use", st = b, want = "empty" }
      steps[#steps + 1] = { op = "work", st = b }
      steps[#steps + 1] = { op = "use", st = b, want = "hold", release = b }
    end
    return true
  end
  local kind = BOX[k.proc]
  local s = free_station(run, kind, c, function(st) return #st.box.items == 0 end)
  if not s then return false end
  s.res = c
  for _, kid in ipairs(k.kids) do
    if not produce(run, c, kid, steps) then return false end
    steps[#steps + 1] = { op = "use", st = s, want = "empty" }
  end
  steps[#steps + 1] = { op = "wait", st = s, until_done = true }
  steps[#steps + 1] = { op = "use", st = s, want = "hold", release = s }
  return true
end

local function plan_order(run, c, o)
  local steps = {}
  -- a counter near the window to build the plate on
  local serve = free_station(run, "serve", c)
  if not serve then return nil end
  local here = { x = serve.x, z = serve.z }
  local counter = free_station(run, "counter", here, function(st) return not st.item end)
  if not counter then return nil end
  counter.res = c
  -- a clean plate
  local plates = free_station(run, "plates", c, function(st) return st.n > 0 end)
  local sink = free_station(run, "sink", c, function(st) return st.clean > 0 end)
  if plates then steps[#steps + 1] = { op = "use", st = plates, want = "hold" }
  elseif sink then steps[#steps + 1] = { op = "use", st = sink, want = "hold" }
  else counter.res = nil return nil end
  steps[#steps + 1] = { op = "use", st = counter, want = "empty" }
  for _, part in ipairs(o.rec.parts) do
    if not produce(run, c, part, steps) then
      for _, st in ipairs(run.stations) do if st.res == c then st.res = nil end end
      return nil
    end
    steps[#steps + 1] = { op = "use", st = counter, want = "empty" }
  end
  steps[#steps + 1] = { op = "use", st = counter, want = "hold", release = counter }
  steps[#steps + 1] = { op = "use", st = serve, want = "empty" }
  return steps
end

-- washing up, when there is no clean plate anywhere
local function plan_wash(run, c)
  local ret = free_station(run, "ret", c, function(st) return st.n > 0 end)
  local sink = free_station(run, "sink", c)
  if not sink then return nil end
  local steps = {}
  if ret then
    steps[#steps + 1] = { op = "use", st = ret, want = "hold" }
    steps[#steps + 1] = { op = "use", st = sink, want = "empty" }
  elseif sink.dirty == 0 then
    return nil
  end
  sink.res = c
  steps[#steps + 1] = { op = "work", st = sink }
  steps[#steps + 1] = { op = "idle", t = 0.3, release = sink }
  return steps
end

local function release_all(run, c)
  for _, st in ipairs(run.stations) do if st.res == c then st.res = nil end end
  for _, o in ipairs(run.orders) do if o.bot == c then o.bot = nil end end
end

---------------------------------------------------------------- acting

local function clear_input(c)
  local inp = c.inp
  inp.mx, inp.mz, inp.a, inp.b, inp.x, inp.xh, inp.y = 0, 0, false, false, false, false, false
end

-- walk next to st and face it; true when it is the chef's target
local function approach_st(run, c, b, st, dt)
  if c.target == st then return true end
  local cx, cz = floor(c.x), floor(c.z)
  local adjacent = false
  for _, s in ipairs(stand_cells(run, st)) do
    if s[1] == cx and s[2] == cz then adjacent = true end
  end
  if adjacent and dist2(c.x, c.z, cx + 0.5, cz + 0.5) < 0.09 then
    -- turn towards the station
    c.inp.mx, c.inp.mz = (st.x - c.x) * 0.25, (st.z - c.z) * 0.25
    return false
  end
  if not b.path or b.path_st ~= st or b.repath <= 0 then
    b.path = bfs(run, cx, cz, stand_cells(run, st))
    b.path_st = st
    b.repath = 1.5
    if not b.path then return nil end
  end
  b.repath = b.repath - dt
  local nxt = b.path[1]
  if not nxt then
    c.inp.mx, c.inp.mz = cx + 0.5 - c.x, cz + 0.5 - c.z
    return false
  end
  local dx, dz = nxt[1] - c.x, nxt[2] - c.z
  local d = sqrt(dx * dx + dz * dz)
  if d < 0.12 then
    remove(b.path, 1)
  else
    c.inp.mx, c.inp.mz = dx / d, dz / d
  end
  return false
end

function Bot.update(run, c, dt)
  clear_input(c)
  local b = c.botm
  if not b then
    b = { steps = nil, i = 1, wait = random() * 0.5, repath = 0, stuck = 0, lx = c.x, lz = c.z }
    c.botm = b
  end
  if c.fall or c.stun > 0 then return end
  b.wait = b.wait - dt
  if b.wait > 0 then return end

  -- stuck (another chef in the way): step aside and plan again
  if dist2(c.x, c.z, b.lx, b.lz) < 0.0004 and not c.work then
    b.stuck = b.stuck + dt
  else
    b.stuck = 0
  end
  b.lx, b.lz = c.x, c.z
  if b.stuck > 2.5 then
    b.stuck = 0
    b.path = nil
    c.inp.mx, c.inp.mz = random() * 2 - 1, random() * 2 - 1
    b.wait = 0.3
    return
  end

  if not b.steps then
    -- put down what the last plan left in the hands
    if c.hold then
      local st = free_station(run, "counter", c, function(s) return not s.item end)
        or free_station(run, "trash", c)
      if st then b.steps, b.i = { { op = "use", st = st, want = "empty" } }, 1
      else c.inp.y = true return end
    else
      for _, o in ipairs(run.orders) do
        if not o.bot then
          local steps = plan_order(run, c, o)
          if steps then
            o.bot = c
            b.steps, b.i, b.order = steps, 1, o
            break
          end
        end
      end
      if not b.steps then
        b.steps, b.i = plan_wash(run, c), 1
        if not b.steps then b.wait = 0.4 return end
      end
    end
    b.path = nil
  end

  local step = b.steps[b.i]
  if not step then
    release_all(run, c)
    b.steps = nil
    return
  end
  local done = false
  if step.op == "idle" then
    step.t = step.t - dt
    done = step.t <= 0
  elseif step.op == "wait" then
    local bx = step.st.box
    if bx.burnt then release_all(run, c) b.steps = nil return end
    done = bx.done
    if not done then approach_st(run, c, b, step.st, dt) end
  else
    local r = approach_st(run, c, b, step.st, dt)
    if r == nil then release_all(run, c) b.steps = nil b.wait = 0.5 return end
    if r then
      if step.op == "use" then
        if step.want == "hold" and c.hold then done = true
        elseif step.want == "empty" and not c.hold then done = true
        else
          c.inp.a = (G.frame + c.n) % 6 == 0
          step.tries = (step.tries or 0) + 1
          if step.tries > 90 then release_all(run, c) b.steps = nil return end
        end
      elseif step.op == "work" then
        if Food.work_kind(run, c, step.st) then
          if not c.work then c.inp.x = (G.frame + c.n) % 6 == 0 end
        else
          done = true
        end
      end
    end
  end
  if done then
    if step.release then step.release.res = nil end
    b.i = b.i + 1
    b.path = nil
  end
end

-- drop plans when the kitchen changes under them (new run)
function Bot.reset(run)
  for _, c in ipairs(run.chefs) do c.botm = nil end
end
