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

-- a tab of the top bar (Teenage Engineering Field tactile membrane tab with F-key badge)
local SHORT_MOD = { "SYN", "DRM", "SEQ", "TAP", "MIX", "FX", "DJ", "BPM" }
local function tab(i, on)
  local w, x = g(120), (i - 1) * g(120)
  local pad = g(2)
  local bg = on and TE.dark or TE.panel
  rectfill(x + pad, g(3), w - pad * 2, LY.top - g(6), bg)
  rect(x + pad, g(3), w - pad * 2, LY.top - g(6), on and TE.dark or TE.border)
  if on then rectfill(x + pad, g(3), w - pad * 2, g(3), TE.orange) end
  if LY.big then
    print("F" .. i, x + g(8), g(14), on and TE.orange or TE.mute)
    print(SHORT_MOD[i], x + g(36), g(14), on and TE.white or TE.dark, 2)
  else
    print("F" .. i .. " " .. SHORT_MOD[i], x + g(5), g(7), on and TE.white or TE.dark)
  end
end

local function transport(t)
  local x0, run, big = g(964), tr.run, LY.big
  rectfill(x0, g(3), LY.W - x0 - g(4), LY.top - g(6), TE.well)
  rect(x0, g(3), LY.W - x0 - g(4), LY.top - g(6), TE.border)
  local px = x0 + g(8)
  if run then
    polyfill({ px, g(10), px, g(28), px + g(16), g(19) }, TE.green)
  else
    rectfill(px + g(2), g(12), g(12), g(12), TE.mute)
  end
  if big then print("SPC", px - g(1), g(28), TE.mute) end
  local bx = x0 + g(34)
  local sx = bx + g(94)
  local tk = now_tick()
  print(string.format("%.1f", S.bpm), bx, g(9), TE.white, LY.ts)
  print(string.format("%03d.%d.%d", 1 + tk // 96, 1 + (tk // 24) % 4, 1 + (tk // 6) % 4), sx, g(9), run and C.e1 or TE.mute, LY.ts)
  local rx = g(1224)
  -- Keep y=26..44 at x>=1224 as a C.ink OLED REC-arm window (lit in C.hot only when tr.rec)
  rectfill(rx, g(26), g(56), g(18), C.ink)
  if tr.rec then
    local pulse = (t * 2) % 1 < 0.6
    rectfill(rx, g(6), g(50), g(19), pulse and C.hot or TE.dark)
    ellipsefill(rx + g(8), g(15), g(4), g(4), TE.white)
    if big then
      print("RET", rx + g(16), g(8), TE.white)
      print("REC", rx + g(8), g(28), C.hot)
    else
      ellipsefill(rx + g(12), g(34), g(4), g(4), C.hot)
    end
  else
    rect(rx, g(6), g(50), g(19), TE.border)
    ellipsefill(rx + g(8), g(15), g(4), g(4), TE.mute)
    if big then
      print("RET", rx + g(16), g(8), TE.mute)
    end
  end
end

-- vector arc helper for the 4 rotary encoder dials
local function dial_arc(cx, cy, r_in, r_out, a0, a1, col)
  local steps = 16
  for s = 0, steps - 1 do
    local t0 = a0 + (a1 - a0) * (s / steps)
    local t1 = a0 + (a1 - a0) * ((s + 1) / steps)
    local c0, s0, c1, s1 = cos(t0), sin(t0), cos(t1), sin(t1)
    polyfill({
      cx + c0 * r_in, cy + s0 * r_in,
      cx + c0 * r_out, cy + s0 * r_out,
      cx + c1 * r_out, cy + s1 * r_out,
      cx + c1 * r_in, cy + s1 * r_in,
    }, col)
  end
end

-- one rotary encoder dial of the inspector (2x2 grid): label, dial arc in ECOL[i], value, bound keys
local ENC_KEYS = { "9/O", "0/P", "-/[", "=/" .. "]" }
local function dial_cell(i, p, cx, cy, cw, ch)
  local c = ECOL[i]
  local x0, y0 = cx - cw // 2, cy - ch // 2
  rectfill(x0, y0, cw, ch, TE.panel)
  rect(x0, y0, cw, ch, TE.border)
  -- label + bound keys header
  print(p.label:sub(1, 8), x0 + g(6), y0 + g(4), TE.dark)
  print(ENC_KEYS[i], x0 + cw - g(30), y0 + g(4), TE.mute)
  -- dark recess ring so all 4 encoder colours (including ivory C.e4) pop crisply
  local r_out, r_in, r_knob = g(27), g(21), g(18)
  local dcy = cy + g(2)
  ellipsefill(cx, dcy, r_out + g(2), r_out + g(2), TE.well)
  local a_start, a_span = 0.75 * PI, 1.5 * PI
  local f = clamp((p.value - p.min) / max(1e-9, p.max - p.min), 0, 1)
  dial_arc(cx, dcy, r_in, r_out, a_start, a_start + a_span, TE.dark)
  if f > 0.01 then
    dial_arc(cx, dcy, r_in, r_out, a_start, a_start + a_span * f, c)
  end
  ellipsefill(cx, dcy, r_knob, r_knob, TE.key)
  ellipse(cx, dcy, r_knob, r_knob, TE.border)
  local ang = a_start + a_span * f
  line(cx + cos(ang) * g(5), dcy + sin(ang) * g(5), cx + cos(ang) * (r_knob - g(2)), dcy + sin(ang) * (r_knob - g(2)), i == 4 and TE.dark or c)
  -- crisp OLED value pill in encoder colour (ensures ECOL[1..4] always visible)
  local vs = fmt_param(p)
  local pw = max(g(56), g(#vs * 8 + 14))
  rectfill(cx - pw // 2, y0 + ch - g(20), pw, g(17), TE.dark)
  rect(cx - pw // 2, y0 + ch - g(20), pw, g(17), TE.border)
  print(vs, cx - g(#vs * 4), y0 + ch - g(18), c)
end

-- the whole frame but the keyboard: TE Field top bar, OLED synthwave header, 2x2 rotary inspector, hint strip
local function draw_screen(page, t)
  local sc, sx = page.sc, LY.sw
  -- Top bar chassis
  rectfill(0, 0, LY.W, LY.top, TE.bg)
  line(0, LY.top - 1, LY.W - 1, LY.top - 1, TE.border)
  for i = 1, 8 do tab(i, i == page.mod) end
  transport(t)

  -- Right inspector: dark OLED header (y=44..110) keeps PREVIEW check & synthwave continuity, TE Field body below
  rectfill(sx, LY.top, LY.W - sx, LY.kb - LY.top, TE.bg)
  rectfill(sx, LY.top, LY.W - sx, g(66), C.ink)
  line(sx, LY.top, sx, LY.kb, TE.border)
  line(sx, LY.top + g(66), LY.W - 1, LY.top + g(66), TE.border)

  local x, w = sx + g(12), LY.W - sx - g(24)
  local y = LY.top + g(6)
  print(page.label, x, y, C.e4, LY.ts)
  if page.preview then print("PREVIEW", x + g(222), y + g(34), C.hot) end
  if sc.sub then print(sc.sub():sub(1, 26), x, y + g(32), C.mute) end
  -- sub-pages with [TAB] badge at top-right of OLED header (above y=84)
  local px0 = x
  local has_sub = false
  for _, pg in ipairs(PAGES) do
    if pg.mod == page.mod then
      local nm = pg.label:match("^%S+ (.+)$") or pg.label
      local on = pg == page
      px0 = print((on and "[" or " ") .. nm .. (on and "] " or "  "), px0, y + g(46), on and C.e1 or C.mute)
      has_sub = true
    end
  end
  if has_sub and LY.big then print("TAB", sx + w - g(14), y + g(2), C.grid) end

  -- 2x2 Rotary Encoder Dials (y=114..322)
  local cw, ch = g(146), g(94)
  for i, p in ipairs(sc.params) do
    local col_i = (i - 1) % 2
    local row_i = (i - 1) // 2
    local cx = sx + g(83) + col_i * g(154)
    local cy = LY.top + g(116) + row_i * g(98)
    dial_cell(i, p, cx, cy, cw, ch)
  end

  -- Track selector pills [1..4] + Mode [\]
  local ty = LY.kb - g(76)
  for ti = 1, 4 do
    local tx = x + (ti - 1) * g(48)
    local on = (S.trk == ti)
    local muted = aget(Q.mutes, ti) ~= 0
    rectfill(tx, ty, g(44), g(22), on and TE.dark or (muted and TE.bg or TE.panel))
    rect(tx, ty, g(44), g(22), on and TE.orange or TE.border)
    print(ti .. ":T" .. ti, tx + g(5), ty + g(4), on and TE.white or (muted and TE.mute or TE.dark))
  end
  local mx = x + g(198)
  rectfill(mx, ty, g(98), g(22), S.song and TE.dark or TE.panel)
  rect(mx, ty, g(98), g(22), TE.border)
  print("P:" .. (S.song and "SONG" or "PAT"), mx + g(6), ty + g(4), S.song and TE.white or TE.dark)

  -- Dark OLED footer strip (y=354..404) matching inspector header
  rectfill(sx, LY.kb - g(50), LY.W - sx, g(50), C.ink)
  line(sx, LY.kb - g(50), LY.W - 1, LY.kb - g(50), TE.border)
  local py = LY.kb - g(44)
  local p = S.pat[S.trk]
  local eng_nm = S.trk == 1 and "DRUM" or ENGINES[trk[S.trk].eng].name
  print(string.format("T%d %s", S.trk, eng_nm:sub(1, 5)), x, py, C.e4)
  print("':" .. LETTERS:sub(p, p), sx + g(84), py, C.e2)
  print(S.song and "SONG" or "PAT", sx + g(126), py, S.song and C.e4 or C.mute)

  -- Status line (y=376..404): pure C.ink unless msg/error/punch/mute is active
  local sy = LY.kb - g(24)
  local fx = PUNCH.label()
  local m = ""
  for i = 1, 4 do
    if aget(Q.mutes, i) ~= 0 then m = m .. i end
  end
  if SV.msg and t < SV.until_t then
    print(SV.msg, x, sy, SV.err and C.hot or C.e4)
  elseif engine.err then
    print("ENGINE " .. engine.err:sub(1, 22), x, sy, C.hot)
  elseif fx then
    print(fx, x, sy, C.hot)
  elseif m ~= "" then
    print("M" .. m, x, sy, C.mute)
  end
  -- 8 Voice LEDs on the right (x >= 1170)
  for i = 0, 7 do
    local vx = x + w - g(98) + i * g(12)
    rectfill(vx, sy + g(2), g(9), g(12), playing(i) and TE.green or C.ink)
    rect(vx, sy + g(2), g(9), g(12), playing(i) and TE.green or C.grid)
  end

  -- Hint strip above keyboard
  rectfill(0, LY.kb, LY.W, g(28), TE.panel)
  line(0, LY.kb, LY.W - 1, LY.kb, TE.border)
  line(0, LY.kb + g(28), LY.W - 1, LY.kb + g(28), TE.border)
  print(HINT[page.label] or "", g(14), LY.kb + g(6), TE.dark)
end
