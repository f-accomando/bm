-- bm Sound: the sounds, sound effects and music of .bm cartridges.
--
-- Four pages: SOUNDS (the instruments: wave, envelope and pitch, then the
-- filter and its motion, then the wave's own settings and the place, room
-- and echo; ready-made ones from the menu), SFX (sound effects for games),
-- PATTERN (an 8-track step sequencer, one track per voice) and SONG (the
-- order of the patterns, the echo on the beat, the room). What it edits is
-- the AUDIO section of a .bm, the same bank sfx() and music() play in the
-- games.
--
-- Gamepad: d-pad moves; A tap adds / removes, A + up/down / left/right
-- changes the note (or the value); Y + up/down / left/right sound and
-- volume; B + up/down / left/right effect and amount; X clears; START
-- plays; SELECT + left/right changes page, SELECT + up/down the item,
-- SELECT alone opens the menu.
-- Keyboard: F1-F4 pages, Esc menu, arrows, Enter, Space plays, Backspace
-- clears, the two rows Z S X D C... and Q 2 W 3 E... are a piano, hold
-- F12 for every key.

local W, H = SCREEN_W, SCREEN_H

----------------------------------------------------------------- look

local C = {
  bg = 0x111318, panel = 0x1B1E25, panel2 = 0x242832, line = 0x323845, cell = 0x1F232B,
  text = 0xE9EBEF, dim = 0x8A92A2, faint = 0x4C5361, dark = 0x0B0C0F,
  blue = 0x3C8CE7, green = 0x35C46E, white = 0xDDE0E6, orange = 0xF2701D,
  red = 0xE8463A, yellow = 0xF2C230, purple = 0xA472F2, cyan = 0x2EC8D2, pink = 0xF0609E,
}
local TRACK_C = { C.orange, C.yellow, C.green, C.cyan, C.blue, C.purple, C.pink, C.white }
local WAVE_C = { C.blue, C.green, C.orange, C.white, C.cyan, C.yellow, C.pink, C.purple, C.red, C.green }
local PAGE_C = { C.blue, C.green, C.orange, C.purple }

local WAVES = { "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL", "FM", "PLUCK", "SUPERSAW", "ORGAN" }
local NOTE = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }
local CHORDS = { "OCTAVE", "MAJOR", "MINOR", "SUS2", "SUS4", "MAJ7", "MIN7", "DOM7", "DIM", "AUG",
                 "POWER", "POWER+8", "MAJOR+8", "MINOR+8", "OCT DOWN", "2 OCTAVES" }
-- effects: one letter in the lanes, the name, and what the amount n means
local FX = {
  [0] = { " ", "NONE", function() return "" end },
  { "G", "GLIDE", function(n) return string.format("%g step%s", (n + 1) / 4, n == 3 and "" or "s") end },
  { "U", "BEND UP", function(n) return n .. " semitones" end },
  { "D", "BEND DOWN", function(n) return n .. " semitones" end },
  { "V", "VIBRATO", function(n) return string.format("%g semitone", n / 8) end },
  { "T", "TREMOLO", function(n) return math.floor(n * 100 / 15) .. "%" end },
  { "C", "CHORD", function(n) return CHORDS[n + 1] end },
  { "A", "ARPEGGIO", function(n) return CHORDS[n + 1] end },
  { "O", "FADE OUT", function(n) return (n + 1) .. " step" .. (n > 0 and "s" or "") end },
  { "I", "FADE IN", function(n) return (n + 1) .. " step" .. (n > 0 and "s" or "") end },
  { "R", "RETRIGGER", function(n) return (n + 1) .. " hits" end },
  { "L", "DELAY", function(n) return n .. "/16 step" end },
  { "X", "CUT", function(n) return (n + 1) .. "/16 step" end },
}
local NFX = 12

