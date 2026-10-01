-- Typing with the pad (M30): one press of the cross and the four buttons
-- writes a whole syllable, the shoulders choose the bank, the dictionary
-- finishes the word. Two ways of writing:
--   sillabe  a consonant from the cross (L2 / R2 for the other banks), a
--            vowel from the buttons; together, the syllable: down + triangle
--            = "ca". L1 doubles the consonant, R1 adds the space
--   steno    the same, plus groups of consonants on the diagonals with L2 /
--            R2 ("tr", "ch", "st"...) and the diphthongs ia, io, ie (two
--            buttons): one press per syllable, like a stenotype
-- A press is a chord: it writes when every button is up again, so the
-- buttons need not go down at the same moment. Words come from
-- require "padwords" (scripts/mkpadwords.py). Guide: docs/PADTYPE.md.
--
--   local pt = require "padtype"
--   pt.on(true), pt.is_on(), pt.set{ mode = "steno" }
--   pt.update(host)     each frame: reads pad() and edits through the host:
--     host.before()     the text before the cursor, on its line
--     host.insert(s), host.erase(n), host.newline(), host.move(dir),
--     host.undo(), host.lang ("it", "en", "lua", "ask", a mix like
--     {it = 1, ask = 1}, or "none"), host.prose (capitals at the start of
--     a sentence), host.words (code: {name = count}), host.name
--   pt.ghost()          the rest of the first suggestion (pt.C_GHOST)
--   pt.flash()          length of the text the last suggestion wrote and
--                       of what follows it (pt.C_PRED), until the next press
--   pt.draw(x, y)       the panel (pt.size() wide and high)
--   pt.encode(text, o)  the shortest presses for a text (benchmark, guide)

local P = {}

------------------------------------------------------------------ buttons

-- the bits of pad()
local LEFT, RIGHT, UP, DOWN = 1, 2, 4, 8
local BA, BB, START, SELECT, BX, BY = 16, 32, 64, 128, 256, 512
local L1, R1, L2, R2 = 1024, 2048, 4096, 8192
local DIRS = LEFT | RIGHT | UP | DOWN
local FACE = BA | BB | BX | BY
local BANKS = L2 | R2
P.BITS = { LEFT = LEFT, RIGHT = RIGHT, UP = UP, DOWN = DOWN, A = BA, B = BB, X = BX, Y = BY,
           START = START, SELECT = SELECT, L1 = L1, R1 = R1, L2 = L2, R2 = R2 }

P.C_GHOST = 0x6C8CC8          -- the suggestion, not written yet
P.C_PRED = 0x50E0B0           -- what the suggestion has just written
P.C_PEND = 0xFFC050           -- the chord being pressed

------------------------------------------------------------------ the tables

-- the consonant of a direction, by bank: none, L2, R2, L2+R2, from the
-- most written to the least (the more buttons, the rarer the letter); L2
-- gives the voiced twin of t, c, p (d, g, b). The diagonals: alone the
-- letters of English and code (x y w k), with a bank the groups of steno.
local ONSET = {
  [UP] = { "t", "d", "s", "z" },
  [RIGHT] = { "n", "l", "r", "m" },
  [DOWN] = { "c", "g", "h", "q" },
  [LEFT] = { "p", "b", "v", "f" },
  [UP | RIGHT] = { "x", "tr", "st", "str" },
  [RIGHT | DOWN] = { "w", "ch", "gn", "gl" },
  [DOWN | LEFT] = { "k", "sc", "sp", "pi" },
  [LEFT | UP] = { "y", "pr", "br", "gr" },
}
local function bank_of(b)
  local k = b & BANKS
  return k == 0 and 1 or k == L2 and 2 or k == R2 and 3 or 4
end
-- the vowels: triangle a (its shape), square e, cross i, circle o; cross
-- and circle together u. Steno: the diphthongs on the other pairs.
local NUCLEUS = { [BY] = "a", [BX] = "e", [BA] = "i", [BB] = "o", [BA | BB] = "u",
                  [BY | BX] = "ia", [BY | BB] = "io", [BA | BX] = "ie" }
local STENO_ONLY = { [BY | BX] = true, [BY | BB] = true, [BA | BX] = true }
-- a button alone with a bank: punctuation
local PUNCT = {
  [L2] = { [BY] = ".", [BX] = ",", [BA] = "'", [BB] = "?" },
  [R2] = { [BY] = "!", [BX] = ":", [BA] = "-", [BB] = '"' },
  [L2 | R2] = { [BY] = "(", [BX] = ")", [BA] = "=", [BB] = ";" },
}
-- Select (Share) held with the buttons: numbers and symbols
local SYMBOL = {
  [0] = { [BY] = "1", [BB] = "2", [BA] = "3", [BX] = "4", [BA | BB] = "j" },
  [L2] = { [BY] = "5", [BB] = "6", [BA] = "7", [BX] = "8" },
  [R2] = { [BY] = "9", [BB] = "0", [BA] = "+", [BX] = "*" },
  [L2 | R2] = { [BY] = "/", [BB] = "<", [BA] = ">", [BX] = "#" },
  [L1] = { [BY] = "[", [BB] = "]", [BA] = "{", [BX] = "}" },
  [R1] = { [BY] = "_", [BB] = "~", [BA] = "%", [BX] = "&" },
  [L1 | R1] = { [BY] = "|", [BB] = "^", [BA] = "@", [BX] = "\\" },
}
P.ONSET, P.NUCLEUS, P.PUNCT, P.SYMBOL = ONSET, NUCLEUS, PUNCT, SYMBOL

-- the accents, in code page 437: a press of the sign goes round them
local ACCENT = { a = "\133", ["\133"] = "a", e = "\138", ["\138"] = "\130", ["\130"] = "e",
                 i = "\141", ["\141"] = "i", o = "\149", ["\149"] = "o", u = "\151", ["\151"] = "u",
                 E = "\144", ["\144"] = "E" }
