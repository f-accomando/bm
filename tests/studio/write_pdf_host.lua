-- bm Write (carts/write): the PDF it exports against the screen it draws (M45).
-- A document with every style, alignment, bold, italic, underline, accents,
-- brackets and a backslash, on five pages, is opened and exported to PDF. A
-- second copy of the cart runs on a screen tall enough to show every page;
-- what it prints and draws is collected. A small PDF reader of this file (it
-- shares no code with the cart) reads the xref, the pages and their content
-- streams. Then, page by page: the same runs of letters at the same place
-- (the screen's pixels times 595/540 points), the same font (Courier, bold,
-- oblique), the same size (6/10 of it is a letter's width), the underlines,
-- the quote bars, the bullets, the numbers, the page numbers and the tables
-- (the columns of a run of rows, the thin lines of the bars, a row too wide
-- for the page wrapped as body text).
--
--   luahost tests/studio/write_pdf_host.lua ROOT [OUT.pdf]     (make test-studio)
local ROOT = arg and arg[1] or "."
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. what .. "\n") end
end

local PW, PH, GAP, TOP, BAR = 540, 764, 18, 12, 16      -- the screen, as the cart draws it
local PT = 595 / PW
local NPAGES = 5
local HEIGHT = BAR + TOP + NPAGES * (PH + GAP) + 40     -- every page in view

