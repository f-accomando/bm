-- snesgfx_test.lua: the SNES graphics converters of bm Pixel (core/lua/snesgfx.lua), on the PC.
--   build/host/luahost tests/snes/snesgfx_test.lua [ROOT]     (make test-snes-gfx)
-- It needs plain Lua 5.4 only: lua5.4 tests/snes/snesgfx_test.lua [ROOT] works too.
local ROOT = arg[1] or "."
local G = assert(loadfile(ROOT .. "/core/lua/snesgfx.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end
local function hex(s) return (s:gsub(".", function(c) return string.format("%02X", c:byte()) end)) end
local function tile(f)
  local t = {}
  for y = 0, 7 do for x = 0, 7 do t[y * 8 + x + 1] = f(x, y) end end
  return t
end

----------------------------------------------------------------- BGR555

check(G.bgr555(0, 0, 0) == 0, "black")
check(G.bgr555(255, 255, 255) == 0x7FFF, "white")
check(G.bgr555(255, 0, 0) == 0x001F, "red is bits 0-4")
check(G.bgr555(0, 255, 0) == 0x03E0, "green is bits 5-9")
check(G.bgr555(0, 0, 255) == 0x7C00, "blue is bits 10-14")
check(G.bgr555(7, 7, 7) == 0, "the 3 low bits are lost")
check(G.bgr555(8, 8, 8) == 0x0421, "8 is 1")
local r, g, b = G.rgb8(0x7FFF)
check(r == 255 and g == 255 and b == 255, "31 expands to 255")
r, g, b = G.rgb8(0x0001)
check(r == 8 and g == 0 and b == 0, "1 expands to 8")
for v = 0, 0x7FFF, 37 do
  local r2, g2, b2 = G.rgb8(v)
  check(G.bgr555(r2, g2, b2) == v, "round trip " .. v)
end
check(G.from_rgb(0xFF8000) == (31 | (16 << 5)), "from_rgb orange")
check(G.snap_rgb(0xFF8000) == 0xFF8400, "snap_rgb 0x80 gives 0x84")
check(hex(G.pal_bytes({ 0x7FFF, 0x001F, 0x7C00 })) == "FF7F1F00007C", "pal bytes, low byte first")
local w = G.pal_words(G.pal_bytes({ 0x7FFF, 0x001F, 0x7C00, 0x1234 }))
check(#w == 4 and w[1] == 0x7FFF and w[3] == 0x7C00 and w[4] == 0x1234, "pal words")

----------------------------------------------------------------- planar tiles (known bytes)

-- 2bpp: the left pixel is bit 7
local one = tile(function(x, y) return (x == 0 and y == 0) and 1 or 0 end)
check(hex(G.encode_tile(one, 2)) == "80000000000000000000000000000000", "2bpp pixel (0,0) = 1")
local two = tile(function(x, y) return (x == 7 and y == 7) and 2 or 0 end)
check(hex(G.encode_tile(two, 2)) == "00000000000000000000000000000001", "2bpp pixel (7,7) = 2 is plane 1, bit 0")
local t2 = tile(function(x, y) return (x + y) % 4 end)
-- row 0: 0 1 2 3 0 1 2 3 -> plane 0 = 01010101 (55), plane 1 = 00110011 (33)
-- row 1: 1 2 3 0 1 2 3 0 -> plane 0 = 1,0,1,0,1,0,1,0 (AA), plane 1 = 0,1,1,0,0,1,1,0 (66)
check(hex(G.encode_tile(t2, 2)):sub(1, 8) == "5533AA66", "2bpp rows 0 and 1")

-- 4bpp: planes 0/1 for the 8 rows, then planes 2/3
local t4 = tile(function(x, y)
  if x == 7 and y == 0 then return 15 elseif x == 0 and y == 7 then return 8 end
  return 0
end)
local e4 = G.encode_tile(t4, 4)
check(#e4 == 32, "4bpp is 32 bytes")
check(hex(e4:sub(1, 2)) == "0101", "4bpp row 0, planes 0/1")
check(hex(e4:sub(15, 16)) == "0000", "4bpp row 7, planes 0/1")
check(hex(e4:sub(17, 18)) == "0101", "4bpp row 0, planes 2/3")
check(hex(e4:sub(31, 32)) == "0080", "4bpp row 7, planes 2/3 (colour 8 is plane 3, bit 7)")

-- 8bpp: four plane pairs, 64 bytes
local t8 = tile(function(x, y) return (x == 0 and y == 0) and 0xA5 or ((x == 7 and y == 7) and 0xFF or 0) end)
local e8 = G.encode_tile(t8, 8)
check(#e8 == 64, "8bpp is 64 bytes")
-- 0xA5 = 1010 0101: planes 0, 2, 5 and 7 are set; pixel (0,0) is bit 7 of each plane byte
check(hex(e8:sub(1, 2)) == "8000", "8bpp planes 0/1, row 0: plane 0")
check(hex(e8:sub(17, 18)) == "8000", "8bpp planes 2/3, row 0: plane 2")
check(hex(e8:sub(33, 34)) == "0080", "8bpp planes 4/5, row 0: plane 5")
check(hex(e8:sub(49, 50)) == "0080", "8bpp planes 6/7, row 0: plane 7")
check(hex(e8:sub(15, 16)) == "0101", "8bpp planes 0/1, row 7: pixel (7,7) = 0xFF, bit 0")
check(hex(e8:sub(63, 64)) == "0101", "8bpp planes 6/7, row 7")

----------------------------------------------------------------- decode and round trips

for _, bpp in ipairs({ 2, 4, 8 }) do
  local top = (1 << bpp) - 1
  local seed = 12345
  local function rnd() seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF; return seed >> 8 end
  local t = tile(function() return rnd() % (top + 1) end)
  local back = G.decode_tile(G.encode_tile(t, bpp), bpp)
  local same = true
  for i = 1, 64 do if back[i] ~= t[i] then same = false end end
  check(same, "tile round trip, bpp " .. bpp)
  check(G.tile_bytes(bpp) == #G.encode_tile(t, bpp), "tile_bytes " .. bpp)

  -- a sheet of 3 x 2 tiles through .chr bytes and back (cols = 3, then padded to 16)
  local sw, sh = 24, 16
  local px = {}
  for y = 0, sh - 1 do for x = 0, sw - 1 do px[y * sw + x] = rnd() % (top + 1) end end
  local get = function(x, y) return px[y * sw + x] end
  local chr = G.encode_sheet(get, sw, sh, bpp)
  check(#chr == 6 * bpp * 8, "sheet size, bpp " .. bpp)
  local out, ow, oh = G.decode_sheet(chr, bpp, 3)
  local ok = ow == sw and oh == sh
  for y = 0, sh - 1 do for x = 0, sw - 1 do if out[y * sw + x + 1] ~= px[y * sw + x] then ok = false end end end
  check(ok, "sheet round trip, bpp " .. bpp)
  local wide = G.encode_sheet(get, sw, sh, bpp, 16)
  check(#wide == 32 * bpp * 8, "padded to 16 tiles a row, bpp " .. bpp)
  local o2, w2 = G.decode_sheet(wide, bpp, 16)
  local ok2 = w2 == 128
  for y = 0, sh - 1 do for x = 0, sw - 1 do if o2[y * w2 + x + 1] ~= px[y * sw + x] then ok2 = false end end end
  check(ok2, "padded sheet round trip, bpp " .. bpp)
  check(wide:sub(3 * bpp * 8 + 1, 4 * bpp * 8) == string.rep("\0", bpp * 8), "empty tile in the padding, bpp " .. bpp)
end

----------------------------------------------------------------- sprite sizes, OAM, tile numbers

local sw0, sh0 = G.sprite_dims(0, false)
check(sw0 == 8 and sh0 == 8, "set 0 small 8x8")
local lw, lh = G.sprite_dims(0, true)
check(lw == 16 and lh == 16, "set 0 large 16x16")
lw, lh = G.sprite_dims(5, true)
check(lw == 64 and lh == 64, "set 5 large 64x64")
lw, lh = G.sprite_dims(3, false)
check(lw == 16 and lh == 16, "set 3 small 16x16")
lw, lh = G.sprite_dims(6, true)
check(lw == 32 and lh == 64, "set 6 large 32x64")
check(#G.SIZE_SETS == 7, "8 size sets (0..7)")

local e, x9 = G.oam_entry(300, 20, 0x1A5, 5, 2, true, false)
check(hex(e) == "2C14A5" .. "6B", "oam entry bytes: x 0x2C, y 0x14, tile 0xA5, attr")
check(x9 == 1, "x bit 9")
-- attr: name 1 | pal 5 << 1 = 0x0A | prio 2 << 4 = 0x20 | flip x 0x40 = 0x6B
check(e:byte(4) == 0x6B, "attr 0x6B")
e = G.oam_entry(10, 20, 1, 7, 3, false, true)
check(e:byte(4) == (0x0E | 0x30 | 0x80), "attr with flip y")
check(G.oam_high({ { x9 = true }, { large = true }, { x9 = true, large = true } }) == (1 | (2 << 2) | (3 << 4)), "oam high table")

local rows = G.sprite_tiles(0x12, 2, 2, false, false)
check(rows[1][1] == 0x12 and rows[1][2] == 0x13 and rows[2][1] == 0x22 and rows[2][2] == 0x23, "sprite tiles 16x16 at 0x12")
rows = G.sprite_tiles(0x12, 2, 2, true, true)
check(rows[1][1] == 0x23 and rows[1][2] == 0x22 and rows[2][1] == 0x13, "flipped sprite tiles")
rows = G.sprite_tiles(0x1F, 2, 1, false, false)
check(rows[1][2] == 0x10, "the tile column wraps inside the row of 16")

----------------------------------------------------------------- sub-palette match

local pals = { { 0, 0x001F, 0x03E0 }, { 0, 0x7C00, 0x7FFF } }
local i, miss = G.match_palette({ 0x001F, false, 0x03E0 }, pals)
check(i == 1 and miss == 0, "tile uses sub-palette 1")
i, miss = G.match_palette({ 0x7C00, 0x7FFF }, pals)
check(i == 2 and miss == 0, "tile uses sub-palette 2")
i, miss = G.match_palette({ 0x001F, 0x7C00, 0x7C00 }, pals)
check(miss == 1 or miss == 2, "a tile that fits none reports the missing pixels")

----------------------------------------------------------------- quantiser

-- few colours: kept as they are (cut to 5 bits), index 0 transparent
local pix = { 0xFF0000, 0x00FF00, false, 0xFF0000, 0x0000FF }
local pal, idx = G.quantize(pix, 3)
check(#pal == 4 and pal[1] == 0, "3 colours + transparent")
check(idx[3] == 0 and idx[1] == idx[4] and idx[1] ~= idx[2] and idx[2] ~= idx[5], "indices keep the colours apart")
local same = true
for k = 1, #pix do
  if pix[k] and pal[idx[k] + 1] ~= G.from_rgb(pix[k]) then same = false end
end
check(same, "exact when the picture fits")

-- many colours: a gradient of 64 reduces to 15, each pixel gets the nearest
local grad = {}
for k = 0, 63 do grad[#grad + 1] = (k * 4 << 16) | (k * 2 << 8) | (255 - k * 4) end
pal, idx = G.quantize(grad, 15)
check(#pal == 16, "15 + 1 colours, got " .. #pal)
local bad = 0
for k = 1, #grad do
  if idx[k] < 1 or idx[k] > 15 then bad = bad + 1 end
  local r1, g1, b1 = G.rgb8(pal[idx[k] + 1])
  local c = grad[k]
  local d = math.abs(r1 - (c >> 16 & 255)) + math.abs(g1 - (c >> 8 & 255)) + math.abs(b1 - (c & 255))
  if d > 100 then bad = bad + 1 end
end
check(bad == 0, "every gradient pixel is near its colour")
local pal2, idx2 = G.quantize(grad, 15)
check(table.concat(pal2, ",") == table.concat(pal, ","), "the quantiser is deterministic")
pal = G.quantize(grad, 3)
check(#pal == 4, "2bpp: 3 + 1 colours")
pal = G.quantize(grad, 255)
local distinct, seen = 0, {}
for _, c in ipairs(grad) do
  local v = G.from_rgb(c)
  if not seen[v] then seen[v] = true; distinct = distinct + 1 end
end
check(#pal == distinct + 1, "8bpp: a picture with few colours keeps them all (+1)")
check(G.nearest({ 0, 0x001F, 0x7C00 }, 0xFF0000) == 1, "nearest red")
check(G.nearest({ 0, 0x001F, 0x7C00 }, 0x0000FF) == 2, "nearest blue")
check(G.nearest({ 0, 0x001F }, false) == 0, "transparent is 0")

-- PNG to tiles and back: an RGB picture of 16x16, quantised to 15 + 1, encoded at 4bpp, decoded, painted
local picture = {}
for y = 0, 15 do for x = 0, 15 do
  if x < 4 and y < 4 then picture[y * 16 + x + 1] = false
  else picture[y * 16 + x + 1] = ((x * 16) << 16) | ((y * 16) << 8) | 0x80 end
end end
local qp, qi = G.quantize(picture, 15)
local chr4 = G.encode_sheet(function(x, y) return qi[y * 16 + x + 1] end, 16, 16, 4)
check(#chr4 == 4 * 32, "16x16 at 4bpp is 4 tiles of 32 bytes")
local dec = G.decode_sheet(chr4, 4, 2)
local ok = true
for k = 1, 256 do if dec[k] ~= qi[k] then ok = false end end
check(ok, "PNG to planar tiles and back: the indices come back")
local transparent_ok = true
for y = 0, 3 do for x = 0, 3 do if dec[y * 16 + x + 1] ~= 0 then transparent_ok = false end end end
check(transparent_ok, "transparent stays colour 0")

io.write(string.format("snesgfx: %d checks, %d failed", checks, fails) .. string.char(10))
os.exit(fails == 0 and 0 or 1)
