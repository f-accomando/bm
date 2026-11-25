-- snesrom.lua: read a SNES ROM (.sfc, .smc) for bm SNES SDK, read only: the header, the map, the vectors, the
-- address of a byte, a 65C816 disassembly and the tiles of a part of the ROM. No bm API and no drawing: the PC
-- tests run it (tests/snes/rom_test.lua). The opcode table is the one of the assembler (snes_asm.opcodes).
--
--   local R = loadfile("core/lua/snesrom.lua")()
--   R.open(bytes)                 the ROM: { bytes, size, copier, map, title, ... } (the 512-byte copier header is cut)
--   R.offset(rom, addr24)         the offset of a CPU address in the ROM (0-based), or nil and a reason
--   R.addr(rom, off)              the CPU address of an offset, in the fast banks ($80+ LoROM, $C0+ HiROM)
--   R.about(rom, name)            the comment lines that say what the ROM is
--   R.vectors(rom)                { {name, addr24, native}, ... } the vectors of the header that point in the ROM
--   R.disasm(rom, start24, opts)  { text, addr = {line = addr24}, line_of = {offset = line}, first, last, m8, x8 }
--                                 opts: m8, x8 (the widths at the start, true = 8 bits), names = { [offset] = "label" },
--                                 last (the offset where the sweep stops, before the end of the bank)
--   R.line_of(d, rom, pc24)       the line of the instruction at pc24 in a disassembly, or nil
--   R.page_bytes(w, h, bpp)       the bytes of a sheet w x h of tiles at bpp
--   R.tiles(rom, off, w, h, bpp, decode)  the indices of the sheet: { [y * w + x] = index } (decode: snesgfx decode_tile)

local R = {}
local sfmt, sbyte = string.format, string.byte

local A = SNES_ASM or require "snes_asm"

-- the opcode table, the other way: OPS[op] = { mnemonic, mode }. Two names for one opcode: the long one wins.
local OPS = {}
do
  local PREF = { jml = true, jsl = true }
  for mn, modes in pairs(A.opcodes) do
    for mode, op in pairs(modes) do
      local old = OPS[op]
      if not old or (PREF[mn] and not PREF[old[1]]) then OPS[op] = { mn, mode } end
    end
  end
end
local LEN = A.lengths
-- the width of an immediate: "m" follows the M flag, "x" the X flag; the others are one byte
local IMM = { ora = "m", ["and"] = "m", eor = "m", adc = "m", lda = "m", cmp = "m", sbc = "m", bit = "m",
              ldx = "x", ldy = "x", cpx = "x", cpy = "x" }
R.ops = OPS

----------------------------------------------------------------- the header and the map

local MAPS = { lo = 0x7FC0, hi = 0xFFC0, exhi = 0x40FFC0 }
local function w16(s, at) local a, b = sbyte(s, at + 1, at + 2); return (a or 0) | ((b or 0) << 8) end

-- how much a place looks like the header of a ROM of that map
local function score(s, map, h)
  if #s < h + 0x40 then return -1 end
  local n = 0
  local sum, comp = w16(s, h + 0x1E), w16(s, h + 0x1C)
  if sum ~ comp == 0xFFFF then n = n + 4 end
  local mode = sbyte(s, h + 0x16) or 0
  local low = mode & 0x0F
  if mode & 0xE0 == 0x20 then
    if (map == "lo" and (low == 0 or low == 2 or low == 3)) or (map == "hi" and low == 1) or (map == "exhi" and low == 5) then n = n + 3 end
  end
  if w16(s, h + 0x3C) >= 0x8000 then n = n + 2 end
  local ok = true
  for i = 1, 21 do
    local c = sbyte(s, h + i)
    if c < 0x20 or c > 0x7E then ok = false; break end
  end
  if ok then n = n + 1 end
  return n
end

