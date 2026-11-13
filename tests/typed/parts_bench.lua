-- The particles of Yharnam (update_parts), three ways, the same scene (docs/design/typed-native.md).
--   plua   parts_bench.lua REPS A     the function of the game, as it is (Lua tables)
--   plua   parts_bench.lua REPS P     Pallene on the same tables (needs parts.so)
--   bindhost parts_bench.lua REPS U   the typed C of parts.bmt on arrays owned by C (needs the host's Parts table)
-- The scene: MAXP 160, three births a frame, kinds as the game, 600 frames. The time is the time of update_parts only. The
-- three give the same count and the same sums of x, y, vx, vy and t.
local REPS, mode = tonumber(arg[1]), arg[2]
local F, MAXP = 600, 160
local KINDS = { 1, 1, 2, 3, 4, 5, 6, 7, 8 }

local JIT = { k = 0 }
for i = 0, 63 do JIT[i] = (((i * 37 + 11) * 73) % 101 / 100 - 0.5) end

local parts = {}
local function spawn_lua(x, y, vx, vy, life, kind, gy)
  if #parts >= (kind == 1 and MAXP - 40 or MAXP) then return end
  local p = { x = x, y = y, vx = vx, vy = vy, t = 0, life = life, kind = kind }
  if kind == 8 then p.gy = gy end            -- as the game: only a splinter has gy
  parts[#parts + 1] = p
end

-- the function of the game (bm/yharnam/main.lua), copied as it is
local function update_parts()
  local n = #parts
  local i = 1
  JIT.k = (JIT.k + 13) & 63
  local k0 = JIT.k
  while i <= n do
    local p = parts[i]
    local pt = p.t + 1
    p.t = pt
    if pt >= p.life then
      parts[i] = parts[n]
      parts[n] = nil
      n = n - 1
    else
      p.x = p.x + p.vx
      p.y = p.y + p.vy
      local k = p.kind
      if k == 1 then
        p.vx = p.vx * 0.94 + JIT[(k0 + i * 7) & 63] * 0.12
        p.vy = p.vy * 0.985 - 0.012
      elseif k == 2 then
        p.vx = p.vx + JIT[(k0 + i * 5) & 63] * 0.08
        p.vy = p.vy * 0.995 - 0.004
      elseif k == 3 then
        p.vx = p.vx * 0.99 + 0.004
        p.vy = p.vy * 0.995
      elseif k == 8 then
        if p.y < p.gy then p.vy = p.vy + 0.13 else p.y, p.vx, p.vy = p.gy, 0, 0 end
      elseif k >= 6 then
        p.vx = p.vx * 0.97 + JIT[(k0 + i * 3) & 63] * 0.03
        p.vy = p.vy * 0.99 - 0.002
      else
        p.vy = p.vy + 0.03
      end
      i = i + 1
    end
  end
end

local pal, jit1
if mode == "P" then
  pal = require "parts"
  jit1 = {}
  for i = 0, 63 do jit1[i + 1] = JIT[i] end
end

local function one_run()
  math.randomseed(11)
  parts = {}
  JIT.k = 0
  local spent = 0
  if mode == "U" then Parts.reset(); for i = 0, 63 do Parts.jit(i + 1, JIT[i]) end end
  for _ = 1, F do
    for _ = 1, 3 do                            -- three births a frame
      local kind = KINDS[math.random(#KINDS)]
      local x, y = math.random() * 400, math.random() * 200
      local vx, vy = (math.random() - 0.5) * 2, (math.random() - 0.5) * 2
      local life, gy = 30 + math.random(90), y + math.random() * 40
      if mode == "U" then Parts.spawn(x, y, vx, vy, life, kind, gy) else spawn_lua(x, y, vx, vy, life, kind, gy) end
    end
    local t0 = os.clock()
    if mode == "A" then
      update_parts()
    elseif mode == "P" then
      JIT.k = (JIT.k + 13) & 63
      local n = pal.update_parts(parts, #parts, jit1, JIT.k)
      for j = #parts, n + 1, -1 do parts[j] = nil end
    else
      JIT.k = (JIT.k + 13) & 63
      Parts.update(JIT.k)
    end
    spent = spent + (os.clock() - t0)
  end
  local sum, n = 0.0, 0
  if mode == "U" then
    n, sum = Parts.checksum()
  else
    for i = 1, #parts do
      local p = parts[i]
      sum = sum + p.x + p.y + p.vx + p.vy + p.t
    end
    n = #parts
  end
  return spent, n, sum
end

local total, n, sum = 0, 0, 0
for _ = 1, REPS do
  local s
  s, n, sum = one_run()
  total = total + s
end
print(string.format("%s %.3f %d %.9f", mode, total * 1e6 / (REPS * F), n, sum))
