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
function keyp() return table.remove(keys, 1) end
function btn(i) return held[i] == true end
function btnp(i) return pressed[i] == true end

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

say(string.format("panel: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
