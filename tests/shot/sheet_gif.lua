-- sheet_gif (APP-32): the animated GIF of rectangles of the sprite sheet. The cart sets known pixels,
-- asks for GIFs and logs each result as hex lines "gif: NAME HEX" (a long one in several lines, in
-- order) or "gif: NAME nil REASON"; tests/shot/check_sheet_gif.py decodes them with its own LZW decoder.
local function hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

local function show(name, a, b)
  if not a then log("gif: " .. name .. " nil " .. tostring(b)); return end
  local h = hex(a)
  for i = 1, #h, 1600 do log("gif: " .. name .. " " .. h:sub(i, i + 1599)) end
end

local done = false
function _draw()
  if done then return end
  done = true
  cls(0)
  cart_sheet(256, 256)
  -- three frames at (8,8) (16,8) (24,8), 4 x 3 each: a few colours, some transparent
  local cols = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000, 0xFFFF00 }
  for f = 0, 2 do
    for y = 0, 2 do
      for x = 0, 3 do
        local i = (x + y * 4 + f * 5) % 7           -- 6 = transparent
        sset(8 + f * 8 + x, 8 + y, cols[i + 1])
      end
    end
  end
  show("three", sheet_gif({ {8, 8, 4, 3}, {16, 8, 4, 3}, {24, 8, 4, 3} }, 7))
  show("one", sheet_gif({ {8, 8, 4, 3} }))
  -- 4 frames of 64 x 64: 200 colours, in a pattern that does not repeat (the LZW table fills and resets)
  local s = 12345
  for f = 0, 3 do
    for y = 0, 63 do
      for x = 0, 63 do
        s = (s * 1103515245 + 12345) % 2147483648
        local i = (s >> 8) % 200
        sset(f * 64 + x, 100 + y, ((i * 5) % 256) << 16 | ((i * 37) % 256) << 8 | ((i * 91) % 256))
      end
    end
  end
  show("noise", sheet_gif({ {0, 100, 64, 64}, {64, 100, 64, 64}, {128, 100, 64, 64}, {192, 100, 64, 64} }, 3))
  -- 400 distinct colours in one 20 x 20 frame, and a transparent corner: the reduction to 255
  for i = 0, 399 do
    sset(i % 20, 80 + i // 20, ((i % 20) * 12) << 16 | ((i // 20) * 12) << 8 | ((i * 7) % 256))
  end
  sset(0, 80, nil)
  show("many", sheet_gif({ {0, 80, 20, 20} }, 25))
  show("outside", sheet_gif({ {250, 250, 10, 10} }, 5))
  show("size", sheet_gif({ {8, 8, 4, 3}, {16, 8, 4, 2} }, 5))
  show("empty", sheet_gif({}, 5))
  local many = {}
  for i = 1, 65 do many[i] = { 8, 8, 4, 3 } end
  show("frames", sheet_gif(many, 5))
  show("delay", sheet_gif({ {8, 8, 4, 3} }, 0))
  cart_sheet(4096, 64)
  show("toobig", sheet_gif({ {0, 0, 4096, 64}, {0, 0, 4096, 64} }, 5))
  show("widest", sheet_gif({ {0, 0, 4096, 64} }, 5))
  log("gif: end")
  quit()
end
