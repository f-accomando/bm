-- snes_sdk_rom.lua: checks of bm SNES SDK, called by snes_sdk_host.lua (make test-studio).
-- 1. The pencil held: Space (or the pad's A) held while the arrows move paints a line, one undo step.
-- 2. A ROM (.sfc in /snes) opened read only: the Code tab shows its disassembly and takes no typing, F9 sets a
--    breakpoint on a line, F5 runs the ROM; the Sprites tab shows its bytes as tiles, page by page; nothing is saved.
return function(t)
  local check, key, frames, sees, status, S = t.check, t.key, t.frames, t.sees, t.status, t.S

  ----------------------------------------------------------------- 1. the pencil held
  key("^n", "^n")
  key("f2", "\n")                                       -- draw: the cursor is at (0,0) of the sprite
  t.hold(true)
  key(" ", "right", "right", "right")
  t.hold(false)
  key("down")
  local c = S(0, 0)
  check(c ~= nil and S(1, 0) == c and S(2, 0) == c and S(3, 0) == c, "space held + right: a line of 4 pixels")
  check(S(3, 1) == nil, "space let go: the down move paints nothing")
  key("^z")
  check(S(0, 0) == nil and S(3, 0) == nil, "one undo takes back all the stroke")
  -- the pad: A held, then the DPAD
  t.pad[4] = true; frames(3)
  t.pad[1] = true; frames(1); t.pad[1] = false; frames(3)
  t.pad[4] = false; frames(3)
  check(S(3, 1) ~= nil and S(4, 1) ~= nil, "A held + right: two pixels")
  -- Del held + arrows clears (the keyboard's Delete is a USB usage: here the pad's A keeps the stroke)
  key("\n")

  ----------------------------------------------------------------- 2. a ROM, read only
  local src = [[
    .org $008000
reset:
    sei
    clc
    xce
    rep #$30
    lda #$1234
loop:
    dex
    bne loop
    rts
]]
  local rom = assert(t.asm.build(src, {}, { title = "TEST GAME" }))
  rom = rom .. string.rep("\xAA", 32768)                  -- 64 KB: the Sprites tab has 4 pages at 4 bpp
  t.snes["Test Game.sfc"] = rom
  key("^n", "^n")                                       -- the drawing changed the project: the second Ctrl+N confirms
  key("^o")
  check(sees("Test Game.sfc  (/snes, 64 KB, read only)"), "Ctrl+O lists the ROM of /snes")
  key("up", "\n")                                       -- the ROMs are after the projects: the last row
  check(sees("(read only)"), "the bar says read only")

  key("f1")
  check(sees("reset") and sees("$1234"), "Code: the disassembly from the reset vector")
  check(status():find("read only", 1, true) ~= nil or sees("read only"), "Code: the status says read only")
  key("x")
  check(status():find("read only", 1, true) ~= nil, "Code: a letter is not typed: " .. status())
  key("^e")
  check(sees("reset") and sees("Go to an address"), "Ctrl+E: the vectors and an address")
  key("esc")
  frames(200)                                           -- the message of the "x" goes: the status shows the line again
  check(status():find("00:8000", 1, true) ~= nil, "Code: the status gives the address of the line: " .. status())
  key("f9")
  check(status():find("breakpoint at 00:8000", 1, true) ~= nil, "F9: a breakpoint on the line: " .. status())
  local loads = t.fake.loads
  key("f5")
  check(t.fake.loads == loads + 1 and t.fake.rom == rom, "F5 runs the ROM as it is")
  key("f2")

  check(sees("ROM $000000-$003FFF"), "Sprites: the first page of the ROM (128x256 at 4 bpp = 16 KB)")
  key("pgdn")
  check(sees("ROM $004000-$007FFF"), "PgDn: the next page")
  key(">")
  check(sees("ROM $004020"), "> : one tile on (32 bytes at 4 bpp)")
  key("b")
  check(sees("ROM $004000") and sees("8 bpp"), "b: 8 bpp, the start on a tile of 64 bytes")
  key("j")
  t.type_text("80:8000")
  check(sees("ROM $000000"), "j 80:8000: the CPU address of the first byte")
  key("\n")
  check(status():find("read only", 1, true) ~= nil, "Sprites: Enter does not draw on a ROM")
  key("^s")
  check(status():find("read only", 1, true) ~= nil, "Ctrl+S does not save a ROM")
  check(t.snes["Test Game.sfc"] == rom, "the ROM file is not changed")
  key("esc", "esc")
  frames(2)

  key("^n")
  check(not sees("(read only)"), "Ctrl+N: a project again")
end
