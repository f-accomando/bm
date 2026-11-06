-- Part 3: the picture. The renderer draws whole 8x8 tiles. A tile is decoded once into a cell of
-- the sprite sheet and drawn with sspr(). A "band" is a run of lines with the same registers: a
-- change of scroll in the middle of the frame (a status bar) or by HDMA makes a new band.

local cache_flush, ppu_frame_start, ppu_line, ppu_frame_end, ppu_render, ppu_decode
do

local SHEET_CELLS_W = 128                       -- the sheet is 1024 x 1024: 128 x 128 cells
local CELLS = SHEET_CELLS_W * SHEET_CELLS_W
local cellx, celly, verof = {}, {}, {}
local ncell = 0

cache_flush = function()
  cellx, celly, verof = {}, {}, {}
  ncell = 0
end

local function decode(addr, bpp, pal, sx, sy)
  for row = 0, 7 do
    local o = addr + row * 2
    local p0 = VRAM[(o & 0xFFFF) + 1]
    local p1 = VRAM[((o + 1) & 0xFFFF) + 1]
    local p2, p3, p4, p5, p6, p7 = 0, 0, 0, 0, 0, 0
    if bpp >= 4 then
      p2 = VRAM[((o + 16) & 0xFFFF) + 1]; p3 = VRAM[((o + 17) & 0xFFFF) + 1]
    end
    if bpp == 8 then
      p4 = VRAM[((o + 32) & 0xFFFF) + 1]; p5 = VRAM[((o + 33) & 0xFFFF) + 1]
      p6 = VRAM[((o + 48) & 0xFFFF) + 1]; p7 = VRAM[((o + 49) & 0xFFFF) + 1]
    end
    for x = 0, 7 do
      local sh = 7 - x
      local c = ((p0 >> sh) & 1) | (((p1 >> sh) & 1) << 1) | (((p2 >> sh) & 1) << 2)
        | (((p3 >> sh) & 1) << 3) | (((p4 >> sh) & 1) << 4) | (((p5 >> sh) & 1) << 5)
        | (((p6 >> sh) & 1) << 6) | (((p7 >> sh) & 1) << 7)
      if c == 0 then sset(sx + x, sy + row, 0, 0)
      else sset(sx + x, sy + row, CG24[pal + c + 1], 255) end
    end
  end
end

ppu_decode = decode                           -- the viewer uses it

-- the sheet position of a character (its address in VRAM bytes, bits per pixel, first colour)
local BPPC = { [2] = 0, [4] = 1, [8] = 2 }
local function cellxy(addr, bpp, pal)
  local key = (addr << 10) | (BPPC[bpp] << 8) | pal
  local nb = bpp >> 1
  local b = (addr >> 4) + 1
  local ver = BLKV[b]
  for k = 1, nb - 1 do ver = ver + BLKV[((b - 1 + k) & 4095) + 1] end
  local r = pal >> 4
  ver = ver + CGV[r + 1]
  if bpp == 8 then for k = 1, 15 do ver = ver + CGV[k + 1] end end
  local x = cellx[key]
  if not x then
    if ncell >= CELLS then return 0, 0 end
    x = (ncell % SHEET_CELLS_W) << 3
    celly[key] = (ncell // SHEET_CELLS_W) << 3
    cellx[key] = x
    ncell = ncell + 1
    verof[key] = -1
  end
  if verof[key] ~= ver then
    verof[key] = ver
    decode(addr, bpp, pal, x, celly[key])
  end
  return x, celly[key]
end

-- one frame never changes the VRAM it draws from: the cell of a character is looked up once
local memo = {}
local function cellp(addr, bpp, pal)
  local key = (addr << 10) | (BPPC[bpp] << 8) | pal
  local v = memo[key]
  if v then return v & 0xFFFF, v >> 16 end
  local x, y = cellxy(addr, bpp, pal)
  memo[key] = x | (y << 16)
  return x, y
end

------------------------------------------------------------------ bands
local bands, curband, bandstart = {}, nil, 0

local function snap(y)
  local sc, nba, hofs, vofs = {}, {}, {}, {}
  for i = 1, 4 do sc[i] = P.sc[i]; nba[i] = P.nba[i]; hofs[i] = P.hofs[i]; vofs[i] = P.vofs[i] end
  return { y0 = y, y1 = y + 1, inidisp = P.inidisp, obsel = P.obsel, bgmode = P.bgmode,
           tm = P.tm, sc = sc, nba = nba, hofs = hofs, vofs = vofs }
end

ppu_frame_start = function()
  bands = {}
  curband = nil
  ppudirty = true
end

ppu_line = function(y)                     -- before the CPU runs on line y
  if y == 0 or (ppudirty and y - bandstart >= ((#bands < 6) and 1 or 8)) then
    if curband then curband.y1 = y end
    curband = snap(y)
    bands[#bands + 1] = curband
    bandstart = y
    ppudirty = false
  end
end

ppu_frame_end = function(h)
  if curband then curband.y1 = h end
end

------------------------------------------------------------------ layers
local BPP = {
  [0] = { 2, 2, 2, 2 }, { 4, 4, 2, 0 }, { 4, 4, 0, 0 }, { 8, 4, 0, 0 },
  { 8, 2, 0, 0 }, { 4, 2, 0, 0 }, { 4, 0, 0, 0 }, { 8, 0, 0, 0 },
}
-- back to front: { layer, priority }, layer 1..4 = BG, 0 = sprites
local ORDER = {
  [0] = { { 4, 0 }, { 3, 0 }, { 0, 0 }, { 4, 1 }, { 3, 1 }, { 0, 1 }, { 2, 0 }, { 1, 0 }, { 0, 2 }, { 2, 1 }, { 1, 1 }, { 0, 3 } },
  { { 3, 0 }, { 0, 0 }, { 3, 1 }, { 0, 1 }, { 2, 0 }, { 1, 0 }, { 0, 2 }, { 2, 1 }, { 1, 1 }, { 0, 3 } },
  { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 2, 1 }, { 0, 2 }, { 1, 1 }, { 0, 3 } },
  { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 2, 1 }, { 0, 2 }, { 1, 1 }, { 0, 3 } },
  { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 2, 1 }, { 0, 2 }, { 1, 1 }, { 0, 3 } },
  { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 2, 1 }, { 0, 2 }, { 1, 1 }, { 0, 3 } },
  { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 }, { 0, 3 } },
  { { 1, 0 }, { 1, 1 }, { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 } },
}
local LST = {}                                 -- LST[layer * 4 + priority] = flat list of draws
for l = 0, 4 do for p = 0, 3 do LST[l * 4 + p] = { n = 0 } end end

