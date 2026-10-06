-- bm Write: the word processor of bm (2026-10-06). Documents with styles
-- (title, two headings, body, quote, bullet and numbered lists), bold,
-- italic and underline, four alignments, laid out on A4 pages as they print:
-- the PDF it exports has the same lines on the same pages (Courier, whose
-- letters are all as wide, as the screen's are). Saved in its own format
-- (.BMD, text: save_text below) in /docs on the SD card, the documents the
-- player lets the apps have (doc_read, doc_write: the console asks the
-- first time); exported to PDF, HTML, Markdown and plain text, and .TXT /
-- .MD files open too. The words are completed as in bm Code (Tab, require
-- "predict"), the pad writes with padtype (Share). Keys: F1, or F12 held.

local predict = require "predict"
local pt = require "padtype"

local W, H = SCREEN_W, SCREEN_H

local C = {
  desk = 0x2B303B, page = 0xFFFFFF, shadow = 0x171A21, ink = 0x1C1F26, head = 0x15233F,
  faint = 0xA0A6B4, bar = 0x1A1E2A, bar2 = 0x2A3145, text = 0xE6E9F2, dim = 0x7C849A,
  acc = 0xFFB84A, on = 0x4F8FE8, sel = 0xC4DAFF, cursor = 0x1A5FD8, ok = 0x70E090,
  err = 0xFF6464, quote = 0x9AA3B8, ghost = 0x8A9AB8, panel = 0x141822, edge = 0x3A4258,
  hot = 0x2E4A8A,
}

------------------------------------------------------------------ the page

-- Pixels of the screen; the PDF is the same at 595/540 points a pixel. A
-- character of Courier is 0.6 of its size wide: a style's PDF size is its
-- character's width in points / 0.6.
local PW, PH = 540, 764                 -- an A4 page (595 x 842 pt)
local MX, MT, MB = 54, 54, 64           -- margins (2 cm; the page number in the bottom one)
local TW = PW - 2 * MX                  -- 432: 72 letters of the body
local GAP, TOP = 18, 12                 -- between the pages, above the first
local BAR, STAT = 16, 16                -- the toolbar and the status line (font 8x16)
local PX = (W - PW) // 2
local PT = 595 / PW

local STY = {
  body   = { name = "Body", font = "6x12", scale = 1, cw = 6, fh = 12, lh = 15, before = 0, after = 7, indent = 0 },
  title  = { name = "Title", font = "8x16", scale = 2, cw = 16, fh = 16, lh = 38, before = 0, after = 14, indent = 0,
             bold = true, align = "center" },
  h1     = { name = "Heading 1", font = "6x12", scale = 2, cw = 12, fh = 12, lh = 28, before = 12, after = 6, indent = 0,
             bold = true },
  h2     = { name = "Heading 2", font = "8x16", scale = 1, cw = 8, fh = 16, lh = 20, before = 9, after = 4, indent = 0,
             bold = true },
  quote  = { name = "Quote", font = "6x12", scale = 1, cw = 6, fh = 12, lh = 15, before = 3, after = 8, indent = 30,
             italic = true },
  bullet = { name = "Bullet list", font = "6x12", scale = 1, cw = 6, fh = 12, lh = 15, before = 0, after = 3, indent = 24 },
  number = { name = "Numbered list", font = "6x12", scale = 1, cw = 6, fh = 12, lh = 15, before = 0, after = 3, indent = 30 },
}
local ORDER = { "body", "title", "h1", "h2", "quote", "bullet", "number" }
local ALIGNS = { "left", "center", "right", "justify" }
local BOLD, ITALIC, UNDER = 1, 2, 4

------------------------------------------------------------------ the document

-- doc[i] = { s = style, a = alignment, t = letters (code page 437, as the
-- font draws them), m = their bits (a letter each: "0" + BOLD | ITALIC |
-- UNDER) }; lines, first, num: the layout's (wrap, layout)
local doc
local file                              -- its name in /docs ("LETTER.BMD"), nil if new
local dirty = false
local cur = { p = 1, c = 0 }            -- the cursor: paragraph, letters before it
local anchor                            -- the selection's other end, or nil
local goal_x                            -- up and down keep the column
local typing                            -- the bits of what is typed next (Ctrl+B... with no selection)
local view = 0                          -- the document's y at the top of the view
local undo, redo, last_kind = {}, {}, nil
local clip_board                        -- the copied paragraphs
local cfg = { lang = "it", complete = true }
local PL, npages, laid = {}, 1, false   -- the placed lines: { p, li, page, y }
local words_n, words_at = 0, nil        -- the words, counted again after a change

local function para(s, a, t, m)
  t = t or ""
  return { s = s or "body", a = a or (STY[s or "body"].align or "left"), t = t, m = m or string.rep("0", #t) }
end

local function touched(p)
  if p then p.lines = nil end
  laid, dirty, words_at, goal_x = false, true, nil, nil
end

local function new_doc()
  doc = { para("body") }
  file, dirty, anchor, typing, goal_x, view = nil, false, nil, nil, nil, 0
  cur.p, cur.c = 1, 0
  undo, redo, last_kind = {}, {}, nil
  laid = false
end

------------------------------------------------------------------ layout

-- the lines of a paragraph: { from, to } letters [from, to), words whole
-- when they fit, the spaces at a line's end on it
local function wrap(p)
  local st = STY[p.s]
  local cols = (TW - st.indent) // st.cw
  local t, n = p.t, #p.t
  local lines, i = {}, 1
  while true do
    if n - i + 1 <= cols then
      lines[#lines + 1] = { i, n + 1 }
      return lines
    end
    local k, brk = i + cols, nil
    for j = k, i + 1, -1 do
      if t:byte(j) == 32 then brk = j; break end
    end
    local e = brk and brk + 1 or k
    lines[#lines + 1] = { i, e }
    i = e
    if i > n then return lines end
  end
end

-- the pages: every line placed (page, y from the page's top)
local function layout()
  if laid then return end
  PL = {}
  local page, y, num = 1, MT, 0
  for pi, p in ipairs(doc) do
    local st = STY[p.s]
    if not p.lines then p.lines = wrap(p) end
    if p.s == "number" then num = num + 1; p.num = num else num = 0 end
    if y > MT then y = y + st.before end
    p.first = #PL + 1
    for li = 1, #p.lines do
      if y + st.lh > PH - MB then page, y = page + 1, MT end
      PL[#PL + 1] = { p = pi, li = li, page = page, y = y }
      y = y + st.lh
    end
    y = y + st.after
  end
  npages = page
  laid = true
end

local function page_top(page) return TOP + (page - 1) * (PH + GAP) end

-- a line's letters [a, e) without the spaces at its end, its x from the
-- text's left edge and, justified, the pixels added to its spaces
local function line_geom(p, li)
  local st = STY[p.s]
  local a, b = p.lines[li][1], p.lines[li][2]
  local e = b
  while e > a and p.t:byte(e - 1) == 32 do e = e - 1 end
  local w, avail = (e - a) * st.cw, TW - st.indent
  local x, per, extra = st.indent, 0, 0
  if p.a == "center" then x = x + (avail - w) // 2
  elseif p.a == "right" then x = x + avail - w
  elseif p.a == "justify" and li < #p.lines then
    local nsp = 0
    for j = a, e - 1 do if p.t:byte(j) == 32 then nsp = nsp + 1 end end
    if nsp > 0 then per, extra = (avail - w) // nsp, (avail - w) % nsp end
  end
  return x, a, e, per, extra
end

-- the x of letter k in a line laid out by line_geom
local function letter_x(p, x, a, k, per, extra)
  local sp = 0
  if per > 0 or extra > 0 then
    for j = a, k - 1 do if p.t:byte(j) == 32 then sp = sp + 1 end end
  end
  return x + (k - a) * STY[p.s].cw + sp * per + math.min(sp, extra)
end

-- the placed line of the cursor, and its index in the paragraph
local function cursor_line(q)
  layout()
  q = q or cur
  local p = doc[q.p]
  for li = 1, #p.lines do
    if q.c + 1 < p.lines[li][2] or li == #p.lines then return p.first + li - 1, li end
  end
end

-- the cursor's page x (from the page's left edge), its document y, its style
local function cursor_xy()
  local k, li = cursor_line()
  local L, p = PL[k], doc[cur.p]
  local x, a, _, per, extra = line_geom(p, li)
  return MX + letter_x(p, x, a, cur.c + 1, per, extra), page_top(L.page) + L.y, STY[p.s]
end

-- the cursor's letters-before in a line, nearest the page x gx
local function col_at(pi, li, gx)
  local p = doc[pi]
  local x, a, _, per, extra = line_geom(p, li)
  local b = p.lines[li][2]
  local maxc = li == #p.lines and b - 1 or b - 2
  local best, bd = a - 1, math.huge
  for c = a - 1, math.max(a - 1, maxc) do
    local d = math.abs(MX + letter_x(p, x, a, c + 1, per, extra) - gx)
    if d < bd then best, bd = c, d end
  end
  return best
end

local function count_words()
  if words_at then return words_n end
  local n, letters = 0, 0
  for _, p in ipairs(doc) do
    for _ in p.t:gmatch("[^%s]+") do n = n + 1 end
    letters = letters + #p.t
  end
  words_n, words_at = n, letters
  return n
end

------------------------------------------------------------------ text

-- Code page 437 (what the font draws and the keyboard types) and Unicode:
-- the files are UTF-8, the PDF WinAnsi
local CP = {
  0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
  0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x20A7, 0x192,
  0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0x2310, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C,
  0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566,
  0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518,
  0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x3B1, 0xDF, 0x393, 0x3C0, 0x3A3, 0x3C3, 0xB5, 0x3C4, 0x3A6,
  0x398, 0x3A9, 0x3B4, 0x221E, 0x3C6, 0x3B5, 0x2229, 0x2261, 0xB1, 0x2265, 0x2264, 0x2320, 0x2321, 0xF7, 0x2248,
  0xB0, 0x2219, 0xB7, 0x221A, 0x207F, 0xB2, 0x25A0, 0xA0,
}
local FROM = {}                         -- Unicode -> code page 437
for i, u in ipairs(CP) do FROM[u] = 127 + i end
FROM[0x2022] = 0xF9                     -- a bullet: the bullet operator
FROM[0x2019], FROM[0x2018] = 39, 39     -- typographic quotes, dashes, ellipsis
FROM[0x201C], FROM[0x201D] = 34, 34
FROM[0x2013], FROM[0x2014] = 45, 45

local function to_utf8(s)
  return (s:gsub("[\128-\255]", function(ch) return utf8.char(CP[ch:byte() - 127]) end))
end

local function from_utf8(s)
  local ok, out = pcall(function()
    local o = {}
    for _, u in utf8.codes(s) do
      if u < 128 then o[#o + 1] = string.char(u)
      elseif u == 0x2026 then o[#o + 1] = "..."
      else o[#o + 1] = string.char(FROM[u] or 63) end
    end
    return table.concat(o)
  end)
  if ok then return out end
  return (s:gsub("[\128-\255]", function(ch) return string.char(FROM[ch:byte()] or 63) end))   -- Latin-1
end

------------------------------------------------------------------ the file

-- .BMD: "bmwrite 1", "key=value" lines (title=), a blank line, then a line
-- a paragraph: "style align runs|text", runs "start:length:bits" of the
-- letters that are not plain (comma separated), text UTF-8
local function save_text()
  local out = { "bmwrite 1", "title=" .. to_utf8((doc[1] and doc[1].t or ""):sub(1, 60)), "" }
  for _, p in ipairs(doc) do
    local runs, i, n = {}, 1, #p.m
    while i <= n do
      local b, j = p.m:byte(i), i
      while j < n and p.m:byte(j + 1) == b do j = j + 1 end
      if b ~= 48 then runs[#runs + 1] = i .. ":" .. (j - i + 1) .. ":" .. (b - 48) end
      i = j + 1
    end
    out[#out + 1] = p.s .. " " .. p.a .. " " .. table.concat(runs, ",") .. "|" .. to_utf8(p.t)
  end
  return table.concat(out, "\n") .. "\n"
end

local function load_bmd(text)
  if not text:find("^bmwrite 1\n") then return nil, "not a bm Write document" end
  local body = text:match("\n\n(.*)$") or ""
  local d = {}
  for line in body:gmatch("([^\n]*)\n?") do
    local s, a, runs, t = line:match("^(%S+) (%S+) (%S*)|(.*)$")
    if s and STY[s] then
      t = from_utf8(t)
      local m = { string.rep("0", #t):byte(1, -1) }
      for st, len, bits in runs:gmatch("(%d+):(%d+):(%d+)") do
        for k = tonumber(st), math.min(#t, tonumber(st) + tonumber(len) - 1) do m[k] = 48 + tonumber(bits) % 8 end
      end
      d[#d + 1] = para(s, a, t, #t > 0 and string.char(table.unpack(m)) or "")
    end
  end
  if #d == 0 then d[1] = para("body") end
  return d
end

-- Markdown (and plain text: no marks) into paragraphs: # title, ##, ###,
-- > quote, - * bullets, 1. numbers, **bold**, *italic*, <u>underline</u>
local function load_md(text)
  text = from_utf8(text:gsub("\r", ""))
  local d = {}
  local function inline(s)
    local t, m, bits = {}, {}, 0
    local i = 1
    while i <= #s do
      if s:sub(i, i + 1) == "**" then bits = bits ~ BOLD; i = i + 2
      elseif s:sub(i, i + 2) == "<u>" then bits = bits | UNDER; i = i + 3
      elseif s:sub(i, i + 3) == "</u>" then bits = bits & ~UNDER; i = i + 4
      elseif s:sub(i, i) == "*" or (s:sub(i, i) == "_" and (i == 1 or s:sub(i - 1, i - 1) == " " or bits & ITALIC ~= 0)) then
        bits = bits ~ ITALIC; i = i + 1
      else
        t[#t + 1] = s:sub(i, i)
        m[#m + 1] = string.char(48 + bits)
        i = i + 1
      end
    end
    return table.concat(t), table.concat(m)
  end
  local pend
  local function flush()
    if pend then
      local t, m = inline(pend.text)
      d[#d + 1] = para(pend.s, nil, t, m)
      pend = nil
    end
  end
  for line in (text .. "\n"):gmatch("([^\n]*)\n") do
    local s, rest
    if line:match("^%s*$") then flush()
    else
      local h, r = line:match("^(#+)%s+(.*)$")
      if h then s, rest = (#h == 1 and "title" or #h == 2 and "h1" or "h2"), r
      elseif line:match("^>%s?") then s, rest = "quote", line:gsub("^>%s?", "")
      elseif line:match("^%s*[-*+]%s+") then s, rest = "bullet", line:gsub("^%s*[-*+]%s+", "")
      elseif line:match("^%s*%d+[.)]%s+") then s, rest = "number", line:gsub("^%s*%d+[.)]%s+", "")
      end
      if s then
        flush()
        pend = { s = s, text = rest }
        if s ~= "quote" and s ~= "body" then flush() end
      elseif pend and pend.s ~= "title" and pend.s ~= "h1" and pend.s ~= "h2" then
        pend.text = pend.text .. " " .. line:gsub("^%s+", "")
      else
        flush()
        pend = { s = "body", text = line }
      end
    end
  end
  flush()
  if #d == 0 then d[1] = para("body") end
  return d
end

------------------------------------------------------------------ exports

local EXP = {}

local function runs_of(p)          -- { {text, bits}, ... } of a paragraph
  local out, i, n = {}, 1, #p.t
  while i <= n do
    local b, j = p.m:byte(i), i
    while j < n and p.m:byte(j + 1) == b do j = j + 1 end
    out[#out + 1] = { p.t:sub(i, j), b - 48 }
    i = j + 1
  end
  return out
end

function EXP.text()
  local out, prev = {}, nil
  for _, p in ipairs(doc) do
    local t = to_utf8(p.t)
    local list = p.s == "bullet" or p.s == "number"
    if prev and not (list and prev == p.s) then out[#out + 1] = "" end     -- a list's items together
    if p.s == "bullet" then t = "- " .. t
    elseif p.s == "number" then t = (p.num or 1) .. ". " .. t
    elseif p.s == "quote" then t = "    " .. t end
    out[#out + 1] = t
    if p.s == "title" or p.s == "h1" then out[#out + 1] = string.rep(p.s == "title" and "=" or "-", #p.t) end
    prev = p.s
  end
  return table.concat(out, "\n") .. "\n"
end

function EXP.markdown()
  local out, prev = {}, nil
  for _, p in ipairs(doc) do
    local s = {}
    for _, r in ipairs(runs_of(p)) do
      local t = to_utf8(r[1]):gsub("([*_\\`])", "\\%1")
      if r[2] & UNDER ~= 0 then t = "<u>" .. t .. "</u>" end
      if r[2] & ITALIC ~= 0 then t = "*" .. t .. "*" end
      if r[2] & BOLD ~= 0 then t = "**" .. t .. "**" end
      s[#s + 1] = t
    end
    local t = table.concat(s)
    local list = p.s == "bullet" or p.s == "number"
    if prev and not (list and prev == p.s) then out[#out + 1] = "" end
    if p.s == "title" then t = "# " .. t
    elseif p.s == "h1" then t = "## " .. t
    elseif p.s == "h2" then t = "### " .. t
    elseif p.s == "quote" then t = "> " .. t
    elseif p.s == "bullet" then t = "- " .. t
    elseif p.s == "number" then t = (p.num or 1) .. ". " .. t end
    out[#out + 1] = t
    prev = p.s
  end
  return table.concat(out, "\n") .. "\n"
end

function EXP.html(title)
  local function esc(s)
    return (to_utf8(s):gsub("[&<>\"]", { ["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;", ['"'] = "&quot;" }))
  end
  local out = {
    "<!doctype html>", "<html><head><meta charset=\"utf-8\">",
    "<title>" .. esc(title) .. "</title>",
    "<style>",
    "@page { size: A4; margin: 2cm }",
    "body { font-family: Georgia, 'Times New Roman', serif; font-size: 12pt; line-height: 1.45;",
    "  max-width: 17cm; margin: 2em auto; padding: 0 1em; color: #1c1f26 }",
    "h1.title { font-size: 2.1em; text-align: center; margin: 0 0 .8em }",
    "h1 { font-size: 1.6em; margin: 1.1em 0 .4em } h2 { font-size: 1.25em; margin: 1em 0 .3em }",
    "p { margin: 0 0 .55em } blockquote { margin: .3em 0 .7em 1.2em; padding-left: .8em;",
    "  border-left: 3px solid #9aa3b8; font-style: italic; color: #3c4250 }",
    "ul, ol { margin: 0 0 .6em 0 } .left { text-align: left } .center { text-align: center }",
    ".right { text-align: right }",
    ".justify { text-align: justify }",
    "</style></head><body>",
  }
  local open
  for _, p in ipairs(doc) do
    local list = p.s == "bullet" and "ul" or p.s == "number" and "ol" or nil
    if open and open ~= list then out[#out + 1] = "</" .. open .. ">"; open = nil end
    if list and not open then out[#out + 1] = "<" .. list .. ">"; open = list end
    local s = {}
    for _, r in ipairs(runs_of(p)) do
      local t = esc(r[1])
      if r[2] & UNDER ~= 0 then t = "<u>" .. t .. "</u>" end
      if r[2] & ITALIC ~= 0 then t = "<i>" .. t .. "</i>" end
      if r[2] & BOLD ~= 0 then t = "<b>" .. t .. "</b>" end
      s[#s + 1] = t
    end
    local t = #s > 0 and table.concat(s) or "&nbsp;"
    local cls = p.a ~= "left" and p.a ~= (STY[p.s].align or "left") and (" class=\"" .. p.a .. "\"") or ""
    if p.s == "title" then out[#out + 1] = "<h1 class=\"title" .. (p.a ~= "center" and " " .. p.a or "") .. "\">" .. t .. "</h1>"
    elseif p.s == "h1" then out[#out + 1] = "<h1" .. cls .. ">" .. t .. "</h1>"
    elseif p.s == "h2" then out[#out + 1] = "<h2" .. cls .. ">" .. t .. "</h2>"
    elseif p.s == "quote" then out[#out + 1] = "<blockquote" .. cls .. ">" .. t .. "</blockquote>"
    elseif list then out[#out + 1] = "<li" .. cls .. ">" .. t .. "</li>"
    else out[#out + 1] = "<p" .. cls .. ">" .. t .. "</p>" end
  end
  if open then out[#out + 1] = "</" .. open .. ">" end
  out[#out + 1] = "</body></html>"
  return table.concat(out, "\n") .. "\n"
end

-- PDF 1.4: the pages of the layout, a run of letters at a time where the
-- screen draws it (Courier, Courier-Bold, -Oblique, -BoldOblique, WinAnsi)
local WINANSI = { [0x20AC] = 0x80, [0x2026] = 0x85, [0x2022] = 0x95, [0x2013] = 0x96, [0x2014] = 0x97,
                  [0x2018] = 0x91, [0x2019] = 0x92, [0x201C] = 0x93, [0x201D] = 0x94, [0x192] = 0x83 }

local function pdf_string(s)
  return (s:gsub("[%(%)\\\128-\255]", function(ch)
    local b = ch:byte()
    if b < 128 then return "\\" .. ch end
    local u = CP[b - 127]
    local w = (u >= 0xA0 and u <= 0xFF) and u or WINANSI[u] or 63
    return string.format("\\%03o", w)
  end))
end

function EXP.pdf(title)
  layout()
  local pages = {}
  for i = 1, npages do pages[i] = {} end
  local function add(page, s) local t = pages[page]; t[#t + 1] = s end
  local function y_pt(px) return 842 - px * PT end
  for _, L in ipairs(PL) do
    local p = doc[L.p]
    local st = STY[p.s]
    local size = st.cw * PT / 0.6
    local fhs = st.fh * st.scale
    local top = L.y + (st.lh - fhs) // 2
    local base = y_pt(top + fhs * 0.78)
    local x, a, e, per, extra = line_geom(p, L.li)
    local left = MX
    if L.li == 1 and p.s == "bullet" then
      add(L.page, string.format("BT /F1 %.2f Tf %.2f %.2f Td (\\225) Tj ET", size, (left + st.indent - 13) * PT, base))
    elseif L.li == 1 and p.s == "number" then
      local n = (p.num or 1) .. "."
      add(L.page, string.format("BT /F1 %.2f Tf %.2f %.2f Td (%s) Tj ET", size,
                                (left + st.indent - (#n + 1) * st.cw) * PT, base, n))
    end
    if p.s == "quote" then
      add(L.page, string.format("0.6 0.64 0.72 rg %.2f %.2f %.2f %.2f re f 0 g", (left + st.indent - 14) * PT,
                                y_pt(L.y + st.lh), 3 * PT, st.lh * PT))
    end
    local k = a
    local gapped = per > 0 or extra > 0
    while k < e do
      local raw = p.m:byte(k) - 48
      local j = k
      while j + 1 < e and p.m:byte(j + 1) - 48 == raw and not (gapped and (p.t:byte(j + 1) == 32 or p.t:byte(j) == 32)) do
        j = j + 1
      end
      local bits = raw | (st.bold and BOLD or 0) | (st.italic and ITALIC or 0)
      local font = 1 + (bits & BOLD ~= 0 and 1 or 0) + (bits & ITALIC ~= 0 and 2 or 0)
      local x0 = (left + letter_x(p, x, a, k, per, extra)) * PT
      local s = p.t:sub(k, j)
      if s:find("[^ ]") then
        add(L.page, string.format("BT /F%d %.2f Tf %.2f %.2f Td (%s) Tj ET", font, size, x0, base, pdf_string(s)))
      end
      if bits & UNDER ~= 0 then
        add(L.page, string.format("%.2f w %.2f %.2f m %.2f %.2f l S", size * 0.06, x0, base - size * 0.12,
                                  x0 + (j - k + 1) * st.cw * PT, base - size * 0.12))
      end
      k = j + 1
    end
  end
  -- the objects: catalog, pages, four fonts, then a page and its contents each
  local objs = {}
  local function obj(s) objs[#objs + 1] = s; return #objs end
  obj("<< /Type /Catalog /Pages 2 0 R >>")
  obj("")                                               -- the pages, at the end
  local fonts = { "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique" }
  for _, f in ipairs(fonts) do obj("<< /Type /Font /Subtype /Type1 /BaseFont /" .. f .. " /Encoding /WinAnsiEncoding >>") end
  local kids = {}
  for i = 1, npages do
    local c = pages[i]
    c[#c + 1] = string.format("BT /F1 %.2f Tf %.2f %.2f Td (%d) Tj ET", 9, 297.5 - #tostring(i) * 2.7, 30, i)
    local stream = table.concat(c, "\n")
    local cid = obj("<< /Length " .. #stream .. " >>\nstream\n" .. stream .. "\nendstream")
    kids[#kids + 1] = obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R "
                          .. "/F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents " .. cid .. " 0 R >>") .. " 0 R"
  end
  objs[2] = "<< /Type /Pages /Kids [" .. table.concat(kids, " ") .. "] /Count " .. npages .. " >>"
  local info = obj("<< /Title (" .. pdf_string(title) .. ") /Producer (bm Write) >>")
  local out, offs = { "%PDF-1.4\n%\226\227\207\211\n" }, {}
  local size = #out[1]
  for i, s in ipairs(objs) do
    offs[i] = size
    local o = i .. " 0 obj\n" .. s .. "\nendobj\n"
    out[#out + 1] = o
    size = size + #o
  end
  local x = { "xref", "0 " .. (#objs + 1), "0000000000 65535 f " }
  for i = 1, #objs do x[#x + 1] = string.format("%010d 00000 n ", offs[i]) end
  out[#out + 1] = table.concat(x, "\n") .. "\n"
  out[#out + 1] = "trailer\n<< /Size " .. (#objs + 1) .. " /Root 1 0 R /Info " .. info .. " 0 R >>\nstartxref\n"
                  .. size .. "\n%%EOF\n"
  return table.concat(out)
end

------------------------------------------------------------------ editing

local function snapshot(kind)
  if kind and kind == last_kind then return end
  local copy = {}
  for i, p in ipairs(doc) do copy[i] = { s = p.s, a = p.a, t = p.t, m = p.m } end
  undo[#undo + 1] = { doc = copy, p = cur.p, c = cur.c }
  if #undo > 80 then table.remove(undo, 1) end
  redo = {}
  last_kind = kind
end

local function state_now()
  local copy = {}
  for i, p in ipairs(doc) do copy[i] = { s = p.s, a = p.a, t = p.t, m = p.m } end
  return { doc = copy, p = cur.p, c = cur.c }
end

local function restore(st)
  doc = {}
  for i, p in ipairs(st.doc) do doc[i] = { s = p.s, a = p.a, t = p.t, m = p.m } end
  cur.p, cur.c = math.min(st.p, #doc), st.c
  cur.c = math.min(cur.c, #doc[cur.p].t)
  anchor, typing, last_kind = nil, nil, nil
  touched()
end

local function sel_range()
  if not anchor then return end
  local a, b = anchor, cur
  if a.p > b.p or (a.p == b.p and a.c > b.c) then a, b = b, a end
  if a.p == b.p and a.c == b.c then return end
  return a.p, a.c, b.p, b.c
end

local function bits_at(p, c)               -- what the letters typed at c get
  if typing then return typing end
  if c > 0 then return p.m:byte(c) - 48 end
  if #p.m > 0 then return p.m:byte(1) - 48 end
  return 0
end

local function copy_range(p0, c0, p1, c1)
  local out = {}
  for i = p0, p1 do
    local p = doc[i]
    local a = i == p0 and c0 + 1 or 1
    local b = i == p1 and c1 or #p.t
    out[#out + 1] = { s = p.s, a = p.a, t = p.t:sub(a, b), m = p.m:sub(a, b) }
  end
  return out
end

local function delete_range(p0, c0, p1, c1)
  local a, b = doc[p0], doc[p1]
  a.t = a.t:sub(1, c0) .. b.t:sub(c1 + 1)
  a.m = a.m:sub(1, c0) .. b.m:sub(c1 + 1)
  for i = p1, p0 + 1, -1 do table.remove(doc, i) end
  cur.p, cur.c, anchor = p0, c0, nil
  touched(a)
end

local function delete_sel()
  local p0, c0, p1, c1 = sel_range()
  if not p0 then anchor = nil; return false end
  snapshot()
  delete_range(p0, c0, p1, c1)
  return true
end

local function insert_text(s)
  if #s == 0 then return end
  local p = doc[cur.p]
  local bits = string.char(48 + bits_at(p, cur.c))
  p.t = p.t:sub(1, cur.c) .. s .. p.t:sub(cur.c + 1)
  p.m = p.m:sub(1, cur.c) .. bits:rep(#s) .. p.m:sub(cur.c + 1)
  cur.c = cur.c + #s
  touched(p)
end

local function paste(frags)
  if not frags or #frags == 0 then return end
  local p = doc[cur.p]
  local tt, tm = p.t:sub(cur.c + 1), p.m:sub(cur.c + 1)
  p.t, p.m = p.t:sub(1, cur.c) .. frags[1].t, p.m:sub(1, cur.c) .. frags[1].m
  local at = cur.p
  for i = 2, #frags do
    local f = frags[i]
    at = at + 1
    table.insert(doc, at, { s = f.s, a = f.a, t = f.t, m = f.m })
  end
  local last = doc[at]
  cur.p, cur.c = at, #last.t
  last.t, last.m = last.t .. tt, last.m .. tm
  touched(p)
  touched(last)
end

-- Enter: the paragraph splits; after a heading's end comes the body; an
-- empty list item ends the list
local function split()
  local p = doc[cur.p]
  if (p.s == "bullet" or p.s == "number") and p.t == "" then
    p.s = "body"
    touched(p)
    return
  end
  local np = { s = p.s, a = p.a, t = p.t:sub(cur.c + 1), m = p.m:sub(cur.c + 1) }
  if (p.s == "title" or p.s == "h1" or p.s == "h2") and np.t == "" then np.s, np.a = "body", "left" end
  p.t, p.m = p.t:sub(1, cur.c), p.m:sub(1, cur.c)
  table.insert(doc, cur.p + 1, np)
  cur.p, cur.c = cur.p + 1, 0
  touched(p)
end

local function backspace()
  if delete_sel() then return end
  local p = doc[cur.p]
  snapshot("bs")
  if cur.c > 0 then
    p.t = p.t:sub(1, cur.c - 1) .. p.t:sub(cur.c + 1)
    p.m = p.m:sub(1, cur.c - 1) .. p.m:sub(cur.c + 1)
    cur.c = cur.c - 1
    touched(p)
  elseif p.s == "bullet" or p.s == "number" or p.s == "quote" then
    p.s = "body"                         -- the mark goes first, as in a word processor
    touched(p)
  elseif cur.p > 1 then
    local q = doc[cur.p - 1]
    cur.c = #q.t
    q.t, q.m = q.t .. p.t, q.m .. p.m
    table.remove(doc, cur.p)
    cur.p = cur.p - 1
    touched(q)
  end
end

local function delete_forward()
  if delete_sel() then return end
  local p = doc[cur.p]
  snapshot("del")
  if cur.c < #p.t then
    p.t = p.t:sub(1, cur.c) .. p.t:sub(cur.c + 2)
    p.m = p.m:sub(1, cur.c) .. p.m:sub(cur.c + 2)
    touched(p)
  elseif cur.p < #doc then
    local q = doc[cur.p + 1]
    p.t, p.m = p.t .. q.t, p.m .. q.m
    table.remove(doc, cur.p + 1)
    touched(p)
  end
end

-- the paragraphs the selection (or the cursor) is in
local function chosen()
  local p0, _, p1 = sel_range()
  if not p0 then return cur.p, cur.p end
  return p0, p1
end

local function set_style(id)
  snapshot()
  local a, b = chosen()
  for i = a, b do
    local p = doc[i]
    local old = p.s
    p.s = id
    if STY[id].align then p.a = STY[id].align elseif STY[old].align and p.a == STY[old].align then p.a = "left" end
    touched(p)
  end
end

local function set_align(al)
  snapshot()
  local a, b = chosen()
  for i = a, b do doc[i].a = al; touched(doc[i]) end
end

-- Ctrl+B, I, U: on the selection (off if every letter has it), or on what
-- is typed next
local function toggle(bit)
  local p0, c0, p1, c1 = sel_range()
  if not p0 then
    typing = bits_at(doc[cur.p], cur.c) ~ bit
    return
  end
  snapshot()
  local all = true
  for i = p0, p1 do
    local m = doc[i].m
    local a, b = i == p0 and c0 + 1 or 1, i == p1 and c1 or #m
    for k = a, b do if (m:byte(k) - 48) & bit == 0 then all = false end end
  end
  for i = p0, p1 do
    local p = doc[i]
    local a, b = i == p0 and c0 + 1 or 1, i == p1 and c1 or #p.m
    local m = { p.m:byte(1, -1) }
    for k = a, b do m[k] = 48 + (all and ((m[k] - 48) & ~bit) or ((m[k] - 48) | bit)) end
    p.m = #m > 0 and string.char(table.unpack(m)) or ""
    touched(p)
  end
end

local function cur_bits()
  if typing then return typing end
  return bits_at(doc[cur.p], cur.c)
end

------------------------------------------------------------------ moving

local function moved()
  typing = nil
  last_kind = nil
end

local function move(dir)
  local p = doc[cur.p]
  if dir == "left" then
    if cur.c > 0 then cur.c = cur.c - 1 elseif cur.p > 1 then cur.p = cur.p - 1; cur.c = #doc[cur.p].t end
    goal_x = nil
  elseif dir == "right" then
    if cur.c < #p.t then cur.c = cur.c + 1 elseif cur.p < #doc then cur.p, cur.c = cur.p + 1, 0 end
    goal_x = nil
  elseif dir == "up" or dir == "down" or dir == "pgup" or dir == "pgdn" then
    local k = cursor_line()
    local gx = goal_x or select(1, cursor_xy())
    local step = (dir == "up" and -1) or (dir == "down" and 1) or (dir == "pgup" and -20) or 20
    local t = math.max(1, math.min(#PL, k + step))
    local L = PL[t]
    cur.p, cur.c = L.p, col_at(L.p, L.li, gx)
    goal_x = gx
  elseif dir == "home" or dir == "end" then
    local _, li = cursor_line()
    local a, b = p.lines[li][1], p.lines[li][2]
    if dir == "home" then cur.c = a - 1
    else cur.c = li == #p.lines and b - 1 or math.max(a - 1, b - 2) end
    goal_x = nil
  end
  moved()
end

------------------------------------------------------------------ completion and the pad

local comp, comp_flash                  -- the suggestion shown, what Tab has just written
local words_ready = false

local function suggest()
  comp = nil
  if not cfg.complete or anchor then return end
  local p = doc[cur.p]
  if p.t:sub(cur.c + 1, cur.c + 1):find("[%w\128-\165]") then return end   -- inside a word
  local c = predict.complete(p.t:sub(1, cur.c), { lang = cfg.lang })
  if c then c.p, c.c = cur.p, cur.c end
  comp = c
end

local function comp_here()
  return comp and comp.p == cur.p and comp.c == cur.c and comp or nil
end

local function accept()
  local c = comp_here()
  if not c then return false end
  snapshot("ins")
  if c.word:sub(1, #c.prefix) ~= c.prefix then         -- its accents, its case
    local p = doc[cur.p]
    p.t = p.t:sub(1, cur.c - #c.prefix) .. p.t:sub(cur.c + 1)
    p.m = p.m:sub(1, cur.c - #c.prefix) .. p.m:sub(cur.c + 1)
    cur.c = cur.c - #c.prefix
    insert_text(c.word)
  else
    insert_text(c.rest)
  end
  comp, comp_flash = nil, { p = cur.p, c = cur.c, n = #c.word }
  return true
end

-- Typing with the pad (Share, require "padtype"): it writes in the
-- paragraph through this host
local pad_host = { now = time, prose = true }
function pad_host.before() return doc[cur.p].t:sub(1, cur.c) end
function pad_host.insert(s)
  for ch in s:gmatch(".") do
    if ch == "\n" then snapshot(); split() else snapshot("ins"); delete_sel(); insert_text(ch) end
  end
end
function pad_host.erase(n)
  for _ = 1, n do backspace() end
end
function pad_host.newline() snapshot(); split() end
function pad_host.move(dir) move(dir) end

------------------------------------------------------------------ files

local overlay                           -- menu, files, prompt, confirm, help, export
local status, status_c, status_t = "", C.dim, 0
local function say(s, c, t) status, status_c, status_t = s, c or C.text, t or 240 end

local function base_name() return file and file:match("^[^.]+") end
local save_session                      -- (the session, below: kept at every save and open)

local function write_doc(name)
  local ok, err = doc_write(name, save_text())
  if not ok then say("cannot save " .. name .. ": " .. tostring(err), C.err); return false end
  file, dirty = name, false
  save_session()
  say("saved /docs/" .. name, C.ok)
  log("write: saved /docs/" .. name .. " (" .. #doc .. " paragraphs)")
  return true
end

-- a line to write; what it starts with is chosen: the first letter replaces it
local function prompt(label, text, done)
  overlay = { kind = "prompt", label = label, text = text or "", done = done, fresh = true }
end

local function ask_name(then_do)
  prompt("Name of the document (8 letters, in /docs):", base_name() or "DOC1", function(n)
    n = n:upper():gsub("%.%w*$", "")
    if not n:match("^[%w_%-]+$") or #n > 8 then say("8 letters or digits, like LETTER", C.err); return end
    then_do(n)
  end)
end

local function save_doc(as)
  if file and not as then return write_doc(file) end
  ask_name(function(n) write_doc(n .. ".BMD") end)
end

local function open_doc(name)
  local text, err = doc_read(name)
  if not text then say("cannot open " .. name .. ": " .. tostring(err), C.err); return end
  local d, why
  if name:upper():match("%.BMD$") then d, why = load_bmd(text) else d = load_md(text) end
  if not d then say(name .. ": " .. why, C.err); return end
  new_doc()
  doc = d
  file = name:upper():match("%.BMD$") and name:upper() or nil
  dirty = file == nil
  save_session()
  say("opened /docs/" .. name, C.ok)
  log("write: opened /docs/" .. name .. " (" .. #doc .. " paragraphs)")
end

local function open_files()
  local list, err = doc_list()
  if not list then say(tostring(err), C.err); return end
  local items = {}
  for _, f in ipairs(list) do
    local ext = f.name:upper():match("%.(%w+)$")
    if ext == "BMD" or ext == "TXT" or ext == "MD" then items[#items + 1] = f end
  end
  table.sort(items, function(a, b) return a.name < b.name end)
  if #items == 0 then say("no documents in /docs yet: Ctrl+S saves this one", C.acc); return end
  overlay = { kind = "files", items = items, sel = 1, top = 1 }
end

local FORMATS = { { "PDF", "PDF" }, { "HTML", "HTM" }, { "Markdown", "MD" }, { "Text", "TXT" } }

local function export(fmt)
  local function go(base)
    local ext = fmt[2]
    local title = to_utf8(doc[1].t ~= "" and doc[1].t or base)
    local data = ext == "PDF" and EXP.pdf(title) or ext == "HTM" and EXP.html(title)
                 or ext == "MD" and EXP.markdown() or EXP.text()
    local name = base .. "." .. ext
    local ok, err = doc_write(name, data)
    if ok then
      say("exported /docs/" .. name .. " (" .. #data .. " bytes)", C.ok)
      log("write: exported /docs/" .. name .. " " .. #data .. " bytes")
    else
      say("cannot export " .. name .. ": " .. tostring(err), C.err)
    end
  end
  layout()
  if base_name() then go(base_name()) else ask_name(go) end
end

local function confirm(question, choices, done)
  overlay = { kind = "confirm", question = question, choices = choices, done = done, sel = 1 }
end

-- something that drops the document: asks first when it has changes
local function leave_doc(then_do)
  if not dirty then then_do(); return end
  confirm("This document has changes. Save them?", { "Save", "Discard", "Cancel" }, function(i)
    if i == 1 then
      if file then if write_doc(file) then then_do() end
      else ask_name(function(n) if write_doc(n .. ".BMD") then then_do() end end) end
    elseif i == 2 then then_do() end
  end)
end

------------------------------------------------------------------ the session

save_session = function()
  local s = { lang = cfg.lang, complete = cfg.complete, file = file, p = cur.p, c = cur.c }
  if dirty then
    local text = save_text()
    if #text < 24000 then s.text = text end
  end
  save({ write = s })
end

local function load_session()
  local d = saved()
  local s = d and d.write
  if not s then return end
  cfg.lang = s.lang or cfg.lang
  if s.complete ~= nil then cfg.complete = s.complete end
  if s.text then
    local dd = load_bmd(s.text)
    if dd then
      doc, file, dirty = dd, s.file, true
      say("the document with its changes is back (Ctrl+S saves it)", C.acc)
    end
  elseif s.file then
    open_doc(s.file)
  end
  if doc[s.p or 1] then cur.p = s.p or 1; cur.c = math.min(s.c or 0, #doc[cur.p].t) end
end

------------------------------------------------------------------ the menu

local MENU = {
  { "New document", "Ctrl+N" }, { "Open...", "Ctrl+O" }, { "Save", "Ctrl+S" }, { "Save as...", "Ctrl+Shift+S" },
  { "Export...", "PDF HTML MD TXT" }, { "Style", "F2" }, { "Alignment", "F4" },
  { "Bold", "Ctrl+B" }, { "Italic", "Ctrl+I" }, { "Underline", "Ctrl+U" }, { "Select", "F3" },
  { "Undo", "Ctrl+Z" }, { "Redo", "Ctrl+Y" }, { "Word completion", "" }, { "Language", "" },
  { "Pad typing", "Share" }, { "Keys", "F1" }, { "Exit", "" },
}

local KEYHELP = {
  { "ctrl b / ctrl i / ctrl u", "bold, italic, underline" },
  { "ctrl l / ctrl e / ctrl j", "align left, centre, justify" },
  { "f2", "the paragraph's style" },
  { "f4", "its alignment (left, centre, right, justify)" },
  { "f3", "start or end a selection (or shift + arrows)" },
  { "tab", "the word in grey-blue" },
  { "ctrl s / ctrl shift s", "save, save as (/docs)" },
  { "ctrl o / ctrl n", "open, new" },
  { "ctrl z / ctrl y", "undo, redo" },
  { "ctrl c / ctrl x / ctrl v", "copy, cut, paste" },
  { "f1", "the keys" },
  { "esc", "the menu: export to PDF, HTML, Markdown, text" },
  "pad",
  { "DPAD", "move" },
  { "A / B", "new paragraph, delete" },
  { "SELECT", "pad typing: on / off" },
  { "START", "the menu" },
}

local HELP = {                          -- "#" a heading
  "#Writing", "Enter new paragraph, Backspace / Del delete", "Tab writes the grey-blue word",
  "Arrows, Home, End, PgUp, PgDn move", "F3 or Shift+arrows select", "",
  "#Look", "F2 style: Body, Title, Heading 1, Heading 2,", "  Quote, Bullet list, Numbered list",
  "F4 alignment: left, centre, right, justify", "Ctrl+L / Ctrl+E / Ctrl+J left, centre, justify",
  "Ctrl+B bold, Ctrl+I italic, Ctrl+U underline", "",
  "#Files (in /docs on the SD card)", "Ctrl+S save, Ctrl+Shift+S save as, Ctrl+O open",
  "Ctrl+N new; Esc: the menu, Export... to PDF,", "  HTML, Markdown and text", "",
  "#Edit", "Ctrl+Z undo, Ctrl+Y redo, Ctrl+C / X / V copy, cut, paste", "",
  "#Pad", "Share writes with the pad, Start the menu",
}

local function open_menu()
  for _, m in ipairs(MENU) do
    if m[1] == "Word completion" then m[2] = cfg.complete and "on" or "off"
    elseif m[1] == "Language" then m[2] = cfg.lang == "it" and "Italiano" or "English"
    elseif m[1] == "Pad typing" then m[2] = "Share: " .. (pt.mode() or "off") end
  end
  overlay = { kind = "menu", sel = 1 }
end

local function next_style()
  local s = doc[cur.p].s
  for i, id in ipairs(ORDER) do
    if id == s then set_style(ORDER[i % #ORDER + 1]); break end
  end
  say("style: " .. STY[doc[cur.p].s].name)
end

local function next_align()
  local a = doc[cur.p].a
  for i, id in ipairs(ALIGNS) do
    if id == a then set_align(ALIGNS[i % #ALIGNS + 1]); break end
  end
  say("alignment: " .. doc[cur.p].a)
end

local do_key

local function menu_choose(name)
  overlay = nil
  if name == "New document" then leave_doc(new_doc)
  elseif name == "Open..." then leave_doc(open_files)
  elseif name == "Save" then save_doc(false)
  elseif name == "Save as..." then save_doc(true)
  elseif name == "Export..." then overlay = { kind = "export", sel = 1 }
  elseif name == "Style" then next_style()
  elseif name == "Alignment" then next_align()
  elseif name == "Bold" then toggle(BOLD)
  elseif name == "Italic" then toggle(ITALIC)
  elseif name == "Underline" then toggle(UNDER)
  elseif name == "Select" then do_key("f3")
  elseif name == "Undo" then do_key("^z")
  elseif name == "Redo" then do_key("^y")
  elseif name == "Word completion" then
    cfg.complete = not cfg.complete
    comp = nil
    say(cfg.complete and "word completion on: Tab writes the grey-blue word" or "word completion off", C.acc)
  elseif name == "Language" then
    cfg.lang = cfg.lang == "it" and "en" or "it"
    words_ready = false
    say("words: " .. (cfg.lang == "it" and "Italiano" or "English"), C.acc)
  elseif name == "Pad typing" then
    pt.on(not pt.mode() and "compose" or nil)
    say(pt.mode() and "pad typing: the cross writes (Share: off)" or "pad typing off", C.acc)
  elseif name == "Keys" then overlay = { kind = "help" }
  elseif name == "Exit" then leave_doc(function() save_session(); dirty = false; quit() end)
  end
end

------------------------------------------------------------------ keys

do_key = function(k)
  local shift = keydown(0xE1) or keydown(0xE5)
  local nav = k == "up" or k == "down" or k == "left" or k == "right" or k == "home" or k == "end"
              or k == "pgup" or k == "pgdn"
  if nav then
    if shift and not anchor then anchor = { p = cur.p, c = cur.c }
    elseif not shift and anchor and not anchor.held then anchor = nil end
    if anchor and shift then anchor.held = nil end
    move(k)
    return
  end
  if k == "f3" then
    if anchor then anchor = nil; say("selection off")
    else anchor = { p = cur.p, c = cur.c, held = true }; say("selecting: move, then Ctrl+B / C / X... (F3: off)", C.acc) end
  elseif k == "\n" then delete_sel(); snapshot(); split(); moved()
  elseif k == "\b" then backspace()
  elseif k == "del" then delete_forward()
  elseif k == "\t" then
    if not accept() then snapshot("ins"); delete_sel(); insert_text("    ") end
  elseif k == "^b" then toggle(BOLD)
  elseif k == "^i" then toggle(ITALIC)
  elseif k == "^u" then toggle(UNDER)
  elseif k == "^l" then set_align("left")
  elseif k == "^e" then set_align("center")
  elseif k == "^j" then set_align("justify")
  elseif k == "f2" then next_style()
  elseif k == "f4" then next_align()
  elseif k == "f1" then overlay = { kind = "help" }
  elseif k == "^s" then save_doc(false)
  elseif k == "^S" then save_doc(true)
  elseif k == "^o" then leave_doc(open_files)
  elseif k == "^n" then leave_doc(new_doc)
  elseif k == "^z" then
    if #undo > 0 then redo[#redo + 1] = state_now(); restore(table.remove(undo)); say("undone") end
  elseif k == "^y" then
    if #redo > 0 then undo[#undo + 1] = state_now(); restore(table.remove(redo)); say("redone") end
  elseif k == "^c" or k == "^x" then
    local p0, c0, p1, c1 = sel_range()
    if p0 then
      clip_board = copy_range(p0, c0, p1, c1)
      if k == "^x" then snapshot(); delete_range(p0, c0, p1, c1) end
      say(k == "^x" and "cut" or "copied")
    end
  elseif k == "^v" then
    if clip_board then snapshot(); delete_sel(); paste(clip_board) end
  elseif k == "esc" then
    if anchor then anchor = nil else open_menu() end
  elseif #k == 1 and k:byte() >= 32 then
    snapshot(k == " " and "space" or "ins")
    delete_sel()
    insert_text(k)
    return "typed"
  end
end

local function overlay_key(k)
  local o = overlay
  if o.kind == "help" then overlay = nil; return end
  if o.kind == "prompt" then
    if k == "\n" then overlay = nil; o.done(o.text)
    elseif k == "esc" then overlay = nil
    elseif k == "\b" then o.text = o.fresh and "" or o.text:sub(1, -2)
    elseif #k == 1 and k:byte() >= 32 and (o.fresh or #o.text < 12) then o.text = (o.fresh and "" or o.text) .. k
    else return end
    o.fresh = false
    return
  end
  local n = o.kind == "menu" and #MENU or o.kind == "files" and #o.items or o.kind == "export" and #FORMATS
            or #o.choices
  if k == "up" then o.sel = (o.sel - 2) % n + 1
  elseif k == "down" then o.sel = o.sel % n + 1
  elseif k == "esc" then overlay = nil
  elseif k == "\n" then
    overlay = nil
    if o.kind == "menu" then menu_choose(MENU[o.sel][1])
    elseif o.kind == "files" then open_doc(o.items[o.sel].name)
    elseif o.kind == "export" then export(FORMATS[o.sel])
    else o.done(o.sel) end
  end
end

-- the pad, when it is not writing: the cross moves (and repeats), A a new
-- paragraph, B deletes, Start the menu
local rep = {}
local function pad_moves()
  local map = { [0] = "left", [1] = "right", [2] = "up", [3] = "down" }
  for b, dir in pairs(map) do
    if btnp(b) then rep[b] = 0; if overlay then overlay_key(dir) else move(dir) end
    elseif btn(b) then
      rep[b] = (rep[b] or 0) + 1
      if rep[b] > 18 and rep[b] % 3 == 0 then if overlay then overlay_key(dir) else move(dir) end end
    end
  end
  if btnp("ok") then if overlay then overlay_key("\n") else do_key("\n") end end
  if btnp("back") then if overlay then overlay_key("esc") else backspace() end end
  if btnp(8) then if overlay then overlay_key("esc") else open_menu() end end
end

------------------------------------------------------------------ frame

local frame = 0

function _init()
  keyp()                                -- typing on
  if keyhelp then keyhelp(KEYHELP, "bm Write") end
  pt.set({ fallback = "off" })
  font("8x16")
  new_doc()
  load_session()
  log("write: ready, " .. #doc .. " paragraphs" .. (file and (", " .. file) or ""))
end

-- Ctrl+Esc or PS: back to bm's menu; changes are asked about first, and
-- Ctrl+Esc again keeps them for next time
function _exit()
  if not dirty or (overlay and overlay.leaving) then save_session(); return true end
  leave_doc(function() save_session(); dirty = false; quit() end)
  if overlay then overlay.leaving = true end
  say("Ctrl+Esc again: the changes are kept for next time", C.acc)
  return false
end

local function keep_cursor_in_view()
  local _, y, st = cursor_xy()
  local vh = H - BAR - STAT
  local below = (pt.mode() and select(2, pt.size()) + 8 or 0)
  if y - view < 12 then view = math.max(0, y - 12) end
  if y + st.lh - view > vh - 12 - below then view = y + st.lh - vh + 12 + below end
end

function _update()
  frame = frame + 1
  if status_t > 0 then status_t = status_t - 1 end
  local k = keyp()
  local typed
  while k do
    comp_flash = nil
    if overlay then overlay_key(k)
    else
      local r = do_key(k)
      typed = typed or r == "typed" or k == "\b"
      if r ~= "typed" and k ~= "\b" then comp = nil end
    end
    k = keyp()
  end
  if typed and not overlay then suggest() end
  if (cfg.complete or pt.mode()) and not words_ready then words_ready = predict.preload({ cfg.lang }) end
  pad_host.lang = cfg.lang
  if overlay then
    pt.idle(pad_host)
    pad_moves()
  else
    local was = pt.mode()
    if pt.update(pad_host) then
      comp = nil
      if btnp(8) then open_menu() end
      if pt.mode() ~= was then
        say(pt.mode() and ("pad typing: " .. pt.mode()) or "pad typing off", C.acc)
      end
    else
      pad_moves()
    end
  end
  if doc[cur.p] == nil then cur.p = #doc end
  cur.c = math.max(0, math.min(cur.c, #doc[cur.p].t))
  layout()
  keep_cursor_in_view()
end

------------------------------------------------------------------ drawing

-- a run of letters: bold drawn twice, italic leaning (its top half a pixel
-- to the right), underline a line below
local function draw_run(s, x, y, bits, st, col)
  local sc = st.scale
  local fhs = st.fh * sc
  if bits & ITALIC ~= 0 then
    local top = fhs // 2
    clip(x, y, #s * st.cw + 2 * sc, top)
    print(s, x + sc, y, col, sc)
    if bits & BOLD ~= 0 then print(s, x + 2 * sc, y, col, sc) end
    clip(x, y + top, #s * st.cw + 2 * sc, fhs - top)
    print(s, x, y, col, sc)
    if bits & BOLD ~= 0 then print(s, x + sc, y, col, sc) end
    clip()
  else
    print(s, x, y, col, sc)
    if bits & BOLD ~= 0 then print(s, x + sc, y, col, sc) end
  end
  if bits & UNDER ~= 0 then rectfill(x, y + fhs, #s * st.cw, sc, col) end
end

local function draw_line(L, sx, sy, s0, s1)
  local p = doc[L.p]
  local st = STY[p.s]
  local x, a, e, per, extra = line_geom(p, L.li)
  local base = sx + MX
  -- the selection: letters [s0, s1) of this paragraph
  if s0 then
    local b = p.lines[L.li][2]
    local u0, u1 = math.max(s0, a), math.min(s1, b)
    if u1 > u0 or (u1 == u0 and s1 > b) then
      local x0 = letter_x(p, x, a, u0, per, extra)
      local x1 = u1 > u0 and letter_x(p, x, a, u1, per, extra) or x0 + st.cw
      rectfill(base + x0, sy, math.max(2, x1 - x0), st.lh, C.sel)
    end
  end
  local col = (p.s == "title" or p.s == "h1" or p.s == "h2") and C.head or C.ink
  local ty = sy + (st.lh - st.fh * st.scale) // 2
  font(st.font)
  if L.li == 1 then
    if p.s == "bullet" then circfill(base + st.indent - 11, sy + st.lh // 2, 2, C.ink)
    elseif p.s == "number" then
      local n = (p.num or 1) .. "."
      print(n, base + st.indent - (#n + 1) * st.cw, ty, C.ink, st.scale)
    end
  end
  if p.s == "quote" then rectfill(base + st.indent - 14, sy, 3, st.lh, C.quote) end
  local gapped = per > 0 or extra > 0
  local k = a
  while k < e do
    local raw = p.m:byte(k) - 48
    local j = k
    while j + 1 < e and p.m:byte(j + 1) - 48 == raw and not (gapped and (p.t:byte(j + 1) == 32 or p.t:byte(j) == 32)) do
      j = j + 1
    end
    local bits = raw | (st.bold and BOLD or 0) | (st.italic and ITALIC or 0)
    draw_run(p.t:sub(k, j), base + letter_x(p, x, a, k, per, extra), ty, bits, st, col)
    k = j + 1
  end
end

local function draw_page(page, sy)
  rectfill(PX + 4, sy + 4, PW, PH, C.shadow)
  rectfill(PX, sy, PW, PH, C.page)
  font("6x12")
  local n = tostring(page)
  print(n, PX + (PW - #n * 6) // 2, sy + PH - 34, C.faint)
end

local function draw_doc()
  layout()
  local vh = H - BAR - STAT
  clip(0, BAR, W, vh)
  rectfill(0, BAR, W, vh, C.desk)
  local y0 = BAR - view
  for page = 1, npages do
    local ty = y0 + page_top(page)
    if ty + PH >= BAR and ty < BAR + vh then draw_page(page, ty) end
  end
  -- the selection, as letters of each paragraph
  local p0, c0, p1, c1 = sel_range()
  -- the first line in view (they go down the document)
  local lo, hi = 1, #PL
  while lo < hi do
    local mid = (lo + hi) // 2
    local L = PL[mid]
    if page_top(L.page) + L.y + STY[doc[L.p].s].lh < view then lo = mid + 1 else hi = mid end
  end
  for i = lo, #PL do
    local L = PL[i]
    local sy = y0 + page_top(L.page) + L.y
    if sy > BAR + vh then break end
    local s0, s1
    if p0 and L.p >= p0 and L.p <= p1 then
      s0 = L.p == p0 and c0 + 1 or 1
      s1 = L.p == p1 and c1 + 1 or #doc[L.p].t + 2
    end
    draw_line(L, PX, sy, s0, s1)
  end
  -- the cursor (a blinking bar), the suggestion after it
  if not overlay then
    local cx, cy, st = cursor_xy()
    local sx, sy = PX + cx, y0 + cy
    local c = comp_here()
    if c and cfg.complete then
      font(st.font)
      print(c.rest, sx, sy + (st.lh - st.fh * st.scale) // 2, C.ghost, st.scale)
    end
    if pt.mode() then
      local pd, g = pt.pending(), pt.ghost()
      font(st.font)
      local x = sx
      if pd then x = print(pd == " " and "_" or pd, x, sy + (st.lh - st.fh * st.scale) // 2, pt.C_PEND, st.scale) end
      if g ~= "" then print(g, x, sy + (st.lh - st.fh * st.scale) // 2, pt.C_GHOST, st.scale) end
    end
    if (frame // 30) % 2 == 0 then rectfill(sx, sy + 1, 2, st.lh - 2, C.cursor) end
  end
  clip()
end

-- the toolbar: the name, then the style, B I U and the alignment of the cursor
local function draw_bar()
  font("8x16")
  rectfill(0, 0, W, BAR, C.bar)
  local x = print("bm Write", 0, 0, C.acc) + 16
  local name = (file or "Untitled") .. (dirty and " *" or "")
  print(name, x, 0, C.text)
  local p = doc[cur.p]
  local bits = cur_bits()
  local right = W
  local function chip(s, on)
    right = right - #s * 8 - 8
    if on then rectfill(right - 4, 0, #s * 8 + 8, BAR, C.on) end
    print(s, right, 0, on and 0xFFFFFF or C.dim)
  end
  for i = #ALIGNS, 1, -1 do
    local a = ALIGNS[i]
    chip(a == "left" and "L" or a == "center" and "C" or a == "right" and "R" or "J", p.a == a)
  end
  right = right - 8
  chip("U", bits & UNDER ~= 0)
  chip("I", bits & ITALIC ~= 0)
  chip("B", bits & BOLD ~= 0)
  right = right - 8
  local sname = STY[p.s].name
  right = right - #sname * 8
  rectfill(right - 8, 0, #sname * 8 + 16, BAR, C.bar2)
  print(sname, right, 0, C.text)
end

local function draw_status()
  font("8x16")
  local y = H - STAT
  rectfill(0, y, W, STAT, C.bar)
  local k = cursor_line()
  local page = PL[k] and PL[k].page or 1
  local left = "Page " .. page .. " of " .. npages .. "   " .. count_words() .. " words"
  if pt.mode() then left = left .. "   PAD " .. pt.mode() end
  if status_t > 0 then
    print(status:sub(1, W // 8 - 1), 0, y, status_c)
  else
    print(left, 0, y, C.dim)
    local r = cfg.lang == "it" and "Italiano" or "English"
    print(r, W - #r * 8, y, C.dim)
  end
end

local function box(c0, r0, cols, rows, title)
  rectfill(c0 * 8, r0 * 16, cols * 8, rows * 16, C.panel)
  rect(c0 * 8, r0 * 16, cols * 8, rows * 16, C.edge)
  if title then
    rectfill(c0 * 8 + 1, r0 * 16 + 1, cols * 8 - 2, 15, C.bar2)
    print(title, (c0 + 1) * 8, r0 * 16, C.acc)
  end
end

local function draw_overlay()
  local o = overlay
  if not o then return end
  font("8x16")
  if o.kind == "menu" then
    local cols = 44
    local c0 = (W // 8 - cols) // 2
    box(c0, 1, cols, #MENU + 2, "bm Write")
    for i, m in ipairs(MENU) do
      local y = (1 + i) * 16
      if i == o.sel then rectfill((c0 + 1) * 8, y, (cols - 2) * 8, 16, C.hot) end
      print(m[1], (c0 + 2) * 8, y, C.text)
      print(m[2], (c0 + cols - 2 - #m[2]) * 8, y, C.dim)
    end
  elseif o.kind == "export" then
    local cols = 36
    local c0 = (W // 8 - cols) // 2
    box(c0, 6, cols, #FORMATS + 3, "Export to /docs as")
    for i, f in ipairs(FORMATS) do
      local y = (7 + i) * 16
      if i == o.sel then rectfill((c0 + 1) * 8, y, (cols - 2) * 8, 16, C.hot) end
      print(f[1], (c0 + 2) * 8, y, C.text)
      local n = (base_name() or "NAME") .. "." .. f[2]
      print(n, (c0 + cols - 2 - #n) * 8, y, C.dim)
    end
  elseif o.kind == "files" then
    local cols, rows = 48, math.min(#o.items, 14)
    local c0 = (W // 8 - cols) // 2
    box(c0, 2, cols, rows + 3, "Open a document (/docs)")
    if o.sel < o.top then o.top = o.sel end
    if o.sel >= o.top + rows then o.top = o.sel - rows + 1 end
    for i = o.top, math.min(#o.items, o.top + rows - 1) do
      local f = o.items[i]
      local y = (4 + i - o.top) * 16
      if i == o.sel then rectfill((c0 + 1) * 8, y, (cols - 2) * 8, 16, C.hot) end
      print(f.name, (c0 + 2) * 8, y, C.text)
      local sz = f.size < 1024 and (f.size .. " B") or ((f.size + 1023) // 1024 .. " KiB")
      print(sz, (c0 + cols - 2 - #sz) * 8, y, C.dim)
    end
  elseif o.kind == "prompt" then
    local cols = 52
    local c0 = (W // 8 - cols) // 2
    box(c0, 8, cols, 5, nil)
    print(o.label, (c0 + 2) * 8, 9 * 16, C.text)
    rectfill((c0 + 2) * 8, 11 * 16, (cols - 4) * 8, 16, C.bar2)
    if o.fresh and #o.text > 0 then rectfill((c0 + 3) * 8, 11 * 16, #o.text * 8, 16, C.hot) end
    print(o.text .. ((frame // 30) % 2 == 0 and "_" or ""), (c0 + 3) * 8, 11 * 16, 0xFFFFFF)
  elseif o.kind == "confirm" then
    local cols = 52
    local c0 = (W // 8 - cols) // 2
    box(c0, 8, cols, 6, nil)
    print(o.question, (c0 + 2) * 8, 9 * 16, C.text)
    local x = (c0 + 2) * 8
    for i, ch in ipairs(o.choices) do
      if i == o.sel then rectfill(x - 8, 11 * 16, (#ch + 2) * 8, 16, C.hot) end
      print(ch, x, 11 * 16, C.text)
      x = x + (#ch + 4) * 8
    end
  elseif o.kind == "help" then
    font("6x12")
    local cols, rows = 64, #HELP + 3
    local c0 = (W // 6 - cols) // 2
    rectfill(c0 * 6, 12, cols * 6, rows * 12, C.panel)
    rect(c0 * 6, 12, cols * 6, rows * 12, C.edge)
    print("bm Write: the keys (any key closes)", (c0 + 2) * 6, 24, C.acc)
    for i, l in ipairs(HELP) do
      local head = l:sub(1, 1) == "#"
      print(head and l:sub(2) or l, (c0 + 2) * 6, (2 + i) * 12, head and C.acc or C.text)
    end
  end
end

function _draw()
  cls(C.desk)
  draw_doc()
  draw_bar()
  draw_status()
  if pt.mode() then
    local ow, oh = pt.size()
    pt.draw((W - ow) // 2, H - STAT - oh - 4)
  end
  draw_overlay()
end
