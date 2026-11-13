-- sneslearn.lua: the helpers of the Learn tab of bm SNES SDK (no bm API; tests/snes/learn_test.lua runs them on the PC).
--
--   SL.parse(name, text)          a lesson from the text of a lesson file (format below)
--   SL.layout(lesson, cols)       the rows to draw: { kind, text, block, first } (kind: title goal h p code try link blank)
--   SL.blocks(lesson)             the code blocks in order: { n, lang, text, run, expect, row }
--   SL.search(lessons, query)     every line that has the words of the query: { lesson, text, block }
--   SL.find_row(rows, query, from) the first row from `from` that has the query
--   SL.example_name(lesson, taken) a free file name for "Open as new file" (less01.s, less01_2.s, ...)
--   SL.open_as_file(lesson, n, exists, write)   writes block n to a NEW file; never replaces a file
--   SL.run_alone(lesson, n, build) builds block n in a scratch buffer; nothing is written
--   SL.state_text(st) / SL.parse_state(lua) / SL.merge_lua(lua, st)   the read position, done marks and notes in the project
--
-- A lesson file (English only; one file per lesson):
--   title: What the CPU does              first lines: title:, goal:, link: Name | https://...  (link: may repeat)
--   goal: One sentence: what you can do after this lesson.
--   link: SNESdev Wiki | https://snes.nesdev.org/wiki/
--   ---                                   the line with three dashes ends the head
--   ## A heading                          a heading
--   Text lines. Lines that follow each other make one paragraph; an empty line ends it.
--   ```asm expect=w10=08,w11>00 frames=20 pad=0100   a code block. asm = a whole program (it assembles and runs); text = only shown.
--   ...
--   ```
--   try: A sentence that asks the reader to change the example.
-- expect= gives the checks of the test: w<hex addr>=<hex> (work RAM), v (video RAM), o (OAM), cg<n> (colour, 16 bit),
-- with = or >, and pic=1 (the picture shows more than the backdrop). frames= and pad= (hex, the held buttons).

local SL = {}

local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end

