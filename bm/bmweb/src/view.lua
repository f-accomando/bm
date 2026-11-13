-- bmweb: the screen and the input. It draws what the browser state holds and
-- turns the mouse, the keyboard and the pad into browser actions. The mouse and the keyboard
-- are the main way in (a tool bar with buttons, an address box you type into, links you click,
-- the wheel, a draggable scroll bar, keys like a desktop browser); the pad does the same
-- with its buttons and an on-screen keyboard that shows only when no keyboard or mouse is used.
-- Address and search: a bar in the middle of the page, like Spotlight. It is open when the cart starts;
-- Ctrl+Space (or Y, or a click in the address box) opens it again.
-- All kernel calls go through `g` (the global table in the cart, a stub in tests/web/cart.lua).
local layout = require("layout")
local cp437 = require("cp437")

local M = {}

M.W, M.H = 640, 360
M.CW, M.LH = 6, 12
M.TOP = 22                      -- the tool bar
M.BOT = 24                      -- two text lines under the page
M.VIEW_H = M.H - M.TOP - M.BOT  -- 314
M.OX = 4                        -- left margin of the page
M.SB = 8                        -- width of the scroll bar strip
M.PAGE_W = M.W - 8 - M.SB + 4   -- 628: the page leaves the scroll bar free
M.WHEEL_STEP = 36               -- pixels for one click of the wheel (3 lines)
M.KEY_STEP = 36                 -- pixels for an arrow key

local C = {
  bar = 0x1c2430, bar_text = 0xe8e8e8, page = 0xffffff, status = 0x10161e, status_text = 0xb8c4d0,
  focus = 0xff8800, ok = 0x30c050, bad = 0xe03030, grey = 0xc8c8c8, dim = 0x707070,
  key = 0x2c3848, key_sel = 0xff8800, box_bg = 0xf4f4f4, shade = 0x000000,
  btn = 0x2c3848, btn_hot = 0x4a5a74, btn_off = 0x1c2430, addr = 0x0c1118, addr_edit = 0x000000,
  find = 0xffe060, sel = 0x2a64c8,
}
M.COLORS = C

local KEYS = {
  "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._/", ":?=&%+#@~!",
}
local ACTIONS = { "https://", ".com", "DEL", "CLR", "GO", "ESC" }

-- Tool bar: id, x, width, label, tip.
local BUTTONS = {
  { id = "back", x = 2, w = 22, label = "<", tip = "Back  (Alt+Left, right click, B)" },
  { id = "fwd", x = 26, w = 22, label = ">", tip = "Forward  (Alt+Right, R2)" },
  { id = "reload", x = 50, w = 22, label = "R", tip = "Reload  (F5, Ctrl+R, L2)" },
  { id = "home", x = 74, w = 22, label = "H", tip = "Home and bookmarks  (Ctrl+H, Select)" },
  { id = "img", x = M.W - 88, w = 26, label = "Im", tip = "Load the pictures of the page  (I, X)" },
  { id = "js", x = M.W - 60, w = 26, label = "JS", tip = "Run page scripts on or off  (F9)" },
  { id = "mark", x = M.W - 32, w = 26, label = "*", tip = "Bookmark this page  (Ctrl+D, Start)" },
}
local ADDR_X, ADDR_W = 100, M.W - 100 - 92

local V = {}
V.__index = V

-- g: the kernel functions the view uses. b: the browser state.
function M.new(g, b)
  local self = setmetatable({
    g = g, b = b, edit = nil, held = {}, shift = false, frame = 0,
    kbm = false,          -- a keyboard or a mouse is in use: no on-screen keyboard unless asked
    mx = nil, my = nil, hover = nil, hover_focus = false, drag = nil, tip = nil,
  }, V)
  if g.mouse then g.mouse(true) end
  if g.controller then
    local c = g.controller(1)
    if type(c) == "table" and c.kind == "keyboard" then self.kbm = true end
  end
  self:start_url_edit()            -- the search bar is open at the start
  return self
end

local function fit(s, n)
  if #s > n then return s:sub(1, math.max(0, n - 2)) .. ".." end
  return s
end

---------------------------------------------------------------------------
-- Text field editor (address bar, find box and form fields)
---------------------------------------------------------------------------
-- The text is typed on the keyboard. The on-screen keyboard shows when no keyboard or mouse
-- was used (a pad alone) or when F2 or a pad button asks for it.
function V:begin_edit(label, text, done, opts)
  text = text or ""
  self.edit = { label = label, text = text, pos = #text, done = done, row = 2, col = 1,
    sel = (opts and opts.select) and text ~= "" or false, osk = not self.kbm,
    spot = opts and opts.spot or false, pick = 0 }
  self.shift = false
