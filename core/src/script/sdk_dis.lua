-- sdk_dis.lua: the ASM listing of a ROM for the Code tab of Shell SDK (read only). A NES ROM is read here,
-- 6502, with the opcode table of the assembler; a SNES ROM is read by core/lua/snesrom.lua, 65C816.
-- A NES ROM of mapper 0 or 3 shows its whole PRG; with another mapper the listing is one PRG bank, because the
-- same address holds a different bank at different times. A linear sweep: bytes that are data can show as
-- instructions. The PC tests run it (tests/snes/sdk_dis_test.lua, make test-sdk-templates).
--
--   D.listing(A, R, bytes, bank)   A: snes_asm, R: snesrom, bytes: the file, bank: 0 up (nil: the last one)
--       the text, one line per row ("8000  lda #$00          A9 00"), or nil and the reason; a bank listing
--       has a third value: { bank, banks, addr, kb, guess } (guess: addr was worked out from the calls)

local D = {}
local sfmt, sbyte, concat = string.format, string.byte, table.concat

local FILL_MIN = 16          -- a run of this many $00 or $FF is one line

local function row(addr, ins, bytes)
  if bytes == "" then return sfmt("%04X  %s", addr, ins) end
  return sfmt("%04X  %-17s %s", addr, ins, bytes)
end

local function empty_note(addr, n, value)
  return sfmt("%04X  ; %d bytes of $%02X", addr, n, value)
end

----------------------------------------------------------------- NES, 6502

local FMT = {
  imp = function() return "" end, acc = function() return " a" end, sig = function(v) return sfmt(" #$%02X", v) end,
  imm = function(v) return sfmt(" #$%02X", v) end,
  dp = function(v) return sfmt(" $%02X", v) end, dpx = function(v) return sfmt(" $%02X,x", v) end,
  dpy = function(v) return sfmt(" $%02X,y", v) end,
  abs = function(v) return sfmt(v < 0x100 and " a:$%04X" or " $%04X", v) end,
  absx = function(v) return sfmt(v < 0x100 and " a:$%04X,x" or " $%04X,x", v) end,
  absy = function(v) return sfmt(v < 0x100 and " a:$%04X,y" or " $%04X,y", v) end,
  absind = function(v) return sfmt(" ($%04X)", v) end,
  indx = function(v) return sfmt(" ($%02X,x)", v) end, indy = function(v) return sfmt(" ($%02X),y", v) end,
}

local function table_6502(A)
  local ops = {}
  for mn, modes in pairs(A.opcodes) do
    local nmos = A.nmos[mn]
    if nmos then
      for mode, op in pairs(modes) do
        if nmos[mode] then ops[op] = { mn, mode, mode == "imm" and 2 or A.lengths[mode] } end
      end
    end
  end
  return ops
end

-- the size of a PRG bank by mapper; the ones not listed have 8 KB banks, the smallest of the common ones
local BANK_KB = { [1] = 16, [2] = 16, [10] = 16, [71] = 16, [7] = 32, [11] = 32, [34] = 32, [66] = 32 }

