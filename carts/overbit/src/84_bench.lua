-- Benchmark (dev kit): how far the console goes, and what the game should
-- default to. At most about one minute. Every phase plays the same match (the
-- same seed: the quality and the renderer never change the game), measured
-- half in a bot's eyes, half from above the point. The report at the end, in
-- pages, in the log and sent (report()).
--
-- BENCHMARK in the title: the calibration (2026-10-10, the user's choice).
-- It climbs the scale of 85_quality from the lightest step (the smallest
-- screen at level 0) to the heaviest (on a TV 640x360 at levels 0..4, then
-- 1080p at 1..4; the .bm 320x180 on the ARM, 720p and 1080p on the GPU; on
-- the RGB30's square panel 360x360, then 720x720), each
-- step with the ARM and with the GPU (as the console's Settings start it;
-- if there is one, and the ARM only where it draws: 85_quality.arm_ok). A
-- renderer stops at its first step under 30 fps; the climb ends when both
-- have stopped, at the top of the scale, or before a phase that would end
-- past about 60 s. Its phases start with the fight on the point (the two
-- teams face to face, a second of play not drawn), the heaviest moment of a
-- match, so that a phase is short. Then the default of the game, saved
-- with Quality.save (screen, level, renderer, the frame rate it is for):
-- the heaviest step that held 60 fps (the target); if none did, the
-- heaviest that held 30 (the fallback, and the governor keeps 30); between
-- the two renderers on the same step the faster.
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
--   res:1080+640              the screens: 1080, 640 (640x360), 360 (640x360 on a TV,
--                             360x360 on a square panel), 720 (720x720 there), or WxH
--   gpu:vs+gpu+arm            the renderers: arm gpu aa gq vs1 vs q (this GPU's only)
--   q:4+3+2                   the qualities, 0..4
--   secs:4                    the seconds measured in each phase (default 3)
--   warm:10                   the seconds of match before the measure (default 10 for
--                             res, gpu or q; 1 for the calibration, after the face to face)
--   ring:1                    after it, the heroes in a ring (default 0)
--   save:1                    the best of those that hold 60 fps becomes the default
--                             (the calibration does it by itself; save:0 stops it)
--   stay:1                    do not leave to the title when done
-- With res, gpu or q given the phases are all their combinations (the renderer
-- the console starts with, and every quality, where one is missing), the match
-- played up to the fight first as before.

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
local PASS30_MS = 32.0                           -- "holds 30 fps": the mean, and the same share over 33.4 ms
local TIME_LIMIT = 60                            -- seconds: no phase of the calibration ends after
-- seconds of match before the measure, seconds measured in each phase, the
-- ring's steps and their seconds; shorter for the tests (OVERBIT_BENCH_FAST,
-- set after the code: read at the start)
local WARM, MEASURE, RING, RING_LEN

-- For counting instructions on the PC (tests/overbit/frames.py): only one
-- phase (OVERBIT_BENCH_ONE = "gpu:2"), the fight at once (OVERBIT_BENCH_HOT:
-- the two teams face to face at the point, no warming up; a number: the
-- seconds of the measure, 4 by default), and a stop after this many seconds
-- of the measure (OVERBIT_BENCH_STOP). For the calibration's tests
-- (tests/overbit/run.py): OVERBIT_BENCH_FAKE(r, w, h, q) gives the frame's
-- milliseconds in place of stat(1).
local function config(opt, matrix)
  local fast = OVERBIT_BENCH_FAST
  if matrix then WARM = fast and 8 or opt.warm or 10 else WARM = opt.warm or 1 end
  MEASURE = fast and 2 or opt.secs or 3
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

-- "1080" -> 1920x1080; "640" -> 640x360; "360": 640x360 on a TV, 360x360 on
-- a square panel; "720": 720x720 there, 1280x720 on a TV; "WxH" as it is
local function res_of(w)
  if w == "1080" or w == "1080p" then return 1920, 1080 end
  if w == "640" then return 640, 360 end
  if w == "360" or w == "360p" then
    if Quality.square() then return 360, 360 end
    return 640, 360
  end
  if w == "720" or w == "720p" then
    if Quality.square() then return 720, 720 end
    return 1280, 720
  end
  local a, b = w:match("^(%d+)x(%d+)$")
  if a then return tonumber(a), tonumber(b) end
end

-- ---------------------------------------------------------------- renderers

-- the renderer drawing now, by name
local function current_name()
  local on, aa, vs, _, q = gpu3d()
  if not on then return "arm" end
  if aa then return "aa" end
  if vs == 1 then return "vs1" end
  if vs and vs >= 2 then return q and "q" or "vs" end
  return q and "gq" or "gpu"
end
Bench.current_name = current_name

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
Bench.available = available

local function use_renderer(r)
  if not RNAME[r] then return end
  gpu3d(r ~= "arm", r == "aa", VS[r] or 0, r == "q" or r == "gq")
end
Bench.use_renderer = use_renderer

-- ---------------------------------------------------------------- phases

-- the matrix's phases: every combination of the flags
local function plan(opt)
  local cur = current_name()
  local phases = {}
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
        -- the ARM does not draw above 640x360 on a TV (seconds a frame at 1080p;
        -- the .bm: 320x180, and its GPU from 720p)
        if (r ~= "arm" or Quality.arm_ok(m[1], m[2])) and (r == "arm" or Quality.gpu_ok(m[1], m[2])) then
          phases[#phases + 1] = { kind = "match", r = r, w = m[1], h = m[2], q = q }
        end
      end
    end
  end
  return phases
end

-- the calibration: the scale, the lightest first, and its renderers (the
-- GPU first, if it starts; then the ARM)
local function climb_plan()
  local ladder = Quality.scale()
  if OVERBIT_BENCH_FAST then                     -- the tests: three steps of the scale
    local few = {}
    for i = 1, min(3, #ladder) do few[i] = ladder[i] end
    ladder = few
  end
  local rs = {}
  local g = Quality.gpu_name()
  if available(g) then rs[1] = g end
  rs[#rs + 1] = "arm"
  return { ladder = ladder, rs = rs, step = 1, ri = 0, out = {} }
end

-- the calibration's next phase going up, or nil: the end of the scale,
-- every renderer under 30 fps, or no time for one more (the last one's length)
local function climb_next(now)
  local C = Bench.climb
  if Bench.i > 0 and now - Bench.began + (Bench.last_len or 0) > TIME_LIMIT then
    C.why = string.format("time (%.0f s)", now - Bench.began)
    return nil
  end
  while C.step <= #C.ladder do
    local e = C.ladder[C.step]
    while C.ri < #C.rs do
      C.ri = C.ri + 1
      local r = C.rs[C.ri]
      if not C.out[r] and (r ~= "arm" or Quality.arm_ok(e[1], e[2]))
         and (r == "arm" or Quality.gpu_ok(e[1], e[2])) then
        return { kind = "match", r = r, w = e[1], h = e[2], q = e[3], step = C.step }
      end
    end
    C.step, C.ri = C.step + 1, 0
  end
  local left = false
  for _, r in ipairs(C.rs) do if not C.out[r] then left = true end end
  C.why = left and "the end of the scale" or "every renderer under 30 fps"
  return nil
end

function Bench.start()
  local opt = parse_flags(Bench.flags)
  Bench.flags_used = Bench.flags
  Bench.flags = nil
  Bench.opt = opt
  Bench.matrix = (opt.res or opt.gpu or opt.q or OVERBIT_BENCH_ONE) and true or false
  config(opt, Bench.matrix)
  Bench.hot = OVERBIT_BENCH_HOT or not Bench.matrix     -- the fight at once
  Bench.saved = { q = G.quality, qauto = G.qauto, diff = G.bot_diff, rules = {}, w = SCREEN_W, h = SCREEN_H }
  local on0, aa0, vs0, _, q0 = gpu3d()
  Bench.saved.on, Bench.saved.aa, Bench.saved.vs, Bench.saved.queue = on0, aa0, vs0, q0
  Bench.ready, Bench.left = false, false
  Bench.rows, Bench.rings, Bench.ver = {}, {}, {}
  Bench.i, Bench.done, Bench.page = 0, false, 1
  Bench.phases, Bench.renderers, Bench.climb = {}, {}, nil
  Bench.ph, Bench.ph_t0, Bench.last_len = nil, nil, nil
  Bench.pick, Bench.pick_fps, Bench.saved_default = nil, nil, false
  G.qauto = false
  G.bot_diff = 2
  for k, v in pairs(RULES) do Bench.saved.rules[k] = Match.rules[k] Match.rules[k] = v end
  lines = {}
  Bench.began = time()
end

-- the first frame: the GPU is up (stat(9)), the console's modes are known
function Bench.setup()
  -- a .b16's square page on a TV: a whole 16:9 one first (the GPU draws only those)
  if SW == SH and not Quality.square() and not Bench.left then
    Bench.left = true
    local m = Quality.modes("arm")[1]
    if m and Quality.set_screen(m[1], m[2]) then return end
  end
  Bench.ready = true
  Quality.sys_r = Quality.sys_r or current_name()   -- the console's renderer, if the game did not start yet
  local opt = Bench.opt
  if Bench.matrix then
    Bench.phases = plan(opt)
    local seen = {}
    for _, ph in ipairs(Bench.phases) do
      if not seen[ph.r] then seen[ph.r] = true Bench.renderers[#Bench.renderers + 1] = ph.r end
    end
    if opt.ring == 1 then
      for _, r in ipairs(Bench.renderers) do Bench.phases[#Bench.phases + 1] = { kind = "ring", r = r, q = RING_Q } end
    end
    if OVERBIT_BENCH_ONE then
      local r, q = OVERBIT_BENCH_ONE:match("(%w+):(%d)")
      Bench.renderers = { r }
      Bench.phases = { { kind = "match", r = r, w = SW, h = SH, q = tonumber(q) } }
    end
  else
    Bench.climb = climb_plan()
    Bench.renderers = Bench.climb.rs
  end
  local C = Bench.climb
  blog(string.format("overbit bench start: %s, flags %s, screen %dx%d, %s, renderers %s", Bench.matrix
    and string.format("%d phases (matrix)", #Bench.phases) or string.format("calibration, %d steps", #C.ladder),
    Bench.flags_used or "auto", SW, SH, G.gpu and "GPU" or "ARM", table.concat(Bench.renderers, " ")))
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
  local now = time()
  if Bench.ph_t0 then Bench.last_len = now - Bench.ph_t0 end
  local ph
  if Bench.matrix then ph = Bench.phases[Bench.i + 1] else ph = climb_next(now) end
  if not ph then Bench.finish() return end
  Bench.i = Bench.i + 1
  Bench.ph, Bench.ph_t0 = ph, now
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
    if Bench.hot then
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
  local ph = Bench.ph
  local ms, fps = stat(1), stat(2)
  if OVERBIT_BENCH_FAKE and ph.kind == "match" then
    ms = OVERBIT_BENCH_FAKE(ph.r, ph.w or SW, ph.h or SH, ph.q)
    fps = min(60, 1000 / max(ms, 1))
  end
  Bench.frames[#Bench.frames + 1] = ms
  S.n = S.n + 1
  S.upd = S.upd + (G.update_ms or 0)
  S.d3 = S.d3 + stat(6)
  local tri = stat(4)
  S.tri, S.vtx, S.px, S.fps = S.tri + tri, S.vtx + stat(7), S.px + stat(5), S.fps + fps
  if tri > S.trimax then S.trimax = tri end
  S.ins, S.kb = S.ins + stat(10), max(S.kb, stat(12))     -- Lua instructions of the frame, KiB of Lua at most
end

-- the frames of a phase: average, 1% (the 99th percentile), worst, over
-- 16.7 ms and over 33.4 ms (60 and 30 fps missed)
local function summary()
  local f = Bench.frames
  local n = #f
  local S = Bench.sum
  if n == 0 then
    return { ms = 0, p99 = 0, worst = 0, over = 0, over30 = 0, upd = 0, d3 = 0, tri = 0, vtx = 0, px = 0, fps = 0,
             ins = 0, kb = 0 }
  end
  local sum, over, over30 = 0, 0, 0
  local sorted = {}
  for i, ms in ipairs(f) do
    sum = sum + ms
    if ms > 16.7 then over = over + 1 end
    if ms > 33.4 then over30 = over30 + 1 end
    sorted[i] = ms
  end
  table.sort(sorted)
  local nn = max(1, S.n)
  return { ms = sum / n, p99 = sorted[max(1, math.ceil(n * 0.99))], worst = sorted[n], over = over * 100 / n,
           over30 = over30 * 100 / n, upd = S.upd / nn, d3 = S.d3 / nn, tri = S.tri / nn, trimax = S.trimax,
           vtx = S.vtx / nn, px = S.px / nn, fps = S.fps / nn, ins = S.ins / nn, kb = S.kb }
end

-- where everyone is: the same in every phase (the same match)
local function match_hash()
  local h = 0
  for _, a in ipairs(G.actors) do h = (h * 31 + ((a.x * 100) // 1) + ((a.z * 100) // 1) * 7 + a.hp // 1) % 1000003 end
  return h
end

local function holds(s) return s.ms <= PASS_MS and s.over <= PASS_OVER end
local function holds30(s) return s.ms <= PASS30_MS and s.over30 <= PASS_OVER end

local function match_done()
  local ph = Bench.ph
  local s = summary()
  s.r, s.q, s.w, s.h = ph.r, ph.q, ph.w or SW, ph.h or SH
  s.ok, s.ok30 = holds(s), holds30(s)
  blog(string.format("overbit bench %s %dx%d %s match %d", RNAME[ph.r], s.w, s.h, Quality.names[ph.q + 1], match_hash()))
  Bench.rows[#Bench.rows + 1] = s
  blog(string.format("overbit bench %s %dx%d %s: %.0f fps, %.1f ms (1%% %.1f, worst %.1f, %.0f%% over 16.7), update %.1f, "
    .. "3D %.1f, %.0f tri (max %d), %.0f vtx, %.0f px, %.0fk instr, %d KiB Lua, bm3d %s", RNAME[ph.r], s.w, s.h,
    Quality.names[ph.q + 1], s.fps, s.ms, s.p99, s.worst, s.over, s.upd, s.d3, s.tri, s.trimax, s.vtx, s.px,
    s.ins / 1000, s.kb, Bench.ver[ph.r] or "?"))
  if Bench.climb and not s.ok30 then
    -- this renderer climbs no more
    Bench.climb.out[ph.r] = true
    blog(string.format("overbit bench stop %s at %dx%d %s: under 30 fps (%.1f ms)", RNAME[ph.r], s.w, s.h,
      Quality.names[ph.q + 1], s.ms))
  end
  Bench.next()
end

-- ---------------------------------------------------------------- frames

function Bench.update()
  local c = Input.cmd
  if Bench.done then
    local np = Bench.npages()
    if c.left_p then Bench.page = (Bench.page - 2) % np + 1 Snd.play("ui") end
    if c.right_p or c.down_p then Bench.page = Bench.page % np + 1 Snd.play("ui") end
    if c.menu_p or c.ok_p or c.back_p then Modes.start("menu") end
    return
  end
  if c.menu_p or c.back_p then restore() Modes.start("menu") return end
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

-- a heavier than b: more pixels, then a higher level, then (the same step)
-- the faster
local function heavier(a, b)
  local pa, pb = a.w * a.h, b.w * b.h
  if pa ~= pb then return pa > pb end
  if a.q ~= b.q then return a.q > b.q end
  return a.ms < b.ms
end

-- what the game should default to, and the frame rate it is for: the
-- heaviest row that held 60 fps; the calibration, if none did, the heaviest
-- that held 30, and if not even that the fastest row
local function choose()
  local best, best30, fastest
  for _, r in ipairs(Bench.rows) do
    if r.ok and (not best or heavier(r, best)) then best = r end
    if r.ok30 and (not best30 or heavier(r, best30)) then best30 = r end
    if not fastest or r.ms < fastest.ms then fastest = r end
  end
  if best then return best, 60 end
  if Bench.matrix then return nil end
  return best30 or fastest, 30
end

-- the screens measured, the largest first
local function screens()
  local out, seen = {}, {}
  for _, row in ipairs(Bench.rows) do
    local k = row.w .. "x" .. row.h
    if not seen[k] then seen[k] = true out[#out + 1] = { row.w, row.h } end
  end
  table.sort(out, function(a, b) return a[1] * a[2] > b[1] * b[2] end)
  return out
end

function Bench.finish()
  Bench.done = true
  local opt = Bench.opt
  restore(true)                                 -- (the screen below)
  if Bench.climb then blog("overbit bench climb end: " .. (Bench.climb.why or "?")) end
  -- the best quality at 60 fps with each renderer and screen
  Bench.best = {}
  for _, row in ipairs(Bench.rows) do
    local k = row.r .. " " .. row.w .. "x" .. row.h
    if row.ok and (not Bench.best[k] or row.q > Bench.best[k].q) then Bench.best[k] = row end
  end
  Bench.screens = screens()
  for _, r in ipairs(Bench.renderers) do
    for _, m in ipairs(Bench.screens) do
      local b = Bench.best[r .. " " .. m[1] .. "x" .. m[2]]
      local tried = false
      for _, row in ipairs(Bench.rows) do if row.r == r and row.w == m[1] and row.h == m[2] then tried = true end end
      if b or tried then
        blog(string.format("overbit bench best %s %dx%d: %s", RNAME[r], m[1], m[2],
          b and Quality.names[b.q + 1] .. " at 60 fps" or "none at 60 fps"))
      end
    end
  end
  local pick, fps = choose()
  Bench.pick, Bench.pick_fps = pick, fps
  local saving = pick and opt.save ~= 0 and (not Bench.matrix or opt.save == 1) and not OVERBIT_BENCH_FAST
  if saving then
    -- the renderer too (the matrix: only if the flags chose them)
    local r = (not Bench.matrix or opt.gpu) and pick.r or nil
    if r then use_renderer(r) end
    Quality.save(pick.w, pick.h, pick.q, r, fps, true)
    Quality.set_screen(pick.w, pick.h)
    Quality.set(pick.q)
    Bench.saved_default = true
    blog(string.format("overbit bench default %dx%d %s %s: %s", pick.w, pick.h, Quality.names[pick.q + 1],
      RNAME[pick.r], fps == 60 and "holds 60 fps"
        or pick.ok30 and "holds 30 fps (nothing held 60)" or "the fastest (nothing held 30 fps)"))
  else
    Quality.set_screen(Bench.saved.w, Bench.saved.h)
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

-- green: 60 fps; yellow: 30; red: not even that
local function row_rgb(r) return r.ok and 0x80FF90 or r.ok30 and 0xFFD060 or 0xFF8080 end

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
  local narrow = LW < 380                       -- (360 wide: the KiB of Lua left out)
  if page <= 2 * k then
    -- frame time (green: 60 fps, yellow: 30), then a page of the work of a frame
    local ROWS = rows()
    local work = page > k
    local first = ((page - 1) % k) * ROWS
    uprint(work and "MATCH OF 10 BOTS: WORK A FRAME" or "MATCH OF 10 BOTS: FRAME TIME", 4, y, 0x7A8290)
    y = y + 13
    uprint(work and ("RENDER   SCREEN QUA   LUA    3D   TRI   VTX     PX  KINSTR" .. (narrow and "" or "  KIB"))
      or "RENDER   SCREEN QUA  FPS   AVG    1% WORST  >16", 4, y, 0xFFE070)
    for i = first + 1, math.min(first + ROWS, #Bench.rows) do
      local r = Bench.rows[i]
      y = y + 11
      if work then
        uprint(ps(r) .. string.format(" %5.1f %5.1f %5.0f %5.0f %6.0f %7.0f", r.upd, r.d3, r.tri, r.vtx, r.px,
          r.ins / 1000) .. (narrow and "" or string.format(" %4d", r.kb)), 4, y, 0xD8DCE2)
      else
        uprint(ps(r) .. string.format(" %4.0f %5.1f %5.1f %5.1f %3.0f%%", r.fps, r.ms, r.p99, r.worst, r.over), 4, y,
          row_rgb(r))
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
      uprint(string.format("%dx%d %s %s", p.w, p.h, Quality.names[p.q + 1], RNAME[p.r]), 4, y, row_rgb(p))
      y = y + 13
      uprint(string.format("%.0f FPS, %.1f MS: %s", p.fps, p.ms, Bench.pick_fps == 60 and "60 FPS"
        or "NOTHING AT 60, 30 FPS"), 4, y, row_rgb(p))
      y = y + 18
    end
    if Bench.climb then
      uprint("CLIMB STOPPED: " .. (Bench.climb.why or "?"):upper(), 4, y, 0x7A8290)
      y = y + 18
    end
    uprint("BEST QUALITY AT 60 FPS IN THE MATCH", 4, y, 0x7A8290)
    for _, r in ipairs(Bench.renderers) do
      for _, m in ipairs(Bench.screens) do
        local b = Bench.best[r .. " " .. m[1] .. "x" .. m[2]]
        local tried = false
        for _, row in ipairs(Bench.rows) do if row.r == r and row.w == m[1] and row.h == m[2] then tried = true end end
        if b or tried then
          y = y + 13
          uprint(string.format("%-8s %4dx%-4d %s", RNAME[r], m[1], m[2], b and Quality.names[b.q + 1] or "NONE"), 4, y,
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
  px = uprompt(Input.cmd.pad and "ok" or "space", px + 36, LH - 15, true)
  uprint("MENU", px + 3, LH - 15, 0x7A8290)
  font()
end

function Bench.draw()
  if Bench.done then report() return end
  local ph = Bench.ph
  if not ph then cls(0x0A0E14) return end
  local where = string.format("%d %s %dx%d %s", Bench.i, RNAME[ph.r], SW, SH, Quality.names[ph.q + 1])
  if ph.step and Bench.climb then where = string.format("%s, STEP %d/%d", where, ph.step, #Bench.climb.ladder) end
  if ph.kind == "match" then
    if Bench.warm > 0 then
      cls(0x0A0E14)
      font("6x12")
      uprint("OVERBIT BENCHMARK", 4, 2, 0xFFE070)
      uprint(Bench.hot and "THE FIGHT STARTS..." or "THE BOTS PLAY UP TO THE FIGHT...", 4, LH // 2 - 10, 0xD8DCE2)
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
