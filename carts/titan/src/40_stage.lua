-- The arena: a ruined city at dusk. The sky is bands of colour with a
-- setting sun; four layers of the sheet scroll each at its own speed
-- (parallax): the far skyline, the towers as tall as the robots, the street
-- they fight on, the rubble in front of everything. Smoke rises behind the
-- towers, far away something explodes now and then, small fires burn along
-- the street.

local SKY = { 0x1E1636, 0x2C1C42, 0x44244C, 0x6A3048, 0x94403E, 0xC05C3E, 0xE0844A }
local HAZE = 0x281F3C            -- under the far skyline
local FAR_Y, MID_Y, GROUND_Y, FG_Y = 102, 56, GROUND - 16, 320
local FAR_K, MID_K, FG_K = 0.25, 0.55, 1.3

-- the fires on the street (arena x) and the smoke columns (x on the far layer)
local FIRES = { 64, 236, 410, 588, 742, 930 }
local PLUMES = { { x = 150, y = 210 }, { x = 430, y = 200 }, { x = 690, y = 214 } }
local CLOUDS = { { 40, 52, 150 }, { 260, 76, 210 }, { 520, 40, 120 }, { 700, 96, 180 }, { 120, 118, 90 },
                 { 420, 134, 140 } }

local FIRE_N = { "fire0", "fire1", "fire2" }
local puffs, flashes, tick = {}, {}, 0

function Stage.reset()
  for i = #puffs, 1, -1 do puffs[i] = nil end
  for i = #flashes, 1, -1 do flashes[i] = nil end
  -- the smoke is already up when the fight starts
  for _ = 1, 150 do Stage.update() end
end

function Stage.update()
  tick = tick + 1
  local t = tick
  for i, pl in ipairs(PLUMES) do
    if (t + i * 5) % 11 == 0 then
      puffs[#puffs + 1] = { x = pl.x + random(-3, 3), y = pl.y, r = 4, t = 0 }
    end
  end
  local j = 0
  for i = 1, #puffs do
    local p = puffs[i]
    p.t = p.t + 1
    p.y = p.y - 0.55
    p.x = p.x + 0.12 + p.t * 0.0016           -- the wind bends the column
    p.r = p.r + 0.09
    if p.t < 190 then j = j + 1; puffs[j] = p end
  end
  for i = #puffs, j + 1, -1 do puffs[i] = nil end
  -- far away, something explodes
  if random(150) == 1 then
    flashes[#flashes + 1] = { x = random(20, 760), y = random(150, 215), t = 0 }
  end
  j = 0
  for i = 1, #flashes do
    local f = flashes[i]
    f.t = f.t + 1
    if f.t < 14 then j = j + 1; flashes[j] = f end
  end
  for i = #flashes, j + 1, -1 do flashes[i] = nil end
end

local function sky(camx)
  for i, c in ipairs(SKY) do rectfill(0, (i - 1) * 36, W, 36, c) end
  -- clouds lit from below
  local drift = G.frame * 0.05
  for _, c in ipairs(CLOUDS) do
    local x = floor((c[1] - camx * 0.06 + drift) % (W + 240)) - 120
    rectfill(x, c[2], c[3], 5, 0x2A1A40)
    rectfill(x + 10, c[2] + 5, c[3] - 24, 2, 0xB0504A)
  end
  -- the sun, low, with the stripes of the haze across it
  local sx = floor(470 - camx * 0.1)
  circfill(sx, 214, 44, 0xE8703E)
  circfill(sx, 214, 38, 0xF8A050)
  circfill(sx, 214, 30, 0xFFD27A)
  for k, y in ipairs({ 204, 214, 222, 229, 235 }) do rectfill(sx - 46, y, 92, k // 2 + 1, SKY[6]) end
  rectfill(0, 240, W, GROUND_Y - 240, HAZE)
end

-- the street and everything behind the robots
function Stage.draw_back(camx)
  camx = floor(camx)
  sky(camx)
  local fx = -floor(camx * FAR_K)
  sprite("far", fx, FAR_Y)
  for _, f in ipairs(flashes) do
    local r = f.t < 4 and f.t * 3 or max(0, 16 - f.t)
    circfill(fx + f.x, f.y, r, f.t < 6 and 0xFFF0A8 or 0xEE641A)
  end
  -- the smoke columns rise behind the towers
  local mx = -floor(camx * MID_K)
  local px = -floor(camx * 0.4)
  for _, p in ipairs(puffs) do
    local c = p.t < 20 and 0x5A3440 or (p.t < 110 and 0x2E2436 or 0x241C30)
    circfill(px + floor(p.x), floor(p.y), floor(p.r), c)
  end
  sprite("mid", mx, MID_Y)
  sprite("ground", -camx, GROUND_Y)
  -- small fires on the far sidewalk
  for i, x in ipairs(FIRES) do
    local sx = x - camx
    if sx > -20 and sx < W + 20 then
      sprite(FIRE_N[(G.frame // 5 + i) % 3 + 1], sx, GROUND - 3)
    end
  end
end

-- the rubble in front of everything
function Stage.draw_front(camx)
  sprite("fg", -floor(camx * FG_K), FG_Y)
end

-- the camera follows the middle of the two robots
function Stage.camera(a, b, snap)
  local want = clamp((a.x + b.x) / 2 - W / 2, 0, ARENA_W - W)
  if snap then G.camx = want else G.camx = G.camx + (want - G.camx) * 0.2 end
  G.camx = clamp(G.camx, 0, ARENA_W - W)
end
