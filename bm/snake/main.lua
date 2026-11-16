-- Snake: eat the apples, do not bite yourself or the walls.
-- Arrows (or the pad) turn, A starts. The snake speeds up as it grows.

local W, H = SCREEN_W, SCREEN_H
local CELL = 20
local TOP = 40                          -- score bar
local GW, GH = W // CELL, (H - TOP) // CELL

local BG, GRID, WALL = 0x0C1A10, 0x122818, 0x2E5E3A
local HEAD, BODY, APPLE, TEXT, ACCENT = 0xB8F070, 0x6CC04A, 0xE84040, 0xF0F0F0, 0xFFD050

local state = "splash"                  -- "splash", "title", "play", "over"
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

---------------------------------------------------------------- splash
-- The bm intro, the game's own first screen (the system draws no splash): the logo
-- falls, lands with a jingle, a ring of dots turns. Same animation as core/src/bm/loading.c,
-- on a 160x90 canvas made bigger. Any button skips it after the logo has landed.

local LOGO_PAL = {
  a = 0x105D9C, b = 0x6BBAFF, c = 0x1896FF, d = 0x84B2FF, e = 0x29559C, f = 0x394D9C, g = 0x52459C, h = 0x63389C, i = 0x428AFF, j = 0x9CAAFF, k = 0x6379FF, l = 0x846DFF, m = 0xADA2FF, n = 0xA55DFF, o = 0xC696FF
}
local LOGO = {
  ".abba.......................................",
  "acccc.......................................",
  "cccccb......................................",
  "cccccc......................................",
  "cccccc......................................",
  "cccccc......................................",
  "cccccc.abdddde..........fffg......ggh.......",
  "ccccccacciiiiid......fjjkkklmm..mmllnooh....",
  "ccccccaaciiiiiide...fkkkkkklllmmllllnnnno...",
  "ccccccaaciiiiiiiie.fkkkkkkklllllllllnnnnno..",
  "ccccccaaciiiiiiiiifffkkkkkklllllllllnnnnnno.",
  "ccccccacciiiiiiiiikffkkkkkklllllllllnnnnnnn.",
  "ccccccaa....eiiiiikfffkkffkllllllllghnnnnnnh",
  "cccccca......eiiiikfffkf...lllllll....nnnnnn",
  "cccccc........iiiikkfff....glllll.....nnnnnn",
  "cccccc........eiiikkfff....glllll.....nnnnnn",
  "cccccc........eiiikkfff....glllll.....nnnnnn",
  "cccccc........iiiikffff....glllll.....nnnnnn",
  "acccccb......eiiiikfff.....glllll.....nnnnnn",
  ".ccccccb....eiiiiikfff.....glllll.....nnnnnn",
  ".accccccbdddiiiiiiffff.....glllll.....nnnnnn",
  "..ccccccciiiiiiiiiffff.....glllll.....nnnnnn",
  "..acccccciiiiiiiieffff.....glllll.....nnnnnn",
  "...accccciiiiiiieeffff......lllll.....hnnnnn",
  ".....accciiiiie..effff......glllg......nnnn.",
  ".......aaiiee.....ff.........gg.........hh..",
}

local SPLASH_BG, LAND, BOUNCE, SPLASH_END = 0x000810, 0.5, 0.15, 1.8
local CW, CH, LOGO_Y = 160, 90, 22
local logo_runs                         -- {y, x, len, rgb}: one rectfill per run of one colour
local splash_t0, splash_note = nil, 1
local S_JINGLE = {                      -- {at, tone, hz, ms, vol, voice}: voices 5-7, free of the game's
  { 0.00, "kick", 150, 140, 50, 5 }, { 0.00, "harp", 523, 420, 70, 6 }, { 0.09, "harp", 659, 420, 70, 7 },
  { 0.18, "harp", 784, 420, 70, 6 }, { 0.27, "epiano", 1047, 900, 75, 7 }, { 0.27, "warm", 262, 900, 70, 5 },
}

