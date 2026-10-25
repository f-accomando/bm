-- Host tests of the Sound editor (carts/sound): the cartridge runs in a fake
-- bm (the same API, no pixels, an SD card in memory) with scripted and
-- random input. It checks that the bank the editor writes is byte for byte
-- the format of the console (the demo bank, packed by scripts/bmaudio.py,
-- comes back identical), that editing, saving into a game, importing and
-- the menus work, and that random input never stops it with an error.
--
--   luahost tests/sound/sim.lua carts/sound/main.lua demo.bmau

local SRC, DEMO = arg[1], arg[2]
local function readfile(p) local f = assert(io.open(p, "rb")); local d = f:read("a"); f:close(); return d end
local demo = readfile(DEMO)

local fails, checks = 0, 0
local function check(c, what)
  checks = checks + 1
  if not c then fails = fails + 1; print("FAIL " .. what) end
end

---------------------------------------------------------------- fake bm

local env = setmetatable({}, { __index = _G })
local calls = 0
local logs = {}
env.log = function(s) logs[#logs + 1] = s end
for _, n in ipairs({ "cls", "pset", "line", "rect", "rectfill", "circ", "circfill", "tri", "camera", "clip",
                     "note", "noteoff", "freq", "envelope", "duty" }) do
  env[n] = function() calls = calls + 1 end
end
env.SCREEN_W, env.SCREEN_H = 640, 360
env.screen = function(w, h) return false end
-- the keys as chips: prompt(name) alone measures
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
local cap                                 -- while not nil: the chips and the words drawn, { s, x, y, w }
env.prompt = function(n, x, y)
  assert(type(n) == "string", "prompt: a name")
  calls = calls + 1
  if type(x) ~= "number" then return chip_w(n), 16 end
  if cap then cap[#cap + 1] = { "[" .. n .. "]", x, y, chip_w(n) } end
  return x + chip_w(n)
end
local last_input = nil
env.lastinput = function() return last_input end
env.print = function(s, x, y, c, scale)
  assert(type(x) == "number" and type(y) == "number", "print: x, y must be numbers")
  calls = calls + 1
  if cap then cap[#cap + 1] = { tostring(s), x, y, #tostring(s) * 8 * (scale or 1) } end
  return x + #tostring(s) * 8 * (scale or 1)
end

-- input: the pad state of this frame, and keys typed
local pad, keys = {}, {}
local pad_prev = {}
env.btn = function(i) return pad[i] or false end
env.btnp = function(i) return (pad[i] and not pad_prev[i]) or false end
env.keyp = function() return table.remove(keys, 1) end
local f12 = false
env.keyheld = function(k) return k == "f12" and f12 end
local keyhelp_list, keyhelp_title
env.keyhelp = function(list, title) keyhelp_list, keyhelp_title = list, title; return 0 end
local shown_keys = 0
env.showkeys = function() shown_keys = shown_keys + 1; return true end

-- the SD card
local files = {
  ["/bm/pong.bm"] = { title = "Pong", bank = false },
  ["/bm/astrowing.bm"] = { title = "Astro Wing", bank = false },
  ["/bm/sounds/OTHER.BM"] = { title = "OTHER", bank = demo },
}
env.ls = function(dir)
  local out = {}
  for p in pairs(files) do
    local d, n = p:match("^(.*)/([^/]+)$")
    if d == dir or (dir == "/" and d == "") then out[#out + 1] = { name = n, size = 1000, dir = false } end
  end
  return out
end
local own = demo
env.cart_audio = function(path)
  if path == nil then return own end
  local f = files[path]
  if not f then return nil, "file not found" end
  return f.bank, f.title
end
local puts = {}
-- a game (.bm) is read only: the sounds go into its editable copy (.BME)
local project_target = dofile("tests/studio/project_rules.lua")
env.cart_put_audio = function(path, data, title, lua)
  assert(type(data) == "string" and data:sub(1, 4) == "BMAU", "cart_put_audio: a bank")
  local moved
  path, moved = project_target(path, function(p) return files[p] ~= nil end, function(a, b)
    files[b] = { title = files[a].title, bank = files[a].bank }
  end)
  if not files[path] then
    assert(type(lua) == "string" and #lua > 0, "a new pack needs its Lua")
    files[path] = { title = title }
  end
  files[path].bank = data
  puts[#puts + 1] = path
  if moved then return true, path end
  return true
end
local arg_ = nil
env.cart_arg = function() return arg_ end
local ran = nil
env.cart_run = function(p) ran = p end
local quitted = false
env.quit = function() quitted = true end

-- the player
local banks, last_bank = 0, nil
local music_state, sfx_state = nil, {}
local played = {}
env.audio_bank = function(d)
  assert(d == nil or (type(d) == "string" and d:sub(1, 4) == "BMAU"), "audio_bank: a bank")
  banks = banks + 1
  last_bank = d
  return true
end
env.music = function(n, fade, pos)
  if n == nil then
    if music_state then return music_state.song, music_state.pos, 0, 0 end
    return nil
  end
  if n < 0 then music_state = nil else music_state = { song = n, pos = pos or 0 } end
end
env.audio_pattern = function(p, bpm, swing)
  if p < 0 then music_state = nil else music_state = { song = -2, pos = 0, pat = p } end
end
env.sfx = function(n, v)
  if n < 0 then if v then sfx_state[v] = nil else sfx_state = {} end return end
  sfx_state[v or 7] = n
  return v or 7
end
env.sfxpos = function(v) if sfx_state[v] then return sfx_state[v], 0 end return nil end
env.audio_play = function(v, s, n, vol, fx, ms)
  assert(v >= 0 and v < 8 and n >= 1 and n <= 127, "audio_play: voice and note")
  played[#played + 1] = n
end
local mutes = {}
env.mute = function(t, on) mutes[t] = on end
-- two of the instruments (src/audio/presets.c), as instruments() and instrument() give them
local INSTR = {
  epiano = { name = "epiano", kind = "keys", about = "electric piano (FM)", wave = 6, duty = 128, vol = 130,
             a = 0, d = 150, s = 70, r = 60, pitch = 0, ptime = 0, vdepth = 0, vrate = 0, detune = 0,
             tone = { 0, 0, 0, 0, 0, 0, 16, 80, 70, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  kick = { name = "kick", kind = "drum", about = "808 kick", wave = 4, duty = 128, vol = 230,
           a = 0, d = 70, s = 0, r = 30, pitch = 30, ptime = 5, vdepth = 0, vrate = 0, detune = 0,
           tone = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
}
env.instruments = function() return { INSTR.kick, INSTR.epiano } end
env.instrument = function(n) return INSTR[n] end
local plays = 0
env.play = function(v, name, note) assert(INSTR[name], "play: an instrument"); plays = plays + 1; return v end
-- the music assistant: ai.music's pieces (src/ai/lua_music.c) and a panel
-- that hands one over at once, after its preview (src/ai/assist.lua)
local function word(n, i, v, fx) return n | i << 8 | v << 16 | (fx or 0) << 24 end
local PIECES = {
  base = { gen = "base.pop", name = "pop backing", kind = "base", bpm = 110, swing = 0, key = 7, minor = false,
           meter = 16, bars = 8, echo = 0, room = 110, seed = 1, chords = { "G", "D", "Em", "C" },
           instruments = { "kick", "epiano" },
           patterns = { { len = 64, tracks = { [0] = { word(36, 0, 230) }, [4] = { word(64, 1, 200) } } },
                        { len = 64, tracks = { [0] = { word(36, 0, 230) } } } } },
  melody = { gen = "melody.calm", name = "calm melody", kind = "melody", bpm = 84, swing = 0, key = 0,
             minor = false, meter = 16, bars = 4, echo = 0, room = 100, seed = 1, chords = { "C" },
             instruments = { "epiano" }, patterns = { { len = 64, tracks = { [6] = { word(72, 0, 225), 0, word(128, 0, 0) } } } } },
  sfx = { gen = "sfx.coin", name = "coin", kind = "sfx", seed = 3, chords = {}, instruments = { "kick" }, patterns = {},
          sfx = { ms = 50, loop = { 0, 0 }, steps = { word(83, 0, 230), word(88, 0, 230), word(128, 0, 0) } } },
}
local asked, previewed = {}, 0
env.ai = { music = function(q) return PIECES[q] end }
local panel
local fake_assist = {
  open = function(o) panel = o end,
  is_open = function() return panel ~= nil end,
  update = function()
    local o = panel
    panel = nil
    local piece = PIECES[asked.next]
    o.on_preview(piece)
    previewed = previewed + 1
    o.on_close()
    o.on_music(piece)
    return true
  end,
  draw = function() end,
}
local bmui, bmlib
env.require = function(name)
  if name == "assist" then return fake_assist end
  if name == "bmui" then                   -- the mouse, the real one (it takes keyp over)
    bmui = bmui or assert(loadfile((SRC:match("^(.-)carts/") or "./") .. "src/script/bmui.lua", "t", env))()
    return bmui
  end
  if name == "bmlib" then
    bmlib = bmlib or assert(loadfile((SRC:match("^(.-)carts/") or "./") .. "src/script/bmlib.lua", "t", env))()
    return bmlib
  end
  error("no module " .. name)
end

-- the mouse (bmui): MS as a USB mouse would give it
local MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
env.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
env.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end

-- the documents (/docs): WAV and MIDI files for the importer
local docs = {}
env.doc_list = function()
  local out = {}
  for n, d in pairs(docs) do out[#out + 1] = { name = n, size = #d } end
  return out
end
env.doc_read = function(n)
  if docs[n] then return docs[n] end
  return nil, "no document " .. n
end
local B                                   -- the cartridge's bank functions (the hook in main.lua)
env._TEST_HOOK = function(b) B = b end

local api = {}
for k in pairs(env) do api[k] = true end
local chunk = assert(load(readfile(SRC), "=main.lua", "t", env))
chunk()

---------------------------------------------------------------- a bank reader (the format of player.h)

local function parse(d)
  local b = { sounds = {}, sfx = {}, pats = {}, songs = {} }
  local ver, ns, nx, np, ng = d:byte(5, 9)
  local pos = 17
  for i = 1, ns do
    b.sounds[i] = d:sub(pos, pos + 7):match("^[^%z]*")
    b.waves = b.waves or {}
    b.waves[i] = d:byte(pos + 8)
    b.tones = b.tones or {}
    b.tones[i] = ver >= 2 and { d:byte(pos + 24, pos + 44) } or nil
    pos = pos + (ver >= 2 and 48 or 24)
  end
  for i = 1, nx do
    local name, ms, len, ls, le
    name, ms, len, ls, le, pos = string.unpack("<c8HBBBxxx", d, pos)
    local steps = {}
    for k = 1, len do steps[k] = { d:byte(pos, pos + 3) }; pos = pos + 4 end
    b.sfx[i] = { name = name:match("^[^%z]*"), ms = ms, len = len, steps = steps }
  end
  for i = 1, np do
    local len, mask = d:byte(pos, pos + 1)
    pos = pos + 4
    local tracks = {}
    for t = 0, 7 do
      if mask & 1 << t ~= 0 then
        tracks[t] = {}
        for k = 1, len do tracks[t][k] = { d:byte(pos, pos + 3) }; pos = pos + 4 end
      end
    end
    b.pats[i] = { len = len, tracks = tracks }
  end
  for i = 1, ng do
    local name, bpm, swing, n, loop, echo, room
    name, bpm, swing, n, loop, echo, room, pos = string.unpack("<c8BBBBBBxx", d, pos)
    b.songs[i] = { name = name:match("^[^%z]*"), bpm = bpm, echo = echo, room = room,
                   order = { d:byte(pos, pos + n - 1) } }
    pos = pos + n
  end
  b.samples = {}
  for i = 1, ver >= 3 and d:byte(10) or 0 do           -- the samples: 32 bytes, then the frames
    local name, frames, _, fmt, ch
    name, frames, _, fmt, ch = string.unpack("<c8I4I4BB", d, pos)
    b.samples[i] = name:match("^[^%z]*")
    pos = pos + 32 + frames * ch * ({ 1, 2, 3, 4, 4 })[fmt + 1]
  end
  check(pos == #d + 1, "the bank has no bytes left over")
  return b
end

---------------------------------------------------------------- running it

local frames = 0
local function frame()
  -- the hints follow what was pressed last: keyboard, a DS4, another pad
  last_input = ({ "keyboard", "ds4", "pad", nil })[frames // 50 % 4 + 1]
  env._update()
  env._draw()
  for i = 0, 9 do pad_prev[i] = pad[i] end
  frames = frames + 1
end
local function run(n) for _ = 1, n or 1 do frame() end end
local function press(...)                 -- buttons held together for 3 frames, then released
  local b = { ... }
  for _, i in ipairs(b) do pad[i] = true end
  run(3)
  for _, i in ipairs(b) do pad[i] = false end
  run(2)
end
local function hold_then(btn_hold, btn_tap)  -- hold one button, tap another
  pad[btn_hold] = true
  run(2)
  pad[btn_tap] = true
  run(1)
  pad[btn_tap] = false
  run(1)
  pad[btn_hold] = false
  run(2)
end
local function type_keys(...) for _, k in ipairs({ ... }) do keys[#keys + 1] = k end run(#{ ... } + 2) end
local L, R, U, D, A, BB, X, Y, START, SELECT = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9

env._init()
run(2)

-- 1. the demo bank comes back byte for byte
check(last_bank == demo, "the demo bank is packed back exactly as scripts/bmaudio.py made it")

-- 2. every page draws, with the pad and the keyboard
for page = 1, 4 do
  type_keys("f" .. page)
  run(3)
end
check(calls > 1000, "the pages draw")
hold_then(SELECT, R)
hold_then(SELECT, L)

-- 3. the pattern page: a note with A, changed with A + up, a sound with Y + up
type_keys("f3")
type_keys("down", "down")                -- pattern 0 (the intro beat), track 2: the hat
type_keys("down", "down", "down", "down")   -- track 6 (voice 6): empty in pattern 0
press(A)                                  -- a note
local b = parse(last_bank)
local st = b.pats[1].tracks[6] and b.pats[1].tracks[6][1]
check(st and st[1] == 60, "A on an empty step puts the last note (C4)")
hold_then(A, U)
b = parse(last_bank)
check(b.pats[1].tracks[6][1][1] == 61, "A + up raises the note")
hold_then(Y, U)
b = parse(last_bank)
check(b.pats[1].tracks[6][1][2] ~= 5, "Y + up changes the sound")
hold_then(BB, U)
b = parse(last_bank)
check(b.pats[1].tracks[6][1][4] >> 4 ~= 0, "B + up sets an effect")
press(X)
b = parse(last_bank)
check(not b.pats[1].tracks[6], "X clears the step (the track is empty again)")
type_keys("^z")
b = parse(last_bank)
check(b.pats[1].tracks[6] and b.pats[1].tracks[6][1][1] == 61, "undo brings the step back")
-- the piano types notes and moves on
type_keys("right", "z", "c", "b")
b = parse(last_bank)
local tr = b.pats[1].tracks[6]
check(tr[2][1] == 60 and tr[3][1] == 64 and tr[4][1] == 67, "the piano keys type C4 E4 G4")
check(#played > 0, "the notes are heard while editing")

-- 3b. solo (a, or B on a track's name) and the piano-roll view (f, or B on a step)
do
  local function prints(y0, y1)           -- the note names printed in the plot, as name@x
    local got, old = {}, env.print
    env.print = function(str, x, y, c, sc)
      if x >= 124 and y >= y0 and y < y1 and str:match("^[A-G]#?%-?%d+$") then got[#got + 1] = str .. "@" .. x end
      return old(str, x, y, c, sc)
    end
    frame()
    env.print = old
    table.sort(got)
    return table.concat(got, " ")
  end
  local function text_shown(str)
    local seen, old = false, env.print
    env.print = function(s2, x, y, c, sc) if s2 == str then seen = true end return old(s2, x, y, c, sc) end
    frame()
    env.print = old
    return seen
  end
  local bank0, banks0 = last_bank, banks
  -- solo: the others are muted by the same mechanism, nothing is saved
  type_keys("left", "left", "left", "left", "left", "left")     -- the header of track 6 (voice 6)
  type_keys("a")
  local ok = true
  for t = 0, 7 do if (mutes[t] == true) == (t == 6) then ok = false end end
  check(ok, "solo: a mutes every track but the one on the page")
  check(text_shown("SOLO: 7"), "solo: the page says which tracks are solo")
  type_keys("down", "a")                                         -- a second track
  ok = true
  for t = 0, 7 do if (mutes[t] == true) == (t == 6 or t == 7) then ok = false end end
  check(ok, "solo: several tracks can be solo")
  type_keys("a")
  check(mutes[7] == true and mutes[6] == false, "solo: a again takes the track out")
  type_keys("up", "a")
  ok = true
  for t = 0, 7 do if mutes[t] then ok = false end end
  check(ok, "solo: the last one off, every track plays")
  press(BB)                                                      -- the pad: B on a track's name
  check(mutes[0] == true and mutes[6] == false, "solo: B on a track's name")
  press(BB)
  check(mutes[0] == false, "solo: B again clears it")
  type_keys("a", "\n")                                           -- A on the header: the mute flag
  check(mutes[6] == true, "solo: the mute flag of a solo track still mutes it")
  type_keys("\n", "a")
  check(banks == banks0 and last_bank == bank0, "solo: nothing is sent or saved (the file bytes are the same)")
  ok = true
  for t = 0, 7 do if mutes[t] then ok = false end end
  check(ok, "solo: all clear again")

  -- the roll shows the notes of the grid
  type_keys("right", "right", "right", "right", "right")          -- step 5: empty
  local row = 78 + 6 * 30
  local grid = prints(row, row + 30)
  check(grid:find("C4@", 1, true) and grid:find("E4@", 1, true) and grid:find("G4@", 1, true), "the grid has the notes: " .. grid)
  type_keys("f")
  check(text_shown("VIEW: PIANO ROLL"), "roll: the view is named")
  local roll = prints(78, 78 + 240)
  check(roll == grid, "roll: the same notes in the same steps as the grid (" .. roll .. " / " .. grid .. ")")
  -- an edit in the roll is the same data as in the grid, the same file bytes
  type_keys("\n")
  local from_roll = last_bank
  check(from_roll ~= bank0, "roll: A adds a note")
  type_keys("^z")
  check(last_bank == bank0, "roll: undo")
  type_keys("f")
  check(text_shown("VIEW: STEP GRID"), "roll: f again is the grid")
  type_keys("\n")
  check(last_bank == from_roll, "the file saved from the roll and from the grid is the same for the same edit")
  type_keys("^z")
  -- the arrows move the cursor; A drops the note there; A on it removes it
  type_keys("f", "\n")
  local n0 = parse(last_bank).pats[1].tracks[6][5][1]
  type_keys("up", "up", "\n")
  check(parse(last_bank).pats[1].tracks[6][5][1] == n0 + 2, "roll: up up then A moves the note two semitones")
  type_keys("down", "\n")
  check(parse(last_bank).pats[1].tracks[6][5][1] == n0 + 1, "roll: down then A moves it back one")
  type_keys("\n")
  check(parse(last_bank).pats[1].tracks[6][5][1] == 0, "roll: A on the note removes it")
  type_keys("^z", "^z", "^z", "^z")
  press(BB)                                                       -- B on a step is the view
  check(text_shown("VIEW: STEP GRID"), "roll: B on a step is the view (the grid again)")
  press(BB)
  check(text_shown("VIEW: PIANO ROLL"), "roll: B on a step again, the roll")
  type_keys("f")
  check(last_bank == bank0, "the views change nothing in the file")
end

-- 4. START plays the pattern, the song page plays the song
press(START)
check(music_state and music_state.song == -2, "START loops the pattern")
press(START)
check(music_state == nil, "START again stops")
type_keys("f4")
press(START)
check(music_state and music_state.song == 0, "START plays the song")
press(START)

-- 5. a sound: its name with the keyboard, a value with A + right
type_keys("f1", "up")                     -- the name field
type_keys("\n", "\b", "\b", "\b", "\b", "B", "O", "O", "M", "\n")
b = parse(last_bank)
check(b.sounds[1] == "BOOM", "the sound is renamed with the name editor")
-- the name editor with the mouse (M48): a click on a letter box chooses it, the keyboard types there
local function mclick(x, y)
  for _, b in ipairs({ 0, 1, 0 }) do
    local was = MS.b
    MS.x, MS.y, MS.b, MS.w = x, y, b, 0
    MS.p = MS.b & ~was
    frame()
    MS.p = 0
  end
  frame()
end
type_keys("\n")                          -- the editor again: BOOM, the first letter chosen
mclick(170 + 2 * 38 + 16, 175)            -- the third box
type_keys("Q", "\n")
b = parse(last_bank)
check(b.sounds[1] == "BOQM", "mouse: a click on a letter box of the name editor chooses the letter: " .. tostring(b.sounds[1]))
type_keys("\n")
mclick(170 + 2 * 38 + 16, 175)
type_keys("O", "\n")
b = parse(last_bank)
check(b.sounds[1] == "BOOM", "mouse: the name is back")
type_keys("down")
hold_then(A, R)

-- 5b. down past the first group: FILTER (the cutoff, the filter envelope),
-- then WAVE & SPACE (the place); each is a byte of the sound's tone
type_keys("down", "down", "down", "down")            -- past DETUNE: CUTOFF
type_keys("+")
b = parse(last_bank)
check(b.tones[1] and b.tones[1][1] == 12, "the cutoff is the tone's first byte (+12)")
type_keys("right", "=")                               -- FILT ENV +1
b = parse(last_bank)
check(b.tones[1][4] == 1, "the filter envelope is the tone's fourth byte")
type_keys("down", "down", "down", "down", "-")        -- WAVE & SPACE: PAN, one to the left
b = parse(last_bank)
check(b.tones[1][6] == 255, "the place is a signed byte (-1)")
check(b.tones[1][12] == 40, "a sound has a little room by default")

-- 5c. the menu's Instrument...: the list plays each one, A puts it in the sound
type_keys("esc", "pgup", "pgup", "down", "down", "down", "down", "down", "down", "down", "\n")
local before = plays
type_keys("down")                                     -- epiano: heard while chosen
check(plays > before, "the instrument list plays what it is on")
type_keys("\n")
b = parse(last_bank)
check(b.waves[1] == 6 and b.tones[1][7] == 16 and b.tones[1][8] == 80, "the instrument fills the sound (FM, its ratio and depth)")
check(b.sounds[1] == "BOOM", "a named sound keeps its name")

-- 5d. the song's echo (in steps) and room
type_keys("f4", "up", "right", "right", "right", "right", "=")
b = parse(last_bank)
check(b.songs[1].echo == 1, "the song's echo, in steps")
type_keys("right", "=")
b = parse(last_bank)
check(b.songs[1].room == 16, "the song's room")
type_keys("down")

-- 6. the sound effect page: steps with the piano, length follows
type_keys("f2", "pgdn", "pgdn", "pgdn", "pgdn", "pgdn", "pgdn", "pgdn", "pgdn", "pgdn")   -- sfx 9: empty
type_keys("q", "w", "e", "r")
b = parse(last_bank)
check(b.sfx[10] and b.sfx[10].len == 8 and b.sfx[10].steps[1][1] == 72 and b.sfx[10].steps[4][1] == 77,
      "the piano writes the steps of a new sound effect")

-- 7. open a game without sounds, make a sound, save into it
-- the menu: 1 Play, 2 Undo, 3 Save, 4 Save as a new pack, 5 Open, 6 Import, 7 Export...
local function menu_item(n)
  type_keys("esc", "pgup", "pgup")
  for _ = 1, n - 1 do type_keys("down") end
end
menu_item(5)
type_keys("\n")                           -- Open...: ask, the project is modified
type_keys("\n")                           -- yes
-- the picker: astrowing.bm, pong.bm (games), then OTHER.BM
type_keys("down", "\n")                   -- pong.bm
type_keys("f3", "\n")
type_keys("^s")
check(puts[#puts] == "/bme/pong.bme", "Save writes into the editable copy of the game opened")
check(files["/bm/pong.bm"].bank == false, "the game stays as it was")
b = parse(files["/bme/pong.bme"].bank)
check(#b.pats >= 1 and b.pats[1].tracks[0], "the game got the pattern")
check(logs[#logs]:find("saved /bme/pong.bme", 1, true), "the save is in the log")

-- 8. import a song (with its patterns and sounds) from the pack
menu_item(6)
type_keys("\n")                           -- Import from...
type_keys("down", "down", "down", "\n")   -- OTHER.BM (after astrowing.bm, pong.bm, PONG.BME)
type_keys("down", "down", "down", "\n")   -- a song
type_keys("\n")                           -- song 0 (DEMO)
b = parse(last_bank)
check(#b.songs >= 2 and #b.pats >= 10 and #b.sounds >= 6, "the song came with its patterns and sounds")

-- 9. save as a new pack
menu_item(4)
type_keys("\n")                           -- Save as a new sound pack...
type_keys("\b", "\b", "\b", "\b", "\b", "\b", "\b", "\b", "M", "I", "N", "E", "\n")
check(files["/bme/mine.bme"] and files["/bme/mine.bme"].bank, "a new sound pack is written (a project)")

-- 9b. the assistant (F6): a backing track into new patterns and a song of its
-- own, a melody over the pattern on the page, a sound effect into a free slot
local function compose_with(kind)
  asked.next = kind
  type_keys("f6")
  run(2)
end
compose_with("base")
b = parse(last_bank)
local song = b.songs[#b.songs]
check(song and song.name == "POP BACK", "the backing track has a song of its own")
check(song.bpm == 110 and #song.order == 2, "its tempo, its two patterns")
local p1 = b.pats[song.order[1] + 1]
check(p1 and p1.len == 64 and p1.tracks[0] and p1.tracks[0][1][1] == 36, "its kick in the new pattern")
local kick_slot = p1.tracks[0][1][2]
check(b.sounds[kick_slot + 1] == "KICK", "its instrument became a sound of the bank")
check(previewed == 1, "it was played before it went in")
local nsongs = #b.songs
type_keys("^z")
b = parse(last_bank)
check(#b.songs == nsongs - 1, "Ctrl+Z takes the backing track back")
type_keys("f3")
compose_with("melody")
b = parse(last_bank)
local found = false
for _, pt in ipairs(b.pats) do
  local tr = pt.tracks[6]
  if tr and tr[1][1] == 72 then found = true end
end
check(found, "the melody went on track 6 of the pattern on the page")
compose_with("sfx")
b = parse(last_bank)
local coin = b.sfx[#b.sfx]
check(coin and coin.name == "COIN" and coin.steps[1][1] == 83 and coin.steps[3][1] == 128, "the coin into a free sound effect")

-- 9b. the mouse (bmui, 2026-10-06): tabs, lists, bars, cells, the menu by clicking
local function mouse(x, y, b, w)
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = x, y, b or 0, w or 0
  MS.p = MS.b & ~was
  frame()
  MS.p, MS.w = 0, 0
end
local function click(x, y, b) mouse(x, y, 0); mouse(x, y, b or 1); mouse(x, y, 0); frame() end
run(300)
click(110, 15)                             -- the first tab: SOUNDS
click(60, 60 + 3 * 21)                     -- the fourth sound in the list
mouse(200, 230, 0, 1)                      -- the wheel on a bar: +1
mouse(200, 230, 1); mouse(240, 230, 1); mouse(260, 230, 1); mouse(260, 230, 0)
run(2)
check(true, "mouse: SOUNDS, a sound clicked, a bar wheeled and dragged")
local before = last_bank
click(300, 15)                             -- the PATTERN tab (the third)
run(2)
mouse(124 + 2 * 30 + 10, 78 + 10)
click(124 + 2 * 30 + 10, 78 + 10)          -- a cell: a note toggled
run(3)
check(last_bank ~= before, "mouse: a click on a cell of the pattern changes the bank")
mouse(124 + 3 * 30 + 10, 78 + 10, 0, 1)    -- the wheel on a cell
click(400, 200, 2)                         -- the right button: the context menu
run(1)
click(10, 345)                             -- outside: closed
click(560, 348)                            -- the SELECT menu at the bottom right
run(2)
click(320, 20)                             -- outside the menu: Esc
run(2)
check(true, "mouse: the context menu and the menu")

do   -- the roll with the mouse: a click puts a note at the pitch under the pointer
  click(320, 5)                             -- (the assistant of 9b is closed first)
  type_keys("f3")
  click(124 + 10, 78 + 10)
  run(1)
  type_keys("f")
  local ys, old = {}, env.print
  env.print = function(str, x, y, c, sc) if x == 92 then ys[str] = y end return old(str, x, y, c, sc) end
  frame()
  env.print = old
  local ry
  for _, y in pairs(ys) do ry = math.min(ry or y, y) end
  check(ry ~= nil, "roll: the ruler names the pitches")
  local before2 = last_bank
  click(124 + 7 * 30 + 4 + 10, ry + 6)
  run(2)
  check(last_bank ~= before2, "roll: a click on the plot changes the bank")
  type_keys("f")
end

-- 9c. a bank with samples (version 3, src/audio/player.h): opened, played and
-- written back byte for byte; the menu's Samples... puts the kit's kick into
-- the sound, which then shows its sample in place of the wave
do
  local pcm = {}
  for i = 0, 99 do pcm[#pcm + 1] = string.pack("<i2", math.floor(16000 * math.sin(i / 8))) end
  local v3 = "BMAU" .. string.char(3) .. demo:sub(6, 9) .. string.char(2) .. demo:sub(11) ..
             string.pack("<c8I4I4BBBbBxxxI4I4", "TONE", 100, 22050, 1, 1, 60, 0, 1, 0, 100) .. table.concat(pcm) ..
             string.pack("<c8I4I4BBBbBxxxI4I4", "ST8", 3, 8000, 0, 2, 72, -5, 0, 0, 0) .. "\1\2\3\4\5\6"
  click(320, 5)                             -- above the menu 9b left open: closed (else a tab)
  files["/bm/sounds/SMP.BME"] = { title = "SMP", bank = v3 }
  arg_ = { path = "/bm/sounds/SMP.BME" }
  env._init()
  arg_ = nil
  run(3)
  check(last_bank == v3, "a bank with samples comes back byte for byte (version 3)")
  b = parse(last_bank)
  check(#b.samples == 2 and b.samples[1] == "TONE" and b.samples[2] == "ST8", "its two samples")
  menu_item(8)                              -- on the SONG page: Samples... (after Export)
  type_keys("\n")
  type_keys("down", "down", "\n")           -- TONE, ST8, then the kit's BD
  b = parse(last_bank)
  check(b.waves[1] == 10 and b.tones[1][7] == 128, "Samples...: the kit's kick into sound 00 (SAMPLE, MOD1 128)")
  check(last_bank:sub(-6) == "\1\2\3\4\5\6" and b.samples[2] == "ST8", "the samples stay as they were")
  type_keys("f1")
  run(3)
  type_keys("^z")
  b = parse(last_bank)
  check(b.waves[1] ~= 10, "undo takes the sample back")
end

-- 9d. importing a SAMPLE sound from another bank brings its sample along: copied
-- to the end of this bank's samples, the sound's MOD1 renumbered; a sample with
-- the same bytes is not copied twice
do
  local st8 = string.pack("<c8I4I4BBBbBxxxI4I4", "ST8", 3, 8000, 0, 2, 72, -5, 0, 0, 0) .. "\1\2\3\4\5\6"
  local news = string.pack("<c8I4I4BBBbBxxxI4I4", "NEWS", 4, 22050, 1, 1, 60, 0, 0, 0, 0) ..
               string.pack("<i2i2i2i2", 1000, -1000, 2000, -2000)
  local src = "BMAU" .. string.char(3) .. demo:sub(6, 9) .. string.char(2) .. demo:sub(11)
  -- sounds 0 and 1 (48 bytes each from byte 17): wave 10, MOD1 (tone[7] = byte 24 + 6) 0 and 1
  local function sample_sound(d, k, mod1)
    local at = 17 + 48 * k                          -- 1-based start of the sound
    d = d:sub(1, at + 8 - 1) .. string.char(10) .. d:sub(at + 9)
    local m = at + 24 + 6
    return d:sub(1, m - 1) .. string.char(mod1) .. d:sub(m + 1)
  end
  src = sample_sound(sample_sound(src, 0, 0), 1, 1)
  src = src:sub(1, 9) .. string.char(2) .. src:sub(11)
  -- the samples sit after the songs: demo has none, so they end the file
  files["/bm/sounds/ZSRC.BME"] = { title = "ZSRC", bank = src .. news .. st8 }
  -- where the picker puts it: games, projects, packs, each sorted by name
  local order = {}
  for _, dir in ipairs({ "/bm", "", "/bme", "/bm/sounds" }) do
    local l = {}
    for p in pairs(files) do
      local d, n = p:match("^(.*)/([^/]+)$")
      if d == dir and (n:lower():match("%.bme?$")) then l[#l + 1] = { n = n, p = p } end
    end
    table.sort(l, function(a, c) return a.n:lower() < c.n:lower() end)
    for _, e in ipairs(l) do order[#order + 1] = e.p end
  end
  local at
  for i, p in ipairs(order) do if p == "/bm/sounds/ZSRC.BME" then at = i end end
  local before = parse(last_bank)
  check(#before.samples == 2, "the project holds two samples before the import")
  local function import_sound(n)
    menu_item(6)
    type_keys("\n")                          -- Import from...
    for _ = 1, at - 1 do type_keys("down") end
    type_keys("\n")                          -- ZSRC.BME
    type_keys("\n")                          -- a sound
    for _ = 1, n do type_keys("down") end
    type_keys("\n")
  end
  local function sample_sounds(b2)
    local out = {}
    for i, w in ipairs(b2.waves) do if w == 10 then out[#out + 1] = b2.tones[i][7] end end
    return out
  end
  local old = #sample_sounds(before)
  import_sound(0)
  b = parse(last_bank)
  check(#b.samples == 3 and b.samples[3] == "NEWS", "the sound's sample went to the end of the bank's samples")
  check(last_bank:sub(-#news) == news and last_bank:find(st8, 1, true), "the bank's samples and the new one are whole")
  local mods = sample_sounds(b)
  local has2 = false
  for _, m in ipairs(mods) do if m == 2 then has2 = true end end
  check(#mods == old + 1 and has2, "its MOD1 points to the new sample (2)")
  local len = #last_bank
  import_sound(1)
  b = parse(last_bank)
  check(#b.samples == 3, "a sample with the same bytes is not copied twice")
  check(#last_bank - len == 48, "only the sound itself was added")
  local found1 = false
  for _, m in ipairs(sample_sounds(b)) do if m == 1 then found1 = true end end
  check(found1, "the second sound points to the sample the bank already had (1)")
  type_keys("^z")
end

-- 9e. WAV files from /docs: the formats, the sampler chunk, the rate, mono, limits
do
  check(B ~= nil, "the cartridge hands its bank functions to the test hook")
  local pk = string.pack
  local function chunk(id, body) return id .. pk("<I4", #body) .. body .. (#body & 1 == 1 and "\0" or "") end
  local function wav(tag, bits, ch, rate, frames_bytes, extra, ext)
    local fmt = pk("<I2I2I4I4I2I2", ext and 0xFFFE or tag, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits)
    if ext then fmt = fmt .. pk("<I2I2I4I2", 22, bits, 0, tag) .. string.rep("\0", 14) end
    local body = "WAVE" .. chunk("fmt ", fmt) .. (extra or "") .. chunk("data", frames_bytes)
    return "RIFF" .. pk("<I4", #body) .. body
  end
  local function head(raw) return string.unpack("<c8I4I4BBBbBxxxI4I4", raw) end
  local function s16s(f, n) local t = {} for i = 0, n - 1 do t[#t + 1] = pk("<i2", f(i)) end return table.concat(t) end

  -- a mono 16-bit file with the sampler chunk: root 69 plus a quarter semitone, a ping-pong loop
  local sine = s16s(function(i) return math.floor(math.sin(i / 100 * 2 * math.pi) * 20000) end, 100)
  local smpl = chunk("smpl", pk("<I4I4I4I4I4I4I4I4I4", 0, 0, 45351, 69, 1 << 30, 0, 0, 1, 0) ..
                             pk("<I4I4I4I4I4I4", 0, 1, 10, 49, 0, 0))
  local w = assert(B.wav_parse(wav(1, 16, 1, 22050, sine, smpl)))
  check(w.frames == 100 and w.rate == 22050 and w.channels == 1 and w.kind == "s16", "wav: the format is read")
  check(w.root == 69 and w.fine == 25 and w.loop == 2 and w.ls == 10 and w.le == 50,
        "wav: the sampler chunk: root, fine tune, ping-pong loop")
  local raw, info = B.wav_sample(w, "loop.wav", {})
  local name, frames, rate, fmt, ch, root, fine, loop, ls, le = head(raw)
  check(#raw == 32 + #sine and raw:sub(33) == sine, "wav: a 16-bit file that needs no change is copied as it is")
  check(name:match("^[^%z]*") == "LOOP.WAV" and frames == 100 and rate == 22050 and fmt == 1 and ch == 1 and
        root == 69 and fine == 25 and loop == 2 and ls == 10 and le == 50, "wav: the 32-byte header")

  -- 8-bit stereo stays 8-bit; mono mixes the two channels (16-bit)
  local u8 = string.char(200, 100, 128, 128, 0, 255)
  local w8 = assert(B.wav_parse(wav(1, 8, 2, 8000, u8)))
  raw = B.wav_sample(w8, "S8", {})
  local _, f8, r8, fm8, c8 = head(raw)
  check(f8 == 3 and r8 == 8000 and fm8 == 0 and c8 == 2 and raw:sub(33) == u8, "wav: 8-bit stereo stays as it is")
  raw = B.wav_sample(w8, "S8", { mono = true })
  local _, fm, rm, fmm, cm = head(raw)
  local m1, m2, m3 = string.unpack("<i2i2i2", raw, 33)
  check(fm == 3 and fmm == 1 and cm == 1 and m1 == 5632, "wav: mono is the mean of the channels")
  check(m2 == 0 and m3 == (-128 * 256 + 127 * 256) // 2, "wav: 8-bit values scale to 16 bits")

  -- 24-bit stereo at 96000 Hz goes to 48000 Hz: a steady value stays what it is
  local s24 = {}
  for i = 1, 200 do s24[i] = pk("<i3i3", 1000 * 256, -2000 * 256) end
  local w24 = assert(B.wav_parse(wav(1, 24, 2, 96000, table.concat(s24))))
  raw, info = B.wav_sample(w24, "DEEP", {})
  local _, f24, r24, fm24, c24 = head(raw)
  check(f24 == 100 and r24 == 48000 and fm24 == 1 and c24 == 2 and #raw == 32 + 100 * 4,
        "wav: 96000 Hz stereo 24-bit becomes 48000 Hz 16-bit")
  local ok = true
  for i = 0, 99 do
    local l, r = string.unpack("<i2i2", raw, 33 + i * 4)
    if l ~= 1000 or r ~= -2000 then ok = false end
  end
  check(ok, "wav: a steady value stays steady through the mean")

  -- 32-bit float at 44100 Hz to 22050 Hz: the mean of two frames, exactly; clipping and NaN
  local fl = {}
  for i = 0, 440 do fl[#fl + 1] = pk("<f", 0.5 * math.sin(i / 441 * 2 * math.pi * 10)) end
  local wf = assert(B.wav_parse(wav(3, 32, 1, 44100, table.concat(fl))))
  raw = B.wav_sample(wf, "F32", { rate = 22050 })
  local _, ff, rf = head(raw)
  check(ff == 220 and rf == 22050 and #raw == 32 + 220 * 2, "wav: 44100 Hz float, asked for 22050 Hz: half the frames")
  local worst = 0
  for i = 0, 219 do
    local a = string.unpack("<f", fl[2 * i + 1]) * 32767
    local b2 = string.unpack("<f", fl[2 * i + 2]) * 32767
    local got = string.unpack("<i2", raw, 33 + i * 2)
    worst = math.max(worst, math.abs(got - (a + b2) / 2))
  end
  check(worst <= 1, "wav: the mean of two frames, within a step of 16 bits (" .. worst .. ")")
  local wc = assert(B.wav_parse(wav(3, 32, 1, 8000, pk("<ffff", 2.0, -3.0, 0 / 0, 0.25))))
  raw = B.wav_sample(wc, "CLIP", {})
  local c1, c2, c3, c4 = string.unpack("<i2i2i2i2", raw, 33)
  check(c1 == 32767 and c2 == -32767 and c3 == 0 and c4 == 8192, "wav: floats clip at full scale and NaN is silence")
  local w64 = assert(B.wav_parse(wav(3, 64, 1, 8000, pk("<dd", 0.5, -0.5))))
  raw = B.wav_sample(w64, "D64", {})
  check(string.unpack("<i2", raw, 33) == 16384 and string.unpack("<i2", raw, 35) == -16383, "wav: 64-bit floats")

  -- a rate that is not a whole ratio keeps a steady value; over 48000 Hz comes down
  local dc = s16s(function() return 1234 end, 480)
  raw = B.wav_sample(assert(B.wav_parse(wav(1, 16, 1, 48000, dc))), "DC", { rate = 11025 })
  local _, fd, rd_ = head(raw)
  ok = fd == math.floor(480 / (48000 / 11025)) and rd_ == 11025
  for i = 0, fd - 1 do if string.unpack("<i2", raw, 33 + i * 2) ~= 1234 then ok = false end end
  check(ok, "wav: a steady value stays steady at a rate that is not a whole ratio")
  local big = s16s(function(i) return i % 100 end, 960)
  raw = B.wav_sample(assert(B.wav_parse(wav(1, 16, 1, 96000, big))), "BIG", {})
  local _, fb, rb = head(raw)
  check(rb == 48000 and fb == 480, "wav: more than 48000 Hz is brought to 48000 Hz")
  -- the loop follows the frames when the rate changes
  local lp = chunk("smpl", pk("<I4I4I4I4I4I4I4I4I4", 0, 0, 0, 60, 0, 0, 0, 1, 0) ..
                           pk("<I4I4I4I4I4I4", 0, 0, 100, 299, 0, 0))
  raw = B.wav_sample(assert(B.wav_parse(wav(1, 16, 1, 96000, s16s(function() return 5 end, 400), lp))), "LP", {})
  local _, _, _, _, _, _, _, lm, lstart, lend = head(raw)
  check(lm == 1 and lstart == 50 and lend == 150, "wav: the loop points move with the rate")
  -- extensible, an odd chunk before the data, a data size that is too big
  local we = assert(B.wav_parse(wav(1, 16, 1, 22050, sine, chunk("LIST", "abc"), true)))
  check(we.kind == "s16" and we.frames == 100, "wav: WAVE_FORMAT_EXTENSIBLE and an odd chunk before the data")
  local wcut = assert(B.wav_parse(wav(1, 16, 1, 22050, sine):sub(1, -41)))
  check(wcut.frames == 80, "wav: a data chunk longer than the file is read up to the end")
  -- what is refused
  check(not B.wav_parse("RIFF....WAVX"), "wav: not a WAV")
  check(select(2, B.wav_parse(wav(1, 12, 1, 22050, sine))):find("12 bits"), "wav: 12 bits refused")
  check(select(2, B.wav_parse(wav(1, 16, 3, 22050, sine))):find("3 channels"), "wav: 3 channels refused")
  check(select(2, B.wav_parse(wav(1, 16, 1, 500, sine))):find("too low"), "wav: 500 Hz refused")
  check(select(2, B.wav_parse(wav(1, 16, 1, 22050, ""))):find("no sound"), "wav: no frames refused")
  check(select(2, B.wav_parse("RIFF" .. pk("<I4", 4) .. "WAVE")):find("without"), "wav: no format refused")

  -- into the bank: the same bytes are reused, 64 samples at most, and the memory
  local bk = B.blank()
  raw, info = B.wav_sample(w, "ONE", {})
  local i1, again = B.add_sample(bk, raw, info)
  local i2, again2 = B.add_sample(bk, raw, info)
  check(i1 == 0 and not again and i2 == 0 and again2 and #bk.samples == 1, "wav: the same sample is not added twice")
  check(bk.sraw == raw and bk.samples[1].o == 1 and bk.samples[1].n == #raw, "wav: the bank's raw samples hold it")
  local raw2, info2 = B.wav_sample(w8, "TWO", {})
  check(B.add_sample(bk, raw2, info2) == 1 and bk.sraw == raw .. raw2 and bk.samples[2].o == #raw + 1,
        "wav: the next one goes to the end")
  local packed = B.parse(B.pack(bk))
  check(packed and #packed.samples == 2 and packed.samples[1].root == 69 and packed.samples[1].loop == 2 and
        packed.samples[2].format == 0, "wav: the bank with the new samples packs and reads back")
  local full = B.blank()
  for k = 1, 64 do full.samples[k] = { frames = 1, channels = 1, n = 0, o = 1 } end
  check(select(2, B.add_sample(full, raw, info)) == "no free sample slots", "wav: a 65th sample is refused")
  local heavy = B.blank()
  heavy.samples[1] = { frames = B.PCM_MAX - 100, channels = 1, n = 0, o = 1 }
  check(select(2, B.add_sample(heavy, raw, { frames = 200, channels = 1 })) == "the samples would not fit in memory",
        "wav: past the bank's memory is refused")
  check(#heavy.samples == 1 and heavy.sraw == "", "wav: a refused sample leaves the bank as it was")
end

-- 9f. MIDI files from /docs: tracks, tempo, the drums, the patterns and the song
do
  local pk = string.pack
  local function vlq(v)
    local t = { v & 127 }
    v = v >> 7
    while v > 0 do table.insert(t, 1, v & 127 | 128); v = v >> 7 end
    return string.char(table.unpack(t))
  end
  -- events { tick, bytes } -> a track chunk
  local function track(ev, no_end)
    for i, e in ipairs(ev) do e[3] = i end                       -- a stable order for the events of one tick
    table.sort(ev, function(a, b2) if a[1] ~= b2[1] then return a[1] < b2[1] end return a[3] < b2[3] end)
    local out, at = {}, 0
    for _, e in ipairs(ev) do out[#out + 1] = vlq(e[1] - at) .. e[2]; at = e[1] end
    if not no_end then out[#out + 1] = "\0\255\47\0" end
    local body = table.concat(out)
    return "MTrk" .. pk(">I4", #body) .. body
  end
  local function smf(fmt, div, ...)
    local tr = { ... }
    return "MThd" .. pk(">I4I2I2I2", 6, fmt, #tr, div) .. table.concat(tr)
  end
  local function note(ev, tick, dur, key, vel, ch)
    ev[#ev + 1] = { tick, string.char(0x90 | ch, key, vel) }
    ev[#ev + 1] = { tick + dur, string.char(0x80 | ch, key, 0) }
  end
  local function tempo(us) return string.char(255, 0x51, 3, us >> 16, us >> 8 & 255, us & 255) end

  local ev = { { 0, tempo(500000) }, { 0, string.char(255, 0x58, 4, 4, 2, 24, 8) }, { 0, string.char(0xC0, 33) } }
  note(ev, 0, 96, 48, 100, 0)
  note(ev, 96, 48, 52, 64, 0)
  for _, k in ipairs({ 60, 64, 67 }) do note(ev, 192, 96, k, 100, 0) end
  note(ev, 0, 10, 36, 127, 9); note(ev, 0, 10, 42, 90, 9); note(ev, 48, 10, 42, 90, 9); note(ev, 96, 10, 38, 110, 9)
  local tune = smf(0, 96, track(ev))
  local m = assert(B.mid_parse(tune))
  check(#m.notes == 9 and m.division == 96 and m.tempo == 500000 and m.num == 4 and m.den == 2 and not m.cut,
        "midi: the notes, the division, the tempo and the time signature are read")
  local bk = B.blank()
  local res = assert(B.mid_import(bk, m, "tune", 0))
  check(res.song == 1 and res.patterns == 1 and res.needed == 1 and res.sounds == 4 and res.notes == 9 and
        res.dropped == 0 and res.bpm == 120 and res.plen == 64, "midi: the song, the pattern, the sounds, the tempo")
  local sg = bk.songs[res.song + 1]
  check(sg.name == "TUNE" and sg.bpm == 120 and #sg.order == 1, "midi: the song takes the file's name and tempo")
  local p = bk.pats[sg.order[1] + 1]
  check(sg.order[1] == 1 and p.len == 64, "midi: the pattern is 64 steps and not the one the first song plays")
  local function st(t, i) local v = p.tracks[t] and p.tracks[t][i] or 0 return v & 255, v >> 8 & 255, v >> 16 & 255 end
  local n, snd, vol = st(1, 1)
  check(n == 48 and vol == 201, "midi: the first note: C3, velocity 100 = volume 201")
  check(st(1, 5) == 52 and st(1, 7) == 128 and st(1, 9) == 60,
        "midi: a note that ends where another starts has no OFF; a gap gets one")
  check(st(5, 9) == 64 and st(6, 9) == 67, "midi: a chord goes to three tracks")
  check(st(2, 1) == 60 and st(3, 1) == 60 and st(3, 3) == 60 and st(4, 5) == 60,
        "midi: the drums are one-shots on the next free tracks")
  local _, ksnd = st(2, 1)
  local _, hsnd = st(3, 1)
  local _, bsnd = st(1, 1)
  check(bk.sounds[ksnd + 1].name == "BD" and bk.sounds[ksnd + 1].wave == 10 and bk.sounds[ksnd + 1].tone[7] == 128,
        "midi: the kick plays the kit's bd")
  check(bk.sounds[hsnd + 1].name == "HH" and bk.sounds[hsnd + 1].tone[7] == 130, "midi: the hi-hat plays the kit's hh")
  check(bk.sounds[bsnd + 1].name == "CH01" and bk.sounds[bsnd + 1].wave == 2, "midi: program 33 (a bass) is a saw")
  check(B.parse(B.pack(bk)) ~= nil, "midi: the bank with the song packs and reads back")
  -- the same file again: the sounds are reused
  local res2 = assert(B.mid_import(bk, m, "again", 0))
  check(res2.sounds == 0 and res2.song == 2 and res2.first == 2,
        "midi: a second import reuses the sounds and takes the next slots")
  -- tempo, bars, simultaneous notes
  local function with(evs) return assert(B.mid_parse(smf(1, 480, track(evs)))) end
  local e2 = { { 0, tempo(300000) }, { 0, tempo(300000) } }
  note(e2, 0, 480, 60, 100, 1)
  local r3 = assert(B.mid_import(B.blank(), with(e2), "T", 0))
  check(r3.bpm == 200 and r3.tempos == 2, "midi: 300000 us a beat is 200 BPM; two tempo events are told")
  local e3 = { { 0, tempo(2000000) } }
  note(e3, 0, 480, 60, 100, 1)
  local r4 = assert(B.mid_import(B.blank(), with(e3), "T", 0))
  check(r4.bpm == 40 and r4.bpm_exact == 30, "midi: a tempo under 40 BPM is held at 40 and told")
  local e4 = { { 0, string.char(255, 0x58, 4, 3, 2, 24, 8) } }          -- 3/4: 12 steps a bar
  note(e4, 0, 480, 60, 100, 0); note(e4, 130 * 120, 120, 62, 100, 0)       -- step 130
  local b4 = B.blank()
  local r5 = assert(B.mid_import(b4, with(e4), "WALTZ", 0))
  check(r5.plen == 60 and r5.needed == 3 and r5.patterns == 3,
        "midi: 3/4 makes patterns of five bars (60 steps); 131 steps need three")
  local o = b4.songs[r5.song + 1].order
  check(#o == 3 and b4.pats[o[3] + 1].tracks[1][130 - 120 + 1] & 255 == 62,
        "midi: the last note is in the third pattern, step 10")
  local e5 = {}
  for k = 1, 10 do note(e5, 0, 480, 40 + k, 100, 0) end
  local r6 = assert(B.mid_import(B.blank(), with(e5), "CHORD", 0))
  check(r6.dropped == 2 and r6.notes == 8, "midi: ten notes at once: eight tracks, two dropped and told")
  -- running status, a note-on with velocity 0 as the note-off, a file cut short
  local run_ = string.char(0x90, 60, 100) .. "\96" .. string.char(60, 0) .. "\0" .. string.char(62, 100)
  local body = "\0" .. run_ .. "\96" .. string.char(62, 0) .. "\0\255\47\0"
  local mr = assert(B.mid_parse("MThd" .. pk(">I4I2I2I2", 6, 0, 1, 96) .. "MTrk" .. pk(">I4", #body) .. body))
  check(#mr.notes == 2 and mr.notes[1].e == 96 and mr.notes[2].t == 96, "midi: running status and note-on with velocity 0")
  local mh = assert(B.mid_parse(tune:sub(1, #tune - 30)))
  check(mh.cut and #mh.notes > 0, "midi: a file cut short keeps the notes it has and says so")
  -- slots: patterns are cut to the free ones; no free song changes nothing
  local few = B.blank()
  for k = 4, B.NPAT do few.pats[k] = { len = 16, tracks = { [1] = { 0x3C } } } end   -- 1 and 2 free (0 is played)
  local r7 = assert(B.mid_import(few, with(e4), "WALTZ", 0))
  check(r7.patterns == 2 and r7.needed == 3, "midi: with two free pattern slots a song of three is cut to two")
  local nosong = B.blank()
  for k = 1, B.NSONG do nosong.songs[k].name = "S" .. k end
  local before = B.pack(nosong)
  check(select(2, B.mid_import(nosong, m, "X", 0)) == "no free song slots" and B.pack(nosong) == before,
        "midi: no free song slot: refused, the bank is as it was")
  -- what is refused
  check(select(2, B.mid_parse("hello")):find("not a MIDI"), "midi: not a MIDI file")
  check(select(2, B.mid_parse("MThd" .. pk(">I4I2I2I2", 6, 0, 1, 0xE728))):find("frames"),
        "midi: a time code division is refused")
  check(select(2, B.mid_parse(smf(0, 96, track({ { 0, tempo(500000) } })))):find("no notes"),
        "midi: a file without notes is refused")

  -- the whole way, with the keys: Ctrl+W, the file, and the song is in the bank
  docs["song.mid"] = tune
  local songs_before = #parse(last_bank).songs
  type_keys("^w", "\n")                    -- song.mid is the first of the list
  b = parse(last_bank)
  check(#b.songs == songs_before + 1 and b.songs[#b.songs].name == "SONG" and b.songs[#b.songs].bpm == 120,
        "midi: Ctrl+W imports song.mid as a song (" .. #b.songs .. " songs)")
  check(logs[#logs]:find("imported the MIDI song.mid", 1, true), "midi: the import is in the log")
  -- and a WAV: mono at 22050 Hz out of a stereo 96000 Hz file
  local stc = {}
  for i = 0, 959 do
    local v = math.floor(math.sin(i / 96 * 2 * math.pi) * 10000)
    stc[#stc + 1] = pk("<i2i2", v, v)
  end
  local fmtc = pk("<I2I2I4I4I2I2", 1, 2, 96000, 96000 * 4, 4, 16)
  local body2 = "WAVE" .. "fmt " .. pk("<I4", 16) .. fmtc .. "data" .. pk("<I4", 960 * 4) .. table.concat(stc)
  docs["tone.wav"] = "RIFF" .. pk("<I4", #body2) .. body2
  local nsm = #parse(last_bank).samples
  type_keys("^w", "down", "\n")            -- tone.wav: the choices as it is, mono, mono 22050 Hz, mono 11025 Hz
  type_keys("down", "down", "\n")
  b = parse(last_bank)
  local tail = last_bank:sub(-(32 + 220 * 2))
  local nm, fr, rt, fm_, ch_ = string.unpack("<c8I4I4BB", tail)
  check(#b.samples == nsm + 1 and nm:match("^[^%z]*") == "TONE" and fr == 220 and rt == 22050 and fm_ == 1 and ch_ == 1,
        "wav: Ctrl+W, tone.wav, mono at 22050 Hz: a mono sample of 220 frames is the bank's last")
  local at_
  for i, wv in ipairs(b.waves) do if wv == 10 and b.tones[i][7] == nsm then at_ = i end end
  check(at_ ~= nil, "wav: the current sound plays the new sample (SAMPLE, MOD1 " .. nsm .. ")")
  check(logs[#logs]:find("imported the WAV tone.wav", 1, true), "wav: the import is in the log")
  docs["song.mid"], docs["tone.wav"] = nil, nil
  type_keys("^w")                          -- nothing to import now: a message, no overlay
end

-- 10. random input: nothing stops the editor
math.randomseed(7)
local K = { "up", "down", "left", "right", "\n", "\b", " ", "f1", "f2", "f3", "f4", "\t", "z", "q", "x", "-", "=",
            "[", "]", ";", "'", "k", "l", "o", "p", "_", "+", ",", ".", "pgup", "pgdn", "esc", "^z", "^c", "^v",
            "i", "^n", "^S", "^w", "a", "f", "a", "f" }
for i = 1, 6000 do
  for b2 = 0, 9 do pad[b2] = math.random() < (b2 == 9 and 0.03 or 0.12) end
  if math.random() < 0.2 then keys[#keys + 1] = K[math.random(#K)] end
  if math.random() < 0.1 then                -- and the mouse, anywhere
    local was = MS.b
    MS.x, MS.y = math.random(0, 639), math.random(0, 359)
    MS.b = math.random() < 0.3 and ({ 1, 2, 4 })[math.random(3)] or 0
    MS.w = math.random() < 0.1 and math.random(-2, 2) or 0
    MS.p = MS.b & ~was
  else
    MS.p, MS.w = 0, 0
  end
  f12 = math.random() < 0.01
  frame()
  if quitted then quitted = false; env._init() end
end
for b2 = 0, 9 do pad[b2] = false end
run(5)
check(last_bank and parse(last_bank), "after random input the bank is still a valid bank")

-- F12 held: the kernel shows the keys keyhelp() gave it
local function in_help(keys)
  for _, e in ipairs(keyhelp_list or {}) do if type(e) == "table" and e[1] == keys then return true end end
  return false
end
check(keyhelp_title == "Sound" and in_help("i") and in_help("SELECT LEFTRIGHT"), "keyhelp: the keys and the pad's")

-- the fixed chip "F12 keys" at the right of the bottom bar, on every page (a keyboard in use):
-- inside the screen, the hints and the menu chip never reach it, a click opens the keys
do
  run(300)                              -- (a message of the last keys hides the hints while it shows)
  local function chip_frame() repeat cap = {}; frame() until last_input == "keyboard" end
  for page = 1, 4 do
    chip_frame()
    local row, chip, label, menu = {}, nil, nil, nil
    for _, t in ipairs(cap) do
      if t[3] == 339 then row[#row + 1] = t end
    end
    for _, t in ipairs(row) do if t[1] == "[f12]" then chip = t end end
    for _, t in ipairs(row) do
      if chip and t[1] == "keys" and t[2] > chip[2] then label = t end
      if t[1] == "menu" then menu = t end
    end
    local what = "page " .. page .. ": the fixed chip F12 keys"
    check(chip and label and chip[2] % 8 == 0 and label[2] + 32 <= 640 - 104, what .. " is drawn, left of the menu chip")
    local bad
    for _, t in ipairs(row) do
      if chip and t ~= chip and t ~= label and t[2] < chip[2] and t[2] + t[4] > chip[2] - 8 then bad = t[1] end
    end
    check(chip and not bad, what .. ": the hints never reach it" .. (bad and (" (" .. bad .. ")") or ""))
    if chip then
      local n = shown_keys
      click(chip[2] + 4, chip[3] + 8)
      check(shown_keys == n + 1, what .. ": a click opens the keys")
    end
    hold_then(SELECT, R)                  -- the next page
  end
  cap = nil
end

-- no globals made by mistake
local stray = {}
for k in pairs(env) do
  if not api[k] and k ~= "_init" and k ~= "_update" and k ~= "_draw" and k ~= "_exit" then stray[#stray + 1] = k end
end
check(#stray == 0, "no stray globals: " .. table.concat(stray, " "))

print(string.format("sound editor: %d/%d checks passed, %d frames, %d banks sent", checks - fails, checks, frames, banks))
os.exit(fails == 0 and 0 or 1)
