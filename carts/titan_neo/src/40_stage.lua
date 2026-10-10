Stage = {}
function Stage.draw(camx)
  -- High quality static stage from processed image, with simple parallax offset if possible
  -- For now draw a rich gradient + rubble approximation, or use sspr if packed
  -- Sky
  for i = 0, 180 do
    local c = 0x1E1636 + i * 2
    rectfill(0, i, W, 1, c % 0xFFFFFF)
  end
  -- Distant buildings
  local off = -floor(camx * 0.3)
  for i = 1, 6 do
    local bx = off + i * 120
    if bx > -80 and bx < W + 80 then
      rectfill(bx, 140, 50, 140, 0x201830)
      rectfill(bx + 10, 100, 30, 40, 0x302040)
    end
  end
  -- Ground
  rectfill(0, GROUND - 10, W, H - GROUND + 10, 0x181820)
  -- Rubble details
  for i = 1, 15 do
    local rx = (i * 50 - camx) % 900 - 20
    if rx > -10 and rx < W then
      rectfill(rx, GROUND - 6, 14, 6, 0x303038)
    end
  end
end
