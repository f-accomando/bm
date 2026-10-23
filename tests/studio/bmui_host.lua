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

-- projects live in /bme (docs/PROJECT-LAYOUT.md): the one helper for their paths and lists
check(U.PROJECTS == "/bme", "projects: the folder is /bme")
check(U.project_path("pong") == "/bme/pong.bme", "project_path: a bare name")
check(U.project_path("/carts/Il mio gioco.bm") == "/bme/ilmiogio.bme", "project_path: a game's path, 8.3")
check(U.project_path("/bme/my-game.bme") == "/bme/my-game.bme" and U.project_path("") == "/bme/game.bme", "project_path: a dash stays; no name: GAME")
check(U.is_project("/bme/a.bme") and not U.is_project("/carts/a.bm") and not U.is_project(nil), "is_project: by the extension")
check(not U.is_old_place("/bme/a.bme") and U.is_old_place("/carts/A.BME") and U.is_old_place("/A.bme") and not U.is_old_place("/carts/a.bm"),
      "is_old_place: a project outside /bme")
check(U.saved_text("/bme/a.bme") == "saved /bme/a.bme", "saved_text: a project in /bme")
check(U.saved_text("/carts/A.BME"):find("saved /carts/A.BME", 1, true) and U.saved_text("/carts/A.BME"):find("old place", 1, true),
      "saved_text: an old project says where it is")
