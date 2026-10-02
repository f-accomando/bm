-- Host tests of the word completion (src/ai/predict.lua): the words before
-- the cursor, the suggestions of each dictionary, their case and accents,
-- and the texts of tests/predict/texts.lua typed again with Tab.
--   luahost tests/predict/predict_test.lua build

local build = arg[1] or "build"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local P = require "predict"
local typist = dofile("tests/predict/typist.lua")
local TEXTS = dofile("tests/predict/texts.lua")

local fails, checks = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    print("FAIL " .. what)
  end
end

-- text
check(P.plain("Perch\130") == "perche", "plain: lowercase, no accents")
check(P.from_utf8("citt\195\160 \226\128\153") == "citt\133 '", "UTF-8 to code page 437")

-- the word being typed and the one before
local prefix, prev = P.word_at("Ciao Marco, doma", "it")
check(prefix == "doma" and prev == "marco", "prose: doma after marco: " .. tostring(prev))
prefix, prev = P.word_at("Fatto. Dom", "it")
check(prefix == "Dom" and prev == "^", "prose: a sentence that starts")
prefix, prev = P.word_at("  local s = string.fo", "lua")
check(prefix == "string.fo" and prev == "s", "code: the dotted name")
check(P.word_at("x = 12", "lua") == nil, "code: a number is not a word")

-- the dictionaries
local function first(lang, before, words)
  local c = P.complete(before, { lang = lang, words = words })
  return c and c.word, c
end
check(first("lua", "f") == "function", "code, a line not indented: f -> function")
check(first("lua", "  f") == "for", "code, indented: f -> for")
local w = first("lua", "  if b")
check(w == "btn" or w == "btnp", "code: 'if b' -> btn: " .. tostring(w))
check(P.dict("lua").api.btn ~= nil, "the API's signatures, from the knowledge base")
local _, c = first("lua", "spr")
check(c and c.ending == "(", "an API function ends with its parenthesis")
check(first({ it = 1, ask = 2 }, "co") == "come", "questions: co -> come")
check(first("it", "Ciao Marco, domani se") == "sera", "after domani: sera")
w = first("it", "perch")
check(w == "perch\130", "the accent comes with the word: " .. tostring(w))
w = first("it", "CA")
check(w and w == w:upper(), "all capitals typed: all capitals written: " .. tostring(w))
w = first("it", "Ma")
check(w and w:sub(1, 1) == "M", "a capital typed: kept: " .. tostring(w))
check(first("en", "The new con") == "console", "English: the new con -> console")
check(P.complete("Ciao ", { lang = "it" }) == nil, "nothing before a word is typed")
check(P.complete("ab", { lang = "it", min = 3 }) == nil, "min: the shortest prefix")
check(P.complete("ca", { lang = "none" }) == nil, "none: no suggestions")

-- the names of the code being written
local words = P.count_words({ "local player_speed = 3", "-- a comment: commented_out", "player_speed = 4" })
check(words.player_speed == 2 and not words.commented_out, "the names of the code, not its comments")
check(first("lua", "x = player_sp", words) == "player_speed", "a name of the tab: player_sp -> player_speed")

-- the texts of 100 characters, typed again: fewer keys
for _, t in ipairs(TEXTS) do
  local keys, tabs = typist(t.text, { lang = t.lang, autoindent = t.lang == "lua" })
  local plain = typist(t.text, { lang = t.lang, autoindent = t.lang == "lua", complete = false })
  check(tabs > 0 and keys < plain, string.format("%s: %d keys with Tab, %d without", t.name, keys, plain))
end

print(string.format("predict: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
