-- snes_sdk_rom.lua: checks of bm SNES SDK, called by snes_sdk_host.lua (make test-studio).
-- 1. The pencil held: Space (or the pad's A) held while the arrows move paints a line, one undo step.
-- 2. A ROM (.sfc in /snes) opened read only at the right of the Code tab (the project stays at the left): no typing, i puts a
--    line in your file, F9 a breakpoint, r runs it; Ctrl+T in Sprites shows its tiles beside your sprite, c copies a block.
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

  ----------------------------------------------------------------- 2. a ROM, read only, at the right
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
  rom = rom .. string.rep("\xAA", 32768)                  -- 64 KB: 4 pages of tiles at 4 bpp
  t.snes["Test Game.sfc"] = rom
  key("^n", "^n")                                       -- the drawing changed the project: the second Ctrl+N confirms
  key("f1")
  key("^o")
  check(sees("Test Game.sfc  (/snes, 64 KB, read only)"), "Ctrl+O lists the ROM of /snes")
  key("up", "\n")                                       -- the ROMs are after the projects: the last row
  check(sees("ROM: Test Game.sfc") and sees("Lessons"), "the right window: the views Lessons and ROM")
  check(sees("reset") and sees("$1234"), "the ROM view: the disassembly from the reset vector")
  check(sees("main.s"), "your project stays at the left (main.s)")
  key("x")
  check(status():find("read only", 1, true) ~= nil, "the ROM view takes no typing: " .. status())
  frames(200)                                           -- the message goes: the status shows the line
  check(status():find("Test Game.sfc (read only)", 1, true) and status():find("00:8000", 1, true), "the status: the ROM and the address: " .. status())
  key("f9")
  check(status():find("breakpoint at 00:8000", 1, true) ~= nil, "F9: a breakpoint on the line: " .. status())
  key("i")
  check(status():find("into main.s", 1, true) ~= nil, "i: the line goes into your file: " .. status())
  key("^e")
  check(sees("reset") and sees("Go to an address"), "Ctrl+E: the vectors and an address")
  key("esc")
  key("^t")
  frames(200)
  check(status():find("Lesson", 1, true) ~= nil, "Ctrl+T: the lessons: " .. status())
  key("^t")
  frames(200)
  check(status():find("Test Game.sfc", 1, true) ~= nil, "Ctrl+T again: the ROM: " .. status())
  local loads = t.fake.loads
  key("r")
  check(t.fake.loads == loads + 1 and t.fake.rom == rom, "r runs the ROM as it is")

  key("f2", "^t")
  check(sees("ROM $000000-$001FFF"), "Sprites, Ctrl+T: the ROM's tiles at the right (a page of 128x128 at 4 bpp = 8 KB)")
  check(sees("Panel") and sees("ROM: Test Game.sfc") and sees("OAM"), "the row of the views: Panel, ROM, OAM")
  check(sees("your sprite ("), "your sprite stays at the left")
  key("pgdn")
  check(sees("ROM $002000-$003FFF"), "PgDn: the next page")
  t.mkey({ "alt" }, "up")
  check(sees("ROM $000000-$001FFF"), "Alt+Up: the page before (as in the Code tab)")
  t.mkey({ "alt" }, "down")
  check(sees("ROM $002000-$003FFF"), "Alt+Down: the next page")
  t.mkey({ "ctrl", "alt" }, "down")
  check(not sees("ROM $002000-$003FFF") and sees("ROM $"), "Ctrl+Alt+Down: the last page")
  t.mkey({ "ctrl", "alt" }, "up")
  check(sees("ROM $000000-$001FFF"), "Ctrl+Alt+Up: the start of the ROM")
  key("pgdn")
  key(">")
  check(sees("ROM $002020"), "> : one tile on (32 bytes at 4 bpp)")
  key("B")
  check(sees("ROM $002000") and sees("8 bpp"), "shift b: 8 bpp, the start on a tile of 64 bytes")
  key("j")
  t.type_text("80:8000")
  check(sees("ROM $000000"), "j 80:8000: the CPU address of the first byte")
  key("c")
  check(status():find("copied %d+x%d+ of the ROM") ~= nil, "c: the box of the ROM is copied: " .. status())
  key("^t")                                         -- the machine runs the ROM (r above): the OAM view comes next
  check(sees("OAM of the Run tab") and sees("sprite 0 of 128") and sees("obsel $63"),
        "Ctrl+T again: the OAM of the machine, sprite 0 chosen")
  key(">")
  check(sees("sprite 1 of 128"), "> : the next sprite that shows")
  key("c")
  check(status():find("copied sprite 1", 1, true) ~= nil, "c: the pixels of the sprite are copied: " .. status())
  key("P")
  check(status():find("colours of OAM palette", 1, true) ~= nil, "shift p: its colours into your sub-palette: " .. status())
  key("n")
  check(sees("sprite 2 of 128"), "n: the next of all 128")
  key("^t")
  check(sees("sub-palette"), "Ctrl+T: your panel again")
  check(t.snes["Test Game.sfc"] == rom, "the ROM file is not changed")

  key("f3")                                         -- the Map tab: the BG layers of the machine that runs
  check(sees("BG1 4 bpp") and sees("BG3 2 bpp") and sees("BG4 -"), "Map: the row of the layers of mode 1")
  check(sees("map $4000") and sees("64x32 cells") and sees("tiles $2000"), "BG1: sc $21 is a map of 64x32 at $4000")
  frames(20)
  check(not sees("reading..."), "the whole map is drawn after some frames")
  key("right", "right")
  check(sees("cell (2,0)  $0000") and sees("tile $000 pal 0 prio 0"), "the arrows choose a cell, its word (the stand-in video RAM is zeros)")
  t.mkey({ "shift" }, "down")
  check(sees("box 1x2 at (2,0)"), "Shift+Down: a box of 1x2")
  key("c")
  check(status():find("copied 1x2 words as .dw", 1, true) ~= nil, "c: the words as .dw lines: " .. status())
  key("t")
  check(status():find("copied the tile of cell (2,1)", 1, true) ~= nil, "t: the tile of the cell: " .. status())
  key("3")
  check(sees("BG3  2 bpp  not on screen"), "3: BG3, not on the main screen (tm $13)")
  key("4")
  check(status():find("BG4 is not there", 1, true) ~= nil, "4: no BG4 in mode 1: " .. status())
  key("1")

  key("f1", "^L")
  key("^n", "^n")
  check(not sees("ROM: Test Game.sfc"), "Ctrl+Shift+L closes the window")
end
