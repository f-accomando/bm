-- bm Code: the code editor of bm. Several cartridges open in tabs, two
-- pages side by side, a small sharp font (6x12: 106 columns, 28 lines of
-- code), the cartridges read and written in place: only their code changes,
-- sprites, map and cover stay as they are. The assistant on F6, and lines
-- "#entry: what you want #" that it carries out. F1: every key. With the
-- pad, Share turns on the writing with chords (src/ai/padtype.lua).

local assist = require "assist"
local padtype = require "padtype"

local W, H = SCREEN_W, SCREEN_H
local FONTS = { "6x12", "8x14", "8x16" }
local C_BG, C_PANE, C_BAR, C_LINE = 0x0E1016, 0x14161E, 0x22273A, 0x343B54
local C_TEXT, C_DIM, C_ACC, C_ERR, C_OK = 0xE0E4F0, 0x6A7290, 0xFFC050, 0xFF6464, 0x70E090
local C_SEL, C_CUR, C_ERRBG, C_GUT, C_GUTCUR = 0x2E4A8A, 0x1C2131, 0x4A1C24, 0x485068, 0xA8B0C8
local C_KW, C_API, C_STR, C_NUM, C_COM = 0xFF7AB0, 0x70D0FF, 0x90E070, 0xFFB060, 0x6A7690
local PAD_LANGS = { "it", "en" }

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

local tabs = {}               -- {path, name, title, author, res, lines, dirty, undo, redo, err}
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
local pad_lang = 1            -- the words of comments and strings: PAD_LANGS

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
  if path then t.path, t.name = path, base_name(path) end
  t.dirty = false
  log("code: saved " .. target)
  return true
end

-- the session (tabs, cursors, split, font): kept across a game tried with
-- cart_run, and from one time to the next
local function save_session()
  local s = { font = font_i, split = split, focus = focus, tabs = {}, panes = {},
              pad = { mode = padtype.mode(), lang = pad_lang } }
  local budget = 16000
  for i, t in ipairs(tabs) do
    local e = { path = t.path, title = t.title, author = t.author, res = t.res }
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
  for _, e in ipairs(s.tabs or {}) do
    if e.text then
      local i = new_tab(e.path, e.text, e)
      tabs[i].dirty = e.dirty or false
    elseif e.path then
      local c = cart_read(e.path)
      if c then new_tab(e.path, c.lua, c) end
    end
  end
  if s.pad then
    padtype.set{ mode = s.pad.mode }
    pad_lang = s.pad.lang or 1
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

------------------------------------------------------------------ pad typing

-- Share turns on the writing with the pad (src/ai/padtype.lua, guide in
-- docs/PADTYPE.md): a chord of the cross and the buttons writes a syllable,
-- the dictionary finishes the word, with the words of where the cursor is:
-- in the code Lua's, the API's and the tab's; after "--" and in strings
-- Italian (or English), from the marker on; after "#entry:" the questions
-- to the assistant.
local pad_words = { tab = nil, at = -1 }
local ASK = { it = 1, ask = 2 }           -- talking to the assistant: docs/PADTYPE.md

-- what the cursor is in: "code", "comment", "string" or "ask" (an #entry:
-- line), and where its text starts
local function pad_place(l, cx)
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

local edit_key

-- the line before the cursor; in a comment, a string or an #entry: only
-- from their marker (the first word has no word before it)
local pad_host = {}
function pad_host.before()
  local t, v = current()
  local l = t.lines[v.cy]:sub(1, v.cx)
  if pad_host.from then return pad_host.mark .. l:sub(pad_host.from) end
  return l
end
function pad_host.insert(s)
  for c in s:gmatch(".") do edit_key(c) end
