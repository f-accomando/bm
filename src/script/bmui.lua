-- bmui: the mouse in bm's tools (require "bmui", in the kernel like bm3d).
--
-- The user's request (2026-10-06): the whole editor suite with the mouse.
-- The rule: the mouse does what the keys do. A click chooses what is under
-- the pointer and, where Enter would act, a double click acts; the wheel
-- scrolls, a drag moves the view or draws; the right button opens a context
-- menu whose entries are the tool's keys. So every action of the mouse has
-- a key behind it, and the tools keep one way of doing each thing.
--
--   U.update()      in _update, first: reads mouse() once a frame
--   U.begin()       in _draw, first: forgets last frame's zones
--   U.zone(x, y, w, h, kind, a, b)   something clickable, as it is drawn;
--                   the last one drawn wins (dialogs over pages)
--   U.click([b])    the zone pressed this frame (button b, 0 left), or nil
--   U.at()          the zone under the pointer
--   U.press(key)    a key as if typed: the next keyp() gives it
--   U.menu(items)   the context menu at the pointer: { {label, key}, ... }
--                   or { {label, fn}, ... }; "-" a line
--   U.x, U.y, U.on  the pointer (U.on: there is one and it shows)
--   U.wheel         the wheel's clicks this frame (up positive)
--   U.moved         the pointer moved this frame
--   U.double        this frame's left press is a double click
--   U.drag          while a button is held after moving 3 pixels:
--                   {b=, x0=, y0=, dx=, dy=} (dx, dy: this frame's move)
--
--   U.tabbar(o)     the tab strip of every editor with tabs (one component,
--                   no copy): draws the tabs and their zones, see below
--   U.modules_split(lines), U.modules_join(mods)   the Lua modules of one
--                   project, see below
--
-- With no mouse nothing changes: U.on is false, no zone is ever clicked.

local U = { x = 0, y = 0, on = false, wheel = 0, double = false, drag = nil, held = 0 }

local C = {
  PANEL = 0x1C2030, BAR = 0x2A3048, TEXT = 0xE0E4F0, DIM = 0x707890, SEL = 0x3050A0, LINE = 0x3A4258,
}
U.C = C

local zones, nz = {}, 0
local queue = {}                -- keys pressed by the mouse, for keyp()
local pressed, released = 0, 0 -- the buttons' edges this frame (bits)
local down_at = {}              -- where each button went down
local dragged = {}              -- each button: it moved since it went down
local last_click_t, last_click_x, last_click_y = -1, 0, 0
local popup                     -- the context menu, when open
local raw_keyp = keyp
local was_on = false

local function now() return time and time() or 0 end

-- keyp() with the mouse's keys first (installed by require)
local function mkeyp()
  if #queue > 0 then return table.remove(queue, 1) end
  return raw_keyp()
end

function U.press(k)
  if k then queue[#queue + 1] = k end
end

function U.wants()               -- the pointer asked for (mouse(true))
  if mouse then mouse(true) end
end

function U.update()
  pressed, released, U.wheel, U.double = 0, 0, 0, false
  local x, y, b, w, shown = nil, nil, 0, 0, false
  if mouse then x, y, b, w, shown = mouse() end
  if not x then
    U.on, U.drag, U.held, U.moved = false, nil, 0, false
    was_on = false
    return
  end
  U.on = shown and true or false
  local moved = x ~= U.x or y ~= U.y
  U.moved = moved and shown and true or false
  local dx, dy = x - U.x, y - U.y
  U.x, U.y, U.wheel = x, y, w or 0
  b = b or 0
  pressed = b & ~U.held
  released = U.held & ~b
  -- mousep() sees a click shorter than a frame too
  if mousep then
    for i = 0, 2 do
      if mousep(i) then pressed = pressed | (1 << i) end
    end
  end
  U.held = b
  if not U.on then
    pressed, released, U.drag = 0, 0, nil
    return
  end
  for i = 0, 2 do
    if pressed >> i & 1 == 1 then down_at[i], dragged[i] = { x, y }, false end
  end
  if pressed & 1 == 1 then
    local t = now()
    U.double = t - last_click_t < 0.5 and math.abs(x - last_click_x) <= 4 and math.abs(y - last_click_y) <= 4
    last_click_t = U.double and -1 or t
    last_click_x, last_click_y = x, y
  end
  -- a drag: a button held and moved past 3 pixels from where it went down
  if U.drag and U.held >> U.drag.b & 1 == 0 then U.drag = nil end
  if U.drag then
    U.drag.dx, U.drag.dy = dx, dy
  elseif U.held ~= 0 and moved then
    for i = 0, 2 do
      local d = down_at[i]
      if U.held >> i & 1 == 1 and d and (math.abs(x - d[1]) > 3 or math.abs(y - d[2]) > 3) then
        U.drag = { b = i, x0 = d[1], y0 = d[2], dx = dx, dy = dy }
        dragged[i] = true
        break
      end
    end
  end
  was_on = true
end

-- the buttons' edges this frame: pressed(b), released(b), down(b)
function U.pressed(b) return U.on and pressed >> (b or 0) & 1 == 1 end
function U.released(b) return U.on and released >> (b or 0) & 1 == 1 end
function U.down(b) return U.on and U.held >> (b or 0) & 1 == 1 end

function U.begin()
  for i = 1, nz do zones[i] = nil end
  nz = 0
end

function U.zone(x, y, w, h, kind, a, b)
  nz = nz + 1
  local z = zones[nz]
  if not z then z = {}; zones[nz] = z end
  z.x, z.y, z.w, z.h, z.kind, z.a, z.b = x, y, w, h, kind, a, b
  return z
end

local function find(x, y)
  for i = nz, 1, -1 do
    local z = zones[i]
    if x >= z.x and x < z.x + z.w and y >= z.y and y < z.y + z.h then return z end
  end
end

function U.at()
  if not U.on then return nil end
  return find(U.x, U.y)
end

-- the zone a button pressed this frame (0 left, 1 right, 2 middle)
function U.click(b)
  if popup or not U.pressed(b or 0) then return nil end
  return find(U.x, U.y)
end

-- a button released without a drag: the zone where it went down, or
-- true over nothing (the right one opens the context menu this way, so a
-- drag with it can turn a view)
function U.clicked(b)
  b = b or 1
  if popup or not U.released(b) or dragged[b] then return nil end
  local d = down_at[b]
  if not d then return nil end
  return find(d[1], d[2]) or true
end

-- a drag of button b (0 left) as arrow keys: one every `step` pixels, as
-- the keyboard would move the thing (a move tool, a value). True while it
-- drags.
local arrow_acc = { 0, 0 }
function U.drag_arrows(step, b)
  local d = U.drag
  if not d or d.b ~= (b or 0) then arrow_acc[1], arrow_acc[2] = 0, 0; return false end
  arrow_acc[1], arrow_acc[2] = arrow_acc[1] + d.dx, arrow_acc[2] + d.dy
  while arrow_acc[1] >= step do U.press("right"); arrow_acc[1] = arrow_acc[1] - step end
  while arrow_acc[1] <= -step do U.press("left"); arrow_acc[1] = arrow_acc[1] + step end
  while arrow_acc[2] >= step do U.press("down"); arrow_acc[2] = arrow_acc[2] - step end
  while arrow_acc[2] <= -step do U.press("up"); arrow_acc[2] = arrow_acc[2] + step end
  return true
end

-- Shift held on a keyboard (a click that adds to a choice)
function U.shift()
  return keydown and (keydown(0xE1) or keydown(0xE5)) or false
end

-- the pointer inside a rectangle
function U.inside(x, y, w, h)
  return U.on and U.x >= x and U.x < x + w and U.y >= y and U.y < y + h
end

-- a row of a list under the pointer: (index, zone) for zones of `kind`
-- whose `a` is the row
function U.row(kind)
  local z = U.at()
  if z and z.kind == kind then return z.a, z end
end

-- a list with the mouse (M48): the wheel moves the chosen row by `step`
-- rows (default 1), a click on a zone of `kind` (its `a` is the row) chooses
-- that row, and a click on the chosen row, or a double click, acts (as
-- Enter would). Returns the new row and true when it acted. Call it before
-- the keys, with the rows' zones drawn last frame.
function U.list(kind, sel, n, step)
  if not U.on or n < 1 then return sel, false end
  if U.wheel ~= 0 then sel = math.max(1, math.min(n, sel - U.wheel * (step or 1))) end
  local z = U.click(0)
  if z and z.kind == kind and z.a and z.a >= 1 and z.a <= n then
    if z.a == sel or U.double then return z.a, true end
    sel = z.a
  end
  return sel, false
end

-- a pixel x to a text column: the gap nearest to x in a line that starts at
-- x0 with letters cw wide and n letters (0 = before the first letter)
function U.col(x, x0, cw, n)
  local c = (x - x0 + cw // 2) // cw
  return c < 0 and 0 or (c > n and n or c)
end

-- a line of text with a caret (M48): t = { text =, pos = } (pos 0..#text; nil
-- is the end). U.line_key(t, k, max) edits it with a key as keyp() gives it
-- (letters insert at the caret; backspace, del, left, right, home, end) and
-- returns true when it used the key. U.line_click(t, x0, cw) puts the caret
-- where the pointer is, for a line printed from x0 with letters cw wide.
-- U.line_draw(t, x0, y, colour, blink, cw) prints the line and, when blink
-- is true, the caret: "_" after the last letter, a thin bar inside the text.
function U.line_key(t, k, max)
  local s = t.text
  local p = t.pos or #s
  if p > #s then p = #s end
  if k == "left" then p = p > 0 and p - 1 or 0
  elseif k == "right" then p = p < #s and p + 1 or #s
  elseif k == "home" then p = 0
  elseif k == "end" then p = #s
  elseif k == "\b" then
    if p > 0 then s, p = s:sub(1, p - 1) .. s:sub(p + 1), p - 1 end
  elseif k == "del" then s = s:sub(1, p) .. s:sub(p + 2)
  elseif #k == 1 and k:byte() >= 32 and (not max or #s < max) then
    s, p = s:sub(1, p) .. k .. s:sub(p + 1), p + 1
  else return false end
  t.text, t.pos = s, p
  return true
end

function U.line_click(t, x0, cw)
  t.pos = U.col(U.x, x0, cw or 8, #t.text)
end

function U.line_draw(t, x0, y, colour, blink, cw)
  cw = cw or 8
  local p = t.pos or #t.text
  if p > #t.text then p = #t.text end
  print(t.text, x0, y, colour)
  if blink then
    if p >= #t.text then print("_", x0 + p * cw, y, colour)
    else rectfill(x0 + p * cw, y, 1, 16, colour) end
  end
end

-- the wheel on a view: the new top (lo..hi) after `step` units per click
function U.scroll(top, lo, hi, step)
  if U.wheel == 0 then return top end
  top = top - U.wheel * step
  return top < lo and lo or (top > hi and hi or top)
end

----------------------------------------------------------------- keys shown as chips

-- the key a chip shows, as keyp() gives it: "ctrl s" (or {"ctrl", "s"})
-- -> "^s", "ctrl shift s" -> "^S", "shift l" -> "L", "enter" -> "\n",
-- "f5" -> "f5"; nil for the pad's buttons (upper case) and for lists of
-- keys that are not one key ({"up", "down"}: "choose")
local CHIP = { enter = "\n", esc = "esc", space = " ", tab = "\t", backspace = "\b", bksp = "\b", del = "del",
               up = "up", down = "down", left = "left", right = "right", home = "home", ["end"] = "end",
               pgup = "pgup", pgdn = "pgdn" }
function U.key_code(keys)
  if type(keys) == "string" then
    local t = {}
    for k in keys:gmatch("%S+") do t[#t + 1] = k end
    keys = t
  end
  local ctrl, shift, main
  for _, k in ipairs(keys) do
    if k == "ctrl" then ctrl = true
    elseif k == "shift" then shift = true
    elseif main then return nil                  -- two keys: not one action
    else main = k end
  end
  if not main or main:match("^%u") then return nil end    -- the pad's buttons
  main = CHIP[main] or main
  if #main > 1 and not main:match("^f%d+$") and not CHIP[main] and main ~= "\n" then
    if not ({ esc = 1, del = 1, up = 1, down = 1, left = 1, right = 1, home = 1, ["end"] = 1, pgup = 1, pgdn = 1 })[main] then
      return nil
    end
  end
  if ctrl then return "^" .. (shift and main:upper() or main) end
  if shift and #main == 1 then return main:upper() end
  return main
end

-- a chip (or a tab, a button) that presses its key when clicked
function U.key_zone(x, y, w, h, keys)
  local k = U.key_code(keys)
  if k then U.zone(x, y, w, h, "key", k) end
end

-- the top bar: the program's name is the first item at the left; returns the
-- x of the first tab
function U.bar_title(name, colour)
  print(name, 4, 0, colour)
  return 4 + #name * 8 + 16
end

-- the top bar: the file's name at the right, in the room left after x (the
-- start is cut when it is long); "*" when changed
function U.bar_file(name, dirty, x, w, c_dirty, c_dim)
  name = name .. (dirty and "*" or "")
  local room = (w - 8 - x) // 8
  if room < 4 then return end
  if #name > room then name = "~" .. name:sub(-(room - 1)) end
  print(name, w - #name * 8 - 8, 0, dirty and c_dirty or c_dim)
end

-- a left click on a key's zone: its key pressed. True when it was one.
function U.keys()
  local z = U.click(0)
  if z and z.kind == "key" then
    U.press(z.a)
    return true
  end
  return false
end

----------------------------------------------------------------- the context menu

-- items: { {label, key or function, [right text]}, "-", ... }; the key is
-- shown on the right (as the tool's menus write them) unless given. The
-- rows sit on the 8x16 grid (the tests read the screen); "-" is a row of
-- its own with a line.
function U.menu(items, x, y)
  local w = 0
  local list = {}
  for _, it in ipairs(items) do
    if it == "-" then
      if #list > 0 and list[#list] ~= "-" then list[#list + 1] = "-" end
    elseif it then
      local label, act, right = it[1], it[2], it[3]
      if not right and type(act) == "string" then right = U.key_name(act) end
      right = right or ""
      list[#list + 1] = { label = label, act = act, right = right }
      local n = #label + (#right > 0 and #right + 3 or 0)
      if n > w then w = n end
    end
  end
  if list[#list] == "-" then list[#list] = nil end
  if #list == 0 then return end
  local pw, ph = w * 8 + 16, #list * 16
  x, y = x or U.x, y or U.y
  local sw, sh = 640, 360
  if screen_size then sw, sh = screen_size() end
  x, y = (x + 7) // 8 * 8, (y + 15) // 16 * 16
  if x + pw > sw - 8 then x = (sw - 8 - pw) // 8 * 8 end
  if y + ph > sh - 8 then y = (sh - 8 - ph) // 16 * 16 end
  popup = { items = list, x = math.max(8, x), y = math.max(16, y), w = pw, h = ph, sel = 0 }
end

function U.menu_open() return popup ~= nil end
function U.menu_close() popup = nil end

-- "^c" -> "Ctrl+C", "f5" -> "F5", "\n" -> "Enter"
local NAMES = { ["\n"] = "Enter", ["\b"] = "Backspace", ["\t"] = "Tab", [" "] = "Space", del = "Del", esc = "Esc",
                up = "Up", down = "Down", left = "Left", right = "Right", home = "Home", ["end"] = "End",
                pgup = "PgUp", pgdn = "PgDn" }
function U.key_name(k)
  if NAMES[k] then return NAMES[k] end
  local c = k:match("^%^(.)$")
  if c then
    if c:match("%u") then return "Ctrl+Shift+" .. c end
    if c == "\n" then return "Ctrl+Enter" end
    return "Ctrl+" .. c:upper()
  end
  if k:match("^f%d+$") then return k:upper() end
  return k
end

local function popup_row(y)
  local p = popup
  local i = (y - p.y) // 16 + 1
  if y >= p.y and i >= 1 and i <= #p.items and p.items[i] ~= "-" then return i end
  return 0
end

local function popup_inside()
  local p = popup
  return U.inside(p.x, p.y - 4, p.w, p.h + 8)
end

-- the context menu's frame: true when it took the input (the tool then does
-- nothing else this frame). Keys work in it too: up, down, Enter, Esc.
function U.menu_update()
  local p = popup
  if not p then return false end
  if U.on and U.x >= p.x and U.x < p.x + p.w then p.sel = popup_row(U.y) end
  local act
  if U.pressed(0) or U.pressed(2) then
    local i = popup_inside() and popup_row(U.y) or 0
    if i > 0 then act = p.items[i].act end
    popup = nil
  elseif U.pressed(1) and not popup_inside() then
    popup = nil
    return false                -- a right click elsewhere: a new menu there
  else
    -- the keys: the menu takes them while it is open
    local k = raw_keyp()
    while k do
      local n = #p.items
      if k == "up" or k == "down" then
        for _ = 1, n do                 -- over the lines
          p.sel = k == "up" and (p.sel - 2) % n + 1 or p.sel % n + 1
          if p.items[p.sel] ~= "-" then break end
        end
      elseif k == "\n" and p.sel > 0 then
        act = p.items[p.sel].act
        popup = nil
        break
      elseif k == "esc" then
        popup = nil
        break
      end
      k = raw_keyp()
    end
  end
  if type(act) == "function" then act()
  elseif type(act) == "string" then U.press(act) end
  return true
end

function U.menu_draw()
  local p = popup
  if not p then return end
  rectfill(p.x + 3, p.y - 1, p.w, p.h + 8, 0x000000)
  rectfill(p.x, p.y - 4, p.w, p.h + 8, C.PANEL)
  rect(p.x, p.y - 4, p.w, p.h + 8, C.LINE)
  for i, it in ipairs(p.items) do
    local y = p.y + (i - 1) * 16
    if it == "-" then
      rectfill(p.x + 4, y + 7, p.w - 8, 1, C.LINE)
    else
      if i == p.sel then rectfill(p.x + 2, y, p.w - 4, 16, C.SEL) end
      print(it.label, p.x + 8, y, C.TEXT)
      if #it.right > 0 then print(it.right, p.x + p.w - 8 - #it.right * 8, y, C.DIM) end
    end
  end
end

-- keyp() takes the mouse's keys first: installed once, for every tool that
-- requires this library
if raw_keyp and _ENV.keyp == raw_keyp then _ENV.keyp = mkeyp end
----------------------------------------------------------------- projects (.bme)

-- The one place that knows where projects live (docs/PROJECT-LAYOUT.md, the C side:
-- src/bm/project.h). New projects are written in /bme; a project found next to its game
-- (an old SD card) is still read, and saved where it is.
U.PROJECTS = "/bme"
local OLD_DIRS = { "/carts", "/" }

-- "/carts/my game.bm", "mygame.bme" or "my game" -> "/bme/mygame.bme" (8.3)
function U.project_path(name)
  local base = name:match("([^/]+)$") or name
  local stem = base:gsub("%.[^.]*$", ""):lower():gsub("[^%w_%-]", "")
  if stem == "" then stem = "game" end
  return U.PROJECTS .. "/" .. stem:sub(1, 8) .. ".bme"
end

-- a project file (by its extension)
function U.is_project(path) return path ~= nil and path:lower():match("%.bme$") ~= nil end

-- a project that is not in /bme (an old card): it is saved where it is
function U.is_old_place(path)
  return U.is_project(path) and path:lower():match("^/bme/") == nil
end

-- the words for the status line after a save: "saved /bme/game.bme", and where an old project is
function U.saved_text(path)
  if U.is_old_place(path) then
    return "saved " .. path .. " (old place: projects go in /bme, this one stays here)"
  end
  return "saved " .. path
end

-- The files a picker lists: /bme first, then the old places. exts: { bm = true, bme = true,
-- b16 = true } (a set of lower case extensions); extra: more folders to read after those.
-- A path once only (case does not matter). Returns the paths, and a table of their sizes.
function U.project_files(exts, extra)
  local dirs = { U.PROJECTS }
  for _, d in ipairs(OLD_DIRS) do dirs[#dirs + 1] = d end
  for _, d in ipairs(extra or {}) do dirs[#dirs + 1] = d end
  local out, seen, sizes = {}, {}, {}
  for _, dir in ipairs(dirs) do
    for _, f in ipairs(ls(dir) or {}) do
      local ext = f.name:lower():match("%.(%w+)$")
      local p = (dir == "/" and "" or dir) .. "/" .. f.name
      if not f.dir and ext and exts[ext] and not (dir == U.PROJECTS and ext ~= "bme") and not seen[p:upper()] then
        seen[p:upper()] = true
        out[#out + 1] = p
        sizes[p] = f.size
      end
    end
  end
  return out, sizes
end

-- The tab strip (APP-8): the one tab bar of bm Code, bm SDK and the other
-- editors with a code page. o = { x=, y=, w= (pixels), cw=, ch= (the font's
-- cell), items = { {label=, on=, dirty=, tag=}, ... }, kind = the zone kind
-- ("tab" by default; a click gives it with a = the item's number), c = {
-- on=, off=, line=, dim= colours} }. Each tab is " label* tag " with one
-- column between tabs. When the tabs do not fit, the strip starts at the
-- first one that lets the active tab show, "<" marks the cut at the left and
-- "..." the cut at the right. Returns the number of the first tab drawn and
-- of the last one.
function U.tabbar(o)
  local cw, ch, kind, items = o.cw or 8, o.ch or 12, o.kind or "tab", o.items
  local c = o.c or {}
  local lab = {}
  local act = 1
  for i, it in ipairs(items) do
    lab[i] = " " .. it.label .. (it.dirty and "*" or "") .. (it.tag or "") .. " "
    if it.on then act = i end
  end
  local cols = o.w // cw
  local function span(a, b)
    local n = 0
    for i = a, b do n = n + #lab[i] + 1 end
    return n
  end
  local from = 1
  while from < act and span(from, act) + (from > 1 and 2 or 0) > cols - 3 do from = from + 1 end
  local x = o.x
  if from > 1 then
    print("<", x, o.y, c.dim or C.DIM)
    x = x + 2 * cw
  end
  local last = from - 1
  for i = from, #items do
    local w = #lab[i] * cw
    if x + w > o.x + o.w then
      print("...", x, o.y, c.dim or C.DIM)
      break
    end
    U.zone(x, o.y, w, ch, kind, i)
    if items[i].on then rectfill(x, o.y, w, ch, c.line or C.LINE) end
    print(lab[i], x, o.y, items[i].on and (c.on or 0xFFFFFF) or (c.off or C.DIM))
    x = x + w + cw
    last = i
  end
  return from, last
end

-- find a text in several sources (find in the project): sources is a list of
-- { name =, lines = }; the text is plain and not case sensitive. Returns the
-- hits in order, one per line: { src = the source's number, line =, col = (1
-- based), text = the line without its indent }.
function U.find_all(sources, text)
  local out = {}
  if not text or text == "" then return out end
  local q = text:lower()
  for si, s in ipairs(sources) do
    for li, l in ipairs(s.lines) do
      local p = l:lower():find(q, 1, true)
      if p then out[#out + 1] = { src = si, line = li, col = p, text = (l:gsub("^%s+", "")) } end
    end
  end
  return out
end

-- The Lua modules of one project (APP-8). The code of a project stays ONE
-- LUA section; a line "--@module NAME" starts a module, which runs to the
-- next such line. For the cartridge it is a comment, so the game, its bytes
-- and its build do not depend on the tabs: the layout is for readability only.
-- What comes before the first marker is the module "main" (no marker line).
-- modules_split(lines) -> { {name=, lines=, head=, empty=}, ... }: head is the
-- marker line as it was (nil for the first module without one), empty is
-- true when the module has no line at all. modules_join(mods) -> lines is its
-- exact inverse: split then join gives the same lines.
function U.modules_split(lines)
  local mods, cur = {}, nil
  for _, l in ipairs(lines) do
    local name = l:match("^%-%-@module%s+([%w_%.%-]+)%s*$")
    if name then
      cur = { name = name, lines = {}, head = l }
      mods[#mods + 1] = cur
    else
      if not cur then
        cur = { name = "main", lines = {} }
        mods[1] = cur
      end
      cur.lines[#cur.lines + 1] = l
    end
  end
  if #mods == 0 then mods[1] = { name = "main", lines = {} } end
  for _, m in ipairs(mods) do m.empty = #m.lines == 0 end
  return mods
end

-- the number of lines a module really has: an editor keeps one empty line in
-- a module that has none (m.empty), which is not part of the code
function U.module_len(m)
  if m.empty and #m.lines == 1 and m.lines[1] == "" then return 0 end
  return #m.lines
end

function U.modules_join(mods)
  local out = {}
  for _, m in ipairs(mods) do
    if m.head then out[#out + 1] = m.head end
    for i = 1, U.module_len(m) do out[#out + 1] = m.lines[i] end
  end
  return out
end

-- true when a name can be a module's: letters, digits, _ . -
function U.module_name_ok(name)
  return name ~= nil and name:match("^[%w_%.%-]+$") ~= nil and #name <= 24
end

U.keyp = mkeyp

return U
