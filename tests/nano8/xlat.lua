-- The translator on its own (tests/nano8/run.py): every file given (the
-- code of a cart, P8SCII, as n8cartinfo --code writes it) must translate
-- and compile as Lua 5.4. luahost xlat.lua build/nano8/main.lua code...
SCREEN_W, SCREEN_H = 640, 360
-- the cart's file builds its tables of names with n8 (the machine): a
-- stand-in with the same names is enough to translate
n8 = setmetatable({}, { __index = function() return function() end end })
for _, k in ipairs({ "cls", "pset", "spr", "map", "print", "rectfill", "sfx", "music", "btn", "btnp",
                     "peek", "poke", "tostr", "_cat", "band", "bor", "bxor", "bnot", "shl", "shr",
                     "lshr", "rotl", "rotr", "map" }) do
  rawset(n8, k, function() end)
end
assert(loadfile(arg[1]))()
local Xl = NANO8.Xl
local bad = 0
for i = 2, #arg do
  local f = assert(io.open(arg[i], "rb"))
  local code = f:read("a")
  f:close()
  local name = arg[i]:match("[^/]+$")
  local out, err = Xl.translate(code)
  if out then
    local fn, cerr = load(out, "=" .. name, "t", {})
    if not fn then out, err = nil, cerr end
  end
  if out then
    print(("  %-36s %6d bytes -> %6d"):format(name, #code, #out))
  else
    print(("  %-36s FAILED: %s"):format(name, err))
    bad = bad + 1
  end
end
os.exit(bad == 0 and 0 or 1)