end
function pad_host.erase(n)                -- n characters, not a whole indent step
  local t, v = current()
  snapshot(t, v, "bs")
  for _ = 1, n do
    local l = t.lines[v.cy]
    if v.cx > 0 then
      t.lines[v.cy] = l:sub(1, v.cx - 1) .. l:sub(v.cx + 1)
      v.cx = v.cx - 1
    elseif v.cy > 1 then
      v.cx = #t.lines[v.cy - 1]
      t.lines[v.cy - 1] = t.lines[v.cy - 1] .. l
      table.remove(t.lines, v.cy)
      v.cy = v.cy - 1
    end
  end
end
function pad_host.newline() edit_key("\n") end
function pad_host.move(d) edit_key(d) end
function pad_host.undo() edit_key("^z") end
function pad_host.exit() say("pad typing off (Share turns it on again)") end

-- the words where the cursor is
local PLACES = {
  code = { name = "lua", prose = false, weight = 0.6 },
  comment = { prose = true, mark = "--", weight = 0.15 },    -- names of the code, a few
  string = { prose = true, mark = '"', weight = 0 },         -- a capital at the start
  ask = { name = "ask", lang = ASK, prose = true, mark = ":", weight = 0.1 },
}
local function pad_context()
  local t, v = current()
  local place, from = pad_place(t.lines[v.cy], v.cx)
  local p = PLACES[place]
  local prose_lang = PAD_LANGS[pad_lang]
  pad_host.place, pad_host.from, pad_host.mark = place, from, p.mark
  pad_host.lang = p.lang or (p.prose and prose_lang or "lua")
  pad_host.name = p.name or prose_lang
  pad_host.prose = p.prose
  if pad_words.tab ~= t or padtype.presses() - pad_words.at >= 30 or padtype.presses() < pad_words.at then
    pad_words.tab, pad_words.at, pad_words.words = t, padtype.presses(), padtype.count_words(t.lines)
  end
  pad_host.words = p.weight > 0 and pad_words.words or nil
  pad_host.words_weight = p.weight
end

