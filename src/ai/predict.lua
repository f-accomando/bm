-- Word completion (M30): the word being typed, finished by a dictionary.
-- A library built into the kernel (require "predict"); its dictionaries
-- are another (require "words", made by scripts/mkwords.py from the texts
-- of src/ai/words, the Lua of the games and the assistant's knowledge base).
--
--   local predict = require "predict"
--   local c = predict.complete(text_before_cursor, { lang = "lua" })
--   if c then
--     print(c.rest, x, y, predict.C_GHOST)    -- the suggestion, in grey-blue
--     -- Tab: the c.prefix typed becomes c.word .. c.ending (its case and
--     -- accents are the dictionary's), then predict.C_PRED (green) on it
--   end
--
-- lang: "it", "en", "lua", "ask" (the questions to the assistant), or a mix
-- with weights ({it = 1, ask = 2}: a question to the assistant). words:
-- the names of the code being written (predict.count_words), added with
-- weight words_weight. Text in code page 437, like the console's; accents
-- do not count ("perche" finds "perch\138"). Guide: docs/PREDICT.md.
--
-- A prefix of one or two letters shows a ghost only when the first word
-- beats the second by a margin (1.25, 1.15 in Lua). Otherwise there is no
-- suggestion, and Tab indents as when the dictionary has nothing.

local P = {}

P.C_GHOST = 0x6C8CC8          -- the suggestion, not written yet
P.C_PRED = 0x50E0B0           -- what the suggestion has just written

local PLAIN = { ["\133"] = "a", ["\138"] = "e", ["\130"] = "e", ["\141"] = "i", ["\149"] = "o",
                ["\151"] = "u", ["\160"] = "a", ["\161"] = "i", ["\162"] = "o", ["\163"] = "u",
                ["\144"] = "e" }
local LETTER = "[%a\128-\165]"

-- lowercase, without accents: the key of a word in the dictionary
local function plain(s)
  s = s:lower()
  if s:find("[\128-\255]") then s = s:gsub("[\128-\255]", PLAIN) end
  return s
end
P.plain = plain

------------------------------------------------------------------ the words

local dicts, loading = {}, {}

-- a language's dictionary, read by a coroutine that stops every few
-- hundred words (P.preload spreads it over the frames)
local function loader(lang)
  return coroutine.create(function()
    local src = (require "words")[lang]
    local d = { words = {}, keys = {}, count = {}, at = {}, func = {}, big = {}, N = 0, api = {}, top = {} }
    local i = 0
    for w, c, f in src.uni:gmatch("(%S+) (%d+)( ?f?)\n") do
      i = i + 1
      d.words[i], d.keys[i], d.count[i], d.at[w] = w, plain(w), tonumber(c), i
      if f ~= "" then d.func[w] = true end
      d.N = d.N + tonumber(c)
      if i % 512 == 0 and coroutine.isyieldable() then coroutine.yield() end
    end
    i = 0
    for line in src.big:gmatch("[^\n]+") do
      local prev, total, rest = line:match("^(%S+) (%d+) (.*)$")
      local t = { total = tonumber(total) }
      for w, c in rest:gmatch("(%S+) (%d+)") do
        t[#t + 1] = w
        t[w] = tonumber(c)
      end
      d.big[prev] = t
      i = i + 1
      if i % 128 == 0 and coroutine.isyieldable() then coroutine.yield() end
    end
    if src.api then
      for name, sig in src.api:gmatch("(%S+) ([^\n]+)\n") do d.api[name] = sig end
    end
    dicts[lang] = d
  end)
end

local function step(lang)
  local co = loading[lang] or loader(lang)
  loading[lang] = co
  local ok, err = coroutine.resume(co)
  if not ok then loading[lang] = nil; error(err, 0) end
  if coroutine.status(co) == "dead" then loading[lang] = nil end
end

-- the dictionary of lang, read now if it is not yet
local function dict(lang)
  while not dicts[lang] do step(lang) end
  return dicts[lang]
end
P.dict = dict

-- A slice of the dictionaries in langs not read yet (a few milliseconds on
-- the console): called once a frame, the first word typed finds them ready.
-- true when they all are.
function P.preload(langs)
  for _, lang in ipairs(langs) do
    if not dicts[lang] then
      step(lang)
      return false
    end
  end
  return true
end

-- another dictionary (the benchmark leaves texts out of it)
function P.reset_words() dicts, loading = {}, {} end

-- UTF-8 text in code page 437, like the console's
local UTF8 = { ["\195\160"] = "\133", ["\195\168"] = "\138", ["\195\169"] = "\130", ["\195\172"] = "\141",
               ["\195\178"] = "\149", ["\195\185"] = "\151", ["\195\136"] = "E'", ["\195\128"] = "A'",
               ["\194\171"] = '"', ["\194\187"] = '"', ["\226\128\148"] = "-", ["\226\128\147"] = "-",
               ["\226\128\153"] = "'", ["\226\128\152"] = "'", ["\226\128\156"] = '"', ["\226\128\157"] = '"',
               ["\226\128\166"] = "..." }
function P.from_utf8(s)
  return (s:gsub("[\192-\239][\128-\191]+", function(c) return UTF8[c] or "?" end))
end

local function lower_bound(keys, k)
  local lo, hi = 1, #keys + 1
  while lo < hi do
    local mid = (lo + hi) // 2
    if keys[mid] < k then lo = mid + 1 else hi = mid end
  end
  return lo
end

local KEYWORD_SPACE = {}
for w in ("local function if then else elseif for in do while repeat until return and or not goto"):gmatch("%S+") do
  KEYWORD_SPACE[w] = true
end

-- the word being written and the one before it ("^": none, or a sentence
-- that ends; "^0": a line of code not indented), from the text before the
-- cursor; nil inside a number
local function word_at(before, lang)
  if lang == "lua" then
    local prefix = before:match("[%a_][%w_%.]*$") or ""
    local rest = before:sub(1, #before - #prefix)
    if rest:match("[%w_%.]$") then return nil end
    local prev = rest:match("([%a_][%w_%.]*)[^%w_%.]*$")
    return prefix, prev or (rest == "" and "^0" or "^")
  end
  local prefix = before:match(LETTER .. "*$")
  local rest = before:sub(1, #before - #prefix)
  local prev, gap = rest:match("(" .. LETTER .. "+'?)([^%a\128-\165]*)$")
  if not prev or gap:find("[%.%?!]") then return prefix, "^" end
  return prefix, prev:lower()
end
P.word_at = word_at

-- the words of a short prefix, the most written first: one or two letters
-- match a thousand words, the others can only come after them (or follow
-- prev, below)
local TOP = 8
local function top_of(d, key)
  local t = d.top[key]
  if t then return t end
  t = {}
  local keys, count = d.keys, d.count
  local i = lower_bound(keys, key)
  while i <= #keys and keys[i]:sub(1, #key) == key do
    t[#t + 1] = i
    i = i + 1
  end
  table.sort(t, function(a, b)
    if count[a] ~= count[b] then return count[a] > count[b] end
    return a < b
  end)
  for k = #t, TOP + 1, -1 do t[k] = nil end
  d.top[key] = t
  return t
end

-- the scores of one dictionary for a prefix after prev, added with weight
-- w: how often the word is written, and how often after prev
local function score_dict(d, key, same, prev, w, add)
  local bg = d.big[prev]
  if key == "" then
    if not bg then return end
    for i = 1, #bg do
      local x = bg[i]
      if bg[x] >= 2 then add(x, w * bg[x] / bg.total) end
    end
    return
  end
  local function score(i)
    local x = d.words[i]
    if x == same then return end
    local c = d.count[i]
    local sc = 0.4 * (c > 0 and c or 0.5) / d.N
    if bg and bg[x] then sc = sc + bg[x] / bg.total end
    add(x, w * sc)
  end
  if #key <= 2 then
    local seen = {}
    for _, i in ipairs(top_of(d, key)) do
      score(i)
      seen[i] = true
    end
    for j = 1, bg and #bg or 0 do
      local i = d.at[bg[j]]
      if i and not seen[i] and d.keys[i]:sub(1, #key) == key then score(i) end
    end
    return
  end
  local keys = d.keys
  local i = lower_bound(keys, key)
  while i <= #keys and keys[i]:sub(1, #key) == key do
    score(i)
    i = i + 1
  end
end

-- The words for a prefix after prev: up to n, best first, and their scores.
-- lang: a dictionary or a mix of them with their weights; buf: the names of
-- the code being written ({name = count, [1] = total}), with weight bufw.
local function candidates(lang, prefix, prev, n, buf, bufw)
  local key = plain(prefix)
  local score, list = {}, {}
  local function add(w, sc)
    if not score[w] then list[#list + 1] = w; score[w] = 0 end
    score[w] = score[w] + sc
  end
  local same = lang == "lua" and prefix or prefix:lower()
  if type(lang) == "table" then
    local names = {}
    for name in pairs(lang) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do score_dict(dict(name), key, same, prev, lang[name], add) end
  else
    score_dict(dict(lang), key, same, prev, 1, add)
  end
  if buf and key ~= "" then
    local total = buf[1] or 1
    for w, c in pairs(buf) do
      if type(w) == "string" and w ~= same and plain(w):sub(1, #key) == key then
        add(w, (bufw or 0.6) * c / total)
      end
    end
  end
  table.sort(list, function(a, b)
    if score[a] ~= score[b] then return score[a] > score[b] end
    return a < b
  end)
  local out = {}
  for k = 1, math.min(n or 3, #list) do out[k] = list[k] end
  return out, score
end
P.candidates = candidates

-- the identifiers of some code, counted; [1] is their total
function P.count_words(lines)
  local t, n = {}, 0
  for _, l in ipairs(lines) do
    l = l:gsub("%-%-.*$", ""):gsub('"[^"]*"', ""):gsub("'[^']*'", "")
    for w in l:gmatch("[%a_][%w_%.]*") do
      if #w > 2 then t[w] = (t[w] or 0) + 1; n = n + 1 end
    end
  end
  t[1] = n
  return t
end

-- a word as it is written after prefix: the case of what is typed (all
-- capitals, or the first one), and its ending: "(" after a function of
-- the code, " " after a keyword and in prose (not after an apostrophe)
local function shaped(w, prefix, lang, caps_first)
  if lang == "lua" then
    local ending = ""
    if dict("lua").func[w] then ending = "(" elseif KEYWORD_SPACE[w] then ending = " " end
    return w, ending
  end
  if not w:find("%u") then
    if #prefix >= 2 and prefix == prefix:upper() and prefix:find("%a") then w = w:upper()
    elseif (prefix ~= "" and prefix:sub(1, 1):find("%u")) or (prefix == "" and caps_first) then
      w = w:sub(1, 1):upper() .. w:sub(2)
    end
  end
  return w, w:sub(-1) == "'" and "" or " "
end
P.shaped = shaped

-- the text before the cursor ends a sentence (or there is none)
function P.sentence_start(before)
  local t = before:gsub("[%s\\\"%(]+$", "")
  return t == "" or t:find("[%.%?!]$") ~= nil
end

-- first word beats the second by this much, or the ghost stays hidden.
-- Lua already separates function/for with ^ and ^0, so its margin is lower.
local MARGIN = 1.25
local MARGIN_LUA = 1.15

-- The suggestion for the word that ends the text before the cursor, or nil:
--   { prefix = what is typed, word = the word, rest = what is missing,
--     ending = what follows it, list = up to o.n words (3) }
-- o: lang ("it" by default; "none": nothing), words, words_weight, min
-- (the shortest prefix, 1), n.
function P.complete(before, o)
  o = o or {}
  local lang = o.lang or "it"
  if lang == "none" then return nil end
  local prefix, prev = word_at(before, lang)
  if not prefix or #prefix < (o.min or 1) then return nil end
  local ok, list, scores = pcall(candidates, lang, prefix, prev, o.n or 3, o.words, o.words_weight)
  if not ok then return nil end
  local chosen, at
  for i, w in ipairs(list) do
    local word, ending = shaped(w, prefix, lang)
    if #word > #prefix then
      chosen = { prefix = prefix, word = word, rest = word:sub(#prefix + 1), ending = ending, list = list }
      at = i
      break
    end
  end
  if not chosen then return nil end
  -- one or two letters match a thousand words: hide the ghost when the
  -- next candidate is close, so Tab is not a guess
  if #plain(prefix) <= 2 and scores and list[at + 1] then
    local s1, s2 = scores[list[at]], scores[list[at + 1]]
    local margin = lang == "lua" and MARGIN_LUA or MARGIN
    if s1 and s2 and s2 > 0 and s1 < s2 * margin then return nil end
  end
  return chosen
end

return P
