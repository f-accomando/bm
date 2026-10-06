-- Benchmark (dev kit): how far the console goes, and what the game should
-- default to. About one minute. Every phase plays the same match (the same
-- seed: the quality never changes the game) first without drawing up to the
-- fight on the point, then measured: half in a bot's eyes, half from above
-- the point. The report at the end, in pages, in the log and sent (report()).
--
-- BENCHMARK in the title: the calibration. From the heaviest of the ladder
-- (85_quality: 1080p at levels 4..1, then 640x360) down, the first step that
-- holds 60 fps is measured last and saved as the default of the game.
--
-- With flags it measures what is asked, on any screen and renderer, and
-- leaves when done (the report goes to the repository as the others):
--
--   set overbit_bench=FLAGS ; play overbit      (a monitor line)
--   ./easy_install.sh bench FLAGS [profile]
--
-- FLAGS: words "key:value" apart by "/", lists by "+" (a config line has no
-- room for commas or semicolons):
--   auto                      the calibration above (the same as no flags)
--   res:1080+640              the screens: 1080, 640 (360p), or WxH of the console's modes
--   gpu:vs+gpu+arm            the renderers: arm gpu aa gq vs1 vs q (this GPU's only)
--   q:4+3+2                   the qualities, 0..4
--   secs:4                    the seconds measured in each phase (default 3)
--   warm:10                   the seconds of match before the measure (default 10)
--   ring:1                    after it, the heroes in a ring (default 0)
--   save:1                    the best of those that hold 60 fps becomes the default
--                             (the calibration does it by itself; save:0 stops it)
--   stay:1                    do not leave to the title when done
-- With res, gpu or q given the phases are all their combinations (the renderer
-- the console starts with, and every quality, where one is missing).

local Bench = {}
local Match = Modes.list.match                   -- 81_match

-- The bench's lines go to the log and, at the end, into a report (the
-- kernel's report(), 2026-10-04): saved on the SD card and sent to the
-- reports' git repository, instead of photos of the pages.
local send_report = report
local lines = {}
local function blog(s)
  lines[#lines + 1] = s
  log(s)
end

local SEED = 2026
local RULES = { unlock = 6 }                     -- the point opens soon: the fight comes earlier
local SETTLE = 0.5                               -- seconds not counted after a change
local RING_Q = 3
local PASS_MS, PASS_OVER = 16.0, 5               -- "holds 60 fps": the mean frame and the share over 16.7 ms
local TIME_LIMIT = 70                            -- seconds: the calibration stops going down
-- seconds of match before the measure, seconds measured in each phase, the
-- ring's steps and their seconds; shorter for the tests (OVERBIT_BENCH_FAST,
-- set after the code: read at the start)
local WARM, MEASURE, RING, RING_LEN

-- For counting instructions on the PC (tests/overbit/frames.py): only one
-- phase (OVERBIT_BENCH_ONE = "gpu:2"), the fight at once (OVERBIT_BENCH_HOT:
-- the two teams face to face at the point, no warming up; a number: the
-- seconds of the measure, 4 by default), and a stop after this many seconds
-- of the measure (OVERBIT_BENCH_STOP).
local function config(opt)
  local fast = OVERBIT_BENCH_FAST
  WARM, MEASURE = fast and 8 or opt.warm or 10, fast and 2 or opt.secs or 3
  if OVERBIT_BENCH_HOT then WARM, MEASURE = 0, tonumber(OVERBIT_BENCH_HOT) or 4 end
  RING = fast and { 1, 4, 8 } or { 1, 2, 4, 8, 12, 16, 24, 32 }
  RING_LEN = fast and 1 or 2
end
local STEPS_A_FRAME = 40                         -- match frames simulated a frame while warming up

local RNAME = { arm = "ARM", gpu = "GPU", aa = "GPU+AA", gq = "GPU+Q", vs1 = "GPU+VS1", vs = "GPU+VS", q = "GPU+VS+Q" }
local VS = { vs1 = 1, vs = 2, q = 2 }            -- the vertex shader: the scenery, every model

-- ---------------------------------------------------------------- the flags

local function words(s)
  local out = {}
  for w in tostring(s):gmatch("[^+]+") do out[#out + 1] = w end
  return out
end

local function parse_flags(text)
  local opt = {}
  for item in tostring(text or ""):gmatch("[^/]+") do
    local k, v = item:match("^([%w_]+):?(.*)$")
    if k == "res" then opt.res = words(v)
    elseif k == "gpu" then opt.gpu = words(v)
    elseif k == "q" then opt.q = {} for _, w in ipairs(words(v)) do opt.q[#opt.q + 1] = clamp(tonumber(w) or 3, 0, 4) // 1 end
    elseif k == "secs" or k == "warm" or k == "ring" or k == "save" or k == "stay" then opt[k] = tonumber(v) or 1
    end
  end
  return opt
end

-- "1080" -> 1920, 1080; "640" or "360" -> 640, 360; "WxH" as it is
local function res_of(w)
  if w == "1080" or w == "1080p" then return 1920, 1080 end
  if w == "640" or w == "360" or w == "360p" then return 640, 360 end
  local a, b = w:match("^(%d+)x(%d+)$")
  if a then return tonumber(a), tonumber(b) end
end

-- ---------------------------------------------------------------- phases

-- the renderer the console starts with, by name
local function current_name()
  local on, aa, vs, _, q = gpu3d()
  if not on then return "arm" end
  if aa then return "aa" end
  if vs == 1 then return "vs1" end
  if vs and vs >= 2 then return q and "q" or "vs" end
  return q and "gq" or "gpu"
end

-- the renderers this GPU has, of those asked (the ARM, the GPU, the GPU with
-- anti-aliasing, with the frame queue (M35), with its vertex shader for the
-- scenery and for every model, and that with the queue)
local function available(name)
  if name == "arm" then return true end
  local on0, aa0, vs0, _, q0 = gpu3d()
  local ok
  if name == "gpu" then ok = gpu3d(true, false, 0, false)
  elseif name == "aa" then local _, aa = gpu3d(true, true, 0, false) ok = aa
  elseif name == "gq" then ok = select(5, gpu3d(true, false, 0, true))
  elseif name == "vs1" or name == "vs" then local _, _, vs = gpu3d(true, false, 2, false) ok = vs
  elseif name == "q" then ok = select(5, gpu3d(true, false, 2, true)) end
  gpu3d(on0, aa0, vs0 or 0, q0 or false)
  return ok and true or false
end

local function use_renderer(r)
  if not RNAME[r] then return end
  gpu3d(r ~= "arm", r == "aa", VS[r] or 0, r == "q" or r == "gq")
end
Bench.use_renderer = use_renderer

-- the phases and what they decide: the ladder or the matrix
local function plan(opt)
  local ladder = Quality.ladder()
  local cur = current_name()
  local phases = {}
  local matrix = opt.res or opt.gpu or opt.q
  if not matrix then
    for _, e in ipairs(ladder) do phases[#phases + 1] = { kind = "match", r = cur, w = e[1], h = e[2], q = e[3] } end
    return phases, false
  end
  local reslist = {}
  for _, w in ipairs(opt.res or {}) do
    local a, b = res_of(w)
    if a then reslist[#reslist + 1] = { a, b } end
  end
  if #reslist == 0 then
    local modes = Quality.modes()
    reslist[1] = modes[#modes] or { SW, SH }
  end
  local rs = {}
  for _, r in ipairs(opt.gpu or { cur }) do
    if RNAME[r] and available(r) then rs[#rs + 1] = r end
  end
  if #rs == 0 then rs[1] = cur end
  for _, r in ipairs(rs) do
    for _, m in ipairs(reslist) do
      for _, q in ipairs(opt.q or { 4, 3, 2, 1 }) do
        -- the ARM does not draw above 640x360 (seconds a frame at 1080p)
        if r ~= "arm" or m[1] * m[2] <= 640 * 360 then
          phases[#phases + 1] = { kind = "match", r = r, w = m[1], h = m[2], q = q }
        end
      end
    end
  end
  return phases, true
end

function Bench.start()
  local opt = parse_flags(Bench.flags)
  Bench.flags_used = Bench.flags
  Bench.flags = nil
  Bench.opt = opt
  config(opt)
  Bench.saved = { q = G.quality, qauto = G.qauto, diff = G.bot_diff, rules = {}, w = SCREEN_W, h = SCREEN_H }
  local on0, aa0, vs0, _, q0 = gpu3d()
  Bench.saved.on, Bench.saved.aa, Bench.saved.vs, Bench.saved.queue = on0, aa0, vs0, q0
  Bench.ready = false
  Bench.rows, Bench.rings, Bench.ver = {}, {}, {}
  Bench.i, Bench.done, Bench.page = 0, false, 1
  Bench.phases, Bench.renderers = {}, {}
  G.qauto = false
  G.bot_diff = 2
  for k, v in pairs(RULES) do Bench.saved.rules[k] = Match.rules[k] Match.rules[k] = v end
  lines = {}
  Bench.began = time()
end

-- the first frame: the GPU is up (stat(9)), the console's modes are known
function Bench.setup()
  Bench.ready = true
  local opt = Bench.opt
  local phases, matrix = plan(opt)
  Bench.matrix = matrix
  Bench.phases = phases
  local seen = {}
  for _, ph in ipairs(phases) do
    if not seen[ph.r] then seen[ph.r] = true Bench.renderers[#Bench.renderers + 1] = ph.r end
  end
  if matrix and opt.ring == 1 then
    for _, r in ipairs(Bench.renderers) do Bench.phases[#Bench.phases + 1] = { kind = "ring", r = r, q = RING_Q } end
  end
  if OVERBIT_BENCH_ONE then
    local r, q = OVERBIT_BENCH_ONE:match("(%w+):(%d)")
    Bench.renderers = { r }
    Bench.phases = { { kind = "match", r = r, w = SW, h = SH, q = tonumber(q) } }
    Bench.matrix = true
  end
  if OVERBIT_BENCH_FAST and not matrix then       -- the tests: three steps of the ladder
    local few = {}
    for i = 1, min(3, #phases) do few[i] = phases[i] end
    Bench.phases = few
  end
  blog(string.format("overbit bench start: %d phases (%s), flags %s, screen %dx%d, %s", #Bench.phases,
    matrix and "matrix" or "calibration", Bench.flags_used or "auto", SW, SH, G.gpu and "GPU" or "ARM"))
  Bench.next()
end

local function restore(keep_screen)
  local s = Bench.saved
  for k, v in pairs(s.rules) do Match.rules[k] = v end
  G.bot_diff = s.diff
  G.qauto = s.qauto
  Quality.set(s.q)
  gpu3d(s.on, s.aa, s.vs or 0, s.queue or false)
  if not keep_screen then Quality.set_screen(s.w, s.h) end
  Match.bench = nil
end

-- the next phase, or the report
function Bench.next()
  Bench.i = Bench.i + 1
  local ph = Bench.phases[Bench.i]
  -- the calibration stops at the first step that holds, or when the time is up
  if ph and not Bench.matrix and Bench.found then ph = nil end
  if ph and not Bench.matrix and Bench.i > 1 and time() - Bench.began > TIME_LIMIT then ph = nil end
  if not ph then Bench.finish() return end
  Bench.ph = ph
  use_renderer(ph.r)
  if ph.kind == "match" and ph.w then Quality.set_screen(ph.w, ph.h) end
  Bench.ver[ph.r] = select(4, gpu3d())        -- the bm3d version (nil on a runtime before it)
  Quality.set(ph.q)
  Bench.t, Bench.frames = 0, {}
  Bench.sum = { upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0, n = 0, trimax = 0, ins = 0, kb = 0 }
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
  Bench.sum = { upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0, n = 0, trimax = 0, ins = 0, kb = 0 }
end

function Bench.ring_end()
  local R = Bench.ring
  local res = { r = Bench.ph.r, best60 = R.best60, best30 = R.best30, rows = R.rows }
  Bench.rings[#Bench.rings + 1] = res
  blog(string.format("overbit bench ring %s: %d heroes at 60 fps, %d at 30 fps", RNAME[res.r], res.best60, res.best30))
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
  S.ins, S.kb = S.ins + stat(10), max(S.kb, stat(12))     -- Lua instructions of the frame, KiB of Lua at most
end

-- the frames of a phase: average, 1% (the 99th percentile), worst, over 16.7 ms
local function summary()
  local f = Bench.frames
  local n = #f
  local S = Bench.sum
  if n == 0 then return { ms = 0, p99 = 0, worst = 0, over = 0, upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0, ins = 0, kb = 0 } end
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
           px = S.px / nn, fps = S.fps / nn, ins = S.ins / nn, kb = S.kb }
end

-- where everyone is: the same in every phase (the same match)
local function match_hash()
  local h = 0
  for _, a in ipairs(G.actors) do h = (h * 31 + ((a.x * 100) // 1) + ((a.z * 100) // 1) * 7 + a.hp // 1) % 1000003 end
  return h
end

local function holds(s) return s.ms <= PASS_MS and s.over <= PASS_OVER end

local function match_done()
  local ph = Bench.ph
  local s = summary()
  s.r, s.q, s.w, s.h = ph.r, ph.q, ph.w or SW, ph.h or SH
  s.ok = holds(s)
  blog(string.format("overbit bench %s %dx%d %s match %d", RNAME[ph.r], s.w, s.h, Quality.names[ph.q + 1], match_hash()))
  Bench.rows[#Bench.rows + 1] = s
  blog(string.format("overbit bench %s %dx%d %s: %.0f fps, %.1f ms (1%% %.1f, worst %.1f, %.0f%% over 16.7), update %.1f, "
    .. "3D %.1f, %.0f tri (max %d), %.0f vtx, %.0f px, %.0fk instr, %d KiB Lua, bm3d %s", RNAME[ph.r], s.w, s.h,
    Quality.names[ph.q + 1], s.fps, s.ms, s.p99, s.worst, s.over, s.upd, s.d3, s.tri, s.trimax, s.vtx, s.px,
    s.ins / 1000, s.kb, Bench.ver[ph.r] or "?"))
  if s.ok and not Bench.found then Bench.found = s end      -- (the calibration stops here)
  Bench.next()
end

-- ---------------------------------------------------------------- frames

function Bench.update()
  local c = Input.cmd
  if Bench.done then
    local np = Bench.npages()
    if c.left_p then Bench.page = (Bench.page - 2) % np + 1 Snd.play("ui") end
    if c.right_p or c.down_p then Bench.page = Bench.page % np + 1 Snd.play("ui") end
    if c.menu_p or c.jump_p then Modes.start("menu") end
    return
  end
  if c.menu_p then restore() Modes.start("menu") return end
  if not Bench.ready then Bench.setup() return end
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
      blog(string.format("overbit bench stop %.2f frame %d", Bench.t, Match.state.frame))
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
      blog(string.format("overbit bench ring %s %2d heroes: %.1f ms, 3D %.1f, %.0f tri", RNAME[Bench.ph.r], s.n, s.ms,
        s.d3, s.tri))
      if s.ms > 33.3 then Bench.ring_end() else Bench.ring_grow() end
    end
  end
end

-- what the game should default to: the first step of the ladder that held
-- (the calibration), or of the rows measured the heaviest that held (the matrix)
local function choose()
  if Bench.found and not Bench.matrix then return Bench.found end
  local best
  for _, r in ipairs(Bench.rows) do
    if r.ok and (not best or r.w * r.h > best.w * best.h
        or (r.w * r.h == best.w * best.h and (r.q > best.q or (r.q == best.q and r.ms < best.ms)))) then
      best = r
    end
  end
  if not best and not Bench.matrix then
    -- nothing held: the lightest step measured
    for _, r in ipairs(Bench.rows) do
      if not best or r.ms < best.ms then best = r end
    end
  end
  return best
end

function Bench.finish()
  Bench.done = true
  local opt, saving = Bench.opt, nil
  restore(true)                                 -- (the screen below)
  -- the best quality at 60 fps with each renderer and screen
  Bench.best = {}
  for _, row in ipairs(Bench.rows) do
    local k = row.r .. row.h
    if row.ok and (not Bench.best[k] or row.q > Bench.best[k].q) then Bench.best[k] = row end
  end
  for _, r in ipairs(Bench.renderers) do
    for _, m in ipairs({ 1080, 360 }) do
      local b = Bench.best[r .. m]
      local tried = false
      for _, row in ipairs(Bench.rows) do if row.r == r and row.h == m then tried = true end end
      if b or tried then
        blog(string.format("overbit bench best %s %dp: %s", RNAME[r], m,
          b and Quality.names[b.q + 1] .. " at 60 fps" or "none at 60 fps"))
      end
    end
  end
  local pick = choose()
  Bench.pick = pick
  saving = pick and opt.save ~= 0 and (not Bench.matrix or opt.save == 1) and not OVERBIT_BENCH_FAST
  if not saving then Quality.set_screen(Bench.saved.w, Bench.saved.h) end
  if pick and opt.save ~= 0 and (not Bench.matrix or opt.save == 1) and not OVERBIT_BENCH_FAST then
    Quality.save(pick.w, pick.h, pick.q, Bench.matrix and opt.gpu and pick.r or nil)
    Quality.set_screen(pick.w, pick.h)
    Quality.set(pick.q)
    Bench.saved_default = true
    blog(string.format("overbit bench default %dx%d quality %s%s", pick.w, pick.h, Quality.names[pick.q + 1],
      pick.ok and "" or " (nothing held 60 fps: the lightest)"))
  end
  Bench.elapsed = time() - Bench.began
  blog(string.format("overbit bench done in %.0f s", Bench.elapsed))
  if send_report then send_report("overbit-bench", table.concat(lines, "\n") .. "\n") end
  -- asked with flags: back to where it was started from (the monitor line goes on)
  if Bench.flags_used and opt.stay ~= 1 and not OVERBIT_BENCH_FAST and not OVERBIT_START then quit() end
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
  urectfill(0, LH - 14, LW, 14, INK)
  uprint(text, 4, LH - 13, 0xFFFFFF)
  font()
end

local function q_short(q) return Quality.names[q + 1]:sub(1, 3) end

-- the match's rows a page (the rest on the next ones), and the pages: frame
-- times and work a frame side by side (on a narrow screen, 320x180 or
-- 384x216: one after the other), then the rings and the drivers
local function rows() return (LH - 62) // 11 end
function Bench.npages()
  local k = math.max(1, (#Bench.rows + rows() - 1) // rows())
  return 2 * k + 1, k
end

local function ps(r) return string.format("%-8s %4dp %-3s", RNAME[r.r], r.h, q_short(r.q)) end

local function report()
  cls(0x0A0E14)
  font("6x12")
  local page = Bench.page
  local np, k = Bench.npages()
  local x = uprint("OVERBIT BENCHMARK", 4, 2, 0xFFE070)
  uprint(string.format("%.0f S", Bench.elapsed or 0), x + 12, 2, 0x7A8290)
  local pg = string.format("PAGE %d/%d", page, np)
  uprint(pg, LW - 4 - #pg * 6, 2, 0x7A8290)
  local y = 18
  if page <= 2 * k then
    -- frame time (green: 60 fps), then a page of the work of a frame
    local ROWS = rows()
    local work = page > k
    local first = ((page - 1) % k) * ROWS
    uprint(work and "MATCH OF 10 BOTS: WORK A FRAME" or "MATCH OF 10 BOTS: FRAME TIME", 4, y, 0x7A8290)
    y = y + 13
    uprint(work and "RENDER   SCREEN QUA   LUA    3D   TRI   VTX     PX  KINSTR  KIB"
      or "RENDER   SCREEN QUA  FPS   AVG    1% WORST  >16", 4, y, 0xFFE070)
    for i = first + 1, math.min(first + ROWS, #Bench.rows) do
      local r = Bench.rows[i]
      y = y + 11
      if work then
        uprint(ps(r) .. string.format(" %5.1f %5.1f %5.0f %5.0f %6.0f %7.0f %4d", r.upd, r.d3, r.tri, r.vtx, r.px,
          r.ins / 1000, r.kb), 4, y, 0xD8DCE2)
      else
        uprint(ps(r) .. string.format(" %4.0f %5.1f %5.1f %5.1f %3.0f%%", r.fps, r.ms, r.p99, r.worst, r.over), 4, y,
          r.ok and 0x80FF90 or 0xFF8080)
      end
    end
  else
    if #Bench.rings > 0 then
      uprint("STRESS: HEROES IN A RING (" .. Quality.names[RING_Q + 1] .. ")", 4, y, 0x7A8290)
      for _, g in ipairs(Bench.rings) do
        y = y + 13
        uprint(string.format("%-8s %2d HEROES AT 60 FPS, %2d AT 30", RNAME[g.r], g.best60, g.best30), 4, y, 0xD8DCE2)
      end
      y = y + 18
    end
    local p = Bench.pick
    if p and Bench.saved_default then
      uprint("DEFAULT OF THE GAME, SAVED", 4, y, 0x7A8290)
      y = y + 13
      uprint(string.format("%dx%d  QUALITY %s  (%.0f FPS, %.1f MS)", p.w, p.h, Quality.names[p.q + 1], p.fps, p.ms), 4, y,
        p.ok and 0x80FF90 or 0xFF8080)
      y = y + 18
    end
    uprint("BEST QUALITY AT 60 FPS IN THE MATCH", 4, y, 0x7A8290)
    for _, r in ipairs(Bench.renderers) do
      for _, m in ipairs({ 1080, 360 }) do
        local b = Bench.best[r .. m]
        local tried = false
        for _, row in ipairs(Bench.rows) do if row.r == r and row.h == m then tried = true end end
        if b or tried then
          y = y + 13
          uprint(string.format("%-8s %4dp %s", RNAME[r], m, b and Quality.names[b.q + 1] or "NONE"), 4, y,
            b and 0x80FF90 or 0xFF8080)
        end
      end
    end
    -- the drivers: the bm3d version each renderer reproduces
    local line = "BM3D"
    y = y + 18
    for _, r in ipairs(Bench.renderers) do
      local d = RNAME[r] .. " " .. (Bench.ver[r] or "?")
      if #line + 2 + #d > (LW - 8) // 6 then      -- the characters on the page
        uprint(line, 4, y, 0x7A8290)
        line, y = "    ", y + 12
      end
      line = line .. (#line > 4 and "  " or " ") .. d
    end
    uprint(line, 4, y, 0x7A8290)
  end
  local px = uprompt(Input.cmd.pad and "LEFT" or "left", 4, LH - 15, true)
  px = uprompt(Input.cmd.pad and "RIGHT" or "right", px + 2, LH - 15, true)
  uprint("PAGE", px + 3, LH - 15, 0x7A8290)
  px = uprompt(Input.cmd.pad and "A" or "space", px + 36, LH - 15, true)
  uprint("MENU", px + 3, LH - 15, 0x7A8290)
  font()
end

function Bench.draw()
  if Bench.done then report() return end
  local ph = Bench.ph
  if not ph then cls(0x0A0E14) return end
  local where = string.format("%d/%d %s %dp %s", Bench.i, #Bench.phases, RNAME[ph.r], SH, Quality.names[ph.q + 1])
  if ph.kind == "match" then
    if Bench.warm > 0 then
      cls(0x0A0E14)
      font("6x12")
      uprint("OVERBIT BENCHMARK", 4, 2, 0xFFE070)
      uprint("THE BOTS PLAY UP TO THE FIGHT...", 4, LH // 2 - 10, 0xD8DCE2)
      local done = 1 - Bench.warm / (WARM * 60)
      urectfill(4, LH // 2 + 6, LW - 8, 6, 0x2A2E36)
      urectfill(4, LH // 2 + 6, max(1, ((LW - 8) * done) // 1), 6, 0xF26A21)
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
