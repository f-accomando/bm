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

local function stats()
  local s = gpu3d_stats()
  check(type(s) == "table", "gpu3d_stats gives a table")
  for _, k in ipairs { "jobs", "bin_us", "render_us", "gltris", "clean_cyc", "inval_cyc", "pool_max", "ovf_max",
                       "ovf_jobs", "busy_us", "tris_in" } do
    check(math.type(s[k]) == "integer", "stats." .. k .. " is an integer")
  end
  for _, k in ipairs { "status", "probes", "options" } do
    check(type(s[k]) == "string", "stats." .. k .. " is a string")
  end
  check(s.options:find("bm3d", 1, true) ~= nil, "options holds the version")
  check(s.jobs > 0, "the cube drew a job")
  check(s.tris_in > 0, "the cube was given triangles")
  check(s.busy_us >= s.bin_us + s.render_us, "busy_us holds the timed jobs")
  local t = gpu3d_stats()
  check(t.jobs >= s.jobs, "reading does not reset")
  log("gpu3d_opt: options " .. s.options)
  log("gpu3d_opt: status " .. s.status)
end

local cube

function _init()
  opts()
  cube = mesh_cube(0x3080F0)
end

function _update()
  frame = frame + 1
end

function _draw()
  cls(0x102030)
  if frame == 2 then
    gpu3d(true)
    camera3d(0, 1, -4, 0, -0.2, 60)
    zclear()
    draw3d(cube, 0, 0, 0, 0.3, 0.2, 0)
  elseif frame == 3 then
    stats()                           -- the job of the frame before has ended
    gpu3d(false)
  elseif frame == 4 then
    log(string.format("gpu3d_opt: %d/%d checks passed", passed, total))
  end
end
