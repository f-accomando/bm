-- The benchmark of the pad typing: how many presses each way of writing
-- needs for the texts of padtype.TEXTS (100 characters: 100 presses
-- on a keyboard), the presses of the first ten words, and, with --folds,
-- the presses per character on the texts of src/ai/words that the
-- dictionary has not seen (each group left out in turn).
--   luahost tests/pad/bench.lua build [--folds]

local build = arg[1] or "build"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local pt = require "padtype"
local texts = { list = pt.TEXTS }
local out = io.write

-- an on-screen keyboard, like the consoles': the cross moves on the grid
-- (across the edges too), cross types, triangle is the space, R2 Enter,
-- a capital with L2 held (no more presses)
local GRID = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?",
               "!:;-\"()\138\133\151", "=+*/_<>#[]" }
local POS = {}
for r, row in ipairs(GRID) do
  for c = 1, #row do
    local ch = row:sub(c, c)
    if not POS[ch] then POS[ch] = { r, c } end
  end
end
local function osk(text)
  local cur, n = POS.g, 0
  for i = 1, #text do
    local ch = text:sub(i, i):lower()
    if ch == " " or ch == "\n" then n = n + 1
    elseif POS[ch] then
      local r, c = POS[ch][1], POS[ch][2]
      local dc = math.abs(c - cur[2])
      n = n + math.abs(r - cur[1]) + math.min(dc, 10 - dc) + 1
      cur = POS[ch]
    else
      n = n + 1
    end
  end
  return n
end

-- multitap of syllables (the first idea): a direction again and again goes
-- through its consonants (up b c d, right f g h, down l m n, left p q r; with
-- R2 s t v, z, j k w, x y), the syllable starts with "a", triangle and
-- circle go forward and back through the vowels, square keeps the
-- consonant alone, cross confirms (only before the same direction again);
-- R1 is the space, anything else one press
local GROUPS = { "bcd", "fgh", "lmn", "pqr", "stv", "z", "jkw", "xy" }
local KEY = {}
for g, s in ipairs(GROUPS) do
  for i = 1, #s do KEY[s:sub(i, i)] = { g, i } end
end
local VOW = "aeiou"
local function multitap(text)
  local s = pt.plain(text)
  local n, last, i = 0, nil, 1
  while i <= #s do
    local ch = s:sub(i, i)
    if KEY[ch] then
      local g, k = KEY[ch][1], KEY[ch][2]
      if last == g then n = n + 1 end
      n = n + k
      local v = VOW:find(s:sub(i + 1, i + 1), 1, true)
      if v and i < #s then
        n = n + math.min(v - 1, 6 - v)
        i = i + 2
      else
        n = n + 1
        i = i + 1
      end
      last = g
    elseif VOW:find(ch, 1, true) then
      local v = VOW:find(ch, 1, true)
      n = n + 1 + math.min(v - 1, 6 - v)
      i = i + 1
      last = "v"
    else
      n = n + 1
      i = i + 1
      last = nil
    end
    if text:sub(i - 1, i - 1):find("[\128-\165]") then n = n + 1 end    -- the accent
  end
  return n
end

-- facile is counted as a beginner writes it: one letter a press with the
-- cross and the triggers, the space on its own
local function count(text, lang, mode, predict)
  local _, n = pt.encode(text, { lang = lang, mode = mode, predict = predict, simple = mode == "facile",
                                 words = lang == "lua" and pt.count_words({}) or nil })
  return n
end

out("| Testo (100 caratteri) | Tastiera | Tastiera a schermo | Multitap sillabe | Facile | Facile + predizione | Sillabe | Steno | Sillabe + predizione | Steno + predizione |\n")
out("|---|---|---|---|---|---|---|---|---|---|\n")
for _, t in ipairs(texts.list) do
  out(string.format("| %s | 100 | %d | %d | %d | %d | %d | %d | %d | %d |\n", t.name, osk(t.text), multitap(t.text),
                    count(t.text, t.lang, "facile", false), count(t.text, t.lang, "facile", true),
                    count(t.text, t.lang, "sillabe", false), count(t.text, t.lang, "steno", false),
                    count(t.text, t.lang, "sillabe", true), count(t.text, t.lang, "steno", true)))
