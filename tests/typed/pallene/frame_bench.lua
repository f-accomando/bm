-- A frame of 17 enemies with Pallene (docs/design/typed-native.md). Run with Pallene's Lua (plua) and the compiled modules in the
-- current directory:   plua frame_bench.lua REPS MODE      MODE = A, P1, P2, P3
--   A:  tables in Lua, the scan in Lua (the same code as ../frame.lua, way A)
--   P1: arrays of numbers; Lua moves the enemies; Pallene scans (scan_soa)
--   P2: the enemies are tables as in the game; Lua moves them; Pallene scans the fields in place (scan_objects)
--   P3: the enemies are tables as in the game; Pallene moves them and scans (frame_objects): one call for the frame
local N, F, REPS = 17, 200, tonumber(arg[1])
local mode = arg[2]
local pf = require "frame"

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

local function objects_of(s)
  local en = {}
  for i = 1, N do en[i] = { x = s.x[i], z = s.z[i], vx = s.vx[i], vz = s.vz[i], alive = s.alive[i] } end
  return en
end

local function zeros(n)
  local t = {}
  for i = 1, n do t[i] = 0 end
  return t
end

local function read_hits(out, counts)
  local sum = 0
  for i = 1, N do
    for k = 1, counts[i] do sum = sum + out[(i - 1) * N + k] end
  end
  return sum
end

local function run_A(s)
  local en = objects_of(s)
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

local function run_P1(s)
  local out, counts = zeros(N * N), zeros(N)
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    for i = 1, N do
      if s.alive[i] then
        s.vx[i], s.vz[i] = bounce(s.x[i], s.vx[i]), bounce(s.z[i], s.vz[i])
        s.x[i], s.z[i] = s.x[i] + s.vx[i], s.z[i] + s.vz[i]
      end
    end
    pairs_n = pairs_n + pf.scan_soa(N, s.x, s.z, s.alive, out, counts)
    hit_sum = hit_sum + read_hits(out, counts)
  end
  return pairs_n, hit_sum
end

local function run_P2(s)
  local en = objects_of(s)
  local out, counts = zeros(N * N), zeros(N)
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    for i = 1, N do
      local e = en[i]
      if e.alive then
        e.vx, e.vz = bounce(e.x, e.vx), bounce(e.z, e.vz)
        e.x, e.z = e.x + e.vx, e.z + e.vz
      end
    end
    pairs_n = pairs_n + pf.scan_objects(N, en, out, counts)
    hit_sum = hit_sum + read_hits(out, counts)
  end
  return pairs_n, hit_sum
end

local function run_P3(s)
  local en = objects_of(s)
  local out, counts = zeros(N * N), zeros(N)
  local pairs_n, hit_sum = 0, 0
  for _ = 1, F do
    pairs_n = pairs_n + pf.frame_objects(N, en, out, counts)
    hit_sum = hit_sum + read_hits(out, counts)
  end
  return pairs_n, hit_sum
end

local runs = { A = run_A, P1 = run_P1, P2 = run_P2, P3 = run_P3 }
local f = assert(runs[mode], "mode A, P1, P2 or P3")
local pn, hs
local t0 = os.clock()
for _ = 1, REPS do pn, hs = f(scene()) end
print(string.format("%s %.3f %d %d", mode, (os.clock() - t0) * 1e6 / (REPS * F), pn, hs))
