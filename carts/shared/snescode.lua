-- snescode.lua: the helpers of the Code tab of bm SNES SDK (carts/snes_sdk). No bm API: the PC tests run it
-- (tests/snes/code_test.lua).
--
--   local SC = loadfile("carts/shared/snescode.lua")()
--   SC.tokens(line)               the colour classes of one line of assembly
--   SC.info(mnemonic)             what the instruction does, the flags, the cycles, the addressing modes
--   SC.plain(msg, line_text)      an error of the assembler in plain words, with what to do
--   SC.snippets                   the usual starts (reset, NMI, DMA copy, joypad read, colour), each line explained
--   SC.build(files, main, name)   assemble and build the ROM from a table of files
--
-- The texts are original: they come from the WDC W65C816S datasheet facts and from the opcode table of the
-- assembler (A.opcodes), not from a guide.

local A = require "snes_asm"
local SC = {}

----------------------------------------------------------------- the words of the language

local MN = {}                                -- every mnemonic: opcode table and aliases
for k in pairs(A.opcodes) do MN[k] = true end
local ALIAS = { bge = "bcs", blt = "bcc", ina = "inc", dea = "dec", tad = "tcd", tda = "tdc",
                tas = "tcs", tsa = "tsc", swa = "xba" }
for k in pairs(ALIAS) do MN[k] = true end
SC.mnemonics = MN
SC.alias = ALIAS

local DIR = {}
for w in ([[org byte word long dword db dw dl dd res ds align incbin include a8 a16 i8 i16 lorom hirom macro
            endmacro rept endr if ifdef ifndef else endif error]]):gmatch("%S+") do DIR[w] = true end
SC.directives = DIR

----------------------------------------------------------------- the colours

