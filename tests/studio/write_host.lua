-- bm Write (carts/write) on the PC with a mouse and a keyboard (M48): the
-- bm API as stand-ins (a table is /docs), keys typed into it, the pointer
-- moved, pressed and rolled frame by frame, and checks on the document it
-- saves and the words it prints. No pad.
--
--   luahost tests/studio/write_host.lua ROOT     (make test-studio)
local ROOT = arg and arg[1] or "."
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. what .. "\n") end
end

local E = {}
for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, 360
local texts, keyq, frame = {}, {}, 0
local docs = {}                           -- /docs: name -> text
local function nop() end
for _, n in ipairs({ "cls", "rect", "rectfill", "circfill", "line", "clip", "font", "sset", "pset" }) do E[n] = nop end
E.print = function(s, x, y) texts[#texts + 1] = { tostring(s), x or 0, y or 0 }; return (x or 0) + #tostring(s) * 8 end
E.log = nop
E.time = function() return frame / 60 end
E.quit = nop
E.btn = function() return false end
E.btnp = function() return false end
E.keyp = function() return table.remove(keyq, 1) end
E.keydown = function() return false end
E.keyhelp = function() return 0 end
E.save, E.saved = function() return true end, function() return nil end
E.doc_write = function(n, d) docs[n] = d; return true end
E.doc_read = function(n) return docs[n:lower()] end
E.doc_list = function()
  local out = {}
  for n, d in pairs(docs) do out[#out + 1] = { name = n:upper(), size = #d } end
  return out
end
local MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
E.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
E.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end
local bmui
E.require = function(name)
  if name == "bmui" then
    bmui = bmui or assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()
    return bmui
  end
  if name == "predict" then
    return { preload = function() return true end, complete = function() return nil end }
  end
  if name == "padtype" then
    local pt = { C_PEND = 0, C_GHOST = 0 }
    return setmetatable(pt, { __index = function() return function() return nil end end })
  end
  error("require: " .. name)
end

assert(loadfile(ROOT .. "/carts/write/main.lua", "t", E))()

local function tick()
  frame = frame + 1
  texts = {}
  E._update()
  E._draw()
  MS.p, MS.w = 0, 0
end
local function frames(n) for _ = 1, n or 1 do tick() end end
local function type_keys(...)
  for _, k in ipairs({ ... }) do keyq[#keyq + 1] = k end
  frames(2)
end
local function type_text(s) for c in s:gmatch(".") do keyq[#keyq + 1] = c end frames(2) end
local function mouse(x, y, b, w)
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = x, y, b or 0, w or 0
  MS.p = MS.b & ~was
  tick()
end
local function click(x, y, b)
  mouse(x, y, 0); mouse(x, y, b or 1); mouse(x, y, 0); frames(1)
end
local function sees(s)
  for _, t in ipairs(texts) do if t[1]:find(s, 1, true) then return t end end
end
-- the document as a file would hold it (Ctrl+S once the name is known)
local function saved_doc()
  type_keys("^s")
  return docs["doc1.bmd"] or ""
end
local function body(txt) return (txt:gsub("^.-\n\n", "")) end

E._init()
frames(3)
-- text: three paragraphs
type_text("Hello world")
type_keys("\n")
type_text("second line")
type_keys("\n")
type_text("third")
type_keys("^s")                          -- the name
-- a click in the name puts the caret between the letters: Backspace takes "c" out, "c" puts it back
click(136 + 3 * 8, 176)
type_keys("\b")
check(sees("do1"), "prompt: a click puts the caret in the name, Backspace takes the letter before it")
type_keys("c")
type_keys("\n")
check(docs["doc1.bmd"] ~= nil, "setup: saved as doc1")
check(body(saved_doc()):find("body left |Hello world\nbody left |second line\nbody left |third", 1, true) ~= nil,
      "setup: the text is there")

-- geometry: the page is 540 wide at x = 50; the text starts 54 in; the bar is 16 high;
-- the page starts 12 below it, the first line 54 down: y = 16 + 12 + 54
local PX0, LY0 = 50 + 54, 16 + 12 + 54
local function at(col, line) return PX0 + col * 6 + 1, LY0 + (line - 1) * (15 + 7) + 5 end   -- body: lh 15, after 7

-- a click puts the cursor; typing goes there
local x, y = at(5, 1)
click(x, y)
type_text("X")
check(body(saved_doc()):find("|HelloX world\n", 1, true) ~= nil, "click: the cursor is placed between the letters")
-- a click on the second paragraph
x, y = at(0, 2)
click(x, y)
type_text("Y")
check(body(saved_doc()):find("|Ysecond line\n", 1, true) ~= nil, "click: another paragraph")
-- a click below the text: the last paragraph's end
click(300, 300)
type_text("Z")
check(body(saved_doc()):find("|thirdZ\n", 1, true) ~= nil, "click: below the text, the end of the last line")

-- a drag selects; typing replaces it
local x1, y1 = at(0, 1)
local x2 = at(5, 1)
mouse(x1, y1, 0); mouse(x1, y1, 1); mouse(x1 + 10, y1, 1); mouse(x2, y1, 1); mouse(x2, y1, 0); frames(1)
type_text("J")
check(body(saved_doc()):find("|JX world\n", 1, true) ~= nil, "drag: the letters dragged over are replaced")
-- a double click selects the word
local wx = at(3, 1)
local wy = select(2, at(3, 1))
click(wx, wy)
click(wx, wy)
type_text("Q")
check(body(saved_doc()):find("|JX Q\n", 1, true) ~= nil, "double click: the word is selected: " .. body(saved_doc()):sub(1, 40))

-- the toolbar: the style chip presses F2, the alignment chips their keys
local st = sees("Body")
check(st ~= nil, "bar: the style shows")
click(st[2] + 4, 8)
check(sees("Title") ~= nil, "bar: a click on the style: the next one")
local cx = sees("C")
check(cx ~= nil and cx[3] == 0, "bar: the alignment chips")
for _, t in ipairs(texts) do if t[1] == "R" and t[3] == 0 then cx = t end end
click(cx[2] + 4, 8)
check(body(saved_doc()):match("^title center |") ~= nil or body(saved_doc()):match("^title right |") ~= nil,
      "bar: a click on R: aligned right: " .. body(saved_doc()):sub(1, 30))
check(body(saved_doc()):match("^title right |") ~= nil, "bar: the R chip aligns right")
type_keys("^l")
check(body(saved_doc()):match("^title left |") ~= nil, "Ctrl+L: left")
type_keys("^r")
check(body(saved_doc()):match("^title right |") ~= nil, "Ctrl+R: right (the chip's key)")

-- the wheel scrolls the pages and the view stays there
click(300, 300)
for i = 1, 60 do type_keys("\n"); type_text("line " .. i) end
check(sees("line 60") ~= nil and sees("JX Q") == nil, "wheel: setup: the view is at the cursor, the end")
for _ = 1, 40 do mouse(300, 150, 0, 3) end          -- the wheel up
mouse(300, 150, 0, 0)
frames(3)
check(sees("JX Q") ~= nil, "wheel: up: the top of the pages is back in view")
check(sees("line 60") == nil, "wheel: the cursor's line is out of view, and the view stays (it is not pulled back)")
local before = sees("line 2")
mouse(300, 150, 0, -1)
frames(3)
local after = sees("line 2")
check(before and after and after[3] < before[3], "wheel: down moves the pages up")
type_text("k")                           -- a key: the view follows the cursor again
check(sees("line 60") ~= nil, "wheel: a key brings the view to the cursor")

-- the right button: the edit menu; a click on a row presses its key
click(300, 200, 2)
check(sees("Cut") ~= nil and sees("Paste") ~= nil and sees("Undo") ~= nil, "right click: the edit menu")
local m = sees("Bold")
click(m[2] + 4, m[3] + 4)
check(sees("Cut") == nil, "right click: a row closes the menu")

-- the Esc menu with the mouse: a click on the bar's name opens it, a click on a row chooses it,
-- again takes it, a click outside is back
click(8, 8)
check(sees("Open...") ~= nil, "bar: a click on bm Write opens the menu")
local keys_row = sees("Keys")
click(keys_row[2] + 4, keys_row[3] + 4)
check(sees("Keys") ~= nil, "menu: the first click chooses")
click(keys_row[2] + 4, keys_row[3] + 4)
check(sees("(any key closes)") ~= nil, "menu: a click on the chosen row takes it: the help")
click(10, 340)
check(sees("(any key closes)") == nil, "help: a click closes it")
click(8, 8)
click(5, 340)
check(sees("Open...") == nil, "menu: a click outside is back")
click(8, 8)
mouse(300, 100, 0, -2)
mouse(300, 100, 0, 0)
frames(2)
check(sees("Open...") ~= nil, "menu: the wheel keeps it open")
click(5, 340, 2)
check(sees("Open...") == nil, "menu: the right button is back")

-- the Open list: a click on a file chooses, again opens
type_keys("^s")
docs["a.bmd"] = docs["doc1.bmd"]
docs["b.bmd"] = "bmwrite 1\ntitle=Other\n\nbody left |Other file\n"
type_keys("^o")
check(sees("Open a document") ~= nil, "open: the list")
local row_b = sees("B.BMD")
click(row_b[2] + 4, row_b[3] + 4)
click(row_b[2] + 4, row_b[3] + 4)
check(sees("opened /docs/B.BMD") ~= nil or sees("Other file") ~= nil, "open: a double click opens the file")


------------------------------------------------------------------ tables (APP-28)
local function sum(str)                  -- FNV-1a: the old exports' bytes without keeping them
  local h = 2166136261
  for i = 1, #str do h = ((h ~ str:byte(i)) * 16777619) & 0xFFFFFFFF end
  return string.format("%08x", h)
end
local function exact(s)                  -- a printed text, whole
  for _, t in ipairs(texts) do if t[1] == s then return t end end
end
local function open_file(name)
  type_keys("^o")
  local row = sees(name)
  click(row[2] + 4, row[3] + 4)
  click(row[2] + 4, row[3] + 4)
end
local function export_as(n)              -- the menu: Export..., the n-th format (PDF HTML MD TXT)
  type_keys("esc", "down", "down", "down", "down", "\n")
  for _ = 2, n do type_keys("down") end
  type_keys("\n")
end

-- the old documents: every old style, the same bytes out as the version before tables made
local OLDDOC = "bmwrite 1\ntitle=Old è\n\ntitle center |Old è\nh1 left |Heading\nh2 left 1:3:2|Sub head\n"
  .. "body left 1:5:1,7:3:2,12:4:4|Bold and ita under line\nbody justify |" .. string.rep("justify words ", 12) .. "\n"
  .. "body right |right\nquote left |A quote\nbullet left |one\nbullet left |two\nnumber left |one\nnumber left |two\nbody left |\n"
type_keys("^n")
docs["old.bmd"] = OLDDOC
open_file("OLD.BMD")
type_keys("^s")
local OLD = { bmd = "9675d2d9", pdf = "1d4deda7", htm = "ba49d784", md = "08019992", txt = "315b9af5" }   -- from the cart before tables
local got = { bmd = sum(docs["old.bmd"]) }
export_as(1); got.pdf = sum(docs["old.pdf"] or "")
export_as(2); got.htm = sum(docs["old.htm"] or "")
export_as(3); got.md = sum(docs["old.md"] or "")
export_as(4); got.txt = sum(docs["old.txt"] or "")
if arg and arg[2] == "sums" then
  print("SUMS " .. string.format("bmd=%q pdf=%q htm=%q md=%q txt=%q", got.bmd, got.pdf, got.htm, got.md, got.txt))
  print(docs["old.bmd"] == OLDDOC and "bmd same as fixture" or ("bmd differs:\n" .. docs["old.bmd"]))
end
check(docs["old.bmd"] == OLDDOC, "old: a document without tables is saved as it was read")
for _, k in ipairs({ "pdf", "htm", "md", "txt" }) do
  check(got[k] == OLD[k], "old: the " .. k .. " export has the bytes it had before tables (" .. got[k] .. ")")
end


-- F2 reaches the table style after the lists
type_keys("^n")
for _ = 1, 7 do type_keys("f2") end
check(sees("Table") ~= nil, "table: F2 seven times from Body: the bar shows Table")
frames(250)
check(sees("Table: | between the cells") ~= nil, "table: the status says how to write it")
type_text("Name|Qty|Price")
type_keys("\n")
type_text("Apple|3|1.2")
type_keys("\n")
type_text("Fig|12|0.5")
-- the columns: Name/Apple/Fig 5 wide, Qty/3/12 3, Price 5; a cell is followed by a space, the bar, a space
local TX, TY = 50 + 54, 16 + 12 + 54
check(exact("Name") and exact("Name")[2] == TX and exact("Qty")[2] == TX + 8 * 6 and exact("Price")[2] == TX + 14 * 6,
      "table: the header's cells start at the columns 0, 8 and 14")
check(exact("Apple") and exact("Apple")[2] == TX and exact("3")[2] == TX + 8 * 6 and exact("1.2")[2] == TX + 14 * 6,
      "table: the second row's cells are under them")
check(exact("Fig")[2] == TX and exact("12")[2] == TX + 8 * 6 and exact("0.5")[2] == TX + 14 * 6, "table: and the third's")
check(exact("Apple")[3] - exact("Name")[3] == 15 and exact("Fig")[3] - exact("Apple")[3] == 15, "table: the rows are 15 pixels apart")
-- the caret keeps the text index: a click in the first cell's padding goes before the bar; Right steps over it
click(TX + 4 * 6 + 2, TY + 30 + 5)
type_text("Y")
type_keys("right")
type_text("Z")
type_keys("^s")
type_text("tab1")
type_keys("\n")
local tabdoc = body(docs["tab1.bmd"] or "")
check(tabdoc == "body table |Name│Qty│Price\nbody table |Apple│3│1.2\nbody table |FigY│Z12│0.5\n",
      "table: the caret keeps the text index (FigY|Z12), saved as body/table: " .. tabdoc)
-- Up keeps the column: from after Z (x 158) to after Apple's 3
type_keys("up")
type_text("W")
type_keys("^s")
check(body(docs["tab1.bmd"]):find("|Apple│3W│1.2\n", 1, true) ~= nil, "table: Up keeps the column: " .. body(docs["tab1.bmd"]))
type_keys("\b")
type_keys("^s")
-- an older Write reads "body table" as body text: the style is known and the alignment is left alone
do
  local s, a, runs, t = body(docs["tab1.bmd"]):match("^(%S+) (%S+) (%S*)|([^\n]*)")
  check(s == "body" and a == "table" and t == "Name│Qty│Price", "table: .bmd rows read as body text by the older (greedy) line reader: " .. tostring(t))
end

-- the exports
export_as(2)
local htm = docs["tab1.htm"] or ""
check(htm:find("<table>\n<tr><td>Name</td><td>Qty</td><td>Price</td></tr>\n<tr><td>Apple</td><td>3</td><td>1.2</td></tr>\n"
               .. "<tr><td>FigY</td><td>Z12</td><td>0.5</td></tr>\n</table>\n", 1, true) ~= nil, "table: HTML: " .. htm:sub(-300))
export_as(3)
local md = docs["tab1.md"] or ""
check(md == "| Name | Qty | Price |\n|---|---|---|\n| Apple | 3 | 1.2 |\n| FigY | Z12 | 0.5 |\n", "table: Markdown: " .. md)
export_as(4)
check(docs["tab1.txt"] == "Name   Qty  Price\nApple  3    1.2\nFigY   Z12  0.5\n", "table: text: " .. tostring(docs["tab1.txt"]))
-- Markdown in: pipe rows are table paragraphs again (the |---| row is not a paragraph)
open_file("TAB1.MD")
type_keys("^s")
type_text("back1")
type_keys("\n")
check(docs["back1.bmd"] == docs["tab1.bmd"], "table: Markdown out and in: the same document: " .. tostring(docs["back1.bmd"]))
docs["mix.md"] = "# Prices\n\nBefore the table.\n\n| a | **b** |\n|:--|--:|\n|1|2 |\n\nAfter.\n"
open_file("MIX.MD")
type_keys("^s")
type_text("mix1")
type_keys("\n")
check(body(docs["mix1.bmd"] or "") == "title center |Prices\nbody left |Before the table.\nbody table 3:1:1|a│b\nbody table |1│2\nbody left |After.\n",
      "table: a Markdown table among paragraphs: " .. tostring(body(docs["mix1.bmd"] or "")))

-- a row wider than the page wraps as body text and the status says so
type_keys("^n")
for _ = 1, 7 do type_keys("f2") end
type_text(string.rep("w", 50) .. "|" .. string.rep("v", 30))
frames(250)
check(sees("table too wide") ~= nil, "table: a row wider than 72 letters: the status says 'table too wide'")
check(sees(string.rep("w", 50) .. "|" .. string.rep("v", 21)) ~= nil, "table: and it wraps as body text")

print(fails == 0 and ("write: " .. checks .. "/" .. checks .. " checks passed") or (fails .. " of " .. checks .. " FAILED"))
if fails > 0 then os.exit(1) end
