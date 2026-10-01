-- The translator on its own (tests/nano8/run.py): every file given (the
-- code of a cart, P8SCII, as n8cartinfo --code writes it) must translate
-- and compile as Lua 5.4. luahost xlat.lua build/nano8/main.lua code...
SCREEN_W, SCREEN_H = 640, 360
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
