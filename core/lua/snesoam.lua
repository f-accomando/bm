-- snesoam.lua: the sprite table (OAM) of the SNES machine, read for bm SNES SDK (the OAM view of the Sprites tab).
-- No bm API: the cart gives the bytes (snes_debug().oam, snes_mem("vram"), snes_mem("cgram")) and the tile decoder,
-- so the PC tests run this module. The rules are those of the PPU of bm (core/src/bm/snes_ppu.c, draw_obj).
local O = {}

-- the sprite sizes of OBSEL bits 7-5: { small w, small h, large w, large h } (values 6 and 7 are the rectangles)
O.SIZES = { { 8, 8, 16, 16 }, { 8, 8, 32, 32 }, { 8, 8, 64, 64 }, { 16, 16, 32, 32 }, { 16, 16, 64, 64 },
            { 32, 32, 64, 64 }, { 16, 32, 32, 64 }, { 16, 32, 32, 32 } }

-- OBSEL from the line snes_debug().regs ("... obsel 63 ..."); 0 when it is not there
function O.obsel(regs)
  local h = regs and regs:match("obsel (%x%x)")
  return h and tonumber(h, 16) or 0
end

-- the byte address in the video RAM of the two name tables of OBSEL
function O.bases(obsel)
  local b0 = (obsel & 7) << 14
  return b0, (b0 + ((((obsel >> 3) & 3) + 1) << 13)) & 0xFFFF
end

-- the top line of a sprite on the screen (it can be below 0: the sprite comes in from the top) and if it shows
local function place(e, lines)
  local top = (e.y + 1) & 255
  if top + e.h > 256 then top = top - 256 end
  e.sy = top
  e.vis = e.x + e.w > 0 and e.x < 256 and top < lines and top + e.h > 0
end

-- the 128 sprites of a table of 544 bytes: { i (0-127), x (-256 to 255), y (the byte), sy (the top line), tile (0-255),
-- name (0 or 1: the name table), pal (0-7), prio (0-3), fx, fy, big, w, h, vis (it shows on the screen) }
function O.decode(oam, obsel, lines)
  lines = lines or 224
  local sz = O.SIZES[((obsel or 0) >> 5) + 1]
  local list = {}
  for i = 0, 127 do
    local b0, b1, b2, b3 = oam:byte(i * 4 + 1, i * 4 + 4)
    b0, b1, b2, b3 = b0 or 0, b1 or 0, b2 or 0, b3 or 0
    local hi = ((oam:byte(513 + (i >> 2)) or 0) >> ((i & 3) * 2)) & 3
    local big = (hi >> 1) == 1
    local x = b0 | ((hi & 1) << 8)
    if x >= 256 then x = x - 512 end
    local e = { i = i, x = x, y = b1, tile = b2, name = b3 & 1, pal = (b3 >> 1) & 7, prio = (b3 >> 4) & 3,
                fx = (b3 >> 6) & 1 == 1, fy = (b3 >> 7) == 1, big = big,
                w = big and sz[3] or sz[1], h = big and sz[4] or sz[2] }
    place(e, lines)
    list[i + 1] = e
  end
  return list
end

-- the byte address of the 8x8 tile at (col, row) of a sprite, as the PPU finds it (the columns wrap in 16)
function O.tile_addr(e, obsel, col, row)
  local b0, b1 = O.bases(obsel)
  local t = ((e.tile & 0xF0) + (row << 4) + (((e.tile & 0x0F) + col) & 0x0F)) & 0xFF
  return ((e.name == 1 and b1 or b0) + t * 32) & 0xFFFF
end

-- the pixels of a sprite, as they are in the video RAM (not flipped): w*h indices 0-15 from 1, row after row.
-- decode is snesgfx's decode_tile(bytes, bpp, at).
function O.pixels(e, obsel, vram, decode)
  local px = {}
  for row = 0, e.h // 8 - 1 do
    for col = 0, e.w // 8 - 1 do
      local a = O.tile_addr(e, obsel, col, row)
      local t
      if a + 32 <= #vram then t = decode(vram, 4, a + 1)
      else t = decode(vram:sub(a + 1) .. vram:sub(1, 32), 4, 1) end        -- the tile wraps at the end of the video RAM
      for y = 0, 7 do
        local base = (row * 8 + y) * e.w + col * 8
        for x = 0, 7 do px[base + x + 1] = t[y * 8 + x + 1] end
      end
    end
  end
  return px
end

-- the 16 colours (BGR555 words) of sprite palette pal: colour RAM entries 128 + pal * 16
function O.colours(cgram, pal)
  local c = {}
  for k = 0, 15 do
    local at = (128 + pal * 16 + k) * 2 + 1
    local lo, hi = cgram:byte(at, at + 1)
    c[k + 1] = ((hi or 0) << 8 | (lo or 0)) & 0x7FFF
  end
  return c
end

-- the sprite that shows at the screen point (x, y): the lowest number is on top
function O.at(list, x, y)
  for _, e in ipairs(list) do
    if e.vis and x >= e.x and x < e.x + e.w and y >= e.sy and y < e.sy + e.h then return e end
  end
  return nil
end

-- the sprites that show, in OAM order
function O.visible(list)
  local v = {}
  for _, e in ipairs(list) do if e.vis then v[#v + 1] = e end end
  return v
end

-- one line of the list: number, place, tile, palette, priority, size, flips
function O.line(e)
  return string.format("%3d %4d,%4d $%d%02X p%d %d %dx%d%s%s", e.i, e.x, e.sy, e.name, e.tile, e.pal, e.prio, e.w, e.h,
                       e.fx and " X" or "", e.fy and " Y" or "")
end

return O
