-- Part 2: the 65C816 CPU.

local cpu_reset, cpu_run
do

local A, X, Y, S, D = 0, 0, 0x1FF, 0x1FF, 0
local DB, PB, PC = 0, 0, 0                    -- DB and PB are kept shifted left by 16
local fN, fV, fM, fX, fD, fI, fZ, fC, fE = 0, 0, 1, 1, 0, 1, 0, 0, 1
local waiting, stopped = false, false
local ops = {}

local function fetch8()
  local v = rd(PB | PC)
  PC = (PC + 1) & 0xFFFF
  return v
end
local function fetch16()
  local v = rd(PB | PC) | (rd(PB | ((PC + 1) & 0xFFFF)) << 8)
  PC = (PC + 2) & 0xFFFF
  return v
end
local function fetch24()
  local v = fetch16()
  return v | (fetch8() << 16)
end

local function getP()
  return (fN << 7) | (fV << 6) | (fM << 5) | (fX << 4) | (fD << 3) | (fI << 2) | (fZ << 1) | fC
end
local function setP(v)
  fN = (v >> 7) & 1; fV = (v >> 6) & 1; fD = (v >> 3) & 1
  fI = (v >> 2) & 1; fZ = (v >> 1) & 1; fC = v & 1
  if fE == 1 then fM = 1; fX = 1 else fM = (v >> 5) & 1; fX = (v >> 4) & 1 end
  if fX == 1 then X = X & 0xFF; Y = Y & 0xFF end
end

local function push8(v)
  wr(S, v & 0xFF)
  S = (S - 1) & 0xFFFF
  if fE == 1 then S = 0x100 | (S & 0xFF) end
end
local function pull8()
  S = (S + 1) & 0xFFFF
  if fE == 1 then S = 0x100 | (S & 0xFF) end
  return rd(S)
end
local function push16(v) push8(v >> 8); push8(v & 0xFF) end
local function pull16() local lo = pull8(); return lo | (pull8() << 8) end

local function nz8(v) fN = (v >> 7) & 1; fZ = (v == 0) and 1 or 0 end
local function nz16(v) fN = (v >> 15) & 1; fZ = (v == 0) and 1 or 0 end

local function interrupt(vn, ve, brk)
  if fE == 0 then push8(PB >> 16) end
  push16(PC)
  local p = getP()
  if fE == 1 then p = (p & ~0x30) | 0x20 | (brk and 0x10 or 0) end
  push8(p)
  fI = 1; fD = 0; PB = 0
  PC = rd16(fE == 1 and ve or vn)
  waiting = false
end

------------------------------------------------------------------ addressing modes (they give a 24-bit address)
local function a_dp() return (D + fetch8()) & 0xFFFF end
local function a_dpx() return (D + fetch8() + X) & 0xFFFF end
local function a_dpy() return (D + fetch8() + Y) & 0xFFFF end
local function a_ind() return DB | rd16((D + fetch8()) & 0xFFFF) end
local function a_indx() return DB | rd16((D + fetch8() + X) & 0xFFFF) end
local function a_indy() return (DB | rd16((D + fetch8()) & 0xFFFF)) + Y & 0xFFFFFF end
local function a_lng()
  local p = (D + fetch8()) & 0xFFFF
  return rd16(p) | (rd((p + 2) & 0xFFFF) << 16)
end
local function a_lngy()
  local p = (D + fetch8()) & 0xFFFF
  return ((rd16(p) | (rd((p + 2) & 0xFFFF) << 16)) + Y) & 0xFFFFFF
end
local function a_abs() return DB | fetch16() end
local function a_absx() return ((DB | fetch16()) + X) & 0xFFFFFF end
local function a_absy() return ((DB | fetch16()) + Y) & 0xFFFFFF end
local function a_long() return fetch24() end
local function a_longx() return (fetch24() + X) & 0xFFFFFF end
local function a_sr() return (S + fetch8()) & 0xFFFF end
local function a_sry() return ((DB | rd16((S + fetch8()) & 0xFFFF)) + Y) & 0xFFFFFF end

local function readM(a) if fM == 1 then return rd(a) end return rd16(a) end
local function writeM(a, v) if fM == 1 then wr(a, v & 0xFF) else wr16(a, v) end end
local function readX(a) if fX == 1 then return rd(a) end return rd16(a) end
local function fetchM() if fM == 1 then return fetch8() end return fetch16() end
local function fetchX() if fX == 1 then return fetch8() end return fetch16() end

