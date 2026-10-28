-- The COMMANDS page: every action with its keyboard key and, beside it, the pad's button (the pad
-- does not change). Up and down choose a row; ok (Space, a click) arms it and the next key pressed
-- is the new key; Esc or Backspace stops. RESET TO DEFAULT asks first. The page is drawn by the
-- title menu (80_modes) and by the pause menu (86_pause); both call open, update and draw. The keys
-- are in 10_input (Input.bind, Input.reset_binds); they are saved in the game's slot 1.

Keys = { sel = 1, wait = false, armed = false, ask = false, ask_sel = 2, msg = nil }

-- the pad's button of each action, as 86_pause showed it (Input.nintendo: the RGB30's letters)
local PAD = { fwd = "LSTICK", bwd = "LSTICK", lft = "LSTICK", rgt = "LSTICK", fire = "R2", fire2 = "L2",
  ab1 = "L1", ab2 = "R1", ult = "Y", jump = "A", crouch = "B", reload = "X", melee = "R3", hero = "DOWN" }
local NINTENDO = { Y = "X", A = "B", B = "A", X = "Y" }

local ESC = 0x29                       -- stops the wait

function Keys.open()
  Keys.sel, Keys.wait, Keys.armed, Keys.ask, Keys.msg, Keys.px = 1, false, false, false, nil, nil
end

local function held_keys()
  local ok, l = pcall(keys)
  return ok and type(l) == "table" and l or {}
end

