-- bmweb: block and inline layout on a fixed grid. Pure Lua, no draw calls.
-- The font has a fixed cell (default 6x12), so a text run is chars * cw wide.
-- The result is a page: a list of lines (y, h, runs), the link table with hit
-- boxes, the image table (placeholder boxes), controls, forms and rects
-- (backgrounds, rules). A cart draws only the lines in the visible range.
local css = require("css")
local url = require("url")

local M = {}

M.LIMITS = {
  max_lines = 5000, -- lines per page; the rest is cut ("page cut")
  max_images = 16,  -- images that get an id (and may be fetched)
  max_link_text = 64,
}

local function ulen(s) return utf8.len(s) or #s end

local function dim(v)
  local n = v and tonumber(v:match("^%s*(%d+)"))
  if n and n >= 1 and n <= 4096 then return n end
  return nil
end

local IGNORE = { title = true, style = true, head = true, option = true }
local TABLE_PARTS = { thead = true, tbody = true, tfoot = true }

local L = {}
L.__index = L

-- opts: width (px), cw, lh, url (page URL), rules (css rules), allow_http,
-- max_lines, max_images.
function M.new(opts)
  local o = setmetatable({
    cw = opts.cw or 6, lh = opts.lh or 12, width = opts.width or 640,
    rules = opts.rules, base = opts.url, urlopts = { allow_http = opts.allow_http },
    max_lines = opts.max_lines or M.LIMITS.max_lines,
    max_images = opts.max_images or M.LIMITS.max_images,
    lines = {}, rects = {}, links = {}, images = {}, controls = {}, forms = {},
    y = 0, mpend = 0, sp = false, nlines = 0, maxh = 0, refused = 0,
    cut = false, full = false, link = nil, form = nil, marker = nil,
  }, L)
  o.out = o.lines
  o.ctx = { x = 0, w = o.width, align = "left", pre = false }
  o.ln = { items = {}, cx = 0, h = 0 }
  return o
end

function L:set_base(href)
  local b = self.base and url.resolve(self.base, href, self.urlopts)
  if b then self.base = b end
end

-- Resolve a link target. Returns nil for a refused or bad reference.
function L:resolve(ref)
  local r
  if self.base then r = url.resolve(self.base, ref, self.urlopts) else r = url.check(ref, self.urlopts) end
  if not r then self.refused = self.refused + 1 end
  return r
end

---------------------------------------------------------------------------
-- Lines
---------------------------------------------------------------------------
function L:settle()
  self.y = self.y + self.mpend
  self.mpend = 0
end

-- Put an item (text run, image box, control) on the current line.
-- item: kind, w, h, plus the fields of a run.
function L:place(item)
  local ln, avail = self.ln, self.ctx.w
  local items = ln.items
  local n = #items
  local gap = (self.sp and n > 0) and self.cw or 0
  if n > 0 and ln.cx + gap + item.w > avail then
    self:flush_line()
    ln, items, n, gap = self.ln, self.ln.items, 0, 0
  end
  local last = items[n]
  if last and not item.kind and not last.kind and last.color == item.color
    and last.bold == item.bold and last.italic == item.italic and last.ul == item.ul
    and last.link == item.link and last.bg == item.bg then
    last.text = last.text .. (gap > 0 and " " or "") .. item.text
    last.w = last.w + gap + item.w
    ln.cx = last.x + last.w
  else
    item.x = ln.cx + gap
    ln.cx = item.x + item.w
    items[n + 1] = item
    if item.h > ln.h then ln.h = item.h end
  end
  self.sp = false
end

