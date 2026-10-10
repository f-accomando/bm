-- Procedural ruined city stage for the .b16 version.
-- Parallax layers drawn with shapes so no large sheet is needed.

local SKY = { 0x1E1636, 0x2C1C42, 0x44244C, 0x6A3048, 0x94403E, 0xC05C3E, 0xE0844A }

function Stage.reset()
  -- nothing to preload
end

function Stage.update()
  -- ambient events can be added here
end

function Stage.draw(camx)
  -- sky gradient
  for i = 1, #SKY do
    rectfill(0, (i - 1) * 40, W, 40, SKY[i])
  end
  -- distant skyline
  local far = -floor(camx * 0.2)
  for i = 1, 8 do
    local bx = far + i * 90
    if bx > -40 and bx < W + 40 then
      rectfill(bx, 140, 30, 120, 0x201828)
      rectfill(bx + 8, 100, 14, 40, 0x302030)
    end
  end
  -- mid towers
  local mid = -floor(camx * 0.5)
  for i = 1, 5 do
    local bx = mid + i * 140
    if bx > -60 and bx < W + 60 then
      rectfill(bx, 160, 50, 140, 0x302838)
      rectfill(bx + 10, 120, 30, 40, 0x403040)
    end
  end
  -- ground
  rectfill(0, GROUND - 8, W, H - GROUND + 8, 0x181820)
  -- rubble
  local fg = -floor(camx * 1.1)
  for i = 1, 12 do
    local rx = fg + i * 60
    if rx > -20 and rx < W + 20 then
      rectfill(rx, GROUND - 6, 12, 6, 0x303038)
    end
  end
  -- small fires
  for i = 1, 4 do
    local fx = (i * 180 - camx) % (ARENA_W + 40) - 20
    if fx > 0 and fx < W then
      circfill(fx, GROUND - 4, 4, 0xFF6020)
      circfill(fx, GROUND - 8, 2, 0xFFC040)
    end
  end
end
