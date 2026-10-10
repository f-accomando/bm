-- A fake bm for the host tests of Yharnam (sim.lua, balance.lua): the same
-- API as the console, no pixels. Its state is in the table it returns:
-- B.held / B.prev (the buttons of this frame and of the one before, bits as
-- btn(); bits 10 and 11 are L1 and R1 for pad()), B.now (time()), B.px
-- (sprite pixels drawn), B.levels (light levels set by fades), B.logs,
-- B.writes (mset calls by layer), B.on_map (called with every map() call).
-- The map is the console's: 256 x 256 cells, layers with names (mlayers),
-- one empty layer "main" unless B.load_map gives the cartridge's.
--
--   local B = dofile("tests/yharnam/fakebm.lua")
--   B.load_map({ { "ground", "carts/yharnam/map_ground.csv" }, ... })   -- (a drawn map: before B.load)
--   local Y = B.load("carts/yharnam/main.lua")    -- the cartridge's YHARNAM table

local B = { held = 0, prev = 0, now = 0, logs = {}, px = 0, SHEET_W = 4096, SHEET_H = 4096, writes = {} }
local SHEET_W, SHEET_H = B.SHEET_W, B.SHEET_H
local MAPW, MAPH = 256, 256
local names, layers = { "main" }, { {} }   -- the map's layers: their names, their cells ([y * MAPW + x])
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
-- the layer of an argument, as the console's: absent the first, a number from 1 or a name
local function layer(l)
  if l == nil then return 1 end
  if type(l) == "string" then
    for i, n in ipairs(names) do if n == l then return i end end
    error("the map has no layer \"" .. l .. "\"", 3)
  end
  chk(math.type(l) == "integer" and l >= 1 and l <= #names, "the map has " .. #names .. " layers, not " .. tostring(l))
  return l
end
env.map = function(mx, my, x, y, mw, mh, l)
  num(mx); num(my); num(x); num(y); num(mw); num(mh)
  chk(mx >= 0 and my >= 0 and mx + mw <= MAPW and my + mh <= MAPH, "map: outside the map")
  local i = layer(l)
  if B.on_map then B.on_map(mx, my, x, y, mw, mh, i) end
end
-- (lean: sim.lua counts the instructions run in here as the cartridge's)
local writes = B.writes
env.mset = function(x, y, n, l)
  chk(math.type(x) == "integer" and math.type(y) == "integer" and math.type(n) == "integer", "mset: integers")
  chk(x >= 0 and y >= 0 and x < MAPW and y < MAPH and n >= 0 and n < 65536, "mset outside the map, or not a cell")
  local i = (l == nil or l == 1) and 1 or layer(l)
  layers[i][y * MAPW + x] = n
  writes[i] = (writes[i] or 0) + 1
end
env.mget = function(x, y, l)
  local i = layer(l)
  if x < 0 or y < 0 or x >= MAPW or y >= MAPH then return 0 end
  return layers[i][y * MAPW + x] or 0
end
env.msize = function(w)
  chk(w == nil, "msize(w, h): not in the fake bm")
  return MAPW, MAPH, #names
end
-- mlayers(list): a name keeps the layer of that name (or a new empty one), {name, from} copies `from`
env.mlayers = function(list)
  if list then
    chk(#list >= 1 and #list <= 8, "mlayers: 1 to 8 layers")
    local old, seen, nn, nl = {}, {}, {}, {}
    for i, n in ipairs(names) do old[n] = layers[i] end
    for i, e in ipairs(list) do
      local name, from = e, e
      if type(e) == "table" then name, from = e[1], e[2] end
      chk(type(name) == "string" and #name >= 1 and #name <= 16, "mlayers: a layer's name is 1 to 16 bytes")
      chk(not seen[name], "mlayers: two layers called \"" .. name .. "\"")
      seen[name] = true
      local src = type(from) == "string" and old[from] or math.type(from) == "integer" and layers[from] or nil
      if type(e) == "table" and src then
        local c = {}
        for k, v in pairs(src) do c[k] = v end
        src = c
      end
      nn[i], nl[i] = name, src or {}
    end
    names, layers = nn, nl
  end
  return table.move(names, 1, #names, 1, {})
end

-- the cartridge's map (mkbm.py --map name=file.csv for each layer, the first one first): its layers from
-- CSV files, before B.load; every layer is MAPW x MAPH here, as Yharnam's
function B.load_map(list)
  names, layers = {}, {}
  for i, e in ipairs(list) do
    local cells, y = {}, 0
    for line in io.lines(e[2]) do
      line = line:gsub("[\r\n]", "")
      if line:find("%S") and not line:match("^#") then
        local x = 0
        for v in (line .. ","):gmatch("([^,]*),") do
          local n = math.tointeger(tonumber(v))
          chk(n and n >= 0 and n < 65536, e[2] .. ": not a cell at row " .. y .. ": " .. v)
          if n ~= 0 then cells[y * MAPW + x] = n end
          x = x + 1
        end
        chk(x == MAPW, e[2] .. ": row " .. y .. " has " .. x .. " cells, not " .. MAPW)
        y = y + 1
      end
    end
    chk(y == MAPH, e[2] .. ": " .. y .. " rows, not " .. MAPH)
    names[i], layers[i] = e[1], cells
  end
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
env.SCREEN_W, env.SCREEN_H = 360, 360
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
-- "ok" and "back" (the system's yes and back) are A and B, as on the Pi with a pad
local NAMED = { ok = 4, back = 5, a = 4, b = 5, x = 6, y = 7 }
local function bit(b) return type(b) == "string" and assert(NAMED[b], "unknown button " .. b) or b end
env.btn = function(b) return (B.held >> bit(b)) & 1 == 1 end
env.prompt = function(name, x, y)
  if type(x) ~= "number" then return 16, 16 end          -- prompt(name, small, scale, player): its size
  num(x, "prompt x"); num(y, "prompt y"); return x + 16
end
env.keyp = function() return nil end
env.lastinput = function() return nil end
-- pad(): bits 1024 L1, 2048 R1 from the held mask's bits 10 and 11
env.pad = function() return B.held & (1024 | 2048) end
env.btnp = function(b) b = bit(b); return (B.held >> b) & 1 == 1 and (B.prev >> b) & 1 == 0 end
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
