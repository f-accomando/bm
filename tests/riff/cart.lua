-- riff (require "riff"), the language of patterns: the mini-notation, the
-- functions, the controls, the scheduler on the sound's clock, the live
-- code and the bake into a song. Run by bmhost (make test-riff); the
-- sound of the last seconds goes to build/riff/riff.wav.

local R = require "riff"
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("riff: FAIL " .. what) end
end

local function near(a, b) return math.abs(a - b) < 1e-6 end

-- "start:value start:value ..." of the events starting in [b, e)
local function show(p, b, e)
  local r = {}
  for _, h in ipairs(R.reify(p):events(b or 0, e or 1)) do
    local v = h.v
    if type(v) == "table" then
      v = v.s or v.note or v.n or (v.notes and table.concat(v.notes, "+")) or "?"
    end
    r[#r + 1] = { h.wb, string.format("%g:%s", h.wb, type(v) == "number" and string.format("%g", v) or tostring(v)) }
  end
  table.sort(r, function(x, y) if x[1] ~= y[1] then return x[1] < y[1] end return x[2] < y[2] end)
  for i, x in ipairs(r) do r[i] = x[2] end
  return table.concat(r, " ")
end

local function expect(p, want, what, b, e)
  local got = show(p, b, e)
  check(got == want, (what or tostring(p)) .. ": " .. got .. " (want " .. want .. ")")
end

local function wholes(p, b, e)
  local r = {}
  for _, h in ipairs(R.reify(p):events(b or 0, e or 1)) do r[#r + 1] = string.format("%g-%g", h.wb, h.we) end
  return table.concat(r, " ")
end

local function count(p, b, e) return #R.reify(p):events(b, e) end

local function test_mini()
  expect("a b c d", "0:a 0.25:b 0.5:c 0.75:d", "a sequence")
  expect("a [b c]", "0:a 0.5:b 0.75:c", "a group")
  expect("<a b c>", "0:a 1:b 2:c 3:a", "one a cycle", 0, 4)
  expect("<a <b c>>", "0:a 1:b 2:a 3:c", "alternation inside", 0, 4)
  expect("a*2 b", "0:a 0.25:a 0.5:b", "faster")
  check(wholes("a/2", 0, 2) == "0-2", "slower: one note over two cycles")
  check(count("a/2", 1, 2) == 0, "slower: nothing starts in the second cycle")
  expect("a@3 b", "0:a 0.75:b", "a weight")
  check(wholes("a@3 b") == "0-0.75 0.75-1", "a weight: the length")
  expect("a!3 b", "0:a 0.25:a 0.5:a 0.75:b", "replicate")
  expect("a ! b", "0:a 0.333333:a 0.666667:b", "! repeats")
  check(wholes("a _ _ b") == "0-0.75 0.75-1", "_ makes it longer")
  expect("a ~ b -", "0:a 0.5:b", "rests")
  expect("x(3,8)", "0:x 0.375:x 0.75:x", "Euclid 3,8")
  expect("x(3,8,2)", "0.125:x 0.5:x 0.75:x", "Euclid turned")
  expect("x(5,8)", "0:x 0.25:x 0.375:x 0.625:x 0.75:x", "Euclid 5,8")
  expect("[a, b c]", "0:a 0:b 0.5:c", "a stack inside")
  expect("a b, c", "0:a 0:c 0.5:b", "a stack at the top")
  expect("{a b c}%4", "0:a 0.25:b 0.5:c 0.75:a", "polymeter")
  expect("{a b c, d e}", "0:a 0:d 0.333333:b 0.333333:e 0.666667:c 0.666667:d", "polymeter: the first's steps")
  expect("a . b c", "0:a 0.5:b 0.75:c", "groups with .")
  expect("<a b>*2", "0:a 0.5:b", "alternation, faster")
  expect("hat*<2 4>", "0:hat 0.5:hat 1:hat 1.25:hat 1.5:hat 1.75:hat", "a patterned speed", 0, 2)
  expect("1 -2 0.5", "0:1 0.333333:-2 0.666667:0.5", "numbers")
  local n = count("a?", 0, 200)
  check(n > 70 and n < 130, "a? keeps about half: " .. n)
  check(count("a?", 0, 200) == n, "a? is the same each time")
  local n2 = count("a?0.9", 0, 200)
  check(n2 < 50, "a?0.9 keeps few: " .. n2)
  local seen = {}
  for c = 0, 30 do seen[show("[a|b|c]", c, c + 1):sub(-1)] = true end
  check(seen.a and seen.b and seen.c, "a|b|c: each one now and then")
  local ok, err = pcall(R.mini, "a [b")
  check(not ok and tostring(err):find("missing ]"), "an error says what: " .. tostring(err))
  ok, err = pcall(R.mini, "a b)")
  check(not ok and tostring(err):find("unexpected"), "a stray bracket: " .. tostring(err))
end

local function test_functions()
  local p = R.mini("a b c d")
  expect(p:fast(2), "0:a 0.125:b 0.25:c 0.375:d 0.5:a 0.625:b 0.75:c 0.875:d", "fast")
  expect(p:slow(2), "0:a 0.5:b", "slow")
  expect(p:rev(), "0:d 0.25:c 0.5:b 0.75:a", "rev")
  expect(R.mini("a b"):ply(2), "0:a 0.25:a 0.5:b 0.75:b", "ply")
  expect(p:late(0.25), "0:d 0.25:a 0.5:b 0.75:c", "late")
  expect(p:early(0.25), "0:b 0.25:c 0.5:d 0.75:a", "early")
  expect(p:iter(4), "1:b 1.25:c 1.5:d 1.75:a", "iter: the second cycle", 1, 2)
  expect(R.mini("a b"):every(2, function(x) return x:fast(2) end), "0:a 0.25:b 0.5:a 0.75:b 1:a 1.5:b",
         "every 2: the first cycle", 0, 2)
  expect(R.mini("a b"):lastOf(2, function(x) return x:rev() end), "0:a 0.5:b 1:b 1.5:a", "lastOf", 0, 2)
  expect(R.mini("a b"):palindrome(), "0:a 0.5:b 1:b 1.5:a", "palindrome", 0, 2)
  expect(R.mini("a"):off(0.25, function(x) return x:fmap(function() return "e" end) end),
         "0:a 0.25:e", "off")
  expect(R.mini("x"):euclid(3, 8), "0:x 0.375:x 0.75:x", "euclid()")
  expect(R.mini("a"):struct("t ~ t t"), "0:a 0.5:a 0.75:a", "struct")
  expect(p:mask("1 0 1 1"), "0:a 0.5:c 0.75:d", "mask")
  expect(R.mini("a b c d e f g h"):chunk(4, function(x) return x:fmap(function() return "Z" end) end),
         "1:a 1.125:b 1.25:Z 1.375:Z 1.5:e 1.625:f 1.75:g 1.875:h", "chunk: the second quarter", 1, 2)
  expect(p:linger(0.25), "0:a 0.25:a 0.5:a 0.75:a", "linger")
  expect(R.mini("a b c d"):swingBy(1 / 3, 2), "0:a 0.416667:b 0.5:c 0.916667:d", "swing: a third of a half later")
  expect(R.cat("a", "b c"), "0:a 1:b 1.5:c", "cat", 0, 2)
  expect(R.seq("a", "b c"), "0:a 0.5:b 0.75:c", "seq")
  expect(R.stack("a", "b b"), "0:a 0:b 0.5:b", "stack")
  expect(R.arrange({ 1, "a" }, { 2, "b c" }), "0:a 1:b 1.5:c 2:b 2.5:c 3:a", "arrange", 0, 4)
  expect(R.run(4), "0:0 0.25:1 0.5:2 0.75:3", "run")
  expect(R.mini("0 0.5"):range(10, 20), "0:10 0.5:15", "range")
  expect(R.sine:segment(4):range(0, 2), "0:1.70711 0.25:1.70711 0.5:0.292893 0.75:0.292893", "a sine in 4 steps")
  expect(R.saw:segment(2), "0:0.25 0.5:0.75", "a saw in 2 steps")
  local r1, r2 = show(R.rand:segment(8)), show(R.rand:segment(8))
  check(r1 == r2, "rand: the same at the same time")
  check(show(R.rand:segment(8), 0, 1) ~= show(R.rand:segment(8), 1, 2):gsub("^1", "0"), "rand: another cycle, others")
  check(R.bjorklund(3, 8)[1] and not R.bjorklund(3, 8)[2] and R.bjorklund(3, 8)[4], "bjorklund")
  local m = 0
  for _, h in ipairs(R.mini("a*16"):degradeBy(0.5):events(0, 10)) do m = m + 1 end
  check(m > 50 and m < 110, "degradeBy: about half of 160: " .. m)
end

local function test_values()
  expect(R.note("c e g"), "0:48 0.333333:52 0.666667:55", "note: octave 3 when missing")
  expect(R.note("c4 a4 eb3 f#2"), "0:60 0.25:69 0.5:51 0.75:42", "note names")
  expect(R.note("60 64"):add(12), "0:72 0.5:76", "add")
  expect(R.note("c e") + "<0 7>", "0:48 0.5:52 1:55 1.5:59", "+ a pattern", 0, 2)
  expect(R.note("c"):transpose(-12), "0:36", "transpose")
  local function notes(p)
    local r = {}
    for _, h in ipairs(p:events(0, 1)) do
      for _, n in ipairs(R.notes_of(h.v.s and h.v or (type(h.v) == "table" and h.v or { note = h.v }))) do
        r[#r + 1] = string.format("%g", n)
      end
    end
    return table.concat(r, " ")
  end
  check(notes(R.n("0 2 4 7"):scale("C:minor")) == "48 51 55 60", "n on a scale: " .. notes(R.n("0 2 4 7"):scale("C:minor")))
  check(notes(R.n("0 -1"):scale("A4:minor_pentatonic")) == "69 67", "a scale from A4, below")
  check(notes(R.n("0 2 4")) == "48 52 55", "n without a scale: C major")
  check(notes(R.chord("Am")) == "57 60 64", "a chord: " .. notes(R.chord("Am")))
  check(notes(R.chord("G7")) == "55 59 62 65", "G7: " .. notes(R.chord("G7")))
  check(notes(R.chord("F#m")) == "54 57 61", "F#m")
  check(notes(R.chord("C"):arp("up")) == "48 52 55", "arp up")
  check(notes(R.chord("C"):arp("down")) == "55 52 48", "arp down")
  check(notes(R.chord("C"):arp("updown")) == "48 52 55 52", "arp updown")
  check(notes(R.s("kick snare hat")) == "36 50 72", "the drums' own notes")
  check(notes(R.s("kick:2 epiano")) == "38 48", "kick:2 two semitones up; others C3")
  local v = R.s("kick*2"):gain(0.5):lpf(800):events(0, 1)[1].v
  check(v.s == "kick" and v.gain == 0.5 and v.lpf == 800, "controls on the events")
  local snd = R.sound_of(v)
  check(type(snd) == "table" and snd.s == "kick" and snd.cutoff == 800 and snd.filter == "lp", "the sound: the filter")
  check(R.sound_of({ s = "pad" }) == "pad", "a sound with nothing more is its name")
  check(R.sound_of({ note = 60 }) == "triangle", "no instrument: the triangle")
  local sv = R.sound_of({ s = "lead", hpf = 300, room = 0.5, pan = -1 })
  check(sv.filter == "hp" and sv.cutoff == 300 and sv.reverb == 0.5 and sv.pan == -1, "hpf, room, pan")
  expect(R.s("kick*4"):lpf("<400 2000>"):fmap(function(x) return x.lpf end), "0:400 0.25:400 0.5:400 0.75:400",
         "a patterned control")
  expect(R.s("kick"):pan(R.sine:range(-1, 1)):fmap(function(x) return string.format("%.2f", x.pan) end), "0:0.00",
         "a signal as a control")
  local j = R.s("a b"):jux(function(x) return x:rev() end):events(0, 1)
  check(#j == 4 and j[1].v.pan and math.abs(j[1].v.pan) == 1, "jux: left and right")
  check(R.note_num("c4") == 60 and R.note_num("A4") == 69 and R.note_num("bb2") == 46 and R.note_num("kick") == nil,
        "note_num")
  local ok = pcall(function() R.note("zz"):events(0, 1) end)
  check(not ok, "a word that is no note")
end

-- the examples of docs/RIFF.md, docs/API*.md and the games' guides
local function test_docs()
  local examples = {
    [[
setcpm(30)
drums = s "kick*4, ~ snare, hat*8" :gain(.9)
bass  = note "<c2 a1 f1 g1>" :s "acid" :lpf(sine:range(300, 1800):slow(4))
keys  = chord "<Cm7 Ab Eb Bb>" :s "epiano" :arp("updown") :fast(2) :room(.4)
]],
    [[
setcpm(28)
local verse  = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4) :pan(sine:range(-.5, .5))
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]],
    [[
setcpm(30)
drums = s "kick*4, ~ snare, hat*8"
bass  = note "<c2 a1 f1 g1>*2" :s "acid" :lpf(800)
lead  = n "0 2 4 <7 6>" :scale("A:minor") :s "pluck" :sometimes(add(12))
]],
  }
  for i, src in ipairs(examples) do
    local ok, err = R.code(src, "doc")
    check(ok, "the example " .. i .. " of the docs: " .. tostring(err))
    for _, name in ipairs(R.playing()) do
      check(#R.get(name):events(0, 4) > 0, "the example " .. i .. ": " .. name .. " plays")
    end
  end
  R.hush()
  local bank = {
    's "kick*4, ~ snare, hat*8"', 's "kick ~ ~ kick, ~ snare, [~ hat]*4" :swing(4)',
    'note "<c2 c2 eb2 g1>*4" :s "acid" :lpf(sine:range(300, 1800):slow(4))',
    'chord "<Am F C G>" :s "epiano" :arp("updown") :fast(4)', 'n "0 2 4 <7 6> 4 2 1 ~" :scale("A:minor") :s "pluck"',
    'chord "<Am F C G>" :s "pad" :room(.6)', 's "kick(3,8), hat(7,16), ~ clap"',
    'note "c4 e4 g4 c5" :s "chip" :every(4, rev) :fast(2)',
  }
  for i, src in ipairs(bank) do                -- bm Sound's examples (carts/sound/main.lua)
    local ok, err = R.code("riff = " .. src, "ex")
    check(ok and #R.get("riff"):events(0, 4) > 0, "bm Sound's example " .. i .. ": " .. tostring(err))
  end
  R.hush()
end

local function test_bake()
  R.setcps(0.5)
  local piece = R.bake(R.s("kick*4, ~ snare, hat*8"):gain(0.8), { cycles = 2, name = "beat" })
  check(piece.bpm == 120 and piece.kind == "base" and piece.name == "beat", "bake: 120 BPM at 0.5 cycles a second")
  check(#piece.patterns == 1 and piece.patterns[1].len == 32, "bake: two cycles, 32 steps")
  local inst = table.concat(piece.instruments, ",")
  check(inst == "kick,hat,snare", "bake: the instruments in order: " .. inst)
  local tr = piece.patterns[1].tracks
  local w = tr[0] and tr[0][1] or 0
  check(w & 255 == 36 and w >> 8 & 255 == 0 and w >> 16 & 255 == 204, "bake: the kick on the first step")
  local hats, kicks = 0, 0
  for t = 0, 7 do
    for i, x in ipairs(tr[t] or {}) do
      if x & 255 == 72 then hats = hats + 1 end
      if x & 255 == 36 then kicks = kicks + 1 end
    end
  end
  check(hats == 16 and kicks == 8, "bake: 16 hats and 8 kicks: " .. hats .. " " .. kicks)
  local long = R.bake(R.note("c d e f"):s("pluck"), { cycles = 8 })
  check(#long.patterns == 2 and long.patterns[2].len == 64, "bake: 128 steps in two patterns")
  -- and back: the piece as a pattern again
  local back = R.piece(piece)
  local kicks2, hats2 = 0, 0
  for _, h in ipairs(back:events(0, 2)) do
    if h.v.s == "kick" and h.v.note == 36 then kicks2 = kicks2 + 1 end
    if h.v.s == "hat" then hats2 = hats2 + 1 end
  end
  check(kicks2 == 8 and hats2 == 16, "piece: the baked beat as a pattern again: " .. kicks2 .. " " .. hats2)
  check(#back:events(2, 4) == #back:events(0, 2), "piece: it repeats")
  if ai and ai.music then
    local m = ai.music("ritmo rock", { seed = 1 })
    check(#R.piece(m):events(0, 1) > 8, "piece: a beat of the assistant plays as a pattern")
  end
end

------------------------------------------------------------------ on the sound's clock

local f, phase = 0, 1
local t_start, waited

function _init()
  test_mini()
  test_functions()
  test_values()
  test_docs()
  test_bake()
  R.setcpm(30)
  check(math.abs(R.cps() - 0.5) < 1e-9, "setcpm(30): half a cycle a second")
  R.bpm(120)
  check(math.abs(R.cps() - 0.5) < 1e-9, "bpm(120): a cycle of four beats is two seconds")
  t_start = audio_time()
end

local CODE = [[
-- a beat and a bass
setcpm(60)
drums = s "kick*4, ~ snare, hat*8" :gain(.9)
bass = note "<c2 a1 f1 g1>" :s "acid" :lpf(sine:range(300, 1800):slow(4))
_quiet = s "clap*16"
]]

function _update()
  f = f + 1
  R.update()
  if phase == 1 then
    -- the clock moves with the sound
    if f == 30 then
      local t = audio_time()
      check(t > t_start + 0.4 and t < t_start + 0.6, "audio_time: half a second in 30 frames: " .. (t - t_start))
      local ok, err = R.code(CODE, "live")
      check(ok, "R.code runs: " .. tostring(err))
      local names = table.concat(R.playing(), ",")
      check(names == "drums,bass" or names == "bass,drums", "the globals with a pattern play: " .. names)
      check(math.abs(R.cps() - 1) < 1e-9, "setcpm in the code")
      phase = 2
    end
  elseif phase == 2 then
    if f == 32 then
      waited = play_voices()
      check(waited > 0, "notes queued ahead: " .. tostring(waited))
    end
    if f == 90 then
      -- a note of the drums is lit: its place in the code
      local lit = R.active()
      local words = {}
      for _, l in ipairs(lit) do words[#words + 1] = CODE:sub(l[1] + 1, l[2]) end
      local s = table.concat(words, ",")
      check(s:find("kick") or s:find("hat") or s:find("snare"), "the notes sounding light up their words: " .. s)
      local lits = R._literals(CODE)
      check(lits["kick*4, ~ snare, hat*8"] and lits["kick*4, ~ snare, hat*8"][1] == CODE:find("kick*4", 1, true) - 1,
            "the strings of the code and where they are")
      -- changed: only the drums, slower
      local ok = R.code('drums = s "kick*2"', "live")
      check(ok and table.concat(R.playing(), ",") == "drums", "a new piece of code: what it does not name stops")
      -- an error leaves the music as it is
      local bad, err = R.code('drums = note "zz"', "live")
      check(not bad and tostring(err):find("zz"), "an error says it: " .. tostring(err))
      check(table.concat(R.playing(), ",") == "drums", "and the music goes on")
      local bad2, err2 = R.code('drums = s "kick*2" :lpf(', "live")
      check(not bad2 and err2, "a syntax error: " .. tostring(err2))
      -- an unknown instrument: the pattern stops when it plays, with the error
      R.code('drums = s "nothing*4"', "live")
      phase = 3
    end
  elseif phase == 3 then
    if f == 130 then
      local errs = R.errors()
      check(#errs > 0 and errs[1]:find("nothing"), "an unknown instrument: " .. tostring(errs[1]))
      check(#R.playing() == 0, "and that pattern stops")
      -- the game's own patterns, and the hush
      R.play("theme", R.note("c4 e4 g4 c5"):s("epiano"):room(0.4))
      R.play("beat", R.s("kick ~ kick snare"))
      phase = 4
    end
  elseif phase == 4 then
    if f == 400 then
      check(#R.playing() == 2, "two patterns of the game")
      R.hush()
      check(#R.playing() == 0 and play_voices() == 0, "hush: nothing left")
      phase = 5
    end
  elseif phase == 5 and f == 410 then
    log(string.format("riff: %d/%d checks passed", checks - fails, checks))
    phase = 6
  end
end

function _draw()
  cls(0)
  print("riff test " .. f, 8, 8, 7)
end
