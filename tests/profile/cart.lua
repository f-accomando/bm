-- R14: the profiler of functions, checked by the game itself (bmhost with
-- --clock-scale 1: the PC's real time). heavy() is many times light(),
-- outer() calls them both, draw_lots() calls spr() 400 times a frame.
local frame, passed, total = 0, 0, 0

local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("profile: FAIL " .. what) end
end

local function heavy()
  local s = 0
  for i = 1, 40000 do s = s + i % 7 end
  return s
end

local function light()
  local s = 0
  for i = 1, 4000 do s = s + i % 7 end
  return s
end

local function outer() return heavy() + light() end

function draw_lots()
  for i = 1, 400 do spr(1, i % 300, 20) end
end

local function find(rows, name)
  for _, r in ipairs(rows) do if r.name == name then return r end end
end

function _init()
  local rows, frames = profile(true)
  check(#rows == 0 and frames == 0, "nothing before the first second")
end

function _update()
  frame = frame + 1
  outer()
end

function _draw()
  cls(0)
  draw_lots()
  if frame == 125 then
    local rows, frames = profile()
    check(frames == 60, "a window of 60 frames: " .. tostring(frames))
    for i = 1, math.min(#rows, 8) do
      local r = rows[i]
      log(string.format("profile: %-12s %-16s self %.3f total %.3f calls %d", r.name, r.where, r.self, r.total, r.calls))
    end
    local h, l, o, s, d, u = find(rows, "heavy"), find(rows, "light"), find(rows, "outer"),
                             find(rows, "spr"), find(rows, "draw_lots"), find(rows, "_update")
    check(h and l and o and s and d and u, "the rows of heavy, light, outer, spr, draw_lots, _update")
    if h and l and o and s and d and u then
      check(h.self > 4 * l.self, "heavy costs more than light")
      check(not h.c and h.where:match("^main.lua:%d+$") and h.calls == 0, "heavy: Lua, main.lua:line")
      check(o.total >= 0.9 * (h.total + l.total) and o.self < h.self / 3,
            "outer: its total holds heavy and light, its own time is small")
      check(u.total >= o.total * 0.95, "_update: its total holds outer")
      check(s.c and s.where == "[C]" and s.calls == 400, "spr: a C function, 400 calls a frame: " .. s.calls)
      check(d.total >= s.total * 0.95 and d.self < d.total, "draw_lots: its total holds spr")
      check(rows[1].self >= rows[#rows].self, "the costliest first")
      check(h.total >= h.self and s.total >= s.self, "total >= self")
    end
  elseif frame == 130 then
    profile(false)
    local rows, frames = profile()
    check(#rows == 0 and frames == 0, "profile(false): stopped, forgotten")
    log("profile: " .. passed .. "/" .. total .. " checks passed")
    quit()
  end
end
