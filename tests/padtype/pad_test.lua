-- Host tests of typing with the pad (src/ai/padtype.lua): the rules of
-- compose (the wait for the double, the layers, the syllables turned by
-- square and triangle, erase, the words of R2, L1 / R1, Share), the
-- on-screen keyboard, and every practice text written to the end by a
-- simulated typist that presses what pt.coach says, frame by frame.
--   luahost tests/padtype/pad_test.lua build [-v]

local build = arg[1] or "build"
local verbose = arg[2] == "-v"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local pt = require "padtype"
local B = pt.BITS

local fails, checks = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    print("FAIL " .. what)
  end
end

------------------------------------------------------------------ the pad

local clock, FRAME = 0, 1 / 60
local function host(lang, text)
  local h = pt.text_host(lang, text)
  h.now = function() return clock end
  return h
end
local function frame(h, bits)
  clock = clock + FRAME
  pt.update(h, bits or 0)
end
-- tap (holding hold): down two frames, up one; times quickly
local function press(h, tap, hold, times)
  hold = hold or 0
  if hold ~= 0 then frame(h, hold) end
  for _ = 1, times or 1 do
    frame(h, hold | tap)
    frame(h, hold | tap)
    frame(h, hold)
  end
  frame(h, 0)
end
local function settle(h) for _ = 1, 24 do frame(h, 0) end end   -- the waiting press goes
local function fresh(lang, mode, text)
  pt.clear(true)
  pt.on(mode or "compose")
  pt.set({ delay = 0.25, fallback = "keyboard" })
  local h = host(lang, text)
  frame(h, 0)
  return h
end

------------------------------------------------------------------ compose

