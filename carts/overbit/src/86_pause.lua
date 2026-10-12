-- The pause menu of the .bm (OVERBIT_TV_BM): Enter / Options in a solo match or
-- on the training range stops the game and opens RESUME, CONTROLS (the list
-- of the keys, the mouse and the pad) and LEAVE. Up, down and ok, or the mouse
-- (move and left click). A network match cannot stop: there the menu button
-- leaves, as before (the .b16 too).

Pause = { on = false, sel = 1, page = "main" }

local ITEMS = { "RESUME", "CONTROLS", "LEAVE" }

local ROWS = {
  { "MOVE", "W A S D", "LEFT STICK" },
  { "LOOK", "ARROWS", "RIGHT STICK" },
  { "FIRE", "MOUSE LEFT / J", "R2" },
  { "SECONDARY FIRE", "MOUSE RIGHT / K", "L2" },
  { "ABILITY 1", "SHIFT", "L1" },
  { "ABILITY 2", "E", "R1" },
  { "ULTIMATE", "Q", "Y" },
  { "JUMP", "SPACE", "A" },
  { "CROUCH", "C / CTRL", "B" },
  { "RELOAD", "R", "X" },
  { "MELEE", "V", "R3" },
  { "CHANGE HERO", "H", "DOWN" },
  { "PAUSE", "ENTER", "START" },
}

function Pause.enabled(net)
  return OVERBIT_TV_BM and not net
end

function Pause.open()
  Pause.on, Pause.sel, Pause.page = true, 1, "main"
  Input.arrow(true)
end

function Pause.close()
  Pause.on = false
  Input.arrow(false)
end

-- the geometry of the main page: the item i's box
local function item_box(i)
  local w = 120
  return LW // 2 - w // 2, 96 + (i - 1) * 22, w, 18
end

-- one frame while paused: returns "leave" when the match is to end
function Pause.update()
  local c = Input.cmd
  if Pause.page == "controls" then
    if c.menu_p or c.back_p or c.ok_p or c.click_p then Pause.page = "main" Snd.play("ui_back") end
    return
  end
  if c.menu_p or c.back_p then Pause.close() Snd.play("ui_back") return end
  if c.up_p then Pause.sel = (Pause.sel - 2) % #ITEMS + 1 Snd.play("ui") end
  if c.down_p then Pause.sel = Pause.sel % #ITEMS + 1 Snd.play("ui") end
  local go = c.ok_p
  if c.ptr_x then
    for i = 1, #ITEMS do
      local x, y, w, h = item_box(i)
      if c.ptr_x >= x and c.ptr_x < x + w and c.ptr_y >= y and c.ptr_y < y + h then
        if c.ptr_x ~= Pause.px or c.ptr_y ~= Pause.py then Pause.sel = i end      -- (a moved pointer picks)
        if c.click_p then Pause.sel, go = i, true end
      end
    end
    Pause.px, Pause.py = c.ptr_x, c.ptr_y
  end
  if go then
    Snd.play("ui")
    if Pause.sel == 1 then Pause.close()
    elseif Pause.sel == 2 then Pause.page = "controls"
    else Pause.close() return "leave" end
  end
end

function Pause.draw()
  font("6x12")
  urectfill(0, 0, LW, LH, 0x000000)               -- (the frame behind is not drawn)
  local cx = LW // 2
  if Pause.page == "controls" then
    local x0, y0 = cx - 150, 24
    uprint("CONTROLS", cx - 24, y0 - 14, 0xFFE070)
    uprint("KEYBOARD AND MOUSE", x0 + 112, y0, 0xFFB43C)
    uprint("PAD", x0 + 252, y0, 0xFFB43C)
    for i, r in ipairs(ROWS) do
      local y = y0 + 6 + i * 14
      uprint(r[1], x0, y, 0xD8DCE2)
      uprint(r[2], x0 + 112, y, 0xFFFFFF)
      uprint(r[3], x0 + 252, y, 0xFFFFFF)
    end
    uprint("MOUSE: LEFT BUTTON FIRES, RIGHT BUTTON IS THE SECONDARY FIRE", x0 - 12, y0 + 6 + (#ROWS + 1) * 14 + 4, 0x9AA2AC)
    uprint("ENTER OR CLICK: BACK", cx - 60, LH - 22, 0x9AA2AC)
    font()
    return
  end
  uprint("PAUSE", cx - 15, 64, 0xFFE070)
  for i, it in ipairs(ITEMS) do
    local x, y, w, h = item_box(i)
    local sel = i == Pause.sel
    if sel then urectfill(x, y, w, h, 0xF26A21) else urect(x, y, w, h, 0x50565F) end
    uprint(it, cx - #it * 3, y + 3, sel and 0xFFFFFF or 0xD8DCE2)
  end
  font()
end