do
  local card = {
    ["/bme"] = { { name = "B.BME", size = 5 }, { name = "x.bm", size = 1 }, { name = "sub", dir = true } },
    ["/carts"] = { { name = "old.bme", size = 7 }, { name = "g.bm", size = 9 }, { name = "g.b16", size = 3 } },
    ["/"] = { { name = "R.BM", size = 2 } },
    ["/bm"] = { { name = "g.b16", size = 3 } },
  }
  E.ls = function(dir) return card[dir] or {} end
  local list, sizes = U.project_files({ bm = true, bme = true })
  check(table.concat(list, " ") == "/bme/B.BME /carts/old.bme /carts/g.bm /R.BM" or table.concat(list, " ") == "/bme/B.BME /carts/old.bme /carts/g.bm /R.BM",
        "project_files: /bme first, then the old places, only .bme in /bme: " .. table.concat(list, " "))
  check(sizes["/bme/B.BME"] == 5 and sizes["/carts/g.bm"] == 9, "project_files: the sizes")
  list = U.project_files({ bm = true, bme = true, b16 = true }, { "/bm" })
  check(#list == 6 and list[#list] == "/bm/g.b16", "project_files: .b16, and an extra folder last: " .. table.concat(list, " "))
  card["/carts"][#card["/carts"] + 1] = { name = "B.BME", size = 1 }
  card["/bme"][#card["/bme"] + 1] = { name = "b.bme", size = 1 }
  list = U.project_files({ bme = true })
  local n = 0
  for _, p in ipairs(list) do if p:upper() == "/BME/B.BME" then n = n + 1 end end
  check(n == 1, "project_files: a path once, case does not matter")
end

-- M48 helpers: U.list, U.col, U.scroll
do
  M.shown = true
  frame(10, 10, 0, 0, Z)
  local LZ = { { 0, 32, 168, 16, "row", 1 }, { 0, 48, 168, 16, "row", 2 }, { 0, 64, 168, 16, "row", 3 } }
  frame(10, 56, 0, 0, LZ)
  frame(10, 56, 1, 0, LZ)
  local sel, act = U.list("row", 1, 3)
  check(sel == 2 and not act, "list: a click chooses the row")
  frame(10, 56, 0, 0, LZ)
  frame(10, 56, 1, 0, LZ)
  sel, act = U.list("row", 2, 3)
  check(sel == 2 and act, "list: a click on the chosen row acts")
  frame(10, 56, 0, 0, LZ)
  frame(10, 40, 1, 0, LZ)
  sel, act = U.list("row", 3, 3)
  check(sel == 1 and not act, "list: a click on another row moves there")
  frame(10, 40, 0, 0, LZ)
  frame(10, 40, 0, -2, LZ)
  sel, act = U.list("row", 1, 3, 1)
  check(sel == 3 and not act, "list: the wheel down moves down, clamped")
  frame(10, 40, 0, 5, LZ)
  sel = U.list("row", 2, 3, 2)
  check(sel == 1, "list: the wheel up moves up, clamped, by the step")
  frame(10, 40, 0, 0, LZ)
  sel, act = U.list("other", 2, 3)
  check(sel == 2 and not act, "list: no click, no change")
  frame(10, 56, 1, 0, LZ)
  sel, act = U.list("other", 2, 3)
  check(sel == 2 and not act, "list: a click on a zone of another kind is not a row")
  frame(10, 56, 0, 0, LZ)
  check(U.col(0, 8, 6, 10) == 0 and U.col(8 + 6 * 3 + 2, 8, 6, 10) == 3 and U.col(8 + 6 * 3 + 4, 8, 6, 10) == 4
        and U.col(900, 8, 6, 10) == 10, "col: the gap nearest to x, clamped")
  frame(10, 40, 0, -2, LZ)
  check(U.scroll(100, 0, 500, 45) == 190 and U.scroll(10, 0, 500, 45) == 100, "scroll: by the step")
  frame(10, 40, 0, 20, LZ)
  check(U.scroll(100, 0, 500, 45) == 0, "scroll: clamped at the top")
  frame(10, 40, 0, 0, LZ)
  check(U.scroll(100, 0, 500, 45) == 100, "scroll: no wheel, no change")
end

-- the tab strip (APP-8): one component for every editor with tabs
do
  local function items(names, on)
    local t = {}
    for i, n in ipairs(names) do t[i] = { label = n, on = i == on } end
    return t
  end
  local function zone_at(x, y)
    M.x, M.y, M.b = x, y, 0
    U.update()
    return U.at()
  end
  U.begin()
  drawn = {}
  local a, b = U.tabbar{ x = 64, y = 0, w = 320, cw = 8, ch = 12, items = items({ "main", "gfx", "ui" }, 2) }
  check(a == 1 and b == 3, "tabbar: all three tabs fit")
  check(drawn[1].s == " main " and drawn[1].x == 64 and drawn[2].s == " gfx " and drawn[2].x == 64 + 6 * 8 + 8,
        "tabbar: a label is ' name ', one column between tabs")
  local z = zone_at(64 + 6 * 8 + 8 + 4, 5)
  check(z and z.kind == "tab" and z.a == 2, "tabbar: a click on a tab gives kind tab and its number")
  check(zone_at(64 + 6 * 8 + 2, 5) == nil, "tabbar: the gap between tabs is no tab")
  U.begin()
  drawn = {}
  U.tabbar{ x = 0, y = 0, w = 200, cw = 8, ch = 12, kind = "mod", items = { { label = "a", dirty = true, tag = " 1" } } }
  check(drawn[1].s == " a* 1 ", "tabbar: dirty star then the tag")
  z = zone_at(4, 5)
  check(z and z.kind == "mod", "tabbar: the zone kind is the caller's")
  -- too many tabs: it starts where the active one shows
  U.begin()
  drawn = {}
  local names = {}
  for i = 1, 12 do names[i] = "mod" .. i end
  a, b = U.tabbar{ x = 0, y = 0, w = 240, cw = 8, ch = 12, items = items(names, 12) }
  check(a > 1 and b == 12, "tabbar: the active tab is always in view (from " .. a .. " to " .. b .. ")")
  check(drawn[1].s == "<", "tabbar: '<' marks the cut at the left")
  U.begin()
  drawn = {}
  a, b = U.tabbar{ x = 0, y = 0, w = 240, cw = 8, ch = 12, items = items(names, 1) }
  check(a == 1 and b < 12 and drawn[#drawn].s == "...", "tabbar: '...' marks the cut at the right")
end

-- the Lua modules of one project: split and join are exact inverses
do
  local function lines_of(s)
    local t = {}
    for l in (s .. "\n"):gmatch("(.-)\n") do t[#t + 1] = l end
    if t[#t] == "" then t[#t] = nil end
    return t
  end
  local cases = {
    "local x = 1\nfunction _draw() end\n",
    "local x = 1\n--@module gfx\nlocal y = 2\n--@module ui\nprint(1)\n",
    "--@module a\nx = 1\n--@module b\n\n--@module c\ny = 2\n",
    "x = 1\n--@module gfx   \n\n\ny = 2\n--@module  ui\n",
    "--@module only\n",
    "-- @module not a marker\n--@moduleX nor this\n  --@module nor this\n",
    "",
  }
  for i, c in ipairs(cases) do
    local ls = lines_of(c)
    local mods = U.modules_split(ls)
    local back = U.modules_join(mods)
    check(table.concat(back, "\n") == table.concat(ls, "\n") and #back == #ls,
          "modules: case " .. i .. " joins back to the same lines")
  end
  local m = U.modules_split(lines_of("a\n--@module gfx\nb\nc\n--@module ui\n"))
  check(#m == 3 and m[1].name == "main" and not m[1].head and m[2].name == "gfx" and m[3].name == "ui",
        "modules: main before the first marker, then one per marker")
  check(#m[1].lines == 1 and #m[2].lines == 2 and m[3].empty and U.module_len(m[3]) == 0,
        "modules: the lines of each, the empty one marked")
  m[3].lines[1] = ""                     -- an editor keeps one empty line in an empty module
  check(#U.modules_join(m) == 5, "modules: that kept empty line is not code")
  m[3].lines[1] = "z"
  check(U.modules_join(m)[6] == "z", "modules: once typed in, it is")
  check(U.module_name_ok("gfx-2.lua_") and not U.module_name_ok("a b") and not U.module_name_ok("") and
        not U.module_name_ok(string.rep("x", 25)), "modules: a name is letters, digits, _ . -")
end

-- a line of text with a caret: keys, click, draw
do
  local t = { text = "abc" }
  U.line_key(t, "d", 8)
  check(t.text == "abcd" and t.pos == 4, "line: a letter goes at the end when pos is nil")
  U.line_key(t, "left"); U.line_key(t, "left"); U.line_key(t, "X", 8)
  check(t.text == "abXcd" and t.pos == 3, "line: a letter goes at the caret")
  U.line_key(t, "\b")
  check(t.text == "abcd" and t.pos == 2, "line: backspace deletes before the caret")
  U.line_key(t, "del")
  check(t.text == "abd" and t.pos == 2, "line: del deletes after the caret")
  U.line_key(t, "home"); U.line_key(t, "\b")
  check(t.text == "abd" and t.pos == 0, "line: backspace at the start does nothing")
  U.line_key(t, "end")
  check(t.pos == 3 and not U.line_key(t, "f5") and not U.line_key(t, "enter"), "line: end; other keys are not used")
  local full = { text = "abc" }
  U.line_key(full, "z", 3)
  check(full.text == "abc", "line: the maximum is kept")
  frame(104, 0, 0, 0, {})
  U.line_click(t, 96, 8)
  check(t.pos == 1, "line: a click puts the caret on the nearest gap (x 104 -> 1)")
  frame(300, 0, 0, 0, {})
  U.line_click(t, 96, 8)
  check(t.pos == 3, "line: a click past the end puts the caret at the end")
  frame(10, 0, 0, 0, {})
  U.line_click(t, 96, 8)
  check(t.pos == 0, "line: a click before the start puts the caret at 0")
  drawn = {}
  t.pos = 1
  U.line_draw(t, 96, 176, 7, true)
  check(drawn[1].s == "abd" and #drawn == 1, "line: a caret inside the text is a bar (no extra print)")
  t.pos = 3; drawn = {}
  U.line_draw(t, 96, 176, 7, true)
  check(drawn[2] and drawn[2].s == "_" and drawn[2].x == 96 + 24, "line: the caret at the end is an underscore")
end

print(fails == 0 and ("bmui: " .. checks .. "/" .. checks .. " checks passed") or (fails .. " of " .. checks .. " FAILED"))
if fails > 0 then os.exit(1) end
