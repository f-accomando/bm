-- Test of pic_due (carts/snes/src/pic.lua): run with build/host/luahost from the repository root.
dofile("carts/snes/src/pic.lua")
local fails = 0
local function check(name, ok) if not ok then fails = fails + 1; print("FAIL " .. name) end end
for ev = 1, 4 do
  local pics, par = 0, { [0] = 0, [1] = 0 }
  local last, maxgap, mingap = nil, 0, 1e9
  local N = 12000
  for f = 0, N - 1 do
    if pic_due(f, ev) then
      pics = pics + 1; par[f % 2] = par[f % 2] + 1
      if last then maxgap = math.max(maxgap, f - last); mingap = math.min(mingap, f - last) end
      last = f
    end
  end
  check("ev " .. ev .. ": the gap is ev or ev + 1", mingap >= ev and maxgap <= ev + 1)
  check("ev " .. ev .. ": the picture rate is within 1 in 3 * ev + 1 of 1 / ev", pics * ev >= N * 3 * ev / (3 * ev + 1) - ev and pics * ev <= N + ev)
  -- a sprite drawn on the even frames only (the blink of the game): it is in about half of the pictures, and never absent for more than a few seconds
  check("ev " .. ev .. ": both parities get pictures", par[0] > 0.3 * pics and par[1] > 0.3 * pics)
  local run, worst = 0, 0
  for f = 0, N - 1 do
    if pic_due(f, ev) then
      if f % 2 == 1 then run = run + 1; worst = math.max(worst, run) else run = 0 end
    end
  end
  check("ev " .. ev .. ": a sprite shown on the even frames only is not missing from more than 4 pictures in a row", worst <= 4)
end
print(fails == 0 and "pic_due: ok" or (fails .. " failed"))
os.exit(fails == 0 and 0 or 1)
