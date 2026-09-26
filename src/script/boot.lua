-- bm33 boot script: runs once at startup, shows that Lua works and how fast.
local t0 = bm33.micros()
local function ms(t) return (bm33.micros() - t) / 1000 end

print(("\27[1;33m%s\27[0m on bm33 %s: %s floats, %d-bit integers"):format(
      _VERSION, bm33.version, math.type(0.5) == "float" and "double" or "?",
      math.ult(math.maxinteger, -1) and 64 or 32))

-- a few language features in one line each
local function fib(n) if n < 2 then return n end return fib(n-1) + fib(n-2) end
local words = {} for w in ("the quick brown fox"):gmatch("%a+") do words[#words+1] = w:upper() end
local co = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i * i) end end)
print(("2^10=%s 7//2=%d sqrt(2)=%.6f %s co:%d,%d,%d"):format(
      2^10, 7 // 2, math.sqrt(2), table.concat(words, " "), co(), co(), co()))

-- errors are caught, not fatal
local ok, err = pcall(function() return nil + 1 end)
print("pcall caught: " .. tostring(err))

-- micro benchmarks
local t = bm33.micros(); local r = fib(25)
local t_fib = ms(t)
t = bm33.micros(); local s = 0 for i = 1, 1000000 do s = s + i end
local t_loop = ms(t)
t = bm33.micros(); local tb = {} for i = 1, 100000 do tb[i] = (i * 7919) % 100003 end table.sort(tb)
local t_sort = ms(t)
t = bm33.micros(); local parts = {} for i = 1, 20000 do parts[#parts+1] = tostring(i) end
local str = table.concat(parts, ",")
local t_str = ms(t)
print(("Lua bench: fib(25)=%d %.0f ms, 1M adds %.0f ms,"):format(r, t_fib, t_loop))
print(("           sort 100k %.0f ms, 20k strings %.0f ms"):format(t_sort, t_str))

tb, parts, str = nil, nil, nil
collectgarbage()
local used, peak = bm33.mem()
print(("Lua memory: %d KiB used, %d KiB peak; boot.lua ran in %.0f ms")
      :format(used // 1024, peak // 1024, ms(t0)))
