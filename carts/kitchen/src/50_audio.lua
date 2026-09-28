-- Sound. Voices: 0 chopping and washing, 1 hands (pick, place, throw),
-- 2 the kitchen (done, warnings, fire, water), 3 orders and menus,
-- 4 disasters, 5-7 music (bass, chords, melody).
-- The music is original: a chord loop per world, a bass line, arpeggios and
-- a short melody, played faster when the kitchen gets busy.

local function hz(n) return 440 * 2 ^ ((n - 69) / 12) end
Snd.hz = hz
Snd.on = true
Snd.music_on = true

local function play(v, f, ms, wave, vol)
  if Snd.on then note(v, f, ms, wave, vol) end
end

function Snd.init()
  envelope(0, 0, 20, 0, 10)
  envelope(1, 0, 30, 40, 20)
  envelope(2, 1, 40, 120, 40)
  envelope(3, 1, 30, 160, 60)
  envelope(4, 2, 60, 80, 60)
  envelope(5, 1, 40, 150, 30)
  envelope(6, 0, 50, 60, 20)
  envelope(7, 2, 60, 170, 50)
  duty(7, 90)
  duty(6, 48)
end

-- sliding notes (a few frames of freq() changes), one per voice
local slides = {}
local function slide(v, f0, f1, frames, ms, wave, vol)
  play(v, f0, ms, wave, vol)
  slides[v] = { f = f0, df = (f1 - f0) / frames, n = frames }
end
-- short tunes on voice 3
local tune, tune_i, tune_t = nil, 1, 0
local function jingle(notes, wave, vol)
  tune, tune_i, tune_t = notes, 1, 0
  Snd.tune_wave, Snd.tune_vol = wave or SQUARE, vol or 110
end
Snd.jingle = jingle

function Snd.pick() slide(1, 520, 780, 4, 60, SQUARE, 70) end
function Snd.drop() slide(1, 400, 220, 5, 70, TRIANGLE, 90) end
function Snd.plop() slide(1, 300, 140, 6, 90, TRIANGLE, 110) end
function Snd.nope() play(1, 150, 90, SQUARE, 70) end
function Snd.throw() slide(1, 900, 300, 8, 120, NOISE, 60) end
function Snd.catch() slide(1, 600, 1000, 4, 70, SQUARE, 70) end
function Snd.land() play(1, 180, 50, NOISE, 80) end
function Snd.bump() play(1, 120, 60, TRIANGLE, 110) end
function Snd.dash() slide(1, 200, 700, 6, 110, NOISE, 50) end
function Snd.boing() slide(1, 200, 520, 10, 180, TRIANGLE, 120) end
function Snd.trash() slide(1, 260, 90, 10, 160, NOISE, 90) end
function Snd.plate() play(1, 1320, 50, TRIANGLE, 80) end

function Snd.chop() play(0, 1800 + random(400), 30, NOISE, 90) end
function Snd.chopped() slide(0, 700, 1100, 4, 70, SQUARE, 70) end
function Snd.scrub() play(0, 3000 + random(800), 60, NOISE, 50) end

function Snd.ding() play(2, 1568, 180, TRIANGLE, 110) end
function Snd.warn() play(2, 988, 70, SQUARE, 90) end
function Snd.burn() slide(2, 600, 100, 20, 400, NOISE, 110) end
function Snd.fire() slide(2, 200, 900, 20, 500, NOISE, 120) end
function Snd.whoosh() slide(2, 1500, 300, 12, 250, NOISE, 90) end
function Snd.spray() play(2, 5000 + random(1000), 90, NOISE, 45) end
function Snd.splash() slide(2, 1200, 200, 14, 300, NOISE, 110) end

function Snd.order() jingle({ { 1319, 3 }, { 1568, 5 } }, TRIANGLE, 90) end
function Snd.expire() jingle({ { 330, 6 }, { 311, 6 }, { 294, 14 } }, SAW, 90) end
function Snd.wrong() jingle({ { 220, 8 }, { 196, 14 } }, SQUARE, 90) end
function Snd.serve(mult)
  local up = ({ 0, 2, 4, 7 })[mult or 1] or 7
  jingle({ { hz(72 + up), 4 }, { hz(76 + up), 4 }, { hz(79 + up), 4 }, { hz(84 + up), 10 } }, SQUARE, 100)
