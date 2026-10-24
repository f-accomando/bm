-- bmweb: the screen and the input. It draws what the browser state holds and
-- turns buttons, keys and the mouse into browser actions. All kernel calls go
-- through `g` (the global table in the cart, a stub in tests/web/cart.lua).
local layout = require("layout")
local cp437 = require("cp437")

local M = {}

M.W, M.H = 640, 360
M.CW, M.LH = 6, 12
M.TOP = 18
M.BOT = 24                      -- two text lines under the page
M.VIEW_H = M.H - M.TOP - M.BOT  -- 318
M.OX = 4                        -- left margin of the page
M.PAGE_W = M.W - 8

local C = {
  bar = 0x1c2430, bar_text = 0xe8e8e8, page = 0xffffff, status = 0x10161e, status_text = 0xb8c4d0,
  focus = 0xff8800, ok = 0x30c050, bad = 0xe03030, grey = 0xc8c8c8, dim = 0x707070,
  key = 0x2c3848, key_sel = 0xff8800, box_bg = 0xf4f4f4, shade = 0x000000,
}
M.COLORS = C

local KEYS = {
  "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._/", ":?=&%+#@~!",
}
local ACTIONS = { "https://", ".com", "DEL", "CLR", "GO", "ESC" }

local V = {}
V.__index = V

-- g: the kernel functions the view uses. b: the browser state.
function M.new(g, b)
  local self = setmetatable({ g = g, b = b, edit = nil, held = {}, shift = false, frame = 0 }, V)
  if g.mouse then g.mouse(true) end
  return self
end

---------------------------------------------------------------------------
-- Text field editor (address bar and form fields), with an on-screen keyboard
---------------------------------------------------------------------------
function V:begin_edit(label, text, done)
  self.edit = { label = label, text = text or "", done = done, row = 2, col = 1 }
  self.shift = false
end

local function fit(s, n)
  if #s > n then return s:sub(1, math.max(0, n - 2)) .. ".." end
  return s
end

function V:finish_edit(accept)
  local e = self.edit
  self.edit = nil
  if accept and e.done then e.done(e.text) end
end

local function osk_press(self, e)
  local r = e.row
  if r <= #KEYS then
    local ch = KEYS[r]:sub(e.col, e.col)
    if ch ~= "" then
      if self.shift and ch:match("%l") then ch = ch:upper() end
      e.text = e.text .. ch
    end
  else
    local a = ACTIONS[e.col]
    if a == "DEL" then e.text = e.text:sub(1, -2)
    elseif a == "CLR" then e.text = ""
    elseif a == "GO" then self:finish_edit(true)
    elseif a == "ESC" then self:finish_edit(false)
    elseif a then e.text = e.text .. a end
  end
end

local function edit_input(self)
  local g, e = self.g, self.edit
  while true do
    local k = g.keyp()
    if not k then break end
    if k == "\n" then self:finish_edit(true); return
    elseif k == "esc" then self:finish_edit(false); return
    elseif k == "\b" then e.text = e.text:sub(1, -2)
    elseif #k == 1 and k:byte() >= 32 and k:byte() < 127 and #e.text < 1000 then e.text = e.text .. k end
  end
  local nrows = #KEYS + 1
  if g.btnp("up") then e.row = e.row > 1 and e.row - 1 or nrows end
  if g.btnp("down") then e.row = e.row < nrows and e.row + 1 or 1 end
  local ncols = e.row <= #KEYS and 10 or #ACTIONS
  if e.col > ncols then e.col = ncols end
  if g.btnp("left") then e.col = e.col > 1 and e.col - 1 or ncols end
  if g.btnp("right") then e.col = e.col < ncols and e.col + 1 or 1 end
  if g.btnp("ok") then osk_press(self, e) end
  if self.edit and g.btnp("back") then e.text = e.text:sub(1, -2) end
  if self.edit and g.btnp("x") then self.shift = not self.shift end
  if self.edit and g.btnp("y") then e.text = e.text .. " " end
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

function V:start_url_edit()
  local b = self.b
  self:begin_edit("Address", b.mode == "page" and b.url or "https://", function(text) b:go(text) end)
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

local function activate(self)
  local act, c = self.b:activate()
  if act == "edit" then self:start_field_edit(c) end
end

