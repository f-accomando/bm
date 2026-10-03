-- Benchmark (dev kit): how far the console goes, with each of its 3D
-- renderers. Part one, the match: ten bots play Control on Partenope and
-- the console draws it with each renderer (the ARM, the GPU, the GPU with
-- anti-aliasing, where they are) at each quality. Every phase plays the
-- same match (the same seed: the quality never changes the game), first
-- without drawing up to the fight on the point, then measured: half in a
-- bot's eyes, half from above the point. Part two, the stress: heroes in a
-- ring, more of them every 3 seconds, until a step passes 33 ms (30 fps),
-- with each renderer. The report at the end, in pages, and in the log.

local Bench = {}
local Match = Modes.list.match                   -- 81_match

local SEED = 2026
local RULES = { unlock = 6 }                     -- the point opens soon: the fight comes earlier
local SETTLE = 0.5                               -- seconds not counted after a change
local RING_Q = 3
-- seconds of match before the measure, seconds measured in each phase, the
-- qualities, the ring's steps and their seconds; shorter for the tests
-- (OVERBIT_BENCH_FAST, set after the code: read at the start)
local WARM, MEASURE, QUALITIES, RING, RING_LEN

-- For counting instructions on the PC (tests/overbit/frames.py): only one
-- phase (OVERBIT_BENCH_ONE = "gpu:2"), the fight at once (OVERBIT_BENCH_HOT:
-- the two teams face to face at the point, no warming up; a number: the
-- seconds of the measure, 4 by default), and a stop after this many seconds
-- of the measure (OVERBIT_BENCH_STOP).
local function config()
  local fast = OVERBIT_BENCH_FAST
  WARM, MEASURE = fast and 8 or 24, fast and 2 or 8
  if OVERBIT_BENCH_HOT then WARM, MEASURE = 0, tonumber(OVERBIT_BENCH_HOT) or 4 end
  QUALITIES = fast and { 2 } or { 1, 2, 3, 4 }
  RING = fast and { 1, 4, 8 } or { 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32 }
  RING_LEN = fast and 1 or 3
end
local STEPS_A_FRAME = 40                         -- match frames simulated a frame while warming up

local RNAME = { arm = "ARM", gpu = "GPU", aa = "GPU+AA", vs1 = "GPU+VS1", vs = "GPU+VS" }
local VS = { vs1 = 1, vs = 2 }                   -- the vertex shader: the scenery, every model

-- ---------------------------------------------------------------- phases

