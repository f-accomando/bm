-- Pong: you (left) against the console (right). First to 7 wins.
-- Up/down move, A serves. Esc or Start+Select goes back to the menu.

local W, H = SCREEN_W, SCREEN_H
local TOP = 24                          -- score bar
local PW, PH = 10, 64                   -- paddle size
local BALL = 10
local WIN = 7

local BG, FG, DIM, ACCENT = 0x101820, 0xF0F0F0, 0x405060, 0xFFC040

local state                             -- "title", "serve", "play", "over"
local you, cpu, ball
local score, winner, serve_dir, flash
local record = { wins = 0, losses = 0 }  -- kept on the SD card with save()

-- sound: effects on voices 0-2, short tunes on voice 3 ({hz, frames}, 0 = rest)
local tune, tune_i, tune_t
local function jingle(notes) tune, tune_i, tune_t = notes, 1, 0 end
local function update_jingle()
  if not tune then return end
  tune_t = tune_t - 1
  if tune_t > 0 then return end
  local n = tune[tune_i]
  if not n then tune = nil; return end
  if n[1] > 0 then note(3, n[1], n[2] * 14, TRIANGLE, 120) end
  tune_t, tune_i = n[2], tune_i + 1
end

local function reset_ball(dir)
  ball = { x = W / 2 - BALL / 2, y = (TOP + H) / 2, vx = 0, vy = 0 }
  serve_dir = dir
end

local function new_game()
  you = { x = 24, y = (TOP + H) / 2 - PH / 2 }
  cpu = { x = W - 24 - PW, y = (TOP + H) / 2 - PH / 2, speed = 3.2 }
  score = { 0, 0 }
  winner = nil
  flash = 0
  reset_ball(1)
  state = "serve"
end

function _init()
  local data = saved()
  if data then record.wins, record.losses = data.wins or 0, data.losses or 0 end
  new_game()
  state = "title"
end

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function hit(p, dir)
  -- the further from the centre of the paddle, the steeper the bounce
  local rel = ((ball.y + BALL / 2) - (p.y + PH / 2)) / (PH / 2)
  local speed = math.min(12, math.sqrt(ball.vx ^ 2 + ball.vy ^ 2) * 1.06)
  local angle = rel * 1.0
  ball.vx = dir * speed * math.cos(angle)
  ball.vy = speed * math.sin(angle)
  flash = 6
  note(0, dir > 0 and 440 or 330, 45, SQUARE, 110)
end

local function point(side)
  score[side] = score[side] + 1
  if score[side] >= WIN then
    winner = side
    state = "over"
    if side == 1 then record.wins = record.wins + 1 else record.losses = record.losses + 1 end
    save(record)
    if side == 1 then
      jingle({ { 523, 8 }, { 659, 8 }, { 784, 8 }, { 1047, 24 } })
    else
      jingle({ { 392, 12 }, { 370, 12 }, { 349, 12 }, { 262, 30 } })
    end
  else
    reset_ball(side == 1 and 1 or -1)
    state = "serve"
    if side == 1 then jingle({ { 659, 5 }, { 988, 10 } }) else jingle({ { 330, 6 }, { 247, 12 } }) end
  end
end

function _update()
  update_jingle()
  if state == "title" or state == "over" then
    if btnp(4) or btnp(5) then
      math.randomseed(stat(3))
      new_game()
    end
    return
  end

  -- you
  if btn(2) then you.y = you.y - 6 end
  if btn(3) then you.y = you.y + 6 end
  you.y = clamp(you.y, TOP, H - PH)

  -- the console follows the ball, a bit late and not too fast
  local target = ball.y + BALL / 2 - PH / 2
  if ball.vx < 0 then target = (TOP + H) / 2 - PH / 2 end
  local d = target - cpu.y
  cpu.y = clamp(cpu.y + clamp(d, -cpu.speed, cpu.speed), TOP, H - PH)

  if state == "serve" then
    ball.y = (TOP + H) / 2
    if btnp(4) or (serve_dir < 0 and stat(3) % 60 == 0) then
      local a = (math.random() - 0.5) * 0.8
      ball.vx, ball.vy = serve_dir * 6 * math.cos(a), 6 * math.sin(a)
      state = "play"
      note(0, 880, 30, SQUARE, 90)
    end
    return
  end

  ball.x = ball.x + ball.vx
  ball.y = ball.y + ball.vy
  if ball.y < TOP then ball.y = TOP; ball.vy = -ball.vy; note(1, 220, 30, SQUARE, 80) end
  if ball.y > H - BALL then ball.y = H - BALL; ball.vy = -ball.vy; note(1, 220, 30, SQUARE, 80) end

  if ball.vx < 0 and ball.x <= you.x + PW and ball.x + BALL >= you.x and
     ball.y + BALL >= you.y and ball.y <= you.y + PH then
    ball.x = you.x + PW
    hit(you, 1)
  elseif ball.vx > 0 and ball.x + BALL >= cpu.x and ball.x <= cpu.x + PW and
     ball.y + BALL >= cpu.y and ball.y <= cpu.y + PH then
    ball.x = cpu.x - BALL
    hit(cpu, -1)
    cpu.speed = math.min(6, cpu.speed + 0.1)
  end

  if ball.x < -BALL then point(2) end
  if ball.x > W then point(1) end
  if flash > 0 then flash = flash - 1 end
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
  print("YOU " .. score[1], 16, 4, FG)
  local s = "CPU " .. score[2]
  print(s, W - 16 - #s * 8, 4, FG)
  center("PONG", 4, ACCENT, 0x000000)

  rectfill(math.floor(you.x), math.floor(you.y), PW, PH, FG)
  rectfill(math.floor(cpu.x), math.floor(cpu.y), PW, PH, FG)
  if state ~= "title" then
    rectfill(math.floor(ball.x), math.floor(ball.y), BALL, BALL, flash > 0 and ACCENT or FG)
  end

  if state == "title" then
    center("P O N G", 112, ACCENT)
    center("up / down to move", 160, FG)
    center("first to " .. WIN .. " points wins", 192, FG)
    if record.wins + record.losses > 0 then
      center(string.format("you %d - %d console", record.wins, record.losses), 288, DIM)
    end
    center("press A to start", 240, ACCENT)
  elseif state == "serve" and serve_dir > 0 then
    center("press A to serve", H - 40, DIM)
  elseif state == "over" then
    center(winner == 1 and "YOU WIN!" or "THE CONSOLE WINS", 140, ACCENT)
    center("press A to play again", 200, FG)
  end
end