-- Classes: "mn" mnemonic, "dir" directive, "label" a label or an equate name, "hex" a $ or 0x number,
-- "num" another number, "cmt" comment, "str" string or character, "op" an operator or a #, "txt" the rest.
-- Returns a list of { from, to, class } (1-based, inclusive) that covers the whole line.
function SC.tokens(line)
  local out, n = {}, #line
  local function add(a, b, c) if b >= a then out[#out + 1] = { a, b, c } end end
  -- the comment: the first ; that is not in a string or a character
  local cmt, q = nil, nil
  local i = 1
  while i <= n do
    local ch = line:sub(i, i)
    if q then
      if ch == "\\" then i = i + 1 elseif ch == q then q = nil end
    elseif ch == '"' or (ch == "'" and line:sub(i + 2, i + 2) == "'") then q = ch
    elseif ch == ";" then cmt = i; break end
    i = i + 1
  end
  local body_end = cmt and cmt - 1 or n
  local pos = 1
  -- labels at the start: [@]name:  (many on a line)
  while true do
    local s, e = line:find("^%s*@?[%a_][%w_]*:", pos)
    if not s or e > body_end then break end
    local ws = line:match("^%s*", pos)
    add(pos, pos + #ws - 1, "txt")
    add(pos + #ws, e, "label")
    pos = e + 1
  end
  -- an equate: name = expression
  local s, e = line:find("^%s*@?[%a_][%w_]*%s*=[^=]", pos)
  if s and e <= body_end + 1 then
    local ws = line:match("^%s*", pos)
    local name = line:match("^%s*(@?[%a_][%w_]*)", pos)
    add(pos, pos + #ws - 1, "txt")
    add(pos + #ws, pos + #ws + #name - 1, "label")
    pos = pos + #ws + #name
  else
    local ws = line:match("^%s*", pos)
    local w = line:match("^%.?[%a_][%w_]*", pos + #ws)
    if w and pos + #ws + #w - 1 <= body_end then
      add(pos, pos + #ws - 1, "txt")
      local lw = w:lower()
      local cls
      if lw:sub(1, 1) == "." then cls = DIR[lw:sub(2)] and "dir" or "txt"
      else
        local base = lw:gsub("%.[bwl]$", "")
        cls = MN[base] and "mn" or "txt"
      end
      -- a mnemonic may carry .b .w .l: the suffix has the colour of the mnemonic
      local tail = line:match("^%.[bwl]", pos + #ws + #w)
      if cls == "mn" and tail then w = w .. tail end
      add(pos + #ws, pos + #ws + #w - 1, cls)
      pos = pos + #ws + #w
    end
  end
  -- the rest: numbers, strings, operators
  while pos <= body_end do
    local c = line:sub(pos, pos)
    local s2, e2
    if c == '"' then
      local j = pos + 1
      while j <= body_end and line:sub(j, j) ~= '"' do if line:sub(j, j) == "\\" then j = j + 1 end; j = j + 1 end
      e2 = math.min(j, body_end)
      add(pos, e2, "str"); pos = e2 + 1
    elseif c == "'" and line:sub(pos + 2, pos + 2) == "'" then
      add(pos, pos + 2, "str"); pos = pos + 3
    elseif c == "$" then
      s2, e2 = line:find("^%$%x*", pos); add(pos, e2, "hex"); pos = e2 + 1
    elseif line:find("^0[xX]%x", pos) then
      s2, e2 = line:find("^0[xX]%x+", pos); add(pos, e2, "hex"); pos = e2 + 1
    elseif c == "%" and line:find("^%%[01]", pos) and not line:sub(1, pos - 1):find("[%w_%)]%s*$") then
      s2, e2 = line:find("^%%[01]+", pos); add(pos, e2, "num"); pos = e2 + 1
    elseif c:match("%d") then
      s2, e2 = line:find("^%d+", pos); add(pos, e2, "num"); pos = e2 + 1
    elseif c:match("[%a_@]") then
      s2, e2 = line:find("^@?[%w_]+", pos); add(pos, e2, "txt"); pos = e2 + 1
    elseif c:match("%s") then
      s2, e2 = line:find("^%s+", pos); add(pos, e2, "txt"); pos = e2 + 1
    else
      add(pos, pos, "op"); pos = pos + 1
    end
  end
  if cmt then add(cmt, n, "cmt") end
  return out
end

----------------------------------------------------------------- the instruction texts (F1)

local MODE_NAME = {
  imp = "implied", acc = "A", imm = "#value", dp = "dp", dpx = "dp,X", dpy = "dp,Y", abs = "abs", absx = "abs,X",
  absy = "abs,Y", long = "long", longx = "long,X", ind = "(dp)", indx = "(dp,X)", indy = "(dp),Y", indl = "[dp]",
  indly = "[dp],Y", sr = "sr,S", sry = "(sr,S),Y", absind = "(abs)", absindx = "(abs,X)", absindl = "[abs]",
  rel = "branch (8 bit)", rell = "branch (16 bit)", blk = "src,dst banks", sig = "#signature",
}
local MODE_ORDER = { "imp", "acc", "imm", "dp", "dpx", "dpy", "abs", "absx", "absy", "long", "longx", "ind", "indx", "indy",
                     "indl", "indly", "sr", "sry", "absind", "absindx", "absindl", "rel", "rell", "blk", "sig" }
SC.mode_name = MODE_NAME

-- cycles for each mode, before the extra cycles (see NOTE)
local C_ALU = { imm = 2, dp = 3, dpx = 4, abs = 4, absx = 4, absy = 4, long = 5, longx = 5, ind = 5, indx = 6,
                indy = 5, indl = 6, indly = 6, sr = 4, sry = 7 }
local C_STORE = { dp = 3, dpx = 4, abs = 4, absx = 5, absy = 5, long = 5, longx = 5, ind = 5, indx = 6, indy = 6,
                  indl = 6, indly = 6, sr = 4, sry = 7 }
local C_RMW = { acc = 2, dp = 5, dpx = 6, abs = 6, absx = 7 }
local C_BIT = { imm = 2, dp = 3, dpx = 4, abs = 4, absx = 4 }
local C_IDX = { imm = 2, dp = 3, dpx = 4, dpy = 4, abs = 4, absx = 4, absy = 4 }
local C_STX = { dp = 3, dpx = 4, dpy = 4, abs = 4, absx = 5 }
local C_TRB = { dp = 5, abs = 6 }
local N_ALU = "+1 if A is 16 bit, +1 if the DP register is not a multiple of 256 (dp modes), +1 for abs,X abs,Y (dp),Y if the index crosses a page or X is 16 bit"
local N_STORE = "+1 if A is 16 bit, +1 if the DP register is not a multiple of 256 (dp modes)"
local N_RMW = "+2 if A is 16 bit (memory), +1 if the DP register is not a multiple of 256 (dp modes)"
local N_IDX = "+1 if X and Y are 16 bit, +1 if the DP register is not a multiple of 256 (dp modes), +1 if the index crosses a page"

-- Each entry: { summary, flags, { cycle table } or "cycle text", note, extra cycles note }
local I = {}
local function alu(name, sum, flags, note) I[name] = { sum, flags, C_ALU, note, N_ALU } end
alu("lda", "Load A from memory or a value.", "N Z")
alu("and", "A = A AND the value. Keeps only the bits that are set in both.", "N Z")
alu("ora", "A = A OR the value. Sets the bits that are set in the value.", "N Z")
alu("eor", "A = A XOR the value. Flips the bits that are set in the value.", "N Z")
alu("adc", "A = A + value + C. Clear C first (clc) for a new addition.", "N V Z C",
    "With the D flag set, the sum is decimal (BCD). Use cld at the start of a program.")
alu("sbc", "A = A - value - (1 - C). Set C first (sec) for a new subtraction.", "N V Z C",
    "C means no borrow. After a subtraction, C = 1 when A was not smaller than the value.")
alu("cmp", "Compare A with the value (A - value). A is not changed.", "N Z C",
    "After cmp: beq if equal, bcs if A >= value, bcc if A < value.")
I.sta = { "Store A in memory.", "none", C_STORE, "sta does not have an immediate form.", N_STORE }
I.stx = { "Store X in memory.", "none", C_STX, nil, N_STORE }
I.sty = { "Store Y in memory.", "none", C_STX, nil, N_STORE }
I.stz = { "Store zero in memory (8 or 16 bit, as M says).", "none", C_STX, "Use stz instead of lda #0 and sta.", N_STORE }
I.ldx = { "Load X from memory or a value.", "N Z", C_IDX, nil, N_IDX }
I.ldy = { "Load Y from memory or a value.", "N Z", C_IDX, nil, N_IDX }
I.cpx = { "Compare X with the value (X - value). X is not changed.", "N Z C", { imm = 2, dp = 3, abs = 4 }, nil, "+1 if X is 16 bit" }
I.cpy = { "Compare Y with the value (Y - value). Y is not changed.", "N Z C", { imm = 2, dp = 3, abs = 4 }, nil, "+1 if Y is 16 bit" }
I.asl = { "Shift left one bit: bit 7 (or 15) goes to C, 0 comes in. Multiplies by 2.", "N Z C", C_RMW, nil, N_RMW }
I.lsr = { "Shift right one bit: bit 0 goes to C, 0 comes in. Divides by 2 (unsigned).", "N Z C", C_RMW, nil, N_RMW }
I.rol = { "Rotate left through C: C comes in at bit 0, the top bit goes to C.", "N Z C", C_RMW, nil, N_RMW }
I.ror = { "Rotate right through C: C comes in at the top bit, bit 0 goes to C.", "N Z C", C_RMW, nil, N_RMW }
I.inc = { "Add 1 to A or to a memory cell.", "N Z", C_RMW, "ina is another name for inc A.", N_RMW }
I.dec = { "Subtract 1 from A or from a memory cell.", "N Z", C_RMW, "dea is another name for dec A.", N_RMW }
I.bit = { "Test bits: Z = (A AND value) is zero. Memory forms also copy bits 7 and 6 (15 and 14) to N and V.", "N V Z",
          C_BIT, "The immediate form changes Z only.", N_ALU }
I.trb = { "Test and reset bits: Z as bit does, then the bits of A are cleared in memory.", "Z", C_TRB, nil, N_RMW }
I.tsb = { "Test and set bits: Z as bit does, then the bits of A are set in memory.", "Z", C_TRB, nil, N_RMW }
I.jmp = { "Jump to an address. The next instruction is the one there.", "none", { abs = 3, long = 4, absind = 5, absindx = 6, absindl = 6 },
          "An address in the same bank gives the 3-byte absolute form. Another bank gives the long form." }
I.jml = { "Jump to a 24 bit address (long). The program bank changes.", "none", { long = 4, absindl = 6 } }
I.jsr = { "Jump to a subroutine in this bank. It pushes the return address (2 bytes). Return with rts.", "none",
          { abs = 6, absindx = 8, long = 8 } }
I.jsl = { "Jump to a subroutine at a 24 bit address. It pushes 3 bytes. Return with rtl.", "none", { long = 8 } }
I.rts = { "Return from a subroutine called with jsr.", "none", 6 }
I.rtl = { "Return from a subroutine called with jsl.", "none", 6 }
I.rti = { "Return from an interrupt: pulls the flags, then the return address.", "all (from the stack)", 6,
          "6 cycles in emulation mode, 7 in native mode. Use it to end the NMI routine." }
I.pea = { "Push a 16 bit value on the stack. Nothing is read from the value as an address.", "none", { abs = 5 } }
I.pei = { "Push the 16 bit word that is at the direct page address.", "none", { ind = 6 }, nil, "+1 if the DP register is not a multiple of 256" }
I.per = { "Push the address of a label (program counter relative).", "none", { rell = 6 } }
I.rep = { "Reset the flag bits that are 1 in the value (clear them). rep #$20 makes A 16 bit, rep #$10 makes X and Y 16 bit.",
          "the bits you give (N V M X D I Z C)", 3,
          "Bit 5 ($20) is M and bit 4 ($10) is X. The assembler follows rep for the size of lda # and ldx #. rep #$30 sets both." }
I.sep = { "Set the flag bits that are 1 in the value. sep #$20 makes A 8 bit, sep #$10 makes X and Y 8 bit.",
          "the bits you give (N V M X D I Z C)", 3,
          "Bit 5 ($20) is M and bit 4 ($10) is X. The assembler follows sep for the size of lda # and ldx #." }
I.brk = { "Software interrupt: it jumps through the BRK vector.", "I = 1, D = 0", 7, "Takes a signature byte (a free number). 8 cycles in native mode." }
I.cop = { "Co-processor interrupt: it jumps through the COP vector.", "I = 1, D = 0", 7, "Takes a signature byte. 8 cycles in native mode." }
I.wdm = { "Reserved by WDC. It does nothing and takes one byte after it.", "none", 2 }
I.mvn = { "Block move, upward: copies A+1 bytes from the source bank (X) to the destination bank (Y). X and Y grow.", "none", 7,
          "Write mvn source_bank,dest_bank. 7 cycles for each byte. A must hold the count minus 1, X the source address and Y the destination address." }
I.mvp = { "Block move, downward: copies A+1 bytes from the end. X and Y shrink.", "none", 7,
          "Write mvp source_bank,dest_bank. 7 cycles for each byte." }
local BR = { bpl = "N = 0 (the last result was positive)", bmi = "N = 1 (the last result was negative)",
             bvc = "V = 0 (no overflow)", bvs = "V = 1 (overflow)", bcc = "C = 0 (a smaller unsigned value, or no carry)",
             bcs = "C = 1 (the same or a larger unsigned value, or a carry)", bne = "Z = 0 (not equal, or not zero)",
             beq = "Z = 1 (equal, or zero)" }
for n, c in pairs(BR) do
  I[n] = { "Branch if " .. c .. ".", "none", 2, "2 cycles, +1 if the branch is taken, +1 more in emulation mode if it crosses a page. Range: 128 bytes back, 127 forward." }
end
I.bra = { "Branch always.", "none", 3, "3 cycles. Range: 128 bytes back, 127 forward." }
I.brl = { "Branch always, with a 16 bit distance (all of the bank).", "none", 4 }
local IMP = {
  clc = { "Clear the carry flag C.", "C" }, sec = { "Set the carry flag C.", "C" },
  cli = { "Clear I: interrupts (IRQ) are allowed.", "I" }, sei = { "Set I: IRQ is blocked (NMI still comes).", "I" },
  clv = { "Clear the overflow flag V.", "V" }, cld = { "Clear D: adc and sbc work in binary.", "D" },
  sed = { "Set D: adc and sbc work in decimal (BCD).", "D" },
  dex = { "Subtract 1 from X.", "N Z" }, dey = { "Subtract 1 from Y.", "N Z" },
  inx = { "Add 1 to X.", "N Z" }, iny = { "Add 1 to Y.", "N Z" },
  nop = { "Do nothing for 2 cycles.", "none" },
  pha = { "Push A on the stack (1 or 2 bytes, as M says).", "none", 3, "+1 cycle if A is 16 bit." },
  php = { "Push the flags (P) on the stack.", "none", 3 },
  phx = { "Push X on the stack (1 or 2 bytes).", "none", 3, "+1 cycle if X is 16 bit." },
  phy = { "Push Y on the stack (1 or 2 bytes).", "none", 3, "+1 cycle if Y is 16 bit." },
  phb = { "Push the data bank register (DB).", "none", 3 },
  phd = { "Push the direct page register (D, 16 bit).", "none", 4 },
  phk = { "Push the program bank register (K).", "none", 3 },
  pla = { "Pull A from the stack (1 or 2 bytes).", "N Z", 4, "+1 cycle if A is 16 bit." },
  plp = { "Pull the flags (P) from the stack.", "all", 4, "The assembler does not see this change: write .a8 .a16 .i8 .i16 after it." },
  plx = { "Pull X from the stack.", "N Z", 4, "+1 cycle if X is 16 bit." },
  ply = { "Pull Y from the stack.", "N Z", 4, "+1 cycle if Y is 16 bit." },
  plb = { "Pull the data bank register (DB). Use it after phb, or with phk to set DB to the program bank.", "N Z", 4 },
  pld = { "Pull the direct page register (D).", "N Z", 5 },
  stp = { "Stop the CPU until reset.", "none", 3 },
  wai = { "Wait for an interrupt: the CPU sleeps until NMI or IRQ. Use it to wait for the next frame.", "none", 3 },
  tax = { "X = A.", "N Z" }, tay = { "Y = A.", "N Z" }, tsx = { "X = the stack pointer.", "N Z" },
  txa = { "A = X.", "N Z" }, tya = { "A = Y.", "N Z" }, tyx = { "X = Y.", "N Z" }, txy = { "Y = X.", "N Z" },
  txs = { "The stack pointer = X.", "none" },
  tcd = { "The direct page register D = the 16 bit A (all of C).", "N Z" },
  tcs = { "The stack pointer = the 16 bit A.", "none" },
  tdc = { "The 16 bit A = the direct page register D.", "N Z" },
  tsc = { "The 16 bit A = the stack pointer.", "N Z" },
  xba = { "Swap the two bytes of the 16 bit A (B and A).", "N Z", 3, "N and Z follow the new low byte." },
  xce = { "Swap the carry flag C with the emulation flag E. clc then xce sets the native mode.", "C M X (and E)", 2,
          "The assembler does not see this: write .a8 .i8 or rep/sep after it. After clc, xce the M and X flags are 1 (8 bit)." },
}
for n, v in pairs(IMP) do I[n] = { v[1], v[2], v[3] or 2, v[4] } end

local ALIAS_NOTE = { bge = "bge is another name for bcs.", blt = "blt is another name for bcc.", ina = "ina is another name for inc A.",
                     dea = "dea is another name for dec A.", tad = "tad is another name for tcd.", tda = "tda is another name for tdc.",
                     tas = "tas is another name for tcs.", tsa = "tsa is another name for tsc.", swa = "swa is another name for xba." }

local function cycles_text(mn, e, modes)
  local c = e[3]
  if type(c) == "number" then return tostring(c) end
  local t = {}
  for _, m in ipairs(MODE_ORDER) do
    if modes[m] and c[m] then t[#t + 1] = (MODE_NAME[m] == "implied" and "" or (MODE_NAME[m] .. " ")) .. c[m] end
  end
  return table.concat(t, ", ")
end

-- info("lda") -> { name, summary, flags, cycles, modes = { { name, opcode, bytes } }, note, extra, short }
-- or nil when the word is not an instruction. A word with .b .w .l after it is accepted.
function SC.info(word)
  word = (word or ""):lower():gsub("%.[bwl]$", "")
  if not MN[word] then return nil end
  local real = ALIAS[word] or word
  local tbl = A.opcodes[real]
  local e = I[real]
  if word == "ina" or word == "dea" then tbl = { acc = tbl.acc } end
  local modes, list = {}, {}
  for m in pairs(tbl) do modes[m] = true end
  for _, m in ipairs(MODE_ORDER) do
    if tbl[m] then
      local len = A.lengths[m]
      -- the size of an immediate follows M or X for the ALU group and the index group (the table has no length)
      local btxt = len and tostring(len) or ((real == "rep" or real == "sep") and "2" or "2 or 3")
      list[#list + 1] = { name = MODE_NAME[m], mode = m, opcode = tbl[m], bytes = len, btxt = btxt }
    end
  end
  if not e then return { name = word, summary = "(no text)", flags = "?", cycles = "?", modes = list } end
  local cyc = cycles_text(real, e, modes)
  local flags = e[2]
  if flags == "none" then flags = "no flags" end
  local note = e[4]
  if ALIAS_NOTE[word] then note = ALIAS_NOTE[word] .. (note and (" " .. note) or "") end
  local imm_note
  if tbl.imm and (real == "lda" or real == "and" or real == "ora" or real == "eor" or real == "adc" or real == "sbc" or real == "cmp" or real == "bit") then
    imm_note = "#value is 1 byte when A is 8 bit and 2 bytes when A is 16 bit."
  elseif tbl.imm and (real == "ldx" or real == "ldy" or real == "cpx" or real == "cpy") then
    imm_note = "#value is 1 byte when X and Y are 8 bit and 2 bytes when they are 16 bit."
  end
  local short = word .. ": " .. e[1] .. "  Flags: " .. flags .. ". Cycles: " .. (cyc:gsub(", .*", "") ~= cyc and "see F1" or cyc)
  return { name = word, real = real, summary = e[1], flags = flags, cycles = cyc, modes = list, note = note, imm_note = imm_note,
           extra = e[5], short = short }
end

-- the lines of the help panel for one instruction
function SC.info_lines(inf)
  local L = { string.upper(inf.name) .. ": " .. inf.summary, "Flags changed: " .. inf.flags,
              "Cycles: " .. inf.cycles }
  if inf.extra then L[#L + 1] = "Extra cycles: " .. inf.extra end
  local m = {}
  for _, x in ipairs(inf.modes) do
    m[#m + 1] = string.format("%s $%02X (%sB)", x.name, x.opcode, x.btxt)
  end
  L[#L + 1] = "Modes: " .. table.concat(m, ", ")
  if inf.imm_note then L[#L + 1] = inf.imm_note end
  if inf.note then L[#L + 1] = inf.note end
  return L
end

-- the instruction word of a source line (after the labels), or nil
function SC.mnemonic_of(line)
  local s = line:gsub(";.*$", "")
  while true do
    local r = s:gsub("^%s*@?[%a_][%w_]*:", "", 1)
    if r == s then break end
    s = r
  end
  local w = s:match("^%s*([%a_][%w_]*)")
  return w and w:lower() or nil
end

-- the word under a column (1-based) of a line, or the instruction word of the line when the cursor is not on a word
function SC.word_at(line, col)
  for s, w, e in line:gmatch("()(%.?[%a_][%w_]*)()") do
    if col >= s and col <= e then return w end
  end
  return nil
end

----------------------------------------------------------------- errors in plain words

local function similar(w)                     -- the nearest mnemonic by edit distance (2 or less)
  local best, bd = nil, 3
  w = w:lower()
  local function dist(a, b)
    local prev = {}
    for j = 0, #b do prev[j] = j end
    for i = 1, #a do
      local cur = { [0] = i }
      for j = 1, #b do
        cur[j] = math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a:byte(i) == b:byte(j) and 0 or 1))
      end
      prev = cur
    end
    return prev[#b]
  end
  local names = {}
  for k in pairs(MN) do names[#names + 1] = k end
  table.sort(names)
  for _, k in ipairs(names) do
    local d = dist(w, k)
    if d < bd then best, bd = k, d end
  end
  return best
end

local CLASS_M = { lda = 1, ["and"] = 1, ora = 1, eor = 1, adc = 1, sbc = 1, cmp = 1, bit = 1 }
local CLASS_X = { ldx = 1, ldy = 1, cpx = 1, cpy = 1 }

-- plain(msg, line_text): the message the assembler gave and the source line -> a text for the user.
function SC.plain(msg, line)
  line = line or ""
  local mn = SC.mnemonic_of(line)
  local n, bits = msg:match("^the value (%-?%d+) does not fit in (%d+) bits")
  if n then
    local reg, rep
    if mn and CLASS_M[mn] then reg, rep = "A", "rep #$20"
    elseif mn and CLASS_X[mn] then reg, rep = (mn == "ldy" or mn == "cpy") and "Y" or "X", "rep #$10" end
    local hex = string.format("$%X", tonumber(n) & 0xFFFFFF)
    if reg and tonumber(bits) == 8 then
      return string.format("%s is 8 bit here, so %s # reads one byte, and %s does not fit. Write %s before this line to make %s 16 bit, or use a value up to $FF.",
                           reg, mn, hex, rep, reg)
    elseif mn == "rep" or mn == "sep" then
      return string.format("%s takes one byte (a value up to $FF). %s is too big.", mn, hex)
    end
    return string.format("The value %s does not fit in %s bits. Use a smaller value, or change the width with rep or sep (.a16 .i16 for the assembler).", hex, bits)
  end
  local v, what = msg:match("^the value (%$%x+) does not fit in (.+)$")
  if v then
    return string.format("The value %s is too big for this address form (%s). Use a longer form: a: absolute, f: long, for example lda f:label, or a long operand.", v, what)
  end
  local sym = msg:match("^the symbol (.+) is not defined$")
  if sym then
    return string.format("The label %s does not exist. Check the spelling (capitals count), or write %s: at the place it names.", sym, sym)
  end
  local w = msg:match("^unknown instruction or macro '(.-)'$")
  if w then
    local near = similar(w)
    return string.format("%s is not an instruction. %sA name for a place needs a colon at the end: %s:", w,
                         near and ("Do you mean " .. near .. "? ") or "", w)
  end
  local d = msg:match("^unknown directive %.(.+)$")
  if d then return string.format("%s is not a directive. The directives start with a dot: .org .byte .word .include and so on.", d) end
  local m = msg:match("^(%a+) needs an operand$")
  if m then return string.format("%s needs a value or an address after it. Example: %s #$00 or %s $2100.", m, m, m) end
  m = msg:match("^(%a+) takes no operand$")
  if m then return string.format("%s works alone. Remove what comes after it.", m) end
  m = msg:match("^(%a+) has no immediate form$")
  if m then return string.format("%s cannot take a # value. Load the value in a register first (lda #value) and then use %s with an address.", m, m) end
  m = msg:match("^(%a+) has no such addressing mode$")
  if m then return string.format("%s does not have this way to write the operand. Press F1 on %s: it lists the forms it has.", m, m) end
  m = msg:match("^bad operand for (%a+)")
  if m then return string.format("The operand of %s is not written right. Forms: #value, address, address,X, (address), [address]. F1 on %s lists them.", m, m) end
  local dist = msg:match("^the branch is too far %((%-?%d+) bytes%)")
  if dist then
    return string.format("The branch is %s bytes away; a short branch reaches 128 bytes back and 127 forward. Use brl (it reaches the whole bank), or branch to a nearby jmp.", dist)
  end
  if msg:match("^the branch target is in another bank") then
    return "A branch cannot leave the bank. Use jmp or jml to go to another bank."
  end
  if msg:match("^no reset vector") then
    return "The program has no start. Write the label reset: at the first instruction (after .org $8000)."
  end
  local file = msg:match("^cannot read (.+)$")
  if file then return string.format("The file %s is not there. Check the name, and that the file is in /docs.", file) end
  if msg:match("^an expression is missing") then return "A number or a label is missing here." end
  if msg:match("^a %) is missing") then return "A closing ) is missing." end
  if msg:match("^the expression ends too early") then return "The line stops in the middle of an expression. Add the missing value." end
  if msg:match("^division by zero") then return "The expression divides by zero." end
  if msg:match("^the symbol (.+) is already defined") then
    local s = msg:match("^the symbol (.+) is already defined")
    return string.format("The name %s is used twice. Each label needs its own name. A local label starts with @.", s)
  end
  if msg:match("^a .macro or .rept is not closed") then return "A .macro or .rept has no .endmacro or .endr." end
  if msg:match("^an .if is not closed") then return "An .if has no .endif." end
  if msg:match("^%.else without") or msg:match("^%.endif without") then return msg:gsub("^(%.%a+) without %.if", "%1 has no .if before it") .. "." end
  return msg
end

-- the errors of a result in plain words: a list of { file, line, msg (plain), raw }
function SC.errors(res, texts)
  local out = {}
  for _, e in ipairs(res.errors or {}) do
    local src = texts and texts[e.file]
    local line
    if src and e.line then
      local n = 0
      for l in (src .. "\n"):gmatch("(.-)\r?\n") do
        n = n + 1
        if n == e.line then line = l; break end
      end
    end
    out[#out + 1] = { file = e.file, line = e.line, msg = SC.plain(e.msg, line), raw = e.msg }
  end
  return out
end

----------------------------------------------------------------- build

-- files: { [name] = text } (the open files). main: the name of the first file. reader(name): the text of any other file. Returns rom, res (ok) or nil, errors, res.
function SC.build(files, main, title, reader)
  local function read(path)
    path = path:gsub("^/docs/", "")
    local low = path:lower()
    for k, v in pairs(files) do if k:lower() == low then return v end end
    return reader and reader(path) or nil           -- a file that is not open: the reader (doc_read) gives it
  end
  local res = A.assemble(files[main] or "", { name = main, read = read })
  if not res.ok then return nil, SC.errors(res, files), res end
  local ok, rom, why = pcall(A.build_rom, res, { title = (title or "SNES SDK"):upper():sub(1, 21) })
  if not ok or not rom then
    local msg = ok and tostring(why) or (type(rom) == "table" and rom.msg or tostring(rom))
    return nil, { { file = main, line = 1, msg = SC.plain(msg, ""), raw = msg } }, res
  end
  return rom, res
end

----------------------------------------------------------------- snippets

-- Each snippet: key (the word you type), name, about (one sentence), text (it assembles by itself, once a
-- label `reset` exists; the snippet `reset` has it). Every line has its own comment.
SC.snippets = {
  { key = "reset", name = "Reset: start of a program", about = "The first code of every game: native mode, stack, screen off, then a loop that waits for the frame.",
    text = [[
.lorom                  ; the ROM map: 32 KB banks at $8000
.org $8000              ; the code starts at the start of bank 0
reset:                  ; the CPU starts here (the reset vector)
    sei                 ; block IRQ while we set up
    clc                 ; carry = 0 ...
    xce                 ; ... swap it with E: now the CPU is in native mode
    rep #$38            ; A, X, Y 16 bit (bits $30) and decimal off (bit $08)
    ldx #$1FFF          ; the top of the work RAM
    txs                 ; the stack pointer starts there
    sep #$20            ; A is 8 bit from here
    lda #$8F            ; bit 7 = forced blank (the screen is off), brightness 15
    sta $2100           ; INIDISP: set up the picture safely
    stz $4200           ; NMITIMEN: no NMI, no IRQ, no auto joypad
    ; ... set up the video, load graphics here ...
    lda #$0F            ; screen on, brightness 15
    sta $2100           ; INIDISP
    lda #$81            ; bit 7 = NMI at each frame, bit 0 = read the pads
    sta $4200           ; NMITIMEN
forever:
    wai                 ; sleep until the next interrupt (the NMI)
    bra forever         ; then sleep again: the game runs in nmi
nmi:
    rep #$30            ; save A, X, Y at 16 bit
    pha                 ; push A
    phx                 ; push X
    phy                 ; push Y
    sep #$20            ; A is 8 bit
    lda $4210           ; RDNMI: reading it ends this NMI
    ; ... the work of each frame goes here ...
    rep #$30            ; back to 16 bit to pull the registers
    ply                 ; pull Y
    plx                 ; pull X
    pla                 ; pull A
    rti                 ; return from the interrupt
]] },
  { key = "nmi", name = "NMI routine (each frame)", about = "The code that runs at the start of the blanking. It saves the registers, ends the NMI and restores them.",
    text = [[
nmi:                    ; the label of the NMI vector: the builder finds it by this name
    rep #$30            ; A, X, Y 16 bit, to save all of them
    pha                 ; push A
    phx                 ; push X
    phy                 ; push Y
    sep #$20            ; A is 8 bit
    lda $4210           ; RDNMI: reading it ends this NMI
    ; ... copy the graphics, move the sprites, read the pad ...
    rep #$30            ; 16 bit again to pull
    ply                 ; pull Y
    plx                 ; pull X
    pla                 ; pull A
    rti                 ; return from the interrupt
]] },
  { key = "dma", name = "DMA copy to the video RAM", about = "Copies a block of bytes from the ROM to the video RAM with DMA channel 0. Do it in forced blank or in the NMI.",
    text = [[
    sep #$20            ; A is 8 bit
    lda #$80            ; VMAIN: the video address grows after the high byte
    sta $2115           ; VMAIN
    rep #$20            ; A is 16 bit
    lda #$0000          ; the video address (in words) where the data goes
    sta $2116           ; VMADDL/H: set it
    lda #$1801          ; low byte = mode 1 (two registers); high byte = $18 ($2118/$2119, the video data)
    sta $4300           ; DMAP0 and BBAD0 in one write
    lda #tiles          ; the 16 bit address of the data in the ROM
    sta $4302           ; A1T0: source address
    sep #$20            ; A is 8 bit
    lda #^tiles         ; the bank of the data
    sta $4304           ; A1B0: source bank
    rep #$20            ; A is 16 bit
    lda #tiles_end-tiles ; the number of bytes
    sta $4305           ; DAS0: byte count
    sep #$20            ; A is 8 bit
    lda #$01            ; bit 0 = channel 0
    sta $420B           ; MDMAEN: start the copy; the CPU waits until it ends
    bra dma_done        ; skip over the data
tiles:                  ; the data to copy (replace it, or use .incbin "gfx.chr")
    .byte $00, $FF, $00, $FF, $00, $FF, $00, $FF ; sample bytes: put your tile data here
tiles_end:              ; the end label gives the size
dma_done:
]] },
  { key = "joypad", name = "Read the joypad", about = "A routine that waits for the auto read to end and stores the 16 buttons of pad 1 in a variable.",
    text = [[
pad1 = $10              ; two bytes in the direct page: the buttons (B Y Sel Start Up Down Left Right A X L R)
read_pad:               ; call it with jsr, once for each frame (needs NMITIMEN bit 0 = 1)
    sep #$20            ; A is 8 bit
wait_pad:
    lda $4212           ; HVBJOY: bit 0 is 1 while the pads are read
    and #$01            ; keep that bit
    bne wait_pad        ; wait until the read is done
    rep #$20            ; A is 16 bit
    lda $4218           ; JOY1L/H: the 16 bits of pad 1 at once
    sta pad1            ; keep them in the variable
    ; the bits: $8000 B, $4000 Y, $2000 Select, $1000 Start, $0800 Up, $0400 Down, $0200 Left, $0100 Right,
    ; $0080 A, $0040 X, $0020 L, $0010 R. Test one with: lda pad1, then and #$0800, then bne (Up is pressed).
    rts                 ; return to the caller
]] },
  { key = "color", name = "Set the backdrop colour", about = "Writes one colour (BGR555) to the colour memory: the backdrop is the colour 0.",
    text = [[
    sep #$20            ; A is 8 bit
    stz $2121           ; CGADD: start at colour 0 (the backdrop)
    lda #$1F            ; the low byte of $001F (red)
    sta $2122           ; CGDATA: write the low byte first ...
    lda #$00            ; the high byte of $001F
    sta $2122           ; ... then the high byte. The bits are 0BBBBBGGGGGRRRRR.
]] },
}

function SC.snippet(key)
  for _, s in ipairs(SC.snippets) do if s.key == key then return s end end
end

return SC
