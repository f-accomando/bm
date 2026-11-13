-- The Learn tab of bm SNES SDK, driven on the PC by snes_sdk_host.lua (make test-studio): the lessons are read, a lesson never changes
-- the files of the user, the three actions of a code block (Insert, Open as new file, Run alone), the split view, Esc back to the
-- Code tab at the same cursor, the search over all lessons, the Notes list, and the read position, the done mark and the notes that
-- stay in the project after a save and an open. The machine is the stand-in of the host file. The helpers have their own test
-- (tests/snes/learn_test.lua, make test-snes-learn).
return function(ctx)
  local check, key, frames, sees, status, docs, fake, type_text = ctx.check, ctx.key, ctx.frames, ctx.sees, ctx.status, ctx.docs, ctx.fake, ctx.type_text

  local function settle() frames(450) end          -- the messages of the status line go away
  local function snapshot() local t = {}; for k, v in pairs(docs) do t[k] = v end; return t end
  local function same(a, b)
    for k, v in pairs(a) do if b[k] ~= v then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
  end

  -- the Code tab with a cursor somewhere
  key("f1")
  key("^home")
  for _ = 1, 4 do key("down") end
  settle()
  local code_status = status()
  check(code_status:find("line 5 of", 1, true), "the Code tab: cursor on line 5: " .. code_status)
  key("^s")
  local before = snapshot()
  check(before["main.s"] ~= nil, "main.s is saved in /docs")

  -- the tab
  key("^l")
  settle()
  check(status():find("Lesson 1 of 13: The CPU, registers and flags", 1, true), "Ctrl+L opens the Learn tab on lesson 1: " .. status())
  check(sees("What the CPU") or sees("The CPU, registers"), "the lesson is on the screen")
  check(sees("65C816 modes") and sees("A moving sprite"), "the list shows the lessons")
  check(same(before, docs), "opening a lesson changes no file")
  key("down", "down", "down")
  key("right")
  settle()
  check(status():find("Lesson 2 of 13", 1, true), "Right: the next lesson: " .. status())
  key("left")
  settle()
  check(status():find("Lesson 1 of 13", 1, true), "Left: the previous lesson")
  key("end")
  check(sees("Read more") or sees("snes.nesdev.org"), "End shows the links at the end")
  key("home")

  -- Esc and F1 go back to the Code tab at the same cursor
  key("esc")
  settle()
  check(status() == code_status, "Esc: back in the Code tab at the same cursor: " .. status())
  key("^l")
  key("f1")
  settle()
  check(status() == code_status, "F1: back in the Code tab at the same cursor: " .. status())

  -- Insert at the cursor
  key("^l")
  key("n")
  key("i")
  check(status():find("inserted in main.s", 1, true), "i inserts the block: " .. status())
  check(same(before, docs), "Insert changes no file on the card until you save")
  key("esc")
  settle()
  local longer = tonumber(status():match("of (%d+)")) > tonumber(code_status:match("of (%d+)"))
  check(longer, "the file is longer after Insert: " .. status())
  key("^z")
  settle()
  check(status():match("of (%d+)") == code_status:match("of (%d+)"), "Ctrl+Z takes the Insert back: " .. status())
  key("^s")
  check(same(before, docs), "after Insert and undo the files are byte for byte the same")

  -- Open as a new file: a new name each time, nothing replaced
  key("^l")
  key("o")
  check(docs["less01.s"] ~= nil and docs["less01.s"]:find("adc #$03", 1, true), "o writes less01.s")
  key("o")
  check(docs["less01_2.s"] ~= nil, "o again: less01_2.s (no file is replaced)")
  for k, v in pairs(before) do check(docs[k] == v, "Open keeps " .. k) end
  check(status():find("less01_2.s", 1, true), "the status names the file: " .. status())
  key("esc")
  settle()
  check(status():find("main.s", 1, true), "you stay in your own file: " .. status())

  -- Run alone: the machine runs the example, the files and the ROM of the project stay
  local files_before = snapshot()
  key("^l")
  local loads = fake.loads
  key("r")
  check(fake.loads == loads + 1 and fake.rom and #fake.rom == 32768, "r loads the example in the machine")
  check(sees("RUNNING") and sees("lesson example"), "the Run tab says it is a lesson example")
  check(same(files_before, docs), "Run alone writes no file (no ROM either)")
  key("f1")
  settle()
  check(status():find("main.s", 1, true), "back in the Code tab")
  key("f5")                                       -- your own project builds and runs again
  check(not sees("lesson example") and fake.loads == loads + 2, "F5 runs your project, not the example")

  -- the split view: lesson and Code buffer
  key("^l")
  key("\t")
  check(sees("main.s") and sees("65C816") or sees("main.s"), "Tab: the Code buffer is beside the lesson")
  key("\t")                                       -- the keys type in the Code buffer
  key("z")
  key("\t")                                       -- back to the lesson list
  check(sees("65C816 modes"), "Tab again: the list is back")
  key("esc")
  key("^z")

  -- search all the lessons
  key("^l")
  key("^f")
  check(sees("search all the lessons"), "Ctrl+F asks for the words")
  type_text("forced blank")
  check(sees("lines with: forced blank") or sees("line with: forced blank"), "Ctrl+F lists the lines of all lessons: " .. (sees("with:") and "ok" or "no list"))
  key("\n")
  settle()
  check(status():find("Lesson", 1, true), "Enter opens the lesson of the line: " .. status())

  -- a mark, a done mark, the position, then a save and an open
  key("^l")
  key("m")
  key("d")
  settle()
  check(status():find("[done]", 1, true), "d marks the lesson done: " .. status())
  key("l")
  check(sees("marked blocks (Notes)"), "l lists the marked blocks")
  key("esc")
  key("pgdn")
  key("^s")                                       -- the project is named: the learn data goes to the .bme
  key("^n")
  check(not status():find("[done]", 1, true), "a new project has no marks")
  key("^o")
  key("\n")
  key("^l")
  settle()
  check(status():find("[done]", 1, true), "after save and open the done mark is back: " .. status())
  key("l")
  check(sees("marked blocks (Notes)"), "after save and open the mark is back")
  key("esc")
  key("esc")
  key("f1")
end