end
function Snd.ui_move() play(3, 880, 30, SQUARE, 60) end
function Snd.ui_ok() jingle({ { 988, 3 }, { 1319, 6 } }, SQUARE, 90) end
function Snd.ui_back() jingle({ { 660, 3 }, { 440, 6 } }, SQUARE, 80) end
function Snd.cash() jingle({ { 1568, 3 }, { 2093, 3 }, { 2637, 8 } }, SQUARE, 100) end
function Snd.place() slide(1, 180, 520, 14, 250, TRIANGLE, 130) end
function Snd.star(i) jingle({ { hz(76 + i * 4), 6 }, { hz(83 + i * 4), 12 } }, TRIANGLE, 120) end
function Snd.go() jingle({ { 523, 6 }, { 523, 6 }, { 1047, 16 } }, SQUARE, 110) end
function Snd.time_up() jingle({ { 784, 6 }, { 659, 6 }, { 523, 6 }, { 392, 20 } }, SQUARE, 110) end
function Snd.fanfare()
  jingle({ { 523, 6 }, { 659, 6 }, { 784, 6 }, { 1047, 12 }, { 0, 4 }, { 784, 6 }, { 1047, 20 } }, SQUARE, 110)
end

-- disasters
function Snd.squeak() slide(4, 2600, 3400, 5, 80, SQUARE, 60) end
function Snd.quack() slide(4, 700, 420, 8, 120, SAW, 90) end
function Snd.boo() slide(4, 300, 180, 30, 600, TRIANGLE, 110) end
function Snd.alarm() jingle({ { 880, 5 }, { 660, 5 }, { 880, 5 }, { 660, 5 } }, SQUARE, 100) end
function Snd.wind() play(4, 300 + random(200), 200, NOISE, 70) end
function Snd.hiss() slide(4, 4000, 2000, 20, 400, NOISE, 80) end
function Snd.clang() play(4, 1400, 120, SQUARE, 80) end

---------------------------------------------------------------- music

-- chords as scale degrees; melodies as strings of 8th notes: 1-9 degrees
-- (8, 9 = an octave up), '.' rest, '-' hold
local SCALES = { major = { 0, 2, 4, 5, 7, 9, 11 }, minor = { 0, 2, 3, 5, 7, 8, 10 },
                 dorian = { 0, 2, 3, 5, 7, 9, 10 } }
Snd.SONGS = {
  { bpm = 124, root = 48, scale = "major", chords = { 1, 5, 6, 4 }, bass = "bounce", arp = "up",
    mel = { "5-3-1-35", "4-2-7-2-", "3-5-8-65", "4---....", "5-3-1-35", "4-6-8-9-", "8-7-6-5-", "3---1..." } },
  { bpm = 116, root = 53, scale = "major", chords = { 1, 4, 2, 5 }, bass = "walk", arp = "updown",
    mel = { "1.3.5-6-", "5.4.3-..", "2.4.6-7-", "8---....", "1.3.5-6-", "5.4.3-2-", "2.2.5.5.", "1---...." } },
  { bpm = 104, root = 50, scale = "minor", chords = { 1, 6, 7, 1 }, bass = "pulse", arp = "up",
    mel = { "1-2-3-5-", "6-5-3-..", "7-6-5-4-", "5---....", "1-2-3-5-", "8-7-6-5-", "4-3-2-7-", "1---...." } },
  { bpm = 132, root = 52, scale = "minor", chords = { 1, 3, 7, 4 }, bass = "bounce", arp = "down",
    mel = { "5.5.8.7.", "6-5-3-..", "3.3.5.4.", "2---....", "5.5.8.7.", "6-8-9-8-", "7-5-4-2-", "1---...." } },
  { bpm = 96, root = 45, scale = "minor", chords = { 1, 6, 4, 5 }, bass = "pulse", arp = "updown",
    mel = { "1--3--2-", "1--7--5-", "6--5--4-", "3-------", "1--3--5-", "8--7--6-", "5--4--3-", "2---7..." } },
  { bpm = 140, root = 46, scale = "dorian", chords = { 1, 4, 1, 5 }, bass = "walk", arp = "up",
    mel = { "1.1.3.4.", "5-4-3-..", "1.1.3.4.", "5-6-7-..", "8.7.6.5.", "4-3-2-..", "3.4.5.6.", "5---1..." } },
}

