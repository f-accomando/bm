-- A fake bm for the host tests of Yharnam (sim.lua, balance.lua): the same
-- API as the console, no pixels. Its state is in the table it returns:
-- B.held / B.prev (the buttons of this frame and of the one before, bits as
-- btn(); bits 10 and 11 are L1 and R1 for pad()), B.now (time()), B.px
-- (sprite pixels drawn), B.levels (light levels set by fades), B.logs.
--
--   local B = dofile("tests/yharnam/fakebm.lua")
--   local Y = B.load("carts/yharnam/main.lua")    -- the cartridge's YHARNAM table

local B = { held = 0, prev = 0, now = 0, logs = {}, px = 0, SHEET_W = 4096, SHEET_H = 4096 }
local SHEET_W, SHEET_H = B.SHEET_W, B.SHEET_H
local MAPW, MAPH = 256, 256
local map = {}
local env = {}
B.env = env

local function chk(cond, msg) if not cond then error(msg, 3) end end
local function num(v, name) chk(type(v) == "number", (name or "argument") .. ": number expected, got " .. type(v)) end

for _, n in ipairs({ "cls", "line", "rect", "circ", "camera", "clip", "note", "noteoff", "freq", "quit",
                     "dark_begin", "dark_end", "timeslice" }) do
  env[n] = function() end
end
env.pset = function(x, y, c) num(x, "pset x"); num(y, "pset y"); num(c, "pset colour") end
env.rectfill = function(x, y, w, h, c) num(x); num(y); num(w); num(h); num(c, "rectfill colour") end
env.circfill = function(x, y, r, c) num(x); num(y); num(r); num(c, "circfill colour") end
env.sspr = function(sx, sy, sw, sh, dx, dy)
  num(sx, "sspr sx"); num(sy, "sspr sy"); num(sw); num(sh); num(dx, "sspr dx"); num(dy, "sspr dy")
  chk(sx >= 0 and sy >= 0 and sx + sw <= SHEET_W and sy + sh <= SHEET_H, "sspr outside the sheet")
  B.px = B.px + sw * sh
end
env.spr = function(n, x, y, w, h)
  num(n, "spr n"); num(x, "spr x"); num(y, "spr y")
  chk(math.type(n) == "integer" and n > 0 and n < (SHEET_W // 8) * (SHEET_H // 8), "spr: bad cell " .. tostring(n))
  B.px = B.px + 64 * (w or 1) * (h or 1)
end
env.map = function(mx, my, x, y, mw, mh)
  num(mx); num(my); num(x); num(y); num(mw); num(mh)
  chk(mx >= 0 and my >= 0 and mx + mw <= MAPW and my + mh <= MAPH, "map: outside the map")
end
env.mset = function(x, y, n)
  chk(math.type(x) == "integer" and math.type(y) == "integer" and math.type(n) == "integer", "mset: integers")
  chk(x >= 0 and y >= 0 and x < MAPW and y < MAPH, "mset outside the map")
  map[y * MAPW + x] = n
end
env.glow = function(x, y, r, lv, d)
  num(x, "glow x"); num(y, "glow y"); num(r, "glow radius")
  chk(math.type(lv) == "integer" and lv >= 0 and lv < 16, "glow: the level is an integer 0..15")
end
env.fades = function(t)
  chk(#t > 0 and #t <= 255, "fades: 1..255 colours")
  local levels = #t[1] - 1
  for _, row in ipairs(t) do
    chk(#row == levels + 1, "fades: rows of the same length")
    for _, c in ipairs(row) do chk(math.type(c) == "integer" and c >= 0 and c <= 0xFFFFFF, "fades: colours") end
  end
  B.levels = levels
  return levels
end
env.SCREEN_W, env.SCREEN_H = 256, 256
env.SQUARE, env.TRIANGLE, env.SAW, env.NOISE, env.SINE, env.METAL = 0, 1, 2, 3, 4, 5
env.print = function(s, x, y, c)
  num(x, "print x"); num(y, "print y")
  return x + #tostring(s) * 8
end
env.log = function(...)
  local t = { ... }
  for i = 1, #t do t[i] = tostring(t[i]) end
  B.logs[#B.logs + 1] = table.concat(t, "\t")
end
env.time = function() return B.now end
env.stat = function() return 0 end
env.btn = function(b) return (B.held >> b) & 1 == 1 end
env.prompt = function(name, x, y) num(x, "prompt x"); num(y, "prompt y"); return x + 16 end
env.lastinput = function() return nil end
-- pad(): bits 1024 L1, 2048 R1 from the held mask's bits 10 and 11
env.pad = function() return B.held & (1024 | 2048) end
env.btnp = function(b) return (B.held >> b) & 1 == 1 and (B.prev >> b) & 1 == 0 end
for _, lib in ipairs({ "string", "table", "math", "utf8", "coroutine" }) do env[lib] = _G[lib] end
for _, f in ipairs({ "assert", "error", "ipairs", "next", "pairs", "pcall", "rawequal", "rawget", "rawlen",
                     "rawset", "select", "setmetatable", "getmetatable", "tonumber", "tostring", "type", "xpcall" }) do
  env[f] = _G[f]
end

-- a call into the cartridge: an error stops the test, with the last logs
function B.call(fn)
  local ok, e = xpcall(fn, debug.traceback)
  if not ok then
    io.stderr:write(e .. "\n")
    for i = math.max(1, #B.logs - 5), #B.logs do io.stderr:write("log: " .. B.logs[i] .. "\n") end
    os.exit(1)
  end
end

-- the cartridge, run once (its main chunk): its YHARNAM table
function B.load(src)
  local f = assert(io.open(src))
  local code = f:read("a")
  f:close()
  local chunk, err = load(code, "=main.lua", "t", env)
  if not chunk then io.stderr:write(err .. "\n") os.exit(1) end
  B.call(chunk)
  assert(env.YHARNAM, "the cartridge has no YHARNAM table for the tests")
  return env.YHARNAM
end

-- one frame (60 a second) with these buttons held; B.nodraw: _update only
function B.frame(buttons)
  B.prev, B.held = B.held, buttons
  B.call(env._update)
  if not B.nodraw then B.call(env._draw) end
  B.now = B.now + 1 / 60
end

return B
