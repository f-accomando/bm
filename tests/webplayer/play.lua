-- Cartridge for trying the web player by hand and for its browser check: a square moves with
-- the pad or the arrows, A changes its colour, the frame counter shows that the 60 Hz clock runs.
local x, y, frame, col = 100, 100, 0, 0
local cols = { 0xFF4040, 0x40FF40, 0x4080FF, 0xFFFF40 }

function _update()
  frame = frame + 1
  if btn(0) then x = x - 2 end
  if btn(1) then x = x + 2 end
  if btn(2) then y = y - 2 end
  if btn(3) then y = y + 2 end
  if btnp(4) then col = (col + 1) % #cols end
end

function _draw()
  cls(0x102040)
  rectfill(x, y, x + 24, y + 24, cols[col + 1])
  print("frame " .. frame, 8, 8, 0xFFFFFF)
  print("x " .. x .. " y " .. y, 8, 20, 0xFFFFFF)
end