------------------------------------------------------------------ ALU
local function bcd_add(a, v, c, nib)
  local r, carry = 0, c
  for i = 0, nib - 1 do
    local sh = i * 4
    local t = ((a >> sh) & 0xF) + ((v >> sh) & 0xF) + carry
    if t > 9 then t = t + 6 end
    carry = (t > 0xF) and 1 or 0
    r = r | ((t & 0xF) << sh)
  end
  return r, carry
end
local function bcd_sub(a, v, c, nib)          -- v is the complement of the operand
  local r, carry = 0, c
  for i = 0, nib - 1 do
    local sh = i * 4
    local t = ((a >> sh) & 0xF) + ((v >> sh) & 0xF) + carry
    if t <= 0xF then t = t - 6; carry = 0 else carry = 1 end
    r = r | ((t & 0xF) << sh)
  end
  return r, carry
end

local function adc(v)
  if fM == 1 then
    local a = A & 0xFF
    local r, c
    if fD == 1 then r, c = bcd_add(a, v, fC, 2) else r = a + v + fC; c = r >> 8; r = r & 0xFF end
    fV = (((~(a ~ v)) & (a ~ r)) >> 7) & 1
    fC = c; A = (A & 0xFF00) | r; nz8(r)
  else
    local r, c
    if fD == 1 then r, c = bcd_add(A, v, fC, 4) else r = A + v + fC; c = r >> 16; r = r & 0xFFFF end
    fV = (((~(A ~ v)) & (A ~ r)) >> 15) & 1
    fC = c; A = r; nz16(r)
  end
end
local function sbc(v)
  if fM == 1 then
    local a = A & 0xFF
    v = (~v) & 0xFF
    local r, c
    if fD == 1 then r, c = bcd_sub(a, v, fC, 2) else r = a + v + fC; c = r >> 8; r = r & 0xFF end
    fV = (((~(a ~ v)) & (a ~ r)) >> 7) & 1
    fC = c; A = (A & 0xFF00) | r; nz8(r)
  else
    v = (~v) & 0xFFFF
    local r, c
    if fD == 1 then r, c = bcd_sub(A, v, fC, 4) else r = A + v + fC; c = r >> 16; r = r & 0xFFFF end
    fV = (((~(A ~ v)) & (A ~ r)) >> 15) & 1
    fC = c; A = r; nz16(r)
  end
end
local function ora(v)
  if fM == 1 then local r = (A | v) & 0xFF; A = (A & 0xFF00) | r; nz8(r)
  else A = (A | v) & 0xFFFF; nz16(A) end
end
local function anda(v)
  if fM == 1 then local r = A & v & 0xFF; A = (A & 0xFF00) | r; nz8(r)
  else A = A & v & 0xFFFF; nz16(A) end
end
local function eor(v)
  if fM == 1 then local r = (A ~ v) & 0xFF; A = (A & 0xFF00) | r; nz8(r)
  else A = (A ~ v) & 0xFFFF; nz16(A) end
end
local function lda(v)
  if fM == 1 then A = (A & 0xFF00) | v; nz8(v) else A = v; nz16(v) end
end
local function cmpr(r, v, is8)
  if is8 then
    r = r & 0xFF
    local t = (r - v) & 0xFF
    fC = (r >= v) and 1 or 0; nz8(t)
  else
    local t = (r - v) & 0xFFFF
    fC = (r >= v) and 1 or 0; nz16(t)
  end
end
local function cmpa(v) cmpr(A, v, fM == 1) end

local ALU = { [0] = ora, anda, eor, adc, false, lda, cmpa, sbc }

local function shiftval(kind, v)              -- 0 ASL 1 LSR 2 ROL 3 ROR 4 INC 5 DEC; the width follows M
  local top, mask = 0x8000, 0xFFFF
  if fM == 1 then top, mask = 0x80, 0xFF end
  local r
  if kind == 0 then fC = (v & top ~= 0) and 1 or 0; r = (v << 1) & mask
  elseif kind == 1 then fC = v & 1; r = v >> 1
  elseif kind == 2 then local c = fC; fC = (v & top ~= 0) and 1 or 0; r = ((v << 1) | c) & mask
  elseif kind == 3 then local c = fC; fC = v & 1; r = (v >> 1) | (c == 1 and top or 0)
  elseif kind == 4 then r = (v + 1) & mask
  else r = (v - 1) & mask end
  fN = (r & top ~= 0) and 1 or 0
  fZ = (r == 0) and 1 or 0
  return r
end
local function rmw(a, kind)
  if fM == 1 then wr(a, shiftval(kind, rd(a))) else wr16(a, shiftval(kind, rd16(a))) end
end
local function accshift(kind)
  if fM == 1 then A = (A & 0xFF00) | shiftval(kind, A & 0xFF) else A = shiftval(kind, A) end