end

-- the questions to the assistant that it was never trained on
-- (src/ai/kb/tests.txt): Italian words, or Italian and the questions of
-- its knowledge base (where one talks to it)
local qs = {}
for l in io.lines("src/ai/kb/tests.txt") do
  local q = l:match("^([^#=][^=]-)%s*=>")
  if q then qs[#qs + 1] = pt.from_utf8(q) end
end
local function per_char(lang, predict)
  local n, c = 0, 0
  for _, q in ipairs(qs) do
    local _, k = pt.encode(q, { lang = lang, predict = predict })
    n, c = n + k, c + #q
  end
  return n / c, c
end
local none, chars = per_char("it", false)
out(string.format("\nDomande all'assistente mai viste (%d, %d caratteri), sillabe, pressioni per carattere:\n",
                  #qs, chars))
out(string.format("senza predizione %.3f, dizionario italiano %.3f, italiano + domande della base di conoscenza %.3f\n",
                  none, per_char("it", true), per_char({ it = 1, ask = 2 }, true)))

-- the first ten words, press by press
local NICE = { L1 = "L1", L2 = "L2", R1 = "R1", R2 = "R2", Share = "Share", Start = "Start",
               up = "\226\134\145", down = "\226\134\147", left = "\226\134\144", right = "\226\134\146",
               triangle = "\226\150\179", square = "\226\150\161", cross = "\226\156\149", circle = "\226\151\139" }
local function nice(b)
  local d = pt.describe(b):gsub("[%w]+", function(w) return NICE[w] or w end)
  return (d:gsub("%+", " + "))
end
local it = texts.list[1].text
local ten = it:match("^(%S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+)")
for _, mode in ipairs({ "facile", "sillabe", "steno" }) do
  local steps, n = pt.encode(ten, { mode = mode, predict = true, simple = mode == "facile" })
  out(string.format("\n%s + predizione, le prime 10 parole (%d caratteri, %d pressioni):\n\n", mode, #ten, n))
  out("| # | Tasti | Scrive |\n|---|---|---|\n")
  for i, s in ipairs(steps) do
    local what = s.out:gsub(" ", "_")
    if s.kind == "pick" then what = what .. " (suggerimento)" end
    if s.kind == "caps" then what = "(maiuscola)" end
    if s.kind == "accent" then what = "(accento)" .. what end
    if s.kind == "space" then what = "_ (spazio)" end
    out(string.format("| %d | %s | %s |\n", i, nice(s.bits), what))
  end
end

-- the corpus, each group left out of the dictionary in turn
if arg[2] == "--folds" then
  local groups = { "giochi", "informativi", "lettere", "narrativa", "quotidiano", "tecnica" }
  local tot = { 0, 0, 0, 0, 0, 0 }
  local chars = 0
  for _, g in ipairs(groups) do
    package.loaded.padwords = dofile(build .. "/pad/fold_" .. g .. ".lua")
    pt.reset_words()
    for _, suffix in ipairs({ "", "_2", "_3", "_4" }) do
      local f = io.open("src/ai/words/it_" .. g .. suffix .. ".txt", "rb")
      if f then
        local text = pt.from_utf8(f:read("a"))
        f:close()
        chars = chars + #text
        for i, m in ipairs({ { "sillabe", false }, { "steno", false }, { "sillabe", true }, { "steno", true },
                             { "facile", false }, { "facile", true } }) do
          tot[i] = tot[i] + count(text, "it", m[1], m[2])
        end
      end
    end
  end
  out(string.format("\nTesti di src/ai/words non visti dal dizionario (%d caratteri), pressioni per carattere:\n", chars))
  out(string.format("facile %.3f, facile + predizione %.3f, sillabe %.3f, steno %.3f, sillabe + predizione %.3f, steno + predizione %.3f\n",
                    tot[5] / chars, tot[6] / chars, tot[1] / chars, tot[2] / chars, tot[3] / chars, tot[4] / chars))
end
