Hud = {}
function Hud.draw(f1, f2)
  local function bar(x, y, w, k, col)
    rectfill(x-1, y-1, w+2, 8, 0x000000)
    rectfill(x, y, w, 6, 0x202028)
    rectfill(x, y, floor(w * clamp(k,0,1)), 6, col)
  end
  -- multi armor bars
  for i = 1, 3 do
    local k = (i == 1 and f1.armor / f1.A.armor) or 1
    bar(20, 8 + (i-1)*10, 120, k, 0x58B4FF)
    bar(W-140, 8 + (i-1)*10, 120, k, 0x58B4FF)
  end
  -- boost
  bar(20, 40, 80, f1.boost / 100, 0x40E8A0)
  bar(W-100, 40, 80, f2.boost / 100, 0x40E8A0)
  print("BOOST", 20, 50, 0x80FFC0)
  print("99", W/2-8, 12, 0xFFFFFF)
end
