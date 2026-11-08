-- Typed Bench: the same functions of Yharnam in Lua (tables, as the game) and in C (data in C), timed on the console.
local TS, CS = 16, 32
local floor = math.floor
local function key2(a, b) return a * 1000003 + b end
local function fdiv(a, b) return floor(a) // b end

-- the game's data: a table of chunks, each with a table of tiles and a list of columns
local chunks = {}
local ch = { ready = true, solid = {}, cols = {} }
chunks[key2(0, 0)] = ch
local seed = 12345
local function rnd() seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF; return seed / 2147483648 end
for i = 0, CS * CS - 1 do
  local v = rnd() < 0.2
  ch.solid[i + 1] = v
  typed_set(i, v)
end
for i = 1, 20 do
  local c = { x = rnd() * 400 + 50, y = rnd() * 400 + 50, rx = 6 + rnd() * 10, ry = 4 + rnd() * 6, off = (i % 7 == 0) }
  ch.cols[i] = c
  typed_col(i - 1, c.x, c.y, c.rx, c.ry, c.off)
end
local function chunk(cx, cy) return chunks[key2(cx, cy)] end
local function inside(cx, cy) return cx >= 0 and cx < 1 and cy >= 0 and cy < 1 end

local function solid_at(x, y)
  local tx, ty = fdiv(x, TS), fdiv(y, TS)
  if not inside(tx // CS, ty // CS) then return true end
  local c = chunk(tx // CS, ty // CS)
  if not (c and c.ready) then return true end
  return c.solid[(ty % CS) * CS + (tx % CS) + 1]
end

local function blocked(x, y)
  if solid_at(x - 6, y - 3) or solid_at(x + 6, y - 3) or solid_at(x - 6, y + 2) or solid_at(x + 6, y + 2) then
    return true
  end
  for _, c in ipairs(ch.cols) do
    local dx = x - c.x
    if dx < 48 and dx > -48 and not c.off then
      dx = dx / (c.rx + 5)
      local dy = (y - c.y) / (c.ry + 3)
      if dx * dx + dy * dy < 1 then return true end
    end
  end
  return false
end

local N = 100000
local function bench(name, f, n)
  n = n or N
  local t0 = time()
  local r = f(n)
  local dt = time() - t0
  log(string.format("typed bench: %-34s %8.3f s  %7.3f us a call  result %s", name, dt, dt / n * 1e6, tostring(r)))
  return dt
end

local X0, Y0 = 20.5, 30.25
local M = 20000          -- blocked: fewer calls (the cart may run 20 million instructions in a frame)
local steps, res = {}, {}
local function pair(label, fa, fb, n)
  steps[#steps + 1] = function() res.a = bench(label .. " Lua", fa, n) end
  steps[#steps + 1] = function()
    res.b = bench(label .. " C", fb, n)
    log(string.format("typed bench: %s: C is %.2f times the speed of Lua", label, res.a / res.b))
  end
end
pair("solid_at (tables / data in C)",
  function(n) local s = 0; for i = 1, n do if solid_at(X0 + (i % 400) * 1.3, Y0 + (i % 300) * 1.7) then s = s + 1 end end; return s end,
  function(n) local s = 0; for i = 1, n do if typed_solid_at(X0 + (i % 400) * 1.3, Y0 + (i % 300) * 1.7) then s = s + 1 end end; return s end)
pair("blocked, one call each",
  function(n) local s = 0; for i = 1, n do if blocked(X0 + (i % 400) * 1.3, Y0 + (i % 300) * 1.7) then s = s + 1 end end; return s end,
  function(n) local s = 0; for i = 1, n do if typed_blocked(X0 + (i % 400) * 1.3, Y0 + (i % 300) * 1.7) then s = s + 1 end end; return s end, M)
pair("blocked, the loop in C",
  function(n) local s = 0; for i = 0, n - 1 do if blocked(X0 + i * 0.02, Y0 + i * 0.015) then s = s + 1 end end; return s end,
  function(n) return typed_blocked_many(n, X0, Y0, 0.02, 0.015) end, M)

local at = 0
function _update()
  at = at + 1
  if steps[at] then steps[at]() elseif at > #steps + 1 then quit() end
end

function _draw() cls(0) end
