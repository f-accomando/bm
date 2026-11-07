-- SPC700 and boot protocol tests. The harness glues carts/snes/src/00_spc.lua and this file.
local fails, count = 0, 0
local function check(name, cond, extra)
  count = count + 1
  if not cond then fails = fails + 1; print("FAIL " .. name .. (extra and (" " .. extra) or "")) end
end

-- what a game does: the handshake, the block upload, the jump
local function upload(addr, bytes)
  apu_reset()
  check("greeting", apu_read(1) == 0xAA and apu_read(2) == 0xBB)
  apu_write(3, addr & 255); apu_write(4, addr >> 8)
  apu_write(2, 1); apu_write(1, 0xCC)
  check("kick echo", apu_read(1) == 0xCC)
  for i, b in ipairs(bytes) do
    apu_write(2, b); apu_write(1, (i - 1) & 255)         -- a 16-bit store: port 0 first, then port 1
    check("byte echo " .. i, apu_read(1) == ((i - 1) & 255))
  end
  apu_write(3, addr & 255); apu_write(4, addr >> 8)
  apu_write(2, 0); apu_write(1, (#bytes + 1) & 255)        -- jump
end

-- ports, MUL, DIV
upload(0x200, {
  0xE8,0x07, 0x8D,0x06, 0xCF, 0xC4,0xF4,              -- A=7 Y=6 MUL YA -> $2A to port 0
  0x8D,0x00, 0xE8,0x64, 0xCD,0x07, 0x9E,              -- YA=100 / 7 -> A=14 Y=2
  0xC4,0xF5, 0xCB,0xF6,                               -- ports 1 and 2
  0x2F,0xFE,                                          -- BRA self
})
apu_write(1, 0) -- no-op while running (ports only)
apu_run(1000)
check("mul", apu_read(1) == 0x2A, string.format("%02X", apu_read(1)))
check("div A", apu_read(2) == 14)
check("div Y", apu_read(3) == 2)

-- a program that reads the ports and answers
upload(0x300, {
  0xE8,0x42, 0xC4,0xF4,                               -- port 0 = $42
  0xE4,0xF5, 0xF0,0xFC,                               -- wait for port 1 <> 0
  0x60, 0x88,0x01, 0xC4,0xF6,                         -- CLRC ADC A,#1 ; port 2 = A
  0x2F,0xFE,
})
apu_run(500)
check("hello", apu_read(1) == 0x42)
apu_write(2, 5)
apu_run(500)
check("answer", apu_read(3) == 6, string.format("%02X", apu_read(3)))

-- a timer
upload(0x400, {
  0x8F,0x01,0xFA, 0x8F,0x01,0xF1,                     -- target 1, timer 0 on
  0xE4,0xFD, 0xF0,0xFC,                               -- wait for the counter
  0xC4,0xF7, 0x2F,0xFE,
})
for _ = 1, 20 do apu_run(200) end
check("timer", apu_read(4) ~= 0, tostring(apu_read(4)))

-- a second upload through the boot ROM after the program jumped to $FFC0 with the ROM enabled
upload(0x500, { 0x8F,0x80,0xF1, 0x5F,0xC0,0xFF })     -- F1 = $80, JMP $FFC0
apu_run(200)
check("boot ROM again", apu_read(1) == 0xAA and apu_read(2) == 0xBB)

print(count .. " spc checks, " .. fails .. " failed")
if fails > 0 then os.exit(1) end
