-- sdk_dis_test.lua: the ASM listing of the Code tab of Shell SDK (core/src/script/sdk_dis.lua), NES and SNES.
-- The listing of the NES start file goes back through the assembler and gives the bytes of the ROM again.
--   build/host/luahost tests/snes/sdk_dis_test.lua        (make test-sdk-templates)
package.path = "core/src/script/?.lua;core/lua/?.lua;" .. package.path
local A = require("snes_asm")
local R = require("snesrom")
local D = require("sdk_dis")

local fails = 0
local function check(name, ok, extra)
    if not ok then fails = fails + 1; print("FAIL " .. name .. (extra and (": " .. tostring(extra)) or "")) end
end

local function read(path)
    local f = assert(io.open(path, "rb"))
    local d = f:read("a")
    f:close()
    return d
end

local function has(text, line) return ("\n" .. text .. "\n"):find("\n" .. line .. "\n", 1, true) ~= nil end

-- the listing as a source again: each instruction and data line at its address
local function round_trip(name, text, prg, base)
    local src, labels, n = {}, {}, 0
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        local label = line:match("^([%a_][%w_]*):$")
        local addr, ins = line:match("^(%x%x%x%x)  (.-)%s+%x%x[ %x]*$")
        if not addr then addr, ins = line:match("^(%x%x%x%x)  (%.db %S+)$") end
        if label then
            labels[#labels + 1] = label .. ":"
        elseif addr then
            src[#src + 1] = "    .org $" .. addr
            for _, l in ipairs(labels) do src[#src + 1] = l end
            labels = {}
            src[#src + 1] = "    " .. ins
            n = n + 1
        end
    end
    local res = A.assemble(table.concat(src, "\n") .. "\n", { cpu = "6502" })
    check(name .. ": the listing assembles", res.ok, not res.ok and A.format_errors(res))
    if not res.ok then return 0 end
    for _, s in ipairs(res.segments) do
        local want = prg:sub(s.addr - base + 1, s.addr - base + #s.data)
        check(name .. ": the bytes of the ROM again", s.data == want, string.format("at $%04X", s.addr))
        if s.data ~= want then break end
    end
    return n
end

-- the NES start file: 32 KB of PRG
local res = A.assemble(read("core/src/script/sdk_tpl_nes.s"), { cpu = "6502" })
check("nes template assembles", res.ok)
local rom = assert(A.build_nes(res, { prg_kb = 32, chr_kb = 8, mapper = 0 }))
local text, why = D.listing(A, R, rom)
check("nes listing", text ~= nil, why)
if text then
    check("nes: reset label", has(text, "reset:"), text:sub(1, 200))
    check("nes: nmi label", has(text, "nmi:"))
    check("nes: the first instruction", text:find("%x%x%x%x  sei%s+78\n") ~= nil, text:sub(1, 200))
    check("nes: the empty end is one line", text:find("\n%x%x%x%x  ; %d+ bytes of %$FF\n") ~= nil)
    check("nes: the vectors", text:find("\nFFFC  %.dw %$%x%x%x%x%s+reset\n") ~= nil, text:sub(-200))
    check("nes: short", select(2, text:gsub("\n", "")) < 600)
    check("nes: many instructions come back", round_trip("nes template", text, rom:sub(17, 16 + 32768), 0x8000) > 30)
end

-- a made ROM: 16 KB of PRG seen at $C000, a byte that is no instruction, an nmi inside what looks like one
local function made(code, nmi, reset, mapper)
    local prg = code .. string.rep("\xFF", 16384 - #code - 6)
    local function w(v) return string.char(v & 0xFF, v >> 8) end
    prg = prg .. w(nmi) .. w(reset) .. w(reset)
    return "NES\x1A" .. string.char(1, 0, (mapper or 0) << 4, 0) .. string.rep("\0", 8) .. prg, prg
end
local code = "\xA9\x01"              -- C000 lda #$01
    .. "\x8D\x00\x20"                -- C002 sta $2000
    .. "\xD0\xF9"                    -- C005 bne C000
    .. "\x02"                        -- C007 no instruction
    .. "\x4C\x00\xC0"                -- C008 jmp C000
    .. "\xAD"                        -- C00B looks like lda abs, but the nmi starts at C00C
    .. "\x40"                        -- C00C rti
    .. string.rep("\0", 20)          -- C00D a run of zeros
    .. "\xA5\x10\x0A\x60"            -- C021 lda $10, asl a, rts
local prg
rom, prg = made(code, 0xC00C, 0xC000)
text, why = D.listing(A, R, rom)
check("made listing", text ~= nil, why)
if text then
    check("made: lda", has(text, "C000  lda #$01          A9 01"), text)
    check("made: a branch to a label", has(text, "C005  bne reset         D0 F9"), text)
    check("made: no instruction is data", has(text, "C007  .db $02"), text)
    check("made: jmp to a label", has(text, "C008  jmp reset         4C 00 C0"), text)
    check("made: the entry point starts an instruction", has(text, "C00B  .db $AD") and has(text, "nmi:"), text)
    check("made: rti", has(text, "C00C  rti               40"), text)
    check("made: a run is one line", has(text, "C00D  ; 20 bytes of $00"), text)
    check("made: accumulator", has(text, "C023  asl a             0A"), text)
    check("made: vectors of a 16 KB ROM", has(text, "FFFA  .dw $C00C         nmi")
          and has(text, "FFFC  .dw $C000         reset"), text)
    round_trip("made", text, prg, 0xC000)
end

-- random bytes as code: every listing assembles and gives the bytes again
math.randomseed(2026)
for k = 1, 4 do
    local b = {}
    for i = 1, 3000 do b[i] = string.char(math.random(0, 255)) end
    rom, prg = made(table.concat(b), 0xC100, 0xC000)
    text, why = D.listing(A, R, rom)
    check("random listing " .. k, text ~= nil, why)
    if text then check("random " .. k .. ": instructions", round_trip("random " .. k, text, prg, 0xC000) > 800) end
    local s = {}
    for i = 1, 0x8000 do s[i] = string.char(math.random(0, 255)) end
    s = table.concat(s)
    local head = "RANDOM".. string.rep(" ", 15) .. "\x20\0\x05\0\x01\0\0\xFF\xFF\0\0"
    s = s:sub(1, 0x7FC0) .. head .. s:sub(0x7FC0 + #head + 1, 0x7FFC) .. "\0\x80" .. s:sub(0x7FFF)
    text, why = D.listing(A, R, s)
    check("random snes listing " .. k, text ~= nil and text:find("\n8000  ") ~= nil, why)
end

text, why = D.listing(A, R, (made(code, 0xC00C, 0xC000, 1)))
check("mapper 1 is refused", text == nil and tostring(why):find("mapper 0") ~= nil, why)
text, why = D.listing(A, R, "NES\x1A")
check("a cut file is refused", text == nil, why)
text, why = D.listing(A, R, "hello")
check("a small file is refused", text == nil, why)

-- the SNES start file
res = A.assemble(read("core/src/script/sdk_tpl_snes.s"), { map = "lorom" })
check("snes template assembles", res.ok)
rom = assert(A.build_rom(res, { title = "TEMPLATE", map = "lorom", min_kb = 32, chipset = 0 }))
text, why = D.listing(A, R, rom)
check("snes listing", text ~= nil, why)
if text then
    check("snes: reset label first", text:sub(1, 7) == "reset:\n", text:sub(1, 200))
    check("snes: the first instruction", text:find("\n8000  sei%s+78\n") ~= nil, text:sub(1, 200))
    check("snes: xce", text:find("  xce%s+FB\n") ~= nil)
    check("snes: the widths show", text:find("\n      %.a16\n") ~= nil)
    check("snes: the empty end is one line", text:find("\n%x%x%x%x  ; %d+ bytes of %$%x%x$") ~= nil, text:sub(-200))
    check("snes: short", select(2, text:gsub("\n", "")) < 600)
end

print(fails == 0 and "sdk_dis_test: ok" or ("sdk_dis_test: " .. fails .. " FAILED"))
os.exit(fails == 0 and 0 or 1)
