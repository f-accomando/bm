-- Sound. Voices: 0 hits and guards, 1 the robots moving (swings, jumps,
-- boosters, steps), 2 the weapons, 3 menus and announcements, 4 heavy
-- things (armour breaking, explosions, landings), 5-7 music (bass, chords,
-- lead). The music is original: a fight theme, a theme for the hangar and
-- one for the title, in the style of the 16-bit fighting games.

local function hz(n) return 440 * 2 ^ ((n - 69) / 12) end
Snd.on = true

local function play(v, f, ms, wave, vol)
  if Snd.on then note(v, f, ms, wave, vol) end
end

function Snd.init()
  envelope(0, 0, 30, 0, 20)
  envelope(1, 0, 40, 30, 30)
  envelope(2, 0, 25, 0, 15)
  envelope(3, 1, 30, 160, 50)
  envelope(4, 1, 80, 60, 90)
  envelope(5, 1, 30, 170, 25)
  envelope(6, 0, 40, 50, 20)
  envelope(7, 2, 50, 180, 40)
  duty(6, 40)
  duty(7, 80)
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

-- the robots
function Snd.swing(rank) slide(1, 700 + rank * 60, 250, 6, 90 + rank * 15, NOISE, 45 + rank * 8) end
function Snd.jump() slide(1, 150, 520, 10, 180, NOISE, 80) end
function Snd.boost() slide(1, 220, 900, 12, 220, NOISE, 90) end
function Snd.land(heavy) slide(4, heavy and 160 or 220, 60, 8, heavy and 220 or 140, NOISE, heavy and 130 or 90) end
function Snd.step() play(1, 90, 40, TRIANGLE, 70) end

-- hits
function Snd.hit(heavy)
  if heavy then slide(0, 900, 90, 10, 200, NOISE, 140) else slide(0, 1400, 300, 6, 110, NOISE, 110) end
end
function Snd.block() slide(0, 2200, 1500, 4, 70, SQUARE, 60) end
function Snd.crack() slide(4, 3000, 1200, 8, 160, NOISE, 110) end
function Snd.armor_break()
  slide(4, 1800, 80, 22, 420, NOISE, 150)
  jingle({ { 988, 3 }, { 740, 3 }, { 494, 8 } }, SAW, 90)
end
function Snd.boom() slide(4, 400, 40, 26, 480, NOISE, 150) end

-- weapons
function Snd.draw_sword() slide(2, 600, 2400, 10, 200, SAW, 70) end
function Snd.shot() slide(2, 1600 + random(200), 400, 3, 50, NOISE, 110) end
function Snd.overheat() slide(2, 3000, 600, 30, 600, NOISE, 100) end

-- menus and the fight's announcements
function Snd.ui_move() play(3, 880, 30, SQUARE, 60) end
function Snd.ui_ok() jingle({ { 988, 3 }, { 1319, 6 } }, SQUARE, 90) end
function Snd.ui_back() jingle({ { 660, 3 }, { 440, 6 } }, SQUARE, 80) end
function Snd.clank() slide(4, 500, 180, 6, 120, SQUARE, 90) end
function Snd.weld() play(2, 4000 + random(2000), 40, NOISE, 40) end
function Snd.round() jingle({ { 392, 6 }, { 523, 6 }, { 659, 12 } }, SQUARE, 100) end
function Snd.fight() jingle({ { 784, 4 }, { 784, 4 }, { 1047, 18 } }, SAW, 110) end
function Snd.ko() jingle({ { 523, 8 }, { 392, 8 }, { 262, 30 } }, SAW, 120) end
function Snd.win()
  jingle({ { 523, 6 }, { 659, 6 }, { 784, 6 }, { 1047, 12 }, { 0, 4 }, { 988, 6 }, { 1047, 24 } }, SQUARE, 110)
end

---------------------------------------------------------------- music

