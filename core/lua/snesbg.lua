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

----------------------------------------------------------------- the capture: a whole level from the scroll

-- A capture follows the scroll of one layer frame after frame and keeps each 8x8 cell that the screen showed, at its
-- place in the level (the world). The world starts at the map: world pixel (x, y) is map pixel (x mod pw, y mod ph),
-- so the cells keep the 16-pixel grid of the map. A cell is the tile it showed (its bytes, kept once) and the
-- attribute bits of its word (palette, priority, flips). A layer of 16x16 tiles gives four 8x8 cells for each tile.
-- cap = { n, mode, bpp, sig, on, h, v (the scroll at the last sample), wx, wy (the world pixel at the top left of
-- the screen), cells (key -> id << 16 | attributes), ids (bytes -> id), tiles (id -> bytes; 0 is the clear tile),
-- nt (the last id), x0, y0, x1, y1 (the cells seen), top, bot (the screen lines read), cgram, ver, samples }

-- what must stay the same in a capture: the mode, the depth and the addresses of the layer
function B.cap_sig(r, n)
  local L = B.layer(r, n)
  return string.format("%d %d %04X %04X %s%s%s", r.mode & 7, L.bpp, L.map or 0, L.char or 0, L.t16 and "t16" or "t8",
                       L.wide and " w" or "", L.tall and " t" or "")
end

function B.cap_new(r, n)
  local L = B.layer(r, n)
  local blank = string.rep("\0", L.bpp * 8)
  return { n = n, mode = r.mode & 7, bpp = L.bpp, sig = B.cap_sig(r, n), on = true, h = L.hofs, v = L.vofs,
           wx = L.hofs, wy = L.vofs, cells = {}, ids = { [blank] = 0 }, tiles = { [0] = blank }, nt = 0,
           top = 0, bot = 223, ver = 0, samples = 0 }
end

-- the key of world cell (gx, gy) in cap.cells
function B.cap_key(gx, gy) return (gy + 32768) * 65536 + (gx + 32768) end
function B.cap_get(cap, gx, gy) return cap.cells[B.cap_key(gx, gy)] end

-- a scroll change, shortest way round a map of p pixels
local function wrapd(d, p)
  d = d % p
  if d > p // 2 then d = d - p end
  return d
end

-- go on after a pause: the screen now is at the same place of the world as at the pause
function B.cap_resume(cap, r)
  local L = B.layer(r, cap.n)
  cap.h, cap.v, cap.sig, cap.on = L.hofs, L.vofs, B.cap_sig(r, cap.n), true
end

