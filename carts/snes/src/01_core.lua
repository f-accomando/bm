-- bm SNES: a Super Nintendo emulator cartridge. It reads .sfc and .smc files from /sfc (the battery RAM goes to /docs).
-- Part 1: memory, ROM header, bus, I/O registers, APU stub, DMA and HDMA.
-- Original code. No SNES firmware or game data is in this file.

local byte = string.byte
local cyc = 0                                 -- the CPU clock, in cycles of about 7 master clocks

------------------------------------------------------------------ memory
local WRAM, VRAM, OAM, CG, CG24, BLKV = {}, {}, {}, {}, {}, {}
for i = 1, 131072 do WRAM[i] = 0 end
for i = 1, 65536 do VRAM[i] = 0 end
for i = 1, 544 do OAM[i] = 0 end
for i = 1, 512 do CG[i] = 0 end
for i = 1, 256 do CG24[i] = 0 end
for i = 1, 4096 do BLKV[i] = 0 end

local CGV = {}                                -- a version number for each row of 16 colours
for i = 1, 16 do CGV[i] = 0 end
local rom, romsize, lorom = "", 1, true
local sram, sramsize, sramdirty = {}, 0, false

-- PPU registers that the renderer reads (the renderer takes snapshots of them)
local P = {
  inidisp = 0x80, obsel = 0, bgmode = 0, mosaic = 0, setini = 0,
  sc = { 0, 0, 0, 0 }, nba = { 0, 0, 0, 0 },
  hofs = { 0, 0, 0, 0 }, vofs = { 0, 0, 0, 0 },
  tm = 0, ts = 0, cgwsel = 0, cgadsub = 0, fr = 0, fg = 0, fb = 0,
}
local ppudirty = true      -- a register that changes the picture was written
local gfxver = 0           -- counts every write that can change the picture (VRAM, OAM, CGRAM, registers)
local oamaddr, vmain, vmaddr, vmpre, cgadd, cglatch, cgflip = 0, 0, 0, 0, 0, 0, false
local bgofslatch, m7latch, m7a, m7b, mpy = 0, 0, 0, 0, 0
local wmadd = 0
local vmstep = { 1, 32, 128, 128 }

local function vaddr(a)                      -- VMAIN address translation
  local t = (vmain >> 2) & 3
  if t == 0 then return a end
  if t == 1 then return (a & 0xFF00) | ((a & 0x1F) << 3) | ((a >> 5) & 7) end
  if t == 2 then return (a & 0xFE00) | ((a & 0x3F) << 3) | ((a >> 6) & 7) end
  return (a & 0xFC00) | ((a & 0x7F) << 3) | ((a >> 7) & 7)
end

local function vwrite(hi, v)
  local ba = (vaddr(vmaddr & 0x7FFF) << 1) & 0xFFFF
  if hi then ba = ba + 1 end
  VRAM[ba + 1] = v
  local b = (ba >> 4) + 1
  BLKV[b] = BLKV[b] + 1
  gfxver = gfxver + 1
  if (vmain & 0x80 ~= 0) == hi then vmaddr = (vmaddr + vmstep[(vmain & 3) + 1]) & 0xFFFF end
end

local function vprefetch()
  local ba = (vaddr(vmaddr & 0x7FFF) << 1) & 0xFFFF
  vmpre = VRAM[ba + 1] | (VRAM[ba + 2] << 8)
end

------------------------------------------------------------------ PPU register writes
local pw = {}
local function mark() ppudirty = true; gfxver = gfxver + 1 end

pw[0x2100] = function(v) P.inidisp = v; mark() end
pw[0x2101] = function(v) P.obsel = v end
pw[0x2102] = function(v) oamaddr = ((oamaddr & 0x200) | (v << 1)) end
pw[0x2103] = function(v) oamaddr = ((oamaddr & 0x1FE) | ((v & 1) << 9)) end
pw[0x2104] = function(v)
  if oamaddr < 544 then OAM[oamaddr + 1] = v; gfxver = gfxver + 1 end
  oamaddr = oamaddr + 1
  if oamaddr >= 544 then oamaddr = 0 end
end
pw[0x2105] = function(v) P.bgmode = v; mark() end
pw[0x2106] = function(v) P.mosaic = v end
for n = 1, 4 do
  pw[0x2106 + n] = function(v) P.sc[n] = v; mark() end
