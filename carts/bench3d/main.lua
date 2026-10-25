-- 3D Bench, Lua edition: the 3D benchmark of bm3D as a cartridge (docs/BENCH3D.md, "Lua edition").
--
-- It ports the ramp of src/bm/b3d.c as a state machine over _draw frames: each test runs on each
-- profile with a load n that grows (1, 2, 3, 4, then +1/3) until a frame takes more than 40 ms; the
-- loads at 60 and 30 frames a second come from the median ms of 6 frames a step. The profiles set the
-- GPU driver with gpu3d() and gpu3d_opt(). The report is the C bench's text and goes out with report().
--
-- Keys of bm/config.txt (the first word of the title "3D Bench" gives the prefix `3d_`; a value has no
-- comma, so a list is written with `/`: `3d_tests=spheres/match`, `3d_profiles=ARM/GPU/VS`):
--   3d_tests     the tests to run (default: all of this edition)
--   3d_profiles  the profiles, by name (GPU+VS) or short name (VS)
--   3d_nowait    1: the summary stays 3 s, then the cartridge ends (for a script)
--
-- Not in this edition (decision of 25 Oct): the QPU rows, the PMU counters, the machine line, the Mali
-- rows. Columns that need the counters are 0 in the R lines.

local W, H = 640, 360
local LIMIT_MS, MS60, MS30 = 40.0, 1000 / 60, 1000 / 30
local RAMP_S = 25                       -- a ramp stops after 25 s
local FRAMES = 6                        -- measured frames a step (and one warm frame before them)
local MAXSTEPS = 64

local floor, max, min, sqrt, ceil = math.floor, math.max, math.min, math.sqrt, math.ceil
local fmt = string.format

-- ---------------------------------------------------------------- numbers (b3d.c: b3d_median, b3d_load_at, b3d_slope)

