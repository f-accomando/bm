-- Host tests of the assistant's panel (src/ai/assist.lua), with the real
-- `ai` table (tests/ai/luaai.c) and the console's drawing and input
-- functions replaced by recorders: what the panel writes, which keys it gets.
--   luaai build/assist.bin tests/ai/panel_test.lua

local say = print
local fails, checks = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    say("FAIL " .. what)
  end
end

-- the console, recorded
SCREEN_W, SCREEN_H = 640, 360
local screen = {}
function print(s, x, y, c) screen[#screen + 1] = tostring(s); return x + #tostring(s) * 8 end
function rectfill() end
function rect() end
function line() end
local FONT = { ["6x12"] = { 6, 12 }, ["8x14"] = { 8, 14 }, ["8x16"] = { 8, 16 } }
local cur_font = FONT["8x16"]
function font(n)
  if n then cur_font = assert(FONT[n], n) end
  return cur_font[1], cur_font[2]
end
local keys, held, pressed = {}, {}, {}
-- the keys as chips: written as "[name]"; prompt(name) alone measures
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
function prompt(n, x, y)
  assert(type(n) == "string", "prompt: a name")
  if type(x) ~= "number" then return chip_w(n), 16 end
  screen[#screen + 1] = "[" .. n .. "]"
  return x + chip_w(n)
end
local last_input
function lastinput() return last_input end
function keyp() return table.remove(keys, 1) end
function btn(i) return held[i] == true end
function btnp(i) return pressed[i] == true end

-- the completion of the question: require "predict" and its dictionaries
local build = arg and arg[1] or "build"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local assist = dofile("src/ai/assist.lua")

local function frame()                  -- one frame: update and draw
  local open = assist.update()
  screen = {}
  assist.draw()
  pressed = {}
  return open
end

local function on_screen(text)
  for _, l in ipairs(screen) do
    if l:find(text, 1, true) then return true end
  end
  return false
end

local function type_(s)
  for c in s:gmatch(".") do keys[#keys + 1] = c end
  frame()
end

-- a question in code mode, answered while typing; Enter inserts the code
local inserted
assist.open{ mode = "code", on_insert = function(c) inserted = c end }
check(assist.is_open(), "open")
frame()
check(on_screen("type a question"), "empty: invites a question")
type_("come faccio a saltare")
check(on_screen("Saltare con la gravit"), "jump: the answer on screen")
check(on_screen("0.35") and on_screen("on_ground"), "jump: the code on screen")
keys = { "\n" }
frame()
check(not assist.is_open(), "Enter closes")
check(inserted and inserted:find("vy, on_ground = -7, false", 1, true), "Enter inserts the code")

-- the guide mode (the SDK's project page): the guides to 2D and 3D games
-- come first when browsing; a question finds its guide, Enter its code
inserted = nil
assist.open{ mode = "guide", on_insert = function(c) inserted = c end }
frame()
check(on_screen("Guida: cominciare un gioco con l'SDK"), "guide: the guides first when browsing")
type_("come faccio un platform")
check(on_screen("Guida: un platform 2D passo passo"), "guide: the platformer's guide")
keys = { "\n" }
frame()
check(inserted and inserted:find("lib.step(hero)", 1, true), "guide: Enter inserts its code")
assist.open{ mode = "guide" }
frame()
type_("how do i start a 3d game")
-- R18: an English question, the answers in English (EN on the title bar);
-- Ctrl+E back to Italian, which stays (the questions no longer choose)
check(on_screen("Guide: your first 3D game"), "guide: the first 3D game, in English")
check(on_screen("  EN"), "EN on the title bar")
keys = { "^e" }
frame()
check(on_screen("Guida: il primo gioco 3D") and on_screen("  IT"), "Ctrl+E: in Italian")
keys = { "esc" }
frame()

-- the word under the cursor comes first
assist.open{ mode = "code", ctx = "circfill" }
frame()
check(on_screen("circfill(x, y, r, c)"), "ctx: its API first")
keys = { "esc" }
frame()
check(not assist.is_open(), "Esc closes")

-- a sprite: Enter hands it over; right is the next variant
local sprite
assist.open{ mode = "sprite", size = 16, on_sprite = function(s) sprite = s end }
type_("moneta")
check(on_screen("Moneta"), "sprite: coin")
keys = { "right" }
frame()
check(on_screen("#2"), "right: variant 2")
keys = { "\n" }
frame()
check(sprite and sprite.w == 16 and #sprite.px == 256 and sprite.gen == "coin" and sprite.seed == 2,
      "sprite handed over: 16x16 coin, variant 2")
-- the colours and the size of the words win
assist.open{ mode = "sprite", size = 16, on_sprite = function(s) sprite = s end }
type_("slime rosso 32x32")
keys = { "\n" }
frame()
check(sprite and sprite.w == 32 and sprite.gen == "slime", "words: 32x32 slime")

-- a 3D model (bm Studio, bm Animator): the recipe, its variant, Enter
-- hands over the faces, the bones and the animations; the words' colour
local model
assist.open{ mode = "mesh", on_mesh = function(m) model = m end }
type_("un cane")
check(on_screen("Cane"), "mesh: dog")
check(on_screen("bones:") and on_screen("walk"), "mesh: faces, bones and animations on screen")
keys = { "right" }
frame()
check(on_screen("#2"), "right: variant 2")
keys = { "\n" }
frame()
check(model and model.gen == "dog" and model.seed == 2 and #model.faces > 50 and #model.bones == 7 and
      #model.clips == 2 and model.clips[2].name == "walk" and #model.clips[2].keys[1].pose == 7,
      "model handed over: dog, variant 2, 7 bones, walk")
check(model and model.faces[1].p[1][2] >= 0 and #model.faces[1].b == #model.faces[1].p, "faces with bones")
assist.open{ mode = "mesh", on_mesh = function(m) model = m end }
type_("casa blu senza scheletro")
keys = { "\n" }
frame()
check(model and model.gen == "house" and not model.bones, "words: a house, no skeleton")
local blue = false
for _, f in ipairs(model.faces) do if f.c == 0x3A62D8 then blue = true end end
check(blue, "words: blue walls")
assist.open{ mode = "mesh", on_mesh = function(m) model = m end }
type_("mech")
keys = { "\n" }
frame()
check(model and model.gen == "mech" and #model.bones == 9 and #model.clips == 3, "the mech: 9 bones, 3 animations")
-- Tab goes round the modes: code, sprite, mesh, any
assist.open{ mode = "sprite" }
keys = { "\t" }
frame()
check(on_screen("mesh"), "Tab from sprite: mesh")
assist.close()

-- an error message: the line, the mistyped name, what it means
assist.open{ error = "main.lua:7: attempt to call a nil value (global 'sprr')\nstack traceback: ..." }
frame()
check(on_screen("line 7: did you mean spr?"), "error: line and typo")
check(on_screen("attempt to call a nil value"), "error: the entry")
keys = { "esc" }
frame()

-- the pad: browse with no question, down, B closes; X changes mode
local closed = false
assist.open{ mode = "code", on_close = function() closed = true end }
frame()
local first = screen[6]
pressed = { [3] = true }
frame()
check(assist.is_open(), "pad down: still open")
pressed = { [6] = true }
frame()
check(on_screen("sprite"), "X: next mode (sprite)")
pressed = { [5] = true }
frame()
check(not assist.is_open() and closed, "B closes, on_close called")

-- held down: repeats after a while
assist.open{ mode = "code" }
held[3] = true
for i = 1, 40 do frame() end
held[3] = nil
check(assist.is_open(), "held down: still open")
assist.close()

-- something it does not know
assist.open{ mode = "any" }
type_("che tempo fa domani a roma")
check(on_screen("not sure") or on_screen("no answer"), "off topic: not sure")
assist.close()

-- a tool that takes no code: Enter says so instead of closing
assist.open{ mode = "code" }
type_("come salto")
keys = { "\n" }
frame()
check(assist.is_open() and on_screen("takes no code"), "no on_insert: says so")
assist.close()

-- 320x180: everything fits
SCREEN_W, SCREEN_H = 320, 180
assist.open{ mode = "code" }
type_("collisione tra rettangoli")
check(on_screen("Collisione tra due"), "320x180: answer")
assist.close()

-- in a tool with the small font: the panel uses it, and gives it back
SCREEN_W, SCREEN_H = 640, 360
font("6x12")
assist.open{ mode = "code" }
type_("come salto")
check(on_screen("Saltare con la gravit"), "6x12: answer")
local fw = font()
check(fw == 6, "6x12: the tool's font is back after drawing")
assist.close()
font("8x16")

-- the keys at the bottom: the keyboard's, or the pad's buttons after a pad
assist.open{ mode = "code" }
type_("come salto")
check(on_screen("[enter]") and on_screen("[esc]") and on_screen("insert"), "keys: the keyboard's")
last_input = "ds4"
frame()
check(on_screen("[A]") and on_screen("[B]") and not on_screen("[enter]"), "keys: the pad's after a pad")
last_input = nil
assist.close()

-- the completion: while a word is typed its rest in grey-blue, Tab writes
-- it (green), and Tab without a suggestion is still the next mode
local predict = require "predict"
local colours = {}
local print0 = print
function print(s, x, y, c) colours[tostring(s)] = c; return print0(s, x, y, c) end
inserted = nil
assist.open{ mode = "code", on_insert = function(c) inserted = c end }
type_("co")
check(colours["me"] == predict.C_GHOST, "co: the rest of come, grey-blue")
check(on_screen("word"), "the keys: Tab writes the word")
keys = { "\t" }
frame()
check(on_screen("? come") or on_screen("come"), "Tab: come written")
check(colours["come"] == predict.C_PRED, "what Tab wrote, green")
check(on_screen("code"), "Tab with a suggestion: the mode stays")
type_(" faccio a sal")
check(colours["vare"] == predict.C_GHOST, "sal: salvare")
type_("tare")
check(on_screen("Saltare con la gravit"), "the question written with Tab, answered")
keys = { " ", "\t" }
frame()
check(on_screen("sprite"), "after a space Tab is the next mode")
assist.close()
print = print0

-- music (bm Sound): the recipe for the words, played as it changes (on_preview),
-- a variant to the right, Enter hands it over; over the notes already there
-- it follows their key
local music, previews = nil, 0
assist.open{ mode = "music", on_music = function(m) music = m end, on_preview = function() previews = previews + 1 end }
type_("ritmo rock")
check(on_screen("Rock beat"), "music: a rock beat for the words")
check(on_screen("BPM"), "music: its tempo and key")
local before = previews
keys = { "right" }
frame()
check(on_screen("#2") and previews > before, "music: right, variant 2, played")
keys = { "\n" }
frame()
check(music and music.kind == "beat" and music.gen == "beat.rock" and music.seed == 2 and #music.patterns == 1,
      "music handed over: the rock beat, variant 2")
check(music and music.patterns[1].tracks[0] and #music.patterns[1].tracks[0] == 64, "music: the kick's track, 64 steps")
assist.open{ mode = "music", on_music = function(m) music = m end,
             context = { notes = { 57, 60, 64, 53, 57, 60, 48, 52, 55, 43, 47, 50 }, bars = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 } } }
type_("una melodia malinconica")
keys = { "\n" }
frame()
check(music and music.kind == "melody" and music.chords[1] == "Am" and music.chords[2] == "F",
      "music: a melody over the chords that are there (Am F)")
assist.open{ mode = "music", on_music = function(m) music = m end }
type_("effetto moneta")
keys = { "\n" }
frame()
check(music and music.kind == "sfx" and music.sfx and #music.sfx.steps >= 2, "music: the coin, a sound effect")

say(string.format("panel: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
