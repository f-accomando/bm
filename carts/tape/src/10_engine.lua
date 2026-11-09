-- bm Tape engine: carts/tape/src/engine.b compiled on the console (native ARMv6).
-- The build puts the .b text at the placeholder line. If compile or load fails,
-- engine.err holds the text (shown on the status strip) and the cartridge still draws.
local ENGINE_SRC = [=====[
--@ENGINE_B@
]=====]

local engine = { err = nil, mod = nil, fn = {} }
local pat_buf = nil

local function fail(where, why)
  engine.mod = nil
  engine.err = where .. ": " .. tostring(why)
end

function engine.init()
  if not (native_compile and native_load and native_run and vm_buf) then
    fail("engine", "no native code on this device")
    return
  end
  local blob, why = native_compile(ENGINE_SRC)
  if not blob then fail("compile", why) return end
  local m, why2 = native_load(blob)
  if not m then fail("load", why2) return end
  for _, name in ipairs({ "euclid", "meter_fall" }) do
    engine.fn[name] = native_fn(m, name)
    if not engine.fn[name] then fail("load", "no function " .. name) return end
  end
  engine.mod = m
  pat_buf = vm_buf("i32", 64)
end

-- an i32 array of n elements for meter_fall (levels n, peaks 2 * n)
function engine.buf(n) return vm_buf("i32", n) end

-- euclidean pattern: a table pat[1..n] of 1 or 0 and the number of pulses; nil without engine
function engine.euclid(k, n, r)
  if not engine.mod then return nil end
  local ok, pulses = pcall(native_run, engine.mod, engine.fn.euclid, k, n, r, pat_buf)
  if not ok then fail("euclid", pulses) return nil end
  local pat = {}
  for i = 1, n do pat[i] = vm_buf_get(pat_buf, i) end
  return pat, pulses
end

-- one step of n meters; levels and peaks are engine.buf arrays, dt in ms (units in engine.b)
function engine.meter_fall(levels, peaks, n, dt)
  if not engine.mod then return false end
  local ok, why = pcall(native_run, engine.mod, engine.fn.meter_fall, levels, peaks, n, dt)
  if not ok then fail("meter", why) return false end
  return true
end