local PLAIN = { ["\133"] = "a", ["\138"] = "e", ["\130"] = "e", ["\141"] = "i", ["\149"] = "o",
                ["\151"] = "u", ["\160"] = "a", ["\161"] = "i", ["\162"] = "o", ["\163"] = "u",
                ["\144"] = "e" }
local LETTER = "[%a\128-\165]"

local function plain(s)
  s = s:lower()
  if s:find("[\128-\255]") then s = s:gsub("[\128-\255]", PLAIN) end
  return s
end
P.plain = plain

------------------------------------------------------------------ state

local st = {
  on = false, mode = "sillabe",
  held = 0, chord = 0, dirs = 0, used = false, hold = 0,
  caps = nil,                   -- nil: automatic, true / false: the next letter
  autospace = false,            -- the last character is the space of a suggestion
  lastpick = nil,               -- {prefix, text}: L1 right after takes it back
  cands = {}, prefix = "", flashlen = 0, flashoff = 0,
  bad = 0, presses = 0, lang = "it",
}

function P.on(v)
  if v == nil then v = true end
  st.on = v
  st.held, st.chord, st.dirs, st.used = 0, 0, 0, false
  st.caps, st.lastpick, st.flashlen = nil, nil, 0
  st.skip = true                -- the buttons held now (Share) write nothing
end
-- after other keys (a menu): nothing until every button is up
function P.wait() st.skip, st.chord, st.pending = true, 0, nil end
function P.is_on() return st.on end
function P.set(o)
  if o.mode then st.mode = o.mode end
end
function P.mode() return st.mode end
function P.presses() return st.presses end
function P.reset_count() st.presses = 0 end

------------------------------------------------------------------ chords

local function bits_count(v)
  local n = 0
  while v ~= 0 do n = n + (v & 1); v = v >> 1 end
  return n
end

-- the text of a letter chord, or nil
local function letters(dirs, b, mode)
  local face = b & FACE
  local on = ""
  if dirs ~= 0 then
    local row = ONSET[dirs]
    if not row then return nil end
    on = row[bank_of(b)]
    if bits_count(dirs) == 2 and bank_of(b) > 1 and mode ~= "steno" then return nil end
  end
  local v = ""
  if face ~= 0 then
    v = NUCLEUS[face]
    if not v or (STENO_ONLY[face] and mode ~= "steno") then return nil end
  end
  if on == "q" and v ~= "" then on = v == "u" and "q" or "qu" end
  if b & L1 ~= 0 and on ~= "" then on = (on:sub(1, 1) == "q" and "c" or on:sub(1, 1)) .. on end
  return on .. v
end

-- what a chord does: {kind, s, space}; nil if it means nothing
function P.decode(b, dirs, mode)
  mode = mode or st.mode
  dirs = dirs or (b & DIRS)
  local face, sh = b & FACE, b & (L1 | R1 | L2 | R2)
  if b & SELECT ~= 0 then
    if b == SELECT then return { kind = "exit" } end
    if b & (DIRS | START) ~= 0 then return nil end
    local t = SYMBOL[sh]
    local s = t and t[face]
    return s and { kind = "sym", s = s } or nil
  end
  if b & START ~= 0 then
    return b == START and { kind = "newline" } or nil
  end
  local space = b & R1 ~= 0
  if dirs == 0 and face == 0 then
    if sh == L1 then return { kind = "erase" }
    elseif sh == R1 then return { kind = "space" }
    elseif sh == R2 then return { kind = "pick", n = 1 }
    elseif sh == L2 then return { kind = "pick", n = 2 }
    elseif sh == L2 | R2 then return { kind = "pick", n = 3 }
    elseif sh == L1 | L2 then return { kind = "accent" }
    elseif sh == L1 | L2 | R1 then return { kind = "accent", space = true }
    elseif sh == L1 | R1 then return { kind = "caps" } end
    return nil
  end
  if dirs == 0 and b & BANKS ~= 0 then
    if b & L1 ~= 0 then return nil end
    local s = PUNCT[b & BANKS][face]
    return s and { kind = "punct", s = s, space = space } or nil
  end
  local s = letters(dirs, b, mode)
  return s and { kind = "text", s = s, space = space } or nil
end

------------------------------------------------------------------ the words

local dicts = {}

