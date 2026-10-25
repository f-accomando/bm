-- 3D Bench, Lua edition: the 3D benchmark of bm3D 7.7 as a cartridge (docs/BENCH3D.md, "Lua edition").
--
-- It ports the ramp of src/bm/b3d.c as a state machine over _draw frames: each test runs on each
-- profile with a load n that grows (1, 2, 3, 4, then x1.5) until a frame takes more than 40 ms; the
-- loads at 60 and 30 frames a second come from the median ms of 3 frames a step. The profiles set the
-- GPU driver with gpu3d() and gpu3d_opt(). The report is the C bench's text and goes out with report().
--
-- Keys of bm/config.txt (the first word of the title "3D Bench" gives the prefix `3d_`; a value has no
-- comma, so a list is written with `/`: `3d_tests=spheres/herotris`, `3d_profiles=ARM/VSQ/VSQ2`):
--   3d_tests     the tests to run: ids, or a group (`cel`, `htmap`), or `all` (default: all of them)
--   3d_profiles  the profiles, by name (GPU+VS+Q) or short name (VSQ), or `all` (default: ARM and VSQ)
--   3d_nowait    1: the summary stays 3 s, then the cartridge ends (for a script)
--
-- Profiles: ARM (the baseline), VSQ = GPU+VS+Q (what a game gets today: vertex shader 2, one job in
-- flight, two-thread pixel shaders, the 2D in the GPU's job, draw merging), VSQ2 = GPU+VS+Q2 with the
-- frame shown a frame later (the candidate default; only when asked for).
-- Not in this edition: the old driver stages, the QPU rows, the PMU counters, the machine line, the Mali.
-- Columns that need the counters are 0 in the R lines.

local W, H = 640, 360
local LIMIT_MS, MS60, MS30 = 40.0, 1000 / 60, 1000 / 30
local RAMP_S = 10                       -- a ramp stops after 10 s
local FRAMES = 3                        -- measured frames a step (and one warm frame before them)
local STRESS_FRAMES = 24                -- measured frames of the stress test at 1x and at 2x
local MAXSTEPS = 64

local floor, max, min, sqrt, ceil = math.floor, math.max, math.min, math.sqrt, math.ceil
local sin, cos = math.sin, math.cos
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

-- b3d.c: hash2d, the positions of the 2D scenes come from it (never from the time)
local function hash2d(i)
  i = i & 0xFFFFFFFF
  i = i ~ (i >> 16); i = (i * 0x7FEB352D) & 0xFFFFFFFF
  i = i ~ (i >> 15); i = (i * 0x846CA68B) & 0xFFFFFFFF
  i = i ~ (i >> 16)
  return i
end

-- a game's logic after the 3D: k thousand steps of a generator in b3d.c (3 instructions each). A Lua step
-- costs more, so a thousand steps of C are WORK_STEPS steps here (an estimate of about 4M ARM instructions
-- for work = 1300; not measured)
local WORK_STEPS = 15
local work_sink = 1
local function work(k)
  local x = work_sink
  for _ = 1, k * WORK_STEPS do x = (x * 1664525 + 1013904223) & 0xFFFFFFFF end
  work_sink = x
end

-- ---------------------------------------------------------------- profiles (b3d.c: prof[], prof_on)

-- gpu: the GPU draws; queue: jobs in flight; later: each frame shown a frame later. A GPU profile has the
-- default switches of bm3D 7.7: vertex shader 2, fs2, 2d and merge on; the rest off.
local PROF = {
  { name = "ARM",       short = "ARM",  version = "0.2" },
  { name = "GPU+VS+Q",  short = "VSQ",  gpu = true, queue = 1, version = "7.7" },
  { name = "GPU+VS+Q2", short = "VSQ2", gpu = true, queue = 2, later = true, version = "6.9" },
}
local DEFAULT_PROFILES = { ARM = true, VSQ = true }

-- the profile's switches on the driver; false and the reason if the profile has no row here
local function prof_on(p)
  gpu3d_opt("filter", false)
  if not p.gpu then
    gpu3d(false, false, 0, 0, false, false)
    return true
  end
  local on, _, vs, _, level, _, later, probe = gpu3d(true, false, 2, p.queue, false, p.later or false)
  if not on then return false, "no GPU" end
  if (probe or 0) < 2 or (vs or 0) < 2 then return false, "the vertex shader level is not there" end
  if not level or level < p.queue then return false, "the frame queue is not there" end
  if p.later and not later then return false, "the frame later is not there" end
  for _, name in ipairs({ "wc", "tex16", "etc1", "sort", "attr" }) do gpu3d_opt(name, false) end
  for _, name in ipairs({ "fs2", "2d", "merge" }) do gpu3d_opt(name, true) end
  return true
end

-- ---------------------------------------------------------------- models and meshes (b3d.c: meshes_make)

-- Models with bones and baked light come from the cartridge (carts/bench3d/build/mkassets.py, MESH and ANIM
-- sections): hero, hero_tex, hero_skin and hero_h (16 bones, 1536 faces; the clip "walk" is b3d.c's pose()),
-- tile (32 faces), tile_a and tile_b (50 faces, the map pieces of herotris), slab10a and slab10b (500 faces).
-- The sprite sheet (384 x 160): sheet 0 (128 x 128 checker) at x 0, sheet 1 (holes) at x 128, sheet 2 at
-- x 256, the three 64 x 32 sheets of herotris at y 128.
local sphere, quad, cube, grid
local hero, hero_tex, hero_skin, hero_h, tile, tile_a, tile_b, slab_a, slab_b
local flags = 0

local function load_models()
  hero, hero_tex, hero_skin, hero_h = model("hero"), model("hero_tex"), model("hero_skin"), model("hero_h")
  tile, tile_a, tile_b = model("tile"), model("tile_a"), model("tile_b")
  slab_a, slab_b = model("slab10a"), model("slab10b")
  cube = mesh_cube(0x60C060)
  grid = nil
end

-- a quad of 2 x 1.125 units (16:9) in the plane z = 0, facing -z; sheet_x: the sheet to texture it with
-- (x of the sheet in the sprite sheet), or nil for a colour
local function make_quad(colour, sheet_x)
  local f, uv = { 1, 3, 2, colour, 1, 4, 3, colour }, nil
  if sheet_x then
    f = { 1, 3, 2, -1, 1, 4, 3, -1 }
    uv = { sheet_x, 128, sheet_x + 128, 0, sheet_x + 128, 128,  sheet_x, 128, sheet_x, 0, sheet_x + 128, 0 }
  end
  return mesh({ -1, -0.5625, 0, 1, -0.5625, 0, 1, 0.5625, 0, -1, 0.5625, 0 }, f, uv)
end

-- a flat grid of k x k squares, side 1, in the plane y = 0, one colour, facing up (b3d.c: make_tile(16))
local function make_grid(k, colour)
  local v, f = {}, {}
  for z = 0, k do
    for x = 0, k do
      v[#v + 1] = x / k - 0.5; v[#v + 1] = 0; v[#v + 1] = z / k - 0.5
    end
  end
  for z = 0, k - 1 do
    for x = 0, k - 1 do
      local a = z * (k + 1) + x
      local b, c = a + 1, a + k + 1
      local d = c + 1
      f[#f + 1] = a + 1; f[#f + 1] = c + 1; f[#f + 1] = b + 1; f[#f + 1] = colour
      f[#f + 1] = b + 1; f[#f + 1] = c + 1; f[#f + 1] = d + 1; f[#f + 1] = colour
    end
  end
  return mesh(v, f)
end

-- ---------------------------------------------------------------- scenes (b3d.c)

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

-- n grids of 512 small faces (about 4 pixels each)
local function tiny(n, f)
  cls3d()
  camera3d(0, 6, -6, 0, -0.7, 60)
  local side = ceil(sqrt(n))
  for i = 0, n - 1 do
    local x = -5.0 + 10.0 * ((i % side) + 0.5) / side
    local z = -2.0 + 8.0 * ((i // side) + 0.5) / side
    draw3d(grid, x, 0, z, 0, f * 0.01, 0, 0.35, 0)
  end
  return 0
end

-- n small cubes, each its own draw: what a draw costs
local function draws(n, f)
  cls3d()
  camera3d(0, 0, -8, 0, 0, 60)
  local side = ceil(sqrt(n))
  for i = 0, n - 1 do
    local x = -5.0 + 10.0 * ((i % side) + 0.5) / side
    local y = 3.0 - 6.0 * ((i // side) + 0.5) / side
    draw3d(cube, x, y, 0, i, f * 0.05, 0, 2.5 / side, 0)
  end
  return 0
end

-- heroes in rows, their bones moving; with their shadows on a floor (sh), floor_k x floor_k pieces of map
local hero_m
local function heroes_at(n, f, sh, floor_k)
  for i = 0, floor_k - 1 do
    for j = 0, floor_k - 1 do
      draw3d(tile, -12.0 + 6.0 * i, 0, -2.0 + 6.0 * j, 0, 0, 0, 6, 0)
    end
  end
  local row = 8
  for i = 0, n - 1 do
    local x = -4.2 + 1.2 * (i % row) + 0.6 * ((i // row) % 2)
    local z = 1.0 + 1.5 * (i // row)
    animate(hero_m, 1, f * 0.05 + i)
    if sh then draw3d(hero_m, x, 0, z, 0, i, 0, 1, 8) end
    draw3d(hero_m, x, 0, z, 0, i, 0, 1, 4)
  end
end

local function heroes(n, f)
  cls3d()
  camera3d(0, 2.2, -4, 0, -0.18, 60)
  shine_light()
  heroes_at(n, f, false, 0)
  scene_light()
  return 0
end

local function heroes_shadow(n, f)
  cls3d()
  camera3d(0, 2.2, -4, 0, -0.18, 60)
  shine_light()
  shadow3d(1)
  heroes_at(n, f, true, 4)
  scene_light()
  return 0
end

-- cel (Q2): the lit textured hero against the cel hero (4 bands) and with the outline
local cel_bands, cel_outline = 0, false
local function cel_set(bands, outline)
  cel_bands, cel_outline = bands, outline
  cel3d(bands, 0.35, outline and 0x2030A0 or nil, 0.03)
end

local function cel_scene(n, f)
  cls3d()
  camera3d(0, 2.2, -4, 0, -0.18, 60)
  shine_light()
  local fl = 4 | (cel_bands > 0 and 256 or 0) | (cel_outline and 512 or 0)
  for i = 0, n - 1 do
    local x = -4.2 + 1.2 * (i % 8) + 0.6 * ((i // 8) % 2)
    local z = 1.0 + 1.5 * (i // 8)
    animate(hero_tex, 1, f * 0.05 + i)
    draw3d(hero_tex, x, 0, z, 0, i, 0, 1, fl)
  end
  scene_light()
  return 0
end

-- herotris (docs/design/triangle-throughput.md): a hero and a map, the load of a game on the Pi Zero W.
-- n is the number of sets: a hero of 1536 triangles with its shadow and a map of 100 pieces of 50 triangles
-- (baked light, two 64 x 32 sheets). The 2D after the 3D: two parallax bands, a HUD of 20 sprites and 3 lines
-- of text. The logic (4M instructions) is in _update.
local function sets_2d(f)
  for r = 0, 3 do
    for c = 0, 39 do sspr(((c + f // 2) % 8) * 16, r * 16, 16, 16, c * 16, r * 16) end
  end
  for r = 0, 2 do
    for c = 0, 39 do sspr(128 + ((c + f) % 8) * 16, (r + 2) * 16, 16, 16, c * 16, H - 48 + r * 16) end
  end
  rectfill(8, H - 84, 180, 20, 0x141E28)
  print("HUD 250/250", 12, H - 82, 0xFFFFFF)
  print("SCORE 12345", W - 100, 70, 0xFFE000)
  print("ROUND 3", W // 2 - 40, 70, 0xFFFFFF)
  for k = 0, 19 do sspr(128 + (k % 8) * 16, 0, 16, 16, 8 + 18 * k, H - 112) end
end

local function set_hero(s, f)
  local x = -4.2 + 1.2 * (s % 8) + 0.6 * ((s // 8) % 2)
  local z = 1.0 + 1.5 * (s // 8)
  animate(hero_h, 1, f * 0.05 + s)
  draw3d(hero_h, x, 0, z, 0, s, 0, 1, 8)
  draw3d(hero_h, x, 0, z, 0, s, 0, 1, 4)
end

local function sets_camera(f)
  cls3d()
  camera3d(0.3 * sin(f * 0.02), 1.7, -5, 0.05, -0.12, 70)
  shine_light()
  shadow3d(1)
end

local function herotris(n, f)
  sets_camera(f)
  for s = 0, n - 1 do
    for i = 0, 9 do
      for j = 0, 9 do
        draw3d(((i + j) & 1) == 1 and tile_b or tile_a, -6.3 + 1.4 * i, -0.003 * s, -1.5 + 1.4 * j, 0, 0, 0, 1.4, 0)
      end
    end
    set_hero(s, f)
  end
  scene_light()
  sets_2d(f)
  return 0
end

-- the map of set s in `pieces` pieces: the first sheet's, then the second's (two runs, as a game that
-- sorts its static map by texture); 100 pieces of 50 faces or 10 strips of 500
local function ht_map(s, pieces)
  local y = -0.003 * s
  for sheet = 0, 1 do
    if pieces == 10 then
      for j = sheet, 9, 2 do
        draw3d(sheet == 1 and slab_b or slab_a, 0, y, -1.5 + 1.4 * j, 0, 0, 0, 1.4, 0)
      end
    else
      for i = 0, 9 do
        for j = 0, 9 do
          if ((i + j) & 1) == sheet then
            draw3d(sheet == 1 and tile_b or tile_a, -6.3 + 1.4 * i, y, -1.5 + 1.4 * j, 0, 0, 0, 1.4, 0)
          end
        end
      end
    end
  end
end

local function ht_scene(n, f, pieces)
  sets_camera(f)
  for s = 0, n - 1 do
    ht_map(s, pieces)
    set_hero(s, f)
  end
  scene_light()
  sets_2d(f)
  return 0
end

local function ht_p100(n, f) return ht_scene(n, f, 100) end
local function ht_p10(n, f) return ht_scene(n, f, 10) end

-- the load of herotris in finer steps: n is a number of quarter sets (25 map pieces; a hero with its
-- shadow for every 4 quarters or part of them)
local function stress(n, f)
  local pieces, nheroes = 25 * n, (n + 3) // 4
  sets_camera(f)
  for s = 0, nheroes - 1 do
    local p = 100 * s
    while p < pieces and p < 100 * s + 100 do
      local i, j = p % 100 // 10, p % 10
      draw3d(((i + j) & 1) == 1 and tile_b or tile_a, -6.3 + 1.4 * i, -0.003 * s, -1.5 + 1.4 * j, 0, 0, 0, 1.4, 0)
      p = p + 1
    end
    set_hero(s, f)
  end
  scene_light()
  sets_2d(f)
  return 0
end

-- the 2D scenes: the positions come from hash2d, the number of the item and the frame; the scene returns
-- the pixels the frame asks to draw (ns_px_frame in the report)

local MAP8_W, MAP8_H = 96, 56
local map8_cover = {}
local function map8_setup()
  if #map8_cover == 8 then return end
  for l = 1, 8 do
    local used = 0
    for y = 0, MAP8_H - 1 do
      for x = 0, MAP8_W - 1 do
        if mget(x, y, l) ~= 0 then used = used + 1 end
      end
    end
    map8_cover[l] = used / (MAP8_W * MAP8_H)
  end
end

-- n layers (8 = the scene) of map() at 640x360, each scrolling faster than the one below. The layers are in the
-- cartridge (mkassets.py): 0-2 every cell, opaque tiles; 3-7 half the cells, tiles with holes
local function map8(n, f)
  cls(0x0A0A1E)
  local fill = 0
  for l = 0, min(n, 8) - 1 do
    local sx, sy = (f * (l + 1) * 5) % 120, (f * (l + 1)) % 64
    map(sx // 8, sy // 8, -(sx % 8), -(sy % 8), W // 8 + 1, H // 8 + 1, l + 1)
    fill = fill + W * H * map8_cover[l + 1]
  end
  return fill
end

-- n sprites of 16 x 16: half opaque (sheet 0), half with holes (sheet 1), some flipped
local function sprites(n, f)
  cls3d()
  for i = 0, n - 1 do
    local h = hash2d(i)
    sspr((i & 1) * 128 + (h & 7) * 16, ((h >> 3) & 7) * 16, 16, 16,
         ((h >> 6) % (W - 16) + f * (1 + (h >> 29))) % (W - 16), (h >> 15) % (H - 16),
         ((h >> 26) & 1) == 1, ((h >> 27) & 1) == 1)
  end
  return n * 256
end

-- n stacks of 8 slices (16 x 16, sheet 1 has holes), turning, in three sizes: a fixed pile
local function stacks(n, f)
  cls(0x0A0A1E)
  local fill = 0
  for i = 0, n - 1 do
    local h = hash2d(i + 77)
    local scale = 1.0 + 0.5 * (i % 3)
    fill = fill + 8 * 256 * scale * scale
    stack(128, 0, 16, 16, 8, 40 + h % (W - 80), 40 + ((h >> 12) & 0xFF) % (H - 80),
          ((h >> 20) & 63) * 0.1 + f * 0.05, scale, 2.0)
  end
  return fill
end

-- the background of fade and light: blocks of the colours the fade has tables for
local FX_COL = { 0xF80000, 0x00FC00, 0x0000F8, 0xF8FC00 }
local function fx_back()
  cls(0x28283C)
  for i = 0, 11 do
    rectfill((i % 4) * (W // 4), (i // 4) * (H // 3), W // 4, H // 3, FX_COL[((i + i // 4) & 3) + 1])
  end
  return W * H                          -- the pass over the whole screen
end

local function fade_setup()
  local tables = {}
  for c = 1, 4 do
    local rgb = FX_COL[c]
    local row = { rgb }
    for k = 0, 15 do                    -- the table of each block colour: darker to itself
      local r, g, b = (rgb >> 16) * (k + 1) // 16, ((rgb >> 8) & 255) * (k + 1) // 16, (rgb & 255) * (k + 1) // 16
      row[#row + 1] = (r << 16) | (g << 8) | b
    end
    tables[c] = row
  end
  fades(tables)
end

-- n lamps (levels, rings dithered) and the fade over the whole screen
local function fade2d(n, f)
  local fill = fx_back()
  dark_begin(3)
  for i = 0, n - 1 do
    local h = hash2d(i + 5)
    glow((h % W) + f, ((h >> 12) & 0x1FF) % H, 40 + ((h >> 24) & 63), 15, 0.5)
  end
  dark_end()
  return fill
end

-- n soft lamps and the light over the whole screen
local function light2d(n, f)
  local fill = fx_back()
  light_begin(0x404040)
  for i = 0, n - 1 do
    local h = hash2d(i + 9)
    light((h % W) + f, ((h >> 12) & 0x1FF) % H, 40 + ((h >> 24) & 63), 0xFFE0A0 + ((h >> 8) & 0x1F) * 0x10100, 1.0)
  end
  light_end()
  return fill
end

-- ---------------------------------------------------------------- the tests

-- id, name, unit, start, max, the profiles that run it (by short name), setup, frame, teardown,
-- quad_px (the cost of a pixel: the frame returns the pixels it fills), work (the logic of a frame, in
-- thousands of steps of b3d.c), merge (the draw merging for this test; nil: the profile's), group, probe, stress
local ALL3 = { ARM = true, VSQ = true, VSQ2 = true }
local ARM_VSQ = { ARM = true, VSQ = true }
local ARM_ONLY = { ARM = true }

local function sp_flat() sphere = mesh_sphere(6, 8, 0x4080FF, 0xFFC040); flags = 0 end      -- 96 faces, as the stress test
local function hero_plain() hero_m = hero end
local function hero_skinned() hero_m = hero_skin end
local function q_flat() flags = 0; quad = make_quad(0x80C0FF); quad_step = (gpu3d()) and 0.9985 or 0.985 end
local function q_tex() flags = 0; quad = make_quad(0, 0); quad_step = (gpu3d()) and 0.9985 or 0.985 end
local function q_alpha() flags = 0; quad = make_quad(0, 128); quad_step = (gpu3d()) and 0.9985 or 0.985 end

local TESTS = {
  { id = "spheres", name = "spheres, 96 faces, flat", unit = "spheres", start = 1, max = 8000, profiles = ARM_VSQ,
    setup = sp_flat, frame = spheres },
  { id = "heroes", name = "heroes: 16 bones, 1536 faces", unit = "heroes", start = 1, max = 512, profiles = ARM_VSQ,
    setup = hero_plain, frame = heroes },
  { id = "heroes_skin", name = "heroes, textured skins (as the Meshy ones)", unit = "heroes", start = 1, max = 512,
    profiles = ARM_VSQ, setup = hero_skinned, frame = heroes },
  { id = "heroes_shadow", name = "heroes with shadows on a floor", unit = "heroes", start = 1, max = 512,
    profiles = ARM_VSQ, setup = hero_plain, frame = heroes_shadow },
  { id = "tiny", name = "small faces (about 4 pixels)", unit = "grids", start = 1, max = 2000, profiles = ARM_VSQ,
    setup = function() grid = make_grid(16, 0xD0D0D0) end, frame = tiny },
  { id = "draws", name = "draw calls: a cube each", unit = "draws", start = 1, max = 40000, profiles = ARM_VSQ,
    frame = draws },
  { id = "quad_flat", name = "fill: quads 320x180, flat", unit = "quads", start = 1, max = 4000, profiles = ARM_VSQ,
    setup = q_flat, frame = quads, quad_px = true },
  { id = "quad_tex", name = "fill: quads, textured", unit = "quads", start = 1, max = 4000, profiles = ARM_VSQ,
    setup = q_tex, frame = quads, quad_px = true },
  { id = "quad_alpha", name = "fill: quads, texels with holes", unit = "quads", start = 1, max = 4000, profiles = ARM_VSQ,
    setup = q_alpha, frame = quads, quad_px = true },
  { id = "cel_lit", name = "cel: lit hero (reference)", unit = "heroes", start = 1, max = 64, profiles = ARM_VSQ,
    setup = function() cel_set(0, false) end, frame = cel_scene,
    teardown = function() cel3d(0, 0.35, nil, 0.02); cel_bands, cel_outline = 0, false end, group = "cel" },
  { id = "cel", name = "cel: 4 bands, dither on", unit = "heroes", start = 1, max = 64, profiles = ARM_VSQ,
    setup = function() cel_set(4, false) end, frame = cel_scene,
    teardown = function() cel3d(0, 0.35, nil, 0.02); cel_bands, cel_outline = 0, false end, group = "cel" },
  { id = "cel_outline", name = "cel: lit hero with the outline (inverted hull)", unit = "heroes", start = 1, max = 64,
    profiles = ARM_VSQ, setup = function() cel_set(0, true) end, frame = cel_scene,
    teardown = function() cel3d(0, 0.35, nil, 0.02); cel_bands, cel_outline = 0, false end, group = "cel" },
  { id = "map8", name = "2D: 8 layers of map(), parallax", unit = "layers", start = 1, max = 8, profiles = ARM_ONLY,
    setup = map8_setup, frame = map8, quad_px = true },
  { id = "sprites", name = "2D: sprites, flip, alpha", unit = "sprites", start = 16, max = 4000, profiles = ARM_VSQ,
    frame = sprites, quad_px = true },
  { id = "stack", name = "2D: sprite stacks (stack)", unit = "stacks", start = 1, max = 600, profiles = ARM_ONLY,
    frame = stacks, quad_px = true },
  { id = "fade", name = "2D: full-screen fade (dark_end)", unit = "lamps", start = 1, max = 64, profiles = ARM_ONLY,
    setup = fade_setup, frame = fade2d, quad_px = true, late = true },
  { id = "light", name = "2D: full-screen light", unit = "lamps", start = 1, max = 64, profiles = ARM_ONLY,
    frame = light2d, quad_px = true, late = true },
  { id = "herotris", name = "hero + map + parallax + HUD + 4M instr of logic", unit = "sets", start = 1, max = 64,
    profiles = ALL3, frame = herotris, work = 1300, probe = true },
  { id = "ht_p100", name = "herotris, map in 100 pieces", unit = "sets", start = 1, max = 64, profiles = ALL3,
    frame = ht_p100, work = 1300, merge = false, group = "htmap" },
  { id = "ht_p100m", name = "herotris, map in 100 pieces, merged", unit = "sets", start = 1, max = 64, profiles = ALL3,
    frame = ht_p100, work = 1300, merge = true, group = "htmap" },
  { id = "ht_p10m", name = "herotris, map in 10 pieces, merged", unit = "sets", start = 1, max = 64, profiles = ALL3,
    frame = ht_p10, work = 1300, merge = true, group = "htmap" },
  { id = "stress", name = "stress: herotris at 1x and 2x the load of 60 fps", unit = "quarters", start = 1, max = 256,
    profiles = { VSQ = true }, frame = stress, work = 1300, stress = true },
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

local function test_wanted(t)
  if not only_tests or only_tests == "all" then return true end
  for _, w in ipairs(words(only_tests)) do
    if w == t.id or w == t.group then return true end
  end
  return false
end

local function prof_wanted(p)
  if not only_profiles then return DEFAULT_PROFILES[p.short] or false end
  if only_profiles == "all" then return true end
  for _, w in ipairs(words(only_profiles)) do
    if w == p.name or w == p.short then return true end
  end
  return false
end

-- { test, profile } in the order of the report, except that fade and light (`late`) go last: dark_begin() and
-- light_begin() move the drawing to RAM for the rest of the run, where the GPU cannot draw
local plan = {}
for pass = 1, 2 do
  for _, t in ipairs(TESTS) do
    if test_wanted(t) and (t.late and 2 or 1) == pass then
      for _, p in ipairs(PROF) do
        if t.profiles[p.short] and prof_wanted(p) then plan[#plan + 1] = { t, p } end
      end
    end
  end
end

-- ---------------------------------------------------------------- the runner

local nowait = cfg("3d_nowait") ~= nil and cfg("3d_nowait") ~= "0"
local res = {}                                   -- res[test.id][profile.short] = the ramp's result
local stress_rows = {}                           -- the stress test: { n60 =, p =, [1] = 1x, [2] = 2x }
local cur                                        -- the run going on: a ramp or a stress load
local pending = {}                               -- runs to do before the next row of the plan
local k = 0                                      -- its place in the plan
local phase = "run"                              -- "run", then "done"
local t_start, t_done
local opts_start, orig_gpu, orig_opts
local sent
local last_ms = 0
local rep_text

local function stat_or0(i)
  return stat(i) or 0
end

-- starts a run of test t on profile p (n: the load of a stress run; the ramp starts at the test's); nil (and a
-- line in the log) when the profile has no row on this console
local function run_start(t, p, kind, n)
  local ok, why = prof_on(p)
  if not ok then
    log(fmt("b3d-skip %s %s: %s", t.id, p.name, why))
    return nil
  end
  scene_light()
  shadow3d(0)
  if p.gpu and t.merge ~= nil then gpu3d_opt("merge", t.merge) end
  if t.setup then t.setup() end
  local r = { t = t, p = p, kind = kind, n = n or t.start, f = 0, last_f = -1, fms = {}, samples = {}, t0 = time(),
              gms = 0, gwait = 0, frames = kind == "stress" and STRESS_FRAMES or FRAMES }
  if t.probe and p.gpu then                      -- one hero alone: what the vertex shader placed (b3d.c: herotris_probe)
    cls3d()
    camera3d(0, 1.7, -5, 0.05, -0.12, 70)
    shine_light()
    animate(hero_h, 1, 0)
    local a = gpu3d_stats()
    draw3d(hero_h, 0, 0, 1, 0, 0, 0, 1, 4)
    r.probe_gl = gpu3d_stats().gltris - a.gltris
    r.vs_probe = select(8, gpu3d()) or 0
    scene_light()
  end
  r.s0 = gpu3d_stats()
  return r
end

-- the sample of the step that ended (its frames are in r.fms); the driver's totals are read once a step
-- (a read draws the waiting 3D), so they cover the warm frame and the measured ones
local function step_end(r)
  local s1 = gpu3d_stats()
  local s0 = r.s0
  local a = { n = r.n }
  a.ms = median(r.fms)
  a.worst = 0
  for i = 1, #r.fms do a.worst = max(a.worst, r.fms[i]) end
  local kf = 1 / (FRAMES + 1)
  a.tris_in, a.tris, a.verts, a.pixels = s1.tris_in, r.tris, r.verts, r.pixels
  a.gltris = (s1.gltris - s0.gltris) * kf
  a.gpu_ms = r.gms / FRAMES
  a.wait_ms = r.gwait / FRAMES
  a.jobs = (s1.jobs - s0.jobs) * kf
  a.bin_us = (s1.bin_us - s0.bin_us) * kf
  a.rnd_us = (s1.render_us - s0.render_us) * kf
  a.clean_kcyc = (s1.clean_cyc - s0.clean_cyc) * kf / 1000
  a.inval_kcyc = (s1.inval_cyc - s0.inval_cyc) * kf / 1000
  a.pool_kb = (s1.pool_max + 1023) // 1024
  a.ovf_kb = (s1.ovf_max + 1023) // 1024
  a.ovf_jobs = s1.ovf_jobs - s0.ovf_jobs
  a.fill = r.fill
  r.s0 = s1
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
  local o = { ran = ns > 0, nsamples = ns, secs = time() - r.t0, p = r.p }
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
    if r.p.gpu and r.p.queue == 0 then            -- the queue's job time is not trustworthy (never today)
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
  o.ms_n, o.tris_n = {}, {}                       -- the first four steps, for the cel lines
  for i = 1, ns do
    if s[i].n >= 1 and s[i].n <= 4 then o.ms_n[s[i].n], o.tris_n[s[i].n] = s[i].ms, s[i].tris_in end
  end
  o.probe_gl, o.vs_probe = r.probe_gl, r.vs_probe
  o.later = r.p.later
  if r.p.later then                               -- the frame later: the pace is the slower of the ARM and the GPU
    local sl = {}
    for i = 1, ns do sl[i] = max(s[i].ms - s[i].wait_ms, s[i].gpu_ms) end
    o.tris60_l = load_at(stri, sl, ns, MS60)
    o.drawn60_l = load_at(sdrawn, sl, ns, MS60)
    if load_at(sn, sl, ns, MS60) < 0 then
      local kk = sl[1] > 0 and MS60 / sl[1] or 0
      o.tris60_l, o.drawn60_l = s[1].tris_in * kk, s[1].tris * kk
    end
  end
  return o
end

local function run_end(r)
  if r.t.teardown then r.t.teardown() end
  if r.p.gpu then gpu3d_opt("merge", true) end
end

local function ramp_end(r)
  run_end(r)
  res[r.t.id] = res[r.t.id] or {}
  local o = ramp_result(r)
  res[r.t.id][r.p.short] = o
  if r.t.stress and o.ran then                    -- then the frame at 1x and 2x the load of 60 fps (whole quarter sets)
    local n1 = o.n60 >= 1 and floor(o.n60) or 1
    stress_rows = { n60 = o.n60, p = r.p, t = r.t, n1 = n1 }
    pending = { { r.t, r.p, 1, n1 }, { r.t, r.p, 2, 2 * n1 } }
  end
end

local function stress_end(r, idx)
  run_end(r)
  local s1 = gpu3d_stats()
  local s0 = r.s0
  local ms, worst, drops = 0, 0, 0
  for i = 1, #r.fms do
    ms = ms + r.fms[i]
    worst = max(worst, r.fms[i])
    if r.fms[i] > MS60 then drops = drops + 1 end
  end
  local fr = max(#r.fms, 1)
  local kf = 1 / (fr + 1)
  local failed = r.p.gpu and not (gpu3d())       -- the driver gave the 3D to the ARM
  stress_rows[idx] = { ran = #r.fms > 0, n = r.n, tris_in = s1.tris_in, frames = fr, secs = time() - r.t0, ms = ms / fr,
                       worst = worst, drops = drops, arm_ms = ms / fr - r.gwait / fr, gpu_ms = r.gms / fr,
                       jobs = (s1.jobs - s0.jobs) * kf, pool_kb = (s1.pool_max + 1023) // 1024,
                       ovf_kb = (s1.ovf_max + 1023) // 1024, ovf_jobs = s1.ovf_jobs - s0.ovf_jobs, failed = failed }
  last_ms = ms / fr
  if failed then pending = {} end
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
    put("only tests %s, profiles %s", only_tests or "all", only_profiles or "ARM/VSQ (the default)")
  end
  put("notes the frame is the runtime's stat(1) (_update + _draw, 3D included), not the copy to the screen; "
      .. "the ramp has 1 warm and %d measured frames a step and n grows by half of n; "
      .. "the logic of herotris and stress is %d Lua steps a thousand of b3d.c's (an estimate); "
      .. "models are cartridge models (texture light of a map piece is one per face); the overlay line is "
      .. "inside the measured frame", FRAMES, WORK_STEPS)
  put("columns R,test,profile,version,n60,n30,over,ms,worst,tris_in,tris,verts,pixels,gltris,jobs,gpu_ms,"
      .. "instr,wait_instr,dmiss,cycles,secs,tris60,drawn60,core_mhz,gpu_mhz,clean_kcyc,inval_kcyc,pool_kb,ovf_kb,"
      .. "ovf_jobs,fill_px,ns_px_gpu,ns_px_frame,gpu0_ms,bin_us,rnd_us")
  for _, t in ipairs(TESTS) do
    for _, p in ipairs(PROF) do
      local r = res[t.id] and res[t.id][p.short]
      if r and r.ran then
        local s = r.at
        put("R,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d,%d,%d,%s,%s,0,0,0,0,%d,%d,%d,0,0,%s,%s,%d,%d,%d,%d,%s,%s,%s,%d,%d",
            t.id, p.name, p.version, f1(r.n60), f1(r.n30), r.over and 1 or 0, f1(s.ms), f1(s.worst),
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
  -- the hero triangles at 60 fps (b3d.c: report_hero)
  local head = false
  for _, t in ipairs(TESTS) do
    if t.probe then
      for _, p in ipairs(PROF) do
        local r = res[t.id] and res[t.id][p.short]
        if r and r.ran then
          if not head then
            put("hero triangles at 60 fps (640x360; a set is a hero of 1536 triangles, its shadow and a map of 5000)")
            put("columns H,test,profile,sets60,hero_tris60,hero_shadow_tris60,submitted60,drawn60,ms,gpu_ms,arm_ms,mesh,"
                .. "probe_gltris,probe_meshes,vs_probe,later_submitted60,later_drawn60")
            head = true
          end
          local s = r.at
          local per = (s.n > 0 and s.tris_in > 0) and s.tris_in / s.n or 8036
          local sets = r.tris60 / per
          local gl = r.probe_gl
          local mesh_ = (not p.gpu or not gl) and "NA" or gl >= 1536 and "ACCEPTED" or gl == 0 and "FALLBACK" or "PARTIAL"
          put("H,%s,%s,%s,%d,%d,%d,%d,%s,%s,%s,%s,%d,-1,%d,%s,%s", t.id, p.name, f2(sets), floor(sets * 1536 + 0.5),
              floor(sets * 3072 + 0.5), floor(r.tris60 + 0.5), floor(r.drawn60 + 0.5), f1(s.ms), f1(s.gpu_ms),
              f1(s.ms - s.wait_ms), mesh_, gl or 0, r.vs_probe or 0, r.later and f1(r.tris60_l) or "-",
              r.later and f1(r.drawn60_l) or "-")
        end
      end
    end
  end
  -- the cost of the cel shader (b3d.c: report_cel): the ms of the same n heroes in the cel tests
  local ids = { "cel_lit", "cel", "cel_lit_nd", "cel_nd", "cel_outline" }
  head = false
  for _, p in ipairs(PROF) do
    local lit, cel_ = res.cel_lit and res.cel_lit[p.short], res.cel and res.cel[p.short]
    if (lit and lit.ran) or (cel_ and cel_.ran) then
      for n = 1, 4 do
        local ms, tr = {}, 0
        for i, id in ipairs(ids) do
          local r = res[id] and res[id][p.short]
          ms[i] = (r and r.ran and r.ms_n[n]) or -1
          if r and r.ran and r.tris_n[n] and r.tris_n[n] > tr then tr = r.tris_n[n] end
        end
        if ms[1] >= 0 or ms[2] >= 0 then
          if not head then
            put("cel cost: the same n heroes (1536 triangles each, lit by the sun), ms a frame; "
                .. "cel = 4 bands, floor 0.35; nd = dither off (not in this edition: -1); outline = hull 0.03, colour 0x2030A0, no bands")
            put("columns C,profile,n,tris,lit,cel,lit_nd,cel_nd,outline,cel_over_lit,cel_nd_over_lit_nd,outline_over_lit")
            head = true
          end
          put("C,%s,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s", p.name, n, floor(tr + 0.5), f2(ms[1]), f2(ms[2]), f2(ms[3]),
              f2(ms[4]), f2(ms[5]), f2((ms[1] > 0 and ms[2] > 0) and ms[2] / ms[1] or -1),
              f2((ms[3] > 0 and ms[4] > 0) and ms[4] / ms[3] or -1), f2((ms[1] > 0 and ms[5] > 0) and ms[5] / ms[1] or -1))
        end
      end
    end
  end
  -- the stress test (b3d.c: report_stress)
  if stress_rows.p then
    put("stress path=%s n60=%s (quarter sets: 4 are one herotris set of 8036 triangles) frames=%d each",
        stress_rows.p.name, f1(stress_rows.n60), STRESS_FRAMES)
    put("columns T,profile,load,n,tris_in,frames,secs,ms,worst,drops,arm_ms,gpu_ms,jobs,pool_kb,ovf_kb,ovf_jobs,v3d_failed")
    for i = 1, 2 do
      local o = stress_rows[i]
      if not o or not o.ran then
        put("T,%s,%dx,not_run", stress_rows.p.name, i)
      else
        put("T,%s,%dx,%d,%d,%d,%s,%s,%s,%d,%s,%s,%s,%d,%d,%d,%d", stress_rows.p.name, i, o.n, floor(o.tris_in), o.frames,
            f1(o.secs), f1(o.ms), f1(o.worst), o.drops, f1(o.arm_ms), f1(o.gpu_ms), f1(o.jobs), o.pool_kb, o.ovf_kb,
            o.ovf_jobs, o.failed and 1 or 0)
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
  if orig_gpu then
    for name, on in pairs(orig_opts) do gpu3d_opt(name, on) end
  end
end

-- ---------------------------------------------------------------- the summary page

local COLX, COLW = 104, 56
local function cell(r)
  if not r or not r.ran then return "-" end
  if r.n60 < 0 then return "<1" end
  if r.n60 >= 100 then return fmt("%d", floor(r.n60 + 0.5)) end
  return fmt("%.1f", r.n60)
end

local function draw_summary()
  cls(0x0A0A1E)
  font("8x16")
  print("3D Bench (Lua edition)", 8, 4, 0xFFFFFF, 2)
  print("load at 60 frames a second (n60); the unit is each test's own", 8, 38, 0x8090B0)
  font("6x12")
  local shown = {}
  for _, p in ipairs(PROF) do
    for _, t in ipairs(TESTS) do
      if res[t.id] and res[t.id][p.short] then shown[#shown + 1] = p; break end
    end
  end
  for i, p in ipairs(shown) do
    print(fmt("%5s", p.short), COLX + (i - 1) * COLW - 6, 58, 0xFFC040)
  end
  local y = 72
  for _, t in ipairs(TESTS) do
    if res[t.id] then
      print(t.id, 8, y, 0xFFFFFF)
      for i, p in ipairs(shown) do
        print(fmt("%5s", cell(res[t.id][p.short])), COLX + (i - 1) * COLW - 6, y, 0x80E0A0)
      end
      print(t.unit, COLX + #shown * COLW + 8, y, 0x8090B0)
      y = y + 12
    end
  end
  y = y + 6
  if #shown == 0 then
    print("no row ran: choose other tests or profiles (3d_tests, 3d_profiles)", 8, y, 0xFF8080)
    y = y + 14
  end
  print(sent and "the report was sent with report(\"bench3d\")" or "the report was not sent (report() said no)", 8, y,
        sent and 0x80E0A0 or 0xFF8080)
  print(nowait and "leaving in 3 s" or "B or Esc leaves", 8, H - 16, 0x8090B0)
  font("8x16")
end

-- ---------------------------------------------------------------- the loop

local function print_overlay(r)
  rectfill(0, 0, W, 16, 0)
  print(fmt("3D Bench %d/%d  %s  %s (bm3d %s)  n=%d  %.1f ms", min(k, #plan), #plan, r.t.id, r.p.name, r.p.version, r.n,
            last_ms), 0, 0, 0xFFFFFF)
end

function _init()
  local on = gpu3d()
  orig_gpu = on
  orig_opts = {}
  if on then
    for _, name in ipairs({ "fs2", "2d", "merge" }) do orig_opts[name] = (gpu3d_opt(name)) end
  end
  opts_start = gpu3d_stats().options
  load_models()
  t_start = time()
  log(fmt("b3d: %d rows planned%s", #plan, only_tests and (" (tests " .. only_tests .. ")") or ""))
end

function _update()
  if phase == "done" then
    if btnp("b") or (nowait and time() - t_done > 3) then quit() end
  elseif cur and cur.t.work then
    work(cur.t.work)                             -- the logic of the frame: with the queue it runs while the GPU draws
  end
end

local function frame_done()
  local r = cur
  r.fms[#r.fms + 1] = stat(1)
  r.gms = r.gms + stat_or0(16)
  r.gwait = r.gwait + stat_or0(18)
  if r.last_f < r.frames then return end
  if r.kind == "stress" then
    stress_end(r, r.idx)
    cur = nil
    return
  end
  r.tris, r.verts, r.pixels = stat_or0(4), stat_or0(7), stat_or0(5)
  step_end(r)
  local a = r.samples[#r.samples]
  local lost = r.p.gpu and not (gpu3d())         -- the driver gave the 3D to the ARM during the step
  local done = lost or a.ms > LIMIT_MS or time() - r.t0 > RAMP_S or #r.samples >= MAXSTEPS
  local nxt = r.n < 4 and r.n + 1 or r.n + max(r.n // 2, 1)
  if lost then r.samples = {} end
  if done or nxt > r.t.max then
    ramp_end(r)
    cur = nil
  else
    r.n, r.f, r.fms, r.gms, r.gwait = nxt, 0, {}, 0, 0
  end
end

function _draw()
  if phase == "done" then
    draw_summary()
    return
  end
  -- 1. the frame before: its cost is the runtime's stat(1) now
  if cur and cur.last_f >= 1 then frame_done() end
  -- 2. a run to start
  while not cur do
    if #pending > 0 then
      local q = table.remove(pending, 1)
      cur = run_start(q[1], q[2], "stress", q[4])
      if cur then cur.idx = q[3] end
    else
      k = k + 1
      if k > #plan then
        finish()
        draw_summary()
        return
      end
      cur = run_start(plan[k][1], plan[k][2], "ramp")
    end
  end
  -- 3. this frame: 0 is the warm one (textures, corners, caches), 1..frames are measured
  local r = cur
  r.fill = r.t.frame(r.n, r.f) or 0
  r.last_f = r.f
  r.f = r.f + 1
  print_overlay(r)
end
