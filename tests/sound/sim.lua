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
env.print = function(s, x, y, c, scale)
  assert(type(x) == "number" and type(y) == "number", "print: x, y must be numbers")
  calls = calls + 1
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

-- the SD card
local files = {
  ["/carts/pong.bm"] = { title = "Pong", bank = false },
  ["/carts/astrowing.bm"] = { title = "Astro Wing", bank = false },
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
env.cart_put_audio = function(path, data, title, lua)
  assert(type(data) == "string" and data:sub(1, 4) == "BMAU", "cart_put_audio: a bank")
  if not files[path] then
    assert(type(lua) == "string" and #lua > 0, "a new pack needs its Lua")
    files[path] = { title = title }
  end
  files[path].bank = data
  puts[#puts + 1] = path
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

local chunk = assert(load(readfile(SRC), "=main.lua", "t", env))
chunk()

---------------------------------------------------------------- a bank reader (the format of player.h)

local function parse(d)
  local b = { sounds = {}, sfx = {}, pats = {}, songs = {} }
  local ns, nx, np, ng = d:byte(6, 9)
  local pos = 17
  for i = 1, ns do b.sounds[i] = d:sub(pos, pos + 7):match("^[^%z]*"); pos = pos + 24 end
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
    local name, bpm, swing, n, loop
    name, bpm, swing, n, loop, pos = string.unpack("<c8BBBBxxxx", d, pos)
    b.songs[i] = { name = name:match("^[^%z]*"), bpm = bpm, order = { d:byte(pos, pos + n - 1) } }
    pos = pos + n
  end
  check(pos == #d + 1, "the bank has no bytes left over")
  return b
end

---------------------------------------------------------------- running it

local frames = 0
local function frame()
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
type_keys("down", "down", "down", "down")   -- track 6 (voice 5): empty in pattern 0
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
type_keys("down")
hold_then(A, R)

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
check(puts[#puts] == "/carts/pong.bm", "Save writes into the game opened")
b = parse(files["/carts/pong.bm"].bank)
check(#b.pats >= 1 and b.pats[1].tracks[0], "the game got the pattern")
check(logs[#logs]:find("saved /carts/pong.bm", 1, true), "the save is in the log")

-- 8. import a song (with its patterns and sounds) from the pack
menu_item(6)
type_keys("\n")                           -- Import from...
type_keys("down", "down", "\n")           -- OTHER.BM
type_keys("down", "down", "down", "\n")   -- a song
type_keys("\n")                           -- song 0 (DEMO)
b = parse(last_bank)
check(#b.songs >= 2 and #b.pats >= 10 and #b.sounds >= 6, "the song came with its patterns and sounds")

-- 9. save as a new pack
menu_item(4)
type_keys("\n")                           -- Save as a new sound pack...
type_keys("\b", "\b", "\b", "\b", "\b", "\b", "\b", "\b", "M", "I", "N", "E", "\n")
check(files["/bm/sounds/MINE.BM"] and files["/bm/sounds/MINE.BM"].bank, "a new sound pack is written")

-- 10. random input: nothing stops the editor
math.randomseed(7)
local K = { "up", "down", "left", "right", "\n", "\b", " ", "f1", "f2", "f3", "f4", "\t", "z", "q", "x", "-", "=",
            "[", "]", ";", "'", "k", "l", "o", "p", "_", "+", ",", ".", "pgup", "pgdn", "esc", "^z", "^c", "^v" }
for i = 1, 6000 do
  for b2 = 0, 9 do pad[b2] = math.random() < (b2 == 9 and 0.03 or 0.12) end
  if math.random() < 0.2 then keys[#keys + 1] = K[math.random(#K)] end
  f12 = math.random() < 0.01
  frame()
  if quitted then quitted = false; env._init() end
end
for b2 = 0, 9 do pad[b2] = false end
run(5)
check(last_bank and parse(last_bank), "after random input the bank is still a valid bank")

print(string.format("sound editor: %d/%d checks passed, %d frames, %d banks sent", checks - fails, checks, frames, banks))
os.exit(fails == 0 and 0 or 1)
