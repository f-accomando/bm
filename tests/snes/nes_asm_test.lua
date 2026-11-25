-- nes_asm_test.lua: the 6502 mode of the assembler and the iNES builder (core/src/script/snes_asm.lua:
-- opts.cpu = "6502", build_nes). Run from the repository root: build/host/luahost tests/snes/nes_asm_test.lua
package.path = "core/src/script/?.lua;" .. package.path
local A = require("snes_asm")

local fails, count = 0, 0
local function check(name, ok, extra)
    count = count + 1
    if not ok then fails = fails + 1; print("FAIL " .. name .. (extra and (": " .. extra) or "")) end
end

local function hex(s) return (s:gsub(".", function(c) return string.format("%02X ", c:byte()) end):gsub(" $", "")) end

local function asm(src)
    local res = A.assemble(src, { cpu = "6502" })
    if not res.ok then return nil, A.format_errors(res), res end
    local t = {}
    for _, s in ipairs(res.segments) do t[#t + 1] = hex(s.data) end
    return table.concat(t, " | "), nil, res
end

local function expect(src, want)
    local got, err = asm(src)
    check(src:gsub("\n", " / "), got == want, "got " .. tostring(got or err) .. ", want " .. want)
end

local function refuse(src, pattern)
    local got, err = asm(src)
    check("refused: " .. src:gsub("\n", " / "), got == nil and err:find(pattern, 1, true) ~= nil, tostring(got or err))
end

-- the official instructions
expect("lda #$10", "A9 10")
expect("lda $10", "A5 10")
expect("lda $1234", "AD 34 12")
expect("lda $10,x", "B5 10")
expect("lda $1234,x", "BD 34 12")
expect("lda $1234,y", "B9 34 12")
expect("lda ($20,x)", "A1 20")
expect("lda ($20),y", "B1 20")
expect("ldx $10,y", "B6 10")
expect("sta $2000", "8D 00 20")
expect("jmp ($fffc)", "6C FC FF")
expect("jsr $c000", "20 00 C0")
expect("asl", "0A")
expect("asl a", "0A")
expect("sei\ncld\ntxs\nrts\nrti", "78 D8 9A 60 40")
expect("loop: dex\nbne loop", "CA D0 FD")
expect("ldx #$ff", "A2 FF")                               -- the width never follows M or X

-- what the 6502 does not have
refuse("bra $8000", "not a 6502 instruction")
refuse("stz $10", "not a 6502 instruction")
refuse("phx", "not a 6502 instruction")
refuse("rep #$30", "not a 6502 instruction")
refuse("inc a", "not a 6502 instruction")
refuse("lda $123456", "not a 6502 instruction")
refuse("lda ($10)", "not a 6502 instruction")
refuse("lda $10,s", "not a 6502 instruction")
refuse("jml $018000", "not a 6502 instruction")
refuse(".org $10000", "16 bits")

-- the same lines are fine for the 65C816
check("65C816 still takes bra", A.assemble("bra $8000\nstz $10").ok)

-- the builder
local function build(src, h)
    local res = A.assemble(src, { cpu = "6502" })
    if not res.ok then return nil, A.format_errors(res) end
    return A.build_nes(res, h)
end

local prog = [[
.org $8000
reset:  sei
        cld
        ldx #$ff
        txs
forever: jmp forever
nmi:    rti
irq:    rti
]]
local rom, info = build(prog, { prg_kb = 32, chr_kb = 8, mapper = 0, vertical = true })
check("nes: builds", rom ~= nil, tostring(info))
if rom then
    check("nes: size", #rom == 16 + 32768 + 8192, tostring(#rom))
    check("nes: header", hex(rom:sub(1, 16)) == "4E 45 53 1A 02 01 01 00 00 00 00 00 00 00 00 00", hex(rom:sub(1, 16)))
    check("nes: code at $8000", hex(rom:sub(17, 22)) == "78 D8 A2 FF 9A 4C", hex(rom:sub(17, 22)))
    check("nes: padding", rom:byte(17 + 100) == 0xFF)
    local v = 16 + 0x7FFA
    -- reset = $8000, forever = $8005, nmi = $8008, irq = $8009
    check("nes: vectors", hex(rom:sub(v + 1, v + 6)) == "08 80 00 80 09 80", hex(rom:sub(v + 1, v + 6)))
    check("nes: info", info.prg_kb == 32 and info.chr_kb == 8 and info.mapper == 0)
end

-- a 16 KB PRG mirrors: the code at $C000 or $8000 is the same bytes
rom = build(prog, { prg_kb = 16, chr_kb = 8 })
check("nes 16 KB: size", rom ~= nil and #rom == 16 + 16384 + 8192)
check("nes 16 KB: vectors at the end", rom and hex(rom:sub(16 + 0x3FFA + 1, 16 + 0x3FFA + 6)) == "08 80 00 80 09 80")

-- mapper number, battery, CHR RAM, CHR ROM data
rom = build(prog, { prg_kb = 128, chr_kb = 0, mapper = 4, battery = true, wram_kb = 8 })
check("nes mmc3: header", rom and hex(rom:sub(1, 16)) == "4E 45 53 1A 08 00 42 00 01 00 00 00 00 00 00 00", rom and hex(rom:sub(1, 16)))
check("nes mmc3: the code is in the last 32 KB", rom and rom:byte(16 + 128 * 1024 - 0x8000 + 1) == 0x78)
rom = build(prog, { prg_kb = 32, chr_kb = 8, mapper = 69 })
check("nes mapper 69: flag bytes", rom and rom:byte(7) == 0x50 and rom:byte(8) == 0x40)
rom = build(prog, { prg_kb = 32, chr_kb = 8, chr = "\1\2\3" })
check("nes chr data", rom and rom:sub(16 + 32768 + 1, 16 + 32768 + 4) == "\1\2\3\0" and #rom == 16 + 32768 + 8192)

-- the user's own vectors
rom = build([[
.org $8000
start: jmp start
.org $fffa
.dw $1234, start, $5678
]], { prg_kb = 32, chr_kb = 8 })
check("nes: own vectors kept", rom and hex(rom:sub(16 + 0x7FFA + 1, 16 + 0x7FFA + 6)) == "34 12 00 80 78 56")

-- errors of the builder
local why
rom, why = build("nop", { prg_kb = 32 })
check("nes: no reset", rom == nil and why:find("reset", 1, true), tostring(why))
rom, why = build("reset: nop\n.org $4000\nnop", { prg_kb = 32 })
check("nes: code outside PRG", rom == nil and why:find("PRG", 1, true), tostring(why))
rom, why = build("reset: nop", { prg_kb = 20 })
check("nes: bad PRG size", rom == nil and why:find("16 KB", 1, true), tostring(why))
rom, why = build("reset: nop", { prg_kb = 32, chr_kb = 0, chr = "x" })
check("nes: chr data on a CHR RAM board", rom == nil and why:find("CHR RAM", 1, true), tostring(why))
rom, why = build("reset: nop\n.org $FF00\n.res 300, 0", { prg_kb = 32 })
check("nes: code past $FFFF", rom == nil and why:find("$FFFF", 1, true), tostring(why))
rom, why = build("reset: nop\n.org $C000\nnop", { prg_kb = 16 })
check("nes 16 KB: the mirror overlaps", rom == nil and why:find("overlaps", 1, true), tostring(why))

-- .chr blocks: the CHR ROM from the source
rom = build([[
.org $8000
reset: jmp reset
.chr
    .db $80,$00,1,2
    .res 12,0
.endchr
nmi: rti
.chr
    .db $FF
.endchr
]], { prg_kb = 32, chr_kb = 8, chr = "file" })
check("nes .chr: in the CHR ROM, before a file", rom and rom:sub(16 + 32768 + 1, 16 + 32768 + 4) == "\128\0\1\2"
      and rom:byte(16 + 32768 + 17) == 0xFF and #rom == 16 + 32768 + 8192)
check("nes .chr: not in the program", rom and rom:sub(17, 20) == "\76\0\128\64", rom and hex(rom:sub(17, 24)))
rom, why = build("reset: rts\n.chr\n.db 1\n.endchr", { prg_kb = 32, chr_kb = 0 })
check("nes .chr: a CHR RAM board", rom == nil and why:find("CHR RAM", 1, true), tostring(why))
rom, why = build("reset: rts\n.chr\n.res 9000, 1\n.endchr", { prg_kb = 32, chr_kb = 8 })
check("nes .chr: too large", rom == nil and why:find("larger", 1, true), tostring(why))
local r = A.assemble("reset: rts\n.chr\n.db 1\n  nop zz\n.endchr", { cpu = "6502" })
check("nes .chr: an error keeps its line", not r.ok and r.errors[1].line == 4, r.errors[1] and r.errors[1].line)
r = A.assemble("reset: rts\n.chr\n.db 1", { cpu = "6502" })
check("nes .chr: not closed", not r.ok and A.format_errors(r):find("not closed", 1, true))
r = A.assemble("reset: rts\n.endchr", { cpu = "6502" })
check("nes .chr: .endchr alone", not r.ok)

print(string.format("nes_asm_test: %d checks, %d failed", count, fails))
os.exit(fails == 0 and 0 or 1)
