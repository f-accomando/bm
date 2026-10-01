-- Host tests of the pad typing (src/ai/padtype.lua): the chords, the editing
-- through a host, the suggestions, the panel, and the benchmark texts
-- written again by the presses encode() finds, through the real engine.
--   luahost tests/pad/pad_test.lua build

package.path = (arg[1] or "build") .. "/?.lua;src/ai/?.lua;" .. package.path
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
function print(s, x, y, c) screen[#screen + 1] = tostring(s); return x end
function rectfill() end
function rect() end
function line() end
function circ() end
function circfill() end
function font() return 6, 12 end
local PAD = 0
function pad() return PAD end

local pt = require "padtype"
local texts = { list = pt.TEXTS }
local B = pt.BITS

local function new_host(lang, prose)
  local h = { text = "", lang = lang, prose = prose, moves = {} }
  function h.before() return h.text:match("[^\n]*$") end
  function h.insert(s) h.text = h.text .. s end
  function h.erase(n) h.text = h.text:sub(1, #h.text - n) end
  function h.newline() h.text = h.text .. "\n" end
  function h.move(d) h.moves[#h.moves + 1] = d end
  function h.undo() h.undone = true end
  return h
end

local function frame(h, bits)
  PAD = bits
  pt.update(h)
end

-- a chord: everything down for two frames, then up
local function press(h, bits)
  frame(h, bits); frame(h, bits); frame(h, 0)
end

------------------------------------------------------------------ chords

local function dec(b, mode)
  local a = pt.decode(b, nil, mode)
  return a and (a.s or a.kind) .. (a.space and "_" or "") or "nil"
end
check(dec(B.DOWN | B.Y) == "ca", "down + triangle = ca")
check(dec(B.DOWN | B.Y | B.L2) == "ga", "L2: the voiced twin, ga")
check(dec(B.DOWN | B.A | B.L2 | B.R2) == "qui", "L2 + R2 + down + cross = qui")
check(dec(B.DOWN | B.X | B.R2) == "he", "R2 + down + square = he")
check(dec(B.DOWN | B.A | B.B | B.L2 | B.R2) == "qu", "qu + u = qu")
check(dec(B.DOWN | B.Y | B.L2 | B.R2 | B.L1) == "cqua", "L1 doubles: cqua")
check(dec(B.UP | B.Y | B.L1) == "tta", "L1 doubles: tta")
check(dec(B.RIGHT | B.B | B.R1) == "no_", "R1: the space after")
check(dec(B.RIGHT | B.L2) == "l", "a consonant alone")
check(dec(B.A | B.B) == "u", "cross + circle = u")
check(dec(B.UP | B.RIGHT) == "x", "a diagonal alone: x")
check(dec(B.UP | B.RIGHT | B.L2 | B.Y) == "nil", "sillabe: no groups")
check(dec(B.UP | B.RIGHT | B.L2 | B.Y, "steno") == "tra", "steno: tra")
check(dec(B.DOWN | B.Y | B.X, "steno") == "cia", "steno: cia")
check(dec(B.DOWN | B.Y | B.X) == "nil", "sillabe: no diphthongs")
check(dec(B.L2 | B.X | B.R1) == ",_", "L2 + square: comma and space")
check(dec(B.SELECT | B.Y) == "1" and dec(B.SELECT | B.R2 | B.B) == "0", "Share: the numbers")
check(dec(B.SELECT) == "exit", "Share alone: out")
check(dec(B.L1) == "erase" and dec(B.R1) == " " or dec(B.R1) == "space", "L1 erases, R1 space")
check(dec(B.R2) == "pick" and dec(B.L1 | B.L2) == "accent" and dec(B.L1 | B.R1) == "caps", "shoulders alone")
check(dec(B.START) == "newline", "Start alone: a new line")

------------------------------------------------------------------ editing

local h = new_host("it", true)
pt.on(true)
frame(h, 0)                                -- (Share, which turned it on, is up)
-- the buttons need not go down together: down, then triangle, up in any order
frame(h, B.DOWN); frame(h, B.DOWN | B.Y); frame(h, B.Y); frame(h, 0)
check(h.text == "Ca", "a chord pressed one button after the other, capital at the start: " .. h.text)
press(h, B.UP | B.Y | B.L1 | B.R1)
check(h.text == "Catta ", "tta and space: " .. h.text)
press(h, B.X)
press(h, B.L1 | B.L2)
check(h.text == "Catta \138", "the accent: " .. h.text)
press(h, B.L1 | B.L2)
check(h.text == "Catta \130", "again: the other accent")
press(h, B.L1)
check(h.text == "Catta ", "L1 erases")
press(h, B.L1 | B.R1)
press(h, B.RIGHT | B.B)
check(h.text == "Catta No", "L1 + R1: the next letter is a capital")
-- L1 held: erases again and again
for _ = 1, 80 do frame(h, B.L1) end
frame(h, 0)
check(h.text == "", "L1 held erases everything: '" .. h.text .. "'")
-- Start + cross moves, Start + L1 undoes, Start alone is Enter
press(h, B.START | B.RIGHT)
press(h, B.START | B.L1)
check(h.moves[1] == "right" and h.undone, "Start + cross moves, Start + L1 undoes")
check(h.text == "", "nothing written by them")
-- a chord that means nothing writes nothing
press(h, B.Y | B.A)
check(h.text == "", "cross + triangle: nothing")

-- the suggestions: "co" offers words, R2 takes the first with a space,
-- L1 right after takes it back; punctuation after a suggestion
h = new_host("it", true)
press(h, B.DOWN | B.B)
local list = pt.suggestions()
check(#list == 3, "three suggestions for co")
local first = list[1]
check(pt.ghost() == first:sub(3), "the ghost is the rest of the first one")
press(h, B.R2)
check(h.text == "C" .. first:sub(2) .. " ", "R2 writes it, with the space: " .. h.text)
check(pt.flash() == #first - 2, "the part written by the suggestion is known")
press(h, B.L1)
check(h.text == "Co", "L1 takes it back")
press(h, B.L2)
check(h.text == "C" .. list[2]:sub(2) .. " ", "L2: the second one")
press(h, B.L2 | B.X)
check(h.text == "C" .. list[2]:sub(2) .. ", ", "a comma moves before the space: " .. h.text)
-- the next word, before any letter
h = new_host("it", true)
for _, b in ipairs({ B.LEFT | B.X, B.RIGHT | B.R2 | B.R1 }) do press(h, b) end   -- pe, r + space
check(h.text == "Per " and pt.suggestions()[1] ~= nil, "after 'Per ' a word is suggested before any letter")

-- code: the API with its parenthesis, keywords with a space
h = new_host("lua", false)
h.text = "  if b"
pt.refresh(h)
local s = pt.suggestions()
check(s[1] == "btn" or s[1] == "btnp", "code: 'if b' -> btn: " .. tostring(s[1]))
press(h, B.R2)
check(h.text:match("btnp?%($"), "an API comes with its parenthesis: " .. h.text)
screen = {}
pt.draw(0, 0)
check(table.concat(screen, " "):find("btnp?%(i"), "the panel shows its signature, from the knowledge base")
h.text = "f"
pt.refresh(h)
s = pt.suggestions()
local fn = s[1] == "function" and B.R2 or s[2] == "function" and B.L2 or B.L2 | B.R2
check(s[1] == "function" or s[2] == "function", "code: f -> function, first or second")
press(h, fn)
check(h.text == "function ", "a keyword comes with a space")

------------------------------------------------------------------ the panel

pt.on(true)
h = new_host("it", true)
frame(h, 0)
PAD = B.DOWN
pt.update(h)
screen = {}
pt.draw(0, 0)
local shown = table.concat(screen, " ")
check(shown:find("ca") and shown:find("co") and shown:find("ci"), "down held: the syllables of c on the buttons")
PAD = 0
pt.update(h)
PAD = B.L2
pt.update(h)
screen = {}
pt.draw(0, 0)
shown = table.concat(screen, " ")
check(shown:find(" d ") or shown:find("^d ") or shown:find(" d$") or shown:find("d"), "L2 held: the voiced bank")
check(shown:find(",") and shown:find("%."), "L2 held: the punctuation on the buttons")
PAD = 0
pt.update(h)
pt.set{ mode = "steno" }
PAD = B.UP | B.RIGHT | B.R2
pt.update(h)
screen = {}
pt.draw(0, 0)
check(table.concat(screen, " "):find("sta"), "steno: the diagonal st on the buttons")
PAD = 0
pt.update(h)
pt.set{ mode = "sillabe" }

------------------------------------------------------------------ texts

-- every text, every way: the presses encode() finds write it again
local function replay(text, o)
  local steps, n = pt.encode(text, o)
  local hh = new_host(o.lang, o.lang ~= "lua")
  if o.lang == "lua" then hh.words = pt.count_words({}) end
  pt.on(true)
  frame(hh, 0)
  pt.set{ mode = o.mode }
  for _, st in ipairs(steps) do press(hh, st.bits) end
  local got = hh.text:gsub(" +$", "")
  local want = text
  if o.lang == "lua" then
    got = got:gsub("\n +", "\n")
    want = want:gsub("\n +", "\n")
  end
  return got == want, n, got
end
for _, t in ipairs(texts.list) do
  for _, mode in ipairs({ "sillabe", "steno" }) do
    for _, pred in ipairs({ false, true }) do
      local ok, n, got = replay(t.text, { mode = mode, lang = t.lang, predict = pred })
      check(ok, string.format("%s, %s%s: written again\n  want %q\n  got  %q", t.name, mode,
                              pred and " + prediction" or "", t.text, got))
      check(n < #t.text, t.name .. ": fewer presses than characters")
    end
  end
end

------------------------------------------------------------------ practice

-- the practice: the guide's presses write the text, as many as the best
for i, t in ipairs(pt.TEXTS) do
  pt.set{ mode = "sillabe" }
  pt.practice_open(i)
  PAD = 0; pt.practice_update()
  check(pt.practice_is_open(), "practice open")
  screen = {}
  pt.practice_draw()
  check(table.concat(screen, " "):find("next:"), t.name .. ": the next press is shown")
  local steps, best = pt.encode(t.text, { lang = t.lang, predict = true, words = pt.count_words({}) })
  for _, st in ipairs(steps) do
    PAD = st.bits; pt.practice_update(); pt.practice_update()
    PAD = 0; pt.practice_update()
  end
  local got = pt.practice_text():gsub(" $", ""):gsub("\n +", "\n")
  check(got == t.text:gsub("\n +", "\n"), t.name .. ": the practice text written: " .. got)
  check(pt.presses() == best, t.name .. ": presses counted " .. pt.presses() .. " = " .. best)
  screen = {}
  pt.practice_draw()
  check(table.concat(screen, " "):find("Done"), t.name .. ": done")
  PAD = 128; pt.practice_update(); PAD = 0; pt.practice_update()
  check(not pt.practice_is_open(), t.name .. ": Share closes the practice")
end
-- following the guide press by press gets there too, as fast
for i, t in ipairs(pt.TEXTS) do
  for _, mode in ipairs({ "sillabe", "steno" }) do
    pt.set{ mode = mode }
    pt.practice_open(i)
    PAD = 0; pt.practice_update()
    local _, best = pt.encode(t.text, { mode = mode, lang = t.lang, predict = true, words = pt.count_words({}) })
    local done
    for _ = 1, 120 do
      local nxt
      nxt, done = pt.practice_next()
      if done or not nxt then break end
      PAD = nxt.bits; pt.practice_update(); pt.practice_update()
      PAD = 0; pt.practice_update()
    end
    check(done and pt.presses() <= best, string.format("%s, %s: the guide gets there in %d presses (best %d)",
                                                         t.name, mode, pt.presses(), best))
    PAD = 128; pt.practice_update(); PAD = 0; pt.practice_update()
  end
end
pt.set{ mode = "sillabe" }

say(string.format("padtype: %d/%d checks passed", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
