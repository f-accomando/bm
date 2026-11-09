-- bm Tape main: scene choice, encoders, transport, key strip, exit page.
local SW, SH = 640, 360
local cur = 1                          -- current scene
local menu = false                     -- the exit page
local now, last_now = 0, 0
local ENC = { { K.Q, K.A }, { K.W, K.S }, { K.E, K.D }, { K.R, K.F } }
local FKEYS = { K.F1, K.F2, K.F3, K.F4, K.F5, K.F6, K.F7, K.F8 }
local rep = { 0, 0, 0, 0 }             -- frames each encoder key is held (0 = up)
local REP_FIRST, REP_EVERY = 24, 3     -- auto-repeat: wait 24 frames, then every 3

local function turn(p, dir)
  local st = shift() and p.fine or p.step
  p.value = clamp(floor((p.value + dir * st) / st + 0.5) * st, p.min, p.max)
end

local function encoders(sc)
  for i = 1, 4 do
    local up, dn = held[ENC[i][1]], held[ENC[i][2]]
    local dir = (up and 1 or 0) - (dn and 1 or 0)
    if dir == 0 then
      rep[i] = 0
    else
      rep[i] = rep[i] + 1
      if rep[i] == 1 or (rep[i] > REP_FIRST and (rep[i] - REP_FIRST) % REP_EVERY == 0) then
        turn(sc.params[i], dir)
      end
    end
  end
end

local function draw_hints()
  rectfill(0, 320, SW, 40, C.ink)
  line(0, 320, SW - 1, 320, C.dim)
  print("LEFT/RIGHT SCENE   F1-F8 JUMP   SPACE PLAY/STOP   SHIFT FINE   ESC EXIT", 8, 324, C.mute)
  local x = 8
  for i, k in ipairs({ "Q/A", "W/S", "E/D", "R/F" }) do
    x = print(k, x, 342, ECOL[i]) + 8
  end
  print("= ENCODERS 1-4, UP/DOWN, HOLD TO REPEAT", x, 342, C.mute)
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
  last_now = time()
end

function _update()
  now = time()
  local dt = clamp(now - last_now, 0, 0.1)
  last_now = now
  poll()
  if btnp("start") then menu = not menu end       -- Esc reaches the cartridge as Start
  if menu then
    if pressed(K.ENTER) then quit() end
    return
  end
  if pressed(K.RIGHT) then cur = cur % #scenes + 1 end
  if pressed(K.LEFT) then cur = (cur - 2) % #scenes + 1 end
  for i, k in ipairs(FKEYS) do
    if pressed(k) then cur = i end
  end
  if pressed(K.SPACE) then tr.run = not tr.run end
  if tr.run then tr.pos = tr.pos + dt end
  encoders(scenes[cur])
end

function _draw()
  cls(C.ink)
  local sc = scenes[cur]
  local v = { sc.params[1].value, sc.params[2].value, sc.params[3].value, sc.params[4].value }
  clipu(0, 14, 320, BOT + 2 - 14)
  sc.draw(now, v)
  clip()
  frame(sc, scenes, now)
  draw_hints()
  if menu then draw_menu() end
end

-- Ctrl+Esc, PS or Start+Select: nothing to save in phase 1, so the console may freeze the app
function _exit()
  return true
end