-- each song: chords (semitones from the root) per bar, a bass pattern and a
-- lead as strings of 16th notes: a letter is a note of the scale (a is the
-- root, b the 2nd... h the octave), '-' holds, '.' rests
local MINOR = { 0, 2, 3, 5, 7, 8, 10 }
local MAJOR = { 0, 2, 4, 5, 7, 9, 11 }
local SONGS = {
  fight = { bpm = 150, root = 40, scale = MINOR, chords = { 0, 0, -4, -2, 0, 0, 3, -2 }, bass = "drive",
            lead = { "e---d-e-g---e-d-", "b---a---b-d-e---", "e---d-e-g---a-g-", "h---g---e---d---",
                     "e-e-g-e-a---g-e-", "d-e-d-b-a-------", "c---d---e---g---", "f---e---d---b---" } },
  hangar = { bpm = 96, root = 45, scale = MINOR, chords = { 0, -4, -2, -5 }, bass = "pulse",
             lead = { "a-------e---d-c-", "d-------c---b---", "c---d---e---g---", "e-------........" } },
  title = { bpm = 128, root = 43, scale = MAJOR, chords = { 0, 5, -3, 7 }, bass = "octave",
            lead = { "e---e-f-g---e---", "f---f-e-d---c---", "c---d-e-f---a---", "g-----------....",
                     "e---e-f-g---c'--", "b---a-g-a---e---", "d---e-f-g---f-e-", "c-----------...." } },
}
Snd.SONGS = SONGS

local mus = { song = nil, step = 0, t = 0 }

function Snd.play_song(name)
  local s = SONGS[name]
  if mus.song == s then return end
  mus.song, mus.step, mus.t = s, 0, 0
end
function Snd.stop_song()
  mus.song = nil
  for v = 5, 7 do noteoff(v) end
end

local LETTER = { a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8 }

local function degree(song, d, oct)
  d = d - 1
  return song.root + (oct or 0) * 12 + (d // 7) * 12 + song.scale[d % 7 + 1]
end

local function music_step(song, s)
  local nbars = #song.chords
  local bar = s // 16 % nbars
  local chord = song.chords[bar + 1]
  local beat = s % 16
  local root = song.root + chord
  -- bass
  local b = song.bass
  if b == "drive" and beat % 2 == 0 then
    local up = (beat == 6 or beat == 14) and 12 or 0
    play(5, hz(root - 12 + up), 90, TRIANGLE, beat % 4 == 0 and 140 or 100)
  elseif b == "pulse" and beat % 4 == 0 then
    play(5, hz(root - 12), 240, TRIANGLE, 120)
  elseif b == "octave" and beat % 2 == 0 then
    play(5, hz(root - 12 + (beat % 4 == 2 and 12 or 0)), 110, TRIANGLE, 110)
  end
  -- chords: a stab on the off-beats (a minor or major third over the root)
  if beat % 4 == 2 then
    local third = song.scale == MINOR and 3 or 4
    play(6, hz(root + 12 + ((beat // 4) % 2 == 0 and third or 7)), 80, SQUARE, 38)
  end
  -- the lead
  local line = song.lead[(s // 16) % #song.lead + 1]
  local ch = sub(line, beat + 1, beat + 1)
  local d = LETTER[ch]
  if d then
    local len = 1
    for k = beat + 2, 16 do
      local c = sub(line, k, k)
      if c == "-" or c == "'" then len = len + 1 else break end
    end
    local oct = sub(line, beat + 2, beat + 2) == "'" and 2 or 1
    play(7, hz(degree(song, d, oct)), floor(len * 60000 / song.bpm / 4 * 0.92), SQUARE, 62)
  end
end

function Snd.update()
  for v, sl in pairs(slides) do
    sl.f = sl.f + sl.df
    if Snd.on then freq(v, sl.f) end
    sl.n = sl.n - 1
    if sl.n <= 0 then slides[v] = nil end
  end
  if tune then
    tune_t = tune_t - 1
    if tune_t <= 0 then
      local n = tune[tune_i]
      if not n then
        tune = nil
      else
        if n[1] > 0 then play(3, n[1], n[2] * 16, Snd.tune_wave, Snd.tune_vol) end
        tune_t, tune_i = n[2], tune_i + 1
      end
    end
  end
  local song = mus.song
  if song and Snd.on then
    mus.t = mus.t + 1 / 60
    local step = 60 / song.bpm / 4
    while mus.t >= step do
      mus.t = mus.t - step
      music_step(song, mus.step)
      mus.step = mus.step + 1
    end
  end
end