function SL.parse(name, text)
  text = text:gsub("\r\n", "\n"):gsub("\r", "\n"):gsub("\t", "    ")
  local L = { name = name, id = tonumber(name:match("^(%d+)")) or 0, title = name, goal = "", links = {}, items = {} }
  local lines = {}
  for l in (text .. "\n"):gmatch("(.-)\n") do lines[#lines + 1] = l end
  local i = 1
  while i <= #lines do                                -- the head
    local l = lines[i]
    i = i + 1
    if l == "---" then break end
    local k, v = l:match("^(%a+):%s*(.-)%s*$")
    if k == "title" then L.title = v
    elseif k == "goal" then L.goal = v
    elseif k == "link" then
      local n, u = v:match("^(.-)%s*|%s*(%S+)$")
      if n then L.links[#L.links + 1] = { n, u } end
    end
  end
  local para = nil
  local nblock = 0
  local function flush() if para then L.items[#L.items + 1] = { k = "p", text = table.concat(para, " ") }; para = nil end end
  while i <= #lines do
    local l = lines[i]
    i = i + 1
    local fence = l:match("^```(.*)$")
    if fence then
      flush()
      local code = {}
      while i <= #lines and not lines[i]:match("^```%s*$") do code[#code + 1] = lines[i]; i = i + 1 end
      i = i + 1
      nblock = nblock + 1
      local lang = fence:match("^(%a+)") or "text"
      local b = { k = "code", lang = lang, text = table.concat(code, "\n") .. "\n", n = nblock, run = lang == "asm", expect = {} }
      for e in fence:gmatch("expect=(%S+)") do
        for c in e:gmatch("[^,]+") do b.expect[#b.expect + 1] = c end
      end
      b.frames = tonumber(fence:match("frames=(%d+)")) or 30
      b.pad = fence:match("pad=(%x+)")
      L.items[#L.items + 1] = b
    elseif l:match("^## ") then flush(); L.items[#L.items + 1] = { k = "h", text = l:sub(4) }
    elseif l:match("^try:") then flush(); L.items[#L.items + 1] = { k = "try", text = trim(l:sub(5)) }
    elseif l:match("^%s*$") then flush()
    else para = para or {}; para[#para + 1] = trim(l) end
  end
  flush()
  return L
end

function SL.blocks(L)
  local out = {}
  for _, it in ipairs(L.items) do if it.k == "code" then out[#out + 1] = it end end
  return out
end

local function wrap(text, cols)
  local out = {}
  local line = ""
  for w in text:gmatch("%S+") do
    if line ~= "" and #line + #w + 1 > cols then out[#out + 1] = line; line = w
    else line = (line == "" and w) or (line .. " " .. w) end
  end
  if line ~= "" then out[#out + 1] = line end
  if #out == 0 then out[1] = "" end
  return out
end
SL.wrap = wrap

function SL.layout(L, cols)
  cols = math.max(16, cols)
  local rows = {}
  local function add(kind, text, block) rows[#rows + 1] = { kind = kind, text = text, block = block } end
  add("title", L.title)
  for _, l in ipairs(wrap(L.goal, cols)) do add("goal", l) end
  add("blank", "")
  for _, it in ipairs(L.items) do
    if it.k == "h" then add("h", it.text); add("blank", "")
    elseif it.k == "p" then for _, l in ipairs(wrap(it.text, cols)) do add("p", l) end; add("blank", "")
    elseif it.k == "try" then
      local w = wrap("Try this: " .. it.text, cols)
      for _, l in ipairs(w) do add("try", l) end
      add("blank", "")
    else
      add("bar", it.run and "program" or "text", it.n)             -- the line with the buttons of the block
      rows[#rows].first = true
      local k = 0
      for l in it.text:gmatch("([^\n]*)\n") do add("code", l, it.n); k = k + 1 end
      add("blank", "")
    end
  end
  if #L.links > 0 then
    add("h", "Read more (outside sources)")
    for _, lk in ipairs(L.links) do add("link", lk[1] .. "  " .. lk[2]) end
  end
  return rows
end

-- the first row (from `from`, then from the top) that has all the words of the query; else the first row that has the first word
function SL.find_row(rows, query, from)
  local words = {}
  for w in query:lower():gmatch("%S+") do words[#words + 1] = w end
  if #words == 0 then return nil end
  local function all(t)
    t = t:lower()
    for _, w in ipairs(words) do if not t:find(w, 1, true) then return false end end
    return true
  end
  for pass = 1, 2 do
    for k = 0, #rows - 1 do
      local i = ((from or 1) - 1 + k) % #rows + 1
      local t = rows[i].text:lower()
      if (pass == 1 and all(t)) or (pass == 2 and t:find(words[1], 1, true)) then return i end
    end
  end
end

-- every line that has all the words of the query (not case sensitive): title, goal, text, code and links
function SL.search(lessons, query)
  local words = {}
  for w in query:lower():gmatch("%S+") do words[#words + 1] = w end
  local out = {}
  if #words == 0 then return out end
  local function has(s)
    s = s:lower()
    for _, w in ipairs(words) do if not s:find(w, 1, true) then return false end end
    return true
  end
  for li, L in ipairs(lessons) do
    local function hit(s, block) out[#out + 1] = { lesson = li, text = s, block = block } end
    if has(L.title) then hit(L.title) end
    if has(L.goal) then hit(L.goal) end
    for _, it in ipairs(L.items) do
      if it.k == "code" then
        for l in it.text:gmatch("([^\n]*)\n") do if has(l) then hit(trim(l), it.n) end end
      elseif has(it.text) then hit(it.text)
      end
    end
  end
  return out
end

-- a free name for the example of a lesson: lessNN.s, then lessNN_2.s ... (8.3: the stem has 8 letters at most)
function SL.example_name(L, taken)
  local base = string.format("less%02d", L.id % 100)
  local n = 1
  while true do
    local suffix = n == 1 and "" or ("_" .. n)
    local name = base:sub(1, 8 - #suffix) .. suffix .. ".s"
    if not taken(name) then return name end
    n = n + 1
    if n > 999 then return nil end
  end
end

-- "Open as new file": the example goes to a file that did not exist; no file is replaced
function SL.open_as_file(L, n, exists, write)
  local b = SL.blocks(L)[n]
  if not b then return nil, "no such block" end
  local name = SL.example_name(L, function(x) return exists(x) end)
  if not name then return nil, "no free name" end
  local ok, e = write(name, b.text)
  if not ok then return nil, tostring(e) end
  return name
end

-- "Run example alone": build the block as one scratch file. Nothing is written; the open files are not read.
-- build(files, main, title, reader) is SC.build.
SL.SCRATCH = "(lesson)"
function SL.run_alone(L, n, build)
  local b = SL.blocks(L)[n]
  if not b then return nil, "no such block" end
  if not b.run then return nil, "this block is only text: it is not a program" end
  local rom, res = build({ [SL.SCRATCH] = b.text }, SL.SCRATCH, "LESSON " .. string.format("%02d", L.id), function() return nil end)
  if not rom then return nil, (res and res[1] and res[1].msg) or "the example does not build" end
  return rom, res
end

----------------------------------------------------------------- state in the project

local BEGIN, END = "-- snes-learn:begin", "-- snes-learn:end"

function SL.new_state() return { last = nil, split = false, lessons = {}, notes = {} } end

local function entry(st, name)
  local e = st.lessons[name]
  if not e then e = { pos = 1, done = false }; st.lessons[name] = e end
  return e
end
SL.entry = entry

function SL.empty(st)
  if st.last or st.split then return false end
  if #st.notes > 0 then return false end
  for _, e in pairs(st.lessons) do if e.done or e.pos > 1 then return false end end
  return true
end

function SL.state_text(st)
  local out = { BEGIN, "--lrn last=" .. (st.last or "") .. " split=" .. (st.split and 1 or 0) }
  local names = {}
  for n in pairs(st.lessons) do names[#names + 1] = n end
  table.sort(names)
  for _, n in ipairs(names) do
    local e = st.lessons[n]
    if e.done or e.pos > 1 then out[#out + 1] = "--ld " .. n .. " pos=" .. e.pos .. " done=" .. (e.done and 1 or 0) end
  end
  local notes = {}
  for i, k in ipairs(st.notes) do notes[i] = k end
  table.sort(notes)
  for _, k in ipairs(notes) do out[#out + 1] = "--ln " .. k end
  out[#out + 1] = END
  return table.concat(out, "\n")
end

function SL.parse_state(lua)
  local st = SL.new_state()
  local a = lua and lua:find(BEGIN, 1, true)
  if not a then return st end
  local body = lua:sub(a):match("^(.-)" .. END:gsub("%p", "%%%0")) or lua:sub(a)
  for line in body:gmatch("[^\r\n]+") do
    local l = line:match("^%-%-lrn (.*)$")
    if l then
      st.last = l:match("last=([%w_%-]+)")
      st.split = l:match("split=(%d)") == "1"
    end
    local n, rest = line:match("^%-%-ld ([%w_%-]+) (.*)$")
    if n then
      local e = entry(st, n)
      e.pos = math.max(1, tonumber(rest:match("pos=(%d+)")) or 1)
      e.done = rest:match("done=(%d)") == "1"
    end
    local k = line:match("^%-%-ln ([%w_%-]+:%d+)$")
    if k then st.notes[#st.notes + 1] = k end
  end
  return st
end

-- the project Lua with the learn block replaced (or added at the end; removed when the state is empty).
-- Everything outside the block, the marks of the Sprites data included, stays as it was.
function SL.merge_lua(lua, st)
  local a = lua:find(BEGIN, 1, true)
  local b = a and select(2, lua:find(END, a, true))
  if SL.empty(st) then
    if a and b then
      local pre, post = lua:sub(1, a - 1), lua:sub(b + 1):gsub("^\n", "", 1)
      return pre .. post
    end
    return lua
  end
  if a and b then return lua:sub(1, a - 1) .. SL.state_text(st) .. lua:sub(b + 1) end
  if lua ~= "" and lua:sub(-1) ~= "\n" then lua = lua .. "\n" end
  return lua .. SL.state_text(st) .. "\n"
end

function SL.note_key(L, n) return L.name .. ":" .. n end
function SL.has_note(st, key)
  for _, k in ipairs(st.notes) do if k == key then return true end end
  return false
end
function SL.toggle_note(st, key)
  for i, k in ipairs(st.notes) do if k == key then table.remove(st.notes, i); return false end end
  st.notes[#st.notes + 1] = key
  return true
end

return SL
