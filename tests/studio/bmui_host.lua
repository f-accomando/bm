-- bmui (src/script/bmui.lua, the mouse in bm's tools) on the PC: a mouse
-- and a keyboard stand in for the console's, frame by frame. Clicks on
-- zones (the last drawn wins), the double click, the drag and the click
-- without one, the wheel, the keys it presses for keyp(), the context menu
-- with the mouse and with the keys, and nothing at all without a mouse.
--   luahost tests/studio/bmui_host.lua ROOT
local ROOT = arg and arg[1] or "."
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; print("FAIL " .. what) end
end

local M = { x = 0, y = 0, b = 0, w = 0, shown = true, present = true, pressed = 0 }
local keys = {}
local clock = 0
local drawn = {}
local E = setmetatable({}, { __index = _G })
E.mouse = function(on)
  if on ~= nil then return true end
  if not M.present then return nil end
  return M.x, M.y, M.b, M.w, M.shown
end
E.mousep = function(i) return M.pressed >> (i or 0) & 1 == 1 end
E.keyp = function() return table.remove(keys, 1) end
E.time = function() return clock end
E.screen_size = function() return 640, 360 end
E.rectfill = function() end
E.rect = function() end
E.print = function(s, x, y) drawn[#drawn + 1] = { s = s, x = x, y = y } end

local U = assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()

-- one frame: the mouse as given, then the zones drawn
local function frame(x, y, b, w, zones)
  local was = M.b
  M.x, M.y, M.b, M.w = x or M.x, y or M.y, b or 0, w or 0
  M.pressed = M.b & ~was
  clock = clock + 1 / 60
  U.update()
  local r = { click = U.click(0), rclick = U.clicked(1), double = U.double, drag = U.drag and {
    b = U.drag.b, dx = U.drag.dx, dy = U.drag.dy } }
  r.menu = U.menu_update()
  U.begin()
  for _, z in ipairs(zones or {}) do U.zone(table.unpack(z)) end
  drawn = {}
  U.menu_draw()
  return r
end

local Z = { { 0, 0, 168, 320, "panel" }, { 0, 32, 168, 16, "row", 1 }, { 0, 48, 168, 16, "row", 2 },
            { 100, 40, 200, 100, "dialog" } }
frame(10, 10, 0, 0, Z)
check(U.keyp == E.keyp, "keyp is the library's for the tool")
local r = frame(10, 50, 1, 0, Z)
check(r.click and r.click.kind == "row" and r.click.a == 2, "a click on a row: that row")
frame(10, 50, 0, 0, Z)
r = frame(120, 50, 1, 0, Z)
check(r.click and r.click.kind == "dialog", "the last zone drawn wins (a dialog over the panel)")
frame(120, 50, 0, 0, Z)
-- a double click: two presses close in time and place
for _ = 1, 30 do frame(10, 36, 0, 0, Z) end
r = frame(10, 36, 1, 0, Z)
check(r.click and r.click.a == 1 and not r.double, "the first click is not a double")
frame(10, 36, 0, 0, Z)
r = frame(11, 37, 1, 0, Z)
check(r.double, "the second, soon after and near: a double click")
frame(11, 37, 0, 0, Z)
for _ = 1, 40 do frame(11, 37, 0, 0, Z) end
r = frame(11, 37, 1, 0, Z)
check(not r.double, "too late: not a double")
frame(11, 37, 0, 0, Z)
-- the wheel
frame(10, 36, 0, -2, Z)
check(U.wheel == -2, "the wheel's clicks of the frame")
frame(10, 36, 0, 0, Z)
check(U.wheel == 0, "  only that frame")
-- a drag with the right button turns a view; without moving it is a click
frame(300, 200, 2, 0, Z)
r = frame(302, 201, 2, 0, Z)
check(not r.drag, "two pixels: not a drag yet")
r = frame(310, 205, 2, 0, Z)
check(r.drag and r.drag.b == 1 and r.drag.dx == 8 and r.drag.dy == 4, "past three pixels: a drag, by the frame's move")
r = frame(312, 205, 0, 0, Z)
check(not r.rclick and not U.drag, "released after a drag: no right click")
frame(312, 205, 2, 0, Z)
r = frame(312, 205, 0, 0, Z)
check(r.rclick == true, "released where it went down: a right click (over nothing)")
frame(10, 50, 2, 0, Z)
r = frame(10, 50, 0, 0, Z)
check(type(r.rclick) == "table" and r.rclick.a == 2, "  over a row: that row")
-- the context menu: its items are the tool's keys
U.menu({ { "Cut", "^x" }, { "Copy", "^c" }, "-", { "Paste", "^v" }, { "Run", function() keys[#keys + 1] = "ran" end } },
       600, 340)
check(U.menu_open(), "the context menu opens")
r = frame(10, 50, 0, 0, Z)
local labels = {}
for _, d in ipairs(drawn) do labels[#labels + 1] = d.s end
local s = table.concat(labels, " ")
check(s:find("Cut", 1, true) and s:find("Ctrl+X", 1, true) and s:find("Paste", 1, true), "  drawn with the keys: " .. s)
local cut = drawn[1]
check(cut.x - 8 + 120 <= 640 and cut.y + 64 + 4 <= 360 and cut.x % 8 == 0 and cut.y % 16 == 0,
      "  kept on the screen, on the 8x16 grid")
-- clicking its second row presses Ctrl+C for the tool
r = frame(cut.x, cut.y + 16 + 4, 1, 0, Z)
check(r.menu and not U.menu_open(), "a click in the menu closes it")
check(r.click == nil, "  and is not a click on the page")
check(U.keyp() == "^c", "  the tool gets Ctrl+C from keyp()")
check(U.keyp() == nil, "  once")
frame(cut.x, cut.y, 0, 0, Z)
-- with the keys: down, down (over the line), Enter
U.menu({ { "Cut", "^x" }, { "Copy", "^c" }, "-", { "Paste", "^v" } }, 10, 10)
keys = { "down", "down", "down", "\n" }
r = frame(500, 300, 0, 0, Z)
check(r.menu and U.keyp() == "^v", "the keys choose too, over the line: Paste")
-- a function as the action
U.menu({ { "Run", function() keys[#keys + 1] = "ran" end } }, 10, 10)
frame(20, 20, 1, 0, Z)
check(U.keyp() == "ran", "an item with a function calls it")
frame(20, 20, 0, 0, Z)
-- a click outside closes it and does nothing else
U.menu({ { "Cut", "^x" } }, 10, 10)
r = frame(400, 300, 1, 0, Z)
check(not U.menu_open() and r.click == nil and U.keyp() == nil, "a click outside closes it, nothing else")
frame(400, 300, 0, 0, Z)
-- Esc closes it
U.menu({ { "Cut", "^x" } }, 10, 10)
keys = { "esc", "a" }
frame(400, 300, 0, 0, Z)
check(not U.menu_open() and U.keyp() == "a", "Esc closes it, the keys after it go to the tool")
keys = {}
-- pressed keys come before the keyboard's
keys = { "x" }
U.press("f2")
check(U.keyp() == "f2" and U.keyp() == "x", "the mouse's keys first, then the keyboard's")
-- the pointer hidden (a key was typed): no clicks
M.shown = false
r = frame(10, 50, 1, 0, Z)
check(not r.click and not U.on, "hidden pointer: no click")
frame(10, 50, 0, 0, Z)
M.shown = true
-- no mouse at all
M.present = false
r = frame(10, 50, 1, 0, Z)
check(not r.click and not U.on and not U.at(), "no mouse: nothing")
M.present = true

-- the chips' keys
check(U.key_code("ctrl s") == "^s" and U.key_code({ "ctrl", "shift", "s" }) == "^S" and U.key_code("shift l") == "L",
      "chips: Ctrl+S, Ctrl+Shift+S, Shift+L")
check(U.key_code("enter") == "\n" and U.key_code("f5") == "f5" and U.key_code("esc") == "esc" and U.key_code("space") == " ",
      "  Enter, F5, Esc, space")
check(U.key_code({ "up", "down" }) == nil and U.key_code("A") == nil and U.key_code("x") == "x", "  two keys, a pad button: none")
U.begin()
U.key_zone(0, 320, 40, 16, "ctrl g")
M.b = 0
frame(10, 330, 1, 0, { { 0, 320, 40, 16, "key", "^g" } })
check(U.keys() and U.keyp() == "^g", "a click on a chip presses its key")
frame(10, 330, 0, 0)

print(fails == 0 and ("bmui: " .. checks .. "/" .. checks .. " checks passed") or (fails .. " of " .. checks .. " FAILED"))
if fails > 0 then os.exit(1) end
