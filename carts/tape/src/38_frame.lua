-- bm Tape screen (design 4.1): a small studio on one display, as an OP-1 or a cassette multitrack has one.
-- 1280 x 720 (640 x 360: everything half the size):
--   top bar    the eight modules as tabs (F1..F8), then the transport: play, tempo, bar.beat.step, REC.
--   scene      960 x 360 at the left: the picture of the page (00_core.lua draws it at SCL = 3 pixels a unit).
--   inspector  320 x 360 at the right: the page, its sister pages, the four encoders with their values, track and voices.
--   keyboard   the lower part (39_keys.lua): a drawing of the keyboard that shows what each key does on this page.
local MODS = { "SYNTH", "DRUM", "SEQ", "TAPE", "MIX", "FX", "DJ", "TEMPO" }
local G = 1                                  -- the screen against 1280 x 720
local LY = { W = 0, H = 0 }
local function g(v) return floor(v * G + 0.5) end

-- what to do on the page, one line (80 characters at most)
local HINT = {
  ["SYNTH ENGINE"] = "Encoders: the 4 macros.  UP DOWN: engine.  OPTION: sound.  Play the piano.",
  ["SYNTH ENVELOPE"] = "Encoders: attack, decay, sustain, release.  Play the piano to hear the shape.",
  ["SYNTH FILTER"] = "Encoders: cutoff, resonance, LFO rate, LFO depth.  Play the piano to hear it.",
  ["DRUM PAD"] = "Hit a pad (Z to M) to pick it.  Encoders: start, decay, pitch, level of the pad.",
  ["SEQ GRID"] = "Encoder 1 moves the step.  A sets or clears it.  Arm REC (Return) and play to write notes.",
  ["TAPE"] = "Encoder 1 moves the slot.  A writes the pattern of the track here.  Delete clears.",
  ["MIX LEVEL"] = "Encoders: the level of tracks 1 to 4.  Play to see the meters.",
  ["MIX PAN"] = "Encoders: the pan of tracks 1 to 4, left to right.",
  ["FX ECHO"] = "Encoders: time, feedback, wet, and the echo send of the current track.",
  ["FX ROOM"] = "Encoders: size, damp, wet, and the room send of the current track.",
  ["FX CHORUS"] = "Encoders: rate, depth, wet, and the chorus send of the current track.",
  ["DJ"] = "Preview only: the decks make no sound yet.",
  ["TEMPO"] = "Encoders: tempo, swing, metronome level, pattern length.",
}

-- the pixel sizes of the screen: scene window, inspector, keyboard; SCL and OY make the scene land in its window
local function layout()
  local W, H = SCREEN_W, SCREEN_H
  if LY.W == W and LY.H == H then return end
  G = W / 1280
  SCL = 3 * G
  LY.W, LY.H = W, H
  LY.big = G >= 1
  LY.ts = LY.big and 2 or 1                     -- text scale of the large text
  LY.top, LY.sw, LY.sh, LY.kb = g(44), g(960), g(360), g(404)
  OX, OY = 0, LY.top - floor(14 * SCL + 0.5)
end

local function fmt_param(p)
  local f = p.format
  if type(f) == "function" then return f(p.value) end
  return string.format(f, p.value)
end

-- a tab of the top bar
local function tab(i, on)
  local w, x = g(120), (i - 1) * g(120)
  if on then rectfill(x, 0, w, LY.top - 1, C.dim) end
  line(x, LY.top - 1, x + w - 1, LY.top - 1, on and C.e4 or C.dim)
  if LY.big then
    print("F" .. i, x + 8, 15, on and C.nav or C.grid)
    print(MODS[i], x + 34, 6, on and C.e4 or C.mute, 2)
  else
    print(MODS[i], x + 4, 3, on and C.e4 or C.mute)
  end
end

