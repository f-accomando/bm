-- Star shooter: waves of alien ships, three lives.
-- Arrows move, A fires (hold for auto fire), B is a short burst of speed.
-- Sprites are drawn into the sheet at start with sset(): no PNG needed.

local W, H = SCREEN_W, SCREEN_H
local TEXT, ACCENT, DIM = 0xF0F0F0, 0xFFD050, 0x506080

-- 16x16 pixel art, one character per pixel ('.' transparent)
local ART = {
  ship = {
    ".......11.......", "......1221......", "......1221......", ".....122221.....",
    ".....123321.....", "....12333321....", "...1223333221...", "..122223322221..",
    ".12222222222221.", "1222222222222221", "1221122222211221", "121..122221..121",
    "11...122221...11", ".....1.44.1.....", "......4554......", ".......55.......",
  },
  alien = {
    "....6......6....", ".....6....6.....", "....66666666....", "...6677667766...",
    "..667777777766..", ".66777877877766.", "6667777777777666", "6.667777777766.6",
    "6..6666666666..6", "...66..66..66...", "..66...66...66..", ".66....66....66.",
    "................", "................", "................", "................",
  },
  boss = {
    "......9999......", "....99aaaa99....", "...9aaaaaaaa9...", "..9aa88aa88aa9..",
    ".9aaa88aa88aaa9.", "9aaaaaaaaaaaaaa9", "9aa9aa9aa9aa9aa9", "9aaaaaaaaaaaaaa9",
    ".99a99a99a99a99.", "..9.9..99..9.9..", ".9..9..99..9..9.", "9...9..99..9...9",
    "................", "................", "................", "................",
  },
}
local PALETTE = {
  ["1"] = 0x2050A0, ["2"] = 0x70A8F0, ["3"] = 0xE0F0FF, ["4"] = 0xFF8020, ["5"] = 0xFFE060,
  ["6"] = 0x30A040, ["7"] = 0x70E060, ["8"] = 0x000000, ["9"] = 0x902050, ["a"] = 0xE04880,
}
local SPR = { ship = 0, alien = 2, boss = 4 }   -- sheet cells (256 px wide: 32 cells per row)