------------------------------------------------------------ the document
local BOLD, ITALIC, UNDER = 1, 2, 4
local WANT = {}                          -- the words each paragraph must have in the PDF
local function para(s, a, ...)
  local text, runs = "", {}
  for _, seg in ipairs({ ... }) do
    local t, bits = seg, 0
    if type(seg) == "table" then t, bits = seg[1], seg[2] end
    if bits ~= 0 then runs[#runs + 1] = string.format("%d:%d:%d", #text + 1, #t, bits) end
    text = text .. t
  end
  if s == "table" then                  -- as save_text writes a row: "body table", the bars U+2502
    WANT[#WANT + 1] = (text:gsub("%s*|%s*", " "))
    return "body table " .. table.concat(runs, ",") .. "|" .. text:gsub("|", "\u{2502}")
  end
  WANT[#WANT + 1] = text
  return s .. " " .. a .. " " .. table.concat(runs, ",") .. "|" .. text
end
local lorem = "Questo paragrafo è abbastanza lungo da andare a capo più volte, con parole di varia lunghezza e qualche "
local P = {
  para("title", "center", "Relazione è già più"),
  para("h1", "left", "Un titolo di primo livello"),
  para("h2", "left", "Un titolo ", { "minore", ITALIC }),
  para("body", "left", lorem, { "grassetto", BOLD }, " e ", { "corsivo", ITALIC }, " e ", { "sottolineato", UNDER },
       " e ", { "tutto insieme", BOLD | ITALIC | UNDER }, ", poi finisce con f(x) = a\\b (nota) e un 50% di °C ± ñ ç ü."),
  para("body", "justify", lorem, { "parole giustificate", BOLD }, " e spazi doppi  qui  e altre parole per riempire " ..
       "ancora due righe di testo, perché la giustificazione allarghi gli spazi fino al margine destro, ", { "corsivo", ITALIC },
       " compreso, e l'ultima riga resti a sinistra."),
  para("body", "right", "Un paragrafo a destra, con ", { "un po'", UNDER }, " di sottolineato"),
  para("body", "center", "Un paragrafo al centro (con le parentesi)"),
  para("quote", "left", "Una citazione abbastanza lunga da occupare due righe o tre, in corsivo, con il ", { "grassetto", BOLD },
       " dentro e una barra a sinistra, per vedere che il rientro vale anche dopo l'a capo."),
  para("bullet", "left", "primo punto"),
  para("bullet", "left", "secondo punto, lungo abbastanza da andare a capo nella riga e mostrare il rientro della lista"),
  para("bullet", "left", "terzo punto"),
  para("number", "left", "uno"),
  para("number", "left", "due ", { "forte", BOLD }),
  para("number", "left", "tre"),
  para("body", "left", "Dopo la lista: un paragrafo di corpo. Poi uno vuoto e uno di soli spazi."),
  para("body", "left", ""),
  para("body", "left", "      "),
  para("body", "left", "Spazi alla fine della riga   "),
}
for i = 1, 40 do
  P[#P + 1] = para(i % 7 == 0 and "h2" or "body", i % 5 == 0 and "justify" or "left",
                   "Paragrafo " .. i .. ": " .. lorem .. (i % 2 == 0 and "no" or "sì") .. " e una parola molto lunga alla fine.")
end
for i = 1, 8 do P[#P + 1] = para("number", "left", "elenco " .. i) end
-- a table of four rows (the columns 15, 3 and 6 wide), then one too wide for the page
P[#P + 1] = para("body", "left", "Una tabella con tre colonne:")
P[#P + 1] = para("table", "left", { "Voce", BOLD }, "|", { "Qta", BOLD }, "|", { "Prezzo", BOLD })
P[#P + 1] = para("table", "left", "Mela|3|1.20")
P[#P + 1] = para("table", "left", { "Pera", UNDER }, "|12|0.5")
P[#P + 1] = para("table", "left", "Albicocca secca|120|10.00")
P[#P + 1] = para("body", "left", "Dopo la tabella, una riga troppo larga:")
local WIDE = string.rep("x", 50) .. "|" .. string.rep("y", 30)
P[#P + 1] = "body table |" .. WIDE:gsub("|", "\u{2502}")
P[#P + 1] = "body table |a\u{2502}b"
local BMD = "bmwrite 1\ntitle=Relazione\n\n" .. table.concat(P, "\n") .. "\n"

------------------------------------------------------------ the cart on a tall screen
local E = {}
for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, HEIGHT
local ev, keyq, frame = {}, {}, 0
local docs = { ["rel.bmd"] = BMD }
local FW = { ["6x12"] = { 6, 12 }, ["8x16"] = { 8, 16 } }
local cur_font = "8x16"
E.cls = function() end
E.pset, E.sset, E.line = E.cls, E.cls, E.cls
E.rectfill = function(x, y, w, h, c) ev[#ev + 1] = { "rect", x, y, w, h, c } end
E.circfill = function(x, y, r, c) ev[#ev + 1] = { "circ", x, y, r, c } end
E.rect = E.cls
E.clip = function(...) ev[#ev + 1] = { "clip", select("#", ...) } end
E.font = function(f) cur_font = f or cur_font; return cur_font end
E.print = function(s, x, y, c, sc)
  s, sc = tostring(s), sc or 1
  ev[#ev + 1] = { "print", s, x or 0, y or 0, sc, cur_font }
  return (x or 0) + #s * FW[cur_font][1] * sc
end
E.log, E.quit = E.cls, E.cls
E.time = function() return frame / 60 end
E.btn, E.btnp, E.keydown = function() return false end, function() return false end, function() return false end
E.keyp = function() return table.remove(keyq, 1) end
E.keyhelp = function() return 0 end
E.prompt = function(n, x) if type(x) ~= "number" then return 16, 16 end; return x + 16 end   -- (the chip "F12 keys" of the status line)
E.save, E.saved = function() return true end, function() return nil end
E.doc_write = function(n, d) docs[n] = d; return true end
E.doc_read = function(n) return docs[n:lower()] end
E.doc_list = function()
  local out = {}
  for n, d in pairs(docs) do out[#out + 1] = { name = n:upper(), size = #d } end
  return out
end
E.mouse = function(on) if on ~= nil then return true end; return 0, 0, 0, 0, false end
E.mousep = function() return false end
local bmui
E.require = function(name)
  if name == "bmui" then
    bmui = bmui or assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()
    return bmui
  end
  if name == "predict" then return { preload = function() return true end, complete = function() return nil end } end
  if name == "padtype" then
    local pt = { C_PEND = 0, C_GHOST = 0 }
    return setmetatable(pt, { __index = function() return function() return nil end end })
  end
  error("require: " .. name)
end
assert(loadfile(ROOT .. "/carts/write/main.lua", "t", E))()
local function tick() frame = frame + 1; ev = {}; E._update(); E._draw() end
local function keys(...) for _, k in ipairs({ ... }) do keyq[#keyq + 1] = k end; tick(); tick() end

E._init()
tick()
keys("^o")
keys("\n")                                  -- the only file in the list
keys("esc", "down", "down", "down", "down", "\n")      -- the menu: Export...
keys("\n")                                  -- PDF
local pdf = docs["rel.pdf"]
check(pdf ~= nil, "export: /docs/rel.pdf written")
if not pdf then print("write pdf: 1 of 1 FAILED"); os.exit(1) end
if arg and arg[2] then                      -- luahost ... ROOT out.pdf: keep the PDF to open it in a viewer
  local f = assert(io.open(arg[2], "wb")); f:write(pdf); f:close()
end
tick()
local scr = ev                              -- the last frame: every page in view

------------------------------------------------------------ a small PDF reader
local function fail_exit() print(fails .. " of " .. checks .. " FAILED"); os.exit(1) end
check(pdf:sub(1, 9) == "%PDF-1.4\n" and pdf:sub(-6) == "%%EOF\n", "PDF: header and end")
local xoff = tonumber(pdf:match("startxref\n(%d+)\n%%%%EOF"))
check(xoff and pdf:sub(xoff + 1, xoff + 5) == "xref\n", "PDF: startxref points to xref")
local nobj = tonumber(pdf:match("xref\n0 (%d+)\n")) - 1
local objs = {}
do
  local pos = pdf:find("0000000000 65535 f", xoff + 1, true)
  for i = 1, nobj do
    local off = tonumber(pdf:sub(pos + 20 * i, pos + 20 * i + 9))
    local head = pdf:sub(off + 1, off + #(i .. " 0 obj\n"))
    check(head == i .. " 0 obj\n", "PDF: object " .. i .. " where the xref says")
    objs[i] = pdf:match("^%d+ 0 obj\n(.-)\nendobj\n", off + 1)
  end
end
local root = objs[1]
check(root and root:find("/Type /Catalog", 1, true), "PDF: catalog")
local kids = {}
for n in objs[2]:match("/Kids %[(.-)%]"):gmatch("(%d+) 0 R") do kids[#kids + 1] = tonumber(n) end
check(#kids == NPAGES and tonumber(objs[2]:match("/Count (%d+)")) == NPAGES, "PDF: " .. NPAGES .. " pages (" .. #kids .. ")")
for _, n in ipairs({ 3, 4, 5, 6 }) do check(objs[n]:find("/Subtype /Type1", 1, true), "PDF: font object " .. n) end
check(objs[3]:find("/BaseFont /Courier ", 1, true) and objs[4]:find("/BaseFont /Courier-Bold ", 1, true)
      and objs[5]:find("/BaseFont /Courier-Oblique ", 1, true) and objs[6]:find("/BaseFont /Courier-BoldOblique ", 1, true),
      "PDF: Courier, Bold, Oblique, BoldOblique are F1 to F4")

-- WinAnsi byte to Unicode, and the screen's code page 437 byte to Unicode (the letters the document uses)
local W2U = { [0x95] = 0x2022 }
local C2U = { [0x8A] = 0xE8, [0x85] = 0xE0, [0x97] = 0xF9, [0x81] = 0xFC, [0xA4] = 0xF1, [0x87] = 0xE7,
              [0xF8] = 0xB0, [0xF1] = 0xB1, [0x8D] = 0xEC, [0x82] = 0xE9, [0x95] = 0xF2, [0x9C] = 0xA3 }
local function pdf_text(raw)                   -- the string between the brackets, unescaped, as Unicode
  local out, i = {}, 1
  while i <= #raw do
    local ch = raw:sub(i, i)
    local b
    if ch == "\\" then
      local oct = raw:match("^%d%d%d", i + 1)
      if oct then b, i = tonumber(oct, 8), i + 4 else b, i = raw:byte(i + 1), i + 2 end
    else b, i = ch:byte(), i + 1 end
    out[#out + 1] = utf8.char(W2U[b] or b)
  end
  return table.concat(out)
end
local function scr_text(s)
  return (s:gsub(".", function(c) local b = c:byte(); return utf8.char(b < 128 and b or (C2U[b] or 0xFFFD)) end))
end

local function read_page(n)
  local content = tonumber(objs[kids[n]]:match("/Contents (%d+) 0 R"))
  check(objs[kids[n]]:find("/MediaBox [0 0 595 842]", 1, true), "PDF: page " .. n .. " is A4")
  local len = tonumber(objs[content]:match("/Length (%d+)"))
  local stream = objs[content]:match("stream\n(.*)\nendstream$")
  check(stream and #stream == len, "PDF: page " .. n .. ": /Length is the stream's")
  local page = { text = {}, lines = {}, fills = {}, tbars = {} }
  for ln in (stream or ""):gmatch("[^\n]+") do
    local f, sz, x, y, s = ln:match("^BT /F(%d) ([%d.]+) Tf ([%d.-]+) ([%d.-]+) Td %((.*)%) Tj ET$")
    if f then
      page.text[#page.text + 1] = { font = tonumber(f), size = tonumber(sz), x = tonumber(x), y = tonumber(y), s = pdf_text(s) }
    else
      local lw, x0, y0, x1, y1 = ln:match("^([%d.]+) w ([%d.-]+) ([%d.-]+) m ([%d.-]+) ([%d.-]+) l S$")
      if lw then
        page.lines[#page.lines + 1] = { x0 = tonumber(x0), y = tonumber(y0), x1 = tonumber(x1), w = tonumber(lw) }
      else
        local rx, ry, rw, rh = ln:match("^[%d. ]+ rg ([%d.-]+) ([%d.-]+) ([%d.-]+) ([%d.-]+) re f 0 g$")
        if rx then
          local f = { x = tonumber(rx), y = tonumber(ry), w = tonumber(rw), h = tonumber(rh) }
          if f.w < 2 then page.tbars[#page.tbars + 1] = f else page.fills[#page.fills + 1] = f end
        else check(false, "PDF: page " .. n .. ": a line of the stream that is not known: " .. ln) end
      end
    end
  end
  return page
end

------------------------------------------------------------ the screen's pages
local tops = {}
for _, e in ipairs(scr) do
  if e[1] == "rect" and e[4] == PW and e[5] == PH and e[6] == 0xFFFFFF then tops[#tops + 1] = e[3] end
end
check(#tops == NPAGES, "screen: " .. NPAGES .. " pages drawn (" .. #tops .. ")")
local function page_of(y)
  for i = #tops, 1, -1 do if y >= tops[i] then return i, y - tops[i] end end
end
local PX = (640 - PW) // 2
local FACE = { ["6x12"] = { 6, 12 }, ["8x16"] = { 8, 16 } }

-- the runs the screen drew: bold prints twice, italic prints twice inside two clips
local runs, rects, circs = {}, {}, {}
do
  local i, n = 1, #scr
  while i <= n do
    local e = scr[i]
    if e[1] == "print" then
      local nxt = scr[i + 1]
      local r = { s = e[2], x = e[3], y = e[4], sc = e[5], face = e[6], bold = false, italic = false }
      if nxt and nxt[1] == "print" and nxt[2] == e[2] and nxt[4] == e[4] and nxt[3] == e[3] + e[5] then
        r.bold = true; i = i + 1
      end
      runs[#runs + 1] = r
    elseif e[1] == "clip" and e[2] == 4 and scr[i + 1] and scr[i + 1][1] == "print" then
      -- italic: clip(upper) print [print] clip(lower) print [print] clip()
      local up, k = scr[i + 1], i + 2
      local r = { s = up[2], sc = up[5], face = up[6], italic = true, bold = false, y = up[4] }
      if scr[k][1] == "print" then r.bold = true; k = k + 1 end
      assert(scr[k][1] == "clip" and scr[k][2] == 4 and scr[k + 1][1] == "print", "screen: italic run shape")
      r.x = scr[k + 1][3]
      k = k + 2
      if scr[k][1] == "print" then k = k + 1 end
      assert(scr[k][1] == "clip" and scr[k][2] == 0, "screen: italic run ends with clip()")
      i = k
      runs[#runs + 1] = r
    elseif e[1] == "rect" and not (e[4] == PW and e[5] == PH) then
      if e[6] ~= 0xFFFFFF and e[2] ~= 0 then rects[#rects + 1] = { x = e[2], y = e[3], w = e[4], h = e[5] } end
    elseif e[1] == "circ" then circs[#circs + 1] = { x = e[2], y = e[3], r = e[4] }
    end
    i = i + 1
  end
end

local total_runs, total_under, total_bars, total_bullets, total_tbars = 0, 0, 0, 0, 0
for n = 1, NPAGES do
  local pg = read_page(n)
  -- the screen's side of this page
  local st = {}
  for _, r in ipairs(runs) do
    local pn, ry = page_of(r.y)
    if pn == n and r.s:find("[^ ]") and r.y < tops[n] + PH - 40 then
      local fw, fh = FACE[r.face][1] * r.sc, FACE[r.face][2] * r.sc
      st[#st + 1] = { s = scr_text(r.s), x = (r.x - PX) * PT, y = 842 - (ry + fh * 0.78) * PT, size = fw * PT / 0.6,
                      bold = r.bold, italic = r.italic, fw = fw }
    elseif pn == n and r.s:find("^%d+$") and r.y >= tops[n] + PH - 40 then
      -- the page number: the pdf puts it by the same margin in 9 points
      local want = tostring(n)
      local got
      for _, t in ipairs(pg.text) do if t.s == want and t.y < 40 then got = t end end
      check(got ~= nil and got.size == 9 and got.font == 1, "page " .. n .. ": the page number is in the PDF, Courier 9")
      if got then
        local sx = (r.x - PX) * PT
        check(math.abs(got.x - sx) < 4, string.format("page %d: page number x %.2f against the screen %.2f", n, got.x, sx))
        local sy = 842 - (r.y - tops[n] + 12 * 0.78) * PT
        check(math.abs(got.y - sy) < 4, string.format("page %d: page number y %.2f against the screen %.2f", n, got.y, sy))
      end
    end
  end
  local pt_ = {}
  for _, t in ipairs(pg.text) do
    if not (t.size == 9 and t.y < 40 and t.s:match("^%d+$")) then pt_[#pt_ + 1] = t end
  end
  -- the bullets: a glyph in the PDF, a dot on the screen
  local pbul, sbul = {}, 0
  for i = #pt_, 1, -1 do
    if pt_[i].s == "\u{2022}" then pbul[#pbul + 1] = table.remove(pt_, i) end
  end
  for _, c in ipairs(circs) do
    local pn, ry = page_of(c.y)
    if pn == n then
      sbul = sbul + 1
      local cx, cy = (c.x - PX) * PT, 842 - ry * PT
      local near
      for _, b in ipairs(pbul) do
        if math.abs(b.x + b.size * 0.3 - cx) < 4 and math.abs(b.y + b.size * 0.25 - cy) < 4 then near = b end
      end
      check(near ~= nil, string.format("page %d: a bullet at the screen dot (%.1f, %.1f)", n, cx, cy))
    end
  end
  check(sbul == #pbul and sbul >= (n == 1 and 3 or 0), string.format("page %d: %d bullets on the screen, %d in the PDF", n, sbul, #pbul))
  total_bullets = total_bullets + sbul
  -- the text runs
  table.sort(pt_, function(a, b) if math.abs(a.y - b.y) > 0.01 then return a.y > b.y end; return a.x < b.x end)
  table.sort(st, function(a, b) if math.abs(a.y - b.y) > 0.01 then return a.y > b.y end; return a.x < b.x end)
  check(#st == #pt_, string.format("page %d: %d runs on the screen, %d in the PDF", n, #st, #pt_))
  local bad = 0
  for i = 1, math.min(#st, #pt_) do
    local a, b = st[i], pt_[i]
    local font = 1 + (a.bold and 1 or 0) + (a.italic and 2 or 0)
    local ok = a.s == b.s and b.font == font and math.abs(a.size - b.size) < 0.01 and math.abs(a.x - b.x) < 0.01
               and math.abs(a.y - b.y) < 0.01
    -- a letter is 0.6 of the size wide: the PDF's run is as wide as the screen's
    if ok then ok = math.abs(utf8.len(b.s) * 0.6 * b.size - utf8.len(a.s) * a.fw * PT) < 0.1 end
    if not ok then
      bad = bad + 1
      if bad <= 3 then
        io.write(string.format("  page %d run %d: screen %q F%d %.2fpt at %.2f,%.2f / PDF %q F%d %.2fpt at %.2f,%.2f\n", n, i,
                 a.s, font, a.size, a.x, a.y, b.s, b.font, b.size, b.x, b.y))
      end
    end
  end
  check(bad == 0, string.format("page %d: %d of %d runs differ", n, bad, #st))
  total_runs = total_runs + #st
  -- underlines: the screen's thin rects below a run, the PDF's lines
  local su = {}
  for _, r in ipairs(rects) do
    local pn, ry = page_of(r.y)
    if pn == n and r.h <= 2 and r.w > 0 then su[#su + 1] = { x = (r.x - PX) * PT, x1 = (r.x + r.w - PX) * PT, y = 842 - ry * PT } end
  end
  check(#su == #pg.lines, string.format("page %d: %d underlines on the screen, %d in the PDF", n, #su, #pg.lines))
  table.sort(su, function(a, b) if math.abs(a.y - b.y) > 0.01 then return a.y > b.y end; return a.x < b.x end)
  table.sort(pg.lines, function(a, b) if math.abs(a.y - b.y) > 3 then return a.y > b.y end; return a.x0 < b.x0 end)
  table.sort(su, function(a, b) if math.abs(a.y - b.y) > 3 then return a.y > b.y end; return a.x < b.x end)
  local ubad = 0
  for i = 1, math.min(#su, #pg.lines) do
    local a, b = su[i], pg.lines[i]
    if not (math.abs(a.x - b.x0) < 0.02 and math.abs(a.x1 - b.x1) < 0.02 and math.abs(a.y - b.y) < 3) then ubad = ubad + 1 end
  end
  check(ubad == 0, string.format("page %d: %d underlines are not where the screen has them", n, ubad))
  total_under = total_under + #su
  -- the quote bars
  local sq = {}
  for _, r in ipairs(rects) do
    local pn, ry = page_of(r.y)
    if pn == n and r.w == 3 and r.h > 2 then sq[#sq + 1] = { x = (r.x - PX) * PT, w = r.w * PT, top = 842 - ry * PT, h = r.h * PT } end
  end
  check(#sq == #pg.fills, string.format("page %d: %d quote bars on the screen, %d in the PDF", n, #sq, #pg.fills))
  table.sort(sq, function(a, b) return a.top > b.top end)
  table.sort(pg.fills, function(a, b) return a.y + a.h > b.y + b.h end)
  for i = 1, math.min(#sq, #pg.fills) do
    local a, b = sq[i], pg.fills[i]
    check(math.abs(a.x - b.x) < 0.02 and math.abs(a.w - b.w) < 0.02 and math.abs(a.h - b.h) < 0.02
          and math.abs(a.top - (b.y + b.h)) < 0.02, string.format("page %d: quote bar %d", n, i))
  end
  total_bars = total_bars + #sq
  -- the tables' bars: thin lines, one pixel wide and a line high
  local sb = {}
  for _, r in ipairs(rects) do
    local pn, ry = page_of(r.y)
    if pn == n and r.w == 1 and r.h == 15 then sb[#sb + 1] = { x = (r.x - PX) * PT, top = 842 - ry * PT } end
  end
  check(#sb == #pg.tbars, string.format("page %d: %d table bars on the screen, %d in the PDF", n, #sb, #pg.tbars))
  table.sort(sb, function(a, b) if math.abs(a.top - b.top) > 0.01 then return a.top > b.top end; return a.x < b.x end)
  table.sort(pg.tbars, function(a, b) if math.abs(a.y + a.h - b.y - b.h) > 0.01 then return a.y + a.h > b.y + b.h end; return a.x < b.x end)
  for i = 1, math.min(#sb, #pg.tbars) do
    local a, b = sb[i], pg.tbars[i]
    check(math.abs(a.x - b.x) < 0.02 and math.abs(a.top - (b.y + b.h)) < 0.02 and math.abs(b.w - PT) < 0.02
          and math.abs(b.h - 15 * PT) < 0.02, string.format("page %d: table bar %d", n, i))
  end
  total_tbars = total_tbars + #sb
  -- nothing in the margins
  for _, t in ipairs(pt_) do
    check(t.x >= 54 * PT - 0.01 and t.x + utf8.len(t.s) * 0.6 * t.size <= (PW - 54) * PT + 0.5 and t.y > 64 * PT - 1 and t.y < 842 - 54 * PT + 1,
          string.format("page %d: %q inside the margins (x %.1f y %.1f)", n, t.s, t.x, t.y))
  end
end
check(total_runs > 200, "enough runs compared (" .. total_runs .. ")")
check(total_tbars == 8, "table bars all compared (" .. total_tbars .. ")")
check(total_under >= 3 and total_bars >= 2 and total_bullets >= 3, string.format("underlines, bars and bullets all compared (%d, %d, %d)", total_under, total_bars, total_bullets))

-- the text of the whole document in reading order is the document's own
local want = {}
for _, t in ipairs(WANT) do
  if t:find("[^ ]") then want[#want + 1] = t:gsub("[ ]+", " ") end
end
local got = {}
for n = 1, NPAGES do
  local pg = read_page(n)
  table.sort(pg.text, function(a, b) if math.abs(a.y - b.y) > 0.01 then return a.y > b.y end; return a.x < b.x end)
  for _, t in ipairs(pg.text) do
    if not (t.size == 9 and t.y < 40) and t.s ~= "\u{2022}" and not t.s:match("^%d+%.$") then got[#got + 1] = t.s end
  end
end
local joined = table.concat(got, " "):gsub("[ ]+", " ")
local all_ok, miss = true, nil
for _, t in ipairs(want) do
  -- a paragraph's words come in order, in pieces: its first and its last six words are there
  local first = t:match("^(%S+ %S+ %S+)")
  local last = t:match("(%S+ %S+)$")
  if first and not joined:find(first, 1, true) then all_ok, miss = false, first end
  if last and not joined:find(last, 1, true) then all_ok, miss = false, last end
end
check(all_ok, "the PDF holds the words of every paragraph (missing: " .. tostring(miss) .. ")")
check(joined:find("f(x) = a\\b (nota)", 1, true) ~= nil, "brackets and backslash come back from the PDF")
check(joined:find("è già più", 1, true) ~= nil, "accents come back from the PDF (WinAnsi)")

-- the table's columns in the PDF: one x for every cell of a column, 18 and 24 letters on (Albicocca secca is 15
-- wide, Qta 3 and the bars take 3 each); the row too wide is body text, bars and all
do
  local xs = {}
  for n = 1, NPAGES do
    for _, t in ipairs(read_page(n).text) do xs[t.s] = t.x; xs.size = xs.size or t.size end
  end
  local c1, c2, c3 = xs["Voce"], xs["Qta"], xs["Prezzo"]
  check(c1 and c1 == xs["Mela"] and c1 == xs["Pera"] and c1 == xs["Albicocca secca"], "pdf table: the first column's cells start at one x")
  check(c2 and c2 == xs["3"] and c2 == xs["12"] and c2 == xs["120"], "pdf table: the second column's")
  check(c3 and c3 == xs["1.20"] and c3 == xs["0.5"] and c3 == xs["10.00"], "pdf table: the third column's")
  local cw = 6 * PT
  check(c1 and c2 and c3 and math.abs(c2 - c1 - 18 * cw) < 0.01 and math.abs(c3 - c1 - 24 * cw) < 0.01,
        "pdf table: the columns are 18 and 24 letters from the first")
  check(joined:find(string.rep("x", 50) .. "|" .. string.rep("y", 21), 1, true) ~= nil,
        "pdf table: a row too wide is body text, with its bar a letter")
end

print(fails == 0 and ("write pdf: " .. checks .. "/" .. checks .. " checks passed, " .. total_runs .. " runs, " .. total_under
      .. " underlines, " .. total_bars .. " bars, " .. total_bullets .. " bullets") or (fails .. " of " .. checks .. " FAILED"))
if fails > 0 then os.exit(1) end
