-- bm Code: the code editor of bm. Several cartridges open in tabs, two
-- pages side by side, a small sharp font (6x12: 106 columns, 28 lines of
-- code), the cartridges read and written in place: only their code changes,
-- sprites, map and cover stay as they are. The assistant on F6, and lines
-- "#entry: what you want #" that it carries out. While a word is typed its
-- rest appears in grey-blue: Tab writes it (require "predict"). Ctrl+Enter
-- plays the code as music (require "riff", docs/RIFF.md) and lights up the
-- words of the notes sounding, Ctrl+. stops it. F1: every key.

local assist = require "assist"
local predict = require "predict"
local pt = require "padtype"
local U = require "bmui"                 -- the mouse: what the keys do

local W, H = SCREEN_W, SCREEN_H
local key_chip = prompt                  -- the kernel's prompt(): prompt() here is the text dialog
local FONTS = { "6x12", "8x14", "8x16" }
local C_BG, C_PANE, C_BAR, C_LINE = 0x0E1016, 0x14161E, 0x22273A, 0x343B54
local C_TEXT, C_DIM, C_ACC, C_ERR, C_OK = 0xE0E4F0, 0x6A7290, 0xFFC050, 0xFF6464, 0x70E090
local C_SEL, C_CUR, C_ERRBG, C_GUT, C_GUTCUR = 0x2E4A8A, 0x1C2131, 0x4A1C24, 0x485068, 0xA8B0C8
local C_BP = 0xA02C34                      -- a breakpoint's line number (F8)
local C_KW, C_API, C_STR, C_NUM, C_COM = 0xFF7AB0, 0x70D0FF, 0x90E070, 0xFFB060, 0x6A7690
local C_RIFF, C_RIFFBG = 0xFFF0A0, 0x6A4E10       -- the words of the notes sounding

local TEMPLATE = [[
-- my game
local x, y = 320, 180

function _init()
end

function _update()
  if btn(0) then x = x - 2 end
  if btn(1) then x = x + 2 end
  if btn(2) then y = y - 2 end
  if btn(3) then y = y + 2 end
end

function _draw()
  cls(0x101828)
  circfill(x, y, 12, 0xFFD050)
  print("hello!", 296, 40, 0xFFFFFF)
end
]]

------------------------------------------------------------------ state

local tabs = {}               -- {path, name, title, author, res, lines, dirty, undo, redo, err, breaks}
local panes = { { tab = 1, views = {} }, { tab = 1, views = {} } }
local focus, split = 1, false
local font_i = 1
local CW, CH, COLS, ROWS = 6, 12, 106, 30
local clipboard
local status, status_c, status_t = "", C_DIM, 0
local overlay                 -- menu, files, prompt, help, confirm
local frame = 0
local find_text
local held, rep = {}, {}
local geo = {}                -- each pane as last drawn: where its text is (the mouse)
local mdrag                   -- a selection the mouse is drawing: {p =, cy =, cx =}

local function say(s, c, t)
  status, status_c, status_t = s, c or C_TEXT, t or 240
end

local function set_font(i)
  font_i = (i - 1) % #FONTS + 1
  CW, CH = font(FONTS[font_i])
  COLS, ROWS = W // CW, H // CH
end

------------------------------------------------------------------ text

