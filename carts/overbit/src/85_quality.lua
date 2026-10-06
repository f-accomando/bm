-- Quality: five levels and two screens, set by the game itself. The governor
-- watches the frame time on the console (stat(1): _update + _draw) and keeps
-- 60 fps: one level down when the frames run long, one up when there is room.
-- There is no graphics menu (the title has none): what the governor settles
-- on, and what the benchmark finds (BENCHMARK, or the flags of 84_bench), is
-- saved with save() and is the default of the next start.
--
--  level  models (detail near)  Gouraud  shadows        effects
--  4      3, far LOD late       yes      planar, 30 m   all
--  3      3                     yes      planar, 30 m   all
--  2      3 near, 2             yes      none           3/4
--  1      2                     no       none           1/2
--  0      1                     no       none           1/4
--
-- The screens: 1920x1080 (the GPU draws it) and 640x360 (the most the ARM
-- draws; also what the GPU falls back to). With the GPU (stat(9)) the
-- pixels cost the ARM nothing: Gouraud from level 1 and the planar shadows
-- from level 2 (to 40 m), only their vertices and triangles count.
--
-- The ladder, from the heaviest: 1080p at levels 4..1, then 640x360 at
-- levels 4..0 (0 only there). A step down keeps the screen while a level is
-- left, then changes it; up only changes the level (the screen is for the
-- benchmark: a game must not flicker between two modes).

Quality = { names = { "LOW", "MEDIUM", "HIGH", "ULTRA", "EXTREME" } }

local hist, nh = {}, 0
local since = 0             -- seconds since the last change
local over = 0              -- seconds the frames have run long
local BUDGET = 16.0         -- ms: above this the frame misses 60 fps
local ROOM = 11.5           -- ms: below this for a while, try a level up
local KEEP = 20.0           -- seconds a setting holds before it becomes the default
local prefs = {}            -- what was saved: res "WxH", q, r (the renderer's name)

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

-- the screens this console offers, the smallest first: 640x360 and 1080p
-- (the ARM: only up to 640x360). A console with other modes only (the
-- RGB30's square ones) gets its own, by the same rule.
function Quality.modes()
  local all, list = {}, {}
  if not screen then return list end
  for i = 1, 16 do
    local w, h = screen(i)
    if not w then break end
    if G.gpu or w * h <= 640 * 360 then all[#all + 1] = { w, h } end
  end
  for _, m in ipairs(all) do
    if (m[1] == 640 and m[2] == 360) or (m[1] == 1920 and m[2] == 1080) then list[#list + 1] = m end
  end
  if #list == 0 then
    table.sort(all, function(a, b) return a[1] * a[2] < b[1] * b[2] end)
    for i = max(1, #all - 1), #all do list[#list + 1] = all[i] end
  end
  table.sort(list, function(a, b) return a[1] * a[2] < b[1] * b[2] end)
  return list
end

-- the heaviest first: { w, h, q }
function Quality.ladder()
  local modes, out = Quality.modes(), {}
  for i = #modes, 1, -1 do
    for q = 4, (i == 1) and 0 or 1, -1 do out[#out + 1] = { modes[i][1], modes[i][2], q } end
  end
  return out
end

function Quality.set_screen(w, h)
  if SCREEN_W == w and SCREEN_H == h then return true end
  return screen and screen(w, h)
end

-- ---------------------------------------------------------------- the saved default

local function parse_res(r)
  local w, h = tostring(r or ""):match("^(%d+)x(%d+)$")
  if w then return tonumber(w), tonumber(h) end
end

function Quality.load()
  local t = saved()
  prefs = {}
  if type(t) ~= "table" then return end
  prefs.res = type(t.res) == "string" and t.res or nil
  prefs.q = type(t.q) == "number" and clamp(t.q // 1, 0, 4) or nil
  prefs.r = type(t.r) == "string" and t.r or nil
  if prefs.q then G.quality = prefs.q end
end

function Quality.save(w, h, q, r)
  local t = saved() or {}                 -- (with the relay, 83_net)
  t.res, t.q = w .. "x" .. h, q
  if r then t.r = r end
  save(t)
  prefs.res, prefs.q, prefs.r = t.res, q, r or prefs.r
  log(string.format("overbit default %dx%d quality %d%s", w, h, q, prefs.r and (" " .. prefs.r) or ""))
end

-- at the start of the game, once the GPU is up (stat(9)): the saved screen
-- if this console offers it, else the best one; the saved renderer
function Quality.start()
  local modes = Quality.modes()
  if #modes == 0 then return end
  local w, h = parse_res(prefs.res)
  local pick = modes[#modes]
  for _, m in ipairs(modes) do if m[1] == w and m[2] == h then pick = m end end
  if OVERBIT_RES then
    local ow, oh = parse_res(OVERBIT_RES)
    for _, m in ipairs(modes) do if m[1] == ow and m[2] == oh then pick = m end end
  end
  if Quality.set_screen(pick[1], pick[2]) then log(string.format("overbit screen %dx%d", pick[1], pick[2])) end
  if prefs.r and Modes.list.bench.use_renderer then Modes.list.bench.use_renderer(prefs.r) end
end

-- ---------------------------------------------------------------- the governor

local function step_down()
  local floor_q = (SW * SH <= 640 * 360) and 0 or 1
  if G.quality > floor_q then Quality.set(G.quality - 1) return true end
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
  over = avg > BUDGET and over + DT or 0
  local playing = G.mode == "match" or G.mode == "range" or G.mode == "explore"
  if avg > BUDGET and since > 1.0 then
    -- the screen only after 3 s of long frames: a dip is not a reason
    local floor_q = (SW * SH <= 640 * 360) and 0 or 1
    if G.quality > floor_q or over > 3.0 then
      if step_down() then log(string.format("overbit quality down to %dx%d %d (%.1f ms)", SW, SH, G.quality, avg)) end
    end
  elseif avg < ROOM and worst < BUDGET and since > 4.0 and G.quality < 4 then
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
