-- rom_test.lua: the ROM reader of bm SNES SDK (core/lua/snesrom.lua) on the PC: header, map, vectors, offsets,
-- the disassembly (it follows rep and sep; it assembles again to the same bytes) and the tiles.
--   build/host/luahost tests/snes/rom_test.lua [ROOT]     (make test-snes-rom)
local ROOT = arg[1] or "."
package.path = ROOT .. "/core/src/script/?.lua;" .. package.path
local A = require "snes_asm"
local R = assert(loadfile(ROOT .. "/core/lua/snesrom.lua"))()
local G = assert(loadfile(ROOT .. "/core/lua/snesgfx.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

local SRC = [[
    .org $008000
reset:
    sei
    clc
    xce
    rep #$30
    lda #$1234
    ldx #$0010
loop:
    sta $7E0010,x
    dex
    bne loop
    sep #$20
    lda #$12
    jsr sub
    jsl sub
    mvn $7E,$7F
    sta a:$0012
    brk #$01
    jmp reset
sub:
    rts
nmi:
    rti
]]

local function build(map)
  local src = SRC
  if map == "hirom" then src = src:gsub("%.org %$008000", ".org $C08000") end
  local rom, res = A.build(src, {}, { title = "ROM TEST", map = map, vectors = { nmi = "nmi" } })
  assert(rom, tostring(res))
  return rom, res
end

-- LoROM
local bytes, res = build("lorom")
local rom = R.open(bytes)
check(rom.map == "lo" and rom.sure, "LoROM found: " .. rom.map)
check(rom.title == "ROM TEST", "title: [" .. tostring(rom.title) .. "]")
check(rom.copier == 0 and rom.size == #bytes, "no copier header")
check(R.offset(rom, 0x008000) == 0 and R.offset(rom, 0x808000) == 0, "LoROM $00:8000 and $80:8000 are offset 0")
check(R.offset(rom, 0x018000) == 0x8000 or rom.size <= 0x8000, "LoROM bank 1")
check(R.offset(rom, 0x7E0000) == nil, "work RAM is not the ROM")
check(R.offset(rom, 0x002000) == nil, "LoROM $0000-$7FFF of bank 0 is not the ROM")
check(R.addr(rom, 0) == 0x808000 and R.addr(rom, 0x7FFF) == 0x80FFFF, "LoROM offset to address")
local v = R.vectors(rom)
check(v[1] and v[1].name == "reset" and v[1].addr == 0x8000, "the reset vector")
local has_nmi = false
for _, e in ipairs(v) do if e.name == "nmi" and e.addr == res.symbols.nmi then has_nmi = true end end
check(has_nmi, "the nmi vector")
check(#R.about(rom, "test.sfc") >= 4, "the about lines")

-- the copier header is cut
local rom2 = R.open(string.rep("\0", 512) .. bytes)
check(rom2.copier == 512 and rom2.size == #bytes and rom2.title == "ROM TEST", "a 512-byte copier header")

-- the disassembly
local d = R.disasm(rom, 0x008000, { name = "test.sfc" })
local text = d.text
local function has(s) return text:find(s, 1, true) ~= nil end
check(has("reset:"), "the reset label")
check(has("nmi:"), "the nmi label")
check(has("lda #$1234"), "rep #$30 makes lda 16-bit: lda #$1234")
check(has("ldx #$0010"), "rep #$30 makes ldx 16-bit")
check(has("lda #$12 "), "sep #$20 makes lda 8-bit again")
check(has(".a16") and has(".i16") and has(".a8"), "the width lines")
check(has("bne L"), "a branch goes to a label")
check(has("jsr L") and has("jsl L"), "jsr and jsl go to a label")
check(has("mvn $7E,$7F"), "mvn source, destination")
check(has("sta a:$0012"), "an absolute operand under $100 keeps its width")
check(has("brk #$01"), "brk with its signature byte")
check(has("jmp reset"), "jmp to the reset label")
check(has("; 00:8000  78"), "the address and the bytes of sei")
local l = R.line_of(d, rom, 0x008000)
check(l and d.addr[l] == 0x008000, "the line of $00:8000")
local l2 = R.line_of(d, rom, 0x808001)
check(l2 and text:match("[^\n]*", 1) and d.addr[l2] == 0x008001, "a mirror address finds the same line")

-- the text assembles again to the same bytes (the code part, up to the first data byte)
local code = text:gsub("\n    %.db[^\n]*", "")
local res2 = A.assemble(code, {})
check(res2.ok, "the disassembly assembles: " .. (res2.ok and "" or A.format_errors(res2)))
if res2.ok then
  local seg = res2.segments[1]
  local want = bytes:sub(1, #seg.data)
  check(seg.data == want, "the same bytes again")
end

-- an address out of the ROM
local dn, why = R.disasm(rom, 0x7E0000)
check(dn == nil and why:find("work RAM"), "disasm of RAM says why")

-- HiROM
local hb = build("hirom")
local hr = R.open(hb)
check(hr.map == "hi", "HiROM found: " .. hr.map)
check(R.offset(hr, 0xC08000) == 0x8000 and R.offset(hr, 0x008000) == 0x8000, "HiROM $C0:8000 and $00:8000")
check(R.addr(hr, 0x8000) == 0xC08000, "HiROM offset to address")
local hd = R.disasm(hr, 0xC08000)
check(hd and hd.text:find("lda #$1234", 1, true), "HiROM disassembly")

-- the tiles: one 4 bpp tile, read back
local px = {}
for i = 1, 64 do px[i] = (i - 1) % 16 end
local tile = G.encode_tile(px, 4)
local fake = R.open(tile .. string.rep("\0", 32 * 1024 - #tile))
local t = R.tiles(fake, 0, 16, 8, 4, G.decode_tile)
check(t[0] == 0 and t[1] == 1 and t[7] == 7 and t[8] == 0 and t[16 + 0] == 8, "the tile at the start of the sheet")
check(R.page_bytes(128, 256, 4) == 16384, "a page of 128x256 at 4 bpp is 16 KB")
local t2 = R.tiles(fake, fake.size - 16, 16, 8, 4, G.decode_tile)
check(t2[0] ~= nil and t2[8] == 0, "a tile past the end is clear")

io.write(string.format("snesrom: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
