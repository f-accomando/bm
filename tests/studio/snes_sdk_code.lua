-- The Code tab of bm SNES SDK, driven on the PC by snes_sdk_host.lua (make test-studio): typing, the colours' text,
-- the build with the errors in plain words, F1, the snippets, find, go to a label, the files and the mouse.
-- ctx: check, key, frames, sees, status, click, mouse, docs (the files doc_write got), screen, type_text.
return function(ctx)
  local check, key, frames, sees, status, click, docs = ctx.check, ctx.key, ctx.frames, ctx.sees, ctx.status, ctx.click, ctx.docs
  local function quiet() frames(450) end           -- a message in the status line fades

  key("f1")
  check(sees("main.s") and sees("reset:") and sees("sei"), "F1 from another tab opens the Code tab on main.s")
  check(sees("build") and sees("snippet"), "the hint row has the Code keys")

  -- a 16 bit value for an 8 bit A: the error in plain words, under the line and in the list
  key("^home")
  for c in ("    lda #$1234"):gmatch(".") do key(c) end
  key("\n")
  check(sees("lda") and sees("$1234"), "typing shows in the editor")
  key("^b")
  check(sees("1 error"), "Ctrl+B: the list says 1 error")
  check(sees("A is 8 bit here") and sees("rep #$20"), "the error is in plain words and says rep #$20")
  check(status():find("A is 8 bit", 1, true), "the status line has it too: " .. status())
  check(docs["snesdemo.sfc"] == nil, "no ROM is written when the build fails")
  check(sees("main.s:1"), "the list has the file and the line")

  -- fix it: take the line out; the build writes the ROM
  key("up")
  key("^k")
  key("^b")
  check(docs["snesdemo.sfc"] and #docs["snesdemo.sfc"] == 32768, "Ctrl+B writes a 32 KB ROM in /docs, named as the project")
  check(docs["main.s"] and docs["main.s"]:find("reset:", 1, true), "a build saves the source file")
  check(status():find("built snesdemo.sfc", 1, true), "the status line tells what was built: " .. status())
  check(not sees("1 error"), "the list is gone after a good build")
  check(sees("RUNNING"), "a good build runs the ROM: the Run tab shows")
  key("f1")                                      -- back to the Code tab

  -- F1 on an instruction
  key("^f")
  ctx.type_text("sei")
  check(sees("sei"), "find puts the cursor on the word")
  key("f1")
  check(sees("F1 SEI") and sees("Flags changed: I") and sees("Cycles: 2"), "F1 shows the help of sei")
  key("esc")
  check(not sees("Flags changed"), "Esc closes the help")
  quiet()
  check(status():find("sei: Set I", 1, true), "on an instruction the status line has a short help: " .. status())

  -- an unknown word: the error suggests a name
  key("^end")
  key("\n")
  for c in ("    ldaa #1"):gmatch(".") do key(c) end
  key("^b")
  check(sees("is not an instruction") and sees("Do you mean lda"), "a wrong word: the error suggests lda")
  key("f8")
  check(sees("main.s:"), "F8 goes to an error")
  key("esc")
  key("^k")
  key("^home")

  -- go to a label
  key("^g")
  check(sees("go to a label in main.s") and sees("forever") and sees("nmi"), "Ctrl+G lists the labels")
  key("down", "down")
  key("\n")
  quiet()
  check(status():find("line", 1, true) and not status():find("line 1 of", 1, true), "the label moved the cursor: " .. status())

  -- snippets
  key("^j")
  check(sees("insert a snippet") and sees("dma") and sees("joypad"), "Ctrl+J lists the snippets")
  key("down", "down", "\n")
  check(status():find("Copies a block", 1, true), "a snippet says what it does: " .. status())

  -- the files
  key("^e")
  check(sees("source files") and sees("main.s") and sees("New file"), "Ctrl+E lists the files")
  key("esc")
  key("^s")
  check(docs["main.s"] and docs["main.s"]:find("tiles_end", 1, true), "Ctrl+S saves the source file with the snippet")

  -- the mouse: a click puts the cursor, the wheel scrolls
  quiet()
  key("^home")
  click(56 + 8 * 4, 32 + 8)
  quiet()
  check(status():find("line 1 of", 1, true) and status():find("column 5", 1, true), "a click puts the cursor: " .. status())
  click(56 + 8 * 2, 32 + 16 * 2 + 8)
  quiet()
  check(status():find("line 3 of", 1, true) or status():find("sei:", 1, true), "a click on a line moves the cursor: " .. status())
  click(100, 16)            -- the file strip
  check(sees("main.s"), "the file strip is there")
end
