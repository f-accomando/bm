-- The Run tab of bm SNES SDK, driven on the PC by snes_sdk_host.lua (make test-studio): F5 and Ctrl+B build and run, F9 sets a
-- breakpoint (in the Code tab at the cursor line, in the Run tab at an address), the machine stops there, F10 steps, F5 continues,
-- the registers with the flags named, the memory view and the poke. The machine is a stand-in (FAKE in the host file): the C core
-- and its debug API are tested by tests/snes/debug_test.c (make test-snes-debug). No Pi or RGB30 run is needed for this.
return function(ctx)
  local check, key, frames, sees, status, docs, fake, A = ctx.check, ctx.key, ctx.frames, ctx.sees, ctx.status, ctx.docs, ctx.fake, ctx.asm
  local function quiet() frames(450) end

  -- the Run tab before a build
  key("f1")
  local loads = fake.loads
  key("f5")
  -- F5 from the Code tab builds the project and runs it
  check(fake.loads == loads + 1 and fake.rom and #fake.rom == 32768, "F5 builds the project and loads the 32 KB ROM in the machine")
  check(sees("RUNNING"), "the Run tab says RUNNING")
  check(sees("A 1234") and sees("PC 8000") and sees("native"), "the registers are on the panel")
  check(sees("A is 8 bit") and sees("X and Y are 8 bit"), "the flags are named in words")
  frames(10)
  check(fake.runs > 5 and fake.draws > 5, "the machine runs a frame in every update and the picture is drawn")

  -- the addresses of the instructions of the main file, as the assembler gives them
  local res = A.assemble(docs["main.s"], { name = "main.s" })
  check(res.ok and #res.lines > 3, "the source assembles: the line map has instructions")
  fake.pcs = {}
  for _, l in ipairs(res.lines) do fake.pcs[#fake.pcs + 1] = l.pc end
  local third = res.lines[3]

  -- F9 in the Code tab at a line
  key("f1")
  key("^home")
  for _ = 1, third.line - 1 do key("down") end
  key("f9")
  check(status():find("breakpoint at 00:" .. string.format("%04X", third.pc & 0xFFFF), 1, true), "F9 sets a breakpoint at the cursor line: " .. status())
  key("f9")
  check(status():find("breakpoint cleared", 1, true), "F9 on the same line clears it: " .. status())
  key("f9")
  check(fake.brk[third.pc], "the breakpoint is armed in the machine")

  -- the run stops there
  key("f5")                                     -- from the Code tab F5 builds and runs again
  frames(10)
  check(sees("STOPPED"), "the machine stops at the breakpoint")
  check(sees(string.format("PC %04X", third.pc & 0xFFFF)), "the PC is the breakpoint address")
  check(status():find("stopped at", 1, true), "the status line says where: " .. status())
  local stop_idx = fake.idx
  frames(5)
  check(fake.idx == stop_idx, "the machine does not run while it is stopped")

  -- F10 steps one instruction
  key("f10")
  check(fake.idx == stop_idx % #fake.pcs + 1, "F10 steps one instruction")
  key("f5")
  check(sees("RUNNING") or sees("STOPPED"), "F5 goes on (the Run tab shows)")
  key("f5")

  -- the Run tab: a breakpoint at an address, and the memory
  key("f5")
  quiet()
  key("f10")                                    -- the first F10 pauses
  check(sees("STOPPED"), "F10 pauses a running game")
  key("g")
  ctx.type_text("7E0300")
  check(sees("7E0300"), "g goes to an address of the memory: the view starts there")
  key("w")
  ctx.type_text("7E0300=12 34")
  check(fake.ram[0x7E0300] == 0x12 and fake.ram[0x7E0301] == 0x34, "w writes the bytes with snes_poke")
  check(sees("12 34"), "the memory view shows the new bytes")
  key("\t")
  check(sees("VRAM") and sees("CGRAM") and sees("OAM"), "the memory spaces are named")
  key("b")
  ctx.type_text("zz")
  check(status():find("hex", 1, true), "a bad address is refused in plain words: " .. status())
  key("c")
  check(next(fake.brk) == nil, "c clears the breakpoints")

  -- the keys are on the hint row
  check(sees("step") and sees("break") and sees("continue"), "the hint row has F5, F9 and F10")
end
