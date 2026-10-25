-- One game, many screens: screen_modes(), the _screen(w, h) hook, and bmlib's
-- lib.view, lib.anchor, lib.safe_clip and lib.fit, on every mode of the device
-- (bmhost, and bmhost --square) for a cartridge whose header is the size the
-- game starts at (640x360 or 360x360: the Makefile builds both).
-- Run by `make test-layout`; each run prints "layout: N/N checks passed".

local lib = require "bmlib"

local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("layout: FAIL " .. what) end
end

local DW, DH = screen()             -- the header size: the first mode
local square = (screen(1)) == 360
local hook_n, hook_w, hook_h = 0, nil, nil
local hook_first                    -- the arguments of the first call and the count when _init ran

function _screen(w, h)
  hook_n = hook_n + 1
  hook_w, hook_h = w, h
  if hook_n == 1 then hook_first = { w, h, SCREEN_W, SCREEN_H } end
end

local init_count

-- the safe area as the rule says it (docs/api/draw.md), for the screen w by h
local function expect_area(w, h)
  if DW * h > DH * w then
    local ah = w * DH // DW
    return 0, (h - ah) // 2, w, ah
  elseif DW * h < DH * w then
    local aw = h * DW // DH
    return (w - aw) // 2, 0, aw, h
  end
  return 0, 0, w, h
end

local function same(a, b) return math.abs(a - b) < 1e-9 end

