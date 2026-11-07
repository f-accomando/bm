-- bm SNES, part 0: the sound CPU (SPC700) and the boot protocol of its ROM.
-- The sound chip is a second computer: the game sends it a sound program through four ports, then talks to
-- it through the same ports. Games wait for its answers, so the SPC700 runs here. The DSP (the sound itself)
-- is not emulated: its registers are stored and read back. The boot ROM (the transfer loop) is played by
-- apu_write: it needs no firmware. Original code.

local apu_reset, apu_write, apu_read, apu_run
local apu_in, apu_out = { 0, 0, 0, 0 }, { 0xAA, 0xBB, 0, 0 }

do

local ram = {}
for i = 1, 65536 do ram[i] = 0 end
local dsp = {}
for i = 1, 128 do dsp[i] = 0 end
local A, X, Y, SP, PC = 0, 0, 0, 0xEF, 0
local fN, fV, fP, fB, fH, fI, fZ, fC = 0, 0, 0, 0, 0, 0, 0, 0
local ctl, dspaddr = 0x80, 0
local tgt, tstage, tout, tenable = { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { false, false, false }
local acc16, acc128 = 0, 0
local running = false            -- true: the SPC700 runs the game's sound program
local halted = false
local ipl_state, ipl_dest, ipl_expect, ipl_pending = 0, 0, 0, false
local sops = {}

local function rd(a)
  if a < 0xF0 or a > 0xFF then return ram[a + 1] end
  if a == 0xF2 then return dspaddr
  elseif a == 0xF3 then return dsp[(dspaddr & 0x7F) + 1]
  elseif a >= 0xF4 and a <= 0xF7 then return apu_in[a - 0xF3]
  elseif a >= 0xFD then
    local v = tout[a - 0xFC]; tout[a - 0xFC] = 0; return v
  end
  return ram[a + 1]
end

local function wr(a, v)
  ram[a + 1] = v
  if a < 0xF0 or a > 0xFF then return end
  if a == 0xF1 then
    for i = 1, 3 do
      local on = (v >> (i - 1)) & 1 == 1
      if on and not tenable[i] then tstage[i] = 0; tout[i] = 0 end
      tenable[i] = on
    end
    if v & 0x10 ~= 0 then apu_in[1] = 0; apu_in[2] = 0 end
    if v & 0x20 ~= 0 then apu_in[3] = 0; apu_in[4] = 0 end
    ctl = v
  elseif a == 0xF2 then dspaddr = v
  elseif a == 0xF3 then if dspaddr < 128 then dsp[dspaddr + 1] = v end
  elseif a >= 0xF4 and a <= 0xF7 then apu_out[a - 0xF3] = v
  elseif a >= 0xFA and a <= 0xFC then tgt[a - 0xF9] = v
  end
end

local function f8() local v = rd(PC); PC = (PC + 1) & 0xFFFF; return v end
local function f16() local v = rd(PC) | (rd((PC + 1) & 0xFFFF) << 8); PC = (PC + 2) & 0xFFFF; return v end
local function dpa(d) return (fP << 8) | d end
local function dp16(d) return rd(dpa(d)) | (rd(dpa((d + 1) & 255)) << 8) end
local function nz(v) fN = (v >> 7) & 1; fZ = (v == 0) and 1 or 0; return v end
local function push(v) wr(0x100 | SP, v); SP = (SP - 1) & 255 end
local function pop() SP = (SP + 1) & 255; return rd(0x100 | SP) end
local function getP()
  return (fN << 7) | (fV << 6) | (fP << 5) | (fB << 4) | (fH << 3) | (fI << 2) | (fZ << 1) | fC
end
local function setP(v)
  fN = (v >> 7) & 1; fV = (v >> 6) & 1; fP = (v >> 5) & 1; fB = (v >> 4) & 1
  fH = (v >> 3) & 1; fI = (v >> 2) & 1; fZ = (v >> 1) & 1; fC = v & 1
end
local function rel(cond)
  local r = f8()
  if cond then PC = (PC + (r >= 128 and r - 256 or r)) & 0xFFFF end
end

------------------------------------------------------------------ arithmetic
local function adc(a, b)
  local r = a + b + fC
  fH = (((a & 15) + (b & 15) + fC) > 15) and 1 or 0
  fV = (((~(a ~ b)) & (a ~ r) & 0x80) ~= 0) and 1 or 0
  fC = r > 255 and 1 or 0
  return nz(r & 255)
end
local function sbc(a, b) return adc(a, b ~ 255) end
local function cmp(a, b)
  local r = a - b
  fC = r >= 0 and 1 or 0
  nz(r & 255)
end
local function f_or(a, b) return nz(a | b) end
local function f_and(a, b) return nz(a & b) end
local function f_eor(a, b) return nz(a ~ b) end
local function asl(v) fC = v >> 7; return nz((v << 1) & 255) end
local function lsr(v) fC = v & 1; return nz(v >> 1) end
local function rol(v) local c = fC; fC = v >> 7; return nz(((v << 1) | c) & 255) end
local function ror(v) local c = fC; fC = v & 1; return nz((v >> 1) | (c << 7)) end
local function inc(v) return nz((v + 1) & 255) end
local function dec(v) return nz((v - 1) & 255) end

------------------------------------------------------------------ the ALU family on A and on memory
-- the operand of mode m: returns the address, or nil for an immediate
local function addr_of(m)
  if m == 0x04 then return dpa(f8())
  elseif m == 0x05 then return f16()
  elseif m == 0x06 then return dpa(X)
  elseif m == 0x07 then return dp16((f8() + X) & 255)
  elseif m == 0x14 then return dpa((f8() + X) & 255)
  elseif m == 0x15 then return (f16() + X) & 0xFFFF
  elseif m == 0x16 then return (f16() + Y) & 0xFFFF
  elseif m == 0x17 then return (dp16(f8()) + Y) & 0xFFFF
  end
end

local function alu(base, fn, store)
  for _, m in ipairs({ 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17 }) do
    sops[base + m] = function()
      local r = fn(A, rd(addr_of(m)))
      if store then A = r end
    end
  end
  sops[base + 0x08] = function() local r = fn(A, f8()); if store then A = r end end
  sops[base + 0x09] = function()
    local s = rd(dpa(f8())); local d = dpa(f8())
    local r = fn(rd(d), s); if store then wr(d, r) end
  end
  sops[base + 0x18] = function()
    local i = f8(); local d = dpa(f8())
    local r = fn(rd(d), i); if store then wr(d, r) end
  end
  sops[base + 0x19] = function()
    local sy = rd(dpa(Y)); local dx = dpa(X)
    local r = fn(rd(dx), sy); if store then wr(dx, r) end
  end
end
alu(0x00, f_or, true); alu(0x20, f_and, true); alu(0x40, f_eor, true)
alu(0x60, cmp, false); alu(0x80, adc, true); alu(0xA0, sbc, true)

-- X and Y compares
sops[0xC8] = function() cmp(X, f8()) end
sops[0x3E] = function() cmp(X, rd(dpa(f8()))) end
sops[0x1E] = function() cmp(X, rd(f16())) end
sops[0xAD] = function() cmp(Y, f8()) end
sops[0x7E] = function() cmp(Y, rd(dpa(f8()))) end
sops[0x5E] = function() cmp(Y, rd(f16())) end

-- shifts, inc, dec: on d, !a, d+X, and A
local function rmw(od, oa, odx, oA, fn)
  sops[od] = function() local a = dpa(f8()); wr(a, fn(rd(a))) end
  sops[oa] = function() local a = f16(); wr(a, fn(rd(a))) end
  if odx then sops[odx] = function() local a = dpa((f8() + X) & 255); wr(a, fn(rd(a))) end end
  sops[oA] = function() A = fn(A) end
end
rmw(0x0B, 0x0C, 0x1B, 0x1C, asl); rmw(0x4B, 0x4C, 0x5B, 0x5C, lsr)
rmw(0x2B, 0x2C, 0x3B, 0x3C, rol); rmw(0x6B, 0x6C, 0x7B, 0x7C, ror)
rmw(0xAB, 0xAC, 0xBB, 0xBC, inc); rmw(0x8B, 0x8C, 0x9B, 0x9C, dec)
sops[0x3D] = function() X = inc(X) end
sops[0x1D] = function() X = dec(X) end
sops[0xFC] = function() Y = inc(Y) end
sops[0xDC] = function() Y = dec(Y) end

------------------------------------------------------------------ moves
sops[0xE8] = function() A = nz(f8()) end
sops[0xE4] = function() A = nz(rd(dpa(f8()))) end
sops[0xF4] = function() A = nz(rd(dpa((f8() + X) & 255))) end
sops[0xE5] = function() A = nz(rd(f16())) end
sops[0xF5] = function() A = nz(rd((f16() + X) & 0xFFFF)) end
sops[0xF6] = function() A = nz(rd((f16() + Y) & 0xFFFF)) end
sops[0xE6] = function() A = nz(rd(dpa(X))) end
sops[0xE7] = function() A = nz(rd(dp16((f8() + X) & 255))) end
sops[0xF7] = function() A = nz(rd((dp16(f8()) + Y) & 0xFFFF)) end
sops[0xBF] = function() A = nz(rd(dpa(X))); X = (X + 1) & 255 end
sops[0xCD] = function() X = nz(f8()) end
sops[0xF8] = function() X = nz(rd(dpa(f8()))) end
sops[0xF9] = function() X = nz(rd(dpa((f8() + Y) & 255))) end
sops[0xE9] = function() X = nz(rd(f16())) end
sops[0x8D] = function() Y = nz(f8()) end
sops[0xEB] = function() Y = nz(rd(dpa(f8()))) end
sops[0xFB] = function() Y = nz(rd(dpa((f8() + X) & 255))) end
sops[0xEC] = function() Y = nz(rd(f16())) end
sops[0x7D] = function() A = nz(X) end
sops[0xDD] = function() A = nz(Y) end
sops[0x5D] = function() X = nz(A) end
sops[0xFD] = function() Y = nz(A) end
sops[0x9D] = function() X = nz(SP) end
sops[0xBD] = function() SP = X end
sops[0xC4] = function() wr(dpa(f8()), A) end
sops[0xD4] = function() wr(dpa((f8() + X) & 255), A) end
sops[0xC5] = function() wr(f16(), A) end
sops[0xD5] = function() wr((f16() + X) & 0xFFFF, A) end
sops[0xD6] = function() wr((f16() + Y) & 0xFFFF, A) end
sops[0xC6] = function() wr(dpa(X), A) end
sops[0xC7] = function() wr(dp16((f8() + X) & 255), A) end
sops[0xD7] = function() wr((dp16(f8()) + Y) & 0xFFFF, A) end
sops[0xAF] = function() wr(dpa(X), A); X = (X + 1) & 255 end
sops[0xD8] = function() wr(dpa(f8()), X) end
sops[0xD9] = function() wr(dpa((f8() + Y) & 255), X) end
sops[0xC9] = function() wr(f16(), X) end
sops[0xCB] = function() wr(dpa(f8()), Y) end
sops[0xDB] = function() wr(dpa((f8() + X) & 255), Y) end
sops[0xCC] = function() wr(f16(), Y) end
sops[0x8F] = function() local i = f8(); wr(dpa(f8()), i) end
sops[0xFA] = function() local s = rd(dpa(f8())); wr(dpa(f8()), s) end

-- 16 bits
sops[0xBA] = function()
  local d = f8()
  A = rd(dpa(d)); Y = rd(dpa((d + 1) & 255))
  fN = Y >> 7; fZ = (A == 0 and Y == 0) and 1 or 0
end
sops[0xDA] = function() local d = f8(); wr(dpa(d), A); wr(dpa((d + 1) & 255), Y) end
sops[0x7A] = function()
  local d = f8()
  fC = 0; A = adc(A, rd(dpa(d))); Y = adc(Y, rd(dpa((d + 1) & 255)))
  fZ = (A == 0 and Y == 0) and 1 or 0
end
sops[0x9A] = function()
  local d = f8()
  fC = 1; A = sbc(A, rd(dpa(d))); Y = sbc(Y, rd(dpa((d + 1) & 255)))
  fZ = (A == 0 and Y == 0) and 1 or 0
end
sops[0x5A] = function()
  local m = dp16(f8())
  local r = ((Y << 8) | A) - m
  fC = r >= 0 and 1 or 0
  r = r & 0xFFFF
  fN = r >> 15; fZ = (r == 0) and 1 or 0
end
sops[0x3A] = function()
  local d = f8(); local r = (dp16(d) + 1) & 0xFFFF
  wr(dpa(d), r & 255); wr(dpa((d + 1) & 255), r >> 8)
  fN = r >> 15; fZ = (r == 0) and 1 or 0
end
sops[0x1A] = function()
  local d = f8(); local r = (dp16(d) - 1) & 0xFFFF
  wr(dpa(d), r & 255); wr(dpa((d + 1) & 255), r >> 8)
  fN = r >> 15; fZ = (r == 0) and 1 or 0
end

------------------------------------------------------------------ stack, flags, jumps
sops[0x2D] = function() push(A) end
sops[0x4D] = function() push(X) end
sops[0x6D] = function() push(Y) end
sops[0x0D] = function() push(getP()) end
sops[0xAE] = function() A = pop() end
sops[0xCE] = function() X = pop() end
sops[0xEE] = function() Y = pop() end
sops[0x8E] = function() setP(pop()) end
sops[0x00] = function() end
sops[0x20] = function() fP = 0 end
sops[0x40] = function() fP = 1 end
sops[0x60] = function() fC = 0 end
sops[0x80] = function() fC = 1 end
sops[0xED] = function() fC = fC ~ 1 end
sops[0xE0] = function() fV = 0; fH = 0 end
sops[0xA0] = function() fI = 1 end
sops[0xC0] = function() fI = 0 end
sops[0xEF] = function() halted = true end
sops[0xFF] = function() halted = true end
sops[0x10] = function() rel(fN == 0) end
sops[0x30] = function() rel(fN == 1) end
sops[0x50] = function() rel(fV == 0) end
sops[0x70] = function() rel(fV == 1) end
sops[0x90] = function() rel(fC == 0) end
sops[0xB0] = function() rel(fC == 1) end
sops[0xD0] = function() rel(fZ == 0) end
sops[0xF0] = function() rel(fZ == 1) end
sops[0x2F] = function() rel(true) end
sops[0x5F] = function() PC = f16() end
sops[0x1F] = function() local a = (f16() + X) & 0xFFFF; PC = rd(a) | (rd((a + 1) & 0xFFFF) << 8) end
sops[0x3F] = function() local a = f16(); push(PC >> 8); push(PC & 255); PC = a end
sops[0x4F] = function() local u = f8(); push(PC >> 8); push(PC & 255); PC = 0xFF00 | u end
sops[0x6F] = function() local lo = pop(); PC = lo | (pop() << 8) end
sops[0x7F] = function() setP(pop()); local lo = pop(); PC = lo | (pop() << 8) end
sops[0x0F] = function()
  push(PC >> 8); push(PC & 255); push(getP())
  fB = 1; fI = 0
  PC = rd(0xFFDE) | (rd(0xFFDF) << 8)
end
for n = 0, 15 do
  sops[0x01 + 0x10 * n] = function()
    push(PC >> 8); push(PC & 255)
    local v = 0xFFDE - 2 * n
    PC = rd(v) | (rd(v + 1) << 8)
  end
end
sops[0x2E] = function() local a = dpa(f8()); rel(A ~= rd(a)) end
sops[0xDE] = function() local a = dpa((f8() + X) & 255); rel(A ~= rd(a)) end
sops[0x6E] = function() local a = dpa(f8()); local v = (rd(a) - 1) & 255; wr(a, v); rel(v ~= 0) end
sops[0xFE] = function() Y = (Y - 1) & 255; rel(Y ~= 0) end

-- bits
for b = 0, 7 do
  sops[0x02 + 0x20 * b] = function() local a = dpa(f8()); wr(a, rd(a) | (1 << b)) end
  sops[0x12 + 0x20 * b] = function() local a = dpa(f8()); wr(a, rd(a) & ~(1 << b)) end
  sops[0x03 + 0x20 * b] = function() local v = rd(dpa(f8())); rel(v & (1 << b) ~= 0) end
  sops[0x13 + 0x20 * b] = function() local v = rd(dpa(f8())); rel(v & (1 << b) == 0) end
end
local function mbit()
  local m = f16()
  return m & 0x1FFF, m >> 13
end
sops[0x0A] = function() local a, b = mbit(); if (rd(a) >> b) & 1 == 1 then fC = 1 end end
sops[0x2A] = function() local a, b = mbit(); if (rd(a) >> b) & 1 == 0 then fC = 1 end end
sops[0x4A] = function() local a, b = mbit(); if (rd(a) >> b) & 1 == 0 then fC = 0 end end
sops[0x6A] = function() local a, b = mbit(); if (rd(a) >> b) & 1 == 1 then fC = 0 end end
sops[0x8A] = function() local a, b = mbit(); fC = fC ~ ((rd(a) >> b) & 1) end
sops[0xAA] = function() local a, b = mbit(); fC = (rd(a) >> b) & 1 end
sops[0xCA] = function() local a, b = mbit(); local v = rd(a); wr(a, fC == 1 and (v | (1 << b)) or (v & ~(1 << b))) end
sops[0xEA] = function() local a, b = mbit(); wr(a, rd(a) ~ (1 << b)) end
sops[0x0E] = function() local a = f16(); local v = rd(a); nz((A - v) & 255); wr(a, v | A) end
sops[0x4E] = function() local a = f16(); local v = rd(a); nz((A - v) & 255); wr(a, v & ~A) end

-- the odd ones
sops[0x9F] = function() A = ((A << 4) | (A >> 4)) & 255; nz(A) end
sops[0xCF] = function() local r = Y * A; A = r & 255; Y = r >> 8; nz(Y) end
sops[0x9E] = function()
  local ya = (Y << 8) | A
  fV = (Y >= X) and 1 or 0
  fH = ((Y & 15) >= (X & 15)) and 1 or 0
  if Y < (X << 1) then
    if X == 0 then A = 255; Y = 255 else A = (ya // X) & 255; Y = ya % X end
  else
    local d = ya - (X << 9)
    A = (255 - d // (256 - X)) & 255
    Y = (X + d % (256 - X)) & 255
  end
  nz(A)
end
sops[0xDF] = function()
  if fC == 1 or A > 0x99 then A = (A + 0x60) & 255; fC = 1 end
  if fH == 1 or (A & 15) > 9 then A = (A + 6) & 255 end
  nz(A)
end
sops[0xBE] = function()
  if fC == 0 or A > 0x99 then A = (A - 0x60) & 255; fC = 0 end
  if fH == 0 or (A & 15) > 9 then A = (A - 6) & 255 end
  nz(A)
end
for i = 0, 255 do if not sops[i] then sops[i] = function() end end end

------------------------------------------------------------------ timers and the run loop
local function timers(c)
  acc16 = acc16 + c
  if acc16 < 16 then return end
  local n = acc16 >> 4
  acc16 = acc16 & 15
  if tenable[3] then
    tstage[3] = tstage[3] + n
    local t = tgt[3] == 0 and 256 or tgt[3]
    if tstage[3] >= t then tout[3] = (tout[3] + tstage[3] // t) & 15; tstage[3] = tstage[3] % t end
  end
  acc128 = acc128 + n
  if acc128 < 8 then return end
  local m = acc128 >> 3
  acc128 = acc128 & 7
  for i = 1, 2 do
    if tenable[i] then
      tstage[i] = tstage[i] + m
      local t = tgt[i] == 0 and 256 or tgt[i]
      if tstage[i] >= t then tout[i] = (tout[i] + tstage[i] // t) & 15; tstage[i] = tstage[i] % t end
    end
  end
end

local function ipl_start()
  running = false
  ipl_state = 0
  ipl_pending = false
  apu_out[1], apu_out[2], apu_out[3], apu_out[4] = 0xAA, 0xBB, 0, 0
end

apu_reset = function()
  apu_in[1], apu_in[2], apu_in[3], apu_in[4] = 0, 0, 0, 0
  for i = 1, 65536 do ram[i] = 0 end
  for i = 1, 128 do dsp[i] = 0 end
  ctl, dspaddr, halted = 0x80, 0, false
  for i = 1, 3 do tgt[i], tstage[i], tout[i], tenable[i] = 0, 0, 0, false end
  acc16, acc128 = 0, 0
  A, X, Y, SP = 0, 0, 0, 0xEF
  setP(0)
  ipl_start()
end

-- The boot ROM, as a state machine. A write to port 0 is the command or the next byte; its data is
-- in port 1 (a 16-bit write puts port 1 after port 0, so the work is done at the next read or write).
local function ipl_sync()
  if not ipl_pending then return end
  ipl_pending = false
  local v = apu_in[1]
  local addr = apu_in[3] | (apu_in[4] << 8)
  if ipl_state == 0 then
    if v ~= 0xCC then return end
    if apu_in[2] == 0 then PC = addr; running = true; halted = false; return end
    ipl_dest, ipl_expect, ipl_state = addr, 0, 1
    apu_out[1] = 0xCC
  elseif v == ipl_expect then
    ram[ipl_dest + 1] = apu_in[2]
    ipl_dest = (ipl_dest + 1) & 0xFFFF
    apu_out[1] = v
    ipl_expect = (ipl_expect + 1) & 255
  elseif apu_in[2] == 0 then
    PC = addr; running = true; halted = false
  else
    ipl_dest, ipl_expect = addr, 0
    apu_out[1] = v
  end
end

apu_write = function(n, v)               -- n = 1..4, the CPU writes a port
  if not running then
    if n ~= 2 then ipl_sync() end
    apu_in[n] = v
    if n == 1 then ipl_pending = true end
    return
  end
  apu_in[n] = v
end

apu_read = function(n)                    -- the CPU reads a port
  if not running then ipl_sync() end
  return apu_out[n]
end

-- Runs the SPC700 for about `cycles` of its clock (about 3 per instruction).
apu_run = function(cycles)
  if not running then ipl_sync() end
  if not running or halted then return end
  local n = 0
  while n < cycles do
    if PC >= 0xFFC0 and ctl & 0x80 ~= 0 then      -- the program jumped to the boot ROM: a new transfer
      ipl_start()
      return
    end
    local op = rd(PC)
    PC = (PC + 1) & 0xFFFF
    sops[op]()
    n = n + 3
    if halted then break end
  end
  timers(n)
end

function apu_info() return running, PC, halted end

end
