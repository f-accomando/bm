-- Pong: you (left) against the console (right), or two players with two
-- controllers (player 1 left, player 2 right). First to 7 wins.
-- Up/down move, A serves. Esc or Start+Select goes back to the menu.

local lib = require "bmlib"
local W, H = SCREEN_W, SCREEN_H
local TOP = 24                          -- score bar
local PW, PH = 10, 64                   -- paddle size
local BALL = 10
local WIN = 7

local BG, FG, DIM, ACCENT = 0x101820, 0xF0F0F0, 0x405060, 0xFFC040

local state                             -- "title", "serve", "play", "over"
local world, you, cpu, ball          -- the entities (lib.world): the two paddles and the ball
local two = false                       -- two players: the right paddle is player 2
local choice = 1                        -- title menu: 1 or 2 players
local score, winner, serve_dir, flash
local record = { wins = 0, losses = 0 }  -- kept on the SD card with save()

-- sound: effects on voices 0-2, short tunes on voice 3 ({hz, frames}, 0 = rest)
local snd = lib.sounds({
  menu = { note = { 1, 660, 30, SQUARE, 70 } },
  volume = { note = { 1, 660, 60, SQUARE, 110 } },
  wall = { note = { 1, 220, 30, SQUARE, 80 } },
  hit_you = { note = { 0, 440, 45, SQUARE, 110 } },
  hit_cpu = { note = { 0, 330, 45, SQUARE, 110 } },
  serve = { note = { 0, 880, 30, SQUARE, 90 } },
  win = { tune = { { 523, 8 }, { 659, 8 }, { 784, 8 }, { 1047, 24 } } },
  lose = { tune = { { 392, 12 }, { 370, 12 }, { 349, 12 }, { 262, 30 } } },
  point_you = { tune = { { 659, 5 }, { 988, 10 } } },
  point_cpu = { tune = { { 330, 6 }, { 247, 12 } } },
})

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function hit(p, dir)
  -- the further from the centre of the paddle, the steeper the bounce
  local rel = ((ball.y + BALL / 2) - (p.y + PH / 2)) / (PH / 2)
  local speed = math.min(12, math.sqrt(ball.vx ^ 2 + ball.vy ^ 2) * 1.06)
  local angle = rel * 1.0
  ball.vx = dir * speed * math.cos(angle)
  ball.vy = speed * math.sin(angle)
  flash = 6
  snd:play(dir > 0 and "hit_you" or "hit_cpu")
end

local function point(side)
  score[side] = score[side] + 1
  if score[side] >= WIN then
    winner = side
    state = "over"
    if not two then
      if side == 1 then record.wins = record.wins + 1 else record.losses = record.losses + 1 end
      save(record)
    end
    snd:play((side == 1 or two) and "win" or "lose")
  else
    ball.x, ball.y, ball.vx, ball.vy = W / 2 - BALL / 2, (TOP + H) / 2, 0, 0
    serve_dir = side == 1 and 1 or -1
    state = "serve"
    snd:play(side == 1 and "point_you" or "point_cpu")
  end
end

-- what each thing does in a frame; world:update() moves the ball by vx, vy first
local kinds = {
  paddle = { w = PW, h = PH,
    draw = function(e) rectfill(math.floor(e.x), math.floor(e.y), PW, PH, FG) end },
  ball = { w = BALL, h = BALL,
    draw = function(e)
      if state ~= "title" then
        rectfill(math.floor(e.x), math.floor(e.y), BALL, BALL, flash > 0 and ACCENT or FG)
      end
    end },
}

-- you (player 1 in a two-player game, any controller against the console)
kinds.you = setmetatable({ update = function(e)
  local p1 = two and 1 or nil
  if btn(2, p1) then e.y = e.y - 6 end
  if btn(3, p1) then e.y = e.y + 6 end
  e.y = clamp(e.y, TOP, H - PH)
end }, { __index = kinds.paddle })

kinds.cpu = setmetatable({ update = function(e)
  if two then
    -- player 2
    if btn(2, 2) then e.y = e.y - 6 end
    if btn(3, 2) then e.y = e.y + 6 end
    e.y = clamp(e.y, TOP, H - PH)
  else
    -- the console follows the ball, a bit late and not too fast
    local target = ball.y + BALL / 2 - PH / 2
    if ball.vx < 0 then target = (TOP + H) / 2 - PH / 2 end
    local d = target - e.y
    e.y = clamp(e.y + clamp(d, -e.speed, e.speed), TOP, H - PH)
  end
end }, { __index = kinds.paddle })

kinds.ball.update = function(e)
  local p1 = two and 1 or nil
  if state == "serve" then
    e.y = (TOP + H) / 2
    local server = serve_dir > 0 and p1 or 2
    if (serve_dir > 0 or two) and btnp(4, server) or (not two and serve_dir < 0 and stat(3) % 60 == 0) then
      local a = (math.random() - 0.5) * 0.8
      e.vx, e.vy = serve_dir * 6 * math.cos(a), 6 * math.sin(a)
      state = "play"
      snd:play("serve")
    end
    return
  end

  if e.y < TOP then e.y = TOP; e.vy = -e.vy; snd:play("wall") end
  if e.y > H - BALL then e.y = H - BALL; e.vy = -e.vy; snd:play("wall") end

  if e.vx < 0 and e.x <= you.x + PW and e.x + BALL >= you.x and
     e.y + BALL >= you.y and e.y <= you.y + PH then
    e.x = you.x + PW
    hit(you, 1)
  elseif e.vx > 0 and e.x + BALL >= cpu.x and e.x <= cpu.x + PW and
     e.y + BALL >= cpu.y and e.y <= cpu.y + PH then
    e.x = cpu.x - BALL
    hit(cpu, -1)
    if not two then cpu.speed = math.min(6, cpu.speed + 0.1) end
  end

  if e.x < -BALL then point(2) end
  if e.x > W then point(1) end
  if flash > 0 then flash = flash - 1 end
