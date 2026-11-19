-- snesbg.lua: the BG layers (tilemaps) of the SNES machine, read for bm SNES SDK (the BG view of the Map tab).
-- No bm API: the cart gives the bytes (snes_debug().regs, snes_mem("vram"), snes_mem("cgram")) and the tile decoder,
-- so the PC tests run this module. The rules are those of the PPU of bm (core/src/bm/snes_ppu.c).
local B = {}
local min = math.min

-- the depth of BG1 to BG4 in each BG mode (0: the layer is not there). Mode 7 is BG1 only, 8 bpp, its own map.
B.BPP = { [0] = { 2, 2, 2, 2 }, { 4, 4, 2, 0 }, { 4, 4, 0, 0 }, { 8, 4, 0, 0 }, { 8, 2, 0, 0 }, { 4, 2, 0, 0 },
          { 4, 0, 0, 0 }, { 8, 0, 0, 0 } }

-- the registers of the line snes_debug().regs: { mode, bgm, tm, sc[1..4], hofs[1..4], vofs[1..4], nba[1..4] }
function B.regs(line)
  line = line or ""
  local function hexes(key, n)
    local t, s = {}, line:match(key .. " ([%x ]+)") or ""
    for h in s:gmatch("%x+") do if #t < n then t[#t + 1] = tonumber(h, 16) end end
    for i = #t + 1, n do t[i] = 0 end
    return t
  end
  local function nums(key)
    local a, b, c, d = line:match(key .. " (%d+) (%d+) (%d+) (%d+)")
    return { tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0, tonumber(d) or 0 }
  end
  local bgm = tonumber(line:match("bgm (%x%x)") or "", 16)
  local mode = tonumber(line:match("mode (%d)") or "0") or 0
  return { mode = mode, bgm = bgm or mode, tm = tonumber(line:match("tm (%x%x)") or "0", 16) or 0,
           sc = hexes("sc", 4), nba = hexes("nba", 4), hofs = nums("hofs"), vofs = nums("vofs"),
           has_nba = line:find("nba ", 1, true) ~= nil }
end

-- one layer n (1-4) of the registers r: { n, bpp (0: not there), m7, map (byte address), wide, tall, cols, rows (in
-- tiles), t16 (16x16 tiles), char (byte address of the tiles), hofs, vofs, on (the main screen shows it),
-- pw, ph (the size of the map in pixels) }
function B.layer(r, n)
  local mode = r.mode & 7
  local L = { n = n, bpp = B.BPP[mode][n], m7 = mode == 7, on = (r.tm >> (n - 1)) & 1 == 1,
              hofs = r.hofs[n] & 0x3FF, vofs = r.vofs[n] & 0x3FF }
  if L.m7 then
    if n ~= 1 then L.bpp = 0 end
    L.map, L.char, L.cols, L.rows, L.t16 = 0, 0, 128, 128, false
  else
    local sc = r.sc[n]
    L.map = (sc & 0xFC) << 9
    L.wide, L.tall = sc & 1 == 1, sc & 2 == 2
    L.cols, L.rows = L.wide and 64 or 32, L.tall and 64 or 32
    L.t16 = (r.bgm >> (3 + n)) & 1 == 1
    L.char = r.nba[n] & 0xFFFF
  end
  local ts = L.t16 and 16 or 8
  L.pw, L.ph = L.cols * ts, L.rows * ts
  return L
end

-- the byte address of the map entry at (col, row): screens of 32x32 words, left to right, then top to bottom
function B.entry_addr(L, col, row)
  if L.m7 then return ((row & 127) * 128 + (col & 127)) * 2 end
  col, row = col % L.cols, row % L.rows
  local scr = 0
  if col >= 32 then scr = 1 end
  if row >= 32 then scr = scr + (L.wide and 2 or 1) end
  return (L.map + scr * 0x800 + ((row & 31) * 32 + (col & 31)) * 2) & 0xFFFF
end

-- the map word at (col, row); Mode 7: the tile number (the low byte of the word)
function B.word(vram, L, col, row)
  local a = B.entry_addr(L, col, row)
  local lo, hi = vram:byte(a + 1, a + 2)
  if L.m7 then return lo or 0 end
  return ((hi or 0) << 8) | (lo or 0)
end

-- the parts of a map word: tile (0-1023), pal (0-7), prio (0-1), fx, fy
function B.fields(w)
  return { tile = w & 0x3FF, pal = (w >> 10) & 7, prio = (w >> 13) & 1, fx = (w >> 14) & 1 == 1, fy = (w >> 15) == 1 }
end

