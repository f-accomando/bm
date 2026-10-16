-- Game budget (API-1a): how many sprites, tiles, 3D spheres, particles and
-- points this console draws at 60 and 30 fps. Same method as
-- carts/stress/main.lua (copied, not imported): 20 frames a step, the load
-- grows, a step stops past 40 ms or past max. The cost of a step is the mean
-- of stat(1) over its 20 frames; the loads at 16.667 and 33.333 ms are
-- interpolated linearly, as in its threshold(). Results go to log() and to
-- report("budget", ...); the table stays on screen for 600 frames.

local lib = require "bmlib"
local W, H = SCREEN_W, SCREEN_H
local FRAMES, LIMIT = 20, 40.0
local PROF_FRAMES, SHOW_FRAMES = 90, 600
local BG = 0x0A0A1E
local ball, P

local tests = {
  { name = "spr16", unit = "spr", start = 16, max = 30000,
    draw = function(n, f)
      cls(BG)
      for i = 1, n do
        local x = (i * 97 + f * (1 + i % 5)) % (W + 16) - 16
        local y = (i * 61 + f * (1 + i % 3)) % (H + 16) - 16
        spr(0, x, y, 2, 2, i % 2 == 1)
      end
    end },
  { name = "tiles8", unit = "tile", start = 81, max = 400000,
    draw = function(n, f)
      cls(BG)
      local r = 0
      while 81 * r < n do
        local w = math.min(81, n - 81 * r)
        map(0, r % 46, -(f % 8), (r * 8) % 368 - (f % 8), w, 1)
        r = r + 1
      end
    end },
  { name = "mesh96", unit = "obj", start = 1, max = 3000,
    draw = function(n, f)
      cls(BG)
      zclear()
      camera3d(0, 0, -8, 0, 0, 60)
      local side = math.ceil(math.sqrt(n))
      local step = 9.0 / math.max(side, 1)
      for i = 0, n - 1 do
        local x = -4.5 + step * (i % side + 0.5)
        local y = 2.6 - step * 0.56 * (i // side + 0.5)
        draw3d(ball, x, y, i % 3, f * 0.03 + i, f * 0.05, 0, step * 0.45)
      end
    end },
  { name = "part2d", unit = "part", start = 16, max = 100000,
    setup = function(n)
      P = lib.particles(n)
      for i = 1, n do
        P:add(i * 97 % W, i * 61 % H, (i % 7 - 3) * 0.25, (i % 5 - 2) * 0.25, 1000, 0xFFD050, { size = 1 })
      end
    end,
    update = function(n, f) P:update() end,
    draw = function(n, f)
      cls(BG)
      P:draw()
    end },
  { name = "point3d", unit = "pt", start = 16, max = 100000,
    draw = function(n, f)
      cls(BG)
      zclear()
      camera3d(0, 0, -8, 0, 0, 60)
      for i = 0, n - 1 do
        point3d(((i * 97 + f) % 900) / 100 - 4.5, ((i * 61) % 500) / 100 - 2.5, (i % 7) * 0.5, 2, 0xFFD050)
      end
    end },
}

local ti, frame, acc, tris = 1, 0, 0, 0
local n = tests[1].start
local samples, rows = {}, {}
local phase = "steps"              -- "steps", then "profile" (spr16 only), then "show"
local prof_n, pf = nil, 0          -- spr16's 60 fps load (a number or nil), profile frame
local prof_lines = {}
local shown, shown_frames = {}, 0

-- load at which the cost reaches limit ms: text, and the 3D triangles there (obj only)
local function threshold(t, s, limit)
  if #s == 0 or s[1].ms > limit then return "<" .. t.start, nil end
  for i = 2, #s do
    if s[i].ms > limit then
      local a, b = s[i - 1], s[i]
      local k = (limit - a.ms) / (b.ms - a.ms)
      local nn = math.floor(a.n + k * (b.n - a.n))
      local tr = nil
      if t.unit == "obj" then tr = math.floor(a.tris + k * (b.tris - a.tris)) end
      return tostring(nn), tr
    end
  end
  return ">" .. t.max, nil
end

local function finish_test()
  local t = tests[ti]
  local a, b = samples[1], samples[#samples]
  local per = #samples > 1 and (b.ms - a.ms) * 1000 / (b.n - a.n) or 0
  local t60, tris60 = threshold(t, samples, 16.667)
  local t30 = threshold(t, samples, 33.333)
  if t.name == "spr16" then prof_n = tonumber(t60) end
  rows[#rows + 1] = string.format("%s,%s,%s,%s,%.2f,%s", t.name, t.unit, t60, t30, per,
    tris60 and string.format("%d", tris60) or "")
end

local function finish_report()
  local lines = { "bm budget 1", string.format("screen %dx%d", W, H),
                  string.format("gpu3d %d", stat(9)), "test,unit,n60,n30,us_item,tris60" }
  for _, r in ipairs(rows) do lines[#lines + 1] = r end
  if prof_n then
    lines[#lines + 1] = string.format("profile_load,spr16,%d", prof_n)
    for _, l in ipairs(prof_lines) do lines[#lines + 1] = l end
  end
  for _, l in ipairs(lines) do log("budget: " .. l) end
  local ok = report("budget", table.concat(lines, "\n"))
  log("budget: report " .. tostring(ok))
  log("budget: done")
  shown, phase = lines, "show"
end

function _init()
  for y = 0, 15 do
    for x = 0, 15 do
      if (x - 7.5) ^ 2 + (y - 7.5) ^ 2 < 60 then sset(x, y, rgb(x * 16, y * 16, 200)) end
    end
  end
  -- sheet cell 1 (8x8, pixels x 8..15 and y 0..7 of a 256-wide sheet): the map's tile
  for y = 0, 7 do
    for x = 8, 15 do
      sset(x, y, rgb(40, 160, 80))
    end
  end
  ball = mesh_sphere(6, 8, 0x4080FF, 0xFFC040)
  if tests[ti].setup then tests[ti].setup(n) end
end

function _update()
  if phase == "steps" then
    local t = tests[ti]
    if t.update then t.update(n, frame) end
    if frame > 0 then acc = acc + stat(1) end    -- cost of the previous frame
    if frame == FRAMES then tris = stat(4) end   -- 3D triangles of the last frame
    frame = frame + 1
    if frame <= FRAMES then return end

    local ms = acc / FRAMES
    samples[#samples + 1] = { n = n, ms = ms, tris = tris }
    frame, acc = 0, 0
    n = math.max(n + 1, n * 3 // 2)
    if ms > LIMIT or n > t.max then
      finish_test()
      ti, samples = ti + 1, {}
      if ti > #tests then
        if prof_n then
          phase, pf = "profile", 0
          profile(true)
        else
          finish_report()
        end
        return
      end
      n = tests[ti].start
    end
    if tests[ti].setup then tests[ti].setup(n) end
  elseif phase == "profile" then
    pf = pf + 1
    if pf == PROF_FRAMES then
      local list = profile() or {}
      for i = 1, math.min(10, #list) do
        local p = list[i]
        prof_lines[i] = string.format("profile,%s,%s,%.3f,%.3f", p.name, p.where, p.self, p.total)
      end
      profile(false)
      finish_report()
    end
  elseif phase == "show" then
    shown_frames = shown_frames + 1
    if shown_frames >= SHOW_FRAMES then quit() end
  end
end

function _draw()
  if phase == "steps" then
    local t = tests[ti]
    t.draw(n, frame)
    rectfill(0, 0, W, 16, 0)
    print(string.format("budget: %s  n=%d  %.2f ms", t.name, n, stat(1)), 0, 0, 0xFFFFFF)
  elseif phase == "profile" then
    tests[1].draw(prof_n, pf)
    rectfill(0, 0, W, 16, 0)
    print(string.format("budget: profile spr16  n=%d  %.2f ms", prof_n, stat(1)), 0, 0, 0xFFFFFF)
  else
    cls(BG)
    for i, l in ipairs(shown) do print(l, 4, 4 + 16 * (i - 1), 0xFFFFFF) end
  end
end
