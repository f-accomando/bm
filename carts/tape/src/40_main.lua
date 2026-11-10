-- bm Tape main: pages, keys, encoders, transport, key strip, exit page.
local SW, SH = 640, 360
local cur = 1                          -- current page
local menu = false                     -- the exit page
local now, fno = 0, 0
local ENC = { { K.Q, K.A }, { K.W, K.S }, { K.E, K.D }, { K.R, K.F } }
local FKEYS = { K.F1, K.F2, K.F3, K.F4, K.F5, K.F6, K.F7, K.F8 }
local TRKEYS = { K.D1, K.D2, K.D3, K.D4 }
local rep = { 0, 0, 0, 0 }             -- frames each encoder key is held (0 = up)
local REP_FIRST, REP_EVERY = 24, 3     -- auto-repeat: wait 24 frames, then every 3

local function turn(p, dir)
  local st = shift() and p.fine or p.step
  p.value = clamp(floor((p.value + dir * st) / st + 0.5) * st, p.min, p.max)
end

local function encoders(sc)
  if ctrl() then return end                 -- Ctrl+S is a save, not the second encoder
  for i = 1, 4 do
    local up, dn = held[ENC[i][1]], held[ENC[i][2]]
    local dir = (up and 1 or 0) - (dn and 1 or 0)
    if dir == 0 then
      rep[i] = 0
    else
      rep[i] = rep[i] + 1
      if rep[i] == 1 or (rep[i] > REP_FIRST and (rep[i] - REP_FIRST) % REP_EVERY == 0) then
        turn(sc.params[i], dir)
        if sc.push then sc.push(sc, i) end
      end
    end
  end
end

-- the key strip below the display: two lines; the second one ends with the keys of the page
local function draw_hints()
  rectfill(0, 320, SW, 40, C.ink)
  line(0, 320, SW - 1, 320, C.dim)
  print("F1-8 MODULE  TAB PAGE  SPACE PLAY  ENTER REC  1-4 TRACK  SHIFT+N MUTE  ESC EXIT", 8, 324, C.mute)
  local x = 8
  for i, k in ipairs({ "Q/A", "W/S", "E/D", "R/F" }) do
    x = print(k, x, 342, ECOL[i]) + 8
  end
  local sc = PAGES[cur].sc
  local tail = "[ ] SOUND -/= OCT ;/' PAT P MODE ZXCV PUNCH"
  if sc == grid_page then tail = "T SET BKSP CLEAR ;/' PAT P MODE CTRL+S/N SAVE/NEW"
  elseif sc == tape_page then tail = "T WRITE BKSP CLEAR ;/' PAT P MODE CTRL+S/N SAVE/NEW" end
  print("= ENC  " .. tail, x, 342, C.mute)
end

local function draw_menu()
  rectfill(150, 110, 340, 100, C.ink)
  rect(150, 110, 340, 100, C.hot)
  print("LEAVE BM TAPE?", 170, 120, C.e4)
  print("CTRL+ESC", 170, 148, C.e3); print("FREEZE AND LEAVE", 270, 148, C.mute)
  print("ENTER", 170, 168, C.e3); print("CLOSE", 270, 168, C.mute)
  print("ESC", 170, 188, C.e3); print("STAY", 270, 188, C.mute)
end

function _init()
  screen(SW, SH)
  rawkeys(true)
  engine.init()
  FX.init()
  MIX.init()
  play_voices(0, 1, 2, 3, 4, 5)          -- the sequencer's notes; the live notes use 6 and 7
  SV.load()
end

-- the keys of one frame: page, track, transport, sounds, step edit
local function keys()
  local page = PAGES[cur]
  if pressed(K.RIGHT) then cur = cur % #PAGES + 1 end
  if pressed(K.LEFT) then cur = (cur - 2) % #PAGES + 1 end
  for i, k in ipairs(FKEYS) do
    if pressed(k) then cur = module_page(i) end
  end
  if pressed(K.TAB) then cur = tab_page(cur) end
  for n, k in ipairs(TRKEYS) do
    if pressed(k) then
      if shift() then
        toggle_mute(n)
      else
        select_track(n)
        if n == 1 and PAGES[cur].mod == 1 then cur = module_page(2) end
      end
    end
  end
  if pressed(K.SPACE) then
    if tr.run then transport_stop() else transport_start() end
  end
  if pressed(K.ENTER) then tr.rec = not tr.rec end
  if pressed(K.P) and not ctrl() then SONG.mode() end
  if pressed(K.SEMI) then SONG.pattern(-1) end
  if pressed(K.QUOTE) then SONG.pattern(1) end
  if ctrl() then
    if pressed(K.S) then SV.save() end
    if pressed(K.N) then SV.new() end
  end
  if pressed(K.MINUS) then octave(-1) end
  if pressed(K.EQUALS) then octave(1) end
  if pressed(K.LB) then next_sound(-1) end
  if pressed(K.RB) then next_sound(1) end
  if pressed(K.UP) and page.sc.arrows then page.sc.arrows(1) end
  if pressed(K.DOWN) and page.sc.arrows then page.sc.arrows(-1) end
  if page.sc == grid_page then
    if pressed(K.T) then grid_toggle() end
    if pressed(K.BKSP) then
      if shift() then clear_track(S.trk) else clear_step(S.trk, S.cur) end
    end
  elseif page.sc == tape_page then
    if pressed(K.T) then SONG.write() end
    if pressed(K.BKSP) then
      if shift() then SONG.clear_row() else SONG.clear() end
    end
  end
  play_keys(true)
end

function _update()
  now = time()
  fno = fno + 1
  poll()
  if btnp("start") then menu = not menu end       -- Esc reaches the cartridge as Start
  if menu then
    if pressed(K.ENTER) then SV.save(); quit() end
    return
  end
  keys()
  local sc = PAGES[cur].sc
  if sc.pull then sc.pull(sc) end
  encoders(sc)
  PUNCH.update(audio_time())
  MIX.step(now)
  if fno % seq_skip == 0 then seq_update(audio_time()) end
  if test_tick then test_tick() end
end

function _draw()
  cls(C.ink)
  local page = PAGES[cur]
  local sc = page.sc
  if sc.pull then sc.pull(sc) end
  local v = { sc.params[1].value, sc.params[2].value, sc.params[3].value, sc.params[4].value }
  clipu(0, 14, 320, BOT + 2 - 14)
  sc.draw(now, v)
  clip()
  frame(page, now)
  draw_hints()
  if menu then draw_menu() end
end

-- Ctrl+Esc, PS or Start+Select: the song is saved, so the console may freeze the app
function _exit()
  live_release()
  transport_stop()
  SV.save()
  return true
end