-- the ARM, the GPU, the GPU with anti-aliasing, the GPU with its vertex
-- shader for the scenery and for every model (where this GPU has them)
local function renderers()
  local on0, aa0, vs0 = gpu3d()
  local list = { "arm" }
  if gpu3d(true) then
    list[#list + 1] = "gpu"
    local _, aa = gpu3d(true, true, 0)
    if aa then list[#list + 1] = "aa" end
    local _, _, vs = gpu3d(true, false, 2)
    if vs then
      list[#list + 1] = "vs1"
      list[#list + 1] = "vs"
    end
  end
  gpu3d(on0, aa0, vs0 or 0)
  return list, on0, aa0, vs0
end

local function use_renderer(r)
  gpu3d(r ~= "arm", r == "aa", VS[r] or 0)
end

function Bench.start()
  config()
  Bench.saved = { q = G.quality, qauto = G.qauto, diff = G.bot_diff, rules = {} }
  local list, on0, aa0, vs0 = renderers()
  Bench.saved.on, Bench.saved.aa, Bench.saved.vs = on0, aa0, vs0
  Bench.renderers = list
  Bench.phases = {}
  for _, r in ipairs(list) do
    for _, q in ipairs(QUALITIES) do Bench.phases[#Bench.phases + 1] = { kind = "match", r = r, q = q } end
  end
  for _, r in ipairs(list) do Bench.phases[#Bench.phases + 1] = { kind = "ring", r = r, q = RING_Q } end
  if OVERBIT_BENCH_ONE then
    local r, q = OVERBIT_BENCH_ONE:match("(%w+):(%d)")
    Bench.renderers = { r }
    Bench.phases = { { kind = "match", r = r, q = tonumber(q) } }
  end
  Bench.rows, Bench.rings = {}, {}
  Bench.i, Bench.done, Bench.page = 0, false, 1
  G.qauto = false
  G.bot_diff = 2
  for k, v in pairs(RULES) do Bench.saved.rules[k] = Match.rules[k] Match.rules[k] = v end
  log(string.format("overbit bench start: %d phases, renderers %s", #Bench.phases, table.concat(list, " ")))
  Bench.next()
end

local function restore()
  local s = Bench.saved
  for k, v in pairs(s.rules) do Match.rules[k] = v end
  G.bot_diff = s.diff
  G.qauto = s.qauto
  Quality.set(s.q)
  gpu3d(s.on, s.aa, s.vs or 0)
  Match.bench = nil
end

-- the next phase, or the report
function Bench.next()
  Bench.i = Bench.i + 1
  local ph = Bench.phases[Bench.i]
  if not ph then Bench.finish() return end
  Bench.ph = ph
  use_renderer(ph.r)
  Quality.set(ph.q)
  Bench.t, Bench.frames = 0, {}
  Bench.sum = { upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0, n = 0, trimax = 0 }
  if ph.kind == "match" then
    -- the same match every time
    math.randomseed(SEED)
    grandom_seed(SEED)
    Match.bench = true
    Match.start()
    if OVERBIT_BENCH_HOT then
      -- the two teams already face to face at the point
      local P = World.mark("point")
      for i, a in ipairs(G.actors) do
        a.x, a.z = P.x + (a.team == 1 and -7 or 7), P.z + (i % 5 - 2) * 2.5
        a.yaw = a.team == 1 and pi / 2 or -pi / 2
      end
      Match.state.phase, Match.state.t = "play", 0
    end
    Bench.warm = WARM * 60
    Bench.who = nil
  else
    Bench.ring_start()
  end
end

-- ---------------------------------------------------------------- the ring

function Bench.ring_start()
  Match.bench = nil
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  World.use("range")
  Bench.cam = Actors.spawn("rally", 1, 0, 0, -18, 0, { name = "Camera" })
  Bench.cam.hidden = true
  G.local_actor = Bench.cam
  G.dmg_numbers = false
  Bench.ring = { i = 0, rows = {}, best60 = 0, best30 = 0 }
  Bench.ring_grow()
end

function Bench.ring_grow()
  local R = Bench.ring
  R.i = R.i + 1
  local n = RING[R.i]
  if not n then Bench.ring_end() return end
  while #G.actors - 1 < n do
    local k = #G.actors
    local id = HERO_ORDER[(k - 1) % #HERO_ORDER + 1]
    local a = Actors.spawn(id, 2, 0, 0, 0, 0, { name = "H" .. k, dummy = true, respawn = 0.5 })
    a.bench_k = k
  end
  for _, a in ipairs(G.actors) do
    if a.bench_k then
      local ang = (a.bench_k - 1) / n * 2 * pi
      a.x, a.z = sin(ang) * 9, cos(ang) * 9 + 4
      a.home.x, a.home.z = a.x, a.z
    end
  end
  Bench.t, Bench.frames = 0, {}
  Bench.sum = { upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0, n = 0, trimax = 0 }
end

function Bench.ring_end()
  local R = Bench.ring
  local res = { r = Bench.ph.r, best60 = R.best60, best30 = R.best30, rows = R.rows }
  Bench.rings[#Bench.rings + 1] = res
  log(string.format("overbit bench ring %s: %d heroes at 60 fps, %d at 30 fps", RNAME[res.r], res.best60, res.best30))
  Bench.next()
end

-- ---------------------------------------------------------------- measures

-- the numbers of the frame before (stat() tells the last frame's)
local function sample()
  if Bench.t <= SETTLE then return end
  local S = Bench.sum
  Bench.frames[#Bench.frames + 1] = stat(1)
  S.n = S.n + 1
  S.upd = S.upd + (G.update_ms or 0)
  S.d3 = S.d3 + stat(6)
  local tri = stat(4)
  S.tri, S.vtx, S.px, S.fps = S.tri + tri, S.vtx + stat(7), S.px + stat(5), S.fps + stat(2)
  if tri > S.trimax then S.trimax = tri end
end

-- the frames of a phase: average, 1% (the 99th percentile), worst, over 16.7 ms
local function summary()
  local f = Bench.frames
  local n = #f
  local S = Bench.sum
  if n == 0 then return { ms = 0, p99 = 0, worst = 0, over = 0, upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0 } end
  local sum, over = 0, 0
  local sorted = {}
  for i, ms in ipairs(f) do
    sum = sum + ms
    if ms > 16.7 then over = over + 1 end
    sorted[i] = ms
  end
  table.sort(sorted)
  local nn = max(1, S.n)
  return { ms = sum / n, p99 = sorted[max(1, math.ceil(n * 0.99))], worst = sorted[n], over = over * 100 / n,
           upd = S.upd / nn, d3 = S.d3 / nn, tri = S.tri / nn, trimax = S.trimax, vtx = S.vtx / nn,
           px = S.px / nn, fps = S.fps / nn }
end

-- where everyone is: the same in every phase (the same match)
local function match_hash()
  local h = 0
  for _, a in ipairs(G.actors) do h = (h * 31 + ((a.x * 100) // 1) + ((a.z * 100) // 1) * 7 + a.hp // 1) % 1000003 end
  return h
end

local function match_done()
  local ph = Bench.ph
  local s = summary()
  s.r, s.q = ph.r, ph.q
  log(string.format("overbit bench %s %s match %d", RNAME[ph.r], Quality.names[ph.q + 1], match_hash()))
  Bench.rows[#Bench.rows + 1] = s
  log(string.format("overbit bench %s %s: %.0f fps, %.1f ms (1%% %.1f, worst %.1f, %.0f%% over 16.7), update %.1f, "
    .. "3D %.1f, %.0f tri (max %d), %.0f vtx, %.0f px", RNAME[ph.r], Quality.names[ph.q + 1], s.fps, s.ms, s.p99,
    s.worst, s.over, s.upd, s.d3, s.tri, s.trimax, s.vtx, s.px))
  Bench.next()
end

-- ---------------------------------------------------------------- frames

function Bench.update()
  local c = Input.cmd
  if Bench.done then
    if c.left_p then Bench.page = (Bench.page - 2) % 3 + 1 Snd.play("ui") end
    if c.right_p or c.down_p then Bench.page = Bench.page % 3 + 1 Snd.play("ui") end
    if c.menu_p or c.jump_p then Modes.start("menu") end
    return
  end
  if c.menu_p then restore() Modes.start("menu") return end
  local ph = Bench.ph
  if ph.kind == "match" then
    if Bench.warm > 0 then
      -- the match up to the fight, not drawn: the same frames as a match
      local k = min(STEPS_A_FRAME, Bench.warm)
      for i = 1, k do
        if i > 1 then G.t = G.t + DT end
        Match.step()
      end
      Bench.warm = Bench.warm - k
      Bench.t = 0
      return
    end
    sample()
    Bench.t = Bench.t + DT
    Match.step()
    if OVERBIT_BENCH_STOP and Bench.t >= OVERBIT_BENCH_STOP - 1e-6 then
      log(string.format("overbit bench stop %.2f frame %d", Bench.t, Match.state.frame))
      quit()
      return
    end
    if Bench.t >= SETTLE + MEASURE then match_done() end
  else
    sample()
    Bench.t = Bench.t + DT
    local cam = Bench.cam
    for _, a in ipairs(G.actors) do
      if a ~= cam then
        local cmd = Input.blank(a.cmd)
        local dx, dz = cam.x - a.x, cam.z - a.z
        a.yaw = atan(dx, dz)
        cmd.mx = sin(G.t * 1.3 + a.id) * 0.8
        cmd.fire = (a.id % 2 == 0) and (G.t % 3 < 1.5)
        a.cmd = cmd
      end
      if a.alive then a.hero.update(a, a.cmd) end
      Actors.tick_fx(a)
      Actors.animate(a)
    end
    Proj.update()
    Fx.update()
    if Bench.t >= SETTLE + RING_LEN then
      local s = summary()
      local R = Bench.ring
      s.n = RING[R.i]
      R.rows[#R.rows + 1] = s
      if s.ms <= 16.7 then R.best60 = s.n end
      if s.ms <= 33.3 then R.best30 = s.n end
      log(string.format("overbit bench ring %s %2d heroes: %.1f ms, 3D %.1f, %.0f tri", RNAME[Bench.ph.r], s.n, s.ms,
        s.d3, s.tri))
      if s.ms > 33.3 then Bench.ring_end() else Bench.ring_grow() end
    end
  end
end

function Bench.finish()
  Bench.done = true
  restore()
  -- the best quality at 60 fps with each renderer, and how much the GPU gives
  Bench.best = {}
  for _, row in ipairs(Bench.rows) do
    if row.ms <= 16.7 and (not Bench.best[row.r] or row.q > Bench.best[row.r]) then Bench.best[row.r] = row.q end
  end
  for _, r in ipairs(Bench.renderers) do
    local b = Bench.best[r]
    log(string.format("overbit bench best %s: %s", RNAME[r], b and Quality.names[b + 1] .. " at 60 fps" or "none at 60 fps"))
  end
  log("overbit bench done")
end

-- ---------------------------------------------------------------- drawing

local INK = 0x101418

-- the camera of a match phase: a bot's eyes, then above the point
local function match_view()
  local first = Bench.t < SETTLE + MEASURE / 2
  if first then
    local who = Bench.who
    if not who or not who.alive then
      -- the hero nearest the point, alive (the same one in every phase)
      local P = World.mark("point")
      local bd = math.huge
      who = nil
      for _, a in ipairs(G.actors) do
        if a.alive then
          local d = (a.x - P.x) ^ 2 + (a.z - P.z) ^ 2
          if d < bd then who, bd = a, d end
        end
      end
      Bench.who = who
    end
    if who then
      Cam.first(who)
      Modes.draw_scene(who, Match.draw_point)
      if who.hero.draw_fp_extra then who.hero.draw_fp_extra(who, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch) end
      who.hero.draw_fp(who, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll)
      Fx.draw2d()
      Hud.draw(who)
      Match.hud_top()
      return
    end
  end
  local P = World.mark("point")
  Cam.orbit(P.x, 1.5, P.z, 0.6 + Bench.t * 0.12, -0.38, 17, 70)
  Modes.draw_scene(nil, Match.draw_point)
  Fx.draw2d()
  Match.hud_top()
end

local function ring_view()
  local cam = Bench.cam
  Cam.set(cam.x, 2.4, cam.z, 0.15 * sin(G.t * 0.4), -0.08, 0, Cam.fp_fov)
  Modes.draw_scene(cam)
end

local function bar(text)
  font("6x12")
  rectfill(0, 166, 320, 14, INK)
  print(text, 4, 167, 0xFFFFFF)
  font()
end

local function q_short(q) return Quality.names[q + 1]:sub(1, 3) end

local function report()
  cls(0x0A0E14)
  font("6x12")
  local page = Bench.page
  print("OVERBIT BENCHMARK", 4, 2, 0xFFE070)
  print(string.format("PAGE %d/3", page), 262, 2, 0x7A8290)
  local y = 18
  if page == 1 then
    print("MATCH OF 10 BOTS: FRAME TIME", 4, y, 0x7A8290)
    y = y + 13
    print("RENDER   QUAL  FPS   AVG   1%  WORST >16", 4, y, 0xFFE070)
    for _, r in ipairs(Bench.rows) do
      y = y + 11
      print(string.format("%-7s  %-3s  %4.0f %5.1f %5.1f %5.1f %3.0f%%", RNAME[r.r], q_short(r.q), r.fps, r.ms, r.p99,
        r.worst, r.over), 4, y, r.ms <= 16.7 and 0x80FF90 or 0xFF8080)
    end
  elseif page == 2 then
    print("MATCH OF 10 BOTS: WORK A FRAME", 4, y, 0x7A8290)
    y = y + 13
    print("RENDER   QUAL  LUA   3D   TRI   VTX    PX", 4, y, 0xFFE070)
    for _, r in ipairs(Bench.rows) do
      y = y + 11
      print(string.format("%-7s  %-3s %5.1f %4.1f %5.0f %5.0f %5.0f", RNAME[r.r], q_short(r.q), r.upd, r.d3, r.tri, r.vtx,
        r.px), 4, y, 0xD8DCE2)
    end
  else
    print("STRESS: HEROES IN A RING (" .. Quality.names[RING_Q + 1] .. ")", 4, y, 0x7A8290)
    for _, g in ipairs(Bench.rings) do
      y = y + 13
      print(string.format("%-7s %2d HEROES AT 60 FPS, %2d AT 30", RNAME[g.r], g.best60, g.best30), 4, y, 0xD8DCE2)
    end
    y = y + 18
    print("BEST QUALITY AT 60 FPS IN THE MATCH", 4, y, 0x7A8290)
    for _, r in ipairs(Bench.renderers) do
      y = y + 13
      local b = Bench.best[r]
      print(string.format("%-7s %s", RNAME[r], b and Quality.names[b + 1] or "NONE"), 4, y, b and 0x80FF90 or 0xFF8080)
    end
    -- the GPU against the ARM at the same quality
    local arm, gpu = {}, {}
    for _, r in ipairs(Bench.rows) do
      if r.r == "arm" then arm[r.q] = r.ms elseif r.r == "gpu" then gpu[r.q] = r.ms end
    end
    local best, bq = 0, nil
    for q, ms in pairs(gpu) do if arm[q] and arm[q] / ms > best then best, bq = arm[q] / ms, q end end
    if bq then
      y = y + 18
      print(string.format("GPU %.1fx FASTER THAN ARM (%s)", best, Quality.names[bq + 1]), 4, y, 0xFFE070)
    end
  end
  local px = prompt(Input.cmd.pad and "LEFT" or "left", 4, 165, true)
  px = prompt(Input.cmd.pad and "RIGHT" or "right", px + 2, 165, true)
  print("PAGE", px + 3, 165, 0x7A8290)
  px = prompt(Input.cmd.pad and "A" or "space", px + 36, 165, true)
  print("MENU", px + 3, 165, 0x7A8290)
  font()
end

function Bench.draw()
  if Bench.done then report() return end
  local ph = Bench.ph
  local where = string.format("%d/%d %s %s", Bench.i, #Bench.phases, RNAME[ph.r], Quality.names[ph.q + 1])
  if ph.kind == "match" then
    if Bench.warm > 0 then
      cls(0x0A0E14)
      font("6x12")
      print("OVERBIT BENCHMARK", 4, 2, 0xFFE070)
      print("THE BOTS PLAY UP TO THE FIGHT...", 4, 80, 0xD8DCE2)
      local done = 1 - Bench.warm / (WARM * 60)
      rectfill(4, 96, 312, 6, 0x2A2E36)
      rectfill(4, 96, max(1, (312 * done) // 1), 6, 0xF26A21)
      font()
      bar("PHASE " .. where)
      return
    end
    match_view()
    bar(string.format("PHASE %s  %.1f ms", where, stat(1)))
  else
    ring_view()
    bar(string.format("STRESS %s  %d HEROES  %.1f ms", RNAME[ph.r], RING[Bench.ring.i] or 0, stat(1)))
  end
end

Modes.list.bench = Bench