end

function V:finish_edit(accept)
  local e = self.edit
  self.edit = nil
  if accept and e.done then e.done(e.text) end
end

local function ed_insert(e, s)
  e.pick = 0
  if e.sel then e.text, e.pos, e.sel = "", 0, false end
  if #e.text + #s > 1000 then return end
  e.text = e.text:sub(1, e.pos) .. s .. e.text:sub(e.pos + 1)
  e.pos = e.pos + #s
end

local function ed_back(e)
  e.pick = 0
  if e.sel then e.text, e.pos, e.sel = "", 0, false; return end
  if e.pos > 0 then
    e.text = e.text:sub(1, e.pos - 1) .. e.text:sub(e.pos + 1)
    e.pos = e.pos - 1
  end
end

local function ed_del(e)
  if e.sel then e.text, e.pos, e.sel = "", 0, false; return end
  e.text = e.text:sub(1, e.pos) .. e.text:sub(e.pos + 2)
end

local function osk_press(self, e)
  local r = e.row
  if r <= #KEYS then
    local ch = KEYS[r]:sub(e.col, e.col)
    if ch ~= "" then
      if self.shift and ch:match("%l") then ch = ch:upper() end
      ed_insert(e, ch)
    end
  else
    local a = ACTIONS[e.col]
    if a == "DEL" then ed_back(e)
    elseif a == "CLR" then e.text, e.pos, e.sel = "", 0, false
    elseif a == "GO" then self:finish_edit(true)
    elseif a == "ESC" then self:finish_edit(false)
    elseif a then ed_insert(e, a) end
  end
end

