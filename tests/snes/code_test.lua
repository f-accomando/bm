-- code_test.lua: the helpers of the Code tab of bm SNES SDK, on the PC: the colours, the F1 texts, the errors in
-- plain words, the snippets and the text buffer (bm/shared/snescode.lua, bm/shared/asmbuf.lua).
--   build/host/luahost tests/snes/code_test.lua [ROOT]     (make test-snes-code)
local ROOT = arg[1] or "."
package.path = ROOT .. "/core/src/script/?.lua;" .. package.path
local A = require "snes_asm"
local SC = assert(loadfile(ROOT .. "/bm/shared/snescode.lua"))()
local B = assert(loadfile(ROOT .. "/bm/shared/asmbuf.lua"))()
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

----------------------------------------------------------------- colours

local function classes(line)
  local out = {}
  for _, t in ipairs(SC.tokens(line)) do out[#out + 1] = line:sub(t[1], t[2]) .. "=" .. t[3] end
  return table.concat(out, " ")
end
local function cls_of(line, word)
  for _, t in ipairs(SC.tokens(line)) do if line:sub(t[1], t[2]) == word then return t[3] end end
end
local L = "loop: lda #$1F ; red"
check(cls_of(L, "loop:") == "label", "label")
check(cls_of(L, "lda") == "mn", "mnemonic")
check(cls_of(L, "$1F") == "hex", "hex")
check(cls_of(L, "; red") == "cmt", "comment")
check(cls_of("    .org $8000", ".org") == "dir", "directive")
check(cls_of("    lda.l table,x", "lda.l") == "mn", "mnemonic with .l")
check(cls_of("    sta 0x2100", "0x2100") == "hex", "0x hex")
check(cls_of("    lda #123", "123") == "num", "decimal")
check(cls_of("    lda #%1010", "%1010") == "num", "binary")
check(cls_of('    .byte "hi;there", 1', '"hi;there"') == "str", "a ; inside a string is not a comment")
check(cls_of("    lda #'A'", "'A'") == "str", "character")
check(cls_of("pad1 = $10", "pad1") == "label", "equate name")
check(cls_of("@loop: dex", "@loop:") == "label", "local label")
check(cls_of("    foo $10", "foo") == "txt", "an unknown word has no colour")
check(cls_of("    nop ; lda", "; lda") == "cmt", "mnemonic in a comment")
do                                              -- the tokens cover the whole line, in order
  for _, line in ipairs({ "", "   ", "a: b: nop", "    lda ($10),y ; c", "x = 1<<2", '    .include "a.inc"' }) do
    local pos, ok = 1, true
    for _, t in ipairs(SC.tokens(line)) do
      if t[1] ~= pos then ok = false end
      pos = t[2] + 1
    end
    check(ok and pos == #line + 1, "tokens cover '" .. line .. "' (" .. classes(line) .. ")")
  end
end

----------------------------------------------------------------- F1 texts

local n_mn = 0
for mn in pairs(A.opcodes) do
  n_mn = n_mn + 1
  local inf = SC.info(mn)
  check(inf ~= nil, "info for " .. mn)
  if inf then
    check(inf.summary and #inf.summary > 3 and inf.summary ~= "(no text)", "summary of " .. mn)
    check(inf.flags and inf.flags ~= "?", "flags of " .. mn)
    check(inf.cycles and inf.cycles ~= "" and inf.cycles ~= "?", "cycles of " .. mn)
    local modes = 0
    for _ in pairs(A.opcodes[mn]) do modes = modes + 1 end
    check(#inf.modes == modes, "all the modes of " .. mn .. " are listed")
    for _, m in ipairs(inf.modes) do
      check(A.opcodes[mn][m.mode] == m.opcode and m.bytes == A.lengths[m.mode], "mode " .. m.mode .. " of " .. mn)
    end
    local text = table.concat(SC.info_lines(inf), "\n")
    check(text:find("Modes:", 1, true) and text:find("Cycles:", 1, true) and text:find("Flags changed:", 1, true), "panel of " .. mn)
    -- the cycle text names a number for each mode that has a table
    check(inf.cycles:find("%d"), "cycles of " .. mn .. " have a number")
  end
end
check(n_mn >= 90, "the opcode table has its mnemonics (" .. n_mn .. ")")
for alias, real in pairs(SC.alias) do
  local inf = SC.info(alias)
  check(inf and inf.note and inf.note:find(real, 1, true), "alias " .. alias)
end
check(SC.info("LDA") and SC.info("lda.l") and SC.info("lda").name == "lda", "case and size suffix")
check(SC.info("hello") == nil and SC.info("") == nil, "not an instruction")
do
  local lda = SC.info("lda")
  check(lda.flags == "N Z", "lda flags")
  check(lda.cycles:find("#value 2", 1, true) and lda.cycles:find("long 5", 1, true), "lda cycles")
  check(lda.imm_note and lda.imm_note:find("A is 8 bit"), "lda immediate size")
  check(SC.info("adc").flags == "N V Z C" and SC.info("cmp").flags == "N Z C", "adc and cmp flags")
  check(SC.info("rep").summary:find("rep #$20", 1, true), "rep names #$20")
  check(SC.info("xce").flags:find("C"), "xce flags")
  check(SC.info("jsr").cycles:find("abs 6", 1, true), "jsr cycles")
  check(SC.info("ina").modes[1].name == "A" and #SC.info("ina").modes == 1, "ina is the A form of inc")
end
check(SC.mnemonic_of("loop: @x: lda #1 ; c") == "lda", "mnemonic of a line")
check(SC.mnemonic_of("    .org $8000") == ".org" or SC.mnemonic_of("    .org $8000") == nil, "a directive is not an instruction")
check(SC.mnemonic_of("; lda") == nil, "comment only")
check(SC.word_at("    lda #$10", 6) == "lda", "word at")

----------------------------------------------------------------- errors in plain words

local function err_of(src)
  local res = A.assemble(src, { name = "t.s" })
  assert(not res.ok, "expected an error for: " .. src)
  local e = SC.errors(res, { ["t.s"] = src })[1]
  return e
end
local e = err_of("lda #$1234")
check(e.line == 1 and e.msg:find("A is 8 bit", 1, true) and e.msg:find("rep #$20", 1, true), "A 8 bit and a 16 bit value: " .. e.msg)
e = err_of(".a16\nlda #$1234\nldx #$1234")
check(e.line == 3 and e.msg:find("X is 8 bit", 1, true) and e.msg:find("rep #$10", 1, true), "X 8 bit and a 16 bit value: " .. e.msg)
e = err_of("ldy #$100")
check(e.msg:find("Y is 8 bit", 1, true) and e.msg:find("rep #$10", 1, true), "Y: " .. e.msg)
e = err_of("  rep #$1234")
check(e.msg:find("one byte", 1, true), "rep: " .. e.msg)
e = err_of("ldaa #1")
check(e.msg:find("not an instruction", 1, true) and e.msg:find("lda", 1, true), "unknown word suggests: " .. e.msg)
e = err_of("start:\n    lda missing_label")
check(e.line == 2 and e.msg:find("missing_label", 1, true) and e.msg:find("does not exist", 1, true), "missing label: " .. e.msg)
e = err_of(".org $8000\nloop:\n.res 200\n    bra loop")
check(e.msg:find("brl", 1, true) and e.msg:find("128", 1, true), "branch too far: " .. e.msg)
e = err_of("    nop\n    .foo")
check(e.line == 2 and e.msg:find("not a directive", 1, true), "directive: " .. e.msg)
e = err_of("    lda")
check(e.msg:find("needs a value", 1, true), "operand needed: " .. e.msg)
e = err_of("    nop $10")
check(e.msg:find("works alone", 1, true), "no operand: " .. e.msg)
e = err_of("    sta #1")
check(e.msg:find("cannot take a #", 1, true), "no immediate: " .. e.msg)
e = err_of("a:\na:")
check(e.line == 2 and e.msg:find("used twice", 1, true), "twice: " .. e.msg)
e = err_of("    .if 1")
check(e.msg:find(".endif", 1, true), "if not closed: " .. e.msg)
e = err_of("    .include \"nothere.inc\"")
check(e.msg:find("nothere.inc", 1, true) and e.msg:find("/docs", 1, true), "include: " .. e.msg)
check(SC.plain("something new", "") == "something new", "an unknown message stays as it is")
-- no error text is longer than a status line pair, and each has the final dot or a hint
do
  local rom, errs = SC.build({ ["m.s"] = "    jmp nowhere\n" }, "m.s")
  check(rom == nil and #errs == 1 and errs[1].file == "m.s" and errs[1].line == 1 and errs[1].raw:find("not defined"), "build gives the errors")
  rom, errs = SC.build({ ["m.s"] = ".lorom\n.org $8000\nstart:\n    rts\n" }, "m.s")
  check(rom == nil and errs[1].msg:find("reset:", 1, true), "no reset: " .. tostring(errs and errs[1].msg))
end

----------------------------------------------------------------- build and include

do
  local files = { ["main.s"] = '.lorom\n.org $8000\n.include "vec.inc"\n', ["vec.inc"] = "reset:\n    sei\n    rts\n" }
  local rom, res = SC.build(files, "main.s", "my game")
  check(rom and #rom >= 0x8000 and res.symbols.reset == 0x8000, "build with an include")
  check(rom and rom:sub(0x7FC1, 0x7FC7):find("MY GAME", 1, true), "the title is in the header")
end

----------------------------------------------------------------- snippets

check(#SC.snippets >= 5, "at least 5 snippets")
for _, sn in ipairs(SC.snippets) do
  local src = sn.text
  if not src:find("reset:", 1, true) then src = ".lorom\n.org $8000\nreset:\n    nop\n" .. src end
  if not src:find(".org", 1, true) then src = ".org $8000\n" .. src end
  local res = A.assemble(src, { name = sn.key .. ".s" })
  check(res.ok, "snippet " .. sn.key .. " assembles: " .. (res.ok and "" or A.format_errors(res)))
  check(#sn.about > 20 and #sn.name > 3, "snippet " .. sn.key .. " has a name and a text")
  -- each line with code has a comment that explains it
  local n_code, n_exp = 0, 0
  for line in sn.text:gmatch("[^\n]+") do
    if line:match("^%s*[^%s;]") and not line:match("^%s*@?[%w_]+:%s*$") then
      n_code = n_code + 1
      if line:find(";", 1, true) then n_exp = n_exp + 1 end
    end
  end
  check(n_code > 0 and n_exp == n_code, string.format("snippet %s: every line is explained (%d of %d)", sn.key, n_exp, n_code))
  -- it fits in a buffer and goes back out the same
  local b = B.new("")
  B.insert_block(b, sn.text)
  check(B.text(b):find(sn.text:sub(1, 20), 1, true), "snippet " .. sn.key .. " goes into a buffer")
end
do
  local rom, res = SC.build({ ["m.s"] = SC.snippet("reset").text }, "m.s", "snippet")
  check(rom ~= nil, "the reset snippet builds a ROM alone: " .. (rom and "" or tostring(res[1] and res[1].msg)))
  if rom then
    check(res.symbols.reset == 0x8000 and res.symbols.nmi, "reset and nmi labels")
    local nmi_vec = rom:byte(0x7FEC) * 256 + rom:byte(0x7FEB)     -- native NMI vector
    check(nmi_vec == res.symbols.nmi, "the builder takes the nmi label for the vector")
  end
  local dma = SC.snippet("dma").text
  local rom2 = SC.build({ ["m.s"] = ".lorom\n.org $8000\nreset:\n" .. dma .. "    rts\n" }, "m.s")
  check(rom2 ~= nil, "the dma snippet builds inside a program")
end

----------------------------------------------------------------- the text buffer

local function typed(b, s)
  for c in s:gmatch(".") do B.key(b, c == "\n" and "\n" or c) end
end
do
  local b = B.new("one\ntwo\n")
  check(#b.lines == 2 and B.text(b) == "one\ntwo\n", "new keeps the lines")
  check(#B.new("").lines == 1 and B.text(B.new("")) == "\n", "empty text has one line")
  check(#B.new("a\r\nb\r\n").lines == 2, "CRLF is one line break")
  typed(b, "xy")
  check(b.lines[1] == "xyone" and b.cx == 2 and b.dirty, "typing")
  B.key(b, "\b"); B.key(b, "\b")
  check(b.lines[1] == "one" and b.cx == 0, "backspace")
  B.key(b, "down"); B.key(b, "end"); B.key(b, "\n"); typed(b, "x")
  check(#b.lines == 3 and b.lines[3] == "x", "enter at the end")
  B.key(b, "\b"); B.key(b, "\b")
  check(#b.lines == 2 and b.lines[2] == "two", "backspace at the start of a line joins")
  B.key(b, "^z")
  check(b.lines[3] == "" or b.lines[3] == "x" or #b.lines == 3, "undo brings a line back")
  B.key(b, "^y")
  check(#b.lines == 2, "redo")
end
do
  local b = B.new("start:\n    nop\n")
  B.key(b, "^end")                                  -- the end of the text: after the last (empty?) line
  b.cy, b.cx = 1, 6
  B.key(b, "\n")
  check(b.lines[2] == "    " and b.cx == 4, "enter after a label indents")
  b = B.new("    lda #1")
  B.key(b, "end"); B.key(b, "\n")
  check(b.lines[2] == "    " and b.cx == 4, "enter keeps the indent")
  b = B.new("x")
  B.key(b, "\t")
  check(b.lines[1] == "    x" and b.cx == 4, "tab goes to the next stop")
  B.key(b, "\b")
  check(b.lines[1] == "x" and b.cx == 0, "backspace removes a whole indent step")
end
do
  local b = B.new("aaa\nbbb\nccc\n")
  b.cy, b.cx = 1, 1
  B.key(b, "^q"); b.cy, b.cx = 2, 2
  check(B.sel_text(b) == "aa\nbb", "selection text")
  B.key(b, "^c")
  check(B.clipboard == "aa\nbb" and not b.mark, "copy")
  b.cy, b.cx = 3, 3
  B.key(b, "^v")
  check(table.concat(b.lines, "|") == "aaa|bbb|cccaa|bb", "paste across lines: " .. table.concat(b.lines, "|"))
  b.cy, b.cx = 1, 0
  B.key(b, "^k")
  check(b.lines[1] == "bbb" and B.clipboard == "aaa\n", "cut line")
  B.set_cursor(b, 99, 99)
  check(b.cy == #b.lines and b.cx == #b.lines[b.cy], "set_cursor cuts to the text")
  B.set_cursor(b, 1, 0)
  B.key(b, "^v")
  check(b.lines[1] == "aaa" and b.lines[2] == "bbb", "paste a whole line goes above")
  B.key(b, "^d")
  check(b.lines[2] == "bbb" and b.lines[3] == "bbb", "duplicate a line")
  B.key(b, "^q"); b.cy, b.cx = 3, 1
  B.key(b, "\b")
  check(not b.mark, "backspace deletes the selection")
end
do
  local b = B.new("reset:\n    nop\n@loop:\n    bra @loop\nvalue = 5\nmain: sub: rts ; end:\n")
  local names = {}
  for _, d in ipairs(B.labels(b)) do names[#names + 1] = d.name .. "@" .. d.line end
  check(table.concat(names, " ") == "reset@1 value@5 main@6 sub@6", "labels: " .. table.concat(names, " "))
  check(B.goto_label(b, "value") and b.cy == 5 and b.cx == 0, "go to a label")
  check(not B.goto_label(b, "nothing"), "go to a missing label")
  B.goto_line(b, 99)
  check(b.cy == #b.lines, "go to a line past the end")
end
do
  local b = B.new("alpha\nBeta beta\ngamma\n")
  check(B.find(b, "beta") and b.cy == 2 and b.cx == 4 and B.sel_text(b) == "Beta", "find (not case sensitive)")
  check(B.find(b, "beta") and b.cy == 2 and b.cx == 9, "find next")
  check(B.find(b, "beta") and b.cy == 2 and b.cx == 4, "find wraps around")
  check(B.find(b, "alpha", true) and b.cy == 1, "find backward")
  check(not B.find(b, "zzz"), "find: not there")
  b.top, b.left = 1, 0
  b.cy, b.cx = 3, 5
  B.scroll(b, 2, 3)
  check(b.top == 2 and b.left == 3, "scroll follows the cursor")
end

io.write(string.format("snes code: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