function V:input()
  local g, b = self.g, self.b
  if self.edit then edit_input(self); return end
  local scroll_step = 4
  if b.mode == "download" then
    if g.btnp("ok") then b:save_download() end
    if g.btnp("back") or g.btnp("start") then b:discard() end
    while g.keyp() do end
    return
  end
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
  while true do
    local k = g.keyp()
    if not k then break end
    if k == "\n" then activate(self)
    elseif k == "\b" then b:back()
    elseif k == "^l" or k == "f6" then self:start_url_edit()
    elseif k == "^r" or k == "f5" then b:reload()
    elseif k == "^d" then b:toggle_mark()
    elseif k == "^h" then b:home()
    elseif k == "\t" then b:move_focus(1)
    elseif k == "pgdn" then b:scroll_by(M.VIEW_H - 24)
    elseif k == "pgup" then b:scroll_by(-(M.VIEW_H - 24))
    elseif k == "home" then b:scroll_to(0)
    elseif k == "end" then b:scroll_to(1e9)
    elseif k == "esc" then b:stop()
    elseif k == "i" then b:load_images() end
  end
  if g.mouse then
    local mx, my, btns, wheel = g.mouse()
    if wheel and wheel ~= 0 then b:scroll_by(-wheel * 36) end
    if g.mousep and g.mousep(0) then
      if my < M.TOP then
        self:start_url_edit()
      elseif my < M.TOP + M.VIEW_H and b.page then
        if b:pick(mx - M.OX, my - M.TOP + b.scroll) then activate(self) end
      end
    end
  end
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
    if r.text and r.text ~= "" then text(g, r.text, x + 2, y + 2, C.dim) end
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
      if y < M.TOP + M.VIEW_H and y + rc.h > M.TOP then g.rectfill(M.OX + rc.x, y, rc.w, rc.h, rc.color) end
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
  local max = b:max_scroll()
  if max > 0 then
    local h = math.max(8, M.VIEW_H * M.VIEW_H // (M.VIEW_H + max))
    local y = M.TOP + (M.VIEW_H - h) * b.scroll // max
    g.rectfill(M.W - 3, y, 3, h, C.dim)
  end
end

local function draw_bar(self)
  local g, b = self.g, self.b
  g.rectfill(0, 0, M.W, M.TOP, C.bar)
  local cols = (M.W - 30) // M.CW
  if self.edit then
    local e = self.edit
    local s = e.text
    if #s > cols - 10 then s = ".." .. s:sub(-(cols - 12)) end
    text(g, e.label .. ": " .. s .. ((self.frame // 20) % 2 == 0 and "_" or " "), 4, 3, 0xffffff)
    return
  end
  local lock = b.secure and "https" or (b.url:sub(1, 7) == "http://" and "NOT SECURE" or "")
  text(g, lock, 4, 3, b.secure and C.ok or C.bad)
  local ux = 4 + (#lock + 1) * M.CW
  local mark = b:marked() and "*" or " "
  local u = b.url ~= "" and b.url or "bm Web"
  text(g, fit(u, (M.W - ux) // M.CW - 3), ux, 3, C.bar_text)
  text(g, mark, M.W - 12, 3, C.focus)
end

local function draw_status(self)
  local g, b = self.g, self.b
  local y = M.H - M.BOT
  g.rectfill(0, y, M.W, M.BOT, C.status)
  local cols = M.W // M.CW - 1
  local t = b:focused()
  local s = b.status
  if t and t.kind == "link" then s = b.page.links[t.id].href
  elseif t and t.kind == "ctl" then s = "field: " .. tostring(b.page.controls[t.id].kind) end
  local mem = g.stat and (" " .. tostring(g.stat(0) or 0) .. " KB") or ""
  text(g, fit(s or "", cols - #mem), 4, y + 1, C.status_text)
  text(g, mem, M.W - 4 - #mem * M.CW, y + 1, C.dim)
  local hint
  if self.edit then hint = "A key  B delete  X shift  Y space  Start go  Select cancel"
  elseif b.mode == "download" then hint = "A save   B discard"
  else hint = "A open  B back  X pictures  Y address  Start mark  Select home  L1/R1 page" end
  text(g, fit(hint, cols), 4, y + 12, C.dim)
end

local function draw_download(self)
  local g, b = self.g, self.b
  local d = b.dl
  if not d then return end
  local w, h = 440, 110
  local x, y = (M.W - w) // 2, (M.H - h) // 2
  g.rectfill(x, y, w, h, 0x202830)
  g.rect(x, y, w, h, C.focus)
  local cols = w // M.CW - 2
  text(g, "Save this file?", x + 8, y + 8, 0xffffff)
  text(g, fit("bm/web/" .. d.name, cols), x + 8, y + 26, C.focus)
  text(g, fit(string.format("%d bytes  %s", d.size, d.type ~= "" and d.type or "unknown type"), cols), x + 8, y + 40, C.status_text)
  text(g, fit(d.url, cols), x + 8, y + 54, C.status_text)
  text(g, fit(d.saving and "saving..." or "A save  B discard", cols), x + 8, y + 86, 0xffffff)
end

local function draw_osk(self)
  local g, e = self.g, self.edit
  local cw, ch = 40, 22
  local x0 = (M.W - cw * 10) // 2
  local y0 = M.H - M.BOT - ch * (#KEYS + 1) - 6
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

function V:draw()
  local g, b = self.g, self.b
  self.frame = self.frame + 1
  if g.font then g.font("6x12") end
  g.cls(0)
  draw_page(self)
  draw_bar(self)
  draw_status(self)
  if b.mode == "download" then draw_download(self) end
  if self.edit then draw_osk(self) end
end

function V:update()
  if self.g.font then self.g.font("6x12") end
  self:input()
  self.b:update()
end

return M