-- the first colour (colour RAM entry) of palette pal of layer L: mode 0 gives each BG its own 32 colours
function B.pal_base(L, mode, pal)
  if L.bpp == 8 then return 0 end
  if L.bpp == 2 then return pal * 4 + ((mode & 7) == 0 and (L.n - 1) * 32 or 0) end
  return pal * 16
end

-- the colours of the colour RAM (BGR555 words), from 0
function B.colours(cgram)
  local c = {}
  for k = 0, 255 do
    local lo, hi = cgram:byte(k * 2 + 1, k * 2 + 2)
    c[k] = ((hi or 0) << 8 | (lo or 0)) & 0x7FFF
  end
  return c
end

-- the 8x8 indices (from 1, row after row) of tile t of layer L, not flipped. decode is snesgfx's decode_tile.
-- cache: a table for the tiles read already (the key is the address).
function B.tile(vram, L, t, decode, cache)
  if L.m7 then
    local key = "m" .. t
    if cache and cache[key] then return cache[key] end
    local px, base = {}, (t & 255) * 128
    for i = 0, 63 do px[i + 1] = vram:byte(base + i * 2 + 2) or 0 end
    if cache then cache[key] = px end
    return px
  end
  local size = L.bpp * 8
  local a = (L.char + (t & 0x3FF) * size) & 0xFFFF
  if cache and cache[a] then return cache[a] end
  local px
  if a + size <= #vram then px = decode(vram, L.bpp, a + 1)
  else px = decode(vram:sub(a + 1) .. vram:sub(1, size), L.bpp, 1) end    -- the tile wraps at the end of the video RAM
  if cache then cache[a] = px end
  return px
end

-- the pixels of the cell (col, row) as the screen shows them (flips done): a list of ts*ts indices from 1, the cell
-- size ts (8 or 16) and the first colour of its palette. A 16x16 cell is the tiles t, t+1, t+16, t+17.
function B.cell(vram, L, mode, col, row, decode, cache)
  local w = B.word(vram, L, col, row)
  if L.m7 then return B.tile(vram, L, w, decode, cache), 8, 0, w end
  local f = B.fields(w)
  local ts = L.t16 and 16 or 8
  local out = {}
  for sy = 0, ts // 8 - 1 do
    for sx = 0, ts // 8 - 1 do
      local qx, qy = f.fx and (ts // 8 - 1 - sx) or sx, f.fy and (ts // 8 - 1 - sy) or sy
      local t = (f.tile + qy * 16 + qx) & 0x3FF
      local px = B.tile(vram, L, t, decode, cache)
      for y = 0, 7 do
        for x = 0, 7 do
          local v = px[y * 8 + x + 1]
          local ox = sx * 8 + (f.fx and 7 - x or x)
          local oy = sy * 8 + (f.fy and 7 - y or y)
          out[oy * ts + ox + 1] = v
        end
      end
    end
  end
  return out, ts, B.pal_base(L, mode, f.pal), w
end

-- the cell (col, row) of the map under the screen point (x, y), with the scroll of the layer
function B.at_screen(L, x, y)
  local ts = L.t16 and 16 or 8
  return ((x + L.hofs) % L.pw) // ts, ((y + L.vofs) % L.ph) // ts
end

-- the boxes of the screen (256 x lines) on the map, in map pixels: 1 to 4 boxes, because the screen wraps at the edges
function B.screen_boxes(L, lines)
  lines = lines or 224
  local out = {}
  local x0, y0 = L.hofs % L.pw, L.vofs % L.ph
  local xs = { { x0, min(256, L.pw - x0) } }
  if x0 + 256 > L.pw then xs[2] = { 0, x0 + 256 - L.pw } end
  local ys = { { y0, min(lines, L.ph - y0) } }
  if y0 + lines > L.ph then ys[2] = { 0, y0 + lines - L.ph } end
  for _, a in ipairs(xs) do
    for _, b in ipairs(ys) do out[#out + 1] = { x = a[1], y = b[1], w = min(a[2], L.pw), h = min(b[2], L.ph) } end
  end
  return out
end

-- the words of a box of the map as assembly lines (".dw $0000, ..."), one line for each row
function B.dw_lines(vram, L, c0, r0, w, h)
  local lines = {}
  for r = r0, r0 + h - 1 do
    local t = {}
    for c = c0, c0 + w - 1 do
      t[#t + 1] = string.format(L.m7 and "$%02X" or "$%04X", B.word(vram, L, c, r))
    end
    lines[#lines + 1] = (L.m7 and "  .db " or "  .dw ") .. table.concat(t, ", ")
  end
  return lines
end

return B