local REGION = { [0] = "Japan", "USA", "Europe", "Sweden", "Finland", "Denmark", "France", "Netherlands", "Spain",
                 "Germany", "Italy", "China", "Indonesia", "Korea", "World", "Canada", "Brazil", "Australia" }
local CHIP = { [0] = "DSP", "Super FX", "OBC-1", "SA-1", "S-DD1", "S-RTC" }

function R.open(bytes)
  local copier = (#bytes % 1024 == 512) and 512 or 0
  local s = copier > 0 and bytes:sub(513) or bytes
  local best, bm = -2, "lo"
  for _, map in ipairs({ "lo", "hi", "exhi" }) do
    local v = score(s, map, MAPS[map])
    if v > best then best, bm = v, map end
  end
  local h = MAPS[bm]
  local rom = { bytes = s, size = #s, copier = copier, map = bm, h = h, sure = best >= 6 }
  if #s >= h + 0x40 then
    rom.title = s:sub(h + 1, h + 21):gsub("[^\32-\126]", " "):gsub("%s+$", "")
    rom.mode = sbyte(s, h + 0x16)
    rom.kind = sbyte(s, h + 0x17)
    rom.rom_kb = 1 << math.min(sbyte(s, h + 0x18), 16)
    local sr = sbyte(s, h + 0x19)
    rom.sram_kb = sr > 0 and sr <= 10 and (1 << sr) or 0
    rom.region = REGION[sbyte(s, h + 0x1A)] or sfmt("$%02X", sbyte(s, h + 0x1A))
    rom.version = sbyte(s, h + 0x1C)
    rom.fast = (rom.mode & 0x10) ~= 0
  else
    rom.title = ""
  end
  return rom
end

-- the offset of a CPU address in the ROM
function R.offset(rom, a)
  local bank, lo = (a >> 16) & 0xFF, a & 0xFFFF
  local off
  if rom.map == "lo" then
    if bank == 0x7E or bank == 0x7F then return nil, "banks $7E and $7F are the work RAM" end
    if lo < 0x8000 then
      if (bank & 0x7F) < 0x40 or (bank & 0x7F) >= 0x70 then return nil, sfmt("%02X:%04X is not the ROM (LoROM: $8000 to $FFFF)", bank, lo) end
    end
    off = (bank & 0x7F) * 0x8000 + (lo & 0x7FFF)
  else
    if bank == 0x7E or bank == 0x7F then return nil, "banks $7E and $7F are the work RAM" end
    if (bank & 0x40) == 0 and lo < 0x8000 then return nil, sfmt("%02X:%04X is not the ROM (HiROM: $8000 to $FFFF in banks $00-$3F)", bank, lo) end
    off = (bank & 0x3F) * 0x10000 + lo
    if rom.map == "exhi" and (bank & 0x80) == 0 then off = off + 0x400000 end
  end
  if off >= rom.size then
    if rom.size > 0 and rom.map ~= "exhi" then off = off % rom.size                  -- a small ROM is mirrored
    else return nil, sfmt("%02X:%04X is past the end of the ROM", bank, lo) end
  end
  return off
end

function R.addr(rom, off)
  if rom.map == "lo" then return ((0x80 + off // 0x8000) << 16) | (0x8000 + off % 0x8000) end
  if rom.map == "exhi" and off >= 0x400000 then return ((0x40 + (off - 0x400000) // 0x10000) << 16) | (off % 0x10000) end
  return ((0xC0 + off // 0x10000) << 16) | (off % 0x10000)
end

local VEC = { { "reset", 0x3C, false }, { "nmi", 0x2A, true }, { "irq", 0x2E, true }, { "brk", 0x26, true },
              { "cop", 0x24, true }, { "nmi_emu", 0x3A, false }, { "irq_emu", 0x3E, false } }
function R.vectors(rom)
  local out, seen = {}, {}
  if rom.size < rom.h + 0x40 then return out end
  for _, v in ipairs(VEC) do
    local a = w16(rom.bytes, rom.h + v[2])
    local off = a >= 0x8000 and R.offset(rom, a)
    if off and not seen[v[1]] then out[#out + 1] = { name = v[1], addr = a, native = v[3], off = off }; seen[v[1]] = true end
  end
  return out
end

local MAPNAME = { lo = "LoROM", hi = "HiROM", exhi = "ExHiROM" }
function R.about(rom, name)
  local kind = rom.kind or 0
  local parts = kind == 0 and "ROM" or kind == 1 and "ROM + RAM" or kind == 2 and "ROM + RAM + battery" or nil
  if not parts then
    local chip = CHIP[kind >> 4] or (kind >> 4 == 0xE and "other chip") or (kind >> 4 == 0xF and "custom chip (SPC7110, ST010, Cx4)") or "?"
    parts = "ROM + " .. chip .. ((kind & 0x0F) >= 4 and " + RAM" or "") .. ((kind & 0x0F) % 3 == 2 and " + battery" or "")
  end
  local out = {
    sfmt("; %s: read only. This is a disassembly of the ROM: nothing is written back.", name or "the ROM"),
    sfmt("; title \"%s\"  %s%s  %d KB (%d KB in the header)  %s", rom.title or "", MAPNAME[rom.map] or rom.map,
         rom.fast and " (fast)" or "", rom.size // 1024, rom.rom_kb or 0, parts),
    sfmt("; save RAM %d KB  region %s  version 1.%d%s", rom.sram_kb or 0, rom.region or "?", rom.version or 0,
         rom.copier > 0 and "  (a 512-byte copier header was cut)" or ""),
  }
  if not rom.sure then out[#out + 1] = "; the header does not look right: the map is a guess" end
  local v = {}
  for _, e in ipairs(R.vectors(rom)) do v[#v + 1] = sfmt("%s %02X:%04X", e.name, e.addr >> 16, e.addr & 0xFFFF) end
  if #v > 0 then out[#out + 1] = "; vectors: " .. table.concat(v, "  ") end
  return out
end

----------------------------------------------------------------- the disassembler

local function hex(v, n) return sfmt("$%0" .. n .. "X", v) end

-- one instruction at the offset: mnemonic, mode, length, the operand value (and the second one of mvn, mvp)
local function decode(s, off, m8, x8)
  local op = sbyte(s, off + 1)
  local e = OPS[op]
  if not e then return nil end
  local mn, mode = e[1], e[2]
  local n = LEN[mode]
  if mode == "imm" then
    local c = IMM[mn]
    n = (c == "m" and not m8 or c == "x" and not x8) and 3 or 2
  end
  if off + n > #s then return nil end
  local v, v2 = 0, nil
  for i = n - 1, 1, -1 do v = (v << 8) | sbyte(s, off + 1 + i) end
  if mode == "blk" then v, v2 = sbyte(s, off + 3), sbyte(s, off + 2) end     -- the source bank, then the destination
  return mn, mode, n, v, v2
end

-- the target of a jump or a branch (a CPU address), or nil
local function target(mn, mode, v, pc, n)
  if mode == "rel" then local d = v >= 0x80 and v - 0x100 or v; return (pc & 0xFF0000) | ((pc + n + d) & 0xFFFF) end
  if mode == "rell" and mn ~= "per" then local d = v >= 0x8000 and v - 0x10000 or v; return (pc & 0xFF0000) | ((pc + n + d) & 0xFFFF) end
  if mode == "abs" and (mn == "jmp" or mn == "jsr") then return (pc & 0xFF0000) | v end
  if mode == "long" and (mn == "jml" or mn == "jsl" or mn == "jmp" or mn == "jsr") then return v end
end

local FMT = {
  imp = function() return "" end, acc = function() return "a" end,
  dp = function(v) return hex(v, 2) end, dpx = function(v) return hex(v, 2) .. ",x" end,
  dpy = function(v) return hex(v, 2) .. ",y" end,
  abs = function(v) return (v < 0x100 and "a:" or "") .. hex(v, 4) end,
  absx = function(v) return (v < 0x100 and "a:" or "") .. hex(v, 4) .. ",x" end,
  absy = function(v) return (v < 0x100 and "a:" or "") .. hex(v, 4) .. ",y" end,
  long = function(v) return (v < 0x10000 and "f:" or "") .. hex(v, 6) end,
  longx = function(v) return (v < 0x10000 and "f:" or "") .. hex(v, 6) .. ",x" end,
  ind = function(v) return "(" .. hex(v, 2) .. ")" end, indx = function(v) return "(" .. hex(v, 2) .. ",x)" end,
  indy = function(v) return "(" .. hex(v, 2) .. "),y" end, indl = function(v) return "[" .. hex(v, 2) .. "]" end,
  indly = function(v) return "[" .. hex(v, 2) .. "],y" end, sr = function(v) return hex(v, 2) .. ",s" end,
  sry = function(v) return "(" .. hex(v, 2) .. ",s),y" end, absind = function(v) return "(" .. hex(v, 4) .. ")" end,
  absindx = function(v) return "(" .. hex(v, 4) .. ",x)" end, absindl = function(v) return "[" .. hex(v, 4) .. "]" end,
  sig = function(v) return "#" .. hex(v, 2) end,
}

-- the last offset of the bank of an address (the code does not run past it)
local function bank_end(rom, start, off)
  local room = rom.map == "lo" and (0x10000 - (start & 0xFFFF)) or (0x10000 - (start & 0xFFFF))
  return math.min(rom.size, off + room) - 1
end

function R.disasm(rom, start, opts)
  opts = opts or {}
  local s = rom.bytes
  local off0, why = R.offset(rom, start)
  if not off0 then return nil, why end
  local last = math.min(bank_end(rom, start, off0), opts.last or math.maxinteger)
  local m8, x8 = opts.m8 ~= false, opts.x8 ~= false
  -- pass 1: the instructions and the widths
  local list, at, starts = {}, off0, {}
  while at <= last do
    local mn, mode, n, v, v2 = decode(s, at, m8, x8)
    if not mn or at + n - 1 > last then
      list[#list + 1] = { off = at, data = sbyte(s, at + 1) }
      at = at + 1
    else
      local it = { off = at, mn = mn, mode = mode, n = n, v = v, v2 = v2, m8 = m8, x8 = x8 }
      list[#list + 1] = it
      starts[at] = true
      if mn == "rep" then
        if v & 0x20 ~= 0 then m8 = false end
        if v & 0x10 ~= 0 then x8 = false end
        it.after = { m8, x8 }
      elseif mn == "sep" then
        if v & 0x20 ~= 0 then m8 = true end
        if v & 0x10 ~= 0 then x8 = true end
        it.after = { m8, x8 }
      end
      at = at + n
    end
  end
  -- the labels: the vectors and the names given, then each jump or branch to an instruction of this range
  local names = {}
  for _, e in ipairs(R.vectors(rom)) do if starts[e.off] then names[e.off] = names[e.off] or e.name end end
  for o, nm in pairs(opts.names or {}) do if starts[o] then names[o] = nm end end
  local base = start - off0                       -- CPU address = base + offset, inside this bank
  for _, it in ipairs(list) do
    if it.mn then
      local t = target(it.mn, it.mode, it.v, base + it.off, it.n)
      local to = t and R.offset(rom, t)
      if to and starts[to] then
        names[to] = names[to] or sfmt("L%06X", base + to)
        it.label = names[to]
      end
    end
  end
  -- pass 2: the text
  local out, addr, line_of = {}, {}, {}
  for _, l in ipairs(R.about(rom, opts.name)) do out[#out + 1] = l end
  out[#out + 1] = sfmt("; from %02X:%04X to the end of the bank. The widths at the start: %s %s (rep and sep change them below).",
                       start >> 16, start & 0xFFFF, m8 and ".a8" or ".a16", x8 and ".i8" or ".i16")
  out[#out + 1] = ""
  out[#out + 1] = sfmt("    .org $%06X", start)
  out[#out + 1] = (opts.m8 ~= false) and "    .a8" or "    .a16"
  out[#out + 1] = (opts.x8 ~= false) and "    .i8" or "    .i16"
  local first = #out + 1
  local db = {}
  local function flush()
    if #db == 0 then return end
    out[#out + 1] = sfmt("    %-26s; %06X  data", ".db " .. table.concat(db.t, ","), db.a)
    addr[#out] = db.a
    db = {}
  end
  for _, it in ipairs(list) do
    local pc = base + it.off
    if names[it.off] then
      flush()
      out[#out + 1] = ""
      out[#out + 1] = names[it.off] .. ":"
      addr[#out] = pc
    end
    if it.data then
      if #db == 0 then db = { a = pc, t = {} } end
      db.t[#db.t + 1] = hex(it.data, 2)
      line_of[it.off] = #out + 1
      if #db.t == 8 then flush() end
    else
      flush()
      local opnd
      if it.label then opnd = it.label
      elseif it.mode == "imm" then opnd = "#" .. hex(it.v, it.n == 3 and 4 or 2)
      elseif it.mode == "rel" or it.mode == "rell" then
        local t = target(it.mn, it.mode, it.v, pc, it.n) or 0
        opnd = hex(t & 0xFFFF, 4)
      elseif it.mode == "blk" then opnd = hex(it.v, 2) .. "," .. hex(it.v2, 2)
      else opnd = FMT[it.mode](it.v) end
      local ins = it.mn .. (opnd ~= "" and (" " .. opnd) or "")
      local b = {}
      for i = 0, it.n - 1 do b[#b + 1] = sfmt("%02X", sbyte(s, it.off + 1 + i)) end
      out[#out + 1] = sfmt("    %-26s; %02X:%04X  %s", ins, pc >> 16, pc & 0xFFFF, table.concat(b, " "))
      addr[#out] = pc
      line_of[it.off] = #out
      if it.after then
        local a, x = it.after[1], it.after[2]
        if a ~= it.m8 then out[#out + 1] = a and "    .a8" or "    .a16" end
        if x ~= it.x8 then out[#out + 1] = x and "    .i8" or "    .i16" end
      end
      if it.mn == "rts" or it.mn == "rtl" or it.mn == "rti" or it.mn == "jmp" or it.mn == "jml" or it.mn == "bra" or it.mn == "brl" then
        out[#out + 1] = ""
      end
    end
  end
  flush()
  return { text = table.concat(out, "\n"), addr = addr, line_of = line_of, first = first, start = start,
           off0 = off0, last = last, base = base, names = names }
end

-- the line of the instruction at a CPU address, if this disassembly has it
function R.line_of(d, rom, pc)
  local off = R.offset(rom, pc)
  return off and d.line_of[off]
end

----------------------------------------------------------------- the tiles

function R.page_bytes(w, h, bpp) return (w // 8) * (h // 8) * bpp * 8 end

function R.tiles(rom, off, w, h, bpp, decode_tile)
  local s, tb, cols = rom.bytes, bpp * 8, w // 8
  local out = {}
  local zero = string.rep("\0", tb)
  for t = 0, cols * (h // 8) - 1 do
    local at = off + t * tb
    local px
    if at + tb <= #s then px = decode_tile(s, bpp, at + 1)
    elseif at < #s then px = decode_tile(s:sub(at + 1) .. zero, bpp, 1)
    end
    local x0, y0 = (t % cols) * 8, (t // cols) * 8
    for y = 0, 7 do
      local row = (y0 + y) * w + x0
      for x = 0, 7 do out[row + x] = px and px[y * 8 + x + 1] or 0 end
    end
  end
  return out
end

return R