local function transport(t)
  local x0, run, big = g(968), tr.run, LY.big
  if run then
    polyfill({ x0, g(10), x0, g(34), x0 + g(24), g(22) }, C.pad)
  else
    rect(x0 + g(2), g(10), g(22), g(24), C.mute)
  end
  local bx = x0 + g(36)
  local sx = bx + g(96)
  local tk = now_tick()
  print(string.format("%.1f", S.bpm), bx, g(6), C.e4, LY.ts)
  print(string.format("%03d.%d.%d", 1 + tk // 96, 1 + (tk // 24) % 4, 1 + (tk // 6) % 4), sx, g(6), run and C.e4 or C.mute, LY.ts)
  if big then
    print("BPM", bx, 30, C.mute)
    print("BAR.BEAT.STEP", sx, 30, C.mute)
  end
  if tr.rec then
    ellipsefill(g(1250), g(14), g(8), g(8), (t * 2) % 1 < 0.6 and C.hot or C.dim)
    if big then print("REC", g(1234), 30, C.hot) end
  end
end

-- one encoder cell of the inspector: label, value, a bar, the keys
local function cell(i, p, x, y, w)
  local c = ECOL[i]
  local h = g(48)
  rectfill(x, y, g(4), h, c)
  print(p.label, x + g(14), y, c)
  print(fmt_param(p), x + g(14), y + g(18), C.e4, LY.ts)
  print(({ "9 O", "0 P", "- [", "= ]" })[i], x + w - g(30), y, C.grid)
  local f = clamp((p.value - p.min) / max(1e-9, p.max - p.min), 0, 1)
  local bw = g(110)
  rect(x + w - bw, y + g(28), bw, g(10), C.dim)
  rectfill(x + w - bw, y + g(28), max(1, floor(bw * f)), g(10), c)
end

-- the whole frame but the keyboard: bars, borders, inspector, hint
local function draw_screen(page, t)
  local sc, sx = page.sc, LY.sw
  for i = 1, 8 do tab(i, i == page.mod) end
  line(sx, 0, sx, LY.kb, C.dim)
  line(0, LY.kb, LY.W - 1, LY.kb, C.dim)
  transport(t)
  local x, w = sx + g(14), LY.W - sx - g(28)
  local y = LY.top + g(8)
  print(page.label, x, y, C.e4, LY.ts)
  if page.preview then print("PREVIEW", x + g(220), y + g(34), C.hot) end
  if sc.sub then print(sc.sub(), x, y + g(34), C.mute) end
  -- the pages of the module, the current one in brackets (TAB goes to the next)
  local px0 = x
  for _, pg in ipairs(PAGES) do
    if pg.mod == page.mod then
      local nm = pg.label:match("^%S+ (.+)$") or pg.label
      local on = pg == page
      px0 = print((on and "[" or " ") .. nm .. (on and "] " or "  "), px0, y + g(54), on and C.e4 or C.grid)
    end
  end
  for i, p in ipairs(sc.params) do cell(i, p, x, y + g(84) + (i - 1) * g(54), w) end
  -- the track, the pattern, the voices
  local sy = LY.kb - g(44)
  local p = S.pat[S.trk]
  local nx = print(string.format("T%d %s  PAT %s  ", S.trk, S.trk == 1 and "DRUM" or ENGINES[trk[S.trk].eng].name, LETTERS:sub(p, p)), x, sy, C.mute)
  print(S.song and "SONG" or "PAT", nx, sy, S.song and C.e4 or C.mute)
  local fx = PUNCH.label()
  local m = ""
  for i = 1, 4 do
    if aget(Q.mutes, i) ~= 0 then m = m .. i end
  end
  if SV.msg and t < SV.until_t then
    print(SV.msg, x, sy + g(20), SV.err and C.hot or C.e4)
  elseif engine.err then
    print("ENGINE " .. engine.err:sub(1, 30), x, sy + g(20), C.hot)
  elseif fx then
    print(fx, x, sy + g(20), C.hot)
  elseif m ~= "" then
    print("MUTED " .. m, x, sy + g(20), C.mute)
  end
  for i = 0, 7 do
    rectfill(x + w - g(96) + i * g(12), sy + g(22), g(8), g(10), playing(i) and C.hot or C.dim)
  end
  print(HINT[page.label] or "", g(14), LY.kb + g(8), C.mute)
end
