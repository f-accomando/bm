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

io.write(string.format("snesbg: %d/%d checks passed\n", checks - fails, checks))
if fails > 0 then os.exit(1) end
