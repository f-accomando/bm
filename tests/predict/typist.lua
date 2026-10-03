-- Types a text on bm Code's keyboard: one key a character (a capital with
-- Shift and an accented letter count as one), Enter keeps the indentation
-- (o.autoindent: Lua), and Tab when the grey-blue suggestion is the word
-- in the text, or its start, and writes at least two letters.
--   local typist = dofile("tests/predict/typist.lua")
--   local keys, tabs, steps = typist(text, { lang = "it" })
-- steps: { key = "Tab" or the character, out = what it wrote }

local predict = require "predict"

return function(text, o)
  o = o or {}
  local lang = o.lang or "it"
  local written, keys, tabs, steps = "", 0, 0, {}
  local i = 1
  while i <= #text do
    local before = written:sub(-80)
    if lang == "lua" then before = before:match("[^\n]*$") end
    local c = o.complete ~= false and predict.complete(before, { lang = lang, words = o.words,
                                                                 words_weight = o.words_weight })
    local a = c and i - #c.prefix
    if c and #c.rest >= 2 and text:sub(a, a + #c.word - 1) == c.word then
      written = written:sub(1, #written - #c.prefix) .. c.word
      i = a + #c.word
      keys, tabs = keys + 1, tabs + 1
      steps[#steps + 1] = { key = "Tab", out = c.rest }
    else
      local ch = text:sub(i, i)
      written = written .. ch
      keys = keys + 1
      i = i + 1
      local out = ch
      if ch == "\n" and o.autoindent then
        local sp = text:match("^ *", i)
        written, i, out = written .. sp, i + #sp, ch .. sp
      end
      steps[#steps + 1] = { key = ch, out = out }
    end
  end
  assert(written == text, "typed again: " .. written)
  return keys, tabs, steps
end
