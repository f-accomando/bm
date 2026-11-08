-- A frame of enemies, three ways (make test-typed). Each frame: Lua moves the 17 enemies (they bounce in a room of 5 x 5), then
-- the pairs closer than 0.8 are found, then Lua reads the hits. The three give the same sums.
--   A: the enemies are tables, as in the game; the scan is Lua.
--   B: the enemies are in C (E.get, E.set); one E.near call for each enemy.
--   C: the enemies are in C; one E.near_all call for the frame.
local N, F, REPS = 17, 200, tonumber(arg[1])
local mode = arg[2]

local function scene()
  math.randomseed(7)
  local s = { x = {}, z = {}, vx = {}, vz = {}, alive = {} }
  for i = 1, N do
    s.x[i], s.z[i] = math.random() * 5, math.random() * 5
    s.vx[i], s.vz[i] = (math.random() - 0.5) * 0.04, (math.random() - 0.5) * 0.04
    s.alive[i] = math.random() < 0.9
  end
  return s
end

local function bounce(p, v)
  if p < 0 or p > 5 then return -v end
  return v
end

local function run_A(s)
  local en = {}
  for i = 1, N do en[i] = { x = s.x[i], z = s.z[i], vx = s.vx[i], vz = s.vz[i], alive = s.alive[i] } end
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    for i = 1, N do
      local e = en[i]
      if e.alive then
        e.vx, e.vz = bounce(e.x, e.vx), bounce(e.z, e.vz)
        e.x, e.z = e.x + e.vx, e.z + e.vz
      end
    end
    for i = 1, N do
      local e = en[i]
      if e.alive then
        for j = 1, N do
          local o = en[j]
          local dx = e.x - o.x
          if j ~= i and o.alive and dx < 0.8 and dx > -0.8 then
            local dz = e.z - o.z
            local d2 = dx * dx + dz * dz
            if d2 < 0.64 and d2 > 0.000001 then pairs_n = pairs_n + 1; hit_sum = hit_sum + j end
          end
        end
      end
    end
  end
  return pairs_n, hit_sum
end

local function setup_C(s)
  E.init(N)
  for i = 1, N do E.set(i, s.x[i], s.z[i], s.alive[i]) end
end

local function move_C(s, alive)
  for i = 1, N do
    if alive[i] then
      local x, z = E.get(i)
      s.vx[i], s.vz[i] = bounce(x, s.vx[i]), bounce(z, s.vz[i])
      E.set(i, x + s.vx[i], z + s.vz[i])
    end
  end
end

local function run_B(s)
  setup_C(s)
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    move_C(s, s.alive)
    for i = 1, N do
      if s.alive[i] then
        local m = E.near(i)
        pairs_n = pairs_n + m
        for k = 1, m do hit_sum = hit_sum + E.hit(k) end
      end
    end
  end
  return pairs_n, hit_sum
end

local function run_C(s)
  setup_C(s)
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    move_C(s, s.alive)
    pairs_n = pairs_n + E.near_all()
    for i = 1, N do
      for k = 1, E.count(i) do hit_sum = hit_sum + E.hit_all(i, k) end
    end
  end
  return pairs_n, hit_sum
end

-- D and E: only the move of the frame, to see what the get and set calls cost (not compared with the others)
local function run_D(s)
  setup_C(s)
  for _ = 1, F do move_C(s, s.alive) end
  return 0, 0
end

local function run_E(s)
  local en = {}
  for i = 1, N do en[i] = { x = s.x[i], z = s.z[i], vx = s.vx[i], vz = s.vz[i], alive = s.alive[i] } end
  for _ = 1, F do
    for i = 1, N do
      local e = en[i]
      if e.alive then
        e.vx, e.vz = bounce(e.x, e.vx), bounce(e.z, e.vz)
        e.x, e.z = e.x + e.vx, e.z + e.vz
      end
    end
  end
  return 0, 0
end

local runs = { A = run_A, B = run_B, C = run_C, D = run_D, E = run_E }
local f = runs[mode]
local pn, hs
local t0 = os.clock()
for _ = 1, REPS do
  pn, hs = f(scene())
end
local us = (os.clock() - t0) * 1e6 / (REPS * F)
print(string.format("%s %.3f %d %d", mode, us, pn, hs))