-- the page's geometry: row height, first row's y, x of the columns, width
local function geometry()
  local n = #Input.ACTIONS
  local rowh = math.min(17, (LH - 56) // (n + 2))
  local x0 = LW // 2 - 160
  return rowh, 30, x0, 332
end

-- the rows are the actions, then RESET TO DEFAULT (a gap of half a row above it)
local function row_y(i)
  local rowh, y0 = geometry()
  local y = y0 + (i - 1) * rowh
  if i > #Input.ACTIONS then y = y + rowh // 2 end
  return y
end

local function reset_boxes()
  local w, h = 56, 18
  local cx, y = LW // 2, LH // 2 + 4
  return cx - w - 8, y, w, h, cx + 8, y, w, h          -- YES, NO
end

local function inside(px, py, x, y, w, h) return px >= x and px < x + w and py >= y and py < y + h end

-- one frame; "close" when the player leaves the page
function Keys.update()
  local c = Input.cmd
  local n = #Input.ACTIONS + 1
  if Keys.ask then
    if c.left_p or c.right_p or c.up_p or c.down_p then Keys.ask_sel = 3 - Keys.ask_sel Snd.play("ui") end
    local go = c.ok_p
    if c.ptr_x and c.click_p then
      local xa, ya, wa, ha, xb, yb, wb, hb = reset_boxes()
      if inside(c.ptr_x, c.ptr_y, xa, ya, wa, ha) then Keys.ask_sel, go = 1, true
      elseif inside(c.ptr_x, c.ptr_y, xb, yb, wb, hb) then Keys.ask_sel, go = 2, true end
    end
    if c.back_p or c.menu_p then
      Keys.ask = false
      Snd.play("ui_back")
    elseif go then
      Keys.ask = false
      if Keys.ask_sel == 1 then
        Input.reset_binds()
        Keys.msg = "COMMANDS BACK TO DEFAULT"
      end
      Snd.play("ui")
    end
    return
  end
  if Keys.wait then
    local l = held_keys()
    if Keys.armed then                          -- the key that armed the row must go up first
      if #l == 0 then Keys.armed = false end
      return
    end
    if c.back_p then Keys.wait = false Snd.play("ui_back") return end
    for _, u in ipairs(l) do
      if u == ESC then
        Keys.wait = false
        Snd.play("ui_back")
        return
      elseif Input.bindable(u) then
        local a = Input.ACTIONS[Keys.sel]
        local moved = Input.bind(a[1], u)
        Keys.msg = moved and ("SWAPPED WITH " .. moved) or nil
        Keys.wait = false
        Snd.play("ui")
        return
      end
    end
    if #l > 0 then Keys.msg = "THE GAME KEEPS THAT KEY" end
    return
  end
  if c.menu_p or c.back_p then Snd.play("ui_back") return "close" end
  if c.up_p then Keys.sel = (Keys.sel - 2) % n + 1 Keys.msg = nil Snd.play("ui") end
  if c.down_p then Keys.sel = Keys.sel % n + 1 Keys.msg = nil Snd.play("ui") end
  local go = c.ok_p
  if c.ptr_x then
    local rowh, _, x0, w = geometry()
    for i = 1, n do
      if inside(c.ptr_x, c.ptr_y, x0 - 6, row_y(i), w, rowh) then
        if c.ptr_x ~= Keys.px or c.ptr_y ~= Keys.py then Keys.sel = i end
        if c.click_p then Keys.sel, go = i, true end
      end
    end
    Keys.px, Keys.py = c.ptr_x, c.ptr_y
  end
  if go then
    Snd.play("ui")
    if Keys.sel <= #Input.ACTIONS then
      Keys.wait, Keys.armed, Keys.msg = true, true, nil
    else
      Keys.ask, Keys.ask_sel = true, 2
    end
  end
end

-- a row of key chips from x, the top at y
local function chips(list, x, y, small)
  for _, nm in ipairs(list) do x = uprompt(nm, x, y, small, true) + 3 end
  return x
end

function Keys.draw()
  font("6x12")
  urectfill(0, 0, LW, LH, 0x000000)               -- (the frame behind is not drawn)
  local cx = LW // 2
  local rowh, y0, x0, w = geometry()
  local small = rowh < 16
  uprint("COMMANDS", cx - 24, 5, 0xFFE070)
  uprint("KEYBOARD", x0 + 112, 19, 0xFFB43C)
  uprint("PAD", x0 + 262, 19, 0xFFB43C)
  local t = Input.cmd.pad
  for i, a in ipairs(Input.ACTIONS) do
    local y = row_y(i)
    local sel = i == Keys.sel
    if sel then urectfill(x0 - 6, y, w, rowh, 0x3A3F47) end
    uprint(a[2], x0, y + (rowh - 12) // 2, sel and 0xFFFFFF or 0xD8DCE2)
    if sel and Keys.wait then
      local blink = (G.frame // 20) % 2 == 0
      uprint(Keys.armed and "..." or "PRESS A KEY", x0 + 112, y + (rowh - 12) // 2, blink and 0xFFE070 or 0xF26A21)
    else
      chips(Input.keynames(a[1]), x0 + 112, y, small)
    end
    local pad = PAD[a[1]]
    if Input.nintendo and NINTENDO[pad] then pad = NINTENDO[pad] end
    uprompt(pad, x0 + 262, y, small, true)
  end
  -- RESET TO DEFAULT
  local n = #Input.ACTIONS + 1
  local y = row_y(n)
  local sel = Keys.sel == n
  if sel then urectfill(x0 - 6, y, w, rowh, 0xF26A21) end
  uprint("RESET TO DEFAULT", x0, y + (rowh - 12) // 2, sel and 0xFFFFFF or 0xD8DCE2)
  if Keys.msg then uprint(Keys.msg, cx - #Keys.msg * 3, LH - 28, 0x60FF90) end
  local hint = Keys.wait and "ESC OR BACKSPACE: CANCEL" or
    (t and "OK: CHANGE   START: BACK" or "SPACE OR CLICK: CHANGE   ENTER: BACK")
  uprint(hint, cx - #hint * 3, LH - 14, 0x9AA2AC)
  if Keys.ask then
    local bw = 200
    urectfill(cx - bw // 2, LH // 2 - 28, bw, 62, 0x1A1D22)
    urect(cx - bw // 2, LH // 2 - 28, bw, 62, 0x50565F)
    local q = "RESET ALL COMMANDS?"
    uprint(q, cx - #q * 3, LH // 2 - 18, 0xFFE070)
    local xa, ya, wa, ha, xb, yb, wb, hb = reset_boxes()
    for k, b in ipairs({ { xa, ya, wa, ha, "YES" }, { xb, yb, wb, hb, "NO" } }) do
      local on = Keys.ask_sel == k
      if on then urectfill(b[1], b[2], b[3], b[4], 0xF26A21) else urect(b[1], b[2], b[3], b[4], 0x50565F) end
      uprint(b[5], b[1] + b[3] // 2 - #b[5] * 3, b[2] + 3, on and 0xFFFFFF or 0xD8DCE2)
    end
  end
  font()
end
