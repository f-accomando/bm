-- Snake: eat the apples, do not bite yourself or the walls.
-- Arrows (or the pad) turn, A starts. The snake speeds up as it grows.

local W, H = SCREEN_W, SCREEN_H
local CELL = 20
local TOP = 40                          -- score bar
local GW, GH = W // CELL, (H - TOP) // CELL

local BG, GRID, WALL = 0x0C1A10, 0x122818, 0x2E5E3A
local HEAD, BODY, APPLE, TEXT, ACCENT = 0xB8F070, 0x6CC04A, 0xE84040, 0xF0F0F0, 0xFFD050

local state = "title"                   -- "title", "play", "over"
local snake, dir, next_dir, apple
local score, best = 0, 0
local step_frames, timer, grow

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

local function free_cell()
  while true do
    local x, y = math.random(0, GW - 1), math.random(0, GH - 1)
    local busy = false
    for _, p in ipairs(snake) do
      if p.x == x and p.y == y then busy = true; break end
    end
    if not busy then return { x = x, y = y } end
  end
end

local function new_game()
  math.randomseed(stat(3))
  snake = {}
  for i = 0, 3 do snake[#snake + 1] = { x = GW // 2 - i, y = GH // 2 } end
  dir, next_dir = { x = 1, y = 0 }, { x = 1, y = 0 }
  apple = free_cell()
  score, step_frames, timer, grow = 0, 8, 0, 0
  state = "play"
  jingle({ { 392, 5 }, { 523, 5 }, { 659, 10 } })
end

function _init()
  local data = saved()                  -- the record survives power off (SD card)
  if data and data.best then best = data.best end
  envelope(2, 0, 50, 0, 20)             -- the crash dies away
end

local function turn(x, y)
  -- no U-turn onto the neck
  if x ~= -dir.x or y ~= -dir.y then next_dir = { x = x, y = y } end
end

function _update()
  update_jingle()
  if state ~= "play" then
    if btnp(4) or btnp(5) then new_game() end
    return
  end
  if btnp(0) then turn(-1, 0) elseif btnp(1) then turn(1, 0)
  elseif btnp(2) then turn(0, -1) elseif btnp(3) then turn(0, 1) end
  if btnp(0) or btnp(1) or btnp(2) or btnp(3) then note(2, 150, 15, SQUARE, 50) end  -- tick

  timer = timer + 1
  if timer < step_frames then return end
  timer = 0
  dir = next_dir

  local head = { x = snake[1].x + dir.x, y = snake[1].y + dir.y }
  if head.x < 0 or head.y < 0 or head.x >= GW or head.y >= GH then
    state = "over"
  else
    for i = 1, #snake - (grow > 0 and 0 or 1) do
      if snake[i].x == head.x and snake[i].y == head.y then state = "over"; break end
    end
  end
  if state == "over" then
    note(2, 1200, 400, NOISE, 110)          -- crash
    jingle({ { 0, 10 }, { 330, 10 }, { 262, 10 }, { 196, 30 } })
    if score > best then
      best = score
      save({ best = best })
    end
    return
  end

  table.insert(snake, 1, head)
  if head.x == apple.x and head.y == apple.y then
    score = score + 10
    grow = grow + 3
    note(0, 660, 40, SQUARE, 100)           -- munch: two quick notes
    note(1, 990, 70, TRIANGLE, 110)
    step_frames = math.max(3, 8 - #snake // 10)
    apple = free_cell()
  end
  if grow > 0 then grow = grow - 1 else table.remove(snake) end
end

local function center(s, y, c, bg)          -- on a dark box, readable over the background
  local x = (W - #s * 8) // 16 * 8     -- on the 8 px text grid
  rectfill(x - 8, y - 4, #s * 8 + 16, 24, bg or BG)
  print(s, x, y, c)
end

local function cell(x, y, c, inset)
  rectfill(x * CELL + inset, TOP + y * CELL + inset, CELL - 2 * inset, CELL - 2 * inset, c)
end

function _draw()
  cls(BG)
  for x = 0, GW - 1, 2 do
    for y = (x // 2) % 2, GH - 1, 2 do
      rectfill(x * CELL, TOP + y * CELL, CELL, CELL, GRID)
    end
  end
  rectfill(0, 0, W, TOP, 0x000000)
  rect(0, TOP, W, H - TOP, WALL)
  print("SNAKE", 16, 12, ACCENT)
  local s = string.format("score %d   best %d", score, best)
  print(s, W - 16 - #s * 8, 12, TEXT)

  if state == "title" then
    center("S N A K E", 128, ACCENT)
    center("arrows to turn, eat the apples", 176, TEXT)
    center("press A to start", 224, ACCENT)
    return
  end

  -- apple with a small leaf
  circfill(apple.x * CELL + CELL // 2, TOP + apple.y * CELL + CELL // 2, CELL // 2 - 2, APPLE)
  rectfill(apple.x * CELL + CELL // 2, TOP + apple.y * CELL + 2, 4, 4, BODY)
  for i = #snake, 2, -1 do cell(snake[i].x, snake[i].y, BODY, 2) end
  cell(snake[1].x, snake[1].y, HEAD, 1)
  -- eyes looking where it goes
  local hx = snake[1].x * CELL + CELL // 2 + dir.x * 3
  local hy = TOP + snake[1].y * CELL + CELL // 2 + dir.y * 3
  local px, py = -dir.y * 4, dir.x * 4          -- across the direction
  rectfill(hx + px - 1, hy + py - 1, 3, 3, 0x000000)
  rectfill(hx - px - 1, hy - py - 1, 3, 3, 0x000000)

  if state == "over" then
    rectfill(W // 2 - 150, 140, 300, 90, 0x000000)
    rect(W // 2 - 150, 140, 300, 90, ACCENT)
    center("GAME OVER", 156, ACCENT, 0x000000)
    center("score " .. score, 180, TEXT, 0x000000)
    center("press A to play again", 204, TEXT, 0x000000)
  end
end
