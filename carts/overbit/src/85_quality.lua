-- Quality: five levels, two screens and two renderers. The screen and the
-- renderer are the player's (the title's RESOLUTION and RENDERER) or the
-- benchmark's (BENCHMARK, or the flags of 84_bench); the level is the
-- governor's: it watches the frame time on the console (stat(1): _update +
-- _draw) and keeps the frame rate the default was made for (60 fps, or 30
-- when the benchmark found nothing that holds 60): one level down when the
-- frames run long, one up when there is room. What it settles on is saved
-- with save() and is the default of the next start.
--
--  level  models (detail near)  Gouraud  shadows        effects
--  4      3, far LOD late       yes      planar, 30 m   all
--  3      3                     yes      planar, 30 m   all
--  2      3 near, 2             yes      none           3/4
--  1      2                     no       none           1/2
--  0      1                     no       none           1/4
--
-- The screens, the console's own (screen(i), the runtime's list):
--   a TV (the Pi)                640x360 and 1920x1080: the GPU draws both,
--                                the ARM only 640x360 (its pixels cost it);
--                                the .bm (OVERBIT_TV_BM, set by the Makefile):
--                                320x180 on the ARM, 1280x720 and 1920x1080
--                                on the GPU;
--   a square panel (the RGB30)   360x360 (shown twice as big) and 720x720,
--                                both for the ARM too (the Cortex-A55; no GPU
--                                driver there yet).
-- With the GPU (stat(9)) the pixels cost the ARM nothing: Gouraud from level
-- 1 and the planar shadows from level 2 (to 40 m), only their vertices and
-- triangles count.
--
-- The renderers: the ARM, or the GPU as the console's Settings start it
-- (anti-aliasing, vertex shader, frame queue: its name in 84_bench). The
-- .b16 is 360x360 in its header: on a TV that square page is left for a
-- whole 16:9 one at the start (the GPU draws only whole pages).
--
-- The scale of the calibration, from the lightest: the smallest screen at
-- levels 0..4, then the other at levels 1..4. The governor moves only the
-- level once the screen was chosen (the menu, the benchmark); before that a
-- step down past level 1 changes the screen too.

Quality = { names = { "LOW", "MEDIUM", "HIGH", "ULTRA", "EXTREME" }, fps = 60 }

local hist, nh = {}, 0
local since = 0             -- seconds since the last change
local over = 0              -- seconds the frames have run long
local KEEP = 20.0           -- seconds a setting holds before it becomes the default
local prefs = {}            -- what was saved: res "WxH", q, r (the renderer's name), fps, fixed
local leave_t = 0           -- frames waited for the whole 16:9 page (a .b16 on a TV)

-- the frame the governor keeps (ms), and below which it tries a level up
local function budget() return Quality.fps == 30 and 32.0 or 16.0 end
local function room() return Quality.fps == 30 and 23.0 or 11.5 end

function Quality.apply()
  local q = G.quality
  Fx.density = ({ 0.25, 0.5, 0.75, 1, 1 })[q + 1]
  shadow3d(0)
end

function Quality.set(q)
  G.quality = clamp(q, 0, 4)
  Quality.apply()
  hist, nh, since, over = {}, 0, 0, 0
end

-- ---------------------------------------------------------------- screens

-- the ARM draws this screen? On a TV not above 640x360 (the Pi Zero's
-- ARM1176: seconds a frame at 1080p), the .bm not above 320x180; on the
-- square panel both
function Quality.arm_ok(w, h)
  if w == h then return true end
  if OVERBIT_TV_BM then return w * h <= 320 * 180 end
  return w * h <= 640 * 360
end

-- the GPU draws this screen? The .bm on a TV: 1280x720 and up; else all
function Quality.gpu_ok(w, h)
  if w == h or not OVERBIT_TV_BM then return true end
  return w * h >= 1280 * 720
end

-- the screens of a TV this game offers (the .bm has three)
local function tv_mode(w, h)
  if OVERBIT_TV_BM then
    return (w == 320 and h == 180) or (w == 1280 and h == 720) or (w == 1920 and h == 1080)
  end
  return (w == 640 and h == 360) or (w == 1920 and h == 1080)
end

-- the console's screen is square (the RGB30's panel: screen() offers only
-- the square modes)
function Quality.square()
  local w, h
  if screen then w, h = screen(1) end
  return w ~= nil and w == h