local function split_lines(s)
  local t = {}
  s = s:gsub("\r", ""):gsub("\t", "  ")
  for l in (s .. "\n"):gmatch("(.-)\n") do t[#t + 1] = l end
  if #t > 1 and t[#t] == "" then t[#t] = nil end
  return t
end

local function text_of(t)
  return table.concat(t.lines, "\n") .. "\n"
end

local function indent_of(l) return #l:match("^ *") end

local function base_name(path) return path:match("[^/]*$") end

------------------------------------------------------------------ tabs

local function new_tab(path, text, info)
  info = info or {}
  local t = {
    path = path, name = path and base_name(path) or "untitled",
    title = info.title or (path and base_name(path):gsub("%.[bB][mM]$", "") or "New game"),
    author = info.author or "", res = info.res or "640x360",
    lines = split_lines(text or ""), dirty = false, undo = {}, redo = {},
  }
  if #t.lines == 0 then t.lines[1] = "" end
  tabs[#tabs + 1] = t
  return #tabs
end

local function view_of(p)
  local pane = panes[p]
  if pane.tab > #tabs then pane.tab = #tabs end
  local t = tabs[pane.tab]
  local v = pane.views[t]
  if not v then
    v = { cx = 0, cy = 1, top = 1, left = 0 }
    pane.views[t] = v
  end
  return t, v
end

local function current() return view_of(focus) end

local function show_tab(i)
  panes[focus].tab = math.max(1, math.min(#tabs, i))
end

local function find_tab(path)
  for i, t in ipairs(tabs) do
    if t.path and t.path:upper() == path:upper() then return i end
  end
end

local function clamp(t, v)
  v.cy = math.max(1, math.min(#t.lines, v.cy))
  v.cx = math.max(0, math.min(#t.lines[v.cy], v.cx))
end

------------------------------------------------------------------ breakpoints

-- the debugger (R13): F8 marks a line, F5 runs the game, which stops there
-- (the kernel's debugger, cart_run(path, {breaks =, stop =}))
local function bp_list(t)
  local out = {}
  for l in pairs(t.breaks or {}) do
    if l <= #t.lines then out[#out + 1] = l end
  end
  table.sort(out)
  return out
end

-- d lines inserted (or removed) after line `at`: the breakpoints below
-- follow their lines; those of the lines removed go
local function bp_shift(t, at, d)
  if not t.breaks or not next(t.breaks) then return end
  local nb = {}
  for l in pairs(t.breaks) do
    if l <= at then nb[l] = true
    elseif d > 0 or l > at - d then nb[l + d] = true end
  end
  t.breaks = nb
end

local function toggle_break(t, v)
  t.breaks = t.breaks or {}
  if t.breaks[v.cy] then
    t.breaks[v.cy] = nil
    say("breakpoint off: line " .. v.cy)
  else
    t.breaks[v.cy] = true
    say("breakpoint on line " .. v.cy .. ": F5 runs the game, it stops there", C_ACC)
  end
  log("code: breakpoints " .. (next(t.breaks) and table.concat(bp_list(t), " ") or "none"))
end

------------------------------------------------------------------ undo

local function snapshot(t, v, kind)
  if kind and t.last_kind == kind and t.last_line == v.cy then return end
  t.undo[#t.undo + 1] = { lines = table.move(t.lines, 1, #t.lines, 1, {}), cx = v.cx, cy = v.cy }
  if #t.undo > 100 then table.remove(t.undo, 1) end
  t.redo = {}
  t.last_kind, t.last_line = kind, v.cy
  t.dirty = true
end

local function undo(t, v, from, to)
  local u = table.remove(from)
  if not u then say("nothing to undo"); return end
  to[#to + 1] = { lines = t.lines, cx = v.cx, cy = v.cy }
  t.lines, v.cx, v.cy = u.lines, u.cx, u.cy
  t.last_kind, t.dirty = nil, true
  clamp(t, v)
end

------------------------------------------------------------------ files

local function open_file(path)
  local i = find_tab(path)
  if i then show_tab(i); return true end
  local c, err = cart_read(path)
  if not c then say("cannot open " .. path .. ": " .. tostring(err), C_ERR); return false end
  i = new_tab(path, c.lua, c)
  -- the first empty tab, untouched, gives its place
  local first = tabs[1]
  if #tabs == 2 and not first.path and not first.dirty and #first.undo == 0 and
     text_of(first) == TEMPLATE then
    table.remove(tabs, 1)
    panes[1].tab, panes[2].tab = 1, 1
    i = 1
  end
  show_tab(i)
  say("opened " .. path .. " (" .. #tabs[i].lines .. " lines)", C_OK)
  log("code: opened " .. path)
  return true
end

local function save_tab(t, path)
  local target = path or t.path
  if not target then return false, "no file name" end
  local ok, err = cart_write(target, { lua = text_of(t), title = t.title, author = t.author,
                                       res = t.res, from = t.path })
  if not ok then return false, err end
  -- a game is read only: the console wrote its editable copy (a .bme)
  if type(err) == "string" then path, target = err, err end
  if path then t.path, t.name = path, base_name(path) end
  t.dirty = false
  log("code: saved " .. target)
  return true
end

-- the session (tabs, cursors, split, font): kept across a game tried with
-- cart_run, and from one time to the next
local complete_on, words_lang = true, 1   -- the word completion: on, the words of comments

local function save_session()
  local s = { font = font_i, split = split, focus = focus, tabs = {}, panes = {},
              complete = { on = complete_on, lang = words_lang } }
  local budget = 16000
  for i, t in ipairs(tabs) do
    local e = { path = t.path, title = t.title, author = t.author, res = t.res }
    if t.breaks and next(t.breaks) then e.breaks = bp_list(t) end
    if not t.path or t.dirty then
      local txt = text_of(t)
      if #txt <= budget then e.text, budget = txt, budget - #txt end
      e.dirty = t.dirty
    end
    s.tabs[i] = e
  end
  for p = 1, 2 do
    local pane = panes[p]
    local t = tabs[pane.tab]
    local v = t and pane.views[t] or {}
    s.panes[p] = { tab = pane.tab, cy = v.cy or 1, cx = v.cx or 0, top = v.top or 1 }
  end
  save({ code = s })
end

local function load_session()
  local d = saved()
  local s = d and d.code
  if not s then return false end
  set_font(s.font or 1)
  if s.complete then complete_on, words_lang = s.complete.on ~= false, s.complete.lang or 1 end
  for _, e in ipairs(s.tabs or {}) do
    local i
    if e.text then
      i = new_tab(e.path, e.text, e)
      tabs[i].dirty = e.dirty or false
    elseif e.path then
      local c = cart_read(e.path)
      if c then i = new_tab(e.path, c.lua, c) end
    end
    if i and e.breaks then
      tabs[i].breaks = {}
      for _, l in ipairs(e.breaks) do tabs[i].breaks[l] = true end
    end
  end
  if #tabs == 0 then return false end
  split, focus = s.split or false, s.focus or 1
  for p = 1, 2 do
    local sp = s.panes and s.panes[p]
    if sp then
      panes[p].tab = math.max(1, math.min(#tabs, sp.tab or 1))
      local t, v = view_of(p)
      v.cy, v.cx, v.top = sp.cy or 1, sp.cx or 0, sp.top or 1
      clamp(t, v)
    end
  end
  return true
end

------------------------------------------------------------------ editing

local OPENERS = { "then%s*$", "do%s*$", "else%s*$", "repeat%s*$", "{%s*$", "%(%s*$",
                  "function%s*[%w_.:]*%s*%(.-%)%s*$" }

local function opens_block(l)
  l = l:gsub("%-%-.*$", "")
  for _, p in ipairs(OPENERS) do
    if l:find(p) and not l:find("end%s*$") then return true end
  end
  return false
end

local function insert_text(t, v, s)
  local l = t.lines[v.cy]
  local before, after = l:sub(1, v.cx), l:sub(v.cx + 1)
  local parts = {}
  for part in (s:gsub("\r", ""):gsub("\t", "  ") .. "\n"):gmatch("(.-)\n") do parts[#parts + 1] = part end
  t.lines[v.cy] = before .. parts[1]
  for k = 2, #parts do table.insert(t.lines, v.cy + k - 1, parts[k]) end
  v.cy = v.cy + #parts - 1
  v.cx = #t.lines[v.cy]
  t.lines[v.cy] = t.lines[v.cy] .. after
end

-- a block of code from the assistant: on its own lines at the cursor, with
-- the indentation of the line it goes to
local function insert_block(t, v, code)
  snapshot(t, v)
  local l = t.lines[v.cy]
  local ind = string.rep(" ", indent_of(l))
  local at = v.cy
  if l:match("%S") then
    at = v.cy + 1
    if opens_block(l) then ind = ind .. "  " end
  else
    table.remove(t.lines, v.cy)
  end
  local n = 0
  for code_line in (code .. "\n"):gmatch("(.-)\n") do
    table.insert(t.lines, at + n, code_line == "" and "" or ind .. code_line)
    n = n + 1
  end
  v.cy, v.cx = at + n - 1, #t.lines[at + n - 1]
  say(n .. " lines inserted (Ctrl+Z undoes)", C_OK)
end

local DEDENT = { ["end"] = true, ["else"] = true, ["elseif"] = true, ["until"] = true, ["}"] = true }

local function prev_nonblank(t, i)
  for k = i - 1, 1, -1 do
    if t.lines[k]:match("%S") then return t.lines[k] end
  end
end

local function sel_range(v)
  if not v.mark then return end
  local a, b = { v.mark.cy, v.mark.cx }, { v.cy, v.cx }
  if a[1] > b[1] or (a[1] == b[1] and a[2] > b[2]) then a, b = b, a end
  return a[1], a[2], b[1], b[2]
end

local function sel_text(t, v)
  local y0, x0, y1, x1 = sel_range(v)
  if not y0 then return t.lines[v.cy] .. "\n" end      -- the whole line
  if y0 == y1 then return t.lines[y0]:sub(x0 + 1, x1) end
  local out = { t.lines[y0]:sub(x0 + 1) }
  for k = y0 + 1, y1 - 1 do out[#out + 1] = t.lines[k] end
  out[#out + 1] = t.lines[y1]:sub(1, x1)
  return table.concat(out, "\n")
end

local function delete_sel(t, v)
  local y0, x0, y1, x1 = sel_range(v)
  if not y0 then
    if #t.lines > 1 then table.remove(t.lines, v.cy) else t.lines[1] = "" end
    v.cx = 0
  else
    t.lines[y0] = t.lines[y0]:sub(1, x0) .. t.lines[y1]:sub(x1 + 1)
    for _ = y0 + 1, y1 do table.remove(t.lines, y0 + 1) end
    v.cy, v.cx = y0, x0
  end
  v.mark = nil
  clamp(t, v)
end

local function word_at(t, v)
  local l = t.lines[v.cy]
  local a, b = v.cx, v.cx + 1
  while a > 0 and l:sub(a, a):match("[%w_.]") do a = a - 1 end
  while b <= #l and l:sub(b, b):match("[%w_]") do b = b + 1 end
  local w = l:sub(a + 1, b - 1):gsub("^%.+", "")
  return w ~= "" and w or nil
end

local function find_next(t, v, text, from_here)
  if not text or text == "" then return end
  local q = text:lower()
  local n = #t.lines
  for k = 0, n do
    local i = (v.cy - 1 + k) % n + 1
    local l = t.lines[i]:lower()
    local start = (k == 0) and (from_here and v.cx + 1 or v.cx + 2) or 1
    local p = l:find(q, start, true)
    if p then
      v.cy, v.cx, v.mark = i, p - 1, nil
      say("found at line " .. i)
      return true
    end
  end
  say("not found: " .. text, C_ERR)
end

local function replace_all(t, v, a, b)
  local n = 0
  local pat = a:gsub("[%^%$%(%)%%%.%[%]%*%+%-%?]", "%%%0")
  local rep = b:gsub("%%", "%%%%")
  snapshot(t, v)
  for i, l in ipairs(t.lines) do
    local nl, k = l:gsub(pat, rep)
    if k > 0 then t.lines[i], n = nl, n + k end
  end
  say(n .. " replaced", n > 0 and C_OK or C_ERR)
end

------------------------------------------------------------------ #entry:

-- "#entry: what to do #" on a line of its own (also after "--"): the
-- assistant does it on the function around the line, or below it
local function entry_request(l)
  return l:match("^%s*%-*%s*#entry:%s*(.-)%s*#%s*$")
end

local function run_entry(t, v)
  local req = entry_request(t.lines[v.cy])
  if not req then return false end
  local r = assist.act(req, t.lines, v.cy)
  if not r then say("#entry: nothing I know for \"" .. req .. "\"", C_ERR); return true end
  if r.ok then
    snapshot(t, v)
    t.lines = r.lines
    v.cy = math.max(1, math.min(#t.lines, r.cursor or v.cy))
    v.cx, v.mark = 0, nil
  end
  if r.explain then overlay = { kind = "text", title = r.message, lines = r.explain } end
  say("#entry: " .. (r.message or "done") .. (r.ok and "  (Ctrl+Z undoes)" or ""), r.ok and C_OK or C_ERR, 600)
  log("code: #entry " .. req .. " -> " .. (r.ok and "" or "not done: ") .. (r.message or "done"))
  return true
end

------------------------------------------------------------------ completion

-- While a word is typed the dictionaries (src/ai/predict.lua, guide in
-- docs/PREDICT.md) show the rest of the likeliest one in grey-blue, and Tab
-- writes it (green until the next key), with the words of where the cursor
-- is: in the code Lua's, the API's and the tab's names; after "--" and in
-- strings Italian (or English: menu), from the marker on; after "#entry:"
-- Italian and the questions to the assistant.
local WORD_LANGS = { "it", "en" }
local comp, comp_flash                  -- the suggestion shown, what Tab has written
local code_words = { n = 0 }
local words_ready = false               -- the dictionaries read, a slice a frame

-- what the cursor is in: "code", "comment", "string" or "ask" (an #entry:
-- line), and where its text starts
local function place_at(l, cx)
  local e = l:find("#entry:", 1, true)
  if e and l:sub(1, e - 1):match("^%s*%-*%s*$") and cx >= e + 6 then return "ask", e + 7 end
  local q, qs, i = nil, nil, 1
  while i <= cx do
    local c = l:sub(i, i)
    if q then
      if c == "\\" then i = i + 1 elseif c == q then q = nil end
    elseif c == '"' or c == "'" then q, qs = c, i
    elseif l:sub(i, i + 1) == "--" then return "comment", i + 2 end
    i = i + 1
  end
  if q then return "string", qs + 1 end
  return "code"
end

-- their words, and how much the names of the tab's code count
local PLACES = {
  code = { weight = 0.6 },
  comment = { prose = true, mark = "--", weight = 0.15 },
  string = { prose = true, mark = '"', weight = 0 },
  ask = { lang = { it = 1, ask = 2 }, mark = ":", weight = 0.1 },
}

-- the names of the tab's code, counted again every 30 suggestions
local function tab_words(t)
  if code_words.tab ~= t or code_words.n >= 30 then
    code_words.tab, code_words.n, code_words.words = t, 0, predict.count_words(t.lines)
  end
  code_words.n = code_words.n + 1
  return code_words.words
end

-- after a key that writes: the suggestion for the word before the cursor
local function suggest(t, v)
  comp = nil
  if not complete_on or v.mark then return end
  local l = t.lines[v.cy]
  if l:sub(v.cx + 1, v.cx + 1):find("[%w_\128-\165]") then return end   -- inside a word
  local place, from = place_at(l, v.cx)
  local p = PLACES[place]
  local before = l:sub(1, v.cx)
  if from then before = p.mark .. before:sub(from) end
  local c = predict.complete(before, { lang = p.lang or (p.prose and WORD_LANGS[words_lang] or "lua"),
                                       words = p.weight > 0 and tab_words(t) or nil, words_weight = p.weight })
  if c then c.t, c.cy, c.cx = t, v.cy, v.cx end
  comp = c
end

-- the suggestion at this cursor, if any
local function comp_here(t, v)
  return comp and comp.t == t and comp.cy == v.cy and comp.cx == v.cx and comp or nil
end

-- Typing with the pad (Share, require "padtype"): it edits the tab through
-- this host, with the words of the place the cursor is in (as the
-- completion); its text before the cursor starts at a comment's or a
-- string's mark, as the completion's
local edit_key
local pad_host = { now = time }
function pad_host.before()
  local t, v = current()
  local l = t.lines[v.cy]
  local place, from = place_at(l, v.cx)
  local before = l:sub(1, v.cx)
  if from then before = PLACES[place].mark .. before:sub(from) end
  return before
end
function pad_host.insert(s)
  for ch in s:gmatch(".") do edit_key(ch) end            -- end, else: back to their block
end
function pad_host.erase(n)                               -- n characters, over the lines
  local t, v = current()
  snapshot(t, v, "bs")
  while n > 0 do
    local l = t.lines[v.cy]
    if v.cx > 0 then
      local k = math.min(n, v.cx)
      t.lines[v.cy] = l:sub(1, v.cx - k) .. l:sub(v.cx + 1)
      v.cx, n = v.cx - k, n - k
    elseif v.cy > 1 then
      v.cx = #t.lines[v.cy - 1]
      t.lines[v.cy - 1] = t.lines[v.cy - 1] .. l
      table.remove(t.lines, v.cy)
      v.cy, n = v.cy - 1, n - 1
    else
      break
    end
  end
end
function pad_host.newline() edit_key("\n") end
function pad_host.move(dir) edit_key(dir) end
local function pad_place()
  local t, v = current()
  local place = place_at(t.lines[v.cy], v.cx)
  local p = PLACES[place]
  pad_host.lang = p.lang or (p.prose and WORD_LANGS[words_lang] or "lua")
  pad_host.prose = p.prose == true or place == "ask"
  pad_host.words = place == "code" and (code_words.tab == t and code_words.words or tab_words(t)) or nil
  pad_host.words_weight = p.weight
  return place
end

------------------------------------------------------------------ music (riff)

-- Ctrl+Enter: the code as music (require "riff"): the riff.code [[ ... ]]
-- block under the cursor (or the first) in a game, else the whole tab.
-- Every global given a pattern plays (drums = s "kick*4"); the words of
-- the notes sounding light up while the text is as it was played.
local riff                    -- the library, loaded the first time (false: none)
local live                    -- what plays: {t, first, last, lines (a copy), starts (offsets of its lines), col}
local live_marks = {}         -- line -> { {from col, to col}, ... } of the notes sounding now

local function riff_lib()
  if riff == nil then
    local ok, m = pcall(require, "riff")
    riff = ok and m or false
  end
  return riff or nil
end

-- the riff code to play: its text, its first line, the column it starts
-- at, its last line
local function riff_source(t, v)
  local blocks = {}
  local i = 1
  while i <= #t.lines do
    local l = t.lines[i]
    local at = l:find("code%s*%(?%s*%[%[")
    if at and not l:sub(1, at):find("%-%-") then
      local col = l:find("%[%[", at) + 2
      local j, close = i, l:find("%]%]", col)
      while not close and j < #t.lines do
        j = j + 1
        close = t.lines[j]:find("%]%]")
      end
      blocks[#blocks + 1] = { i, col, j, close or (#t.lines[j] + 1) }
      i = j + 1
    else
      i = i + 1
    end
  end
  local b = blocks[1]
  for _, x in ipairs(blocks) do
    if v.cy >= x[1] and v.cy <= x[3] then b = x end
  end
  if not b then return table.concat(t.lines, "\n"), 1, 1, #t.lines end
  local first, col, last, close = b[1], b[2], b[3], b[4]
  local parts = {}
  for k = first, last do
    local l = t.lines[k]
    if k == first and k == last then l = l:sub(col, close - 1)
    elseif k == first then l = l:sub(col)
    elseif k == last then l = l:sub(1, close - 1) end
    parts[#parts + 1] = l
  end
  return table.concat(parts, "\n"), first, col, last
end

local function riff_stop(quiet)
  local R = riff_lib()
  if R then R.hush() end
  live, live_marks = nil, {}
  if not quiet then say("music stopped", C_ACC) end
end

local function riff_play(t, v)
  local R = riff_lib()
  if not R then say("riff is not on this console", C_ERR) return end
  local src, first, col, last = riff_source(t, v)
  t.err = nil
  local ok, err = R.code(src, "riff")
  if not ok then
    err = tostring(err)
    local n = tonumber(err:match("^riff:(%d+):"))
    if n then t.err = { line = first + n - 1, msg = err } end
    say(err:gsub("^riff:%d+: ", ""), C_ERR, 600)
    return
  end
  local lines, starts, off = {}, {}, 0
  for k = first, last do
    lines[#lines + 1] = t.lines[k]
    starts[#starts + 1] = off
    off = off + #t.lines[k] - (k == first and col - 1 or 0) + 1
  end
  live = { t = t, first = first, last = last, lines = lines, starts = starts, col = col }
  local names = R.playing()
  if #names == 0 then
    say("nothing plays: give a pattern to a name (drums = s \"kick*4\")", C_ACC, 600)
  else
    say("playing: " .. table.concat(names, " ") .. "   (Ctrl+. stops)", C_OK, 600)
  end
  log("code: riff " .. table.concat(names, " "))
end

-- every frame: the notes ahead into the queue, the errors, the words lit
local function riff_frame()
  if not live then return end
  local R = riff
  R.update()
  for _, e in ipairs(R.errors()) do say("riff: " .. e, C_ERR, 600) end
  if #R.playing() == 0 then live, live_marks = nil, {} return end
  live_marks = {}
  local t = live.t
  for k = live.first, live.last do
    if t.lines[k] ~= live.lines[k - live.first + 1] then return end     -- changed: no lights
  end
  local starts = live.starts
  for _, l in ipairs(R.active()) do
    local a, b = l[1], l[2]
    local k = #starts
    while k > 1 and starts[k] > a do k = k - 1 end
    local line = live.first + k - 1
    local base = k == 1 and live.col - 1 or 0
    local m = live_marks[line] or {}
    m[#m + 1] = { base + a - starts[k], base + b - starts[k] }
    live_marks[line] = m
  end
end

------------------------------------------------------------------ actions

local function run_game(stop)
  local t = current()
  if not t.path then say("save it first (Esc, Save as)", C_ERR); return end
  for _, o in ipairs(tabs) do
    if o.dirty and o.path then
      local ok, err = save_tab(o)
      if not ok then say("cannot save " .. o.name .. ": " .. tostring(err), C_ERR); return end
    end
  end
  t.err = nil
  save_session()
  riff_stop(true)
  local breaks = bp_list(t)
  log("code: run " .. t.path .. (#breaks > 0 and " (breakpoints " .. table.concat(breaks, " ") .. ")" or "")
      .. (stop and ", stopping at the first line" or ""))
  cart_run(t.path, { breaks = breaks, stop = stop })
end

-- opened by the bm SDK on a file: the way back to it (the tabs saved first)
local sdk_path
local function back_to_sdk()
  for _, o in ipairs(tabs) do
    if o.dirty and o.path then
      local ok, err = save_tab(o)
      if not ok then say("cannot save " .. o.name .. ": " .. tostring(err), C_ERR); return end
    end
  end
  save_session()
  cart_tool("sdk", sdk_path)
end

local function open_assistant(t, v)
  assist.open{ mode = "code", ctx = word_at(t, v),
               on_insert = function(code) insert_block(t, v, code) end }
end

local function explain_error(t, v)
  if not t.err then say("no error: F5 runs the game, F9 explains its error") return end
  assist.open{ error = t.err.msg, on_insert = function(code) insert_block(t, v, code) end }
end

-- a line of text; words: "code" for the completion of the tab's names
local function prompt(label, text, done, words)
  overlay = { kind = "prompt", label = label, text = text or "", done = done, words = words }
end

local function confirm(question, choices, done)
  overlay = { kind = "confirm", question = question, choices = choices, done = done }
end

-- a project's 8.3 name: "game" -> "GAME.BME"; nil if it cannot be one
local function project_name(name)
  name = name:upper():gsub("%.BM$", ".BME")
  if not name:match("%.") then name = name .. ".BME" end
  if not name:match("^[%w_%-]+%.BME$") or #name:match("^[^.]*") > 8 then return nil end
  return name
end

local function new_cart()
  local n = 1
  while find_tab("/carts/GAME" .. n .. ".BME") or cart_read("/carts/GAME" .. n .. ".BME") do n = n + 1 end
  prompt("New project, file name (8.3 in /carts):", "GAME" .. n .. ".BME", function(name)
    name = project_name(name)
    if not name then
      say("8.3 name, like MYGAME.BME", C_ERR)
      return
    end
    local path = "/carts/" .. name
    if cart_read(path) then say(path .. " exists: Ctrl+O opens it", C_ERR); return end
    local i = new_tab(path, TEMPLATE, { title = name:gsub("%.BME$", "") })
    tabs[i].dirty = true
    show_tab(i)
    say("new: Ctrl+S writes " .. path, C_OK)
  end)
end

local function save_as(t)
  local suggest = t.path and project_name(base_name(t.path)) or "GAME.BME"
  prompt("Save as (8.3 in /carts):", suggest, function(name)
    name = project_name(name)
    if not name then
      say("8.3 name, like MYGAME.BME", C_ERR)
      return
    end
    local ok, err = save_tab(t, "/carts/" .. name)
    say(ok and "saved " .. t.path or "cannot save: " .. tostring(err), ok and C_OK or C_ERR)
  end)
end

local function save_current()
  local t = current()
  if not t.path then save_as(t); return end
  local ok, err = save_tab(t)
  say(ok and "saved " .. t.path or "cannot save: " .. tostring(err), ok and C_OK or C_ERR)
end

local function close_tab(i)
  table.remove(tabs, i)
  for p = 1, 2 do
    if panes[p].tab >= i then panes[p].tab = math.max(1, panes[p].tab - 1) end
  end
  if #tabs == 0 then
    new_tab(nil, TEMPLATE)
    panes[1].tab, panes[2].tab = 1, 1
  end
end

local function ask_close(i)
  local t = tabs[i]
  if not t.dirty then close_tab(i); return end
  confirm("Save the changes to " .. t.name .. "?", { "Save", "Discard", "Cancel" }, function(c)
    if c == "Save" then
      if not t.path then save_as(t); return end
      local ok, err = save_tab(t)
      if not ok then say("cannot save: " .. tostring(err), C_ERR); return end
      close_tab(i)
    elseif c == "Discard" then
      close_tab(i)
    end
  end)
end

local function quit_editor()
  local dirty = 0
  for _, t in ipairs(tabs) do if t.dirty then dirty = dirty + 1 end end
  if dirty == 0 then save_session(); quit(); return end
  confirm(dirty .. " tab(s) with changes not saved.", { "Save all", "Keep for later", "Cancel" }, function(c)
    if c == "Save all" then
      for _, t in ipairs(tabs) do
        if t.dirty and t.path then save_tab(t) end
      end
      save_session()
      quit()
    elseif c == "Keep for later" then
      save_session()                     -- they come back next time
      quit()
    end
  end)
end

-- Ctrl+Esc or PS (the system's keys): back to bm's menu; with changes not
-- saved it asks as Exit does, and Ctrl+Esc again keeps them for next time
function _exit()
  local dirty = 0
  for _, t in ipairs(tabs) do if t.dirty then dirty = dirty + 1 end end
  if dirty == 0 or (overlay and overlay.leaving) then save_session(); return true end
  quit_editor()
  if overlay then overlay.leaving = true end
  say("Ctrl+Esc again: the changes are kept for next time", C_ACC)
  return false
end

local function open_files()
  local items = { { label = "+ New project...", new = true } }
  for _, dir in ipairs({ "/carts", "/" }) do
    for _, f in ipairs(ls(dir)) do
      local n = f.name:lower()
      if not f.dir and (n:match("%.bm$") or n:match("%.bme$")) then
        local path = (dir == "/" and "" or dir) .. "/" .. f.name
        items[#items + 1] = { label = path, size = f.size, path = path }
      end
    end
  end
  overlay = { kind = "files", items = items, sel = math.min(2, #items), top = 1 }
end

local MENU = {
  { "New project", "Ctrl+N" }, { "Open...", "Ctrl+O" }, { "Save", "Ctrl+S" },
  { "Save as...", "Ctrl+Shift+S" }, { "Close tab", "Ctrl+W" }, { "Run the game", "F5" },
  { "Breakpoint", "F8" }, { "Debug from the start", "" }, { "Clear breakpoints", "" },
  { "Split screen", "F4" }, { "Font size", "F10" }, { "Find", "Ctrl+F" },
  { "Replace", "Ctrl+H" }, { "Go to line", "Ctrl+L" }, { "Assistant", "F6" },
  { "Explain the error", "F9" }, { "Play the music (riff)", "Ctrl+Enter" }, { "Stop the music", "Ctrl+." },
  { "Word completion", "" }, { "Words in comments", "" },
  { "Pad typing", "" }, { "Keys", "F12 held" }, { "Exit", "" },
}

local function open_menu()
  for _, m in ipairs(MENU) do                -- the completion's settings, on the right
    if m[1] == "Word completion" then m[2] = complete_on and "on" or "off"
    elseif m[1] == "Words in comments" then m[2] = WORD_LANGS[words_lang]
    elseif m[1] == "Pad typing" then m[2] = "Share: " .. (pt.mode() or "off") end
  end
  overlay = { kind = "menu", sel = 1 }
end

local do_command

local function menu_choose(name)
  overlay = nil
  local t, v = current()
  if name == "New project" then new_cart()
  elseif name == "Open..." then open_files()
  elseif name == "Save" then save_current()
  elseif name == "Save as..." then save_as(t)
  elseif name == "Close tab" then ask_close(panes[focus].tab)
  elseif name == "Run the game" then run_game()
  elseif name == "Breakpoint" then toggle_break(t, v)
  elseif name == "Debug from the start" then run_game(true)
  elseif name == "Clear breakpoints" then
    t.breaks = nil
    say("no breakpoints")
  elseif name == "Split screen" then do_command("f4")
  elseif name == "Font size" then do_command("f10")
  elseif name == "Find" then do_command("^f")
  elseif name == "Replace" then do_command("^h")
  elseif name == "Go to line" then do_command("^l")
  elseif name == "Assistant" then open_assistant(t, v)
  elseif name == "Explain the error" then explain_error(t, v)
  elseif name == "Play the music (riff)" then riff_play(t, v)
  elseif name == "Stop the music" then riff_stop()
  elseif name == "Word completion" then
    complete_on = not complete_on
    comp = nil
    say(complete_on and "word completion on: Tab writes the word in grey-blue" or "word completion off", C_ACC)
  elseif name == "Words in comments" then
    words_lang = words_lang % #WORD_LANGS + 1
    words_ready = false
    say("words in comments and strings: " .. WORD_LANGS[words_lang], C_ACC)
  elseif name == "Pad typing" then
    pt.on(not pt.mode() and "compose" or nil)
    say(pt.mode() and "pad typing: the cross writes, the overlay shows how (Share: off)" or "pad typing off", C_ACC)
  elseif name == "Keys" then overlay = { kind = "help" }
  elseif name == "Back to bm SDK" then back_to_sdk()
  elseif name == "Exit" then quit_editor() end
end

------------------------------------------------------------------ keys

-- commands that do not depend on the text: the same from the menu
do_command = function(k)
  local t, v = current()
  if k == "f2" then show_tab((panes[focus].tab - 2) % #tabs + 1)
  elseif k == "f3" then show_tab(panes[focus].tab % #tabs + 1)
  elseif k == "f4" then
    split = not split
    if split and panes[2].tab == panes[1].tab and #tabs > 1 then
      panes[2].tab = panes[1].tab % #tabs + 1
    end
    if not split then focus = 1 end
    say(split and "two pages: F7 moves between them, F2/F3 change the page's tab" or "one page")
  elseif k == "f7" then
    if split then focus = 3 - focus end
  elseif k == "f10" then
    set_font(font_i + 1)
    say("font " .. FONTS[font_i])
  elseif k == "f5" or k == "^r" then run_game()
  elseif k == "f8" then toggle_break(t, v)
  elseif k == "f6" then open_assistant(t, v)
  elseif k == "f9" then explain_error(t, v)
  elseif k == "^\n" then riff_play(t, v)
  elseif k == "^." then riff_stop()
  -- the system's keys (the kernel's syskeys.c)
  elseif k == "^s" then save_current()
  elseif k == "^S" then save_as(t)
  elseif k == "^o" then open_files()
  elseif k == "^n" then new_cart()
  elseif k == "^t" then show_tab(new_tab(nil, ""))
  elseif k == "^w" then ask_close(panes[focus].tab)
  elseif k == "^f" then
    prompt("Find:", find_text or word_at(t, v) or "", function(s)
      find_text = s
      find_next(t, v, s, true)
    end, "code")
  elseif k == "^g" then find_next(t, v, find_text)
  elseif k == "^h" then
    prompt("Replace:", find_text or word_at(t, v) or "", function(a)
      if a == "" then return end
      prompt("Replace \"" .. a .. "\" with:", "", function(b) replace_all(t, v, a, b) end, "code")
    end, "code")
  elseif k == "^l" then
    prompt("Go to line (1-" .. #t.lines .. "):", "", function(s)
      local n = tonumber(s)
      if n then v.cy, v.cx = math.floor(n), 0; clamp(t, v) end
    end)
  elseif k == "esc" then
    if v.mark then v.mark = nil else open_menu() end
  else
    return false
  end
  return true
end

function edit_key(k)
  local t, v = current()
  local l = t.lines[v.cy]
  local moved = true
  if k == "up" then v.cy = v.cy - 1
  elseif k == "down" then v.cy = v.cy + 1
  elseif k == "left" then
    if v.cx > 0 then v.cx = v.cx - 1 elseif v.cy > 1 then v.cy = v.cy - 1; v.cx = #t.lines[v.cy] end
  elseif k == "right" then
    if v.cx < #l then v.cx = v.cx + 1 elseif v.cy < #t.lines then v.cy = v.cy + 1; v.cx = 0 end
  elseif k == "home" then v.cx = (v.cx == indent_of(l)) and 0 or indent_of(l)
  elseif k == "end" then v.cx = #l
  elseif k == "pgup" then v.cy = v.cy - (ROWS - 3)
  elseif k == "pgdn" then v.cy = v.cy + (ROWS - 3)
  else
    moved = false
  end
  if moved then
    t.last_kind = nil
    v.cy = math.max(1, math.min(#t.lines, v.cy))
    v.cx = math.min(v.cx, #t.lines[v.cy])
    return
  end
  if k == "\n" then
    if entry_request(l) and run_entry(t, v) then return end
    snapshot(t, v, "nl")
    local before = l:sub(1, v.cx)
    local ind = string.rep(" ", indent_of(l))
    if opens_block(before) then ind = ind .. "  " end
    local after = l:sub(v.cx + 1):gsub("^ +", "")
    t.lines[v.cy] = before
    table.insert(t.lines, v.cy + 1, ind .. after)
    v.cy, v.cx = v.cy + 1, #ind
  elseif k == "\b" then
    if v.mark then snapshot(t, v); delete_sel(t, v); return end
    snapshot(t, v, "bs")
    if v.cx > 0 then
      local n = (v.cx == indent_of(l) and v.cx % 2 == 0) and 2 or 1      -- a whole indent step
      t.lines[v.cy] = l:sub(1, v.cx - n) .. l:sub(v.cx + 1)
      v.cx = v.cx - n
    elseif v.cy > 1 then
      v.cx = #t.lines[v.cy - 1]
      t.lines[v.cy - 1] = t.lines[v.cy - 1] .. l
      table.remove(t.lines, v.cy)
      v.cy = v.cy - 1
    end
  elseif k == "del" then
    if v.mark then snapshot(t, v); delete_sel(t, v); return end
    snapshot(t, v, "del")
    if v.cx < #l then t.lines[v.cy] = l:sub(1, v.cx) .. l:sub(v.cx + 2)
    elseif v.cy < #t.lines then t.lines[v.cy] = l .. t.lines[v.cy + 1]; table.remove(t.lines, v.cy + 1) end
  elseif k == "\t" or k == "^u" then
    -- indent / unindent the line, or every line of the selection
    snapshot(t, v)
    local y0, _, y1 = sel_range(v)
    y0, y1 = y0 or v.cy, y1 or v.cy
    for i = y0, y1 do
      if k == "\t" then t.lines[i] = "  " .. t.lines[i]
      else t.lines[i] = t.lines[i]:gsub("^  ", "", 1) end
    end
    v.cx = math.max(0, v.cx + (k == "\t" and 2 or -2))
    clamp(t, v)
  elseif k == "^z" then undo(t, v, t.undo, t.redo)
  elseif k == "^y" then undo(t, v, t.redo, t.undo)
  elseif k == "^b" then
    if v.mark then v.mark = nil; say("selection off")
    else v.mark = { cy = v.cy, cx = v.cx }; say("selection: move the cursor, then Ctrl+C / Ctrl+X") end
  elseif k == "^c" then
    clipboard = sel_text(t, v)
    v.mark = nil
    say("copied")
  elseif k == "^x" then
    clipboard = sel_text(t, v)
    snapshot(t, v)
    delete_sel(t, v)
    say("cut")
  elseif k == "^v" then
    if not clipboard then say("nothing copied"); return end
    snapshot(t, v)
    if clipboard:sub(-1) == "\n" then v.cx = 0 end           -- whole lines: above this one
    insert_text(t, v, clipboard)
  elseif k == "^k" then
    clipboard = t.lines[v.cy] .. "\n"
    snapshot(t, v)
    v.mark = nil
    delete_sel(t, v)
  elseif k == "^d" then
    snapshot(t, v)
    table.insert(t.lines, v.cy + 1, l)
    v.cy = v.cy + 1
  elseif #k == 1 and k:byte() >= 32 then
    if v.mark then snapshot(t, v); delete_sel(t, v); l = t.lines[v.cy] end
    snapshot(t, v, "ins")
    t.lines[v.cy] = l:sub(1, v.cx) .. k .. l:sub(v.cx + 1)
    v.cx = v.cx + 1
    -- end, else, elseif, until, }: back to the indentation of their block
    local now = t.lines[v.cy]
    local word = now:match("^%s*(%S+)%s*$")
    if word and DEDENT[word] and v.cx == #now then
      local prev = prev_nonblank(t, v.cy)
      if prev then
        local want = indent_of(prev) - (opens_block(prev) and 0 or 2)
        if want >= 0 and indent_of(now) > want then
          t.lines[v.cy] = string.rep(" ", want) .. word
          v.cx = #t.lines[v.cy]
        end
      end
    end
  else
    return false
  end
  return true
end

-- Tab: the suggestion written, in one undo step with the word typed
local function accept(t, v)
  local c = comp_here(t, v)
  if not c then return false end
  local s = c.rest
  if c.word:sub(1, #c.prefix) ~= c.prefix then         -- its accents, its case
    snapshot(t, v, "ins")
    local l = t.lines[v.cy]
    t.lines[v.cy] = l:sub(1, v.cx - #c.prefix) .. l:sub(v.cx + 1)
    v.cx, s = v.cx - #c.prefix, c.word
  end
  for ch in s:gmatch(".") do edit_key(ch) end            -- "end": back to its block
  comp, comp_flash = nil, { t = t, cy = v.cy, cx = v.cx, n = #c.word }
  return true
end

-- the completion in a prompt (the tab's names, for a find)
local function prompt_suggest(o)
  o.comp = o.words and complete_on and predict.complete(o.text, { lang = "lua", words = tab_words(current()) }) or nil
end

local function overlay_key(k)
  local o = overlay
  if o.kind == "help" or o.kind == "text" then overlay = nil
  elseif o.kind == "prompt" then
    local c = o.comp
    o.comp, o.flash = nil, nil
    if k == "esc" then overlay = nil
    elseif k == "\n" then overlay = nil; o.done(o.text)
    elseif k == "\t" and c then
      o.text = o.text:sub(1, #o.text - #c.prefix) .. c.word
      o.flash = #c.word
    elseif k == "\b" then o.text = o.text:sub(1, -2); prompt_suggest(o)
    elseif #k == 1 and k:byte() >= 32 then o.text = o.text .. k; prompt_suggest(o) end
  elseif o.kind == "confirm" then
    if k == "esc" then overlay = nil
    elseif k == "left" or k == "up" then o.sel = math.max(1, (o.sel or 1) - 1)
    elseif k == "right" or k == "down" or k == "\t" then o.sel = math.min(#o.choices, (o.sel or 1) + 1)
    elseif k == "\n" then overlay = nil; o.done(o.choices[o.sel or 1])
    else
      for _, c in ipairs(o.choices) do
        if k:lower() == c:sub(1, 1):lower() then overlay = nil; o.done(c) return end
      end
    end
  elseif o.kind == "menu" or o.kind == "files" then
    local n = o.kind == "menu" and #MENU or #o.items
    if k == "esc" then overlay = nil
    elseif k == "up" then o.sel = (o.sel - 2) % n + 1
    elseif k == "down" then o.sel = o.sel % n + 1
    elseif k == "pgup" then o.sel = math.max(1, o.sel - 10)
    elseif k == "pgdn" then o.sel = math.min(n, o.sel + 10)
    elseif k == "\n" then
      if o.kind == "menu" then menu_choose(MENU[o.sel][1])
      else
        local it = o.items[o.sel]
        overlay = nil
        if it.new then new_cart() else open_file(it.path) end
      end
    end
  end
end

-- the pad: the cross moves (and repeats), Y + cross pages and tabs, X the
-- assistant, Start the menu; in a list A chooses and B goes back
local function pressed(b)
  if btnp(b) then rep[b] = 0; return true end
  if btn(b) then
    rep[b] = (rep[b] or 0) + 1
    return rep[b] > 18 and rep[b] % 3 == 0
  end
  rep[b] = nil
  return false
end

local function pad()
  local dirs = { [0] = "left", "right", "up", "down" }
  if overlay then
    if overlay.kind == "prompt" then
      if btnp(5) then overlay = nil end
      return
    end
    for b = 2, 3 do if pressed(b) then overlay_key(dirs[b]) end end
    for b = 0, 1 do if pressed(b) then overlay_key(dirs[b]) end end
    if btnp(4) then overlay_key("\n") elseif btnp(5) then overlay_key("esc") end
    return
  end
  local y = btn(7)
  for b = 0, 3 do
    if pressed(b) then
      if not y then edit_key(dirs[b])
      elseif b == 0 then do_command("f2")
      elseif b == 1 then do_command("f3")
      else edit_key(b == 2 and "pgup" or "pgdn") end
    end
  end
  if btnp(6) then local t, v = current(); open_assistant(t, v) end
  if btnp(8) then open_menu() end
end

------------------------------------------------------------------ the mouse

-- the line and column under the pointer in pane p (the nearest boundary
-- between two characters), clamped to the text
local function text_pos(p, x, y)
  local g, t = geo[p], view_of(p)
  local cy = g.top + (y - g.y0) // CH
  cy = math.max(1, math.min(#t.lines, cy))
  local cx = g.left + (x - g.tx + CW // 2) // CW
  return cy, math.max(0, math.min(#t.lines[cy], cx))
end

-- the word around a column: its start and end
local function word_bounds(l, cx)
  local a, b = cx, cx
  while a > 0 and l:sub(a, a):match("[%w_]") do a = a - 1 end
  while b < #l and l:sub(b + 1, b + 1):match("[%w_]") do b = b + 1 end
  return a, b
end

local function context_menu(t, v)
  local items = {
    { "Cut", "^x" }, { "Copy", "^c" }, { "Paste", "^v" }, "-",
    { "Undo", "^z" }, { "Redo", "^y" }, "-",
    { "Find...", "^f" }, { "Go to line...", "^l" }, { "Breakpoint", "f8" }, "-",
    { "Run the game", "f5" },
  }
  if t.err then items[#items + 1] = { "Explain the error", "f9" } end
  items[#items + 1] = { "Assistant", "f6" }
  items[#items + 1] = { "Menu", "esc" }
  U.menu(items)
end

-- the mouse does what the keys do: a click places the cursor (in the
-- numbers: a breakpoint, F8), a drag selects, a double click takes the
-- word, the wheel scrolls; tabs, menu, lists and dialogs by clicking; the
-- right button the context menu
local function mouse_frame()
  U.update()
  if U.menu_update() then return end
  if assist.is_open() or not U.on then mdrag = nil; return end
  local o = overlay
  if o then
    mdrag = nil
    if (o.kind == "menu" or o.kind == "files") and U.wheel ~= 0 then
      local n = o.kind == "menu" and #MENU or #o.items
      o.sel = math.max(1, math.min(n, o.sel - U.wheel * 3))
    end
    local z = U.click(0) or U.click(1)
    if not z then return end
    if z.kind == "item" then
      o.sel = z.a
      if o.kind == "menu" or U.double then U.press("\n") end   -- a list of files: a double click opens
    elseif z.kind == "choice" then
      o.sel = z.a
      U.press("\n")
    elseif z.kind == "shade" or o.kind == "help" or o.kind == "text" then
      U.press("esc")
    end
    return
  end
  -- the wheel: the page under the pointer scrolls, the cursor stays
  local z = U.at()
  if U.wheel ~= 0 and z and z.kind == "pane" then
    local t, v = view_of(z.a)
    v.free = true
    v.top = math.max(1, math.min(#t.lines - geo[z.a].nrows + 1, v.top - U.wheel * 3))
  end
  -- a selection being drawn
  if mdrag then
    if not U.down(0) then mdrag = nil
    else
      local t, v = view_of(mdrag.p)
      local g = geo[mdrag.p]
      local cy, cx = text_pos(mdrag.p, U.x, U.y)
      if U.y < g.y0 then cy = math.max(1, g.top - 1) end          -- past the edge: it scrolls
      if U.y >= g.y0 + g.h then cy = math.min(#t.lines, g.top + g.nrows) end
      v.free = nil
      v.cy, v.cx = cy, math.min(cx, #t.lines[cy])
      if v.cy ~= mdrag.cy or v.cx ~= mdrag.cx then v.mark = { cy = mdrag.cy, cx = mdrag.cx } else v.mark = nil end
      return
    end
  end
  local left = U.click(0)
  if left then
    if left.kind == "tab" then
      panes[focus].tab = left.a
    elseif left.kind == "keys" then
      overlay = { kind = "help" }
    elseif left.kind == "pane" then
      local p = left.a
      focus = p
      local t, v = view_of(p)
      local cy, cx = text_pos(p, U.x, U.y)
      v.free = nil
      comp, comp_flash = nil, nil
      if U.x < geo[p].gx then                  -- the line numbers: a breakpoint there
        v.cy, v.cx, v.mark = cy, 0, nil
        toggle_break(t, v)
      elseif U.double then                     -- the word
        local a, b = word_bounds(t.lines[cy], cx)
        v.cy, v.cx = cy, b
        v.mark = b > a and { cy = cy, cx = a } or nil
      else
        v.cy, v.cx, v.mark = cy, cx, nil
        mdrag = { p = p, cy = cy, cx = cx }
      end
    end
  end
  local mid = U.click(2)
  if mid and mid.kind == "tab" then ask_close(mid.a) end
  local right = U.clicked(1)
  if right then
    if type(right) == "table" and right.kind == "tab" then
      panes[focus].tab = right.a
      U.menu({ { "New tab", "^t" }, { "Save", "^s" }, { "Save as...", "^S" }, { "Close tab", "^w" }, "-",
               { "Open...", "^o" }, { "Split screen", "f4" } })
    elseif type(right) == "table" and right.kind == "pane" then
      local p = right.a
      focus = p
      local t, v = view_of(p)
      local cy, cx = text_pos(p, U.x, U.y)
      -- outside the selection: the cursor goes there first
      local y0, x0, y1, x1 = sel_range(v)
      local inside = y0 and (cy > y0 or (cy == y0 and cx >= x0)) and (cy < y1 or (cy == y1 and cx <= x1))
      if not inside then v.cy, v.cx, v.mark = cy, cx, nil end
      v.free = nil
      context_menu(t, v)
    end
  end
end

------------------------------------------------------------------ frame

function _init()
  keyp()                                 -- typing on
  mouse(true)
  if keyhelp then keyhelp(KEYHELP, "bm Code") end
  pt.set({ fallback = "off" })            -- Share: compose, then the pad moves again
  set_font(1)
  load_session()
  local a = cart_arg()
  if a and a.path and a.from == "sdk" then
    sdk_path = a.path
    table.insert(MENU, #MENU, { "Back to bm SDK", "saves first" })
  end
  if a and a.path then
    if not find_tab(a.path) then open_file(a.path) end
    show_tab(find_tab(a.path) or 1)
    if a.error and a.error:find("stopped in the debugger", 1, true) then
      local t, v = current()
      local line = tonumber(a.error:match("main%.lua:(%d+):"))
      if line then v.cy, v.cx = line, 0; clamp(t, v) end
      say("stopped in the debugger" .. (line and " at line " .. line or ""), C_ACC, 600)
    elseif a.error then
      local t, v = current()
      local line = tonumber(a.error:match("main%.lua:(%d+):"))
      t.err = { line = line, msg = a.error }
      if line then v.cy, v.cx = line, 0; clamp(t, v) end
      say("the game stopped: " .. (a.error:match("^[^\n]*") or a.error) .. "  (F9: explain)", C_ERR, 900)
    elseif a.back then
      say("back from the game", C_OK)
    end
  end
  if #tabs == 0 then
    new_tab(nil, TEMPLATE)
    say("F1 keys   Ctrl+O open a cartridge   Ctrl+N new one   F6 assistant", C_ACC, 900)
  end
  log("code: ready, " .. #tabs .. " tab(s), font " .. FONTS[font_i])
end

function _update()
  frame = frame + 1
  if status_t > 0 then status_t = status_t - 1 end
  riff_frame()
  mouse_frame()
  if assist.update() then return end
  if U.menu_open() then return end
  local k = keyp()
  local typed
  while k do
    local t, v = current()
    v.free = nil                         -- a key: the page follows the cursor again
    typed, comp_flash = false, nil
    if overlay then overlay_key(k)
    elseif k == "\t" and accept(t, v) then
    elseif not do_command(k) then
      edit_key(k)
      typed = (#k == 1 and k:byte() >= 32) or k == "\b"
    end
    if assist.is_open() then break end
    k = keyp()
  end
  if typed ~= nil then                   -- a key: the suggestion for its word, or none
    comp = nil
    if typed and not overlay then suggest(current()) end
  end
  if (complete_on or pt.mode()) and not words_ready then
    words_ready = predict.preload({ "lua", WORD_LANGS[words_lang], "ask" })
  end
  if assist.is_open() then
    pt.idle(pad_host)
  elseif overlay then
    pt.idle(pad_host)
    pad()
  else
    pad_place()
    local was = pt.mode()
    if pt.update(pad_host) then
      comp = nil
      if btnp(8) then open_menu() end      -- Start: the menu (the typing leaves it)
      if pt.mode() ~= was then
        say(pt.mode() and ("pad typing: " .. pt.mode() .. "  (Share: " ..
                           (pt.mode() == "compose" and "off)" or "compose)")) or "pad typing off", C_ACC)
      end
    else
      pad()
    end
  end
end

-- lines inserted or removed in a frame (keys, the pad, the assistant, undo):
-- the tab's breakpoints follow them
do
  local body = _update
  function _update()
    local t, v = current()
    local n0, cy0, cx0 = #t.lines, v.cy, v.cx
    body()
    local d = #t.lines - n0
    if d ~= 0 and t.breaks then
      local at = math.min(cy0, v.cy)
      if d > 0 and cx0 == 0 and v.cy == cy0 + d then at = cy0 - 1 end   -- Enter at the start: the line went down
      bp_shift(t, at, d)
    end
  end
end

------------------------------------------------------------------ drawing

local KEYWORDS = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%S+") do
  KEYWORDS[w] = true
end
local API = {}
for _, e in ipairs(ai.list("api")) do API[e.id] = true end
for w in ("math string table ipairs pairs tostring tonumber select type setmetatable"):gmatch("%S+") do
  API[w] = true
end

local seg_cache, seg_n = {}, 0

-- a line in coloured pieces { start column, text, colour }, cached
local function segments(s)
  local c = seg_cache[s]
  if c then return c end
  c = {}
  local i, n = 1, #s
  while i <= n do
    local ch = s:sub(i, i)
    local j, col
    if s:sub(i, i + 1) == "--" then j, col = n, C_COM
    elseif ch == '"' or ch == "'" then
      j = i + 1
      while j <= n and s:sub(j, j) ~= ch do j = j + (s:sub(j, j) == "\\" and 2 or 1) end
      col = C_STR
    elseif ch:match("[%a_]") then
      j = (s:find("[^%w_]", i) or n + 1) - 1
      local w = s:sub(i, j)
      col = KEYWORDS[w] and C_KW or API[w] and C_API or C_TEXT
    elseif ch:match("%d") then
      j = (s:find("[^%w%.]", i) or n + 1) - 1
      col = C_NUM
    elseif ch == " " then
      j = (s:find("[^ ]", i) or n + 1) - 1
    else
      j = (s:find("[%w_\"' %-]", i + 1) or n + 1) - 1
      if j < i then j = i end
      col = C_TEXT
    end
    if col then c[#c + 1] = { i, s:sub(i, math.min(j, n)), col } end
    i = j + 1
  end
  seg_n = seg_n + 1
  if seg_n > 3000 then seg_cache, seg_n = {}, 0 end
  seg_cache[s] = c
  return c
end

-- a pane: columns c0 .. c0 + ncols - 1, rows r0 .. r0 + nrows - 1
local function draw_pane(p, c0, ncols, r0, nrows)
  local t, v = view_of(p)
  clamp(t, v)
  local x0, y0 = c0 * CW, r0 * CH
  local digits = math.max(3, #tostring(#t.lines))
  local gut = digits + 1
  local tcols = ncols - gut - 1
  -- keep the cursor in sight (not while the wheel scrolls the page)
  if not v.free then
    if v.cy < v.top then v.top = v.cy end
    if v.cy >= v.top + nrows then v.top = v.cy - nrows + 1 end
  end
  v.top = math.max(1, math.min(v.top, #t.lines - nrows + 1))
  if v.cx < v.left then v.left = v.cx end
  if v.cx >= v.left + tcols then v.left = v.cx - tcols + 1 end
  geo[p] = { x0 = x0, y0 = y0, w = ncols * CW, h = nrows * CH, tx = x0 + gut * CW, gx = x0 + digits * CW,
             top = v.top, left = v.left, tcols = tcols, nrows = nrows }
  U.zone(x0, y0, ncols * CW, nrows * CH, "pane", p)
  local active = p == focus and not overlay and not assist.is_open()
  rectfill(x0, y0, ncols * CW, nrows * CH, C_PANE)
  local tx = x0 + gut * CW
  local y0s, x0s, y1s, x1s = sel_range(v)
  for r = 0, nrows - 1 do
    local i = v.top + r
    local l = t.lines[i]
    if not l then break end
    local y = y0 + r * CH
    if t.err and t.err.line == i then rectfill(tx, y, tcols * CW + CW, CH, C_ERRBG)
    elseif i == v.cy and p == focus then rectfill(tx, y, tcols * CW + CW, CH, C_CUR) end
    if y0s and i >= y0s and i <= y1s then
      local a = (i == y0s) and x0s or 0
      local b = (i == y1s) and x1s or #l + 1
      a, b = math.max(a, v.left), math.min(b, v.left + tcols)
      if b > a then rectfill(tx + (a - v.left) * CW, y, (b - a) * CW, CH, C_SEL) end
    end
    local num = tostring(i)
    local bp = t.breaks and t.breaks[i]
    if bp then rectfill(x0, y, digits * CW, CH, C_BP) end
    print(string.rep(" ", digits - #num) .. num, x0, y, bp and C_TEXT or i == v.cy and C_GUTCUR or C_GUT)
    local c = active and i == v.cy and comp_here(t, v)
    if c then l = l:sub(1, v.cx) .. c.rest .. l:sub(v.cx + 1) end
    if entry_request(l) then
      print(l:sub(v.left + 1, v.left + tcols), tx, y, C_ACC)
    else
      for _, sg in ipairs(segments(l)) do
        local s0, txt, col = sg[1], sg[2], sg[3]
        local s1 = s0 + #txt - 1
        if s1 > v.left and s0 <= v.left + tcols then
          local a = math.max(s0, v.left + 1)
          local b = math.min(s1, v.left + tcols)
          print(txt:sub(a - s0 + 1, b - s0 + 1), tx + (a - 1 - v.left) * CW, y, col)
        end
      end
    end
    if #l > v.left + tcols then print(">", tx + tcols * CW, y, C_DIM) end
    -- the suggestion, then what Tab has just written, in their colours
    local function paint(a, txt, col)
      for k = 1, #txt do
        local cx = a + k - 1
        if cx >= v.left and cx < v.left + tcols then
          rectfill(tx + (cx - v.left) * CW, y, CW, CH, C_CUR)
          print(txt:sub(k, k), tx + (cx - v.left) * CW, y, col)
        end
      end
    end
    if c then paint(v.cx, c.rest, predict.C_GHOST) end
    -- riff: the words of the notes sounding
    local lit = live and live.t == t and live_marks[i]
    if lit then
      for _, m in ipairs(lit) do
        for cx = m[1], m[2] - 1 do
          if cx >= v.left and cx < v.left + tcols and cx < #l then
            rectfill(tx + (cx - v.left) * CW, y, CW, CH, C_RIFFBG)
            print(l:sub(cx + 1, cx + 1), tx + (cx - v.left) * CW, y, C_RIFF)
          end
        end
      end
    end
    -- the pad's typing: the press waiting, the rest of its word, the
    -- syllable still turning (light blue), what a suggestion wrote
    if active and i == v.cy and pt.mode() then
      local at, pd, g = v.cx, pt.pending(), pt.ghost()
      if pd then paint(at, pd == " " and "_" or pd, pt.C_PEND); at = at + 1 end
      if g ~= "" then paint(at, g, pt.C_GHOST) end
      local fl, op = pt.flash(), pt.open_len()
      if fl > 0 and fl <= v.cx then paint(v.cx - fl, l:sub(v.cx - fl + 1, v.cx), pt.C_PRED) end
      if op > 0 and op <= v.cx then paint(v.cx - op, l:sub(v.cx - op + 1, v.cx), pt.C_OPEN) end
    end
    local f = comp_flash
    if f and active and i == v.cy and f.t == t and f.cy == i and f.cx == v.cx then
      paint(v.cx - f.n, l:sub(v.cx - f.n + 1, v.cx), predict.C_PRED)
    end
  end
  -- the cursor: a bar that blinks
  if active and (frame // 30) % 2 == 0 then
    local cy = v.cy - v.top
    if cy >= 0 and cy < nrows then
      rectfill(tx + (v.cx - v.left) * CW, y0 + cy * CH, 2, CH, C_ACC)
    end
  end
  return t, v
end

-- keys and pad buttons as chips (prompt()), as high as a text row (12 px
-- up to the 8x14 font, 16 with 8x16), then the label; returns the x after
local function chips(keys, label, x, y, c)
  for _, k in ipairs(keys) do x = key_chip(k, x, y, CH < 16) + 1 end
  return print(label, (x + 2 + CW - 1) // CW * CW, y, c or C_DIM) + 2 * CW   -- text on its columns
end
local function chips_w(keys)
  local w = 0
  for _, k in ipairs(keys) do w = w + key_chip(k, CH < 16) + 1 end
  return w
end
-- the key, or the pad's button when a pad was used last
local function key_or_pad(key, pad)
  local li = lastinput()
  return { (li == "ds4" or li == "pad") and pad or key }
end
-- a shortcut of the menu ("Ctrl+N", "F5") as its keys, or nil
local function shortcut_keys(s)
  local c = s:match("^Ctrl%+(.)$")
  if c then return { "ctrl", c:lower() } end
  if s:match("^F%d+$") then return { s:lower() } end
end

local function draw_tabs()
  rectfill(0, 0, W, CH, C_BAR)
  local x = 0
  for i, t in ipairs(tabs) do
    local label = " " .. t.name .. (t.dirty and "*" or "") .. " "
    if split then
      if panes[1].tab == i then label = label .. "1 " end
      if panes[2].tab == i then label = label .. "2 " end
    end
    if x + #label > COLS - 17 then
      print("...", x * CW, 0, C_DIM)
      break
    end
    local on = panes[focus].tab == i
    U.zone(x * CW, 0, #label * CW, CH, "tab", i)
    if on then rectfill(x * CW, 0, #label * CW, CH, C_LINE) end
    print(label, x * CW, 0, on and 0xFFFFFF or C_DIM)
    x = x + #label + 1
  end
  local lx = (COLS - 11) * CW                        -- "held: keys" on the last columns, F12 before
  U.zone(lx - 3 - key_chip("f12", CH < 16), 0, W - lx, CH, "keys")
  key_chip("f12", lx - 3 - key_chip("f12", CH < 16), 0, CH < 16)
  print("held: keys", lx, 0, C_DIM)
end

local function draw_status(t, v)
  local y = (ROWS - 1) * CH
  rectfill(0, y, W, H - y, C_BAR)
  local right = string.format("ln %d/%d col %d  %s", v.cy, #t.lines, v.cx + 1, FONTS[font_i])
  local left = (t.path or "untitled") .. (t.dirty and " *" or "")
  if pt.mode() then
    local lang = pad_host.lang
    left = left .. "  PAD " .. pt.mode() .. " " .. (type(lang) == "table" and "ask" or tostring(lang))
  end
  if live then left = left .. "  MUSIC " .. #riff.playing() end
  print(left, 0, y, C_TEXT)
  print(right, (COLS - #right) * CW, y, C_DIM)
  if status_t == 0 and entry_request(t.lines[v.cy]) then
    status, status_c, status_t = "Enter: the assistant does it", C_ACC, 1
  elseif status_t == 0 and comp_here(t, v) and not overlay then
    local c = comp_here(t, v)
    local others = {}
    for _, w in ipairs(c.list) do
      if w ~= c.word and #w > #c.prefix then others[#others + 1] = w end
    end
    local more = #others > 0 and "  (also: " .. table.concat(others, ", ") .. ")" or ""
    status, status_c, status_t = "Tab: " .. c.word .. more, predict.C_GHOST, 1
  end
  if status ~= "" and status_t > 0 then
    local room = COLS - #left - #right - 4
    if room > 8 then print(status:sub(1, room), (#left + 2) * CW, y, status_c) end
  end
end

-- the keys while F12 is held, under the system's (keyhelp(), the kernel
-- shows them): keyboard keys in lower case, the pad's buttons in upper case
local KEYHELP = {
  { "ctrl w", "close the tab" },
  { "ctrl t", "a new empty tab" },
  { "f2 / f3", "the tab before / after" },
  { "f4", "two pages side by side" },
  { "f7", "the other page" },
  { "f10", "font 6x12 / 8x14 / 8x16" },
  { "f9", "explain the game's error" },
  { "f8", "breakpoint: the game stops on the line" },
  { "ctrl enter", "play the code as music (riff)" },
  { "ctrl .", "stop the music" },
  { "ctrl b", "start a selection" },
  { "ctrl k / ctrl d", "cut / duplicate the line" },
  { "tab / ctrl u", "indent / unindent" },
  { "tab", "after a word: the grey-blue rest" },
  { "ctrl g", "find the next" },
  { "ctrl h", "replace all" },
  { "ctrl l", "go to line" },
  { "enter", "on #entry: ... #: the assistant does it" },
  "pad",
  { "SELECT", "pad typing: on / off" },
  { "DPAD", "move" },
  { "Y DPAD", "pages, tabs" },
  { "X", "the assistant" },
  { "START", "the menu" },
}

local HELP = {
  "Files", "Ctrl+N new cartridge", "Ctrl+O open", "Ctrl+S save", "Ctrl+Shift+S save as",
  "Ctrl+W close tab", "F5 / Ctrl+R run, then back here", "",
  "Debugger", "F8 breakpoint on the line (F5 stops there)", "  then F10 next, F8 into, Shift+F8 out,",
  "  F5 go on, Esc stop; the arrows: variables", "",
  "Tabs and pages", "Ctrl+T new empty tab", "F2 / F3 previous / next tab", "F4 two pages side by side",
  "F7 the other page", "F10 font 6x12 / 8x14 / 8x16", "",
  "Editing", "Ctrl+Z undo, Ctrl+Y redo", "Ctrl+B start a selection", "Ctrl+C copy, Ctrl+X cut",
  "Ctrl+V paste (a line: above)", "Ctrl+K cut line, Ctrl+D duplicate", "Tab / Ctrl+U indent / unindent",
  "Tab after a word: the grey-blue rest",
  "Ctrl+F find, Ctrl+G next", "Ctrl+H replace all", "Ctrl+L go to line", "",
  "Assistant", "F6 ask (the word under the cursor)", "F9 explain the game's error",
  "#entry: what to do #  then Enter:", "  the assistant does it here", "",
  "Music (riff, docs/RIFF.md)", "Ctrl+Enter play the tab, or the", "  riff.code [[ ]] under the cursor",
  "Ctrl+. stop the music", "",
  "Pad", "cross moves, Y+cross pages/tabs", "X assistant, Start menu",
  "Share: pad typing (docs/PADTYPE.md)",
  "Ctrl+Esc back to bm, F12 held: keys",
}

local function draw_box(c0, r0, cols, rows, title)
  rectfill(c0 * CW, r0 * CH, cols * CW, rows * CH, C_PANE)
  rect(c0 * CW, r0 * CH, cols * CW, rows * CH, C_LINE)
  rectfill(c0 * CW, r0 * CH, cols * CW, CH, C_BAR)
  print(title, (c0 + 1) * CW, r0 * CH, C_ACC)
end

local function draw_overlay()
  local o = overlay
  U.zone(0, 0, W, H, "shade")             -- a click outside the box: Esc
  if o.kind == "text" then
    local cols = math.min(COLS - 4, 80)
    local rows = math.min(ROWS - 4, #o.lines + 3)
    local c0, r0 = (COLS - cols) // 2, 2
    draw_box(c0, r0, cols, rows, o.title .. " (any key closes)")
    for i = 1, rows - 2 do
      local l = o.lines[i]
      if not l then break end
      print(l:sub(1, cols - 2), (c0 + 1) * CW, (r0 + i) * CH, i == 1 and C_ACC or C_TEXT)
    end
  elseif o.kind == "help" then
    local half = (#HELP + 1) // 2
    local cols, rows = math.min(COLS - 4, 80), math.min(ROWS - 2, half + 2)
    local c0, r0 = (COLS - cols) // 2, 1
    draw_box(c0, r0, cols, rows, "bm Code: keys (any key closes)")
    for i, s in ipairs(HELP) do
      local col = i <= half and 0 or 1
      local r = (i - 1) % half
      if r + 1 < rows then
        local heading = s ~= "" and not s:find(" ") or s == "Tabs and pages"
        print(s:sub(1, cols // 2 - 2), (c0 + 1 + col * (cols // 2)) * CW, (r0 + 1 + r) * CH,
              heading and C_ACC or C_TEXT)
      end
    end
  elseif o.kind == "prompt" then
    local cols = math.min(COLS - 4, 70)
    local c0, r0 = (COLS - cols) // 2, ROWS // 2 - 2
    draw_box(c0, r0, cols, 3, o.label)
    U.zone(c0 * CW, r0 * CH, cols * CW, 3 * CH, "box")
    local cur = (frame // 30) % 2 == 0 and "_" or " "
    local shown = o.text:sub(-(cols - 4 - (o.comp and #o.comp.rest or 0)))
    local tx, ty = (c0 + 1) * CW, (r0 + 1) * CH
    print(shown, tx, ty, C_TEXT)
    if o.flash then print(shown:sub(-o.flash), tx + (#shown - math.min(o.flash, #shown)) * CW, ty, predict.C_PRED) end
    if o.comp then print(o.comp.rest, tx + #shown * CW, ty, predict.C_GHOST) end
    print(cur, tx + #shown * CW, ty, C_TEXT)
    chips({ "esc" }, "cancel", chips({ "enter" }, "OK", (c0 + 1) * CW, (r0 + 2) * CH), (r0 + 2) * CH)
  elseif o.kind == "confirm" then
    local cols = math.min(COLS - 4, math.max(#o.question + 4, 50))
    local c0, r0 = (COLS - cols) // 2, ROWS // 2 - 2
    draw_box(c0, r0, cols, 4, "bm Code")
    U.zone(c0 * CW, r0 * CH, cols * CW, 4 * CH, "box")
    print(o.question:sub(1, cols - 2), (c0 + 1) * CW, (r0 + 1) * CH, C_TEXT)
    local x = c0 + 1
    for i, c in ipairs(o.choices) do
      local on = i == (o.sel or 1)
      U.zone(x * CW, (r0 + 2) * CH, (#c + 2) * CW, CH, "choice", i)
      if on then rectfill(x * CW, (r0 + 2) * CH, (#c + 2) * CW, CH, C_SEL) end
      print(" " .. c .. " ", x * CW, (r0 + 2) * CH, on and 0xFFFFFF or C_TEXT)
      x = x + #c + 4
    end
    local x = chips({ "enter" }, "or the first letter", (c0 + 1) * CW, (r0 + 3) * CH)
    chips({ "esc" }, "cancel", x, (r0 + 3) * CH)
  else
    local menu = o.kind == "menu"
    local n = menu and #MENU or #o.items
    local cols = menu and 40 or math.min(COLS - 4, 60)
    local rows = math.min(n, ROWS - 6) + 2
    local c0, r0 = (COLS - cols) // 2, 2
    draw_box(c0, r0, cols, rows, menu and "bm Code" or "Open a cartridge (Enter)")
    U.zone(c0 * CW, r0 * CH, cols * CW, rows * CH, "box")
    local vis = rows - 2
    o.vis = vis
    if o.sel < (o.top or 1) then o.top = o.sel end
    if o.sel >= (o.top or 1) + vis then o.top = o.sel - vis + 1 end
    for r = 0, vis - 1 do
      local i = (o.top or 1) + r
      if i > n then break end
      local y = (r0 + 1 + r) * CH
      U.zone((c0 + 1) * CW, y, (cols - 2) * CW, CH, "item", i)
      if i == o.sel then rectfill((c0 + 1) * CW, y, (cols - 2) * CW, CH, C_SEL) end
      local label, right
      if menu then label, right = MENU[i][1], MENU[i][2]
      else
        local it = o.items[i]
        label = it.label
        right = it.size and string.format("%d KB", (it.size + 1023) // 1024) or ""
      end
      print(label:sub(1, cols - 12), (c0 + 2) * CW, y, i == o.sel and 0xFFFFFF or C_TEXT)
      local keys = menu and shortcut_keys(right)
      if keys then                       -- the shortcut's keys, on the right
        local kx = (c0 + cols - 1) * CW - chips_w(keys)
        for _, k in ipairs(keys) do kx = key_chip(k, kx, y, CH < 16) + 1 end
      else
        print(right, (c0 + cols - 1 - #right) * CW, y, C_DIM)
      end
    end
    local fy = (r0 + rows - 1) * CH
    local x = chips(key_or_pad("enter", "A"), menu and "choose" or "open", (c0 + 1) * CW, fy)
    chips(key_or_pad("esc", "B"), "back", x, fy)
  end
end

function _draw()
  U.begin()
  font(FONTS[font_i])
  cls(C_BG)
  draw_tabs()
  local nrows = ROWS - 2
  local ow, oh = pt.size()
  if pt.mode() then nrows = nrows - (oh + 4 + CH - 1) // CH end   -- the pad's overlay below the code
  if split then
    local left = (COLS - 1) // 2
    draw_pane(1, 0, left, 1, nrows)
    draw_pane(2, left + 1, COLS - left - 1, 1, nrows)
    rectfill(left * CW + CW // 2 - 1, CH, 2, nrows * CH, C_LINE)
    -- the page with the keys: a line on top
    local c0 = focus == 1 and 0 or left + 1
    local w = focus == 1 and left or COLS - left - 1
    rectfill(c0 * CW, CH, w * CW, 1, C_ACC)
  else
    draw_pane(1, 0, COLS, 1, nrows)
  end
  local t, v = current()
  if pt.mode() then
    pt.draw((W - ow) // 2, (nrows + 1) * CH + 2)
    font(FONTS[font_i])
  end
  draw_status(t, v)
  if overlay then draw_overlay() end
  assist.draw()
  font("8x16")
  U.menu_draw()                          -- the context menu, over everything
  font(FONTS[font_i])
end
