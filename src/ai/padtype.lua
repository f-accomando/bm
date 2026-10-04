-- Typing with the pad (assistive typing): Italian, English and Lua with a
-- controller, two ways.
--   compose   the cross writes consonants and the prediction finishes the
--             syllable ("t" -> "te"); square and triangle turn it to the
--             other syllables (te -> ta -> to... and back). A press waits
--             a moment (P.set{delay}) for its double: up = t, up up quickly
--             = d, its alternative, never "tt". L2, R2 and L2+R2 are three
--             more layers of the cross (and of the four buttons:
--             punctuation, the three suggested words, numbers and code)
--   keyboard  an on-screen keyboard, a key at a time with the cross
-- Share switches between them (or turns compose off: fallback "off"); R3
-- opens the keyboard for one character. L1 and R1 move back and forward,
-- L1 + R1 held is a new line; cross = space, cross twice = a full stop,
-- circle erases what was last written. An overlay of the controller shows
-- what every button writes. Words: require "predict" (its dictionaries).
-- Guide: docs/PADTYPE.md.
--
--   local pt = require "padtype"
--   pt.set{ delay = 0.25, fallback = "keyboard" }   -- or "off"
--   pt.on("compose"), pt.on(nil), pt.mode()
--   pt.update(host, [bits])   each frame: reads pad() and edits the host:
--     host.before()           the text before the cursor, on its line
--     host.insert(s), host.erase(n), host.newline(), host.move(dir)
--     host.lang               "it", "en", "lua", "ask", a mix like
--                             {it = 1, ask = 2}, or "none"
--     host.prose              capitals at the start of a sentence
--                             (default: not for "lua")
--     host.words              code: the names of the buffer {name = count}
--     host.now()              the clock (default time())
--   pt.draw(x, y)             the overlay, pt.size() wide and high
--   pt.ghost()                the rest of the first suggestion (C_GHOST)
--   pt.flash()                chars the last suggestion wrote (C_PRED)
--   pt.open_len()             chars of the syllable still turning (C_OPEN)
--   pt.pending()              the press waiting for its double (C_PEND)
--   pt.coach(host, target)    the next press to write target (practice)
--   pt.text_host(lang)        a host over a string (practice, tests)

local predict = require "predict"

local P = {}

P.C_GHOST = predict.C_GHOST   -- the suggestion, not written yet
P.C_PRED = predict.C_PRED     -- what the suggestion has just written
P.C_PEND = 0xFFC050           -- a press waiting for its double
P.C_OPEN = 0x8FD0FF           -- the syllable that square / triangle turn

------------------------------------------------------------------ buttons

-- the bits of pad(): A is the cross of a DS4, B the circle, X the square,
-- Y the triangle (the same places on every pad)
local LEFT, RIGHT, UP, DOWN = 1, 2, 4, 8
local BA, BB, START, SELECT, BX, BY = 16, 32, 64, 128, 256, 512
local L1, R1, L2, R2, L3, R3 = 1024, 2048, 4096, 8192, 16384, 32768
local DIRS = LEFT | RIGHT | UP | DOWN
local ONE_DIR = { [LEFT] = "left", [RIGHT] = "right", [UP] = "up", [DOWN] = "down" }
local FACES = { BA, BB, BX, BY }
local LAYER = { [0] = 1, [L2] = 2, [R2] = 3, [L2 | R2] = 4 }
local LAYER_BITS = { 0, L2, R2, L2 | R2 }
P.BITS = { LEFT = LEFT, RIGHT = RIGHT, UP = UP, DOWN = DOWN, A = BA, B = BB, X = BX, Y = BY,
           START = START, SELECT = SELECT, L1 = L1, R1 = R1, L2 = L2, R2 = R2, L3 = L3, R3 = R3 }

------------------------------------------------------------------ the layers

-- The cross, by layer (none, L2, R2, L2+R2): { one press, two quick
-- presses }. The eight consonants written most (n t r s l c d p are 69%
-- of the syllables of Italian, English and Lua together) need no
-- trigger; the double is the twin of the single: t/d, n/m, r/l, s/c.
-- Every direction keeps its family in the layers: up the stops (t d p b k
-- q), right the nasal and soft ones (n m f v w y), down r l g h j, left
-- the hissing ones (s c z x). L2+R2: the numbers, 1-4 clockwise from up,
-- their doubles 5-8 (0 is the circle, 9 is 8 turned with square).
local CROSS = {
  { [UP] = { "t", "d" }, [RIGHT] = { "n", "m" }, [DOWN] = { "r", "l" }, [LEFT] = { "s", "c" } },
  { [UP] = { "p", "b" }, [RIGHT] = { "f", "v" }, [DOWN] = { "g", "h" }, [LEFT] = { "z", "x" } },
  { [UP] = { "k", "q" }, [RIGHT] = { "w", "y" }, [DOWN] = { "j", "_" }, [LEFT] = { "[", "]" } },
  { [UP] = { "1", "5" }, [RIGHT] = { "2", "6" }, [DOWN] = { "3", "7" }, [LEFT] = { "4", "8" } },
}
-- The four buttons, by layer: actions (names) or characters. None: cross
-- space (twice: a full stop), circle erases, square and triangle turn the
-- syllable. L2: the punctuation of prose. R2: the three suggested words,
-- circle erases a word. L2+R2: code (and the 0).
local FACE = {
  { [BA] = { "space", "period" }, [BB] = { "undo" }, [BX] = { "next" }, [BY] = { "prev" } },
  { [BA] = { ",", ";" }, [BB] = { "?", "!" }, [BX] = { "'", '"' }, [BY] = { ":", "-" } },
  { [BA] = { "word1" }, [BX] = { "word2" }, [BY] = { "word3" }, [BB] = { "delword" } },
  { [BA] = { "(", ")" }, [BB] = { "0" }, [BX] = { "=", "+" }, [BY] = { "{", "}" } },
}
P.CROSS, P.FACE = CROSS, FACE

