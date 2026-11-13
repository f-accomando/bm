-- bm Tape main: pages, keys, encoders, transport, key strip, exit page.
local cur = 1                          -- current page
local menu = false                     -- the exit page
local now, fno = 0, 0
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

local function draw_menu()
  local x, y, w, h = g(380), g(200), g(520), g(190)
  rectfill(x, y, w, h, C.ink)
  rect(x, y, w, h, C.hot)
  local s = LY.ts
  print("LEAVE BM TAPE?", x + g(30), y + g(20), C.e4, s)
  print("CTRL+ESC", x + g(30), y + g(76), C.e3, s); print("FREEZE AND LEAVE", x + g(200), y + g(76), C.mute, s)
  print("ENTER", x + g(30), y + g(116), C.e3, s); print("CLOSE", x + g(200), y + g(116), C.mute, s)
  print("ESC", x + g(30), y + g(150), C.e3, s); print("STAY", x + g(200), y + g(150), C.mute, s)
end

function _init()
  screen(1280, 720)                      -- from the next frame; a console that cannot draw 720p keeps 640 x 360 and the layout halves
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
  if pressed(K.BSLASH) then SONG.mode() end
  if pressed(K.GUIL) then SONG.pattern(-1) end
  if pressed(K.GUIR) then SONG.pattern(1) end
  if ctrl() then
    if pressed(K.S) then SV.save() end
    if pressed(K.N) then SV.new() end
  end
  if pressed(K.F) then octave(-1) end
  if pressed(K.K) then octave(1) end
  if pressed(K.ALTL) then next_sound(-1) end
  if pressed(K.ALTR) then next_sound(1) end
  if pressed(K.UP) and page.sc.arrows then page.sc.arrows(1) end
  if pressed(K.DOWN) and page.sc.arrows then page.sc.arrows(-1) end
  if page.sc == grid_page then
    if pressed(K.A) then grid_toggle() end
    if pressed(K.BKSP) then
      if shift() then clear_track(S.trk) else clear_step(S.trk, S.cur) end
    end
  elseif page.sc == tape_page then
    if pressed(K.A) then SONG.write() end
    if pressed(K.BKSP) then
      if shift() then SONG.clear_row() else SONG.clear() end
    end
  end
  play_keys(not ctrl())
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
  layout()
  cls(C.ink)
  local page = PAGES[cur]
  local sc = page.sc
  if sc.pull then sc.pull(sc) end
  local v = { sc.params[1].value, sc.params[2].value, sc.params[3].value, sc.params[4].value }
  clipu(0, 14, 320, 122)
  sc.draw(now, v)
  clip()
  draw_screen(page, now)
  draw_keys(page)
  if menu then draw_menu() end
end

-- Ctrl+Esc, PS or Start+Select: the song is saved, so the console may freeze the app
function _exit()
  live_release()
  transport_stop()
  SV.save()
  return true
end
