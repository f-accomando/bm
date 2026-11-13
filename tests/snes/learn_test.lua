-- learn_test.lua: the Learn tab of bm SNES SDK on the PC: the lesson files (bm/snes_sdk/lessons/*.txt) and the helpers
-- (core/lua/sneslearn.lua). Every example is assembled with snes_asm and written as a ROM for the core runner
-- (tests/snes/learn_run.c); the test also checks that Insert, Open as new file and Run example alone keep the
-- files of the user byte for byte, and that the marks survive a save and an open.
--   build/host/luahost tests/snes/learn_test.lua [ROOT] [OUTDIR]     (make test-snes-learn)
local ROOT = arg[1] or "."
local OUT = arg[2]
package.path = ROOT .. "/core/src/script/?.lua;" .. package.path
local A = require "snes_asm"
local SL = assert(loadfile(ROOT .. "/core/lua/sneslearn.lua"))()
local SC = assert(loadfile(ROOT .. "/core/lua/snescode.lua"))()
local BF = assert(loadfile(ROOT .. "/core/lua/asmbuf.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

local function slurp(path) local f = io.open(path, "rb"); if not f then return nil end; local s = f:read("a"); f:close(); return s end

-- the list of the lesson files (the list is in the file LIST, one name per line: Lua has no directory listing)
local names = {}
for n in (slurp(ROOT .. "/bm/snes_sdk/lessons/LIST") or ""):gmatch("[^\r\n]+") do names[#names + 1] = n end
check(#names >= 10, "at least 10 lessons (" .. #names .. ")")

local lessons = {}
for i, n in ipairs(names) do
  local text = slurp(ROOT .. "/bm/snes_sdk/lessons/" .. n .. ".txt")
  check(text ~= nil, "lesson file " .. n)
  if text then lessons[#lessons + 1] = SL.parse(n, text) end
end

local manifest = {}
for _, L in ipairs(lessons) do
  local tag = L.name
  check(L.id > 0 and L.title ~= L.name and #L.title > 3, tag .. ": a number and a title")
  check(#L.goal > 10, tag .. ": a goal")
  check(#L.links >= 1, tag .. ": a link to an outside source")
  for _, lk in ipairs(L.links) do check(lk[2]:match("^https://%S+$") ~= nil, tag .. ": link " .. lk[2]) end
  local blocks = SL.blocks(L)
  local runs, tries = 0, 0
  for _, it in ipairs(L.items) do if it.k == "try" then tries = tries + 1 end end
  check(tries >= 1, tag .. ": a try this")
  for _, b in ipairs(blocks) do
    if b.run then
      runs = runs + 1
      local rom, res = SC.build({ ["main.s"] = b.text }, "main.s", string.format("LESSON %02d", L.id), nil)
      check(rom ~= nil, tag .. " block " .. b.n .. " assembles" .. (rom and "" or (": " .. tostring(res and res[1] and res[1].msg))))
      if rom then
        check(#rom >= 0x8000, tag .. " block " .. b.n .. " ROM size")
        check(#b.expect >= 1, tag .. " block " .. b.n .. " has an expect=")
        -- the same program through SL.run_alone
        local rom2 = SL.run_alone(L, b.n, SC.build)
        check(rom2 == rom, tag .. " block " .. b.n .. " run_alone gives the same ROM")
        if OUT then
          local rp = string.format("%s/%s_%d.sfc", OUT, L.name, b.n)
          local f = assert(io.open(rp, "wb")); f:write(rom); f:close()
          manifest[#manifest + 1] = rp .. " " .. b.frames .. " " .. (b.pad or "0") .. " " .. table.concat(b.expect, " ")
        end
      end
    end
  end
  check(runs >= 1, tag .. ": a program that runs")
  -- layout: every code line is in a row of its block; the rows are not empty
  local rows = SL.layout(L, 60)
  check(#rows > 5, tag .. " layout")
  local lines_in = 0
  for _, r in ipairs(rows) do if r.kind == "code" then lines_in = lines_in + 1; check(#r.text <= 100, tag .. ": code line too long") end end
  local want = 0
  for _, b in ipairs(blocks) do for _ in b.text:gmatch("\n") do want = want + 1 end end
  check(lines_in == want, tag .. " code rows " .. lines_in .. " = " .. want)
  for _, r in ipairs(rows) do if r.kind == "p" or r.kind == "goal" then check(#r.text <= 60, tag .. " paragraph row over the width") end end
end
check(#lessons >= 10, "lessons parsed")
if OUT then local f = assert(io.open(OUT .. "/manifest.txt", "wb")); f:write(table.concat(manifest, "\n") .. "\n"); f:close() end

-- the topics asked for are all there
local topics = { "register", "REP", "addressing", "LoROM", "vector", "NMI", "VRAM", "palette", "DMA", "background", "OAM", "joypad", "moving" }
for _, t in ipairs(topics) do
  local hits = SL.search(lessons, t)
  check(#hits > 0, "a lesson speaks of " .. t)
end

-- English only: no accented letters
for _, n in ipairs(names) do
  local s = slurp(ROOT .. "/bm/snes_sdk/lessons/" .. n .. ".txt")
  check(not s:find("[\128-\255]"), n .. " has only ASCII letters")
end

----------------------------------------------------------------- search

local hits = SL.search(lessons, "forced blank")
check(#hits >= 3, "search forced blank: " .. #hits)
check(#SL.search(lessons, "zzzzqq") == 0, "search with no hit")
check(#SL.search(lessons, "") == 0, "search empty")
local h2 = SL.search(lessons, "STA $2118")
local hit_code = false
for _, h in ipairs(h2) do if h.block then hit_code = true end end
check(hit_code, "search finds code lines")
check(#SL.search(lessons, "vram forced") > 0, "search with two words in any order")
do
  local L = lessons[7]
  local rows = SL.layout(L, 60)
  local r = SL.find_row(rows, "vmain", 1)
  check(r ~= nil and rows[r].text:lower():find("vmain", 1, true), "find_row")
  check(SL.find_row(rows, "zzzz", 1) == nil, "find_row none")
end

----------------------------------------------------------------- names

do
  local L = { id = 1, name = "01-cpu" }
  local taken = {}
  local n1 = SL.example_name(L, function(x) return taken[x] end)
  check(n1 == "less01.s", "first name " .. tostring(n1))
  taken[n1] = true
  local n2 = SL.example_name(L, function(x) return taken[x] end)
  check(n2 == "less01_2.s", "second name " .. tostring(n2))
  taken[n2] = true
  for i = 3, 12 do
    local n = SL.example_name(L, function(x) return taken[x] end)
    check(n and #n:match("^(.-)%.s$") <= 8, "8.3 name " .. tostring(n))
    taken[n] = true
  end
  local n13 = SL.example_name({ id = 13 }, function(x) return false end)
  check(n13 == "less13.s", "id 13")
end

----------------------------------------------------------------- Insert, Open, Run keep the files of the user

do
  local project = {                                   -- the files of the user (a table plays /docs)
    ["main.s"] = ".lorom\n.org $8000\nreset:\n    sei\nforever: bra forever\n",
    ["less01.s"] = "; my own file, named like an example\n",
    ["game.inc"] = "x = 1\n",
    ["main.sfc"] = "ROM-BYTES\0\1\2",
  }
  local function snapshot() local t = {}; for k, v in pairs(project) do t[k] = v end; return t end
  local function same(a, b)
    for k, v in pairs(a) do if b[k] ~= v then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
  end
  local before = snapshot()
  local L = lessons[1]
  -- Run example alone: nothing is written, the project is not read
  local reads = 0
  local rom = SL.run_alone(L, 1, function(files, main, title, reader)
    local keys = 0
    for _ in pairs(files) do keys = keys + 1 end
    check(keys == 1 and files[SL.SCRATCH], "scratch build has only the example")
    return SC.build(files, main, title, function(n) reads = reads + 1; return project[n] end)
  end)
  check(rom ~= nil, "run alone builds")
  check(same(project, before), "Run example alone keeps every file byte for byte")
  check(reads == 0, "Run example alone does not read the files of the user")
  local rom3, why = SL.run_alone(L, 2, SC.build)             -- block 2 does not exist
  check(rom3 == nil and why ~= nil, "run alone with a bad block")
  -- a text block is not a program
  local T = SL.parse("99-x", "title: x test\ngoal: a goal for the test\n---\n```text\nlda #1\n```\n")
  local r4, w4 = SL.run_alone(T, 1, SC.build)
  check(r4 == nil and w4:find("only text"), "a text block does not run")
  -- Open as new file: a new name, the others stay
  local function exists(n) return project[n] ~= nil end
  local function write(n, t) if project[n] then return nil, "exists" end; project[n] = t; return true end
  local name = SL.open_as_file(L, 1, exists, write)
  check(name == "less01_2.s", "open as file avoids less01.s: " .. tostring(name))
  check(project[name] == SL.blocks(L)[1].text, "the new file has the example")
  for k, v in pairs(before) do check(project[k] == v, "Open keeps " .. k) end
  local name2 = SL.open_as_file(L, 1, exists, write)
  check(name2 == "less01_3.s", "second open: " .. tostring(name2))
  for k, v in pairs(before) do check(project[k] == v, "Open (2) keeps " .. k) end
  local nm, e = SL.open_as_file(L, 9, exists, write)
  check(nm == nil and e, "open with a bad block")
  -- the new file assembles
  local rom5 = SC.build({ ["main.s"] = project[name] }, "main.s", "T", nil)
  check(rom5 ~= nil, "the opened file assembles")
  -- Insert at cursor: only the buffer changes; the file text is the old text plus the example; undo restores it
  local b = BF.new(before["main.s"])
  local old = BF.text(b)
  b.cy, b.cx = 4, 0
  BF.insert_block(b, SL.blocks(L)[1].text)
  check(BF.text(b) ~= old and BF.text(b):find("adc #$03", 1, true), "insert changed the buffer")
  check(same(project, project), "(project)")
  for k, v in pairs(before) do check(project[k] == v, "Insert keeps " .. k) end
  BF.key(b, "^z", 20)
  check(BF.text(b) == old, "undo after insert")
end

----------------------------------------------------------------- marks survive save and open

do
  local st = SL.new_state()
  SL.entry(st, "03-addressing").pos = 17
  SL.entry(st, "05-header").done = true
  SL.entry(st, "02-modes").pos = 1                     -- not read: no line
  st.last, st.split = "03-addressing", true
  SL.toggle_note(st, SL.note_key({ name = "04-memory" }, 1))
  SL.toggle_note(st, SL.note_key({ name = "09-dma" }, 1))
  local sdk = "-- bm viewer\nfunction _draw() end\n-- snes-sdk:begin\n--cfg bpp=4 set=0 large=0 cur=1 sel=2\n--pal 1 0000 001F\n-- snes-sdk:end\n"
  local lua = SL.merge_lua(sdk, st)
  check(lua:sub(1, #sdk) == sdk, "the Sprites marks stay as they are")
  local st2 = SL.parse_state(lua)
  check(st2.last == "03-addressing" and st2.split == true, "last lesson and split saved")
  check(st2.lessons["03-addressing"].pos == 17, "position saved")
  check(st2.lessons["05-header"].done == true, "done mark saved")
  check(SL.has_note(st2, "04-memory:1") and SL.has_note(st2, "09-dma:1") and #st2.notes == 2, "notes saved")
  -- a second save replaces the block (no second block), the sdk part is untouched
  st2.lessons["03-addressing"].pos = 30
  local lua2 = SL.merge_lua(lua, st2)
  local _, count = lua2:gsub("snes%-learn:begin", "")
  check(count == 1, "one learn block")
  check(SL.parse_state(lua2).lessons["03-addressing"].pos == 30, "position replaced")
  check(lua2:sub(1, #sdk) == sdk, "the Sprites marks stay (2)")
  -- the Sprites tab replaces its block: the learn block is outside it, so it stays
  local a = lua2:find("-- snes-sdk:begin", 1, true)
  local _, b2 = lua2:find("-- snes-sdk:end", a, true)
  local lua3 = lua2:sub(1, a - 1) .. "-- snes-sdk:begin\n--cfg bpp=2\n-- snes-sdk:end" .. lua2:sub(b2 + 1)
  check(SL.parse_state(lua3).lessons["05-header"].done, "the learn block survives a Sprites save")
  -- the marks of the learn block do not read as Sprites data
  check(not lua:sub(a, lua:find("-- snes-sdk:end", a, true)):find("lrn", 1, true), "the marks are apart")
  -- an empty state leaves the text as it was
  check(SL.merge_lua(sdk, SL.new_state()) == sdk, "no state, no block")
  check(SL.merge_lua(lua, SL.new_state()) == sdk, "an empty state removes the block")
  -- the toggle
  check(SL.toggle_note(st, "04-memory:1") == false and not SL.has_note(st, "04-memory:1"), "note off")
  -- no block in an old project
  check(SL.empty(SL.parse_state(sdk)), "old project: empty state")
end

io.write(string.format("snes learn: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