local function paint(name, cell)
  local ox, oy = (cell % 32) * 8, (cell // 32) * 8
  for y, row in ipairs(ART[name]) do
    for x = 1, #row do
      local c = PALETTE[row:sub(x, x)]
      sset(ox + x - 1, oy + y - 1, c)          -- nil = transparent
    end
  end
end

local state = "title"                   -- "title", "play", "over"
local player, shots, enemies, bolts, sparks, stars
local score, best, lives, wave, fire_cd, invuln, wave_timer = 0, 0, 3, 0, 0, 0, 0

function _init()
  local data = saved()                  -- the record survives power off (SD card)
  if data and data.best then best = data.best end
  for name, cell in pairs(SPR) do paint(name, cell) end
  stars = {}
  for i = 1, 120 do
    stars[i] = { x = math.random(0, W - 1), y = math.random(0, H - 1), z = math.random(1, 3) }
  end
end

local function spawn_wave()
  wave = wave + 1
  local rows, cols = math.min(2 + wave // 2, 5), 8
  for r = 1, rows do
    for c = 1, cols do
      local boss = (r == 1 and wave % 3 == 0)
      enemies[#enemies + 1] = {
        hx = 80 + (c - 1) * 60, x = 80 + (c - 1) * 60, y = -40 - r * 36, home = 40 + r * 36,
        phase = c * 0.6 + r, hp = boss and 3 or 1, boss = boss,
      }
    end
  end
  wave_timer = 90
end

local function new_game()
  math.randomseed(stat(3))
  player = { x = W / 2 - 8, y = H - 40 }
  shots, enemies, bolts, sparks = {}, {}, {}, {}
  score, lives, wave, fire_cd, invuln = 0, 3, 0, 0, 60
  spawn_wave()
  state = "play"
end

local function burst(x, y, color, n)
  for i = 1, n do
    local a, s = math.random() * 6.283, 1 + math.random() * 3
    sparks[#sparks + 1] = { x = x, y = y, vx = math.cos(a) * s, vy = math.sin(a) * s,
                            life = 20 + math.random(0, 15), c = color }
  end
end

local function overlap(ax, ay, aw, ah, bx, by, bw, bh)
  return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

local function update_stars(speed)
  for _, s in ipairs(stars) do
    s.y = s.y + s.z * speed
    if s.y >= H then s.y = s.y - H; s.x = math.random(0, W - 1) end
  end
end

function _update()
  update_stars(state == "play" and 1 or 0.4)
  if state ~= "play" then
    if btnp(4) then new_game() end
    return
  end

  -- player
  local speed = btn(5) and 6 or 4
  if btn(0) then player.x = player.x - speed end
  if btn(1) then player.x = player.x + speed end
  if btn(2) then player.y = player.y - speed end
  if btn(3) then player.y = player.y + speed end
  player.x = math.max(0, math.min(W - 16, player.x))
  player.y = math.max(H // 2, math.min(H - 16, player.y))
  fire_cd = fire_cd - 1
  if btn(4) and fire_cd <= 0 then
    shots[#shots + 1] = { x = player.x + 6, y = player.y - 6 }
    fire_cd = 8
  end
  if invuln > 0 then invuln = invuln - 1 end

  for i = #shots, 1, -1 do
    local s = shots[i]
    s.y = s.y - 9
    if s.y < -10 then table.remove(shots, i) end
  end

  -- enemies: fly in, then sway and dive now and then
  local t = time()
  for i = #enemies, 1, -1 do
    local e = enemies[i]
    -- x is always computed from the ship's home column (hx), never
    -- accumulated: ships cannot drift off screen and block the wave
    local sway = math.sin(t * 1.5 + e.phase) * 30
    if e.y < e.home and not e.diving then
      e.y = e.y + 3
      e.x = e.hx + sway
    elseif e.diving then
      e.y = e.y + 3 + wave * 0.3
      e.x = e.hx + sway + math.sin(t * 4 + e.phase) * 60
      if e.y > H + 20 then e.y = -20; e.diving = false end
    else
      e.x = e.hx + sway
      if math.random() < 0.0015 * wave then e.diving = true end
      if math.random() < 0.003 + 0.001 * wave then
        bolts[#bolts + 1] = { x = e.x + 7, y = e.y + 14 }
      end
    end
    for j = #shots, 1, -1 do
      local s = shots[j]
      if overlap(s.x, s.y, 4, 10, e.x, e.y, 16, 12) then
        table.remove(shots, j)
        e.hp = e.hp - 1
        burst(s.x, s.y, 0xFFE060, 4)
        if e.hp <= 0 then
          score = score + (e.boss and 50 or 10)
          burst(e.x + 8, e.y + 6, e.boss and 0xE04880 or 0x70E060, 18)
          table.remove(enemies, i)
        end
        break
      end
    end
  end

  for i = #bolts, 1, -1 do
    local b = bolts[i]
    b.y = b.y + 4
    if b.y > H then table.remove(bolts, i) end
  end

  -- player hit: by a bolt or a diving ship
  if invuln == 0 then
    local hit = false
    for i = #bolts, 1, -1 do
      if overlap(bolts[i].x, bolts[i].y, 3, 8, player.x + 3, player.y + 2, 10, 12) then
        table.remove(bolts, i); hit = true
      end
    end
    for _, e in ipairs(enemies) do
      if overlap(e.x, e.y, 16, 12, player.x + 3, player.y + 2, 10, 12) then hit = true end
    end
    if hit then
      lives = lives - 1
      burst(player.x + 8, player.y + 8, 0xFF8020, 30)
      invuln = 120
      if lives <= 0 then
        if score > best then
          best = score
          save({ best = best })
        end
        state = "over"
      end
    end
  end

  for i = #sparks, 1, -1 do
    local p = sparks[i]
    p.x, p.y, p.life = p.x + p.vx, p.y + p.vy, p.life - 1
    if p.life <= 0 then table.remove(sparks, i) end
  end

  if #enemies == 0 then
    wave_timer = wave_timer - 1
    if wave_timer <= 0 then spawn_wave() end
  end
end

local function center(s, y, c, bg)          -- on a dark box, readable over the background
  local x = (W - #s * 8) // 16 * 8     -- on the 8 px text grid
  rectfill(x - 8, y - 4, #s * 8 + 16, 24, bg or 0x05060C)
  print(s, x, y, c)
end

function _draw()
  cls(0x05060C)
  for _, s in ipairs(stars) do
    pset(s.x, s.y, s.z == 3 and 0xFFFFFF or s.z == 2 and 0x8090B0 or 0x404860)
  end

  if state == "title" then
    spr(SPR.ship, W // 2 - 8, 250, 2, 2)
    spr(SPR.alien, W // 2 - 60, 90, 2, 2)
    spr(SPR.boss, W // 2 - 8, 70, 2, 2)
    spr(SPR.alien, W // 2 + 44, 90, 2, 2)
    center("S T A R   S H O O T E R", 128, ACCENT)
    center("arrows move, A fires, B goes faster", 176, TEXT)
    center("press A to start", 208, ACCENT)
    if best > 0 then center("best " .. best, 300, DIM) end
    return
  end

  for _, e in ipairs(enemies) do
    e.x = math.max(0, math.min(W - 16, e.x))
    spr(e.boss and SPR.boss or SPR.alien, math.floor(e.x), math.floor(e.y), 2, 2)
  end
  for _, s in ipairs(shots) do rectfill(s.x, s.y, 4, 10, 0xFFE060) end
  for _, b in ipairs(bolts) do rectfill(b.x, b.y, 3, 8, 0xFF4060) end
  for _, p in ipairs(sparks) do pset(p.x, p.y, p.c) end
  if state == "play" and (invuln == 0 or (invuln // 6) % 2 == 0) then
    spr(SPR.ship, math.floor(player.x), math.floor(player.y), 2, 2)
  end

  print(string.format("score %d", score), 8, 4, TEXT)
  print("wave " .. wave, W // 2 - 24, 4, DIM)
  for i = 1, lives do spr(SPR.ship, W - 8 - i * 20, 2, 2, 2) end
  if #enemies == 0 and state == "play" then center("wave " .. (wave + 1), 160, ACCENT) end

  if state == "over" then
    center("GAME OVER", 150, ACCENT)
    center("score " .. score .. "   best " .. best, 180, TEXT)
    center("press A to play again", 210, TEXT)
  end
end
