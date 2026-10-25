-- B3D-1: gpu3d_opt() on the V3D emulator (bmhost-gpu); the stats part comes with B3D-2.
local passed, total, frame = 0, 0, 0

local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("gpu3d_opt: FAIL " .. what) end
end

local HW = { "wc", "tex16", "t16", "etc1", "fs2", "attr" }       -- the GPU may refuse them
local ALWAYS = { "sort", "filter", "bilinear", "2d", "merge" }   -- a flag of the driver or the runtime

local function opts()
  -- no GPU asked for: false, false for every name
  gpu3d(false)
  for _, n in ipairs(HW) do
    local a, b = gpu3d_opt(n, true)
    check(a == false and b == false, n .. " on the ARM")
  end
  local ok = pcall(gpu3d_opt, "nonsense")
  check(not ok, "unknown name is an error (ARM)")

  local on = gpu3d(true)
  check(on == true, "the GPU draws (gpu3d(true))")
  if not on then return end
  ok = pcall(gpu3d_opt, "nonsense", true)
  check(not ok, "unknown name is an error (GPU)")
  for _, n in ipairs(ALWAYS) do
    local a, b = gpu3d_opt(n, true)
    check(a == true and b == true, n .. " on")
    a = gpu3d_opt(n)
    check(a == true, n .. " reads back")
    a = gpu3d_opt(n, false)
    check(a == false, n .. " off")
  end
  for _, n in ipairs(HW) do
    local a, b = gpu3d_opt(n, true)
    check(type(a) == "boolean" and type(b) == "boolean" and a == b, n .. " on: in_force = available")
    a = gpu3d_opt(n, false)
    check(a == false, n .. " off")
    log("gpu3d_opt: " .. n .. " on -> " .. tostring(b))
  end
  check(gpu3d_opt("t16") == gpu3d_opt("tex16"), "t16 is tex16")
  gpu3d(false)
end

function _init()
  opts()
end

function _update()
  frame = frame + 1
end

function _draw()
  cls(0)
  if frame == 3 then
    log(string.format("gpu3d_opt: %d/%d checks passed", passed, total))
  end
end
