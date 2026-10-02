-- Benchmark (dev kit): how far the console goes. Mechs in a ring, running
-- and firing, more and more of them every 3 seconds at the chosen quality;
-- the average frame time of each step is kept. It stops when a step passes
-- 33 ms (30 fps) and shows the table: the last row under 16.7 ms is what
-- the console holds at 60 fps.

local Bench = {}
local STEPS = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20 }
local STEP_LEN = 3.0

function Bench.start()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  Bench.i, Bench.t, Bench.rows, Bench.done = 0, 0, {}, false
  Bench.q = G.quality
  G.qauto_was, G.qauto = G.qauto, false
  Bench.cam = Actors.spawn("rally", 1, 0, 0, -18, 0, { name = "Camera" })
  Bench.cam.hidden = true
  G.dmg_numbers = false
  Bench.grow()
end

function Bench.grow()
  Bench.i = Bench.i + 1
  local n = STEPS[Bench.i]
  if not n then Bench.finish() return end
  -- the ring: every mech on the far team, some firing at the camera
  while #G.actors - 1 < n do
    local k = #G.actors
    local a = Actors.spawn("rally", 2, 0, 0, 0, 0, { name = "M" .. k, dummy = true, respawn = 0.5 })
    a.bench_k = k
  end
  for _, a in ipairs(G.actors) do
    if a.bench_k then
      local ang = (a.bench_k - 1) / n * 2 * pi
      a.x, a.z = sin(ang) * 9, cos(ang) * 9 + 4
      a.home.x, a.home.z = a.x, a.z
    end
  end
  Bench.t, Bench.sum, Bench.frames, Bench.worst = 0, 0, 0, 0
end

function Bench.finish()
  Bench.done = true
  G.qauto = G.qauto_was
  local best = 0
  for _, r in ipairs(Bench.rows) do if r.ms <= 16.7 then best = r.n end end
  Bench.best = best
  log(string.format("overbit bench quality %s: %d mechs at 60 fps", Quality.names[Bench.q + 1], best))
  for _, r in ipairs(Bench.rows) do
    log(string.format("overbit bench %2d mechs %5.1f ms (worst %5.1f) %d tri %d px", r.n, r.ms, r.worst, r.tri, r.px))
  end
end

function Bench.update()
  if Input.cmd.menu_p or (Bench.done and Input.cmd.jump_p) then Modes.start("menu") return end
  if Bench.done then return end
  Bench.t = Bench.t + DT
  local cam = Bench.cam
  for _, a in ipairs(G.actors) do
    if a ~= cam then
      local c = Input.blank(a.cmd)
      local dx, dz = cam.x - a.x, cam.z - a.z
      a.yaw = atan(dx, dz)
      c.mx = sin(G.t * 1.3 + a.id) * 0.8
      c.fire = (a.id % 2 == 0) and (G.t % 3 < 1.5)
      a.cmd = c
    end
    if a.alive then a.hero.update(a, a.cmd) end
    Actors.tick_fx(a)
    Actors.animate(a)
  end
  Proj.update()
  Fx.update()
  -- skip the first half second of each step (things settle)
  if Bench.t > 0.5 then
    local ms = stat(1)
    Bench.sum, Bench.frames = Bench.sum + ms, Bench.frames + 1
    Bench.worst = max(Bench.worst, ms)
  end
  if Bench.t >= STEP_LEN then
    local avg = Bench.sum / max(1, Bench.frames)
    Bench.rows[#Bench.rows + 1] = { n = STEPS[Bench.i], ms = avg, worst = Bench.worst, tri = stat(4), px = stat(5) }
    if avg > 33.3 then Bench.finish() else Bench.grow() end
  end
end

function Bench.draw()
  local cam = Bench.cam
  local yaw = 0.15 * sin(G.t * 0.4)
  Cam.set(cam.x, 2.4, cam.z, yaw, -0.08, 0, Cam.fp_fov)
  Modes.draw_scene(cam)
  font("6x12")
  rectfill(0, 0, 320, 14, 0x101418)
  print(string.format("BENCHMARK %s  %d mechs  %.1f ms", Quality.names[Bench.q + 1],
    STEPS[min(Bench.i, #STEPS)] or 0, stat(1)), 4, 2, 0xFFFFFF)
  if Bench.done or #Bench.rows > 0 then
    local y = 18
    rectfill(196, 16, 122, 14 + 11 * #Bench.rows, 0x101418)
    print("mechs   ms  worst", 200, y, 0xFFE070)
    for _, r in ipairs(Bench.rows) do
      y = y + 11
      print(string.format("%3d  %5.1f  %5.1f", r.n, r.ms, r.worst), 200, y, r.ms <= 16.7 and 0x80FF90 or 0xFF8080)
    end
  end
  if Bench.done then
    local s = string.format("60 FPS UP TO %d MECHS", Bench.best)
    rectfill(80, 150, 160, 16, 0xF26A21)
    print(s, 160 - #s * 3, 152, 0xFFFFFF)
  end
  font()
end

Modes.list.bench = Bench