end

-- the screens this console offers, the smallest first, for renderer r
-- ("arm", a GPU's name; nil: the one drawing now; "all": whoever draws):
-- 640x360 and 1080p on a TV (the .bm: 320x180, 720p and 1080p), 360x360 and
-- 720x720 on a square panel. A
-- console with other modes only gets its two largest, by the same rule.
function Quality.modes(r)
  local all, list = {}, {}
  if not screen then return list end
  local arm = r == "arm" or (r == nil and not G.gpu)
  local gpu = not arm and r ~= "all"
  for i = 1, 16 do
    local w, h = screen(i)
    if not w then break end
    if (not arm or Quality.arm_ok(w, h)) and (not gpu or Quality.gpu_ok(w, h)) then all[#all + 1] = { w, h } end
  end
  for _, m in ipairs(all) do
    local w, h = m[1], m[2]
    if (w == h and (w == 360 or w == 720)) or tv_mode(w, h) then
      list[#list + 1] = m
    end
  end
  if #list == 0 then
    table.sort(all, function(a, b) return a[1] * a[2] < b[1] * b[2] end)
    for i = max(1, #all - 1), #all do list[#list + 1] = all[i] end
  end
  table.sort(list, function(a, b) return a[1] * a[2] < b[1] * b[2] end)
  return list
end

-- the calibration's steps, the lightest first: { w, h, q } (level 0 only on
-- the smallest screen); 84_bench skips those a renderer does not draw
function Quality.scale()
  local out = {}
  for i, m in ipairs(Quality.modes("all")) do
    for q = (i == 1) and 0 or 1, 4 do out[#out + 1] = { m[1], m[2], q } end
  end
  return out
end

function Quality.set_screen(w, h)
  if SCREEN_W == w and SCREEN_H == h then return true end
  return screen and screen(w, h)
end

-- ---------------------------------------------------------------- renderers

-- the renderer drawing now, by name (84_bench: "arm", "gpu", "vs"...)
function Quality.renderer() return Modes.list.bench.current_name() end

-- the GPU of the menu and of the calibration: as the console's Settings
-- started it (Quality.sys_r, taken before the saved renderer is set); the
-- vertex shader for every model when the console started on the ARM
function Quality.gpu_name()
  if Quality.sys_r and Quality.sys_r ~= "arm" then return Quality.sys_r end
  return "vs"
end

-- ---------------------------------------------------------------- the saved default

local function parse_res(r)
  local w, h = tostring(r or ""):match("^(%d+)x(%d+)$")
  if w then return tonumber(w), tonumber(h) end
end

function Quality.load()
  local t = saved()
  prefs = {}
  Quality.fps = 60
  if type(t) ~= "table" then return end
  prefs.res = type(t.res) == "string" and t.res or nil
  prefs.q = type(t.q) == "number" and clamp(t.q // 1, 0, 4) or nil
  prefs.r = type(t.r) == "string" and t.r or nil
  prefs.fps = t.fps == 30 and 30 or nil
  prefs.fixed = t.fixed == true or nil
  if prefs.q then G.quality = prefs.q end
  Quality.fps = prefs.fps or 60
end

-- the default of the next start: screen, level and (if given) the renderer,
-- the frame rate it was made for (60 or 30) and whether the screen was
-- chosen (the governor then keeps it); what is not given stays as saved
function Quality.save(w, h, q, r, fps, fixed)
  local t = saved() or {}                 -- (with the relay, 83_net)
  t.res, t.q = w .. "x" .. h, q
  if r then t.r = r end
  if fps then t.fps = fps end
  if fixed ~= nil then t.fixed = fixed or nil end
  save(t)
  prefs.res, prefs.q, prefs.r, prefs.fps, prefs.fixed = t.res, q, t.r, t.fps, t.fixed
  Quality.fps = t.fps == 30 and 30 or 60
  log(string.format("overbit default %dx%d quality %d%s%s", w, h, q, prefs.r and (" " .. prefs.r) or "",
    Quality.fps == 30 and " 30 fps" or ""))
end

-- the menu (RESOLUTION, RENDERER): the screen w x h and the renderer r (nil:
-- the one drawing) the player chose, set and saved; the ARM on a TV goes to
-- 640x360 (the .bm: 320x180, and its GPU to 1080p from there). Returns what
-- was set (the GPU may not start: the ARM).
function Quality.choose(w, h, r)
  local B = Modes.list.bench
  if r then B.use_renderer(r) end
  r = B.current_name()
  if (r == "arm" and not Quality.arm_ok(w, h)) or (r ~= "arm" and not Quality.gpu_ok(w, h)) then
    local m = Quality.modes(r)
    if #m > 0 then w, h = m[#m][1], m[#m][2] end
  end
  Quality.set_screen(w, h)
  Quality.save(w, h, G.quality, r, nil, true)
  return w, h, r
end

-- at the start of the game, once the GPU is up (stat(9)): the saved renderer
-- and screen if this console offers it, else the best one. false: a frame
-- more (a .b16's square page on a TV goes to 640x360 first, where the GPU
-- can start as the console's Settings say)
function Quality.start()
  local B = Modes.list.bench
  local modes = Quality.modes("all")
  if #modes == 0 then return true end
  if SW == SH and not Quality.square() and leave_t < 30 then
    local m = Quality.modes("arm")[1] or modes[1]
    if leave_t == 0 then Quality.set_screen(m[1], m[2]) end
    leave_t = leave_t + 1                       -- (a console that cannot: after half a second, as it is)
    return false
  end
  Quality.sys_r = Quality.sys_r or B.current_name()
  Quality.has_gpu = Quality.sys_r ~= "arm" or B.available(Quality.gpu_name())
  if prefs.r then B.use_renderer(prefs.r) end
  local r = B.current_name()
  modes = Quality.modes(r)
  local w, h = parse_res(prefs.res)
  local pick = modes[#modes]
  for _, m in ipairs(modes) do if m[1] == w and m[2] == h then pick = m end end
  if OVERBIT_RES then
    local ow, oh = parse_res(OVERBIT_RES)
    for _, m in ipairs(modes) do if m[1] == ow and m[2] == oh then pick = m end end
  end
  if Quality.set_screen(pick[1], pick[2]) then
    log(string.format("overbit screen %dx%d %s", pick[1], pick[2], r))
  end
  return true
end

-- ---------------------------------------------------------------- the governor

local function step_down()
  local floor_q = (SW * SH <= 640 * 360) and 0 or 1
  if G.quality > floor_q then Quality.set(G.quality - 1) return true end
  if prefs.fixed then return false end          -- the screen chosen: only the level moves
  local modes = Quality.modes()
  for i = #modes, 2, -1 do
    if modes[i][1] == SCREEN_W and modes[i][2] == SCREEN_H then
      Quality.set_screen(modes[i - 1][1], modes[i - 1][2])
      Quality.set(3)
      return true
    end
  end
  return false
end

-- every frame, with the cost of the last one
function Quality.update()
  since = since + DT
  local ms = stat(1)
  nh = nh + 1
  hist[(nh - 1) % 30 + 1] = ms
  if not G.qauto or nh < 30 then return end
  local sum, worst = 0, 0
  for i = 1, 30 do
    sum = sum + hist[i]
    if hist[i] > worst then worst = hist[i] end
  end
  local avg = sum / 30
  local B = budget()
  over = avg > B and over + DT or 0
  local playing = G.mode == "match" or G.mode == "range" or G.mode == "explore"
  if avg > B and since > 1.0 then
    -- the screen only after 3 s of long frames: a dip is not a reason
    local floor_q = (SW * SH <= 640 * 360) and 0 or 1
    if G.quality > floor_q or over > 3.0 then
      if step_down() then log(string.format("overbit quality down to %dx%d %d (%.1f ms)", SW, SH, G.quality, avg)) end
    end
  elseif avg < room() and worst < B and since > 4.0 and G.quality < 4 then
    Quality.set(G.quality + 1)
    log(string.format("overbit quality up to %d (%.1f ms)", G.quality, avg))
  end
  -- what holds for 20 s of play becomes the default
  if playing and since >= KEEP then
    local res = SCREEN_W .. "x" .. SCREEN_H
    if prefs.res ~= res or prefs.q ~= G.quality then Quality.save(SCREEN_W, SCREEN_H, G.quality) end
  end
end

function Quality.avg()
  local n = min(nh, 30)
  if n == 0 then return 0 end
  local s = 0
  for i = 1, n do s = s + hist[i] end
  return s / n
end