end
local function tsb(a, trb)
  local v = readM(a)
  local m = (fM == 1) and (A & 0xFF) or A
  fZ = (v & m == 0) and 1 or 0
  if trb then v = v & ~m else v = v | m end
  writeM(a, v)
end
local function bit(v, imm)
  local m = (fM == 1) and (A & 0xFF) or A
  fZ = (v & m == 0) and 1 or 0
  if not imm then
    if fM == 1 then fN = (v >> 7) & 1; fV = (v >> 6) & 1 else fN = (v >> 15) & 1; fV = (v >> 14) & 1 end
  end
end

------------------------------------------------------------------ opcode table
-- ALU families (ORA AND EOR ADC STA LDA CMP SBC) on every addressing mode
local M01 = { [0] = a_indx, a_dp, false, a_abs, a_indy, a_dpx, a_absy, a_absx }
local M11 = { [0] = a_sr, a_lng, false, a_long, a_sry, a_lngy, false, a_longx }
local function alu_op(f, store, am, imm)
  if store then return function() writeM(am(), A) end end
  if imm then return function() f(fetchM()) end end
  return function() f(readM(am())) end
end
for aaa = 0, 7 do
  local f, store = ALU[aaa], aaa == 4
  for bbb = 0, 7 do
    local am1, am3 = M01[bbb], M11[bbb]
    if bbb == 2 then
      if not store then ops[(aaa << 5) | 9] = alu_op(f, false, nil, true) end
    elseif am1 then
      ops[(aaa << 5) | (bbb << 2) | 1] = alu_op(f, store, am1)
    end
    if am3 then ops[(aaa << 5) | (bbb << 2) | 3] = alu_op(f, store, am3) end
  end
  ops[(aaa << 5) | 0x12] = alu_op(f, store, a_ind)         -- (dp)
end
ops[0x89] = function() bit(fetchM(), true) end

-- shifts and rotates, INC and DEC
local SHF = { { 0x06, 0x0A, 0x0E, 0x16, 0x1E, 0 }, { 0x46, 0x4A, 0x4E, 0x56, 0x5E, 1 },
              { 0x26, 0x2A, 0x2E, 0x36, 0x3E, 2 }, { 0x66, 0x6A, 0x6E, 0x76, 0x7E, 3 },
              { 0xE6, 0x1A, 0xEE, 0xF6, 0xFE, 4 }, { 0xC6, 0x3A, 0xCE, 0xD6, 0xDE, 5 } }
for _, e in ipairs(SHF) do
  local k = e[6]
  ops[e[1]] = function() rmw(a_dp(), k) end
  ops[e[2]] = function() accshift(k) end
  ops[e[3]] = function() rmw(a_abs(), k) end
  ops[e[4]] = function() rmw(a_dpx(), k) end
  ops[e[5]] = function() rmw(a_absx(), k) end
end
ops[0x04] = function() tsb(a_dp(), false) end
ops[0x0C] = function() tsb(a_abs(), false) end
ops[0x14] = function() tsb(a_dp(), true) end
ops[0x1C] = function() tsb(a_abs(), true) end
ops[0x24] = function() bit(readM(a_dp())) end
ops[0x2C] = function() bit(readM(a_abs())) end
ops[0x34] = function() bit(readM(a_dpx())) end
ops[0x3C] = function() bit(readM(a_absx())) end

-- index registers
ops[0xA0] = function() local v = fetchX(); Y = v; if fX == 1 then nz8(v) else nz16(v) end end
ops[0xA2] = function() local v = fetchX(); X = v; if fX == 1 then nz8(v) else nz16(v) end end
local function ldxy(reg, am)
  return function()
    local v = readX(am())
    if reg == 0 then X = v else Y = v end
    if fX == 1 then nz8(v) else nz16(v) end
  end
end
ops[0xA4] = ldxy(1, a_dp);  ops[0xAC] = ldxy(1, a_abs); ops[0xB4] = ldxy(1, a_dpx); ops[0xBC] = ldxy(1, a_absx)
ops[0xA6] = ldxy(0, a_dp);  ops[0xAE] = ldxy(0, a_abs); ops[0xB6] = ldxy(0, a_dpy); ops[0xBE] = ldxy(0, a_absy)
local function stxy(reg, am)
  return function()
    local a = am()
    local v = (reg == 0) and X or Y
    if fX == 1 then wr(a, v & 0xFF) else wr16(a, v) end
  end
