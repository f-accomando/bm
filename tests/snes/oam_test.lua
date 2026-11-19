-- oam_test.lua: the OAM reader of bm SNES SDK (core/lua/snesoam.lua) on the PC: OBSEL, the 128 entries, the place on
-- the screen, the tiles in the video RAM (as the PPU of bm finds them), the colours and the sprite under a point.
--   build/host/luahost tests/snes/oam_test.lua [ROOT]     (make test-snes-oam)
local ROOT = arg[1] or "."
local O = assert(loadfile(ROOT .. "/core/lua/snesoam.lua"))()
local G = assert(loadfile(ROOT .. "/core/lua/snesgfx.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

-- OBSEL $63: sizes 16x16 and 32x32, the name tables at $C000 and $E000 (bytes)
check(O.obsel("rot 0@0 mode 1 obsel 63 tm 13") == 0x63, "the OBSEL of the regs line")
check(O.obsel(nil) == 0 and O.obsel("mode 1") == 0, "no OBSEL: 0")
local b0, b1 = O.bases(0x63)
check(b0 == 0xC000 and b1 == 0xE000, string.format("the name tables of $63: $%04X $%04X", b0, b1))
local g0, g1 = O.bases(0x19)
check(g0 == 0x4000 and g1 == 0xC000, "OBSEL $19: the gap 3 puts the second name table $8000 after the first")

-- the table: 128 entries of 4 bytes and 32 bytes of high bits
local ent, high = {}, {}
for i = 0, 127 do ent[i] = { 0, 0xE0, 0, 0 } end                  -- y 224: under the screen
for i = 0, 31 do high[i] = 0 end
ent[0] = { 10, 19, 0x02, (1 << 6) | (2 << 4) | (3 << 1) }        -- x 10, top line 20, tile $02, pal 3, prio 2, flip X
ent[1] = { 0xF0, 0xF0, 0x0F, 0x81 }                              -- x -16 (bit 8 set), top line -15, large, name 1, flip Y
high[0] = (1 << 2) | (1 << 3)                                     -- sprite 1: bit 8 of x, large
local parts = {}
for i = 0, 127 do parts[#parts + 1] = string.char(table.unpack(ent[i])) end
for i = 0, 31 do parts[#parts + 1] = string.char(high[i]) end
local oam = table.concat(parts)
check(#oam == 544, "a table of 544 bytes")
local list = O.decode(oam, 0x63, 224)
check(#list == 128, "128 entries")
local a, b, c = list[1], list[2], list[3]
check(a.x == 10 and a.y == 19 and a.sy == 20 and a.tile == 2 and a.name == 0 and a.pal == 3 and a.prio == 2,
      "sprite 0: place, tile, palette, priority")
check(a.fx and not a.fy and not a.big and a.w == 16 and a.h == 16 and a.vis, "sprite 0: flip X, small 16x16, it shows")
check(b.x == -16 and b.sy == -15 and b.big and b.w == 32 and b.h == 32 and b.name == 1 and b.fy and b.vis,
      "sprite 1: x -16, from the top (-15), large 32x32, name table 1, flip Y, it shows")
check(c.sy == 225 and not c.vis, "sprite 2: y 224 is under the screen")
check(#O.visible(list) == 2, "two sprites show")
local l8 = O.decode(oam, 0x00, 224)
check(l8[1].w == 8 and l8[2].w == 16, "OBSEL size 0: 8x8 and 16x16")
local l6 = O.decode(oam, 0xC0, 224)
check(l6[1].w == 16 and l6[1].h == 32 and l6[2].w == 32 and l6[2].h == 64, "OBSEL size 6: 16x32 and 32x64")

-- the tiles: rows of 16 in the name table; the columns wrap in 16
check(O.tile_addr(a, 0x63, 0, 0) == 0xC000 + 0x02 * 32, "tile $02 at $C040")
check(O.tile_addr(a, 0x63, 1, 1) == 0xC000 + 0x13 * 32, "column 1, row 1: tile $13")
check(O.tile_addr(b, 0x63, 1, 0) == 0xE000 + 0x00 * 32, "tile $0F, column 1: tile $00 (the column wraps), name table 1")

-- the pixels from the video RAM (4 bpp) and the colours from the colour RAM
local vram = {}
for i = 1, 65536 do vram[i] = "\0" end
local pat = {}
for y = 0, 7 do for x = 0, 7 do pat[y * 8 + x + 1] = (x + y * 3) % 16 end end
local bytes = G.encode_tile(pat, 4)
for k = 1, 32 do vram[0xC000 + 0x02 * 32 + k] = bytes:sub(k, k) end
vram = table.concat(vram)
local px = O.pixels(a, 0x63, vram, G.decode_tile)
check(#px == 256, "16x16 = 256 pixels")
local same = true
for y = 0, 7 do for x = 0, 7 do if px[y * 16 + x + 1] ~= pat[y * 8 + x + 1] then same = false end end end
check(same, "the first tile of sprite 0 is the pattern in the video RAM (not flipped)")
check(px[8 * 16 + 1] == 0, "the tile under it ($12) is empty")
local cg = {}
for i = 1, 512 do cg[i] = "\0" end
local at = (128 + 3 * 16 + 5) * 2
cg[at + 1], cg[at + 2] = string.char(0x34), string.char(0x92)
local col = O.colours(table.concat(cg), 3)
check(#col == 16 and col[6] == 0x1234 and col[1] == 0, string.format("colour 5 of palette 3: $%04X (bit 15 cleared)", col[6]))

-- the sprite under a point: the lowest number is on top
check(O.at(list, 12, 22) == a, "the point (12,22) is on sprite 0")
check(O.at(list, 5, 5) == b, "the point (5,5) is on sprite 1")
check(O.at(list, 200, 200) == nil, "no sprite at (200,200)")
local line = O.line(a)
check(line:find("10,  20 $002 p3 2 16x16 X", 1, true) ~= nil, "the list line: " .. line)

io.write(string.format("snesoam: %d/%d checks passed\n", checks - fails, checks))
if fails > 0 then os.exit(1) end