end
pw[0x210B] = function(v) P.nba[1] = (v & 15) << 13; P.nba[2] = (v >> 4) << 13; mark() end
pw[0x210C] = function(v) P.nba[3] = (v & 15) << 13; P.nba[4] = (v >> 4) << 13; mark() end
for n = 1, 4 do
  pw[0x210D + (n - 1) * 2] = function(v)
    P.hofs[n] = ((v << 8) | (bgofslatch & 0xF8) | ((P.hofs[n] >> 8) & 7)) & 0x3FF
    bgofslatch = v; mark()
  end
  pw[0x210E + (n - 1) * 2] = function(v)
    P.vofs[n] = ((v << 8) | bgofslatch) & 0x3FF
    bgofslatch = v; mark()
  end
end
pw[0x2115] = function(v) vmain = v end
pw[0x2116] = function(v) vmaddr = (vmaddr & 0xFF00) | v; vprefetch() end
pw[0x2117] = function(v) vmaddr = (vmaddr & 0x00FF) | (v << 8); vprefetch() end
pw[0x2118] = function(v) vwrite(false, v) end
pw[0x2119] = function(v) vwrite(true, v) end
pw[0x211B] = function(v) m7a = ((v << 8) | m7latch) & 0xFFFF; m7latch = v end
pw[0x211C] = function(v)
  m7b = ((v << 8) | m7latch) & 0xFFFF; m7latch = v
  local a = m7a; if a >= 0x8000 then a = a - 0x10000 end
  local b = v; if b >= 0x80 then b = b - 0x100 end
  mpy = (a * b) & 0xFFFFFF
end
pw[0x2121] = function(v) cgadd = v; cgflip = false end
pw[0x2122] = function(v)
  if not cgflip then
    cglatch = v; cgflip = true
  else
    local i = cgadd * 2
    CG[i + 1] = cglatch; CG[i + 2] = v & 0x7F
    local w = cglatch | ((v & 0x7F) << 8)
    local r, g, b = w & 31, (w >> 5) & 31, (w >> 10) & 31
    CG24[cgadd + 1] = ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2)
    CGV[(cgadd >> 4) + 1] = CGV[(cgadd >> 4) + 1] + 1
    cgadd = (cgadd + 1) & 0xFF; cgflip = false
    mark()
  end
end
pw[0x212C] = function(v) P.tm = v; mark() end
pw[0x212D] = function(v) P.ts = v; mark() end
pw[0x2130] = function(v) P.cgwsel = v; mark() end
pw[0x2131] = function(v) P.cgadsub = v; mark() end
pw[0x2132] = function(v)
  local c = v & 31
  if v & 0x20 ~= 0 then P.fr = c end
  if v & 0x40 ~= 0 then P.fg = c end
  if v & 0x80 ~= 0 then P.fb = c end
  mark()
end
pw[0x2133] = function(v) P.setini = v; mark() end
pw[0x2180] = function(v) WRAM[(wmadd & 0x1FFFF) + 1] = v; wmadd = (wmadd + 1) & 0x1FFFF end
pw[0x2181] = function(v) wmadd = (wmadd & 0x1FF00) | v end
pw[0x2182] = function(v) wmadd = (wmadd & 0x100FF) | (v << 8) end
pw[0x2183] = function(v) wmadd = (wmadd & 0x0FFFF) | ((v & 1) << 16) end

local function ppuread(o)
  if o == 0x2134 then return mpy & 0xFF end
  if o == 0x2135 then return (mpy >> 8) & 0xFF end
  if o == 0x2136 then return (mpy >> 16) & 0xFF end
  if o == 0x2138 then
    local v = OAM[oamaddr + 1] or 0
    oamaddr = (oamaddr + 1) % 544
    return v
  end
  if o == 0x2139 or o == 0x213A then
    local hi = (o == 0x213A)
    local v = hi and (vmpre >> 8) or (vmpre & 0xFF)
    if (vmain & 0x80 ~= 0) == hi then
      vmaddr = (vmaddr + vmstep[(vmain & 3) + 1]) & 0xFFFF
      vprefetch()
    end
    return v
  end
  if o == 0x213B then
    local v = CG[cgadd * 2 + (cgflip and 2 or 1)]
    if cgflip then cgadd = (cgadd + 1) & 0xFF end
    cgflip = not cgflip
    return v
  end
  if o == 0x213E then return 0x01 end
  if o == 0x213F then return 0x03 end
  if o == 0x2180 then
    local v = WRAM[(wmadd & 0x1FFFF) + 1]; wmadd = (wmadd + 1) & 0x1FFFF; return v
  end
  return 0