local function collectBG(b, n, bpp, ya, yb)
  local L0, L1 = LST[n * 4], LST[n * 4 + 1]
  L0.n = 0; L1.n = 0
  local sc = b.sc[n]
  local scb = (sc >> 2) << 11
  local wide, tall = (sc & 1) ~= 0, (sc & 2) ~= 0
  local big = ((b.bgmode >> (3 + n)) & 1) ~= 0
  local sh = big and 4 or 3
  local h, v = b.hofs[n], b.vofs[n]
  local nba = b.nba[n]
  local bytes = bpp * 8
  local ty0, ty1 = (ya + v) >> sh, (yb - 1 + v) >> sh
  local tx0, tx1 = h >> sh, (h + 255) >> sh
  local xm = wide and 63 or 31
  local ym = tall and 63 or 31
  local pw2 = wide and 2 or 1
  local mode0 = (b.bgmode & 7) == 0
  for ty = ty0, ty1 do
    local tmy = ty & ym
    local rowa = scb + ((tmy >> 5) * pw2) * 0x800 + (tmy & 31) * 64
    local dy = (ty << sh) - v
    for tx = tx0, tx1 do
      local tmx = tx & xm
      local ea = (rowa + ((tmx >> 5) * 0x800) + (tmx & 31) * 2) & 0xFFFF
      local lo, hi = VRAM[ea + 1], VRAM[ea + 2]
      local tile = lo | ((hi & 3) << 8)
      local pal = (hi >> 2) & 7
      local L = ((hi >> 5) & 1) == 1 and L1 or L0
      local fx, fy = (hi >> 6) & 1, hi >> 7
      local pbase
      if bpp == 4 then pbase = pal << 4
      elseif bpp == 2 then pbase = (mode0 and ((n - 1) << 5) or 0) + (pal << 2)
      else pbase = 0 end
      local dx = (tx << sh) - h
      if not big then
        local cx, cy = cellp((nba + tile * bytes) & 0xFFFF, bpp, pbase)
        local c = L.n
        L[c + 1] = cx; L[c + 2] = cy; L[c + 3] = dx; L[c + 4] = dy; L[c + 5] = fx | (fy << 1)
        L.n = c + 5
      else
        for j = 0, 1 do
          for i = 0, 1 do
            local t = (tile + ((fx == 1) and (1 - i) or i) + 16 * ((fy == 1) and (1 - j) or j)) & 0x3FF
            local cx, cy = cellp((nba + t * bytes) & 0xFFFF, bpp, pbase)
            local c = L.n
            L[c + 1] = cx; L[c + 2] = cy; L[c + 3] = dx + i * 8; L[c + 4] = dy + j * 8
            L[c + 5] = fx | (fy << 1)
            L.n = c + 5
          end
        end
      end
    end
  end
end

