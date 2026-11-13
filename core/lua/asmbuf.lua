-- asmbuf.lua: a text buffer for the editors of bm cartridges that edit assembly (Code tab of bm SNES SDK).
-- No bm API and no drawing: the PC tests run it (tests/snes/code_test.lua). Columns are 0-based (cx = the
-- number of characters before the cursor), lines are 1-based.
--
--   local B = loadfile("bm/shared/asmbuf.lua")()
--   local b = B.new(text)
--   B.key(b, k)          one key of keyp(): moves, types, deletes, undo, copy, paste; true when the buffer took it
--   B.text(b)            the whole text
--   B.labels(b)          { { name, line }, ... } the labels of the text (not the local ones with @)
--   B.find(b, text, back) find the next place (from the cursor), move there and select it; false when none
--   B.goto_label(b, name) move to the label; false when there is none

local B = {}
local TAB = 4

function B.new(text)
  local lines = {}
  text = (text or ""):gsub("\r\n", "\n"):gsub("\r", "\n"):gsub("\t", "    ")
  for l in (text .. "\n"):gmatch("(.-)\n") do lines[#lines + 1] = l end
  if #lines > 1 and lines[#lines] == "" and text:sub(-1) == "\n" then lines[#lines] = nil end
  if #lines == 0 then lines[1] = "" end
  return { lines = lines, cx = 0, cy = 1, top = 1, left = 0, undo = {}, redo = {}, kind = nil, mark = nil,
           dirty = false, want = 0 }
end

function B.text(b) return table.concat(b.lines, "\n") .. "\n" end

local function clone(l) local t = {}; for i = 1, #l do t[i] = l[i] end; return t end

local function snapshot(b, kind)
  if kind and b.kind == kind then return end          -- the same kind of key: one undo step
  b.kind = kind
  b.undo[#b.undo + 1] = { lines = clone(b.lines), cx = b.cx, cy = b.cy }
  if #b.undo > 200 then table.remove(b.undo, 1) end
  b.redo = {}
end

local function restore(b, from, to)
  local s = table.remove(from)
  if not s then return false end
  to[#to + 1] = { lines = clone(b.lines), cx = b.cx, cy = b.cy }
  b.lines, b.cx, b.cy = s.lines, s.cx, s.cy
  b.kind, b.mark, b.dirty = nil, nil, true
  return true
end

local function clamp(b)
  b.cy = math.max(1, math.min(#b.lines, b.cy))
  b.cx = math.max(0, math.min(#b.lines[b.cy], b.cx))
end
B.clamp = clamp

local function indent_of(l) return #l:match("^ *") end

-- the selection: from the mark to the cursor. Returns y0, x0, y1, x1 (in order) or nil.
function B.sel_range(b)
  local m = b.mark
  if not m or (m.cy == b.cy and m.cx == b.cx) then return nil end
  if m.cy < b.cy or (m.cy == b.cy and m.cx < b.cx) then return m.cy, m.cx, b.cy, b.cx end
  return b.cy, b.cx, m.cy, m.cx
end

function B.sel_text(b)
  local y0, x0, y1, x1 = B.sel_range(b)
  if not y0 then return nil end
  if y0 == y1 then return b.lines[y0]:sub(x0 + 1, x1) end
  local t = { b.lines[y0]:sub(x0 + 1) }
  for y = y0 + 1, y1 - 1 do t[#t + 1] = b.lines[y] end
  t[#t + 1] = b.lines[y1]:sub(1, x1)
  return table.concat(t, "\n")
end

local function delete_sel(b)
  local y0, x0, y1, x1 = B.sel_range(b)
  b.mark = nil
  if not y0 then return false end
  local head, tail = b.lines[y0]:sub(1, x0), b.lines[y1]:sub(x1 + 1)
  for _ = y0 + 1, y1 do table.remove(b.lines, y0 + 1) end
  b.lines[y0] = head .. tail
  b.cy, b.cx = y0, x0
  return true
end

function B.insert_text(b, s)
  s = s:gsub("\r", "")
  local first, rest = s:match("^([^\n]*)\n?(.*)$")
  local l = b.lines[b.cy]
  if not s:find("\n", 1, true) then
    b.lines[b.cy] = l:sub(1, b.cx) .. s .. l:sub(b.cx + 1)
    b.cx = b.cx + #s
    return
  end
  local tail = l:sub(b.cx + 1)
  b.lines[b.cy] = l:sub(1, b.cx) .. first
  local y = b.cy
  local parts = {}
  for p in (rest .. "\n"):gmatch("(.-)\n") do parts[#parts + 1] = p end
  if rest:sub(-1) == "\n" then parts[#parts] = nil end     -- text that ends with a newline
  for i, p in ipairs(parts) do
    y = y + 1
    table.insert(b.lines, y, p)
    if i == #parts then b.cx = #p; b.lines[y] = p .. tail end
  end
  if #parts == 0 then b.lines[b.cy] = b.lines[b.cy] .. tail; b.cx = #b.lines[b.cy] - #tail
  else b.cy = y end
end

-- insert a block of lines (a snippet) at the start of the cursor line (or at the cursor when it is on a text)
function B.insert_block(b, s)
  snapshot(b)
  b.kind = nil
  b.mark = nil
  if not s:find("\n$") then s = s .. "\n" end
  if b.lines[b.cy]:match("%S") then
    b.cx = #b.lines[b.cy]
    B.insert_text(b, "\n" .. s:gsub("\n$", ""))
  else
    b.cx = 0
    B.insert_text(b, s)
  end
  b.dirty = true
  clamp(b)
end

B.clipboard = nil

-- One key. `rows` is the number of lines a page moves.
function B.key(b, k, rows)
  rows = rows or 20
  local l = b.lines[b.cy]
  local moved = true
  if k == "up" then b.cy = b.cy - 1; b.cx = math.min(b.want, #(b.lines[b.cy] or ""))
  elseif k == "down" then b.cy = b.cy + 1; b.cx = math.min(b.want, #(b.lines[b.cy] or ""))
  elseif k == "left" then
    if b.cx > 0 then b.cx = b.cx - 1 elseif b.cy > 1 then b.cy = b.cy - 1; b.cx = #b.lines[b.cy] end
    b.want = b.cx
  elseif k == "right" then
    if b.cx < #l then b.cx = b.cx + 1 elseif b.cy < #b.lines then b.cy = b.cy + 1; b.cx = 0 end
    b.want = b.cx
  elseif k == "home" then b.cx = (b.cx == indent_of(l)) and 0 or indent_of(l); b.want = b.cx
  elseif k == "end" then b.cx = #l; b.want = b.cx
  elseif k == "pgup" then b.cy = b.cy - rows; b.cx = math.min(b.want, #(b.lines[math.max(1, b.cy)] or ""))
  elseif k == "pgdn" then b.cy = b.cy + rows; b.cx = math.min(b.want, #(b.lines[math.min(#b.lines, b.cy)] or ""))
  elseif k == "^home" then b.cy, b.cx, b.want = 1, 0, 0
  elseif k == "^end" then b.cy = #b.lines; b.cx = #b.lines[b.cy]; b.want = b.cx
  else moved = false end
  if moved then
    b.kind = nil
    b.mark = nil
    clamp(b)
    return true
  end
  if k == "\n" then
    snapshot(b)
    delete_sel(b)
    l = b.lines[b.cy]
    local before = l:sub(1, b.cx)
    local ind = string.rep(" ", indent_of(l))
    if before:gsub("%s*;.*$", ""):match(":%s*$") then ind = string.rep(" ", TAB) end   -- after a label
    local after = l:sub(b.cx + 1):gsub("^ +", "")
    b.lines[b.cy] = before
    table.insert(b.lines, b.cy + 1, ind .. after)
    b.cy, b.cx = b.cy + 1, #ind
  elseif k == "\b" then
    if b.mark and B.sel_range(b) then snapshot(b); delete_sel(b)
    else
      b.mark = nil
      snapshot(b, "bs")
      if b.cx > 0 then
        local n = (b.cx == indent_of(l) and b.cx % TAB == 0) and TAB or 1     -- a whole indent step
        b.lines[b.cy] = l:sub(1, b.cx - n) .. l:sub(b.cx + 1)
        b.cx = b.cx - n
      elseif b.cy > 1 then
        b.cx = #b.lines[b.cy - 1]
        b.lines[b.cy - 1] = b.lines[b.cy - 1] .. l
        table.remove(b.lines, b.cy)
        b.cy = b.cy - 1
      end
    end
  elseif k == "del" then
    if b.mark and B.sel_range(b) then snapshot(b); delete_sel(b)
    else
      b.mark = nil
      snapshot(b, "del")
      if b.cx < #l then b.lines[b.cy] = l:sub(1, b.cx) .. l:sub(b.cx + 2)
      elseif b.cy < #b.lines then b.lines[b.cy] = l .. b.lines[b.cy + 1]; table.remove(b.lines, b.cy + 1) end
    end
  elseif k == "\t" then
    snapshot(b)
    local sp = TAB - b.cx % TAB
    delete_sel(b)
    l = b.lines[b.cy]
    b.lines[b.cy] = l:sub(1, b.cx) .. string.rep(" ", sp) .. l:sub(b.cx + 1)
    b.cx = b.cx + sp
  elseif k == "^u" then                                   -- unindent the line
    snapshot(b)
    local n = math.min(TAB, indent_of(l))
    b.lines[b.cy] = l:sub(n + 1)
    b.cx = math.max(0, b.cx - n)
  elseif k == "^z" then if not restore(b, b.undo, b.redo) then return false end
  elseif k == "^y" then if not restore(b, b.redo, b.undo) then return false end
  elseif k == "^q" then                                   -- the mark of a selection on / off (the mouse drags one too)
    if b.mark then b.mark = nil else b.mark = { cy = b.cy, cx = b.cx } end
    return true
  elseif k == "^c" then
    B.clipboard = B.sel_text(b) or (l .. "\n")
    b.mark = nil
    return true
  elseif k == "^x" then
    local t = B.sel_text(b)
    snapshot(b)
    if t then B.clipboard = t; delete_sel(b)
    else B.clipboard = l .. "\n"; B.delete_line(b) end
  elseif k == "^v" then
    if not B.clipboard then return true end
    snapshot(b)
    delete_sel(b)
    if B.clipboard:sub(-1) == "\n" then b.cx = 0 end      -- whole lines go above this one
    B.insert_text(b, B.clipboard)
  elseif k == "^k" then
    snapshot(b)
    B.clipboard = l .. "\n"
    B.delete_line(b)
  elseif k == "^d" then
    snapshot(b)
    table.insert(b.lines, b.cy + 1, l)
    b.cy = b.cy + 1
  elseif #k == 1 and k:byte() >= 32 then
    snapshot(b, "ins")
    delete_sel(b)
    l = b.lines[b.cy]
    b.lines[b.cy] = l:sub(1, b.cx) .. k .. l:sub(b.cx + 1)
    b.cx = b.cx + 1
  else
    return false
  end
  b.dirty = true
  b.want = b.cx
  clamp(b)
  return true
end

function B.delete_line(b)
  table.remove(b.lines, b.cy)
  if #b.lines == 0 then b.lines[1] = "" end
  b.cx, b.mark = 0, nil
  clamp(b)
end

----------------------------------------------------------------- labels and search

function B.labels(b)
  local out = {}
  for i, l in ipairs(b.lines) do
    local s = l:gsub(";.*$", "")
    local pos = 1
    while true do
      local ws, name, e = s:match("^(%s*)([%a_][%w_]*):()", pos)
      if not name then break end
      out[#out + 1] = { name = name, line = i }
      pos = e
    end
    local eq = s:match("^%s*([%a_][%w_]*)%s*=[^=]")
    if eq then out[#out + 1] = { name = eq, line = i } end
  end
  return out
end

function B.goto_label(b, name)
  for _, d in ipairs(B.labels(b)) do
    if d.name == name then
      b.cy, b.cx, b.mark, b.kind = d.line, 0, nil, nil
      b.want = 0
      return true
    end
  end
  return false
end

function B.goto_line(b, n)
  b.cy, b.cx, b.mark, b.kind, b.want = math.max(1, math.min(#b.lines, n)), 0, nil, nil, 0
end

-- find the text (not case sensitive); from the cursor onward (or backward), around the end. Selects the match.
function B.find(b, text, back)
  if text == "" then return false end
  local needle = text:lower()
  local n = #b.lines
  local function hit(y, from, to_back)
    local l = b.lines[y]:lower()
    if to_back then
      local last
      local s = 1
      while true do
        local a = l:find(needle, s, true)
        if not a or a - 1 + #needle > from then break end
        last = a; s = a + 1
      end
      return last
    end
    return l:find(needle, from + 1, true)
  end
  local y, x = b.cy, b.cx
  if back then
    local y0, x0 = B.sel_range(b)
    if y0 then y, x = y0, x0 end
  end
  for step = 0, n do
    local yy = back and ((y - 1 - step) % n) + 1 or ((y - 1 + step) % n) + 1
    local a
    if back then
      a = hit(yy, step == 0 and x or #b.lines[yy], true)
    else
      a = hit(yy, step == 0 and x or 0, false)
    end
    if a then
      -- the cursor goes after the match, the mark before it
      b.cy, b.cx, b.mark = yy, a - 1 + #needle, { cy = yy, cx = a - 1 }
      b.kind = nil
      return true
    end
  end
  return false
end

-- the cursor at a place (a click): line y, column x, cut to the text
function B.set_cursor(b, y, x, keep_mark)
  b.cy, b.cx = y, x
  clamp(b)
  b.want, b.kind = b.cx, nil
  if not keep_mark then b.mark = nil end
end

-- keep the cursor on the screen: rows and cols are the size of the view
function B.scroll(b, rows, cols)
  if b.cy < b.top then b.top = b.cy end
  if b.cy > b.top + rows - 1 then b.top = b.cy - rows + 1 end
  if b.cx < b.left then b.left = b.cx end
  if b.cx > b.left + cols - 1 then b.left = b.cx - cols + 1 end
  b.top = math.max(1, b.top)
  b.left = math.max(0, b.left)
end

return B