-- the prompt of a find, a file name...: one line of text, Start is Enter;
-- its words: the code's for a find, none for a number or a file name
local prompt_host = { prose = false }
function prompt_host.before() return overlay.text end
function prompt_host.insert(s) overlay.text = overlay.text .. s end
function prompt_host.erase(n) overlay.text = overlay.text:sub(1, #overlay.text - n) end
function prompt_host.newline() local o = overlay; overlay = nil; o.done(o.text) end
function prompt_host.exit() say("pad typing off") end
local function prompt_context()
  local code = overlay.pad == "code"
  prompt_host.lang, prompt_host.name = code and "lua" or "none", code and "lua" or "-"
  prompt_host.words, prompt_host.words_weight = code and pad_words.words or nil, 0.6
end

local function pad_typing(on)
  padtype.on(on)
  if on then
    pad_context()
    padtype.refresh(pad_host)
    say("pad typing: chords write, Share stops (" .. padtype.mode() .. ", " .. PAD_LANGS[pad_lang] ..
        " in comments)", C_ACC, 600)
  else
    say("pad typing off")
  end
end

------------------------------------------------------------------ actions

local function run_game()
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
  log("code: run " .. t.path)
  cart_run(t.path)
end

local function open_assistant(t, v)
  assist.open{ mode = "code", ctx = word_at(t, v),
               on_insert = function(code) insert_block(t, v, code) end }
end

local function explain_error(t, v)
  if not t.err then say("no error: F5 runs the game, F9 explains its error") return end
  assist.open{ error = t.err.msg, on_insert = function(code) insert_block(t, v, code) end }
end

local function prompt(label, text, done, pad)
  overlay = { kind = "prompt", label = label, text = text or "", done = done, pad = pad }
end

local function confirm(question, choices, done)
  overlay = { kind = "confirm", question = question, choices = choices, done = done }
end

local function new_cart()
  local n = 1
  while find_tab("/carts/GAME" .. n .. ".BM") or cart_read("/carts/GAME" .. n .. ".BM") do n = n + 1 end
  prompt("New cartridge, file name (8.3 in /carts):", "GAME" .. n .. ".BM", function(name)
    if not name:match("^[%w_%-]+%.[bB][mM]$") or #name:match("^[^.]*") > 8 then
      say("8.3 name, like MYGAME.BM", C_ERR)
      return
    end
    local path = "/carts/" .. name:upper()
    if cart_read(path) then say(path .. " exists: Ctrl+O opens it", C_ERR); return end
    local i = new_tab(path, TEMPLATE, { title = name:gsub("%.[bB][mM]$", "") })
    tabs[i].dirty = true
    show_tab(i)
    say("new: Ctrl+S writes " .. path, C_OK)
  end)
end

local function save_as(t)
  local suggest = t.path and base_name(t.path) or "GAME.BM"
  prompt("Save as (8.3 in /carts):", suggest, function(name)
    if not name:match("^[%w_%-]+%.[bB][mM]$") or #name:match("^[^.]*") > 8 then
      say("8.3 name, like MYGAME.BM", C_ERR)
      return
    end
    local ok, err = save_tab(t, "/carts/" .. name:upper())
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

local function open_files()
  local items = { { label = "+ New cartridge...", new = true } }
  for _, dir in ipairs({ "/carts", "/" }) do
    for _, f in ipairs(ls(dir)) do
      if not f.dir and f.name:lower():match("%.bm$") then
        local path = (dir == "/" and "" or dir) .. "/" .. f.name
        items[#items + 1] = { label = path, size = f.size, path = path }
      end
    end
  end
  overlay = { kind = "files", items = items, sel = math.min(2, #items), top = 1 }
end

local MENU = {
  { "New cartridge", "Ctrl+N" }, { "Open...", "Ctrl+O" }, { "Save", "Ctrl+S" },
  { "Save as...", "" }, { "Close tab", "Ctrl+W" }, { "Run the game", "F5" },
  { "Split screen", "F4" }, { "Font size", "F10" }, { "Find", "Ctrl+F" },
  { "Replace", "Ctrl+H" }, { "Go to line", "Ctrl+L" }, { "Assistant", "F6" },
  { "Explain the error", "F9" }, { "Pad typing", "Share" }, { "Pad mode", "" },
  { "Pad words in comments", "" }, { "Pad practice...", "" }, { "Keys", "F1" }, { "Exit", "" },
}

local function open_menu()
  for _, m in ipairs(MENU) do                -- the pad's settings, on the right
    if m[1] == "Pad mode" then m[2] = padtype.mode()
    elseif m[1] == "Pad words in comments" then m[2] = PAD_LANGS[pad_lang] end
  end
  overlay = { kind = "menu", sel = 1 }
end

local do_command

local function menu_choose(name)
  overlay = nil
  local t, v = current()
  if name == "New cartridge" then new_cart()
  elseif name == "Open..." then open_files()
  elseif name == "Save" then save_current()
  elseif name == "Save as..." then save_as(t)
  elseif name == "Close tab" then ask_close(panes[focus].tab)
  elseif name == "Run the game" then run_game()
  elseif name == "Split screen" then do_command("f4")
  elseif name == "Font size" then do_command("f10")
  elseif name == "Find" then do_command("^f")
  elseif name == "Replace" then do_command("^h")
  elseif name == "Go to line" then do_command("^l")
  elseif name == "Assistant" then open_assistant(t, v)
  elseif name == "Explain the error" then explain_error(t, v)
  elseif name == "Pad typing" then pad_typing(not padtype.is_on())
  elseif name == "Pad mode" then
    local nxt = { facile = "sillabe", sillabe = "steno", steno = "facile" }
    local what = { facile = " (the cross a clock of letters a-h, L2 i-p, R2 q-x)",
                   sillabe = " (cross + button = a syllable)", steno = " (groups on the diagonals, ia io ie)" }
    padtype.set{ mode = nxt[padtype.mode()] or "facile" }
    say("pad: " .. padtype.mode() .. what[padtype.mode()], C_ACC)
  elseif name == "Pad words in comments" then
    pad_lang = pad_lang % #PAD_LANGS + 1
    say("pad: words in comments and strings: " .. PAD_LANGS[pad_lang], C_ACC)
  elseif name == "Pad practice..." then
    local names = {}
    for _, tx in ipairs(padtype.TEXTS) do names[#names + 1] = tx.name end
    confirm("Write a text of 100 characters with the pad (" .. padtype.mode() .. "):", names, function(c)
      for i, tx in ipairs(padtype.TEXTS) do
        if tx.name == c then padtype.practice_open(i) end
      end
    end)
  elseif name == "Keys" then overlay = { kind = "help" }
  elseif name == "Exit" then quit_editor() end
end

------------------------------------------------------------------ keys

-- commands that do not depend on the text: the same from the menu
do_command = function(k)
  local t, v = current()
  if k == "f1" then overlay = { kind = "help" }
  elseif k == "f2" then show_tab((panes[focus].tab - 2) % #tabs + 1)
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
  elseif k == "f6" then open_assistant(t, v)
  elseif k == "f9" then explain_error(t, v)
  elseif k == "^s" then save_current()
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

edit_key = function(k)
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

local function overlay_key(k)
  local o = overlay
  if o.kind == "help" or o.kind == "text" then overlay = nil
  elseif o.kind == "prompt" then
    if k == "esc" then overlay = nil
    elseif k == "\n" then overlay = nil; o.done(o.text)
    elseif k == "\b" then o.text = o.text:sub(1, -2)
    elseif #k == 1 and k:byte() >= 32 then o.text = o.text .. k end
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
  if padtype.practice_is_open() then
    if not padtype.practice_update() then say("practice closed") end
    return
  end
  if padtype.is_on() then
    if not overlay then
      pad_context()
      padtype.update(pad_host)
      return
    elseif overlay.kind == "prompt" then
      prompt_context()
      padtype.update(prompt_host)
      return
    end
    padtype.wait()                       -- a menu: its keys, then the chords again
  end
  if btnp(9) and (not overlay or overlay.kind == "prompt") then
    pad_typing(true)
    if overlay then prompt_context(); padtype.refresh(prompt_host) end
    return
  end
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

------------------------------------------------------------------ frame

function _init()
  keyp()                                 -- typing on
  set_font(1)
  load_session()
  local a = cart_arg()
  if a and a.path then
    if not find_tab(a.path) then open_file(a.path) end
    show_tab(find_tab(a.path) or 1)
    if a.error then
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

local assist_was_open = false

function _update()
  frame = frame + 1
  if status_t > 0 then status_t = status_t - 1 end
  if assist.update() then assist_was_open = true; return end
  if assist_was_open and padtype.is_on() then  -- back from the panel: the words here
    pad_context()
    padtype.refresh(pad_host)
  end
  assist_was_open = false
  local k = keyp()
  while k do
    if overlay then overlay_key(k)
    elseif not do_command(k) then edit_key(k) end
    if assist.is_open() then break end
    k = keyp()
  end
  if not assist.is_open() then pad() end
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
  -- keep the cursor in sight
  if v.cy < v.top then v.top = v.cy end
  if v.cy >= v.top + nrows then v.top = v.cy - nrows + 1 end
  if v.cx < v.left then v.left = v.cx end
  if v.cx >= v.left + tcols then v.left = v.cx - tcols + 1 end
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
    print(string.rep(" ", digits - #num) .. num, x0, y, i == v.cy and C_GUTCUR or C_GUT)
    local ghost = i == v.cy and p == focus and not overlay and padtype.is_on() and padtype.ghost()
    if ghost and ghost ~= "" then l = l:sub(1, v.cx) .. ghost .. l:sub(v.cx + 1) end
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
    -- the pad's suggestion, then what it has just written, in their colours
    local function paint(a, txt, col)
      for k = 1, #txt do
        local cx = a + k - 1
        if cx >= v.left and cx < v.left + tcols then
          rectfill(tx + (cx - v.left) * CW, y, CW, CH, C_CUR)
          print(txt:sub(k, k), tx + (cx - v.left) * CW, y, col)
        end
      end
    end
    if ghost and ghost ~= "" then paint(v.cx, ghost, padtype.C_GHOST) end
    local fl, off = padtype.flash()
    if i == v.cy and p == focus and fl and padtype.is_on() then
      paint(v.cx - off - fl, l:sub(v.cx - off - fl + 1, v.cx - off), padtype.C_PRED)
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

local function draw_tabs()
  rectfill(0, 0, W, CH, C_BAR)
  local x = 0
  for i, t in ipairs(tabs) do
    local label = " " .. t.name .. (t.dirty and "*" or "") .. " "
    if split then
      if panes[1].tab == i then label = label .. "1 " end
      if panes[2].tab == i then label = label .. "2 " end
    end
    if x + #label > COLS - 6 then
      print("...", x * CW, 0, C_DIM)
      break
    end
    local on = panes[focus].tab == i
    if on then rectfill(x * CW, 0, #label * CW, CH, C_LINE) end
    print(label, x * CW, 0, on and 0xFFFFFF or C_DIM)
    x = x + #label + 1
  end
  print("F1 keys", (COLS - 7) * CW, 0, C_DIM)
end

local function draw_status(t, v)
  local y = (ROWS - 1) * CH
  rectfill(0, y, W, H - y, C_BAR)
  local right = string.format("ln %d/%d col %d  %s", v.cy, #t.lines, v.cx + 1, FONTS[font_i])
  local left = (t.path or "untitled") .. (t.dirty and " *" or "")
  if padtype.is_on() then left = "PAD " .. padtype.mode() .. " " .. (pad_host.name or "lua") .. "  " .. left end
  print(left, 0, y, C_TEXT)
  print(right, (COLS - #right) * CW, y, C_DIM)
  if status_t == 0 and entry_request(t.lines[v.cy]) then
    status, status_c, status_t = "Enter: the assistant does it", C_ACC, 1
  end
  if status ~= "" and status_t > 0 then
    local room = COLS - #left - #right - 4
    if room > 8 then print(status:sub(1, room), (#left + 2) * CW, y, status_c) end
  end
end

local HELP = {
  "Files", "Ctrl+N new cartridge", "Ctrl+O open", "Ctrl+S save", "Esc menu (save as...)",
  "Ctrl+W close tab", "F5 / Ctrl+R run, then back here", "",
  "Tabs and pages", "Ctrl+T new empty tab", "F2 / F3 previous / next tab", "F4 two pages side by side",
  "F7 the other page", "F10 font 6x12 / 8x14 / 8x16", "",
  "Editing", "Ctrl+Z undo, Ctrl+Y redo", "Ctrl+B start a selection", "Ctrl+C copy, Ctrl+X cut",
  "Ctrl+V paste (a line: above)", "Ctrl+K cut line, Ctrl+D duplicate", "Tab / Ctrl+U indent / unindent",
  "Ctrl+F find, Ctrl+G next", "Ctrl+H replace all", "Ctrl+L go to line", "",
  "Assistant", "F6 ask (the word under the cursor)", "F9 explain the game's error",
  "#entry: what to do #  then Enter:", "  the assistant does it here", "",
  "Pad", "cross moves, Y+cross pages/tabs", "X assistant, Start menu",
  "Share: writing with the pad on/off", "  easy: cross a-h, L2 i-p, R2 q-x",
  "  R2 / L2 / L2+R2 the suggestions", "  Start+cross moves, Start Enter",
}

local function draw_box(c0, r0, cols, rows, title)
  rectfill(c0 * CW, r0 * CH, cols * CW, rows * CH, C_PANE)
  rect(c0 * CW, r0 * CH, cols * CW, rows * CH, C_LINE)
  rectfill(c0 * CW, r0 * CH, cols * CW, CH, C_BAR)
  print(title, (c0 + 1) * CW, r0 * CH, C_ACC)
end

local function draw_overlay()
  local o = overlay
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
    local cur = (frame // 30) % 2 == 0 and "_" or " "
    print(o.text:sub(-(cols - 4)) .. cur, (c0 + 1) * CW, (r0 + 1) * CH, C_TEXT)
    print("Enter: OK   Esc: cancel", (c0 + 1) * CW, (r0 + 2) * CH, C_DIM)
  elseif o.kind == "confirm" then
    local cols = math.min(COLS - 4, math.max(#o.question + 4, 50))
    local c0, r0 = (COLS - cols) // 2, ROWS // 2 - 2
    draw_box(c0, r0, cols, 4, "bm Code")
    print(o.question:sub(1, cols - 2), (c0 + 1) * CW, (r0 + 1) * CH, C_TEXT)
    local x = c0 + 1
    for i, c in ipairs(o.choices) do
      local on = i == (o.sel or 1)
      if on then rectfill(x * CW, (r0 + 2) * CH, (#c + 2) * CW, CH, C_SEL) end
      print(" " .. c .. " ", x * CW, (r0 + 2) * CH, on and 0xFFFFFF or C_TEXT)
      x = x + #c + 4
    end
    print("Enter or the first letter; Esc: cancel", (c0 + 1) * CW, (r0 + 3) * CH, C_DIM)
  else
    local menu = o.kind == "menu"
    local n = menu and #MENU or #o.items
    local cols = menu and 40 or math.min(COLS - 4, 60)
    local rows = math.min(n, ROWS - 6) + 2
    local c0, r0 = (COLS - cols) // 2, 2
    draw_box(c0, r0, cols, rows, menu and "bm Code" or "Open a cartridge (Enter)")
    local vis = rows - 2
    if o.sel < (o.top or 1) then o.top = o.sel end
    if o.sel >= (o.top or 1) + vis then o.top = o.sel - vis + 1 end
    for r = 0, vis - 1 do
      local i = (o.top or 1) + r
      if i > n then break end
      local y = (r0 + 1 + r) * CH
      if i == o.sel then rectfill((c0 + 1) * CW, y, (cols - 2) * CW, CH, C_SEL) end
      local label, right
      if menu then label, right = MENU[i][1], MENU[i][2]
      else
        local it = o.items[i]
        label = it.label
        right = it.size and string.format("%d KB", (it.size + 1023) // 1024) or ""
      end
      print(label:sub(1, cols - 12), (c0 + 2) * CW, y, i == o.sel and 0xFFFFFF or C_TEXT)
      print(right, (c0 + cols - 1 - #right) * CW, y, C_DIM)
    end
    print(menu and "Enter/A choose  Esc/B back" or "Enter/A open  Esc/B back",
          (c0 + 1) * CW, (r0 + rows - 1) * CH, C_DIM)
  end
end

function _draw()
  font(FONTS[font_i])
  cls(C_BG)
  draw_tabs()
  local nrows = ROWS - 2
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
  draw_status(t, v)
  if overlay then draw_overlay() end
  if padtype.practice_is_open() then
    padtype.practice_draw()
    local pw, ph = padtype.size()
    padtype.draw((COLS - pw // CW - 1) * CW, (ROWS - ph // CH - 2) * CH, padtype.practice_hint())
  elseif padtype.is_on() and (not overlay or overlay.kind == "prompt") and not assist.is_open() then
    -- the panel of the chords, away from the cursor
    local pw, ph = padtype.size()
    local rows = ph // CH
    local row = (v.cy - v.top + 1) > ROWS - rows - 4 and 2 or ROWS - rows - 2
    padtype.draw((COLS - pw // CW - 1) * CW, row * CH)
  end
  assist.draw()
end