end

------------------------------------------------------------------ APU (SPC700 not emulated)
-- The boot ROM of the sound chip is played as a small state machine: the 0xAA 0xBB
-- greeting, the 0xCC handshake, the block upload and the "run" command. After "run" the
-- four ports echo what the CPU wrote, so most games pass their sound driver handshake.
local apu_in, apu_out = { 0, 0, 0, 0 }, { 0xAA, 0xBB, 0, 0 }
local apu_mode, apu_expect = 0, 0
local function apu_reset()
  apu_in[1], apu_in[2], apu_in[3], apu_in[4] = 0, 0, 0, 0
  apu_out[1], apu_out[2], apu_out[3], apu_out[4] = 0xAA, 0xBB, 0, 0
  apu_mode, apu_expect = 0, 0
end
local function apu_write(n, v)               -- n = 1..4
  apu_in[n] = v
  if apu_mode == 2 then apu_out[n] = v; return end
  if n ~= 1 then return end
  if apu_mode == 0 then
    if v == 0xCC then apu_out[1] = 0xCC; apu_mode = 1; apu_expect = 0 end
  elseif v == apu_expect then
    apu_out[1] = v; apu_expect = (apu_expect + 1) & 0xFF
  elseif apu_in[2] == 0 then
    apu_mode = 2; apu_out[1] = v
  else
    apu_out[1] = v; apu_expect = 0
  end
end

------------------------------------------------------------------ CPU I/O ($4200..), joypads
local nmitimen, nmiflag, irqFlag, vblank, hblank = 0, 0, 0, false, false
local htime, vtime = 0x1FF, 0x1FF
local wrmpya, wrdiv, rddiv, rdmpy = 0xFF, 0xFFFF, 0, 0
local hdmaen = 0
local nmiPending = false
local joy1, joy2, joyshift1, joyshift2, joystrobe = 0, 0, 0, 0, 0
local DM = {}                                 -- DMA registers $4300..$437F
for i = 0x4300, 0x437F do DM[i] = 0xFF end
local runDMA                                   -- defined below

local function cpuread(o)
  if o == 0x4210 then
    local v = (nmiflag << 7) | 0x02
    nmiflag = 0
    return v
  elseif o == 0x4211 then
    local v = irqFlag << 7
    irqFlag = 0
    return v
  elseif o == 0x4212 then
    return (vblank and 0x80 or 0) | (hblank and 0x40 or 0)
  elseif o == 0x4213 then return 0xC0
  elseif o == 0x4214 then return rddiv & 0xFF
  elseif o == 0x4215 then return (rddiv >> 8) & 0xFF
  elseif o == 0x4216 then return rdmpy & 0xFF
  elseif o == 0x4217 then return (rdmpy >> 8) & 0xFF
  elseif o == 0x4218 then return joy1 & 0xFF
  elseif o == 0x4219 then return (joy1 >> 8) & 0xFF
  elseif o == 0x421A then return joy2 & 0xFF
  elseif o == 0x421B then return (joy2 >> 8) & 0xFF
  end
  return 0
end

local function cpuwrite(o, v)
  if o == 0x4200 then
    local was = nmitimen
    nmitimen = v
    if v & 0x80 ~= 0 and was & 0x80 == 0 and nmiflag == 1 then nmiPending = true end
    if v & 0x30 == 0 then irqFlag = 0 end
  elseif o == 0x4202 then wrmpya = v
  elseif o == 0x4203 then rdmpy = wrmpya * v
  elseif o == 0x4204 then wrdiv = (wrdiv & 0xFF00) | v
  elseif o == 0x4205 then wrdiv = (wrdiv & 0x00FF) | (v << 8)
  elseif o == 0x4206 then
    if v == 0 then rddiv = 0xFFFF; rdmpy = wrdiv
    else rddiv = wrdiv // v; rdmpy = wrdiv % v end
  elseif o == 0x4207 then htime = (htime & 0x100) | v
  elseif o == 0x4208 then htime = (htime & 0xFF) | ((v & 1) << 8)
  elseif o == 0x4209 then vtime = (vtime & 0x100) | v
  elseif o == 0x420A then vtime = (vtime & 0xFF) | ((v & 1) << 8)
  elseif o == 0x420B then runDMA(v)
  elseif o == 0x420C then hdmaen = v
  end
