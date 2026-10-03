-- The benchmark of the word completion (docs/PREDICT.md): how many keys
-- the texts of tests/predict/texts.lua need on bm Code's keyboard, without
-- and with Tab (100 characters: 100 keys, a capital with Shift counting as
-- one), the keys of the first ten words, the questions to the assistant
-- it was never trained on, and, with --folds, the texts of src/ai/words
-- that the dictionary has not seen (each group left out in turn).
--   luahost tests/predict/bench.lua build [--folds]

local build = arg[1] or "build"
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local P = require "predict"
local typist = dofile("tests/predict/typist.lua")
local TEXTS = dofile("tests/predict/texts.lua")
local out = io.write

local function keys(text, lang, complete)
  local k, tabs, steps = typist(text, { lang = lang, autoindent = lang == "lua", complete = complete })
  return k, tabs, steps
end

out("| Testo | Caratteri | Tasti | Tasti con Tab | Tab | Risparmio |\n|---|---|---|---|---|---|\n")
for _, t in ipairs(TEXTS) do
  local plain = keys(t.text, t.lang, false)
  local k, tabs = keys(t.text, t.lang, true)
  out(string.format("| %s | %d | %d | %d | %d | %.0f%% |\n", t.name, #t.text, plain, k, tabs,
                    100 * (plain - k) / plain))
end

-- the questions to the assistant that it was never trained on
-- (src/ai/kb/tests.txt): Italian words, or Italian and the questions of
-- its knowledge base (as in the assistant's panel)
local qs = {}
for l in io.lines("src/ai/kb/tests.txt") do
  local q = l:match("^([^#=][^=]-)%s*=>")
  if q then qs[#qs + 1] = P.from_utf8(q) end
end
local function per_char(lang)
  local n, c = 0, 0
  for _, q in ipairs(qs) do
    n, c = n + keys(q, lang, lang ~= "none"), c + #q
  end
  return n / c, c
end
local none, chars = per_char("none")
out(string.format("\nDomande all'assistente mai viste (%d, %d caratteri), tasti per carattere:\n", #qs, chars))
out(string.format("senza completamento %.3f, dizionario italiano %.3f, italiano + domande della base di conoscenza %.3f\n",
                  none, per_char("it"), per_char({ it = 1, ask = 2 })))

-- the first ten words, key by key
local it = TEXTS[1].text
local ten = it:match("^(%S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+ %S+)")
local k, tabs, steps = keys(ten, "it", true)
out(string.format("\nLe prime 10 parole (%d caratteri, %d tasti, %d Tab):\n\n", #ten, k, tabs))
out("| Parola | Tasti | Quanti |\n|---|---|---|\n")
local word, list = "", {}
for i = 1, #steps + 1 do
  local s = steps[i]
  if not s or s.key == " " then
    out(string.format("| %s | %s | %d su %d |\n", word, table.concat(list, " "), #list, #word))
    word, list = "", {}
  else
    word = word .. s.out
    list[#list + 1] = s.key == "Tab" and "Tab (" .. s.out .. ")" or s.key
  end
end
out("\nLo spazio dopo ogni parola: un tasto.\n")

-- the corpus, each group left out of the dictionary in turn
if arg[2] == "--folds" then
  local groups = { "giochi", "informativi", "lettere", "narrativa", "quotidiano", "tecnica" }
  local plain, with, tabs_all, chars = 0, 0, 0, 0
  for _, g in ipairs(groups) do
    package.loaded.words = dofile(build .. "/predict/fold_" .. g .. ".lua")
    P.reset_words()
    for _, suffix in ipairs({ "", "_2", "_3", "_4" }) do
      local f = io.open("src/ai/words/it_" .. g .. suffix .. ".txt", "rb")
      if f then
        local text = P.from_utf8(f:read("a"))
        f:close()
        chars = chars + #text
        plain = plain + keys(text, "it", false)
        local n, tabs = keys(text, "it", true)
        with, tabs_all = with + n, tabs_all + tabs
      end
    end
  end
  out(string.format("\nTesti di src/ai/words non visti dal dizionario (%d caratteri):\n", chars))
  out(string.format("tasti per carattere %.3f senza completamento, %.3f con Tab (%d Tab): %.1f%% di tasti in meno\n",
                    plain / chars, with / chars, tabs_all, 100 * (plain - with) / plain))
end