-- a consonant waits for its double, then the prediction finishes it
local h = fresh("it")
press(h, B.UP)
check(h.text() == "", "a press waits for its double: " .. h.text())
check(pt.pending() == "t", "the waiting press: t")
settle(h)
local t = h.text()
check(t:sub(1, 1) == "T" and #t >= 1, "up: t, a capital at the start, finished by the prediction: " .. t)
check(pt.open_len() == #t, "the syllable is open")

-- two quick presses: the alternative, never the same twice
h = fresh("it")
press(h, B.UP, 0, 2)
check(h.text():sub(1, 1) == "D", "up up: d: " .. h.text())
h = fresh("it", "compose", "x ")
press(h, B.LEFT, 0, 2)
check(h.text():sub(3, 3) == "c", "left left: c: " .. h.text())
-- a slow second press: the same consonant again
h = fresh("en", "compose", "a ")
press(h, B.UP)
settle(h)
local first = h.text()
press(h, B.UP)
settle(h)
check(h.text():sub(1, #first + 1) == first .. "t", "up, wait, up: t then t: " .. h.text())

-- another press does not wait: it lets the first go
h = fresh("it", "compose", "x ")
press(h, B.UP)
press(h, B.RIGHT)
check(h.text():sub(3, 3) == "t", "up then right: the t is written at once: " .. h.text())

-- square and triangle turn the syllable, circle erases it
h = fresh("it", "compose", "la ")
press(h, B.DOWN)                     -- r
settle(h)
local s1 = h.text()
press(h, B.X)
local s2 = h.text()
check(s2 ~= s1 and s2:sub(1, 4) == "la r", "square: the next syllable: " .. s1 .. " -> " .. s2)
press(h, B.Y)
check(h.text() == s1, "triangle: back to the first: " .. h.text())
press(h, B.B)
check(h.text() == "la ", "circle: the syllable goes: [" .. h.text() .. "]")
press(h, B.B)
check(h.text() == "la", "circle again: a character: [" .. h.text() .. "]")

-- the vowel syllable: square at the start of a word
h = fresh("it", "compose", "Io ")
press(h, B.X)
local v = h.text():sub(4)
check(v:match("^[aeiou\133\138\130\141\149\151]+$") ~= nil, "square: a vowel: " .. v)
h = fresh("it", "compose", "Lui ")
press(h, B.Y)
check(h.text():match("^Lui [\133\138\130\141\149\151]$") ~= nil, "triangle at the start of a word: an accented vowel: " .. h.text())

-- the dictionary: cia -> ciao; t after tu -> tto
h = fresh("it", "compose", "")
local cands = pt.syllables(h, "Ciao Marco, tu", "t")
local has = false
for _, c in ipairs(cands) do if c == "to" then has = true end end
check(has, "after tu, t: tto among the syllables")
cands = pt.syllables(h, "", "c")
check(#cands > 6 and cands[#cands] == "\138", "the tail ends with the accented vowels")
cands = pt.syllables(h, "Lui ", "")
local acc = false
for _, c in ipairs(cands) do if c == "\138" then acc = true end end
check(acc, "a vowel at the start of a word: \138 among them")

-- the double press of cross: a full stop (a dot in code)
h = fresh("it", "compose", "Fatto")
press(h, B.A)
settle(h)
check(h.text() == "Fatto ", "cross: a space")
h = fresh("it", "compose", "Fatto")
press(h, B.A, 0, 2)
check(h.text() == "Fatto. ", "cross cross: a full stop and a space: [" .. h.text() .. "]")
press(h, B.UP)
settle(h)
check(h.text():sub(8, 8) == "T", "after the full stop: a capital: " .. h.text())
h = fresh("lua", "compose", "math")
press(h, B.A, 0, 2)
check(h.text() == "math.", "code: cross cross is a dot: " .. h.text())

-- the layers: L2 p, R2 + right right y, L2 + R2 numbers, punctuation
h = fresh("en", "compose", "a ")
press(h, B.UP, B.L2)
settle(h)
check(h.text():sub(3, 3) == "p", "L2 + up: p: " .. h.text())
h = fresh("en", "compose", "a ")
press(h, B.RIGHT, B.R2, 2)
check(h.text():sub(3, 3) == "y", "R2 + right right: y: " .. h.text())
h = fresh("lua", "compose", "x = ")
press(h, B.UP, B.L2 | B.R2)
press(h, B.B, B.L2 | B.R2)
press(h, B.B, B.L2 | B.R2)
check(h.text() == "x = 100", "L2 + R2: 1, circle 0, 0: " .. h.text())
press(h, B.Y)
check(h.text() == "x = 109", "triangle turns the digit: 0 -> 9: " .. h.text())
h = fresh("it", "compose", "Ciao")
press(h, B.A, B.L2)
settle(h)
check(h.text() == "Ciao,", "L2 + cross: a comma: " .. h.text())
press(h, B.B, B.L2, 2)
check(h.text() == "Ciao,!", "L2 + circle twice: !: " .. h.text())
h = fresh("lua", "compose", "f")
press(h, B.A, B.L2 | B.R2, 2)
check(h.text() == "f)", "L2 + R2 + cross twice: ): " .. h.text())

-- no diagonals: a press counts when the cross settles on one direction
h = fresh("it", "compose", "x ")
frame(h, B.UP | B.RIGHT)
frame(h, B.UP | B.RIGHT)
frame(h, 0)
settle(h)
check(h.text() == "x ", "a diagonal writes nothing: [" .. h.text() .. "]")
frame(h, B.UP | B.RIGHT)
frame(h, B.RIGHT)
frame(h, B.RIGHT)
frame(h, 0)
settle(h)
check(h.text():sub(3, 3) == "n", "from the diagonal to right: n: " .. h.text())

-- the words of R2
h = fresh("it", "compose", "Ciao Marco, doma")
local w = pt.suggestions()[1]
check(w and w.word == "domani", "the first suggestion: domani: " .. tostring(w and w.word))
check(pt.ghost() == "ni", "the ghost: ni")
press(h, B.A, B.R2)
check(h.text() == "Ciao Marco, domani ", "R2 + cross: the word and a space")
check(pt.flash() == #"domani ", "what the suggestion wrote: green")
press(h, B.A, B.L2)
settle(h)
check(h.text() == "Ciao Marco, domani, ", "a comma goes after the word: [" .. h.text() .. "]")
press(h, B.B)
check(h.text() == "Ciao Marco, domani ", "circle: the comma goes: [" .. h.text() .. "]")
press(h, B.B)
check(h.text() == "Ciao Marco, doma", "circle: the word goes, its beginning back: [" .. h.text() .. "]")
press(h, B.B, B.R2)
check(h.text() == "Ciao Marco, ", "R2 + circle: the word goes: [" .. h.text() .. "]")
h = fresh("lua", "compose", "  cl")
local found
for k, s in ipairs(pt.suggestions()) do if s.word == "cls" then found = k end end
check(found ~= nil, "code: cl -> cls")
if found then
  press(h, ({ B.A, B.X, B.Y })[found], B.R2)
  check(h.text() == "  cls(", "a function: with its parenthesis: " .. h.text())
end

-- L1 and R1: back and forward, held the cross moves, both a new line
h = fresh("it", "compose", "abc")
press(h, B.L1)
check(h.cursor() == 2, "L1: back")
press(h, B.R1)
check(h.cursor() == 3, "R1: forward")
frame(h, B.L1)
for _ = 1, 4 do frame(h, B.L1 | B.LEFT) end
frame(h, B.L1)
frame(h, 0)
check(h.cursor() == 2, "L1 held + left: the cursor, not a letter: " .. h.cursor())
h = fresh("it", "compose", "abc")
frame(h, B.L1)
for _ = 1, 20 do frame(h, B.L1 | B.R1) end
frame(h, 0)
check(h.text() == "abc\n", "L1 + R1 held: a new line")
h = fresh("lua", "compose", "if x then")
frame(h, B.L1)
for _ = 1, 20 do frame(h, B.L1 | B.R1) end
frame(h, 0)
check(h.text() == "if x then\n  ", "code: the new line indented after then: [" .. h.text() .. "]")
press(h, B.B)
check(h.text() == "if x then", "circle: the new line goes: [" .. h.text() .. "]")
-- R1 closes the syllable: square then starts a vowel one
h = fresh("it", "compose", "x ")
press(h, B.LEFT, 0, 2)
local c1 = h.text()
press(h, B.R1)
press(h, B.X)
check(#h.text() > #c1 and h.text():sub(1, #c1) == c1, "R1 then square: a new vowel: " .. h.text())

-- L3: a capital
h = fresh("it", "compose", "ciao ")
press(h, B.L3)
press(h, B.RIGHT, 0, 2)
check(h.text():sub(6, 6) == "M", "L3: the next letter a capital: " .. h.text())

-- Share: the keyboard, and back
h = fresh("it", "compose", "")
press(h, B.SELECT)
check(pt.mode() == "keyboard", "Share: the keyboard")
press(h, B.A)                         -- the key under the cursor: q
check(h.text() == "Q", "the keyboard: cross writes the key (a capital at the start): " .. h.text())
press(h, B.DOWN)
press(h, B.A)
check(h.text() == "Qa", "down, cross: a")
press(h, B.X)
check(h.text() == "Qa ", "square: a space")
press(h, B.B)
check(h.text() == "Qa", "circle: erases")
press(h, B.SELECT)
check(pt.mode() == "compose", "Share: compose again")
pt.set({ fallback = "off" })
press(h, B.SELECT)
check(pt.mode() == nil, "fallback off: Share turns it off")
press(h, B.UP)
settle(h)
check(h.text() == "Qa", "off: the pad is the host's")
press(h, B.SELECT)
check(pt.mode() == "compose", "Share: on again")
-- R3: the keyboard for one character
h = fresh("lua", "compose", "a ")
press(h, B.R3)
check(pt.mode() == "keyboard", "R3: the keyboard")
press(h, B.Y)                         -- the symbols
press(h, B.RIGHT)
press(h, B.RIGHT)                     -- row 2: + - *
press(h, B.A)
check(h.text() == "a *", "the keyboard's symbols: * " .. h.text())
check(pt.mode() == "compose", "after one character: compose again")

------------------------------------------------------------------ the texts

-- the typist: what coach says, pressed; the presses counted by padtype
local function typist(lang, target, mode)
  local hh = fresh(lang, mode)
  pt.reset_count()
  local steps, waits = 0, 0
  while true do
    local s = pt.coach(hh, target)
    if not s then break end
    if s.wait then
      frame(hh, 0)
      waits = waits + 1
      if waits > 100000 then break end
    else
      steps = steps + 1
      if steps > #target * 6 + 40 then
        return nil, hh.text(), s
      end
      if verbose then print(lang, mode, s.label, s.tap, s.hold, s.double, "[" .. hh.text() .. "]") end
      if s.both then
        frame(hh, B.L1)
        for _ = 1, 20 do frame(hh, B.L1 | B.R1) end
        frame(hh, 0)
      else
        press(hh, s.tap, s.hold, s.double and 2 or 1)
      end
    end
  end
  settle(hh)
  return pt.presses(), hh.text()
end

local total = {}
for _, lang in ipairs({ "it", "en", "lua" }) do
  for _, mode in ipairs({ "compose", "keyboard" }) do
    local chars, presses, ok = 0, 0, true
    for i, target in ipairs(pt.TEXTS[lang]) do
      local n, text, s = typist(lang, target, mode)
      local good = n and text:gsub("%s+$", "") == target:gsub("%s+$", "")
      check(good, lang .. " " .. mode .. " text " .. i .. " written to the end: [" .. text .. "]" ..
            (s and (" stuck on " .. tostring(s.label)) or ""))
      if good then
        chars, presses = chars + #target, presses + n
      else
        ok = false
      end
    end
    total[lang .. " " .. mode] = presses / math.max(1, chars)
    print(string.format("%-4s %-8s %5d characters %6d presses  %.2f a character", lang, mode, chars, presses,
                        presses / math.max(1, chars)))
  end
  check(total[lang .. " compose"] < total[lang .. " keyboard"], lang .. ": compose takes fewer presses than the keyboard")
end
for _, lang in ipairs({ "it", "en", "lua" }) do
  check(total[lang .. " compose"] < total[lang .. " keyboard"] / 2.5,
        lang .. ": compose, less than half the presses of the on-screen keyboard")
end
check(total["it compose"] < 1.15 and total["en compose"] < 1.15 and total["lua compose"] < 1.4,
      "presses a character: no worse than when this test was written (1.09, 1.06, 1.33)")

-- a word the dictionary does not know: the tail reaches it
local n, text = typist("it", "Zakutek", "compose")
check(n and text == "Zakutek", "an unknown word: written with the tail: " .. tostring(text))

print(string.format("padtype: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