local function median(v)
  local t = {}
  for i = 1, #v do t[i] = v[i] end
  table.sort(t)
  local c = #t
  if c == 0 then return 0 end
  if c % 2 == 1 then return t[(c + 1) // 2] end
  return 0.5 * (t[c // 2] + t[c // 2 + 1])
end

-- the load at which the cost crosses `limit` (the ms of the steps are not always sorted: the last
-- step under the limit counts), by a line between the step and the next; -1: even the lightest is too slow
local function load_at(n, ms, count, limit)
  local last = 0
  for i = 1, count do
    if ms[i] <= limit then last = i end
  end
  if last == 0 then return -1 end
  if last == count then return n[last] end
  if ms[last + 1] <= ms[last] then return n[last] end
  return n[last] + (limit - ms[last]) / (ms[last + 1] - ms[last]) * (n[last + 1] - n[last])
end

-- the least-squares line y = a + b x: b, a; -1, -1 with fewer than two distinct x
local function slope(x, y, count)
  local sx, sy, sxx, sxy = 0, 0, 0, 0
  for i = 1, count do
    sx = sx + x[i]; sy = sy + y[i]
    sxx = sxx + x[i] * x[i]; sxy = sxy + x[i] * y[i]
  end
  local d = count * sxx - sx * sx
  if count < 2 or d <= 1e-9 * sxx then return -1, -1 end
  local b = (count * sxy - sx * sy) / d
  return b, (sy - b * sx) / count
end

local function f1(v)
  if v < 0 then return "-1" end
  local c = floor(v * 10 + 0.5)
  return fmt("%d.%d", c // 10, c % 10)
end

local function f2(v)
  if v < 0 then return "-1" end
  local c = floor(v * 100 + 0.5)
  return fmt("%d.%02d", c // 100, c % 100)
end

-- ---------------------------------------------------------------- profiles (b3d.c: prof[], prof_on, prof_off)

-- gpu: the GPU draws; vs: vertex shader level; queue: 0, 1 or 2 jobs; the rest are gpu3d_opt() switches
local PROF = {
  { name = "ARM",       short = "ARM",  vs = 0, queue = 0 },
  { name = "GPU",       short = "GPU",  gpu = true, vs = 0, queue = 0 },
  { name = "GPU+VS",    short = "VS",   gpu = true, vs = 2, queue = 0 },
  { name = "GPU+VS+Q",  short = "VSQ",  gpu = true, vs = 2, queue = 1 },
  { name = "GPU+VS+Q2", short = "VSQ2", gpu = true, vs = 2, queue = 2 },
  { name = "GPU+FS2",   short = "FS2",  gpu = true, vs = 0, queue = 0, fs2 = true },
  { name = "GPU+VS+A8", short = "VSA",  gpu = true, vs = 2, queue = 0, fs2 = true, attr = true },
  { name = "GPU+WC",    short = "WC",   gpu = true, vs = 0, queue = 0, wc = true },
  { name = "GPU+2D",    short = "2D",   gpu = true, vs = 0, queue = 0, two_d = true },
  { name = "GPU+T16",   short = "T16",  gpu = true, vs = 0, queue = 0, t16 = true },
  { name = "GPU+ETC1",  short = "ETC1", gpu = true, vs = 0, queue = 0, t16 = true, etc1 = true },
}
local ALIAS = { ["A8"] = "VSA", ["GPU+A8"] = "VSA" }    -- the task's name of the attribute row

-- the driver version a profile reproduces (b3d.c: prof_version)
local function prof_version(p)
  if p.etc1 then return "7.3" elseif p.attr then return "7.1" elseif p.vs > 0 and p.fs2 then return "6.6"
  elseif p.fs2 then return "5.3" elseif p.t16 then return "5.2" elseif p.queue == 2 then return "5.1"
  elseif p.two_d then return "4.8" elseif p.wc then return "4.5" end
  if not p.gpu then return "0.2" end
  if p.queue > 0 then return "4.1" end
  return p.vs >= 2 and "3.4" or p.vs == 1 and "3.0" or "2.1"
end

local SWITCHES = { { "wc", "wc" }, { "tex16", "t16" }, { "etc1", "etc1" }, { "fs2", "fs2" }, { "sort", "sort" },
                   { "attr", "attr" } }

local function opts_off()
  for _, s in ipairs(SWITCHES) do gpu3d_opt(s[1], false) end
  gpu3d_opt("filter", false)
  gpu3d_opt("merge", false)           -- the C bench never merges draws (a run of 100 tiles would)
end

-- the profile's switches on the driver; false and the reason if the profile has no row here
local function prof_on(p)
  if not p.gpu then
    gpu3d(false, false, 0, 0, false, false)
    return true
  end
  local on, _, _, _, level, _, _, probe = gpu3d(true, false, p.vs, p.queue > 0 and p.queue or false, false, false)
  if not on then return false, "no GPU" end
  if p.vs > (probe or 0) then return false, "the vertex shader level is not there" end
  if p.queue > 0 and (not level or level < p.queue) then return false, "the frame queue is not there" end
  for _, s in ipairs(SWITCHES) do
    local key = s[2]
    local now, avail = gpu3d_opt(s[1], p[key] and true or false)
    if p[key] and not (now and avail) then return false, s[1] .. " is not possible on this GPU" end
  end
  gpu3d_opt("filter", false)
  gpu3d_opt("merge", false)
  gpu3d_opt("2d", p.two_d and true or false)
  return true
end

-- ---------------------------------------------------------------- meshes (b3d.c: meshes_make)

local sphere, quad, tile, hero
local flags = 0

-- the sheet: a 128x128 checker in two colours (b3d.c: make_sheet, variant 0)
local function make_sheet()
  local pair = { 0x2060D0, 0xF0D040 }
  for y = 0, 127 do
    for x = 0, 127 do sset(x, y, pair[(((x >> 3) ~ (y >> 3)) & 1) + 1]) end
  end
end

-- a quad of 2 x 1.125 units (16:9) in the plane z = 0, facing -z
local function make_quad(colour)
  return mesh({ -1, -0.5625, 0, 1, -0.5625, 0, 1, 0.5625, 0, -1, 0.5625, 0 },
              { 1, 3, 2, colour, 1, 4, 3, colour })
end

-- a flat tile of k x k squares, side 1, in the plane y = 0, facing up, textured as b3d.c's `tile`
local function make_tile(k)
  local v, f, uv = {}, {}, {}
  for z = 0, k do
    for x = 0, k do
      v[#v + 1] = x / k - 0.5; v[#v + 1] = 0; v[#v + 1] = z / k - 0.5
    end
  end
  local function corner(i)             -- the sheet's pixel of a vertex (0-based index)
    uv[#uv + 1] = (i * 13 % 8) * 16
    uv[#uv + 1] = (i * 7 % 8) * 16
  end
  for z = 0, k - 1 do
    for x = 0, k - 1 do
      local a = z * (k + 1) + x
      local b, c = a + 1, a + k + 1
      local d = c + 1
      f[#f + 1] = a + 1; f[#f + 1] = c + 1; f[#f + 1] = b + 1; f[#f + 1] = -1
      corner(a); corner(c); corner(b)
      f[#f + 1] = b + 1; f[#f + 1] = c + 1; f[#f + 1] = d + 1; f[#f + 1] = -1
      corner(b); corner(c); corner(d)
    end
  end
  return mesh(v, f, uv)
end

-- the 16 parts of a hero: x y z of the part's middle, its size
local PART = {
  { 0, 1.0, 0, 0.32 }, { 0, 1.45, 0, 0.30 }, { 0, 1.85, 0, 0.18 }, { 0, 0.62, 0, 0.24 },
  { -0.35, 1.5, 0, 0.12 }, { -0.42, 1.2, 0, 0.11 }, { -0.45, 0.95, 0, 0.09 },
  { 0.35, 1.5, 0, 0.12 }, { 0.42, 1.2, 0, 0.11 }, { 0.45, 0.95, 0, 0.09 },
  { -0.15, 0.5, 0, 0.13 }, { -0.15, 0.25, 0, 0.12 }, { -0.15, 0.05, 0.05, 0.08 },
  { 0.15, 0.5, 0, 0.13 }, { 0.15, 0.25, 0, 0.12 }, { 0.15, 0.05, 0.05, 0.08 },
}
local GLOSSY, EMISSIVE = 0x20000000, 0x40000000

-- a hero: 16 spheres of 96 faces (6 rings, 8 segments, as r3d_mesh_sphere) in one mesh. The C bench's
-- hero has one bone a part and moves them; mesh() has no bones, so this one stands still
local function make_hero()
  local rings, segs = 6, 8
  local v, f = {}, {}
  for b = 1, 16 do
    local p = PART[b]
    local base = #v // 3
    for r = 0, rings do
      local th = math.pi * r / rings
      for s = 0, segs - 1 do
        local ph = 2 * math.pi * s / segs
        v[#v + 1] = p[1] + math.sin(th) * math.cos(ph) * p[4]
        v[#v + 1] = p[2] + math.cos(th) * p[4]
        v[#v + 1] = p[3] + math.sin(th) * math.sin(ph) * p[4]
      end
    end
    local t = 0
    for r = 0, rings - 1 do
      for s = 0, segs - 1 do
        local a = r * segs + s
        local bb = r * segs + (s + 1) % segs
        local c, d = a + segs, bb + segs
        local col = ((r + s) & 1) == 1 and 0xC05040 or 0x4070C0
        for k = 0, 1 do
          local colour = col
          if b <= 3 and (t + k) % 2 == 1 then colour = colour | GLOSSY end
          if b == 3 and t + k < 8 then colour = colour | EMISSIVE end
          if k == 0 then
            f[#f + 1] = base + a + 1; f[#f + 1] = base + bb + 1; f[#f + 1] = base + c + 1
          else
            f[#f + 1] = base + bb + 1; f[#f + 1] = base + d + 1; f[#f + 1] = base + c + 1
          end
          f[#f + 1] = colour
        end
        t = t + 2
      end
    end
  end
  return mesh(v, f)
end

-- ---------------------------------------------------------------- scenes (b3d.c: spheres, quads, match)

local function cls3d()
  cls(0x0A0A1E)
  zclear()
end

local function scene_light()
  light3d(-0.4, 0.7, -0.6, 0.3)
  sky3d(0xFFFFFF, 0xFFFFFF, 0xFFFFFF)
  shine3d(0.6, 16, 0)
  fog3d()
  lamp3d()
end

local function shine_light()
  sky3d(0xFFF0D0, 0x90B0FF, 0x806040)
  shine3d(0.6, 16, 0.5)
  fog3d(0x9090A0, 6, 14)
  for i = 0, 3 do
    lamp3d(i + 1, -3.0 + 2.0 * i, 0.5, -1, 2.5, 0.8, 0xFF8040 >> (i * 4))
  end
end

-- n spheres of 96 faces on a grid in front of the camera (the stress test's)
local function spheres(n, f)
  cls3d()
  camera3d(0, 0, -8, 0, 0, 60)
  local side = ceil(sqrt(n))
  local step = 9.0 / (side > 1 and side or 1)
  for i = 0, n - 1 do
    local x = -4.5 + step * ((i % side) + 0.5)
    local y = 2.6 - step * 0.56 * ((i // side) + 0.5)
    draw3d(sphere, x, y, i % 3, f * 0.03 + i, f * 0.05, 0, step * 0.45, flags)
  end
  return 0
end

-- quads of 320x180 pixels, one per quadrant in turn, each nearer than the one before: every pixel drawn
local FOCAL = (W * 0.5) / math.tan(math.rad(60) / 2)
local quad_step = 0.9985
local function quads(n, f)
  cls3d()
  camera3d(0, 0, 0, 0, 0, 60)
  light3d(0.3, 0.4, -1, 0.3)
  local k = 160.0 / FOCAL
  local d = 40.0
  for i = 0, n - 1 do
    local x = (i & 1) ~= 0 and k * d or -k * d
    local y = (i & 2) ~= 0 and -0.5625 * k * d or 0.5625 * k * d
    draw3d(quad, x, y, d, 0, 0, 0, k * d, flags)
    d = d * quad_step
  end
  scene_light()
  return n * 320 * 180
end

-- a match: a map of 10 x 10 pieces, n heroes with their shadows, a model in first person, a HUD over the 3D
local HUD_BG = 0x141E28
local function match(n, f)
  cls3d()
  camera3d(0.3 * math.sin(f * 0.02), 1.7, -5, 0.05, -0.12, 70)
  shine_light()
  shadow3d(1)
  for i = 0, 9 do
    for j = 0, 9 do
      draw3d(tile, -27.0 + 6.0 * i, 0, -6.0 + 6.0 * j, 0, 0, 0, 6, 0)
    end
  end
  local row = 8
  for i = 0, n - 1 do
    local x = -4.2 + 1.2 * (i % row) + 0.6 * ((i // row) % 2)
    local z = 1.0 + 1.5 * (i // row)
    draw3d(hero, x, 0, z, 0, i, 0, 1, 8)            -- the shadow on the floor
    draw3d(hero, x, 0, z, 0, i, 0, 1, 4)            -- smooth
  end
  draw3d(hero, 0.5, 0.2, -4.2, 0, 3.0, 0, 0.5, 4 | 64)    -- in first person
  scene_light()
  rectfill(8, H - 30, 180, 20, HUD_BG)
  rectfill(W - 120, H - 30, 110, 20, HUD_BG)
  print("HUD 250/250", 12, H - 28, 0xFFFFFF)
  return 0
end

-- id, name, unit, start, max, the profiles that run it (by short name), setup, frame, quad_px (the cost of a pixel)
local TESTS = {
  { id = "spheres", name = "spheres, 96 faces, flat", unit = "spheres", start = 1, max = 8000,
    profiles = { ARM = 1, GPU = 1, VS = 1, WC = 1, VSA = 1 },
    setup = function()
      sphere = mesh_sphere(6, 8, 0x4080FF, 0xFFC040)      -- 96 faces, as the stress test
      flags = 0
    end,
    frame = spheres },
  { id = "quad_flat", name = "fill: quads 320x180, flat", unit = "quads", start = 1, max = 4000,
    profiles = { ARM = 1, GPU = 1, VS = 1 },
    setup = function()
      flags = 0
      quad = make_quad(0x80C0FF)
      quad_step = (gpu3d()) and 0.9985 or 0.985           -- b3d.c: the GPU's quads sit closer together
    end,
    frame = quads, quad_px = true },
  -- the C bench's `match` also runs GPU+Q, AA, VS1, VSS: not in this edition. VSQ and VSQ2 are added here
  { id = "match", name = "a match: map, heroes, shadows, HUD", unit = "heroes", start = 1, max = 256,
    profiles = { ARM = 1, GPU = 1, VS = 1, VSQ = 1, VSQ2 = 1, WC = 1, ["2D"] = 1, T16 = 1, ETC1 = 1, FS2 = 1, VSA = 1 },
    setup = function()
      tile = tile or make_tile(4)                          -- the map's piece: 32 faces, textured
      hero = hero or make_hero()
    end,
    frame = match },
}

-- ---------------------------------------------------------------- the choice of tests and profiles

local function cfg(key)
  local ok, v = pcall(cart_config, key)
  if ok and v and v ~= "" then return v end
end

local function words(s)
  local t = {}
  for w in s:gmatch("[^/ ,;]+") do t[#t + 1] = w end
  return t
end

local only_tests, only_profiles = cfg("3d_tests"), cfg("3d_profiles")

local function test_wanted(id)
  if not only_tests or only_tests == "all" then return true end
  for _, w in ipairs(words(only_tests)) do
    if w == id then return true end
  end
  return false
end

local function prof_wanted(p)
  if not only_profiles or only_profiles == "all" then return true end
  local function match1(w)
    w = ALIAS[w] or w
    return w == p.name or w == p.short
  end
  for _, w in ipairs(words(only_profiles)) do
    if match1(w) then return true end
    if w:find("+", 1, true) then                  -- "ARM+GPU": short names with plus signs
      for part in w:gmatch("[^+]+") do
        if match1(part) and not w:find(p.name, 1, true) then return true end
      end
    end
  end
  return false
end

local plan = {}                                  -- { test, profile } in the order of the report
for _, t in ipairs(TESTS) do
  if test_wanted(t.id) then
    for _, p in ipairs(PROF) do
      if t.profiles[p.short] and prof_wanted(p) then plan[#plan + 1] = { t, p } end
    end
  end
end

-- ---------------------------------------------------------------- the runner

local nowait = cfg("3d_nowait") ~= nil and cfg("3d_nowait") ~= "0"
local res = {}                                   -- res[test.id][profile.short] = the ramp's result
local ramp                                       -- the ramp running
local k = 0                                      -- its place in the plan
local phase = "run"                              -- "run", then "done"
local t_start, t_done
local opts_start, orig_gpu, orig_2d
local sent
local last_ms = 0
local rep_text

local function stat_or0(i)
  return stat(i) or 0
end

-- starts the ramp of plan[k]; false (and a line in the log) when the profile has no row on this console
local function ramp_start(t, p)
  local ok, why = prof_on(p)
  if not ok then
    log(fmt("b3d-skip %s %s: %s", t.id, p.name, why))
    return nil
  end
  scene_light()
  shadow3d(0)
  t.setup()
  return { t = t, p = p, n = t.start, f = 0, last_f = -1, fms = {}, samples = {}, t0 = time(), gms = 0, wms = 0 }
end

-- the sample of the step that ended (its frames are in r.fms)
local function step_end(r)
  local s1 = gpu3d_stats()
  local s0 = r.s0 or s1
  local a = { n = r.n }
  a.ms = median(r.fms)
  a.worst = 0
  for i = 1, #r.fms do a.worst = max(a.worst, r.fms[i]) end
  local kf = 1 / FRAMES
  a.tris_in, a.tris, a.verts, a.pixels = s1.tris_in, r.tris, r.verts, r.pixels
  a.gltris = (s1.gltris - s0.gltris) * kf
  a.gpu_ms = r.gms * kf
  a.jobs = (s1.jobs - s0.jobs) * kf
  a.bin_us = (s1.bin_us - s0.bin_us) * kf
  a.rnd_us = (s1.render_us - s0.render_us) * kf
  a.clean_kcyc = (s1.clean_cyc - s0.clean_cyc) * kf / 1000
  a.inval_kcyc = (s1.inval_cyc - s0.inval_cyc) * kf / 1000
  a.pool_kb = (s1.pool_max + 1023) // 1024
  a.ovf_kb = (s1.ovf_max + 1023) // 1024
  a.ovf_jobs = s1.ovf_jobs - s0.ovf_jobs
  a.fill = r.fill
  r.samples[#r.samples + 1] = a
  -- the line of b3d.c ("b3d <id> <profile> n=...": the counters of the ARM are not in this edition)
  log(fmt("b3d %s %s n=%d %d.%02d ms worst %d.%02d, %d tri, %d vtx, 0 instr, GPU %d.%02d ms, %d jobs, 0 meshes, core 0 GPU 0 MHz",
          r.t.id, r.p.name, a.n, floor(a.ms), floor(a.ms * 100) % 100, floor(a.worst), floor(a.worst * 100) % 100,
          floor(a.tris), floor(a.verts), floor(a.gpu_ms), floor(a.gpu_ms * 100) % 100, floor(a.jobs)))
  last_ms = a.ms
end

-- the ramp's result from its samples (b3d.c: the end of ramp())
local function ramp_result(r)
  local s, ns = r.samples, #r.samples
  local o = { ran = ns > 0, nsamples = ns, secs = time() - r.t0 }
  if ns == 0 then return o end
  o.over = s[ns].ms <= LIMIT_MS
  o.last_n = s[ns].n
  local sn, sms, stri, sdrawn = {}, {}, {}, {}
  for i = 1, ns do
    sn[i], sms[i], stri[i], sdrawn[i] = s[i].n, s[i].ms, s[i].tris_in, s[i].tris
  end
  o.n60 = load_at(sn, sms, ns, MS60)
  o.n30 = load_at(sn, sms, ns, MS30)
  o.ns_gpu, o.ns_ms, o.gpu0_ms = -1, -1, -1
  if r.t.quad_px then
    local sfill, sgpu = {}, {}
    for i = 1, ns do sfill[i], sgpu[i] = s[i].fill, s[i].gpu_ms end
    o.ns_ms = slope(sfill, sms, ns) * 1e6
    if r.p.gpu and r.p.queue == 0 then            -- the queue's job time is not trustworthy
      local b, a0 = slope(sfill, sgpu, ns)
      o.ns_gpu, o.gpu0_ms = b * 1e6, a0
      if o.ns_gpu < 0 then o.ns_gpu, o.gpu0_ms = -1, -1 elseif o.gpu0_ms < 0 then o.gpu0_ms = 0 end
    end
    if o.ns_ms < 0 then o.ns_ms = -1 end
  end
  o.tris60 = load_at(stri, sms, ns, MS60)
  o.drawn60 = load_at(sdrawn, sms, ns, MS60)
  if o.n60 < 0 then
    local kk = s[1].ms > 0 and MS60 / s[1].ms or 0
    o.tris60, o.drawn60 = s[1].tris_in * kk, s[1].tris * kk
  end
  o.at = s[1]
  for i = 1, ns do
    if s[i].ms <= MS60 then o.at = s[i] end
  end
  return o
end

local function ramp_end(r)
  if r.t.teardown then r.t.teardown() end
  opts_off()
  gpu3d_opt("2d", orig_2d and true or false)
  res[r.t.id] = res[r.t.id] or {}
  res[r.t.id][r.p.short] = ramp_result(r)
  res[r.t.id][r.p.short].p = r.p
end

-- ---------------------------------------------------------------- the report (b3d.c: report())

local function build_report()
  local rep = {}
  local function put(f, ...) rep[#rep + 1] = fmt(f, ...) .. "\n" end
  local st = gpu3d_stats()
  put("bm 3D Bench (Lua edition)")
  put("drivers bm3d %s", (opts_start or ""):match("^bm3d%s+(%S+)") or "?")
  put("gpu %s", st.status)
  put("probes %s", st.probes ~= "" and st.probes or "none")
  put("size %dx%d (the page and the render)", W, H)
  put("vsprobe gpu3d_vshader=%s (0 none, 1 scenery, 2 heroes)", tostring(select(8, gpu3d()) or 0))
  put("options %s (at the start; each profile sets its own)", opts_start or "?")
  put("counters none (a cartridge has no PMU: instr, wait_instr, dmiss, cycles, core_mhz, gpu_mhz are 0)")
  put("previous none")
  if only_tests or only_profiles then
    put("only tests %s, profiles %s", only_tests or "all", only_profiles or "all")
  end
  put("notes the frame is the runtime's stat(1) (_update + _draw, 3D included), not the copy to the screen; "
      .. "match: the heroes have no bones and the map no baked light (mesh() has neither), so its numbers "
      .. "are not those of the C bench; the overlay line is inside the measured frame")
  put("columns R,test,profile,version,n60,n30,over,ms,worst,tris_in,tris,verts,pixels,gltris,jobs,gpu_ms,"
      .. "instr,wait_instr,dmiss,cycles,secs,tris60,drawn60,core_mhz,gpu_mhz,clean_kcyc,inval_kcyc,pool_kb,ovf_kb,"
      .. "ovf_jobs,fill_px,ns_px_gpu,ns_px_frame,gpu0_ms,bin_us,rnd_us")
  for _, t in ipairs(TESTS) do
    for _, p in ipairs(PROF) do
      local r = res[t.id] and res[t.id][p.short]
      if r and r.ran then
        local s = r.at
        put("R,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d,%d,%d,%s,%s,0,0,0,0,%d,%d,%d,0,0,%s,%s,%d,%d,%d,%d,%s,%s,%s,%d,%d",
            t.id, p.name, prof_version(p), f1(r.n60), f1(r.n30), r.over and 1 or 0, f1(s.ms), f1(s.worst),
            floor(s.tris_in), floor(s.tris), floor(s.verts), floor(s.pixels), floor(s.gltris), f1(s.jobs),
            f1(s.gpu_ms), floor(r.secs), floor(r.tris60 + 0.5), floor(r.drawn60 + 0.5), f1(s.clean_kcyc),
            f1(s.inval_kcyc), s.pool_kb, s.ovf_kb, s.ovf_jobs, floor(s.fill), f2(r.ns_gpu), f2(r.ns_ms),
            f2(r.gpu0_ms), floor(s.bin_us + 0.5), floor(s.rnd_us + 0.5))
      end
    end
  end
  put("pixel cost: ns a pixel = ms a Mpixel (slope over the steps; GPU: the job's time, queue off), "
      .. "ms to fill 640x360 / 1280x720 / 1920x1080 at that cost")
  put("columns P,test,profile,ns_px_gpu,ns_px_frame,gpu0_ms,gpu_360,gpu_720,gpu_1080,frame_360,frame_720,frame_1080")
  local scr = { 640 * 360, 1280 * 720, 1920 * 1080 }
  for _, t in ipairs(TESTS) do
    if t.quad_px then
      for _, p in ipairs(PROF) do
        local r = res[t.id] and res[t.id][p.short]
        if r and r.ran then
          local v = {}
          for i = 1, 3 do
            v[i] = f2(r.ns_gpu < 0 and -1 or r.ns_gpu * scr[i] / 1e6)
            v[3 + i] = f2(r.ns_ms < 0 and -1 or r.ns_ms * scr[i] / 1e6)
          end
          put("P,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", t.id, p.name, f2(r.ns_gpu), f2(r.ns_ms), f2(r.gpu0_ms),
              v[1], v[2], v[3], v[4], v[5], v[6])
        end
      end
    end
  end
  put("total %d s", floor(time() - t_start))
  return table.concat(rep)
end

local function finish()
  phase = "done"
  t_done = time()
  rep_text = build_report()
  for line in rep_text:gmatch("[^\n]+") do log(line) end
  sent = report("bench3d", rep_text)
  gpu3d(orig_gpu and true or false)
end

-- ---------------------------------------------------------------- the summary page

local COLX, COLW = 88, 49
local function cell(r)
  if not r or not r.ran then return "-" end
  if r.n60 < 0 then return "<1" end
  if r.n60 >= 100 then return fmt("%d", floor(r.n60 + 0.5)) end
  return fmt("%.1f", r.n60)
end

local function draw_summary()
  cls(0x0A0A1E)
  print("3D Bench (Lua edition)", 8, 6, 0xFFFFFF, 2)
  print("load at 60 frames a second (n60), the unit is each test's own", 8, 40, 0x8090B0)
  local shown = {}
  for _, p in ipairs(PROF) do
    for _, t in ipairs(TESTS) do
      if res[t.id] and res[t.id][p.short] then shown[#shown + 1] = p; break end
    end
  end
  for i, p in ipairs(shown) do
    print(fmt("%4s", p.short), COLX + (i - 1) * COLW - 6, 66, 0xFFC040)
  end
  local y = 86
  for _, t in ipairs(TESTS) do
    if res[t.id] then
      print(t.id, 8, y, 0xFFFFFF)
      for i, p in ipairs(shown) do
        print(fmt("%4s", cell(res[t.id][p.short])), COLX + (i - 1) * COLW - 6, y, 0x80E0A0)
      end
      y = y + 20
    end
  end
  y = y + 10
  if #shown == 0 then
    print("no row ran: choose other tests or profiles (3d_tests, 3d_profiles)", 8, y, 0xFF8080)
    y = y + 20
  end
  print("each test n counts: spheres, quads, heroes", 8, y, 0x8090B0)
  y = y + 24
  print(sent and "the report was sent with report(\"bench3d\")" or "the report was not sent (report() said no)", 8, y,
        sent and 0x80E0A0 or 0xFF8080)
  if nowait then
    print("leaving in 3 s", 8, H - 24, 0x8090B0)
  else
    print("B or Esc leaves", 8, H - 24, 0x8090B0)
  end
end

-- ---------------------------------------------------------------- the loop

local function print_overlay(r)
  rectfill(0, 0, W, 16, 0)
  print(fmt("3D Bench %d/%d  %s  %s (bm3d %s)  n=%d  %.1f ms", k, #plan, r.t.id, r.p.name, prof_version(r.p), r.n,
            last_ms), 0, 0, 0xFFFFFF)
end

function _init()
  local on, aa, vs = gpu3d()
  orig_gpu = on
  if on then orig_2d = (gpu3d_opt("2d")) end
  opts_start = gpu3d_stats().options
  make_sheet()
  t_start = time()
  log(fmt("b3d: %d rows planned%s", #plan, only_tests and (" (tests " .. only_tests .. ")") or ""))
end

function _update()
  if phase == "done" and (btnp("b") or (nowait and time() - t_done > 3)) then quit() end
end

function _draw()
  if phase == "done" then
    draw_summary()
    return
  end
  -- 1. the frame before: its cost is the runtime's stat(1) now
  if ramp and ramp.last_f >= 1 then
    ramp.fms[#ramp.fms + 1] = stat(1)
    ramp.gms = ramp.gms + stat_or0(16)
    if ramp.last_f == FRAMES then
      ramp.tris, ramp.verts, ramp.pixels = stat_or0(4), stat_or0(7), stat_or0(5)
      step_end(ramp)
      local a = ramp.samples[#ramp.samples]
      local lost = ramp.p.gpu and not (gpu3d())      -- the driver gave the 3D to the ARM during the step
      local done = lost or a.ms > LIMIT_MS or time() - ramp.t0 > RAMP_S or #ramp.samples >= MAXSTEPS
      local nxt = ramp.n < 4 and ramp.n + 1 or ramp.n + ramp.n // 3
      if lost then ramp.samples = {} end
      if done or nxt > ramp.t.max then
        ramp_end(ramp)
        ramp = nil
      else
        ramp.n, ramp.f, ramp.fms, ramp.gms, ramp.s0 = nxt, 0, {}, 0, nil
      end
    end
  end
  -- 2. a ramp to start
  while not ramp do
    k = k + 1
    if k > #plan then
      finish()
      draw_summary()
      return
    end
    ramp = ramp_start(plan[k][1], plan[k][2])
  end
  -- 3. this frame: 0 is the warm one (textures, corners, caches), 1..FRAMES are measured
  local r = ramp
  if r.f == 1 then r.s0 = gpu3d_stats() end
  r.fill = r.t.frame(r.n, r.f) or 0
  r.last_f = r.f
  r.f = r.f + 1
  print_overlay(r)
end
