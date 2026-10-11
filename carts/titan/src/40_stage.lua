-- Titan Clash EX: Dynamic Multi-Layer Parallax Arena Stages
-- Adapted for 360x360 square screen (PowKiddy RGB30).
-- Stages: Neo Hong Kong Harbor (Sunset), Industrial Mecha Dock, Stratosphere Elevator.

local SKY_SUNSET = { 0x1E1636, 0x2C1C42, 0x44244C, 0x6A3048, 0x94403E, 0xC05C3E, 0xE0844A }
local SKY_DOCK   = { 0x0E141E, 0x141C28, 0x1C2638, 0x26344C, 0x364864, 0x4A6082, 0x627C9E }
local SKY_SPACE  = { 0x04060C, 0x080C18, 0x0E1428, 0x182444, 0x263C68, 0x3E629A, 0x5E8ECC }

local HAZE = 0x281F3C
local FAR_Y, MID_Y, GROUND_Y, FG_Y = 96, 52, GROUND - 16, 320
local FAR_K, MID_K, FG_K = 0.24, 0.52, 1.25

local FIRES = { 54, 210, 380, 560, 720, 890 }
local PLUMES = { { x = 140, y = 190 }, { x = 420, y = 185 }, { x = 680, y = 195 } }
local CLOUDS = { { 30, 48, 140 }, { 240, 68, 190 }, { 490, 36, 110 }, { 670, 88, 160 } }

local puffs, flashes, tick = {}, {}, 0

function Stage.reset()
  for i = #puffs, 1, -1 do puffs[i] = nil end
  for i = #flashes, 1, -1 do flashes[i] = nil end
  for _ = 1, 100 do Stage.update() end
end

function Stage.update()
  tick = tick + 1
  local t = tick
  for i, pl in ipairs(PLUMES) do
    if (t + i * 5) % 10 == 0 then
      puffs[#puffs + 1] = { x = pl.x + random(-3, 3), y = pl.y, r = 4, t = 0 }
    end
  end
  local j = 0
  for i = 1, #puffs do
    local p = puffs[i]
    p.t = p.t + 1
    p.y = p.y - 0.55
    p.x = p.x + 0.12 + p.t * 0.0016
    p.r = p.r + 0.09
    if p.t < 180 then j = j + 1; puffs[j] = p end
  end
  for i = #puffs, j + 1, -1 do puffs[i] = nil end

  if random(140) == 1 then
    flashes[#flashes + 1] = { x = random(20, 740), y = random(130, 195), t = 0 }
  end
  j = 0
  for i = 1, #flashes do
    local f = flashes[i]
    f.t = f.t + 1
    if f.t < 14 then j = j + 1; flashes[j] = f end
  end
  for i = #flashes, j + 1, -1 do flashes[i] = nil end
end

local function sky(camx, stage_id)
  local palette = stage_id == 2 and SKY_DOCK or (stage_id == 3 and SKY_SPACE or SKY_SUNSET)
  for i, c in ipairs(palette) do
    rectfill(0, (i - 1) * 32, W, 32, c)
  end

  -- Clouds
  local drift = G.frame * 0.05
  for _, c in ipairs(CLOUDS) do
    local x = floor((c[1] - camx * 0.06 + drift) % (W + 200)) - 100
    rectfill(x, c[2], c[3], 5, 0x2A1A40)
    rectfill(x + 10, c[2] + 5, c[3] - 20, 2, 0xB0504A)
  end

  -- Amber setting sun
  if stage_id == 1 then
    local sx = floor(250 - camx * 0.1)
    circfill(sx, 190, 42, 0xE8703E)
    circfill(sx, 190, 36, 0xF8A050)
    circfill(sx, 190, 28, 0xFFD27A)
    for k, y in ipairs({ 182, 190, 198, 205 }) do
      rectfill(sx - 44, y, 88, k // 2 + 1, palette[6])
    end
  end
  rectfill(0, 224, W, GROUND_Y - 224, HAZE)
end

function Stage.draw_back(camx)
  camx = floor(camx)
  sky(camx, G.stage or 1)

  local fx = -floor(camx * FAR_K)
  sprite("far", fx, FAR_Y)

  for _, f in ipairs(flashes) do
    local r = f.t < 4 and f.t * 3 or max(0, 15 - f.t)
    circfill(fx + f.x, f.y, r, f.t < 6 and 0xFFF0A8 or 0xEE641A)
  end

  local mx = -floor(camx * MID_K)
  local px = -floor(camx * 0.4)
  for _, p in ipairs(puffs) do
    local c = p.t < 20 and 0x5A3440 or (p.t < 110 and 0x2E2436 or 0x241C30)
    circfill(px + floor(p.x), floor(p.y), floor(p.r), c)
  end

  sprite("mid", mx, MID_Y)
  sprite("ground", -camx, GROUND_Y)

  -- small fires along the battle street
  local fire_n = { "fire0", "fire1", "fire2" }
  for _, x in ipairs(FIRES) do
    local sx = x - camx
    if sx > -20 and sx < W + 20 then
      sprite(fire_n[((tick + x) // 6) % 3 + 1], sx, GROUND_Y + 14)
    end
  end
end

function Stage.draw_front(camx)
  camx = floor(camx)
  local gx = -floor(camx * FG_K)
  sprite("fg", gx, FG_Y)
end