local mus = { song = nil, step = 0, t = 0, intensity = 0 }

function Snd.play_song(i)
  if mus.song == Snd.SONGS[i] then return end
  mus.song, mus.step, mus.t = Snd.SONGS[i], 0, 0
end
function Snd.stop_song()
  mus.song = nil
  for v = 5, 7 do noteoff(v) end
end
function Snd.intensity(k) mus.intensity = clamp(k, 0, 1) end

local function degree(song, d, oct)
  local sc = SCALES[song.scale]
  d = d - 1
  local o = d // 7
  return song.root + (oct or 0) * 12 + o * 12 + sc[d % 7 + 1]
end

local function music_step(song, s)
  local bar = s // 16 % 4
  local chord = song.chords[bar + 1]
  local beat = s % 16
  local vol = 70
  -- bass
  local b = song.bass
  if b == "bounce" and beat % 4 == 0 then
    local d = (beat % 8 == 0) and chord or chord + 4
    play(5, hz(degree(song, d)), 180, TRIANGLE, 120)
  elseif b == "walk" and beat % 4 == 0 then
    local d = chord + ({ 0, 2, 4, 5 })[beat // 4 + 1]
    play(5, hz(degree(song, d)), 200, TRIANGLE, 120)
  elseif b == "pulse" and beat % 2 == 0 then
    play(5, hz(degree(song, chord)), 110, TRIANGLE, beat % 8 == 0 and 130 or 90)
  end
  -- arpeggio, only when the kitchen is busy enough
  if mus.intensity > 0.25 and beat % 2 == 1 then
    local tones = { chord, chord + 2, chord + 4, chord + 7 }
    local i = (beat // 2) % 4
    if song.arp == "down" then i = 3 - i elseif song.arp == "updown" then i = ({ 0, 1, 2, 1 })[i + 1] end
    play(6, hz(degree(song, tones[i + 1], 1)), 70, SQUARE, 40 + floor(mus.intensity * 30))
  end
  -- melody (8th notes)
  if beat % 2 == 0 then
    local m = song.mel[(s // 16) % #song.mel + 1]
    local ch = sub(m, beat // 2 + 1, beat // 2 + 1)
    local d = tonumber(ch)
    if d then
      local len = 1
      for k = beat // 2 + 2, 8 do if sub(m, k, k) == "-" then len = len + 1 else break end end
      play(7, hz(degree(song, d, 1)), floor(len * 60000 / song.bpm / 2 * 0.9), SQUARE, vol)
    end
  end
end

function Snd.update(dt)
  for v, sl in pairs(slides) do
    sl.f = sl.f + sl.df
    freq(v, sl.f)
    sl.n = sl.n - 1
    if sl.n <= 0 then slides[v] = nil end
  end
  if tune then
    tune_t = tune_t - 1
    if tune_t <= 0 then
      local n = tune[tune_i]
      if not n then tune = nil
      else
        if n[1] > 0 then play(3, n[1], n[2] * 16, Snd.tune_wave, Snd.tune_vol) end
        tune_t, tune_i = n[2], tune_i + 1
      end
    end
  end
  local song = mus.song
  if song and Snd.music_on and Snd.on then
    local bpm = song.bpm * (1 + mus.intensity * 0.3)
    mus.t = mus.t + dt
    local step = 60 / bpm / 4
    while mus.t >= step do
      mus.t = mus.t - step
      music_step(song, mus.step)
      mus.step = mus.step + 1
    end
  end
end
