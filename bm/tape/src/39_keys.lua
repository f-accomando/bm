-- bm Tape keyboard map (design 4.4): the lower part of the screen draws the keyboard. Each key shows what it does on the
-- page that is open now: the colour is the group, a lit key is the one that is on (module, track, transport), a bright key is
-- held. A key with no label does nothing here. The rows are data: change ROWS to fit another keyboard (HID usage, width in
-- keys, cap); the roles are in role().
local draw_keys
do
local NOTE = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }
local MODU, ENCU, PADI = {}, {}, {}
for i, u in ipairs(FKEYS) do MODU[u] = i end
for i, e in ipairs(ENC) do ENCU[e[1]] = { i, 1 }; ENCU[e[2]] = { i, -1 } end
for i, u in ipairs(PADK) do PADI[u] = i end
local TRKU = {}
for n, u in ipairs(TRKEYS) do TRKU[u] = n end
local PUNCHU = { [K.D5] = "STUT", [K.D6] = "HALF", [K.D7] = "SWEEP", [K.D8] = "CRUSH" }

-- the rows of an Apple Magic Keyboard (ANSI, no numpad): { usage, width, cap [, half] }; usage false is a gap, half 1 is the
-- upper half of a key (the arrows). The F keys are the modules.
local ROWS = {
  { { 0x29, 1.5, "esc" }, { false, 0.25 }, { 0x3A, 1, "F1" }, { 0x3B, 1, "F2" }, { 0x3C, 1, "F3" }, { 0x3D, 1, "F4" },
    { 0x3E, 1, "F5" }, { 0x3F, 1, "F6" }, { 0x40, 1, "F7" }, { 0x41, 1, "F8" }, { 0x42, 1, "F9" }, { 0x43, 1, "F10" },
    { 0x44, 1, "F11" }, { 0x45, 1, "F12" } },
  { { 0x35, 1, "~" }, { 0x1E, 1, "1" }, { 0x1F, 1, "2" }, { 0x20, 1, "3" }, { 0x21, 1, "4" }, { 0x22, 1, "5" }, { 0x23, 1, "6" },
    { 0x24, 1, "7" }, { 0x25, 1, "8" }, { 0x26, 1, "9" }, { 0x27, 1, "0" }, { 0x2D, 1, "-" }, { 0x2E, 1, "=" }, { 0x2A, 2, "delete" } },
  { { 0x2B, 1.5, "tab" }, { 0x14, 1, "Q" }, { 0x1A, 1, "W" }, { 0x08, 1, "E" }, { 0x15, 1, "R" }, { 0x17, 1, "T" }, { 0x1C, 1, "Y" },
    { 0x18, 1, "U" }, { 0x0C, 1, "I" }, { 0x12, 1, "O" }, { 0x13, 1, "P" }, { 0x2F, 1, "[" }, { 0x30, 1, "]" }, { 0x31, 1.5, "\\" } },
  { { 0x39, 1.75, "caps" }, { 0x04, 1, "A" }, { 0x16, 1, "S" }, { 0x07, 1, "D" }, { 0x09, 1, "F" }, { 0x0A, 1, "G" }, { 0x0B, 1, "H" },
    { 0x0D, 1, "J" }, { 0x0E, 1, "K" }, { 0x0F, 1, "L" }, { 0x33, 1, ";" }, { 0x34, 1, "'" }, { 0x28, 2.25, "return" } },
  { { 0xE1, 2.25, "shift" }, { 0x1D, 1, "Z" }, { 0x1B, 1, "X" }, { 0x06, 1, "C" }, { 0x19, 1, "V" }, { 0x05, 1, "B" }, { 0x11, 1, "N" },
    { 0x10, 1, "M" }, { 0x36, 1, "," }, { 0x37, 1, "." }, { 0x38, 1, "/" }, { 0xE5, 2.75, "shift" } },
  { { false, 1 }, { 0xE0, 1, "ctrl" }, { 0xE2, 1, "opt" }, { 0xE3, 1.25, "cmd" }, { 0x2C, 5.5, "space" }, { 0xE7, 1.25, "cmd" },
    { 0xE6, 1, "opt" }, { 0x50, 1, "<" }, { 0x52, 1, "^", 1 }, { 0x4F, 1, ">" } },
}