-- the on-screen keyboard: two pages of four rows, then the special keys
local KB = {
  { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" },
  { "!\"#$%&/()=", "+-*<>[]{}_", "\133\138\130\141\149\151;:@\\", "|^~`\174\175\248\156\241\225" },
}
local KB_SPECIAL = { "shift", "shift", "page", "space", "space", "space", "space", "del", "del", "enter" }
P.KB = KB

-- where each character is: { hold = triggers, tap = button, double }
local KEYOF = {}
for layer = 4, 1, -1 do
  for _, t in ipairs({ CROSS[layer], FACE[layer] }) do
    for btn, def in pairs(t) do
      for k = 1, 2 do
        local c = def[k]
        if c and #c == 1 then KEYOF[c] = { hold = LAYER_BITS[layer], tap = btn, double = k == 2 } end
      end
    end
  end
end

------------------------------------------------------------------ letters

local plain = predict.plain
local VOWEL = {}
for c in ("aeiouyAEIOUY\130\133\138\141\149\151\160\161\162\163\144"):gmatch(".") do VOWEL[c] = true end
local LETTER = "[%a\128-\165]"

-- after a consonant, when the dictionary knows nothing: the consonant on
-- its own, the vowels in order; for Italian then the accented vowels the
-- dictionary did not propose, so that triangle from the start of a
-- syllable gives è (then à, é, ò, ì, ù) when it is not among the first
local TAIL = {
  it = { "", "a", "e", "i", "o", "u", "\151", "\141", "\149", "\130", "\133", "\138" },
  en = { "", "a", "e", "i", "o", "u", "y" },
  lua = { "", "a", "e", "i", "o", "u" },
}
local MAX_DICT = 8              -- syllables from the dictionary, then the tail

local function capital(s)
  local c = s:sub(1, 1)
  if c == "\138" or c == "\130" then c = "\144" else c = c:upper() end
  return c .. s:sub(2)
end

local function langs_of(lang)
  if type(lang) == "table" then
    local t = {}
    for name, w in pairs(lang) do t[#t + 1] = { name, w } end
    table.sort(t, function(a, b) return a[1] < b[1] end)
    return t
  end
  if not lang or lang == "none" then return {} end
  return { { lang, 1 } }
end

-- the language that decides words, capitals and the tail
local function main_lang(lang)
  if type(lang) == "table" then return lang.it and "it" or lang.en and "en" or "it" end
  if lang == "none" or lang == "ask" or not lang then return "it" end
  return lang
end

local function code_of(host) return main_lang(host.lang) == "lua" end
local function prose_of(host)
  if host.prose ~= nil then return host.prose end
  return not code_of(host)
end

local function word_at(before, ml)
  local prefix, prev = predict.word_at(before, ml == "lua" and "lua" or "it")
  if not prefix then return "", "^" end
  return prefix, prev
end

------------------------------------------------------------------ syllables

local function lower_bound(keys, k)
  local lo, hi = 1, #keys + 1
  while lo < hi do
    local mid = (lo + hi) // 2
    if keys[mid] < k then lo = mid + 1 else hi = mid end
  end
  return lo
end

-- What follows the consonant cons in the word w from position i: its
-- double, an h, then the vowels ("tto" of tutto, "he" of che, "iao" of
-- ciao); "" when a consonant follows. A syllable that starts with a vowel
-- (cons "") is its vowels, nil if there are none.
local function cont_of(w, i, cons)
  local c = ""
  if cons ~= "" then
    local l = cons:lower()
    local ch = w:sub(i, i)
    if ch ~= "" and ch:lower() == l then c, i = ch, i + 1; ch = w:sub(i, i) end
    if l ~= "h" and (ch == "h" or ch == "H") then c, i = c .. ch, i + 1 end
  end
  local j = i
  while VOWEL[w:sub(j, j)] do j = j + 1 end
  if cons == "" and j == i then return nil end
  return c .. w:sub(i, j - 1)
end
P.cont_of = cont_of

-- How often each continuation follows key in a dictionary (its words
-- that start with key), cached: the scan is the slow part.
local cache, cache_n = {}, 0
local function base_conts(name, key, cons)
  local ck = name .. "|" .. key .. "|" .. cons
  local t = cache[ck]
  if t then return t end
  local d = predict.dict(name)
  local keys, words, count = d.keys, d.words, d.count
  local low = name ~= "lua"
  t = {}
  local function scan(k)
    local i, n = lower_bound(keys, k), 0
    while i <= #keys and n < 4000 and keys[i]:sub(1, #k) == k do
      local c = cont_of(words[i], #key + 1, cons)
      if c then
        if low then c = c:lower() end
        t[c] = (t[c] or 0) + (count[i] > 0 and count[i] or 0.5) / d.N
      end
      i, n = i + 1, n + 1
    end
  end
  if key == "" then
    for v in ("aeiouy"):gmatch(".") do scan(v) end
  else
    scan(key)
  end
  cache_n = cache_n + 1
  if cache_n > 300 then cache, cache_n = {}, 0 end
  cache[ck] = t
  return t
end

-- The syllables of the consonant cons (or of a vowel, cons "") at the end
-- of before: the dictionary's, the most likely first (how often each
-- word is written, and after the word before), then the tail.
local function syllables(host, before, cons)
  local lang = host.lang or "it"
  local ml = main_lang(lang)
  local prefix, prev = word_at(before, ml)
  local key = plain(prefix .. cons)
  local sc, list = {}, {}
  local function add(c, s)
    if not sc[c] then sc[c] = 0; list[#list + 1] = c end
    sc[c] = sc[c] + s
  end
  for _, lw in ipairs(langs_of(lang)) do
    local name, w = lw[1], lw[2]
    local ok, base = pcall(base_conts, name, key, cons)
    if ok then
      for c, s in pairs(base) do add(c, w * 0.4 * s) end
      local bg = predict.dict(name).big[prev]
      for j = 1, bg and #bg or 0 do
        local x = bg[j]
        if plain(x):sub(1, #key) == key then
          local c = cont_of(x, #key + 1, cons)
          if c then add(name ~= "lua" and c:lower() or c, w * bg[x] / bg.total) end
        end
      end
    end
  end
  if host.words and ml == "lua" and key ~= "" then
    local total = host.words[1] or 1
    for name, n in pairs(host.words) do
      if type(name) == "string" and plain(name):sub(1, #key) == key and #name > #key then
        local c = cont_of(name, #key + 1, cons)
        if c then add(c, 0.6 * n / total) end
      end
    end
  end
  table.sort(list, function(a, b)
    if sc[a] ~= sc[b] then return sc[a] > sc[b] end
    return a < b
  end)
  for k = #list, MAX_DICT + 1, -1 do sc[list[k]] = nil; list[k] = nil end
  for _, c in ipairs(TAIL[ml] or TAIL.it) do
    if not sc[c] and not (cons == "" and c == "") then
      list[#list + 1] = c
      sc[c] = 0
    end
  end
  return list
end
P.syllables = syllables

------------------------------------------------------------------ state

local st

local function reset(keep)
  keep = keep or {}
  st = {
    mode = keep.mode, fallback = keep.fallback or "keyboard", delay = keep.delay or 0.25,
    prev = 0, dir_down = nil, pend = nil,
    units = {}, open = false, soft = false, shift = false,
    words = {}, flash = 0, presses = 0, dirty = true,
    nav = {}, hold_b = nil,
    kb = keep.kb or { page = 1, row = 2, col = 1, shift = false },
    popup = false, preview = {}, host = nil, rep = {},
  }
end
reset()

function P.set(o)
  if o.delay then st.delay = math.max(0.08, math.min(1, o.delay)) end
  if o.fallback then st.fallback = o.fallback end
end
function P.get() return { delay = st.delay, fallback = st.fallback } end
function P.on(mode)
  if mode == true then mode = "compose" end
  st.mode = mode or nil
  st.pend, st.popup, st.dirty = nil, false, true
end
function P.mode() return st.mode end
function P.is_on() return st.mode ~= nil end
function P.presses() return st.presses end
function P.reset_count() st.presses = 0 end
-- a clean start (a new text): no history, no pending press (all: the
-- keyboard's cursor too)
function P.clear(all)
  reset({ mode = st.mode, fallback = st.fallback, delay = st.delay, kb = not all and st.kb or nil })
end
-- the dictionaries, read a slice a frame (true when ready)
function P.preload(langs) return predict.preload(langs) end

------------------------------------------------------------------ editing

local function push(u)
  local t = st.units
  t[#t + 1] = u
  if #t > 64 then table.remove(t, 1) end
end
local function top() return st.units[#st.units] end

-- the unit is still right before the cursor
local function valid(host, u)
  if not u or u.ins == "" then return false end
  local b = host.before()
  local nl = u.ins:find("\n[^\n]*$")
  if nl then return b == u.ins:sub(nl + 1) end
  return b:sub(-#u.ins) == u.ins
end

local function close() st.open = false end

local function chunk_text(u, j)
  local s = u.cons .. u.cands[j or u.ci]
  if u.cap then s = capital(s) end
  return s
end

local function caps_now(host, before, prefix)
  local auto = prose_of(host) and prefix == "" and predict.sentence_start(before)
  return st.shift ~= auto
end

-- a consonant (or a vowel, cons ""), finished by the dictionary
local function syllable(host, cons, from_end)
  close()
  local before = host.before()
  local prefix = word_at(before, main_lang(host.lang))
  local cands = syllables(host, before, cons)
  local u = { kind = "chunk", cons = cons, cands = cands, ci = from_end and #cands or 1,
              cap = caps_now(host, before, prefix), del = "" }
  st.shift, st.soft = false, false
  u.ins = chunk_text(u)
  host.insert(u.ins)
  push(u)
  st.open = true
end

-- square / triangle: the next or previous syllable, digit (or a new
-- syllable that starts with a vowel)
local function rotate(host, d)
  local u = top()
  if not (st.open and valid(host, u)) then
    syllable(host, "", d < 0)
    return
  end
  local text
  if u.kind == "digit" then
    u.v = (u.v + d) % 10
    text = tostring(u.v)
  else
    u.ci = (u.ci - 1 + d) % #u.cands + 1
    text = chunk_text(u)
  end
  host.erase(#u.ins)
  host.insert(text)
  u.ins = text
end

-- punctuation that goes right after the word, over the suggestion's space
local ATTACH = { [","] = " ", [";"] = " ", [":"] = " ", ["!"] = " ", ["?"] = " ", ["."] = " ",
                 ["'"] = "", [")"] = " " }

local function char(host, c)
  close()
  local before = host.before()
  if st.soft and before:sub(-1) == " " and ATTACH[c] then
    local text = c .. ATTACH[c]
    host.erase(1)
    host.insert(text)
    push({ kind = "punct", del = " ", ins = text, soft = true })
    st.soft = ATTACH[c] ~= ""
    return
  end
  local d = c:match("^%d$")
  host.insert(c)
  push({ kind = d and "digit" or "char", del = "", ins = c, v = d and tonumber(c) })
  st.soft = false
  st.open = d ~= nil
end

local function space(host)
  close()
  if st.soft then st.soft = false; return end    -- the suggestion's space is there
  host.insert(" ")
  push({ kind = "space", del = "", ins = " " })
end

-- cross twice: a full stop and a space (in code a dot, nothing after)
local function period(host)
  close()
  local p = code_of(host) and "." or ". "
  if st.soft and host.before():sub(-1) == " " then
    host.erase(1)
    host.insert(p)
    push({ kind = "punct", del = " ", ins = p, soft = true })
  else
    host.insert(p)
    push({ kind = "punct", del = "", ins = p })
  end
  st.soft = false
end

-- circle: what was last written goes (a syllable, a word, a space...), or
-- a character when that is not known
local function undo(host)
  close()
  local u = top()
  if u and valid(host, u) then
    st.units[#st.units] = nil
    host.erase(#u.ins)
    if u.del ~= "" then host.insert(u.del) end
    st.soft = u.soft == true
    local t = top()
    st.open = t ~= nil and (t.kind == "chunk" or t.kind == "digit") and valid(host, t)
  else
    st.units = {}
    st.soft = false
    host.erase(1)
  end
end

local function delword(host)
  close()
  local b = host.before()
  local s = b:match("[%w_\128-\165]+%s*$") or b:match(".%s*$") or ""
  if s ~= "" then host.erase(#s) end
  st.units, st.soft = {}, false
end

local function newline(host)
  close()
  host.newline()
  push({ kind = "newline", del = "", ins = "\n" .. host.before() })
  st.soft = false
end

local function move(host, dir)
  close()
  st.units, st.soft, st.shift = {}, false, false
  host.move(dir)
end

-- R1: the syllable is good as it is; nothing to close, the cursor goes on
local function forward(host)
  if st.open then close() else move(host, "right") end
end

local function shift(host)
  local u = top()
  if st.open and u and u.kind == "chunk" and valid(host, u) then
    u.cap = not u.cap
    local text = chunk_text(u)
    host.erase(#u.ins)
    host.insert(text)
    u.ins = text
  else
    st.shift = not st.shift
  end
end

-- the suggestions: up to three words that finish (or follow) the word
local function suggest(host)
  st.words = {}
  local lang = host.lang or "it"
  if lang == "none" then return end
  local ml = main_lang(lang)
  local before = host.before()
  local prefix, prev = predict.word_at(before, ml == "lua" and "lua" or "it")
  if not prefix then return end
  local ok, list = pcall(predict.candidates, lang, prefix, prev, 3, host.words, host.words_weight)
  if not ok then return end
  local caps = prose_of(host) and prefix == "" and predict.sentence_start(before)
  for _, w in ipairs(list) do
    local word, ending = predict.shaped(w, prefix, ml == "lua" and "lua" or "it", caps)
    if word ~= prefix then
      st.words[#st.words + 1] = { word = word, ending = ending, prefix = prefix }
    end
  end
end

local function take_word(host, k)
  close()
  local s = st.words[k]
  if not s or (s.prefix ~= "" and host.before():sub(-#s.prefix) ~= s.prefix) then return end
  local text = s.word .. s.ending
  host.erase(#s.prefix)
  host.insert(text)
  push({ kind = "word", del = s.prefix, ins = text, soft = st.soft })
  st.soft = s.ending == " " and prose_of(host)
  st.flash = #text
end

local ACTIONS = {
  space = space, period = period, undo = undo, delword = delword,
  next = function(h) rotate(h, 1) end, prev = function(h) rotate(h, -1) end,
  word1 = function(h) take_word(h, 1) end, word2 = function(h) take_word(h, 2) end,
  word3 = function(h) take_word(h, 3) end,
}

local function run(host, a)
  st.flash = 0
  local f = ACTIONS[a]
  if f then f(host)
  elseif a:find("^%a$") then syllable(host, a)
  else char(host, a) end
  st.dirty = true
end

local function refresh(host)
  st.dirty = false
  suggest(host)
  st.preview = {}
end

------------------------------------------------------------------ presses

-- the press that waits for its double: now it is a single one
local function commit(host)
  local p = st.pend
  if not p then return end
  st.pend = nil
  run(host, p.def[1])
end
P.commit = function() if st.host then commit(st.host) end end

-- a press of a button of the cross (dir) or of the four: with a double it
-- waits; the same press again in time is the double
local function tap(host, layer, btn, is_dir, now)
  local def = (is_dir and CROSS or FACE)[layer][btn]
  if not def then commit(host); return end
  local key = layer * 65536 + btn
  local p = st.pend
  if p then
    st.pend = nil
    if p.key == key and now - p.t < st.delay then
      run(host, def[2])
      return
    end
    run(host, p.def[1])
  end
  if def[2] then
    st.pend = { key = key, t = now, def = def, layer = layer, btn = btn }
  else
    run(host, def[1])
  end
end

local HOLD_NL, REP_START, REP_STEP = 0.25, 0.4, 0.07

-- L1 and R1: back and forward; held, the cross moves the cursor (and up /
-- down the lines); both held, a new line. true: the cross was theirs.
local function nav_step(host, b, pressed, released, now, dir)
  local n = st.nav
  local sh = b & (L1 | R1)
  if pressed & (L1 | R1) ~= 0 then
    commit(host)
    if not n.t then n.t, n.used = now, false end
  end
  if not n.t then return false end
  if sh == L1 | R1 then
    n.both = n.both or now
    if not n.nl and now - n.both >= HOLD_NL then
      newline(host)
      st.dirty = true
      n.nl, n.used = true, true
    end
  end
  if sh ~= 0 and dir then
    move(host, ONE_DIR[dir])
    st.dirty, n.used = true, true
    return true
  end
  if (sh == L1 or sh == R1) and not n.used and not n.both and now - n.t >= REP_START then
    if not n.r_at or now >= n.r_at then
      if sh == L1 then move(host, "left") else forward(host) end
      st.dirty, n.rep, n.r_at = true, true, now + REP_STEP
    end
  end
  if sh == 0 then
    if not n.used and not n.rep then
      if n.both then newline(host)
      elseif released & L1 ~= 0 then move(host, "left")
      elseif released & R1 ~= 0 then forward(host) end
      st.dirty = true
    end
    st.nav = {}
  end
  return sh ~= 0
end

-- circle held: it erases on, a character at a time
local function hold_erase(host, b, pressed, now, layer)
  if pressed & BB ~= 0 then st.hold_b = now + REP_START end
  if b & BB == 0 or layer ~= 1 then st.hold_b = nil; return end
  if st.hold_b and now >= st.hold_b then
    close()
    st.units, st.soft = {}, false
    host.erase(1)
    st.hold_b = now + REP_STEP
    st.dirty = true
  end
end

local function compose_step(host, b, pressed, now, dir)
  local layer = LAYER[b & (L2 | R2)]
  if dir then tap(host, layer, dir, true, now) end
  for _, f in ipairs(FACES) do
    if pressed & f ~= 0 then tap(host, layer, f, false, now) end
  end
  hold_erase(host, b, pressed, now, layer)
  if pressed & L3 ~= 0 then
    commit(host)
    shift(host)
    st.dirty = true
  end
  if pressed & R3 ~= 0 then
    commit(host)
    st.mode, st.popup = "keyboard", true
  end
  if pressed & START ~= 0 then commit(host) end
end

------------------------------------------------------------------ keyboard

local function kb_key(page, row, col)
  if row == 5 then return KB_SPECIAL[col] end
  return KB[page][row]:sub(col, col)
end

-- the first column of a special key, and the one after it
local function special_span(col)
  local name = KB_SPECIAL[col]
  local a, z = col, col
  while a > 1 and KB_SPECIAL[a - 1] == name do a = a - 1 end
  while z < 10 and KB_SPECIAL[z + 1] == name do z = z + 1 end
  return a, z
end

local function kb_move(dir)
  local kb = st.kb
  if dir == UP then kb.row = (kb.row - 2) % 5 + 1
  elseif dir == DOWN then kb.row = kb.row % 5 + 1
  elseif kb.row == 5 then
    local a, z = special_span(kb.col)
    kb.col = dir == LEFT and (a - 2) % 10 + 1 or z % 10 + 1
  elseif dir == LEFT then kb.col = (kb.col - 2) % 10 + 1
  else kb.col = kb.col % 10 + 1 end
end

local function kb_type(host)
  local kb = st.kb
  local k = kb_key(kb.page, kb.row, kb.col)
  st.flash = 0
  if k == "shift" then kb.shift = not kb.shift; return
  elseif k == "page" then kb.page = 3 - kb.page; return
  elseif k == "space" then space(host)
  elseif k == "del" then undo(host)
  elseif k == "enter" then newline(host)
  else
    local before = host.before()
    if k:find("%a") then
      local prefix = word_at(before, main_lang(host.lang))
      local auto = prose_of(host) and prefix == "" and predict.sentence_start(before)
      if kb.shift ~= auto then k = k:upper() end
      kb.shift = false
    end
    char(host, k)
    st.open = false
  end
  st.dirty = true
  if st.popup then st.mode, st.popup = "compose", false end
end

local function keyboard_step(host, b, pressed, now, dir)
  local kb = st.kb
  if dir then
    kb_move(dir)
    st.rep.dir, st.rep.t = dir, now + REP_START
  elseif b & DIRS ~= 0 and st.rep.dir and b & DIRS == st.rep.dir and now >= st.rep.t then
    kb_move(st.rep.dir)
    st.rep.t = now + REP_STEP
  elseif b & DIRS == 0 then
    st.rep.dir = nil
  end
  if b & R2 ~= 0 then
    local w = pressed & BA ~= 0 and 1 or pressed & BX ~= 0 and 2 or pressed & BY ~= 0 and 3
    if w then st.flash = 0; take_word(host, w); st.dirty = true end
    if pressed & BB ~= 0 then delword(host); st.dirty = true end
  else
    if pressed & BA ~= 0 then kb_type(host) end
    if pressed & BB ~= 0 then st.flash = 0; undo(host); st.dirty = true end
    if pressed & BX ~= 0 then st.flash = 0; space(host); st.dirty = true end
    if pressed & BY ~= 0 then kb.page = 3 - kb.page end
    hold_erase(host, b, pressed, now, 1)
  end
  if pressed & (L2 | L3) ~= 0 then kb.shift = not kb.shift end
  if pressed & R3 ~= 0 then st.mode, st.popup = "compose", false end
end

------------------------------------------------------------------ update

local function now_of(host)
  if host and host.now then return host.now() end
  if time then return time() end
  return os.clock()
end

-- Once a frame: the pad (pad() or bits) edits the host. true when the
-- pad was the typing's (the host leaves it alone this frame).
function P.update(host, bits)
  st.host = host
  local b = bits or (pad and pad()) or 0
  local now = now_of(host)
  local pressed, released = b & ~st.prev, st.prev & ~b
  st.prev = b
  if pressed & SELECT ~= 0 then
    if st.host then commit(host) end
    if st.mode == "compose" then
      st.mode = st.fallback ~= "off" and st.fallback or nil
    else
      st.mode = "compose"
    end
    st.popup, st.dirty, st.nav = false, true, {}
    st.presses = st.presses + 1
    return true
  end
  if not st.mode then return false end
  for _, bit in ipairs({ LEFT, RIGHT, UP, DOWN, BA, BB, BX, BY, L1, R1, L2, R2, L3, R3 }) do
    if pressed & bit ~= 0 then st.presses = st.presses + 1 end
  end
  if st.pend and now - st.pend.t >= st.delay then commit(host) end
  -- the cross: one direction at a time, never a diagonal; a press counts
  -- when the cross settles on one direction
  local d, dir = b & DIRS, nil
  if d == 0 then st.dir_down = nil
  elseif ONE_DIR[d] and d ~= st.dir_down then dir, st.dir_down = d, d end
  if nav_step(host, b, pressed, released, now, dir) then dir = nil end
  if st.mode == "compose" then
    compose_step(host, b, pressed, now, dir)
  else
    keyboard_step(host, b, pressed, now, dir)
  end
  if st.dirty then refresh(host) end
  return true
end

-- what the host shows around the cursor
function P.ghost()
  local s = st.words[1]
  if not s or s.prefix == "" or st.mode == nil or plain(s.word:sub(1, #s.prefix)) ~= plain(s.prefix) then
    return ""
  end
  return s.word:sub(#s.prefix + 1)
end
function P.flash() return st.flash end
function P.open_len()
  local u = top()
  if st.open and u and st.host and valid(st.host, u) then return #u.ins end
  return 0
end
function P.pending()
  local p = st.pend
  if not p then return nil end
  local a = p.def[1]
  if a == "space" then return " " end
  if #a == 1 then return a end
  return nil
end
function P.suggestions() return st.words end
function P.shift() return st.shift end

------------------------------------------------------------------ drawing

local C_PANEL, C_EDGE, C_BOX, C_BOXE = 0x10141E, 0x3A4660, 0x1C2433, 0x46546E
local C_TEXT, C_DIM, C_HELD, C_WORD = 0xFFFFFF, 0x8C9AB4, P.C_PEND, P.C_PRED
-- the colours of a DS4's symbols, by place: cross, circle, square, triangle
local C_FACE = { [BA] = 0x7AA8FF, [BB] = 0xFF6E6E, [BX] = 0xF08CD8, [BY] = 0x5EE0A0 }
local W, H = 340, 132

function P.size() return W, H end

local function set_font(name)
  if font then font(name) end
end
local function cur_font()
  if not font then return nil end
  local w, h = font()
  return w .. "x" .. h
end

local function chip(name, x, y, c)
  if prompt then return prompt(name, x, y, true) end
  print(name, x, y, c or C_DIM)
  return x + #name * 6 + 2
end

-- the syllable each direction would write now (computed a few a frame)
local function preview(layer, dir)
  local k = layer * 16 + dir
  local v = st.preview[k]
  if v ~= nil then return v or nil end
  if st.preview_n and st.preview_n >= 2 then return nil end
  st.preview_n = (st.preview_n or 0) + 1
  local host, c = st.host, CROSS[layer][dir][1]
  v = false
  if host and c:find("^%a$") and host.lang ~= "none" then
    local ok, cands = pcall(syllables, host, host.before(), c)
    if ok and cands[1] ~= "" then v = c .. cands[1] end
  end
  st.preview[k] = v
  return v or nil
end

local FACE_LABEL = { space = "space", period = ".", undo = "del", next = "next", prev = "prev",
                     delword = "del wd" }

local function face_label(layer, btn)
  local def = FACE[layer][btn]
  if not def then return "", nil end
  local a = def[1]
  if a:find("^word") then
    local w = st.words[tonumber(a:sub(5))]
    return w and w.word or "-", nil, true
  end
  return FACE_LABEL[a] or a, def[2] and (FACE_LABEL[def[2]] or def[2])
end

local function box(x, y, w, h, edge, fill)
  rectfill(x, y, w, h, fill or C_BOX)
  rect(x, y, w, h, edge or C_BOXE)
end

local function draw_compose(x, y, b)
  local layer = LAYER[b & (L2 | R2)]
  local pend = st.pend
  -- shoulders
  local function shoulder(name, bx, bit, label)
    local on = b & bit ~= 0
    box(bx, y + 3, 30, 14, on and C_HELD or C_BOXE, on and 0x4A3A10 or C_BOX)
    print(name, bx + 3, y + 4, on and C_HELD or C_DIM)
    if label then print(label, bx + 32, y + 4, C_DIM) end
  end
  shoulder("L2", x + 4, L2)
  shoulder("L1", x + 38, L1, "<")
  shoulder("R1", x + W - 68, R1)
  print(">", x + W - 76, y + 4, C_DIM)
  shoulder("R2", x + W - 34, R2)
  local title = ({ "abc", "L2  pfg zx", "R2  words", "123  code" })[layer]
  print(title, x + W // 2 - #title * 3, y + 4, layer == 1 and C_DIM or C_HELD)
  -- the cross: the syllable a press writes now, its double small
  local cx, cy = x + 64, y + 66
  rectfill(cx - 9, cy - 30, 18, 60, 0x0A0D14)
  rectfill(cx - 48, cy - 8, 96, 16, 0x0A0D14)
  local spots = { [UP] = { cx - 19, cy - 42 }, [DOWN] = { cx - 19, cy + 18 },
                  [LEFT] = { cx - 60, cy - 12 }, [RIGHT] = { cx + 22, cy - 12 } }
  for dir, at in pairs(spots) do
    local def = CROSS[layer][dir]
    local waiting = pend and pend.layer == layer and pend.btn == dir
    box(at[1], at[2], 38, 24, waiting and C_HELD or C_BOXE)
    set_font("8x16")
    local s = def[1]:find("%a") and preview(layer, dir) or def[1]
    print(s:sub(1, 3), at[1] + 3, at[2] + 4, waiting and C_HELD or C_TEXT)
    set_font("6x12")
    print(def[2], at[1] + 31, at[2] + 1, waiting and C_HELD or C_DIM)
  end
  -- the four buttons, framed in the colours of their symbols
  local fx = x + 276
  local fspots = { [BY] = { fx - 20, cy - 42 }, [BA] = { fx - 20, cy + 18 },
                   [BX] = { fx - 60, cy - 12 }, [BB] = { fx + 20, cy - 12 } }
  set_font("6x12")
  for btn, at in pairs(fspots) do
    local s1, s2, word = face_label(layer, btn)
    local waiting = pend and pend.layer == layer and pend.btn == btn
    box(at[1], at[2], 40, 24, waiting and C_HELD or C_FACE[btn])
    local c = waiting and C_HELD or word and C_WORD or C_TEXT
    print(s1:sub(1, 6), at[1] + 3, at[2] + 1, c)
    if s2 then print(s2, at[1] + 37 - #s2 * 6, at[2] + 12, waiting and C_HELD or C_DIM) end
  end
  -- the middle: the three words of R2
  local mx, my = x + 130, y + 24
  print("R2 +", mx, my, layer == 3 and C_HELD or C_DIM)
  local names = { "A", "X", "Y" }
  for k = 1, 3 do
    local w = st.words[k]
    local px = chip(names[k], mx, my + 15 * k)
    print(w and w.word:sub(1, 11) or "", px + 2, my + 15 * k, C_WORD)
  end
  -- the bottom line: the syllable that turns, or the other keys
  local u = top()
  local by = y + H - 15
  if st.open and u and u.kind == "chunk" and st.host and valid(st.host, u) then
    local px = chip("X", x + 6, by)
    print("next", px + 2, by, C_DIM)
    px = x + 64
    local n = #u.cands
    for k = -2, 2 do
      local j = (u.ci - 1 + k) % n + 1
      local s = chunk_text(u, j)
      if s == "" then s = "_" end
      local c = k == 0 and P.C_OPEN or C_DIM
      if k == 0 then rect(px - 2, by - 1, #s * 6 + 4, 13, P.C_OPEN) end
      print(s, px, by, c)
      px = px + #s * 6 + 10
    end
    px = chip("Y", x + W - 50, by)
    print("prev", px + 2, by, C_DIM)
  else
    local px = chip("L3", x + 6, by)
    px = print(st.shift and "ABC" or "Aa", px + 2, by, st.shift and C_HELD or C_DIM) + 8
    px = chip("R3", px, by)
    px = print("#+", px + 2, by, C_DIM) + 8
    px = chip("L1", px, by)
    px = chip("R1", px, by)
    px = print("enter", px + 2, by, C_DIM) + 8
    px = chip("SELECT", px, by)
    print(st.fallback == "off" and "off" or "keyboard", px + 2, by, C_DIM)
  end
end

local function draw_keyboard(x, y, b)
  local kb = st.kb
  local kx, ky, kw, kh = x + 20, y + 4, 30, 20
  set_font("8x16")
  for row = 1, 4 do
    for col = 1, 10 do
      local k = KB[kb.page][row]:sub(col, col)
      local on = kb.row == row and kb.col == col
      local px, py = kx + (col - 1) * kw, ky + (row - 1) * (kh + 2)
      box(px, py, kw - 2, kh, on and C_HELD or C_BOXE, on and 0x4A3A10 or C_BOX)
      if kb.shift and kb.page == 1 then k = k:upper() end
      print(k, px + 10, py + 2, on and C_HELD or C_TEXT)
    end
  end
  set_font("6x12")
  local NAMES = { shift = kb.shift and "SHIFT" or "shift", page = kb.page == 1 and "#+" or "abc",
                  space = "space", del = "del", enter = "enter" }
  local col = 1
  while col <= 10 do
    local a, z = special_span(col)
    local name = KB_SPECIAL[a]
    local on = kb.row == 5 and kb.col >= a and kb.col <= z
    local px, py = kx + (a - 1) * kw, ky + 4 * (kh + 2)
    local w = (z - a + 1) * kw - 2
    box(px, py, w, kh, on and C_HELD or C_BOXE, on and 0x4A3A10 or C_BOX)
    local s = NAMES[name]
    print(s, px + w // 2 - #s * 3, py + 4, on and C_HELD or C_DIM)
    col = z + 1
  end
  -- the words, and the buttons
  local by = y + H - 16
  local px = chip("R2", x + 6, by)
  local names = { "A", "X", "Y" }
  for k = 1, 3 do
    local w = st.words[k]
    if w then
      px = chip(names[k], px + 4, by)
      px = print(w.word:sub(1, 10), px + 2, by, C_WORD)
    end
  end
  px = math.max(px + 10, x + 200)
  px = chip("SELECT", px, by)
  print("compose", px + 2, by, C_DIM)
end

-- the overlay of the controller (or the keyboard) at (x, y)
function P.draw(x, y)
  if not st.mode then return end
  local old = cur_font()
  st.preview_n = 0
  rectfill(x, y, W, H, C_PANEL)
  rect(x, y, W, H, C_EDGE)
  set_font("6x12")
  if st.mode == "compose" then draw_compose(x, y, st.prev) else draw_keyboard(x, y, st.prev) end
  if old then set_font(old) end
end

------------------------------------------------------------------ practice

-- a host over a string: the practice and the tests. Lua: a new line
-- keeps the indentation (two more after then, do, function, {), end /
-- else / until go back two.
local DEDENT = { ["end"] = true, ["else"] = true, ["elseif"] = true, ["until"] = true, ["}"] = true }
function P.text_host(lang, text)
  local h = { lang = lang, t = text or "" }
  h.cur = #h.t
  local code = main_lang(lang) == "lua"
  function h.text() return h.t end
  function h.cursor() return h.cur end
  function h.before() return h.t:sub(1, h.cur):match("[^\n]*$") end
  local function put(s)
    h.t = h.t:sub(1, h.cur) .. s .. h.t:sub(h.cur + 1)
    h.cur = h.cur + #s
  end
  function h.insert(s)
    local was = h.before():match("^%s+(%S+)$")
    put(s)
    if code and not (was and DEDENT[was]) then
      local ind, w = h.before():match("^(%s+)(%S+)$")
      if ind and DEDENT[w] and #ind >= 2 then
        local at = h.cur - #ind - #w
        h.t = h.t:sub(1, at) .. h.t:sub(at + 3)
        h.cur = h.cur - 2
      end
    end
  end
  function h.erase(n)
    n = math.min(n, h.cur)
    h.t = h.t:sub(1, h.cur - n) .. h.t:sub(h.cur + 1)
    h.cur = h.cur - n
  end
  function h.newline()
    local line = h.before()
    local ind = line:match("^%s*")
    if code then
      local l = line:gsub("%s*%-%-.*$", "")
      if l:match("%f[%w]then$") or l:match("%f[%w]do$") or l:match("%f[%w]else$") or
         l:match("%f[%w]repeat$") or l:match("{$") or l:match("%f[%w]function%f[%W].*%)$") then
        ind = ind .. "  "
      end
    end
    put("\n" .. ind)
  end
  function h.move(dir)
    if dir == "left" then h.cur = math.max(0, h.cur - 1)
    elseif dir == "right" then h.cur = math.min(#h.t, h.cur + 1)
    else
      local s = h.t:sub(1, h.cur)
      local col = #s:match("[^\n]*$")
      local starts = { 1 }
      for i in h.t:gmatch("()\n") do starts[#starts + 1] = i + 1 end
      local line = 1
      for k, at in ipairs(starts) do if at <= h.cur + 1 then line = k end end
      line = line + (dir == "up" and -1 or 1)
      if line >= 1 and line <= #starts then
        local at = starts[line]
        local len = #(h.t:sub(at):match("^[^\n]*"))
        h.cur = at - 1 + math.min(col, len)
      end
    end
  end
  return h
end

local function face_of_word(k) return ({ BA, BX, BY })[k] end

-- the keyboard's place of a character: page, row, col, shift
local function kb_place(c)
  for page = 1, 2 do
    for row = 1, 4 do
      local s = KB[page][row]
      local col = s:find(c, 1, true)
      if col then return page, row, col, false end
      if page == 1 and c:find("%u") then
        col = s:find(c:lower(), 1, true)
        if col then return page, row, col, true end
      end
    end
  end
end

local function kb_coach(host, c)
  local kb = st.kb
  if c == " " then return { tap = BX, label = "space" } end
  if c == "\n" then return { both = true, label = "enter" } end
  local page, row, col, up = kb_place(c)
  if not page then return nil end
  if page ~= kb.page then return { tap = BY, label = "page" } end
  if page == 1 and c:find("%a") then
    local before = host.before()
    local prefix = word_at(before, main_lang(host.lang))
    local auto = prose_of(host) and prefix == "" and predict.sentence_start(before)
    if (kb.shift ~= auto) ~= up then return { tap = L2, label = "shift" } end
  end
  if row ~= kb.row then
    local down = (row - kb.row) % 5
    return { tap = down <= 5 - down and DOWN or UP }
  end
  if col ~= kb.col then
    local right = (col - kb.col) % 10
    return { tap = right <= 10 - right and RIGHT or LEFT }
  end
  return { tap = BA, label = c }
end

-- The next press to write target, when the host's text is its beginning
-- (or has a mistake): { hold = triggers held, tap = the button, double =
-- press it twice quickly, both = L1 + R1 held, wait = let the press
-- waiting for its double go } or nil when it is all written.
-- code: a line whose end, else, until... is being written goes back two
-- spaces when the word is whole; until then it is compared as if it had
-- (P.align: the practice shows it so)
local function as_target(typed, target)
  local ls = typed:find("[^\n]*$")
  local ind, w = typed:sub(ls):match("^( +)([%a}]*)$")
  if not ind or #ind < 2 or target:sub(1, ls - 1) ~= typed:sub(1, ls - 1) then return typed end
  local tind, tw = target:sub(ls):match("^( *)([%a}]+)")
  if tind and #tind == #ind - 2 and DEDENT[tw] and tw:sub(1, #w) == w and w ~= tw then
    return typed:sub(1, ls - 1) .. tind .. w
  end
  return typed
end

P.align = as_target

function P.coach(host, target)
  if not st.mode then return { tap = SELECT, label = "on" } end
  if st.pend then return { wait = true } end
  if st.dirty then refresh(host) end
  local real, code = host.text(), code_of(host)
  local typed = code and as_target(real, target) or real
  if (typed:gsub("%s+$", "")) == (target:gsub("%s+$", "")) then return nil end
  local u = top()
  -- the syllable or digit still turning: the one the text wants
  if st.mode == "compose" and st.open and valid(host, u) then
    local base = real:sub(1, #real - #u.ins)
    if code then base = as_target(base, target) end
    if target:sub(1, #base) == base then
      local want = target:sub(#base + 1)
      local best, cur, n
      if u.kind == "digit" then
        best, cur, n = tonumber(want:match("^%d")), u.v, 10
      else
        -- the syllable that writes the most (the nearest of two)
        local blen = -1
        cur, n = u.ci, #u.cands
        for j = 1, n do
          local t = chunk_text(u, j)
          if want:sub(1, #t) == t and (#t > blen or (#t == blen and
             math.min((j - cur) % n, (cur - j) % n) < math.min((best - cur) % n, (cur - best) % n))) then
            best, blen = j, #t
          end
        end
      end
      if best and best ~= cur then
        local fwd = (best - cur) % n
        return fwd <= n - fwd and { tap = BX, label = "next" } or { tap = BY, label = "prev" }
      end
    end
  end
  -- a mistake: erase
  local prose = prose_of(host)
  local soft = st.soft and typed:sub(-1) == " "
  local n = 0
  while n < #typed and typed:byte(n + 1) == target:byte(n + 1) do n = n + 1 end
  local skip = soft and n == #typed - 1 and ATTACH[target:sub(n + 1, n + 1)] ~= nil
  if n < #typed and not skip then return { tap = BB, label = "erase" } end
  local pos = skip and #typed or #typed + 1
  local rest = target:sub(pos)
  if rest:match("^%s*$") and (rest == "" or not rest:find("\n")) then return nil end
  if st.mode == "keyboard" then
    return kb_coach(host, rest:sub(1, 1)) or { tap = SELECT, label = "compose" }
  end
  -- a suggestion that writes the word (at least two characters more)
  for k, s in ipairs(st.words) do
    local at = pos - #s.prefix
    if not skip and at >= 1 and target:sub(at, at + #s.word - 1) == s.word then
      local after = target:sub(at + #s.word, at + #s.word)
      local fits = s.ending == "" or after == s.ending or
                   (s.ending == " " and prose and (after == "" or ATTACH[after] ~= nil))
      local gain = #s.word - #s.prefix + (after == s.ending and #s.ending or 0)
      if fits and gain >= 2 then return { hold = R2, tap = face_of_word(k), label = s.word } end
    end
  end
  local c = rest:sub(1, 1)
  if c == "\n" then return { both = true, label = "enter" } end
  if c == " " then return { tap = BA, label = "space" } end
  if c == "." and (not prose or rest:sub(1, 2) == ". " or rest:match("^%.%s*$")) then
    return { tap = BA, double = true, label = "." }
  end
  local before = host.before()
  if skip then before = before:sub(1, -2) end
  if c:find(LETTER) then
    local lc = c:lower()
    local prefix = word_at(before, main_lang(host.lang))
    local auto = prose and prefix == "" and predict.sentence_start(before)
    if c:find("%a") and (c ~= lc) ~= (st.shift ~= auto) then return { tap = L3, label = "Aa" } end
    local k = KEYOF[lc]
    if k then return { hold = k.hold, tap = k.tap, double = k.double, label = lc } end
    -- a vowel: a syllable of its own (closing the one before)
    if st.open and valid(host, u) then return { tap = R1, label = "ok" } end
    local cands = syllables(host, before, "")
    local best, blen = 1, -1
    for j, s in ipairs(cands) do
      if rest:sub(1, #s):lower() == s:lower() and #s > blen then best, blen = j, #s end
    end
    if #cands - best < best - 1 then return { tap = BY, label = "prev" } end
    return { tap = BX, label = "next" }
  end
  if c == "9" then c = "0" end              -- 0 turned back with triangle
  local k = KEYOF[c]
  if k and c ~= "." then return { hold = k.hold, tap = k.tap, double = k.double, label = c } end
  return { tap = R3, label = "keyboard" }
end

-- The texts of the practice (written for bm), by language: UTF-8 here,
-- code page 437 like the console's from predict.from_utf8.
local TEXTS = {
  it = {
    "Ciao Marco, domani sera passo da te verso le otto. Porto la console e due controller, così proviamo il gioco nuovo.",
    "Il treno era in ritardo e la stazione piena di gente. Ho letto quasi tutto il libro mentre aspettavo.",
    "Per fare una buona pizza serve pazienza: l'impasto deve riposare almeno otto ore in un posto tiepido.",
    "Questa mattina il mare era calmo e il cielo limpido. Siamo usciti con la barca e abbiamo visto i delfini.",
    "Scrivere con il controller sembra difficile, ma dopo qualche minuto le dita trovano da sole le lettere giuste.",
    "La biblioteca chiude alle sette, quindi ci vediamo davanti all'ingresso alle sei e mezza. Non fare tardi!",
    "Nel livello finale il drago protegge il castello. Serve una spada di fuoco e tanta pazienza per batterlo.",
    "Grazie per il regalo, è bellissimo. Lo userò ogni giorno e penserò a te ogni volta che lo vedo.",
  },
  en = {
    "The rain stopped just before noon, so we walked to the old bridge and watched the river carry the leaves away.",
    "Press start to begin a new game. Collect every star in the level and the hidden door will open.",
    "Thanks for your message. I will send the files tomorrow morning, after the meeting with the team.",
    "A good game teaches its rules without words: the first room shows you how to jump, the second one why.",
    "We packed sandwiches, a map and two bottles of water, then climbed the hill to see the sunrise.",
    "My little brother wants to learn to code. Tonight we will write a small game where a cat chases a mouse.",
    "The library has a quiet room on the top floor. It is the best place in town to read or to think.",
    "Remember to save your progress before you turn off the console, or you will start again from the beginning.",
  },
  lua = {
    "function _update()\n  if btn(0) then x = x - 2 end\n  if btn(1) then x = x + 2 end\nend",
    "function _draw()\n  cls(0)\n  print(\"score \" .. score, 4, 4)\n  circfill(x, y, 6, 8)\nend",
    "local enemies = {}\nfor i = 1, 8 do\n  enemies[i] = { x = i * 20, y = 40, alive = true }\nend",
    "local function hit(a, b)\n  return math.abs(a.x - b.x) < 8 and math.abs(a.y - b.y) < 8\nend",
    "function _init()\n  player = { x = 160, y = 90, speed = 2 }\n  score = 0\nend",
    "for _, e in ipairs(enemies) do\n  if e.alive and hit(player, e) then\n    e.alive = false\n    score = score + 10\n  end\nend",
    "local t = time()\nlocal wave = math.sin(t * 2) * 10\nrectfill(0, 80 + wave, 320, 4, 12)",
    "local names = { \"ada\", \"bea\", \"carlo\" }\ntable.sort(names)\nprint(table.concat(names, \", \"))",
  },
}
for _, list in pairs(TEXTS) do
  for i, s in ipairs(list) do list[i] = predict.from_utf8(s) end
end
P.TEXTS = TEXTS

return P
