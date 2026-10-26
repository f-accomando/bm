-- The borders of the hunt: the areas are closed by walls and barricades (gates), the way between them is
-- open once the gates are, and the lamps and bosses can be reached.
--   luahost tests/yharnam/borders.lua carts/yharnam/main.lua   -> "borders ok", else the failed checks and exit 1
-- Every chunk is made, then the 128 x 128 tiles of the map are checked. A tile is blocked if the chunk's
-- ground is solid there, or if an object that is not broken, with a collider, sealed or at a gate, stands on it.
-- All the checks run, so one run shows every failure.
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local SRC = arg[1]
assert(SRC, "luahost tests/yharnam/borders.lua carts/yharnam/main.lua")
local Y = B.load(SRC)
local M, CS = Y.MAP, Y.CS
local TS = 16
local N = M.size * CS                         -- tiles across the map
local floor = math.floor

local chunks = {}
for cy = 0, M.size - 1 do
  for cx = 0, M.size - 1 do chunks[cy * M.size + cx] = Y.ensure(cx, cy) end
end

local solid, gate_tile, gate_at = {}, {}, {}  -- by ty * N + tx
for cy = 0, M.size - 1 do
  for cx = 0, M.size - 1 do
    local ch = chunks[cy * M.size + cx]
    for ly = 0, CS - 1 do
      for lx = 0, CS - 1 do
        if ch.solid[ly * CS + lx + 1] then solid[(cy * CS + ly) * N + cx * CS + lx] = true end
      end
    end
    for _, o in ipairs(ch.objs) do
      if o.col and not o.broken and (o.sealed or o.gate) then
        local tx, ty = floor(o.x / TS), floor(o.y / TS)
        if tx >= 0 and ty >= 0 and tx < N and ty < N then
          local k = ty * N + tx
          if o.gate then gate_tile[k] = true; gate_at[k] = o.gate else solid[k] = true end
        end
      end
    end
  end
end