-- Where an 8 KB bank runs, from its own calls: the address that most jsr and jmp of the bank point into, with
-- more weight for a target just after an rts, an rti or a jmp (the start of a routine).
local function guess_base(ops, prg)
  local targets, i, n = {}, 1, #prg
  while i <= n do
    local e = ops[sbyte(prg, i)]
    local len = e and e[3] or 1
    if e and e[2] == "abs" and (e[1] == "jsr" or e[1] == "jmp") and i + 2 <= n then
      targets[#targets + 1] = sbyte(prg, i + 1) | (sbyte(prg, i + 2) << 8)
    end
    i = i + len
  end
  local best, best_score = 0x8000, 0
  for _, base in ipairs({ 0x8000, 0xA000, 0xC000 }) do
    local score = 0
    for _, t in ipairs(targets) do
      local o = t - base                               -- the offset of the target in the bank
      if o >= 0 and o < n then
        local before = o >= 1 and sbyte(prg, o) or nil
        local routine = before == 0x60 or before == 0x40 or (o >= 3 and sbyte(prg, o - 2) == 0x4C)
        score = score + (routine and 4 or 1)
      end
    end
    if score > best_score then best, best_score = base, score end
  end
  return best
end

-- The listing of the bytes of prg, seen from address base. vectors: the last six bytes are the vectors.
-- whole: from the first byte; else from the lowest entry point of the vectors.
local function sweep(ops, prg, base, vectors, whole)
  local size = #prg
  local function word(i) return sbyte(prg, i + 1) | (sbyte(prg, i + 2) << 8) end
  local last = base + size - (vectors and 7 or 1)      -- the last byte that can be code
  local function byte(a) return sbyte(prg, a - base + 1) end

  local names, start, nmi, reset, irq = {}, nil, nil, nil, nil
  if vectors then
    nmi, reset, irq = word(size - 6), word(size - 4), word(size - 2)
    for _, v in ipairs({ { "reset", reset }, { "nmi", nmi }, { "irq", irq } }) do
      local a = v[2]
      -- a PRG of 16 KB shows twice: an address of the other copy counts too
      if not whole and size == 0x4000 and a >= 0x8000 then a = base + (a - 0x8000) % 0x4000 end
      if a >= base and a <= last then
        names[a] = names[a] or v[1]
        start = math.min(start or a, a)
      end
    end
  end
  if whole then start = base end
  if not start then return nil, "the reset vector is not in the code" end

  -- the empty end of the PRG is one line
  local stop, fb, run = last, byte(last), 0
  if fb == 0 or fb == 0xFF then
    while stop - run >= start and byte(stop - run) == fb and not names[stop - run] do run = run + 1 end
  end
  if run >= FILL_MIN then stop = stop - run else run = 0 end

  local list, starts = {}, {}
  local at = start
  while at <= stop do
    local b = byte(at)
    local n = 0
    if b == 0 or b == 0xFF then
      while at + n <= stop and byte(at + n) == b and (n == 0 or not names[at + n]) do n = n + 1 end
    end
    if n >= FILL_MIN then
      list[#list + 1] = { a = at, fill = b, n = n }
    else
      local e = ops[b]
      local ok = e and at + e[3] - 1 <= stop
      if ok then
        for i = 1, e[3] - 1 do if names[at + i] then ok = false end end   -- an entry point starts an instruction
      end
      if ok then
        local v = 0
        for i = e[3] - 1, 1, -1 do v = (v << 8) | byte(at + i) end
        list[#list + 1] = { a = at, mn = e[1], mode = e[2], n = e[3], v = v }
        starts[at] = true
        n = e[3]
      else
        list[#list + 1] = { a = at, data = b }
        n = 1
      end
    end
    at = at + n
  end

  -- a label at each target of a branch, jmp or jsr that starts an instruction
  for _, it in ipairs(list) do
    local t
    if it.mode == "rel" then t = (it.a + 2 + (it.v >= 0x80 and it.v - 0x100 or it.v)) & 0xFFFF
    elseif it.mode == "abs" and (it.mn == "jmp" or it.mn == "jsr") then t = it.v end
    if t then
      it.target = t
      if starts[t] then names[t] = names[t] or sfmt("L%04X", t) end
    end
  end

  local out = {}
  local data, data_at = {}, nil
  local function flush()
    if #data == 0 then return end
    out[#out + 1] = row(data_at, ".db " .. concat(data, ","), "")
    data = {}
  end
  for _, it in ipairs(list) do
    if names[it.a] then
      flush()
      if #out > 0 and out[#out] ~= "" then out[#out + 1] = "" end
      out[#out + 1] = names[it.a] .. ":"
    end
    if it.data then
      if #data == 0 then data_at = it.a end
      data[#data + 1] = sfmt("$%02X", it.data)
      if #data == 4 then flush() end
    else
      flush()
      if it.fill then
        out[#out + 1] = empty_note(it.a, it.n, it.fill)
      else
        local opnd
        if it.target and names[it.target] then opnd = " " .. names[it.target]
        elseif it.mode == "rel" then opnd = sfmt(" $%04X", it.target)
        else opnd = FMT[it.mode](it.v) end
        local hx = {}
        for i = 0, it.n - 1 do hx[#hx + 1] = sfmt("%02X", byte(it.a + i)) end
        out[#out + 1] = row(it.a, it.mn .. opnd, concat(hx, " "))
        if it.mn == "rts" or it.mn == "rti" or it.mn == "jmp" then out[#out + 1] = "" end
      end
    end
  end
  flush()
  if run > 0 then out[#out + 1] = empty_note(stop + 1, run, fb) end
  if vectors then
    if out[#out] ~= "" then out[#out + 1] = "" end
    out[#out + 1] = "; the vectors"
    out[#out + 1] = row(last + 1, sfmt(".dw $%04X", nmi), "nmi")
    out[#out + 1] = row(last + 3, sfmt(".dw $%04X", reset), "reset")
    out[#out + 1] = row(last + 5, sfmt(".dw $%04X", irq), "irq")
  end
  while out[#out] == "" do out[#out] = nil end
  return concat(out, "\n")
end

local function nes(A, bytes, bank)
  if #bytes < 16 then return nil, "the file is shorter than its header" end
  local prg_n = sbyte(bytes, 5) * 16384
  local mapper = (sbyte(bytes, 7) >> 4) | (sbyte(bytes, 8) & 0xF0)
  local first = 16 + ((sbyte(bytes, 7) & 4) ~= 0 and 512 or 0)
  if prg_n == 0 or #bytes < first + prg_n then return nil, "the file is shorter than its header says" end
  local ops = table_6502(A)

  if mapper == 0 or mapper == 3 then                   -- the PRG is fixed: all of it
    local win = math.min(prg_n, 0x8000)
    local prg = bytes:sub(first + prg_n - win + 1, first + prg_n)
    local reset = sbyte(prg, win - 3) | (sbyte(prg, win - 2) << 8)
    if reset < 0x8000 then return nil, "the reset vector is not in the ROM" end
    -- a PRG of 16 KB shows twice: the listing takes the copy that the reset vector names
    local base = (win == 0x4000 and reset >= 0xC000) and 0xC000 or 0x8000
    return sweep(ops, prg, base, true, false)
  end

  local size = math.min((BANK_KB[mapper] or 8) * 1024, prg_n)
  local banks = prg_n // size
  bank = bank and math.max(0, math.min(bank, banks - 1)) or banks - 1
  local prg = bytes:sub(first + bank * size + 1, first + (bank + 1) * size)
  local fixed = bank == banks - 1                      -- the last bank stays at the top: it has the vectors
  local base, guess = 0x8000, false
  if size < 0x8000 and fixed then
    base = 0x10000 - size
  elseif size == 0x2000 then
    base, guess = guess_base(ops, prg), true
  end
  local text, why = sweep(ops, prg, base, fixed or size == 0x8000, true)
  if not text then return nil, why end
  return text, nil, { bank = bank, banks = banks, addr = base, kb = size // 1024, guess = guess }
end

----------------------------------------------------------------- SNES, 65C816 (snesrom.lua)

local function snes(R, bytes)
  if #bytes < 0x8000 then return nil, "too small for a SNES ROM" end
  local rom = R.open(bytes)
  local reset
  for _, v in ipairs(R.vectors(rom)) do if v.name == "reset" then reset = v end end
  if not reset then return nil, "the reset vector is not in the ROM" end
  local off = reset.off
  local bank_last = math.min(rom.size, off + 0x10000 - (reset.addr & 0xFFFF)) - 1
  -- the header and the vectors are not code
  local stop = (rom.h > off and rom.h <= bank_last) and rom.h - 1 or bank_last
  local fb, run = sbyte(rom.bytes, stop + 1), 0
  if fb == 0 or fb == 0xFF then
    while stop - run > off and sbyte(rom.bytes, stop - run + 1) == fb do run = run + 1 end
  end
  if run >= FILL_MIN then stop = stop - run else run = 0 end
  local d, why = R.disasm(rom, reset.addr, { last = stop })
  if not d then return nil, why end

  local out, n = {}, 0
  for line in (d.text .. "\n"):gmatch("([^\n]*)\n") do
    n = n + 1
    if n >= d.first and (#out > 0 or line ~= "") then
      local ins, addr, hx = line:match("^    (.-)%s*; %x%x:(%x%x%x%x)  (.*)$")
      if ins then
        out[#out + 1] = row(tonumber(addr, 16), ins, hx)
      else
        ins, addr = line:match("^    (%.db .-)%s*; %x%x(%x%x%x%x)  data$")
        if ins then out[#out + 1] = row(tonumber(addr, 16), ins, "")
        else out[#out + 1] = (line:gsub("^    ", "      ")) end
      end
    end
  end
  while out[#out] == "" do out[#out] = nil end
  if run > 0 then
    out[#out + 1] = empty_note((reset.addr + stop - off + 1) & 0xFFFF, run, fb)
  end
  return concat(out, "\n")
end

function D.listing(A, R, bytes, bank)
  if bytes:sub(1, 4) == "NES\x1A" then return nes(A, bytes, bank) end
  return snes(R, bytes)
end

return D