local OBJSZ = {
  [0] = { 8, 8, 16, 16 }, { 8, 8, 32, 32 }, { 8, 8, 64, 64 }, { 16, 16, 32, 32 },
  { 16, 16, 64, 64 }, { 32, 32, 64, 64 }, { 16, 32, 32, 64 }, { 16, 32, 32, 32 },
}
-- the sprites that can be seen, parsed once a frame (the sizes come from the first band)
local SP = { n = 0 }                           -- SP[k] = { x, y, w, h, tile, attr }; the last one is drawn first
local function parse_obj(ob, lines)
  local sz = OBJSZ[(ob >> 5) & 7]
  local n = 0
  for i = 127, 0, -1 do
    local o = i * 4
    local y = OAM[o + 2] + 1
    if y >= 225 then y = y - 256 end
    local hb = OAM[512 + (i >> 2) + 1] >> ((i & 3) * 2)
    local w, h = sz[1], sz[2]
    if hb & 2 ~= 0 then w, h = sz[3], sz[4] end
    if y < lines and y + h > 0 then
      local x = OAM[o + 1] | ((hb & 1) << 8)
      if x >= 256 then x = x - 512 end
      if x < 256 and x + w > 0 then
        n = n + 1
        local e = SP[n]
        if not e then e = {}; SP[n] = e end
        e[1], e[2], e[3], e[4], e[5], e[6] = x, y, w, h, OAM[o + 3], OAM[o + 4]
      end
    end
  end
  SP.n = n
end

local function collectOBJ(b, ya, yb)
  for p = 0, 3 do LST[p].n = 0 end
  local ob = b.obsel
  local base = (ob & 7) << 14
  local gap = (((ob >> 3) & 3) + 1) << 13
  for k = 1, SP.n do
    local e = SP[k]
    local x, y, w, h = e[1], e[2], e[3], e[4]
    if y < yb and y + h > ya then
      local tile, at = e[5], e[6]
      local fx, fy = (at >> 6) & 1, (at >> 7) & 1
      local pbase = 128 + (((at >> 1) & 7) << 4)
      local tb = base
      if at & 1 ~= 0 then tb = base + gap end
      local wt, ht = w >> 3, h >> 3
      local L = LST[(at >> 4) & 3]
      for ty = 0, ht - 1 do
        local r = (fy == 1) and (ht - 1 - ty) or ty
        for tx = 0, wt - 1 do
          local c = (fx == 1) and (wt - 1 - tx) or tx
          local t = ((tile + (r << 4)) & 0xF0) | ((tile + c) & 0x0F)
          local cx, cy = cellp((tb + t * 32) & 0xFFFF, 4, pbase)
          local n = L.n
          L[n + 1] = cx; L[n + 2] = cy; L[n + 3] = x + tx * 8; L[n + 4] = y + ty * 8
          L[n + 5] = fx | (fy << 1)
          L.n = n + 5
        end
      end
    end
  end
end

local function drawlist(L)
  for i = 1, L.n, 5 do
    local f = L[i + 4]
    sspr(L[i], L[i + 1], 8, 8, L[i + 2], L[i + 3], (f & 1) ~= 0, (f & 2) ~= 0)
  end
end

local function drawBand(b)
  local ya, yb = b.y0, b.y1
  if yb <= ya then return end
  clip(0, ya, 256, yb - ya)
  if b.inidisp & 0x80 ~= 0 then
    rectfill(0, ya, 256, yb - ya, 0)
    return
  end
  rectfill(0, ya, 256, yb - ya, CG24[1])
  local mode = b.bgmode & 7
  if mode == 7 then
    -- mode 7 (the rotating plane) is not drawn yet: only the sprites show
  end
  local tm = b.tm
  local bpps = BPP[mode]
  for n = 1, 4 do
    if mode ~= 7 and bpps[n] > 0 and (tm >> (n - 1)) & 1 == 1 then
      collectBG(b, n, bpps[n], ya, yb)
    else
      LST[n * 4].n = 0; LST[n * 4 + 1].n = 0
    end
  end
  if tm & 16 ~= 0 then collectOBJ(b, ya, yb) else for p = 0, 3 do LST[p].n = 0 end end
  local ord = ORDER[mode]
  local top3 = (mode == 1 and b.bgmode & 8 ~= 0)
  for i = 1, #ord do
    local e = ord[i]
    if not (top3 and e[1] == 3 and e[2] == 1) then drawlist(LST[e[1] * 4 + e[2]]) end
  end
  if top3 then drawlist(LST[3 * 4 + 1]) end
  local br = b.inidisp & 15
  if br < 15 then
    blend("alpha", ((15 - br) * 255) // 15)
    rectfill(0, ya, 256, yb - ya, 0)
    blend("none")
  end
end

local drawnver = -1
ppu_render = function(target_id, lines)
  if gfxver == drawnver then return end        -- nothing that shows has changed since the last drawing
  drawnver = gfxver
  memo = {}
  if bands[1] then parse_obj(bands[1].obsel, lines) else SP.n = 0 end
  target(target_id)
  for i = 1, #bands do drawBand(bands[i]) end
  clip()
  target()
  if ncell > CELLS - 3000 then cache_flush() end
end
end