local function note_name(n)
  if n == 0 then return "" end
  if n >= 128 then return "OFF" end
  return NOTE[n % 12 + 1] .. (n // 12 - 1)
end

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end

-- colour a towards b by k (0..1)
local function mix(a, b, k)
  local function ch(sh) local x, y = a >> sh & 255, b >> sh & 255; return math.floor(x + (y - x) * k + 0.5) << sh end
  return ch(16) | ch(8) | ch(0)
end

----------------------------------------------------------------- the bank

-- A step is one integer: note | sound << 8 | vol << 16 | fx << 24
-- (note 0: nothing new, 1..127 MIDI, 128 off; fx: type << 4 | amount).
local function mk(note, sound, vol, fx) return note | sound << 8 | vol << 16 | fx << 24 end
local function s_note(s) return s & 255 end
local function s_sound(s) return s >> 8 & 255 end
local function s_vol(s) return s >> 16 & 255 end
local function s_fx(s) return s >> 24 & 255 end

local B = {}
B.NSOUND, B.NSFX, B.NPAT, B.NSONG = 32, 64, 64, 8
-- the tone of a sound: registers 11..31 of the voice (src/audio/synth.h),
-- tone[1] is register 11; the plain one has a little room (AU_ROOM_SEND)
B.TONE, B.ROOM_SEND = 21, 40
local function tone_default()
  local t = {}
  for i = 1, B.TONE do t[i] = 0 end
  t[22 - 10] = B.ROOM_SEND
  return t
end
B.tone_default = tone_default

function B.sound()
  return { name = "", wave = 0, duty = 128, vol = 200, a = 1, d = 0, s = 255, r = 10,
           pitch = 0, ptime = 0, vdepth = 0, vrate = 0, detune = 0, tone = tone_default() }
end
function B.sfx() return { name = "", ms = 60, len = 8, ls = 0, le = 0, steps = {} } end
function B.pat() return { len = 16, tracks = {} } end
function B.song() return { name = "", bpm = 120, swing = 0, loop = 0, echo = 0, room = 0, order = {} } end

function B.new()
  local b = { sounds = {}, sfx = {}, pats = {}, songs = {} }
  for i = 1, B.NSOUND do b.sounds[i] = B.sound() end
  for i = 1, B.NSFX do b.sfx[i] = B.sfx() end
  for i = 1, B.NPAT do b.pats[i] = B.pat() end
  for i = 1, B.NSONG do b.songs[i] = B.song() end
  return b
end

-- a new project: one sound to start with, one song with one pattern
function B.blank()
  local b = B.new()
  b.sounds[1].name = "SOUND"
  b.songs[1].name = "SONG"
  b.songs[1].order = { 0 }
  return b
end

local SOUND_KEYS = { "wave", "duty", "vol", "a", "d", "s", "r", "pitch", "ptime", "vdepth", "vrate", "detune" }

function B.sound_empty(s)
  local d = B.sound()
  if s.name ~= "" then return false end
  for _, k in ipairs(SOUND_KEYS) do if s[k] ~= d[k] then return false end end
  for i = 1, B.TONE do if s.tone[i] ~= d.tone[i] then return false end end
  return true
end
function B.sfx_empty(x)
  if x.name ~= "" then return false end
  for k = 1, x.len do local st = x.steps[k] or 0; if s_note(st) ~= 0 or s_fx(st) ~= 0 then return false end end
  return true
end
function B.track_empty(p, t)
  local tr = p.tracks[t]
  if not tr then return true end
  for k = 1, p.len do local st = tr[k] or 0; if s_note(st) ~= 0 or s_fx(st) ~= 0 then return false end end
  return true
end
function B.pat_empty(p)
  for t = 1, 8 do if not B.track_empty(p, t) then return false end end
  return true
end
function B.song_empty(s) return s.name == "" and #s.order == 0 end

local function last_used(list, empty)
  for i = #list, 1, -1 do if not empty(list[i]) then return i end end
  return 0
end

local function name8(s) return (s:upper():gsub("[^%w%-_ !%.]", ""):sub(1, 8)) end
local function unname(s) return (s:match("^[^%z]*")) end

local PSOUND = "<c8BBBBBBBbBBBbxxxx"   -- then the tone, 21 bytes, and 3 reserved
local PSFX = "<c8HBBBxxx"
local PSONG = "<c8BBBBBBxx"

function B.pack(b)
  local ns = last_used(b.sounds, B.sound_empty)
  local nx = last_used(b.sfx, B.sfx_empty)
  local np = last_used(b.pats, B.pat_empty)
  local ng = last_used(b.songs, B.song_empty)
  local out = { "BMAU", string.pack("<BBBBBxxxxxxx", 2, ns, nx, np, ng) }
  for i = 1, ns do
    local s = b.sounds[i]
    out[#out + 1] = string.pack(PSOUND, name8(s.name), s.wave, s.duty, s.vol, s.a, s.d, s.s, s.r,
                                s.pitch, s.ptime, s.vdepth, s.vrate, s.detune)
    out[#out + 1] = string.char(table.unpack(s.tone, 1, B.TONE)) .. "\0\0\0"
  end
  for i = 1, nx do
    local x = b.sfx[i]
    local ls, le = x.ls, x.le
    if le > x.len or ls >= le then ls, le = 0, 0 end
    out[#out + 1] = string.pack(PSFX, name8(x.name), x.ms, x.len, ls, le)
    for k = 1, x.len do out[#out + 1] = string.pack("<I4", x.steps[k] or 0) end
  end
  for i = 1, np do
    local p = b.pats[i]
    local mask, body = 0, {}
    for t = 1, 8 do
      if not B.track_empty(p, t) then
        mask = mask | 1 << (t - 1)
        local tr = p.tracks[t]
        for k = 1, p.len do body[#body + 1] = string.pack("<I4", tr[k] or 0) end
      end
    end
    out[#out + 1] = string.pack("<BBxx", p.len, mask)
    out[#out + 1] = table.concat(body)
  end
  for i = 1, ng do
    local s = b.songs[i]
    local order = #s.order > 0 and s.order or { 0 }
    local loop = s.loop
    if loop ~= 255 and loop >= #order then loop = 0 end
    out[#out + 1] = string.pack(PSONG, name8(s.name), s.bpm, s.swing, #order, loop, s.echo or 0, s.room or 0)
    for k = 1, #order do out[#out + 1] = string.char(order[k]) end
  end
  return table.concat(out)
end

function B.parse(data)
  if type(data) ~= "string" or #data < 16 or data:sub(1, 4) ~= "BMAU" then return nil, "not a sound bank" end
  local ver, ns, nx, np, ng = data:byte(5, 9)
  if ver ~= 1 and ver ~= 2 then return nil, "unsupported sound bank version" end
  local b = B.new()
  local ok, err = pcall(function()
    local pos = 17
    for i = 1, ns do
      local s = b.sounds[i]
      local name
      name, s.wave, s.duty, s.vol, s.a, s.d, s.s, s.r, s.pitch, s.ptime, s.vdepth, s.vrate, s.detune, pos =
        string.unpack(PSOUND, data, pos)
      s.name = unname(name)
      if s.wave >= #WAVES then s.wave = 0 end
      if ver >= 2 then
        s.tone = { data:byte(pos, pos + B.TONE - 1) }
        pos = pos + 24
      end
    end
    for i = 1, nx do
      local x = b.sfx[i]
      local name
      name, x.ms, x.len, x.ls, x.le, pos = string.unpack(PSFX, data, pos)
      x.name = unname(name)
      for k = 1, x.len do x.steps[k], pos = string.unpack("<I4", data, pos) end
    end
    for i = 1, np do
      local p = b.pats[i]
      local mask
      p.len, mask, pos = string.unpack("<BBxx", data, pos)
      for t = 1, 8 do
        if mask & 1 << (t - 1) ~= 0 then
          local tr = {}
          for k = 1, p.len do tr[k], pos = string.unpack("<I4", data, pos) end
          p.tracks[t] = tr
        end
      end
    end
    for i = 1, ng do
      local s = b.songs[i]
      local name, n
      local echo, room
      name, s.bpm, s.swing, n, s.loop, echo, room, pos = string.unpack(PSONG, data, pos)
      s.name = unname(name)
      if ver >= 2 then s.echo, s.room = math.min(echo, 16), room end
      for k = 1, n do s.order[k] = data:byte(pos + k - 1) end
      pos = pos + n
    end
  end)
  if not ok then return nil, "the sound bank is broken" end
  return b
end

function B.copy(v)
  if type(v) ~= "table" then return v end
  local t = {}
  for k, x in pairs(v) do t[k] = B.copy(x) end
  return t
end

-- the sounds a list of steps uses
local function sounds_of_steps(steps, n, used)
  for k = 1, n do
    local st = steps[k] or 0
    if s_note(st) > 0 and s_note(st) < 128 then used[s_sound(st)] = true end
  end
end

----------------------------------------------------------------- state

local bank = B.blank()
local proj = { path = nil, title = "", is_game = false, dirty = false }
local cur = { page = 1, sound = 0, sfx = 0, pat = 0, song = 0 }
local octave = 4                      -- of the piano keys
local frame = 0
local msg, msg_c, msg_t = nil, C.text, 0
local bank_changed = true             -- the player needs the new bank
local overlay = nil                   -- menu, picker, name editor, question

local function say(s, c, t) msg, msg_c, msg_t = s, c or C.text, t or 200 end
-- what happens to files goes to the console's log too (Dev > Log, the serial port)
local function note_log(s) log("sound: " .. s) end

----------------------------------------------------------------- undo

local undo, undo_group = {}, 0
local function begin_edit() undo_group = undo_group + 1 end
local function set(t, k, v)
  if t[k] == v then return end
  undo[#undo + 1] = { t, k, t[k], undo_group }
  if #undo > 2000 then table.remove(undo, 1) end
  t[k] = v
  proj.dirty = true
  bank_changed = true
end
local function undo_last()
  if #undo == 0 then say("nothing to undo", C.dim) return end
  local g = undo[#undo][4]
  while #undo > 0 and undo[#undo][4] == g do
    local u = table.remove(undo)
    u[1][u[2]] = u[3]
  end
  bank_changed = true
  proj.dirty = true
  say("undone", C.dim, 60)
end

----------------------------------------------------------------- drawing

local ui = {}

function ui.text(s, x, y, c, scale) return print(s, x, y, c or C.text, scale) end
function ui.textc(s, x, y, c, scale)
  scale = scale or 1
  print(s, x - #s * 4 * scale, y, c or C.text, scale)
end
function ui.textr(s, x, y, c) print(s, x - #s * 8, y, c or C.text) end

-- a rectangle with cut corners (reads as rounded at this size)
function ui.box(x, y, w, h, c)
  rectfill(x + 1, y, w - 2, h, c)
  rectfill(x, y + 1, 1, h - 2, c)
  rectfill(x + w - 1, y + 1, 1, h - 2, c)
end
function ui.frame(x, y, w, h, c)
  rectfill(x + 1, y, w - 2, 1, c)
  rectfill(x + 1, y + h - 1, w - 2, 1, c)
  rectfill(x, y + 1, 1, h - 2, c)
  rectfill(x + w - 1, y + 1, 1, h - 2, c)
end
function ui.frame2(x, y, w, h, c)
  ui.frame(x, y, w, h, c)
  ui.frame(x + 1, y + 1, w - 2, h - 2, c)
end

-- small icons, drawn (the font has no arrows)
function ui.arrow(x, y, dir, c)
  if dir == "u" then tri(x + 4, y + 3, x, y + 10, x + 8, y + 10, c)
  elseif dir == "d" then tri(x, y + 5, x + 8, y + 5, x + 4, y + 12, c)
  elseif dir == "l" then tri(x + 1, y + 8, x + 7, y + 4, x + 7, y + 12, c)
  elseif dir == "r" then tri(x + 7, y + 8, x + 1, y + 4, x + 1, y + 12, c)
  elseif dir == "ud" then
    tri(x + 4, y + 1, x, y + 6, x + 8, y + 6, c)
    tri(x, y + 9, x + 8, y + 9, x + 4, y + 14, c)
  elseif dir == "lr" then
    tri(x, y + 8, x + 5, y + 4, x + 5, y + 12, c)
    tri(x + 12, y + 8, x + 7, y + 4, x + 7, y + 12, c)
  end
end
function ui.play_icon(x, y, c) tri(x, y, x, y + 12, x + 10, y + 6, c) end
function ui.stop_icon(x, y, c) rectfill(x, y + 1, 10, 10, c) end

-- the hints as chips (prompt()): keys or pad buttons, then the label;
-- returns the x after it
local function snap(x) return (x + 7) // 8 * 8 end   -- text stays on its 8 px columns
function ui.chips(keys, label, x, y)
  for _, k in ipairs(keys) do x = prompt(k, x, y) + 1 end
  return print(label, snap(x + 2), y, C.dim) + 8
end
function ui.chips_w(keys, label, x)
  local kx = x
  for _, k in ipairs(keys) do kx = kx + prompt(k) + 1 end
  return snap(kx + 2) + #label * 8 + 8 - x
end

-- a pad button and the direction held with it ("ud", "lr"), as on the pad;
-- with the keyboard in use, the keys that do the same (see HELP)
local DIRS = { u = "UP", d = "DOWN", l = "LEFT", r = "RIGHT", ud = "UPDOWN", lr = "LEFTRIGHT" }
local KEYS_OF = {
  A = { "enter" }, Aud = { "-", "=" }, Alr = { "_", "+" }, Yud = { "[", "]" }, Ylr = { ";", "'" },
  B = { "esc" }, Bud = { "k", "l" }, Blr = { "o", "p" }, X = { "backspace" }, Y = { "i" }, START = { "space" },
  SELECT = { "esc" },
}
local function hint_keys(btn, dir)
  local src = lastinput()
  local k = src ~= "pad" and src ~= "ds4" and KEYS_OF[btn .. (dir or "")]
  return k or (dir and { btn, DIRS[dir] } or { btn })
end
function ui.hint(x, y, btn, dir, label) return ui.chips(hint_keys(btn, dir), label, x, y) end
function ui.hint_w(x, btn, dir, label) return ui.chips_w(hint_keys(btn, dir), label, x) end

-- a value bar: label, value text, fill 0..1
function ui.bar(x, y, w, label, value, k, c, selected)
  if selected then ui.box(x - 4, y - 3, w + 8, 36, C.panel2) ui.frame2(x - 4, y - 3, w + 8, 36, c) end
  print(label, x, y, selected and C.text or C.dim)
  ui.textr(value, x + w, y, selected and C.text or C.dim)
  rectfill(x, y + 20, w, 6, C.dark)
  local f = math.floor(clamp(k, 0, 1) * w + 0.5)
  if f > 0 then rectfill(x, y + 20, f, 6, c) end
end

-- the pages draw the help line with these
local hints = {}
local function hint_line(list) hints = list end

----------------------------------------------------------------- input

local held, rep = {}, {}
local function read_pad()
  for i = 0, 9 do
    if btn(i) then held[i] = (held[i] or 0) + 1 else held[i] = 0 end
    local h = held[i]
    rep[i] = h == 1 or (h > 14 and h % 4 == 0)
  end
end

-- piano: two rows of the keyboard, the lower one at `octave`
local PIANO = {}
do
  local lower, upper = "zsxdcvgbhnjm", "q2w3er5t6y7u"
  for i = 1, 12 do PIANO[lower:sub(i, i)] = i - 1; PIANO[upper:sub(i, i)] = i + 11 end
end

----------------------------------------------------------------- sound previews

local PREVIEW_VOICE = 7
local function preview(voice, sound, note, vol, fx, ms)
  if note > 0 and note < 128 then audio_play(voice, sound, note, vol or 255, fx or 0, ms or 350) end
end
local function preview_step(voice, st)
  preview(voice, s_sound(st), s_note(st), s_vol(st), s_fx(st))
end

----------------------------------------------------------------- shared views

local TOP, BOTTOM = 34, 336           -- the area between the top bar and the help line

-- a list of slots on the left: items, the selected index (0-based), label(i)
local function slot_list(title, n, sel, label, used, c)
  print(title, 8, TOP + 4, C.dim)
  local rows = 13
  local first = clamp(sel - rows // 2, 0, math.max(0, n - rows))
  for r = 0, rows - 1 do
    local i = first + r
    if i >= n then break end
    local y = TOP + 26 + r * 21
    local s = i == sel
    if s then ui.box(4, y - 2, 144, 20, c) end
    print(string.format("%02d", i), 8, y, s and C.dark or C.faint)
    local l = label(i)
    print(l ~= "" and l or (used(i) and "-" or ""), 32, y, s and C.dark or (used(i) and C.text or C.faint))
  end
  if first > 0 then ui.arrow(140, TOP + 18, "u", C.dim) end
  if first + rows < n then ui.arrow(140, TOP + 26 + rows * 21 - 6, "d", C.dim) end
end

local function ms_text(rate)
  local ms = rate * 2000 / 255
  if ms >= 1000 then return string.format("%.1f s", ms / 1000) end
  return math.floor(ms + 0.5) .. " ms"
end
local function pct(v) return math.floor(v * 100 / 255 + 0.5) .. "%" end

----------------------------------------------------------------- SOUNDS

-- the parameters of a sound, in three groups that scroll with up and down
-- (SOUND, FILTER, WAVE & SPACE). A parameter: label, min, max, text of
-- the value, step of left/right, and how it is read and written: a field
-- of the sound, or a byte of its tone (registers 11..31, s.tone[1] is 11)
local function field(key, label, lo, hi, fmt, step)
  return { label = label, lo = lo, hi = hi, fmt = fmt, step = step,
           get = function(s) return s[key] end, put = function(s, v) set(s, key, v) end }
end
local function tbyte(i, label, lo, hi, fmt, step, signed)
  return { label = label, lo = lo, hi = hi, fmt = fmt, step = step,
           get = function(s) local v = s.tone[i]; return signed and v > 127 and v - 256 or v end,
           put = function(s, v) set(s.tone, i, v & 255) end }
end
local function tbits(i, mask, shift, label, lo, hi, fmt)
  return { label = label, lo = lo, hi = hi, fmt = fmt, step = 1,
           get = function(s) return (s.tone[i] & mask) >> shift end,
           put = function(s, v) set(s.tone, i, (s.tone[i] & ~mask & 255) | (v << shift & mask)) end }
end
local function off_or(f) return function(v) return v == 0 and "OFF" or f(v) end end
local function hz_text(hz) return hz >= 1000 and string.format("%.1f kHz", hz / 1000) or math.floor(hz + 0.5) .. " Hz" end
local function cutoff_hz(v) return 20 * 2 ^ ((v - 1) * 9.9658 / 254) end
local function lfo_hz(v) return 0.1 * 2 ^ ((v - 1) * 7.64 / 254) end
-- the wave's own settings: what they mean depends on the wave (synth.h)
local MOD_LABEL = { [6] = { "FM RATIO", "FM DEPTH" }, [7] = { "BRIGHT", "RING" }, [8] = { "SPREAD", "-" },
                    [9] = { "BARS 1 2", "BARS 3 4" } }
local function mod_text(n)
  return function(v, s)
    local w = s.wave
    if w == 6 then
      if n == 1 then return v == 0 and "1 AUTO" or string.format("%g", v / 16) end
      return v == 0 and "1.3 AUTO" or string.format("%.1f", v / 32)
    elseif w == 7 then return v == 0 and "AUTO" or pct(v)
    elseif w == 8 then return n == 1 and (v == 0 and "AUTO" or pct(v)) or "-"
    elseif w == 9 then
      if s.tone[7] == 0 and s.tone[8] == 0 then return "AUTO" end
      return (v >> 4) .. " " .. (v & 15)
    end
    return "-"
  end
end
local FILTERS = { "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH" }
local SGROUPS = {
  { name = "SOUND", c = C.blue, cols = {
    { field("wave", "WAVE", 0, #WAVES - 1, function(v) return WAVES[v + 1] end, 1),
      field("duty", "DUTY", 0, 255, pct, 16), field("vol", "VOLUME", 0, 255, pct, 16),
      field("detune", "DETUNE", -99, 99, function(v) return string.format("%+d cent", v) end, 10) },
    { field("a", "ATTACK", 0, 255, ms_text, 10), field("d", "DECAY", 0, 255, ms_text, 10),
      field("s", "SUSTAIN", 0, 255, pct, 16), field("r", "RELEASE", 0, 255, ms_text, 10) },
    { field("pitch", "BEND FROM", -48, 48, function(v) return string.format("%+d st", v) end, 12),
      field("ptime", "BEND TIME", 0, 255, function(v) return v == 0 and "off" or (v * 10) .. " ms" end, 10),
      field("vdepth", "VIBRATO", 0, 200, function(v) return v .. " cent" end, 10),
      field("vrate", "VIB SPEED", 0, 200, function(v) return string.format("%.1f Hz", v / 10) end, 10) } } },
  { name = "FILTER", c = C.green, cols = {
    { tbyte(1, "CUTOFF", 0, 255, off_or(function(v) return hz_text(cutoff_hz(v)) end), 12),
      tbyte(2, "RESONANCE", 0, 255, pct, 16),
      tbits(3, 3, 0, "MODE", 0, 3, function(v) return FILTERS[v + 1] end),
      tbits(3, 4, 2, "KEY TRACK", 0, 1, function(v) return v == 1 and "ON" or "OFF" end) },
    { tbyte(4, "FILT ENV", -127, 127, function(v) return string.format("%+.1f oct", v / 16) end, 16, true),
      tbyte(5, "ENV DECAY", 0, 255, function(v) return v == 0 and "HOLD" or ms_text(v) end, 10),
      tbyte(11, "DRIVE", 0, 255, off_or(pct), 16), tbyte(10, "NOISE", 0, 255, off_or(pct), 16) },
    { tbyte(14, "LFO SPEED", 0, 255, off_or(function(v) return string.format("%.1f Hz", lfo_hz(v)) end), 12),
      tbyte(15, "LFO CUT", 0, 255, off_or(function(v) return string.format("%.1f oct", v * 4 / 255) end), 16),
      tbyte(16, "LFO DUTY", 0, 255, off_or(pct), 16),
      tbits(18, 1, 0, "8-BIT", 0, 1, function(v) return v == 1 and "ON" or "OFF" end) } } },
  { name = "WAVE & SPACE", c = C.orange, cols = {
    { tbyte(7, function(s) return (MOD_LABEL[s.wave] or { "WAVE SET 1" })[1] end, 0, 255, mod_text(1), 16),
      tbyte(8, function(s) return (MOD_LABEL[s.wave] or { "", "WAVE SET 2" })[2] end, 0, 255, mod_text(2), 16),
      tbyte(9, "FM FADE", 0, 255, function(v, s) return s.wave ~= 6 and "-" or v == 0 and "STAYS" or ms_text(v) end, 10),
      tbyte(17, "FM FEEDBACK", 0, 255, function(v, s) return s.wave ~= 6 and "-" or pct(v) end, 16) },
    { tbyte(6, "PAN", -127, 127, function(v)
        return v == 0 and "CENTER" or (v < 0 and "LEFT " or "RIGHT ") .. math.floor(math.abs(v) * 100 / 127) .. "%" end, 16, true),
      tbyte(12, "ROOM", 0, 255, off_or(pct), 16), tbyte(13, "ECHO", 0, 255, off_or(pct), 16) } } },
}
local function plabel(d, s) return type(d.label) == "function" and d.label(s) or d.label end
local SCOL_X = { 168, 324, 480 }

local sp = { group = 1, col = 1, row = 1 }      -- row 0: the name
local last_note = 60                 -- the note the previews and new steps use

local function sound_param(g, col, row)
  local c = SGROUPS[g].cols[col]
  return c and c[row]
end

-- one cycle of the wave, as the synthesizer makes it (for the picture)
local function wave_y(s, p, seed)
  local w, duty = s.wave, s.duty
  local function saw(x) return 2 * (x % 1) - 1 end
  if w == 0 then return p < duty / 256 and 1 or -1
  elseif w == 1 then return p < 0.5 and -1 + 4 * p or 3 - 4 * p
  elseif w == 2 then return saw(p)
  elseif w == 4 then return math.sin(p * 2 * math.pi)
  elseif w == 6 then
    local ratio = s.tone[7] > 0 and s.tone[7] / 16 or 1
    local depth = (s.tone[8] > 0 and s.tone[8] or 40) / 32
    return math.sin(2 * math.pi * p + depth * math.sin(2 * math.pi * p * ratio))
  elseif w == 7 then
    local k = 0.18
    return ((p < k and p / k or 1 - (p - k) / (1 - k)) * 2 - 1) * 0.9
  elseif w == 8 then
    return (saw(p) + 0.75 * saw(p * 1.035 + 0.3) + 0.75 * saw(p * 0.965 + 0.6)) * 0.55
  elseif w == 9 then
    local m1, m2 = s.tone[7], s.tone[8]
    local b = (m1 == 0 and m2 == 0) and { 15, 9, 5, 3 } or { m1 >> 4, m1 & 15, m2 >> 4, m2 & 15 }
    local sum, y = 0, 0
    for h = 1, 4 do sum = sum + b[h]; y = y + b[h] * math.sin(2 * math.pi * h * p) end
    return sum > 0 and y / sum * 1.2 or 0
  else
    local k = math.floor(p * (w == 5 and 8 or 24)) + seed
    return ((k * 7919 + (w == 5 and k % 3 or k * k) * 104729) % 97) / 48 - 1
  end
end

local function draw_wave_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("WAVE", x + 6, y + 4, C.dim)
  print(WAVES[s.wave + 1], x + w - #WAVES[s.wave + 1] * 8 - 6, y + 4, c)
  local cy, amp = y + h // 2 + 8, (h // 2 - 16) * (0.35 + 0.65 * s.vol / 255)
  rectfill(x + 6, cy, w - 12, 1, C.line)
  local px, py
  for i = 0, w - 13 do
    local p = (i / ((w - 12) / 2)) % 1
    local v = wave_y(s, p, i // ((w - 12) // 2))
    if v > 1.2 then v = 1.2 elseif v < -1.2 then v = -1.2 end
    local yy = math.floor(cy - v * amp)
    if px then line(px, py, x + 6 + i, yy, c) end
    px, py = x + 6 + i, yy
  end
end

local function draw_env_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("ENVELOPE", x + 6, y + 4, C.dim)
  local A, D, R = s.a * 2000 / 255, s.d * 2000 / 255, s.r * 2000 / 255
  local hold = 250
  local total = math.max(A + D * 1.5 + hold + R * 1.5, 400)
  local k = (w - 16) / total
  local x0, base, top = x + 8, y + h - 8, y + 26
  local sus = s.s / 255
  rectfill(x0, base, w - 16, 1, C.line)
  -- as the synthesizer: the attack bends like an RC, decay and release fall
  -- exponentially (5% left at their time)
  local px, py = x0, base
  local function to(t, lv)
    local xx, yy = x0 + t * k, base - (base - top) * lv
    line(px, py, xx, yy, c)
    px, py = xx, yy
  end
  for i = 1, 8 do local f = i / 8; to(A * f, (1 - math.exp(-1.466 * f)) / (1 - math.exp(-1.466))) end
  for i = 1, 12 do local f = i / 12; to(A + D * 1.5 * f, sus + (1 - sus) * math.exp(-3 * 1.5 * f)) end
  to(A + D * 1.5 + hold, sus)
  for i = 1, 12 do local f = i / 12; to(A + D * 1.5 + hold + R * 1.5 * f, sus * math.exp(-3 * 1.5 * f)) end
  circfill(math.floor(x0 + A * k), top, 2, c)
end

local function draw_pitch_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("PITCH", x + 6, y + 4, C.dim)
  local span = math.max(math.abs(s.pitch), s.vdepth / 100, 1)
  local T = s.ptime * 10
  local total = math.max(T * 1.6, 400)
  local cy, half = y + 26 + (h - 34) // 2, (h - 34) // 2
  rectfill(x + 8, cy, w - 16, 1, C.line)
  local px, py
  for i = 0, w - 17 do
    local t = i / (w - 16) * total
    local off = 0
    if T > 0 and t < T then off = s.pitch * (1 - t / T) ^ 2 end
    if s.vdepth > 0 and s.vrate > 0 then off = off + s.vdepth / 100 * math.sin(2 * math.pi * s.vrate / 10 * t / 1000) end
    local yy = math.floor(cy - off / span * half)
    if px then line(px, py, x + 8 + i, yy, c) end
    px, py = x + 8 + i, yy
  end
end

-- the filter's response (the synthesizer's state variable filter), 20 Hz
-- to 20 kHz across, -36 to +18 dB up
local function draw_filter_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("FILTER", x + 6, y + 4, C.dim)
  local cut = s.tone[1]
  if cut == 0 then
    print("OFF", x + w - 30, y + 4, C.faint)
    rectfill(x + 8, y + 26 + (h - 34) * 18 // 54, w - 16, 1, c)
    return
  end
  local fc, k, mode = cutoff_hz(cut), 2 - 1.94 * s.tone[2] / 255, s.tone[3] & 3
  print(FILTERS[mode + 1]:sub(1, 4), x + w - 38, y + 4, c)
  local top, bot = y + 26, y + h - 8
  local px, py
  for i = 0, w - 17 do
    local f = 20 * 1000 ^ (i / (w - 17))
    local r = f / fc
    local den = math.sqrt((1 - r * r) ^ 2 + (k * r) ^ 2)
    local g = mode == 0 and 1 / den or mode == 1 and k * r / den or mode == 2 and r * r / den or math.abs(1 - r * r) / den
    local db = 20 * math.log(math.max(g, 1e-4), 10)
    db = math.max(-36, math.min(18, db))
    local yy = math.floor(top + (18 - db) / 54 * (bot - top))
    if px then line(px, py, x + 8 + i, yy, c) end
    px, py = x + 8 + i, yy
  end
  rectfill(x + 8, top + (bot - top) * 18 // 54, w - 16, 1, C.line)
end

-- the filter envelope and the LFO, over a second
local function draw_motion_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("MOTION", x + 6, y + 4, C.dim)
  local fenv = s.tone[4] > 127 and s.tone[4] - 256 or s.tone[4]
  local fd, rate, lc = s.tone[5] * 2000 / 255, s.tone[14], s.tone[15]
  local cy, half = y + 26 + (h - 34) // 2, (h - 34) // 2
  rectfill(x + 8, cy, w - 16, 1, C.line)
  local px, py
  for i = 0, w - 17 do
    local t = i / (w - 16) * 1000
    local oct = 0
    if fenv ~= 0 then oct = fenv / 16 * (fd > 0 and math.exp(-3 * t / fd) or 1) end
    if rate > 0 then oct = oct + lc * 4 / 255 * math.sin(2 * math.pi * lfo_hz(rate) * t / 1000) end
    local yy = math.floor(cy - math.max(-1, math.min(1, oct / 4)) * half)
    if px then line(px, py, x + 8 + i, yy, c) end
    px, py = x + 8 + i, yy
  end
end

-- where the sound sits: left to right, and how much room and echo
local function draw_space_graph(s, x, y, w, h, c)
  ui.box(x, y, w, h, C.panel)
  print("SPACE", x + 6, y + 4, C.dim)
  local pan = s.tone[6] > 127 and s.tone[6] - 256 or s.tone[6]
  local mx, my = x + w // 2, y + 26 + (h - 34) // 2
  rectfill(x + 12, my, w - 24, 1, C.line)
  print("L", x + 4, my - 7, C.faint)
  print("R", x + w - 12, my - 7, C.faint)
  local room, echo = s.tone[12], s.tone[13]
  local px = mx + pan * (w // 2 - 16) // 127
  for r = 1, 3 do
    local rr = 6 + r * 8 * room // 255
    if room > 0 then circ(px, my, rr, r == 1 and C.line or C.panel2) end
  end
  for e = 1, 3 do
    if echo > 0 then
      local ex = px + (e % 2 == 1 and -1 or 1) * e * 14
      circfill(ex, my, math.max(1, 4 - e), echo > e * 60 and c or C.faint)
    end
  end
  circfill(px, my, 5, c)
end

local function draw_param_graphs(s, g, gy)
  if g == 1 then
    draw_wave_graph(s, 160, gy, 152, 92, WAVE_C[s.wave + 1])
    draw_env_graph(s, 316, gy, 152, 92, C.green)
    draw_pitch_graph(s, 472, gy, 152, 92, C.orange)
  elseif g == 2 then
    draw_filter_graph(s, 160, gy, 152, 92, C.green)
    draw_env_graph(s, 316, gy, 152, 92, C.blue)
    draw_motion_graph(s, 472, gy, 152, 92, C.cyan)
  else
    draw_wave_graph(s, 160, gy, 152, 92, WAVE_C[s.wave + 1])
    draw_space_graph(s, 316, gy, 308, 92, C.orange)
  end
end

-- a ready-made instrument (instruments(), src/audio/presets.c) into the
-- sound; the list plays each one as it is chosen
local function preset_into(s, name)
  local p = instrument and instrument(name)
  if not p then return end
  begin_edit()
  for _, k in ipairs({ "wave", "duty", "vol", "a", "d", "s", "r", "pitch", "ptime", "vdepth", "vrate", "detune" }) do
    set(s, k, p[k])
  end
  set(s, "tone", B.copy(p.tone))
  if s.name == "" or s.name == "SOUND" then set(s, "name", name8(p.name)) end
  say("instrument " .. p.name:upper() .. ": " .. p.about, C.green, 240)
end

local function choose_preset()
  if not instruments then return end
  local items = {}
  for _, it in ipairs(instruments()) do
    items[#items + 1] = { label = it.name:upper(), note = it.kind, name = it.name }
  end
  local s = bank.sounds[cur.sound + 1]
  ui.choose("instrument into sound " .. string.format("%02d", cur.sound), items, function(it) preset_into(s, it.name) end)
  overlay.hover = function(it) if play then play(PREVIEW_VOICE, it.name, last_note, 500) end end
end

local P = {}

P[1] = {
  name = "SOUNDS",
  item = function(d) cur.sound = (cur.sound + d) % B.NSOUND end,
  draw = function()
    local s = bank.sounds[cur.sound + 1]
    slot_list("SOUNDS", B.NSOUND, cur.sound, function(i) return bank.sounds[i + 1].name end,
              function(i) return not B.sound_empty(bank.sounds[i + 1]) end, C.blue)
    -- the name
    local nsel = sp.row == 0
    if nsel then ui.box(160, TOP + 2, 472, 38, C.panel2) ui.frame2(160, TOP + 2, 472, 38, C.blue) end
    print(s.name ~= "" and s.name or "NO NAME", 172, TOP + 5, s.name ~= "" and C.text or C.faint, 2)
    ui.textr(string.format("sound %02d", cur.sound), 620, TOP + 14, C.dim)
    -- the group of parameters: its graphs, then its bars
    local g = sp.group
    local grp = SGROUPS[g]
    draw_param_graphs(s, g, TOP + 48)
    local by = TOP + 152
    for col = 1, #grp.cols do
      for row = 1, #grp.cols[col] do
        local d = grp.cols[col][row]
        local v = d.get(s)
        ui.bar(SCOL_X[col], by + (row - 1) * 38, 136, plabel(d, s), d.fmt(v, s), (v - d.lo) / (d.hi - d.lo),
               grp.c, sp.row == row and sp.col == col)
      end
    end
    if #grp.cols < 3 then
      -- the third column of the last group: the groups, and the presets
      local x = SCOL_X[3]
      for i, gg in ipairs(SGROUPS) do
        print((i == g and "> " or "  ") .. gg.name, x, by + 4 + (i - 1) * 20, i == g and gg.c or C.dim)
      end
      print("MENU: INSTRUMENT", x, by + 84, C.faint)
      print("PRESETS", x, by + 100, C.faint)
    else
      print(grp.name, 486, TOP + 152 - 14, grp.c)
    end
    if sp.row == 0 then
      hint_line({ { "A", nil, "rename" }, { "Y", nil, "play" }, { "START", nil, "chord" } })
    else
      hint_line({ { "A", "ud", "+-1" }, { "A", "lr", "+-10" }, { "Y", nil, "play " .. note_name(last_note) },
                  { "START", nil, "chord" }, { "X", nil, "default" } })
    end
  end,
  move = function(dx, dy)
    local grp = SGROUPS[sp.group]
    if dy ~= 0 then
      local n = #(grp.cols[sp.col] or {})
      local r = sp.row + dy
      if r > n then                                 -- past the bottom: the next group
        if sp.group < #SGROUPS then sp.group, r = sp.group + 1, 1 else r = n end
      elseif r < 1 and sp.group > 1 then            -- past the top: the group before
        sp.group = sp.group - 1
        r = #SGROUPS[sp.group].cols[math.min(sp.col, #SGROUPS[sp.group].cols)]
      elseif r < 0 then
        r = 0
      end
      sp.row = r
    end
    if dx ~= 0 and sp.row > 0 then sp.col = clamp(sp.col + dx, 1, #SGROUPS[sp.group].cols) end
    sp.col = clamp(sp.col, 1, #SGROUPS[sp.group].cols)
    if sp.row > 0 then sp.row = clamp(sp.row, 1, #SGROUPS[sp.group].cols[sp.col]) end
  end,
  edit = function(mod, dx, dy)
    if mod ~= "A" or sp.row == 0 then return end
    local s = bank.sounds[cur.sound + 1]
    local d = sound_param(sp.group, sp.col, sp.row)
    local step = dy ~= 0 and -dy or dx * d.step
    if d.step == 1 then step = dy ~= 0 and -dy or dx end
    d.put(s, clamp(d.get(s) + step, d.lo, d.hi))
  end,
  tap = function(mod)
    if mod == "A" and sp.row == 0 then
      local s = bank.sounds[cur.sound + 1]
      ui.ask_name("name of the sound", s.name, function(t) begin_edit(); set(s, "name", t) end)
    elseif mod == "Y" or mod == "A" then
      preview(PREVIEW_VOICE, cur.sound, last_note)
    end
  end,
  clear = function()
    if sp.row == 0 then return end
    local d = sound_param(sp.group, sp.col, sp.row)
    local fresh = B.sound()
    d.put(bank.sounds[cur.sound + 1], d.get(fresh))
  end,
  play = function()
    -- the sound as a chord, strummed (the delay effect starts each note later)
    local n = last_note
    for i, k in ipairs({ 0, 4, 7, 12 }) do
      preview(i == 4 and PREVIEW_VOICE or 3 + i, cur.sound, n + k, 200, 11 << 4 | (i - 1) * 3, 700)
    end
  end,
  piano = function(n) last_note = n; preview(PREVIEW_VOICE, cur.sound, n) end,
  key = function(k)
    local d = sp.row > 0 and sound_param(sp.group, sp.col, sp.row)
    if not d then return false end
    local s = bank.sounds[cur.sound + 1]
    local step = ({ ["-"] = -1, ["="] = 1, ["_"] = -d.step, ["+"] = d.step })[k]
    if step then begin_edit(); d.put(s, clamp(d.get(s) + step, d.lo, d.hi)) return true end
    return false
  end,
}

----------------------------------------------------------------- step editing (SFX and PATTERN)

-- the common edits of a step: A note, Y sound and volume, B effect
local function edit_step(st, mod, dx, dy, default_sound)
  local note, sound, vol, fx = s_note(st), s_sound(st), s_vol(st), s_fx(st)
  if mod == "A" then
    if note == 0 or note >= 128 then
      note, sound, vol = last_note, default_sound, vol > 0 and vol or 255
    else
      note = clamp(note - dy + dx * 12, 1, 127)
    end
    last_note = note
  elseif mod == "Y" then
    if note == 0 then return st end
    if dy ~= 0 then sound = (sound - dy) % B.NSOUND end
    if dx ~= 0 then vol = clamp(vol + dx * 16, 15, 255) end
    if dx > 0 and vol > 239 then vol = 255 end
  elseif mod == "B" then
    local t, n = fx >> 4, fx & 15
    if dy ~= 0 then t = (t - dy) % (NFX + 1) end
    if dx ~= 0 then n = clamp(n + dx, 0, 15) end
    fx = t == 0 and 0 or (t << 4 | n)
    if note == 0 and t ~= 0 and vol == 0 then vol = 255 end
  end
  return mk(note, sound, vol, fx)
end

local function step_info(st)
  local note, fx = s_note(st), s_fx(st)
  local t = {}
  if note == 0 then t[1] = fx ~= 0 and "(the note goes on)" or "empty"
  elseif note >= 128 then t[1] = "OFF: the note is released"
  else
    local snd = bank.sounds[s_sound(st) + 1]
    t[1] = string.format("%s  %s  vol %s", note_name(note), snd.name ~= "" and snd.name or ("sound " .. s_sound(st)),
                         pct(s_vol(st)))
  end
  if fx >> 4 ~= 0 then t[2] = FX[fx >> 4][2] .. " " .. FX[fx >> 4][3](fx & 15) end
  return t
end

local STEP_HINTS = {
  { "A", nil, "add" }, { "A", "ud", "note" }, { "A", "lr", "oct" }, { "Y", "ud", "sound" },
  { "Y", "lr", "vol" }, { "B", "ud", "fx" }, { "B", "lr", "amt" }, { "X", nil, "clear" },
}

----------------------------------------------------------------- SFX

local xp = { step = 0, field = -1 }  -- field >= 0: the header row
local SFX_FIELDS = { "NAME", "SPEED", "STEPS", "LOOP FROM", "LOOP TO" }

local function sfx_now() return bank.sfx[cur.sfx + 1] end

local function sfx_field_text(x, f)
  if f == 0 then return x.name ~= "" and x.name or "NO NAME"
  elseif f == 1 then return x.ms .. " ms"
  elseif f == 2 then return tostring(x.len)
  elseif f == 3 then return x.le > x.ls and tostring(x.ls + 1) or "-"
  else return x.le > x.ls and tostring(x.le) or "OFF" end
end

P[2] = {
  name = "SFX",
  item = function(d) cur.sfx = (cur.sfx + d) % B.NSFX end,
  draw = function()
    local x = sfx_now()
    slot_list("SOUND EFFECTS", B.NSFX, cur.sfx, function(i) return bank.sfx[i + 1].name end,
              function(i) return not B.sfx_empty(bank.sfx[i + 1]) end, C.green)
    -- header fields
    local fx0 = 160
    for f = 0, 4 do
      local label, val = SFX_FIELDS[f + 1], sfx_field_text(x, f)
      local w = f == 0 and 120 or 82
      local sel = xp.field == f
      ui.box(fx0, TOP + 2, w, 38, sel and C.panel2 or C.panel)
      if sel then ui.frame2(fx0, TOP + 2, w, 38, C.green) end
      print(label, fx0 + 6, TOP + 4, C.dim)
      print(val, fx0 + 6, TOP + 21, sel and C.text or (f == 0 and C.text or C.dim))
      fx0 = fx0 + w + 6
    end
    -- the lanes
    local x0, cw = 164, 13
    local ly, lh = TOP + 48, 128
    ui.box(160, ly - 4, 464, lh + 8, C.panel)
    local lo, hi = 200, 0
    for k = 1, x.len do
      local n = s_note(x.steps[k] or 0)
      if n > 0 and n < 128 then lo, hi = math.min(lo, n), math.max(hi, n) end
    end
    if hi == 0 then lo, hi = last_note - 12, last_note + 12 end
    local mid = (lo + hi) / 2
    local span = math.max(hi - lo + 6, 24)
    lo = math.floor(mid - span / 2)
    local function ny(n) return ly + lh - 4 - math.floor((n - lo) / span * (lh - 12)) end
    -- C lines
    for n = (lo // 12 + 1) * 12, lo + span, 12 do
      local yy = ny(n)
      rectfill(x0, yy, 32 * cw, 1, C.line)
      print("C" .. (n // 12 - 1), 588, yy - 8, C.faint)
    end
    local playing = sfxpos(PREVIEW_VOICE) == cur.sfx and select(2, sfxpos(PREVIEW_VOICE))
    local ring = nil
    for k = 1, 32 do
      local st = x.steps[k] or 0
      local cx = x0 + (k - 1) * cw
      local inside = k <= x.len
      if playing and playing == k - 1 then rectfill(cx, ly - 2, cw - 1, lh + 4, C.panel2) end
      if (k - 1) % 4 == 0 then rectfill(cx, ly + lh, 1, 4, C.faint) end
      local n = s_note(st)
      if inside then
        if n > 0 and n < 128 then
          ring = n
          local yy = ny(n)
          local c = WAVE_C[bank.sounds[s_sound(st) + 1].wave + 1]
          rectfill(cx + 2, yy, cw - 4, ly + lh - yy, c)
          rectfill(cx + 1, yy - 1, cw - 2, 3, C.white)
        elseif n >= 128 then
          ring = nil
          print("x", cx + 3, ly + lh - 20, C.red)
        elseif ring then
          rectfill(cx, ny(ring), cw, 1, C.dim)
        end
        -- volume and effect
        if n > 0 and n < 128 then
          local vh = math.floor(s_vol(st) / 255 * 22)
          rectfill(cx + 3, ly + lh + 34 - vh, cw - 6, vh, C.white)
        end
        local f = s_fx(st)
        if f >> 4 ~= 0 then
          print(FX[f >> 4][1], cx + 2, ly + lh + 38, C.yellow)
          rectfill(cx + 2, ly + lh + 55, math.floor((f & 15) / 15 * (cw - 4)) + 1, 2, C.yellow)
        end
      else
        rectfill(cx + 5, ly + lh - 6, 3, 3, C.faint)
      end
    end
    -- loop
    if x.le > x.ls then
      local a, b = x0 + x.ls * cw, x0 + x.le * cw - 1
      rectfill(a, ly - 4, b - a, 2, C.cyan)
      print("LOOP", a + 2, ly - 2, C.cyan)
    end
    -- the cursor
    if xp.field < 0 then
      local cx = x0 + xp.step * cw
      ui.frame2(cx - 1, ly - 3, cw + 1, lh + 64, C.white)
    end
    print("VOL", 588, ly + lh + 16, C.faint)
    print("FX", 588, ly + lh + 38, C.faint)
    -- what the cursor step holds
    local iy = TOP + 246
    ui.box(160, iy, 464, 52, C.panel)
    if xp.field < 0 then
      local st = x.steps[xp.step + 1] or 0
      local t = step_info(st)
      print(string.format("STEP %02d", xp.step + 1), 170, iy + 6, C.dim)
      print(note_name(s_note(st)) ~= "" and note_name(s_note(st)) or "--", 170, iy + 20, C.green, 2)
      print(t[1], 260, iy + 8, C.text)
      if t[2] then print(t[2], 260, iy + 28, C.yellow) end
      if xp.step + 1 > x.len then print("beyond the end: editing it adds steps", 260, iy + 28, C.dim) end
    else
      print(({ "A: rename the sound effect", "A + up/down: +-1 ms, left/right: +-10",
               "A + up/down: steps, left/right: +-4", "the loop repeats steps FROM..TO while it plays",
               "A + up/down: the end of the loop (OFF: no loop)" })[xp.field + 1], 170, iy + 18, C.dim)
    end
    if xp.field < 0 then hint_line(STEP_HINTS)
    else hint_line({ { "A", "ud", "change" }, { "A", "lr", "faster" }, { "START", nil, "play" } }) end
  end,
  move = function(dx, dy)
    if xp.field >= 0 then
      if dy > 0 then xp.field = -1
      elseif dx ~= 0 then xp.field = clamp(xp.field + dx, 0, 4) end
      return
    end
    if dy < 0 then xp.field = clamp(xp.step // 7, 0, 4) return end
    xp.step = clamp(xp.step + dx, 0, 31)
  end,
  edit = function(mod, dx, dy)
    local x = sfx_now()
    if xp.field >= 0 then
      if mod ~= "A" then return end
      local f, d = xp.field, (dy ~= 0 and -dy or 0)
      if f == 1 then set(x, "ms", clamp(x.ms + d + dx * 10, 5, 2000))
      elseif f == 2 then set(x, "len", clamp(x.len + d + dx * 4, 1, 32))
      elseif f == 3 then
        local ls = clamp(x.ls + d + dx, 0, x.len - 1)
        set(x, "ls", ls)
        if x.le <= ls then set(x, "le", math.min(ls + 1, x.len)) end
      elseif f == 4 then
        local le = clamp(x.le + d + dx, 0, x.len)
        set(x, "le", le)
        if le > 0 and x.ls >= le then set(x, "ls", le - 1) end
      end
      return
    end
    local k = xp.step + 1
    local st = edit_step(x.steps[k] or 0, mod, dx, dy, cur.sound)
    set(x.steps, k, st)
    if k > x.len then set(x, "len", k) end
    if mod ~= "B" then preview_step(PREVIEW_VOICE, st) end
  end,
  tap = function(mod)
    local x = sfx_now()
    if xp.field >= 0 then
      if mod == "A" and xp.field == 0 then
        ui.ask_name("name of the sound effect", x.name, function(t) begin_edit(); set(x, "name", t) end)
      end
      return
    end
    local k = xp.step + 1
    local st = x.steps[k] or 0
    if mod == "A" then
      if s_note(st) == 0 then
        st = mk(last_note, cur.sound, 255, s_fx(st))
        preview_step(PREVIEW_VOICE, st)
      else
        st = 0
      end
      set(x.steps, k, st)
      if k > x.len then set(x, "len", k) end
    elseif mod == "Y" then
      preview_step(PREVIEW_VOICE, st)
    end
  end,
  clear = function()
    if xp.field < 0 then set(sfx_now().steps, xp.step + 1, 0) end
  end,
  play = function()
    if sfxpos(PREVIEW_VOICE) then sfx(-1, PREVIEW_VOICE) else sfx(cur.sfx, PREVIEW_VOICE) end
  end,
  piano = function(n)
    last_note = n
    if xp.field >= 0 then preview(PREVIEW_VOICE, cur.sound, n) return end
    local x = sfx_now()
    local k = xp.step + 1
    local old = x.steps[k] or 0
    local st = mk(n, s_note(old) > 0 and s_note(old) < 128 and s_sound(old) or cur.sound, 255, s_fx(old))
    set(x.steps, k, st)
    if k > x.len then set(x, "len", k) end
    preview_step(PREVIEW_VOICE, st)
    xp.step = math.min(xp.step + 1, 31)
  end,
}

----------------------------------------------------------------- PATTERN

local pp = { track = 0, step = 0, field = -1 }   -- track -1 is never used; step -1: the track header
local track_sound = { 0, 1, 2, 3, 4, 5, 6, 7 }   -- the sound new notes get, per track
local muted = {}
local PAT_FIELDS = { "STEPS", "TEMPO", "SWING" }

local function pat_now() return bank.pats[cur.pat + 1] end
local function song_now() return bank.songs[cur.song + 1] end

local function pat_get(p, t, k) local tr = p.tracks[t + 1]; return tr and tr[k + 1] or 0 end
local function pat_set(p, t, k, st)
  local tr = p.tracks[t + 1]
  if not tr then tr = {}; p.tracks[t + 1] = tr end
  set(tr, k + 1, st)
end

-- the sound of each track: the last one its notes use
local function learn_tracks()
  local p = pat_now()
  for t = 0, 7 do
    for k = p.len - 1, 0, -1 do
      local st = pat_get(p, t, k)
      if s_note(st) > 0 and s_note(st) < 128 then track_sound[t + 1] = s_sound(st) break end
    end
  end
end

local function sound_name(i)
  local n = bank.sounds[i + 1].name
  return n ~= "" and n or string.format("SOUND %d", i)
end

local function playing_pattern()
  local song, _, step, pat = music()
  if song and pat == cur.pat then return step end
  return nil
end

local GX, GY, CW, CH = 124, TOP + 44, 30, 30
local function cell_x(k) return GX + (k % 16) * CW + (k % 16 // 4) * 4 end
local function cell_y(t) return GY + t * CH end

P[3] = {
  name = "PATTERN",
  item = function(d) cur.pat = (cur.pat + d) % B.NPAT; learn_tracks() end,
  draw = function()
    local p = pat_now()
    local song = song_now()
    -- header
    print(string.format("PATTERN %02d", cur.pat), 8, TOP + 5, C.orange, 2)
    local fx0 = 200
    local vals = { tostring(p.len), song.bpm .. " BPM", song.swing .. "%" }
    for f = 0, 2 do
      local sel = pp.field == f
      ui.box(fx0, TOP + 2, 96, 38, sel and C.panel2 or C.panel)
      if sel then ui.frame2(fx0, TOP + 2, 96, 38, C.orange) end
      print(PAT_FIELDS[f + 1], fx0 + 6, TOP + 4, C.dim)
      print(vals[f + 1], fx0 + 6, TOP + 21, sel and C.text or C.dim)
      fx0 = fx0 + 102
    end
    -- the pages of 16 steps
    local page = pp.step >= 0 and pp.step // 16 or 0
    local pages = (p.len + 15) // 16
    for i = 0, 3 do
      local c = i >= pages and C.panel or (i == page and C.orange or C.faint)
      ui.box(520 + i * 28, TOP + 14, 22, 14, c)
      if i < pages then print(tostring(i * 16 + 1), 520 + i * 28 + (i == 0 and 7 or 3), TOP + 13, i == page and C.dark or C.dim) end
    end
    print("song: " .. (song.name ~= "" and song.name or string.format("%d", cur.song)), 520, TOP + 28, C.faint)
    -- the grid
    local now = playing_pattern()
    for t = 0, 7 do
      local y = cell_y(t)
      local tc = TRACK_C[t + 1]
      local hsel = pp.field < 0 and pp.track == t and pp.step < 0
      local m = muted[t]
      ui.box(8, y + 1, 108, CH - 3, hsel and C.panel2 or C.panel)
      ui.box(10, y + 3, 20, CH - 7, m and C.faint or tc)
      print(tostring(t + 1), 16, y + 6, C.dark)
      print(sound_name(track_sound[t + 1]):sub(1, 8), 36, y + 6, m and C.faint or (hsel and C.text or C.dim))
      if m then print("M", 104, y + 6, C.red) end
      if hsel then ui.frame2(8, y + 1, 108, CH - 3, C.white) end
      for c = 0, 15 do
        local k = page * 16 + c
        local x = cell_x(k)
        if k < p.len then
          local st = pat_get(p, t, k)
          local n = s_note(st)
          local bg = (c // 4) % 2 == 0 and C.cell or C.panel
          if now == k then bg = C.panel2 end
          if n > 0 and n < 128 then
            local v = s_vol(st) / 255
            ui.box(x, y + 1, CW - 3, CH - 3, m and C.faint or mix(bg, tc, 0.4 + 0.6 * v))
            local nm = note_name(n)
            print(nm, x + (CW - 3 - #nm * 8) // 2, y + 6, C.dark)
          elseif n >= 128 then
            ui.box(x, y + 1, CW - 3, CH - 3, bg)
            print("--", x + 5, y + 6, C.red)
          else
            ui.box(x, y + 1, CW - 3, CH - 3, bg)
            rectfill(x + 12, y + 13, 3, 3, now == k and C.white or C.faint)
          end
          if s_fx(st) >> 4 ~= 0 then
            tri(x + CW - 11, y + 1, x + CW - 4, y + 1, x + CW - 4, y + 8, C.white)
          end
        else
          rectfill(x + 12, y + 13, 3, 3, C.panel)
        end
      end
    end
    if pp.field < 0 and pp.step >= 0 then
      ui.frame2(cell_x(pp.step) - 2, cell_y(pp.track) - 1, CW + 1, CH + 1, C.white)
    end
    -- what the cursor holds
    local iy = GY + 8 * CH + 4
    if pp.field >= 0 then
      print(({ "A + up/down: steps, left/right: +-16 (up to 64)", "A + up/down: tempo of the song, left/right: +-10",
               "A + up/down: swing, the first step of each pair longer" })[pp.field + 1], 8, iy + 4, C.dim)
      hint_line({ { "A", "ud", "change" }, { "A", "lr", "+-more" }, { "START", nil, "play" } })
    elseif pp.step < 0 then
      print(string.format("TRACK %d (voice %d)  new notes: %s", pp.track + 1, pp.track, sound_name(track_sound[pp.track + 1])),
            8, iy + 4, C.dim)
      hint_line({ { "A", nil, muted[pp.track] and "unmute" or "mute" }, { "A", "ud", "sound" }, { "Y", nil, "play" },
                  { "START", nil, "play" } })
    else
      local st = pat_get(p, pp.track, pp.step)
      local t = step_info(st)
      print(string.format("TRACK %d  STEP %02d", pp.track + 1, pp.step + 1), 8, iy + 4, C.dim)
      print(t[1], 168, iy + 4, C.text)
      if t[2] then print(t[2], 440, iy + 4, C.yellow) end
      hint_line(STEP_HINTS)
    end
  end,
  move = function(dx, dy)
    local p = pat_now()
    if pp.field >= 0 then
      if dy > 0 then pp.field = -1
      elseif dx ~= 0 then pp.field = clamp(pp.field + dx, 0, 2) end
      return
    end
    if dy < 0 and pp.track == 0 then pp.field = 0 return end
    pp.track = clamp(pp.track + dy, 0, 7)
    if dx ~= 0 then pp.step = clamp(pp.step + dx, -1, p.len - 1) end
  end,
  edit = function(mod, dx, dy)
    local p, song = pat_now(), song_now()
    if pp.field >= 0 then
      if mod ~= "A" then return end
      local d = dy ~= 0 and -dy or 0
      if pp.field == 0 then
        local len = clamp(p.len + d + dx * 16, 1, 64)
        set(p, "len", len)
        pp.step = math.min(pp.step, len - 1)
      elseif pp.field == 1 then set(song, "bpm", clamp(song.bpm + d + dx * 10, 40, 255))
      else set(song, "swing", clamp(song.swing + d + dx * 10, 0, 100)) end
      return
    end
    if pp.step < 0 then
      if mod == "A" and dy ~= 0 then
        track_sound[pp.track + 1] = (track_sound[pp.track + 1] - dy) % B.NSOUND
        preview(pp.track, track_sound[pp.track + 1], last_note)
      end
      return
    end
    local st = edit_step(pat_get(p, pp.track, pp.step), mod, dx, dy, track_sound[pp.track + 1])
    pat_set(p, pp.track, pp.step, st)
    if mod == "Y" and s_note(st) > 0 and s_note(st) < 128 then track_sound[pp.track + 1] = s_sound(st) end
    if mod ~= "B" then preview_step(pp.track, st) end
  end,
  tap = function(mod)
    local p = pat_now()
    if pp.field >= 0 then return end
    if pp.step < 0 then
      if mod == "A" then
        muted[pp.track] = not muted[pp.track]
        mute(pp.track, muted[pp.track] or false)
      elseif mod == "Y" then
        preview(pp.track, track_sound[pp.track + 1], last_note)
      end
      return
    end
    local st = pat_get(p, pp.track, pp.step)
    if mod == "A" then
      if s_note(st) == 0 then
        st = mk(last_note, track_sound[pp.track + 1], 255, s_fx(st))
        preview_step(pp.track, st)
      else
        st = 0
      end
      pat_set(p, pp.track, pp.step, st)
    elseif mod == "Y" then
      preview_step(pp.track, st)
    end
  end,
  clear = function()
    if pp.field < 0 and pp.step >= 0 then pat_set(pat_now(), pp.track, pp.step, 0) end
  end,
  play = function()
    if music() then music(-1) else
      local song = song_now()
      audio_pattern(cur.pat, song.bpm, song.swing)
    end
  end,
  piano = function(n)
    last_note = n
    if pp.field >= 0 or pp.step < 0 then preview(pp.track, track_sound[pp.track + 1], n) return end
    local p = pat_now()
    local old = pat_get(p, pp.track, pp.step)
    local st = mk(n, track_sound[pp.track + 1], s_vol(old) > 0 and s_note(old) > 0 and s_vol(old) or 255, s_fx(old))
    pat_set(p, pp.track, pp.step, st)
    preview_step(pp.track, st)
    pp.step = math.min(pp.step + 1, p.len - 1)
  end,
}

----------------------------------------------------------------- SONG

local gp = { slot = 0, field = -1 }
local SONG_FIELDS = { "NAME", "TEMPO", "SWING", "LOOP", "ECHO", "ROOM" }
local NSF = #SONG_FIELDS - 1          -- the last field
local OX, OY, OW, OH = 164, TOP + 70, 28, 30
local function pat_c(n) return TRACK_C[n % 8 + 1] end

local function song_field_text(s, f)
  if f == 0 then return s.name ~= "" and s.name or "NO NAME"
  elseif f == 1 then return s.bpm .. " BPM"
  elseif f == 2 then return s.swing .. "%"
  elseif f == 3 then return s.loop == 255 and "OFF" or ("FROM " .. (s.loop + 1))
  elseif f == 4 then return (s.echo or 0) == 0 and "AS IS" or (s.echo .. "/16")
  else return (s.room or 0) == 0 and "AS IS" or pct(s.room) end
end

local function set_order(s, order) set(s, "order", order) end

P[4] = {
  name = "SONG",
  item = function(d) cur.song = (cur.song + d) % B.NSONG; gp.slot = 0 end,
  draw = function()
    local s = song_now()
    slot_list("SONGS", B.NSONG, cur.song, function(i) return bank.songs[i + 1].name end,
              function(i) return not B.song_empty(bank.songs[i + 1]) end, C.purple)
    local fx0 = 160
    for f = 0, NSF do
      local w = f == 0 and 112 or 64
      local sel = gp.field == f
      ui.box(fx0, TOP + 2, w, 38, sel and C.panel2 or C.panel)
      if sel then ui.frame2(fx0, TOP + 2, w, 38, C.purple) end
      print(SONG_FIELDS[f + 1], fx0 + 6, TOP + 4, C.dim)
      print(song_field_text(s, f), fx0 + 6, TOP + 21, sel and C.text or (f == 0 and C.text or C.dim))
      fx0 = fx0 + w + 6
    end
    print("ORDER OF THE PATTERNS", 164, TOP + 50, C.dim)
    local ms, morder = music()
    local playing = ms == cur.song and morder or nil
    for i = 0, 63 do
      local x, y = OX + (i % 16) * OW, OY + (i // 16) * OH
      if i < #s.order then
        local n = s.order[i + 1]
        local used = not B.pat_empty(bank.pats[n + 1])
        ui.box(x, y, OW - 3, OH - 4, used and pat_c(n) or C.faint)
        print(string.format("%02d", n), x + 5, y + 5, C.dark)
        if playing == i then rectfill(x + 2, y + OH - 8, OW - 7, 3, C.white) end
      elseif i == #s.order then
        ui.frame(x, y, OW - 3, OH - 4, C.faint)
        print("+", x + 9, y + 5, C.faint)
      else
        rectfill(x + 11, y + 11, 3, 3, C.panel2)
      end
      if s.loop ~= 255 and i == s.loop and i < #s.order then
        tri(x, y - 2, x + 8, y - 2, x, y + 6, C.cyan)
      end
    end
    if gp.field < 0 then
      ui.frame2(OX + (gp.slot % 16) * OW - 2, OY + (gp.slot // 16) * OH - 2, OW + 1, OH, C.white)
    end
    -- the pattern of the slot
    local py = OY + 4 * OH + 8
    ui.box(160, py, 464, BOTTOM - py, C.panel)
    if gp.field < 0 and gp.slot < #s.order then
      local n = s.order[gp.slot + 1]
      local p = bank.pats[n + 1]
      print(string.format("PATTERN %02d", n), 172, py + 8, pat_c(n))
      print(p.len .. " steps", 172, py + 26, C.dim)
      print("A: edit it", 172, py + 44, C.dim)
      for t = 0, 7 do
        for k = 0, math.min(p.len, 32) - 1 do
          local st = pat_get(p, t, k)
          local n2 = s_note(st)
          local c = n2 > 0 and n2 < 128 and TRACK_C[t + 1] or (n2 >= 128 and C.red or C.panel2)
          rectfill(300 + k * 10 + (k // 4) * 2, py + 8 + t * 10, 8, 8, c)
        end
      end
    elseif gp.field < 0 then
      print("A: add a pattern here", 172, py + 26, C.dim)
    else
      print(({ "A: rename the song", "A + up/down: tempo, left/right: +-10",
               "A + up/down: swing, the first step of each pair longer",
               "A + up/down: where the song goes on after the end (OFF: it stops)",
               "A + up/down: the echo's time in steps, on the beat",
               "A + up/down: the size of the room, small to a hall" })[gp.field + 1], 172, py + 26, C.dim)
      if gp.field >= 4 then
        print("(a sound's ECHO and ROOM say how much of it goes there)", 172, py + 44, C.faint)
      end
    end
    if gp.field >= 0 then
      hint_line({ { "A", "ud", "change" }, { "A", "lr", "+-10" }, { "START", nil, "play" } })
    else
      hint_line({ { "A", nil, "edit" }, { "A", "ud", "pattern" }, { "Y", nil, "insert copy" }, { "X", nil, "remove" },
                  { "START", nil, "play from here" } })
    end
  end,
  move = function(dx, dy)
    local s = song_now()
    if gp.field >= 0 then
      if dy > 0 then gp.field = -1
      elseif dx ~= 0 then gp.field = clamp(gp.field + dx, 0, NSF) end
      return
    end
    if dy < 0 and gp.slot < 16 then gp.field = clamp(gp.slot * (NSF + 1) // 16, 0, NSF) return end
    gp.slot = clamp(gp.slot + dx + dy * 16, 0, math.min(#s.order, 63))
  end,
  edit = function(mod, dx, dy)
    local s = song_now()
    if mod ~= "A" then return end
    local d = dy ~= 0 and -dy or 0
    if gp.field >= 0 then
      if gp.field == 1 then set(s, "bpm", clamp(s.bpm + d + dx * 10, 40, 255))
      elseif gp.field == 2 then set(s, "swing", clamp(s.swing + d + dx * 10, 0, 100))
      elseif gp.field == 3 then
        local l = s.loop == 255 and -1 or s.loop
        l = clamp(l + d + dx, -1, math.max(#s.order - 1, 0))
        set(s, "loop", l < 0 and 255 or l)
      elseif gp.field == 4 then set(s, "echo", clamp((s.echo or 0) + d + dx, 0, 16))
      elseif gp.field == 5 then set(s, "room", clamp((s.room or 0) + d * 16 + dx * 64, 0, 255))
      end
      return
    end
    local order = B.copy(s.order)
    local i = gp.slot + 1
    if i > #order then
      if #order >= 64 then return end
      order[i] = #order > 0 and order[#order] or 0
    else
      order[i] = (order[i] + d + dx * 10) % B.NPAT
    end
    set_order(s, order)
  end,
  tap = function(mod)
    local s = song_now()
    if gp.field >= 0 then
      if mod == "A" and gp.field == 0 then
        ui.ask_name("name of the song", s.name, function(t) begin_edit(); set(s, "name", t) end)
      end
      return
    end
    local i = gp.slot + 1
    if mod == "A" then
      if i <= #s.order then
        cur.pat = s.order[i]
        learn_tracks()
        cur.page = 3
      elseif #s.order < 64 then
        local order = B.copy(s.order)
        order[i] = #order > 0 and order[#order] or 0
        set_order(s, order)
      end
    elseif mod == "Y" and i <= #s.order and #s.order < 64 then
      local order = B.copy(s.order)
      table.insert(order, i, order[i])
      set_order(s, order)
      gp.slot = math.min(gp.slot + 1, 63)
    end
  end,
  clear = function()
    local s = song_now()
    if gp.field >= 0 or gp.slot >= #s.order then return end
    local order = B.copy(s.order)
    table.remove(order, gp.slot + 1)
    set_order(s, order)
    if s.loop ~= 255 and s.loop >= #order then set(s, "loop", math.max(#order - 1, 0)) end
  end,
  play = function()
    if music() then music(-1) else music(cur.song, 0, gp.field < 0 and math.min(gp.slot, #song_now().order - 1) or 0) end
  end,
}

----------------------------------------------------------------- overlays

local NAME_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_!."

-- an 8-character name, with the pad (up/down letters) or the keyboard
function ui.ask_name(label, text, done)
  local t = {}
  for i = 1, 8 do t[i] = text:sub(i, i) ~= "" and text:sub(i, i):upper() or " " end
  overlay = { kind = "name", label = label, chars = t, pos = 1, done = done }
end

function ui.ask(q, detail, yes, done)
  overlay = { kind = "ask", q = q, detail = detail, yes = yes, done = done }
end

function ui.choose(title, items, done, sel)
  overlay = { kind = "choose", title = title, items = items, sel = sel or 1, done = done }
end

local function name_text(o) return (table.concat(o.chars):gsub("%s+$", "")) end

local function overlay_key(k)
  local o = overlay
  if o.kind == "name" then
    if k == "\n" then overlay = nil; o.done(name_text(o))
    elseif k == "esc" then overlay = nil
    elseif k == "left" then o.pos = math.max(1, o.pos - 1)
    elseif k == "right" then o.pos = math.min(8, o.pos + 1)
    elseif k == "\b" then o.pos = math.max(1, o.pos - 1); o.chars[o.pos] = " "
    elseif k == "up" or k == "down" then
      local i = NAME_CHARS:find(o.chars[o.pos], 1, true) or 1
      i = (i - 1 + (k == "up" and -1 or 1)) % #NAME_CHARS + 1
      o.chars[o.pos] = NAME_CHARS:sub(i, i)
    elseif #k == 1 and NAME_CHARS:find(k:upper(), 1, true) then
      o.chars[o.pos] = k:upper()
      o.pos = math.min(8, o.pos + 1)
    end
  elseif o.kind == "ask" then
    if k == "\n" or k == "y" then overlay = nil; o.done()
    elseif k == "esc" or k == "n" then overlay = nil end
  elseif o.kind == "help" then
    overlay = nil
  else
    local n = #o.items
    local was = o.sel
    if k == "up" then o.sel = (o.sel - 2) % n + 1
    elseif k == "down" then o.sel = o.sel % n + 1
    elseif k == "pgup" then o.sel = math.max(1, o.sel - 10)
    elseif k == "pgdn" then o.sel = math.min(n, o.sel + 10)
    elseif k == "\n" then
      local it = o.items[o.sel]
      overlay = o.parent
      if it and not it.off then o.done(it, o.sel) end
    elseif k == "esc" then overlay = o.parent end
    if o.hover and overlay == o and o.sel ~= was then o.hover(o.items[o.sel]) end
  end
end

-- one action per frame: the first may close the overlay
local function overlay_pad()
  local o = overlay
  local k
  if o.kind == "name" then
    if btnp(6) then o.chars[o.pos] = " " end
    k = rep[2] and "up" or rep[3] and "down" or rep[0] and "left" or rep[1] and "right" or
        (btnp(4) or btnp(8)) and "\n" or btnp(5) and "esc" or nil
  else
    k = rep[2] and "up" or rep[3] and "down" or btnp(4) and "\n" or (btnp(5) or btnp(9)) and "esc" or nil
  end
  if k then overlay_key(k) end
end

local function draw_panel(w, h, title, c)
  local x, y = (W - w) // 2, (H - h) // 2
  ui.box(x - 2, y - 2, w + 4, h + 4, c or C.white)
  ui.box(x, y, w, h, C.panel)
  if title then
    ui.box(x, y, w, 28, C.panel2)
    print(title, x + 12, y + 6, c or C.text)
  end
  return x, y
end

local function draw_overlay()
  local o = overlay
  -- darken what is behind: a grid of dots is cheap and reads as a shadow
  for yy = TOP, BOTTOM, 4 do rectfill(0, yy, W, 2, C.dark) end
  if o.kind == "name" then
    local x, y = draw_panel(360, 150, o.label, C.yellow)
    for i = 1, 8 do
      local cx = x + 30 + (i - 1) * 38
      ui.box(cx, y + 50, 32, 44, i == o.pos and C.yellow or C.panel2)
      print(o.chars[i], cx + 8, y + 56, i == o.pos and C.dark or C.text, 2)
    end
    ui.arrow(x + 30 + (o.pos - 1) * 38 + 12, y + 34, "u", C.yellow)
    ui.arrow(x + 30 + (o.pos - 1) * 38 + 12, y + 96, "d", C.yellow)
    local kb, hx = lastinput() == "keyboard", x + 12
    hx = ui.chips(kb and { "up", "down" } or { "UPDOWN" }, "letter", hx, y + 122)
    hx = ui.chips(kb and { "left", "right" } or { "LEFTRIGHT" }, "move", hx, y + 122)
    hx = ui.hint(hx, y + 122, "A", nil, "done")
    ui.hint(hx, y + 122, "B", nil, "cancel")
  elseif o.kind == "ask" then
    local x, y = draw_panel(420, 130, o.q, C.orange)
    print(o.detail or "", x + 16, y + 44, C.text)
    local hx = ui.hint(x + 16, y + 92, "A", nil, o.yes)
    ui.hint(hx + 8, y + 92, "B", nil, "cancel")
  elseif o.kind == "help" then
    local lines = o.lines
    local x, y = draw_panel(624, #lines * 16 + 40, "CONTROLS", C.cyan)
    for i, l in ipairs(lines) do print(l, x + 12, y + 20 + i * 16, i == 1 and C.yellow or C.text) end
  else
    local n = #o.items
    local rows = math.min(n, 13)
    local x, y = draw_panel(440, rows * 20 + (o.footer and 60 or 44), o.title, PAGE_C[cur.page])
    local first = clamp(o.sel - rows // 2, 1, math.max(1, n - rows + 1))
    for i = first, math.min(n, first + rows - 1) do
      local it = o.items[i]
      local yy = y + 34 + (i - first) * 20
      if i == o.sel then ui.box(x + 6, yy - 2, 428, 20, PAGE_C[cur.page]) end
      local c = i == o.sel and C.dark or (it.off and C.faint or C.text)
      print(it.label, x + 14, yy, c)
      if it.note then ui.textr(it.note, x + 426, yy, i == o.sel and C.dark or C.dim) end
    end
    if o.footer then print(o.footer, x + 14, y + rows * 20 + 38, C.dim) end
    if first > 1 then ui.arrow(x + 420, y + 30, "u", C.dim) end
    if first + rows - 1 < n then ui.arrow(x + 420, y + rows * 20 + 24, "d", C.dim) end
  end
end

----------------------------------------------------------------- files

local PACK_DIR = "/bm/sounds"
-- the Lua of a sound pack: a little player of its songs and effects
local PACK_LUA = [[
-- a sound pack made with bm Sound (Dev tab): open it there to edit it
local song, fx = 0, 0
function _update()
  if btnp(2) then song = math.max(0, song - 1) end
  if btnp(3) then song = song + 1 end
  if btnp(0) then fx = math.max(0, fx - 1) end
  if btnp(1) then fx = fx + 1 end
  if btnp(4) then music(song) end
  if btnp(5) then music(-1); sfx(-1) end
  if btnp(6) then sfx(fx) end
end
function _draw()
  cls(0x111318)
  print("SOUND PACK", 32, 32, 0xF2701D, 2)
  print("up/down song " .. song .. "   A play   B stop", 32, 96, 0xE9EBEF)
  print("left/right effect " .. fx .. "   X play", 32, 120, 0xE9EBEF)
  local s, p = music()
  if s then print("playing song " .. s .. ", position " .. (p + 1), 32, 160, 0x35C46E) end
end
]]

local function is_bm(name) return name:lower():match("%.bm$") ~= nil end

local function list_files()
  local out = {}
  local function add(dir, kind)
    local l = ls(dir)
    table.sort(l, function(a, b) return a.name:lower() < b.name:lower() end)
    for _, e in ipairs(l) do
      if not e.dir and is_bm(e.name) then
        out[#out + 1] = { path = (dir == "/" and "" or dir) .. "/" .. e.name, name = e.name, kind = kind, size = e.size }
      end
    end
  end
  add("/carts", "game")
  add("/", "game")
  add(PACK_DIR, "pack")
  return out
end

-- what a file holds (read when the picker rests on it)
local function file_info(e)
  if e.info then return e.info end
  local data, title = cart_audio(e.path)
  local info = { title = title or e.name }
  if data == nil then info.err = title or "cannot read"; info.title = e.name
  elseif data == false then info.text = "no sounds yet"
  else
    local b = B.parse(data)
    if b then
      info.bank = b
      local c = function(l, f) local n = 0; for _, x in ipairs(l) do if not f(x) then n = n + 1 end end return n end
      info.text = string.format("%d sounds, %d sfx, %d patterns, %d songs", c(b.sounds, B.sound_empty),
                                c(b.sfx, B.sfx_empty), c(b.pats, B.pat_empty), c(b.songs, B.song_empty))
    else info.text = "a sound bank this version cannot read" end
  end
  e.info = info
  return info
end

local function short_name(path) return (path:match("([^/]+)$") or path):upper() end

local function reset_view()
  cur.sound, cur.sfx, cur.pat, cur.song = 0, 0, 0, 0
  sp.row, xp.step, xp.field, pp.track, pp.step, pp.field, gp.slot, gp.field = 1, 0, -1, 0, 0, -1, 0, -1
  undo = {}
  muted = {}
  for t = 0, 7 do mute(t, false) end
  learn_tracks()
end

local function use_bank(b, path, title, is_game)
  music(-1)
  sfx(-1)
  bank = b
  proj = { path = path, title = title or "", is_game = is_game, dirty = false }
  bank_changed = true
  reset_view()
end

local function open_file(path)
  local data, title = cart_audio(path)
  if data == nil then say("cannot open " .. path .. ": " .. tostring(title), C.red) return false end
  local is_game = not path:upper():find("^" .. PACK_DIR:upper())
  if data == false then
    note_log("opened " .. path .. " (no sounds yet)")
    use_bank(B.blank(), path, title, is_game)
    cur.page = 1
    say(title .. " has no sounds yet: make them, then Save puts them in it", C.yellow, 400)
    return true
  end
  local b, err = B.parse(data)
  if not b then say(err, C.red) return false end
  use_bank(b, path, title, is_game)
  cur.page = 4
  say("opened " .. (title ~= "" and title or path) .. ": Save writes the sounds back into it", C.green, 300)
  note_log("opened " .. path)
  return true
end

local function write_to(path, title)
  local data = B.pack(bank)
  local ok, err = cart_put_audio(path, data, title, PACK_LUA)
  if not ok then
    say("cannot save " .. path .. ": " .. tostring(err), C.red, 400)
    note_log("cannot save " .. path .. ": " .. tostring(err))
    return false
  end
  note_log("saved " .. path .. ", " .. #data .. " bytes")
  return true
end

local function save_as_pack(name)
  if name == "" then say("a pack needs a name", C.red) return end
  local path = PACK_DIR .. "/" .. name .. ".BM"
  if write_to(path, name) then
    proj.path, proj.title, proj.is_game, proj.dirty = path, name, false, false
    say("saved the sound pack " .. path, C.green, 300)
  end
end

local function ask_pack_name()
  local base = proj.title ~= "" and proj.title or "SOUNDS"
  ui.ask_name("name of the new sound pack", name8(base), save_as_pack)
end

local function save()
  if not proj.path then ask_pack_name() return end
  if write_to(proj.path, proj.title) then
    proj.dirty = false
    say("saved " .. short_name(proj.path) .. (proj.is_game and ": the game plays the new sounds" or ""), C.green, 300)
  end
end

-- the picker: games, then sound packs
local function pick_file(title, kinds, done)
  local items = {}
  for _, e in ipairs(list_files()) do
    if kinds[e.kind] then
      items[#items + 1] = { label = e.name, note = e.kind == "pack" and "sound pack" or "game", entry = e }
    end
  end
  if #items == 0 then say("no cartridges on the SD card", C.red) return end
  overlay = { kind = "choose", title = title, items = items, sel = 1, done = function(it) done(it.entry) end,
              files = true }
end

-- copying from another bank: sounds first (the same sound is not copied twice)
local function free_slot(list, empty, from)
  for i = from or 1, #list do if empty(list[i]) then return i end end
  return nil
end

local function same(a, b)
  for _, k in ipairs(SOUND_KEYS) do if a[k] ~= b[k] then return false end end
  return a.name == b.name
end

local function import_sounds(src, used)
  local map = {}
  for i in pairs(used) do
    local s = src.sounds[i + 1]
    local slot
    for j = 1, B.NSOUND do if same(bank.sounds[j], s) then slot = j break end end
    if not slot then
      slot = free_slot(bank.sounds, B.sound_empty)
      if not slot then return nil, "no free sound slots" end
      bank.sounds[slot] = B.copy(s)
    end
    map[i] = slot - 1
  end
  return map
end

local function remap(steps, n, map)
  for k = 1, n do
    local st = steps[k] or 0
    if s_note(st) > 0 and s_note(st) < 128 and map[s_sound(st)] then
      steps[k] = mk(s_note(st), map[s_sound(st)], s_vol(st), s_fx(st))
    end
  end
end

local function import_pattern(src, i, map)
  local slot = free_slot(bank.pats, B.pat_empty)
  if not slot then return nil, "no free pattern slots" end
  local p = B.copy(src.pats[i + 1])
  for t = 1, 8 do if p.tracks[t] then remap(p.tracks[t], p.len, map) end end
  bank.pats[slot] = p
  return slot - 1
end

local function do_import(src, kind, i)
  local used = {}
  if kind == "sound" then used[i] = true
  elseif kind == "sfx" then sounds_of_steps(src.sfx[i + 1].steps, src.sfx[i + 1].len, used)
  elseif kind == "pattern" then
    local p = src.pats[i + 1]
    for t = 1, 8 do if p.tracks[t] then sounds_of_steps(p.tracks[t], p.len, used) end end
  elseif kind == "song" then
    for _, n in ipairs(src.songs[i + 1].order) do
      local p = src.pats[n + 1]
      if p then for t = 1, 8 do if p.tracks[t] then sounds_of_steps(p.tracks[t], p.len, used) end end end
    end
  end
  local map, err = import_sounds(src, used)
  if not map then return say(err, C.red) end
  if kind == "sound" then
    cur.sound = map[i]
    say("imported as sound " .. map[i], C.green)
  elseif kind == "sfx" then
    local slot = free_slot(bank.sfx, B.sfx_empty)
    if not slot then return say("no free sound effect slots", C.red) end
    local x = B.copy(src.sfx[i + 1])
    remap(x.steps, x.len, map)
    bank.sfx[slot] = x
    cur.sfx = slot - 1
    say("imported as sound effect " .. (slot - 1), C.green)
  elseif kind == "pattern" then
    local slot, e = import_pattern(src, i, map)
    if not slot then return say(e, C.red) end
    cur.pat = slot
    say("imported as pattern " .. slot, C.green)
  elseif kind == "song" then
    local s = B.copy(src.songs[i + 1])
    local gslot = free_slot(bank.songs, B.song_empty)
    if not gslot then return say("no free song slots", C.red) end
    local pmap = {}
    for k, n in ipairs(s.order) do
      if not pmap[n] then
        local slot, e = import_pattern(src, n, map)
        if not slot then return say(e, C.red) end
        pmap[n] = slot
      end
      s.order[k] = pmap[n]
    end
    bank.songs[gslot] = s
    cur.song = gslot - 1
    say("imported as song " .. (gslot - 1) .. " with its patterns and sounds", C.green)
  end
  undo = {}
  proj.dirty = true
  bank_changed = true
  note_log("imported a " .. kind)
end

local function import_from(e)
  local info = file_info(e)
  if not info.bank then say(info.err or (info.title .. ": " .. (info.text or "no sounds")), C.red) return end
  local src = info.bank
  local function pick(kind, list, empty, label)
    local items = {}
    for i, x in ipairs(list) do
      if not empty(x) then items[#items + 1] = { label = string.format("%02d  %s", i - 1, label(x, i)), index = i - 1 } end
    end
    if #items == 0 then say("nothing of that kind in " .. info.title, C.dim) return end
    ui.choose("import from " .. info.title, items, function(it) do_import(src, kind, it.index) end)
  end
  ui.choose("import from " .. info.title, {
    { label = "a sound", note = "into a free slot" },
    { label = "a sound effect", note = "with its sounds" },
    { label = "a pattern", note = "with its sounds" },
    { label = "a song", note = "patterns, sounds" },
    { label = "everything", note = "replaces this" },
  }, function(it, i)
    if i == 1 then pick("sound", src.sounds, B.sound_empty, function(x) return x.name end)
    elseif i == 2 then pick("sfx", src.sfx, B.sfx_empty, function(x) return x.name end)
    elseif i == 3 then pick("pattern", src.pats, B.pat_empty, function(x) return x.len .. " steps" end)
    elseif i == 4 then pick("song", src.songs, B.song_empty, function(x) return x.name .. "  " .. #x.order .. " patterns" end)
    else
      ui.ask("Replace everything?", "The project takes all of " .. info.title .. ".", "Replace", function()
        local keep = proj
        use_bank(B.copy(src), keep.path, keep.title, keep.is_game)
        proj.dirty = true
        say("everything of " .. info.title .. " imported", C.green)
      end)
    end
  end)
end

local function export_to(e)
  local info = file_info(e)
  ui.ask("Put the sounds into " .. short_name(e.path) .. "?",
         info.bank and "Its sounds and music are replaced by these." or "It gets these sounds and music.",
         "Export", function()
           if write_to(e.path, info.title) then
             e.info = nil
             if proj.path == e.path then proj.dirty = false end
             say("exported to " .. short_name(e.path), C.green, 300)
           end
         end)
end

----------------------------------------------------------------- item copy, paste, clear

local clip = {}
local ITEM = {
  { "sound", function() return bank.sounds, cur.sound end, B.sound },
  { "sound effect", function() return bank.sfx, cur.sfx end, B.sfx },
  { "pattern", function() return bank.pats, cur.pat end, B.pat },
  { "song", function() return bank.songs, cur.song end, B.song },
}
local function item_copy()
  local it = ITEM[cur.page]
  local list, i = it[2]()
  clip[cur.page] = B.copy(list[i + 1])
  say(it[1] .. " " .. i .. " copied", C.dim, 90)
end
local function item_paste()
  local it = ITEM[cur.page]
  if not clip[cur.page] then say("copy a " .. it[1] .. " first", C.dim) return end
  local list, i = it[2]()
  begin_edit()
  set(list, i + 1, B.copy(clip[cur.page]))
  if cur.page == 3 then learn_tracks() end
  say(it[1] .. " pasted into " .. i, C.dim, 90)
end
local function item_clear()
  local it = ITEM[cur.page]
  local list, i = it[2]()
  begin_edit()
  set(list, i + 1, it[3]())
  say(it[1] .. " " .. i .. " cleared (undo brings it back)", C.dim, 120)
end

----------------------------------------------------------------- menu

local HELP = {
  "gamepad                               keyboard",
  "d-pad      move                       arrows     move",
  "A          add / remove, choose       Enter      add / remove",
  "A + up/dn  note +-1 (or the value)    - =        note / value -1 +1",
  "A + l/r    octave (or +-10)           _ +        octave / +-10",
  "Y + up/dn  sound of the step          [ ]        sound",
  "Y + l/r    volume of the step         ; '        volume",
  "B + up/dn  effect                     k l        effect",
  "B + l/r    amount of the effect       o p        amount",
  "X          clear the step             Backspace  clear",
  "Y          listen, insert a copy      i          listen, insert",
  "START      play / stop                Space      play / stop",
  "SELECT + l/r  page                    F1-F4 Tab  page",
  "SELECT + up/dn  the next sound,       PgUp PgDn  next sound, pattern...",
  "                pattern, song...      Z S X.. Q 2 W..  piano  , . octave",
  "SELECT     this menu                  Ctrl+S save  Ctrl+O open  F5 try",
  "START + SELECT together leave the     Ctrl+C Ctrl+V copy, paste",
  "editor (as Ctrl+Esc)                  F12 held: all the keys",
  "F6 (or the menu): the assistant writes beats, backing tracks, bass lines,",
  "arpeggios, melodies and sound effects for the words (\"base lofi in re\").",
  "SOUNDS: down past the last row goes to FILTER, then WAVE & SPACE;",
  "the menu's Instrument... puts a ready-made sound in (it plays as you choose).",
}

-- the keys while F12 is held, under the system's (keyhelp(), the kernel
-- shows them): keyboard keys in lower case, the pad's buttons in upper case
local KEYHELP = {
  { "f1 - f4 / tab", "sounds, sfx, pattern, song / the next page" },
  { "up down left right", "move" },
  { "enter", "add / remove, choose" },
  { "- / =", "note or value -1 / +1" },
  { "shift - / shift =", "octave or value -10 / +10" },
  { "[ / ]", "the sound of the step" },
  { "; / '", "the volume of the step" },
  { "k / l", "effect" },
  { "o / p", "amount of the effect" },
  { "backspace", "clear the step" },
  { "i", "listen, insert a copy (the pad's Y)" },
  { "z s x d c v g b h n j m", "piano" },
  { "q 2 w 3 e r 5 t 6 y 7 u", "piano, an octave up" },
  { ", / .", "the piano's octave" },
  { "space", "play / stop" },
  { "pgup / pgdn", "the next sound, pattern, song" },
  { "ctrl e", "export to a game" },
  { "f6", "the assistant: beats, backing tracks, melodies, sound effects" },
  "pad",
  { "DPAD", "move" },
  { "A", "add / remove, choose" },
  { "A UPDOWN / A LEFTRIGHT", "note or value / octave" },
  { "Y UPDOWN / Y LEFTRIGHT", "sound / volume of the step" },
  { "B UPDOWN / B LEFTRIGHT", "effect / amount" },
  { "X", "clear the step" },
  { "Y", "listen, insert a copy" },
  { "START", "play / stop" },
  { "SELECT LEFTRIGHT", "page" },
  { "SELECT UPDOWN", "the next sound, pattern, song" },
  { "SELECT", "the menu" },
}

----------------------------------------------------------------- the assistant (F6)

-- ai.music (src/ai/music.c) in the assistant's panel (require "assist",
-- mode "music"): rhythms, backing tracks, bass lines, arpeggios, melodies
-- and sound effects; each one heard while it is chosen, A puts it in the
-- bank. Rhythms and backing tracks go into new patterns with a song of
-- their own; bass lines, arpeggios and melodies over the pattern on the
-- page (they follow its key and chords); a sound effect into the first
-- free slot. Their instruments become sounds of the bank (the ones with
-- the same name are used again).

local assist_lib = false
local function assist_mod()
  if assist_lib == false then
    local ok, m = pcall(require, "assist")
    assist_lib = ok and type(m) == "table" and m or nil
  end
  return assist_lib
end

local SOUND_FIELDS = { "wave", "duty", "vol", "a", "d", "s", "r", "pitch", "ptime", "vdepth", "vrate", "detune" }

-- the bank's sound for an instrument: the one with its name, or a free one
local function sound_for(b, name, put)
  local up = name8(name)
  for i = 1, B.NSOUND do if b.sounds[i].name == up then return i - 1 end end
  local p = instrument and instrument(name)
  if not p then return nil end
  for i = 1, B.NSOUND do
    local s = b.sounds[i]
    if B.sound_empty(s) then
      for _, k in ipairs(SOUND_FIELDS) do put(s, k, p[k]) end
      put(s, "tone", B.copy(p.tone))
      put(s, "name", up)
      return i - 1
    end
  end
  return nil
end

-- the notes of the patterns from the one on the page, as ai.music's
-- context (16 steps to a bar)
local function context_notes(npats)
  local notes, bars, off = {}, {}, 0
  for k = 0, npats - 1 do
    local p = bank.pats[(cur.pat + k) % B.NPAT + 1]
    for t = 1, 8 do
      local tr = p.tracks[t]
      if tr then
        for i = 1, p.len do
          local n = s_note(tr[i] or 0)
          if n > 0 and n < 128 and #notes < 500 then
            notes[#notes + 1] = n
            bars[#bars + 1] = (off + i - 1) // 16
          end
        end
      end
    end
    off = off + p.len
  end
  return { notes = notes, bars = bars }
end

-- the piece into a bank (put: set, undone with Ctrl+Z, or a plain write
-- on a copy for the preview); what it made: {sfx =}, {song =, pat =} or {pat =}
local function merge(b, piece, put)
  local map = {}
  for i, name in ipairs(piece.instruments) do
    local s = sound_for(b, name, put)
    if not s then return nil, "no free sound for " .. name:upper() end
    map[i - 1] = s
  end
  local function conv(w)
    local note, inst, vol, fx = w & 255, w >> 8 & 255, w >> 16 & 255, w >> 24 & 255
    if note == 0 and fx == 0 then return 0 end
    if note >= 128 then return mk(128, 0, 255, 0) end
    return mk(note, map[inst] or 0, vol, fx)
  end
  if piece.kind == "sfx" then
    for i = 1, B.NSFX do
      local x = b.sfx[i]
      if B.sfx_empty(x) then
        local steps = {}
        for k, w in ipairs(piece.sfx.steps) do steps[k] = conv(w) end
        put(x, "name", name8(piece.name))
        put(x, "ms", piece.sfx.ms)
        put(x, "len", #steps)
        put(x, "ls", piece.sfx.loop[1])
        put(x, "le", piece.sfx.loop[2])
        put(x, "steps", steps)
        return { sfx = i - 1 }
      end
    end
    return nil, "no free sound effect"
  end
  if piece.kind == "beat" or piece.kind == "base" then
    local free = {}
    for i = 1, B.NPAT do
      if #free < #piece.patterns and B.pat_empty(b.pats[i]) then free[#free + 1] = i - 1 end
    end
    if #free < #piece.patterns then return nil, "not enough free patterns" end
    local g
    for i = 1, B.NSONG do if not g and B.song_empty(b.songs[i]) then g = i - 1 end end
    if not g then return nil, "no free song" end
    for k, pp in ipairs(piece.patterns) do
      local tracks = {}
      for t, steps in pairs(pp.tracks) do
        local tr = {}
        for i = 1, pp.len do tr[i] = conv(steps[i] or 0) end
        tracks[t + 1] = tr
      end
      put(b.pats[free[k] + 1], "len", pp.len)
      put(b.pats[free[k] + 1], "tracks", tracks)
    end
    local song = b.songs[g + 1]
    put(song, "name", name8(piece.name))
    put(song, "bpm", piece.bpm)
    put(song, "swing", piece.swing)
    put(song, "loop", 0)
    put(song, "echo", piece.echo)
    put(song, "room", piece.room)
    put(song, "order", free)
    return { song = g, pat = free[1] }
  end
  -- a bass line, an arpeggio, a melody: its tracks over the patterns from the one on the page
  for k, pp in ipairs(piece.patterns) do
    local p = b.pats[(cur.pat + k - 1) % B.NPAT + 1]
    local len = math.max(B.pat_empty(p) and 0 or p.len, pp.len)
    local tracks = B.copy(p.tracks)
    for t, steps in pairs(pp.tracks) do
      local tr = {}
      for i = 1, len do tr[i] = conv(steps[i] or 0) end
      tracks[t + 1] = tr
    end
    put(p, "len", len)
    put(p, "tracks", tracks)
  end
  return { pat = cur.pat }
end

local preview_due                         -- {piece, frames}: played when the choice rests

local function play_piece(piece)
  local tmp = B.copy(bank)
  local res = merge(tmp, piece, function(t, k, v) t[k] = v end)
  if not res then return end
  audio_bank(B.pack(tmp))
  if res.sfx then
    sfx(res.sfx, PREVIEW_VOICE)
  elseif res.song then
    music(res.song)
  else
    audio_pattern(res.pat, piece.bpm, piece.swing)
  end
end

local function compose()
  local a = assist_mod()
  if not (a and ai and ai.music) then say("the music assistant is not on this console", C.red) return end
  music(-1)
  a.open{
    mode = "music", context = context_notes(4),
    on_preview = function(piece) preview_due = { piece = piece, t = 10 } end,
    on_close = function()
      preview_due = nil
      music(-1)
      sfx(-1)
      bank_changed = true                 -- the bank as it is again
    end,
    on_music = function(piece)
      begin_edit()
      local res, err = merge(bank, piece, set)
      if not res then say("assistant: " .. err, C.red, 300) return end
      if res.sfx then
        cur.page, cur.sfx = 2, res.sfx
      elseif res.song then
        cur.page, cur.song, cur.pat = 4, res.song, res.pat
      else
        cur.page = 3
        learn_tracks()
      end
      local where = res.sfx and string.format("sound effect %02d", res.sfx)
                    or res.song and string.format("song %d", res.song) or string.format("pattern %02d", res.pat)
      say(string.format("assistant: %s into %s (Ctrl+Z takes it back)", piece.name, where), C.green, 300)
      note_log("assistant: " .. piece.gen .. " #" .. piece.seed .. " into " .. where)
    end,
  }
end

local function play_toggle() P[cur.page].play() end

local function open_other()
  local function go() pick_file("open: edit its sounds", { game = true, pack = true }, function(e) open_file(e.path) end) end
  if proj.dirty then ui.ask("Open another file?", "The changes not saved are lost.", "Open", go) else go() end
end

local function new_project()
  local function go() use_bank(B.blank(), nil, "", false); say("new project", C.green) end
  if proj.dirty then ui.ask("Start a new project?", "The changes not saved are lost.", "New", go) else go() end
end

local function try_game()
  if not (proj.path and proj.is_game) then say("open a game first (Ctrl+O)", C.red); return end
  save()
  if not proj.dirty then cart_run(proj.path) end
end

local from_sdk                            -- the bm SDK opened this one (cart_arg().from)

local function open_menu()
  local it = ITEM[cur.page][1]
  local items = {
    { label = music() and "Stop" or "Play", note = "START", act = play_toggle },
    { label = "Undo", note = "Ctrl+Z", act = undo_last, off = #undo == 0 },
    { label = "Save", note = proj.path and ("into " .. short_name(proj.path)) or "Ctrl+S: a new pack", act = save },
    { label = "Save as a new sound pack...", note = "Ctrl+Shift+S", act = ask_pack_name },
    { label = "Open a game or a sound pack...", note = "Ctrl+O", act = open_other },
    { label = "Import from...", note = "a sound, a song...", act = function()
        pick_file("import from", { game = true, pack = true }, import_from)
      end },
    { label = "Export to a game...", act = function() pick_file("export to", { game = true }, export_to) end },
  }
  if cur.page == 1 and instruments then
    items[#items + 1] = { label = "Instrument...", note = "a ready-made sound", act = choose_preset }
  end
  items[#items + 1] = { label = "Compose with the assistant...", note = "F6: beats, melodies, sfx", act = compose }
  if proj.path and proj.is_game then
    items[#items + 1] = { label = "Try it in the game", note = "F5, saves first", act = try_game }
  end
  items[#items + 1] = { label = "Copy this " .. it, note = "Ctrl+C", act = item_copy }
  items[#items + 1] = { label = "Paste over this " .. it, note = "Ctrl+V", act = item_paste, off = not clip[cur.page] }
  items[#items + 1] = { label = "Clear this " .. it, act = item_clear }
  items[#items + 1] = { label = "New project", note = "Ctrl+N", act = new_project }
  items[#items + 1] = { label = "The demo project", note = "sounds and songs", act = function()
    local function go()
      local b = B.parse(cart_audio() or "")
      if b then use_bank(b, nil, "DEMO", false); cur.page = 4; say("the demo: START plays the song", C.green, 300) end
    end
    if proj.dirty then ui.ask("Open the demo?", "The changes not saved are lost.", "Open", go) else go() end
  end }
  items[#items + 1] = { label = "Controls", note = "pad and keyboard", act = function() overlay = { kind = "help", lines = HELP } end }
  if from_sdk and proj.path then              -- opened by the bm SDK on this file: the way back
    items[#items + 1] = { label = "Back to bm SDK", note = "saves first", act = function()
      save()
      if not proj.dirty then cart_tool("sdk", proj.path) end
    end }
  end
  items[#items + 1] = { label = "Exit", act = function()
    if proj.dirty then ui.ask("Exit without saving?", "The changes not saved are lost.", "Exit", quit) else quit() end
  end }
  local title = "bm SOUND  -  " .. (proj.path and short_name(proj.path) or (proj.title ~= "" and proj.title or "new project"))
  ui.choose(title, items, function(x) x.act() end)
  overlay.footer = proj.dirty and "changes not saved yet" or "everything saved"
end

----------------------------------------------------------------- top bar and help line

local TABS = { "SOUNDS", "SFX", "PATTERN", "SONG" }

local function draw_top()
  rectfill(0, 0, W, TOP - 2, C.dark)
  ui.box(6, 5, 26, 22, C.orange)
  print("bm", 11, 8, C.dark)
  print("SOUND", 38, 8, C.text)
  local x = 100
  for i, t in ipairs(TABS) do
    local fk = "f" .. i                               -- its key, as a chip
    local lx = snap(x + 4 + prompt(fk) + 2)
    local w = lx + #t * 8 + 6 - x
    local on = cur.page == i
    ui.box(x, 5, w, 22, on and PAGE_C[i] or C.panel)
    prompt(fk, x + 4, 8)
    print(t, lx, 8, on and C.dark or C.dim)
    x = x + w + 6
  end
  print("OCT " .. octave, 412, 8, C.faint)
  -- transport
  local song, order = music()
  local px = 470
  if song or sfxpos(PREVIEW_VOICE) then
    ui.play_icon(px, 10, (frame // 15) % 2 == 0 and C.green or C.dim)
    local what = song == -2 and "PATTERN" or (song and ("SONG " .. song) or "SFX")
    print(what, px + 16, 8, C.green)
  else
    ui.stop_icon(px, 10, C.faint)
    print("STOP", px + 16, 8, C.faint)
  end
  local name = proj.path and short_name(proj.path) or (proj.title ~= "" and proj.title or "NEW")
  if #name > 12 then name = name:sub(1, 12) end
  local nx = W - 8 - #name * 8
  print(name, nx, 8, proj.dirty and C.yellow or C.dim)
  if proj.dirty then circfill(nx - 8, 15, 3, C.yellow) end
end

local function draw_bottom()
  rectfill(0, BOTTOM + 2, W, H - BOTTOM - 2, C.dark)
  local y = BOTTOM + 3
  if msg_t > 0 and msg then
    print(msg:sub(1, 79), 8, y, msg_c)
    return
  end
  local x = 8
  for _, h in ipairs(hints) do
    if x + ui.hint_w(x, h[1], h[2], h[3]) > W - 108 then break end
    x = ui.hint(x, y, h[1], h[2], h[3])
  end
  ui.hint(W - 104, y, "SELECT", nil, "menu")
end

----------------------------------------------------------------- input

local sel_used, mod_used, mod_was = false, {}, {}
local MODS = { A = 4, Y = 7, B = 5 }

local function goto_page(i)
  cur.page = (i - 1) % 4 + 1
  if cur.page == 3 then learn_tracks() end
end

local function pad_logic()
  local dx = (rep[1] and 1 or 0) - (rep[0] and 1 or 0)
  local dy = (rep[3] and 1 or 0) - (rep[2] and 1 or 0)
  if btn(9) then
    if dx ~= 0 then goto_page(cur.page + dx); sel_used = true
    elseif dy ~= 0 then begin_edit(); P[cur.page].item(dy); sel_used = true end
    mod_was.SELECT = true
    return
  elseif mod_was.SELECT then
    mod_was.SELECT = false
    if not sel_used then open_menu() end
    sel_used = false
    return
  end
  if btnp(8) then play_toggle() end
  local mod = btn(4) and "A" or btn(7) and "Y" or btn(5) and "B" or nil
  if (dx ~= 0 or dy ~= 0) then
    if mod then
      begin_edit()
      P[cur.page].edit(mod, dx, dy)
      mod_used[mod] = true
    else
      P[cur.page].move(dx, dy)
    end
  end
  for m, b in pairs(MODS) do
    if btn(b) then
      if not mod_was[m] then mod_used[m] = false end
      mod_was[m] = true
    elseif mod_was[m] then
      mod_was[m] = false
      if not mod_used[m] then begin_edit(); P[cur.page].tap(m) end
    end
  end
  if btnp(6) then begin_edit(); P[cur.page].clear() end
end

local KEY_EDIT = {
  ["-"] = { "A", 0, 1 }, ["="] = { "A", 0, -1 }, ["_"] = { "A", -1, 0 }, ["+"] = { "A", 1, 0 },
  ["["] = { "Y", 0, 1 }, ["]"] = { "Y", 0, -1 }, [";"] = { "Y", -1, 0 }, ["'"] = { "Y", 1, 0 },
  k = { "B", 0, 1 }, l = { "B", 0, -1 }, o = { "B", -1, 0 }, p = { "B", 1, 0 },
}

local function global_key(k)
  local page = P[cur.page]
  if k == "f1" or k == "f2" or k == "f3" or k == "f4" then goto_page(tonumber(k:sub(2)))
  elseif k == "\t" then goto_page(cur.page + 1)
  elseif k == "esc" then open_menu()
  elseif k == " " then play_toggle()
  -- the system's keys (the kernel's syskeys.c)
  elseif k == "^s" then save()
  elseif k == "^S" then ask_pack_name()
  elseif k == "^n" then new_project()
  elseif k == "f5" or k == "^r" then try_game()
  elseif k == "^z" then undo_last()
  elseif k == "^c" then item_copy()
  elseif k == "^v" then item_paste()
  elseif k == "^o" then open_other()
  elseif k == "^e" then pick_file("export to", { game = true }, export_to)
  elseif k == "f6" then compose()
  elseif k == "up" then page.move(0, -1)
  elseif k == "down" then page.move(0, 1)
  elseif k == "left" then page.move(-1, 0)
  elseif k == "right" then page.move(1, 0)
  elseif k == "\n" then begin_edit(); page.tap("A")
  elseif k == "i" then begin_edit(); page.tap("Y")      -- the pad's Y
  elseif k == "\b" or k == "del" then begin_edit(); page.clear()
  elseif k == "pgup" then page.item(-1)
  elseif k == "pgdn" then page.item(1)
  elseif k == "," then octave = math.max(0, octave - 1); say("piano: octave " .. octave, C.dim, 60)
  elseif k == "." then octave = math.min(8, octave + 1); say("piano: octave " .. octave, C.dim, 60)
  elseif page.key and page.key(k) then return
  elseif KEY_EDIT[k] then
    local e = KEY_EDIT[k]
    begin_edit()
    page.edit(e[1], e[2], e[3])
  elseif PIANO[k] and page.piano then
    begin_edit()
    page.piano(clamp(12 * (octave + 1) + PIANO[k], 1, 127))
  end
end

----------------------------------------------------------------- main

-- Ctrl+Esc or PS (the system's keys): back to bm's menu; with unsaved
-- changes it asks first, and the same again leaves without saving
function _exit()
  if not proj.dirty or (overlay and overlay.leaving) then return true end
  ui.ask("Exit without saving?", "The changes are lost. Ctrl+Esc again: exit.", "Exit", quit)
  overlay.leaving = true
  return false
end

function _init()
  keyp()                                  -- the keyboard types (piano, names)
  if keyhelp then keyhelp(KEYHELP, "Sound") end
  local a = cart_arg()
  from_sdk = a and a.from == "sdk"
  if a and a.path and open_file(a.path) then
    if a.back then
      say(a.error and ("the game stopped: " .. a.error:sub(1, 60)) or "back from the game", a.error and C.red or C.green, 300)
    end
  else
    local b = B.parse(cart_audio() or "")
    if b then
      use_bank(b, nil, "DEMO", false)
      cur.page = 4
      say("the demo project: START plays the song, SELECT+left/right pages, SELECT menu", C.green, 600)
    end
  end
end

function _update()
  frame = frame + 1
  if msg_t > 0 then msg_t = msg_t - 1 end
  local a = assist_lib or nil
  if a and a.is_open() then
    if preview_due then
      preview_due.t = preview_due.t - 1
      if preview_due.t <= 0 then
        local piece = preview_due.piece
        preview_due = nil
        play_piece(piece)
      end
    end
    if a.update() then return end
  end
  read_pad()
  while true do
    local k = keyp()
    if not k then break end
    if overlay then overlay_key(k) else global_key(k) end
  end
  if overlay then overlay_pad() else pad_logic() end
  if bank_changed then
    bank_changed = false
    local ok, err = audio_bank(B.pack(bank))
    if not ok then say("the sounds cannot play: " .. tostring(err), C.red) end
  end
end

function _draw()
  cls(C.bg)
  P[cur.page].draw()
  draw_top()
  draw_bottom()
  if overlay then draw_overlay() end
  if assist_lib and assist_lib.is_open() then assist_lib.draw() end
end
