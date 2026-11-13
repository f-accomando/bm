-- snesgfx: the SNES graphics converters of bm Pixel. No UI and no bm API: plain Lua 5.4, so the PC tests
-- (tests/snes/snesgfx_test.lua) run it as it is. bm Pixel gets it with require "snesgfx".
--
-- Words: a tile is 8x8 pixels. A pixel is an index (0 = transparent) into a sub-palette. A sub-palette has
-- 2^bpp colours (4, 16 or 256); colour 0 is transparent. A colour is BGR555: bit 0-4 red, 5-9 green,
-- 10-14 blue, stored low byte first.
--
-- The tile data is planar, as the video RAM wants it (docs/api/snes.md, snes_tiles). The left pixel is
-- bit 7. One row of a plane pair is 2 bytes (plane 0, plane 1). A 2bpp tile is 16 bytes (rows 0-7 of planes
-- 0/1). A 4bpp tile is 32 bytes (planes 0/1, then planes 2/3). An 8bpp tile is 64 bytes (planes 0/1, 2/3,
-- 4/5, then 6/7).

local G = {}

local floor, min, max = math.floor, math.min, math.max
local spack = string.pack

----------------------------------------------------------------- colours

-- 8-bit channels to the BGR555 word (the 3 low bits are lost; no rounding, as the hardware table says)
function G.bgr555(r, g, b)
  return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
end

-- the 8-bit channels of a BGR555 word: the 5 bits repeat in the low 3 (31 gives 255, 0 gives 0)
function G.rgb8(v)
  local r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
  return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)
end

-- 0xRRGGBB to BGR555 and back (a colour as the editor keeps it)
function G.from_rgb(c) return G.bgr555(c >> 16 & 255, c >> 8 & 255, c & 255) end
function G.to_rgb(v)
  local r, g, b = G.rgb8(v)
  return (r << 16) | (g << 8) | b
end

-- the colour the SNES shows for 0xRRGGBB (after the 5-bit loss): the live preview
function G.snap_rgb(c) return G.to_rgb(G.from_rgb(c)) end

-- a list of BGR555 words to the bytes of a .pal file (2 bytes each, low byte first)
function G.pal_bytes(list)
  local t = {}
  for i, v in ipairs(list) do t[i] = spack("<I2", v & 0x7FFF) end
  return table.concat(t)
end

