-- asm_test.lua: the tests of the 65C816 assembler and the ROM builder (core/src/script/snes_asm.lua).
-- Run from the repository root:  build/host/luahost tests/snes/asm_test.lua   (make test-snes-asm)
package.path = "core/src/script/?.lua;" .. package.path
local A = require("snes_asm")

local fails, count = 0, 0
local function check(name, ok, extra)
    count = count + 1
    if not ok then fails = fails + 1; print("FAIL " .. name .. (extra and (": " .. extra) or "")) end
end

local function hex(s) return (s:gsub(".", function(c) return string.format("%02X ", c:byte()) end):gsub(" $", "")) end

-- Assemble a source at $8000; return the bytes of the first segment as hex, or nil and the error text.
local function asm(src, opts)
    local res = A.assemble(src, opts)
    if not res.ok then return nil, A.format_errors(res), res end
    local t = {}
    for _, s in ipairs(res.segments) do t[#t + 1] = hex(s.data) end
    return table.concat(t, " | "), nil, res
end

local function expect(src, want, opts)
    local got, err = asm(src, opts)
    check(src:gsub("\n", " / "), got == want, "got " .. tostring(got or err) .. ", want " .. want)
end

local function expect_err(src, pattern, line, opts)
    local res = A.assemble(src, opts)
    local e = res.errors[1]
    check("error: " .. src:gsub("\n", " / "), not res.ok and e and e.msg:find(pattern, 1, true) and (line == nil or e.line == line),
          e and (e.file .. ":" .. e.line .. ": " .. e.msg) or "no error")
end

-- ---------------------------------------------------------------------------------------------------
-- 1. All 256 opcodes, in the order of the opcode map. The operand letters: D direct page, A absolute,
--    L long, I immediate, R relative. The expected first byte is the index; the expected length follows
--    from the operand.
-- ---------------------------------------------------------------------------------------------------
local MAP = {
 "brk I|ora (D,x)|cop I|ora D,s|tsb D|ora D|asl D|ora [D]|php|ora I|asl a|phd|tsb A|ora A|asl A|ora L",
 "bpl R|ora (D),y|ora (D)|ora (D,s),y|trb D|ora D,x|asl D,x|ora [D],y|clc|ora A,y|inc a|tcs|trb A|ora A,x|asl A,x|ora L,x",
 "jsr A|and (D,x)|jsl L|and D,s|bit D|and D|rol D|and [D]|plp|and I|rol a|pld|bit A|and A|rol A|and L",
 "bmi R|and (D),y|and (D)|and (D,s),y|bit D,x|and D,x|rol D,x|and [D],y|sec|and A,y|dec a|tsc|bit A,x|and A,x|rol A,x|and L,x",
 "rti|eor (D,x)|wdm I|eor D,s|mvp D,D|eor D|lsr D|eor [D]|pha|eor I|lsr a|phk|jmp A|eor A|lsr A|eor L",
 "bvc R|eor (D),y|eor (D)|eor (D,s),y|mvn D,D|eor D,x|lsr D,x|eor [D],y|cli|eor A,y|phy|tcd|jml L|eor A,x|lsr A,x|eor L,x",
 "rts|adc (D,x)|per R|adc D,s|stz D|adc D|ror D|adc [D]|pla|adc I|ror a|rtl|jmp (A)|adc A|ror A|adc L",
 "bvs R|adc (D),y|adc (D)|adc (D,s),y|stz D,x|adc D,x|ror D,x|adc [D],y|sei|adc A,y|ply|tdc|jmp (A,x)|adc A,x|ror A,x|adc L,x",
 "bra R|sta (D,x)|brl R|sta D,s|sty D|sta D|stx D|sta [D]|dey|bit I|txa|phb|sty A|sta A|stx A|sta L",
 "bcc R|sta (D),y|sta (D)|sta (D,s),y|sty D,x|sta D,x|stx D,y|sta [D],y|tya|sta A,y|txs|txy|stz A|sta A,x|stz A,x|sta L,x",
 "ldy I|lda (D,x)|ldx I|lda D,s|ldy D|lda D|ldx D|lda [D]|tay|lda I|tax|plb|ldy A|lda A|ldx A|lda L",
 "bcs R|lda (D),y|lda (D)|lda (D,s),y|ldy D,x|lda D,x|ldx D,y|lda [D],y|clv|lda A,y|tsx|tyx|ldy A,x|lda A,x|ldx A,y|lda L,x",
 "cpy I|cmp (D,x)|rep I|cmp D,s|cpy D|cmp D|dec D|cmp [D]|iny|cmp I|dex|wai|cpy A|cmp A|dec A|cmp L",
 "bne R|cmp (D),y|cmp (D)|cmp (D,s),y|pei (D)|cmp D,x|dec D,x|cmp [D],y|cld|cmp A,y|phx|stp|jml [A]|cmp A,x|dec A,x|cmp L,x",
 "cpx I|sbc (D,x)|sep I|sbc D,s|cpx D|sbc D|inc D|sbc [D]|inx|sbc I|nop|xba|cpx A|sbc A|inc A|sbc L",
 "beq R|sbc (D),y|sbc (D)|sbc (D,s),y|pea A|sbc D,x|inc D,x|sbc [D],y|sed|sbc A,y|plx|xce|jsr (A,x)|sbc A,x|inc A,x|sbc L,x",
}
do
    local seen = {}
    for row = 0, 15 do
        local cells = {}
        for c in (MAP[row + 1] .. "|"):gmatch("([^|]*)|") do cells[#cells + 1] = c end
        check("opcode map row " .. row .. " has 16 cells", #cells == 16, #cells)
        for col = 0, 15 do
            local op = row * 16 + col
            local cell = cells[col + 1] or ""
            local mn, operand = cell:match("^(%S+)%s*(.*)$")
            -- 16-bit-wide immediates are not used here: A and X are 8 bits (the start state)
            local len = 1
            local src
            local o = operand
            if o ~= "" then
                if o == "a" then len = 1
                else
                    local pat = o:gsub("[ILADR]", { I = "#$12", L = "$123456", A = "$1234", D = "$12", R = "$8010" })
                    o = pat
                    if operand:find("^%(A%)$") or operand:find("^%(A,x%)$") or operand == "[A]" or operand == "A" or operand:find("^A,") then len = 3
                    elseif operand:find("L") then len = 4
                    elseif operand == "D,D" then len = 3
                    elseif mn == "brl" or mn == "per" then len = 3
                    else len = 2 end
                end
            end
            if mn == "pea" then len = 3 end
            if mn == "mvn" or mn == "mvp" then len = 3 end
            src = mn .. (o ~= "" and (" " .. o) or "")
            local res = A.assemble(".org $8000\n" .. src .. "\n")
            local ok = res.ok and res.segments[1] and #res.segments[1].data == len and res.segments[1].data:byte(1) == op
            check(string.format("opcode $%02X %s", op, src), ok,
                  res.ok and res.segments[1] and hex(res.segments[1].data) or A.format_errors(res))
            seen[op] = true
        end
    end
    local n = 0
    for _ in pairs(seen) do n = n + 1 end
    check("256 opcodes tested", n == 256)
end

-- ---------------------------------------------------------------------------------------------------
-- 2. Addressing modes: operand bytes, size choice, forced sizes, prefix operators
-- ---------------------------------------------------------------------------------------------------
expect("lda #$12", "A9 12")
expect("lda $12", "A5 12")
expect("lda $1234", "AD 34 12")
expect("lda $123456", "AF 56 34 12")
expect("lda $12,x", "B5 12")
expect("lda $1234,x", "BD 34 12")
expect("lda $123456,x", "BF 56 34 12")
expect("lda $1234,y", "B9 34 12")
expect("lda ($12,x)", "A1 12")
expect("lda ($12),y", "B1 12")
expect("lda ($12)", "B2 12")
expect("lda [$12]", "A7 12")
expect("lda [$12],y", "B7 12")
expect("lda $12,s", "A3 12")
expect("lda ($12,s),y", "B3 12")
expect("ldx $12,y", "B6 12")
expect("ldx $1234,y", "BE 34 12")
expect("stx $12,y", "96 12")
expect("jmp ($1234)", "6C 34 12")
expect("jmp ($1234,x)", "7C 34 12")
expect("jmp [$1234]", "DC 34 12")
expect("jml [$1234]", "DC 34 12")
expect("jml $123456", "5C 56 34 12")
expect("jsr ($1234,x)", "FC 34 12")
expect("pei ($12)", "D4 12")
expect("pea $1234", "F4 34 12")
expect("pea #$1234", "F4 34 12")
expect("mvn $7E,$7F", "54 7F 7E")             -- the source first, the destination in the opcode's second byte
expect("mvp $01,$02", "44 02 01")
expect("brk", "00 00")
expect("brk #$05", "00 05")
expect("cop #$12", "02 12")
expect("wdm #$01", "42 01")
expect("asl", "0A")
expect("asl a", "0A")
expect("ina\ndea", "1A 3A")
expect("tad\ntda\ntas\ntsa\nswa", "5B 7B 1B 3B EB")
-- forced sizes
expect("lda a:$12", "AD 12 00")
expect("lda z:$12", "A5 12")
expect("lda f:$12", "AF 12 00 00")
expect("lda.w $12", "AD 12 00")
expect("lda.b $12", "A5 12")
expect("lda.l $12", "AF 12 00 00")
expect("lda a:$123456", "AD 56 34")             -- a forced absolute keeps the low 16 bits
expect("jmp f:$8000", "5C 00 80 00")
-- the choice by value, and the in-bank rule of jmp and jsr
expect("jmp $808000", "5C 00 80 80")             -- bank 0 code: $808000 is another bank, then long
-- branches
expect(".org $8000\nbra $8010", "80 0E")
expect(".org $8000\nbra $8000", "80 FE")
expect(".org $8000\nbne skip\nnop\nskip: nop", "D0 01 EA EA")
expect(".org $8000\nbrl $8003+$100", "82 00 01")
expect(".org $8000\nper $8010", "62 0D 00")
expect(".org $8010\nbra back\nback:", "80 00")
-- the accumulator-width immediates follow M, the index ones follow X
expect("lda #1\nldx #1\ncmp #1\ncpy #1", "A9 01 A2 01 C9 01 C0 01")
expect(".a16\nlda #$1234\nldx #1", "A9 34 12 A2 01")
expect(".i16\nlda #1\nldx #$1234\nldy #2\ncpx #3", "A9 01 A2 34 12 A0 02 00 E0 03 00")
expect(".a16\n.i16\nadc #1\nsbc #2\nand #3\neor #4\nora #5\nbit #6\ncmp #7", "69 01 00 E9 02 00 29 03 00 49 04 00 09 05 00 89 06 00 C9 07 00")
expect(".a16\nrep #$20\nsep #$20\nlda #1", "C2 20 E2 20 A9 01")
expect("rep #$30\nlda #$1234\nldx #$5678\nsep #$30\nlda #1\nldx #2", "C2 30 A9 34 12 A2 78 56 E2 30 A9 01 A2 02")
expect("rep #$20\nlda #1\nsep #$10\nldx #2\nlda #3", "C2 20 A9 01 00 E2 10 A2 02 A9 03 00")
expect("rep #$10\nldx #1\nrep #$20\nsep #$30\nldx #2", "C2 10 A2 01 00 C2 20 E2 30 A2 02")
expect("lda #-1", "A9 FF")
expect("lda #255", "A9 FF")
-- 3. prefix operators and expressions
expect("lda #<$1234\nlda #>$1234\nlda #^$123456", "A9 34 A9 12 A9 12")
expect(".org $8000\nlda #<lbl\nlda #>lbl\nlda #^lbl\nlbl:", "A9 06 A9 80 A9 00")
expect(".org $018000\nlda #^lbl\nlbl:", "A9 01")
expect("lda #<($1234+1)", "A9 35")
expect("lda #<$1234+1", "A9 35")                  -- < takes the whole expression after it
expect("lda #1+2*3", "A9 07")
expect("lda #(1+2)*3", "A9 09")
expect("lda #$10|$01", "A9 11")
expect("lda #$FF&$0F", "A9 0F")
expect("lda #$F0~$FF", "A9 0F")                  -- ~ is xor between two values
expect("lda #~$FE&$FF", "A9 01")                 -- and not before one
expect("lda #1<<4", "A9 10")
expect("lda #$80>>3", "A9 10")
expect("lda #10%4", "A9 02")
expect("lda #17/4", "A9 04")
expect("lda #%1010", "A9 0A")
expect("lda #0x1F", "A9 1F")
expect("lda #'A'", "A9 41")
expect("lda #(3>2)+(2>3)+(2==2)+(1!=1)", "A9 02")
expect("lda #!0+!5", "A9 01")
expect("lda #(1&&0)||(2&&3)", "A9 01")
expect("x = 5\nlda #x*2", "A9 0A")
expect("lda #later\nlater = 3", "A9 03")
expect("size = end - start\n.org $8000\nstart: nop\nnop\nend:\nlda #size", "EA EA A9 02")
expect(".org $8000\nlda #*-$8000", "A9 00")
expect(".org $8000\nnop\nlda #*&$FF", "EA A9 01")

-- ---------------------------------------------------------------------------------------------------
-- 4. Labels, local labels, forward references, the size of a forward reference
-- ---------------------------------------------------------------------------------------------------
do
    local got, err, res = asm(".org $8000\nstart: lda #1\nloop: dex\nbne loop\nrts\nfar: nop")
    check("labels: bytes", got == "A9 01 CA D0 FD 60 EA", got or err)
    check("labels: values", res and res.symbols.start == 0x8000 and res.symbols.loop == 0x8002 and res.symbols.far == 0x8006)
end
do
    local got, err, res = asm(".org $8000\nfirst:\n@l: nop\nbra @l\nsecond:\n@l: nop\nbra @l\n")
    check("local labels: each global label has its own", got == "EA 80 FD EA 80 FD", got or err)
    check("local labels: in the symbol table", res and res.symbols["first@l"] == 0x8000 and res.symbols["second@l"] == 0x8003)
end
-- a forward reference is absolute; a known direct-page value is direct page
expect(".org $8000\nlda fwd\nfwd = $10", "AD 10 00")
expect("fwd = $10\nlda fwd", "A5 10")
expect(".org $8000\nlda z:fwd\nfwd = $10", "A5 10")
expect(".org $8000\njmp fwd\nfwd: nop", "4C 03 80 EA")
expect(".org $8000\njsr fwd\nfwd: rts", "20 03 80 60")
expect(".org $808000\njmp fwd\nfwd: nop", "4C 03 80 EA")              -- in-bank: absolute
expect(".org $808000\njmp $818000", "5C 00 80 81")                       -- another bank: long
expect(".org $808000\njml fwd\nfwd: nop", "5C 04 80 80 EA")
expect("lda $7E0010", "AF 10 00 7E")
expect("lda $7E0010,x", "BF 10 00 7E")
expect("sta $808000", "8F 00 80 80")

-- ---------------------------------------------------------------------------------------------------
-- 5. Directives
-- ---------------------------------------------------------------------------------------------------
expect(".byte 1,2,$FF,-1", "01 02 FF FF")
expect(".byte \"Hi\",0", "48 69 00")
expect(".byte \"a\\n\\\"\",'b'", "61 0A 22 62")
expect(".word $1234,-2", "34 12 FE FF")
expect(".long $123456", "56 34 12")
expect(".dword $12345678", "78 56 34 12")
expect(".org $8000\nlbl: .word lbl, lbl+1", "00 80 01 80")
expect(".org $8000\n.long lbl\nlbl:", "03 80 00")
expect(".res 3,$AA", "AA AA AA")
expect(".org $8000\nnop\n.res 4\nnop", "EA | EA")                             -- the gap is its own segment
expect(".org $8000\nnop\n.align 4\nnop", "EA 00 00 00 EA")
expect(".org $8000\nnop\n.align 4,$FF\nnop", "EA FF FF FF EA")
expect(".org $8000\nnop\n.org $8010\nnop", "EA | EA")
expect(".db 1\n.dw 2\n.dl 3\n.dd 4\n.ds 1,9", "01 02 00 03 00 00 04 00 00 00 09")
expect("; a comment\nnop ; another\n  \n", "EA")
expect(".byte \";\"", "3B")
expect("one: two: three: nop", "EA")
expect("LDA #1\nNop\n.BYTE 2", "A9 01 EA 02")                 -- mnemonics and directives are case-insensitive
do
    local files = { ["inc.s"] = "val = 7\n.byte val\n", ["blob.bin"] = "\1\2\3\4\5" }
    local read = function(p) return files[p] end
    expect(".include \"inc.s\"\n.byte val+1", "07 08", { read = read })
    expect(".incbin \"blob.bin\"", "01 02 03 04 05", { read = read })
    expect(".incbin \"blob.bin\",1,2", "02 03", { read = read })
    expect(".incbin \"blob.bin\",3", "04 05", { read = read })
    expect_err(".incbin \"nope.bin\"", "cannot read nope.bin", 1, { read = read })
    expect_err(".incbin \"blob.bin\",4,9", "outside the file", 1, { read = read })
    expect_err(".include \"nope.s\"", "cannot read nope.s", 1, { read = read })
    -- an error in an included file names that file and its line
    files["bad.s"] = "nop\nbogus\n"
    local res = A.assemble(".include \"bad.s\"\n", { name = "main.s", read = read })
    check("errors: the included file and line", not res.ok and res.errors[1].file == "bad.s" and res.errors[1].line == 2, A.format_errors(res))
    -- the line after the include goes back to the main file
    files["ok.s"] = "nop\n"
    res = A.assemble(".include \"ok.s\"\nbogus2\n", { name = "main.s", read = read })
    check("errors: back in the main file", not res.ok and res.errors[1].file == "main.s" and res.errors[1].line == 2, A.format_errors(res))
end

-- ---------------------------------------------------------------------------------------------------
-- 6. Macros, conditions, repeat
-- ---------------------------------------------------------------------------------------------------
expect(".macro two\nnop\nnop\n.endmacro\ntwo\ntwo", "EA EA EA EA")
expect(".macro ld16 reg, v\nl\\@: lda #v\n.endmacro\nld16 a, 1\nld16 a, 2", "A9 01 A9 02")
expect(".macro setx v\nldx #v\n.endmacro\nsetx 5\nsetx $10+1", "A2 05 A2 11")
expect(".macro loop n\nldx #n\n@w: dex\nbne @w\n.endmacro\nentry:\nloop 3", "A2 03 CA D0 FD")
expect(".macro pair a, b\nlda #a\nlda #b\n.endmacro\npair 1,2", "A9 01 A9 02")
expect(".macro outer\ninner\n.endmacro\n.macro inner\nnop\n.endmacro\nouter", "EA")
expect(".if 1\nnop\n.else\nrts\n.endif", "EA")
expect(".if 0\nnop\n.else\nrts\n.endif", "60")
expect("x = 3\n.if x > 2\nnop\n.endif\n.if x > 5\nrts\n.endif", "EA")
expect(".ifdef x\nnop\n.else\nrts\n.endif\nx = 1\n.ifdef x\nbrk\n.endif", "60 00 00")
expect(".ifndef y\nnop\n.endif", "EA")
expect(".if 1\n.if 0\nnop\n.else\nrts\n.endif\n.else\nbrk\n.endif", "60")
expect(".if 0\n.if 1\nnop\n.endif\n.else\nrts\n.endif", "60")
expect(".rept 3\nnop\n.endr", "EA EA EA")
expect(".rept 2\n.rept 2\ninx\n.endr\niny\n.endr", "E8 E8 C8 E8 E8 C8")
expect(".rept 0\nnop\n.endr", "")

-- ---------------------------------------------------------------------------------------------------
-- 7. Errors: the message and the line
-- ---------------------------------------------------------------------------------------------------
expect_err("nop\nbogus", "unknown instruction", 2)
expect_err("lda", "needs an operand", 1)
expect_err("nop $12", "takes no operand", 1)
expect_err("lda #$100", "does not fit", 1)
expect_err("lda #-129", "does not fit", 1)
expect_err("ldx #$10000\n", "does not fit", 1)
expect_err("sta #1", "no immediate form", 1)
expect("lda $12,y", "B9 12 00")
expect_err("ldx $12,x", "no such addressing mode", 1)
expect_err("jmp $12,x", "no such addressing mode", 1)
expect_err("lda ($1234)", "does not fit", 1)
expect_err("lda $1000000", "does not fit", 1)
expect_err("lda -1", "does not fit", 1)
expect_err("lda z:$1234", "does not fit", 1)
expect_err("lda z:$123456,y", "no direct page form", 1)
expect_err("lda missing", "missing is not defined", 1)
expect_err("lda #missing", "missing is not defined", 1)
expect_err("dup: nop\ndup: nop", "already defined", 2)
expect_err("x = 1\nx = 2", "already defined", 2)
expect_err("a = b\nb = a\nlda #a", "refers to itself", 3)
expect_err(".org $8000\nbra $9000", "too far", 2)
expect_err(".org $8000\nbra $018000", "another bank", 2)
expect_err(".org $8000\nbeq missing", "missing is not defined", 2)
expect_err("rep #lbl\nlbl:", "needs a constant", 1)
expect_err("lda (", "", 1)
expect_err("lda #1+", "ends too early", 1)
expect_err("lda #(1+2", "a ) is missing", 1)
expect_err("lda #1 2", "after the expression", 1)
expect_err("lda #$", "bad hex", 1)
expect_err("lda #1/0", "division by zero", 1)
expect_err("lda #12ab", "bad number", 1)
expect_err("lda #1 @", "unexpected character", 1)
expect_err(".byte 256", "does not fit", 1)
expect_err(".byte 1,,2", "empty value", 1)
expect_err(".word 70000", "does not fit", 1)
expect_err(".word \"ab\"", "for .byte only", 1)
expect_err(".byte \"abc", "closing quote", 1)
expect_err(".bogus", "unknown directive", 1)
expect_err(".org lbl\nlbl:", "must be known", 1)
expect_err(".res -1", "negative", 1)
expect_err(".align 0", "positive", 1)
expect_err(".macro m\nnop\n", "not closed", nil)
expect_err(".if 1\nnop\n", "not closed", nil)
expect_err(".else", ".else without .if", 1)
expect_err(".endif", ".endif without .if", 1)
expect_err(".macro m a\nnop\n.endmacro\nm 1,2", "takes 1 arguments", 4)
expect_err(".macro m\nm\n.endmacro\nm", "nested too deep", nil)
expect_err(".error \"stop here\"", "stop here", 1)
expect_err("mvn $01", "needs a source bank and a destination bank", 1)
expect_err("lda $01,$02", "takes one operand", 1)
expect_err("brk $01", "# signature", 1)
expect_err("brk #$100", "signature byte", 1)
expect_err("jsr [$1234]", "no such addressing mode", 1)
-- an error inside a macro names the macro and the call line
do
    local res = A.assemble("nop\n.macro m\nbogus\n.endmacro\nm\n")
    local e = res.errors[1]
    check("errors: macro call line and macro name", e and e.line == 5 and e.msg:find("in macro m", 1, true), e and (e.line .. " " .. e.msg))
end
-- more than one error: all are reported
do
    local res = A.assemble("bogus1\nnop\nbogus2\nlda\n")
    check("errors: all pass 1 errors reported", #res.errors == 3, #res.errors)
    check("errors: format", A.format_errors(res):find("main.s:1: unknown instruction", 1, true) ~= nil)
end
-- errors of pass 2 come with their line
expect_err("nop\nlda #1\nlda #later\nlater = 300", "does not fit", 3)

-- ---------------------------------------------------------------------------------------------------
-- 8. Symbol table output
-- ---------------------------------------------------------------------------------------------------
do
    local _, _, res = asm(".org $018000\nstart: nop\nvalue = $42\n@x: nop\nend2: rts")
    local t = A.symbol_table(res)
    check("symbols: label format", t:find("$01:8000 start", 1, true) ~= nil, t)
    check("symbols: constant format", t:find("= $42 value", 1, true) ~= nil, t)
    check("symbols: local label", t:find("$01:8001 start@x", 1, true) ~= nil, t)
    check("symbols: sorted by value", t:find("= $42 value") < t:find("$01:8000 start"), t)
end

-- ---------------------------------------------------------------------------------------------------
-- 9. The ROM builder
-- ---------------------------------------------------------------------------------------------------
local function rom_of(src, h, opts)
    local rom, res, info = A.build(src, opts or {}, h)
    if not rom then return nil, res end
    return rom, info
end
local function u16(s, off) return s:byte(off + 1) | (s:byte(off + 2) << 8) end
local PROG = ".lorom\n.org $8000\nreset: sei\nclc\nxce\nloop: wai\nbra loop\nnmi: rti\nirq: rti\n"
do
    local rom, info = rom_of(PROG, { title = "my game", region = 2, version = 3, dev_id = 0x33 })
    check("builder: builds", rom ~= nil, info)
    if rom then
        check("builder: LoROM size 32 KB", #rom == 0x8000 and info.size == 0x8000)
        check("builder: title", rom:sub(0x7FC1, 0x7FC7) == "MY GAME" and rom:sub(0x7FC8, 0x7FD5) == string.rep(" ", 14))
        check("builder: map mode LoROM", rom:byte(0x7FD6) == 0x20)
        check("builder: ROM only", rom:byte(0x7FD7) == 0x00)
        check("builder: size code 32 KB", rom:byte(0x7FD8) == 0x05)
        check("builder: no SRAM", rom:byte(0x7FD9) == 0x00)
        check("builder: region", rom:byte(0x7FDA) == 2)
        check("builder: dev id", rom:byte(0x7FDB) == 0x33)
        check("builder: version", rom:byte(0x7FDC) == 3)
        check("builder: reset vector", u16(rom, 0x7FFC) == 0x8000)
        check("builder: emulation NMI", u16(rom, 0x7FFA) == 0x8006)
        check("builder: native NMI", u16(rom, 0x7FEA) == 0x8006)
        check("builder: IRQ", u16(rom, 0x7FEE) == 0x8007 and u16(rom, 0x7FFE) == 0x8007)
        check("builder: missing vectors point at the RTI stub", u16(rom, 0x7FE4) == 0xFFE0 and u16(rom, 0x7FF4) == 0xFFE0 and rom:byte(0x7FE1) == 0x40)
        local sum = 0
        for i = 1, #rom do sum = sum + rom:byte(i) end
        sum = sum & 0xFFFF
        check("builder: checksum equals the sum of the ROM", u16(rom, 0x7FDE) == sum and info.checksum == sum)
        check("builder: the complement", u16(rom, 0x7FDC) == sum ~ 0xFFFF)
        check("builder: code is at the start", rom:sub(1, 5) == "\x78\x18\xFB\xCB\x80", hex(rom:sub(1, 5)))
        check("builder: padding is zero", rom:byte(0x7000) == 0)
    end
end
do
    -- header layout by address: $FFC0 title (21), $FFD5 map, $FFD6 chipset, $FFD7 size, $FFD8 sram, $FFD9 region, $FFDA dev, $FFDB version, $FFDC complement, $FFDE checksum
    local rom, info = rom_of(PROG, { title = "T", dev_id = 0x33, version = 1 })
    local function at(a) return rom:byte(a - 0x8000 + 1) end   -- a is the CPU address $FFxx of bank 0
    check("header: map at $FFD5", at(0xFFD5) == 0x20)
    check("header: chipset at $FFD6", at(0xFFD6) == 0x00)
    check("header: ROM size at $FFD7", at(0xFFD7) == 0x05)
    check("header: SRAM size at $FFD8", at(0xFFD8) == 0x00)
    check("header: region at $FFD9", at(0xFFD9) == 0x01)
    check("header: dev id at $FFDA", at(0xFFDA) == 0x33)
    check("header: version at $FFDB", at(0xFFDB) == 1)
    local comp = at(0xFFDC) | (at(0xFFDD) << 8)
    local chk = at(0xFFDE) | (at(0xFFDF) << 8)
    check("header: complement + checksum = $FFFF", comp + chk == 0xFFFF and comp == chk ~ 0xFFFF)
    check("header: checksum is the byte sum", chk == info.checksum)
    check("header: reset at $FFFC", at(0xFFFC) | (at(0xFFFD) << 8) == 0x8000)
end
do
    local rom = rom_of(PROG, { title = "x", fast = true, sram_kb = 8 })
    check("builder: FastROM bit", rom and rom:byte(0x7FD6) == 0x30)
    check("builder: battery RAM chipset", rom and rom:byte(0x7FD7) == 0x02)
    check("builder: SRAM size code 8 KB = 3", rom and rom:byte(0x7FD9) == 0x03)
    rom = rom_of(PROG, { sram_kb = 2, battery = false })
    check("builder: RAM without battery", rom and rom:byte(0x7FD7) == 0x01 and rom:byte(0x7FD9) == 0x01)
    rom = rom_of(PROG, { fill = 0xFF })
    check("builder: fill byte", rom and rom:byte(0x7000) == 0xFF)
    rom = rom_of(PROG, { min_kb = 128 })
    check("builder: minimum size", rom and #rom == 0x20000 and rom:byte(0x7FD8) == 0x07)
end
do
    -- the size is a power of two; a second bank makes it 64 KB; the code in bank 1 lands at $8000 of the file
    local src = ".lorom\n.org $8000\nreset: nop\n.org $018000\nsecond: .byte $AB\n.org $028000\n.byte 1\n"
    local rom, info = rom_of(src, {})
    check("builder: three banks make 128 KB", rom and #rom == 0x20000 and info.size == 0x20000, rom and #rom)
    check("builder: bank 1 at file offset $8000", rom and rom:byte(0x8001) == 0xAB)
    check("builder: bank 2 at file offset $10000", rom and rom:byte(0x10001) == 1)
    check("builder: size code 128 KB", rom and rom:byte(0x7FD8) == 0x07)
    check("builder: sum includes everything", rom and info.checksum == (function() local s = 0; for i = 1, #rom do s = s + rom:byte(i) end; return s & 0xFFFF end)())
end
do
    local rom, info = rom_of(".hirom\n.org $C00000\nreset: nop\nnmi: rti\n.org $C10000\n.byte $CD\n", { title = "HI" })
    check("hirom: builds", rom ~= nil, info)
    if rom then
        check("hirom: size 128 KB", #rom == 0x20000)
        check("hirom: map byte at $FFD5 of the file", rom:byte(0xFFD6) == 0x21)
        check("hirom: title at $FFC0", rom:sub(0xFFC1, 0xFFC2) == "HI")
        check("hirom: reset vector", u16(rom, 0xFFFC) == 0x0000)
        check("hirom: nmi vector", u16(rom, 0xFFFA) == 0x0001)
        check("hirom: second bank", rom:byte(0x10001) == 0xCD)
        check("hirom: code at file start", rom:byte(1) == 0xEA)
    end
    rom = rom_of(".org $C00000\nreset: nop\n", { map = "hirom" })
    check("hirom: the map from the header options", rom and rom:byte(0xFFD6) == 0x21)
end
do
    -- vectors by option
    local src = ".org $8000\nstart: nop\nmyirq: rti\nmynmi: rti\n"
    local rom = rom_of(src, { vectors = { reset = "start", nmi = "mynmi", irq = 0x8001 } })
    check("vectors: by name", rom and u16(rom, 0x7FFC) == 0x8000 and u16(rom, 0x7FEA) == 0x8002 and u16(rom, 0x7FFA) == 0x8002)
    check("vectors: by number", rom and u16(rom, 0x7FEE) == 0x8001 and u16(rom, 0x7FFE) == 0x8001)
    rom = rom_of(src, { vectors = { reset = "start", nmi = "mynmi", emu_nmi = 0x8001 } })
    check("vectors: emulation override", rom and u16(rom, 0x7FEA) == 0x8002 and u16(rom, 0x7FFA) == 0x8001)
    -- an upper-case label is found too
    rom = rom_of(".org $8000\nRESET: nop\nNMI: rti\n", {})
    check("vectors: upper-case labels", rom and u16(rom, 0x7FFC) == 0x8000 and u16(rom, 0x7FFA) == 0x8001)
end
do
    local function berr(src, h, pat, name)
        local rom, e = A.build(src, {}, h)
        check("builder error: " .. name, rom == nil and e and tostring(e):find(pat, 1, true) ~= nil, tostring(e))
    end
    berr(".org $8000\nnop\n", {}, "no reset vector", "no reset")
    berr(".org $8000\nreset: nop\n", { vectors = { nmi = "nope" } }, "unknown label nope", "unknown vector label")
    berr(".org $0000\nreset: nop\n", {}, "is not ROM in LoROM", "low half of a LoROM bank")
    berr(".org $7E8000\nreset: nop\n", {}, "WRAM", "WRAM")
    berr(".org $8000\nreset: nop\n.org $8000\nnop\n", {}, "overlaps", "overlap")
    berr(".org $FFD0\nreset: nop\n", {}, "header area", "header area")
    berr(".org $FFF0\nreset: .byte 1,2,3\n", {}, "header area", "vectors area")
    berr(".org $FFF0\nreset: .res 8,1\n.res 20,1\n", {}, "crosses a bank border", "bank crossing")
    berr(".org $8000\nreset: nop\n", { map = "exrom" }, "lorom or hirom", "map name")
    berr("bogus\n", {}, "bogus", "assembler errors reach the caller")
    berr(".hirom\n.org $C00000\nreset: nop\n.org $008000\n.byte 1\n.org $000000\n.byte 2\n", {}, "is not ROM in HiROM", "HiROM low half")
end
do
    -- a program whose last byte is in $FFB0-$FFBF is allowed (below the header)
    local rom = rom_of(".org $FFB0\nreset: nop\n", {})
    check("builder: code just below the header", rom and rom:byte(0x7FB1) == 0xEA)
    check("builder: lorom_offset", A.rom_offset(0x018000, "lorom") == 0x8000 and A.rom_offset(0x808000, "lorom") == 0 and A.rom_offset(0xC10000, "hirom") == 0x10000)
end
do
    -- the whole API in one call, with a read callback; and no I/O library
    local rom, res = A.build(".include \"p.s\"\n", { read = function(p) return (p == "p.s") and ".org $8000\nreset: rti\n" or nil end }, { title = "ONE" })
    check("build: one call with include", rom and rom:byte(1) == 0x40 and rom:sub(0x7FC1, 0x7FC3) == "ONE", tostring(res))
end
do
    -- a real program: a loop with 16-bit and 8-bit code, a table, a macro, local labels
    local src = [[
.lorom
VMAIN = $2115
.macro setaxy
    rep #$30
.endmacro
.org $8000
reset:
    sei
    clc
    xce
    setaxy
    .a16
    .i16
    ldx #$1FFF
    txs
    lda #$0000
    tcd
    sep #$20
    .a8
    lda #$80
    sta VMAIN
    ldx #0
@copy:
    lda table,x
    sta $2118
    inx
    cpx #table_end-table
    bne @copy
    jsr sub
    jmp done
sub:
    rep #$20
    lda #$1234
    sta $10
    rts
done:
    wai
    bra done
table: .byte 1,2,3,4
table_end:
nmi: rti
]]
    local rom, res = A.build(src, { name = "real.s" }, { title = "REAL" })
    check("real program assembles", rom ~= nil, tostring(res))
    if rom then
        -- sei clc xce rep#$30 ldx#$1FFF txs lda#$0000 tcd sep#$20 lda#$80 ...
        check("real program: bytes", hex(rom:sub(1, 24)) == "78 18 FB C2 30 A2 FF 1F 9A A9 00 00 5B E2 20 A9 80 8D 15 21 A2 00 00 BD", hex(rom:sub(1, 24)))
        check("real program: table size", res.symbols.table_end - res.symbols.table == 4)
    end
end

print(string.format("asm_test: %d checks, %d failed", count, fails))
os.exit(fails == 0 and 0 or 1)
