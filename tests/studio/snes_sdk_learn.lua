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

  -- the window: Ctrl+L opens it at the right of the Code tab
  local function to_lesson() settle(); if not status():find("^Lesson") then key("^l") end; settle() end
  key("^l")
  settle()
  check(status():find("Lesson 1 of 14: The CPU, registers and flags", 1, true), "Ctrl+L opens the lesson window on lesson 1: " .. status())
  check(sees("What the CPU") or sees("The CPU, registers"), "the lesson is on the screen")
  check(sees("1/14") and sees("main.s"), "the window and your file are on the screen together")
  check(same(before, docs), "opening a lesson changes no file")
  key("down", "down", "down")
  key("right")
  settle()
  check(status():find("Lesson 2 of 14", 1, true), "Right: the next lesson: " .. status())
  key("left")
  settle()
  check(status():find("Lesson 1 of 14", 1, true), "Left: the previous lesson")
  key("end")
  check(sees("Read more") or sees("snes.nesdev.org"), "End shows the links at the end")
  key("home")
  key("g")
  check(sees("Lessons") and sees("Jump with gravity"), "g lists the lessons")
  key("esc")

  -- Esc and F1 give the keys back to the editor (the cursor stays); Ctrl+L takes them again
  key("esc")
  settle()
  check(status() == code_status, "Esc: the editor has the keys at the same cursor: " .. status())
  check(sees("1/14"), "the window stays open")
  key("^l")
  key("f1")
  settle()
  check(status() == code_status, "F1: the editor has the keys at the same cursor: " .. status())

  -- Insert at the cursor
  to_lesson()
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
  to_lesson()
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
  to_lesson()
  local loads = fake.loads
  key("r")
  check(fake.loads == loads + 1 and fake.rom and #fake.rom == 32768, "r loads the example in the machine")
  check(sees("RUNNING") and sees("lesson example"), "the Run tab says it is a lesson example")
  check(same(files_before, docs), "Run alone writes no file (no ROM either)")
  key("f1")
  key("esc")
  settle()
  check(status():find("main.s", 1, true), "back in the Code tab")
  key("f5")                                       -- your own project builds and runs again
  check(not sees("lesson example") and fake.loads == loads + 2, "F5 runs your project, not the example")

  -- the keys go to the editor beside the window, and Ctrl+L closes the window
  to_lesson()
  key("\t")
  settle()
  check(status():find("main.s", 1, true) and sees("1/14"), "Tab: the editor has the keys, the window stays")
  key("z")
  key("^z")
  key("^l")
  key("^l")
  settle()
  check(not sees("1/14") and status():find("main.s", 1, true), "Ctrl+L in the window closes it")

  -- search all the lessons
  to_lesson()
  key("^f")
  check(sees("search all the lessons"), "Ctrl+F asks for the words")
  type_text("forced blank")
  check(sees("lines with: forced blank") or sees("line with: forced blank"), "Ctrl+F lists the lines of all lessons: " .. (sees("with:") and "ok" or "no list"))
  key("\n")
  settle()
  check(status():find("Lesson", 1, true), "Enter opens the lesson of the line: " .. status())

  -- a mark, a done mark, the position, then a save and an open
  to_lesson()
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
  to_lesson()
  check(status():find("[done]", 1, true), "after save and open the done mark is back: " .. status())
  key("l")
  check(sees("marked blocks (Notes)"), "after save and open the mark is back")
  key("esc")
  key("esc")
  key("f1")
end
