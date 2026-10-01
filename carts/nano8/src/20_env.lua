-- Env: the globals a cart sees. Most are the machine's own (n8.*, in C);
-- here the ones that belong to the console around it (time, stat, flip,
-- the pause menu items, persistent data, run and load) and the Lua basics
-- the carts may use.

local FLIP = {}           -- what flip() yields to Vm
Env.FLIP = FLIP

-- the n8 functions that are the console's, not the cart's
local CONSOLE = { blit = true, load = true, preview = true, drawlabel = true, snapshot = true, power = true,
                  setfps = true, buttons = true, compile = true, sndstat = true, pause = true, peekstr = true,
                  pokestr = true, glyph = true, text = true, traceback = true }

-- the symbols as constants: the buttons, and fill patterns (with the
-- transparency bit .5)
local GLYPHS = {
  [139] = 0, [145] = 1, [148] = 2, [131] = 3, [142] = 4, [151] = 5,
  [128] = 0x0000, [129] = 0x5a5a, [130] = 0x511f, [132] = 0x7d7d, [133] = 0xb81d, [134] = 0xf99f,
  [135] = 0x51bf, [136] = 0xb5bf, [137] = 0x999f, [138] = 0xb11f, [140] = 0xa0e0, [141] = 0x9b3f,
  [143] = 0xb1bf, [144] = 0xf5ff, [146] = 0xb15f, [147] = 0x1b1f, [149] = 0xf5bf, [150] = 0x7adf,
  [152] = 0x0f0f, [153] = 0x5555,
}
local BUTTON_GLYPHS = { [139] = true, [145] = true, [148] = true, [131] = true, [142] = true, [151] = true }

-- all(t): the iterator the carts use; deleting the item at hand (del)
-- does not skip the next one
local function all(t)
  if type(t) ~= "table" then return function() end end
  local i, last = 0, nil
  return function()
    if i == 0 or t[i] == last then i = i + 1 end
    last = t[i]
    return last
  end
end

-- inext(t, i): the iterator of ipairs, starting from nil ("for i, v in inext, t")
local function inext(t, i)
  i = (i or 0) + 1
  local v = t[i]
  if v ~= nil then return i, v end
end

local function foreach(t, f)
  for v in all(t) do f(v) end
end

-- stat(n): what the console knows
local function stat(n, a)
  n = floor(tonumber(n) or 0)
  if n == 0 then return min(collectgarbage("count"), 2047) end
  if n == 1 or n == 2 then return Vm.cpu or 0 end
  if n == 4 then return "" end
  if n == 5 then return 41 end
  if n == 6 then return Vm.param or "" end
  if n == 7 then return Vm.fps end
  if n == 8 or n == 9 then return Vm.fps end
  if n == 11 then return 1 end
  if (n >= 16 and n <= 26) or (n >= 46 and n <= 56) then return n8.sndstat(n) end
  if n == 57 then return n8.sndstat(57) ~= 0 end
  if n == 28 then return keydown(floor(tonumber(a) or 0)) end
  if n == 30 then return false end
  if n == 31 then return "" end
  if n >= 32 and n <= 36 then return 0 end
  if n >= 80 and n <= 95 then
    local d = { 2026, 1, 1, 0, 0, 0 }
    return d[(n - 80) % 10 + 1] or 0
  end
  if n == 100 or n == 101 then return nil end
  return 0
end

local function tostring_(v)
  return n8.tostr(v)
end

-- cartdata(id) -> true if there was data: 64 numbers at 0x5e00, kept on
-- the SD card under id
local function cartdata(id)
  if Vm.cartid then return false end
  id = tostring(id)
  Vm.cartid = id
  local d = Cfg.cartdata(id)
  if d then
    n8.pokestr(0x5e00, d)
    Vm.saved_data = d
    return true
  end
  Vm.saved_data = n8.peekstr(0x5e00, 256)
  return false
end

local function dget(i)
  i = floor(tonumber(i) or 0)
  if i < 0 or i > 63 then return 0 end
  return n8.peek4(0x5e00 + i * 4)
end

local function dset(i, v)
  i = floor(tonumber(i) or 0)
  if i < 0 or i > 63 then return end
  n8.poke4(0x5e00 + i * 4, v)
end

-- the guest's own copy of everything
function Env.make()
  local G = {}
  for k, v in pairs(n8) do
    if not CONSOLE[k] then G[k] = v end
  end
  -- what the translation calls
  G.__cat, G.__band, G.__bor, G.__bxor, G.__bnot = n8._cat, n8.band, n8.bor, n8.bxor, n8.bnot
  G.__shl, G.__shr, G.__lshr, G.__rotl, G.__rotr = n8.shl, n8.shr, n8.lshr, n8.rotl, n8.rotr
  G.__peek, G.__peek2, G.__peek4 = n8.peek, n8.peek2, n8.peek4
  G._cat = nil
  G.mapdraw = n8.map
  -- Lua
  G.pairs, G.ipairs, G.next, G.inext, G.type, G.select = pairs, ipairs, next, inext, type, select
  G.unpack, G.pack = table.unpack, table.pack
  G.setmetatable, G.getmetatable = setmetatable, getmetatable
  G.rawget, G.rawset, G.rawequal, G.rawlen = rawget, rawset, rawequal, rawlen
  G.assert, G.error, G.pcall, G.tostring = assert, error, pcall, tostring_
  G.cocreate, G.coresume, G.costatus, G.yield = coroutine.create, coroutine.resume, coroutine.status, coroutine.yield
  G.all, G.foreach, G.stat = all, foreach, stat
  -- the console
  G.time = function() return Vm.t end
  G.t = G.time
  G.flip = function() coroutine.yield(FLIP) end
  G.printh = function(s) log("nano8: " .. n8.tostr(s)) end
  G.menuitem = function(i, label, fn) Vm.menuitem(i, label, fn) end
  G.extcmd = function(cmd) Vm.extcmd(cmd) end
  G.cartdata, G.dget, G.dset = cartdata, dget, dset
  G.run = function() Vm.request = { what = "run" }; coroutine.yield(FLIP) end
  G.stop = function(msg) Vm.request = { what = "stop", msg = msg }; coroutine.yield(FLIP) end
  G.load = function(path, crumb, param) Vm.request = { what = "load", path = path, param = param }; coroutine.yield(FLIP) end
  G.reset = n8.reset
  G.serial = function() return 0 end
  G.holdframe = function() end
  G._set_fps = function(f) Vm.set_fps(f) end
  for code, v in pairs(GLYPHS) do
    if BUTTON_GLYPHS[code] then
      G[Xl.glyph_name(code)] = v
    else
      local f = v >= 0x8000 and v - 0x10000 or v
      G[Xl.glyph_name(code)] = code == 128 and 0 or f + 0.5
    end
  end
  return G
end
