-- bg_test.lua: the BG reader of bm SNES SDK (core/lua/snesbg.lua) on the PC: the registers of the debug line, the
-- layers of each mode, the map addresses (as the PPU of bm finds them), the cells with flips, 16x16 tiles, Mode 7,
-- the screen on the map and the assembly lines.
--   build/host/luahost tests/snes/bg_test.lua [ROOT]     (make test-snes-bg)
local ROOT = arg[1] or "."
local B = assert(loadfile(ROOT .. "/core/lua/snesbg.lua"))()
local G = assert(loadfile(ROOT .. "/core/lua/snesgfx.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

-- the debug line of the kernel (core/src/bm/api_snes.c)
local line = "rot 0@0 mode 1 obsel 63 tm 15 ts 00 sc 21 31 59 00 hofs 1030 8 0 0 vofs 16 0 0 0 bgm 19 nba 0000 2000 4000 6000"
local r = B.regs(line)
check(r.mode == 1 and r.bgm == 0x19 and r.tm == 0x15, "mode, BGMODE and the main screen")
check(r.sc[1] == 0x21 and r.sc[3] == 0x59 and r.nba[2] == 0x2000 and r.nba[4] == 0x6000 and r.has_nba, "sc and nba")
check(r.hofs[1] == 1030 and r.vofs[1] == 16, "the scroll")
local old = B.regs("rot 0@0 mode 3 obsel 00 tm 01 ts 00 sc 00 00 00 00 hofs 0 0 0 0 vofs 0 0 0 0")
check(old.bgm == 3 and old.nba[1] == 0 and not old.has_nba, "an old line: no bgm and no nba")

-- the layers: depth, map, size, 16x16 tiles
local L1, L2, L3, L4 = B.layer(r, 1), B.layer(r, 2), B.layer(r, 3), B.layer(r, 4)
check(L1.bpp == 4 and L2.bpp == 4 and L3.bpp == 2 and L4.bpp == 0, "mode 1: 4, 4, 2 bpp and no BG4")
check(L1.map == 0x4000 and L1.wide and not L1.tall and L1.cols == 64 and L1.rows == 32, "BG1: sc $21 is $4000, 64x32")
check(L1.t16 and L1.pw == 1024 and L1.ph == 512, "BG1: bgm bit 4 gives 16x16 tiles, 1024x512 pixels")
check(L2.map == 0x6000 and L2.wide and not L2.t16 and L2.char == 0x2000, "BG2: $6000, 8x8 tiles, tiles at $2000")
check(L3.map == 0xB000 and L3.wide and not L3.tall and L3.on, "BG3: sc $59 is $B000; it is on the main screen")
check(L1.hofs == 6 and not L2.on, "BG1: the scroll wraps at 1024; BG2 is not on the main screen")
check(B.layer(B.regs("mode 7 tm 01"), 1).m7 and B.layer(B.regs("mode 7 tm 01"), 1).pw == 1024, "Mode 7: 128x128 tiles")
check(B.layer(B.regs("mode 7"), 2).bpp == 0, "Mode 7: no BG2")

-- the map addresses: screens of 32x32 words
local sq = B.layer(B.regs("mode 1 sc 03 00 00 00"), 1)               -- 64x64 at 0
check(B.entry_addr(sq, 0, 0) == 0 and B.entry_addr(sq, 31, 0) == 62, "screen 0, row 0")
check(B.entry_addr(sq, 32, 0) == 0x800 and B.entry_addr(sq, 0, 32) == 0x1000 and B.entry_addr(sq, 33, 33) == 0x1800 + 66,
      "64x64: screens 1, 2, 3")
local tl = B.layer(B.regs("mode 1 sc 02 00 00 00"), 1)               -- 32x64: the second screen is under the first
check(B.entry_addr(tl, 0, 32) == 0x800 and B.entry_addr(tl, 32, 0) == 0, "32x64: screen 1 is below, columns wrap at 32")

-- the cells: a 4 bpp tile with flip X, from the map and the tile data
local vram = {}
for i = 1, 65536 do vram[i] = "\0" end
local function poke(a, s) for k = 1, #s do vram[a + k] = s:sub(k, k) end end
local pat = {}
for y = 0, 7 do for x = 0, 7 do pat[y * 8 + x + 1] = (x + y * 2) % 16 end end
local L = B.layer(B.regs("mode 1 sc 00 00 00 00 bgm 01 nba 2000 0000 0000 0000"), 1)
poke(0x2000 + 5 * 32, G.encode_tile(pat, 4))
local w = 5 | (3 << 10) | (1 << 13) | (1 << 14)                       -- tile 5, palette 3, priority 1, flip X
poke(B.entry_addr(L, 2, 1), string.char(w & 255, w >> 8))
local v = table.concat(vram)
check(B.word(v, L, 2, 1) == w, "the word of cell (2,1)")
local f = B.fields(w)
check(f.tile == 5 and f.pal == 3 and f.prio == 1 and f.fx and not f.fy, "the parts of the word")
local px, ts, base = B.cell(v, L, 1, 2, 1, G.decode_tile, {})
check(ts == 8 and base == 48, "an 8x8 cell, the colours from 48 (palette 3 of 16)")
local same = true
for y = 0, 7 do for x = 0, 7 do if px[y * 8 + x + 1] ~= pat[y * 8 + (7 - x) + 1] then same = false end end end
check(same, "flip X: the tile is drawn from the right")
check(B.pal_base(B.layer(B.regs("mode 0"), 3), 0, 2) == 64 + 8, "mode 0: BG3 palette 2 is colour 72")

-- 16x16 tiles: the tiles t, t+1, t+16, t+17
local L16 = B.layer(B.regs("mode 1 sc 00 00 00 00 bgm 11 nba 2000 0000 0000 0000"), 1)
poke(0x2000 + 0x16 * 32, G.encode_tile(pat, 4))                     -- tile $16 = $05 + 17: the bottom right of the cell
poke(B.entry_addr(L16, 0, 0), string.char(5, 0))
v = table.concat(vram)
local p16, t16 = B.cell(v, L16, 1, 0, 0, G.decode_tile, {})
check(t16 == 16 and #p16 == 256 and p16[8 * 16 + 8 + 1] == pat[1] and p16[1] == 0, "16x16: tile t+17 is at the bottom right")

-- Mode 7: the tile number in the low bytes, the pixels in the high bytes
local L7 = B.layer(B.regs("mode 7 tm 01"), 1)
poke(B.entry_addr(L7, 3, 2), string.char(9))
poke(9 * 128 + 1, string.char(0x2A))                                -- tile 9, pixel 0: the high byte of word 9*64
v = table.concat(vram)
local p7, s7, b7, t7 = B.cell(v, L7, 7, 3, 2, G.decode_tile, {})
check(t7 == 9 and s7 == 8 and b7 == 0 and p7[1] == 0x2A, "Mode 7: tile 9, its first pixel $2A")

-- the screen on the map: it wraps at the edges
local sb = B.screen_boxes(L1, 224)
check(#sb == 1 and sb[1].x == 6 and sb[1].w == 256, "BG1 at x 6: one box")
local Lw = B.layer(B.regs("mode 1 sc 00 00 00 00 hofs 200 0 0 0 vofs 100 0 0 0"), 1)  -- 256x256, the screen at (200,100)
local bx = B.screen_boxes(Lw, 224)
check(#bx == 4 and bx[1].w == 56 and bx[2].x == 200 and bx[1].h == 156 and bx[4].w == 200 and bx[4].h == 68,
      "a 256x256 map with the screen at (200,100): four boxes")
local c, rr = B.at_screen(Lw, 60, 10)
check(c == 0 and rr == 13, "the screen point (60,10) is cell (0,13)")

-- assembly lines
local dl = B.dw_lines(v, L, 1, 1, 2, 1)
check(dl[1] == string.format("  .dw $0000, $%04X", w), "the .dw line: " .. dl[1])

-- the capture: a level of 180x28 cells that a game streams into a map of 64x32 (a ring), while the camera goes right
-- (past the wrap of the scroll at 1024) and then back left. Tile t of the level has its own bytes; the map words have palette 2 and some flips.
local WL, HL = 180, 28
local function lvl(gx, gy) return (gx * 7 + gy * 3) % 40 + 1 end          -- 40 different tiles
local function lword(gx, gy) return lvl(gx, gy) | (2 << 10) | ((gx % 5 == 0) and (1 << 14) or 0) end
local cv = {}
for i = 1, 65536 do cv[i] = 0 end
local function tbytes(t) return string.pack(">I2", t) .. string.rep("\0", 30) end
for t = 1, 40 do local b = tbytes(t); for k = 1, 32 do cv[0x2000 + t * 32 + k] = b:byte(k) end end
local function regs_at(x)
  return B.regs(string.format("mode 1 tm 01 sc 01 00 00 00 hofs %d 0 0 0 vofs 0 0 0 0 bgm 01 nba 2000 0000 0000 0000", x % 1024))
end
local CL = B.layer(regs_at(0), 1)
local function stream(cam)                                            -- the columns on the screen (and one more)
  for gx = cam // 8, cam // 8 + 32 do
    if gx >= 0 and gx < WL then
      for gy = 0, HL - 1 do
        local a = B.entry_addr(CL, gx % 64, gy)
        local w = lword(gx, gy)
        cv[a + 1], cv[a + 2] = w & 255, w >> 8
      end
    end
  end
  local s = {}
  for i = 1, 65536, 4096 do s[#s + 1] = string.char(table.unpack(cv, i, i + 4095)) end
  return table.concat(s)
end
local cam = 0
local cap = B.cap_new(regs_at(cam), 1)
check(cap.on and cap.bpp == 4 and cap.wx == 0, "a new capture of BG1")
check(B.cap_step(cap, regs_at(cam), stream(cam), "cg") == "ok", "the first sample")
while cam < (WL - 32) * 8 do cam = cam + 4; check(B.cap_step(cap, regs_at(cam), stream(cam), "cg") == "ok", "a sample at " .. cam) end
while cam > 40 do cam = cam - 4; B.cap_step(cap, regs_at(cam), stream(cam), "cg") end
check(cap.wx == cam, string.format("the world follows the scroll through the wrap at 1024: %d %d", cap.wx, cam))
check(cap.x0 == 0 and cap.x1 == WL - 1 and cap.y0 == 0 and cap.y1 == HL - 1,
      string.format("the cells seen: %d..%d x %d..%d", cap.x0, cap.x1, cap.y0, cap.y1))
check(cap.nt == 40, "40 tiles kept once each: " .. cap.nt)
local good = true
for gy = 0, HL - 1 do
  for gx = 0, WL - 1 do
    local v = B.cap_get(cap, gx, gy)
    if not v or cap.tiles[v >> 16] ~= tbytes(lvl(gx, gy)) or (v & 0xFC00) ~= (lword(gx, gy) & 0xFC00) then good = false end
  end
end
check(good, "each cell of the level has its tile and its attributes")
local ex = B.cap_export(cap)
check(not ex.err and ex.w == WL and ex.h == HL and #ex.map == WL * HL * 2 and #ex.chr == 41 * 32 and ex.pal == "cg",
      "the export: a map of 180x28 words, 41 tiles (the clear one first), the colours")
local w0 = string.unpack("<I2", ex.map, 1)
check(cap.tiles[w0 & 0x3FF] == tbytes(lvl(0, 0)) and (w0 >> 10) & 7 == 2 and (w0 >> 14) & 1 == 1, "the first word: new tile number, palette 2, flip X")
check(ex.nb > 0 and #ex.blk == ex.nb * 8 and #ex.bkm == (WL // 2) * (HL // 2) * 2, "the 16x16 blocks and the block map: " .. tostring(ex.nb))
local dw = B.cap_dw(cap, 0, 0, 2, 1)
check(dw[1] == string.format("  .dw $%04X, $%04X", B.cap_word(cap, 0, 0), B.cap_word(cap, 1, 0)), "the .dw lines of the level")
-- a jump pauses; resume goes on at the same place; a new layer address pauses
check(B.cap_step(cap, regs_at(cam + 300), stream(cam), "cg") == "jump" and not cap.on, "a jump of 300 pixels pauses")
B.cap_resume(cap, regs_at(cam + 300))
check(cap.on and B.cap_step(cap, regs_at(cam + 302), stream(cam), "cg") == "ok" and cap.wx == cam + 2, "resume: the world goes on from the pause")
check(B.cap_step(cap, B.regs("mode 1 tm 01 sc 05 00 00 00 bgm 01 nba 2000 0000 0000 0000"), stream(cam), "cg") == "scene",
      "the map moved in the video RAM: a new scene, it pauses")
-- a layer of 16x16 tiles: four 8x8 cells for each tile, flip X swaps the left and the right part
local r16 = B.regs("mode 1 tm 01 sc 00 00 00 00 hofs 0 0 0 0 vofs 0 0 0 0 bgm 11 nba 2000 0000 0000 0000")
local L16b = B.layer(r16, 1)
local v16 = {}
for i = 1, 65536 do v16[i] = "\0" end
local wf = 1 | (1 << 14)
local a16 = B.entry_addr(L16b, 0, 0)
v16[a16 + 1], v16[a16 + 2] = string.char(wf & 255), string.char(wf >> 8)
for _, t in ipairs({ 1, 2, 17, 18 }) do local b = tbytes(t); for k = 1, 32 do v16[0x2000 + t * 32 + k] = b:sub(k, k) end end
local c16 = B.cap_new(r16, 1)
B.cap_step(c16, r16, table.concat(v16), "")
check(c16.tiles[B.cap_get(c16, 0, 0) >> 16] == tbytes(2) and c16.tiles[B.cap_get(c16, 1, 0) >> 16] == tbytes(1)
      and c16.tiles[B.cap_get(c16, 0, 1) >> 16] == tbytes(18), "16x16 with flip X: the cells are tiles 2, 1 and 18, 17")
local big = { cells = {}, nt = 1100, x0 = 0, y0 = 0, x1 = 1, y1 = 1, tiles = {} }
check(B.cap_export(big).err ~= nil, "more than 1024 tiles: the export says why")

io.write(string.format("snesbg: %d/%d checks passed\n", checks - fails, checks))
if fails > 0 then os.exit(1) end
