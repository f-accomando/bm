-- Ui: the screens of nano8. The list of carts (labels in a grid), the game
-- (the 128x128 screen, sharp 2x or full height), the pause menu, the
-- controls, the errors.

Ui.mode = "browser"
Ui.carts = {}
local ticks = 0
local bg_dirty = 3

-- ---------------------------------------------------------------- helpers

local function text(s, x, y, c) return print(s, x, y, c or C.text) end
local function center(s, cx, y, c) return print(s, cx - #s * 4, y, c or C.text) end

local function fit(s, n)
  s = tostring(s or "")
  if #s <= n then return s end
  return sub(s, 1, n - 1) .. "~"
end

-- the carts' text is P8SCII: symbols become "?" for the console font
local function plain(s)
  return (tostring(s or ""):gsub("[\0-\31\127-\255]", "?"))
end

local function wrap(s, width, max_lines)
  local lines = {}
  for para in (s .. "\n"):gmatch("(.-)\n") do
    para = para:gsub("\t", "  ")
    while #para > width do
      local cut = para:sub(1, width):match(".*()[ ,%)]") or width
      if cut < width / 2 then cut = width end
      lines[#lines + 1] = para:sub(1, cut)
      para = para:sub(cut + 1)
    end
    lines[#lines + 1] = para
    if #lines >= max_lines then break end
  end
  while #lines > max_lines do remove(lines) end
  return lines
end

local function logo(x, y)
  local e = n8.text("nano", x, y, C.text, 3)
  n8.text("8", e, y, C.accent, 3)
end

local function game_rect()
  if Cfg.data.scale == "fill" then return 140, 0, 360, 360 end
  return 192, 52, 256, 256
end

function Ui.go(mode)
  Ui.mode = mode
  bg_dirty = 3
  In.settle()
end

-- ---------------------------------------------------------------- the list

local B = { sel = 1, top = 0, info = {}, owner = {}, msg = nil }
local COLS, ROWS, CW, CH, GAP = 4, 2, 128, 128, 24
local X0, Y0 = (W - (COLS * CW + (COLS - 1) * GAP)) // 2, 60
local PITCH = CH + 20
local SLOTS = 47

local function slot_of(i) return (i - 1) % SLOTS + 1 end

function Ui.scan()
  local list, seen = {}, {}
  for _, d in ipairs(DIRS) do
    for _, e in ipairs(ls(d)) do
      local low = e.name:lower()
      if not e.dir and (low:match("%.p8$") or low:match("%.p8%.png$")) and not seen[low] then
        seen[low] = true
        list[#list + 1] = { path = (d == "/" and "" or d) .. "/" .. e.name, name = e.name, dir = d, size = e.size }
      end
    end
  end
  sort(list, function(a, b) return a.name:lower() < b.name:lower() end)
  Ui.carts = list
  B.info, B.owner = {}, {}
  B.sel = clamp(B.sel, 1, max(1, #list))
  if Cfg.data.last then
    for i, c in ipairs(list) do
      if c.path == Cfg.data.last then B.sel = i end
    end
  end
end

local function title_of(i)
  local inf = B.info[i]
  local t = inf and inf.title ~= "" and inf.title or Vm.base_name(Ui.carts[i].name)
  return plain(t)
end

-- one label a frame, the visible ones first
local function load_labels()
  local first = B.top * COLS + 1
  for i = first, min(#Ui.carts, first + COLS * ROWS - 1) do
    local s = slot_of(i)
    if B.owner[s] ~= i then
      local inf, err = n8.preview(Ui.carts[i].path, s)
      B.info[i] = inf or { title = "", author = "", bad = err }
      B.owner[s] = i
      return
    end
  end
end

local function play(i)
  local c = Ui.carts[i]
  if not c then return end
  Cfg.data.last = c.path
  Cfg.changed()
  Vm.close()
  if Vm.start(c.path) then
    Ui.go("game")
  else
    Ui.go("error")
  end
end
Ui.play = play

local function browser_update()
  load_labels()
  local n = In.nav()
  local count = #Ui.carts
  if count > 0 then
    if n.left then B.sel = B.sel - 1 end
    if n.right then B.sel = B.sel + 1 end
    if n.up then B.sel = B.sel - COLS end
    if n.down then B.sel = B.sel + COLS end
    if n.l then B.sel = B.sel - COLS * ROWS end
    if n.r then B.sel = B.sel + COLS * ROWS end
    B.sel = clamp(B.sel, 1, count)
    local row = (B.sel - 1) // COLS
    if row < B.top then B.top = row end
    if row >= B.top + ROWS then B.top = row - ROWS + 1 end
    if n.ok then play(B.sel) end
  end
  if n.more then
    Ui.controls_from = "browser"
    Ui.go("controls")
  end
end

local function browser_draw()
  cls(C.bg)
  logo(X0, 12)
  local count = #Ui.carts
  local hint = "A play   X controls"
  print(hint, W - X0 - #hint * 8, 10, C.dim)
  local cnt = fmt("%d cart%s", count, count == 1 and "" or "s")
  print(cnt, W - X0 - #cnt * 8, 30, C.line)
  if count == 0 then
    center("No carts found.", W // 2, 140, C.text)
    center("Copy .p8 or .p8.png files to carts/nano8/ on the SD card", W // 2, 170, C.dim)
    center("(or to carts/), then open nano8 again.", W // 2, 188, C.dim)
    return
  end
  local first = B.top * COLS + 1
  for k = 0, COLS * ROWS - 1 do
    local i = first + k
    if i > count then break end
    local x = X0 + (k % COLS) * (CW + GAP)
    local y = Y0 + (k // COLS) * PITCH
    local selected = i == B.sel
    rectfill(x - 3, y - 3, CW + 6, CH + 6, selected and C.accent or C.line)
    local s = slot_of(i)
    if B.owner[s] ~= i or not n8.drawlabel(s, x, y, CW, CH) then
      rectfill(x, y, CW, CH, C.panel)
      local inf = B.info[i]
      if not inf then
        center("...", x + CW // 2, y + 56, C.dim)
      elseif inf.bad then
        center("unreadable", x + CW // 2, y + 56, C.err)
      else
        local t = wrap(title_of(i), 14, 3)
        for r, l in ipairs(t) do
          n8.text(l, x + (CW - #l * 8) // 2, y + 40 + (r - 1) * 16, C.text, 2)
        end
        center("no label", x + CW // 2, y + CH - 24, C.line)
      end
    end
    print(fit(title_of(i), 16), x, y + CH + 3, selected and C.text or C.dim)
  end
  -- scroll marks
  local rows = (count + COLS - 1) // COLS
  if rows > ROWS then
    local h = 2 * PITCH - 20
    local bh = max(12, h * ROWS // rows)
    local by = Y0 + (h - bh) * B.top // max(1, rows - ROWS)
    rectfill(W - 10, Y0, 3, h, C.panel)
    rectfill(W - 10, by, 3, bh, C.accent)
  end
  local c = Ui.carts[B.sel]
  local inf = B.info[B.sel]
  local who = inf and inf.author ~= "" and plain(inf.author) or ""
  print(fit(c.path .. (who ~= "" and "   " .. who or ""), 50), X0, 34, C.dim)
end

-- ---------------------------------------------------------------- the game

local function game_bg()
  local gx, gy, gw, gh = game_rect()
  rectfill(0, 0, W, gy, C.bg)
  rectfill(0, gy + gh, W, H - gy - gh, C.bg)
  rectfill(0, gy, gx, gh, C.bg)
  rectfill(gx + gw, gy, W - gx - gw, gh, C.bg)
  if gy >= 8 then
    rectfill(gx - 2, gy - 2, gw + 4, 2, C.line)
    rectfill(gx - 2, gy + gh, gw + 4, 2, C.line)
    rectfill(gx - 2, gy, 2, gh, C.line)
    rectfill(gx + gw, gy, 2, gh, C.line)
    local t = fit(title_of(B.sel), 30)
    if Vm.path ~= (Ui.carts[B.sel] or {}).path then t = fit(plain(Vm.name), 30) end
    center(t, W // 2, 20, C.dim)
    center("Start / Enter: pause      Esc: home", W // 2, gy + gh + 16, C.line)
  else
    local t = wrap(Vm.name and plain(Vm.name) or "", 14, 4)
    for k, l in ipairs(t) do print(l, 12, 16 + (k - 1) * 18, C.dim) end
    print("Start: pause", 12, H - 24, C.line)
  end
end

local function game_draw()
  local gx, gy, gw, gh = game_rect()
  if bg_dirty > 0 or ticks % 16 == 0 then
    game_bg()
    if bg_dirty > 0 then bg_dirty = bg_dirty - 1 end
  end
  n8.blit(gx, gy, gw, gh)
end

-- ---------------------------------------------------------------- pause

local P = { sel = 1, items = {} }

local function resume()
  n8.pause(false)
  Vm.pause_held = true
  Ui.go("game")
end

local function leave_game()
  Vm.close()
  Ui.go("browser")
  log("nano8: back to the list")
end
Ui.leave_game = leave_game

local function build_pause()
  local items = { { label = "Continue", act = resume } }
  for i = 1, 5 do
    local it = Vm.menu[i]
    if it then
      items[#items + 1] = {
        label = plain(it.label),
        act = function()
          if not Vm.call_item(i, 1 << 4 | 1 << 5) then resume() end
        end,
        lr = function(d) Vm.call_item(i, d < 0 and 1 or 2) end,
      }
    end
  end
  items[#items + 1] = { label = "Reset cart", act = function()
    n8.pause(false)
    Vm.start(Vm.path, Vm.param)
    Ui.go(Vm.state == "run" and "game" or "error")
  end }
  items[#items + 1] = { label = "Controls", act = function()
    Ui.controls_from = "pause"
    Ui.go("controls")
  end }
  items[#items + 1] = {
    label = function() return "Screen: " .. (Cfg.data.scale == "fill" and "full height" or "sharp 2x") end,
    act = function() Ui.toggle_scale() end, lr = function() Ui.toggle_scale() end,
  }
  items[#items + 1] = {
    label = function() return "Volume: " .. volume() end,
    lr = function(d) volume(clamp(volume() + d, 0, 10)) end,
  }
  items[#items + 1] = { label = "Back to the list", act = leave_game }
  P.items = items
  P.sel = clamp(P.sel, 1, #items)
end

function Ui.toggle_scale()
  Cfg.data.scale = Cfg.data.scale == "fill" and "crisp" or "fill"
  Cfg.changed()
  bg_dirty = 3
end

function Ui.open_pause()
  log("nano8: paused")
  n8.pause(true)
  Vm.save_now()
  P.sel = 1
  build_pause()
  Ui.go("pause")
end

local function pause_update()
  build_pause()
  local n = In.nav()
  if n.up then P.sel = P.sel - 1 end
  if n.down then P.sel = P.sel + 1 end
  P.sel = (P.sel - 1) % #P.items + 1
  local it = P.items[P.sel]
  if (n.left or n.right) and it.lr then it.lr(n.left and -1 or 1) end
  if n.ok and it.act then it.act()
  elseif n.back or n.start then resume() end
end

local function pause_draw()
  game_draw()
  local gx, gy, gw, gh = game_rect()
  local w, h = 240, 34 + #P.items * 20
  local x, y = gx + (gw - w) // 2, gy + (gh - h) // 2
  rectfill(x - 2, y - 2, w + 4, h + 4, C.accent)
  rectfill(x, y, w, h, C.panel)
  n8.text("paused", x + 12, y + 9, C.dim, 2)
  -- how the cart runs: frames a second (of the ones it wants), CPU of a frame
  local perf = fmt("%d/%d fps  cpu %d%%", Vm.fps_real or Vm.fps, Vm.fps, floor(min(Vm.cpu or 0, 9.99) * 100))
  n8.text(perf, x + w - 8 - #perf * 4, y + 12, C.dim, 1)
  for k, it in ipairs(P.items) do
    local l = type(it.label) == "function" and it.label() or it.label
    local yy = y + 30 + (k - 1) * 20
    if k == P.sel then
      rectfill(x + 6, yy - 2, w - 12, 20, C.sel)
      print(">", x + 10, yy, C.accent)
    end
    print(fit(l, 25), x + 24, yy, k == P.sel and C.text or C.dim)
    if it.lr and k == P.sel then print("<>", x + w - 30, yy, C.dim) end
  end
end

-- ---------------------------------------------------------------- controls

local K = { row = 1, col = 1, capture = nil }
local COLNAMES = { "Keyboard 1", "Keyboard 2", "Controller" }
local CX = { 150, 300, 450 }

local function binding_text(row, col)
  if col <= 2 then
    local list = Cfg.data.keys[col][row]
    local names = {}
    for _, u in ipairs(list) do names[#names + 1] = In.keyname(u) end
    return #names > 0 and concat(names, " ") or "-"
  end
  return In.padname(Cfg.data.pad[row])
end

local function controls_update()
  local cap = K.capture
  if cap then
    if time() - cap.t0 > 8 then K.capture = nil; In.settle(); return end
    if cap.phase == "release" then
      if not In.anything() then cap.phase = "wait" end
      return
    end
    if cap.col <= 2 then
      if pad() & In.PAD.b ~= 0 then K.capture = nil; In.settle(); return end
      for _, u in ipairs(keys()) do
        if u ~= 0x29 then
          local list = Cfg.data.keys[cap.col][cap.row]
          if cap.add then
            local dup = false
            for _, v in ipairs(list) do dup = dup or v == u end
            if not dup and #list < 4 then list[#list + 1] = u end
          else
            Cfg.data.keys[cap.col][cap.row] = { u }
          end
          Cfg.changed()
          K.capture = nil
          In.settle()
          return
        end
      end
    else
      if keydown(0x2A) then K.capture = nil; In.settle(); return end
      local b = pad()
      if b ~= 0 then
        local bit = b & -b
        Cfg.data.pad[cap.row] = cap.add and (Cfg.data.pad[cap.row] | bit) or bit
        Cfg.changed()
        K.capture = nil
        In.settle()
      end
    end
    return
  end
  local n = In.nav()
  if n.up then K.row = K.row - 1 end
  if n.down then K.row = K.row + 1 end
  K.row = (K.row - 1) % 8 + 1
  if K.row <= 7 then
    if n.left then K.col = K.col - 1 end
    if n.right then K.col = K.col + 1 end
    K.col = (K.col - 1) % 3 + 1
  end
  if n.ok or n.more then
    if K.row == 8 then
      Cfg.data.keys = In.default_keys()
      Cfg.data.pad = In.default_pad()
      Cfg.changed()
    else
      K.capture = { row = K.row, col = K.col, add = n.more and true or false, phase = "release", t0 = time() }
    end
  elseif n.back then
    Cfg.flush(true)
    Ui.go(Ui.controls_from == "pause" and "pause" or "browser")
  end
end

local function controls_draw()
  cls(C.bg)
  logo(40, 16)
  n8.text("controls", 150, 22, C.dim, 2)
  for c = 1, 3 do print(COLNAMES[c], CX[c], 58, C.dim) end
  rectfill(40, 76, W - 80, 1, C.line)
  for r = 1, 7 do
    local y = 86 + (r - 1) * 26
    print(In.BUTTONS[r], 48, y, C.text)
    for c = 1, 3 do
      local sel = K.row == r and K.col == c
      if sel then rectfill(CX[c] - 6, y - 4, 146, 24, C.sel) end
      local t = binding_text(r, c)
      if K.capture and K.capture.row == r and K.capture.col == c then
        t = c <= 2 and "press a key..." or "press a button..."
      end
      print(fit(t, 17), CX[c], y, sel and C.text or C.dim)
    end
  end
  local y = 86 + 7 * 26 + 6
  if K.row == 8 then rectfill(42, y - 4, 200, 24, C.sel) end
  print("Reset to defaults", 48, y, K.row == 8 and C.text or C.dim)
  local hint = K.capture and (K.capture.col <= 2 and "Press the key to use (B on a controller cancels)"
                                               or "Press the controller button (Backspace cancels)")
            or "A change   X/Tab add another   B/Backspace back"
  print(hint, 48, H - 26, C.dim)
end

-- ---------------------------------------------------------------- errors

local function error_update()
  local n = In.nav()
  if n.ok or n.back then
    Vm.close()
    Ui.go("browser")
  elseif n.more and Vm.path then
    local path = Vm.path
    Vm.close()
    if Vm.start(path) then Ui.go("game") end
  end
end

local function error_draw()
  cls(C.bg)
  logo(40, 16)
  local e = Vm.err or { title = "Error", msg = "" }
  rectfill(40, 52, W - 80, 2, C.err)
  print(fit(e.title, 70), 40, 62, C.err)
  local lines = wrap(plain(e.msg), 70, 13)
  for k, l in ipairs(lines) do print(l, 40, 86 + (k - 1) * 18, C.text) end
  print("A back to the list   X try again", 40, H - 26, C.dim)
end

-- ---------------------------------------------------------------- the frame

function Ui.update()
  ticks = ticks + 1
  local m = Ui.mode
  if m == "game" then
    if Vm.state == "run" then
      if Vm.tick() == "pause" then Ui.open_pause() end
    end
    if Vm.state == "error" then
      Ui.go("error")
    elseif Vm.state == "done" then
      local n = In.nav()
      if n.ok or n.back then leave_game() elseif n.more then play(B.sel) end
    end
  elseif m == "pause" then
    pause_update()
  elseif m == "browser" then
    browser_update()
  elseif m == "controls" then
    controls_update()
  elseif m == "error" then
    error_update()
  end
end

function Ui.draw()
  local m = Ui.mode
  if m == "game" then
    game_draw()
    if Vm.state == "done" then
      local gx, gy, gw, gh = game_rect()
      local msg = Vm.stop_msg and plain(Vm.stop_msg) or "The cart has ended"
      rectfill(0, H - 22, W, 22, C.panel)
      print(fit(msg, 40) .. "   A back   X again", 12, H - 19, C.dim)
    end
  elseif m == "pause" then
    pause_draw()
  elseif m == "browser" then
    browser_draw()
  elseif m == "controls" then
    controls_draw()
  elseif m == "error" then
    error_draw()
  end
end