-- The list under the search bar: the bookmarks and the pages visited whose address has the typed
-- text in it (all of them, newest first, when nothing is typed). At most 6 rows.
local SPOT_ROWS = 6
local function spot_results(self)
  local e, b = self.edit, self.b
  local want = e.text:lower()
  local out, seen = {}, {}
  local function add(u, kind)
    if seen[u] or #out >= SPOT_ROWS then return end
    if want == "" or u:lower():find(want, 1, true) then
      seen[u] = true
      out[#out + 1] = { url = u, kind = kind }
    end
  end
  for _, m in ipairs(b.marks) do add(m, "bookmark") end
  for i = #b.hist, 1, -1 do add(b.hist[i].url, "history") end
  return out
end

local function spot_accept(self)
  local e = self.edit
  if e.pick > 0 then
    local r = spot_results(self)[e.pick]
    if r then e.text = r.url end
  end
  self:finish_edit(true)
end

local function edit_keys(self)
  local g, e = self.g, self.edit
  while self.edit do
    local k = g.keyp()
    if not k then break end
    self.kbm = true
    if k ~= "f2" then e.osk = false end
    if k == "\n" then
      if e.spot then spot_accept(self) else self:finish_edit(true) end
      return
    elseif k == "esc" then self:finish_edit(false); return
    elseif e.spot and (k == "down" or k == "up") then
      local n = #spot_results(self)
      e.pick = (e.pick + (k == "down" and 1 or -1)) % (n + 1)
    elseif k == "\b" then ed_back(e)
    elseif k == "del" then ed_del(e)
    elseif k == "left" then e.sel = false; if e.pos > 0 then e.pos = e.pos - 1 end
    elseif k == "right" then e.sel = false; if e.pos < #e.text then e.pos = e.pos + 1 end
    elseif k == "home" then e.sel = false; e.pos = 0
    elseif k == "end" then e.sel = false; e.pos = #e.text
    elseif k == "^a" then e.sel = e.text ~= ""
    elseif k == "^u" then e.text, e.pos, e.sel = e.text:sub(e.pos + 1), 0, false
    elseif k == "^k" then e.text, e.sel = e.text:sub(1, e.pos), false
    elseif k == "f2" then e.osk = not e.osk
    elseif #k == 1 and k:byte() >= 32 and k:byte() < 127 then ed_insert(e, k)
    elseif #k > 1 and k:byte(1) >= 0xC2 then ed_insert(e, k) end
  end
end

-- Geometry of the on-screen keyboard (draw and mouse use the same).
local function osk_geom()
  local cw, ch = 40, 22
  local x0 = (M.W - cw * 10) // 2
  local y0 = M.H - M.BOT - ch * (#KEYS + 1) - 6
  return x0, y0, cw, ch
end

local function osk_hit(mx, my)
  local x0, y0, cw, ch = osk_geom()
  if mx < x0 or mx >= x0 + cw * 10 or my < y0 or my >= y0 + ch * (#KEYS + 1) then return nil end
  local r = (my - y0) // ch + 1
  if r <= #KEYS then return r, (mx - x0) // cw + 1 end
  return r, (mx - x0) // (cw * 10 // #ACTIONS) + 1
end

local function edit_input(self)
  local g, e = self.g, self.edit
  edit_keys(self)
  if not self.edit then return end
  -- a pad button shows the on-screen keyboard
  local padbtn = false
  for _, n in ipairs({ "ok", "back", "x", "y", "start", "select", "up", "down", "left", "right" }) do
    if g.btnp(n) then padbtn = true end
  end
  if padbtn and not e.osk then e.osk = true; self.kbm = false; return end
  if not e.osk then return end
  local nrows = #KEYS + 1
  if g.btnp("up") then e.row = e.row > 1 and e.row - 1 or nrows end
  if g.btnp("down") then e.row = e.row < nrows and e.row + 1 or 1 end
  local ncols = e.row <= #KEYS and 10 or #ACTIONS
  if e.col > ncols then e.col = ncols end
  if g.btnp("left") then e.col = e.col > 1 and e.col - 1 or ncols end
  if g.btnp("right") then e.col = e.col < ncols and e.col + 1 or 1 end
  if g.btnp("ok") then osk_press(self, e) end
  if self.edit and g.btnp("back") then ed_back(e) end
  if self.edit and g.btnp("x") then self.shift = not self.shift end
  if self.edit and g.btnp("y") then ed_insert(e, " ") end
  if self.edit and g.btnp("start") then self:finish_edit(true) end
  if self.edit and g.btnp("select") then self:finish_edit(false) end
end

---------------------------------------------------------------------------
-- Input
---------------------------------------------------------------------------
-- Held buttons repeat: at once, then after 18 frames every 5.
local function rep(self, name)
  local g = self.g
  if g.btn(name) then
    local n = (self.held[name] or 0) + 1
    self.held[name] = n
    return n == 1 or (n > 18 and (n - 18) % 5 == 0)
  end
  self.held[name] = nil
  return false
end

-- The search bar in the middle of the page: type an address, or pick a bookmark or a page you
-- visited. Empty when the page is the home page, else it holds the address of the page.
function V:start_url_edit()
  local b = self.b
  self:begin_edit("Go to", b.mode == "page" and b.url or "", function(text)
    if text ~= "" then b:go(text) end
  end, { select = true, spot = true })
end

function V:start_find()
  local b = self.b
  self:begin_edit("Find", b.find_text or "", function(text) b:find(text, false) end, { select = true })
end

function V:start_field_edit(c)
  local b = self.b
  self:begin_edit(c.name or "Text", b.values[c.name or ""] or c.value or "", function(text)
    b:set_value(c, text)
    if c.form then
      local n = 0
      for _, o in ipairs(b.page.controls) do if o.form == c.form and o.kind == "text" then n = n + 1 end end
      if n == 1 then b:submit_form_of(c) end
    end
  end)
end

local function do_act(self, act, c)
  if act == "edit" then self:start_field_edit(c) end
end

local function activate(self)
  do_act(self, self.b:activate())
end

-- A tool bar button, from the mouse (id) or a key.
function V:press_button(id)
  local b = self.b
  if id == "back" then b:back()
  elseif id == "fwd" then b:forward()
  elseif id == "reload" then
    if b.nav or b.rend then b:stop() else b:reload() end
  elseif id == "home" then b:home()
  elseif id == "img" then b:load_images()
  elseif id == "js" then b:toggle_js()
  elseif id == "mark" then b:toggle_mark()
  elseif id == "addr" then self:start_url_edit() end
end

local function toolbar_at(mx, my)
  if my < 0 or my >= M.TOP then return nil end
  for _, bt in ipairs(BUTTONS) do
    if mx >= bt.x and mx < bt.x + bt.w then return bt.id, bt.tip end
  end
  if mx >= ADDR_X and mx < ADDR_X + ADDR_W then return "addr", "Open an address or a bookmark  (Ctrl+Space, Y)" end
  return nil
end

local function scrollbar_geom(self)
  local b = self.b
  local max = b:max_scroll()
  if max <= 0 then return nil end
  local h = math.max(16, M.VIEW_H * M.VIEW_H // (M.VIEW_H + max))
  local y = M.TOP + (M.VIEW_H - h) * b.scroll // max
  return y, h, max
end

-- Keys of the page view. May open an editor.
local function page_keys(self)
  local g, b = self.g, self.b
  while true do
    local k = g.keyp()
    if not k then break end
    self.kbm = true
    if k == "\n" then activate(self)
    elseif k == "\b" then b:back()
    elseif k == "^ " or k == "^space" or k == "^@" or k == "\0" then self:start_url_edit()
    elseif k == "^r" or k == "f5" then b:reload()
    elseif k == "^d" then b:toggle_mark()
    elseif k == "^h" then b:home()
    elseif k == "^f" or k == "/" then self:start_find()
    elseif k == "f3" or k == "^g" then b:find(b.find_text, true)
    elseif k == "f9" or k == "^j" then b:toggle_js()
    elseif k == "\t" then b:move_focus(1)
    elseif k == "pgdn" or k == " " then b:scroll_by(M.VIEW_H - 24)
    elseif k == "pgup" then b:scroll_by(-(M.VIEW_H - 24))
    elseif k == "home" then b:scroll_to(0)
    elseif k == "end" then b:scroll_to(1e9)
    elseif k == "down" then b:scroll_by(M.KEY_STEP)
    elseif k == "up" then b:scroll_by(-M.KEY_STEP)
    elseif k == "right" then b:move_focus(1)
    elseif k == "left" then b:move_focus(-1)
    elseif k == "esc" then b:stop()
    elseif k == "i" then b:load_images()
    elseif k == "j" then b:scroll_by(M.KEY_STEP)
    elseif k == "k" then b:scroll_by(-M.KEY_STEP)
    elseif k == "b" then b:back()
    elseif k == "f" then b:forward() end
    if self.edit then return end
  end
end

local function mouse_input(self)
  local g, b = self.g, self.b
  if not g.mouse then return end
  local mx, my, btns, wheel = g.mouse()
  if not mx then return end
  local moved = (mx ~= self.mx or my ~= self.my)
  if moved and self.mx ~= nil then self.kbm = true end
  self.mx, self.my = mx, my
  local left = g.mousep and g.mousep(0)
  local right = g.mousep and g.mousep(1)
  local middle = g.mousep and g.mousep(2)

  -- scroll bar drag
  if self.drag then
    if btns and (btns & 1) ~= 0 then
      local _, h, max = scrollbar_geom(self)
      if h then
        local room = M.VIEW_H - h
        if room > 0 then b:scroll_to(self.drag.s0 + (my - self.drag.y0) * max // room) end
      end
    else
      self.drag = nil
    end
    return
  end
  if wheel and wheel ~= 0 then b:scroll_by(-wheel * M.WHEEL_STEP) end
  if right then b:back(); return end

  if my < M.TOP then
    local id, tip = toolbar_at(mx, my)
    self.hover, self.tip = id, tip
    if left and id then self:press_button(id) end
    return
  end
  self.hover, self.tip = nil, nil
  if my < M.TOP + M.VIEW_H then
    -- scroll bar strip
    if mx >= M.W - M.SB - 2 then
      local y, h = scrollbar_geom(self)
      if y and left then
        if my >= y and my < y + h then self.drag = { y0 = my, s0 = b.scroll }
        elseif my < y then b:scroll_by(-(M.VIEW_H - 24))
        else b:scroll_by(M.VIEW_H - 24) end
      end
      return
    end
    if not b.page then return end
    local px, py = mx - M.OX, my - M.TOP + b.scroll
    if left or middle then
      do_act(self, b:click_page(px, py))
    elseif moved then
      local t = b:pick(px, py)
      if t then self.hover_focus = true
      elseif self.hover_focus then b.focus = nil; self.hover_focus = false end
    end
  end
end

-- The search bar: box, input row and result rows.
local function spot_geom()
  return 90, 78, M.W - 180, 34, 18      -- x, y, width, input height, row height
end

local function mouse_edit(self)
  local g, e = self.g, self.edit
  if not g.mouse then return end
  local mx, my = g.mouse()
  if not mx then return end
  if mx ~= self.mx or my ~= self.my then
    if self.mx ~= nil then self.kbm = true end
    self.mx, self.my = mx, my
  end
  if e.spot then
    local bx, by, bw, ih, rh = spot_geom()
    local res = spot_results(self)
    if mx >= bx and mx < bx + bw and my >= by + ih and my < by + ih + rh * #res then
      e.pick = (my - by - ih) // rh + 1                 -- hover
    end
    if not (g.mousep and g.mousep(0)) then return end
    if e.pick > 0 and mx >= bx and mx < bx + bw and my >= by + ih and my < by + ih + rh * #res then
      spot_accept(self)
      return
    end
    if mx >= bx and mx < bx + bw and my >= by and my < by + ih then
      local tx = bx + 12 + (#e.label + 2) * M.CW
      e.sel = false
      e.pos = math.max(0, math.min(#e.text, (mx - tx + M.CW // 2) // M.CW))
      return
    end
    if e.osk then
      local r, c = osk_hit(mx, my)
      if r then e.row, e.col = r, c; osk_press(self, e); return end
    end
    if my >= M.TOP then
      self:finish_edit(false)
      return
    end
  end
  if not (g.mousep and g.mousep(0)) then return end
  if e.osk then
    local r, c = osk_hit(mx, my)
    if r then
      e.row, e.col = r, c
      osk_press(self, e)
      return
    end
  end
  if my < M.TOP then
    local id = toolbar_at(mx, my)
    if id == "addr" and e.spot then
      return
    elseif id == "addr" then
      -- a click in the box puts the cursor there
      local tx = ADDR_X + 4 + (#e.label + 2) * M.CW
      e.sel = false
      e.pos = math.max(0, math.min(#e.text, (mx - tx + M.CW // 2) // M.CW))
    elseif id then
      self:finish_edit(false)
      self:press_button(id)
    end
  elseif not e.osk then
    self:finish_edit(false)
  end
end

function V:input()
  local g, b = self.g, self.b
  if self.edit then
    edit_input(self)
    if self.edit then mouse_edit(self) end
    return
  end
  if b.mode == "download" then
    if g.btnp("ok") then b:save_download() end
    if g.btnp("back") or g.btnp("start") then b:discard() end
    while true do
      local k = g.keyp()
      if not k then break end
      self.kbm = true
      if k == "\n" or k == "y" then b:save_download()
      elseif k == "esc" or k == "n" or k == "\b" then b:discard() end
    end
    if g.mouse and g.mousep and g.mousep(0) then
      local mx, my = g.mouse()
      if mx then
        local bx, by, _, bh = self:download_box()
        if my >= by + bh - 26 and my < by + bh - 6 then
          if mx >= bx + 8 and mx < bx + 108 then b:save_download()
          elseif mx >= bx + 116 and mx < bx + 216 then b:discard() end
        end
      end
    end
    return
  end
  local scroll_step = 4
  if g.btn("down") then b:scroll_by(scroll_step) end
  if g.btn("up") then b:scroll_by(-scroll_step) end
  if rep(self, "right") then b:move_focus(1) end
  if rep(self, "left") then b:move_focus(-1) end
  if g.btnp("r1") then b:scroll_by(M.VIEW_H - 24) end
  if g.btnp("l1") then b:scroll_by(-(M.VIEW_H - 24)) end
  if g.btnp("ok") then activate(self) end
  if g.btnp("back") then b:back() end
  if g.btnp("x") then b:load_images() end
  if g.btnp("y") then self:start_url_edit() end
  if g.btnp("start") then b:toggle_mark() end
  if g.btnp("select") then b:home() end
  if g.btnp("l2") then b:reload() end
  if g.btnp("r2") then b:forward() end
  page_keys(self)
  if self.edit then return end
  mouse_input(self)
end

---------------------------------------------------------------------------
-- Drawing
---------------------------------------------------------------------------
local function text(g, s, x, y, c) g.print(cp437.conv(s), x, y, c) end

local function draw_run(self, line, r, oy)
  local g, b = self.g, self.b
  local page = b.page
  local x = M.OX + r.x
  local y = oy + layout.run_y(line, r)
  local kind = r.kind
  if b.found and b.found.run == r then g.rectfill(x, y, r.w, r.h, C.find) end
  if not kind then
    local c = r.color or 0
    local s = r.text
    text(g, s, x, y, c)
    if r.bold then text(g, s, x + 1, y, c) end
    if r.ul then g.line(x, y + r.h - 1, x + r.w - 1, y + r.h - 1, c) end
  elseif kind == "img" then
    local im = r.img and page.images[r.img]
    if im and im.tex then
      local t = im.tex
      g.sspr(t.x, t.y, t.w, t.h, x, y)
      if r.link then g.rect(x, y, t.w, t.h, r.color or 0) end
    else
      g.rectfill(x, y, r.w, r.h, C.box_bg)
      g.rect(x, y, r.w, r.h, im and im.failed and C.bad or C.grey)
      if r.text and r.text ~= "" then text(g, r.text, x + 2, y + 2, C.dim) end
    end
  elseif kind == "svg" or kind == "box" then
    g.rectfill(x, y, r.w, r.h, C.box_bg)
    g.rect(x, y, r.w, r.h, C.grey)
    if r.text and r.text ~= "" then text(g, r.text, x + 2, y + 2, r.link and r.color or C.dim) end
  elseif kind == "ctl" then
    local c = r.ref
    local label = r.text
    if c then
      if c.kind == "text" then
        local v = b.values[c.name or ""] or c.value or ""
        local size = c.size or 20
        if #v > size then v = v:sub(1, size) else v = v .. string.rep("_", size - #v) end
        label = "[" .. v .. "]"
      elseif c.kind == "checkbox" or c.kind == "radio" then
        label = c.checked and "[x]" or "[ ]"
      elseif c.kind == "select" then
        local o = c.options[c.selected]
        label = "[" .. (o and o.label or "") .. " v]"
      end
    end
    g.rectfill(x, y, r.w, r.h, C.box_bg)
    text(g, label, x, y, 0x000000)
  end
end

local function draw_page(self)
  local g, b = self.g, self.b
  local page = b.page
  g.clip(0, M.TOP, M.W, M.VIEW_H)
  g.rectfill(0, M.TOP, M.W, M.VIEW_H, C.page)
  if b.mode == "image" and b.img then
    local im = b.img
    g.sspr(im.x, im.y, im.w, im.h, (M.W - im.w) // 2, M.TOP + 4)
  end
  if page then
    local oy = M.TOP - b.scroll
    for _, rc in ipairs(page.rects) do
      local y = oy + rc.y
      if y < M.TOP + M.VIEW_H and y + rc.h > M.TOP then
        if rc.kind == "border" then
          g.rect(M.OX + rc.x, y, rc.w, rc.h, rc.color)
        elseif rc.color then
          g.rectfill(M.OX + rc.x, y, rc.w, rc.h, rc.color)
        end
        local bd = rc.bd
        if bd then
          local x0, x1, y1 = M.OX + rc.x, M.OX + rc.x + rc.w - 1, y + math.max(rc.h, 1) - 1
          if bd:find("t", 1, true) then g.line(x0, y, x1, y, rc.bdc) end
          if bd:find("b", 1, true) then g.line(x0, y1, x1, y1, rc.bdc) end
          if bd:find("l", 1, true) then g.line(x0, y, x0, y1, rc.bdc) end
          if bd:find("r", 1, true) then g.line(x1, y, x1, y1, rc.bdc) end
        end
      end
    end
    local first, last = layout.visible(page, b.scroll, b.scroll + M.VIEW_H)
    local ly = b.mode == "image" and b.img and (b.img.h + 8) or 0
    for i = first, last do
      local line = page.lines[i]
      for _, r in ipairs(line.runs) do draw_run(self, line, r, oy + ly) end
    end
    local t = b:focused()
    if t then
      local boxes = t.kind == "link" and page.links[t.id].boxes or { t.box }
      for _, bx in ipairs(boxes) do
        g.rect(M.OX + bx.x - 1, oy + ly + bx.y - 1, bx.w + 2, bx.h + 2, C.focus)
      end
    end
  end
  g.clip()
  -- scroll bar
  local y, h = scrollbar_geom(self)
  if y then
    g.rectfill(M.W - M.SB, M.TOP, M.SB, M.VIEW_H, 0xe8e8e8)
    g.rectfill(M.W - M.SB + 1, y + 1, M.SB - 2, h - 2, self.drag and C.focus or C.dim)
  end
end

local function draw_bar(self)
  local g, b = self.g, self.b
  g.rectfill(0, 0, M.W, M.TOP, C.bar)
  for _, bt in ipairs(BUTTONS) do
    local label = bt.label
    local on = true
    if bt.id == "back" then on = b:can_back()
    elseif bt.id == "fwd" then on = b:can_forward()
    elseif bt.id == "reload" then
      if b.nav or b.rend or b.sub then label = "X" end
      on = b.mode ~= "home" or label == "X"
    elseif bt.id == "mark" then label = b:marked() and "*" or "+"
    elseif bt.id == "js" then on = b.js_on end
    local bg = (self.hover == bt.id and on) and C.btn_hot or (on and C.btn or C.btn_off)
    g.rectfill(bt.x, 2, bt.w, M.TOP - 4, bg)
    local col = on and 0xffffff or C.dim
    if bt.id == "mark" and b:marked() then col = C.focus end
    if bt.id == "js" and on then col = C.ok end
    text(g, label, bt.x + (bt.w - #label * M.CW) // 2, 5, col)
  end
  local cols = (ADDR_W - 8) // M.CW
  local ed = self.edit
  if ed and ed.spot then ed = nil end
  g.rectfill(ADDR_X, 2, ADDR_W, M.TOP - 4, ed and C.addr_edit or C.addr)
  if ed then
    g.rect(ADDR_X, 2, ADDR_W, M.TOP - 4, C.focus)
    local label = ed.label .. ": "
    local avail = cols - #label
    local s, off = ed.text, 0
    if #s > avail - 1 then
      off = math.max(0, ed.pos - (avail - 2))
      s = s:sub(off + 1, off + avail - 1)
    end
    local tx = ADDR_X + 4 + #label * M.CW
    text(g, label, ADDR_X + 4, 5, C.dim)
    if ed.sel and #s > 0 then g.rectfill(tx, 4, #s * M.CW, 14, C.sel) end
    text(g, s, tx, 5, 0xffffff)
    if (self.frame // 20) % 2 == 0 then
      local cx = tx + (ed.pos - off) * M.CW
      g.line(cx, 4, cx, 16, 0xffffff)
    end
    return
  end
  g.rect(ADDR_X, 2, ADDR_W, M.TOP - 4, self.hover == "addr" and C.focus or C.btn)
  local lock = b.secure and "https" or (b.url:sub(1, 7) == "http://" and "NOT SECURE" or "")
  text(g, lock, ADDR_X + 4, 5, b.secure and C.ok or C.bad)
  local ux = ADDR_X + 4 + (#lock + (#lock > 0 and 1 or 0)) * M.CW
  local u = b.url ~= "" and b.url or "bm Web - type an address"
  if b.secure then u = u:gsub("^https://", "") end
  text(g, fit(u, (ADDR_X + ADDR_W - ux) // M.CW - 1), ux, 5, b.url ~= "" and C.bar_text or C.dim)
end

local function draw_status(self)
  local g, b = self.g, self.b
  local y = M.H - M.BOT
  g.rectfill(0, y, M.W, M.BOT, C.status)
  local cols = M.W // M.CW - 1
  local t = b:focused()
  local s = b.status
  if self.tip then s = self.tip
  elseif t and t.kind == "link" then s = b.page.links[t.id].href
  elseif t and t.kind == "ctl" then s = "field: " .. tostring(b.page.controls[t.id].kind) end
  local mem = g.stat and (" " .. tostring(g.stat(0) or 0) .. " KB") or ""
  text(g, fit(s or "", cols - #mem), 4, y + 1, C.status_text)
  text(g, mem, M.W - 4 - #mem * M.CW, y + 1, C.dim)
  local hint
  local e = self.edit
  if e and e.osk then hint = "A key  B delete  X shift  Y space  Start go  Select cancel"
  elseif e and e.spot then hint = "Enter open  Up/Down choose  Esc close  F2 on-screen keys"
  elseif e then hint = "Enter go  Esc cancel  Ctrl+A select all  F2 on-screen keys"
  elseif b.mode == "download" then
    hint = self.kbm and "Enter save   Esc discard" or "A save   B discard"
  elseif self.kbm then
    hint = "Click open  Wheel scroll  Right-click back  Ctrl+Space search  Ctrl+F find  F5 reload  F9 scripts"
  else hint = "A open  B back  X pictures  Y address  Start mark  Select home  L1/R1 page" end
  text(g, fit(hint, cols), 4, y + 12, C.dim)
end

function V:download_box()
  local w, h = 440, 110
  return (M.W - w) // 2, (M.H - h) // 2, w, h
end

local function draw_download(self)
  local g, b = self.g, self.b
  local d = b.dl
  if not d then return end
  local x, y, w, h = self:download_box()
  g.rectfill(x, y, w, h, 0x202830)
  g.rect(x, y, w, h, C.focus)
  local cols = w // M.CW - 2
  text(g, "Save this file?", x + 8, y + 8, 0xffffff)
  text(g, fit((d.model and "/bmm/" or "bm/web/") .. d.name .. (d.model and "  (3D model)" or ""), cols), x + 8, y + 26, C.focus)
  text(g, fit(string.format("%d bytes  %s", d.size, d.type ~= "" and d.type or "unknown type"), cols), x + 8, y + 40, C.status_text)
  text(g, fit(d.url, cols), x + 8, y + 54, C.status_text)
  if d.saving then
    text(g, "saving...", x + 8, y + h - 24, 0xffffff)
  else
    g.rectfill(x + 8, y + h - 26, 100, 20, C.ok)
    text(g, self.kbm and "Save (Enter)" or "Save (A)", x + 14, y + h - 22, 0x000000)
    g.rectfill(x + 116, y + h - 26, 100, 20, C.bad)
    text(g, self.kbm and "Drop (Esc)" or "Drop (B)", x + 122, y + h - 22, 0xffffff)
  end
end

local function draw_osk(self)
  local g, e = self.g, self.edit
  local x0, y0, cw, ch = osk_geom()
  g.rectfill(x0 - 6, y0 - 6, cw * 10 + 12, ch * (#KEYS + 1) + 12, 0x101820)
  for r = 1, #KEYS do
    for c = 1, 10 do
      local ch1 = KEYS[r]:sub(c, c)
      if self.shift and ch1:match("%l") then ch1 = ch1:upper() end
      local sel = e.row == r and e.col == c
      g.rectfill(x0 + (c - 1) * cw + 1, y0 + (r - 1) * ch + 1, cw - 2, ch - 2, sel and C.key_sel or C.key)
      text(g, ch1, x0 + (c - 1) * cw + cw // 2 - 3, y0 + (r - 1) * ch + 5, 0xffffff)
    end
  end
  local r = #KEYS + 1
  local aw = cw * 10 // #ACTIONS
  for c, a in ipairs(ACTIONS) do
    local sel = e.row == r and e.col == c
    g.rectfill(x0 + (c - 1) * aw + 1, y0 + (r - 1) * ch + 1, aw - 2, ch - 2, sel and C.key_sel or C.key)
    text(g, a, x0 + (c - 1) * aw + (aw - #a * M.CW) // 2, y0 + (r - 1) * ch + 5, 0xffffff)
  end
end

local function draw_spot(self)
  local g, e = self.g, self.edit
  local bx, by, bw, ih, rh = spot_geom()
  local res = spot_results(self)
  local h = ih + rh * #res + 8
  g.rectfill(bx + 4, by + 4, bw, h, 0x000000)              -- shadow
  g.rectfill(bx, by, bw, h, 0x1c2430)
  g.rect(bx, by, bw, h, C.focus)
  -- input row
  g.rectfill(bx + 6, by + 6, bw - 12, ih - 12, 0x000000)
  local label = e.label .. ": "
  local tx = bx + 12 + #label * M.CW
  local cols = (bw - 24) // M.CW - #label
  local s, off = e.text, 0
  if #s > cols - 1 then
    off = math.max(0, e.pos - (cols - 2))
    s = s:sub(off + 1, off + cols - 1)
  end
  text(g, label, bx + 12, by + 11, C.dim)
  if e.text == "" then text(g, "address, or a bookmark", tx, by + 11, C.dim) end
  if e.sel and #s > 0 then g.rectfill(tx, by + 9, #s * M.CW, 14, C.sel) end
  text(g, s, tx, by + 11, 0xffffff)
  if (self.frame // 20) % 2 == 0 then
    local cx = tx + (e.pos - off) * M.CW
    g.line(cx, by + 9, cx, by + 23, 0xffffff)
  end
  -- result rows
  for i, r in ipairs(res) do
    local y = by + ih + (i - 1) * rh
    if i == e.pick then g.rectfill(bx + 4, y, bw - 8, rh, C.key_sel) end
    local tag = r.kind == "bookmark" and "*" or " "
    text(g, tag, bx + 10, y + 3, i == e.pick and 0x000000 or C.focus)
    text(g, fit(r.url, (bw - 40) // M.CW), bx + 26, y + 3, i == e.pick and 0x000000 or C.bar_text)
  end
end

function V:draw()
  local g, b = self.g, self.b
  self.frame = self.frame + 1
  if g.font then g.font("6x12") end
  g.cls(0)
  draw_page(self)
  draw_bar(self)
  draw_status(self)
  if b.mode == "download" then draw_download(self) end
  if self.edit and self.edit.spot then draw_spot(self) end
  if self.edit and self.edit.osk then draw_osk(self) end
end

function V:update()
  if self.g.font then self.g.font("6x12") end
  self:input()
  self.b:update()
end

return M