-- one sample: the registers r, the video RAM and the colour RAM of one frame. Returns "ok"; "jump" (the scroll moved
-- more than jump pixels: a new room or a cut, the capture pauses); "scene" (the layer changed: it pauses); "paused".
function B.cap_step(cap, r, vram, cgram, jump)
  if not cap.on then return "paused" end
  local L = B.layer(r, cap.n)
  if L.m7 or L.bpp == 0 or B.cap_sig(r, cap.n) ~= cap.sig then cap.on = false; return "scene" end
  local dx, dy = wrapd(L.hofs - cap.h, L.pw), wrapd(L.vofs - cap.v, L.ph)
  jump = jump or 64
  if math.abs(dx) > jump or math.abs(dy) > jump then cap.on = false; return "jump" end
  cap.h, cap.v = L.hofs, L.vofs
  cap.wx, cap.wy = cap.wx + dx, cap.wy + dy
  local size, ts = L.bpp * 8, L.t16 and 16 or 8
  local seen = {}
  local gx0, gx1 = cap.wx // 8, (cap.wx + 255) // 8
  local gy0, gy1 = (cap.wy + cap.top) // 8, (cap.wy + cap.bot) // 8
  for gy = gy0, gy1 do
    local my = (gy * 8) % L.ph
    for gx = gx0, gx1 do
      local mx = (gx * 8) % L.pw
      local w = B.word(vram, L, mx // ts, my // ts)
      local tile = w & 0x3FF
      if L.t16 then                                  -- the 8x8 part of the 16x16 tile at this cell
        local sx, sy = (mx // 8) & 1, (my // 8) & 1
        if (w >> 14) & 1 == 1 then sx = 1 - sx end
        if (w >> 15) == 1 then sy = 1 - sy end
        tile = (tile + sy * 16 + sx) & 0x3FF
      end
      local a = (L.char + tile * size) & 0xFFFF
      local raw = seen[a]
      if not raw then
        raw = vram:sub(a + 1, a + size)
        if #raw < size then raw = raw .. vram:sub(1, size - #raw) end
        if #raw < size then raw = raw .. string.rep("\0", size - #raw) end
        seen[a] = raw
      end
      local id = cap.ids[raw]
      if not id then cap.nt = cap.nt + 1; id = cap.nt; cap.ids[raw] = id; cap.tiles[id] = raw end
      cap.cells[B.cap_key(gx, gy)] = (id << 16) | (w & 0xFC00)
    end
  end
  if not cap.x0 then cap.x0, cap.x1, cap.y0, cap.y1 = gx0, gx1, gy0, gy1
  else
    cap.x0, cap.x1 = math.min(cap.x0, gx0), math.max(cap.x1, gx1)
    cap.y0, cap.y1 = math.min(cap.y0, gy0), math.max(cap.y1, gy1)
  end
  cap.cgram = cgram
  cap.ver, cap.samples = cap.ver + 1, cap.samples + 1
  return "ok"
end

-- the word of world cell (gx, gy) with the new tile numbers (the ids); 0 for a cell not seen
function B.cap_word(cap, gx, gy)
  local v = cap.cells[B.cap_key(gx, gy)]
  if not v then return 0 end
  return (v & 0xFC00) | ((v >> 16) & 0x3FF)
end

-- the words of a box of the level as assembly lines (the new tile numbers)
function B.cap_dw(cap, x, y, w, h)
  local lines = {}
  for gy = y, y + h - 1 do
    local t = {}
    for gx = x, x + w - 1 do t[#t + 1] = string.format("$%04X", B.cap_word(cap, gx, gy)) end
    lines[#lines + 1] = "  .dw " .. table.concat(t, ", ")
  end
  return lines
end

-- the files of the level: map (words, row after row), chr (the tiles in id order, as in the video RAM), pal (the
-- colour RAM), blk (the 16x16 blocks found: 4 words each, top left, top right, bottom left, bottom right) and bkm
-- (the block numbers, 16 bits, row after row). The box starts and ends on the 16-pixel grid.
-- Returns { map, chr, pal, blk, bkm, x0, y0, w, h (cells), nt (tiles), nb (blocks), err }.
function B.cap_export(cap)
  if not cap.x0 then return { err = "nothing captured yet" } end
  local X0, Y0 = cap.x0 - cap.x0 % 2, cap.y0 - cap.y0 % 2
  local X1, Y1 = cap.x1 + (1 - cap.x1 % 2), cap.y1 + (1 - cap.y1 % 2)
  local w, h = X1 - X0 + 1, Y1 - Y0 + 1
  local out = { x0 = X0, y0 = Y0, w = w, h = h, nt = cap.nt + 1, pal = cap.cgram or "" }
  if cap.nt > 1023 then
    out.err = string.format("%d tiles: a map word holds tile numbers up to 1023", cap.nt + 1)
    return out
  end
  local map = {}
  for gy = Y0, Y1 do
    for gx = X0, X1 do map[#map + 1] = string.pack("<I2", B.cap_word(cap, gx, gy)) end
  end
  out.map = table.concat(map)
  local chr = {}
  for id = 0, cap.nt do chr[#chr + 1] = cap.tiles[id] end
  out.chr = table.concat(chr)
  local bid, blk, bkm = {}, {}, {}
  for by = Y0, Y1, 2 do
    for bx = X0, X1, 2 do
      local b = string.pack("<I2I2I2I2", B.cap_word(cap, bx, by), B.cap_word(cap, bx + 1, by),
                            B.cap_word(cap, bx, by + 1), B.cap_word(cap, bx + 1, by + 1))
      local k = bid[b]
      if not k then k = #blk; bid[b] = k; blk[#blk + 1] = b end
      bkm[#bkm + 1] = string.pack("<I2", k)
    end
  end
  out.blk, out.bkm, out.nb = table.concat(blk), table.concat(bkm), #blk
  return out
end

return B
