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
U.keyp = mkeyp

return U