end

local function ioread(o)
  if o >= 0x4300 then return DM[o] or 0 end
  if o >= 0x4200 then return cpuread(o) end
  if o == 0x4016 then
    local b = (joyshift1 >> 15) & 1
    joyshift1 = ((joyshift1 << 1) | 1) & 0xFFFF
    return b
  elseif o == 0x4017 then
    local b = (joyshift2 >> 15) & 1
    joyshift2 = ((joyshift2 << 1) | 1) & 0xFFFF
    return b | 0x1C
  end
  if o >= 0x2140 and o < 0x2180 then return apu_out[(o & 3) + 1] end
  if o >= 0x2100 and o < 0x2200 then return ppuread(o) end
  return 0
end

local function iowrite(o, v)
  if o >= 0x4300 then DM[o] = v; return end
  if o >= 0x4200 then cpuwrite(o, v); return end
  if o == 0x4016 then
    if joystrobe == 1 and v & 1 == 0 then joyshift1 = joy1; joyshift2 = joy2 end
    joystrobe = v & 1
    if joystrobe == 1 then joyshift1 = joy1; joyshift2 = joy2 end
    return
  end
  if o >= 0x2140 and o < 0x2180 then apu_write((o & 3) + 1, v); return end
  local f = pw[o]
  if f then f(v) end
end

------------------------------------------------------------------ the bus
local function rd(a)
  local b = a >> 16
  local o = a & 0xFFFF
  if b == 0x7E then return WRAM[o + 1] end
  if b == 0x7F then return WRAM[0x10001 + o] end
  local bb = b & 0x7F
  if bb < 0x40 then
    if o >= 0x8000 then
      local idx
      if lorom then idx = (bb << 15) | (o & 0x7FFF) else idx = (bb << 16) | o end
      if idx >= romsize then idx = idx % romsize end
      return byte(rom, idx + 1)
    elseif o < 0x2000 then return WRAM[o + 1]
    elseif o < 0x6000 then return ioread(o)
    elseif not lorom and bb >= 0x20 and sramsize > 0 then
      return sram[(((bb - 0x20) << 13) | (o - 0x6000)) % sramsize + 1]
    end
    return 0
  end
  if lorom then
    if bb >= 0x70 and o < 0x8000 then
      if sramsize > 0 then return sram[(((bb - 0x70) << 15) | o) % sramsize + 1] end
      return 0
    end
    local idx = (bb << 15) | (o & 0x7FFF)
    if idx >= romsize then idx = idx % romsize end
    return byte(rom, idx + 1)
  end
  local idx = ((b & 0x3F) << 16) | o
  if idx >= romsize then idx = idx % romsize end
  return byte(rom, idx + 1)
end

local function wr(a, v)
  local b = a >> 16
  local o = a & 0xFFFF
  if b == 0x7E then WRAM[o + 1] = v; return end
  if b == 0x7F then WRAM[0x10001 + o] = v; return end
  local bb = b & 0x7F
  if bb < 0x40 then
    if o < 0x2000 then WRAM[o + 1] = v
    elseif o < 0x6000 then if o >= 0x2100 then iowrite(o, v) end
    elseif o < 0x8000 then
      if not lorom and bb >= 0x20 and sramsize > 0 then
        sram[(((bb - 0x20) << 13) | (o - 0x6000)) % sramsize + 1] = v; sramdirty = true
      end
    end
    return
  end
  if lorom and bb >= 0x70 and o < 0x8000 and sramsize > 0 then
    sram[(((bb - 0x70) << 15) | o) % sramsize + 1] = v; sramdirty = true
  end
end

local function rd16(a) return rd(a) | (rd((a + 1) & 0xFFFFFF) << 8) end
local function wr16(a, v) wr(a, v & 0xFF); wr((a + 1) & 0xFFFFFF, (v >> 8) & 0xFF) end

