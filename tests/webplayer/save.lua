-- Cartridge for the saves of the web player: it counts its starts with save()/saved(). The page keeps
-- the file in IndexedDB, so the number grows after a reload. Press A (Space) to save again.
local t = saved() or { runs = 0 }
t.runs = t.runs + 1
save(t)

function _update()
  if btnp(4) then
    t.runs = t.runs + 100
    save(t)
  end
end

function _draw()
  cls(0x102040)
  rectfill(8, 8, 8 + math.min(t.runs, 600) // 2, 24, 0x40FF40)
  print("runs " .. t.runs, 8, 30, 0xFFFFFF)
end