-- a language's dictionary, read the first time it is needed
local function dict(lang)
  local d = dicts[lang]
  if d then return d end
  local src = (require "padwords")[lang]
  d = { words = {}, keys = {}, count = {}, func = {}, big = {}, N = 0, api = {} }
  local i = 0
  for w, c, f in src.uni:gmatch("(%S+) (%d+)( ?f?)\n") do
    i = i + 1
    d.words[i], d.keys[i], d.count[i] = w, plain(w), tonumber(c)
    if f ~= "" then d.func[w] = true end
    d.N = d.N + tonumber(c)
  end
  for line in src.big:gmatch("[^\n]+") do
    local prev, total, rest = line:match("^(%S+) (%d+) (.*)$")
    local t = { total = tonumber(total) }
    for w, c in rest:gmatch("(%S+) (%d+)") do
      t[#t + 1] = w
      t[w] = tonumber(c)
    end
    d.big[prev] = t
  end
  if src.api then
    for name, sig in src.api:gmatch("(%S+) ([^\n]+)\n") do d.api[name] = sig end
  end
  dicts[lang] = d
  return d
end
P.dict = dict

-- another dictionary (the benchmark leaves texts out of it)
function P.reset_words() dicts = {} end

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
-- that ends), from the text before the cursor
local function word_at(before, lang)
  if lang == "lua" then
    local prefix = before:match("[%a_][%w_%.]*$") or ""
    local rest = before:sub(1, #before - #prefix)
    if rest:match("[%w_%.]$") then return nil end        -- inside a number
    local prev = rest:match("([%a_][%w_%.]*)[^%w_%.]*$")
    return prefix, prev or "^"
  end
  local prefix = before:match(LETTER .. "*$")
  local rest = before:sub(1, #before - #prefix)
  local prev, gap = rest:match("(" .. LETTER .. "+'?)([^%a\128-\165]*)$")
  if not prev or gap:find("[%.%?!]") then return prefix, "^" end
  return prefix, prev:lower()
end
P.word_at = word_at

-- the scores of one dictionary for a prefix after prev, added with weight w
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
  local keys = d.keys
  local i = lower_bound(keys, key)
  while i <= #keys and keys[i]:sub(1, #key) == key do
    local x = d.words[i]
    if x ~= same then
      local c = d.count[i]
      local sc = 0.4 * (c > 0 and c or 0.5) / d.N
      if bg and bg[x] then sc = sc + bg[x] / bg.total end
      add(x, w * sc)
    end
    i = i + 1
  end
end

-- The suggestions for a prefix after prev: up to n words, best first.
-- lang: a dictionary ("it", "en", "lua", "ask") or a mix of them with their
-- weights ({it = 1, ask = 1}: a question to the assistant); buf: the names
-- of the code being written ({name = count, [1] = total}), with weight bufw.
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
  return out
end
P.candidates = candidates

-- the identifiers of some code, counted (the suggestions of bm Code);
-- [1] is their total
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

-- a suggestion as it is written: the case of what is typed, its ending
local function shaped(w, prefix, lang, caps_first)
  local ending = ""
  if lang == "lua" then
    local d = dict("lua")
    if d.func[w] then ending = "(" elseif KEYWORD_SPACE[w] then ending = " " end
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

local function sentence_start(before)
  local t = before:gsub("[%s\"%(]+$", "")
  return t == "" or t:find("[%.%?!]$") ~= nil
end

------------------------------------------------------------------ editing

-- the suggestions where the host is writing: host.lang is the dictionary
-- (or a mix, see candidates; "none": no suggestions), host.name its name
-- for the panel, host.words and host.words_weight the names of the code
local function refresh(host)
  st.cands, st.prefix = {}, ""
  local lang = host.lang or "it"
  st.lang = host.name or (type(lang) == "string" and lang) or "mix"
  st.code = lang == "lua"
  if lang == "none" then return end
  local before = host.before()
  local prefix, prev = word_at(before, lang)
  if not prefix then return end
  st.prefix, st.prev = prefix, prev
  local ok, list = pcall(candidates, lang, prefix, prev, 3, host.words, host.words_weight)
  if ok then st.cands = list end
end
P.refresh = refresh

local function caps_first(host, before)
  if st.caps ~= nil then return st.caps end
  return host.prose and sentence_start(before)
end

local function insert(host, s, auto)
  host.insert(s)
  st.autospace = auto or false
end

-- a decoded chord, done on the host
local function act(a, host)
  local before = host.before()
  local k = a.kind
  local pick = st.lastpick
  st.lastpick, st.flashlen, st.picked = nil, 0, nil
  if k == "exit" then
    P.on(false)
    if host.exit then host.exit() end
    return
  elseif k == "erase" then
    if pick then                                -- a suggestion taken back
      host.erase(#pick.text)
      host.insert(pick.prefix)
    else
      host.erase(1)
    end
    st.autospace, st.caps = false, nil
  elseif k == "space" then
    insert(host, " ")
  elseif k == "newline" then
    if st.autospace and before:sub(-1) == " " then host.erase(1) end
    host.newline()
    st.autospace, st.caps = false, nil
  elseif k == "text" then
    local s = a.s
    local up = caps_first(host, before)
    if up then s = s:sub(1, 1):upper() .. s:sub(2) end
    st.caps = nil
    insert(host, s .. (a.space and " " or ""))
  elseif k == "punct" or k == "sym" then
    local s = a.s
    if st.autospace and before:sub(-1) == " " and s:find("^[,%.;:!%?']$") then
      host.erase(1)                             -- "casa ," -> "casa, "
      insert(host, s)
      if s ~= "'" then insert(host, " ", true) end
    else
      insert(host, s .. (a.space and " " or ""))
    end
  elseif k == "accent" then                      -- the vowel just written
    local last = before:sub(-1)
    if before ~= "" and ACCENT[last] then
      host.erase(1)
      insert(host, ACCENT[last])
    else
      st.bad = 20
    end
    if a.space then insert(host, " ") end
  elseif k == "caps" then                        -- the next letter: capital or not
    st.caps = not caps_first(host, before)
  elseif k == "pick" then
    local w = st.cands[a.n]
    if not w then st.bad = 20; return end
    local word, ending = shaped(w, st.prefix, host.lang or "it", caps_first(host, before:sub(1, #before - #st.prefix)))
    host.erase(#st.prefix)
    host.insert(word .. ending)
    st.lastpick = { prefix = st.prefix, text = word .. ending }
    st.autospace = ending == " "
    st.flashlen, st.flashoff = #word - #st.prefix, #ending
    st.caps, st.picked = nil, w
    if host.on_pick then host.on_pick(w) end
  end
end

-- the text a chord would write (the panel shows it while it is held)
local function preview(a)
  if not a then return nil end
  if a.kind == "text" or a.kind == "punct" or a.kind == "sym" then
    return a.s .. (a.space and "_" or "")
  elseif a.kind == "space" then return "_"
  elseif a.kind == "erase" then return "<-"
  elseif a.kind == "pick" then return st.cands[a.n] or "-"
  elseif a.kind == "accent" then return "`" .. (a.space and "_" or "")
  elseif a.kind == "caps" then return "Abc"
  elseif a.kind == "newline" then return "Enter"
  elseif a.kind == "exit" then return "exit" end
end

-- each frame: the buttons held, a chord written when they are all up again
function P.update(host)
  if not st.on then return false end
  local b = pad()
  local down = b & ~st.held
  st.held = b
  if st.skip then
    if b == 0 then st.skip = false end
    return true
  end
  if st.bad > 0 then st.bad = st.bad - 1 end
  if b ~= 0 then
    if st.chord == 0 then st.dirs, st.used, st.hold = 0, false, 0 end
    st.chord = st.chord | b
    st.hold = st.hold + 1
    local d = b & DIRS
    if d ~= 0 and (bits_count(d) == 2 or bits_count(st.dirs) < 2) then
      if not (d == LEFT | RIGHT or d == UP | DOWN or bits_count(d) > 2) then st.dirs = d end
    end
    -- Start + cross moves the cursor (and repeats), Start + L1 undoes
    if b & START ~= 0 and b & SELECT == 0 then
      local mv = (st.hold > 18 and st.hold % 3 == 0) and (b & DIRS) or (down & DIRS)
      for bit, name in pairs({ [LEFT] = "left", [RIGHT] = "right", [UP] = "up", [DOWN] = "down" }) do
        if mv & bit ~= 0 and host.move then host.move(name); st.used = true end
      end
      if down & L1 ~= 0 and host.undo then host.undo(); st.used = true end
      if st.used then st.lastpick, st.autospace, st.flashlen = nil, false, 0; refresh(host) end
    end
    -- L1 held alone: erases again and again
    if st.chord == L1 and st.hold > 24 and st.hold % 4 == 0 then
      host.erase(1)
      st.used, st.lastpick = true, nil
      refresh(host)
    end
    local a = P.decode(st.chord, st.dirs)
    st.pending = preview(a)
    return true
  end
  if st.chord ~= 0 then
    local c, dirs = st.chord, st.dirs
    st.chord, st.pending = 0, nil
    if not st.used then
      local a = P.decode(c, dirs)
      if a then
        st.presses = st.presses + 1
        act(a, host)
        if not st.on then return true end
      else
        st.bad = 20
      end
      refresh(host)
    end
  end
  return true
end

-- the rest of the first suggestion, for the line being written
function P.ghost()
  local w = st.cands[1]
  if not st.on or not w then return nil end
  return w:sub(#st.prefix + 1)
end
function P.suggestions() return st.cands, st.prefix end
function P.flash()
  if st.flashlen > 0 then return st.flashlen, st.flashoff end
end
function P.pending() return st.pending end

------------------------------------------------------------------ drawing

local C_BG, C_LINE, C_BAR = 0x14161E, 0x343B54, 0x22273A
local C_TEXT, C_DIM, C_ACC, C_ERR, C_OK = 0xE0E4F0, 0x6A7290, 0xFFC050, 0xFF6464, 0x70E090
local C_ON = 0xFFC050

-- a small pad: shoulders, cross and buttons, the bits of b lit
function P.chord_icon(b, x, y)
  local function lit(bit) return b & bit ~= 0 and C_ON or C_LINE end
  rectfill(x, y, 9, 4, lit(L2)); rectfill(x + 11, y, 9, 4, lit(L1))
  rectfill(x + 30, y, 9, 4, lit(R1)); rectfill(x + 41, y, 9, 4, lit(R2))
  local cx, cy = x + 10, y + 15
  rectfill(cx - 2, cy - 8, 5, 5, lit(UP)); rectfill(cx - 2, cy + 4, 5, 5, lit(DOWN))
  rectfill(cx - 8, cy - 2, 5, 5, lit(LEFT)); rectfill(cx + 4, cy - 2, 5, 5, lit(RIGHT))
  local fx = x + 40
  circfill(fx, cy - 6, 2, lit(BY)); circfill(fx, cy + 6, 2, lit(BA))
  circfill(fx - 6, cy, 2, lit(BX)); circfill(fx + 6, cy, 2, lit(BB))
  if b & SELECT ~= 0 then rectfill(x + 20, y + 8, 4, 3, C_ON) end
  if b & START ~= 0 then rectfill(x + 27, y + 8, 4, 3, C_ON) end
end

-- the four shapes of the buttons
local function shape(which, x, y, c)
  if which == BY then
    line(x + 3, y, x, y + 6, c); line(x + 3, y, x + 6, y + 6, c); line(x, y + 6, x + 6, y + 6, c)
  elseif which == BB then circ(x + 3, y + 3, 3, c)
  elseif which == BA then line(x, y, x + 6, y + 6, c); line(x + 6, y, x, y + 6, c)
  elseif which == BX then rect(x, y, 7, 7, c) end
end

local W_COLS, H_ROWS = 46, 7
function P.size()
  local cw, ch = font()
  return W_COLS * cw, H_ROWS * ch
end

-- what each button would write now, with the buttons held
local function labels()
  local b = st.held
  local sh = b & (L1 | R1 | L2 | R2)
  local dl, fl = {}, {}
  if b & SELECT ~= 0 then
    local t = SYMBOL[sh] or {}
    for _, f in ipairs({ BY, BX, BA, BB }) do fl[f] = t[f] or "" end
    return dl, fl, "Share + buttons: 1-8, with L2 R2 L1 R1 more"
  end
  if b & START ~= 0 then
    dl = { [UP] = "up", [DOWN] = "dn", [LEFT] = "<", [RIGHT] = ">" }
    return dl, fl, "Start + cross: move   Start + L1: undo"
  end
  local bank = bank_of(b)
  for dir, row in pairs(ONSET) do
    if bits_count(dir) == 1 or bank == 1 or st.mode == "steno" then dl[dir] = row[bank] end
  end
  local dirs = st.chord ~= 0 and st.dirs or (b & DIRS)
  for f, v in pairs(NUCLEUS) do
    if not STENO_ONLY[f] or st.mode == "steno" then
      if dirs ~= 0 then fl[f] = letters(dirs, (b & (BANKS | L1)) | f, st.mode) or ""
      elseif b & BANKS ~= 0 then fl[f] = (PUNCT[b & BANKS] or {})[f] or ""
      else fl[f] = v end
    end
  end
  return dl, fl
end

-- the panel: the cross and the buttons with what they write now (it
-- changes with L2, R2, Share, Start held), the suggestions, the chord
-- being pressed. On a grid of the font's cells. hint: the bits of a
-- chord to light up (the practice's next press).
function P.draw(x, y, hint)
  hint = st.held == 0 and hint or 0
  local cw, ch = font()
  local w, h = W_COLS * cw, H_ROWS * ch
  rectfill(x, y, w, h, C_BG)
  rect(x, y, w, h, C_LINE)
  rectfill(x, y, w, ch, C_BAR)
  print("PAD " .. st.mode .. " " .. (st.lang or ""), x + cw, y, C_ACC)
  print("Share: off", x + w - 11 * cw, y, C_DIM)
  local dl, fl, note = labels()
  local hdir = hint & DIRS
  local function hl(bit, c) return (hint ~= 0 and (bit == hdir or (bit & FACE ~= 0 and hint & bit ~= 0))) and P.C_PEND or c end
  if hint & BANKS ~= 0 then                    -- the bank of the hint: its letters
    for dir, row in pairs(ONSET) do dl[dir] = row[bank_of(hint)] end
  end
  local function at(t, col, row, c, left)
    if t and t ~= "" then
      if not left then col = col - #t // 2 end
      print(t, x + col * cw, y + row * ch, c or C_TEXT)
    end
  end
  -- the cross: its diagonals at the corners
  at(dl[UP], 7, 1, hl(UP)); at(dl[DOWN], 7, 3, hl(DOWN))
  at(dl[LEFT], 3, 2, hl(LEFT)); at(dl[RIGHT], 11, 2, hl(RIGHT))
  at(dl[UP | LEFT], 3, 1, hl(UP | LEFT, C_DIM)); at(dl[UP | RIGHT], 11, 1, hl(UP | RIGHT, C_DIM))
  at(dl[DOWN | LEFT], 3, 3, hl(DOWN | LEFT, C_DIM)); at(dl[DOWN | RIGHT], 11, 3, hl(DOWN | RIGHT, C_DIM))
  local cx, cy = x + 7 * cw + cw // 2, y + 2 * ch + ch // 2
  rectfill(cx - 1, cy - 4, 3, 9, C_LINE); rectfill(cx - 4, cy - 1, 9, 3, C_LINE)
  -- the buttons, their shape before what they write
  local pos = { [BY] = { 22, 1 }, [BA] = { 22, 3 }, [BX] = { 17, 2 }, [BB] = { 27, 2 } }
  for f, p in pairs(pos) do
    local held = st.held & f ~= 0
    shape(f, x + (p[1] - 2) * cw, y + p[2] * ch + 2, held and C_ON or hl(f, C_DIM))
    at(fl[f], p[1], p[2], held and C_ON or hl(f, C_TEXT), true)
  end
  if fl[BA | BB] and fl[BA | BB] ~= "" then
    shape(BA, x + 26 * cw, y + 3 * ch + 2, C_DIM); shape(BB, x + 27 * cw + 2, y + 3 * ch + 2, C_DIM)
    at(fl[BA | BB], 29, 3, C_TEXT, true)
  end
  -- the suggestions: R2, L2, L2+R2
  local keys = { "R2", "L2", "LR" }
  for i = 1, 3 do
    local s = st.cands[i]
    if s then
      at(keys[i], 33, i, C_DIM, true)
      at(s:sub(1, 10), 36, i, i == 1 and P.C_GHOST or C_TEXT, true)
    end
  end
  -- the chord being pressed, or the banks
  if note then
    at(note, 1, 5, C_DIM, true)
  elseif st.pending then
    P.chord_icon(st.chord, x + cw, y + 4 * ch + 4)
    at("> " .. st.pending, 11, 5, st.bad > 0 and C_ERR or P.C_PEND, true)
  elseif st.bad > 0 then
    at("that chord means nothing", 1, 5, C_ERR, true)
  elseif hint ~= 0 then
    P.chord_icon(hint, x + cw, y + 4 * ch + 4)
    at("next: " .. P.describe(hint), 11, 5, P.C_PEND, true)
  else
    -- code: the API being written, as the assistant's knowledge base has it
    local d = st.code and dicts.lua
    local sig = d and (d.api[st.picked or ""] or d.api[st.cands[1] or ""])
    if sig then at(sig:sub(1, W_COLS - 2), 1, 4, 0x70D0FF, true); return end
    local row = function(bank) return ONSET[UP][bank] .. ONSET[RIGHT][bank] .. ONSET[DOWN][bank] .. ONSET[LEFT][bank] end
    at("L2 " .. row(2) .. "  R2 " .. row(3) .. "  L2+R2 " .. row(4) .. "  L1 double", 1, 4, C_DIM, true)
    at("L1 del  R1 space  L1+L2 accent  L1+R1 Caps", 1, 5, C_DIM, true)
  end
end

------------------------------------------------------------------ practice

-- the texts of the benchmark: 100 characters, 100 presses on a keyboard
P.TEXTS = {
  { name = "italiano", lang = "it",
    text = "Ciao Marco, domani sera giochiamo da me? La console nuova \138 arrivata: tu porta la pizza e le bibite!" },
  { name = "english", lang = "en",
    text = "Hi Mark, do we play at my place tomorrow night? The new console is here: you bring pizza and drinks!" },
  { name = "lua", lang = "lua",
    text = "function _update()\n  if btn(0) then x = x - 2 end\n  if btn(1) then x = x + 2 end\n  t = t + 0.125\nend" },
}

local OPENERS = { "then%s*$", "do%s*$", "else%s*$", "repeat%s*$", "{%s*$", "%(%s*$",
                  "function%s*[%w_.:]*%s*%(.-%)%s*$" }
local DEDENT = { ["end"] = true, ["else"] = true, ["elseif"] = true, ["until"] = true }

-- a host that is one text, with the indentation of bm Code for code
function P.text_host(lang)
  local h = { text = "", lang = lang, prose = lang ~= "lua" }
  local function cur() return h.text:match("[^\n]*$") end
  function h.before() return cur() end
  function h.insert(s)
    h.text = h.text .. s
    local l = cur()
    local ind, w = l:match("^( +)(%a+)$")
    if lang == "lua" and w and DEDENT[w] then h.text = h.text:sub(1, #h.text - #l) .. l:sub(3) end
  end
  function h.erase(n) h.text = h.text:sub(1, #h.text - n) end
  function h.newline()
    local l = cur()
    local ind = l:match("^ *")
    if lang == "lua" then
      local code = l:gsub("%-%-.*$", "")
      for _, op in ipairs(OPENERS) do
        if code:find(op) and not code:find("end%s*$") then ind = ind .. "  "; break end
      end
    else
      ind = ""
    end
    h.text = h.text .. "\n" .. ind
  end
  function h.move() end
  function h.undo() end
  return h
end

local pr                                -- the practice, when open

-- how much of target typed has written: the spaces at the start of a line
-- (the editor's indentation) do not count; nil if typed went elsewhere
local function written_of(typed, target)
  local i, j = 1, 1
  local bol = true
  while i <= #typed do
    if bol then
      while typed:sub(i, i) == " " do i = i + 1 end
      while target:sub(j, j) == " " do j = j + 1 end
      if i > #typed then break end
    end
    if typed:sub(i, i) ~= target:sub(j, j) then return nil, j - 1 end
    bol = typed:sub(i, i) == "\n"
    i, j = i + 1, j + 1
  end
  return j - 1, j - 1
end

local function guide()
  local h, target = pr.host, pr.t.text
  local typed, cut = h.text, false
  pr.next, pr.left = nil, nil
  local n = written_of(typed, target)
  if not n and st.autospace and typed:sub(-1) == " " then
    n, cut = written_of(typed:sub(1, -2), target), true   -- the space of a suggestion, before punctuation
  end
  pr.same = n or select(2, written_of(typed, target))
  if not n then                               -- a vowel waiting for its accent
    local m = written_of(typed:sub(1, -2), target)
    local want, have = m and target:sub(m + 1, m + 1), typed:sub(-1)
    if want and PLAIN[want] and (have == PLAIN[want] or PLAIN[have] == PLAIN[want]) then
      local presses, c = 0, have
      repeat c = ACCENT[c]; presses = presses + 1 until c == want or presses > 3
      local space = presses == 1 and target:sub(m + 2, m + 2) == " "
      local upto = m + 1 + (space and 1 or 0)
      local _, left = P.encode(target, { mode = st.mode, lang = pr.t.lang, predict = true,
                                         from = target:sub(1, upto), words = h.words })
      pr.next = { bits = L1 | L2 | (space and R1 or 0), kind = "accent", out = space and " " or "" }
      pr.left = presses + left
      return
    end
  end
  if n == #target then
    pr.done = true
  elseif n then
    local steps, left = P.encode(target, { mode = st.mode, lang = pr.t.lang, predict = true, from = target:sub(1, n),
                                           autospace = st.autospace, cut = cut, caps = st.caps, words = h.words })
    pr.next, pr.left = steps[1], left
  end
end

-- the practice: a text of P.TEXTS to write, the presses counted, the next
-- best press shown; Share closes it
function P.practice_open(i)
  local t = P.TEXTS[i]
  pr = { t = t, host = P.text_host(t.lang) }
  pr.host.words = P.count_words({})
  pr.host.exit = function() pr.closed = true end
  local _, best = P.encode(t.text, { mode = st.mode, lang = t.lang, predict = true, words = pr.host.words })
  pr.best = best
  P.on(true)
  st.presses = 0
  refresh(pr.host)
  guide()
end
function P.practice_is_open() return pr ~= nil end
function P.practice_text() return pr and pr.host.text end
function P.practice_hint() return pr and pr.next and pr.next.bits or 0 end
function P.practice_next() return pr and pr.next, pr and pr.done end

function P.practice_update()
  if not pr then return false end
  local n, mode = st.presses, st.mode
  P.update(pr.host)
  if pr.closed then
    pr = nil
    return false
  end
  if st.presses ~= n or st.mode ~= mode then guide() end
  return true
end

-- text cut in lines of n columns (and at its new lines)
local function wrap(text, n)
  local out = {}
  for l in (text .. "\n"):gmatch("(.-)\n") do
    repeat
      out[#out + 1] = l:sub(1, n)
      l = l:sub(n + 1)
    until l == ""
  end
  return out
end

function P.practice_draw()
  if not pr then return end
  local cw, ch = font()
  local cols = SCREEN_W // cw - 2
  local x0, y0 = cw, ch
  local w, rows = cols * cw, math.min(15, SCREEN_H // ch - H_ROWS - 3)   -- the panel below
  rectfill(x0, y0, w, rows * ch, C_BG)
  rect(x0, y0, w, rows * ch, C_LINE)
  rectfill(x0, y0, w, ch, C_BAR)
  print("Pad practice: " .. pr.t.name .. " (" .. st.mode .. ")", x0 + cw, y0, C_ACC)
  print("Share: close", x0 + w - 13 * cw, y0, C_DIM)
  local typed = pr.host.text
  local target = pr.t.text
  local same = pr.same or 0
  -- the text to write: green as far as it is written
  local y, left = y0 + 2 * ch, same
  for _, l in ipairs(wrap(target, cols - 2)) do
    local a = math.max(0, math.min(#l, left))
    print(l:sub(1, a), x0 + cw, y, C_OK)
    print(l:sub(a + 1), x0 + cw + a * cw, y, C_DIM)
    left = left - #l - 1
    y = y + ch
  end
  -- what is written, the suggestion after it
  y = y + ch
  local lines = wrap(typed, cols - 2)
  for i, l in ipairs(lines) do
    print(l, x0 + cw, y, (pr.next or pr.done) and C_TEXT or C_ERR)
    if i == #lines then
      local gx = x0 + cw + #l * cw
      local g = P.ghost()
      if g then print(g, gx, y, P.C_GHOST) end
      rectfill(gx, y, 2, ch, C_ACC)
    end
    y = y + ch
  end
  -- the counts and the next press
  y = y0 + (rows - 4) * ch
  local n = st.presses
  local per = #typed > 0 and n / #typed or 0
  print(string.format("presses %d   characters %d   per character %.2f   best %d   keyboard %d",
                      n, #typed, per, pr.best, #target), x0 + cw, y, C_TEXT)
  y = y + 2 * ch
  if pr.done then
    print(string.format("Done: %d presses for %d characters (best %d). Share: close", n, #target, pr.best),
          x0 + cw, y, C_OK)
  elseif pr.next then
    print("next:", x0 + cw, y, C_DIM)
    P.chord_icon(pr.next.bits, x0 + 7 * cw, y - 6)
    local what = pr.next.kind == "pick" and "suggestion" or pr.next.kind == "caps" and "capital" or
                 pr.next.kind == "accent" and "accent" or pr.next.out:gsub("\n", "Enter"):gsub(" ", "_")
    print(P.describe(pr.next.bits) .. "  ->  " .. what .. string.format("   (%d left)", pr.left),
          x0 + 17 * cw, y, P.C_PEND)
  else
    print("not the text: L1 erases", x0 + cw, y, C_ERR)
  end
end

------------------------------------------------------------------ encoding

-- every letter chord of a mode: text -> bits (the fewest buttons)
local chord_cache = {}
local function chords(mode)
  if chord_cache[mode] then return chord_cache[mode] end
  local t = {}
  local function put(s, b)
    if s and s ~= "" and (not t[s] or bits_count(b) < bits_count(t[s])) then t[s] = b end
  end
  local faces = { 0 }
  for f in pairs(NUCLEUS) do faces[#faces + 1] = f end
  for _, bank in ipairs({ 0, L2, R2, L2 | R2 }) do
    for _, f in ipairs(faces) do
      for _, dbl in ipairs({ 0, L1 }) do
        for dir in pairs(ONSET) do
          put(letters(dir, bank | f | dbl, mode), dir | bank | f | dbl)
        end
        if bank == 0 and dbl == 0 then put(letters(0, f, mode), f) end
      end
    end
  end
  -- a trie of the chord texts
  local trie = {}
  for s, b in pairs(t) do
    local n = trie
    for i = 1, #s do
      local c = s:sub(i, i)
      n[c] = n[c] or {}
      n = n[c]
    end
    n["$"] = b
  end
  chord_cache[mode] = { list = t, trie = trie }
  return chord_cache[mode]
end

local function stroke_of(s)            -- punctuation and symbols
  for bank, row in pairs(PUNCT) do
    for f, c in pairs(row) do if c == s then return bank | f end end
  end
  for sh, row in pairs(SYMBOL) do
    for f, c in pairs(row) do if c == s then return SELECT | sh | f end end
  end
  if s == " " then return R1 end
  if s == "\n" then return START end
end

local SIGN_CAPS, SIGN_ACC = L1 | R1, L1 | L2

-- presses of the sign from a plain vowel to an accented one (e -> è -> é)
local function accent_presses(ch)
  local base = PLAIN[ch]
  local n, c = 0, base
  repeat
    c = ACCENT[c]
    n = n + 1
  until c == ch or c == base or n > 3
  return n
end

-- The shortest way to write a word: cost[k] for its first k characters,
-- each chunk one chord, with the signs it needs (a capital before, an
-- accent after); anything else one press of the Share layer or a bank.
local function word_steps(word, mode, caps_auto, start)
  local tr = chords(mode).trie
  local n = #word
  start = start or 0
  local cost, back, info = { [start] = 0 }, {}, {}
  local INF = 1e9
  for p = start, n - 1 do
    if cost[p] then
      local first = word:sub(p + 1, p + 1)
      local up = first:find("%u") ~= nil
      local pre = up ~= ((p == 0 and caps_auto) and true or false)
      local node, q = tr, p
      while q < n do
        local ch = word:sub(q + 1, q + 1)
        if q > p and ch:find("%u") then break end          -- a capital starts a chord
        node = node[plain(ch)]
        if not node then break end
        q = q + 1
        local acc = PLAIN[ch] and accent_presses(ch) or 0
        if node["$"] then
          local v = cost[p] + 1 + (pre and 1 or 0) + acc
          if v < (cost[q] or INF) then
            cost[q], back[q], info[q] = v, p, { bits = node["$"], pre = pre, acc = acc }
          end
        end
        if acc > 0 then break end                         -- an accent ends it
      end
      -- anything else (j too): one press of Share or a bank; 0 if none
      local b = stroke_of(first) or 0
      if cost[p] + 1 < (cost[p + 1] or INF) and (b ~= 0 or not tr[plain(first)]) then
        cost[p + 1], back[p + 1], info[p + 1] = cost[p] + 1, p, { bits = b, sym = true }
      end
    end
  end
  return cost, back, info
end

-- Tokens of a text: words, spaces, new lines (with the indentation that
-- follows, written by the editor), anything else one by one.
local function tokens(text, lang)
  local out = {}
  local i, n = 1, #text
  local wpat = lang == "lua" and "^[%a_][%w_%.]*" or ("^" .. LETTER .. "+'?")
  while i <= n do
    local w = text:match(wpat, i)
    if w and not (lang ~= "lua" and w:sub(1, 1):find("[_]")) then
      out[#out + 1] = { k = "word", s = w }
      i = i + #w
    else
      local c = text:sub(i, i)
      if c == "\n" then
        local ind = text:match("^\n *", i)
        out[#out + 1] = { k = "nl", s = ind }
        i = i + #ind
      elseif c == " " then
        out[#out + 1] = { k = "space", s = " " }
        i = i + 1
      else
        out[#out + 1] = { k = "char", s = c }
        i = i + 1
      end
    end
  end
  return out
end

-- The fewest presses that write text, in mode (sillabe / steno), with the
-- suggestions when o.predict (the ones the console would show at each
-- point): a list of {bits, kind, out} and its length. With o.from (the
-- start of text, already written) only the rest: the guide of the
-- practice; o.autospace: the console has the space of a suggestion at
-- the end (o.cut: left out of o.from, the text has punctuation there),
-- o.caps: the capital already chosen for the next letter (true / false).
function P.encode(text, o)
  o = o or {}
  local mode, lang = o.mode or "sillabe", o.lang or "it"
  local prose = lang ~= "lua"
  local toks = tokens(text, lang)
  local steps = {}
  local function step(bits, kind, out)
    steps[#steps + 1] = { bits = bits, kind = kind, out = out }
  end
  local line = ""                     -- the line the console is writing
  local autospace = false
  local PICK = { R2, L2, L2 | R2 }
  local from, pos = o.from and #o.from or 0, 0
  local caps_now = o.caps
  for ti, tk in ipairs(toks) do
    local nxt = toks[ti + 1]
    local start = from - pos          -- characters of this token already there
    pos = pos + #tk.s
    if start >= #tk.s then            -- written before
      line = tk.k == "nl" and tk.s:sub(2) or line .. tk.s
      autospace = o.autospace and pos == from
      if autospace and o.cut then line = line .. " " end
    elseif tk.k == "word" then
      local word = tk.s
      start = math.max(start, 0)
      local caps_auto = prose and sentence_start(line)
      if caps_now ~= nil and start == 0 then caps_auto = caps_now end
      caps_now = nil
      local cost, back, info = word_steps(word, mode, caps_auto, start)
      local best, how = cost[#word] or 1e9, nil
      if o.predict then
        local _, prev = word_at(line, lang)
        -- a name can be started in lowercase: the suggestion has its capital
        local typed = { { word, cost, back, info } }
        if prose and not caps_auto and word:find("^%u") and start == 0 then
          local low = word:sub(1, 1):lower() .. word:sub(2)
          local c2, b2, i2 = word_steps(low, mode, false)
          typed[2] = { low, c2, b2, i2 }
        end
        for _, ty in ipairs(typed) do
          local tw, tcost = ty[1], ty[2]
          for k = start, #word - 1 do
            if tcost[k] and tcost[k] + 1 < best then
              local prefix = tw:sub(1, k)
              for r, w in ipairs(candidates(lang, prefix, prev or "^", 3, o.words, o.words_weight)) do
                local sw, ending = shaped(w, prefix, lang, caps_auto)
                if sw == word then
                  local after = ending == "" or (nxt and nxt.s:sub(1, #ending) == ending) or
                                (ending == " " and (not nxt or nxt.k == "nl" or
                                                    (nxt.k == "char" and nxt.s:find("^[,%.;:!%?']$") ~= nil)))
                  if after and tcost[k] + 1 < best then
                    best, how = tcost[k] + 1, { k = k, r = r, ending = ending, tw = tw }
                    cost, back, info = ty[2], ty[3], ty[4]
                  end
                end
              end
            end
          end
        end
      end
      local k = how and how.k or #word
      local chunks = {}
      local q = k
      while q > start do
        table.insert(chunks, 1, { p = back[q], q = q, i = info[q] })
        q = back[q]
      end
      local tw = how and how.tw or word
      for _, c in ipairs(chunks) do
        local s = tw:sub(c.p + 1, c.q)
        if c.i.sym then
          step(c.i.bits, "sym", s)
        else
          if c.i.pre then step(SIGN_CAPS, "caps", "") end
          step(c.i.bits, "chord", c.i.acc > 0 and s:sub(1, -2) .. PLAIN[s:sub(-1)] or s)
          for _ = 1, c.i.acc do step(SIGN_ACC, "accent", "") end
        end
      end
      if how then
        step(PICK[how.r], "pick", word:sub(how.k + 1) .. how.ending)
        line = line .. word .. how.ending
        autospace = how.ending == " "
        if how.ending ~= "" and nxt and nxt.s:sub(1, #how.ending) == how.ending then nxt.done = true end
      else
        line = line .. word
        autospace = false
      end
    elseif tk.done then
      -- written by the suggestion
    elseif tk.k == "space" then
      local last = steps[#steps]
      if last and (last.kind == "chord" or last.kind == "accent" or last.kind == "punct")
         and last.bits & R1 == 0 then
        last.bits, last.out = last.bits | R1, last.out .. " "
      else
        step(R1, "space", " ")
      end
      line = line .. " "
      autospace = false
    elseif tk.k == "nl" then
      step(START, "newline", "\n")
      line = tk.s:sub(2)
      autospace = false
    else
      local c = tk.s
      local b = stroke_of(c) or 0
      local kind = b & SELECT ~= 0 and "sym" or "punct"
      step(b, kind, c)
      if autospace and c:find("^[,%.;:!%?']$") then
        line = line:sub(1, -2) .. c
        if c ~= "'" then
          line = line .. " "
          if nxt and nxt.k == "space" then nxt.done = true end
          autospace = true
        else
          autospace = false
        end
      else
        line = line .. c
        autospace = false
      end
    end
  end
  return steps, #steps
end

-- a press in words: "L2+down+triangle"
local NAMES = { { L1, "L1" }, { L2, "L2" }, { R1, "R1" }, { R2, "R2" }, { SELECT, "Share" }, { START, "Start" },
                { UP, "up" }, { DOWN, "down" }, { LEFT, "left" }, { RIGHT, "right" },
                { BY, "triangle" }, { BX, "square" }, { BA, "cross" }, { BB, "circle" } }
function P.describe(b, short)
  local t = {}
  for _, nb in ipairs(NAMES) do
    if b & nb[1] ~= 0 then t[#t + 1] = nb[2] end
  end
  return table.concat(t, "+")
end

return P