end
ops[0x84] = stxy(1, a_dp);  ops[0x8C] = stxy(1, a_abs); ops[0x94] = stxy(1, a_dpx)
ops[0x86] = stxy(0, a_dp);  ops[0x8E] = stxy(0, a_abs); ops[0x96] = stxy(0, a_dpy)
ops[0x64] = function() writeM(a_dp(), 0) end
ops[0x74] = function() writeM(a_dpx(), 0) end
ops[0x9C] = function() writeM(a_abs(), 0) end
ops[0x9E] = function() writeM(a_absx(), 0) end
local function cpxy(reg, imm, am)
  return function()
    local v
    if imm then v = fetchX() else v = readX(am()) end
    cmpr(reg == 0 and X or Y, v, fX == 1)
  end
end
ops[0xE0] = cpxy(0, true); ops[0xE4] = cpxy(0, false, a_dp); ops[0xEC] = cpxy(0, false, a_abs)
ops[0xC0] = cpxy(1, true); ops[0xC4] = cpxy(1, false, a_dp); ops[0xCC] = cpxy(1, false, a_abs)
ops[0xE8] = function() if fX == 1 then X = (X + 1) & 0xFF; nz8(X) else X = (X + 1) & 0xFFFF; nz16(X) end end
ops[0xC8] = function() if fX == 1 then Y = (Y + 1) & 0xFF; nz8(Y) else Y = (Y + 1) & 0xFFFF; nz16(Y) end end
ops[0xCA] = function() if fX == 1 then X = (X - 1) & 0xFF; nz8(X) else X = (X - 1) & 0xFFFF; nz16(X) end end
ops[0x88] = function() if fX == 1 then Y = (Y - 1) & 0xFF; nz8(Y) else Y = (Y - 1) & 0xFFFF; nz16(Y) end end

-- transfers
local function setXY(v) if fX == 1 then v = v & 0xFF; nz8(v) else v = v & 0xFFFF; nz16(v) end return v end
ops[0xAA] = function() X = setXY(A) end
ops[0xA8] = function() Y = setXY(A) end
ops[0x9B] = function() Y = setXY(X) end                      -- TXY
ops[0xBB] = function() X = setXY(Y) end                      -- TYX
ops[0xBA] = function() X = setXY(S) end                      -- TSX
ops[0x8A] = function() if fM == 1 then local v = X & 0xFF; A = (A & 0xFF00) | v; nz8(v) else A = X; nz16(A) end end
ops[0x98] = function() if fM == 1 then local v = Y & 0xFF; A = (A & 0xFF00) | v; nz8(v) else A = Y; nz16(A) end end
ops[0x9A] = function() if fE == 1 then S = 0x100 | (X & 0xFF) else S = X end end
ops[0x1B] = function() if fE == 1 then S = 0x100 | (A & 0xFF) else S = A end end
ops[0x3B] = function() A = S; nz16(A) end
ops[0x5B] = function() D = A; nz16(D) end
ops[0x7B] = function() A = D; nz16(A) end
ops[0xEB] = function() A = ((A & 0xFF) << 8) | (A >> 8); nz8(A & 0xFF) end

-- stack
ops[0x48] = function() if fM == 1 then push8(A) else push16(A) end end
ops[0x68] = function()
  if fM == 1 then local v = pull8(); A = (A & 0xFF00) | v; nz8(v) else A = pull16(); nz16(A) end
end
ops[0xDA] = function() if fX == 1 then push8(X) else push16(X) end end
ops[0x5A] = function() if fX == 1 then push8(Y) else push16(Y) end end
ops[0xFA] = function() if fX == 1 then X = pull8(); nz8(X) else X = pull16(); nz16(X) end end
ops[0x7A] = function() if fX == 1 then Y = pull8(); nz8(Y) else Y = pull16(); nz16(Y) end end
ops[0x08] = function() push8(getP()) end
ops[0x28] = function() setP(pull8()) end
ops[0x0B] = function() push16(D) end
ops[0x2B] = function() D = pull16(); nz16(D) end
ops[0x8B] = function() push8(DB >> 16) end
ops[0xAB] = function() local v = pull8(); DB = v << 16; nz8(v) end
ops[0x4B] = function() push8(PB >> 16) end
ops[0xF4] = function() push16(fetch16()) end
ops[0xD4] = function() push16(rd16((D + fetch8()) & 0xFFFF)) end
ops[0x62] = function() local o = fetch16(); push16((PC + o) & 0xFFFF) end