-- the reverse: the list of words in a .pal file
function G.pal_words(bytes)
  local list = {}
  for i = 1, #bytes - 1, 2 do
    local lo, hi = bytes:byte(i, i + 1)
    list[#list + 1] = (lo | (hi << 8)) & 0x7FFF
  end
  return list
end

----------------------------------------------------------------- tiles

-- px: 64 indices, row by row. Returns the planar bytes for bpp 2, 4 or 8.
function G.encode_tile(px, bpp)
  local out = {}
  for pair = 0, bpp // 2 - 1 do
    local p0, p1 = pair * 2, pair * 2 + 1
    for y = 0, 7 do
      local a, b = 0, 0
      for x = 0, 7 do
        local v = px[y * 8 + x + 1] or 0
        a = (a << 1) | ((v >> p0) & 1)
        b = (b << 1) | ((v >> p1) & 1)
      end
      out[#out + 1] = string.char(a, b)
    end
  end
  return table.concat(out)
end

-- the bytes of one tile to 64 indices (bytes, offset 1-based)
function G.decode_tile(bytes, bpp, at)
  at = at or 1
  local px = {}
  for i = 1, 64 do px[i] = 0 end
  for pair = 0, bpp // 2 - 1 do
    local p0, p1 = pair * 2, pair * 2 + 1
    for y = 0, 7 do
      local a, b = bytes:byte(at + pair * 16 + y * 2, at + pair * 16 + y * 2 + 1)
      for x = 0, 7 do
        local bit = 7 - x
        local k = y * 8 + x + 1
        px[k] = px[k] | (((a >> bit) & 1) << p0) | (((b >> bit) & 1) << p1)
      end
    end
  end
  return px
end

function G.tile_bytes(bpp) return bpp * 8 end

-- A sheet of indices (get(x, y) gives 0..2^bpp-1; w, h in pixels, multiples of 8) to .chr bytes. The tiles
-- go row by row. cols (tiles in a row of the result, 16 for sprites: a sprite wider than 8 takes the tile
-- that is 1 right and 16 down) pads the rows with empty tiles; the default is the sheet's own width.
function G.encode_sheet(get, w, h, bpp, cols)
  local tw, th = w // 8, h // 8
  cols = cols or tw
  local out = {}
  local empty = string.rep("\0", bpp * 8)
  for ty = 0, th - 1 do
    for tx = 0, cols - 1 do
      if tx < tw then
        local px = {}
        for y = 0, 7 do
          for x = 0, 7 do px[y * 8 + x + 1] = get(tx * 8 + x, ty * 8 + y) end
        end
        out[#out + 1] = G.encode_tile(px, bpp)
      else
        out[#out + 1] = empty
      end
    end
  end
  return table.concat(out)
end

-- .chr bytes back to indices: returns a table t[y * w + x + 1] for a sheet of cols tiles in a row
function G.decode_sheet(bytes, bpp, cols)
  local size = bpp * 8
  local n = #bytes // size
  local rows = (n + cols - 1) // cols
  local w, h = cols * 8, rows * 8
  local t = {}
  for i = 1, w * h do t[i] = 0 end
  for i = 0, n - 1 do
    local px = G.decode_tile(bytes, bpp, i * size + 1)
    local tx, ty = i % cols, i // cols
    for y = 0, 7 do
      for x = 0, 7 do t[(ty * 8 + y) * w + tx * 8 + x + 1] = px[y * 8 + x + 1] end
    end
  end
  return t, w, h
end

----------------------------------------------------------------- sprite sizes and OAM

-- the 8 size selects of OBSEL (bits 5-7): {small, large} as {width, height}. Selects 6 and 7 are the
-- rectangular ones that the hardware also has.
local DIMS = {
  [0] = { { 8, 8 }, { 16, 16 } }, { { 8, 8 }, { 32, 32 } }, { { 8, 8 }, { 64, 64 } }, { { 16, 16 }, { 32, 32 } },
  { { 16, 16 }, { 64, 64 } }, { { 32, 32 }, { 64, 64 } }, { { 16, 32 }, { 32, 64 } }, { { 16, 32 }, { 32, 32 } },
}
G.SIZE_SETS = DIMS

-- the width and height of a sprite for the size select (0..7) and the size bit (false small, true large)
function G.sprite_dims(sel, large)
  local d = DIMS[sel][large and 2 or 1]
  return d[1], d[2]
end

-- the 4 bytes of an OAM entry (the low table): x low, y, tile low 8 bits, attributes.
-- attributes: bit 0 name table (tile bit 8), 1-3 palette (0-7), 4-5 priority, 6 flip X, 7 flip Y.
-- x9 (the 9th bit of x) and large live in the high table; returns them as the second and third result.
function G.oam_entry(x, y, tile, pal, prio, flipx, flipy)
  local attr = ((tile >> 8) & 1) | ((pal & 7) << 1) | ((prio & 3) << 4) | (flipx and 0x40 or 0) | (flipy and 0x80 or 0)
  return string.char(x & 255, y & 255, tile & 255, attr), (x >> 8) & 1
end

-- the 8-bit pixel bytes of the 2 high-table bits of 4 sprites (bit 0 x9, bit 1 large of each sprite)
function G.oam_high(list)
  local b = 0
  for i = 1, 4 do
    local e = list[i]
    if e then b = b | (((e.x9 and 1 or 0) | (e.large and 2 or 0)) << ((i - 1) * 2)) end
  end
  return b
end

-- the tile numbers of a sprite of tw x th tiles: the first tile, then 1 to the right, 16 down (the
-- 16-tile-wide name table). Flip X and Y turn the order, as the hardware does. Returns rows of tile numbers.
function G.sprite_tiles(first, tw, th, flipx, flipy)
  local rows = {}
  for ty = 0, th - 1 do
    local row = {}
    for tx = 0, tw - 1 do
      local sx, sy = flipx and (tw - 1 - tx) or tx, flipy and (th - 1 - ty) or ty
      local n = (first & 0xF0) + (((first & 15) + sx) & 15)
      n = n + sy * 16
      row[tx + 1] = n
    end
    rows[ty + 1] = row
  end
  return rows
end

----------------------------------------------------------------- the sheet's tiles and their sub-palettes

-- The sub-palette of a tile: the index (1-based) of the first sub-palette (list of BGR555 words, 0 first)
-- that holds all the colours of the tile (colours as BGR555 words, nil = transparent). With none, the one that
-- misses the fewest pixels. Returns the index and the number of missing pixels.
function G.match_palette(colours, pals)
  local best, miss = 1, math.huge
  for i, pal in ipairs(pals) do
    local have = {}
    for k = 1, #pal do have[pal[k]] = true end
    local m = 0
    for _, c in ipairs(colours) do
      if c and not have[c] then m = m + 1 end
    end
    if m < miss then best, miss = i, m end
    if m == 0 then break end
  end
  return best, miss
end

----------------------------------------------------------------- the quantiser

-- Colours of a picture to at most n colours (n = 2^bpp - 1) plus transparent. pix: list of 0xRRGGBB or false
-- (transparent, or alpha under 128). The colours are first cut to 5 bits a channel. A median cut makes the
-- palette; each pixel then takes the nearest colour. Returns pal (BGR555 words, pal[1] = 0 transparent, at most
-- n + 1) and idx (the index 0..n of each pixel, 0 = transparent), the same order as pix. Deterministic.
function G.quantize(pix, n)
  local count, list = {}, {}
  for i = 1, #pix do
    local c = pix[i]
    if c then
      local v = G.from_rgb(c)
      if not count[v] then count[v] = 0; list[#list + 1] = v end
      count[v] = count[v] + 1
    end
  end
  table.sort(list)                         -- (a fixed order: the same input gives the same palette)
  local pal = { 0 }
  local lookup = {}
  if #list <= n then
    for i, v in ipairs(list) do pal[i + 1] = v; lookup[v] = i end
  else
    local function ch(v, k) return (v >> (k * 5)) & 31 end
    local boxes = { list }
    while #boxes < n do
      -- the box with the widest channel range (counted by the pixels it holds)
      local bi, bscore, bk
      for i, box in ipairs(boxes) do
        if #box > 1 then
          for k = 0, 2 do
            local lo, hi = 31, 0
            for _, v in ipairs(box) do
              local c = ch(v, k)
              if c < lo then lo = c end
              if c > hi then hi = c end
            end
            local sc = hi - lo
            if not bscore or sc > bscore then bi, bscore, bk = i, sc, k end
          end
        end
      end
      if not bi or bscore == 0 then break end
      local box = boxes[bi]
      table.sort(box, function(a, b)
        local ca, cb = ch(a, bk), ch(b, bk)
        if ca ~= cb then return ca < cb end
        return a < b
      end)
      -- split where the pixel count halves
      local total = 0
      for _, v in ipairs(box) do total = total + count[v] end
      local acc, cut = 0, 1
      for i, v in ipairs(box) do
        acc = acc + count[v]
        cut = i
        if acc * 2 >= total then break end
      end
      if cut >= #box then cut = #box - 1 end
      local a, b = {}, {}
      for i, v in ipairs(box) do
        if i <= cut then a[#a + 1] = v else b[#b + 1] = v end
      end
      boxes[bi] = a
      boxes[#boxes + 1] = b
    end
    for i, box in ipairs(boxes) do
      local sr, sg, sb, total = 0, 0, 0, 0
      for _, v in ipairs(box) do
        local c = count[v]
        sr, sg, sb, total = sr + (v & 31) * c, sg + ((v >> 5) & 31) * c, sb + ((v >> 10) & 31) * c, total + c
      end
      pal[i + 1] = floor(sr / total + 0.5) | (floor(sg / total + 0.5) << 5) | (floor(sb / total + 0.5) << 10)
    end
    for _, v in ipairs(list) do
      local best, bd = 1, math.huge
      local r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
      for i = 2, #pal do
        local p = pal[i]
        local dr, dg, db = r - (p & 31), g - ((p >> 5) & 31), b - ((p >> 10) & 31)
        local d = dr * dr * 3 + dg * dg * 4 + db * db * 2
        if d < bd then best, bd = i, d end
      end
      lookup[v] = best - 1
    end
  end
  local idx = {}
  for i = 1, #pix do
    local c = pix[i]
    idx[i] = c and lookup[G.from_rgb(c)] or 0
  end
  return pal, idx
end

-- The index of the nearest colour of a sub-palette (BGR555 words; entry 1 is transparent) for 0xRRGGBB.
-- false (transparent) gives 0.
function G.nearest(pal, c)
  if not c then return 0 end
  local v = G.from_rgb(c)
  local r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
  local best, bd = 0, math.huge
  for i = 2, #pal do
    local p = pal[i]
    local dr, dg, db = r - (p & 31), g - ((p >> 5) & 31), b - ((p >> 10) & 31)
    local d = dr * dr * 3 + dg * dg * 4 + db * db * 2
    if d < bd then best, bd = i - 1, d end
  end
  return best
end

return G
