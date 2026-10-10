Hud = {}
function Hud.draw(f1, f2)
  -- Clean NeoGeo style bars
  local function bar(x, y, w, k, col)
    rectfill(x-1, y-1, w+2, 10, 0x000000)
    rectfill(x, y, w, 8, 0x202028)
    rectfill(x, y, floor(w * k), 8, col)
  end
  bar(20, 10, 140, f1.life / 1000, 0xFFD040)
  bar(20, 22, 140, f1.armor / f1.A.armor, 0x58B4FF)
  bar(W-160, 10, 140, f2.life / 1000, 0xFFD040)
  bar(W-160, 22, 140, f2.armor / f2.A.armor, 0x58B4FF)
  -- clock
  print("99", W/2 - 8, 12, 0xFFFFFF)
end