-- flags
ops[0x18] = function() fC = 0 end
ops[0x38] = function() fC = 1 end
ops[0x58] = function() fI = 0 end
ops[0x78] = function() fI = 1 end
ops[0xB8] = function() fV = 0 end
ops[0xD8] = function() fD = 0 end
ops[0xF8] = function() fD = 1 end
ops[0xC2] = function() setP(getP() & ~fetch8()) end
ops[0xE2] = function() setP(getP() | fetch8()) end
ops[0xFB] = function()
  local e = fE
  fE = fC; fC = e
  if fE == 1 then fM = 1; fX = 1; X = X & 0xFF; Y = Y & 0xFF; S = 0x100 | (S & 0xFF) end
end
ops[0xEA] = function() end
ops[0x42] = function() fetch8() end
ops[0xCB] = function() waiting = true end
ops[0xDB] = function() stopped = true end
ops[0x00] = function() fetch8(); interrupt(0xFFE6, 0xFFFE, true) end
ops[0x02] = function() fetch8(); interrupt(0xFFE4, 0xFFF4, true) end

-- branches and jumps
local function branch(cond)
  local o = fetch8()
  if cond then
    if o >= 128 then o = o - 256 end
    PC = (PC + o) & 0xFFFF
    cyc = cyc + 1
  end
end
ops[0x10] = function() branch(fN == 0) end
ops[0x30] = function() branch(fN == 1) end
ops[0x50] = function() branch(fV == 0) end
ops[0x70] = function() branch(fV == 1) end
ops[0x90] = function() branch(fC == 0) end
ops[0xB0] = function() branch(fC == 1) end
ops[0xD0] = function() branch(fZ == 0) end
ops[0xF0] = function() branch(fZ == 1) end
ops[0x80] = function() branch(true) end
ops[0x82] = function() local o = fetch16(); PC = (PC + o) & 0xFFFF end
ops[0x4C] = function() PC = fetch16() end
ops[0x5C] = function() local a = fetch24(); PC = a & 0xFFFF; PB = a & 0xFF0000 end
ops[0x6C] = function() PC = rd16(fetch16()) end
ops[0x7C] = function() PC = rd16(PB | ((fetch16() + X) & 0xFFFF)) end
ops[0xDC] = function()
  local p = fetch16()
  local a = rd16(p) | (rd((p + 2) & 0xFFFF) << 16)
  PC = a & 0xFFFF; PB = a & 0xFF0000
end
ops[0x20] = function() local a = fetch16(); push16((PC - 1) & 0xFFFF); PC = a end
ops[0xFC] = function()
  local a = fetch16()
  push16((PC - 1) & 0xFFFF)
  PC = rd16(PB | ((a + X) & 0xFFFF))
end
ops[0x22] = function()
  local a = fetch24()
  push8(PB >> 16); push16((PC - 1) & 0xFFFF)
  PC = a & 0xFFFF; PB = a & 0xFF0000
end
ops[0x60] = function() PC = (pull16() + 1) & 0xFFFF end
ops[0x6B] = function() PC = (pull16() + 1) & 0xFFFF; PB = pull8() << 16 end
ops[0x40] = function()
  setP(pull8())
  PC = pull16()
  if fE == 0 then PB = pull8() << 16 end
end
local function block(dir)
  local db, sb = fetch8(), fetch8()
  wr((db << 16) | Y, rd((sb << 16) | X))
  if fX == 1 then X = (X + dir) & 0xFF; Y = (Y + dir) & 0xFF
  else X = (X + dir) & 0xFFFF; Y = (Y + dir) & 0xFFFF end
  A = (A - 1) & 0xFFFF
  DB = db << 16
  if A ~= 0xFFFF then PC = (PC - 3) & 0xFFFF end
end
ops[0x54] = function() block(1) end
ops[0x44] = function() block(-1) end

for i = 0, 255 do
  if not ops[i] then ops[i] = ops[0xEA] end
end

------------------------------------------------------------------ the instruction loop
cpu_reset = function()
  A, X, Y, S, D, DB, PB = 0, 0, 0, 0x1FF, 0, 0, 0
  fN, fV, fM, fX, fD, fI, fZ, fC, fE = 0, 0, 1, 1, 0, 1, 0, 0, 1
  waiting, stopped = false, false
  nmiPending, irqFlag = false, 0
  PC = rd16(0xFFFC)
end

cpu_run = function(target)
  while cyc < target do
    if nmiPending then
      nmiPending = false
      interrupt(0xFFEA, 0xFFFA, false)
    elseif irqFlag == 1 and fI == 0 then
      interrupt(0xFFEE, 0xFFFE, false)
    end
    if waiting then
      if irqFlag == 1 then waiting = false else cyc = target end
    elseif stopped then
      cyc = target
    else
      local op = rd(PB | PC)
      PC = (PC + 1) & 0xFFFF
      ops[op]()
      cyc = cyc + 4
    end
  end
end
end
