-- snesrun.lua: the helpers of the Run tab of bm SNES SDK (debug panel): flags, registers, hex view,
-- breakpoints and the map between source lines and addresses. No bm API and no drawing: the PC tests run it
-- (tests/snes/run_test.lua). The caller gives the functions of the debug API (snes_peek, snes_mem).
--
--   local R = loadfile("core/lua/snesrun.lua")()
--   R.flags(p, e)             { {name, on}, ... } the flags of the P register, in the order N V M X D I Z C
--   R.regs_text(r)            the register lines for the panel
--   R.flags_words(p, e)       the set flags in words
--   R.parse_addr(text)        "$7E0010", "0x10", "7E:0010" or a hex number -> number, or nil
--   R.parse_poke(text)        "7E0010=12 34" or "7E0010 12" -> addr, { bytes }, or nil and a reason
--   R.hexdump(str, base, w)   lines "ADDR  xx xx ... |text|" of a string of bytes
--   R.read(space, addr, n, api)  the bytes of a space: "ram" (the 24-bit CPU space), "vram", "cgram", "oam"
--   R.addr_of_line(lines, file, line)  the address of the first instruction on or after the line
--   R.line_of_addr(lines, pc24)        file, line of the instruction at pc24
--   R.toggle(set, addr)       sets or clears a breakpoint in the table set: true (set), false (cleared) or nil, reason (8 are set)
--   R.list(set)               the breakpoints, sorted

local R = {}

R.MAX_BREAK = 8
R.spaces = { "ram", "vram", "cgram", "oam" }
R.space_size = { ram = 0x1000000, vram = 65536, cgram = 512, oam = 544 }
R.flag_names = {
  { "N", 0x80, "negative" }, { "V", 0x40, "overflow" }, { "M", 0x20, "A is 8 bit" }, { "X", 0x10, "X and Y are 8 bit" },
  { "D", 0x08, "decimal mode" }, { "I", 0x04, "IRQ off" }, { "Z", 0x02, "zero" }, { "C", 0x01, "carry" },
}

function R.flags(p, e)
  local out = {}
  for _, f in ipairs(R.flag_names) do
    local name = f[1]
    if e and f[2] == 0x20 then name = "-"             -- emulation mode: bit 5 is not used
    elseif e and f[2] == 0x10 then name = "B" end     -- emulation mode: bit 4 is the break flag
    out[#out + 1] = { name = name, on = (p & f[2]) ~= 0, about = f[3] }
  end
  return out
end

function R.flags_text(p, e)
  local t = {}
  for _, f in ipairs(R.flags(p, e)) do t[#t + 1] = f.on and f.name or f.name:lower() end
  return table.concat(t, " ")
end

local function hex(v, n) return string.format("%0" .. n .. "X", v) end

-- the registers as lines (short: the panel is narrow); A always has 4 digits (the high byte stays when M is set);
-- X and Y have the width of the X flag
function R.regs_text(r)
  local x8 = r.e or (r.p & 0x10) ~= 0
  local xm, xd = x8 and 0xFF or 0xFFFF, x8 and 2 or 4
  return {
    string.format("A %s  X %s  Y %s", hex(r.a, 4), hex(r.x & xm, xd), hex(r.y & xm, xd)),
    string.format("S %s  D %s  DB %s", hex(r.s, 4), hex(r.d, 4), hex(r.db, 2)),
    string.format("PB %s  PC %s  P %s", hex(r.pb, 2), hex(r.pc, 4), hex(r.p, 2)),
    R.flags_text(r.p, r.e) .. (r.e and "  emulation" or "  native"),
  }
end

-- the flags that are set, in words
function R.flags_words(p, e)
  local t = {}
  for _, f in ipairs(R.flags(p, e)) do if f.on then t[#t + 1] = f.about end end
  return #t > 0 and table.concat(t, ", ") or "no flag is set"
end

function R.parse_addr(text)
  if type(text) ~= "string" then return nil end
  text = text:gsub("%s+", ""):gsub("^%$", ""):gsub("^0[xX]", "")
  local bank, low = text:match("^(%x+):(%x+)$")
  if bank then
    if #bank > 2 or #low > 4 then return nil end
    return tonumber(bank, 16) << 16 | tonumber(low, 16)
  end
  if not text:match("^%x+$") or #text > 6 then return nil end
  return tonumber(text, 16)
end

function R.parse_poke(text)
  local a, rest = tostring(text):match("^%s*([^=%s]+)%s*[=%s]%s*(.-)%s*$")
  if not a then return nil, "write: address=byte, for example 7E0010=1F" end
  local addr = R.parse_addr(a)
  if not addr then return nil, "the address is a hex number: $7E0010 or 7E:0010" end
  local bytes = {}
  for w in rest:gmatch("%S+") do
    w = w:gsub("^%$", ""):gsub("^0[xX]", "")
    if not w:match("^%x%x?$") then return nil, "a byte is 1 or 2 hex digits (00 to FF)" end
    bytes[#bytes + 1] = tonumber(w, 16)
  end
  if #bytes == 0 then return nil, "no byte to write" end
  return addr, bytes
end

local function printable(b) return (b >= 32 and b < 127) and string.char(b) or "." end

function R.hexdump(str, base, w)
  w = w or 8
  local lines = {}
  for i = 1, #str, w do
    local hx, tx = {}, {}
    for j = i, math.min(i + w - 1, #str) do
      local b = str:byte(j)
      hx[#hx + 1] = hex(b, 2)
      tx[#tx + 1] = printable(b)
    end
    local pad = string.rep("   ", w - #hx)
    local a = base + i - 1
    lines[#lines + 1] = string.format("%s  %s%s |%s|", hex(a, a > 0xFFFF and 6 or 4), table.concat(hx, " "), pad, table.concat(tx))
  end
  return lines
end

-- api = { peek = snes_peek, mem = snes_mem }
function R.read(space, addr, n, api)
  local size = R.space_size[space]
  if not size then return nil, "unknown memory" end
  if addr < 0 or addr >= size then return nil, "address outside " .. space end
  if addr + n > size then n = size - addr end
  if space == "ram" then return api.peek(addr, n) end
  return api.mem(space, addr, n)
end

function R.addr_of_line(lines, file, line)
  local best, bl
  for _, l in ipairs(lines or {}) do
    if l.file == file and l.line >= line and (not bl or l.line < bl) then best, bl = l.pc, l.line end
  end
  return best, bl
end

function R.line_of_addr(lines, pc24)
  for _, l in ipairs(lines or {}) do
    if (l.pc & 0x7FFFFF) == (pc24 & 0x7FFFFF) then return l.file, l.line end
  end
end

function R.list(set)
  local t = {}
  for a in pairs(set) do t[#t + 1] = a end
  table.sort(t)
  return t
end

function R.toggle(set, addr)
  if set[addr] then set[addr] = nil; return false end
  if #R.list(set) >= R.MAX_BREAK then return nil, "8 breakpoints are set: clear one first" end
  set[addr] = true
  return true
end

return R