local function lighter(c)               -- half way to white
  local r, g, b = (c >> 16) & 255, (c >> 8) & 255, c & 255
  return ((r + 255) // 2) << 16 | ((g + 255) // 2) << 8 | (b + 255) // 2
end

local function build_runs()
  logo_runs = {}
  for y, row in ipairs(LOGO) do
    local x = 1
    while x <= #row do
      local ch = row:sub(x, x)
      local e = x
      while e < #row and row:sub(e + 1, e + 1) == ch do e = e + 1 end
      if ch ~= "." then logo_runs[#logo_runs + 1] = { y - 1, x - 1, e - x + 1, LOGO_PAL[ch] } end
      x = e + 1
    end
  end
end

local function splash_sound(t)
  while splash_note <= #S_JINGLE and t >= LAND + S_JINGLE[splash_note][1] do
    local n = S_JINGLE[splash_note]
    tone(n[6], n[2])
    note(n[6], n[3], n[4], nil, n[5])
    splash_note = splash_note + 1
  end
end

local function splash_done()
  for v = 5, 7 do tone(v) end           -- the voices as a game finds them
  state = "title"
end

local function splash_update()
  if not logo_runs then build_runs() end
  splash_t0 = splash_t0 or time()
  local t = time() - splash_t0
  splash_sound(t)
  local skip = t > LAND + BOUNCE and (btnp(4) or btnp(5) or btnp(8))
  if t >= SPLASH_END or skip then splash_done() end
end

local function splash_draw()
  if not logo_runs then build_runs() end
  splash_t0 = splash_t0 or time()
  local t = time() - splash_t0
  cls(SPLASH_BG)
  local s = math.max(1, math.min(W // CW, H // CH))
  local ox, oy = (W - CW * s) // 2, (H - CH * s) // 2
  local lw, lh = #LOGO[1], #LOGO
  local x0 = (CW - lw) // 2
  local y0
  if t < LAND then
    local k = t / LAND                  -- falling faster and faster
    y0 = math.floor(-lh + (LOGO_Y + lh) * k * k)
  elseif t < LAND + BOUNCE then
    y0 = LOGO_Y - math.floor(4 * math.sin(math.pi * (t - LAND) / BOUNCE))
  else
    y0 = LOGO_Y
  end
  for pass = 0, 1 do                    -- its shadow, then the logo
    local d = pass == 0 and 2 or 0
    for _, r in ipairs(logo_runs) do
      rectfill(ox + (x0 + r[2] + d) * s, oy + (y0 + r[1] + d) * s, r[3] * s, s, pass == 0 and 0x000000 or r[4])
    end
  end
  if t > LAND + 0.12 and t < LAND + 0.52 then     -- a shine across the letters after the landing
    local shine = math.floor((t - LAND - 0.12) * 200) - 10
    for y = 0, lh - 1 do
      local row = LOGO[y + 1]
      for k = 0, 3 do
        local x = y + shine + k
        if x >= 0 and x < lw then
          local ch = row:sub(x + 1, x + 1)
          if ch ~= "." then rectfill(ox + (x0 + x) * s, oy + (y0 + y) * s, s, s, lighter(LOGO_PAL[ch])) end
        end
      end
    end
  end
  if t > LAND + 0.3 then                -- a circle of dots turning
    local dx = { 0, 5, 7, 5, 0, -5, -7, -5 }
    local dy = { -7, -5, 0, 5, 7, 5, 0, -5 }
    local cx, cy = CW // 2, LOGO_Y + lh + 4 + 7 + 2
    local head = math.floor(t * 1000 / 85) % 8
    local shade = { 0xFFFFFF, 0xA5A5FF, 0x6B83FF, 0x4A5EAA, 0x252525 }
    for i = 0, 7 do
      local behind = (head - i) % 8
      rectfill(ox + (cx + dx[i + 1] - 1) * s, oy + (cy + dy[i + 1] - 1) * s, 2 * s, 2 * s, shade[math.min(behind, 4) + 1])
    end
  end
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
  if state == "splash" then return splash_update() end
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
  if state == "splash" then return splash_draw() end
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

---------------------------------------------------------------- pause

-- START while playing: the game stops, with RESUME, VOLUME (left/right;
-- the console keeps it for every game) and QUIT to the title.
local paused, psel = false, 1
local PAUSE_ROWS = { "RESUME", "VOLUME", "QUIT" }
local PLAYING = { play = true }
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
    note(1, 660, 60, SQUARE, 110)               -- how loud it is now
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
  rectfill(x - 2, y - 2, bw + 4, bh + 4, 0xFFD050)
  rectfill(x, y, bw, bh, 0x101418)
  print("PAUSED", x + (bw - 48 * s) // 2, y + 4 * s, 0xFFD050, s)
  for i, r in ipairs(PAUSE_ROWS) do
    local ry = y + (14 + i * 14) * s
    local sel = i == psel
    if sel then rectfill(x + 4 * s, ry - s, bw - 8 * s, 10 * s, 0x2A3442) end
    print((sel and "> " or "  ") .. r, x + 6 * s, ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
    if r == "VOLUME" then
      print(tostring(volume()), x + (bw - 18 * s), ry - (s == 2 and 0 or 3), sel and 0xFFFFFF or 0x9098A8)
      for k = 1, 10 do
        rectfill(x + bw + (k * 3 - 54) * s, ry + (6 - math.min(k, 8) // 2) * s, 2 * s, (math.min(k, 8) // 2 + 2) * s,
                 k <= volume() and 0xFFD050 or 0x303844)
      end
    end
  end
end