------------------------------------------------------------------ DMA and HDMA
local DMA_MODES = {
  [0] = { 0 }, { 0, 1 }, { 0, 0 }, { 0, 0, 1, 1 }, { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0 }, { 0, 0, 1, 1 },
}

local function bwrite(reg, v)                 -- write to the B bus ($2100 + reg)
  local o = 0x2100 + reg
  if o >= 0x2140 and o < 0x2144 then apu_write((o & 3) + 1, v); return end
  local f = pw[o]
  if f then f(v) end
end
local function bread(reg) return ioread(0x2100 + reg) end

runDMA = function(mask)
  for ch = 0, 7 do
    if mask & (1 << ch) ~= 0 then
      local base = 0x4300 + ch * 16
      local ctl = DM[base]
      local breg = DM[base + 1]
      local src = DM[base + 2] | (DM[base + 3] << 8) | (DM[base + 4] << 16)
      local size = DM[base + 5] | (DM[base + 6] << 8)
      if size == 0 then size = 0x10000 end
      local mode = DMA_MODES[ctl & 7]
      local nm = #mode
      local step = 1
      local fixed = false
      local am = (ctl >> 3) & 3
      if am == 2 then step = -1 elseif am ~= 0 then fixed = true end
      local toA = ctl & 0x80 ~= 0
      local i = 0
      local bank = src & 0xFF0000
      local off = src & 0xFFFF
      cyc = cyc + size + 8
      for _ = 1, size do
        local r = breg + mode[(i % nm) + 1]
        if toA then
          wr(bank | off, bread(r))
        else
          bwrite(r, rd(bank | off))
        end
        if not fixed then off = (off + step) & 0xFFFF end
        i = i + 1
      end
      DM[base + 2] = off & 0xFF; DM[base + 3] = off >> 8
      DM[base + 5] = 0; DM[base + 6] = 0
    end
  end
end

local hd_addr, hd_cnt, hd_hdr, hd_do, hd_on, hd_ind = {}, {}, {}, {}, {}, {}
local function hdma_load(ch)
  local base = 0x4300 + ch * 16
  local bank = DM[base + 4] << 16
  local a = hd_addr[ch]
  local h = rd(bank | a); a = (a + 1) & 0xFFFF
  hd_hdr[ch] = h
  if h == 0 then hd_on[ch] = false; hd_addr[ch] = a; return end
  hd_cnt[ch] = h & 0x7F
  if hd_cnt[ch] == 0 then hd_cnt[ch] = 128 end
  if hd_ind[ch] then
    DM[base + 5] = rd(bank | a); DM[base + 6] = rd(bank | ((a + 1) & 0xFFFF)); a = (a + 2) & 0xFFFF
  end
  hd_addr[ch] = a
  hd_do[ch] = true
end

local function hdma_init()
  for ch = 0, 7 do
    local base = 0x4300 + ch * 16
    hd_on[ch] = false
    if hdmaen & (1 << ch) ~= 0 then
      hd_on[ch] = true
      hd_ind[ch] = DM[base] & 0x40 ~= 0
      hd_addr[ch] = DM[base + 2] | (DM[base + 3] << 8)
      hdma_load(ch)
    end
  end
end

local function hdma_line()
  for ch = 0, 7 do
    if hd_on[ch] and hdmaen & (1 << ch) ~= 0 then
      local base = 0x4300 + ch * 16
      if hd_do[ch] then
        local ctl = DM[base]
        local mode = DMA_MODES[ctl & 7]
        local breg = DM[base + 1]
        local bank, a
        if hd_ind[ch] then
          bank = DM[base + 7] << 16; a = DM[base + 5] | (DM[base + 6] << 8)
        else
          bank = DM[base + 4] << 16; a = hd_addr[ch]
        end
        for i = 1, #mode do
          bwrite(breg + mode[i], rd(bank | a)); a = (a + 1) & 0xFFFF
        end
        if hd_ind[ch] then DM[base + 5] = a & 0xFF; DM[base + 6] = a >> 8 else hd_addr[ch] = a end
        cyc = cyc + #mode + 2
      end
      hd_cnt[ch] = hd_cnt[ch] - 1
      hd_do[ch] = hd_hdr[ch] & 0x80 ~= 0
      if hd_cnt[ch] == 0 then hdma_load(ch) end
    end
  end
end