-- a 4-way flood fill from (sx, sy); ok(tx, ty) says whether a tile may be entered
local function fill(sx, sy, ok)
  local seen, stack, n = { [sy * N + sx] = true }, { sx, sy }, 0
  while #stack > 0 do
    local y, x = table.remove(stack), table.remove(stack)
    n = n + 1
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = x + d[1], y + d[2]
      if nx >= 0 and ny >= 0 and nx < N and ny < N and not seen[ny * N + nx] and ok(nx, ny) then
        seen[ny * N + nx] = true
        stack[#stack + 1] = nx; stack[#stack + 1] = ny
      end
    end
  end
  return seen, n
end

local fails = 0
local function fail(msg) fails = fails + 1; print("FAIL " .. msg) end

local bosses, shrines = {}, {}
for i, a in ipairs(M.areas) do
  local ch = chunks[a.bc[2] * M.size + a.bc[1]]
  if ch.boss then
    bosses[i] = { a.bc[1] * CS + ch.boss.lx, a.bc[2] * CS + ch.boss.ly }
  else
    fail("boss: area " .. a.id .. " has no ch.boss in chunk " .. a.bc[1] .. "," .. a.bc[2])
  end
end
for cy = 0, M.size - 1 do
  for cx = 0, M.size - 1 do
    local sh = chunks[cy * M.size + cx].shrine
    if sh then shrines[#shrines + 1] = { cx, cy, floor(sh.x / TS), floor(sh.y / TS) } end
  end
end

-- (a) from its boss, an area is closed: the fill stays in the area (the gates count as blocked)
for i, a in ipairs(M.areas) do
  local b = bosses[i]
  if b then
    local seen = fill(b[1], b[2], function(x, y)
      return not solid[y * N + x] and not gate_tile[y * N + x]
    end)
    local out = 0
    for k in pairs(seen) do
      if M.area_px((k % N) * TS, (k // N) * TS) ~= i then out = out + 1 end
    end
    if out > 0 then fail("(a) area " .. a.id .. ": the fill from the boss leaves the area on " .. out .. " tiles") end
  end
end

-- (b) (and (g): the cuts are in the map) with the gates open, the start reaches the 4 bosses and the 8 shrines
do
  local seen = fill(8, 12, function(x, y) return not solid[y * N + x] end)
  local miss = 0
  for i, b in pairs(bosses) do
    if not seen[b[2] * N + b[1]] then miss = miss + 1; print("  boss of " .. M.areas[i].id .. " not reached") end
  end
  if #shrines ~= 8 then fail("(b) " .. #shrines .. " shrines, 8 expected") end
  for _, s in ipairs(shrines) do
    if not seen[s[4] * N + s[3]] then miss = miss + 1; print("  shrine of chunk " .. s[1] .. "," .. s[2] .. " not reached") end
  end
  if miss > 0 then fail("(b) from the start (8,12), " .. miss .. " bosses or shrines not reached") end
end

-- (c) the perimeter is closed
do
  local open = 0
  for i = 0, N - 1 do
    for _, k in ipairs({ i, (N - 1) * N + i, i * N, i * N + N - 1 }) do
      if not solid[k] and not gate_tile[k] then open = open + 1 end
    end
  end
  if open > 0 then fail("(c) " .. open .. " tiles of the perimeter are not blocked") end
end

-- (d) every gate has at least one object in its chunks
for _, g in ipairs(M.gates) do
  local n = 0
  local p = g.pair
  local seenc = {}
  for _, c in ipairs({ { p[1], p[2] }, { p[3], p[4] } }) do
    local cx, cy = c[1] // 2, c[2] // 2
    if not seenc[cy * M.size + cx] then
      seenc[cy * M.size + cx] = true
      for _, o in ipairs(chunks[cy * M.size + cx].objs) do if o.gate == g then n = n + 1 end end
    end
  end
  if n == 0 then fail("(d) gate " .. g.id .. " has no object in its chunks (0 objects)") end
end

-- (e) the shrines are at least 2 chunks apart (Chebyshev)
do
  local close = 0
  for i = 1, #shrines do
    for j = i + 1, #shrines do
      local d = math.max(math.abs(shrines[i][1] - shrines[j][1]), math.abs(shrines[i][2] - shrines[j][2]))
      if d < 2 then close = close + 1 end
    end
  end
  if close > 0 then fail("(e) " .. close .. " pairs of shrines closer than 2 chunks") end
end

-- (f) a cut is a closed wall except its gate: in the strip of its tile range, a fill from one side
-- does not reach the other side with the gate blocked, and does with the gate open. Its gate tiles
-- are all of its gate. (g) every boss and shrine is reached from the start with the cuts in place:
-- the fill of (b) runs over the map made with the cuts.
for _, c in ipairs(M.cuts) do
  local function tile(along, across) if c.vert then return across, along end return along, across end
  local seed
  for t = c.lo, c.hi do
    local x, y = tile(t, c.at - 1)
    if not solid[y * N + x] and not gate_tile[y * N + x] then seed = { x, y }; break end
  end
  if not seed then
    fail("(f) cut " .. c.id .. ": no free tile on the start side")
  else
    local function strip(x, y)
      local along = c.vert and y or x
      return along >= c.lo and along <= c.hi
    end
    local function crossed(seen)
      local n = 0
      for k in pairs(seen) do
        local x, y = k % N, k // N
        if (c.vert and x or y) > c.at then n = n + 1 end
      end
      return n
    end
    local closed = fill(seed[1], seed[2], function(x, y)
      return strip(x, y) and not solid[y * N + x] and not gate_tile[y * N + x]
    end)
    local opened = fill(seed[1], seed[2], function(x, y) return strip(x, y) and not solid[y * N + x] end)
    local n = crossed(closed)
    if n > 0 then fail("(f) cut " .. c.id .. ": closed, the fill crosses the line on " .. n .. " tiles") end
    if crossed(opened) == 0 then fail("(f) cut " .. c.id .. ": open, the fill does not cross the line") end
    local gates = 0
    for t = c.lo, c.hi do
      local x, y = tile(t, c.at)
      if gate_at[y * N + x] == c.g then gates = gates + 1 end
    end
    if gates ~= c.gate[2] - c.gate[1] + 1 then
      fail("(f) cut " .. c.id .. ": " .. gates .. " gate tiles, " .. (c.gate[2] - c.gate[1] + 1) .. " expected")
    end
  end
end

if fails > 0 then print("borders: " .. fails .. " checks failed"); os.exit(1) end
print("borders ok")