end

local function new_game()
  world = lib.world(kinds)
  you = world:spawn("you", { x = 24, y = (TOP + H) / 2 - PH / 2 })
  cpu = world:spawn("cpu", { x = W - 24 - PW, y = (TOP + H) / 2 - PH / 2, speed = 3.2 })
  ball = world:spawn("ball", { x = W / 2 - BALL / 2, y = (TOP + H) / 2 })
  serve_dir = 1
  score = { 0, 0 }
  winner = nil
  flash = 0
  state = "serve"
end

function _init()
  local data = saved()
  if data then record.wins, record.losses = data.wins or 0, data.losses or 0 end
  new_game()
  state = "title"
  if players() >= 2 then choice = 2 end
end

function _update()
  snd:update()
  if state == "title" then
    if btnp(2) or btnp(3) then choice = 3 - choice; snd:play("menu") end
  end
  if state == "title" or state == "over" then
    if btnp(4) or btnp(5) then
      math.randomseed(stat(3))
      if state == "title" then two = choice == 2 end
      new_game()
    end
    return
  end

  world:update()
end

local function center(s, y, c, bg)          -- on a dark box, over the net
  local x = (W - #s * 8) // 16 * 8     -- on the 8 px text grid
  rectfill(x - 8, y - 4, #s * 8 + 16, 24, bg or BG)
  print(s, x, y, c)
end

function _draw()
  cls(BG)
  -- field
  rectfill(0, 0, W, TOP, 0x000000)
  for y = TOP + 4, H, 24 do rectfill(W // 2 - 2, y, 4, 12, DIM) end
  print((two and "P1 " or "YOU ") .. score[1], 16, 4, FG)
  local s = (two and "P2 " or "CPU ") .. score[2]
  print(s, W - 16 - #s * 8, 4, FG)
  center("PONG", 4, ACCENT, 0x000000)

  world:draw()

  if state == "title" then
    center("P O N G", 96, ACCENT)
    center((choice == 1 and "> " or "  ") .. "1 PLAYER  vs console", 144, choice == 1 and ACCENT or FG)
    center((choice == 2 and "> " or "  ") .. "2 PLAYERS          ", 168, choice == 2 and ACCENT or FG)
    center("up / down to choose, first to " .. WIN .. " wins", 208, DIM)
    if record.wins + record.losses > 0 then
      center(string.format("you %d - %d console", record.wins, record.losses), 288, DIM)
    end
    local n = players()
    center(n >= 2 and (n .. " controllers connected") or "2 players: pair a second pad (monitor T)", 312, DIM)
    center("press A to start", 240, ACCENT)
  elseif state == "serve" and (serve_dir > 0 or two) then
    center(two and ("player " .. (serve_dir > 0 and 1 or 2) .. ": A to serve") or "press A to serve",
           H - 40, DIM)
  elseif state == "over" then
    local msg = two and ("PLAYER " .. winner .. " WINS!") or (winner == 1 and "YOU WIN!" or "THE CONSOLE WINS")
    center(msg, 140, ACCENT)
    center("press A to play again", 200, FG)
  end
end

---------------------------------------------------------------- pause

-- START while playing: the game stops, with RESUME, VOLUME (left/right;
-- the console keeps it for every game) and QUIT to the title.
local paused, psel = false, 1
local PAUSE_ROWS = { "RESUME", "VOLUME", "QUIT" }
local PLAYING = { serve = true, play = true }
local game_update, game_draw = _update, _draw

function _update()
  if not paused then
    if PLAYING[state] and btnp(8) then paused, psel = true, 1 return end
    return game_update()
  end
  if btnp(8) or btnp(5) then paused = false return end
  if btnp(2) then psel = (psel + #PAUSE_ROWS - 2) % #PAUSE_ROWS + 1 end
  if btnp(3) then psel = psel % #PAUSE_ROWS + 1 end
  local row = PAUSE_ROWS[psel]
  if row == "VOLUME" and (btnp(0) or btnp(1) or btnp(4)) then
    local v = volume() + (btnp(0) and -1 or 1)
    if btnp(4) and v > 10 then v = 0 end
    volume(v)
    snd:play("volume")                           -- how loud it is now
  elseif btnp(4) then
    paused = false
    if row == "QUIT" then state = "title" end
  end
end

function _draw()
  game_draw()
  if not paused then return end
  local s = W >= 640 and 2 or 1
  local bw, bh = s == 2 and 208 or 140, 76 * s
  local x, y = (W - bw) // 2, (H - bh) // 2
  rectfill(x - 2, y - 2, bw + 4, bh + 4, 0xFFC040)
  rectfill(x, y, bw, bh, 0x101418)
  print("PAUSED", x + (bw - 48 * s) // 2, y + 4 * s, 0xFFC040, s)
  for i, r in ipairs(PAUSE_ROWS) do
    local ry = y + (14 + i * 14) * s
    local sel = i == psel
    if sel then rectfill(x + 4 * s, ry - s, bw - 8 * s, 10 * s, 0x2A3442) end
    print((sel and "> " or "  ") .. r, x + 6 * s, ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
    if r == "VOLUME" then
      print(tostring(volume()), x + (bw - 18 * s), ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
      for k = 1, 10 do
        rectfill(x + bw + (k * 3 - 54) * s, ry + (6 - math.min(k, 8) // 2) * s, 2 * s, (math.min(k, 8) // 2 + 2) * s,
                 k <= volume() and 0xFFC040 or 0x303844)
      end
    end
  end
end