-- what key u does on this page: label, group colour, whether it is on, and for a piano key whether it is black
local function role(u, page, sc)
  local m = MODU[u]
  if m then return MODS[m], C.nav, page.mod == m end
  local e = ENCU[u]
  if e then
    local lb = sc.params[e[1]].label
    return lb:sub(1, 5) .. (e[2] > 0 and "+" or "-"), ECOL[e[1]], false
  end
  local pi = PADI[u]
  if pi then
    if S.trk == 1 then
      if pi > 12 then return end
      return PADS[pi].name, C.pad, S.pad == pi, BLACK[pi]
    end
    local n = key_note(S.trk, pi)
    return NOTE[n % 12 + 1] .. (n // 12 - 1), C.pad, live.held[pi] ~= nil, BLACK[pi]
  end
  local n = TRKU[u]
  if n then return "T" .. n, C.nav, S.trk == n end
  if PUNCHU[u] then return PUNCHU[u], C.hot, false end
  if u == K.SPACE then return tr.run and "STOP" or "PLAY", C.hot, tr.run end
  if u == K.ENTER then return "REC", C.hot, tr.rec end
  if u == K.TAB then return "PAGE", C.nav, false end
  if u == K.LEFT then return "PG<", C.nav, false end
  if u == K.RIGHT then return "PG>", C.nav, false end
  if (u == K.UP or u == K.DOWN) and sc.arrows then return u == K.UP and "ENG+" or "ENG-", C.nav, false end
  if u == K.BSLASH then return S.song and "SONG" or "PAT", C.mute, S.song end
  if u == K.GUIL then return "PAT-", C.mute, false end
  if u == K.GUIR then return "PAT+", C.mute, false end
  if u == K.ALTL then return "SND-", C.mute, false end
  if u == K.ALTR then return "SND+", C.mute, false end
  if (u == K.F or u == K.K) and S.trk > 1 then return u == K.F and "OCT-" or "OCT+", C.pad, false end
  if u == K.A then
    if sc == grid_page then return "STEP", C.e3, false elseif sc == tape_page then return "WRITE", C.e3, false end
  end
  if u == K.BKSP and (sc == grid_page or sc == tape_page) then return "CLEAR", C.e3, false end
  if u == K.SHL or u == K.SHR then return "SHIFT", C.mute, false end
  if u == K.CTL or u == K.CTR then return "CTRL", C.mute, false end
  if u == 0x29 then return "EXIT", C.nav, false end
end

local function key(x, y, w, h, cap, u, page, sc)
  local txt, col, on, black = role(u, page, sc)
  local down = held[u]
  local fill, edge = C.key, C.dim
  if txt then
    edge = col
    if on then fill = fade(col, 0.4) elseif col == C.pad then fill = black and C.ink or fade(col, 0.22) end
  end
  if down then fill, edge = col or C.mute, col or C.mute end
  rectfill(x, y, w, h, fill)
  rect(x, y, w, h, edge)
  local cols = (w - 4) // 8
  if LY.big and h > g(30) then
    print(cap:sub(1, cols), x + 4, y + 3, down and C.ink or C.grid)
    if txt then print(txt:sub(1, cols), x + 4, y + h - 19, down and C.ink or col) end
  else
    local s = (txt or cap):sub(1, cols)
    print(s, x + (w - #s * 8) // 2, y + (h - 16) // 2, down and C.ink or (txt and col or C.grid))
  end
end

local function legend(x, y)
  local rows = {
    { C.nav, "MODULES TRACKS PAGES" }, { C.pad, "PIANO / PADS" }, { C.hot, "PLAY REC PUNCH" }, { C.e3, "EDIT STEPS" }, { C.mute, "PATTERN SOUND" },
  }
  print("KEY COLORS", x, y, C.e4)
  for i, r in ipairs(rows) do
    rectfill(x, y + 24 + (i - 1) * 20, 12, 12, r[1])
    print(r[2], x + 20, y + 22 + (i - 1) * 20, C.mute)
  end
  local ey = y + 24 + #rows * 20
  for i = 1, 4 do rectfill(x + (i - 1) * 14, ey, 12, 12, ECOL[i]) end
  print("ENCODERS", x + 64, ey - 2, C.mute)
  local ty = ey + 28
  for i, s in ipairs({ "SHIFT+1-4  MUTE", "SHIFT+ENC  FINE", "HOLD 5-8  PUNCH", "CTRL+S  SAVE", "CTRL+N  NEW SONG", "ESC  EXIT PAGE" }) do
    print(s, x, ty + (i - 1) * 18, C.grid)
  end
end

function draw_keys(page)
  local sc, p = page.sc, g(56)
  local x0, y0, rh = g(14), LY.kb + g(34), g(46)
  local right = 0
  for ri, row in ipairs(ROWS) do
    local x, y = x0, y0 + (ri - 1) * rh
    for _, k in ipairs(row) do
      if k[1] then
        local kw, kh, ky = floor(k[2] * p) - g(4), rh - g(4), y
        if k[4] then kh = rh // 2 - g(2); ky = y end
        key(x, ky, kw, kh, k[3], k[1], page, sc)
        if k[1] == 0x52 then key(x, y + rh // 2, kw, kh, "v", 0x51, page, sc) end
      end
      x = x + floor(k[2] * p)
    end
    right = max(right, x)
  end
  if LY.big then legend(right + 24, y0) end
end
end