function L:emit_line(line)
  if self.nlines >= self.max_lines then self.cut = true; self.full = true; return end
  local out = self.out
  out[#out + 1] = line
  self.nlines = self.nlines + 1
  if line.h > self.maxh then self.maxh = line.h end
end

function L:flush_line(force)
  local ln = self.ln
  local items = ln.items
  local marker = self.marker
  if #items == 0 and not force then self.sp = false; return end
  self:settle()
  local lh = self.lh
  local h = ln.h
  if h < lh then h = lh end
  local ctx = self.ctx
  local off = 0
  if ctx.align == "center" then off = (ctx.w - ln.cx) // 2
  elseif ctx.align == "right" then off = ctx.w - ln.cx end
  if off < 0 then off = 0 end
  local y = self.y
  local runs = {}
  local boxes = {}
  if marker then
    self.marker = nil
    local w = ulen(marker) * self.cw
    local x = ctx.x - w - self.cw
    if x < 0 then x = 0 end
    runs[1] = { text = marker, x = x, w = w, h = lh, color = 0 }
  end
  for i = 1, #items do
    local it = items[i]
    it.x = ctx.x + off + it.x
    runs[#runs + 1] = it
    local li = it.link
    if li then
      local b = boxes[li]
      if b then b.w = it.x + it.w - b.x
      else
        b = { x = it.x, y = y, w = it.w, h = h }
        boxes[li] = b
        local l = self.links[li].boxes
        l[#l + 1] = b
      end
    end
    local ref = it.ref
    if ref then ref.box = { x = it.x, y = y + h - it.h, w = it.w, h = it.h }; it.ref = nil end
  end
  self.ln = { items = {}, cx = 0, h = 0 }
  self.sp = false
  self:emit_line({ y = y, h = h, runs = runs })
  self.y = y + h
end

---------------------------------------------------------------------------
-- Blocks
---------------------------------------------------------------------------
function L:open_block(node, st)
  self:flush_line()
  local ctx = self.ctx
  local pw = ctx.w
  local px = css.px
  local ml, mr = px(st.ml, pw), px(st.mr, pw)
  local pl, pr = px(st.pl, pw), px(st.pr, pw)
  local pt = px(st.pt, pw)
  self.mpend = math.max(self.mpend, px(st.mt, pw))
  local blk = { pb = px(st.pb, pw), mb = px(st.mb, pw) }
  local x = ctx.x + ml + pl
  local w = pw - ml - mr - pl - pr
  local minw = self.cw * 4
  if w < minw then w = minw end
  if st.bg or pt > 0 then self:settle() end
  if st.bg and not self.full then
    local r = { kind = "bg", x = ctx.x + ml, y = self.y, w = pw - ml - mr, h = 0, color = st.bg }
    self.rects[#self.rects + 1] = r
    blk.rect = r
  end
  self.y = self.y + pt
  self.ctx = { x = x, w = w, align = st.align, pre = ctx.pre, parent = ctx, blk = blk }
end

function L:close_block()
  self:flush_line()
  local ctx = self.ctx
  local blk = ctx.blk
  if not blk then return end
  if blk.rect or blk.pb > 0 then self:settle() end
  self.y = self.y + blk.pb
  if blk.rect then blk.rect.h = self.y - blk.rect.y end
  self.mpend = math.max(self.mpend, blk.mb)
  self.ctx = ctx.parent
end

---------------------------------------------------------------------------
-- Text
---------------------------------------------------------------------------
local function textitem(self, text, st, w)
  -- A text run has no "kind" and no "y" field, and the style flags only when
  -- set: a smaller table (the run y is line.y + line.h - run.h, see run_y).
  local it = { text = (text:gsub("\u{A0}", " ")), w = w, h = self.lh, color = st.color, link = self.link }
  if st.bold then it.bold = true end
  if st.italic then it.italic = true end
  if st.ul then it.ul = true end
  return it
end

function L:word(w, st)
  local cw, avail = self.cw, self.ctx.w
  local n = ulen(w)
  local li = self.link and self.links[self.link]
  if li and #li.text < M.LIMITS.max_link_text then
    li.text = li.text .. (li.text ~= "" and " " or "") .. w
  end
  if n * cw <= avail then
    self:place(textitem(self, w, st, n * cw))
    return
  end
  -- A word longer than a line: cut it into pieces of one line.
  local maxc = math.max(1, avail // cw)
  if #self.ln.items > 0 then self:flush_line() end
  local pos = 1
  while pos <= #w and not self.full do
    local stop = utf8.offset(w, maxc + 1, pos) or (#w + 1)
    local piece = w:sub(pos, stop - 1)
    self:place(textitem(self, piece, st, ulen(piece) * cw))
    pos = stop
    if pos <= #w then self:flush_line() end
  end
end

function L:pre_text(s, st)
  local ctx = self.ctx
  if ctx.pre_first then
    ctx.pre_first = false
    s = s:gsub("^\r?\n", "")
  end
  s = s:gsub("\r", ""):gsub("\t", "    ")
  local cw, maxc = self.cw, math.max(1, self.ctx.w // self.cw)
  local pos, n = 1, #s
  while pos <= n and not self.full do
    local nl = s:find("\n", pos, true)
    local piece = s:sub(pos, (nl or n + 1) - 1)
    while piece ~= "" do
      local stop = utf8.offset(piece, maxc + 1) or (#piece + 1)
      local part = piece:sub(1, stop - 1)
      self:place(textitem(self, part, st, ulen(part) * cw))
      piece = piece:sub(stop)
      if piece ~= "" then self:flush_line() end
    end
    if nl then self:flush_line(true) end
    pos = (nl or n) + 1
  end
end

function L:text(s, st)
  if self.full or st.hidden then return end
  if self.ctx.pre then return self:pre_text(s, st) end
  if s:find("^[ \t\r\n\f]") then self.sp = true end
  local first = true
  for w in s:gmatch("[^ \t\r\n\f]+") do
    if not first then self.sp = true end
    first = false
    self:word(w, st)
  end
  if s:find("[ \t\r\n\f]$") then self.sp = true end
end

local function textof(node, out)
  out = out or {}
  if node.tag == "#text" then out[#out + 1] = node.text
  else for _, k in ipairs(node.kids) do textof(k, out) end end
  return out
end

---------------------------------------------------------------------------
-- Images, svg boxes and form controls
---------------------------------------------------------------------------
function L:box_item(w, h, label, st)
  local cw = self.cw
  local maxc = math.max(0, w // cw)
  if ulen(label) > maxc then
    local off = utf8.offset(label, maxc + 1)
    if off then label = label:sub(1, off - 1) end
  end
  return { kind = "box", w = w, h = h, text = label, link = self.link, color = st.color }
end

function L:image(node, st)
  local a = node.attrs
  local alt = a.alt or ""
  local avail = self.ctx.w
  local w, h = dim(a.width), dim(a.height)
  if not w then
    w = alt == "" and 48 or math.max(24, math.min(avail, (ulen(alt) + 2) * self.cw))
    if not h then h = self.lh * 2 end
  elseif not h then
    h = math.max(self.lh, w * 3 // 4)
  end
  if w > avail then h = math.max(self.lh, h * avail // w); w = avail end
  if h > 4000 then h = 4000 end
  local item = self:box_item(w, h, alt, st)
  item.kind = "img"
  local src = a.src and a.src ~= "" and self:resolve(a.src)
  if src and #self.images < self.max_images then
    local id = #self.images + 1
    local rec = { src = src, alt = alt, w = w, h = h }
    self.images[id] = rec
    item.img = id
    item.ref = rec
  end
  self:place(item)
end

function L:svg(node, st)
  local label = node.attrs["aria-label"] or node.attrs.title or ""
  local w = dim(node.attrs.width) or 24
  local h = dim(node.attrs.height) or 24
  if w > self.ctx.w then w = self.ctx.w end
  local item = self:box_item(w, h, label, st)
  item.kind = "svg"
  self:place(item)
end

local function pad_field(s, size)
  local n = ulen(s)
  if n > size then return s:sub(1, (utf8.offset(s, size + 1) or #s + 1) - 1) end
  return s .. string.rep("_", size - n)
end

function L:register_control(rec)
  local id = #self.controls + 1
  rec.form = self.form
  self.controls[id] = rec
  return id, rec
end

function L:control(node, st)
  local a, tag = node.attrs, node.tag
  local label, rec
  if tag == "input" then
    local ty = (a.type or "text"):lower()
    if ty == "hidden" then
      self:register_control({ kind = "hidden", name = a.name, value = a.value or "" })
      return
    elseif ty == "password" then
      label = "[password refused]"
    elseif ty == "submit" or ty == "button" or ty == "reset" or ty == "image" then
      label = "[" .. (a.value or (ty == "reset" and "Reset" or "Submit")) .. "]"
      rec = { kind = (ty == "submit" or ty == "image") and "submit" or "button", name = a.name, value = a.value or "" }
    elseif ty == "checkbox" or ty == "radio" then
      local on = a.checked ~= nil
      label = on and "[x]" or "[ ]"
      rec = { kind = ty, name = a.name, value = a.value or "on", checked = on }
    elseif ty == "file" then
      label = "[file not supported]"
    else
      local size = math.min(dim(a.size) or 20, math.max(1, self.ctx.w // self.cw - 2))
      local shown = a.value or a.placeholder or ""
      label = "[" .. pad_field(shown, size) .. "]"
      rec = { kind = "text", name = a.name, value = a.value or "", placeholder = a.placeholder, size = size }
    end
  elseif tag == "button" then
    local ty = (a.type or "submit"):lower()
    local t = table.concat(textof(node)):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "")
    label = "[" .. t .. "]"
    rec = { kind = ty == "submit" and "submit" or "button", name = a.name, value = a.value or "" }
  elseif tag == "select" then
    local opts, sel = {}, nil
    local function walk(n)
      for _, k in ipairs(n.kids) do
        if k.tag == "option" then
          local txt = table.concat(textof(k)):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "")
          opts[#opts + 1] = { value = k.attrs.value or txt, label = txt }
          if k.attrs.selected ~= nil then sel = #opts end
        elseif k.tag ~= "#text" then walk(k) end
      end
    end
    walk(node)
    sel = sel or 1
    local cur = opts[sel]
    label = "[" .. (cur and cur.label or "") .. " v]"
    rec = { kind = "select", name = a.name, options = opts, selected = sel, value = cur and cur.value or "" }
  else -- textarea
    local txt = table.concat(textof(node))
    local w = math.min(self.ctx.w, 30 * self.cw)
    local item = self:box_item(w, 3 * self.lh, txt:gsub("%s+", " "), st)
    item.kind = "ctl"
    local id, r = self:register_control({ kind = "textarea", name = a.name, value = txt })
    item.ctl, item.ref = id, r
    self:place(item)
    return
  end
  local item = textitem(self, label, st, ulen(label) * self.cw)
  item.kind = "ctl"
  item.link = nil
  if rec then
    local id, r = self:register_control(rec)
    item.ctl, item.ref = id, r
  end
  self:place(item)
end

---------------------------------------------------------------------------
-- Tables
---------------------------------------------------------------------------
-- Approximate min and max content width of a subtree, in px.
local BLOCKISH = { div = true, p = true, ul = true, ol = true, li = true, table = true, tr = true,
  h1 = true, h2 = true, h3 = true, h4 = true, h5 = true, h6 = true, pre = true, blockquote = true,
  dl = true, dt = true, dd = true, hr = true, section = true, article = true, header = true,
  footer = true, nav = true, main = true, form = true, br = true }

function L:measure(node)
  local cw = self.cw
  local acc = { cur = 0, max = 0, min = 0 }
  local function brk() if acc.cur > acc.max then acc.max = acc.cur end; acc.cur = 0 end
  local function walk(n)
    if n.tag == "#text" then
      for w in n.text:gmatch("[^ \t\r\n\f]+") do
        local wd = ulen(w) * cw
        if wd > acc.min then acc.min = wd end
        acc.cur = acc.cur + (acc.cur > 0 and cw or 0) + wd
      end
      return
    end
    local a = n.attrs
    if IGNORE[n.tag] or (a and (a.hidden or (" " .. (a.class or "") .. " "):find(" hidden ", 1, true))) then return end
    if n.tag == "img" or n.tag == "svg" then
      local w = dim(a.width) or 48
      if w > acc.min then acc.min = w end
      acc.cur = acc.cur + w
      return
    end
    local b = BLOCKISH[n.tag]
    if b then brk() end
    for _, k in ipairs(n.kids) do walk(k) end
    if b then brk() end
  end
  walk(node)
  brk()
  return math.min(acc.min, acc.max), acc.max
end

-- Rows of cells: list of { {node=, span=}, ... }
local function table_rows(node)
  local rows = {}
  local function add_row(tr)
    local cells = {}
    for _, k in ipairs(tr.kids) do
      if k.tag == "td" or k.tag == "th" then
        cells[#cells + 1] = { node = k, span = math.max(1, math.min(dim(k.attrs.colspan) or 1, 32)) }
      elseif k.tag ~= "#text" or k.text:find("%S") then
        cells[#cells + 1] = { node = { tag = "td", attrs = {}, kids = { k }, parent = tr }, span = 1 }
      end
    end
    if #cells > 0 then rows[#rows + 1] = cells end
  end
  local function scan(n)
    for _, k in ipairs(n.kids) do
      if k.tag == "tr" then add_row(k)
      elseif TABLE_PARTS[k.tag] then scan(k)
      elseif k.tag ~= "#text" or k.text:find("%S") then
        if k.tag ~= "caption" then
          add_row({ kids = { k }, tag = "tr" })
        end
      end
    end
  end
  scan(node)
  return rows
end

local function cell_children(self, cell, cst)
  for _, k in ipairs(cell.node.kids) do
    if self.full then return end
    self:node(k, cst)
  end
end

function L:table(node, st)
  self:flush_line()
  self:settle()
  local cw = self.cw
  local rows = table_rows(node)
  local caption
  for _, k in ipairs(node.kids) do if k.tag == "caption" then caption = k end end
  if caption then self:node(caption, st); self:flush_line() end
  if #rows == 0 then return end
  local ctx = self.ctx
  local avail = ctx.w
  local border = (dim(node.attrs.border) or 0) > 0 and 1 or 0
  local cp = 3                       -- cell padding on each side
  local over = 2 * cp + border       -- width a cell adds around its content
  local ncol = 0
  for _, r in ipairs(rows) do
    local c = 0
    for _, cell in ipairs(r) do c = c + cell.span end
    if c > ncol then ncol = c end
  end
  local cmin, cmax = {}, {}
  for c = 1, ncol do cmin[c] = 0; cmax[c] = 0 end
  for _, r in ipairs(rows) do
    local c = 1
    for _, cell in ipairs(r) do
      local mn, mx = self:measure(cell.node)
      cell.mn, cell.mx = mn, mx
      if cell.span == 1 then
        if mn > cmin[c] then cmin[c] = mn end
        if mx > cmax[c] then cmax[c] = mx end
      end
      c = c + cell.span
    end
  end
  -- A cell that spans columns widens the last column it covers if needed.
  for _, r in ipairs(rows) do
    local c = 1
    for _, cell in ipairs(r) do
      if cell.span > 1 then
        local have = 0
        for k = c, c + cell.span - 1 do have = have + (cmax[k] or 0) + over end
        local need = cell.mx + over
        if need > have and cmax[c + cell.span - 1] then cmax[c + cell.span - 1] = cmax[c + cell.span - 1] + (need - have) end
        local haven = 0
        for k = c, c + cell.span - 1 do haven = haven + (cmin[k] or 0) + over end
        local needn = cell.mn + over
        if needn > haven and cmin[c + cell.span - 1] then cmin[c + cell.span - 1] = cmin[c + cell.span - 1] + (needn - haven) end
      end
      c = c + cell.span
    end
  end
  local sum_max, sum_min = 0, 0
  for c = 1, ncol do sum_max = sum_max + cmax[c] + over; sum_min = sum_min + cmin[c] + over end
  local widths, stack = {}, false
  local want = node.attrs.width
  if sum_max <= avail then
    for c = 1, ncol do widths[c] = cmax[c] + over end
    local full = want and (want == "100%" or (dim(want) or 0) >= avail)
    if full and sum_max < avail then
      local extra = avail - sum_max
      local rem = extra
      for c = 1, ncol do
        local add = (c == ncol) and rem or (extra * (cmax[c] + over) // sum_max)
        widths[c] = widths[c] + add; rem = rem - add
      end
    end
  elseif sum_min <= avail then
    local spare, grow = avail - sum_min, sum_max - sum_min
    local rem = spare
    for c = 1, ncol do
      local add = (c == ncol) and rem or (spare * (cmax[c] - cmin[c]) // grow)
      widths[c] = cmin[c] + over + add; rem = rem - add
    end
  else
    stack = true -- too wide even at min widths: the cells of a row stack
  end
  local tst = st
  local saved_out = self.out
  for _, r in ipairs(rows) do
    local row_top = self.y
    if stack then
      for _, cell in ipairs(r) do
        local cst = css.compute(cell.node, self.rules, tst)
        self:open_block(cell.node, cst)
        cell_children(self, cell, cst)
        self:close_block()
        self:settle()
      end
    else
      local tmp = {}
      self.out = tmp
      local x = ctx.x
      local row_bottom = row_top
      local c = 1
      local boxes = {}
      for _, cell in ipairs(r) do
        local w = 0
        for k = c, math.min(ncol, c + cell.span - 1) do w = w + widths[k] end
        local cst = css.compute(cell.node, self.rules, tst)
        self.ctx = { x = x + cp + border, w = math.max(cw * 2, w - over), align = cst.align, pre = false,
          parent = ctx, blk = nil }
        self.y = row_top + 2
        self.mpend = 0
        cell_children(self, cell, cst)
        self:flush_line()
        if self.y + 2 > row_bottom then row_bottom = self.y + 2 end
        boxes[#boxes + 1] = { x = x, w = w, bg = cst.bg }
        x = x + w
        c = c + cell.span
      end
      self.ctx = ctx
      self.out = saved_out
      for i, l in ipairs(tmp) do l.o = i end
      table.sort(tmp, function(p, q) if p.y ~= q.y then return p.y < q.y end return p.o < q.o end)
      for _, l in ipairs(tmp) do l.o = nil; saved_out[#saved_out + 1] = l end
      for _, b in ipairs(boxes) do
        if b.bg then self.rects[#self.rects + 1] = { kind = "bg", x = b.x, y = row_top, w = b.w, h = row_bottom - row_top, color = b.bg } end
        if border > 0 then self.rects[#self.rects + 1] = { kind = "border", x = b.x, y = row_top, w = b.w, h = row_bottom - row_top, color = 0x808080 } end
      end
      self.y = row_bottom
    end
  end
  self.mpend = math.max(self.mpend, 6)
end

---------------------------------------------------------------------------
-- Nodes
---------------------------------------------------------------------------
function L:children(node, st)
  local kids = node.kids
  for i = 1, #kids do
    if self.full then return end
    self:node(kids[i], st)
  end
end

-- Lay out one node (and its subtree) as a child of the current block.
-- ps is the computed style of the parent.
function L:node(node, ps)
  if self.full then return end
  local tag = node.tag
  if tag == "#text" then return self:text(node.text, ps) end
  if IGNORE[tag] then return end
  local st = css.compute(node, self.rules, ps)
  if st.display == "none" then return end
  if tag == "a" and not node.attrs.href then st.color, st.ul = ps.color, ps.ul end
  if st.hidden then return end
  if tag == "br" then self:flush_line(true); return end
  if tag == "hr" then
    self:flush_line()
    self.mpend = math.max(self.mpend, css.px(st.mt, 0))
    self:settle()
    self.rects[#self.rects + 1] = { kind = "hr", x = self.ctx.x, y = self.y, w = self.ctx.w, h = 1, color = 0x808080 }
    self.y = self.y + 1
    self.mpend = css.px(st.mb, 0)
    return
  end
  if tag == "img" then return self:image(node, st) end
  if tag == "svg" then return self:svg(node, st) end
  if tag == "input" or tag == "button" or tag == "select" or tag == "textarea" then
    return self:control(node, st)
  end
  if tag == "table" then
    self:open_block(node, st)
    self:table(node, st)
    self:close_block()
    return
  end
  if tag == "form" then
    local saved = self.form
    local id = #self.forms + 1
    local action = node.attrs.action
    local act
    if action and action ~= "" then act = self:resolve(action)
    else act = self.base and url.strip_fragment(self.base) end
    self.forms[id] = { action = act, method = (node.attrs.method or "get"):lower() }
    if not saved then self.form = id end
    self:open_block(node, st)
    self:children(node, st)
    self:close_block()
    self.form = saved
    return
  end
  if tag == "a" then
    local saved = self.link
    local href = node.attrs.href
    if href then
      local target = self:resolve(href)
      if target then
        local id = #self.links + 1
        self.links[id] = { href = target, text = "", boxes = {} }
        self.link = id
      else
        -- a refused target is shown as plain text, not as a link
        st.color, st.ul = ps.color, ps.ul
      end
    end
    if st.display == "block" then
      self:open_block(node, st); self:children(node, st); self:close_block()
    else
      self:children(node, st)
    end
    self.link = saved
    return
  end
  if st.display == "block" then
    self:open_block(node, st)
    if tag == "pre" then
      self.ctx.pre = true
      self.ctx.pre_first = true
    elseif tag == "ul" or tag == "ol" then
      self.ctx.list = { ordered = tag == "ol", n = tonumber(node.attrs.start) or 1 }
    elseif tag == "li" then
      local list = self.ctx.parent and self.ctx.parent.list
      if list then
        if list.ordered then self.marker = list.n .. "."; list.n = list.n + 1 else self.marker = "*" end
      else self.marker = "*" end
    end
    self:children(node, st)
    if self.marker then self:flush_line(true) end
    self:close_block()
    return
  end
  self:children(node, st)
end

function L:finish()
  self:flush_line()
  return {
    lines = self.lines, rects = self.rects, links = self.links, images = self.images,
    controls = self.controls, forms = self.forms,
    w = self.width, h = self.y, cw = self.cw, lh = self.lh, maxh = self.maxh,
    nlines = self.nlines, cut = self.cut, refused = self.refused, base = self.base,
  }
end

---------------------------------------------------------------------------
-- Queries on a finished page
---------------------------------------------------------------------------
-- The y of a run: runs sit at the bottom of their line.
function M.run_y(line, run) return line.y + line.h - run.h end

-- Index range of the lines that touch [y0, y1).
function M.visible(page, y0, y1)
  local lines = page.lines
  local n = #lines
  local lo, hi = 1, n + 1
  local key = y0 - page.maxh
  while lo < hi do
    local mid = (lo + hi) // 2
    if lines[mid].y < key then lo = mid + 1 else hi = mid end
  end
  local first = lo
  local last = first - 1
  for i = first, n do
    if lines[i].y >= y1 then break end
    last = i
  end
  return first, last
end

-- What is at (x, y)? Returns "link", id | "img", id | "ctl", id | nil.
function M.hit(page, x, y)
  local function inside(b) return b and x >= b.x and x < b.x + b.w and y >= b.y and y < b.y + b.h end
  for id, c in ipairs(page.controls) do if inside(c.box) then return "ctl", id end end
  for id, l in ipairs(page.links) do
    for _, b in ipairs(l.boxes) do if inside(b) then return "link", id end end
  end
  for id, im in ipairs(page.images) do if inside(im.box) then return "img", id end end
  return nil
end

-- The URL a GET form sends. values: optional map name -> text typed by the
-- user. pressed: id of the submit control that was activated (optional).
function M.form_url(page, form_id, values, pressed, opts)
  local f = page.forms[form_id]
  if not f then return nil, "no such form" end
  if f.method ~= "get" then return nil, "POST not supported" end
  if not f.action then return nil, "no form action" end
  values = values or {}
  local fields = {}
  for id, c in ipairs(page.controls) do
    if c.form == form_id and c.name and c.name ~= "" then
      local k = c.kind
      if k == "hidden" or k == "text" or k == "textarea" or k == "select" then
        local v = values[c.name]
        if v == nil then v = c.value end
        fields[#fields + 1] = { c.name, v }
      elseif (k == "checkbox" or k == "radio") and c.checked then
        fields[#fields + 1] = { c.name, c.value }
      elseif k == "submit" and id == pressed then
        fields[#fields + 1] = { c.name, c.value }
      end
    end
  end
  return url.form_get(f.action, fields, opts)
end

return M