local function view_checks(label)
  local w, h = screen()
  local ex, ey, ew, eh = expect_area(w, h)
  local v = lib.view()
  check(v.x == ex and v.y == ey and v.w == ew and v.h == eh,
        string.format("%s: view is %d,%d,%d,%d (got %s,%s,%s,%s)", label, ex, ey, ew, eh, v.x, v.y, v.w, v.h))
  check(same(v.scale, ew / DW), label .. ": view.scale is " .. ew / DW .. " (got " .. tostring(v.scale) .. ")")
  local _, kind = aspect()
  check(v.wide == (kind == "wide") and v.square == (kind == "square") and v.tall == (kind == "tall"),
        label .. ": view wide/square/tall follow aspect()")
  check((v.wide and 1 or 0) + (v.square and 1 or 0) + (v.tall and 1 or 0) == 1, label .. ": exactly one shape")
  if w == DW and h == DH then
    check(v.scale == 1.0 and v.x == 0 and v.y == 0, label .. ": the design size has scale 1.0")
  end

  -- the nine places, element 40 by 20, margin 4, 6
  local ew2, eh2, mx, my = 40, 20, 4, 6
  local xs = { l = ex + mx, c = ex + (ew - ew2) // 2, r = ex + ew - ew2 - mx }
  local ys = { t = ey + my, c = ey + (eh - eh2) // 2, b = ey + eh - eh2 - my }
  local names = {
    tl = { "l", "t" }, t = { "c", "t" }, tr = { "r", "t" },
    l = { "l", "c" }, c = { "c", "c" }, r = { "r", "c" },
    bl = { "l", "b" }, b = { "c", "b" }, br = { "r", "b" },
  }
  for where, p in pairs(names) do
    local x, y = lib.anchor(where, ew2, eh2, mx, my)
    check(x == xs[p[1]] and y == ys[p[2]],
          string.format("%s: anchor %s is %d,%d (got %s,%s)", label, where, xs[p[1]], ys[p[2]], x, y))
  end
  -- without margins, and no margin on the centred axis
  local x, y = lib.anchor("tl", 10, 10)
  check(x == ex and y == ey, label .. ": anchor tl without margin is the corner")
  x, y = lib.anchor("br", 10, 10)
  check(x == ex + ew - 10 and y == ey + eh - 10, label .. ": anchor br without margin is the far corner")
  x, y = lib.anchor("c", ew, eh, 9, 9)
  check(x == ex and y == ey, label .. ": an element as big as the area sits on it")

  -- fit
  local s, fx, fy = lib.fit(DW, DH)
  check(math.abs(s - ew / DW) < 0.01 and fx == ex and fy == ey, label .. ": fit of the design is the safe area (whole pixels of the area round it)")
  local s2, x2, y2 = lib.fit(DW * 2, DH)          -- twice as wide: bars above and below
  local es = math.min(ew / (DW * 2), eh / DH)
  check(same(s2, es), label .. ": fit scale is the smaller ratio")
  check(x2 == ex + math.floor((ew - DW * 2 * es) / 2) and y2 == ey + math.floor((eh - DH * es) / 2),
        label .. ": fit is in the middle")
  check(x2 >= ex and y2 >= ey and x2 + DW * 2 * es <= ex + ew + 1e-9 and y2 + DH * es <= ey + eh + 1e-9,
        label .. ": fit stays inside the safe area")

  -- the arguments are checked
  check(not pcall(lib.anchor, "x", 1, 1), label .. ": anchor refuses a place that does not exist")
  check(not pcall(lib.anchor, "c", "a", 1), label .. ": anchor refuses a size that is no number")
  check(not pcall(lib.fit, 0, 10), label .. ": fit refuses a size of 0")
end

-- the cases with numbers written out (a bug in expect_area must not hide itself)
local function fixed_cases(w, h)
  if DW == 360 and DH == 360 and w == 640 and h == 360 then
    local v = lib.view()
    check(v.x == 140 and v.y == 0 and v.w == 360 and v.h == 360 and v.scale == 1.0 and v.wide,
          "360 design on 640x360: view 140,0,360,360 scale 1 wide")
    local x, y = lib.anchor("tl", 40, 20, 4, 6); check(x == 144 and y == 6, "360 on 640x360: tl 144,6")
    x, y = lib.anchor("t", 40, 20, 4, 6);        check(x == 300 and y == 6, "360 on 640x360: t 300,6")
    x, y = lib.anchor("tr", 40, 20, 4, 6);       check(x == 456 and y == 6, "360 on 640x360: tr 456,6")
    x, y = lib.anchor("l", 40, 20, 4, 6);        check(x == 144 and y == 170, "360 on 640x360: l 144,170")
    x, y = lib.anchor("c", 40, 20, 4, 6);        check(x == 300 and y == 170, "360 on 640x360: c 300,170")
    x, y = lib.anchor("r", 40, 20, 4, 6);        check(x == 456 and y == 170, "360 on 640x360: r 456,170")
    x, y = lib.anchor("bl", 40, 20, 4, 6);       check(x == 144 and y == 334, "360 on 640x360: bl 144,334")
    x, y = lib.anchor("b", 40, 20, 4, 6);        check(x == 300 and y == 334, "360 on 640x360: b 300,334")
    x, y = lib.anchor("br", 40, 20, 4, 6);       check(x == 456 and y == 334, "360 on 640x360: br 456,334")
    local s, fx, fy = lib.fit(180, 180)
    check(s == 2 and fx == 140 and fy == 0, "360 on 640x360: fit(180, 180) is 2, 140, 0")
    s, fx, fy = lib.fit(640, 360)
    check(same(s, 0.5625) and fx == 140 and fy == 78, "360 on 640x360: fit(640, 360) is 0.5625, 140, 78")
  elseif DW == 640 and DH == 360 and w == 1280 and h == 720 then
    local v = lib.view()
    check(v.x == 0 and v.y == 0 and v.w == 1280 and v.h == 720 and v.scale == 2.0 and v.wide,
          "640x360 design on 1280x720: view 0,0,1280,720 scale 2")
    local x, y = lib.anchor("br", 100, 50, 10, 10)
    check(x == 1170 and y == 660, "640x360 on 1280x720: br 1170,660")
  elseif DW == 640 and DH == 360 and w == 360 and h == 360 then
    local v = lib.view()
    check(v.x == 0 and v.y == 79 and v.w == 360 and v.h == 202 and v.square,
          "640x360 design on 360x360: view 0,79,360,202 square")
  elseif DW == 640 and DH == 360 and w == 720 and h == 405 then
    local v = lib.view()
    check(v.x == 0 and v.y == 0 and v.w == 720 and v.h == 405 and same(v.scale, 1.125),
          "640x360 design on 720x405: whole screen, scale 1.125")
  end
end

-- the clip: the safe area is painted, the rest is not (read in _draw)
local clip_label

local order = {}                    -- the modes, one after the other
local step = 0                      -- the mode asked for last (index into order)
local f = 0
local prev_w, prev_h
local expect_n
local phase = 0

function _init()
  init_count = hook_n
  check(hook_first ~= nil and hook_first[1] == DW and hook_first[2] == DH,
        "_screen is called at start with the first mode " .. DW .. "x" .. DH)
  check(hook_first and hook_first[3] == DW and hook_first[4] == DH, "SCREEN_W and SCREEN_H are set at the first call")
  check(init_count == 1, "_screen ran once before _init (" .. init_count .. ")")

  local t, cur = screen_modes()
  check(type(t) == "table" and #t == (square and 3 or 7), "screen_modes has " .. (square and 3 or 7) .. " modes")
  for i = 1, #t do
    local w, h = screen(i)
    check(t[i][1] == w and t[i][2] == h, "screen_modes()[" .. i .. "] is screen(" .. i .. ")")
  end
  check(screen(#t + 1) == nil, "screen(n + 1) is nil")
  if square then
    check(t[1][1] == 360 and t[2][1] == 720 and t[3][2] == 405, "square panel: 360x360, 720x720, 720x405")
  else
    check(t[1][1] == 320 and t[4][1] == 640 and t[7][1] == 1920, "TV: 320x180 ... 1920x1080")
  end
  local want
  for i = 1, #t do
    if t[i][1] == DW and t[i][2] == DH then want = i end
  end
  check(cur == want, "the current mode index is " .. tostring(want) .. " (got " .. tostring(cur) .. ")")
  for i = 1, #t do order[#order + 1] = i end
  order[#order + 1] = want or 1       -- back to the start
  prev_w, prev_h = DW, DH
  expect_n = 1
  log(string.format("layout: panel %s, design %dx%d", square and "square" or "16:9", DW, DH))
end

function _update()
  f = f + 1
  local w, h = screen()
  -- the frame after screen(): the hook has run once, with the new size
  if step > 0 then
    local changed = (w ~= prev_w or h ~= prev_h)
    if changed then expect_n = expect_n + 1 end
    check(hook_n == expect_n,
          string.format("mode %d (%dx%d): %d calls of _screen (expected %d)", step, w, h, hook_n, expect_n))
    check(hook_w == w and hook_h == h and SCREEN_W == w and SCREEN_H == h,
          string.format("mode %d: _screen got %sx%s, the screen is %dx%d", step, tostring(hook_w), tostring(hook_h), w, h))
    local t, cur = screen_modes()
    local ok
    for i = 1, #t do
      if t[i][1] == w and t[i][2] == h then ok = (cur == i) end
    end
    check(ok == true or ok == nil and cur == nil, "screen_modes says which mode is current at " .. w .. "x" .. h)
    local label = string.format("%dx%d", w, h)
    view_checks(label)
    fixed_cases(w, h)
    clip_label = label
  end
  prev_w, prev_h = w, h
  if step < #order then
    step = step + 1
    local mw, mh = screen(order[step])
    check(mw ~= nil and screen(mw, mh) == true, "screen(" .. order[step] .. ") is a mode and screen(w, h) takes it")
  elseif phase == 0 then
    phase = 1                       -- the size asked is the one now: no change, no call
    check(screen(w, h) == true, "screen() with the size it has is accepted")
  elseif phase == 1 then
    phase = 2
    check(hook_n == expect_n, "no call of _screen without a change (" .. hook_n .. ", expected " .. expect_n .. ")")
    log(string.format("layout: %d/%d checks passed", checks - fails, checks))
    quit()
  end
end

function _draw()
  cls(0)
  if clip_label then
    local w, h = screen()
    local ex, ey, ew, eh = expect_area(w, h)
    local sx, sy, sw, sh = lib.safe_clip()
    check(sx == ex and sy == ey and sw == ew and sh == eh, clip_label .. ": safe_clip returns the safe area")
    rectfill(0, 0, w, h, 0xFFFFFF)
    clip()
    check(pget(ex, ey) ~= 0 and pget(ex + ew - 1, ey + eh - 1) ~= 0, clip_label .. ": the safe area is painted")
    if ex > 0 then check(pget(ex - 1, ey) == 0 and pget(ex + ew, ey) == 0, clip_label .. ": nothing left or right of it") end
    if ey > 0 then check(pget(ex, ey - 1) == 0 and pget(ex, ey + eh) == 0, clip_label .. ": nothing above or below it") end
    clip_label = nil
  end
  print("layout", 8, 8, 0xFFFFFF)
end
