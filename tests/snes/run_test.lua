-- run_test.lua: the helpers of the Run tab of bm SNES SDK, on the PC (core/lua/snesrun.lua), with the
-- assembler: the map from source lines to addresses (res.lines) and the breakpoints.
--   build/host/luahost tests/snes/run_test.lua [ROOT]     (make test-snes-run)
local ROOT = arg[1] or "."
package.path = ROOT .. "/core/src/script/?.lua;" .. package.path
local A = require "snes_asm"
local R = assert(loadfile(ROOT .. "/core/lua/snesrun.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

-- flags
check(R.flags_text(0xB1, false) == "N v M X d i z C", "flags native " .. R.flags_text(0xB1, false))
check(R.flags_text(0x34, true) == "n v - B d I z c", "flags emulation " .. R.flags_text(0x34, true))
check(#R.flags(0, false) == 8, "8 flags")
-- registers: X and Y follow the X flag
local t = R.regs_text({ a = 0x1234, x = 0x56, y = 0x78, s = 0x1FF, d = 0, db = 0x7E, pb = 0, pc = 0x8004, p = 0x30, e = false })
check(t[1] == "A 1234  X 56  Y 78", "8 bit regs: " .. t[1])
t = R.regs_text({ a = 0x1234, x = 0x5678, y = 0x9ABC, s = 0x1FF, d = 0, db = 0, pb = 0, pc = 0x8004, p = 0x00, e = false })
check(t[1] == "A 1234  X 5678  Y 9ABC", "16 bit regs: " .. t[1])
check(t[3]:find("PC 8004", 1, true) ~= nil, "pc")
check(R.flags_words(0x21, false) == 'A is 8 bit, carry', 'flag words ' .. R.flags_words(0x21, false))
check(R.flags_words(0, false) == 'no flag is set', 'no flag')
-- addresses
check(R.parse_addr("$7E0010") == 0x7E0010, "$ addr")
check(R.parse_addr("0x10") == 0x10, "0x addr")
check(R.parse_addr("7E:0010") == 0x7E0010, "bank:addr")
check(R.parse_addr("zz") == nil, "bad addr")
check(R.parse_addr("1234567") == nil, "too long")
local a, b = R.parse_poke("7E0010=12 34")
check(a == 0x7E0010 and #b == 2 and b[2] == 0x34, "poke 2 bytes")
a, b = R.parse_poke("$10 FF")
check(a == 0x10 and b[1] == 255, "poke with a space")
check(R.parse_poke("10=") == nil, "poke no byte")
check(R.parse_poke("10=123") == nil, "poke bad byte")
-- hex view
local d = R.hexdump("AB\0\xFF", 0x7E0000, 4)
check(d[1] == "7E0000  41 42 00 FF |AB..|", "hexdump " .. d[1])
d = R.hexdump("A", 0x10, 4)
check(d[1] == "0010  41          |A|", "hexdump short: " .. d[1])
-- read through a fake debug API
local api = { peek = function(a, n) return string.rep("p", n) end, mem = function(s, a, n) return s .. n end }
check(R.read("ram", 0x10, 4, api) == "pppp", "read ram")
check(R.read("vram", 0, 16, api) == "vram16", "read vram")
check(R.read("oam", 540, 16, api) == "oam4", "read oam is cut at the end")
check(R.read("cgram", 512, 1, api) == nil, "read outside")
-- breakpoints
local set = {}
check(R.toggle(set, 0x8000) == true and R.toggle(set, 0x8000) == false, "toggle")
for i = 1, 8 do R.toggle(set, 0x8000 + i) end
local ok, why = R.toggle(set, 0x9000)
check(ok == nil and why:find("8"), "the 9th breakpoint is refused")
check(R.list(set)[1] == 0x8001, "sorted list")

-- the line map of the assembler
local src = [[
.lorom
.org $8000
reset:
    sei
    clc
    xce
    rep #$30
; a comment line
    lda #$1234
    sta $0300
loop:
    bra loop
]]
local res = A.assemble(src, { name = "main.s" })
check(res.ok, "assembles")
check(#res.lines == 7, "7 instruction lines, got " .. #res.lines)
check(R.addr_of_line(res.lines, "main.s", 4) == 0x8000, "sei at $8000")
check(R.addr_of_line(res.lines, "main.s", 7) == 0x8003, "rep at $8003")
check(R.addr_of_line(res.lines, "main.s", 8) == 0x8005, "a comment line takes the next instruction")
check(R.addr_of_line(res.lines, "main.s", 9) == 0x8005, "lda")
check(R.addr_of_line(res.lines, "main.s", 10) == 0x8008, "sta after a 16 bit lda")
check(R.addr_of_line(res.lines, "main.s", 99) == nil, "no line after the last")
check(R.addr_of_line(res.lines, "other.s", 1) == nil, "another file")
local f, l = R.line_of_addr(res.lines, 0x8008)
check(f == "main.s" and l == 10, "line of address")
check(R.line_of_addr(res.lines, 0x808000 | 0x8008) == "main.s", "a mirror of the bank finds the line")

io.write(string.format("snes run: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
