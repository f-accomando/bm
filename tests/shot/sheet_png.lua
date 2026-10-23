-- sheet_png (APP-19): PNG bytes with alpha of rectangles of the sprite sheet. The cart sets known
-- pixels, asks for both forms and logs each result as a hex line "png: NAME HEX" (or "png: NAME nil
-- REASON"); tests/shot/check_sheet_png.py decodes them. The colours are exact in RGB565.
local function hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

local function show(name, a, b)
  if a then log("png: " .. name .. " " .. hex(a)) else log("png: " .. name .. " nil " .. tostring(b)) end
end

local done = false
function _draw()
  if done then return end
  done = true
  cls(0)
  cart_sheet(64, 64)
  -- area A at (8,8), 4 x 2: red green blue transparent / white black opaque-red transparent
  sset(8, 8, 0xFF0000);  sset(9, 8, 0x00FF00); sset(10, 8, 0x0000FF); sset(11, 8, nil)
  sset(8, 9, 0xFFFFFF);  sset(9, 9, 0x000000); sset(10, 9, 0xFF0000); sset(11, 9, nil)
  -- area B at (20,20), 3 x 2: blue x3 then green x3
  for x = 20, 22 do sset(x, 20, 0x0000FF); sset(x, 21, 0x00FF00) end
  show("one", sheet_png(8, 8, 4, 2))
  show("list", sheet_png({ {8, 8, 2, 2}, {10, 8, 2, 2}, {20, 20, 3, 2} }))
  show("outside", sheet_png(60, 60, 10, 10))
  show("height", sheet_png({ {8, 8, 2, 2}, {20, 20, 3, 1} }))
  cart_sheet(4096, 8)
  show("toowide", sheet_png({ {0, 0, 4096, 8}, {0, 0, 1, 8} }))
  show("widest", sheet_png(0, 0, 4096, 8))
  log("png: end")
  quit()
end
